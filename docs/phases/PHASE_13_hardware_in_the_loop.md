# Faza 13 — Hardware-in-the-loop UART Tests

Scop: validare reală, pe hardware, a protocolului dintre STM32 și ESP32.

Tool: `tools/hil_uart_test.py`

## De ce există

Testele unitare verifică protocolul în software. HIL verifică sistemul real:

- pinii UART sunt cablați corect;
- baud-rate-ul este aliniat;
- frame-urile de 12 bytes sunt valide;
- CRC-ul detectează frame-uri corupte;
- STM32 primește heartbeat/comenzi;
- ESP32 primește gesturi/raw ADC și trimite heartbeat;
- haptic/OLED reacționează la comenzi și erori.

## Instalare

```powershell
python -m pip install pyserial
```

## Test STM32 separat

Deconectează ESP32 de pe UART și conectează un adaptor USB-UART la STM32:

| USB-UART | STM32 |
|---|---|
| TX | PA10 RX |
| RX | PA9 TX |
| GND | GND |

Folosește doar logică de **3.3V**.

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --duration 20 --require-frames
```

Ce verifici:

- scriptul trimite heartbeat periodic către STM32;
- STM32 trimite frame-uri `GESTURE`;
- dacă miști senzorii, payload-ul se schimbă;
- dacă oprești scriptul, STM32 trebuie să intre în alarmă locală după timeout.

Comenzi către STM32:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --cmd reset --duration 8
python tools/hil_uart_test.py stm32 --port COM7 --cmd set-thresh --finger index --value 800 --duration 8
python tools/hil_uart_test.py stm32 --port COM7 --cmd calibrate --duration 20
```

Pentru `calibrate`, urmează secvența din firmware:

1. Ține degetele drepte aproximativ 5 secunde.
2. Ține degetele îndoite aproximativ 5 secunde.
3. Pragurile sunt salvate în NVS.

## Test ESP32 separat

Deconectează STM32 de pe UART și conectează un adaptor USB-UART la ESP32:

| USB-UART | ESP32 |
|---|---|
| TX | GPIO16 RX |
| RX | GPIO17 TX |
| GND | GND |

Trimite gesturi simulate către ESP32:

```powershell
python tools/hil_uart_test.py esp32 --port COM7 --gesture 6 --duration 15 --require-frames
```

Gestul `6` este `HELP`. ESP32 trebuie să:

- primească frame-ul `GESTURE`;
- trimită heartbeat înapoi pe UART;
- trimită notificare BLE dacă telefonul este conectat;
- publice MQTT dacă WiFi/MQTT este configurat.

Poți simula raw ADC:

```powershell
python tools/hil_uart_test.py esp32 --port COM7 --raw-adc 1200 1600 500 450 --duration 15
```

## Sniff pasiv

Pentru a observa o singură direcție fără să transmiți:

| USB-UART | Linie observată |
|---|---|
| RX | TX-ul pe care vrei să-l vezi |
| GND | GND comun |

Nu conecta TX-ul adaptorului în modul sniff.

```powershell
python tools/hil_uart_test.py tap --port COM7 --duration 30
```

## Criterii de acceptare

| Test | Pass dacă |
|---|---|
| STM32 HIL | Primești frame-uri `GESTURE` valide CRC |
| STM32 command | Primești `ACK` după `reset`, `set-thresh`, `calibrate` |
| STM32 safety | Dacă heartbeat-ul se oprește, OLED/motor/buzzer intră în alarmă locală |
| ESP32 HIL | Primești `HEARTBEAT` valid și vezi notificarea BLE/MQTT |
| Tap | Decodezi frame-uri fără CRC mismatch pe cablaj normal |

## Note

- Baud-rate-ul curent este `115200`.
- Frame-ul are 12 bytes: `SOF | TYPE | SEQ | PAYLOAD[8] | CRC8`.
- Pentru test rapid folosește build-dir în `C:\zephyr-workspace`, nu în OneDrive.
