# System Requirements - GloveAssist

## 1. Project title
GloveAssist Secure Gesture Glove

## 2. Final objective
Build a reliable glove that detects finger gestures and transmits them over BLE to a mobile client, with local feedback and persistent profiles.

## 3. Stakeholders
- Student team
- Mentor / evaluator
- End user (demo operator)

## 4. Functional requirements
FR-001: The system shall detect at least 5 predefined gestures.
FR-002: The system shall provide user calibration for each finger sensor.
FR-003: The system shall transmit recognized gestures over BLE within 150 ms.
FR-004: The system shall persist calibration and profile data on ESP32 storage.
FR-005: The system shall display device state on OLED.
FR-006: The system shall emit audible feedback for key events.
FR-007: The system shall use a robust UART protocol between STM32 and ESP32 with integrity check.
FR-008: The system shall support watchdog-based recovery.
FR-009: The system shall provide data export for sniffing and debug analysis.

## 5. Non-functional requirements
NFR-001: Deterministic sensor acquisition period <= 10 ms.
NFR-002: Gesture classification accuracy >= 90% on validation set.
NFR-003: System uptime during demo >= 30 minutes without crash.
NFR-004: Code style shall follow MISRA-oriented rules for C modules.
NFR-005: Documentation artifacts shall support traceability requirement->test.
NFR-006: CI pipeline shall execute on each push and pull request.

## 6. Constraints
C-001: Hardware: STM32F103 Blue Pill + ESP32 CH340C 30P.
C-002: Language: C (bare-metal oriented architecture).
C-003: Timeline: complete demonstrable system in one month.

## 7. Acceptance criteria
AC-001: End-to-end demo for at least 5 gestures is successful.
AC-002: BLE communication visible in sniffer with expected packet behavior.
AC-003: UART frames validated with CRC checks and error handling.
AC-004: CI status is green on main branch.
AC-005: Required documents and diagrams are complete and versioned.
