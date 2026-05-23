# ⚡ Non-Functional Requirements - GloveAssist v1.0

---

## 🚀 Performance

### NFR-PERF-001: Gesture Latency

**Requirement:**  
End-to-end latency sensor → BLE TX ≤ 200ms

**Measurement:**  
GPIO toggle at gesture detect → BLE characteristic notify sent

---

### NFR-PERF-002: Sensor Sampling

**Requirement:**  
ADC reads 4 sensors la 2 Hz (500ms) în demo; arhitectura FreeRTOS permite upgrade la ≥ 50 Hz fără rescrierea logicii

---

### NFR-ARCH-001: Unificare Zephyr RTOS (STM32 + ESP32)

**Requirement:**  
Ambele MCU-uri rulează **Zephyr RTOS** pentru portabilitate și gestionare unificată a thread-urilor.

**STM32 Thread Architecture (Zephyr):**
- `sensor_thread` (PRI=5): citire ADC (4 canale) + clasificare TinyML + trimitere UART
- `haptic_thread` (PRI=6): control motor vibrații (PWM Zephyr)
- `display_thread` (PRI=7): actualizare OLED (SPI cu DMA)

**ESP32 Thread Architecture (Zephyr):**
- `ble_thread` (PRI=5): gestionare conexiune BLE + notificări GATT
- `mqtt_thread` (PRI=6): publicare evenimente pe broker MQTT over TLS
- `uart_rx_thread` (PRI=4): parsare frame-uri UART de la STM32
- `security_thread` (PRI=3): monitorizare honey-pots + rolling counter

**Motivație:**  
Zephyr oferă API unificat pentru ambele platforme (ADC, BLE, MQTT, PWM, SPI), eliminând fragmentarea FreeRTOS vs. ESP-IDF. Thread-urile independente elimină blocajul reciproc dintre citirea ADC și așteptarea ACK.

**Acceptance:**  
✅ Zephyr kernel pornit fără assert pe STM32F103 și ESP32  
✅ Toate thread-urile rulează concurent fără deadlock  
✅ Timebase gestionat de Zephyr system clock (nu SysTick manual)  
✅ Portabilitate: același API `k_thread_create()` pe ambele MCU-uri

---

### NFR-PERF-003: OLED Update

**Requirement:**  
SPI DMA transfer completes within 100ms of gesture event

---

### NFR-PERF-004: TinyML Inference Latency

**Requirement:**  
Execuția modelului Neural Network (TensorFlow Lite Micro) pe STM32F103 se termină în **< 50ms** de la primirea vectorului ADC.

**Measurement:**  
`DWT_CYCCNT` cyclecount la input → la output al `tflm_invoke()` → convertit în ms la 72 MHz

**Acceptance:**  
✅ Inferență INT8 < 50ms pe STM32 @ 72 MHz  
✅ Nu blochează `sensor_thread` (rulat sincron în același thread, buget de timp alocat)  
✅ Confidență returnată odată cu ID-ul gestului (nu necesită apel separat)

---

## 🔒 Reliability

### NFR-REL-001: System Up time

**Requirement:**  
System runs >= 30 minutes without unplanned reset during demo

---

### NFR-REL-002: Gesture Accuracy

**Requirement:**  
≥ 80% correct classification on 20-gesture validation set (mix of 4 single + 1 fist)

---

###NFR-REL-003: BLE Delivery

**Requirement:**  
≥ 95% of gestures reach mobile app

**Test:**  
Send 20 gestures → app logs ≥ 19

---

### NFR-REL-004: UART Integrity

**Requirement:**  
- CRC detects injected corruption: **100%** (no false accepts)
- Clean frames accepted: **≥ 95%**

---

## 🛡️ Safety

### NFR-SAFE-001: Watchdog Recovery

**Requirement:**  
IWDG resets STM32 within 2 seconds if main loop freezes

---

### NFR-SAFE-002: Sensor Fault *(Optional, nice-to-have)*

**Requirement:**  
Detect open-circuit ADC (out-of-range value) and display `"SENSOR ERROR"` on OLED

---

### NFR-SAFE-003: Izolare Driver Haptic (2N2222)

