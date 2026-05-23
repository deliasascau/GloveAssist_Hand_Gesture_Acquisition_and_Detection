# GloveAssist — Hartă Funcționalități Implementate

> **Legendă**: `STM32` = Blue Pill STM32F103 · `ESP32` = DevKitC · `COMMON` = shared cod între ambele

---

## 1. Achiziție Senzori Flex (ADC)

**MCU**: STM32 · **Fișier**: `application/stm32_app/src/sensor_logic.c`

**Ce face:**
Citește continuu 4 senzori flex DIY (rezistențe care variază cu îndoirea degetului) pe canalele ADC PA0–PA3 cu rezoluție 12 biți.

**Circuit:**
```
3.3V ─── [1kΩ fix] ─── PA_x (ADC) ─── [senzor flex ~600–1100Ω] ─── GND
```
- Deget drept: rezistență senzor mare → tensiune pe PA_x mare → ADC ~3165
- Deget îndoit: rezistență senzor mică → tensiune pe PA_x mică → ADC ~47–347

**Configurare DT** (`stm32_min_dev.overlay`):
```c
io-channels = <&adc1 0>, <&adc1 1>, <&adc1 2>, <&adc1 3>;
// ADC1 CH0=PA0 CH1=PA1 CH2=PA2 CH3=PA3
```

**Constante** (`app_config.h`):
```c
#define ADC_MIN_VALID  300U   // sub 300 = senzor deconectat
#define ADC_MAX_VALID 3900U   // peste 3900 = scurtcircuit
```

---

## 2. Filtru Moving Average

**MCU**: STM32 · **Fișier**: `sensor_logic.c` → `filter_update()`

**Ce face:**
Elimină zgomotul ADC (jitter electric) prin media ultimelor N citiri per canal.

**Implementare:**
```c
#define FILTER_WINDOW_SIZE  8U  // media ultimelor 8 citiri

static uint16_t filter_buf[4][8];  // circular buffer per canal
static uint32_t filter_sum[4];     // sumă curentă (evită recalculul)

static uint16_t filter_update(uint8_t ch, uint16_t sample) {
    filter_sum[ch] -= filter_buf[ch][idx];   // scoate valoarea veche
    filter_buf[ch][idx] = sample;             // adaugă valoarea nouă
    filter_sum[ch] += sample;
    return filter_sum[ch] / FILTER_WINDOW_SIZE;
}
```

**De ce:** fără filtru, ADC fluctuează ±50 LSB → gesturi false. Cu filtru → fluctuații ±5 LSB.

---

## 3. Clasificare Gesturi

**MCU**: STM32 · **Fișier**: `sensor_logic.c` → `classify_gesture()`

**Ce face:**
Compară valorile filtrate cu pragurile per-deget și identifică gestul.

**Gesturi implementate:**

| ID | Nume | Condiție |
|----|------|----------|
| 0 | `NONE` | niciun deget îndoit |
| 1 | `INDEX` | doar index îndoit |
| 2 | `MIDDLE` | doar mijlociu îndoit |
| 3 | `RING` | doar inelar îndoit |
| 4 | `PINKY` | doar degetul mic îndoit |
| 5 | `FIST` | toate 4 degetele îndoite |
| 6 | `HELP` | inelar + degetul mic (semnal urgență) |

**Praguri calibrate** (date reale 09-Apr-2026):
```c
#define GESTURE_THRESH_INDEX   621U  // I: OPEN=1195, FIST=47
#define GESTURE_THRESH_MIDDLE  874U  // M: OPEN=1695, FIST=53
#define GESTURE_THRESH_RING   2009U  // R: OPEN=3671, FIST=347
#define GESTURE_THRESH_PINKY   944U  // P: OPEN=1845, FIST=43
```

**Logică:**
```c
bent[i] = (filtered[i] < k_thresh[i]);  // valoare mică = deget îndoit
if (bent_count == 4)  → FIST
if (bent_count == 1)  → GESTURE_INDEX + single_idx
if (bent_count == 2 && ring && pinky) → HELP
```

---

## 4. Calibrare Automată cu Persistență NVS

**MCU**: STM32 · **Fișier**: `application/stm32_app/src/calibration.c`

**Ce face:**
Permite recalibrarea pragurilor de detecție (deget drept/îndoit per senzor) și le salvează în memoria flash non-volatilă (NVS). La reboot, pragurile se restaurează automat.

