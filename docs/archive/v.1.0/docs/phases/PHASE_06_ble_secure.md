# Faza 6 — BLE Securizat (LE Secure Connections, AES-128)

> Implementare curenta: ESP32 activeaza `CONFIG_BT_SMP`, `CONFIG_BT_SMP_SC_ONLY`
> si cere `BT_SECURITY_L4` la conectare. Caracteristica RX si CCC-ul NUS cer
> criptare, deci telefonul trebuie sa faca pairing inainte de comenzi/notificari.

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 5 completă  

---

## Obiective

- BLE cu **LE Secure Connections** (LESC) + **Passkey Entry** (6 cifre)
- **AES-128** pentru criptarea datelor pe link BLE
- **MTU 247** pentru latență sub 20 ms per frame

---

## Configurare Kconfig (prj.conf)

```kconfig
# Security Manager Protocol
CONFIG_BT_SMP=y
CONFIG_BT_PRIVACY=y
CONFIG_BT_SMP_SC_ONLY=y         # Forțează Secure Connections (nu Legacy)
CONFIG_BT_SMP_ENFORCE_MITM=y    # Forțează MITM protection (Passkey Entry)

# MTU tuning pentru latență < 20ms
CONFIG_BT_L2CAP_TX_MTU=247
CONFIG_BT_BUF_ACL_RX_SIZE=251
CONFIG_BT_BUF_ACL_TX_SIZE=251

# Bond storage (retenție pereche)
CONFIG_BT_SETTINGS=y
CONFIG_SETTINGS=y
```

---

## Flux de Imperechere (Pairing)

```
Telefon                          ESP32
   │                               │
   │── Scan → find "GloveAssist" ──►│
   │◄── ADV_IND (LESC capable) ────│
   │                               │
   │── CONNECT_REQ ───────────────►│
   │◄── MTU_EXCHANGE (247) ────────│
   │                               │
   │── PAIRING_REQ ───────────────►│
   │   (MITM=1, SC=1, Passkey)     │
   │                               │
   │◄── Afișează PIN pe OLED ──────│ ← 6 cifre generate random
   │── Introduce PIN pe telefon ──►│
   │                               │
   │◄──── PAIRING_CONFIRM (AES) ───│
   │──── PAIRING_RANDOM ──────────►│
   │◄─── PAIRING SUCCESS ──────────│
   │                               │
   │◄═══ AES-128 encrypted link ═══│
```

---

## Implementare în Zephyr

```c
/* comms_ble.c — auth callbacks */
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    /* Afișează pe OLED (Faza 8) sau LOG pentru debug */
    LOG_INF("BLE Passkey: %06u", passkey);
    display_show_passkey(passkey);  /* Faza 8 */
}

static void auth_cancel(struct bt_conn *conn)
{
    LOG_WRN("BLE pairing cancelled");
}

static struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .cancel          = auth_cancel,
};

/* În ble_init(): */
bt_conn_auth_cb_register(&auth_cb);
```

---

## MTU Tuning pentru Latență < 20 ms

| Parametru | Valoare |
|-----------|---------|
| MTU | 247 bytes |
| Connection interval | 7.5–15 ms |
| Supervision timeout | 4000 ms |
| Latency | 0 (no skip) |

```c
/* Exchange MTU după conectare */
static void connected(struct bt_conn *conn, uint8_t err)
{
    bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
}
```

---

## Plan de lucru

1. Adaugă `CONFIG_BT_SMP=y`, `CONFIG_BT_SMP_SC_ONLY=y` în `prj.conf`
2. Implementare `bt_conn_auth_cb` cu `passkey_display`
3. MTU exchange în callback `connected`
4. Test: verifică că un client neautorizat nu poate citi datele
5. Verifică că latența notificărilor este sub 20 ms (timestamp LOG)

---

## Criterii de Acceptare

- [ ] Pairing necesită introducerea PIN-ului pe telefon
- [ ] Wireshark arată date criptate (nu plaintext gesturi)
- [ ] MTU negociat la 247 bytes
- [ ] Latență notificare ≤ 20 ms (măsurată cu nRF Sniffer)
- [ ] Reconectare automată după restart mănușă (bond persistent)
