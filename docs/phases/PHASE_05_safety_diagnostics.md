# Faza 5 — Safety & Diagnostică (IEC 61508 SIL-1)

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 4 completă  
**Standard:** IEC 61508, IEC 62304 Class B

---

## Obiective

Demonstrarea că mănușa este un **dispozitiv de siguranță** prin implementarea mecanismelor de detecție și recuperare din defecte.

---

## 5.1 Range Checking ADC

```
Plajă validă: 0.5V – 3.0V → [614 – 3686] pe 12-bit ADC (3.3V ref)
```

```c
#define ADC_MIN_VALID   614U   /* 0.5V / 3.3V * 4095 */
#define ADC_MAX_VALID  3686U   /* 3.0V / 3.3V * 4095 */

/* Per fiecare deget: */
if ((raw < ADC_MIN_VALID) || (raw > ADC_MAX_VALID)) {
    finger_fault[finger_idx] = FAULT_OPEN_CIRCUIT;  /* sau SHORT */
    error_report(ERR_SENSOR_DRIFT, finger_idx);
}
```

**Diagnostic: fir rupt vs. scurtcircuit:**
- `raw < 614` → fir rupt (rezistență infinită → tensiune minimă)
- `raw > 3686` → scurtcircuit (rezistență zero → tensiune maximă)

---

## 5.2 Algoritmul de Fallback (Interpolare)

Dacă degetul **Mijlociu** (index 1) este defect:

```c
/* Interpolare liniară din Arătător (0) și Inelar (2) */
if (finger_fault[FINGER_MIDDLE] != FAULT_NONE) {
    flex_filtered[FINGER_MIDDLE] =
        (flex_filtered[FINGER_INDEX] + flex_filtered[FINGER_RING]) / 2U;
    status_flags |= FLAG_INTERPOLATED_DATA;
}
```

**Matrice de fallback:**

| Deget defect | Sursă interpolare | Degradare AI |
|-------------|-------------------|-------------|
| Index (0) | Degetul Mijlociu (1) | Minimă |
| Mijlociu (1) | (Index + Inelar) / 2 | Moderată |
| Inelar (2) | (Mijlociu + Mic) / 2 | Moderată |
| Mic (3) | Degetul Inelar (2) | Minimă |
| ≥2 defecte | Operare imposibilă → DEGRADED | Totală |

---

## 5.3 Haptic Morse Code la Erori Critice

| Eroare | Pattern Morse | Durată |
|--------|--------------|--------|
| SPI Timeout (ESP32 pierdut) | `... --- ...` (SOS) | 3s, repetat |
| Sensor fault (≥2 degete) | `-.` (N) | 1s, repetat |
| Watchdog reset | `.-.` (R - Reset) | 2s, o dată |
| BLE disconnect | `--..--` (,) | 1s, o dată |

---

## 5.4 Finite State Machine (FSM) Sistem

```
        ┌──────────┐
   boot │ POWER_ON │
   ───► └────┬─────┘
             │ 500 ms self-test
             ▼
        ┌──────────────┐   calibration OK   ┌──────────────┐
        │ CALIBRATION  │ ─────────────────► │  NORMAL_OP   │
        └──────────────┘                    └──────┬───────┘
              │                                    │
              │ calibration fail                   │ eroare critică
              ▼                                    ▼
        ┌──────────────┐                   ┌──────────────────┐
        │ ERROR_CALIB  │                   │  ERROR_HANDLING  │
        └──────────────┘                   └────────┬─────────┘
                                                    │
                                          ┌─────────▼────────┐
                                          │   ERROR_LATCH    │
                                          │  (necesită reset)│
                                          └──────────────────┘
```

---

## Plan de lucru

1. Implementare range check în `sensor_logic.c`
2. Implementare algoritm fallback cu matrice de interpolare
3. Implementare coduri Morse în `haptic_ui.c`
4. Implementare FSM în `safety_diag.c` (state machine completă)
5. Test FMEA: simulare deget defect, verificare fallback

---

## Criterii de Acceptare

- [ ] ADC out-of-range → `FAULT` marcat în `status_flags`
- [ ] Deget defect → interpolare activă, AI funcționează limitat
- [ ] SPI timeout 2s → SOS haptic pornit
- [ ] FSM ajunge în `ERROR_LATCH` dacă ≥2 degete defecte simultan
- [ ] FMEA documentat (Faza 11)