**Flow calibrare:**
1. ESP32/BLE trimite comanda `CMD_CALIBRATE` → STM32 primește pe UART
2. `calibration_request_start()` setează flag atomic
3. `sensor_thread` detectează `calibration_take_start_request()` → pornește `calibration_start()`
4. Măsoară 30 citiri cu degetele drepte (referință OPEN)
5. Măsoară 30 citiri cu degetele îndoite (referință BENT)
6. Calculează prag = (OPEN + BENT) / 2
7. Salvează în NVS cu magic `"GLVC"` + checksum FNV1a

**Format stocat NVS** (`calibration_profile_t`):
```c
typedef struct {
    uint32_t magic;       // 0x474C5643 = "GLVC"
    uint16_t version;     // 1
    uint16_t size;
    uint8_t  calibrated;  // 1 dacă calibrat vreodată
    uint16_t thresh[4];   // praguri active
    uint16_t open_ref[4]; // referință deget drept
    uint16_t bent_ref[4]; // referință deget îndoit
    uint32_t checksum;    // FNV1a pe structură (fără câmpul checksum)
} calibration_profile_t;
```

**Partiție NVS** (overlay): `storage` — 2KB la adresa `0x0801F800`

**Migrare**: citește și format vechi (chei NVS ID 1–3) → migrare automată la format nou (ID 10)

---

## 5. Sampling Adaptiv (Low-Power)

**MCU**: STM32 · **Fișier**: `sensor_logic.c` → bucla principală

**Ce face:**
Reduce frecvența de eșantionare când pacientul nu mișcă degetele → economie de energie + reducere zgomot.

```c
#define SENSOR_SAMPLE_PERIOD_MS      100U   // normal: 10 Hz
#define SENSOR_IDLE_SAMPLE_PERIOD_MS 250U   // idle:   4 Hz
#define SENSOR_IDLE_TIMEOUT_MS      3000U   // după 3s fără schimbare → idle
```

**Logică:** dacă gestul nu se schimbă timp de 3s → trece la 250ms. La orice mișcare → revine la 100ms.

---

## 6. Protocol de Comunicare UART (Frame Protocol)

**MCU**: STM32 + ESP32 · **Fișiere**: `application/common/src/frame_protocol.c`, `application/common/include/app_config.h`

**Ce face:**
Definește formatul cadrelor schimbate între STM32 și ESP32 pe UART (PA9/PA10 ↔ GPIO17/GPIO16, 115200 baud, 8N1).

**Structura unui cadru (12 bytes):**
```
┌─────┬──────┬─────┬──────────────────────┬──────┐
│ SOF │ TYPE │ SEQ │ PAYLOAD [8 bytes XOR] │ CRC8 │
│ 1B  │  1B  │  1B │         8B            │  1B  │
└─────┴──────┴─────┴──────────────────────┴──────┘
SOF = 0xAA (marker start)
```

**Tipuri cadre (`MSG_TYPE_*`):**

| Tip | Direcție | Conținut |
|-----|----------|----------|
| `MSG_TYPE_GESTURE` | STM32 → ESP32 | gesture_id + flex_raw[4] |
| `MSG_TYPE_SENSOR_RAW` | STM32 → ESP32 | date ADC brute |
| `MSG_TYPE_HEARTBEAT` | ESP32 → STM32 | confirmare că ESP32 e alive |
| `MSG_TYPE_COMMAND` | ESP32 → STM32 | comandă de la telefon (ex: calibrare) |
| `MSG_TYPE_HONEYPOT` | STM32 → ESP32 | cadru fals anti-sniff |

**Obfuscare XOR:**
```c
xor_key = SEQ ^ PROTO_XOR_KEY_BASE  // 0x5A
payload[i] ^= xor_key               // fiecare byte din payload
```

**CRC-8/CCITT** (polinomul 0x07) calculat pe `TYPE + SEQ + PAYLOAD` decodat.

---

## 7. Modulul UART STM32

**MCU**: STM32 · **Fișier**: `application/stm32_app/src/uart_comm.c`

**Ce face:**
Trimite cadre cu date senzori la ESP32 (100ms) și primește heartbeat + comenzi de la ESP32.

**RX — ISR-driven ring buffer:**
```c
#define RX_BUF_SIZE  (sizeof(glove_frame_t) * 4)  // 48 bytes
// ISR scrie în rx_buf circular
// Thread citește și procesează cadre complete
```

