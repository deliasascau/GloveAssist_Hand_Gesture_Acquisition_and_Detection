# Faza 12 — Dashboard Adafruit IO + Vizualizare Date Medicale

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 7 (MQTT over Wi-Fi), Faza 8 (OLED local)  
**Tehnologii:** Adafruit IO MQTT API, Python (PC-side), ESP32 WiFi

---

## Obiectiv

Afișare date în timp real pe dashboard web Adafruit IO:
- Valorile celor 4 senzori flex (gauge sau line chart)
- Gestul curent detectat (text feed)
- Stare conectivitate BLE/WiFi
- Istoric sesiune (grafic pe zi)
- Consolă medic — text feed cu alerte

---

## 12.1 Feed-uri Adafruit IO

| Feed Name | Tip | Format | Update Rate |
|-----------|-----|--------|-------------|
| `glove/flex-0` | Numeric | `0..255` | 100ms |
| `glove/flex-1` | Numeric | `0..255` | 100ms |
| `glove/flex-2` | Numeric | `0..255` | 100ms |
| `glove/flex-3` | Numeric | `0..255` | 100ms |
| `glove/gesture` | Text | `FIST`, `OPEN`, `HELP`, `PINCH` | On change |
| `glove/confidence` | Numeric | `0..100` | On change |
| `glove/ble-connected` | Boolean | `0` / `1` | On change |
| `glove/battery` | Numeric | `0..100` (%) | 60s |
| `glove/fault` | Text | `NONE` / `SENSOR_F03` / `SPI_TIMEOUT` | On fault |
| `glove/session-kcal` | Numeric | Kilocalories sesiune | 60s |

---

## 12.2 Layout Dashboard

```
┌─────────────────────────────────────────────────────────────────┐
│               GloveAssist — Live Monitoring                     │
├────────────┬────────────┬────────────┬────────────┬────────────┤
│  Flex 0    │  Flex 1    │  Flex 2    │  Flex 3    │  Gesture   │
│  [Gauge]   │  [Gauge]   │  [Gauge]   │  [Gauge]   │  [Text]    │
│  0..255    │  0..255    │  0..255    │  0..255    │  "FIST"    │
├────────────┴────────────┴────────────┴────────────┼────────────┤
│                                                   │ Confidence │
│        Flex Values — Line Chart (5 min view)      │  [Gauge]   │
│                                                   │  87%       │
│  255 ──────────────────────────────────────────   │            │
│  128 ──── F0 ─ F1 ─ F2 ─ F3 ───────────────────  ├────────────┤
│    0 ──────────────────────────────────────────   │  BLE       │
│                                                   │  ● Online  │
├─────────────────────────────────────────────────  ├────────────┤
│                                                   │  Battery   │
│       Gesturi Detectate – Histogram (azi)         │  [Gauge]   │
│                                                   │  72%       │
│  FIST ████████████████ 48                         ├────────────┤
│  OPEN ███████████ 33                              │  Fault     │
│  HELP ███ 9                                       │  NONE      │
│  PINCH██ 6                                        │            │
│                                                   │            │
├───────────────────────────────────────────────────┴────────────┤
│  Consolă Medic                                                  │
│  [2024-01-15 10:23] FAULT: SENSOR_OPEN on flex-2 — recovered   │
│  [2024-01-15 10:21] SESSION_START — patient ID 0x42            │
│  [2024-01-15 10:00] DEVICE_ONLINE — firmware v1.2.0            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 12.3 Configurare MQTT ESP32

**Fișier:** `application/esp32_app/src/mqtt_task.c`

```c
#define ADAFRUIT_IO_HOST   "io.adafruit.com"
#define ADAFRUIT_IO_PORT   8883U              /* TLS */
#define ADAFRUIT_IO_USER   CONFIG_AIO_USER    /* Kconfig secret */
#define ADAFRUIT_IO_KEY    CONFIG_AIO_KEY     /* Kconfig secret */

/* Feed topics */
#define FEED_FLEX0    ADAFRUIT_IO_USER "/feeds/glove.flex-0"
#define FEED_FLEX1    ADAFRUIT_IO_USER "/feeds/glove.flex-1"
#define FEED_FLEX2    ADAFRUIT_IO_USER "/feeds/glove.flex-2"
#define FEED_FLEX3    ADAFRUIT_IO_USER "/feeds/glove.flex-3"
#define FEED_GESTURE  ADAFRUIT_IO_USER "/feeds/glove.gesture"
#define FEED_FAULT    ADAFRUIT_IO_USER "/feeds/glove.fault"
```

**prj.conf additions:**
```kconfig
# Adafruit IO credentials (set via menuconfig or .env injection)
CONFIG_AIO_USER=""
CONFIG_AIO_KEY=""

