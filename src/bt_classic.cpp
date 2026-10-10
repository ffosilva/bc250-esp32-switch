#include "bt_classic.h"

#include <esp_bt.h>
#include <esp_mac.h>
#include <string.h>

#include "board.h"

// Tell Arduino core's initArduino() that Bluetooth is in use, otherwise it calls
// esp_bt_controller_mem_release(ESP_BT_MODE_BTDM) at boot, which permanently
// disables the Bluetooth controller.
extern "C" bool btInUse() {
  return true;
}

// ---- HCI opcodes (OGF<<10 | OCF) -------------------------------------------
static const uint16_t OP_RESET               = 0x0C03;
static const uint16_t OP_WRITE_LOCAL_NAME    = 0x0C13;
static const uint16_t OP_WRITE_PAGE_TIMEOUT  = 0x0C18;
static const uint16_t OP_READ_SCAN_ENABLE    = 0x0C19;
static const uint16_t OP_WRITE_SCAN_ENABLE   = 0x0C1A;
static const uint16_t OP_READ_PAGE_SCAN_ACT  = 0x0C1B;
static const uint16_t OP_WRITE_PAGE_SCAN_ACT = 0x0C1C;
static const uint16_t OP_WRITE_COD           = 0x0C24;
static const uint16_t OP_WRITE_INQUIRY_MODE  = 0x0C45;
static const uint16_t OP_READ_PAGE_SCAN_TYPE = 0x0C46;
static const uint16_t OP_WRITE_PAGE_SCAN_TYPE= 0x0C47;
static const uint16_t OP_INQUIRY             = 0x0401;
static const uint16_t OP_REJECT_CONN_REQ     = 0x040A;
static const uint16_t OP_REMOTE_NAME_REQ     = 0x0419;
static const uint16_t OP_READ_BD_ADDR        = 0x1009;

static const char *hciOpName(uint16_t op) {
  switch (op) {
    case OP_RESET:               return "Reset";
    case OP_WRITE_LOCAL_NAME:    return "Write_Local_Name";
    case OP_WRITE_PAGE_TIMEOUT:  return "Write_Page_Timeout";
    case OP_READ_SCAN_ENABLE:    return "Read_Scan_Enable";
    case OP_WRITE_SCAN_ENABLE:   return "Write_Scan_Enable";
    case OP_READ_PAGE_SCAN_ACT:  return "Read_Page_Scan_Act";
    case OP_WRITE_PAGE_SCAN_ACT: return "Write_Page_Scan_Act";
    case OP_WRITE_COD:           return "Write_COD";
    case OP_WRITE_INQUIRY_MODE:  return "Write_Inquiry_Mode";
    case OP_READ_PAGE_SCAN_TYPE: return "Read_Page_Scan_Type";
    case OP_WRITE_PAGE_SCAN_TYPE:return "Write_Page_Scan_Type";
    case OP_READ_BD_ADDR:        return "Read_BD_ADDR";
    case OP_INQUIRY:             return "Inquiry";
    case OP_REJECT_CONN_REQ:     return "Reject_Conn_Req";
    case OP_REMOTE_NAME_REQ:     return "Remote_Name_Req";
    default:                     return "Unknown_Op";
  }
}

static const char *hciStatusName(uint8_t status) {
  switch (status) {
    case 0x00: return "SUCCESS";
    case 0x01: return "Unknown HCI Command";
    case 0x02: return "Unknown Connection Identifier";
    case 0x03: return "Hardware Failure";
    case 0x0C: return "Command Disallowed";
    case 0x11: return "Unsupported Feature or Parameter";
    case 0x12: return "Invalid HCI Command Parameters";
    default:   return "Error";
  }
}

// ---- HCI event codes --------------------------------------------------------
static const uint8_t EVT_INQUIRY_COMPLETE   = 0x01;
static const uint8_t EVT_CONN_REQUEST       = 0x04;
static const uint8_t EVT_REMOTE_NAME_DONE   = 0x07;
static const uint8_t EVT_CMD_COMPLETE       = 0x0E;
static const uint8_t EVT_CMD_STATUS         = 0x0F;
static const uint8_t EVT_INQUIRY_RSSI       = 0x22;
static const uint8_t EVT_EXT_INQUIRY        = 0x2F;

