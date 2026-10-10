# Controller Wake Specification

## Purpose
Detects paired wireless gamepads via Classic Bluetooth or BLE to wake the BC250 host from an unpowered state.

## Requirements

### Requirement: Controller Wake Detection
The system SHALL power on the host computer when a bound wireless controller is detected while the system is in STATE_OFF.

#### Scenario: Bound controller activated while machine is off
- **WHEN** the bound controller is observed within 4000 ms while in STATE_OFF and wake is not inhibited
- **THEN** the system triggers powerOn, asserts PS_ON#, and transitions to STATE_BOOTING

### Requirement: Wake Cooldown Inhibitor
The system SHALL inhibit controller wake triggers for a defined cooldown duration following every power-off event.

#### Scenario: Controller reconnect packets during cooldown
- **WHEN** controller activity is detected within 15000 ms after transitioning to STATE_OFF
- **THEN** the system ignores the wake signal and remains in STATE_OFF

### Requirement: Classic Bluetooth Host Impersonation
The system SHALL run page listening under the host PC's adapter MAC address and reject incoming connection requests on Classic Bluetooth targets.

#### Scenario: Controller pages the host adapter
- **WHEN** a paired controller attempts a connection to the host adapter MAC while the machine is in STATE_OFF
- **THEN** the system marks the controller as present, immediately rejects the HCI connection request without link authentication, and powers on the machine

#### Scenario: System transitions out of STATE_OFF
- **WHEN** the system leaves STATE_OFF into STATE_BOOTING or STATE_ON
- **THEN** the system disables page scan to allow the controller to connect to the real host adapter

### Requirement: Bluetooth Low Energy Scanning
The system SHALL continuously scan for advertising packets matching the bound controller MAC address on BLE targets.

#### Scenario: BLE advertisement detected
- **WHEN** an advertisement matching the bound controller MAC is received while in STATE_OFF
- **THEN** the system updates the last-seen timestamp and initiates power-on if cooldown has expired

### Requirement: ESP32-CAM Flash LED Wake Pulse
The system SHALL pulse the onboard white flash LED (GPIO 4) specifically on the AI-Thinker ESP32-CAM board upon a controller wake event when flashOnWake is enabled.

#### Scenario: Flash LED pulse on ESP32-CAM wake
- **WHEN** a controller wake occurs on an ESP32-CAM target with flashOnWake enabled
- **THEN** the system illuminates the onboard flash LED on GPIO 4 for 500 ms

#### Scenario: Flash LED disabled or non-CAM hardware target
- **WHEN** a controller wake occurs and the device is not an ESP32-CAM target or flashOnWake is disabled
- **THEN** the system does not illuminate any flash indicator LED
