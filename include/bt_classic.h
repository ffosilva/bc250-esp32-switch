#pragma once

#include <Arduino.h>
#include <vector>


// Raw-HCI (VHCI) Classic Bluetooth layer. There is no Bluedroid host stack: the
// ESP32 never accepts a link. It only
//   * impersonates the BC250's Bluetooth adapter (same BD_ADDR) while the machine
//     is OFF, rejecting every incoming Connection_Request. A request from any
//     bound controller counts as "controller present" (wake). Rejecting before
//     any link/authentication exists leaves the controller's pairing untouched.
//   * (portal only) runs interactive page listening and periodic Inquiry.

// Validate a "aa:bb:cc:dd:ee:ff" style address (case-insensitive).
bool btValidMac(const String &mac);

// Set the BT MAC to hostMac, bring up the controller (Classic only) and run the
// HCI init sequence. The radio starts non-connectable. Returns false on failure.
bool btBegin(const String &hostMac);

// Re-initialise with a different host MAC (portal: host MAC changed).
bool btRestart(const String &hostMac);

// Page scan on/off. Only meaningful after btBegin().
void btSetConnectable(bool on);

// Drives the HCI command queue and the discovery duty cycle. Call from loop().
void btLoop();

// ---- Normal mode: bound-controller presence -------------------------------
void btSetWakeAddrs(const std::vector<String> &addrs);
void btSetWakeAddr(const String &addr);
// True if any bound controller has ever paged us;
// *lastSeenMs = millis() of the most recent sighting.
bool btWakeSeen(unsigned long *lastSeenMs);

// ---- Portal: interactive listener ----------------------------------------
struct BtListenerResult {
  bool     detected;
  char     addr[18];
  char     name[33];
  uint32_t cod;
};

void             btListenerStart();
BtListenerResult btListenerGetStatus();
String           btResolveHeuristicName(const char *mac, uint32_t cod);

// ---- Portal: device discovery ---------------------------------------------
struct BtDev {
  char     addr[18];
  char     name[33];
  int      rssi;       // dBm, 0 if unknown (page-only entries)
  uint32_t cod;        // class of device, 0 if unknown
  bool     paged;      // seen paging us => already paired to the host
  bool     used;
  bool     nameTried;
};

void btSetDiscovery(bool on);
int  btSnapshotDevices(BtDev *out, int max);  // returns number of used entries