static const uint8_t REJECT_UNACCEPTABLE_ADDR = 0x0F;

// ---- Command queue ----------------------------------------------------------
// Producers: loop task (init, scan enable, inquiry, name request) and the BT
// controller task (reject). Only btLoop() ever sends, one command at a time,
// waiting for the controller's Command Complete/Status in between.
struct Cmd {
  uint8_t  buf[4 + 248];
  uint16_t len;
};
static const int QLEN = 24;
static Cmd           queue[QLEN];
static int           qHead = 0, qCount = 0;
static portMUX_TYPE  qMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool          cmdPending   = false;
static volatile unsigned long cmdSentAt    = 0;
static const unsigned long    CMD_TIMEOUT_MS = 1000;

static bool   btUp = false;
static String currentHostMac;

static bool enqueue(uint16_t op, const uint8_t *params, uint8_t plen) {
  bool ok = false;
  portENTER_CRITICAL(&qMux);
  if (qCount < QLEN) {
    Cmd &c = queue[(qHead + qCount) % QLEN];
    c.buf[0] = 0x01;  // HCI command packet
    c.buf[1] = op & 0xFF;
    c.buf[2] = op >> 8;
    c.buf[3] = plen;
    if (plen) memcpy(&c.buf[4], params, plen);
    c.len = 4 + plen;
    qCount++;
    ok = true;
  }
  portEXIT_CRITICAL(&qMux);
  if (!ok) Serial.printf("[BT  ] command queue full, dropped op 0x%04X\n", op);
  return ok;
}

static void pumpQueue() {
  if (!btUp) return;
  unsigned long now = millis();
  if (cmdPending) {
    if (now - cmdSentAt < CMD_TIMEOUT_MS) return;
    Serial.println("[BT  ] HCI cmd timeout; controller never answered");
    cmdPending = false;  // controller never answered; carry on
  }
  if (!esp_vhci_host_check_send_available()) return;

  Cmd c;
  bool have = false;
  portENTER_CRITICAL(&qMux);
  if (qCount > 0) {
    c = queue[qHead];
    qHead = (qHead + 1) % QLEN;
    qCount--;
    have = true;
  }
  portEXIT_CRITICAL(&qMux);
  if (!have) return;

  cmdPending = true;
  cmdSentAt = now;
  esp_vhci_host_send_packet(c.buf, c.len);
}

// ---- State shared with the controller task ---------------------------------
static portMUX_TYPE stMux = portMUX_INITIALIZER_UNLOCKED;

static const int MAX_WAKE_ADDRS = 8;
static uint8_t   wakeAddrs[MAX_WAKE_ADDRS][6];  // HCI (little-endian) order
static int       wakeAddrCount = 0;
static volatile bool          wakeSeenEver = false;
static volatile unsigned long wakeLastSeen = 0;

static BtListenerResult listenerResult = {false, "", "", 0};
static volatile bool    listenerActive = false;


static bool          connectable = false;      // requested page-scan state

static const int MAX_DEVS = 48;
static BtDev     devs[MAX_DEVS];

static bool          discovery      = false;
static bool          inquiryActive  = false;
static unsigned long nextInquiryAt  = 0;
static bool          namePending    = false;
static unsigned long nameSentAt     = 0;

// ---- Address helpers --------------------------------------------------------
bool btValidMac(const String &mac) {
  if (mac.length() != 17) return false;
  for (int i = 0; i < 17; i++) {
    char c = mac[i];
    if (i % 3 == 2) {
      if (c != ':') return false;
    } else if (!isxdigit((unsigned char)c)) {
      return false;
    }
  }
  return true;
}

// "aa:bb:cc:dd:ee:ff" -> out[0..5] = aa..ff (display order).
static void parseMac(const String &mac, uint8_t *out) {
  for (int i = 0; i < 6; i++) {
    out[i] = (uint8_t)strtoul(mac.substring(i * 3, i * 3 + 2).c_str(), nullptr, 16);
  }
}

