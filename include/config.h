#pragma once

#include <Arduino.h>
#include <vector>


// Bound wireless gamepad structure.
//   mac  - controller BD_ADDR/MAC in lower-case colon form ("aa:bb:..").
//   name - human-readable friendly name (e.g. "DualSense - Living Room").
struct BoundController {
  String mac;
  String name;
};

const size_t MAX_CONTROLLERS = 8;

// Persistent configuration, stored in NVS via the Preferences library.
//   controllers - array of bound controllers (Classic BT, up to 8).
//   wakeAddr    - primary bound controller BD_ADDR/MAC (mirrored from controllers[0]).
//   hostAddr    - BC250 Bluetooth adapter BD_ADDR (Classic BT only).
//   passHash    - SHA-256 hex of the portal password ("" => not set yet).
//   forceSetup  - request that the next boot enters the WiFi setup portal.
//   flashOnWake - pulse ESP32-CAM flash LED upon controller wake.
struct Config {
  std::vector<BoundController> controllers;
  String wakeAddr;
  String hostAddr;
  String passHash;
  bool   forceSetup;
  bool   flashOnWake;
};

extern Config config;

void loadConfig();

void setControllers(const std::vector<BoundController> &controllers);
bool addController(const String &mac, const String &name);
bool removeController(const String &mac);
bool renameController(const String &mac, const String &name);

void setWakeAddr(const String &addr);
void setHostAddr(const String &addr);
void setPassHash(const String &hash);
void setForceSetup(bool force);
void setFlashOnWake(bool enable);

// Fully provisioned: a password and controller(s) are set (plus host adapter if Classic BT).
bool isConfigured();

