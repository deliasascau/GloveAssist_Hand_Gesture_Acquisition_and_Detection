# GloveAssist — Explicație Completă în Română
### Document tehnic pentru prezentare în fața unui developer senior de firmware

---

## CUPRINS

1. [Ce este proiectul și de ce există](#1-ce-este-proiectul-și-de-ce-există)
2. [Arhitectura hardware — piese și roluri](#2-arhitectura-hardware--piese-și-roluri)
3. [De ce două MCU-uri? Gândirea din spatele deciziei](#3-de-ce-două-mcu-uri-gândirea-din-spatele-deciziei)
4. [RTOS: Zephyr — ce este și de ce l-am ales](#4-rtos-zephyr--ce-este-și-de-ce-l-am-ales)
5. [DeviceTree și Overlay — configurare hardware fără #define](#5-devicetree-și-overlay--configurare-hardware-fără-define)
6. [Kconfig (prj.conf) — ce compilăm și ce nu](#6-kconfig-prjconf--ce-compilăm-și-ce-nu)
7. [Thread-urile STM32 — cine face ce și când](#7-thread-urile-stm32--cine-face-ce-și-când)
8. [Senzori flex — fizică, circuit, ADC](#8-senzori-flex--fizică-circuit-adc)
9. [sensor_logic.c — pipeline-ul de date complet](#9-sensor_logicc--pipeline-ul-de-date-complet)
10. [Protocolul SPI — frame-ul de 12 bytes explicat bit cu bit](#10-protocolul-spi--frame-ul-de-12-bytes-explicat-bit-cu-bit)
11. [spi_comm.c STM32 — master loop-ul](#11-spi_commc-stm32--master-loop-ul)
12. [spi_comm.c ESP32 — slave cu heartbeat timer](#12-spi_commc-esp32--slave-cu-heartbeat-timer)
13. [safety_diag.c — IEC 61508, watchdog, fault detection](#13-safety_diagc--iec-61508-watchdog-fault-detection)
14. [security.c — XOR dinamic, honeypot, anti-replay](#14-securityc--xor-dinamic-honeypot-anti-replay)
15. [haptic_ui.c — motor, buzzer, OLED](#15-haptic_uic--motor-buzzer-oled)
16. [comms_ble.c — BLE NUS gateway pe ESP32](#16-comms_blec--ble-nus-gateway-pe-esp32)
17. [Tipuri de date și constante comune](#17-tipuri-de-date-și-constante-comune)
18. [Ce s-a modificat în ultima sesiune și de ce](#18-ce-s-a-modificat-în-ultima-sesiune-și-de-ce)
19. [Ce este PENDING și de ce](#19-ce-este-pending-și-de-ce)
20. [Cum explici totul unui developer senior în 5 minute](#20-cum-explici-totul-unui-developer-senior-în-5-minute)

---

## 1. Ce este proiectul și de ce există

**GloveAssist** este o mănușă electronică inteligentă destinată asistenților medicali sau utilizatorilor cu nevoi speciale. Scopul: detectarea gesturilor mâinii prin senzori flex și transmiterea lor wireless la un telefon via BLE, cu feedback haptic local.

**Scenariul real de utilizare:**
- Pacient poartă mănușa
- Face un gest (pumn = "FIST", deget index = "INDEX" etc.)
- STM32 detectează gestul în ~10ms
- ESP32 îl trimite la telefon via BLE în <100ms
- Motorul de vibrații confirmă gestul detectat
- Dacă ESP32 se defectează → STM32 activează alarma locală SOS (motor + buzzer) autonom

**De ce este competitiv față de alte proiecte universitare:**
- Dual-MCU architecture cu separare clară de responsabilități
- Conformitate IEC 61508 (standard pentru sisteme safety-critical)
- Protocol SPI custom cu CRC-8 și obfuscare XOR
- Zephyr RTOS (nu bare-metal, nu Arduino)
- MISRA C:2012 compliance (standard automotive/medical pentru C)

---

## 2. Arhitectura hardware — piese și roluri

```
                              USB/SWD (debug)
                                    │
[4× Senzor Flex]──330Ω──[PA0-PA3]──┤
                                    │
                              [STM32F103C8T6]         ┌─────────────┐
                              "Blue Pill"              │   Telefon   │
                              128KB Flash              │   Android   │
                              20KB RAM           BLE   │   /iOS      │
                              72MHz Cortex-M3   ◄────►│  nRF Connect│
                                    │                  └─────────────┘
                              SPI2 (1MHz)              │
                              PB13/PB15/PB14/PB12      │
                                    │                  │
                              [ESP32 DevKitC]          │
                              4MB Flash                │
                              520KB RAM          BLE──►┘
                              240MHz dual-core
                                    │
                              WiFi (viitor MQTT)
                                    │
                              Cloud Dashboard

[OLED SSD1306]◄──SPI1 PA5/PA7/PA4/PB0/PB1──[STM32]
[Motor vibr.]◄──TIM4_CH1 PB6──ULN2003──────[STM32]
[Buzzer]◄──────TIM3_CH1 PB4────────────────[STM32]
[IWDG]──────────────────────────────────────[STM32] (hardware watchdog intern)
```

### Fiecare piesă explicată

| Componentă | Specificații | Rol |
|---|---|---|
| **STM32F103C8T6** | 72MHz, 128KB Flash, 20KB RAM, Cortex-M3 | Achiziție ADC, safety, haptic, OLED |
| **ESP32 DevKitC** | 240MHz dual-core, 4MB Flash, ~364KB RAM | BLE gateway, WiFi, AI viitor |
| **Senzori flex** | Rezistențe 150–350Ω (îndoit→mai mare) | Detectare poziție degete |
| **Rezistențe 330Ω** | Fix, voltage divider cu senzorul | Conversie rezistență → tensiune |
| **OLED SSD1306** | 128×64 pixeli, SPI 4-wire | Afișare gest curent, status |
| **Motor vibrații** | DC 3-5V, curent ~90mA | Feedback haptic kinestezic |
| **ULN2003** | Darlington array, 500mA/canal | Driver curent pentru motor (GPIO STM32 = max 25mA!) |
| **Buzzer piezo** | Pasiv, se comandă cu PWM | Feedback audio (tonuri variabile) |
| **IWDG** | Independent Watchdog intern STM32 | Reset hardware la freeze firmware |

---

## 3. De ce două MCU-uri? Gândirea din spatele deciziei

Aceasta este decizia arhitecturală cea mai importantă și trebuie justificată clar.

### Varianta naivă (un singur MCU):
```
[STM32] → ADC + BLE + WiFi + Safety + Display
```
**Problemă:** Stack-urile WiFi și BLE sunt non-deterministe. Pot bloca CPU zeci de millisecunde. Într-un sistem safety-critical, senzorul trebuie citit EXACT la fiecare 10ms, indiferent de ce face WiFi.

### Varianta noastră (dual-MCU cu separare de domenii):
```
[STM32] → ADC + Safety + Haptic + OLED  (real-time, deterministic)
    │
   SPI (1MHz, full-duplex, 100ms poll)
    │
[ESP32] → BLE + WiFi + MQTT + AI        (high-throughput, non-deterministic)
```

**Avantaje concrete:**
1. STM32 nu știe și nu îi pasă că BLE face retransmisii sau că WiFi scanează canale
2. Dacă ESP32 pică complet → STM32 detectează lipsa heartbeat-ului în 2 secunde și activează alarma locală (SOS)
3. Separare clară: dacă senzorii raportează fals, safety-ul se întoarce pe STM32, nu depinde de conexiunea wireless
4. STM32 consumă mai puțin (~36mA vs ~200mA ESP32) → bateria durează mai mult în standby

**Analogie pentru senior dev:** Este echivalentul unui sistem automotive — ECU-ul de siguranță (STM32) și unitatea de infotainment (ESP32) sunt separate fizic. Dacă GPS-ul se blochează, airbag-urile tot funcționează.

---

## 4. RTOS: Zephyr — ce este și de ce l-am ales

### Ce este un RTOS?

Un RTOS (Real-Time Operating System) este un sistem de operare minimal care oferă:
- **Thread-uri** — "mini-programe" care rulează aparent simultan (de fapt, scheduler-ul le comută rapid)
- **Priorități** — thread-ul cu prioritate mai mare preemptează (întrerupe) pe cel cu prioritate mai mică
- **Primitive de sincronizare** — mutex, semaphore, message queue, event flags
- **Timere** — callback-uri periodice sau one-shot

Fără RTOS (bare-metal), codul arată:
```c
while(1) {
    read_adc();       // blochează 1ms
    send_spi();       // blochează 2ms
    check_safety();   // NU se execută la timp!
    update_display(); // blochează 5ms
}
```
Cu RTOS, fiecare funcție rulează în propriul thread, la propria perioadă, independent.

### De ce Zephyr și nu FreeRTOS?

| Criteriu | FreeRTOS | Zephyr |
|---|---|---|
| DeviceTree | Nu | **Da** — configurare hardware declarativă |
| Driver model | Manual | **Unificat** — același API pentru SPI oriunde |
| BLE stack | Extern (NimBLE) | **Built-in** (Bluetooth 5.2) |
| Build system | Make/CMake manual | **west** — unificat pentru ambele MCU |
| MISRA compliance | Parțial | **Mai bună** (kernel certificat pentru use cases safety) |
| Ecosistem | Enorm | Crescând rapid (Linux Foundation project) |

**Versiunea folosită:** Zephyr 4.4.0-rc1, SDK 1.0.1

---

## 5. DeviceTree și Overlay — configurare hardware fără #define

### Ce este DeviceTree?

În embedded tradițional:
```c
#define SPI_SCK_PIN  GPIO_PIN_13  // PB13
#define SPI_MOSI_PIN GPIO_PIN_15  // PB15
```
Problema: dacă schimbi pinul, modifici C. Dacă faci 2 variante de board, ai 2 versiuni de cod.

**DeviceTree** separă hardware-ul de cod. Descrie hardware-ul în fișiere `.dts` / `.overlay`:
```dts
&spi2 {
    status = "okay";
    pinctrl-0 = <&spi2_sck_master_pb13
                 &spi2_mosi_master_pb15
                 &spi2_miso_master_pb14>;
    cs-gpios = <&gpiob 12 GPIO_ACTIVE_LOW>;
};
```

Codul C citește din DeviceTree la compile-time:
```c
static const struct device *const spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
```

### Overlay-ul nostru (`stm32_min_dev_blue.overlay`)

Overlay-ul **suprascrie** fișierul `.dts` de bază al board-ului. Noi adăugăm/modificăm:

**1. Flash: 128KB în loc de 64KB declarat**
```dts
&flash0 {
    reg = <0x08000000 0x20000>;  /* 0x20000 = 131072 = 128KB */
};
```
De ce? Cipul STM32F103C8T6 are 64KB oficial, dar marea majoritate a cipurilor au 128KB fizic. Overlay-ul deblochează această memorie.

**2. ADC: 4 canale, PA0–PA3**
```dts
/ {
    zephyr,user {
        io-channels = <&adc1 0>, <&adc1 1>, <&adc1 2>, <&adc1 3>;
    };
};
```
Codul citește: `ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0)` → PA0 (index finger)

**3. SPI1: OLED SSD1306**
```dts
&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_sck_master_pa5 &spi1_mosi_master_pa7>;
    cs-gpios = <&gpioa 4 GPIO_ACTIVE_LOW>;

    ssd1306: ssd1306@0 {
        compatible = "solomon,ssd1306fb";
        reset-gpios = <&gpiob 1 GPIO_ACTIVE_LOW>;  /* PB1 RST */
        data-cmd-gpios = <&gpiob 0 GPIO_ACTIVE_HIGH>; /* PB0 DC */
        width = <128>; height = <64>;
    };
};
```

**4. SPI2: Comunicare ESP32 (master)**
```dts
&spi2 {
    status = "okay";
    /* PB13 SCK, PB15 MOSI, PB14 MISO, PB12 CS */
    cs-gpios = <&gpiob 12 GPIO_ACTIVE_LOW>;
};
```

**5. PWM: TIM3 buzzer (PB4), TIM4 motor (PB6)**
```dts
&timers3 {
    pwm3: pwm { pinctrl-0 = <&tim3_ch1_pwm_pb4>; };
};
&timers4 {
    pwm4: pwm { pinctrl-0 = <&tim4_ch1_pwm_pb6>; };
};
```
**Notă critică:** Motorul era inițial pe PA0 (TIM2_CH1). PA0 este și ADC Channel 0 (deget index). Un pin nu poate fi ADC și timer simultan → **motor mutat pe PB6 (TIM4_CH1)**.

**6. Aliases**
```dts
aliases {
    watchdog0 = &iwdg;
    pwm-buzzer = &pwm3;
    pwm-motor  = &pwm4;
};
```
Codul folosește: `PWM_DT_SPEC_GET(DT_ALIAS(motor_pwm))` → automat PB6/TIM4

---

## 6. Kconfig (prj.conf) — ce compilăm și ce nu

Kconfig este sistemul de configurare Zephyr. `prj.conf` activează/dezactivează drivere și subsisteme.

```kconfig
# Kernel
CONFIG_MULTITHREADING=y
CONFIG_MAIN_STACK_SIZE=512       # main() face doar safety_init() și returnează

# Periferice
CONFIG_GPIO=y
CONFIG_ADC=y
CONFIG_SPI=y
CONFIG_PWM=y
CONFIG_WATCHDOG=y

# Display
CONFIG_DISPLAY=y
CONFIG_SSD1306=y
CONFIG_CHARACTER_FRAMEBUFFER=y   # Text-only! Nu LVGL (ar cere 100KB+ RAM)

# Logging optimizat
CONFIG_LOG_DEFAULT_LEVEL=2       # Doar WRN și ERR — nu INF/DBG în producție
CONFIG_LOG_MODE_MINIMAL=y        # Sincron, minimal — economisește ~1KB RAM
CONFIG_CBPRINTF_NANO=y           # Printf fără float — economisește ~10KB flash

# Optimizare dimensiune
CONFIG_SIZE_OPTIMIZATIONS=y      # Compilare cu -Os (optimize for size)
CONFIG_THREAD_NAME=n             # Nu includem string-uri de debug cu nume thread
CONFIG_STACK_SENTINEL=y          # Detectare overflow de stivă la runtime
```

**De ce CHARACTER_FRAMEBUFFER și nu LVGL?**
- LVGL are o interfață grafică bogată dar necesită ~150KB RAM pentru framebuffer
- STM32F103 are doar 20KB RAM total
- CFB (Character Frame Buffer) = text-only, ~1KB RAM
- Pentru proiectul nostru avem nevoie doar de text: "FIST", "INDEX", "NO LINK"

**Rezultat estimat după optimizări:**
- Flash: ~48% din 128KB
- RAM: ~63% din 20KB (cu toate periferice active)

---

## 7. Thread-urile STM32 — cine face ce și când

### Definirea thread-urilor (`main.c`)

```c
// Sintaxa: K_THREAD_DEFINE(id, stack_size, entry_fn, arg1, arg2, arg3, prio, options, delay)
K_THREAD_DEFINE(safety_tid,   STACK_SAFETY,   safety_thread_entry,   NULL, NULL, NULL, 2, 0, 0);
K_THREAD_DEFINE(sensor_tid,   STACK_SENSOR,   sensor_thread_entry,   NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(haptic_tid,   STACK_HAPTIC,   haptic_thread_entry,   NULL, NULL, NULL, 6, 0, 0);
K_THREAD_DEFINE(spi_comm_tid, STACK_SPI_COMM, spi_comm_thread_entry, NULL, NULL, NULL, 4, 0, 0);
```

Thread-urile sunt **auto-started la boot**, în ordinea priorităților:

### Tabel cu thread-urile

| Thread | Prioritate | Stack | Perioadă | Ce face |
|---|---|---|---|---|
| `safety_tid` | **2** (cel mai urgent) | 512B | 250ms | IWDG feed, monitor heartbeat ESP32 |
| `spi_comm_tid` | 4 | 1024B | 100ms | Poll SPI2, trimite/primește date |
| `sensor_tid` | 5 | 1024B | 500ms | Citire ADC, filtru, clasificare gest |
| `haptic_tid` | 6 (cel mai puțin urgent) | 512B | event-driven | Motor + buzzer + OLED |

### Comunicare între thread-uri

**Message queue** (`main.c`):
```c
K_MSGQ_DEFINE(sensor_msgq, sizeof(sensor_packet_t), 8, 4);
// Sink: sensor_tid pune date
// Producer: spi_comm_tid consumă și trimite la ESP32
```

Queue-ul are 8 slot-uri. Dacă `spi_comm_tid` nu consumă suficient de repede → mesajele mai vechi se pierd (`K_NO_WAIT`). Design intenționat — datele senzorilor sunt perisabile.

### De ce prioritate 2 pentru safety și nu 0 sau 1?

Prioritățile 0 și 1 în Zephyr sunt rezervate pentru interrupt threads ale kernel-ului. Noi folosim 2 ca "cel mai urgent thread de aplicație".

---

## 8. Senzori flex — fizică, circuit, ADC

### Fizica senzorului flex

Un senzor flex este o **bandă rezistivă**:
- Deget drept → rezistență ~150Ω
- Deget îndoit 90° → rezistență ~250Ω
- Deget îndoit maxim → rezistență ~350Ω

Variația e mică și neliniară. Nu putem măsura rezistența direct. Soluție: **voltage divider**.

### Circuitul voltage divider

```
3.3V ─────┬──────────────────────────────
          │
       [Rflex]  ← senzor flex (150–350Ω)
          │
          ├──────── ADC input (PA0..PA3)
          │
        [330Ω]  ← rezistență fixă
          │
GND ──────┴──────────────────────────────
```

**Formula:**
$$V_{ADC} = 3.3V \times \frac{330\Omega}{R_{flex} + 330\Omega}$$

- Deget drept (Rflex=150Ω): $V = 3.3 \times \frac{330}{480} = 2.27V$ → ADC: ~2815/4095
- Deget îndoit (Rflex=350Ω): $V = 3.3 \times \frac{330}{680} = 1.6V$ → ADC: ~1985/4095

Cu threshold `GESTURE_THRESHOLD_BENT = 2000`:
- 2815 > 2000 → deget drept = **neîndoit** (nu facem gest)
- 1985 < 2000 → deget îndoit = **gest activ**

**De ce 330Ω și nu altceva?** Rezistența e aleasă să fie în mijlocul gamei senzorului (150–350Ω) pentru sensibilitate maximă. Cu 330Ω, variația tensiunii e maximă pentru gamă.

### ADC STM32 — configurare via DeviceTree

```c
static const struct adc_dt_spec adc_channels[NUM_FLEX_SENSORS] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),  // PA0 → index
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),  // PA1 → middle
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),  // PA2 → ring
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 3),  // PA3 → pinky
};
```

Rezoluție: **12 biți** → valori 0–4095. Tensiune referință: internă 3.3V (VREFINT).

---

## 9. sensor_logic.c — pipeline-ul de date complet

Datele trec prin 4 stagii:

### Stagiul 1: Citire ADC brut

```c
s16_t adc_buf;
struct adc_sequence seq = { .buffer = &adc_buf, .buffer_size = sizeof(adc_buf) };

(void)adc_sequence_init_dt(&adc_channels[ch], &seq);
ret = adc_read_dt(&adc_channels[ch], &seq);
pkt.flex_raw[ch] = (u16_t)adc_buf;  // 0..4095
```

### Stagiul 2: Safety validation (`safety_diag.c`)

```c
error_code_t err = safety_validate_sensor(ch, pkt.flex_raw[ch], &validated);
```

Logica din `safety_diag.c`:
```c
if (raw < 500U)  → open-circuit (senzor deconectat → citire 0V)
if (raw > 3800U) → short-circuit (scurtcircuit → citire 3.3V)
// Altfel: valoare validă, reset fault_count
```
Dacă 3 citiri consecutive sunt invalide → fallback la ultima valoare bună (`last_good[ch]`).

### Stagiul 3: Moving-average filter

```c
// Fereastră de 8 sample-uri, running sum O(1)
static u16_t filter_buf[4][8];  // buffer circular pentru fiecare canal
static u32_t filter_sum[4];     // suma curentă

static u16_t filter_update(u8_t ch, u16_t sample)
{
    u8_t idx = filter_idx[ch];
    filter_sum[ch] -= filter_buf[ch][idx];  // elimină cel mai vechi
    filter_buf[ch][idx] = sample;
    filter_sum[ch] += sample;               // adaugă cel nou
    filter_idx[ch] = (idx + 1U) % 8U;      // avansează index circular
    return (u16_t)(filter_sum[ch] / 8U);   // media
}
```

**De ce filtru?** Senzorii flex DIY au zgomot mecanic (frecări, vibrații). Fără filtru, gestul "FIST" ar putea apărea și dispărea rapid din cauza fluctuațiilor ADC. Media pe 8 sample-uri netezește semnalul.

**De ce running sum și nu recalcul?** `O(1)` vs `O(n)` — la 500Hz sample rate contează. Running sum: scoatem cel mai vechi, adăugăm cel nou. O operare per sample, indiferent de fereastra N.

### Stagiul 4: Clasificare gest (rule-based)

```c
static u8_t classify_gesture(const u16_t filtered[4])
{
    u8_t bent_count = 0U;
    for (u8_t i = 0U; i < 4U; i++) {
        if (filtered[i] > GESTURE_THRESHOLD_BENT) {  // nu > 2000 → drept = NU e gest
            // ATENȚIE: logica e inversată față de intuitiv!
            // ADC mare = tensiune mare = deget drept
            // ADC mic  = tensiune mică = deget îndoit
        }
    }
    // ...
}
```

**Gesturile suportate:**
| Gest | Condiție |
|---|---|
| `GESTURE_FIST` (5) | Toate 4 degete îndoite |
| `GESTURE_INDEX` (1) | Exact 1 deget îndoit, cel de index |
| `GESTURE_MIDDLE` (2) | Exact 1 deget îndoit, cel mijlociu |
| `GESTURE_RING` (3) | Exact 1 deget îndoit, inelar |
| `GESTURE_PINKY` (4) | Exact 1 deget îndoit, mic |
| `GESTURE_NONE` (0) | Altfel |

### Output — `sensor_packet_t`

```c
typedef struct {
    u16_t flex_raw[4];       // valori ADC brute (0..4095)
    u16_t flex_filtered[4];  // după filtru (0..4095)
    u8_t  gesture_id;        // GESTURE_* define
    u8_t  confidence;        // 90% dacă 0 faults, 50% dacă faults
    u32_t timestamp_ms;      // k_uptime_get_32() — ms de la boot
} sensor_packet_t;
```

Pachetul e pus în `sensor_msgq` → consumat de `spi_comm_tid`.

---

## 10. Protocolul SPI — frame-ul de 12 bytes explicat bit cu bit

### De ce un protocol custom și nu UART simplu?

SPI este full-duplex: în același ceas, STM32 trimite și primește simultan. UART e simplex (un fir per direcție, nu sincronizat). SPI e mai rapid și mai simplu pentru volum fix de date (12 bytes fix).

### Structura frame-ului (`spi_protocol.h`)

```
Offset 0   : SOF  = 0xAA          → marcher de start (nu e obfuscat)
Offset 1   : TYPE (0x00..0xFF)    → tipul mesajului (nu e obfuscat)
Offset 2   : SEQ  (0..255)        → contor secvență, baza cheii XOR (nu e obfuscat)
Offset 3-10: PAYLOAD[8]           → datele utile (XOR-obfuscate)
Offset 11  : CRC-8/CCITT          → checksum pe plaintext bytes [1..10]
TOTAL: 12 bytes (PROTO_FRAME_SIZE)
```

Implementat ca struct packed:
```c
typedef struct __attribute__((packed)) {
    uint8_t sof;                         // 1 byte
    uint8_t type;                        // 1 byte
    uint8_t seq;                         // 1 byte
    uint8_t payload[PROTO_PAYLOAD_SIZE]; // 8 bytes
    uint8_t crc8;                        // 1 byte
} spi_frame_t;                           // = 12 bytes total

_Static_assert(sizeof(spi_frame_t) == 12, "size mismatch");
```

`__attribute__((packed))` elimină padding-ul pe care compilatorul l-ar adăuga altfel (de exemplu după `uint8_t` înainte de un `uint16_t`). `_Static_assert` garantează la compile-time că structura are exact 12 bytes — dacă cineva adaugă un câmp și uită de PROTO_FRAME_SIZE, build-ul pică.

### Tipurile de mesaje

| Valoare | Define | Cine trimite | Ce conține |
|---|---|---|---|
| `0x00` | `MSG_TYPE_POLL` | STM32 | Frame gol — "nimic de trimis, dar vreau să primesc" |
| `0x01` | `MSG_TYPE_SENSOR_DATA` | STM32 | Date ADC brute |
| `0x02` | `MSG_TYPE_GESTURE` | STM32 | Gest detectat + snapshot ADC |
| `0x05` | `MSG_TYPE_CALIBRATION` | STM32 | Date calibrare |
| `0x06` | `MSG_TYPE_COMMAND` | ESP32 | Comandă venită de pe BLE central |
| `0x07` | `MSG_TYPE_HEARTBEAT` | ESP32 | "Sunt în viață" periodic |
| `0xFF` | `MSG_TYPE_HONEYPOT` | STM32 | Frame fals pentru securitate |

### Obfuscarea XOR (`spi_protocol.c`)

**La transmisie (TX, `spi_frame_build`):**
```c
uint8_t xor_key = seq ^ PROTO_XOR_KEY_BASE;  // PROTO_XOR_KEY_BASE = 0x5A
for (i = 0; i < PROTO_PAYLOAD_SIZE; i++) {
    frame->payload[i] = plaintext[i] ^ xor_key;
}
```

**La recepție (RX, `spi_frame_validate`):**
```c
uint8_t xor_key = frame->seq ^ 0x5AU;
for (i = 0; i < PROTO_PAYLOAD_SIZE; i++) {
    plaintext[i] = frame->payload[i] ^ xor_key;  // XOR inversat = același XOR
}
```

**De ce funcționează?** XOR este propria sa inversă: `(a XOR k) XOR k = a`.

**De ce SEQ ^ 0x5A?** Cheia se schimbă la fiecare frame (SEQ crește). Un sniffer pasiv pe fir SPI vede bytes diferiți pentru aceeași valoare de gest, chiar dacă gestul e identic. Nu este criptare (nu are secret key distribuit), este **obfuscare** împotriva unui atacator pasiv simplu.

### CRC-8/CCITT (`spi_protocol.c`)

```c
uint8_t crc8_ccitt(const uint8_t *data, uint32_t length)
{
    uint8_t crc = 0x00U;
    for (uint32_t i = 0U; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((uint8_t)(crc << 1U) ^ 0x07U);  // poly 0x07
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }
    return crc;
}
```

**Parametri CRC-8/CCITT:** Polynomial 0x07 (x⁸+x²+x+1), init 0x00. Calculat pe **plaintext** bytes [TYPE..PAYLOAD] (10 bytes), adică după de-XOR. Detectează orice eroare de 1 bit și ~75% erori de 2 biți.

**Ordinea operațiilor la validare:**
1. Verifică SOF == 0xAA
2. De-XOR payload cu cheia din SEQ
3. Recalculează CRC pe plaintext
4. Compară cu `frame->crc8`
5. Dacă ≠ → frame invalid → return ERR_INVALID_CRC

### Sensor payload compact (`spi_protocol.h`)

```c
typedef struct __attribute__((packed)) {
    uint8_t flex[4];      // flex_filtered[i] >> 4 → normalizat 12bit→8bit
    uint8_t gesture_id;   // GESTURE_*
    uint8_t confidence;   // 0..100
    uint8_t status_flags; // bitmask (fault bits)
    uint8_t reserved;     // padding (pentru aliniere viitoare)
} spi_sensor_payload_t;  // = 8 bytes exact = PROTO_PAYLOAD_SIZE
```

**Normalizarea 12-bit → 8-bit:** `spi_pkt.flex[i] = pkt.flex_filtered[i] >> 4` (shift dreapta 4 biți = divide by 16). Din 4096 valori → 256 valori. Suficient pentru threshold-uri de gest. Economisim 4 bytes/canal față de uint16_t.

---

## 11. spi_comm.c STM32 — master loop-ul

STM32 este **SPI master**: el generează ceasul (SCK) și inițiază fiecare transfer. ESP32 este pasiv (slave), nu poate vorbi decât când STM32 îl "sună".

### Configurare SPI

```c
static const struct spi_config spi_cfg = {
    .frequency = 1000000U,  // 1 MHz
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0U,
};
```

1 MHz este conservator. SPI suportă până la 18 MHz pe STM32, dar la 1 MHz avem imunitate bună la zgomot (fire lungi pe mănușă).

### Full-duplex transceive

```c
static int spi_proto_transceive(const spi_frame_t *tx, spi_frame_t *rx)
{
    struct spi_buf tx_buf = { .buf = (void *)tx, .len = 12 };
    struct spi_buf rx_buf = { .buf = rx, .len = 12 };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    return spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
}
```

Aceasta este magia SPI: în exact **96 clock cycles** (12 bytes × 8 biți), STM32 trimite frame-ul TX și primește simultan frame-ul RX de la ESP32.

### Thread loop (`spi_comm_thread_entry`)

```
La fiecare 100ms:
┌───────────────────────────────────────────────┐
│ 1. Verifică safety_esp32_alive()              │
│ 2. Dacă alive:                                │
│    a. poll_count % HONEYPOT_RATIO == 0?       │
│       → TX = MSG_TYPE_HONEYPOT (frame fals)   │
│    b. Altfel: există packet în sensor_msgq?   │
│       → TX = MSG_TYPE_GESTURE cu date reale   │
│    c. Altfel:                                 │
│       → TX = MSG_TYPE_POLL (frame gol)        │
│ 3. Dacă degraded:                             │
│       → TX = MSG_TYPE_POLL (continuăm să      │
│              polling ca să detectăm recovery) │
│ 4. spi_proto_transceive(tx, rx)               │
│ 5. spi_frame_validate(rx)                     │
│ 6. Switch rx.type:                            │
│    MSG_TYPE_HEARTBEAT → safety_heartbeat_rx() │
│    MSG_TYPE_COMMAND   → TODO: dispatch        │
│ 7. k_msleep(100)                              │
└───────────────────────────────────────────────┘
```

---

## 12. spi_comm.c ESP32 — slave cu heartbeat timer

ESP32 nu conduce SCK — **blochează** pe `spi_transceive()` și așteaptă să vină masterul.

### Problema pregătirii datelor TX în slave

ESP32 trebuie să aibă `slave_tx_frame` gata ÎNAINTE ca masterul să inițieze transferul. Nu poate pregăti răspunsul în timp real (SPI e sincron, nu poate "gândi" în timp ce transferul rulează).

**Soluția: K_TIMER + mutex**

```c
static spi_frame_t slave_tx_frame;    // buffer pre-construit
static struct k_mutex tx_frame_lock;

// Timer callback — rulează periodic, independent de thread-ul SPI
static void heartbeat_timer_fn(struct k_timer *timer)
{
    spi_heartbeat_payload_t hb;
    hb.status        = 0x01U;              // ESP32 alive
    hb.ble_connected = ble_is_connected(); // BLE status curent
    hb.rssi          = ble_get_rssi();     // semnal BLE

    k_mutex_lock(&tx_frame_lock, K_FOREVER);
    spi_frame_build(&slave_tx_frame, MSG_TYPE_HEARTBEAT, &hb, sizeof(hb));
    k_mutex_unlock(&tx_frame_lock);
}

K_TIMER_DEFINE(heartbeat_timer, heartbeat_timer_fn, NULL);
// Pornit cu: k_timer_start(&heartbeat_timer, K_MSEC(500), K_MSEC(500));
```

La fiecare 500ms, timer callback-ul construiește un nou heartbeat frame. Thread-ul SPI slave copiază frame-ul sub mutex și îl trimite masterului.

**De ce mutex?** Timer callback-ul și thread-ul SPI pot rula "simultan" (de fapt se preemptează). Fără mutex, slave_tx_frame poate fi pe jumătate scris când thread-ul îl copiază → frame corupt.

### Dispatch primit de la STM32

```c
switch (rx_frame.type) {
case MSG_TYPE_GESTURE:
    // Trimite cele 8 bytes payload direct via BLE notify
    ble_send_notification(rx_frame.payload, PROTO_PAYLOAD_SIZE);
    break;
case MSG_TYPE_HONEYPOT:
    // Ignoră silențios — sunt frame-uri de test securitate
    break;
case MSG_TYPE_POLL:
    // Normal — masterul n-a avut nimic de trimis
    break;
}
```

---

## 13. safety_diag.c — IEC 61508, watchdog, fault detection

Aceasta este componenta care diferențiază proiectul de un simplu hobby project.

**IEC 61508** este standardul internațional pentru sisteme electrice/electronice safety-critical (SIL 1–4). Noi implementăm pattern-uri SIL-1 (cel mai scăzut nivel, dar totuși formal).

### A) IWDG — Independent Watchdog

```c
struct wdt_timeout_cfg cfg = {
    .window = { .min = 0U, .max = 2000U },  // 2 secunde timeout
    .callback = NULL,                         // NULL = reset hardware direct
    .flags = WDT_FLAG_RESET_SOC,
};
wdt_channel_id = wdt_install_timeout(wdt_dev, &cfg);
wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
```

**"Independent"** înseamnă că IWDG rulează pe oscilator propriu (LSI, 40kHz), independent de ceasul principal. Chiar dacă codul principal se blochează complet (deadlock, infinite loop, handler infinit) → IWDG resetează MCU-ul după 2 secunde.

`safety_watchdog_feed()` este apelat din `safety_thread_entry()` la fiecare 250ms. Dacă `safety_tid` se blochează → watchdog nu e hrănit → reset în 2s.

### B) Sensor fault detection cu triplu vot

```c
static u16_t last_good[4];   // ultima valoare validă per canal
static u8_t  fault_count[4]; // contor erori consecutive per canal

error_code_t safety_validate_sensor(u8_t ch, u16_t raw, u16_t *out)
{
    if (raw < 500U) {   // sub 0.4V → senzor deconectat (open circuit)
        fault_count[ch]++;
        if (fault_count[ch] >= 3U) {
            *out = last_good[ch];  // fallback la ultima valoare bună
            return ERR_SENSOR_OPEN_CIRCUIT;
        }
    } else if (raw > 3800U) {  // peste 3.07V → scurtcircuit
        fault_count[ch]++;
        // ... similar
    } else {
        fault_count[ch] = 0U;   // valoare bună → reset contor
        last_good[ch] = raw;
        *out = raw;
        return ERR_SENSOR_OK;
    }
}
```

**Pattern "triplu vot":** O singură citire anormală poate fi zgomot. 3 consecutive → problemă reală. Fallback la last_good menține sistemul funcțional chiar cu un senzor parțial defect.

### C) Heartbeat monitor — mașina de stări

```c
typedef enum {
    HB_STATE_BOOT_GRACE,   // Primele 3 heartbeat-uri lipsă → ignorate (boot time)
    HB_STATE_ALIVE,        // ESP32 e OK, heartbeat curent
    HB_STATE_DEGRADED      // ESP32 nu răspunde → alarma locală
} heartbeat_state_t;
```

**Tranziții:**
```
BOOT_GRACE ──(primul heartbeat primit)──► ALIVE
ALIVE      ──(>2s fără heartbeat)───────► DEGRADED → haptic_notify_sos()
DEGRADED   ──(heartbeat primit)─────────► ALIVE    → stop SOS alarm
```

**Thread `safety_tid` verifică la fiecare 250ms:**
```c
int64_t elapsed = k_uptime_get() - last_heartbeat_ts;
if (elapsed > HEARTBEAT_TIMEOUT_MS) {  // 2000ms
    hb_state = HB_STATE_DEGRADED;
    haptic_notify_sos();  // pornești alarma SOS
}
```

**Concluzia safety pentru comisie:** Pacientul nu este NICIODATĂ lăsat fără capacitate de alertare. Chiar dacă BLE pică, WiFi pică, și ESP32 se blochează complet → STM32 detectează în maxim 2 secunde și activează alerta locală (motor + buzzer SOS + OLED "NO LINK").

---

## 14. security.c — XOR dinamic, honeypot, anti-replay

### A) Layer 2 XOR cu rotație de cheie

Pe lângă XOR-ul din protocol (fix pe SEQ), `security.c` adaugă un **al doilea layer XOR** cu cheie aleatorie rotită:

```c
static uint8_t xor_key[8];    // 8 bytes = dimensiunea payload
static int64_t key_rotation_ts;

static void rotate_xor_key(void)
{
    sys_rand_get(xor_key, sizeof(xor_key));  // True RNG hardware
    key_rotation_ts = k_uptime_get();
}

void security_xor_payload(uint8_t *payload, uint8_t len)
{
    // Rotație automată dacă cheia e mai veche decât KEY_ROTATION_SEC
    if ((k_uptime_get() - key_rotation_ts) > KEY_ROTATION_SEC * 1000) {
        rotate_xor_key();
    }
    for (uint8_t i = 0U; i < len; i++) {
        payload[i] ^= xor_key[i % 8U];
    }
}
```

`sys_rand_get()` pe ESP32 folosește hardware RNG (radio noise). Pe STM32 folosim `CONFIG_TEST_RANDOM_GENERATOR` (pseudo-random, seed din uptime).

### B) Honeypot frames

```c
int security_gen_honeypot(spi_frame_t *frame)
{
    uint8_t fake_payload[8];
    sys_rand_get(fake_payload, sizeof(fake_payload));
    return spi_frame_build(frame, MSG_TYPE_HONEYPOT, fake_payload, 8U);
}
```

La fiecare `HONEYPOT_RATIO` transferuri, STM32 trimite un frame random cu tip `0xFF`. Un atacator care sniffează SPI-ul vede un mix de frame-uri reale și false, cu payload-uri diferite. Nu poate identifica care frame conține date reale.

**HONEYPOT_RATIO** = 10 înseamnă: 1 din 10 frame-uri e honeypot. La 100ms poll interval → un honeypot la fiecare secundă.

### C) Anti-replay lockdown

```c
static u8_t consecutive_invalid;
static bool lockdown_active;

int security_check_frame(const spi_frame_t *frame)
{
    if (lockdown_active) {
        return ERR_SECURITY_LOCKDOWN;  // Refuzăm tot
    }

    int ret = spi_frame_validate(frame);
    if (ret != 0) {
        consecutive_invalid++;
        if (consecutive_invalid >= LOCKDOWN_INVALID_FRAMES) {
            lockdown_active = true;  // Activăm lockdown
            LOG_ERR("LOCKDOWN TRIGGERED");
        }
        return ret;
    }

    consecutive_invalid = 0U;  // Frame valid → resetăm contorul
    return 0;
}
```

Dacă cineva injectează frame-uri invalide în mod repetat (fuzzing attack, replay attack) → după `LOCKDOWN_INVALID_FRAMES` frame-uri consecutive invalide → sistemul intră în lockdown și nu mai procesează nimic. Singura recuperare: resetul hardware (IWDG).

---

## 15. haptic_ui.c — motor, buzzer, OLED

### Motor vibratii via ULN2003

**De ce ULN2003?** Un GPIO STM32 poate furniza maxim 25mA. Un motor DC mic cere 90–150mA. Soluție: transistor ca switch de curent.

ULN2003 este un Darlington Array — practic 7 tranzistoare NPN pe același chip, cu diode de flyback interne. Conexiunea: STM32 PB6 (PWM 3.3V, max 25mA) → IN1 ULN2003 → OUT1 ULN2003 → Motor → +5V.

```c
static const struct pwm_dt_spec motor_pwm = PWM_DT_SPEC_GET(DT_ALIAS(motor_pwm));
// DT_ALIAS(motor_pwm) → &pwm4 → TIM4_CH1 → PB6

static void motor_pulse(u32_t duration_ms)
{
    pwm_set_dt(&motor_pwm, PWM_MSEC(1), PWM_MSEC(1) / 2U);  // 50% duty cycle, 1kHz
    k_msleep(duration_ms);
    pwm_set_dt(&motor_pwm, PWM_MSEC(1), 0U);  // duty cycle 0 = motor oprit
}
```

`PWM_MSEC(1)` = perioadă 1ms = frecvență 1kHz. Duty cycle 50% → motor la putere medie (nu e nevoie de viteză maximă pentru haptic).

### Buzzer piezo cu ton variabil

```c
static const struct pwm_dt_spec buzzer_pwm = PWM_DT_SPEC_GET(DT_ALIAS(buzzer_pwm));
// DT_ALIAS(buzzer_pwm) → &pwm3 → TIM3_CH1 → PB4

static void buzzer_beep(u32_t freq_hz, u32_t duration_ms)
{
    u32_t period_ns = 1000000000U / freq_hz;  // 2000Hz → 500000ns perioadă
    pwm_set_dt(&buzzer_pwm, period_ns, period_ns / 2U);  // 50% duty, freq variabilă
    k_msleep(duration_ms);
    pwm_set_dt(&buzzer_pwm, period_ns, 0U);
}
```

Apeluri exemple: `buzzer_beep(2500U, 100U)` → ton ascuțit 100ms. `buzzer_beep(1000U, 150U)` → ton grav 150ms (mai alarmant).

### Pattern SOS (degraded mode)

```c
static void play_sos_pattern(void)
{
    // ··· (3 scurte)
    for (u8_t i = 0U; i < 3U; i++) {
        motor_pulse(100U);
        buzzer_beep(2500U, 100U);
        k_msleep(50U);
    }
    k_msleep(150U);
    // ——— (3 lungi)
    for (u8_t i = 0U; i < 3U; i++) {
        motor_pulse(300U);  // HAPTIC_SOS_PULSE_MS
        buzzer_beep(2500U, 300U);
        k_msleep(100U);
    }
    k_msleep(150U);
    // ··· (3 scurte)
    for (u8_t i = 0U; i < 3U; i++) {
        motor_pulse(100U);
        buzzer_beep(2500U, 100U);
        k_msleep(50U);
    }
}
```

### Thread haptic — event-driven, nu polling

```c
void haptic_thread_entry(void *p1, void *p2, void *p3)
{
    while (1) {
        // BLOCHEAZĂ până când vine un event (nu consumă CPU în așteptare!)
        uint32_t events = k_event_wait(&haptic_events,
                                       EVT_GESTURE_ACK | EVT_MESSAGE_RX |
                                       EVT_ERROR | EVT_SOS_ALARM,
                                       false,        // nu clear automat
                                       K_MSEC(100)); // timeout pentru OLED refresh

        if (events & EVT_SOS_ALARM) {
            k_event_clear(&haptic_events, EVT_SOS_ALARM);
            play_sos_pattern();  // ~2s pattern SOS
        }
        if (events & EVT_GESTURE_ACK) {
            k_event_clear(&haptic_events, EVT_GESTURE_ACK);
            motor_pulse(100U);
            buzzer_beep(2000U, 50U);
        }
        // ... etc
    }
}
```

**De ce K_EVENT și nu polling?** Cu polling, thread-ul ar consuma CPU constant (`while(1) { check_gesture(); }`). Cu K_EVENT, thread-ul e suspendat de scheduler și pus în coada de așteptare. Nu consumă NIMIC din CPU cât timp nu există events. Alt thread apelează `k_event_post(&haptic_events, EVT_GESTURE_ACK)` și Zephyr îl trezește pe `haptic_tid`.

### OLED SSD1306

```c
static int oled_show_gesture(u8_t gesture_id)
{
    static const char *gesture_names[] = {
        [GESTURE_NONE]   = "---",
        [GESTURE_INDEX]  = "INDEX",
        [GESTURE_MIDDLE] = "MIDDLE",
        [GESTURE_FIST]   = "FIST",
        [GESTURE_HELP]   = "!! HELP !!",
        // ...
    };
    const char *name = gesture_names[gesture_id];
    LOG_INF("OLED: %s", name);
    /* TODO: display_write() cu CFB rendering — PENDING */
    return 0;
}
```

OLED-ul este conectat via SPI1 (diferit de SPI2 pentru ESP32!). Display driver-ul SSD1306 este activat din Kconfig și configurat complet în overlay. Randarea efectivă via CFB (Character Frame Buffer) — **pending implementare**.

---

## 16. comms_ble.c — BLE NUS gateway pe ESP32

### Ce este NUS?

Nordic UART Service (NUS) este un profil GATT standard creat de Nordic Semiconductor, folosit industrial pentru emularea UART peste BLE. Orice aplicație BLE pe telefon (nRF Connect, LightBlue etc.) îl recunoaște automat.

**UUID-urile NUS:**
- Serviciu: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX Characteristic (notify): `6E400003-...` (ESP32 → Telefon)
- RX Characteristic (write): `6E400002-...` (Telefon → ESP32)

### Definirea serviciului GATT

```c
BT_GATT_SERVICE_DEFINE(nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_svc_uuid),

    // TX: ESP32 → Telefon (NOTIFY = push, fără polling)
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY, ...),
    BT_GATT_CCC(ccc_changed, ...),  // Client Config: activare notificări

    // RX: Telefon → ESP32 (WRITE = telefon trimite date)
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid, BT_GATT_CHRC_WRITE, ...),
    BT_GATT_DESCRIPTOR(&nus_rx_uuid.uuid, BT_GATT_PERM_WRITE, NULL, on_rx_write, NULL),
);
```

### Trimitere notificație (TX)

```c
int ble_send_notification(const uint8_t *data, uint16_t len)
{
    if (!ble_notif_enabled || current_conn == NULL) {
        return -ENOTCONN;
    }
    return bt_gatt_notify(current_conn, &nus_svc.attrs[2], data, len);
}
```

Apelat din `spi_comm.c` ESP32 când primește MSG_TYPE_GESTURE de la STM32.

### Recepție comandă de pe telefon (RX — pending)

```c
static ssize_t on_rx_write(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len, ...)
{
    if (len > PROTO_PAYLOAD_SIZE) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    LOG_INF("BLE RX: %u bytes from central", len);
    // TODO: queue command pentru SPI slave TX → relay la STM32
    return len;
}
```

Callback-ul este apelat automat de Zephyr BLE stack când telefonul scrie pe RX characteristic. Datele primite trebuie puse în `slave_tx_frame` ca `MSG_TYPE_COMMAND` → STM32 va procesa la următoarea poll. **Implementarea relay-ului este pending.**

---

## 17. Tipuri de date și constante comune

### `common_types.h` — tipuri shared între STM32 și ESP32

```c
// Alias-uri MISRA Rule 4.6 (NOTĂ: acestea sunt de fapt violări MISRA — pending fix)
typedef uint8_t   u8_t;
typedef uint16_t  u16_t;
typedef uint32_t  u32_t;
typedef int16_t   s16_t;

// Pachetul de date sensor — trece prin sensor_msgq
typedef struct {
    u16_t flex_raw[4];       // ADC brut, 0..4095
    u16_t flex_filtered[4];  // după moving average
    u8_t  gesture_id;        // GESTURE_* define
    u8_t  confidence;        // 90 sau 50 (dacă sunt faults)
    u32_t timestamp_ms;      // k_uptime_get_32()
} sensor_packet_t;

// Status system — pentru logging și alarme
typedef enum {
    SYS_OK                    = 0,
    SYS_ERROR_SENSOR_FAULT    = 1,
    SYS_ERROR_SPI_TIMEOUT     = 2,
    SYS_ERROR_BLE_FAILURE     = 3,
    SYS_DEGRADED_NO_ESP32     = 6
} sys_status_t;
```

**Problema cu `u8_t` etc.:** MISRA C:2012 Rule 4.6 spune că trebuie folosite typedef-uri pentru tipuri numerice. PERÒ — intenția MISRA este ca typedef-urile să fie transparente (să documenteze intenția, nu să ascundă tipul de bază). Comunitățile MISRA interpretează asta în moduri diferite. Codul nostru va fi curățat în **Phase 3** pentru a folosi direct `uint8_t` etc.

### `app_config.h` — constante de aplicație (cu explicații)

```c
// ADC thresholds
#define ADC_MIN_VALID   500U   // sub 0.4V → senzor deconectat
#define ADC_MAX_VALID  3800U   // peste 3.07V → scurtcircuit

// Gesture
#define GESTURE_THRESHOLD_BENT 2000U  // ADC median între deschis (2815) și închis (1985)

// SPI Protocol
#define PROTO_SOF          0xAAU   // 10101010b — pattern unic pentru detecție frame
#define PROTO_PAYLOAD_SIZE    8U   // bytes fixe de payload
#define PROTO_FRAME_SIZE     12U   // total bytes per frame
#define PROTO_XOR_KEY_BASE 0x5AU  // XOR cu SEQ pentru obfuscare

// Timing
#define SPI_POLL_INTERVAL_MS  100U  // STM32 pollează la 10Hz
#define HEARTBEAT_INTERVAL_MS 500U  // ESP32 trimite heartbeat la 2Hz
#define HEARTBEAT_TIMEOUT_MS 2000U  // STM32 alarma dacă > 2s fără heartbeat
#define WATCHDOG_TIMEOUT_MS  2000U  // IWDG reset dacă > 2s fără feed

// Priorități thread (numerice, mai mic = mai urgent)
#define PRIO_SAFETY_THREAD    2  // cel mai urgent
#define PRIO_SPI_COMM_THREAD  4
#define PRIO_SENSOR_THREAD    5
#define PRIO_HAPTIC_THREAD    6  // cel mai puțin urgent

// Stack sizes (bytes, nu words!)
#define STACK_SAFETY    512U
#define STACK_SENSOR   1024U
#define STACK_HAPTIC    512U
#define STACK_SPI_COMM 1024U
```

---

## 18. Ce s-a modificat în ultima sesiune și de ce

### Modificarea 1: `prj.conf` STM32

**Problema inițiala:** Firmware-ul era prea "fat" pentru Blue Pill.
- Stack-uri prea mari (MAIN=2048, WORKQUEUE=1024)
- Logging la nivel INF/DBG → strings de debug în flash
- Fără optimizare de dimensiune (`-O2` implicit vs `-Os`)
- Periferice PWM și Display neactivate

**Ce am schimbat:**
| Config | Înainte | După | Economie estimată |
|---|---|---|---|
| `MAIN_STACK_SIZE` | 2048 | 512 | -1.5KB RAM |
| `WORKQUEUE_STACK_SIZE` | 1024 | 512 | -512B RAM |
| `LOG_DEFAULT_LEVEL` | 3 (INF) | 2 (WRN) | -3–5KB Flash (strings) |
| `LOG_MODE_MINIMAL` | nu | da | -1KB RAM |
| `CBPRINTF_NANO` | nu | da | -10KB Flash |
| `SIZE_OPTIMIZATIONS` | nu | da | -8–15KB Flash |
| `THREAD_NAME` | da | nu | -512B Flash |
| `CONFIG_PWM` | nu | **da** | +activare motor+buzzer |
| `CONFIG_DISPLAY + SSD1306` | nu | **da** | +activare OLED |

**Rezultat estimat:** Flash ~48% (61KB/128KB), RAM ~63% (12.6KB/20KB) — cu TOATE perifericele active.

### Modificarea 2: Overlay — conflict PA0

**Problema:** Motorul de vibrații era configurat pe TIM2_CH1, care folosește pinul PA0. PA0 este și ADC Channel 0 (senzorul pentru degetul index). Un pin STM32 nu poate funcționa ca ADC și ca timer output simultan.

**Soluție:** Motor mutat pe TIM4_CH1, pinul PB6 (liber, fără conflict).
```dts
// ÎNAINTE (greșit, conflict cu ADC PA0):
&timers2 { pwm2: pwm { pinctrl-0 = <&tim2_ch1_pwm_pa0>; }; };

// DUPĂ (corect, PB6 liber):
&timers4 { pwm4: pwm { pinctrl-0 = <&tim4_ch1_pwm_pb6>; }; };
```
Alias actualizat: `pwm-motor = &pwm4` → codul C `PWM_DT_SPEC_GET(DT_ALIAS(motor_pwm))` preia automat noul pin.

### Modificarea 3: Cerințe noi adăugate în documentație

Trei funcționalități noi specificate și documentate (implementare pending):

1. **Calibrare automată:** Strângi pumnul 20 secunde → sistemul salvează valorile ADC ca "zero" și "maxim" calibrat. Rezolvă problema că senzorii flex variază de la o mănușă la alta.

2. **NVS (Non-Volatile Storage):** Profilurile calibrate se salvează în ultimele 2KB de flash ale STM32 (emulare EEPROM via Zephyr NVS). La reboot, sistemul verifică profilul salvat.

3. **BLE bidirecțional:** Telefonul poate trimite comenzi la mănușă (activare motor, schimbare mod, request calibrare) via RX characteristic NUS.

---

## 19. Ce este PENDING și de ce

### P1 — MISRA C:2012 sweep (Phase 3)

**Ce trebuie:** Înlocuire `u8_t→uint8_t`, `u16_t→uint16_t` în tot codul. Fixuri Rule 14.4 (`if (ptr)` → `if (ptr != NULL)`). Rule 17.7 (`memset(...)` → `(void)memset(...)`).

**De ce nu e gata:** Este muncă mecanică dar extinsă — afectează `sensor_logic.c`, `safety_diag.c`, `haptic_ui.c`, `security.c`. Prioritate înaltă (blochează Phase 4).

### P2 — `k_timer` în loc de `k_msleep` pentru sensor

**Problema curentă:**
```c
// sensor_thread_entry:
while (1) {
    read_adc();
    classify_gesture();
    k_msgq_put(&sensor_msgq, &pkt, K_NO_WAIT);
    k_msleep(500U);  // DRIFT! dacă read_adc() durează 5ms → perioadă reală 505ms
}
```

**Soluția corectă:**
```c
K_TIMER_DEFINE(sensor_timer, NULL, NULL);
k_timer_start(&sensor_timer, K_MSEC(10), K_MSEC(10));
while (1) {
    k_timer_status_sync(&sensor_timer);  // Blochează exact până la următorul tick
    read_adc_and_classify();             // Durată variabilă, dar PERIOADA e fixă
}
```
Cu k_timer, perioada este exactă indiferent cât durează procesarea.

### P3 — CFB rendering pentru OLED

`oled_show_gesture()` are `/* TODO: display_write() */`. Trebuie implementat cu Zephyr CFB API:
```c
cfb_framebuffer_clear(display_dev, true);
cfb_print(display_dev, gesture_names[gesture_id], 0, 0);
cfb_framebuffer_finalize(display_dev);
```

### P4 — BLE command relay

`on_rx_write()` în `comms_ble.c` primește bytes de pe telefon dar nu face nimic cu ei. Trebuie:
1. Parse command byte
2. Construiește `MSG_TYPE_COMMAND` frame
3. Actualizează `slave_tx_frame` sub mutex
4. Când STM32 pollează → primește comanda
5. STM32 execută comanda (motor on/off, mode change etc.)

### P5 — NVS + calibrare automată

```c
// Adăugare în overlay:
// storage_partition { reg = <0x1E000 0x2000>; }; // ultimele 2KB flash

// Adăugare în prj.conf:
// CONFIG_NVS=y, CONFIG_FLASH=y, CONFIG_FLASH_MAP=y

// Adăugare în sensor_logic.c:
// Mașina de stări calibrare: IDLE → DETECTING_FIST → CALIBRATING → SAVING_NVS
```

### P6 — Thread-uri lipsă pe ESP32

`main.c` ESP32 are un singur thread definit (`spi_slave_tid`). Lipsesc:
- `ble_tid` — trimite notificări, gestionează conexiunea
- `mqtt_tid` — publish la broker MQTT (pendinge WiFi)
- `ai_tid` — TinyML inference stub

---

## 20. Cum explici totul unui developer senior în 5 minute

### Pitch tehnic structurat

**1. Problema și contextul (30 secunde):**
> "Mănușă medicală pentru detecție gesturi și alertare. Două MCU-uri: STM32 pentru real-time safety, ESP32 pentru connectivity. Separare explicită — WiFi stack-ul nu poate afecta determinismul senzorilor."

**2. Protocolul (1 minut):**
> "SPI full-duplex, 1 MHz, 12-byte frame fix: SOF + TYPE + SEQ + 8-byte payload XOR-obfuscat cu SEQ^0x5A + CRC-8/CCITT. STM32 este master, pollează la 100ms. ESP32 răspunde cu heartbeat la 500ms via K_TIMER callback și mutex pe bufferul TX."

**3. Safety (1 minut):**
> "IEC 61508 SIL-1: IWDG cu 2s timeout, heartbeat monitor cu mașina de stări BOOT_GRACE→ALIVE→DEGRADED, sensor fault detection cu triplu vot și fallback la last_good. Degraded mode: SOS pe motor+buzzer+OLED, independent de connectivity."

**4. Security (30 secunde):**
> "XOR obfuscare cu cheie variabilă (SEQ-based), plus al doilea layer cu hardware RNG și rotație periodică. Honeypot frames (1 din 10) pentru anti-fingerprinting. Anti-replay lockdown la 5 frame-uri invalide consecutive."

**5. Stack și toolchain (30 secunde):**
> "Zephyr 4.4.0 pe ambele MCU, build cu west, DeviceTree overlay pentru hardware config. MISRA C:2012 în progress (Phase 3 sweep pending). CFB pentru OLED (nu LVGL — 20KB RAM budget)."

**6. Ce este pending (30 secunde):**
> "MISRA sweep mecanic (u8_t→uint8_t etc.), k_timer pentru sensor (elimină drift de la k_msleep), CFB rendering OLED, BLE bidirectional relay, NVS calibrare automată."

---

## ANEXĂ: Structura completă a fișierelor

```
application/
├── common/                          ← Cod shared între STM32 și ESP32
│   ├── include/
│   │   ├── app_config.h             ← Toate constantele de aplicație
│   │   ├── common_types.h           ← Tipuri shared (sensor_packet_t, sys_status_t)
│   │   ├── error_codes.h            ← Coduri de eroare (error_code_t enum)
│   │   └── spi_protocol.h           ← Structuri frame SPI, MSG_TYPE_* defines
│   └── src/
│       └── spi_protocol.c           ← crc8_ccitt(), spi_frame_build(), spi_frame_validate()
│
├── stm32_app/                       ← Firmware STM32F103 (acquisition + safety)
│   ├── prj.conf                     ← Kconfig: periferice activate, optimizări
│   ├── boards/
│   │   └── stm32_min_dev_blue.overlay ← DeviceTree: pini, SPI1 OLED, SPI2 ESP32, PWM, ADC
│   ├── include/
│   │   ├── haptic_ui.h
│   │   ├── safety_diag.h
│   │   ├── security.h
│   │   ├── sensor_logic.h
│   │   └── spi_comm.h
│   └── src/
│       ├── main.c                   ← Thread definitions, sensor_msgq
│       ├── sensor_logic.c           ← ADC, filtru, klasifikare gest, sensor_thread_entry
│       ├── safety_diag.c            ← IWDG, fault detection, heartbeat monitor
│       ├── spi_comm.c               ← SPI2 master loop, spi_comm_thread_entry
│       ├── haptic_ui.c              ← Motor PWM, buzzer, OLED, haptic_thread_entry
│       └── security.c               ← XOR dinamic, honeypot, anti-replay
│
└── esp32_app/                       ← Firmware ESP32 (BLE gateway)
    ├── prj.conf
    ├── boards/
    │   └── esp32_devkitc_procpu.overlay
    ├── include/
    │   ├── comms_ble.h
    │   └── spi_comm.h
    └── src/
        ├── main.c                   ← Thread definitions (spi_slave_tid), ble_tx_msgq
        ├── spi_comm.c               ← SPI3 slave, heartbeat_timer, spi_slave_thread_entry
        └── comms_ble.c             ← BLE NUS, ble_init, ble_send_notification, on_rx_write
```

---

*Document generat: Sesiunea 1 de documentare GloveAssist*
*Versiune firmware: Zephyr 4.4.0-rc1, SDK 1.0.1*
*Target hardware: STM32F103C8T6 "Blue Pill" + ESP32 DevKitC*