// HCI carries bd_addr little-endian; render it as aa:bb:cc:dd:ee:ff.
static void leToStr(const uint8_t *le, char *out /*18*/) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", le[5], le[4], le[3], le[2],
           le[1], le[0]);
}

// ---- Device table (portal) --------------------------------------------------
static void recordDevice(const uint8_t *le, const char *name, int rssi,
                         uint32_t cod, bool paged) {
  char addr[18];
  leToStr(le, addr);
  portENTER_CRITICAL(&stMux);
  int idx = -1, freeSlot = -1;
  for (int i = 0; i < MAX_DEVS; i++) {
    if (devs[i].used) {
      if (strcmp(devs[i].addr, addr) == 0) { idx = i; break; }
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }
  if (idx < 0 && freeSlot >= 0) {
    idx = freeSlot;
    memset(&devs[idx], 0, sizeof(BtDev));
    devs[idx].used = true;
    strncpy(devs[idx].addr, addr, sizeof(devs[idx].addr) - 1);
  }
  if (idx >= 0) {
    if (rssi != 0) devs[idx].rssi = rssi;
    if (cod != 0) devs[idx].cod = cod;
    if (paged) devs[idx].paged = true;
    if (name && name[0]) {
      strncpy(devs[idx].name, name, sizeof(devs[idx].name) - 1);
      devs[idx].name[sizeof(devs[idx].name) - 1] = 0;
    }
  }
  portEXIT_CRITICAL(&stMux);
}

// Pull a (shortened) complete local name out of an EIR blob.
static void eirName(const uint8_t *eir, int len, char *out, size_t outLen) {
  out[0] = 0;
  int i = 0;
  while (i < len) {
    uint8_t l = eir[i];
    if (l == 0 || i + 1 + l > len) break;
    uint8_t type = eir[i + 1];
    if (type == 0x08 || type == 0x09) {
      size_t n = l - 1;
      if (n >= outLen) n = outLen - 1;
      memcpy(out, &eir[i + 2], n);
      out[n] = 0;
      if (type == 0x09) return;  // complete name wins
    }
    i += 1 + l;
  }
}

// ---- Event handling (controller task) ---------------------------------------
static void handleInquiryEntry(const uint8_t *e, const uint8_t *eir, int eirLen) {
  // e: bd_addr[6] psrm[1] rsvd[1] cod[3] clk[2] rssi[1]
  uint32_t cod = e[8] | (e[9] << 8) | (e[10] << 16);
  int rssi = (int8_t)e[13];
  char name[33] = {0};
  if (eir) eirName(eir, eirLen, name, sizeof(name));
  recordDevice(e, name, rssi, cod, false);
}

static int onHostRecv(uint8_t *data, uint16_t len) {
  if (len < 3 || data[0] != 0x04) return 0;  // HCI event packets only
  uint8_t code = data[1];
  uint8_t plen = data[2];
  const uint8_t *p = &data[3];
  if (len < 3 + plen) return 0;

  switch (code) {
    case EVT_CMD_COMPLETE: {
      cmdPending = false;
      if (plen >= 3) {
        uint16_t op = p[1] | (p[2] << 8);
        uint8_t status = (plen >= 4) ? p[3] : 0;
        const char *name = hciOpName(op);
        if (status != 0) {
          Serial.printf("[BT  ] HCI CMD 0x%04X (%s) FAILED: 0x%02X (%s)\n",
                        op, name, status, hciStatusName(status));
          if (op == OP_WRITE_PAGE_SCAN_TYPE) {
            Serial.println("[BT  ] Interlaced page scan rejected; falling back to Standard scan (0x00)");
            uint8_t stdType = 0x00;
            enqueue(OP_WRITE_PAGE_SCAN_TYPE, &stdType, 1);
            enqueue(OP_READ_PAGE_SCAN_TYPE, nullptr, 0);
          }
        } else {
          if (op == OP_READ_BD_ADDR && plen >= 10) {
            char addr[18];
            leToStr(&p[4], addr);
            bool match = currentHostMac.equalsIgnoreCase(addr);
            Serial.printf("[BT  ] Active controller BD_ADDR: %s (impersonating %s: %s)\n",
                          addr, currentHostMac.c_str(), match ? "MATCH" : "MISMATCH!");
          } else if (op == OP_READ_PAGE_SCAN_ACT && plen >= 8) {
            uint16_t interval = p[4] | (p[5] << 8);
            uint16_t window = p[6] | (p[7] << 8);
            Serial.printf("[BT  ] Page scan activity: interval=%u slots (%.1f ms), window=%u slots (%.1f ms)\n",
                          interval, interval * 0.625f, window, window * 0.625f);
          } else if (op == OP_READ_PAGE_SCAN_TYPE && plen >= 5) {
            uint8_t t = p[4];
            Serial.printf("[BT  ] Page scan type: %s (0x%02X)\n",
                          t == 0x01 ? "Interlaced" : (t == 0x00 ? "Standard" : "Unknown"), t);
          } else if (op == OP_READ_SCAN_ENABLE && plen >= 5) {
            uint8_t se = p[4];
            Serial.printf("[BT  ] Active scan enable: 0x%02X (page_scan=%s, inq_scan=%s)\n",
                          se, (se & 0x02) ? "ON" : "OFF", (se & 0x01) ? "ON" : "OFF");
          }
        }
      }
      break;
    }

    case EVT_CMD_STATUS: {
      cmdPending = false;
      if (plen >= 4) {
        uint8_t status = p[0];
        uint16_t op = p[2] | (p[3] << 8);
        if (status != 0) {
          Serial.printf("[BT  ] HCI CMD 0x%04X (%s) status error: 0x%02X (%s)\n",
                        op, hciOpName(op), status, hciStatusName(status));
        }
      }
      break;
    }

    case EVT_CONN_REQUEST:
      if (plen >= 10) {
        bool isWake = false;
        char fromAddr[18];
        leToStr(p, fromAddr);
        uint32_t cod = p[6] | (p[7] << 8) | (p[8] << 16);

        portENTER_CRITICAL(&stMux);
        for (int i = 0; i < wakeAddrCount; i++) {
          if (memcmp(p, wakeAddrs[i], 6) == 0) {
            wakeLastSeen = millis();
            wakeSeenEver = true;
            isWake = true;
            break;
          }
        }

        // Record for interactive listener mode
        listenerResult.detected = true;
        strncpy(listenerResult.addr, fromAddr, sizeof(listenerResult.addr) - 1);
        listenerResult.addr[sizeof(listenerResult.addr) - 1] = 0;
        listenerResult.cod = cod;
        listenerResult.name[0] = 0;
        for (int i = 0; i < MAX_DEVS; i++) {
          if (devs[i].used && strcmp(devs[i].addr, fromAddr) == 0 && devs[i].name[0]) {
            strncpy(listenerResult.name, devs[i].name, sizeof(listenerResult.name) - 1);
            listenerResult.name[sizeof(listenerResult.name) - 1] = 0;
            break;
          }
        }
        portEXIT_CRITICAL(&stMux);

        recordDevice(p, "", 0, cod, true);  // learn mode: paired controllers

        // If listener doesn't have a name yet, immediately request remote name
        if (listenerResult.name[0] == 0) {
          uint8_t req[10] = {0};
          memcpy(req, p, 6);
          req[6] = 0x01;  // R1
          enqueue(OP_REMOTE_NAME_REQ, req, sizeof(req));
        }

        // Reject before any link/authentication exists -> pairing untouched.
        uint8_t params[7];
        memcpy(params, p, 6);
        params[6] = REJECT_UNACCEPTABLE_ADDR;
        enqueue(OP_REJECT_CONN_REQ, params, sizeof(params));
        Serial.printf("[BT  ] Connection_Request from %s (wake match: %s, rejecting)\n",
                      fromAddr, isWake ? "YES" : "NO");
      }
      break;

    case EVT_INQUIRY_COMPLETE:
      inquiryActive = false;
      nextInquiryAt = millis() + BT_IDLE_MS;
      break;

    case EVT_INQUIRY_RSSI:
      if (plen >= 1) {
        int n = p[0];
        for (int i = 0; i < n && 1 + (i + 1) * 14 <= plen; i++) {
          handleInquiryEntry(p + 1 + i * 14, nullptr, 0);
        }
      }
      break;

    case EVT_EXT_INQUIRY:
      if (plen >= 15) handleInquiryEntry(p + 1, p + 15, plen - 15);
      break;

    case EVT_REMOTE_NAME_DONE:
      namePending = false;
      if (plen >= 7 && p[0] == 0) {
        char name[33] = {0};
        size_t n = plen - 7;
        if (n > sizeof(name) - 1) n = sizeof(name) - 1;
        memcpy(name, p + 7, n);  // NUL-padded by the controller
        recordDevice(p + 1, name, 0, 0, false);
        char addr[18];
        leToStr(p + 1, addr);
        portENTER_CRITICAL(&stMux);
        if (listenerResult.detected && strcmp(listenerResult.addr, addr) == 0) {
          strncpy(listenerResult.name, name, sizeof(listenerResult.name) - 1);
          listenerResult.name[sizeof(listenerResult.name) - 1] = 0;
        }
        portEXIT_CRITICAL(&stMux);
      }
      break;


    default:
      Serial.printf("[BT  ] Unhandled HCI event: 0x%02X (plen=%u)\n", code, plen);
      break;
  }
  return 0;
}

static void onSendAvailable() {}

static const esp_vhci_host_callback_t vhciCb = {onSendAvailable, onHostRecv};

// ---- Public API -------------------------------------------------------------
static void queueInit() {
  uint8_t name[248] = {0};
  strncpy((char *)name, BT_LOCAL_NAME, sizeof(name) - 1);
  uint8_t cod[3] = {(uint8_t)(BT_COD & 0xFF), (uint8_t)((BT_COD >> 8) & 0xFF),
                    (uint8_t)((BT_COD >> 16) & 0xFF)};
  // Fast interlaced page scan (spec-compliant, proven in BlueRetro):
  // interval = 50 ms (0x0050 = 80 slots), window = 11.25 ms (0x0012 = 18 slots).
  // Note: For interlaced scan, Bluetooth Core Spec mandates window < interval.
  uint8_t psa[4] = {0x50, 0x00, 0x12, 0x00};
  uint8_t pageScanType = 0x01;  // Interlaced scan (dual-train frequency listening)
  uint8_t inqMode = 0x02;       // extended inquiry results

  enqueue(OP_RESET, nullptr, 0);
  enqueue(OP_WRITE_LOCAL_NAME, name, sizeof(name));
  enqueue(OP_WRITE_COD, cod, sizeof(cod));
  enqueue(OP_WRITE_PAGE_SCAN_ACT, psa, sizeof(psa));
  enqueue(OP_WRITE_PAGE_SCAN_TYPE, &pageScanType, 1);
  enqueue(OP_WRITE_INQUIRY_MODE, &inqMode, 1);

  // Read back actual settings to verify in logs:
  enqueue(OP_READ_BD_ADDR, nullptr, 0);
  enqueue(OP_READ_PAGE_SCAN_ACT, nullptr, 0);
  enqueue(OP_READ_PAGE_SCAN_TYPE, nullptr, 0);
}

bool btBegin(const String &hostMac) {
  if (!btValidMac(hostMac)) {
    Serial.println("[BT  ] invalid host MAC");
    return false;
  }
  static bool memReleased = false;
  if (!memReleased) {
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);  // Classic only
    memReleased = true;
  }

  currentHostMac = hostMac;

  uint8_t mac[6];
  parseMac(hostMac, mac);
  esp_err_t err = esp_iface_mac_addr_set(mac, ESP_MAC_BT);
  if (err != ESP_OK) {
    Serial.printf("[BT  ] esp_iface_mac_addr_set failed: %d\n", err);
    return false;
  }

  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  cfg.mode = ESP_BT_MODE_CLASSIC_BT;

  esp_bt_controller_status_t status = esp_bt_controller_get_status();
  if (status == ESP_BT_CONTROLLER_STATUS_IDLE) {
    err = esp_bt_controller_init(&cfg);
    if (err != ESP_OK) {
      Serial.printf("[BT  ] esp_bt_controller_init failed: %d\n", err);
      return false;
    }
    unsigned long t0 = millis();
    while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
      if (millis() - t0 > 1000) {
        Serial.println("[BT  ] timeout waiting for controller to init");
        return false;
      }
      delay(5);
    }
  }

  status = esp_bt_controller_get_status();
  if (status == ESP_BT_CONTROLLER_STATUS_INITED) {
    err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
      Serial.printf("[BT  ] esp_bt_controller_enable failed: %d\n", err);
      return false;
    }
    unsigned long t0 = millis();
    while (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
      if (millis() - t0 > 1000) {
        Serial.println("[BT  ] timeout waiting for controller to enable");
        return false;
      }
      delay(5);
    }
  }

  err = esp_vhci_host_register_callback(&vhciCb);
  if (err != ESP_OK) {
    Serial.printf("[BT  ] register callback failed: %d\n", err);
    return false;
  }

  portENTER_CRITICAL(&qMux);
  qHead = qCount = 0;
  portEXIT_CRITICAL(&qMux);
  cmdPending = false;
  connectable = false;
  inquiryActive = false;
  namePending = false;
  nextInquiryAt = millis();
  btUp = true;
  queueInit();
  Serial.printf("[BT  ] up, impersonating host %s\n", hostMac.c_str());
  return true;
}

