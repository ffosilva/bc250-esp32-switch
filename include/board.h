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
//   GPIO13 (BOARD_SENSE)   <-----> BC250 TPMS1 pin 9 (ADC2_CH4)
//
// The ESP32-CAM dedicates most pins to the camera, flash LED, and SD card.
// Switch terminal B is wired directly to header GND (BUTTON_GND = -1).
// GPIO12 is intentionally avoided because it is strapping pin MTDI (pulling it
// HIGH at boot sets flash VDD to 1.8V and bricks boot).
// GPIO13 is ADC2_CH4. ADC2 cannot be sampled while WiFi is running; however,
// board sense is only required during normal operation (WiFi is off).
const int BUTTON_SENSE = 15;
const int BUTTON_GND   = -1; // -1 means wired to physical GND header

const int PS_ON_PIN    = 14;
const int BOARD_SENSE  = 13; // ADC2_CH4

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

// ATX PS_ON# is active LOW and idles at ~5V (pulled up inside the PSU).
// Driven as OPEN-DRAIN so we never push 3.3V against the PSU's 5V pull-up:
//   LOW  -> sink to GND -> PSU on
//   HIGH -> high-impedance -> PSU pull-up wins -> PSU off
const int PS_ON_PIN = 32;

// BC250 TPMS1 (pin 9): reads ~3.3V while the board is powered/booted, 0 when
// off. In practice it's a higher-impedance source that settles near ~2.9V and
// hovers close to the ESP's digital logic threshold, so digitalRead() flickers.
// We read it as an ADC voltage with hysteresis instead (see thresholds below).
// GPIO34 is ADC1_CH6 (input-only). It must be an ADC1 pin on DevKitC: ADC2 is
// unusable while WiFi is running (setup portal).
const int BOARD_SENSE = 34;
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

// WiFi TX power for the SoftAP (the ESP32-C3 mini low-power workaround is not
// needed on the ESP32-WROOM). Lower this if the AP misbehaves on a weak supply.
#define AP_TX_POWER WIFI_POWER_19_5dBm

//*******  Classic Bluetooth wake  ***************

// Classic BT controllers don't advertise: a powered-on, paired controller PAGES
// its bonded host's BD_ADDR. While the machine is OFF the ESP32 adopts the BC250
// Bluetooth adapter's address (config.hostAddr) and listens for a page from the
// bound controller (config.wakeAddr). Every page is rejected at the HCI
// Connection_Request stage, so no link or authentication ever happens and the
// controller's pairing with the BC250 is left untouched.

// Local name / class of device advertised by the ESP32 (computer, desktop).
const char *const BT_LOCAL_NAME = "BC250 Switch";
const uint32_t    BT_COD        = 0x000104;

// The controller counts as "present" while a page from it was seen within this
// window. While OFF, presence => the machine powers on ("machine follows
// controller"). Controllers re-page every ~1-2 s after a reject.
const unsigned long BT_PRESENCE_TIMEOUT_MS = 4000;

// Guard window after any power-off during which presence is ignored. This is
// your chance to also switch the controller off (it then goes absent and the
// machine stays down). If you leave the controller on, once this elapses the
// machine follows it back on. It also rides out the brief reconnect burst the
// controller emits when it loses its host at shutdown.
const unsigned long BT_WAKE_COOLDOWN_MS = 15000;

// Portal-only device discovery duty cycle (keeps airtime free for the SoftAP).
const unsigned long BT_INQUIRY_MS = 5000;   // inquiry length
const unsigned long BT_IDLE_MS    = 10000;  // pause between inquiries

