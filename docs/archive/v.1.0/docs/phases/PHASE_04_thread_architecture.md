# Faza 4 — Arhitectura de Thread-uri (Timere Fixe, AI Thread)

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 3 completă  

---

## Obiective

Implementarea arhitecturii complete de thread-uri cu priorități determinate, timere fixe și separare clară a responsabilităților.

---

## Arhitectura Finală de Thread-uri

### STM32F103 — Procesor de Achiziție și Siguranță

```
┌─────────────────────────────────────────────────────────┐
│  Prioritate 2 │ safety_tid    │ 250 ms │ IWDG + heartbeat│
│  Prioritate 4 │ spi_comm_tid  │ 100 ms │ SPI master poll │
│  Prioritate 5 │ sensor_tid    │  10 ms │ ADC + IIR filter│
│  Prioritate 6 │ haptic_tid    │ event  │ Motor + buzzer  │
│  Prioritate 7 │ display_tid   │ 100 ms │ OLED SSD1306    │
└─────────────────────────────────────────────────────────┘
```

### ESP32 — Procesor BLE/Cloud

```
┌─────────────────────────────────────────────────────────┐
│  Prioritate 3 │ spi_slave_tid │ blocking│ SPI slave rx  │
│  Prioritate 4 │ ai_tid        │ 100 ms  │ TFLite Micro  │
│  Prioritate 5 │ ble_tid       │ event   │ NUS + pairing │
│  Prioritate 6 │ mqtt_tid      │ 500 ms  │ WiFi + MQTT   │
└─────────────────────────────────────────────────────────┘
```

---

## Sensor Thread — 10 ms Fix (STM32)

```c
/* k_timer la exact 10 ms — nu k_msleep (drift acumulat) */
K_TIMER_DEFINE(sensor_timer, sensor_timer_fn, NULL);

void sensor_thread_entry(void *p1, void *p2, void *p3)
{
    k_timer_start(&sensor_timer, K_MSEC(10), K_MSEC(10));
    while (1) {
        k_timer_status_sync(&sensor_timer);  /* Blochează exact 10ms */
        adc_read_all_channels();
        apply_iir_filter();
        k_msgq_put(&sensor_msgq, &packet, K_NO_WAIT);
    }
}
```

---

## AI Thread — TFLite Micro (ESP32)

```c
/* Buffer circular din SPI -> AI inference */
static float ai_input_buffer[NUM_FLEX_SENSORS];

void ai_thread_entry(void *p1, void *p2, void *p3)
{
    tflite_init();  /* Încarcă modelul din flash */
    while (1) {
        k_sem_take(&ai_data_ready, K_FOREVER);
        preprocess_sensor_data(ai_input_buffer);
        uint8_t gesture = tflite_infer(ai_input_buffer);
        publish_gesture(gesture);
    }
}
```

---

## Plan de lucru

1. Refactorizare `sensor_logic.c` → `k_timer` cu period 10 ms exact
2. Creare `ai_engine.c` pe ESP32 cu TFLite Micro stub
3. Creare `mqtt_task.c` pe ESP32 cu WiFi init
4. Separare BLE în thread dedicat cu `k_sem` pentru notificări
5. Testare că sensor_tid nu deranjează spi_comm_tid (prioritate corectă)

---

## Criterii de Acceptare

- [ ] `sensor_tid` rulează la exact 10 ms (verificat cu osciloscop sau LOG timestamp)
- [ ] `ai_tid` nu blochează `spi_slave_tid`
- [ ] `mqtt_tid` nu afectează latența BLE (priorități corecte)
- [ ] Watchdog reset dacă oricare thread îngheață > 2s
