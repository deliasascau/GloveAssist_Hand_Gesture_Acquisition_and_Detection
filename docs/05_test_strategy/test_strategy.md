# Test Strategy

## Objectives
- Verify functional behavior against requirements.
- Detect regressions early in CI.
- Demonstrate stability and communication robustness.

## Test levels
1. Unit tests (host)
- parser, CRC, gesture classification logic

2. Component tests (target)
- UART driver, BLE service, storage module

3. Integration tests
- STM32 <-> ESP32 frame exchange
- mobile command round-trip

4. System validation
- end-to-end gesture scenario
- stress and endurance run

## Entry and exit criteria
Entry:
- requirements baseline approved
- interface definitions frozen

Exit:
- all critical tests pass
- no open critical defects
- demo script validated

## Evidence artifacts
- CI logs
- test reports
- sniffing captures
- validation checklist
