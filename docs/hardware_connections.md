# GloveAssist — Conexiuni Hardware

## 1. UART — STM32 ↔ ESP32 (comunicare inter-MCU)

> Protocol: 115200 baud, 8N1 · Frame: 12 bytes `glove_frame_t` (`frame_protocol`) · **Masă comună obligatorie**
> Pentru prototip păstrăm 115200 baud: este suficient pentru frame-uri mici și mai tolerant la cablajul pe mănușă.

```
STM32 Blue Pill              ESP32 DevKitC
──────────────────────────────────────────
PA9  (USART1 TX)  ─────────  GPIO16 (UART2 RX)
PA10 (USART1 RX)  ─────────  GPIO17 (UART2 TX)
GND               ─────────  GND
```

Ambele MCU lucrează la 3.3V — nu e nevoie de level-shifter.

---

## 2. ADC — Senzori Flex (4×)

> Divizor de tensiune · ADC 12-bit · Senzori DIY: rezistență ~600–1100 Ω
> ⚠️ **Rezistența fixă = 1kΩ** (nu 330Ω — rezoluție prea slabă cu senzori DIY de 600–1100Ω)
> S   chema: valoarea ADC **scade** când degetul se îndoaie.

```
STM32 Blue Pill    Deget
────────────────────────
PA0 (ADC1_CH0)  ←  Index
PA1 (ADC1_CH1)  ←  Middle
PA2 (ADC1_CH2)  ←  Ring
PA3 (ADC1_CH3)  ←  Pinky
```

Schema unui canal:
```
3.3V ──[1kΩ]──┬── PA_x (ADC)
              [Senzor flex DIY]
             GND
```

Deget drept: senzor rezistență mare → mai multă tensiune pe 1kΩ → ADC citește **mai mare**.
Deget îndoit: senzor rezistență mică → mai puțină tensiune pe 1kΩ → ADC citește **mai mic**.

---

## 3. I2C — OLED SSD1306 (STM32)

> OLED cu 4 pini: VCC, GND, SCL, SDA · Driver: `solomon,ssd1306` · Alimentare: 3.3V
> Se recomandă **I2C2** ca să evităm conflictul cu PB6, folosit pentru motorul de vibrații.

```
STM32 Blue Pill    OLED SSD1306
────────────────────────────────
PB10 (I2C2_SCL) ──  SCL
PB11 (I2C2_SDA) ──  SDA
3.3V            ──  VCC
GND             ──  GND
```

Adresă uzuală SSD1306 I2C: `0x3C` sau `0x3D` (verifică modulul concret).

> ⚠️ Observație software importantă: pe `stm32_min_dev` PB10/PB11 sunt folosiți implicit și de `USART3`.
> Când OLED-ul este pe `I2C2` (PB10/PB11), setează `&usart3 { status = "disabled"; };`
> în overlay-ul STM32, altfel apar conflicte de pin și display-ul poate rămâne blank.

---

## 4. GPIO (PWM software) — Buzzer Pasiv (STM32)

> Buzzer **pasiv** (+ și − marcate, scoate sunete ciudate la DC direct) · Firmware-ul generează semnal PWM software pe PB4 la ~2500 Hz via `k_busy_wait` toggle.
> STM32F103 GPIO poate livra max **25mA** — buzzerul pasiv tipic consumă 30–50mA → **tranzistor NPN obligatoriu**.

### Circuit (NPN + rezistor de bază)

```
3.3V ──────────────────── Buzzer (+)
                          Buzzer (−) ── Collector (NPN, ex. BC547 / 2N2222)
PB4 ── [1kΩ] ──────────── Base
                          Emitter ────── GND
```

1. **Rezistorul de 1kΩ** între PB4 și baza tranzistorului
2. **Tranzistorul NPN** (BC547, 2N2222) — comutatorul de putere
3. **Buzzerul între colector și 3.3V** (sau 5V dacă buzzerul tău e de 5V)
4. *(Opțional)* **Diodă 1N4148** în paralel cu buzzerul (anod la colector, catod la VCC) — protecție la spike

> ⚠️ Fără tranzistor, PWM-ul software de pe PB4 va produce un sunet slab/distorsionat și poate deteriora pinul GPIO pe termen lung.

---

## 5. PWM — Motor Vibrații via ULN2003 (STM32)

> TIM4_CH1 · Dioda flyback internă în ULN2003 · **PA0 rezervat ADC — nu se folosește pentru motor**

```
STM32 Blue Pill    ULN2003       Motor DC 5V
─────────────────────────────────────────────
PB6 (TIM4_CH1)  ──  IN1
                    OUT1  ──────  Motor (−)
5V              ──  COM (pin 9)
                    COM   ──────  Motor (+)
GND             ──  GND
```

---

## 6. Alimentare

