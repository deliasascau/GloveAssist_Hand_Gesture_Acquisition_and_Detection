# Faza 7 — WiFi + MQTT/TLS + Adafruit IO Cloud

> Implementare curenta: `wifi_mqtt.c` foloseste MQTT TLS (`MQTT_TRANSPORT_SECURE`)
> pe portul 8883 si cere certificat CA in `app_config.h`. Fara certificat CA,
> thread-ul WiFi/MQTT se opreste intentionat, ca sa nu trimita date in clar.

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 6 completă  

---

## Obiectives

- **MQTTS** (port 8883) cu **TLS 1.2** pe ESP32
- **LWT (Last Will and Testament)** — serverul notifică medicul la disconnect
- **Format JSON** pentru interoperabilitate cu dashboard
- Buffer offline: salvare date în RAM dacă WiFi e indisponibil

---

## Topologie Cloud

```
Mănușă (ESP32)
     │
     │ MQTTS / TLS 1.2 / port 8883
     ▼
Adafruit IO MQTT Broker
     │
     │ WebSocket / REST API
     ▼
Dashboard Web (browser medic)
```

---

## Topic MQTT

| Topic | Direcție | QoS | Descriere |
|-------|----------|-----|-----------|
| `AIO_USER/feeds/glove.gesture` | Publish | 1 | Gest curent |
| `AIO_USER/feeds/glove.flex` | Publish | 0 | Array flex 0-255 |
| `AIO_USER/feeds/glove.status` | Publish | 1 | Status sistem |
| `AIO_USER/feeds/glove.command` | Subscribe | 1 | Comandă de la medic |

---

## Format JSON

```json
{
  "ts": 1743410400,
  "flex": [128, 96, 64, 32],
  "gesture": "FIST",
  "confidence": 87,
  "rssi": -62,
  "battery_pct": 84,
  "status": "NORMAL_OP"
}
```

---

## Last Will and Testament (LWT)

```c
/* Configurat la conectare — dacă conexiunea pică, brokerul publică automat: */
static const char lwt_payload[] =
    "{\"status\":\"offline\",\"reason\":\"connection_lost\"}";

struct mqtt_utf8 lwt_topic = {
    .utf8   = "gloveassist/status",
    .size   = sizeof("gloveassist/status") - 1U,
};
```

**Efectul:** Dashboard-ul medicului afișează alertă imediată la disconnect.

---

## TLS 1.2 Configuration

```kconfig
CONFIG_WIFI=y
CONFIG_NET_L2_WIFI_MGMT=y
CONFIG_MQTT_LIB=y
CONFIG_MQTT_LIB_TLS=y
CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_TLS_CREDENTIALS=y
CONFIG_MBEDTLS=y
```

```c
/* Certificat Adafruit IO CA (inclus în flash ca array C) */
static const uint8_t adafruit_ca_cert[] = {
#include "adafruit_io_ca.pem.inc"
};
```

---

## Plan de lucru

1. Creare `mqtt_task.c` pe ESP32
2. WiFi provisioning (SSID/pass în `prj.conf` → ulterior NVS storage)
3. TLS cert inclus în build ca array C
4. LWT configurat la `mqtt_connect()`
5. Buffer circular offline (ring buffer 32 intrări în RAM)
6. TEST: simulare disconnect — verifică că LWT apare pe broker

---

## Criterii de Acceptare

- [ ] Datele apar pe Adafruit IO la 2s interval
- [ ] TLS verificat (Wireshark: TLS handshake vizibil, data criptată)
- [ ] LWT publicat la decuplarea alimentării
- [ ] JSON valid (validat cu `jq` sau schema validator)
- [ ] Buffer offline: datele se publică la reconectare
