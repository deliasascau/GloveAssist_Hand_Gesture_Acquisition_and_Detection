# Faza 17 — Calibrare Robustă și Persistență

Scop: calibrarea nu trebuie să fie doar salvată în flash, ci și verificabilă după reset.

## Ce există acum

STM32 folosește Zephyr NVS în partiția `storage`.

Profilul de calibrare este salvat ca structură versionată:

```text
magic
version
size
calibrated flag
threshold[4]
open_ref[4]
bent_ref[4]
checksum
```

Cheie NVS:

```text
CALIB_NVS_ID_PROFILE = 10
```

Cheile vechi `1..3` sunt păstrate doar pentru migrare:

```text
1 = threshold[4]
2 = open_ref[4]
3 = bent_ref[4]
```

## De ce e mai robust

La boot, firmware-ul verifică:

- magic number;
- versiunea profilului;
- dimensiunea structurii;
- pragurile sunt în interval valid ADC;
- checksum-ul profilului este corect.

Dacă ceva nu e valid:

1. încearcă migrare din formatul vechi;
2. dacă nu există date valide, folosește defaults din `app_config.h`;
3. marchează sistemul ca necalibrat.

Astfel, un NVS corupt nu produce gesturi false din praguri greșite.

## Comenzi suportate

Prin BLE -> ESP32 -> UART -> STM32:

| Comandă | Efect |
|---|---|
| `CMD_CALIBRATE` | pornește calibrarea automată OPEN/BENT |
| `CMD_SET_THRESH` | salvează manual pragul unui deget |
| `CMD_RESET` | revine la pragurile default |

Test cu HIL:

```powershell
python tools/hil_uart_test.py stm32 --port COM7 --cmd calibrate --duration 20
python tools/hil_uart_test.py stm32 --port COM7 --cmd set-thresh --finger index --value 800 --duration 8
python tools/hil_uart_test.py stm32 --port COM7 --cmd reset --duration 8
```

## Flux calibrare automată

1. Trimiți `CMD_CALIBRATE`.
2. Ții degetele drepte aproximativ 5 secunde.
3. Ții degetele îndoite aproximativ 5 secunde.
4. Firmware-ul calculează:

```text
threshold = (open_ref + bent_ref) / 2
```

5. Profilul complet este salvat cu checksum.

## Teste recomandate

| Test | Pass dacă |
|---|---|
| Boot fără NVS | folosește defaults, `calibrated=false` |
| Calibrare completă | după reset, pragurile persistă |
| Set threshold manual | după reset, noul prag persistă |
| Reset defaults | după reset, pragurile revin la defaults |
| NVS vechi | migrează automat la profil versionat |
| NVS corupt | nu folosește profilul, cade pe defaults |

## Ce poți spune la prezentare

> Calibrarea este persistentă, versionată și verificată cu checksum. Dacă flash-ul conține date corupte sau o versiune veche, firmware-ul nu le folosește orb, ci migrează sau revine la defaults.
