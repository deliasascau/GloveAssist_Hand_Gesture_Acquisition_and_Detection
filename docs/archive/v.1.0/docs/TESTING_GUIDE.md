# GloveAssist - Testing Guide

**Date:** May 11, 2026  
**Version:** 1.0 - Post-cleanup baseline

## 📋 Pre-Test Checklist

### Hardware Setup
- [ ] STM32F103C8 Blue Pill connected via ST-Link v2
- [ ] ESP32-WROOM DevKit connected via USB (CP2102 driver installed)
- [ ] 4× flex sensors wired to STM32 PA0-PA3 with 330Ω pull-down resistors
- [ ] OLED SSD1306 128×64 connected to STM32 I2C2 (PB10 SCL, PB11 SDA)
- [ ] Motor vibration connected to STM32 PB6 via ULN2003
- [ ] Active buzzer connected to STM32 PB4
- [ ] UART cross-wired: STM32 PA9(TX)→ESP32 GPIO16(RX), ESP32 GPIO17(TX)→STM32 PA10(RX)
- [ ] Common GND between STM32 and ESP32
- [ ] Power: STM32 via ST-Link 3.3V, ESP32 via USB 5V

### Software Setup
- [ ] Build STM32 firmware (49 KB flash, 9.3 KB RAM)
- [ ] Build ESP32 firmware (368 KB signed + 39 KB MCUboot)
- [ ] Install nRF Connect app on Android/iOS for BLE testing
- [ ] (Optional) Install MQTT client for cloud testing
- [ ] Serial terminal ready (e.g., PuTTY, Tera Term) at 115200 baud

## 🧪 Test Cases

### Test 1: STM32 Boot & Initialization
**Objective:** Verify STM32 boots correctly and initializes all peripherals.

**Steps:**
1. Flash STM32 using OpenOCD (adapter speed 50 kHz):
   ```powershell
   $oocd = "C:\Users\Lenovo\.platformio\packages\tool-openocd\bin\openocd.exe"
   $scripts = "C:\Users\Lenovo\.platformio\packages\tool-openocd\openocd\scripts"
   & $oocd -f "$scripts\interface\stlink.cfg" -f "$scripts\target\stm32f1x.cfg" `
     -c "adapter speed 50" -c "reset_config none" -c "init" -c "reset halt" `
     -c "program C:\zephyr-workspace\build-glove-stm32\zephyr\zephyr.hex verify reset exit"
   ```
2. Connect serial terminal to STM32 (if logging enabled)
3. Power cycle STM32

**Expected Results:**
- [ ] No flash errors ("** Verified OK **")
- [ ] OLED displays initialization message
- [ ] Buzzer beeps once (if implemented)
- [ ] Log shows: "calibration_init", "Praguri active: I=... M=... R=... P=..."
- [ ] Log shows: "Sensor thread started"

**Actual Results:**
```
[Write your observations here]
```

---

### Test 2: ESP32 Boot & BLE Advertising
**Objective:** Verify ESP32 boots, BLE starts, and device is discoverable.

**Steps:**
1. Flash ESP32:
   ```powershell
   $env:ZEPHYR_BASE='C:\zephyr-workspace\zephyr'
   $env:PATH='C:\Users\Lenovo\OneDrive\Desktop\GLOVE-STM32-ESP32\.venv\Scripts;C:\zephyr-workspace\.venv\Scripts;' + $env:PATH
   C:\zephyr-workspace\.venv\Scripts\python.exe -m west flash `
     --build-dir 'C:\zephyr-workspace\build-glove-esp32' `
     --esp-device COM11 --esp-baud-rate 460800
   ```
2. Open nRF Connect app
3. Scan for BLE devices

**Expected Results:**
- [ ] Flash completes without errors
- [ ] Device "GloveAssist" appears in BLE scan
- [ ] RSSI shows strong signal (>-70 dBm if close)
- [ ] Can connect to device

**Actual Results:**
```
[Write your observations here]
```

---

### Test 3: UART Communication (Heartbeat)
**Objective:** Verify UART link between STM32 and ESP32.

**Steps:**
1. Both MCUs powered and running
2. Monitor STM32 serial log (if available)
3. Monitor ESP32 serial log via USB

**Expected Results:**
- [ ] ESP32 log shows: "UART TX heartbeat: seq=..." every 500ms
- [ ] STM32 log shows: "ESP32 heartbeat received" periodically
- [ ] No CRC errors or invalid frames logged

**Actual Results:**
```
[Write your observations here]
```

---

### Test 4: Flex Sensor ADC Reading
**Objective:** Verify ADC reads flex sensors correctly.

