# Design: Classic Multi-Controller Pairing & Interactive Listener

## Context
See `proposal.md` for background and problem statement.

Currently, the Bluetooth Classic layer (`src/bt_classic.cpp`) handles incoming `EVT_CONN_REQUEST` packets by checking against a single 6-byte little-endian address (`wakeAddrBytes`). In the portal, devices are populated via a periodic general inquiry (`OP_INQUIRY`) and remote name requests (`OP_REMOTE_NAME_REQ`). The user picks one device which writes a single string `wakeAddr` to NVS.

## Goals / Non-Goals

**Goals:**
- Provide real-time, interactive controller capture in setup mode via a dedicated listener API.
- Maintain an in-memory list of up to 8 bound controllers and evaluate incoming HCI connection requests in sub-microsecond time.
- Persist controllers with friendly names in NVS and support seamless migration from older single-controller setups.
- Modernize the portal web UI to support adding, renaming, and removing controllers.

**Non-Goals:**
- Changing the BLE target (`BOARD_ESP32C3`) architecture.
- Automating host adapter MAC discovery (the user explicitly noted host MAC entry is fine as-is for now).
- Supporting more than 8 controllers (8 easily covers all 4-player and backup console gamepad use cases).

## Decisions

### Decision 1: In-Memory Multi-Address Representation & Matching
- **Decision**: Define a struct `BoundController` holding the 6-byte LE MAC address, display MAC string, and 32-character friendly name. Keep an array `boundControllers[8]` protected by the existing `stMux` spinlock.
- **Matching**: Inside `EVT_CONN_REQUEST`, iterate through `boundControllers[0..boundControllerCount-1]` comparing 6 bytes with `memcmp`.
- **Rationale**: An 8-iteration loop takes nanoseconds inside the critical section, eliminating the need for complex hash maps or dynamic heap allocations in an ISR-adjacent context.
- **Alternatives Considered**: Dynamic linked list or heap allocation (risks fragmentation and lock contention in FreeRTOS tasks).

### Decision 2: NVS Serialization Format & Migration
- **Decision**: Store the controllers as a JSON array string under NVS key `"controllers"`:
  ```json
  [{"mac": "98:b6:e9:12:34:56", "name": "Living Room DualSense"}]
  ```
- **Migration**: At boot, if `"controllers"` key does not exist but `"wakeAddr"` has a non-empty string, initialize the list with that address named `"Controller 1"`. For backward compatibility with external status consumers, `config.wakeAddr` is kept populated with the first controller's MAC.
- **Rationale**: `ArduinoJson` is already used across `portal.cpp`. Storing a JSON string fits comfortably within NVS limits (less than 400 bytes for 8 entries) and is easy to parse.
- **Alternatives Considered**: Individual indexed keys (`mac0`, `name0`...) which clutter the Preferences namespace and make atomic updates harder.

### Decision 3: Interactive Listener State Machine
- **Decision**: Add a volatile capture slot `lastCapturedDev` in `bt_classic.cpp`. When the portal is in listening mode (started via `POST /api/listener/start`):
  1. The controller button is pressed, paging the impersonated host MAC.
  2. `EVT_CONN_REQUEST` fires. The ESP32 captures the MAC, issues an immediate `OP_REJECT_CONN_REQ` (preserving the controller's link key), and records the device in `lastCapturedDev`.
  3. `bt_classic` automatically attempts `OP_REMOTE_NAME_REQ` to fetch the device's advertised name.
  4. The web UI polls `GET /api/listener/status` every 500 ms. Once a device is captured, it resolves the friendly name (using the remote name, or an OUI/CoD heuristic fallback) and presents a confirmation modal.
- **Rationale**: Replaces the confusing general inquiry scan with a direct, unambiguous trigger.
- **Alternatives Considered**: WebSockets or Server-Sent Events (overkill and increases firmware binary footprint for a simple captive portal).

### Decision 4: Portal UI Flow Evolution
- **Decision**: Replace the device list on the setup screen with a "Bound Controllers" card showing cards/chips of current controllers with `[Rename]` and `[Remove]` buttons, plus a prominent `[+ Add Gamepad]` button triggering the listening modal. Also preserve a manual MAC entry field for edge cases.

## Risks / Trade-offs

- **[Risk] Controller powers off immediately after page rejection before Remote Name Request completes**
  &rarr; *Mitigation*: Implement heuristic fallback names based on MAC OUI vendor prefix (Sony, Microsoft, Nintendo) or Bluetooth Class of Device (CoD), and allow the user to easily edit the name in the capture modal before saving.
- **[Risk] Web UI polling during listener puts load on SoftAP web server**
  &rarr; *Mitigation*: Poll `/api/listener/status` at a gentle 500-1000 ms cadence and stop polling immediately upon detection or modal cancel.
- **[Risk] Attempting to exceed 8 controllers**
  &rarr; *Mitigation*: Enforce limit in both UI (disable "Add" button when count is 8) and backend API (`HTTP 400` if array is full).

## Migration Plan

1. On first boot with updated firmware, `loadConfig()` inspects NVS.
2. If `controllers` key is missing and `wakeAddr` is present, it auto-migrates the entry.
3. If user performs setup, new bindings are stored in `controllers`.
