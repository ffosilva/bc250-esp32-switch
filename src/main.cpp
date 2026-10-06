#include <Arduino.h>
#include "bt_classic.h"
#include "board.h"
#include "config.h"
#include "portal.h"

// The ESP32 is permanently powered from the ATX connector (5VSB), so it runs
// independently of the PSU's main rails. It controls the SFX PSU through PS_ON#
// and watches the BC250's TPMS1 line to know whether the board itself is up.
//
// Control model
// -------------
//   * Asserting PS_ON# turns the PSU on; the BC250 is expected to power up from
//     applied power (BIOS "restore on AC power"), so PSU on == board boots.
//   * We have no wire to the board's power button, so "turning the board off"
//     means cutting PSU power via PS_ON#. This is a hard power-off, not a
//     graceful OS shutdown.
//   * If the board shuts itself down (e.g. OS shutdown), TPMS1 drops to 0 while
//     the PSU is still energized. We detect that and release PS_ON# so the PSU
//     follows the board down.
//   * While OFF, the ESP32 impersonates the BC250's Bluetooth adapter. A paired
//     Classic BT controller that powers on pages its host; the page is rejected
//     (pairing untouched) but counts as "controller present", which wakes the
//     machine just like a button tap. The radio is silent while BOOTING/ON so the
//     real adapter answers the controller.

enum PowerState {
  STATE_OFF,      // PSU released, board down
  STATE_BOOTING,  // PSU asserted, waiting for TPMS1 to go HIGH
  STATE_ON,       // PSU asserted, board up (TPMS1 HIGH)
};

static PowerState state = STATE_OFF;

// --- Button debounce / press tracking ---
static bool          buttonStable      = false;  // debounced "pressed" level
static bool          buttonLastRaw     = false;
static unsigned long buttonLastChange  = 0;
static unsigned long pressStart        = 0;
static bool          pressStartedOff   = false;  // press began while OFF
static bool          longPressFired    = false;
static bool          setupFired        = false;

// --- Board-sense debounce ---
static bool          boardSenseStable  = false;  // debounced TPMS1 HIGH
static bool          boardSenseLastRaw = false;
static unsigned long boardSenseChange  = 0;

// --- Bluetooth wake ---
// Presence is recorded by the BT controller task (see bt_classic.cpp) and read
// from loop() via btWakeSeen().
static bool          btWakeActive    = false;  // host + controller MACs configured
static unsigned long btInhibitUntil  = 0;      // wakes ignored until this

// --- Misc timers ---
static unsigned long bootStart    = 0;
static unsigned long lastHeartbeat = 0;

static const char *stateName(PowerState s) {
  switch (s) {
    case STATE_OFF:     return "OFF";
    case STATE_BOOTING: return "BOOTING";
    case STATE_ON:      return "ON";
  }
  return "?";
}

static void setState(PowerState next) {
  if (next == state) return;
  Serial.printf("[STATE] %s -> %s\n", stateName(state), stateName(next));
  state = next;
  // Only answer controller pages while the machine is down.
  if (btWakeActive) btSetConnectable(next == STATE_OFF);
}

// Raw (pre-time-debounce) board-up level, derived from the TPMS1 voltage with
// hysteresis so a signal hovering near the logic threshold doesn't chatter.
static bool senseLevel = false;

// One ADC sample in mV. The classic ESP32's analogReadMilliVolts() needs an eFuse
// calibration (vref) that many modules never had burned; without it every read
// fails and returns 0. Use the raw 12-bit count instead: the ADC1 + 11 dB range is
// roughly 0..3.3 V, which is accurate enough for the wide hysteresis thresholds.
static uint32_t senseReadMv() {
  return (uint32_t)analogRead(BOARD_SENSE) * 3300UL / 4095UL;
}

static bool readBoardSense(uint32_t *outMv = nullptr) {
  // Average several reads to reject single-sample ADC/line spikes. Without this,
  // one stray spike past SENSE_HIGH_MV chatters the hysteresis and restarts the
  // board-off debounce, stalling shutdown detection (see SENSE_OVERSAMPLE).
  uint32_t acc = 0;
  for (int i = 0; i < SENSE_OVERSAMPLE; i++) {
    acc += senseReadMv();
  }
  uint32_t mv = acc / SENSE_OVERSAMPLE;
  if (outMv) *outMv = mv;
  if (senseLevel) {
    if (mv < SENSE_LOW_MV) senseLevel = false;
  } else {
    if (mv > SENSE_HIGH_MV) senseLevel = true;
  }
  return senseLevel;
}

