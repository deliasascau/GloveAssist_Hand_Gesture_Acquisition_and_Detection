# Faza 14 — Fault Injection Real

Scop: demonstrăm că mănușa nu doar funcționează în cazul ideal, ci reacționează controlat când apar defecte.

## Reguli de siguranță

- Folosește doar semnale de **3.3V** pe STM32/ESP32.
- Nu conecta niciodată 5V direct pe pini ADC/GPIO.
- Pentru forțarea unui nivel pe ADC, folosește o rezistență serie de **1kΩ-10kΩ**.
- GND trebuie să fie comun între placă, adaptor USB-UART și sursa de alimentare.

## Test 1 — Pierdere ESP32 / Heartbeat Timeout

Problema simulată: ESP32 se blochează, se resetează sau se întrerupe legătura UART.

Setup:

- STM32 alimentat și flash-uit.
- USB-UART conectat la STM32:

| USB-UART | STM32 |
|---|---|
| TX | PA10 RX |
| RX | PA9 TX |
| GND | GND |

Comandă:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --duration 8 --drop-heartbeat-after 2
```

Ce face scriptul:

1. Trimite heartbeat normal timp de 2 secunde.
2. Se oprește din trimis heartbeat.
3. STM32 trebuie să detecteze lipsa ESP32 după `HEARTBEAT_TIMEOUT_MS`.

Pass dacă:

- OLED afișează alarmă locală / no link;
- motorul vibrează;
- buzzerul activ pornește pattern-ul SOS;
- sistemul nu depinde de telefon/cloud pentru alertă.

## Test 2 — Frame UART Corupt

Problema simulată: zgomot pe fir, byte pierdut, CRC invalid.

Comandă:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --cmd reset --bad-command-crc --duration 6
```

Ce face scriptul:

- trimite o comandă validă ca structură, dar cu CRC stricat.

Pass dacă:

- STM32 nu trimite `ACK`;
- comanda nu este executată;
- feedback-ul de eroare apare local, dacă este vizibil prin OLED/buzzer/motor.

## Test 3 — Senzor Flex Forțat LOW

Problema simulată: pin ADC blocat jos sau circuit defect spre GND.

Setup pentru un canal, de exemplu `PA0`:

```text
PA0 --[1kΩ]-- GND
```

Pass dacă:

- după câteva citiri consecutive, firmware-ul detectează valoare invalidă;
- frame-ul transmis pe UART are confidence mai mică (`50%` în loc de `90%`);
- sistemul continuă să funcționeze folosind fallback, dacă doar un senzor este afectat.

Observare cu HIL:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --duration 20 --require-frames
```

Caută în output:

```text
GESTURE ... conf=50%
```

## Test 4 — Senzor Flex Forțat HIGH / Senzor Deconectat

Problema simulată: firul spre senzor se rupe sau nodul ADC rămâne tras sus.

Metoda simplă:

- deconectează partea senzorului care merge spre GND;
- nodul ADC va fi tras spre 3.3V prin rezistența fixă de 1kΩ.

Metodă controlată:

```text
PA0 --[10kΩ]-- 3.3V
```

Pass dacă:

- firmware-ul marchează citirea ca invalidă după citiri consecutive;
- pentru un singur senzor defect, sistemul continuă cu fallback;
- pentru toate cele 4 canale defecte, sistemul intră în stare de siguranță / reset watchdog.

## Test 5 — Toți Senzorii Defecți

Problema simulată: conectorul mănușii este scos complet sau alimentarea senzorilor este ruptă.

Metodă:

- forțează toate intrările `PA0..PA3` în LOW prin rezistențe serie;
- sau deconectează toți senzorii astfel încât ADC-urile ies din intervalul valid.

Pass dacă:

- sistemul nu mai raportează gesturi valide false;
- intră în lockdown / alarmă locală;
- watchdog-ul resetează STM32 dacă starea rămâne imposibilă.

## Test 6 — WiFi/MQTT indisponibil

Problema simulată: router oprit, parolă greșită, internet căzut.

Metodă:

- configurează WiFi greșit sau oprește routerul;
- lasă BLE și UART pornite.

Pass dacă:

- gesturile rămân disponibile local și prin BLE;
- MQTT încearcă reconectare fără să blocheze UART/BLE;
- HELP local rămâne disponibil prin OLED/motor/buzzer.

## Tabel de rezultate recomandat

| Test | Metodă | Rezultat așteptat | Rezultat real | Pass/Fail |
|---|---|---|---|---|
| Heartbeat lost | `--drop-heartbeat-after 2` | alarmă locală |  |  |
| CRC invalid | `--bad-command-crc` | fără ACK |  |  |
| PA0 LOW | PA0 prin 1kΩ la GND | confidence 50% |  |  |
| PA0 HIGH | PA0 prin 10kΩ la 3.3V | confidence 50% |  |  |
| Toți senzorii fault | PA0-PA3 invalide | lockdown/reset |  |  |
| WiFi down | router oprit | BLE/UART continuă |  |  |

Acest tabel este material bun pentru prezentare: arată embedded real, nu doar feature-uri.
