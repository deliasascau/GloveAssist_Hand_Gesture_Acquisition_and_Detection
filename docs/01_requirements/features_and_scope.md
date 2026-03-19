# Features and Scope

## In-scope for v1.0
F-001: Finger flex sensor acquisition pipeline on STM32.
F-002: Gesture classifier with threshold + hysteresis + confidence score.
F-003: SPI OLED status display.
F-004: Buzzer event signaling.
F-005: UART framed protocol STM32 <-> ESP32.
F-006: BLE GATT service for gesture events.
F-007: Persistent profile storage on ESP32.
F-008: Recalibration command from mobile side.
F-009: Watchdog supervision.

## Out-of-scope for v1.0
O-001: Full OTA updates.
O-002: Advanced ML model on target.
O-003: Multi-user cloud backend.

## Starter features (as required by mentor format)
SF-001: Send 3 stable gestures over BLE to mobile app.
SF-002: Trigger and save recalibration profile from command.

## Stretch features
X-001: Encrypted application payload over BLE characteristic.
X-002: Sensor fault detection and degraded mode.
X-003: Power optimization profile.
