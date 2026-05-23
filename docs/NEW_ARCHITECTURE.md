# GloveAssist - New Architecture: STM32 Lite + ESP32 Brain

**Date:** May 11, 2026  
**Version:** 2.0 - Edge Computing Architecture

---

## 🎯 PHILOSOPHY: "Thin Sensor Layer, Fat Brain Layer"

**OLD Architecture:** Both MCUs do processing → resource constrained  
**NEW Architecture:** STM32 = dumb sensor hub, ESP32 = smart brain → unlimited scaling

---

## 📊 ARCHITECTURE OVERVIEW

```
╔═══════════════════════════════════════════════════════════════════════╗
║                          SYSTEM ARCHITECTURE                          ║
╚═══════════════════════════════════════════════════════════════════════╝

┌─────────────────────────────┐        UART 115200        ┌─────────────────────────────┐
│   STM32F103C8 "LITE"        │◄──────────────────────────►│   ESP32-WROOM "BRAIN"       │
│   Sensor + Actuator Hub     │    Raw Data / Commands     │   Edge Computing Unit       │
└─────────────────────────────┘                            └─────────────────────────────┘
            │                                                           │
    ┌───────┴────────┐                                      ┌──────────┴──────────┐
    │                │                                      │                     │
┌───▼────┐    ┌──────▼──────┐                    ┌─────────▼──────┐   ┌─────────▼─────────┐
│  ADC   │    │  Actuators  │                    │  Processing    │   │  Connectivity     │
│ 4 flex │    │ • OLED      │                    │ • Filter       │   │ • BLE NUS         │
│sensors │    │ • Motor PWM │                    │ • Classifier   │   │ • WiFi/MQTT       │
│12-bit  │    │ • Buzzer    │                    │ • Calibration  │   │ • Cloud edge      │
│@ 50Hz  │    │ • LED       │                    │ • Security     │   │ • OTA updates     │
└────────┘    └─────────────┘                    │ • ML inference │   └───────────────────┘
                                                  └────────────────┘
    
    TASK: Acquire & Display                      TASK: Think & Decide
    RAM:  ~6 KB / 20 KB (30%)                    RAM:  ~60 KB / 96 KB (62%)
    Flash: ~28 KB / 128 KB (22%)                 Flash: ~800 KB / 4 MB (20%)
```

---

## 🔄 DATA FLOW

### **Sensing Path (STM32 → ESP32)**

```
┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐
│   ADC    │─50Hz─┤ Validate │─UART─┤  Filter  │──────┤ Classify │
│ 4 ch×12b │      │  Range   │      │ Moving   │      │ Gesture  │
└──────────┘      └──────────┘      │ Average  │      │ Logic    │
   STM32              STM32          └──────────┘      └──────────┘
                                         ESP32            ESP32
                                         
                 Raw ADC Frame              Filtered Data      Gesture ID
                 [AA][08][seq][            
                  ch0_h,ch0_l,              
                  ch1_h,ch1_l,
                  ch2_h,ch2_l,
                  ch3_h,ch3_l][crc]
```

### **Actuation Path (ESP32 → STM32)**

```
┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐
│ Gesture  │──────┤   BLE    │──────┤  Command │─UART─┤  Haptic  │
│ Detected │      │  Phone   │      │  Frame   │      │ Feedback │
└──────────┘      └──────────┘      └──────────┘      └──────────┘
   ESP32             ESP32              ESP32            STM32
   
                                    [AA][04][seq][
                                     cmd_id,
                                     gesture_id,
                                     intensity,
                                     duration,
                                     ...][crc]
```

---

## 📦 FRAME PROTOCOL V2

### **New Frame Types**

```c
// Existing (keep)
#define FRAME_TYPE_GESTURE      0x01  // ESP→STM: gesture detected
#define FRAME_TYPE_SENSOR_RAW   0x02  // STM→ESP: raw ADC values (NEW!)
#define FRAME_TYPE_STATUS       0x03  // Both: system status
#define FRAME_TYPE_COMMAND      0x04  // ESP→STM: actuator commands
#define FRAME_TYPE_HEARTBEAT    0x05  // ESP→STM: keepalive
#define FRAME_TYPE_SECURITY     0x06  // Both: security events

// New types
#define FRAME_TYPE_SENSOR_FILTERED 0x07  // ESP→STM: filtered values (for OLED display)
#define FRAME_TYPE_CALIBRATE_REQ   0x08  // ESP→STM: start calibration sequence
#define FRAME_TYPE_CALIBRATE_DATA  0x09  // STM→ESP: calibration samples
#define FRAME_TYPE_THRESH_UPDATE   0x0A  // ESP→STM: new thresholds after calibration
```

### **SENSOR_RAW Payload (STM32 → ESP32)**