**Steps:**
1. Bend each finger one at a time
2. Observe OLED display or serial log

**Expected Results:**
- [ ] Index finger: ADC value changes from ~1195 (open) to ~47 (bent)
- [ ] Middle finger: ADC value changes from ~1695 to ~53
- [ ] Ring finger: ADC value changes from ~3671 to ~347
- [ ] Pinky finger: ADC value changes from ~1845 to ~43
- [ ] No sensor faults logged

**Actual Results:**
```
Finger | Open ADC | Bent ADC | Notes
-------|----------|----------|-------
Index  |          |          |
Middle |          |          |
Ring   |          |          |
Pinky  |          |          |
```

---

### Test 5: Gesture Classification
**Objective:** Verify gesture classifier detects correct gestures.

**Steps:**
1. Perform each gesture and hold for 2 seconds
2. Observe OLED display and/or BLE notifications

**Gestures to test:**
- FIST: all 4 fingers bent
- INDEX: only index finger bent
- MIDDLE: only middle finger bent
- RING: only ring finger bent
- PINKY: only pinky finger bent
- HELP: ring + pinky bent, index + middle open
- NONE: all fingers open

**Expected Results:**
- [ ] Each gesture detected after 2s hold time
- [ ] OLED shows gesture name
- [ ] Haptic feedback triggered (motor vibration)
- [ ] BLE notification sent to phone (if connected)

**Actual Results:**
```
Gesture | Detected? | Delay (s) | Haptic OK? | BLE OK? | Notes
--------|-----------|-----------|------------|---------|-------
FIST    |           |           |            |         |
INDEX   |           |           |            |         |
MIDDLE  |           |           |            |         |
RING    |           |           |            |         |
PINKY   |           |           |            |         |
HELP    |           |           |            |         |
NONE    |           |           |            |         |
```

---

### Test 6: Auto-Calibration (Fist-Hold 20s)
**Objective:** Verify automatic calibration trigger.

**Steps:**
1. Ensure all fingers are in neutral position
2. Make a FIST and hold for 20 seconds
3. Follow on-screen instructions if OLED displays prompts
4. Expected: Phase 1 (open) → Phase 2 (bent) → save to NVS

**Expected Results:**
- [ ] After 20s fist hold, calibration starts automatically
- [ ] OLED/log shows: "CALIBRARE START"
- [ ] Phase 1: "tine toate degetele DREPTE — 5 secunde"
- [ ] Phase 2: "tine toate degetele INDOITE — 5 secunde"
- [ ] Log shows: "PRAG CALCULAT: I=... M=... R=... P=..."
- [ ] Log shows: "CALIBRARE COMPLETA — praguri salvate in NVS"
- [ ] Haptic feedback confirms success

**Actual Results:**
```
[Write your observations here]
```

---

### Test 7: BLE Command - Calibrate
**Objective:** Verify BLE command triggers calibration on STM32.

**Steps:**
1. Connect to "GloveAssist" via nRF Connect
2. Enable notifications on NUS TX characteristic (UUID 0x6E400003)
3. Write to NUS RX characteristic (UUID 0x6E400002):
   - Hex: `01` (CMD_CALIBRATE)
4. Follow calibration prompts

**Expected Results:**
- [ ] ESP32 relays command to STM32
- [ ] Calibration starts immediately
- [ ] BLE notification confirms: "Calibration started"
- [ ] Same behavior as Test 6

**Actual Results:**
```
[Write your observations here]
```

---

### Test 8: BLE Command - ACK (Text Shortcut)
**Objective:** Verify text shortcut mapping to CMD_ACK_RECEIVED.

**Steps:**
1. Send gesture from STM32 (e.g., INDEX)
2. BLE notification sent to phone
3. Send text "inteles" or "ack" or "ok" from phone to NUS RX

**Expected Results:**
- [ ] ESP32 logs: "BLE text ACK -> CMD_ACK_RECEIVED relayed to STM32"
- [ ] STM32 receives ACK and stops repeating gesture notifications
- [ ] OLED shows "ACK received" or similar

**Actual Results:**
```
[Write your observations here]
```

---

### Test 9: NVS Persistence (Reboot Test)
**Objective:** Verify calibration data survives power cycle.

**Steps:**
1. Perform calibration (Test 6 or 7)
2. Note down calculated thresholds from log
3. Power cycle STM32
4. Check log on boot

**Expected Results:**
- [ ] Log shows: "Praguri active: I=... M=... R=... P=..."
- [ ] Values match previous calibration (±5 tolerance)
- [ ] No "NVS empty" or "using defaults" message

