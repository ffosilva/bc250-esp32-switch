# Tasks

## 1. Storage & Configuration Data Model

- [x] 1.1 Update `include/config.h` and `src/config.cpp` to represent bound controllers (up to 8, with MAC address and friendly name) and persist them as JSON under NVS key `controllers`.
- [x] 1.2 Implement automatic migration from legacy `wakeAddr` to the new `controllers` array in `loadConfig()`, and update `isConfigured()` to check for at least one bound controller on Classic BT targets.


## 2. Bluetooth Classic Engine & Listener

- [x] 2.1 Update `src/bt_classic.cpp` and `include/bt_classic.h` to store up to 8 bound controller addresses and perform multi-address matching inside `EVT_CONN_REQUEST`.
- [x] 2.2 Implement the listener capture state in `src/bt_classic.cpp` to record the most recently paged controller, initiate `OP_REMOTE_NAME_REQ`, and provide getter/reset methods for portal endpoints.
- [x] 2.3 Implement name resolution heuristics (OUI prefix / Class of Device fallback) for gamepads that disconnect before completing remote name requests.


## 3. Setup Portal API Endpoints

- [x] 3.1 Implement `POST /api/listener/start` and `GET /api/listener/status` in `src/portal.cpp`.
- [x] 3.2 Implement `POST /api/controllers/add`, `POST /api/controllers/remove`, and `POST /api/controllers/rename` in `src/portal.cpp` with capacity (max 8) and input validation.
- [x] 3.3 Update `GET /api/status` in `src/portal.cpp` to include the `controllers` list alongside the legacy `wakeAddr` mirror.


## 4. Web UI Dashboard & Interactive Modal

- [x] 4.1 Redesign the controller screen in `app/index.html` into a "Bound Controllers" dashboard showing current gamepads, rename inputs, and remove actions.
- [x] 4.2 Implement the interactive "Listening" modal in `app/index.html` that polls `/api/listener/status` and prompts the user to review/edit the detected friendly name before saving.
- [x] 4.3 Verify SPIFFS filesystem build using PlatformIO (`pio run -e esp32dev -t buildfs`).


## 5. Verification & Documentation

- [x] 5.1 Update `README.md` to document the interactive listener pairing process and multi-controller wake capabilities.
- [x] 5.2 Verify firmware compilation across both Classic Bluetooth targets (`pio run -e esp32dev` and `pio run -e esp32cam`).

