# IEC-Oriented Safety Plan

## Scope
This project is educational, but the development flow follows IEC-inspired discipline for hazard awareness and traceability.

## Safety goals
SG-001: Avoid unsafe outputs caused by corrupted sensor input.
SG-002: Recover from software lockup using watchdog.
SG-003: Maintain predictable communication behavior under frame errors.

## Initial hazards
H-001: False gesture detection due to sensor drift.
H-002: Stale BLE data due to communication timeout.
H-003: MCU freeze during demo operation.

## Mitigations
M-001: calibration and drift checks.
M-002: heartbeat and timeout handling.
M-003: watchdog and safe-state fallback.

## Verification links
- timeout tests
- CRC corruption tests
- watchdog recovery test