static void psuOn() {
  digitalWrite(PS_ON_PIN, PS_ON_ASSERT);
  Serial.println("[PSU ] PS_ON# asserted (LOW) -> PSU ON");
}

static void psuOff() {
  digitalWrite(PS_ON_PIN, PS_ON_RELEASE);
  Serial.println("[PSU ] PS_ON# released (high-Z) -> PSU OFF");
}

// Shared power-on path, used by both the button and the Bluetooth wake. Takes the
// loop's `now` rather than calling millis() itself: the BOOTING timeout compares
// against the `now` cached at the top of normalLoop(), and a fresh millis() here
// can land a millisecond past it. Since the comparison is unsigned, bootStart >
// now makes (now - bootStart) underflow to a huge value and trip the timeout on
// the very next loop. Sharing one clock per iteration keeps the math monotonic.
static void powerOn(const char *reason, unsigned long now) {
  Serial.printf("[ACT ] %s -> powering on\n", reason);
  psuOn();
  bootStart = now;
  setState(STATE_BOOTING);
}

// Shared power-off path. Starts the BT-wake cooldown so the controller's
// post-shutdown reconnect burst can't immediately wake us again. Takes `now` for
// the same single-clock-per-loop reason as powerOn().
static void powerOff(const char *reason, unsigned long now) {
  Serial.printf("[ACT ] %s -> powering off\n", reason);
  psuOff();
  btInhibitUntil = now + BT_WAKE_COOLDOWN_MS;
  setState(STATE_OFF);
}

static void startBtWake() {
  if (config.wakeAddr.isEmpty() || config.hostAddr.isEmpty()) {
    Serial.println("[BT  ] controller/host not configured; BT wake disabled "
                   "(hold button 8s while OFF to configure)");
    return;
  }
  if (!btBegin(config.hostAddr)) {
    Serial.println("[BT  ] init failed; BT wake disabled");
    return;
  }
  btSetWakeAddr(config.wakeAddr);
  btWakeActive = true;
  btSetConnectable(state == STATE_OFF);
  Serial.printf("[BT  ] waiting for controller %s (as host %s)\n",
                config.wakeAddr.c_str(), config.hostAddr.c_str());
}

// Persist a setup request and reboot into the WiFi portal.
static void enterSetupMode(const char *reason) {
  Serial.printf("[ACT ] %s -> entering setup mode\n", reason);
  setForceSetup(true);
  delay(50);
  ESP.restart();
}

// Debounce a raw level into a stable level. Returns true if the stable value
// changed this call, writing the new value into *stable.
static bool debounce(bool raw, bool *stable, bool *lastRaw,
                     unsigned long *lastChange, unsigned long now,
                     unsigned long window) {
  if (raw != *lastRaw) {
    *lastRaw = raw;
    *lastChange = now;
  }
  if (raw != *stable && (now - *lastChange) >= window) {
    *stable = raw;
    return true;
  }
  return false;
}

static void normalBegin() {
  Serial.println("=== BC250 PSU controller ===");

  // PS_ON# open-drain, released by default so the PSU stays off at boot.
  pinMode(PS_ON_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(PS_ON_PIN, PS_ON_RELEASE);

  // Switch: GPIO6 = local ground, GPIO5 = sensed input with pull-up.
  pinMode(BUTTON_GND, OUTPUT);
  digitalWrite(BUTTON_GND, LOW);
  pinMode(BUTTON_SENSE, INPUT_PULLUP);

  // TPMS1 sense: read as ADC over the full 0-3.3V range.
  analogSetAttenuation(ADC_11db);  // global; the per-pin call errors before the first read

  // Seed debounced states from the current levels.
  buttonStable = buttonLastRaw = (digitalRead(BUTTON_SENSE) == LOW);
  boardSenseStable = boardSenseLastRaw = readBoardSense();

  // If the board is already up when the ESP (re)boots, adopt the ON state
  // rather than assuming OFF — keeps us in sync after an ESP-only reset.
  if (boardSenseStable) {
    psuOn();  // make sure PS_ON# matches reality
    setState(STATE_ON);
  }

  Serial.printf("[INIT] state=%s board=%s bound=%s\n",
                stateName(state), boardSenseStable ? "UP" : "DOWN",
                config.wakeAddr.isEmpty() ? "(none)" : config.wakeAddr.c_str());

  startBtWake();
}

static bool g_setupMode = false;

void setup() {
  Serial.begin(115200);
  // Give USB-CDC a moment to enumerate so early logs aren't lost.
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 2000) {
    delay(10);
  }
  Serial.println();

  loadConfig();

  // The portal only starts on an explicit request (8s button hold while OFF).
  // Otherwise we ALWAYS run the normal controller so button control works even
  // when no Bluetooth controller has been configured.
  if (config.forceSetup) {
    g_setupMode = true;
    portalBegin();
  } else {
    normalBegin();
  }
}