bool btRestart(const String &hostMac) {
  if (btUp) {
    btUp = false;
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
  }
  return btBegin(hostMac);
}

void btSetConnectable(bool on) {
  if (!btUp || on == connectable) return;
  connectable = on;
  uint8_t v = on ? 0x02 : 0x00;  // page scan only; never discoverable
  enqueue(OP_WRITE_SCAN_ENABLE, &v, 1);
  enqueue(OP_READ_SCAN_ENABLE, nullptr, 0);
  Serial.printf("[BT  ] page scan requested: %s\n", on ? "ON" : "OFF");
}

void btSetWakeAddrs(const std::vector<String> &addrs) {
  portENTER_CRITICAL(&stMux);
  wakeAddrCount = 0;
  for (const auto &addr : addrs) {
    if (wakeAddrCount >= MAX_WAKE_ADDRS) break;
    if (btValidMac(addr)) {
      uint8_t be[6];
      parseMac(addr, be);
      for (int i = 0; i < 6; i++) {
        wakeAddrs[wakeAddrCount][i] = be[5 - i];  // -> HCI order
      }
      wakeAddrCount++;
    }
  }
  portEXIT_CRITICAL(&stMux);
  Serial.printf("[BT  ] set %d wake controller address(es)\n", wakeAddrCount);
}