```
Byte    Content         Description
----    -------         -----------
0       ch0_h           Flex sensor 0 ADC high byte
1       ch0_l           Flex sensor 0 ADC low byte
2       ch1_h           Flex sensor 1 ADC high byte
3       ch1_l           Flex sensor 1 ADC low byte
4       ch2_h           Flex sensor 2 ADC high byte
5       ch2_l           Flex sensor 2 ADC low byte
6       ch3_h           Flex sensor 3 ADC high byte
7       ch3_l           Flex sensor 3 ADC low byte

Total: 8 bytes (fits in 12-byte frame)
Rate: 50 Hz → 600 bytes/sec (5% of 115200 baud)
```

### **SENSOR_FILTERED Payload (ESP32 → STM32, optional)**

```
Same format as SENSOR_RAW, but with filtered values.
STM32 uses this only for OLED display (shows smooth graphs).
```

---

## 💾 MEMORY FOOTPRINT COMPARISON

### **STM32 - Before vs After**

| Component | OLD (v1.0) | NEW (v2.0 Lite) | Saved |
|-----------|------------|-----------------|-------|
| **Flash** | 49 KB | ~28 KB | **21 KB** |
| filter_buf, filter_sum | 80 bytes | 0 | 80 bytes |
| classify_gesture() | 2 KB | 0 | 2 KB |
| calibration.c | 6 KB | 1 KB (stub) | 5 KB |
| security.c (complex) | 3 KB | 0.5 KB (basic) | 2.5 KB |
| haptic_ui.c (OLED driver) | 4 KB | 4 KB | 0 |
| **Total RAM** | 9.3 KB | ~6 KB | **3.3 KB** |
| **Total Flash** | 49 KB | ~28 KB | **21 KB** |

### **ESP32 - Before vs After**

| Component | OLD (v1.0) | NEW (v2.0 Brain) | Added |
|-----------|------------|------------------|-------|
| **Flash** | 368 KB | ~550 KB | +182 KB |
| sensor_filter.c | 0 | 2 KB | +2 KB |
| gesture_classify.c | 0 | 3 KB | +3 KB |
| calibration_engine.c | 0 | 8 KB | +8 KB |
| security_full.c | 1 KB | 4 KB | +3 KB |
| ml_inference.c (stub) | 0 | 5 KB | +5 KB |
| **Total RAM** | 21.3 KB | ~40 KB | +18.7 KB |
| **Total Flash** | 368 KB | ~550 KB | +182 KB |

**Result:**  
- STM32: **99 KB flash FREE** (was 78 KB)
- ESP32: **3.45 MB flash FREE** (was 3.63 MB) - still HUGE space for ML models!

---

## 🧩 MODULE RESPONSIBILITIES

### **STM32 Modules (Simplified)**

| Module | File | Responsibility | Lines |
|--------|------|----------------|-------|
| Main | `main.c` | Boot, threads, minimal init | 150 |
| ADC Driver | `adc_driver.c` | Read 4 channels @ 50Hz | 120 |
| UART Comm | `uart_comm.c` | Send raw frames, receive commands | 200 |
| Haptic UI | `haptic_ui.c` | OLED + motor + buzzer control | 300 |
| Safety Basic | `safety_basic.c` | Watchdog, timeout detection | 100 |
| **Total** | **5 files** | **Thin layer** | **~870 LOC** |

### **ESP32 Modules (Expanded)**

| Module | File | Responsibility | Lines |
|--------|------|----------------|-------|
| Main | `main.c` | Boot, threads, orchestration | 200 |
| UART Comm | `uart_comm.c` | Receive raw, send commands | 250 |
| Sensor Filter | `sensor_filter.c` | Moving average per channel | 150 |
| Gesture Classifier | `gesture_classify.c` | Simple + ML inference | 300 |
| Calibration Engine | `calibration_engine.c` | Auto-calibrate, adaptive | 400 |
| Security Full | `security_full.c` | Honeypot, lockdown, rate-limit | 300 |
| BLE NUS | `comms_ble.c` | Phone gateway | 400 |
| WiFi MQTT | `wifi_mqtt.c` | Cloud publishing | 500 |
| ML Inference | `ml_inference.c` | TensorFlow Lite Micro (stub) | 200 |
| **Total** | **9 files** | **Smart brain** | **~2700 LOC** |

---

## 🚀 IMPLEMENTATION PHASES

### **Phase 1: Raw Data Streaming (Week 1)**

**Goal:** STM32 sends raw ADC, ESP32 filters and classifies.

**Changes:**
1. ✅ Define `FRAME_TYPE_SENSOR_RAW` in `frame_protocol.h`
2. ✅ STM32: Modify `sensor_logic.c` → remove filter, send raw ADC
3. ✅ ESP32: Create `sensor_filter.c` → receive raw, apply moving average
4. ✅ ESP32: Move `classify_gesture()` from STM32
5. ✅ ESP32: Send `FRAME_TYPE_GESTURE` back to STM32
6. ✅ STM32: Receive gesture ID → trigger haptic feedback

