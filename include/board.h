#pragma once

//*******  Pin definitions  ***************
//
#if defined(BOARD_ESP32CAM)
// AI-Thinker ESP32-CAM (ESP-32S module) wiring:
//
//   ESP32-CAM Header              External
//   ----------------              --------
//   GPIO15 (BUTTON_SENSE)  <-----> momentary switch terminal A
//   GND                    <-----> momentary switch terminal B
//   GPIO14 (PS_ON_PIN)     <-----> ATX PS_ON# (green wire, active LOW)
//   GPIO13 (BOARD_SENSE)   <-----> BC250 TPMS1 pin 9 (digital, INPUT_PULLDOWN)
//
// The ESP32-CAM dedicates most pins to the camera, flash LED, and SD card.
// Switch terminal B is wired directly to header GND (BUTTON_GND = -1).
// GPIO12 is intentionally avoided because it is strapping pin MTDI (pulling it
// HIGH at boot sets flash VDD to 1.8V and bricks boot).
// GPIO13 is read as digital input with internal pull-down (INPUT_PULLDOWN).
// ADC2 cannot be sampled when Bluetooth or WiFi is active, and GPIO13 is shared
// with SD DAT3 which floats HIGH unless pulled down.
const int BUTTON_SENSE = 15;
const int BUTTON_GND   = -1; // -1 means wired to physical GND header

const int PS_ON_PIN    = 14;
const int BOARD_SENSE  = 13;

// Onboard white flashlight LED (GPIO 4). Pulsed as indicator when the
// Bluetooth controller wakes the machine.
const int FLASH_LED             = 4;
const unsigned long FLASH_DURATION_MS = 500;

#elif defined(BOARD_ESP32C3)
// ESP32-C3 DevKitM-1 wiring:
//
//   ESP32-C3                      External
//   --------                      --------
//   GPIO5  (BUTTON_SENSE)  <-----> momentary switch terminal A
//   GPIO6  (BUTTON_GND)    <-----> momentary switch terminal B
//   GPIO4  (PS_ON_PIN)     <-----> ATX PS_ON# (green wire, active LOW)
//   GPIO3  (BOARD_SENSE)   <-----> BC250 TPMS1 pin 9 (3.3V = board on)
//
// The switch bridges GPIO5 and GPIO6. GPIO6 is driven LOW to act as a local
// ground, and GPIO5 is read with an internal pull-up: pressed reads LOW.
const int BUTTON_SENSE = 5;
const int BUTTON_GND   = 6;
const int PS_ON_PIN    = 4;
const int BOARD_SENSE  = 3;

#else
// Standard ESP32-WROOM (DevKitC) wiring:
//
//   ESP32-WROOM (DevKitC)         External
//   ---------------------         --------
//   GPIO25 (BUTTON_SENSE)  <-----> momentary switch terminal A
//   GPIO26 (BUTTON_GND)    <-----> momentary switch terminal B
//   GPIO32 (PS_ON_PIN)     <-----> ATX PS_ON# (green wire, active LOW)
//   GPIO34 (BOARD_SENSE)   <-----> BC250 TPMS1 pin 9 (3.3V = board on)
//
// The switch bridges GPIO25 and GPIO26. GPIO26 is driven LOW to act as a local
// ground, and GPIO25 is read with an internal pull-up: pressed reads LOW.
const int BUTTON_SENSE = 25;
const int BUTTON_GND   = 26;
const int PS_ON_PIN    = 32;
const int BOARD_SENSE  = 34;
#endif

// Hysteresis thresholds for the analog board-sense reading. The gap between
// them keeps a noisy signal sitting near the threshold from chattering:
//   reading rises above HIGH -> treat as "board up"
//   reading falls below LOW  -> treat as "board down"
//   in between               -> hold previous state
const int SENSE_HIGH_MV = 2000;
const int SENSE_LOW_MV  = 800;

// TPMS1 is high-impedance and the ESP32 single-shot ADC is noisy, so an isolated
// analogReadMilliVolts() can spike hundreds of mV above the true level. Once the
// board powers off the line floats near 0V but still throws the occasional spike
// past SENSE_HIGH_MV. A single such spike flips the hysteresis HIGH for one loop,
// which restarts the BOARD_OFF_DEBOUNCE_MS countdown -> shutdown detection stalls
// for an unbounded, random time. Averaging this many samples per reading is a
// low-pass that keeps a lone spike from ever crossing a threshold.
const int SENSE_OVERSAMPLE = 16;

//*******  Logic levels  ***************

const int PS_ON_ASSERT  = LOW;   // PSU on
const int PS_ON_RELEASE = HIGH;  // PSU off (open-drain -> high-Z)

//*******  Timing (milliseconds)  ***************

// Switch debounce window.
const unsigned long DEBOUNCE_MS = 30;

// Hold the button this long while the board is ON to force it off.
const unsigned long LONG_PRESS_MS = 5000;

// Hold the button this long while OFF to enter WiFi setup mode (reconfigure the
// bound controller / password). Longer than LONG_PRESS_MS and only armed for
// presses that begin while OFF, so it never collides with force-off.
const unsigned long SETUP_HOLD_MS = 8000;

// TPMS1 must stay LOW continuously for this long before we treat the board as
// having shut itself down. Filters out brief dips/transients during boot/reset.
const unsigned long BOARD_OFF_DEBOUNCE_MS = 1500;

// How long to wait for TPMS1 to go HIGH after asserting PS_ON#. If the board
// hasn't signalled UP by then we assume the boot failed, release the PSU and
// return to idle (OFF).
const unsigned long BOOT_TIMEOUT_MS = 10000;

// Periodic heartbeat log interval.
const unsigned long HEARTBEAT_MS = 1000;

//*******  WiFi setup portal  ***************

// SoftAP name shown when the device is in setup mode (open network).
const char *const AP_SSID = "BC250 Switch Setup";

// WiFi TX power for the SoftAP.
// ESP32-C3 mini boards have an RF/power design flaw (arduino-esp32 #6551):
// at full power the AP emits no usable beacons. Lowering TX power makes it work.
#if defined(BOARD_ESP32C3)
#define AP_TX_POWER WIFI_POWER_8_5dBm
#else
#define AP_TX_POWER WIFI_POWER_19_5dBm
#endif

//*******  Wake timing & parameters  ***************

const unsigned long WAKE_PRESENCE_TIMEOUT_MS = 4000;
const unsigned long WAKE_WAKE_COOLDOWN_MS    = 15000;

// Classic Bluetooth parameters
const char *const BT_LOCAL_NAME = "BC250 Switch";
const uint32_t    BT_COD        = 0x000104;
const unsigned long BT_INQUIRY_MS = 5000;   // inquiry length
const unsigned long BT_IDLE_MS    = 10000;  // pause between inquiries

// Backward-compatibility aliases
const unsigned long BT_PRESENCE_TIMEOUT_MS  = WAKE_PRESENCE_TIMEOUT_MS;
const unsigned long BT_WAKE_COOLDOWN_MS     = WAKE_WAKE_COOLDOWN_MS;
const unsigned long BLE_PRESENCE_TIMEOUT_MS = WAKE_PRESENCE_TIMEOUT_MS;
const unsigned long BLE_WAKE_COOLDOWN_MS    = WAKE_WAKE_COOLDOWN_MS;
