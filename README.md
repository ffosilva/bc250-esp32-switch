# BC250 ESP32 Power Switch

An ESP32 power controller for an AMD **BC250** board running as a desktop. The
BC250 is fed from a PCI-E connector and has no ATX power button, so this firmware
drives the SFX PSU's `PS_ON#` line and senses board power, giving you a real power
button — plus optional "turn on when I pick up my controller" via Bluetooth.

Supports multiple targets from the same repository:
- **ESP32-WROOM (DevKitC)**: Classic Bluetooth (BR/EDR) controller wake.
- **AI-Thinker ESP32-CAM**: Classic Bluetooth wake with onboard flash LED wake indicator.
- **ESP32-C3 (SuperMini / DevKitM-1)**: Bluetooth Low Energy (BLE) controller wake.

## Features

- **Push-button power**: tap to turn on; hold 5 s while running to force off.
- **Follows the board**: if the OS shuts the board down, the PSU is cut automatically.
- **Boot watchdog**: if the board doesn't come up within 10 s, the PSU is released.
- **Bluetooth controller wake** (optional):
  - **Classic Bluetooth (BR/EDR)** on ESP32 / ESP32-CAM (PS4/PS5, Xbox, Switch Pro, 8BitDo in Classic mode).
  - **BLE advertisement wake** on ESP32-C3 (controllers operating in BLE mode).
- **WiFi setup portal**: configure the bound controller from a phone — no reflashing.

## Wiring

The ESP32 is permanently powered from the ATX connector's **5 V standby** (into the
`5V` / VIN pin), so it runs whether the machine is on or off. Share a common ground
between the ESP, the PSU, and the board.

| ESP32 (DevKitC) | ESP32-CAM | ESP32-C3 | Connects to | Notes |
|-----------------|-----------|----------|-------------|-------|
| GPIO25 | GPIO15 | GPIO5 | Momentary switch, terminal A | Read with internal pull-up |
| GPIO26 | GND (header) | GPIO6 | Momentary switch, terminal B | DevKitC/C3 drives LOW; CAM wires directly to GND |
| GPIO32 | GPIO14 | GPIO4 | ATX `PS_ON#` (green wire) | **Open-drain**, active LOW: LOW = PSU on, released = off |
| GPIO34 | GPIO13 | GPIO3 | BC250 `TPMS1` (pin 9) | ~3.3 V when the board is up, 0 when off (ADC voltage) |
| 5V / GND | 5V / GND | 5V / GND | PSU standby + common ground | Permanent power for the ESP |

> **`PS_ON#` is 5 V.** It idles at ~5 V (pulled up inside the PSU), above the ESP32's
> GPIO maximum (~3.6 V). Open-drain keeps the ESP from *driving* 5 V, but the pin still
> sees it. A small N-MOSFET/NPN buffer (or at least a ~1 kΩ series resistor) is
> **recommended** — see [docs/pinout.md](docs/pinout.md#ps_on-5-v-caution-recommended-buffer).

`TPMS1` is a higher-impedance signal that hovers near the logic threshold, so it's read
as an analog voltage with hysteresis rather than a digital pin (ADC1 on DevKitC and ESP32-C3; digital input with pull-down on ESP32-CAM).

**Full pinout, header diagrams for DevKitC, ESP32-CAM, and ESP32-C3 (SuperMini & DevKitM-1), ATX 24-pin and TPMS1 connector diagrams, and the
`PS_ON#` buffer circuit: [docs/pinout.md](docs/pinout.md).**

## Button controls

| Action | Result |
|--------|--------|
| Tap while **off** | Power on |
| Hold ≥ 5 s while **on** | Force power off |
| Hold ≥ 8 s while **off** | Enter WiFi setup portal |

The button is the primary control and always works, even with no controller configured.

## Bluetooth controller wake

This uses **classic Bluetooth (BR/EDR)**, the kind used by most gamepads when paired to
a PC (DualShock 4 / DualSense, Xbox Wireless, 8BitDo, Nintendo Switch Pro Controller / Joy-Con).

A powered-on, paired classic controller doesn't advertise; it *pages* its bonded host.
While the machine is **off**, the ESP32 takes on the BC250's Bluetooth adapter address
and runs **continuous, interlaced page listening**:

1. You turn on the controller (e.g. press the **HOME button** on a Switch controller, or
   power button on an 8BitDo/Xbox/PS controller) → the controller pages its host.
2. The ESP32 catches the page and **rejects** the connection (no link, no authentication,
   so the controller's pairing with the BC250 is never touched) but counts it as
   "controller present", and powers the machine on.
3. Once the machine is powering on, the ESP32 goes silent (stops answering pages). The
   controller's next attempt reaches the real BC250 adapter and connects normally.

When a controller is bound (via the portal), the machine **follows the controller**:
turn the controller on and the machine powers up. After a power-off there's a short
guard window so the controller's reconnect burst can't immediately switch it back on —
turn the controller off within that window to keep the machine down.

Requirements:

- The controller must already be **paired to the BC250**.
- You must give the ESP32 the BC250 Bluetooth **adapter MAC**: on Linux run
  `bluetoothctl show` (the `Controller aa:bb:cc:dd:ee:ff` line); on Windows, Device
  Manager → Bluetooth adapter → Properties → Advanced.

## Setup portal

Hold the button ≥ 8 s while off (or on first use) to start the portal:

1. Connect to the open WiFi network **`BC250 Switch Setup`** and open `http://192.168.4.1`.
2. Create a password.
3. Enter the BC250's Bluetooth adapter MAC.
4. Pick your controller: turn on a controller that's already paired to the BC250 (it
   shows up as **paired**), or put one in pairing mode (it shows up after a scan). You
   can also enter its MAC manually.
5. Finish — the device reboots into normal operation.

## Build & flash

PlatformIO (pioarduino). Two steps — firmware and the portal's web UI (a single
`app/index.html` packed into SPIFFS).

**ESP32-DevKitC (`esp32dev`, default — Classic BT wake):**
```bash
pio run -e esp32dev -t upload     # firmware
pio run -e esp32dev -t uploadfs   # web UI filesystem
```

**AI-Thinker ESP32-CAM (`esp32cam` — Classic BT wake):**
```bash
pio run -e esp32cam -t upload     # firmware
pio run -e esp32cam -t uploadfs   # web UI filesystem
```
> *ESP32-CAM flashing note*: Use a USB-to-UART adapter (or ESP32-CAM-MB base board). Connect **GPIO0 to GND** before powering on to enter download mode; disconnect GPIO0 and reset to run.

**ESP32-C3 DevKitM-1 (`esp32-c3-devkitm-1` — BLE wake):**
```bash
pio run -e esp32-c3-devkitm-1 -t upload     # firmware
pio run -e esp32-c3-devkitm-1 -t uploadfs   # web UI filesystem
```

## Notes

- **Multi-target support**: Switching between Classic Bluetooth (ESP32 / ESP32-CAM) and BLE (ESP32-C3) is done by choosing the PlatformIO environment target (`-e esp32dev`, `-e esp32cam`, or `-e esp32-c3-devkitm-1`).
- **WiFi TX power** for the portal's SoftAP is `AP_TX_POWER` in
  [include/board.h](include/board.h) (lowered automatically on ESP32-C3 to prevent the mini RF bug).
- Serial debug runs at **115200** baud.
- Pin assignments and all timing constants live in [include/board.h](include/board.h).