# TLS credentials (certificat rădăcină Adafruit IO)
CONFIG_MQTT_TLS=y
CONFIG_NET_SOCKETS_TLS_MAX_CREDENTIALS=2
```

---

## 12.4 Throttling la Rata Adafruit IO

Adafruit IO Free plan: **60 mesaje/minut** (1 msg/sec).

Strategie în `mqtt_task.c`:
1. **Rate limiter**: trimite date flex la 1Hz (nu la 10Hz cum vine de la SPI).
2. **On-change only** pentru gesture, fault, BLE status.
3. **Batch publish**: folosește MQTT QoS 0 pentru date continue, QoS 1 pentru alarme.

```c
/* Rate limiter — trimite la 1Hz indiferent de viteza SPI (10Hz) */
static int64_t s_last_publish_ms;

if (k_uptime_get() - s_last_publish_ms >= 1000) {
    mqtt_publish_flex_values(&latest_sensor_data);
    s_last_publish_ms = k_uptime_get();
}
```

---

## 12.5 Last Will & Testament (LWT)

Configurare LWT la connect so că medicul vede dacă dispozitivul se deconectează brusc:

```c
connect_param.will_topic.topic.utf8   = FEED_FAULT;
connect_param.will_topic.topic.size   = strlen(FEED_FAULT);
connect_param.will_message.data.data  = (uint8_t *)"DEVICE_OFFLINE";
connect_param.will_message.data.len   = 14U;
connect_param.will_message.qos        = MQTT_QOS_1_AT_LEAST_ONCE;
connect_param.will_retain             = 1U;
```

---

## 12.6 Kconfig — Injecție Credențiale (Securitate)

**IMPORTANT:** Niciodată hardcodat în sursă.

**Fișier:** `application/esp32_app/Kconfig`
```kconfig
config AIO_USER
    string "Adafruit IO Username"
    default ""
    help
      Adafruit IO account username. Set via cmake -DCONFIG_AIO_USER=...

config AIO_KEY
    string "Adafruit IO Key (secret)"
    default ""
    help
      Adafruit IO active key. Must be treated as secret.
      Never commit this value to version control.
```

**Build cu credențiale:**
```powershell
west build -b esp32_devkitc/esp32/procpu application\esp32_app -- `
  -DCONFIG_AIO_USER='"myuser"' `
  -DCONFIG_AIO_KEY='"aio_xxxxxxxxxxxx"'
```

---

## 12.7 Python Dashboard Local (Alternativă Offline)

Dacă Adafruit IO nu e disponibil (demonstrare fără internet), un script Python citește UART și afișează live:

**Fișier:** `tools/dashboard.py`
```python
import serial
import struct
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

PORT     = "COM3"
BAUDRATE = 115200
FRAME_SZ = 12

flex_data = [deque([0]*100, maxlen=100) for _ in range(4)]

def parse_frame(raw: bytes) -> dict | None:
    if len(raw) < FRAME_SZ or raw[0] != 0xAA:
        return None
    frame_type = raw[1]
    seq        = raw[2]
    payload    = bytearray(raw[3:11])
    crc_recv   = raw[11]
    # Un-XOR
    key = (seq ^ 0x5A) & 0xFF
    for i in range(8):
        payload[i] ^= key
    # CRC-8/CCITT verify (poly 0x07)
    crc_calc = crc8_ccitt(bytes([raw[1], raw[2]]) + bytes(payload))
    if crc_calc != crc_recv:
        return None
    if frame_type == 0x02:  # MSG_TYPE_GESTURE
        return {"flex": list(payload[:4]), "gesture": payload[4], "conf": payload[5]}
    return None

def crc8_ccitt(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc

# ... animate with matplotlib
```

---

## Plan de Lucru

1. Creare cont Adafruit IO + feeds manual pe dashboard
2. Implementare `mqtt_task.c` cu TLS + rate limiter
3. Testare MQTT cu `mosquitto_pub` mock înainte de integrare hardware
4. Adăugare LWT + reconectare automată (exponential backoff)
5. Implementare `tools/dashboard.py` pentru demo offline
6. Documentare screenshot final dashboard în `docs/deliverables/`

---

## Criterii de Acceptare

- [ ] 10 feed-uri configurate și vizibile pe Adafruit IO
- [ ] Rate limiter respectă limita de 60 msg/min
- [ ] LWT funcțional la deconectare forțată (test: deconectare alimentare)
- [ ] Credențiale injectate prin Kconfig, nu hardcodate
- [ ] Dashboard screenshot salvat în `docs/deliverables/dashboard_screenshot.png`
- [ ] Script Python `tools/dashboard.py` funcțional pe COM port
