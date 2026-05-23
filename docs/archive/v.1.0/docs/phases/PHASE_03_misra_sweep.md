# Faza 3 — Conformitate MISRA C:2012 (Sweep Complet)

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 2 completă  
**Efort estimat:** 2–3 sesiuni

---

## Obiective

Fiecare fișier `.c` și `.h` din `application/` trebuie să treacă analiza statică Cppcheck cu ruleset MISRA C:2012.

---

## Reguli MISRA de Target

| Regulă | Descriere | Fișiere afectate |
|--------|-----------|-----------------|
| **4.6** | Tipuri exclusiv din `<stdint.h>` | Toate — eliminare `u8_t`, `u16_t` |
| **15.5** | O singură instrucțiune `return` per funcție | `sensor_logic.c`, `safety_diag.c` |
| **15.4** | Un singur `break` per `switch` per iterație | `spi_comm.c` |
| **21.3** | Fără `malloc`/`free` | Verificare globală |
| **14.4** | Controlul `if`/`while` cu expresie booleană | Toate |
| **10.1** | Conversii implicite interzise | Cast explicit |
| **17.7** | Valoarea de retur a funcțiilor void-like trebuie ignorată explicit | `(void)memset(...)` |

---

## Plan de lucru

1. Rulează Cppcheck local:
   ```bash
   cppcheck --enable=all --std=c11 --addon=misra.py \
     application/common/src/ application/stm32_app/src/ application/esp32_app/src/
   ```
2. Fix rulă 4.6 — sweep de înlocuire `u8_t→uint8_t`, `u16_t→uint16_t` etc.
3. Fix regulă 15.5 — restructurare funcții cu `result` pattern
4. Adaugă headere Doxygen lipsă pe toate funcțiile
5. Validare finală — zero warnings MISRA

---

## Headere Doxygen Obligatorii (format)

```c
/**
 * @brief  Scurtă descriere (o linie).
 *
 * @details  Descriere extinsă opțională.
 *
 * @param[in]  param1  Descriere parametru intrare
 * @param[out] param2  Descriere parametru ieșire
 * @return     0 succes, cod negativ la eroare
 *
 * @pre   pre-condiție (ex: device_is_ready(dev))
 * @post  post-condiție (ex: frame->crc8 valid)
 */
```

---

## Criterii de Acceptare

- [ ] `cppcheck --addon=misra.py` returnează 0 violation-uri obligatorii
- [ ] Zero utilizări de `int`, `char`, `long` nequalificate
- [ ] Toate funcțiile au header Doxygen complet
- [ ] `_Static_assert` pentru toate struct-urile cu dimensiune fixă