**Actual Results:**
```
Previous thresholds: I=___ M=___ R=___ P=___
After reboot:        I=___ M=___ R=___ P=___
Match? YES / NO
```

---

### Test 10: Haptic Feedback (Motor + Buzzer)
**Objective:** Verify haptic UI responds correctly.

**Steps:**
1. Perform gestures and trigger different events:
   - Gesture detected → motor vibration pattern
   - Error condition → buzzer beep
   - Calibration complete → buzzer + motor
2. Observe motor and buzzer behavior

**Expected Results:**
- [ ] Motor vibrates on gesture detection (PB6 PWM)
- [ ] Buzzer beeps on error (PB4 GPIO)
- [ ] Different patterns for different events

**Actual Results:**
```
Event                | Motor OK? | Buzzer OK? | Notes
---------------------|-----------|------------|-------
Gesture detected     |           |            |
Calibration start    |           |            |
Calibration complete |           |            |
Error condition      |           |            |
```

---

### Test 11: OLED Display
**Objective:** Verify OLED shows correct information.

**Steps:**
1. Power on STM32
2. Perform gestures
3. Trigger calibration
4. Trigger error (disconnect sensor)

**Expected Results:**
- [ ] Boot message displayed
- [ ] Current gesture name displayed
- [ ] ADC values displayed (if implemented)
- [ ] Calibration prompts displayed
- [ ] Error messages displayed

**Actual Results:**
```
Screen          | Content OK? | Readable? | Notes
----------------|-------------|-----------|-------
Boot screen     |             |           |
Gesture display |             |           |
Calibration UI  |             |           |
Error screen    |             |           |
```

---

### Test 12: Safety - Heartbeat Loss
**Objective:** Verify STM32 detects ESP32 heartbeat timeout.

**Steps:**
1. Both MCUs running normally
2. Disconnect ESP32 power or UART wire
3. Wait 3 seconds (timeout)

**Expected Results:**
- [ ] STM32 log shows: "ESP32 heartbeat timeout"
- [ ] Local alarm triggered (buzzer beep or motor)
- [ ] OLED shows warning
- [ ] System enters safe mode (stops sending data)

**Actual Results:**
```
[Write your observations here]
```

---

### Test 13: Security - Invalid Frame Detection
**Objective:** Verify security layer detects corrupted frames.

**Steps:**
1. (Manual test via code modification or hardware fault injection)
2. Send frame with wrong CRC
3. Send frame with invalid sequence number
4. Send frame with SOF != 0xAA

**Expected Results:**
- [ ] Invalid frames logged
- [ ] Honeypot counter increments
- [ ] After N invalid frames → lockdown mode
- [ ] No system crash or undefined behavior

**Actual Results:**
```
[Write your observations here]
```

---

### Test 14: WiFi + MQTT Publishing (Optional Cloud Profile)
**Objective:** Verify ESP32 connects to WiFi and publishes to MQTT broker.

**Prerequisites:**
- Build ESP32 with cloud profile: `-DEXTRA_CONF_FILE=prj_wifi_mqtt.conf`
- Update `app_config.h` with WiFi credentials and Adafruit IO key
- Flash ESP32

**Steps:**
1. Power on ESP32
2. Monitor serial log
3. Check Adafruit IO dashboard

**Expected Results:**
- [ ] Log shows: "WiFi connected, IP: ..."
- [ ] Log shows: "MQTT connected to io.adafruit.com:8883"
- [ ] Gestures appear on Adafruit IO feed
- [ ] TLS handshake successful

**Actual Results:**
```
[Write your observations here]
```

---

### Test 15: OTA Firmware Update (ESP32)
**Objective:** Verify OTA update via BLE works.

**Prerequisites:**
- MCUboot enabled (default in prj.conf)
- Install mcumgr CLI tool or use nRF Connect Device Manager app

**Steps:**
1. Connect to ESP32 via BLE
2. Upload new signed firmware image via SMP (Simple Management Protocol)
3. Reboot ESP32

**Expected Results:**
- [ ] MCUboot validates signature
- [ ] New image boots successfully
- [ ] Version number incremented (if implemented)
- [ ] System fully functional after update

**Actual Results:**
```
[Write your observations here]
```

---

### Test 16: Watchdog Timer
**Objective:** Verify watchdog resets system on hang.

**Steps:**
1. (Code modification required) Inject infinite loop in sensor thread
2. Flash and run
3. Wait for watchdog timeout

**Expected Results:**
- [ ] System resets after watchdog timeout (~5-10s)
- [ ] Log shows reset reason (if implemented)
- [ ] System recovers and continues normal operation

**Actual Results:**
```
[Write your observations here]
```

