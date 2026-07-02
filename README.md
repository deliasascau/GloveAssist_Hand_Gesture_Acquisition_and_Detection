# GloveAssist - Dual MCU Embedded Project

![CI](https://github.com/deliasascau/GloveAssist_Hand_Gesture_Acquisition_and_Detection/actions/workflows/ci.yml/badge.svg)

## Purpose
GloveAssist is a bare-metal C embedded system with dual MCU architecture:
- STM32F103 (sensor acquisition, local processing, deterministic control)
- ESP32 (BLE communication, data storage, mobile interface)

The project is developed from zero and follows an engineering workflow:
requirements, features, architecture diagrams, implementation, tests, documentation, and CI/CD.

## Active Codebase
The active firmware code is in `application/`.

The old `v.1.0` snapshot is archived in `docs/archive/v.1.0/` and is kept only for reference. Do not use it for current builds or feature work.

## Mandatory capability coverage
- Wireless protocols: BLE (ESP32), UART inter-MCU link
- Data processing: sensor filtering, calibration, gesture recognition
- Cryptography: BLE secure pairing + application-level message protection
- Sniffing: BLE packet capture and UART frame verification
- CI/CD: source control, automated checks, unit tests pipeline

## Repository structure
- application: active STM32, ESP32, and common firmware code
- docs: project engineering artifacts and archived snapshots
- tests: host and integration test assets
- tools: local development and support tooling
- .github/workflows: CI pipelines

## Current status
Project bootstrap completed:
- engineering documents initialized
- architecture diagrams initialized
- git standards and CI pipeline initialized

## Next execution steps
1. Freeze current requirements and feature scope
2. Review diagrams with mentor and approve interfaces
3. Implement first vertical slice: one gesture end-to-end
4. Add unit tests for parser and gesture logic
5. Demonstrate checkpoint 1 package
