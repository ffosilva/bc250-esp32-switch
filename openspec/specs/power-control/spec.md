# Power Control Specification

## Purpose
Manages the ATX power supply state machine, push-button inputs, boot watchdog, and board shutdown detection for the BC250 system.

## Requirements

### Requirement: Power State Management
The power controller SHALL maintain three mutually exclusive operational states: STATE_OFF, STATE_BOOTING, and STATE_ON, driving the ATX PS_ON# signal accordingly.

#### Scenario: Initial boot with board unpowered
- **WHEN** the ESP32 starts up and the board sense signal is LOW
- **THEN** the system enters STATE_OFF and releases the PS_ON# line to high-impedance

#### Scenario: Initial boot with board already energized
- **WHEN** the ESP32 starts up and the board sense signal is stable HIGH
- **THEN** the system enters STATE_ON and asserts PS_ON# LOW to synchronize with the running board

### Requirement: Push-Button Power On
The system SHALL trigger a system power-on sequence when the momentary power button is tapped while in STATE_OFF.

#### Scenario: Short button press while off
- **WHEN** the momentary button is pressed and released in under 8000 ms while in STATE_OFF
- **THEN** the system asserts PS_ON# LOW, starts the boot timer, and transitions to STATE_BOOTING

### Requirement: Boot Watchdog
The system SHALL monitor the board sense line during boot and abort power-up if the motherboard fails to signal active power within the boot timeout.

#### Scenario: Board boots successfully
- **WHEN** the board sense signal transitions to HIGH within 10000 ms after entering STATE_BOOTING
- **THEN** the system transitions from STATE_BOOTING to STATE_ON and maintains PS_ON# asserted

#### Scenario: Board boot timeout failure
- **WHEN** the board sense signal fails to reach HIGH within 10000 ms in STATE_BOOTING
- **THEN** the system releases PS_ON# to high-impedance and transitions to STATE_OFF

### Requirement: Board-Following Shutdown Detection
The system SHALL detect when the host motherboard shuts down independently and automatically release the power supply.

#### Scenario: Host operating system powers down
- **WHEN** the board sense signal remains continuously LOW for 1500 ms while in STATE_ON
- **THEN** the system releases PS_ON# to high-impedance and transitions to STATE_OFF

### Requirement: Force Power Off
The system SHALL force an immediate power cut when the user holds the momentary power button during system operation.

#### Scenario: Long button hold while powered on
- **WHEN** the button is held continuously for 5000 ms or longer while in STATE_ON
- **THEN** the system releases PS_ON# to high-impedance, transitions to STATE_OFF, and arms the wake cooldown

### Requirement: Board Sense Signal Conditioning
The system SHALL filter and condition the board sense signal against high-impedance line noise and transient spikes.

#### Scenario: Noise rejection via oversampled hysteresis
- **WHEN** analog board sense voltage is read on ADC-equipped hardware targets
- **THEN** the system averages 16 consecutive ADC samples and applies hysteresis thresholds of 2000 mV for HIGH and 800 mV for LOW
