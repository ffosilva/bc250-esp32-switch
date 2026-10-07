#include "wake.h"
#include "config.h"
#include <NimBLEDevice.h>

const char *wakeModeName() {
  return "ble";
}

bool wakeNeedsHostAddr() {
  return false;
}

// Normal mode wake scan tracking
static volatile bool          bleSeenEver = false;
static volatile unsigned long bleLastSeen = 0;
static bool                   bleInitialized = false;

static void ensureBleInit() {
  if (!bleInitialized) {
    NimBLEDevice::init("");
    bleInitialized = true;
  }
}

class WakeScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    // Compare address case-insensitively
    if (strcasecmp(dev->getAddress().toString().c_str(), config.wakeAddr.c_str()) == 0) {
      bleLastSeen = millis();
      bleSeenEver = true;
    }
  }
};

static WakeScanCallbacks wakeScanCallbacks;

bool wakeBegin() {
  if (config.wakeAddr.isEmpty()) {
    Serial.println("[BLE ] no controller bound; BLE wake disabled "
                   "(hold button 8s while OFF to configure)");
    return false;
  }
  ensureBleInit();
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&wakeScanCallbacks, true);  // true = report duplicates
  scan->setActiveScan(false);  // passive: we only need the address, saves power
  scan->setInterval(160);      // ms
  scan->setWindow(80);         // ms (<= interval; ~50% duty)
  scan->start(0, false);       // 0 = scan continuously
  Serial.printf("[BLE ] scanning for controller %s\n", config.wakeAddr.c_str());
  return true;
}

void wakeLoop() {
  // NimBLE scan runs on its own background task
}

void wakeSetConnectable(bool on) {
  if (!bleInitialized) return;
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (!scan) return;
  if (on) {
    if (!scan->isScanning()) {
      scan->start(0, false);
    }
  } else {
    if (scan->isScanning()) {
      scan->stop();
    }
  }
}

bool wakeSeen(unsigned long *lastSeenMs) {
  if (!bleSeenEver) return false;
  if (lastSeenMs) *lastSeenMs = bleLastSeen;
  return true;
}

// --- Portal: BLE device discovery ---
struct ScannedBleDev {
  char addr[18];
  char name[33];
  int  rssi;
  bool used;
};

static const int       MAX_DEVS = 48;
static ScannedBleDev   devs[MAX_DEVS];
static portMUX_TYPE    devsMux = portMUX_INITIALIZER_UNLOCKED;

class SetupScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    std::string addrStr = d->getAddress().toString();
    const char *addr = addrStr.c_str();
    std::string name = d->getName();
    int rssi = d->getRSSI();

    portENTER_CRITICAL(&devsMux);
    int idx = -1, free = -1;
    for (int i = 0; i < MAX_DEVS; i++) {
      if (devs[i].used) {
        if (strcasecmp(devs[i].addr, addr) == 0) { idx = i; break; }
      } else if (free < 0) {
        free = i;
      }
    }
    if (idx < 0 && free >= 0) {
      idx = free;
      devs[idx].used = true;
      strncpy(devs[idx].addr, addr, sizeof(devs[idx].addr) - 1);
      devs[idx].addr[sizeof(devs[idx].addr) - 1] = 0;
      devs[idx].name[0] = 0;
    }
    if (idx >= 0) {
      devs[idx].rssi = rssi;
      if (!name.empty()) {
        strncpy(devs[idx].name, name.c_str(), sizeof(devs[idx].name) - 1);
        devs[idx].name[sizeof(devs[idx].name) - 1] = 0;
      }
    }
    portEXIT_CRITICAL(&devsMux);
  }
};

static SetupScanCallbacks setupScanCallbacks;

void wakeSetDiscovery(bool on) {
  ensureBleInit();
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (on) {
    portENTER_CRITICAL(&devsMux);
    memset(devs, 0, sizeof(devs));
    portEXIT_CRITICAL(&devsMux);

    scan->setScanCallbacks(&setupScanCallbacks, true);
    scan->setActiveScan(true);
    scan->setInterval(500);  // ms
    scan->setWindow(45);     // ms (~9% duty, leaves radio free for SoftAP WiFi)
    scan->start(0, false);
  } else {
    scan->stop();
  }
}

int wakeSnapshotDevices(WakeDev *out, int max) {
  ScannedBleDev snap[MAX_DEVS];
  portENTER_CRITICAL(&devsMux);
  memcpy(snap, devs, sizeof(devs));
  portEXIT_CRITICAL(&devsMux);

  int count = 0;
  for (int i = 0; i < MAX_DEVS && count < max; i++) {
    if (!snap[i].used) continue;
    strncpy(out[count].addr, snap[i].addr, sizeof(out[count].addr) - 1);
    out[count].addr[sizeof(out[count].addr) - 1] = 0;
    strncpy(out[count].name, snap[i].name, sizeof(out[count].name) - 1);
    out[count].name[sizeof(out[count].name) - 1] = 0;
    out[count].rssi = snap[i].rssi;
    out[count].cod = 0;
    out[count].paged = false;
    count++;
  }
  return count;
}

bool wakeValidMac(const String &mac) {
  if (mac.length() != 17) return false;
  for (int i = 0; i < 17; i++) {
    if (i % 3 == 2) {
      if (mac[i] != ':') return false;
    } else {
      if (!isxdigit(mac[i])) return false;
    }
  }
  return true;
}

bool wakeSetHostAddr(const String &hostMac) {
  (void)hostMac;
  return true;
}