```
Sursă                Tensiune    Consumatori
────────────────────────────────────────────────────────────
LiPo / USB 5V        5V          ESP32 VIN, ULN2003 COM, Motor
Regulator Blue Pill  3.3V        STM32 VCC, OLED SSD1306, senzori flex (~94 mA total)
GND comun            0V          TOATE componentele
```

> ⚠️ **Regulatorul de pe Blue Pill este slab (~150–300 mA).** Nu alimenta motorul de vibrații sau ESP32 din 3.3V-ul STM32.
> OLED-ul I2C este conectat la STM32 și se alimentează la 3.3V; motorul rămâne pe 5V prin ULN2003.
>
> **Regulă de aur:** GND STM32 și GND ESP32 pe același fir — fără masă comună comunicarea UART nu funcționează.

---

## 7. ST-Link V2 — Flash STM32 (programare firmware)

> Necesar doar la flash/debug · Jumper **BOOT0 = 0** (spre GND) înainte de flash

```
ST-Link V2 (pin)    Blue Pill
──────────────────────────────
SWDIO  (pin 7)  ──  PA13
SWCLK  (pin 9)  ──  PA14
GND    (pin 12) ──  GND
3.3V   (pin 19) ──  3V3
```

Comandă flash după conectare:
```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD freq=50 -d "C:\zephyr-workspace\build-glove-stm32\zephyr\zephyr.hex" -v -rst
```

> ⚠️ `freq=50` (50 kHz) este **obligatoriu** pentru Blue Pill — condensatoarele de pe liniile SWD cauzează erori la viteze mai mari.

---

## 8. Serial Monitor STM32 — via Arduino ca adaptor USB-Serial

> Dacă nu ai un adaptor USB-TTL (CP2102/CH340), poți folosi orice **Arduino Uno/Nano/Mega** ca bridge USB-serial.
> Arduino-ul are onboard un chip USB-serial (CH340G pe clone, ATmega16U2 pe originale) pe care îl poți folosi direct.

### Pregătire Arduino (o singură dată)

Există două metode:

**Metoda A — RESET la GND (cea mai simplă, fără sketch):**
Conectează un fir între pinul **RESET** și **GND** de pe Arduino. Asta ține ATmega-ul în reset permanent → liniile TX/RX devin pass-through direct la chip-ul USB-serial.

**Metoda B — Sketch gol:**
Uploadează pe Arduino un sketch cu `setup()` și `loop()` goale. ATmega-ul nu mai trimite nimic pe serial.

### Conexiuni

```
Arduino             Blue Pill
──────────────────────────────
TX (pin 1)      ──  PA10 (USART1 RX)
RX (pin 0)      ──  PA9  (USART1 TX)
GND             ──  GND
RESET ── GND    (doar Metoda A)
```

> ⚠️ **Nu conecta 5V/3.3V de la Arduino la Blue Pill** dacă Blue Pill e deja alimentat din altă sursă.
> ⚠️ **Arduino Uno are logică 5V pe TX** — pune un divizor de tensiune (2×1kΩ) sau un level-shifter între Arduino TX și Blue Pill PA10 (3.3V tolerant dar nu garantat pe termen lung).
> Arduino Nano 3.3V sau orice Arduino care lucrează la 3.3V nu au această problemă.

Setări terminal serial: **115200 baud · 8N1 · fără flow control**

Log-uri așteptate la boot:
```
[00:00:00.001] safety_init OK
[00:00:00.002] calibration_init: NVS gol/corupt - scriu profil defaults
[00:00:00.003] Praguri active: I=621 M=874 R=2009 P=944
[00:00:00.004] Sensor thread started — 100 ms period
```

---

## 9. Rezumat complet conexiuni (toate simultan)

```
                    ┌─────────────────────────────────────────┐
                    │           Blue Pill STM32F103            │
                    │                                          │
  Flex Index  ──── PA0   PA9  (TX) ─────────── GPIO16  ──┐   │
  Flex Middle ──── PA1   PA10 (RX) ─────────── GPIO17  ──┤   │
  Flex Ring   ──── PA2                          GND    ──┤   │
  Flex Pinky  ──── PA3                                    │   │
                        PB10 (SCL) ── OLED SCL        ESP32   │
                        PB11 (SDA) ── OLED SDA            │   │
                        PB6  (PWM) ── ULN2003 IN1         │   │
                        PB4  (PWM) ── [1kΩ] ── NPN Base    │   │
                        NPN Collector ── Buzzer (−)         │   │
                        3V3 ── Buzzer (+)                   │   │
                        PA13 ──────── ST-Link SWDIO        │   │
                        PA14 ──────── ST-Link SWCLK        │   │
                        3V3  ──────── OLED VCC             │   │
                        GND  ──────── toate GND ───────────┘   │
                    └─────────────────────────────────────────┘

  ULN2003 OUT1 ── Motor (-)
  ULN2003 COM  ── 5V
  Motor (+)    ── 5V
```
