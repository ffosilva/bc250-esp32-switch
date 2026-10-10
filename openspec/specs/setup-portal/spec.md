# Setup Portal Specification

## Purpose
Provides a captive WiFi access point and web interface for device configuration, controller discovery, and provisioning.

## Requirements

### Requirement: Setup Portal Activation
The system SHALL launch a captive WiFi SoftAP portal when explicitly requested by user input or configuration flag.

#### Scenario: Button held for setup while off
- **WHEN** the momentary button is held continuously for 8000 ms or longer while in STATE_OFF
- **THEN** the system sets forceSetup to true and restarts into setup mode

#### Scenario: Setup mode initiated via serial command
- **WHEN** an 's' or 'S' character is received on the serial interface
- **THEN** the system sets forceSetup to true and restarts into setup mode

#### Scenario: Button held to exit setup portal
- **WHEN** the momentary button is held continuously for 8000 ms while running in setup mode
- **THEN** the system clears forceSetup and restarts into normal operation mode

### Requirement: Captive Network and Web Serving
The system SHALL broadcast an open WiFi SoftAP named 'BC250 Switch Setup' and redirect HTTP traffic to the setup interface.

#### Scenario: Client connects to setup network
- **WHEN** a client associates with the BC250 Switch Setup access point and queries any DNS address
- **THEN** the captive DNS server resolves all queries to 192.168.4.1 and the web server serves index.html from SPIFFS

### Requirement: Portal Password Protection
The system SHALL require password authentication for configuration changes and issue single-session bearer tokens.

#### Scenario: First-time password creation
- **WHEN** a POST request is made to /api/password with a password of 6 or more characters and no password was previously set
- **THEN** the system stores the SHA-256 hash in NVS and returns a session token

#### Scenario: Authenticating with existing password
- **WHEN** a POST request is made to /api/login with the correct password
- **THEN** the system returns a valid session token for subsequent protected API requests

#### Scenario: Unauthorized access attempt
- **WHEN** a protected endpoint is called without a valid X-Auth-Token header
- **THEN** the system rejects the request with HTTP status 401 Unauthorized

### Requirement: Wireless Device Discovery
The system SHALL discover nearby Bluetooth controllers and report them through the device discovery API.

#### Scenario: Client requests discovered devices
- **WHEN** an authenticated GET request is received at /api/devices
- **THEN** the system returns a JSON list of discovered devices containing MAC address, friendly name, RSSI, and paged status

### Requirement: Interactive Controller Listener
The system SHALL capture incoming connection requests during setup to provide real-time controller detection.

#### Scenario: Controller pages during active listener session
- **WHEN** a paired controller pages the host adapter while an authenticated client is querying /api/listener/status
- **THEN** the system returns detected as true along with the controller MAC address, resolved or heuristic friendly name, and whether it is already bound

#### Scenario: Clearing or starting listener session
- **WHEN** an authenticated POST request is sent to /api/listener/start
- **THEN** the system clears the last-captured controller detection slot and returns HTTP 200 OK

### Requirement: Controller Binding and Provisioning Completion
The system SHALL persist bound controller configurations and adapter addresses and reboot into normal operation when finished.

#### Scenario: Binding controller MAC address
- **WHEN** an authenticated POST request is sent to /api/select with a valid colon-separated MAC address
- **THEN** the system saves the address to NVS and returns HTTP 200 OK

#### Scenario: Adding bound controller
- **WHEN** an authenticated POST request is sent to /api/controllers/add with a valid MAC address and non-empty friendly name
- **THEN** the system appends the controller to the bound list in NVS and returns HTTP 200 OK

#### Scenario: Removing bound controller
- **WHEN** an authenticated POST request is sent to /api/controllers/remove with a bound MAC address
- **THEN** the system deletes the controller from the bound list in NVS and returns HTTP 200 OK

#### Scenario: Renaming bound controller
- **WHEN** an authenticated POST request is sent to /api/controllers/rename with a bound MAC address and new friendly name
- **THEN** the system updates the friendly name in NVS and returns HTTP 200 OK

#### Scenario: Exceeding maximum controller capacity
- **WHEN** a client attempts to add a 9th controller to a list already containing 8 controllers
- **THEN** the system rejects the request with HTTP status 400 Bad Request

#### Scenario: Completing setup
- **WHEN** an authenticated POST request is sent to /api/finish on a fully configured device
- **THEN** the system clears forceSetup, returns HTTP 200 OK, and reboots into normal mode after 800 ms