void btSetWakeAddr(const String &addr) {
  std::vector<String> addrs;
  if (!addr.isEmpty()) addrs.push_back(addr);
  btSetWakeAddrs(addrs);
}

bool btWakeSeen(unsigned long *lastSeenMs) {
  if (lastSeenMs) *lastSeenMs = wakeLastSeen;
  return wakeSeenEver;
}

void btListenerStart() {
  portENTER_CRITICAL(&stMux);
  listenerResult.detected = false;
  listenerResult.addr[0] = 0;
  listenerResult.name[0] = 0;
  listenerResult.cod = 0;
  listenerActive = true;
  portEXIT_CRITICAL(&stMux);
  Serial.println("[BT  ] listener started");
}

BtListenerResult btListenerGetStatus() {
  BtListenerResult res;
  portENTER_CRITICAL(&stMux);
  res = listenerResult;
  if (res.detected && res.name[0] == 0) {
    for (int i = 0; i < MAX_DEVS; i++) {
      if (devs[i].used && strcmp(devs[i].addr, res.addr) == 0 && devs[i].name[0]) {
        strncpy(res.name, devs[i].name, sizeof(res.name) - 1);
        res.name[sizeof(res.name) - 1] = 0;
        strncpy(listenerResult.name, devs[i].name, sizeof(listenerResult.name) - 1);
        listenerResult.name[sizeof(listenerResult.name) - 1] = 0;
        break;
      }
    }
  }
  portEXIT_CRITICAL(&stMux);
  return res;
}

