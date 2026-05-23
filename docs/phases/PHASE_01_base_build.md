# Faza 1 — Build de Bază Funcțional

**Status:** ✅ COMPLETĂ  
**Data finalizării:** Martie 2026  
**Ramură:** `main`

---

## Obiective

Faza 1 a stabilit fundația întregului proiect: un mediu de build Zephyr RTOS funcțional pentru ambele MCU-uri, cu comunicație SPI validată la nivel de compilare.

---

## Ce s-a realizat

### 1.1 Mediu de Dezvoltare

| Componentă | Versiune | Cale |
|------------|---------|------|
| Zephyr RTOS | 4.4.0-rc1 | `C:\...\zephyr-workspace\zephyr` |
| Zephyr SDK | 1.0.1 | `C:\zephyr-sdk-1.0.1` |
| Python venv | 3.12.10 | `.venv\Scripts\python.exe` |
| west | 1.5.0 | în venv |
| esptool | 5.2.0 | în venv |
| ARM GCC | 14.3.0 | din SDK |
| Xtensa GCC | 14.3.0 | din SDK |

### 1.2 Structura Proiectului

```
GLOVE-STM32-ESP32/
└── application/
    ├── common/
    │   ├── include/         # Headere partajate STM32 + ESP32
    │   │   ├── app_config.h
    │   │   ├── common_types.h
    │   │   ├── error_codes.h
    │   │   ├── safety_diag.h
    │   │   └── spi_protocol.h
    │   └── src/
    │       └── spi_protocol.c
    ├── stm32_app/
    │   ├── src/             # main.c, sensor_logic.c, safety_diag.c,
    │   │   └── ...          # spi_comm.c, security.c, haptic_ui.c
    │   ├── boards/
    │   │   └── stm32_min_dev_blue.overlay
    │   ├── CMakeLists.txt
    │   └── prj.conf
    └── esp32_app/
        ├── src/             # main.c, spi_comm.c, comms_ble.c
        ├── boards/
        │   └── esp32_devkitc_procpu.overlay
        ├── CMakeLists.txt
        └── prj.conf
```

### 1.3 Target STM32F103 (Blue Pill)

**Board:** `stm32_min_dev@blue`  
**Rezultat build:**

```
Memory region    Used Size   Region Size   %age Used
FLASH:           70240 B     128 KB         53.59%
RAM:             12760 B      20 KB         62.30%
```

**Threads activi:**

| Thread | Prioritate | Stack | Rol |
|--------|-----------|-------|-----|
| `safety_tid` | 2 (max) | 512 B | Watchdog IWDG + monitor heartbeat |
| `spi_comm_tid` | 4 | 1024 B | SPI master, poll la 100 ms |
| `sensor_tid` | 5 | 1024 B | ADC achiziție flex sensors |

**Periferice activate:**
- SPI2 master: PB13(SCK), PB15(MOSI), PB14(MISO), PB12(CS)
- ADC1: canale PA0–PA3 (4 senzori flex)
- IWDG: watchdog hardware

### 1.4 Target ESP32 DevKit

**Board:** `esp32_devkitc/esp32/procpu`  
**Rezultat build:**

```
Memory region      Used Size   Region Size   %age Used
FLASH:             391028 B    4194048 B      9.32%
dram0_0_seg:        58820 B     140452 B     41.88%
iram0_0_seg:       107008 B      224 KB      46.65%
```

**Threads activi:**

| Thread | Prioritate | Stack | Rol |
|--------|-----------|-------|-----|
| `spi_slave_tid` | 4 | 1024 B | SPI slave + heartbeat timer |

**Subsisteme activate:**
- SPI3 (VSPI) slave: GPIO25(SCK), GPIO26(MOSI), GPIO27(MISO), GPIO4(CS)
- Bluetooth: BLE NUS advertising "GloveAssist"
- Watchdog: `wdt0`

### 1.5 Probleme Rezolvate

| Problemă | Cauză | Soluție |
|----------|-------|---------|
| STM32 HAL linker regression | `hal_cortex.c`/`hal_gpio.c` lipsă din CMake | Patch `modules/hal/stm32/.../CMakeLists.txt` |
| Flash overflow 4712 bytes | `stm32f103X8.dtsi` declara 64 KB | Overlay: `&flash0 { reg = <0x08000000 0x20000>; }` |
| `haptic_thread_entry` unresolved | `haptic_ui.c` dezactivat, `main.c` îl referința | Guard `#if defined(CONFIG_PWM)` |
| `SPI_DT_SPEC_GET` build error | Nod DT fără binding compatible | Switch la `DEVICE_DT_GET` + `spi_config` manual |
| `BT_LE_ADV_CONN` undeclared | Zephyr 4.4 a redenumit macro-ul | `BT_LE_ADV_CONN_FAST_1` |
| `bt_gatt_notify_cb` prea mulți argumenți | Signatura API Zephyr 4.4: 2 argumente | Eliminat al 3-lea argument (callback) |
| `esptool` not found la link | PATH-ul moștenit de ninja nu includea venv | `$env:PATH = ".venv\Scripts;" + $env:PATH` înainte de `west build` |

---

## Cum se buildează

```powershell
# Navighează la workspace
Set-Location "C:\Users\Lenovo\OneDrive\Desktop\zephyr-workspace"

# STM32
.\.venv\Scripts\python.exe -m west build `
    -b "stm32_min_dev@blue" `
    "C:\...\GLOVE-STM32-ESP32\application\stm32_app" `
    --build-dir build-stm32

# ESP32 (esptool trebuie în PATH)
$env:PATH = ".\.venv\Scripts;" + $env:PATH
.\.venv\Scripts\python.exe -m west build `
    -b "esp32_devkitc/esp32/procpu" `
    "C:\...\GLOVE-STM32-ESP32\application\esp32_app" `
    --build-dir build-esp32
```

---

## Limitări cunoscute (rezolvate în fazele ulterioare)

- PWM (motor + buzzer) dezactivat — blocat de `pinctrl-0` pe TIM2/TIM3
- OLED SSD1306 dezactivat
- Frame-ul SPI era 41 bytes cu CRC-16 (refactorizat în **Faza 2**)
- Senzori trimiteau `sensor_packet_t` de 22 bytes (depășea payload-ul) — **fixat în Faza 2**
- Fără XOR obfuscation pe wire — **implementat în Faza 2**
