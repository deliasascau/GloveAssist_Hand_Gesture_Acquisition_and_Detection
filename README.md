# GloveAssist - Dual MCU Embedded Project

![CI](https://github.com/deliasascau/GloveAssist_Hand_Gesture_Acquisition_and_Detection/actions/workflows/ci.yml/badge.svg)

## Purpose
GloveAssist is a Zephyr-based dual MCU embedded system:
- STM32F103: deterministic sensor acquisition, ADC fault monitoring, local OLED/buzzer/motor/LED feedback
- ESP32: secure UART session, gesture recognition, BLE, WiFi, MQTT/TLS, calibration and cloud publishing

The project is developed from zero and follows an engineering workflow:
requirements, features, architecture diagrams, implementation, tests, documentation, and CI/CD.

## Active Codebase
The active firmware code is in `application/`.

The technical project documentation is in `docs/project_documentation.md`.
Testing strategy, design diagrams, and presentation notes are in:
- `docs/complete_project_explainer.md`
- `docs/deep_dive/README.md`
- `docs/testing_strategy.md`
- `docs/system_design.md`
- `docs/presentation_notes.md`
- `docs/star_ardei_presentation.md`

## Mandatory capability coverage
- Wireless protocols: BLE, WiFi/MQTT, secure UART inter-MCU link
- Data processing: ADC acquisition, ADC fault monitoring, calibration, gesture recognition
- Cryptography: BLE MITM passkey pairing, MQTT/TLS, AES-CTR + HMAC UART frames
- Power: battery/power budget analysis for the two-cell parallel Li-ion pack
- Verification: unit tests, flash scripts, hardware E2E smoke test
- CI/CD: Docker-based build/test pipeline, self-hosted hardware jobs, GitHub Releases

## Repository structure
- application: active STM32, ESP32, and common firmware code
- docs: project documentation
- tests: host and integration test assets
- scripts: local development, flash, credential, and E2E support tooling
- .github/workflows: CI pipelines

## Current status
Presentation-ready firmware path:
- STM32 and ESP32 firmware build successfully in CI
- unit tests validate the shared frame protocol
- hardware E2E validates UART handshake, BLE advertising, WiFi IPv4, MQTT connect and MQTT publish
- releases are generated from `v*` tags with STM32/ESP32 binaries attached

## Next execution steps
1. Create a `v1.0.0` tag for the final release package
2. Keep `vars.RUN_HARDWARE_TESTS=true` only when the boards are connected
3. Use `docs/project_documentation.md` as the main technical documentation for presentation/review