String btResolveHeuristicName(const char *mac, uint32_t cod) {
  if (!mac || strlen(mac) < 8) return "Wireless Gamepad";
  String s = mac;
  s.toLowerCase();
  String prefix = s.substring(0, 8);  // "aa:bb:cc"

  // Sony Interactive Entertainment OUIs
  if (prefix == "00:1b:fb" || prefix == "98:b6:e9" || prefix == "fc:62:b9" ||
      prefix == "00:04:1f" || prefix == "2c:cc:44" || prefix == "70:9e:29" ||
      prefix == "e8:47:3a" || prefix == "00:26:5c") {
    return "PlayStation Controller";
  }

  // Microsoft OUIs
  if (prefix == "5c:ba:37" || prefix == "7c:ed:8d" || prefix == "98:5f:d3" ||
      prefix == "28:18:78" || prefix == "e4:17:d8" || prefix == "00:50:f2" ||
      prefix == "dc:97:ba") {
    return "Xbox Wireless Controller";
  }

  // Nintendo OUIs
  if (prefix == "00:21:4d" || prefix == "94:58:cb" || prefix == "00:09:bf" ||
      prefix == "58:2f:40" || prefix == "e0:e7:51") {
    return "Nintendo Switch Controller";
  }

  // Class of Device (CoD)
  uint8_t major = (cod >> 8) & 0x1F;
  uint8_t minor = (cod >> 2) & 0x3F;
  if (major == 0x05) {
    if (minor & 0x08) return "Gamepad";
    if (minor & 0x04) return "Joystick";
    return "Wireless Peripheral";
  }

  return "Wireless Controller";
}