**TX — polling cu mutex:**
```c
// La fiecare 100ms, thread-ul UART:
// 1. Citește sensor_packet_t din message queue
// 2. Construiește glove_frame_t cu frame_build()
// 3. Aplică XOR security (security_xor_payload)
// 4. Trimite byte cu byte via uart_poll_out()
// 5. La ratio 1:3, trimite un cadru honeypot
```

---

## 8. Haptic Feedback — Motor Vibrații

**MCU**: STM32 · **Fișier**: `application/stm32_app/src/haptic_ui.c`

**Ce face:**
Controlează motorul de vibrații DC prin PWM (TIM4_CH1, PB6) pentru a da feedback tactil pacientului la diverse evenimente.

**Hardware**: PB6 → ULN2003 IN1 → motor DC 5V

**Pattern-uri implementate:**

| Eveniment | Durată puls | Semnificație |
|-----------|-------------|--------------|
| Gesture recunoscut | 100ms | Confirmare |
| Mesaj primit de la telefon | 2×100ms | Notificare |
| Eroare | 3×200ms | Alertă |
| SOS (ESP32 down) | 3-scurt + 3-lung + 3-scurt | Alarmă severă |

```c
static void motor_pulse(u32_t duration_ms) {
    pwm_set_dt(&motor_pwm, PWM_MSEC(1), PWM_MSEC(1) / 2U);  // 50% duty
    k_msleep(duration_ms);
    pwm_set_dt(&motor_pwm, PWM_MSEC(1), 0U);                  // stop
}
```

---

## 9. Haptic Feedback — Buzzer Pasiv

**MCU**: STM32 · **Fișier**: `haptic_ui.c` → `buzzer_beep()`

**Ce face:**
Generează ton audio pe buzzerul pasiv prin toggle GPIO software (PWM software) la frecvența dorită.

**Hardware**: PB4 → [1kΩ] → 2N2222A Base, Collector → Buzzer(−), Buzzer(+) → 3.3V

**Implementare (software PWM):**
```c
static void buzzer_beep(u32_t freq_hz, u32_t duration_ms) {
    u32_t half_period_us = 500000U / freq_hz;  // ex: 2500Hz → 200µs
    // toggle GPIO la half_period_us cu k_busy_wait
}
```

**Frecvențe folosite:**
- Gesture ACK: 2000 Hz, 50ms
- SOS: 2500 Hz, 100ms (scurt) / 300ms (lung)
- Eroare: 1000 Hz, 200ms (ton mai jos)

---

## 10. OLED Display SSD1306

**MCU**: STM32 · **Fișier**: `haptic_ui.c` → `oled_show_gesture()`

**Ce face:**
Afișează gestul curent detectat pe display-ul OLED 128×64 prin I2C2 (PB10 SCL, PB11 SDA).

**Texte afișate:**
```
--- / INDEX / MIDDLE / RING / PINKY / FIST / !! HELP !!
```

În modul SOS (ESP32 down):
```
!! NO LINK — LOCAL ALARM !!
```

**API Zephyr folosit:** CFB (Character Frame Buffer)
```c
cfb_framebuffer_clear(display_dev, false);
cfb_print(display_dev, gesture_names[gesture_id], 0, 0);
cfb_framebuffer_finalize(display_dev);
```

---

## 11. Sistemul de Siguranță IEC 61508 (Safety Diagnostics)

**MCU**: STM32 · **Fișier**: `application/stm32_app/src/safety_diag.c`

### 11a. Watchdog IWDG

**Ce face:** Dacă firmware-ul îngheață (loop infinit, fault), IWDG resetează automat STM32 după 2 secunde.

```c
#define WATCHDOG_TIMEOUT_MS  2000U
// Thread safety (prioritate 2, cea mai mare) hrănește WDT la fiecare ciclu
void safety_watchdog_feed(void) { wdt_feed(wdt_dev, wdt_channel_id); }
```

### 11b. Validare Senzori (Fault Detection)

**Ce face:** Detectează senzori deconectați (open-circuit) sau scurtcircuitați și folosește ultima valoare bună ca fallback.

```c
if (raw < ADC_MIN_VALID) {  // < 300 → open circuit
    fault_count[ch]++;
    if (fault_count[ch] >= 3) → folosește last_good[ch]
}
if (raw > ADC_MAX_VALID) {  // > 3900 → scurtcircuit → LOCKDOWN
    → safety_enter_lockdown()
}
```

