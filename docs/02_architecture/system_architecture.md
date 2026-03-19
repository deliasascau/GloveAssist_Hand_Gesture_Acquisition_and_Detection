# System Architecture

## High-level blocks
1. Sensor and control domain (STM32F103)
- flex sensor acquisition
- signal filtering and normalization
- gesture recognition
- OLED and buzzer control
- watchdog and scheduler

2. Connectivity and persistence domain (ESP32)
- BLE GATT server
- command/control endpoint
- profile storage in non-volatile memory
- optional payload protection

3. Inter-MCU communication
- UART 115200 8N1
- framed messages with version, length, sequence, and CRC

## Data flow summary
- STM32 samples sensors and computes gesture result.
- STM32 sends gesture event to ESP32 over UART.
- ESP32 publishes event over BLE characteristic.
- Mobile command triggers recalibration.
- ESP32 forwards command to STM32, receives status, stores profile.

## Key design decisions
D-001: dual-MCU split to isolate real-time processing from wireless stack.
D-002: strict interface framing to reduce integration risk.
D-003: requirement-test traceability to support evaluation checkpoints.
