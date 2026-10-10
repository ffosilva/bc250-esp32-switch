# Spec Delta: controller-wake

## MODIFIED Requirements

### Requirement: Controller Wake Detection
The system SHALL power on the host computer when any bound wireless controller from the authorized controllers list (up to 8) is detected while the system is in STATE_OFF.

#### Scenario: Bound controller activated while machine is off
- **WHEN** any controller present in the bound controllers list is observed within 4000 ms while in STATE_OFF and wake is not inhibited
- **THEN** the system triggers powerOn, asserts PS_ON#, and transitions to STATE_BOOTING

#### Scenario: Unbound controller activated
- **WHEN** an incoming connection request is received from a controller MAC not present in the bound controllers list
- **THEN** the system rejects the connection without triggering power-on and remains in STATE_OFF

### Requirement: Classic Bluetooth Host Impersonation
The system SHALL run page listening under the host PC's adapter MAC address and reject incoming connection requests on Classic Bluetooth targets.

#### Scenario: Controller pages the host adapter
- **WHEN** a paired controller attempts a connection to the host adapter MAC while the machine is in STATE_OFF
- **THEN** the system checks if the sender MAC matches any bound controller, marks presence if matched, immediately rejects the HCI connection request without link authentication, and powers on the machine if matched

#### Scenario: System transitions out of STATE_OFF
- **WHEN** the system leaves STATE_OFF into STATE_BOOTING or STATE_ON
- **THEN** the system disables page scan to allow the controller to connect to the real host adapter
