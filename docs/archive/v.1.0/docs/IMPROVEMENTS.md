# GloveAssist — Îmbunătățiri Planificate

## Ușor (1–2 zile fiecare)

### [ ] 1. Calibrare automată în flash (NVS Zephyr)
- La primul boot sau la comandă BLE, firmware-ul face o sesiune de calibrare
- Salvează valorile OPEN/BENT per deget în **Zephyr NVS** (non-volatile storage în flash intern)
- La repornire, încarcă pragurile din NVS
- Dacă valorile ADC live diferă cu >20% față de NVS → recalibrare automată
- **Fișiere afectate**: `sensor_logic.c`, `app_config.h`, `prj.conf` (CONFIG_NVS=y)

### [ ] 2. OLED rendering real (SSD1306)
- `oled_show_gesture()` în `haptic_ui.c` are `TODO: display_write()` neimplementat
- Zephyr CFBF (Character Framebuffer) funcționează cu SSD1306 fără cod grafic suplimentar
- Afișaj: nume gest centrat + bara de confidence + stare BLE (icon)
- **Fișiere afectate**: `haptic_ui.c`, `prj.conf` (CONFIG_CHARACTER_FRAMEBUFFER=y)

### [ ] 3. BLE command relay STM32 ← ESP32 ← telefon
- `on_rx_write()` în `comms_ble.c` are `TODO: implement command queuing`
- Telefonul poate trimite comenzi: `CALIBRATE`, `SET_THRESH`, `RESET`
- ESP32 le împachetează în frame UART și le trimite la STM32
- **Fișiere afectate**: `comms_ble.c`, `uart_comm.c` (ESP32), `uart_comm.c` (STM32)

---

## Mediu (3–5 zile fiecare)

### [x] 4. WiFi/MQTT real (Adafruit IO)
- `wifi_mqtt.c` are implementare reala: WiFi, DNS, MQTT publish si reconnect.
- MQTT este configurat peste TLS pe portul 8883; lipsa certificatului CA opreste thread-ul.
- Publica: gesture_id, confidence, stare sistem pe feed-uri Adafruit IO.
- Mai ramane testul pe broker real cu credentiale si certificat CA.
- **Fisiere afectate**: `wifi_mqtt.c`, `prj.conf` (ESP32), `app_config.h` (SSID/pass/CA)

### [ ] 5. TinyML gesture classifier
- `lib/tensorflow_lite/` există dar este gol
- Model mic: 4 input (ADC filtered), 7 output (gesturi), ~8KB flash
- Antrenat pe datele reale de calibrare colectate după implementarea NVS
- Rulează în paralel cu classifier-ul rule-based; câștigă dacă confidence > 80%
- **Fișiere afectate**: `sensor_logic.c`, `lib/tensorflow_lite/`, `prj.conf`

---

## Complex (săptămână+)

### [ ] 6. OTA firmware via BLE (MCUboot)
- Update firmware wireless fără cablu USB
- Zephyr suportă MCUboot nativ; necesită partiționare flash (2 slots)
- Util pentru dispozitiv medical purtat — nu necesită desfacere
- **Fișiere afectate**: `prj.conf`, `CMakeLists.txt`, overlay boards, nou fișier `mcuboot.conf`

### [ ] 7. Criptare BLE reală (AES-CCM)
- XOR rotativ actual este **obfuscare**, nu criptare
- Zephyr BLE stack suportă AES-CCM la nivel GATT (LE Secure Connections)
- Înlocuiește `security_xor_payload()` cu AES-CCM 128-bit
- Cheie derivată la pairing, stocat în NVS (depinde de #1)
- **Fișiere afectate**: `security.c`, `security.h`, `comms_ble.c`

---

## Status

| # | Titlu | Status | Data |
|---|-------|--------|------|
| 1 | Calibrare NVS | ⏳ în lucru | — |
| 2 | OLED rendering | ⬜ neînceput | — |
| 3 | BLE command relay | ⬜ neînceput | — |
| 4 | WiFi/MQTT real | ✅ implementat, de testat pe broker real | 2026-05-01 |
| 5 | TinyML | ⬜ neînceput | — |
| 6 | OTA MCUboot | ⬜ neînceput | — |
| 7 | AES-CCM BLE | ⬜ neînceput | — |