**Test:** Verify gestures still work, check latency (<50ms)

---

### **Phase 2: Calibration Migration (Week 2)**

**Goal:** ESP32 handles all calibration logic.

**Changes:**
1. ESP32: Create `calibration_engine.c` with adaptive algorithm
2. ESP32: Store thresholds in ESP32 NVS (not STM32)
3. ESP32: Send `FRAME_TYPE_THRESH_UPDATE` to STM32 after calibration
4. STM32: Replace `calibration.c` with stub that receives thresholds
5. BLE: Add "Calibrate Now" command from phone

**Test:** Trigger calibration via BLE, verify thresholds update

---

### **Phase 3: Advanced Security (Week 3)**

**Goal:** ESP32 becomes security gatekeeper.

**Changes:**
1. ESP32: Implement full honeypot detection
2. ESP32: Rate limiting on UART/BLE
3. ESP32: Send `CMD_LOCKDOWN` to STM32 if attack detected
4. STM32: Minimal security (only CRC + sequence)

**Test:** Inject invalid frames, verify lockdown

---

### **Phase 4: ML Preparation (Week 4)**

**Goal:** Add TensorFlow Lite Micro stub, test inference pipeline.

**Changes:**
1. ESP32: Integrate TFLite Micro library
2. ESP32: Create `ml_inference.c` with dummy model
3. ESP32: Pipeline: raw → filter → feature extraction → inference → gesture
4. Train simple model (FFN, 4 inputs → 7 outputs) offline

**Test:** Deploy dummy model, verify it runs

---

### **Phase 5: Production ML (Month 2)**

**Goal:** Real ML model for complex gestures.

**Tasks:**
1. Collect training data (100+ samples per gesture)
2. Train model (TensorFlow → TFLite conversion)
3. Deploy to ESP32 via OTA
4. Add new gesture types (swipe, pinch, rotate)

---

## 📡 LATENCY ANALYSIS

### **OLD Architecture (v1.0)**

```
ADC sample (2ms) → Filter (0.1ms) → Classify (0.2ms) → Haptic (1ms)
Total: ~3.3ms
```

### **NEW Architecture (v2.0)**

```
ADC sample (2ms) → UART TX (1ms) → ESP Filter (0.1ms) → 
ESP Classify (0.5ms) → UART RX (1ms) → Haptic (1ms)
Total: ~5.6ms
```

**Delta: +2.3ms**  
**Still well under 50ms threshold for real-time feedback ✅**

---

## ⚠️ RISK MITIGATION

### **Risk 1: ESP32 Crash → System Dead**

**Mitigation:**
- STM32 watchdog detects no heartbeat → enters "safe mode"
- Safe mode: uses last known thresholds, disables WiFi commands
- OLED shows "ESP32 offline" warning
- Buzzer beeps periodically

### **Risk 2: UART Congestion**

**Mitigation:**
- Monitor UART buffer usage
- Reduce sample rate to 25Hz if buffer >80% full
- Priority queue: commands > heartbeat > raw data

### **Risk 3: Debugging Harder**

**Mitigation:**
- Unified logging format across both MCUs
- Frame tracer tool (Python script) to decode UART traffic
- Remote debug via BLE (send "dump state" command)

---

## 🎓 LESSONS LEARNED (Post-Implementation)

*[To be filled after Phase 1 testing]*

---

## 🔮 FUTURE EXTENSIONS (Enabled by This Architecture)

1. **Multi-Sensor Support**
   - Add IMU (MPU6050) on STM32 → ESP32 fuses flex + accel data
   - Add pressure sensors on fingertips
   - ESP32 does sensor fusion

2. **Cloud Edge Computing**
   - ESP32 preprocesses data locally → sends features (not raw) to cloud
   - Reduces bandwidth by 10x
   - Enables fleet-wide learning

3. **Personalized Models**
   - ESP32 stores per-user ML models in flash (4MB available!)
   - Switch model based on BLE connection (phone MAC address)

4. **Gesture Recording**
   - Record gesture sequences to SD card (via ESP32 SPI)
   - Replay for training or debugging

5. **Multi-Glove Sync**
   - Two gloves (left + right) → both send data to single ESP32
   - ESP32 does bimanual gesture recognition

---

## 📚 REFERENCES

- Zephyr RTOS: Inter-processor communication patterns
- TensorFlow Lite Micro: ESP32 examples
- Edge ML: "TinyML" book by Pete Warden

---

**Status:** Architecture Defined ✅  
**Next Step:** Implement Phase 1 (Raw Data Streaming)
