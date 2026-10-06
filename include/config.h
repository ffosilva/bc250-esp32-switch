#pragma once

#include <Arduino.h>

// Persistent configuration, stored in NVS via the Preferences library.
//   wakeAddr   - bound controller Classic BT BD_ADDR, lower-case colon form ("aa:bb:..").
//   hostAddr   - BC250 Bluetooth adapter BD_ADDR, impersonated while the machine is OFF.
//   passHash   - SHA-256 hex of the portal password ("" => not set yet).
//   forceSetup - request that the next boot enters the WiFi setup portal.
struct Config {
  String wakeAddr;
  String hostAddr;
  String passHash;
  bool   forceSetup;
};

extern Config config;

void loadConfig();

void setWakeAddr(const String &addr);
void setHostAddr(const String &addr);
void setPassHash(const String &hash);
void setForceSetup(bool force);

// Fully provisioned: a password has been set and a controller and the host adapter MAC are set.
bool isConfigured();