### 11c. Heartbeat Monitor (Redundanță Dual-Core)

**Ce face:** Monitorizează că ESP32 trimite heartbeat la fiecare 500ms. Dacă lipsește >2s, intră în **modul degradat**.

```c
#define HEARTBEAT_INTERVAL_MS  500U   // ESP32 trimite la 500ms
#define HEARTBEAT_TIMEOUT_MS  2000U   // timeout STM32
#define HEARTBEAT_GRACE_BOOTS    3U   // ignoră primele 3 missed la boot
```

**Modul degradat** (ESP32 down):
- Motor: pulsuri puternice continue
- Buzzer: SOS (3 scurt + 3 lung + 3 scurt)
- OLED: `!! NO LINK — LOCAL ALARM !!`
- Pacientul **nu rămâne niciodată fără alertă**

**Recuperare:** când heartbeat revine → ieșire automată din modul degradat.

---

## 12. Securitate (Anti-Sniff, Anti-Replay)

**MCU**: STM32 · **Fișier**: `application/stm32_app/src/security.c`

### 12a. XOR Key Rotation

**Ce face:** Obfuscare payload cu cheie XOR rotită la fiecare 10 secunde.

```c
#define KEY_ROTATION_SEC  10U

static void rotate_xor_key(void) {
    sys_rand_get(xor_key, sizeof(xor_key));  // cheie nouă random
}
// La fiecare apel security_xor_payload(), dacă > 10s → rotate
```

### 12b. Honeypot Packets

**Ce face:** Injectează cadre false (1:3 ratio) printre cadre reale pentru a îngreuna analiza traficului UART.

```c
#define HONEYPOT_RATIO  3U  // 1 honeypot la 3 cadre reale
// Payload honeypot = random bytes cu MSG_TYPE_HONEYPOT
```

### 12c. Anti-Replay Lockdown

**Ce face:** Dacă se detectează 5 cadre invalide consecutive → sistem intră în LOCKDOWN (refuză toate cadrele până la reboot).

```c
#define LOCKDOWN_INVALID_FRAMES  5U
// consecutive_invalid >= 5 → lockdown_active = true
// → ERR_SECURITY_LOCKDOWN la orice frame ulterior
```

---

## 13. BLE NUS Gateway (ESP32)

**MCU**: ESP32 · **Fișier**: `application/esp32_app/src/comms_ble.c`

**Ce face:**
ESP32 expune un serviciu BLE **Nordic UART Service (NUS)** — orice telefon cu nRF Connect / aplicație compatibilă poate primi date de la mănușă și trimite comenzi.

**UUIDs NUS:**
```
Service:  6e400001-b5a3-f393-e0a9-e50e24dcca9e
TX (notif ESP32→Phone): 6e400003-...
RX (write Phone→ESP32): 6e400002-...
```

**Flow date:**
```
STM32 → UART → ESP32 → BLE GATT Notify → Telefon
Telefon → BLE Write → ESP32 → UART → STM32
```

**Format comandă primită de la telefon (4 bytes):**
```
[cmd_id 1B][finger_idx 1B][value_lo 1B][value_hi 1B]
```

**Advertising:** `"GloveAssist"` cu SMP pairing.

---

## 14. WiFi + MQTT TLS → Adafruit IO (ESP32)

**MCU**: ESP32 · **Fișier**: `application/esp32_app/src/wifi_mqtt.c`

**Ce face:**
Conectează ESP32 la WiFi (WPA2-PSK) și publică datele mănușii în cloud pe platforma Adafruit IO prin MQTT over TLS (port 8883).

**Flow:**
1. WiFi WPA2-PSK connect (`net_mgmt`)
2. IP via DHCP
3. DNS resolve `io.adafruit.com`
4. TLS handshake cu certificat CA DigiCert Global Root G2
5. MQTT connect cu `username:AIO_KEY`
6. Publish din message queue internă
7. Keepalive ping la fiecare `MQTT_KEEPALIVE_S/2`
8. Reconectare automată cu backoff exponențial

**Feed-uri Adafruit IO:**
```
deliass/feeds/glove.gesture  → "NONE"/"INDEX"/"HELP"/etc.
deliass/feeds/glove.status   → {"esp32":true,"ble":false}
```

