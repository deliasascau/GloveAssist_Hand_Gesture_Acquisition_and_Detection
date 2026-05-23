# Faza 19 — OTA Firmware Update via BLE (MCUboot + SMP)

> **Status:** ✅ IMPLEMENTAT (06-Mai-2026)  
> **Dependințe:** Faza 6 (BLE funcțional)

---

## Ce este și de ce contează

Fără OTA, orice modificare de firmware necesită acces fizic la dispozitiv:
desfaci mănușa, conectezi cablul USB, flashezi manual, reasambezi.

Cu OTA, firmware-ul se actualizează **wireless de pe telefon**, fără cablu și
fără a atinge hardware-ul. Acesta este standardul pentru dispozitivele medicale
comerciale (Fitbit, Abbott, Medtronic) și este o cerință implicită în
**IEC 62304** (software lifecycle medical devices).

**Feature de diferențiere față de soluții PCB + LoRa + PLC** — acestea nu au
OTA wireless nativ.

---

## Arhitectură

```
Flash ESP32 (4MB total)
┌──────────────────────────────────────────────────────────┐
│ 0x000000  MCUboot bootloader         64 KB  (read-only)  │
│ 0x010000  slot0 — aplicație curentă 960 KB  (rulează)   │
│ 0x100000  slot1 — staging OTA       960 KB  (scriere)    │
│ 0x1F0000  scratch — swap buffer      64 KB               │
│ 0x3F0000  storage — NVS/calibrare    64 KB               │
└──────────────────────────────────────────────────────────┘
```

Când ESP32 pornește, **MCUboot** (bootloader separat) verifică slot0 și slot1.
Dacă slot1 conține o imagine nouă marcată "pending", MCUboot copiază imaginea
din slot1 în slot0 (swap) și pornește noul firmware.

Dacă noul firmware se resetează înainte de confirmare → MCUboot revine
automat la imaginea veche din slot0 (**revert mode**).

---

## Flux OTA

```
PC Developer                   nRF Connect App             ESP32
      │                              │                        │
      │ west build --sysbuild        │                        │
      │ → zephyr.signed.bin          │                        │
      │                              │                        │
      │──── transfer fișier ────────►│                        │
      │                              │── BLE connect ────────►│
      │                              │── SMP: upload chunk ──►│ ← scrie în slot1
      │                              │── SMP: upload chunk ──►│
      │                              │        ... (30s) ...   │
      │                              │── SMP: image confirm ─►│ ← marchează pending
      │                              │── SMP: os reset ──────►│
      │                              │                        │
      │                              │                 [MCUboot swap]
      │                              │                        │
      │                              │◄── BLE advertise ──────│ ← rulează firmware nou
      │                              │── connect + verify ───►│
      │                              │                boot_write_img_confirmed()
```

---

## Mecanismul de siguranță (revert mode)

Acesta este motivul pentru care OTA este **sigur pentru dispozitive medicale**:

```
Boot 1 (firmware nou):
  MCUboot → swap slot1→slot0 → pornește aplicația
  Aplicația inițializează toți subsistemii
  Dacă OK → boot_write_img_confirmed() → MCUboot marchează "valid"

Boot 2 (dacă firmware-ul nou se resetează accidental înainte de confirmare):
  MCUboot → detectează "neconfirmat" → revert: swap înapoi la imaginea veche
  Dispozitivul pornește cu firmware-ul anterior, garantat funcțional
```

Codul din `main.c`:
```c
/* Confirmare imagine validă — TREBUIE apelat după ce toți subsistemii sunt up.
 * Dacă dispozitivul se resetează înainte de această linie, MCUboot
 * va restaura automat firmware-ul anterior la next boot. */
ret = boot_write_img_confirmed();
```

---

## Fișiere modificate

| Fișier | Modificare |
|--------|-----------|
| `application/esp32_app/prj.conf` | `CONFIG_BOOTLOADER_MCUBOOT`, `CONFIG_MCUMGR`, `CONFIG_MCUMGR_TRANSPORT_BT`, `CONFIG_MCUMGR_GRP_IMG`, MTU 498 |
| `application/esp32_app/boards/esp32_devkitc_procpu.overlay` | Partiții flash: `slot0`, `slot1`, `scratch`, `storage` |
| `application/esp32_app/sysbuild/mcuboot.conf` | Fișier nou — configurare MCUboot bootloader |
| `application/esp32_app/src/main.c` | `#include <zephyr/dfu/mcuboot.h>` + `boot_write_img_confirmed()` |
| `.vscode/tasks.json` | `--sysbuild` adăugat la task-ul "Zephyr: Build ESP32" |

---

## Build și flash inițial

```powershell
# Build (MCUboot + aplicație — două binare)
$env:ZEPHYR_BASE='C:\zephyr-workspace\zephyr'
$env:PATH='C:\zephyr-workspace\.venv\Scripts;' + $env:PATH
python -m west build -p always --sysbuild `
    -b esp32_devkitc/esp32/procpu `
    application/esp32_app `
    --build-dir C:\zephyr-workspace\build-glove-esp32

# Flash inițial cu cablu (o singură dată)
python -m west flash --build-dir C:\zephyr-workspace\build-glove-esp32
```

Fișierul generat pentru OTA wireless:
```
C:\zephyr-workspace\build-glove-esp32\esp32_app\zephyr\zephyr.signed.bin
```

---

## Procedură OTA wireless (după primul flash)

1. Compilează firmware-ul nou pe PC → obții `zephyr.signed.bin`
2. Transferi fișierul pe telefon (USB, cloud, email)
3. Deschizi **nRF Connect** (Android/iOS) → tab DFU
4. Conectare la `GloveAssist`
5. Selectezi `zephyr.signed.bin` → **Start DFU**
6. Upload ~30 secunde, ESP32 se resetează automat
7. Noul firmware rulează; dacă funcționează → confirmat permanent

---

## Securitate (pentru prezentare)

| Aspect | Status |
|--------|--------|
| Semnătură imagine | `CONFIG_BOOT_SIGNATURE_TYPE_NONE` — dezactivată (demo) |
| Autentificare BLE pentru OTA | `CONFIG_MCUMGR_TRANSPORT_BT_AUTHEN=n` — deschis (demo) |
| **Producție** | Semnătură ECDSA-P256 + autentificare BLE obligatorii |

> **Notă pentru prezentare:** Semnătura este dezactivată pentru simplificarea
> demo-ului. Un produs comercial ar folosi `CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256`
> cu cheie privată generată offline și stocată securizat (nu în repository).
