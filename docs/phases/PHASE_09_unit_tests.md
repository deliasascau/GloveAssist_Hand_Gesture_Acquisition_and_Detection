# Faza 9 — Ztest Unit Tests

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 3 completă  
**Locație:** `tests/` (folder nou în repo)

---

## Obiective

Suite de teste automate Zephyr Ztest pentru funcțiile critice:
1. `test_crc8` — CRC-8/CCITT cu vectori de test cunoscuți  
2. `test_xor_obfuscation` — Encode/decode round-trip  
3. `test_moving_average` — Filtru IIR cu valori controlate  
4. `test_range_check` — Detecție valori ADC în/afara[0.5V–3.0V]  
5. `test_frame_build_validate` — Build + validate round-trip complet

---

## Structura Folderului de Teste

```
tests/
├── unit/
│   ├── CMakeLists.txt
│   ├── prj.conf
│   └── src/
│       ├── test_crc8.c
│       ├── test_xor.c
│       ├── test_filter.c
│       ├── test_range_check.c
│       └── test_spi_protocol.c
```

---

## Test 1: CRC-8/CCITT

```c
ZTEST(crc8_suite, test_known_vector)
{
    /* Vector de test: CRC-8/CCITT al "123456789" = 0xF4 */
    static const uint8_t data[] = "123456789";
    uint8_t result = crc8_ccitt(data, sizeof(data) - 1U);
    zassert_equal(result, 0xF4U, "CRC-8 mismatch: got 0x%02X", result);
}

ZTEST(crc8_suite, test_empty_input)
{
    uint8_t result = crc8_ccitt(NULL, 0U);
    /* Comportament definit pentru input gol */
    zassert_equal(result, 0x00U, "CRC-8 empty should be 0x00");
}
```

---

## Test 2: XOR Obfuscation Round-Trip

```c
ZTEST(xor_suite, test_encode_decode_roundtrip)
{
    spi_frame_t tx_frame;
    static const uint8_t original[PROTO_PAYLOAD_SIZE] =
        {0x80U, 0x60U, 0x40U, 0x20U, 0x06U, 0x64U, 0x00U, 0x00U};

    (void)spi_frame_build(&tx_frame, MSG_TYPE_GESTURE, original, PROTO_PAYLOAD_SIZE);

    /* Validarea trebuie să reconstruiască exact payload-ul original */
    int32_t ret = spi_frame_validate(&tx_frame);
    zassert_equal(ret, 0, "Validate failed: %d", ret);
    zassert_equal(tx_frame.sof, (uint8_t)PROTO_SOF, "SOF mismatch");
}

ZTEST(xor_suite, test_corrupted_payload_detected)
{
    spi_frame_t frame;
    (void)spi_frame_build(&frame, MSG_TYPE_GESTURE, NULL, 0U);

    frame.payload[0] ^= 0xFFU;  /* Corupere deliberată */
    int32_t ret = spi_frame_validate(&frame);
    zassert_not_equal(ret, 0, "Corruption not detected");
}
```

---

## Test 3: Moving Average / IIR Filter

```c
ZTEST(filter_suite, test_iir_convergence)
{
    filter_state_t state;
    filter_init(&state);

    /* La input constant, output trebuie să convergă la input */
    for (uint8_t i = 0U; i < 32U; i++) {
        filter_update(&state, 2000U);
    }
    uint16_t out = filter_get(&state);
    zassert_within(out, 2000U, 10U, "IIR did not converge: %u", out);
}
```

---

## Test 4: Range Checking ADC

```c
ZTEST(range_suite, test_valid_range)
{
    sensor_fault_t fault = sensor_check_range(1500U);
    zassert_equal(fault, FAULT_NONE, "1500 should be VALID");
}

ZTEST(range_suite, test_below_min)
{
    sensor_fault_t fault = sensor_check_range(100U);
    zassert_equal(fault, FAULT_OPEN_CIRCUIT, "100 should be OPEN_CIRCUIT");
}

ZTEST(range_suite, test_above_max)
{
    sensor_fault_t fault = sensor_check_range(4000U);
    zassert_equal(fault, FAULT_SHORT_CIRCUIT, "4000 should be SHORT");
}
```

---

## Cum se rulează testele

```powershell
# Build și run pe hardware QEMU (emulator ARM)
.\.venv\Scripts\python.exe -m west build `
    -b qemu_cortex_m3 tests/unit --build-dir build-tests

.\.venv\Scripts\python.exe -m west build `
    --build-dir build-tests -t run
```

---

## Plan de lucru

1. Creare structură `tests/unit/`
2. Implementare toate cele 5 suite-uri de teste
3. Verificare rulare pe QEMU (`qemu_cortex_m3`)
4. Integrare în CI/CD (Faza 10)

---

## Criterii de Acceptare

- [ ] 100% teste trec pe QEMU
- [ ] CRC-8 vector "123456789" = `0xF4` confirmat
- [ ] Round-trip XOR: build → validate → 0 erori
- [ ] Range check: 0/4095 detectate corect
- [ ] Filter: convergență în < 32 iterații
