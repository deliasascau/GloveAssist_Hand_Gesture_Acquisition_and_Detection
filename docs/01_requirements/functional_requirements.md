# 📋 Functional Requirements - GloveAssist v1.0 (Updated)

---

## 🎯 Problem Statement

Patients immobilized in bed cannot easily reach emergency call button. **GloveAssist** provides gesture-based alerting via smart glove with BLE connectivity and haptic/visual feedback.

---

## 👥 User Context

| Aspect | Details |
|--------|---------|
| **Primary user** | Immobilized patient (bedridden, post-surgery, limited mobility) |
| **Caregiver** | Single mobile device (phone/tablet) for alerts |
| **Hardware** | 4 flex sensors: index, middle, ring, pinky *(thumb excluded)* |
| **MCU architecture** | STM32F103 (sensor + processing, **Zephyr RTOS**) + ESP32 (BLE/MQTT gateway, **Zephyr RTOS**) |

---

## 📝 Functional Requirements

### FR-001: Gesture Recognition - Single Finger

**Description:**  
Detect single finger closure held ≥ 500ms and classify which finger.

**Acceptance:**  
✅ Recognizes 4 individual fingers with ≥ 85% accuracy on 20-gesture test set.

---

### FR-002: Gesture Recognition - Multi-Finger Combinations

**Description:**  
Detect 2, 3, or 4 fingers closed simultaneously, held ≥ 500ms.

**Acceptance:**  
✅ Recognizes fist (4 fingers) and 2-finger combo with ≥ 80% accuracy.

---

### FR-003: Gesture Transmission over BLE

**Description:**  
Transmit gesture ID to single paired mobile device via BLE within 200ms.

**Acceptance:**  
✅ Gesture reaches mobile app GATT notification within 200ms  
✅ Single device only (caregiver phone)

---

### FR-004: Visual Feedback (OLED)

**Description:**  
Display gesture result and system status on OLED (SPI with DMA).

**Acceptance:**  
✅ Shows "Ready", gesture name (e.g., "HELP"), and BLE status  
✅ Updates within 100ms

---

### FR-005: Audio + Haptic Feedback

**Description:**  
Trigger buzzer (PWM tone) and vibration motor (coin type) on gesture detection.

**Hardware Driver:**  
Transistor NPN **2N2222** în configurație Low-Side Switch:
- **Emitor (E):** GND comun
- **Bază (B):** Pin GPIO prin rezistență **1kΩ**
- **Colector (C):** Firul Negru (−) al motorului
- **Diodă Flyback 1N4148:** În paralel cu motorul (catod la +, anod la −)

**Acceptance:**  
✅ Single beep + vibration pulse within 100ms of gesture  
✅ Different pattern for incoming messages (2 pulses)  
✅ GPIO protejat de consumul motorului (~80mA) prin tranzistorul 2N2222

---

### FR-006: Calibration and Profile Storage

**Description:**  
Save sensor calibration profile to EEPROM (emulated on STM32 flash).  
At boot, verify profile validity; if invalid/missing, trigger auto-recalibration via fist closure.

**Acceptance:**  
✅ Profile persists across reboot  
✅ Invalid profile → OLED prompt: *"Close fist twice"*

---

### FR-007: UART STM32↔ESP32 Protocol

**Description:**  
Framed UART messages cu CRC16 integrity check.  
**Format:** `SOF(0xAA) + TYPE + COUNTER(u32) + LEN + PAYLOAD[32] + CRC16` = 41 bytes  
ACK invers: `0xAC + counter_hi + counter_lo` = 3 bytes