static void normalLoop() {
  unsigned long now = millis();

  // --- Serial commands for debugging ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      enterSetupMode("serial 's' command");
    }
  }

  // --- Sample & debounce inputs ---
  uint32_t senseMv;
  bool buttonRaw = (digitalRead(BUTTON_SENSE) == LOW);   // pressed == LOW
  bool boardRaw  = readBoardSense(&senseMv);             // board up (hysteresis)

  if (debounce(buttonRaw, &buttonStable, &buttonLastRaw,
               &buttonLastChange, now, DEBOUNCE_MS)) {
    if (buttonStable) {
      // Press began.
      pressStart = now;
      pressStartedOff = (state == STATE_OFF);
      longPressFired = false;
      setupFired = false;
      Serial.println("[BTN ] pressed");
    } else {
      // Released. A short tap that began while OFF powers on (power-on is on
      // release so that a long hold from OFF can mean "enter setup" instead).
      unsigned long held = now - pressStart;
      Serial.printf("[BTN ] released after %lu ms\n", held);
      if (pressStartedOff && !setupFired && state == STATE_OFF &&
          held < SETUP_HOLD_MS) {
        powerOn("short press while OFF", now);
      }
    }
  }

  // --- Button holds ---
  if (buttonStable && pressStartedOff && !setupFired &&
      (now - pressStart) >= SETUP_HOLD_MS) {
    // Long hold from OFF -> reconfigure (does not power the machine on).
    setupFired = true;
    enterSetupMode("long hold (>8s) while OFF");
  }
  if (buttonStable && !pressStartedOff && !longPressFired && state == STATE_ON &&
      (now - pressStart) >= LONG_PRESS_MS) {
    // Long hold that began while ON -> force off.
    longPressFired = true;
    powerOff("long press (>5s) while ON", now);
  }

  // --- Bluetooth controller wake ("machine follows controller") ---
  // While OFF, the controller paging us powers the machine on. The guard
  // window after a power-off lets you switch the controller off first (so it
  // goes absent and the machine stays down) and rides out the controller's
  // reconnect burst at shutdown.
  btLoop();
  unsigned long btLast = 0;
  bool btPresent = btWakeSeen(&btLast) && (now - btLast) < BT_PRESENCE_TIMEOUT_MS;
  bool btInhibited = (int32_t)(btInhibitUntil - now) > 0;
  if (state == STATE_OFF && btPresent && !btInhibited) {
    powerOn("controller present (BT)", now);
  }

  bool boardChanged = debounce(boardRaw, &boardSenseStable, &boardSenseLastRaw,
                               &boardSenseChange, now,
                               state == STATE_BOOTING ? DEBOUNCE_MS
                                                      : BOARD_OFF_DEBOUNCE_MS);

  // --- State-driven board-sense handling ---
  switch (state) {
    case STATE_BOOTING:
      if (boardSenseStable) {
        Serial.println("[ACT ] TPMS1 HIGH -> board is up");
        setState(STATE_ON);
      } else if ((now - bootStart) >= BOOT_TIMEOUT_MS) {
        powerOff("boot timed out, board never signalled UP", now);
      }
      break;

    case STATE_ON:
      // Board dropped TPMS1 on its own (OS shutdown / crash) -> follow it down.
      if (boardChanged && !boardSenseStable) {
        powerOff("TPMS1 LOW while ON, board shut down", now);
      }
      break;

    case STATE_OFF:
    default:
      break;
  }

  // --- Heartbeat ---
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    Serial.printf("[HB  ] state=%s board=%s btn=%s bt=%s | sense: %umV (%s)\n",
                  stateName(state),
                  boardSenseStable ? "UP" : "DOWN",
                  buttonStable ? "down" : "up",
                  btPresent ? "present" : "absent",
                  senseMv, boardRaw ? "high" : "low");
  }
}

void loop() {
  if (g_setupMode) {
    portalLoop();
    return;
  }
  normalLoop();
}
