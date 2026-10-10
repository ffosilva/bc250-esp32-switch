# Hardware Targets Specification

## Purpose
Defines target-specific pin assignments, peripheral routing, RF power constraints, and build configurations.

## Requirements

### Requirement: ESP32-WROOM DevKitC Configuration
The system SHALL configure GPIOs and analog peripherals for the ESP32-WROOM DevKitC target using Classic Bluetooth.

#### Scenario: Pin and peripheral assignment
- **WHEN** compiled with BOARD_ESP32DEV
- **THEN** the system routes BUTTON_SENSE to GPIO25, BUTTON_GND to GPIO26, PS_ON_PIN to GPIO32, and BOARD_SENSE to GPIO34 on ADC1 with 11 dB attenuation

### Requirement: AI-Thinker ESP32-CAM Configuration
The system SHALL configure dedicated GPIOs, digital board sensing, and flash LED support for the AI-Thinker ESP32-CAM target.

#### Scenario: Pin assignment avoiding strapping and camera lines
- **WHEN** compiled with BOARD_ESP32CAM
- **THEN** the system routes BUTTON_SENSE to GPIO15, PS_ON_PIN to GPIO14, BOARD_SENSE to GPIO13 with INPUT_PULLDOWN, and FLASH_LED to GPIO4

### Requirement: ESP32-C3 Configuration
The system SHALL configure GPIOs, factory-calibrated ADC1, reduced WiFi RF power, and BLE wake for the ESP32-C3 target.

#### Scenario: Pin assignment and RF power limit
- **WHEN** compiled with BOARD_ESP32C3
- **THEN** the system routes BUTTON_SENSE to GPIO5, BUTTON_GND to GPIO6, PS_ON_PIN to GPIO4, BOARD_SENSE to GPIO3, sets WiFi TX power to 8.5 dBm, and uses NimBLE for BLE wake