**Requirement:**  
Motorul de vibrații coin (~80mA) este comandat exclusiv prin tranzistorul NPN **2N2222** în configurație **Low-Side Switch**. Pinul GPIO al STM32 nu furnizează curent direct motorului.

**Circuit:**

| Pin tranzistor | Conexiune |
|---------------|-----------|
| **Emitor (E)** | GND comun |
| **Bază (B)** | GPIO STM32 prin rezistență **1kΩ** |
| **Colector (C)** | Firul Negru (−) al motorului |
| **Diodă flyback** | 1N4148 în paralel cu motorul (catod la VCC, anod la colector) |

**Rationale:**  
GPIO-urile STM32 suportă max ~20mA. Curentul motorului (~80mA) depășește această limită — fără tranzistor, pinul GPIO ar fi deteriorat ireversibil.

**Acceptance:**  
✅ GPIO STM32 furnizează < 5mA în circuit (curent de bază al 2N2222)  
✅ Motorul se activează/dezactivează corect la HIGH/LOW pe GPIO  
✅ Dioda flyback prezentă (verificată pe schemă) — protecție spike inductiv

---

## 🔐 Security

### NFR-SEC-001: BLE Obfuscation

**Requirement:**  
Gesture payload not transmitted as plaintext (simple XOR or AES-128-ECB)

**Acceptance:**  
Wireshark BLE sniffer shows non-readable payload

---

### NFR-SEC-002: BLE Pairing

**Requirement:**  
ESP32 supports BLE Secure Connections pairing (PIN entry on mobile app)

---

## 💬 Bidirectional Messaging

### NFR-MSG-001: Message RX Latency

**Requirement:**  
Message from mobile app → vibration ≤ 500ms

---

### NFR-MSG-002: Haptic Pattern Distinction

**Requirement:**  
Patterns must be distinguishable in blind test:

| Event | Pattern | Duration |
|-------|---------|----------|
| Gesture confirmed | 1 pulse | 100ms |
| Message received | 2 pulses | 200ms + 100ms gap + 200ms |
| System error | 3 pulses | 150ms each, 50ms gaps |

---

## 🧪 Testability

### NFR-TEST-001: Unit Tests

**Requirement:**  
UART parser, CRC, gesture threshold logic have host-compiled unit tests

**Acceptance:**  
✅ Tests run in CI, exit code 0 = pass  
❌ No coverage % requirement (time constraint)

---

### NFR-CODE-001: Static Analysis *(Optional)*

**Requirement:**  
Run cppcheck on firmware, zero critical errors

**Note:**  
MISRA full compliance out of scope (1 month timeline)

---

## 🔄 CI/CD Automation

### NFR-CI-001: Build Pipeline

**Requirement:**  
GitHub Actions workflow builds STM32 + ESP32 firmware on each push

**Acceptance:**  
✅ ARM GCC cross-compile for STM32 (produces `.elf` + `.bin`)  
✅ ESP-IDF build for ESP32 (produces `.bin`)  
✅ Host unit tests compile and execute (GCC Linux)  
✅ Pipeline status badge in README (🟢 green / 🔴 red)  
✅ Build failure blocks PR merge

**Out of scope v1.0:**  
❌ Coverage reports  
❌ Advanced static analysis beyond basic cppcheck

---

## ⚙️ Constraints

### CNS-001: Hardware Platform

| Component | Specs |
|-----------|-------|
| **STM32F103 Blue Pill** | 72 MHz, 64 KB RAM, 256 KB Flash |
| **ESP32 CH340C 30-pin** | 240 MHz dual-core, 320 KB RAM, 4 MB Flash |
| **Flex sensors** | 4× improvised (resistor divider to ADC) |
| **OLED** | SPI 128×64 |
| **Buzzer** | PWM |
| **Vibration motor** | PWM or GPIO |

---

### CNS-002: Timeline

| Milestone | Duration |
|-----------|----------|
| **v1.0 demo-ready** | 4 weeks (30 days) |
| **Final presentation** | 7 minutes |

---

### CNS-003: Development Tools