**Credențiale** (`app_config.h`):
```c
#define WIFI_SSID           "Delia's iPhone"
#define WIFI_PSK            "DN18062022"
#define ADAFRUIT_IO_USERNAME "deliass"
#define ADAFRUIT_IO_KEY     "aio_bhTV24hKaOuQLZeee6zb1xXETAG7"
#define ADAFRUIT_IO_PORT    8883  // TLS
```

---

## 15. OTA (Over-The-Air Update) — ESP32

**MCU**: ESP32 · **Fișiere**: `esp32_app/prj.conf`, `esp32_app/boards/esp32_devkitc_procpu.overlay`, `esp32_app/src/main.c`

**Ce face:**
Permite actualizarea firmware-ului ESP32 wireless prin BLE, folosind MCUboot ca bootloader și SMP (Simple Management Protocol).

**Partiții flash** (overlay):
```
boot     : 64KB  @ 0x000000  ← MCUboot
slot0    : 960KB @ 0x010000  ← firmware activ
slot1    : 960KB @ 0x100000  ← firmware nou (primit OTA)
scratch  : 64KB  @ 0x1F0000  ← swap temporar
storage  : 64KB  @ 0x3F0000  ← NVS/settings
```

**Config** (`prj.conf`):
```
CONFIG_BOOTLOADER_MCUBOOT=y
CONFIG_MCUMGR=y
CONFIG_MCUMGR_TRANSPORT_BT=y   ← SMP over BLE
CONFIG_MCUMGR_GRP_IMG=y        ← image management
CONFIG_IMG_MANAGER=y
```

**Confirmare imagine** (după boot cu firmware nou):
```c
// main.c — după init BLE reușit
boot_write_img_confirmed();  // dacă nu e confirmat → MCUboot rollback la versiunea veche
```

**Tool OTA**: nRF Connect for Mobile → DFU → upload `.bin`

---

## 16. Threaduri Zephyr RTOS — STM32

**Fișier**: `application/stm32_app/src/main.c`

| Thread | Prioritate | Stack | Funcție |
|--------|-----------|-------|---------|
| `safety_thread` | 2 (cea mai mare) | 1024B | WDT feed + heartbeat monitor |
| `uart_comm_thread` | 4 | 1024B | TX cadre UART + RX heartbeat/comenzi |
| `sensor_thread` | 5 | 2048B | ADC + filtru + clasificare gesturi |
| `haptic_thread` | 6 (cea mai mică) | 1024B | Motor + buzzer + OLED |

**Comunicare inter-thread:**
- `sensor_msgq` (message queue): sensor_thread → uart_comm_thread (date senzori)
- `haptic_events` (k_event): orice thread → haptic_thread (notificări)
- `atomic_t start_requested`: sensor_thread ↔ calibration (flag calibrare)

---

## 17. Cod Comun (COMMON)

**Fișiere**: `application/common/`

| Fișier | Conținut |
|--------|----------|
| `include/app_config.h` | Toate constantele: praguri ADC, timing, credențiale WiFi/MQTT, config protocol |
| `include/common_types.h` | Typedefs MISRA (u8_t, u16_t...), `sensor_packet_t`, `sys_status_t`, constante degete |
| `include/frame_protocol.h` | `glove_frame_t`, `frame_command_payload_t`, MSG_TYPE_* |
| `src/frame_protocol.c` | `crc8_ccitt()`, `frame_build()`, `frame_validate()`, decode XOR |
| `include/error_codes.h` | `error_code_t` enum: ERR_SENSOR_OK, ERR_SENSOR_OPEN_CIRCUIT, ERR_SECURITY_LOCKDOWN |

---

## 18. Teste Unitare

**Fișier**: `tests/unit_tests.c` · **Platform**: `native_sim` (fără hardware)

**27 teste implementate:**

| Categorie | Nr. teste | Ce testează |
|-----------|-----------|-------------|
| CRC-8 | 4 | valori cunoscute, date goale, date mari |
| Frame build/validate | 11 | SOF corect, CRC valid, SOF greșit, payload corupt |
| SEQ counter | 2 | incrementare, wrap-around la 255→0 |
| XOR decode | 3 | encode-decode simetric, key base |
| Payload size | 2 | PROTO_FRAME_SIZE = 12, PROTO_PAYLOAD_SIZE = 8 |
| Config sanity | 5 | praguri > 0, THRESH_INDEX < THRESH_RING, timeout valori |

**Rulare:**
```powershell
west build -b native_sim tests/ --build-dir build/tests
./build/tests/zephyr/zephyr.exe
```
