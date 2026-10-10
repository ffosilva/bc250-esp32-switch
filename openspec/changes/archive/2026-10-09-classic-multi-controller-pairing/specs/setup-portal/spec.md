# Spec Delta: setup-portal

## ADDED Requirements

### Requirement: Interactive Controller Listener
The system SHALL capture incoming connection requests during setup to provide real-time controller detection.

#### Scenario: Controller pages during active listener session
- **WHEN** a paired controller pages the host adapter while an authenticated client is querying /api/listener/status
- **THEN** the system returns detected as true along with the controller MAC address, resolved or heuristic friendly name, and whether it is already bound

#### Scenario: Clearing or starting listener session
- **WHEN** an authenticated POST request is sent to /api/listener/start
- **THEN** the system clears the last-captured controller detection slot and returns HTTP 200 OK

## MODIFIED Requirements

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
