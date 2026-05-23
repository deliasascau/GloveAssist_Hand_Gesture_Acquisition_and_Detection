# GloveAssist - v2.0 Architecture

Smart assistive glove firmware built on Zephyr RTOS for two MCUs with **distributed processing**:

- **STM32F103C8 "Blue Pill" (SENSOR HUB LITE)**: real-time ADC acquisition (4 flex sensors @ 50Hz), UART raw data streaming, OLED display, haptic feedback (motor + buzzer), auto-calibration, NVS storage, basic security, watchdog.
- **ESP32-WROOM DevKit (BRAIN)**: signal processing (filtering + gesture classification), BLE NUS gateway, WiFi/MQTT TLS cloud publishing, OTA firmware updates (MCUboot), heartbeat monitoring, advanced security, **ready for TinyML integration**.

**Inter-MCU communication**: UART (115200 baud, interrupt-driven) using 12-byte frame protocol with rolling counter anti-replay protection.

## 📊 Current Build Status (May 11, 2026 - v2.0)

| MCU | Flash Used | RAM Used | Features | ML Ready | Build Time |
|-----|------------|----------|----------|----------|------------|
| **STM32F103** | 48.4 KB / 128 KB (37.8%) | 9.2 KB / 20 KB (46.2%) | Sensor hub LITE | - | ~31 sec |
| **ESP32** | 378 KB / 4 MB (9%) | 75 KB DRAM | Brain + BLE + OTA | ✅ **3.67 MB free** | ~87 sec |

✅ **79.6 KB flash available on STM32** for safety/security features  
✅ **3.67 MB flash available on ESP32** for TensorFlow Lite Micro + complex ML models  
✅ **OTA firmware updates fully implemented** on ESP32 with MCUboot

## Architecture v2.0: "Thin Sensor Layer, Fat Brain Layer"

**Philosophy**: STM32 focuses on real-time ADC acquisition, ESP32 handles all computation

```text
┌─────────────────────────────────────┐
│      STM32 (Sensor Hub LITE)        │
├─────────────────────────────────────┤
│  • ADC read (50 Hz)                 │
│  • Build MSG_TYPE_RAW_ADC           │───┐
│  • UART send → ESP32                │   │
│                                     │   │ UART 115200
│  ◄───────────────────────────────── │   │ 12-byte frames
│  • Receive MSG_TYPE_GESTURE         │   │
│  • Haptic feedback trigger          │   │
│  • OLED display update              │   │
└─────────────────────────────────────┘   │
                                          │
┌─────────────────────────────────────┐   │
│        ESP32 (Brain)                │ ◄─┘
├─────────────────────────────────────┤
│  • Receive MSG_TYPE_RAW_ADC         │
│  • Apply moving average filter      │
│  • Classify gesture (rule-based)    │
│  • Send MSG_TYPE_GESTURE → STM32    │───┘
│  • Forward to BLE NUS               │
│  • Publish to MQTT (optional)       │
│  • [Phase 2] Calibration engine     │
│  • [Phase 3] Security honeypot      │
│  • [Phase 4] TinyML inference       │
└─────────────────────────────────────┘
```

**Benefits**:
- **3.67 MB free on ESP32** → ready for CNN/RNN gesture models
- **79 KB free on STM32** → space for advanced safety features
- **Latency**: ~3.7 ms end-to-end (well under 50 ms human perception)
- **Scalability**: Easy to add ML features without touching STM32

## Current Architecture (v1.0 → v2.0 Migration)

### OLD (v1.0): Local Processing on STM32
```text
Flex sensors -> STM32 ADC -> filter -> classifier -> UART frame -> ESP32
                         ↓ (all processing on STM32)
                      haptic feedback

ESP32: Just a "dumb gateway" for BLE/MQTT
```

### NEW (v2.0): Distributed Processing
```text
Flex sensors -> STM32 ADC (RAW) ──UART──> ESP32 filter/classifier ──UART──> STM32 haptic
                                              │
                                              ├──> BLE NUS notifications
                                              └──> WiFi/MQTT cloud publishing

ESP32: "Smart brain" with 3.67 MB for ML models
STM32: "Thin sensor hub" focused on real-time acquisition
```

## Project Structure (Cleaned - May 2026 v2.0)

