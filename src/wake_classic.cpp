#include "wake.h"
#include "bt_classic.h"
#include "config.h"

const char *wakeModeName() {
  return "classic";
}

bool wakeNeedsHostAddr() {
  return true;
}

bool wakeBegin() {
  if (config.controllers.empty() && config.wakeAddr.isEmpty()) {
    Serial.println("[BT  ] controller/host not configured; BT wake disabled "
                   "(hold button 8s while OFF to configure)");
    return false;
  }
  if (config.hostAddr.isEmpty()) {
    Serial.println("[BT  ] host not configured; BT wake disabled");
    return false;
  }
  if (!btBegin(config.hostAddr)) {
    Serial.println("[BT  ] init failed; BT wake disabled");
    return false;
  }
  std::vector<String> addrs;
  for (const auto &c : config.controllers) {
    addrs.push_back(c.mac);
  }
  if (addrs.empty() && !config.wakeAddr.isEmpty()) {
    addrs.push_back(config.wakeAddr);
  }
  btSetWakeAddrs(addrs);
  Serial.printf("[BT  ] waiting for %d controller(s) (as host %s)\n",
                (int)addrs.size(), config.hostAddr.c_str());
  return true;
}

void wakeLoop() {
  btLoop();
}

void wakeSetConnectable(bool on) {
  btSetConnectable(on);
}

bool wakeSeen(unsigned long *lastSeenMs) {
  return btWakeSeen(lastSeenMs);
}

void wakeSetDiscovery(bool on) {
  if (on) {
    if (!config.hostAddr.isEmpty()) {
      if (btBegin(config.hostAddr)) {
        btSetConnectable(true);
        btSetDiscovery(true);
      }
    }
  } else {
    btSetDiscovery(false);
  }
}

int wakeSnapshotDevices(WakeDev *out, int max) {
  static const int MAX_BT = 48;
  BtDev snap[MAX_BT];
  int n = btSnapshotDevices(snap, max < MAX_BT ? max : MAX_BT);
  for (int i = 0; i < n; i++) {
    strncpy(out[i].addr, snap[i].addr, sizeof(out[i].addr) - 1);
    out[i].addr[sizeof(out[i].addr) - 1] = 0;
    strncpy(out[i].name, snap[i].name, sizeof(out[i].name) - 1);
    out[i].name[sizeof(out[i].name) - 1] = 0;
    out[i].rssi = snap[i].rssi;
    out[i].cod = snap[i].cod;
    out[i].paged = snap[i].paged;
  }
  return n;
}

bool wakeValidMac(const String &mac) {
  return btValidMac(mac);
}

bool wakeSetHostAddr(const String &hostMac) {
  if (hostMac != config.hostAddr) {
    setHostAddr(hostMac);
    if (btRestart(hostMac)) {
      btSetConnectable(true);
      btSetDiscovery(true);
      return true;
    }
    return false;
  }
  return true;
}
