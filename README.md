# GloveAssist - Dual MCU Embedded Project

## Purpose
GloveAssist is a bare-metal C embedded system with dual MCU architecture:
- STM32F103 (sensor acquisition, local processing, deterministic control)
- ESP32 (BLE communication, data storage, mobile interface)

The project is developed from zero and follows an engineering workflow:
requirements, features, architecture diagrams, implementation, tests, documentation, and CI/CD.

## Mandatory capability coverage
- Wireless protocols: BLE (ESP32), UART inter-MCU link
- Data processing: sensor filtering, calibration, gesture recognition
- Cryptography: BLE secure pairing + application-level message protection
- Sniffing: BLE packet capture and UART frame verification
- CI/CD: source control, automated checks, unit tests pipeline

## Repository structure
- docs: project engineering artifacts
- firmware: MCU software split by platform
- tests: host and integration test assets
- ci: quality gates and CI support files
- .github/workflows: CI pipelines

## Current status
Project bootstrap completed:
- engineering documents initialized
- architecture diagrams initialized
- git standards and CI pipeline initialized

## Next execution steps
1. Freeze requirements and feature scope (v1.0 baseline)
2. Review diagrams with mentor and approve interfaces
3. Implement first vertical slice: one gesture end-to-end
4. Add unit tests for parser and gesture logic
5. Demonstrate checkpoint 1 package
