# Faza 15 — Timing Analysis

Scop: măsurăm latențe și perioade reale, ca să demonstrăm comportament embedded predictibil.

## Ce măsurăm

| Măsurătoare | De ce contează |
|---|---|
| UART frame period STM32 -> ESP32 | confirmă sampling/classification cadence |
| Heartbeat period ESP32 -> STM32 | confirmă safety monitor |
| Heartbeat timeout -> alarmă locală | confirmă degraded mode |
| Gest -> BLE notification | confirmă latență către telefon |
| Gest -> MQTT publish | confirmă latență către cloud |
| Gest -> OLED/motor/buzzer | confirmă feedback local |

## Instrumente

- `tools/hil_uart_test.py`
- adaptor USB-UART 3.3V
- telefon cu nRF Connect / LightBlue pentru BLE
- dashboard Adafruit IO / broker MQTT
- opțional: analizor logic pentru măsurători mai precise

## 1. Măsurare perioadă frame STM32

Conectează USB-UART la STM32:

| USB-UART | STM32 |
|---|---|
| TX | PA10 RX |
| RX | PA9 TX |
| GND | GND |

Rulează:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --duration 20 --timestamps --csv timing_stm32.csv
```

Ce urmărești:

- `GESTURE` la aproximativ perioada `SENSOR_SAMPLE_PERIOD_MS`;
- inter-frame delta stabil, fără pauze mari;
- CRC valid pentru frame-uri.

Exemplu output:

```text
t=1.104s dt=100.8ms GESTURE seq=12 ... gesture=FIST conf=90%
```

Pass recomandat:

- frame period mediu: aproape de `100 ms`;
- jitter acceptabil pentru prototip: sub `+/- 30 ms`.

## 2. Măsurare heartbeat ESP32

Conectează USB-UART la ESP32:

| USB-UART | ESP32 |
|---|---|
| TX | GPIO16 RX |
| RX | GPIO17 TX |
| GND | GND |

Rulează:

```powershell
python tools/hil_uart_test.py esp32 --port COM7 --duration 20 --timestamps --csv timing_esp32.csv
```

Pass recomandat:

- `HEARTBEAT` la aproximativ `500 ms`;
- fără CRC mismatch.

## 3. Heartbeat timeout -> alarmă

Rulează:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --duration 8 --drop-heartbeat-after 2 --timestamps
```

Măsoară cu cronometru sau video:

1. momentul în care scriptul afișează `heartbeat stopped`;
2. momentul în care OLED/motor/buzzer intră în alarmă.

Pass recomandat:

- alarmă locală apare după aproximativ `HEARTBEAT_TIMEOUT_MS` plus toleranța thread-ului safety;
- pentru configurația curentă: în jur de `2.0-2.5 s`.

## 4. Gest -> BLE

Setup:

1. STM32 și ESP32 conectate normal.
2. Telefon conectat la BLE NUS.
3. nRF Connect cu notificări activate pe TX characteristic.

Metodă simplă:

- filmează simultan mâna și ecranul telefonului;
- fă gestul `HELP`;
- calculează diferența dintre momentul gestului și apariția notificării.

Pass recomandat:

- sub `500 ms` pentru prototip;
- sub `200 ms` dacă sampling-ul și hold-time-ul sunt optimizate.

Notă: codul are `GESTURE_HOLD_TIME_MS=300`, deci latența minimă include intenționat o stabilizare de 300 ms.

## 5. Gest -> MQTT

Setup:

1. WiFi/MQTT configurat cu credentiale reale.
2. Dashboard Adafruit IO deschis.
3. BLE/UART funcționale.

Metodă:

- fă gestul `HELP`;
- notează timpul când apare pe dashboard.

Pass recomandat:

- sub `2 s` pe WiFi stabil;
- dacă brokerul/cloud întârzie, sistemul local și BLE trebuie să continue.

## 6. Gest -> OLED/motor/buzzer

Metodă simplă:

- filmează mâna și OLED-ul/motorul/buzzerul;
- fă gesturi distincte: `INDEX`, `FIST`, `HELP`;
- verifică dacă feedback-ul local apare după stabilizarea gestului.

Pass recomandat:

- sub `500 ms` pentru feedback local;
- `HELP` trebuie să fie clar diferit în dashboard/BLE, iar alarma locală să rămână disponibilă la fault.

## Tabel recomandat pentru raport

| Măsurătoare | Așteptat | Măsurat | Pass/Fail |
|---|---:|---:|---|
| STM32 frame period | 100 ms +/- 30 ms |  |  |
| ESP32 heartbeat | 500 ms +/- 100 ms |  |  |
| Heartbeat lost -> alarm | 2.0-2.5 s |  |  |
| Gest -> BLE | <500 ms |  |  |
| Gest -> MQTT | <2 s |  |  |
| Gest -> OLED/haptic | <500 ms |  |  |

## Analiză CSV rapidă

Fișierele generate cu `--csv` pot fi deschise în Excel.

Coloana utilă este `delta_ms`, care arată timpul dintre frame-uri.

Pentru o analiză rapidă în PowerShell:

```powershell
Import-Csv timing_stm32.csv | Measure-Object delta_ms -Average -Minimum -Maximum
```

Pentru raport, pune media/min/max și 1-2 capturi de ecran.
