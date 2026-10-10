# Proposal: Classic Multi-Controller Pairing & Interactive Listener

## Why
On Bluetooth Classic targets (`esp32dev` and `esp32cam`), the existing setup workflow requires users to navigate an unfiltered Bluetooth inquiry scan that lists unrelated nearby devices or requires controllers to be placed in pairing mode (which breaks or fails to pair with the host PC). Furthermore, the switch currently only supports a single bound controller address, preventing households with multiple gamepads from using more than one controller to wake the machine.

Introducing an interactive "listener" mode lets users tap a button on their controller to instantly capture it, assign a friendly name, and register up to 8 distinct gamepads to wake the BC250 host.

## What Changes
- **Interactive Listener Mode**: Add `/api/listener` endpoints to intercept and report controllers that page the host adapter during setup, providing instant feedback without running confusing inquiry scans.
- **Multi-Controller Capacity**: Expand the wake-matching engine to support up to 8 simultaneously bound gamepads.
- **Friendly Names**: Associate human-readable names with each bound controller (inferred via HCI remote name request / OUI heuristic, with custom renaming in the UI).
- **Controller Management Portal UI**: Replace the single-select device picker in `app/index.html` with a bound-controllers management dashboard (view, add via listener, rename, and remove).
- **Persistent Storage & Backward Compatibility**: Persist the list of bound controllers in NVS (`controllers` JSON), with automatic migration from the legacy `wakeAddr` key and mirroring of the primary controller.

## Capabilities

### New Capabilities
*(None; builds on existing capabilities)*

### Modified Capabilities
- `controller-wake`: Expand wake detection to match incoming HCI connection requests against any of up to 8 bound controller addresses.
- `setup-portal`: Add interactive listener status/start endpoints and controller management APIs (`/api/controllers/add`, `/api/controllers/remove`, `/api/controllers/rename`).
- `configuration-storage`: Persist an array of up to 8 controllers with MAC addresses and friendly names in NVS, with automatic migration from legacy `wakeAddr`.

## Impact
- **Hardware Targets**: Scoped to Classic Bluetooth targets (`BOARD_ESP32DEV` and `BOARD_ESP32CAM`). BLE targets (`BOARD_ESP32C3`) remain unchanged.
- **Firmware Code**: `src/bt_classic.cpp`, `src/config.cpp`, `src/portal.cpp`, `include/config.h`, `include/bt_classic.h`.
- **Portal Assets**: `app/index.html` (repacked into SPIFFS filesystem).
- **APIs**: New endpoints on the SoftAP web server; backward-compatible fields maintained on `/api/status`.