| Category | Tools |
|----------|-------|
| **STM32** | Zephyr RTOS, Zephyr HAL (ADC, SPI, UART, PWM), TensorFlow Lite Micro |
| **ESP32** | Zephyr RTOS, Zephyr BLE stack (NimBLE), MQTT over TLS |
| **Build** | PlatformIO (unified build system for both platforms) |
| **Programming** | ST-Link (STM32), USB Serial (ESP32) |
| **CI** | GitHub Actions |
| **Compiler** | ARM GCC (STM32), Xtensa GCC via Zephyr SDK (ESP32) |

---

### CNS-004: RTOS Decision Rationale

**De ce Zephyr RTOS pe ambele MCU-uri:**

| Factor | Justificare |
|--------|-------------|
| **API Unificat** | Acelaşi API `k_thread_create()`, `k_sem_give()`, `k_msgq_put()` pe STM32 şi ESP32 |
| **Portabilitate** | Cod comun (protocol.h, crc16.c) compilat fără modificări pe ambele platforme |
| **BLE Stack** | Zephyr include NimBLE stack nativ — fără dependență de ESP-IDF |
| **MQTT Support** | Zephyr Networking + Mbed TLS pentru MQTT over TLS (port 8883) |
| **TinyML** | TensorFlow Lite Micro portat pe Zephyr (`tflite-micro` module) |
| **Thread Safety** | Primitive Zephyr (mutex, semaphore, message queue) previn race conditions |
| **Power Mgmt** | Zephyr Power Management API pentru tickless idle |

**Zephyr pe STM32F103 — Considerente RAM:**

| Factor | Detaliu |
|--------|---------|
| **RAM Total** | 20 KB |
| **Overhead Zephyr** | ~6 KB (kernel + stive thread) |
| **RAM disponibil aplicație** | ~14 KB (suficient pentru buffer UART + TFLM arena) |
| **TFLM Arena** | 8 KB (model INT8 mic, alocat static) |

**Zephyr pe ESP32 — Avantaje:**

| Factor | Justificare |
|--------|-------------|
| **RAM Abundent** | 320 KB RAM; overhead ~8 KB = 2.5% |
| **WiFi + BLE Simultan** | Zephyr gestionă ambele stive concurent |
| **SPIFFS** | Zephyr Flash File System API pentru loguri de securitate |

**ESP32 Thread Architecture (Zephyr):**

```
Thread Name       | Priority | Stack | Purpose
------------------|----------|-------|----------------------------------
bluetooth_rx_tx   | 5        | 4KB   | BLE NimBLE stack (Zephyr)
uart_rx_thread    | 4        | 2KB   | Parse UART frames from STM32
mqtt_thread       | 6        | 4KB   | Publish events via MQTT over TLS
security_thread   | 3        | 2KB   | Honey-pots + rolling counter FSM
logger_thread     | 2        | 2KB   | Write SPIFFS intrusion logs
```

**Migrare de la FreeRTOS/bare-metal:**  
Decizia de unificare pe Zephyr elimina fragmentarea arhitecturala din v0.x (bare-metal STM32 + ESP-IDF ESP32). Toate componentele — ADC, BLE, MQTT, PWM, TFLM — au acum driverele oficiale Zephyr.

**v2.0 Enhancement:**
- Zephyr MCUboot pentru OTA firmware updates
- Zephyr Trusted Execution Environment (TrustZone pe STM32L5)

---

### CNS-005: Power Budget

**System Power Consumption:**

| Component | Operating Mode | Current Draw | Duty Cycle | Average |
|-----------|---------------|--------------|------------|----------|
| **STM32F103** | 72MHz active + ADC + UART + SPI | 40mA + 10mA | 100% | **50mA** |
| **ESP32** | BLE active (periodic TX) | 120-160mA | 50% | **80mA** |
| **Flex sensors (5×)** | 10kΩ @ 3.3V resistor divider | 0.33mA × 5 | 100% | **1.7mA** |
| **OLED 128×64** | SPI display (partial updates) | 15-20mA | 50% | **10mA** |
| **Vibration motor** | Coin motor 3V | 80mA | 10% (1s/10s) | **8mA** |
| **Buzzer** | Piezo PWM | 5-10mA | 1% | **0.5mA** |
| **LEDs (2× optional)** | Status indicators | 20mA × 2 | 50% | **20mA** |
| **Voltage regulator** | LDO 85% efficiency loss | - | - | **+25mA** |
| | | | **TOTAL AVERAGE** | **~195mA** |

