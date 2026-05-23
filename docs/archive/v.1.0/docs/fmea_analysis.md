# GloveAssist — FMEA Analysis

## Failure Mode and Effects Analysis

| ID | Component | Failure Mode | Effect | Severity | Mitigation | Detection |
|----|-----------|-------------|--------|----------|------------|-----------|
| F-001 | Flex sensor | Open circuit (wire break) | ADC reads ~0 V | High | Threshold check < 0.4 V, fallback to last good value | `ERR_SENSOR_OPEN_CIRCUIT` in logs |
| F-002 | Flex sensor | Short circuit | ADC reads ~3.3 V | High | Threshold check > 3.0 V, fallback to last good value | `ERR_SENSOR_SHORT_CIRCUIT` in logs |
| F-003 | Flex sensor | Drift / aging | Inaccurate readings | Medium | Calibration routine (FR-008), recalibrate to flash | Periodic accuracy check |
| F-004 | All 4 sensors | Simultaneous fault | No gesture detection | Critical | System lockdown, OLED "FAULT", motor OFF | `safety_enter_lockdown()` |
| F-005 | UART link | Cable disconnect | No data to ESP32 | High | ACK timeout (500 ms), retry 3×, then error state | `ERR_UART_ACK_TIMEOUT` |
| F-006 | UART link | CRC mismatch | Corrupted packet | Medium | Drop frame, request retransmit | `ERR_UART_CRC_MISMATCH` |
| F-007 | UART link | Replay attack | Stale data injected | High | Rolling counter validation, max gap = 5 | `ERR_SECURITY_REPLAY` |
| F-008 | BLE | Disconnection | Phone loses data | Medium | Re-advertise automatically, buffer last gestures | `ERR_BLE_DISCONNECTED` |
| F-009 | BLE | Sniffing | Data leak | Medium | Payload XOR obfuscation, honeypot packets (3:1 ratio) | Security audit logs |
| F-010 | Motor (2N2222) | Transistor short | Motor always ON | High | GPIO default LOW, watchdog reboot, hardware fuse | Current sensing (future) |
| F-011 | Motor (2N2222) | Transistor open | No haptic feedback | Low | User perceives no vibration, logged | `ERR_SENSOR_OK` but no ack |
| F-012 | OLED SSD1306 | SPI failure | No display | Low | System continues without display, log warning | SPI error return code |
| F-013 | STM32 | Firmware hang | All STM32 tasks stop | Critical | IWDG watchdog (2 s), automatic reset | Watchdog reset counter in flash |
| F-014 | ESP32 | Firmware hang | BLE + UART stop | Critical | Task watchdog, automatic reboot | ESP32 WDT reset |
| F-015 | Power supply | Brown-out | Undefined behavior | Critical | BOD (brown-out detect), graceful shutdown | BOD interrupt |

## Risk Priority

- **Critical**: F-004, F-013, F-014, F-015 → Must be mitigated before deployment
- **High**: F-001, F-002, F-005, F-007, F-010 → Mitigated in firmware
- **Medium**: F-003, F-006, F-008, F-009 → Mitigated, monitoring recommended
- **Low**: F-011, F-012 → Acceptable with logging
