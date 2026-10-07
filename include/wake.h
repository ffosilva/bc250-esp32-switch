#pragma once

#include <Arduino.h>

struct WakeDev {
  char     addr[18];
  char     name[33];
  int      rssi;       // dBm, 0 if unknown
  uint32_t cod;        // class of device (Classic only, 0 for BLE)
  bool     paged;      // paged our host (Classic only, false for BLE)
};

// Mode metadata
const char *wakeModeName();        // "classic" or "ble"
bool        wakeNeedsHostAddr();    // true for Classic BT, false for BLE

// Normal mode
bool wakeBegin();
void wakeLoop();
void wakeSetConnectable(bool on);
bool wakeSeen(unsigned long *lastSeenMs);

// Setup portal
void wakeSetDiscovery(bool on);
int  wakeSnapshotDevices(WakeDev *out, int max);
bool wakeValidMac(const String &mac);
bool wakeSetHostAddr(const String &hostMac);