```text
GLOVE-STM32-ESP32/
  application/
    common/                 shared headers, frame protocol, types
      include/              common_types.h, frame_protocol.h, app_config.h
      src/                  frame_protocol.c (CRC, validation)
    stm32_app/              STM32F103 firmware (Zephyr 4.4) - SENSOR HUB LITE
      src/                  main.c, sensor_logic.c (LITE), calibration.c, 
                           haptic_ui.c, uart_comm.c, security.c, safety_diag.c
      include/              module headers
      boards/               stm32_min_dev.overlay (I2C2, USART1, ADC, GPIO)
      prj.conf              Kconfig (optimized for 64KB target)
    esp32_app/              ESP32 firmware (Zephyr 4.4) - BRAIN
      src/                  main.c, uart_comm.c, comms_ble.c, wifi_mqtt.c,
                           sensor_filter.c (NEW v2.0), gesture_classify.c (NEW v2.0)
      include/              module headers, sensor_filter.h, gesture_classify.h
      boards/               esp32_devkitc_procpu.overlay (UART, BLE, WiFi)
      sysbuild/             mcuboot.conf (OTA configuration)
      prj.conf              Kconfig
  docs/                     documentation
    NEW_ARCHITECTURE.md     Phase 1-4 design doc (v2.0)
    PHASE1_IMPLEMENTATION_LOG.md  Code changes log
    PHASE1_BUILD_RESULTS.md       Build metrics + analysis
    TESTING_GUIDE.md              17 test cases + fault injection
    build_notes_2026-05-10.md    Build history
    esp32_app/              ESP32 gateway firmware (Zephyr 4.4)
      src/                  main.c, comms_ble.c, uart_comm.c, wifi_mqtt.c, nvs_calib.c
      include/              module headers
      boards/               esp32_devkitc_procpu.overlay (UART2, GPIO)
      prj.conf              BLE-only profile
      prj_wifi_mqtt.conf    Cloud profile (optional)
      sysbuild/mcuboot.conf MCUboot configuration for OTA
    tests/                  archived test apps (reference only)
  docs/
    01_requirements/        original requirements
    phases/                 implementation log by phase
    PROJECT_CLARIFICATION_RO.md
    hardware_connections.md wiring diagrams
    fmea_analysis.md        failure modes & safety
    archive/                reference documents
  tools/                    helper scripts
  lib/                      external libraries (if any)
  .vscode/tasks.json        build & flash commands
  backup_old_builds_*.zip   archived old build artifacts (85 MB)
```

## Hardware Summary

| Component | Connection | Notes |
|-----------|------------|-------|
| 4× flex sensors | STM32 PA0-PA3 | ADC1 CH0-CH3, 12-bit, 330Ω resistors |
| OLED SSD1306 | STM32 I2C2 | PB10 SCL, PB11 SDA, address 0x3C, 128×64 |
| Motor vibration | STM32 PB6 | Software PWM through ULN2003 |
| Buzzer | STM32 PB4 | Active buzzer, GPIO on/off |
| STM32 ↔ ESP32 | UART | STM32 USART1 PA9(TX)/PA10(RX) ↔ ESP32 UART2 GPIO16(RX)/GPIO17(TX), 115200 baud |
| Phone gateway | ESP32 BLE NUS | Text notifications + binary commands, bidirectional |
| Cloud gateway | ESP32 WiFi/MQTT | Adafruit IO over TLS (optional profile) |

**Note:** USART3 on STM32 is disabled in overlay to avoid conflict with I2C2 on PB10/PB11.

## Build Environment

Expected local setup:

```text
C:\zephyr-workspace
C:\zephyr-workspace\.venv
C:\zephyr-workspace\zephyr
C:\zephyr-sdk-1.0.1
```

The project-local `.venv` is used first in `PATH` because it contains the working `esptool.exe`.

## Build Commands

PowerShell:

```powershell
$env:ZEPHYR_BASE = "C:\zephyr-workspace\zephyr"
$env:PATH = "C:\Users\Lenovo\OneDrive\Desktop\GLOVE-STM32-ESP32\.venv\Scripts;C:\zephyr-workspace\.venv\Scripts;" + $env:PATH
```

STM32:

```powershell
C:\zephyr-workspace\.venv\Scripts\python.exe -m west build -p always `
  -b "stm32_min_dev@blue" `
  "C:\Users\Lenovo\OneDrive\Desktop\GLOVE-STM32-ESP32\application\stm32_app" `
  --build-dir "C:\zephyr-workspace\build-glove-stm32"
```

ESP32:

```powershell
C:\zephyr-workspace\.venv\Scripts\python.exe -m west build -p always `
  --sysbuild `
  -b "esp32_devkitc/esp32/procpu" `
  "C:\Users\Lenovo\OneDrive\Desktop\GLOVE-STM32-ESP32\application\esp32_app" `
  --build-dir "C:\zephyr-workspace\build-glove-esp32"

# Optional: cloud profile (WiFi + MQTT/TLS)
C:\zephyr-workspace\.venv\Scripts\python.exe -m west build -p always `
  --sysbuild `
  -b "esp32_devkitc/esp32/procpu" `
  "C:\Users\Lenovo\OneDrive\Desktop\GLOVE-STM32-ESP32\application\esp32_app" `
  --build-dir "C:\zephyr-workspace\build-glove-esp32" `
  -- -DEXTRA_CONF_FILE=prj_wifi_mqtt.conf
```

Artifacts:

```text
C:\zephyr-workspace\build-glove-stm32\zephyr\zephyr.bin
C:\zephyr-workspace\build-glove-esp32\zephyr\zephyr.bin
```

## Tests

The unit tests target Zephyr `native_sim`, which is not supported by Zephyr POSIX architecture on Windows. Run them in Linux, WSL, or CI:

```bash
west build -p always -b native_sim tests --build-dir build-glove-tests
west build -t run --build-dir build-glove-tests
```

## Documentation

Start with:

- [Project clarification](docs/PROJECT_CLARIFICATION_RO.md)
- [Romanian project explanation](docs/EXPLICATIE_PROIECT_RO.md)
- [Hardware connections](docs/hardware_connections.md)
- [Functional requirements](docs/01_requirements/functional_requirements.md)
- [Non-functional requirements](docs/01_requirements/nonfunctional_requirements.md)