---

### Test 17: Flash Memory Usage
**Objective:** Verify both MCUs fit within memory constraints.

**STM32 Limits:**
- Flash: 128 KB
- RAM: 20 KB

**ESP32 Limits:**
- Flash: 4 MB
- IRAM: 128 KB
- DRAM: 96 KB (effective, after BLE/WiFi stack)

**Actual Usage:**
- STM32: 49 KB flash (38%), 9.3 KB RAM (47%) ✅
- ESP32: 368 KB app + 39 KB MCUboot, 28.5 KB IRAM, 21.3 KB DRAM ✅

**Pass Criteria:**
- [ ] STM32 flash < 120 KB (leaving 8 KB buffer)
- [ ] STM32 RAM < 18 KB (leaving 2 KB buffer)
- [ ] ESP32 app < 1.5 MB (plenty of room for OTA dual-bank)
- [ ] ESP32 DRAM < 80 KB (leaving buffer for runtime allocation)

---

## 🐛 Fault Injection Tests (Advanced)

### FI-1: Sensor Disconnect
1. Disconnect one flex sensor wire during operation
2. Verify ADC reads out-of-range value
3. Verify safety subsystem logs fault
4. Verify system continues with degraded mode (3 sensors)

### FI-2: I2C Bus Hang
1. Short SDA or SCL line momentarily
2. Verify I2C driver recovers or times out gracefully
3. Verify OLED updates stop but system doesn't crash
4. Verify log shows I2C error

### FI-3: UART Noise
1. Send random bytes on UART at high rate
2. Verify CRC/sequence validation rejects invalid frames
3. Verify honeypot detection triggers after N failures
4. Verify system enters lockdown mode

### FI-4: Flash Corruption (NVS)
1. Erase NVS partition manually via OpenOCD
2. Reboot STM32
3. Verify system detects empty NVS
4. Verify default thresholds loaded
5. Verify calibration can re-populate NVS

### FI-5: Low Voltage (Brown-out)
1. Gradually reduce supply voltage to 2.8V
2. Verify brown-out detector resets system (if enabled)
3. Verify system recovers when voltage restored to 3.3V

---

## 📊 Test Summary Template

| Test # | Test Name | Status | Notes |
|--------|-----------|--------|-------|
| 1 | STM32 Boot | ⬜ PASS / ⬜ FAIL | |
| 2 | ESP32 BLE | ⬜ PASS / ⬜ FAIL | |
| 3 | UART Heartbeat | ⬜ PASS / ⬜ FAIL | |
| 4 | Flex Sensor ADC | ⬜ PASS / ⬜ FAIL | |
| 5 | Gesture Classification | ⬜ PASS / ⬜ FAIL | |
| 6 | Auto-Calibration | ⬜ PASS / ⬜ FAIL | |
| 7 | BLE Calibrate Cmd | ⬜ PASS / ⬜ FAIL | |
| 8 | BLE ACK Shortcut | ⬜ PASS / ⬜ FAIL | |
| 9 | NVS Persistence | ⬜ PASS / ⬜ FAIL | |
| 10 | Haptic Feedback | ⬜ PASS / ⬜ FAIL | |
| 11 | OLED Display | ⬜ PASS / ⬜ FAIL | |
| 12 | Heartbeat Loss | ⬜ PASS / ⬜ FAIL | |
| 13 | Security - Invalid Frame | ⬜ PASS / ⬜ FAIL | |
| 14 | WiFi/MQTT (optional) | ⬜ PASS / ⬜ FAIL / ⬜ SKIP | |
| 15 | OTA Update | ⬜ PASS / ⬜ FAIL | |
| 16 | Watchdog Timer | ⬜ PASS / ⬜ FAIL | |
| 17 | Memory Constraints | ⬜ PASS / ⬜ FAIL | |

**Overall Status:** ⬜ ALL PASS / ⬜ SOME FAILURES  
**Tested By:** _______________  
**Date:** _______________

---

## 📝 Notes

- Always test with **real hardware** - emulators don't catch timing or electrical issues.
- Use **serial logging** liberally during development; disable for production builds to save flash/RAM.
- Test **calibration first** - it's the foundation for gesture detection.
- **Power cycle** between tests to ensure clean state.
- Document **any deviations** from expected results for debugging.

---

## 🚀 Next Steps After Testing

1. ✅ All tests pass → **DEPLOY** to production glove
2. ⚠️ Some failures → **DEBUG** and retest
3. 📊 Performance issues → **OPTIMIZE** (reduce stack sizes, disable verbose logging)
4. 📚 **DOCUMENT** final configuration in user manual