**Peak Consumption (all active):** ~362mA (during vibration motor burst)

**Battery Options:**

| Type | Capacity | Voltage | Runtime @ 195mA | Weight | Cost | Use Case |
|------|----------|---------|-----------------|--------|------|----------|
| **LiPo 500mAh** | 500mAh | 3.7V | **2.6 hours** | 12g | $6-8 | ✅ **Recommended for demo** (wearable) |
| **LiPo 1000mAh** | 1000mAh | 3.7V | **5.1 hours** | 25g | $8-10 | Extended testing, whole-day usage |
| **Li-ion 18650** | 2500mAh | 3.7V | **12.8 hours** | 45g | $5 | Benchtop testing (too bulky for wearable) |
| **USB Power Bank** | Unlimited | 5V | **Unlimited** | External | $0 | Development & stationary demo |

**Power Supply Architecture:**
- **Primary**: LiPo 3.7V → AMS1117-3.3 LDO → 3.3V rail (STM32 + ESP32 + peripherals)
- **Charging**: TP4056 module with USB input (optional for v1.0)
- **Protection**: Over-discharge, over-current, short-circuit protection circuit

**Compliance with NFR-REL-001:**
- Required: ≥30 minutes runtime for demo
- Provided: LiPo 500mAh = 2.6 hours = **5× safety margin** ✅
- Peak current: 362mA < 500mA discharge rate (1C) ✅

**v1.0 Strategy:**
- **Development**: USB power bank (zero battery concern)
- **Final demo**: LiPo 500mAh wearable on arm/wrist
- **Cost**: ~$8 (battery + LDO + charging module)

---

### CNS-006: PHY Layer Optimization (Hardware Signal Quality)

**Requirement:**  
Implement physical layer (PHY) noise reduction and signal conditioning to achieve ≥11-bit effective ADC resolution (from 12-bit nominal).

**Problem Statement:**  
Standard ADC connections suffer from:
- **EMI (Electromagnetic Interference):** WiFi/BLE radios create 2.4GHz noise on analog wiring
- **Power supply ripple:** Switching regulators introduce high-frequency noise on VREF
- **Quantization noise:** 12-bit ADC (4096 levels) degrades to ~9-bit effective (512 levels) without filtering

**PLC Comparison:**  
Industrial PLC systems typically use:
- Shielded cables (expensive, not wearable)
- 10-bit ADC modules (lower resolution)
- No active filtering (relies on cable shielding)

**GloveAssist PHY Optimization:**

**1. Active Low-Pass Filtering (Analog Frontend):**
```
Flex Sensor → RC Filter (1kHz cutoff) → Sallen-Key 2nd-order LPF → STM32 ADC
```
- **Components:** 2× 10kΩ resistors, 2× 100nF capacitors, 1× TL072 op-amp per sensor
- **Effect:** Removes >90% of high-frequency noise (>1kHz)
- **Cost:** ~$2 for 4-channel filtering

**2. EMI Shielding (Twisted-Pair + Ferrite Beads):**
- **Wiring:** 26 AWG twisted-pair cables (sensor signal + GND twisted together)
- **Ferrite beads:** 1× ferrite core at ADC input (suppresses RF >100MHz)
- **Grounding:** Star-point ground (not daisy-chain) to prevent ground loops
- **Cost:** ~$3 for ferrite beads + twisted-pair wire

**3. VREF Decoupling (Ultra-Low-Noise Reference):**
- **Component:** TL431 precision voltage reference (2.5V) + voltage divider to 3.3V
- **Decoupling:** 10µF tantalum + 100nF ceramic capacitors at VREF pin
- **Effect:** Reduces VREF ripple from ±50mV to ±5mV
- **Cost:** ~$1

**4. ADC Oversampling & Decimation (Software):**
```c
// Oversample 16× at 50Hz → Effective resolution: 12 + log2(16)/2 = 14-bit
uint32_t adc_sum = 0;
for (int i = 0; i < 16; i++) {
    adc_sum += ADC_Read(channel);
    delay_us(100);  // 10kHz sampling
}
uint16_t adc_avg = adc_sum >> 4;  // Divide by 16
```
- **Benefit:** +2 bits resolution (12-bit → 14-bit effective)
- **Cost:** 1.6ms sampling time (still meets 20ms period requirement)

