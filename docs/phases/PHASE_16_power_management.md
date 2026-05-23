# Faza 16 — Power Management

Scop: demonstrăm că mănușa este proiectată pentru baterie, nu doar pentru alimentare de pe USB.

## Ce am adăugat în firmware

STM32 are acum sampling adaptiv:

- normal: `SENSOR_SAMPLE_PERIOD_MS = 100 ms`;
- idle: `SENSOR_IDLE_SAMPLE_PERIOD_MS = 250 ms`;
- intră în idle după `SENSOR_IDLE_TIMEOUT_MS = 3000 ms` cu gest stabil `NONE`.

Efect:

- când mâna este inactivă, citirea ADC și traficul UART scad;
- safety thread și heartbeat monitor rămân active;
- la primul gest detectat, sampling-ul revine la perioada normală.

Fișiere:

- `application/common/include/app_config.h`
- `application/stm32_app/src/sensor_logic.c`

## Ce măsurăm

| Stare | Ce este activ |
|---|---|
| Idle local | STM32 + senzori + OLED, fără gest |
| BLE connected | ESP32 BLE conectat la telefon |
| WiFi/MQTT connected | ESP32 conectat la router și broker |
| Gesture event | clasificare + BLE notify + eventual MQTT publish |
| Local alarm | motor + buzzer + OLED alarmă |

## Setup de măsurare

Recomandat:

- USB power meter pe alimentarea de 5V;
- sau multimetru în serie cu bateria;
- sau INA219/INA226 dacă vrei logging automat.

Măsoară curentul în mA pentru fiecare stare.

Important:

- motorul de vibrații are vârfuri scurte de curent;
- WiFi are spike-uri când transmite;
- notează media aproximativă, nu doar valoarea instantanee.

## Calculator autonomie

Tool:

```powershell
python tools/power_budget.py
```

Exemplu cu baterie de 1200 mAh:

```powershell
python tools/power_budget.py --battery-mah 1200 `
  --state idle_ble,85,0.80 `
  --state gesture_ble,130,0.15 `
  --state wifi_mqtt_publish,210,0.04 `
  --state local_alarm,280,0.01
```

Format `--state`:

```text
NUME,CURENT_MA,DUTY_CYCLE
```

Exemplu:

```text
idle_ble,85,0.80
```

înseamnă: sistemul stă 80% din timp în idle și consumă 85 mA.

## Test pentru sampling adaptiv

Conectează HIL pe STM32 și rulează:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --duration 15 --timestamps --csv power_idle_timing.csv
```

Pași:

1. Ține mâna nemișcată, fără gest, cel puțin 5 secunde.
2. Observă `delta_ms` în CSV.
3. Fă un gest.
4. Observă că perioada revine spre `100 ms`.

Pass dacă:

- în idle, `delta_ms` urcă spre `250 ms`;
- la gest, `delta_ms` revine spre `100 ms`;
- heartbeat safety rămâne activ.

## Tabel recomandat pentru raport

| Stare | Curent măsurat | Duty estimat | Contribuție |
|---|---:|---:|---:|
| Idle BLE |  | 80% |  |
| Gesture BLE |  | 15% |  |
| WiFi/MQTT publish |  | 4% |  |
| Local alarm |  | 1% |  |
| Total mediu |  | 100% |  |

## Ce poți spune la prezentare

> Sistemul nu face sampling agresiv permanent. Când nu există gesturi, reduce frecvența de achiziție ADC, dar păstrează safety heartbeat activ. Am măsurat consumul pe stări și am estimat autonomia bateriei folosind duty-cycle real.