**Acceptance:**  
✅ 115200 baud, frame fix 41 bytes  
✅ CRC rejects corrupted frames  
✅ ACK confirmat pe STM32 ([ACK #N])  
✅ No retries in v1.0 (demo simplicity)

---

### FR-008: Event Logging

**Description:**  
Store last 50 gestures in ESP32 RAM (circular buffer): timestamp, gesture ID, TX status.

**Acceptance:**  
✅ Export log via BLE characteristic read (text format, newline-separated)

---

### FR-009: Watchdog Recovery

**Description:**  
STM32 internal watchdog (IWDG) with 2-second timeout. Resets on freeze.

**Acceptance:**  
✅ Inject freeze (infinite loop) → system resets within 2 seconds  
✅ OLED shows "REBOOT" briefly on recovery

---

### FR-010: BLE Payload Protection

**Description:**  
Apply simple XOR obfuscation or AES-128-ECB on gesture payload before BLE TX.

**Acceptance:**  
✅ Sniffer capture shows non-plaintext payload  
✅ Mobile app decrypts  
✅ Key hardcoded (v1.0 simplicity)  
❌ No replay protection (out of scope)

---

### FR-011: Receive Messages from Mobile App

**Description:**  
Mobile app writes text message (max 32 chars) to BLE characteristic.  
ESP32 forwards to STM32 via UART.

**Acceptance:**  
✅ Message reaches STM32 within 500ms  
✅ STM32 displays first 16 chars on OLED for 3 seconds

---

### FR-012: Haptic Feedback on Message RX

**Description:**  
Trigger 2-pulse vibration (different from 1-pulse gesture confirmation) when message arrives.

**Acceptance:**  
✅ Distinct pattern: 2× 200ms pulses with 100ms gap  
✅ Patient can distinguish from gesture feedback

---

### FR-013: Sniffing and Debug Capture

**Description:**  
Log BLE TX/RX and UART frames to:
1. **SPIFFS flash storage** (64KB, persistent) for standalone demo
2. **Serial console** (when USB connected) for live development debug
3. **BLE characteristic read** (0x2B02) for post-demo log export

**Acceptance:**  
✅ **MANDATORY:** UART frames logged as hex dump: direction, timestamp, raw bytes, CRC status  
✅ **MANDATORY:** BLE notifications logged: timestamp, gesture ID, encrypted payload (hex)  
✅ **MANDATORY:** Log persists in SPIFFS across reboots (max 64KB circular buffer)  
✅ **MANDATORY:** Export log via BLE read after demo (no PC connection required)  
✅ **MANDATORY:** Dual output: SPIFFS (always) + serial console (if connected)  
❌ **OPTIONAL:** SMP pairing procedure capture with external BLE sniffer (nRF52840 + Wireshark)  
❌ **OPTIONAL:** External logic analyzer hookup on STM32 UART pins

**Alternative for SMP validation (no external hardware):**  
- ESP32 logs SMP events to serial console (pairing start/complete, PIN display)  
- Android BLE scanner apps (nRF Connect) show pairing procedure  
- Linux `btmon` tool captures BLE HCI traffic on development laptop

**Rationale:**  
Enables sniffing validation during standalone battery-powered demo without PC connection.

---

### FR-014: WiFi Cloud Integration *(OPTIONAL - Nice-to-Have)*

**Description:**  
ESP32 connects to WiFi network and forwards gesture events to cloud server for:
1. Multi-caregiver push notifications (nurse, doctor, family)
2. Centralized nurse station dashboard (all patients)
3. Data analytics and historical logging
4. Remote monitoring for families
5. OTA firmware updates

**Acceptance:**  
✅ ESP32 dual-mode: BLE (local) + WiFi (cloud) operate simultaneously  
✅ HTTP POST gesture events to REST API endpoint  
✅ JSON payload: `{patient_id, gesture, timestamp, encrypted_payload}`  
✅ Server stores events in database (SQLite/MongoDB/PostgreSQL)  
✅ Web dashboard displays real-time gesture stream (WebSocket optional)  
✅ Fallback: BLE continues working if WiFi unavailable  
✅ No additional hardware required (ESP32 has WiFi built-in)

**Implementation Notes:**  
- Server options: Laptop local (demo), Heroku free tier, or AWS/Azure  
- WiFi credentials configured via BLE write or hardcoded for demo  
- Power consumption: +100-200mA when WiFi TX active  
- Estimated effort: +1-2 weeks implementation

**Out of Scope v1.0:**  
❌ HTTPS/TLS encryption (HTTP sufficient for demo)  
❌ OAuth authentication (hardcoded API key acceptable)  
❌ Cloud database management (simple storage sufficient)  
❌ Mobile app cloud sync (direct BLE remains primary interface)

**Rationale:**  
Demonstrates scalability from single-patient (BLE) to multi-patient hospital deployment (WiFi cloud). Shows architectural thinking beyond v1.0 scope.

---

### FR-015: Advanced Anti-Sniffing & Intrusion Detection *(MANDATORY - Security Shield)*

**Description:**  
Implement active defense mechanisms to prevent sniffing, replay attacks, and data injection on UART and BLE channels:

1. **Honey-Pot Packets (Decoy Traffic):**  
   STM32 transmits fake UART frames with identical structure to real packets. ESP32 silently discards them based on packet type identifier. Ratio: 3 honey-pots per 1 real packet.

2. **Rolling Counter (Anti-Replay):**  
   Each packet (real + honey-pot) contains monotonically increasing counter $N$. ESP32 accepts only $N = \text{last} + 1$. Replayed packets ($N \leq \text{last}$) are rejected immediately.

3. **Dynamic XOR Key (Time-Based Obfuscation):**  
   Replace hardcoded XOR key with time-based PRNG seed: $\text{Key}_t = \text{PRNG}(t, \text{secret})$. Key rotates every 10 seconds. Sniffed packets become useless after key rotation.

4. **Intrusion Detection & Lockdown:**  
   ESP32 enters **Lockdown Mode** if any condition is met:
   - Invalid CRC16 detected (potential injection)
   - Counter out-of-sequence by >5 (replay attack)
   - 5 consecutive invalid frames (active attack)
   
   **Lockdown Actions:**
   - Stop accepting UART data
   - Send BLE alert to mobile: *"⚠️ Security breach detected. Glove locked."*
   - Flash red LED pattern on ESP32
   - Log intrusion attempt with timestamp to SPIFFS
   - Require manual reset or BLE unlock command

**Acceptance:**  
✅ **Honey-Pots:** Wireshark UART capture shows 75% decoy traffic (indistinguishable structure)  
✅ **Rolling Counter:** Replayed packet causes intrusion alert within 100ms  
✅ **Dynamic Key:** Sniffed packet invalid after 10-second window  
✅ **Lockdown Mode:** Triggered by invalid CRC or counter anomaly  
✅ **Mobile Alert:** BLE notification shows security breach message  
✅ **Unlock Mechanism:** Caregiver enters 4-digit PIN on mobile app to resume operation

**Validation Tests:**  
1. **Sniffing Immunity:** Logic analyzer captures UART → 80% undecodable without key  
2. **Replay Attack:** Inject old valid frame → ESP32 rejects + triggers lockdown  
3. **Injection Attack:** Corrupt CRC16 manually → Lockdown within 1 frame  
4. **Key Rotation:** Capture packet at $t=0$, replay at $t=15$ → Invalid decryption

**Rationale:**  
This "digital shield" transforms GloveAssist from a simple IoT device into a **medical-grade secure system**. Unlike industrial PLC stations (which accept any valid command), this architecture actively defends against:
- **Passive sniffing** (obfuscation + honey-pots)
- **Replay attacks** (rolling counter)
- **Active injection** (lockdown + audit logs)

**Competitive Advantage:**  
| Feature | GloveAssist (STM32+ESP32) | Weather Station (PLC-STM) |
|---------|---------------------------|---------------------------|
| **Data Visibility** | Obfuscated (XOR + dynamic key) | Plaintext (standard industrial protocol) |
| **Injection Immunity** | Honey-pots + rolling counter | Low (PLC executes any valid command) |
| **Attack Detection** | Real-time lockdown + mobile alert | None (only logs sensor errors) |
| **Replay Protection** | ✅ Counter validation | ❌ Not implemented |
| **Audit Trail** | SPIFFS intrusion logs | ❌ No security logging |

**Implementation Effort:**  
- STM32: +150 LOC (honey-pot generator, counter management)
- ESP32: +200 LOC (intrusion detection FSM, lockdown handler)
- Total: ~2-3 days implementation + 1 day validation

---

### FR-016: Reinforcement Learning - Adaptive Thresholds *(OPTIONAL - v2.0 Future Enhancement)*

**Description:**  
Implement Q-Learning algorithm to automatically adapt gesture detection thresholds based on patient's motor ability evolution.

**ProblemStatement:**  
Static thresholds (e.g., "60% finger closure = gesture") fail when:
- Patient's mobility improves during rehabilitation → needs higher precision
- Patient fatigues over time → lower mobility, false negatives increase
- Different patients have vastly different motor ranges

**Solution - RL Agent:**  
STM32 runs lightweight Q-Learning agent that adjusts thresholds based on:
1. **Reward Signal:** Successful gesture confirmation (caregiver responds) = +1 reward
2. **Penalty:** False positives (patient denies gesture) = -1 penalty  
3. **State Space:** [finger_range_low, finger_range_high, confidence_level]
4. **Action Space:** [increase_threshold, decrease_threshold, maintain]

**Algorithm (Simplified Q-Learning):**
```
Q(state, action) ← Q(state, action) + α[reward + γ·max(Q(next_state, •)) - Q(state, action)]

α = learning rate (0.1)
γ = discount factor (0.9)
```

**Edge Computing:**  
- Q-table stored in STM32 EEPROM (64 states × 3 actions = 192 bytes)
- Update occurs after each gesture (10ms inference time)
- No cloud dependency (runs offline)

**Acceptance:**  
✅ System adapts thresholds within 20 gestures of use  
✅ Detection accuracy improves by ≥10% after 100 gestures vs static thresholds  
✅ Q-table persists across reboots (EEPROM storage)  
✅ Training mode toggled via BLE command (caregiver confirms/denies gestures)

**Validation Tests:**  
1. **Scenario A - Improving Patient:** Start with low mobility (30% closure), simulate improvement to 80% closure over 50 gestures → thresholds auto-increase
2. **Scenario B - Fatiguing Patient:** Start with 70% closure, simulate fatigue to 40% → thresholds auto-decrease
3. **Convergence:** Q-values stabilize within 100 training iterations

**Competitive Advantage:**  
| Feature | GloveAssist (RL) | PLC Weather Station |
|---------|------------------|---------------------|
| **Threshold Logic** | **Adaptive (learns patient behavior)** | Static (hardcoded) |
| **Personalization** | Auto-calibrates per patient | Manual recalibration required |
| **Rehabilitation Support** | Tracks motor improvement over weeks | No adaptation |
| **Edge AI** | Q-Learning on 32KB RAM | ❌ No AI (only ladder logic) |

**Implementation Effort:**  
- STM32: +300 LOC (Q-Learning algorithm, EEPROM management)
- Training dataset: 100 gestures per patient (manual labeling)
- Total: ~6-7 days (algorithm + testing + validation)

**Rationale:**  
Demonstrates **Edge AI** capability - the system becomes "smarter" with use, unlike industrial systems that execute fixed logic. This is the bridge between "data collection" and "intelligent medical device."

**Status:** ⚠️ **v2.0 Future Enhancement** (documented architecture, implementation deferred post-thesis)

---

### FR-017: MQTT Telemedicine Cloud Integration *(MANDATORY - v1.0)*

**Description:**  
Integrate MQTT protocol over WiFi to forward gesture events to cloud-based telemedicine dashboard for multi-caregiver monitoring.

**Architecture:**  
```
Patient → STM32 → ESP32 → [Dual-path]
                           ├─ BLE → Mobile App (local, real-time)
                           └─ MQTT → Cloud Broker → Web Dashboard (remote, historical)
```

**MQTT Topics Structure:**
```
gloveassist/{patient_id}/gesture       → Real-time gesture events
gloveassist/{patient_id}/vitals        → System status (battery, uptime)
gloveassist/{patient_id}/security      → Intrusion alerts (FR-015)
gloveassist/{patient_id}/command       → Remote control (lockdown, calibration)
```

**Message Format (JSON):**
```json
{
  "patient_id": "P12345",
  "timestamp": 1678723456,
  "gesture_id": 3,
  "gesture_name": "WATER",
  "confidence": 94,
  "encrypted_payload": "A3F7B2...",
  "sensor_values": [1234, 2345, 3456, 789],
  "battery_level": 67
}
```

**Cloud Broker Options:**
1. **Local (Demo):** Mosquitto on laptop (5 minutes setup)
2. **Public (Testing):** HiveMQ Cloud free tier (100 devices)
3. **Production (Real):** AWS IoT Core or Azure IoT Hub

**Security - MQTT over TLS:**
- Port 8883 (MQTT-TLS) vs 1883 (plaintext)
- Client certificate authentication (X.509)
- Topic-based access control (patient can't see other patients)

**Acceptance:**  
✅ ESP32 publishes gesture events to MQTT broker within 500ms  
✅ Web dashboard displays real-time gesture stream (WebSocket or polling)  
✅ Multi-caregiver support: nurse station + family member receive same events  
✅ Historical data: last 1000 gestures stored in cloud database (SQLite/MongoDB)  
✅ Command downlink: Send "lockdown" command from dashboard → ESP32 executes  
✅ Fallback: BLE continues working if WiFi/MQTT unavailable

**Validation Tests:**  
1. **Latency:** Gesture → MQTT publish → Dashboard display <1 second end-to-end
2. **Reliability:** Disconnect WiFi mid-gesture → system buffers in SPIFFS → retransmits on reconnect
3. **Security:** Wireshark capture on WiFi → verify TLS encryption (no plaintext gestures)
4. **Multi-Client:** 3 devices subscribe to same patient topic → all receive events

**Power Consumption Impact:**
- WiFi active (MQTT publishing): +120mA average
- Deep sleep between gestures: 15mA
- **Strategy:** MQTT publishes only on gesture (not continuous), power budget: 195mA → 315mA peak

**Competitive Advantage:**  
| Feature | GloveAssist (MQTT/IoT) | PLC Weather Station |
|---------|------------------------|---------------------|
| **Cloud Protocol** | **MQTT (IoT standard, lightweight)** | Modbus TCP (industrial, heavyweight) |
| **Real-time Dashboard** | WebSocket-based, <1s latency | ❌ No cloud integration |
| **Multi-User** | Unlimited subscribers (pub/sub model) | Single HMI screen |
| **Scalability** | 1000+ patients on same broker | ❌ Not designed for scale |
| **Remote Control** | Bi-directional (command downlink) | ❌ Local only |

**Implementation Effort:**  
- ESP32: +250 LOC (MQTT client, WiFi reconnect logic, TLS setup)
- Web Dashboard: Basic HTML/JS (50 LOC) or Node.js backend (200 LOC)
- Cloud Setup: 1-2 hours (Mosquitto or HiveMQ)
- Total: ~3-4 days (MQTT + dashboard + testing)

**Dependencies:**  
- Requires FR-014 WiFi infrastructure (ESP32 WiFi already present)
- Broker setup (can use public free tier for demo)

---

### FR-018: TinyML Gesture Classification *(MANDATORY - v1.0)*

**Description:**  
Înlocuirea pragurilor statice de detecție cu un model **Neural Network** rulat on-device pe STM32 prin **TensorFlow Lite Micro (TFLM)**. Modelul primește vectorul de 4 valori ADC normalizate și returnează ID-ul gestului cu scor de confidență.

**Architecture:**  
```
[ADC x4] → Normalizare → NN Model (TFLM) → Gesture ID + Confidence
              (float32)      STM32 Flash        (ID: 0-7, conf: 0-100%)
```

**Model Specifications:**
- **Input:** 4 valori senzori ADC normalizate [0.0 – 1.0]
- **Layers:** Dense(8, ReLU) → Dense(8, ReLU) → Dense(N_classes, Softmax)
- **Quantization:** INT8 post-training (reduce Flash footprint ~4×)
- **Flash footprint:** < 20 KB
- **Training:** Python/Keras pe dataset local, export `.tflite` → `model_data.h`

**Acceptance:**  
✅ Precizie clasificare gesturi ≥ **90%** pe set de validare (20 gesturi)  
✅ Inferență completă pe STM32 în < **50ms**  
✅ Modelul este încărcat din Flash (array C `uint8_t model_data[]`)  
✅ Confidență < 60% → gestul este ignorat (previne false positive)  
✅ Fallback la praguri statice dacă TFLM init eșuează

**Validation Tests:**  
1. **Accuracy Test:** 20 gesturi standardizate → ≥ 18 clasificate corect (90%)  
2. **Latency Test:** `DWT_CYCCNT` măsoară cicluri CPU → < 50ms la 72 MHz  
3. **Robustness:** Gesturi la limita de prag → confidență < 60% → ignorate corect

**Rationale:**  
Clasificarea bazată pe praguri statice (if/else) este fragilă — variați în presiunea degetelor, îmbătrânirea senzorilor sau diferențele între pacienți duc la false positive. TinyML permite **generalizare** și **adaptabilitate** fără creșterea complexității logicii de decizie.

**Rationale:**  
Demonstrates **Internet of Medical Things (IoMT)** architecture. Shows understanding of modern cloud-native protocols vs legacy industrial systems. MQTT is the de facto standard for IoT (used by Tesla, Amazon Alexa, etc.).

**Status:** ⚠️ **v1.0 Optional** (implement if time permits after PHY + TinyML)

---

### FR-018: TinyML Gesture Classification *(MANDATORY - v1.0 Core Feature)*

**Description:**  
Replace rule-based threshold logic with neural network inference running on STM32 for robust, multi-patient gesture recognition.

**Problem with Current Approach (FR-001):**
```c
// Current: Hardcoded thresholds (brittle)
if (sensor[0] > THRESHOLD_60_PERCENT) {
    gesture_id = 1;  // INDEX finger
}
```
**Issues:**
- Breaks when patient has different hand size
- Sensitive to sensor placement variations
- Cannot distinguish subtle gestures (e.g., "INDEX" vs "INDEX+MIDDLE slightly")

**Solution - TinyML Neural Network:**
```
Input: [sensor1, sensor2, sensor3, sensor4] (4 features, 12-bit ADC)
       ↓
Hidden Layer: 16 neurons (ReLU activation)
       ↓
Output: [prob_gesture0, prob_gesture1, ..., prob_gesture7] (softmax)
       ↓
Predicted gesture = argmax(output)
```

**Model Architecture:**
- **Framework:** TensorFlow Lite Micro (runs on Cortex-M3)
- **Model Size:** ~8KB flash (quantized INT8)
- **Inference Time:** <50ms per gesture (meets 200ms latency requirement)
- **RAM Usage:** ~4KB (activation buffers)

**Training Pipeline:**
1. **Dataset Collection:** 500 samples (50 gestures × 10 repetitions per patient × 1 patient initial)
2. **Data Augmentation:** Add noise (±10% ADC jitter) to simulate sensor drift
3. **Training:** Google Colab (free GPU) → 50 epochs, Adam optimizer
4. **Quantization:** Float32 → INT8 (8× smaller, 3× faster)
5. **Deployment:** Convert to C array, compile into STM32 firmware

**Dataset Format (CSV):**
```csv
sensor1, sensor2, sensor3, sensor4, gesture_label
1234,    2345,    3456,    789,     1  # INDEX
2345,    3456,    789,     1234,    2  # MIDDLE
...
```

**Acceptance:**  
✅ **Accuracy:** ≥90% on test set (100 unseen gestures)  
✅ **Inference Speed:** <50ms per prediction (measured with STM32 timer)  
✅ **Memory Footprint:** <10KB flash, <5KB RAM  
✅ **Multi-Patient:** Retrain with new patient data within 10 minutes  
✅ **Confidence Score:** Output probability for each gesture (reject if max_prob < 70%)  
✅ **Edge Deployment:** No cloud dependency (model runs offline on STM32)

**Validation Tests:**  
1. **Accuracy Test:** 10-fold cross-validation on 500-sample dataset → Report mean accuracy
2. **Real-time Test:** Live gesture detection with serial monitor showing: `[NN] Gesture=3 (WATER), Confidence=94%, Inference=42ms`
3. **Robustness Test:** Add 20% noise to sensors → accuracy degrades <5%
4. **Multi-Patient Test:** Train on Patient A, test on Patient B → accuracy >75% (transfer learning)

**Demo Impact for Jury:**
```
Serial Monitor Output:
[TINYML] Model loaded: 8124 bytes
[TINYML] Inference speed: 43ms
[TINYML] Gesture detected: INDEX (ID=1)
[TINYML] Confidence: 96.3%
[TINYML] Output probabilities: [0.01, 0.96, 0.01, 0.01, 0.01, 0.00, 0.00, 0.00]
```

**Competitive Advantage:**  
| Feature | GloveAssist (TinyML) | PLC Weather Station |
|---------|----------------------|---------------------|
| **Classification Logic** | **Neural Network (learned from data)** | Threshold comparisons (hardcoded) |
| **Accuracy** | 90-95% (robust to noise) | 75-85% (brittle to sensor drift) |
| **Adaptability** | Retrain for new patient in 10 min | Manual threshold tuning per patient |
| **Edge AI** | ✅ 8KB model on STM32 | ❌ No AI capability |
| **Industry Trend** | TinyML = future of wearables (Google, Meta) | PLC = 1970s technology |

**Implementation Effort:**  
- Dataset collection: 1 day (manual gesture recording)
- Model training: 0.5 days (Google Colab)
- TFLite Micro integration: 2 days (STM32 compilation, testing)
- Validation testing: 1 day
- Total: ~4-5 days

**Tools & Libraries:**
- TensorFlow Lite Micro: https://www.tensorflow.org/lite/microcontrollers
- STM32CubeAI (optional): Optimized NN kernels for STM32
- Dataset labeling: Python script + CSV export

**Rationale:**  
**This is your killer feature.** TinyML is cutting-edge (2025-2026 hot topic in embedded AI). Shows you understand modern ML deployment, not just "IoT data logger." PLC systems CANNOT run neural networks.

**References:**
- Pete Warden, "TinyML: Machine Learning with TensorFlow Lite on Arduino and Ultra-Low-Power Microcontrollers" (2019)
- Google TensorFlow Lite Micro documentation

**Status:** ✅ **v1.0 MANDATORY** (core differentiator vs PLC)

---

## 🗺️ Gesture Mapping Summary

| Gesture | Fingers Closed | Command | Use Case |
|---------|:--------------:|---------|----------|
| **Single - Index** | 1 | `OK` | Acknowledge assistance |
| **Single - Middle** | 1 | `BATHROOM` | Toilet assistance request |
| **Single - Ring** | 1 | `WATER` | Request water/drink |
| **Single - Pinky** | 1 | `INFO` | Check-in / information |
| **Multi - 2 Fingers** | 2 | `HELP` | ⚠️ **Urgent assistance** |
| **Multi - 3 Fingers** | 3 | `PAIN` | Report discomfort |
| **Multi - Fist (4)** | 4 | `EMERGENCY` | 🚨 **Critical alert** |

---

## 📌 Summary Notes

**Total Functional Requirements:** 18  
**Mandatory (v1.0):** 15  
- FR-001 through FR-013: Core functionality  
- FR-015: Advanced anti-sniffing security  
- FR-018: TinyML gesture classification

**Optional (v1.0 - Nice-to-Have):** 2  
- FR-014: WiFi cloud integration  
- FR-017: MQTT telemedicine dashboard

**Future Enhancement (v2.0):** 1  
- FR-016: Reinforcement learning adaptive thresholds

**Core Features:**
- **Thumb excluded:** Limited mobility constraint for immobilized patients
- **Calibration method:** Fist closure (4 fingers) for auto-recalibration
- **BLE simplicity:** Single device pairing (caregiver phone only)
- **Security shield:** Active defense with honey-pots + intrusion detection (FR-015)
- **🚀 Edge AI:** TinyML neural network for gesture classification (FR-018)

**Competitive Advantages (vs PLC Weather Station):**
1. **TinyML**: Neural network on 32KB RAM (vs threshold comparisons)
2. **Anti-Sniffing**: Honey-pots + rolling counter + lockdown (vs plaintext Modbus)
3. **MQTT**: IoT-native cloud protocol (vs industrial Modbus TCP)
4. **Adaptive Learning**: RL adjusts to patient evolution (vs static logic)

**Future Enhancements (v2.0):**
- Multi-caregiver notifications via MQTT cloud
- Centralized nurse station dashboard
- Remote family monitoring
- Reinforcement learning for rehabilitation tracking
- Zero additional hardware cost (ESP32 WiFi built-in)