# Configuration Storage Specification

## Purpose
Manages persistent non-volatile storage for controller bindings, adapter MACs, credentials, and runtime options.

## Requirements

### Requirement: Persistent Storage of Configuration Parameters
The system SHALL store and retrieve configuration keys from non-volatile storage under the 'bc250' namespace.

#### Scenario: Loading configuration at startup
- **WHEN** the firmware boots
- **THEN** the system reads wakeAddr, hostAddr, passHash, forceSetup, and flashOnWake from NVS Preferences

#### Scenario: Updating configuration values
- **WHEN** any configuration setter is called
- **THEN** the system writes the updated value to NVS immediately

### Requirement: Provisioning Status Evaluation
The system SHALL determine whether the device is fully provisioned based on required target-specific parameters.

#### Scenario: Provisioned Classic Bluetooth device
- **WHEN** passHash, wakeAddr, and hostAddr are all non-empty strings on Classic BT targets
- **THEN** isConfigured returns true

#### Scenario: Provisioned BLE device
- **WHEN** passHash and wakeAddr are both non-empty strings on BLE targets
- **THEN** isConfigured returns true

#### Scenario: Missing required parameters
- **WHEN** any required parameter is empty
- **THEN** isConfigured returns false