void btSetDiscovery(bool on) { discovery = on; }

int btSnapshotDevices(BtDev *out, int max) {
  int n = 0;
  portENTER_CRITICAL(&stMux);
  for (int i = 0; i < MAX_DEVS && n < max; i++) {
    if (devs[i].used) out[n++] = devs[i];
  }
  portEXIT_CRITICAL(&stMux);
  return n;
}

void btLoop() {
  if (!btUp) return;
  unsigned long now = millis();

  if (discovery) {
    if (!inquiryActive && !namePending && (int32_t)(now - nextInquiryAt) >= 0) {
      // LAP 0x9E8B33 (GIAC), length in 1.28 s units, unlimited responses.
      uint8_t inq[5] = {0x33, 0x8B, 0x9E, (uint8_t)(BT_INQUIRY_MS * 100 / 128), 0x00};
      inquiryActive = true;
      nextInquiryAt = now + BT_INQUIRY_MS + 2000;  // safety if Complete is lost
      enqueue(OP_INQUIRY, inq, sizeof(inq));
    }

    // Resolve names for entries that don't have one yet (one at a time, never
    // during an inquiry).
    if (namePending && now - nameSentAt > 8000) namePending = false;
    if (!inquiryActive && !namePending) {
      uint8_t req[10] = {0};
      bool found = false;
      portENTER_CRITICAL(&stMux);
      for (int i = 0; i < MAX_DEVS; i++) {
        if (devs[i].used && !devs[i].name[0] && !devs[i].nameTried) {
          devs[i].nameTried = true;
          unsigned b[6];
          sscanf(devs[i].addr, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2],
                 &b[3], &b[4], &b[5]);
          for (int k = 0; k < 6; k++) req[k] = (uint8_t)b[5 - k];
          found = true;
          break;
        }
      }
      portEXIT_CRITICAL(&stMux);
      if (found) {
        req[6] = 0x01;  // page scan repetition mode R1
        namePending = true;
        nameSentAt = now;
        enqueue(OP_REMOTE_NAME_REQ, req, sizeof(req));
      }
    }
  }

  pumpQueue();
}