**Acceptance Criteria:**
✅ **SNR (Signal-to-Noise Ratio):** ≥60dB measured with oscilloscope (vs 45dB unfiltered)  
✅ **ADC Stability:** ±10 counts drift on stationary sensor (vs ±50 counts unfiltered)  
✅ **EMI Immunity:** BLE burst transmission causes <5% ADC reading change (vs 20% without ferrite)  
✅ **Cost:** ≤$6 additional hardware (affordable for medical device)

**Validation Tests:**
1. **Baseline Test:** Measure ADC noise floor without filtering → Record histogram (expected: ±50 counts)
2. **Filtered Test:** With RC + Sallen-Key filter → Noise floor ±5 counts ✅
3. **EMI Test:** Trigger BLE TX burst while reading ADC → <5% deviation ✅
4. **Long-term Drift:** Record sensor for 1 hour → Standard deviation <10 counts ✅

**Competitive Advantage:**
| Feature | GloveAssist (PHY Optimized) | PLC Weather Station |
|---------|-----------------------------|--------------------|
| **Effective Resolution** | **14-bit (oversampling)** | 10-bit (standard industrial ADC) |
| **SNR** | **60dB** (active filtering) | 45dB (passive shielding) |
| **EMI Immunity** | Ferrite beads + twisted-pair | Shielded cable (not wearable) |
| **VREF Stability** | ±5mV (precision reference) | ±50mV (LDO ripple) |
| **Cost** | **$6** (optimized for wearable) | $20+ (industrial shielded cables) |

**Implementation Effort:**
- Hardware modification: 2-3 hours (soldering, breadboard testing)
- Software oversampling: 1 hour (ADC driver modification)
- Validation testing: 1 day (oscilloscope measurements, long-term stability)
- Total: ~2 days

**Rationale:**  
Demonstrates **analog design expertise** beyond digital firmware. Shows understanding of signal integrity principles critical for medical-grade devices. This is the "invisible" engineering that separates hobbyist projects from professional systems.

**Bill of Materials (BOM) - PHY Components:**
| Component | Quantity | Unit Cost | Total |
|-----------|----------|-----------|-------|
| TL072 Dual Op-Amp | 2× | $0.50 | $1.00 |
| 10kΩ resistors | 8× | $0.02 | $0.16 |
| 100nF capacitors | 8× | $0.05 | $0.40 |
| TL431 voltage reference | 1× | $0.30 | $0.30 |
| Ferrite beads | 4× | $0.20 | $0.80 |
| 10µF tantalum cap | 2× | $0.50 | $1.00 |
| Twisted-pair wire (1m) | 4× | $0.50 | $2.00 |
| | | **TOTAL** | **~$5.66** |

**Status:** ✅ **v1.0 MANDATORY** (differentiates from standard ADC readings)

---

## 📌 Summary

**Total NFR Count:** 15  
**Total Constraints:** 6 (CNS-001 through CNS-006)  
**Must-have:** 13  
**Nice-to-have:** 2 (sensor fault, static analysis)

**Coverage on 5 mandatory topics:**  
✅ Wireless protocols (BLE + UART)  
✅ Data processing (performance, accuracy, **TinyML**)  
✅ Cryptography (obfuscation, pairing, **anti-sniffing**)  
✅ Sniffing (**FR-015 honey-pots + intrusion detection**)  
✅ CI/CD (pipeline automation)

**New v1.0 Features:**
✅ **PHY Optimization** (CNS-006): 14-bit effective ADC resolution  
✅ **TinyML** (FR-018): Neural network gesture classification  
⚠️ **MQTT** (FR-017): Optional telemedicine dashboard  
📐 **RL** (FR-016): v2.0 adaptive learning architecture

**Architecture Decision:**  
✅ STM32: Bare-metal super-loop (simplicity, timeline, RAM efficiency)  
✅ ESP32: FreeRTOS (mandatory for BLE, built-in ESP-IDF)  
⏭️ v2.0: Optional FreeRTOS on STM32 for unified architecture