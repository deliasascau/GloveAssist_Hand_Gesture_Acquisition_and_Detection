# Faza 18 - Securitate reala: BLE SMP + MQTT TLS

**Status:** implementat in firmware, necesita test pe hardware real.

## Ce rezolva

Aceasta faza muta comunicatia externa din modul demo in modul securizat:

- BLE cere pairing cu LE Secure Connections si passkey.
- GATT/NUS nu mai accepta comenzi necriptate de la telefon.
- MQTT foloseste TLS pe portul 8883, cu verificarea certificatului brokerului.
- WiFi/MQTT nu porneste daca lipsesc credentialele sau certificatul CA.

## BLE

Configuratia ESP32 activa:

```kconfig
CONFIG_BT_SMP=y
CONFIG_BT_SMP_SC_ONLY=y
CONFIG_BT_SMP_MIN_ENC_KEY_SIZE=16
CONFIG_BT_SMP_ENFORCE_MITM=y
CONFIG_BT_MAX_PAIRED=1
```

La conectare, ESP32 cere `BT_SECURITY_L4`. Telefonul trebuie sa faca pairing, iar
passkey-ul de 6 cifre apare in logul serial al ESP32:

```text
BLE pairing passkey: 123456
```

Caracteristicile NUS sunt protejate astfel:

- CCC pentru notificari: read/write encrypt.
- RX pentru comenzi telefon -> ESP32 -> STM32: write encrypt.

Pentru prezentare, passkey-ul poate ramane in log serial. Pentru varianta mai
eleganta, ESP32 poate trimite passkey-ul pe UART catre STM32, iar STM32 il afiseaza
pe OLED.

## MQTT TLS

Configuratia ESP32 activa:

```kconfig
CONFIG_MQTT_LIB_TLS=y
CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_TLS_CREDENTIALS=y
CONFIG_MBEDTLS=y
CONFIG_MBEDTLS_ENABLE_HEAP=y
CONFIG_MBEDTLS_HEAP_SIZE=8192
CONFIG_MBEDTLS_PEM_PARSE_C=y
CONFIG_MBEDTLS_X509_CRT_PARSE_C=y
CONFIG_MBEDTLS_CIPHERSUITE_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256=y
CONFIG_MBEDTLS_CIPHERSUITE_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256=y
CONFIG_MBEDTLS_SSL_SERVER_NAME_INDICATION=y
```

Pentru ESP32-WROOM, BLE + WiFi + TLS sunt la limita de RAM. Configuratia curenta
reduce stack-ul thread-ului MQTT la 2 KB si pastreaza heap-ul MbedTLS la 8 KB.
La test real, daca TLS handshake-ul esueaza cu eroare de alocare, creste heap-ul
MbedTLS si muta o functie optionala in build separat.

Setarile sunt in `application/common/include/app_config.h`:

```c
#define ADAFRUIT_IO_HOST        "io.adafruit.com"
#define ADAFRUIT_IO_PORT         8883U
#define MQTT_TLS_SEC_TAG          42
#define ADAFRUIT_IO_CA_CERT_PEM ""
```

Pentru test real, completeaza `ADAFRUIT_IO_CA_CERT_PEM` cu certificatul CA curent
al brokerului. Format recomandat:

```c
#define ADAFRUIT_IO_CA_CERT_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"...\n" \
"-----END CERTIFICATE-----\n"
```

Daca certificatul CA lipseste, thread-ul WiFi/MQTT se opreste intentionat si
logheaza ca lipseste configurarea. Asa evitam sa facem fallback la MQTT in clar.

## Teste recomandate

1. Conecteaza telefonul la BLE si verifica faptul ca cere pairing/passkey.
2. Incearca sa scrii pe caracteristica RX fara pairing: scrierea trebuie respinsa.
3. Configureaza WiFi, Adafruit IO si CA cert, apoi verifica publicarea pe port 8883.
4. Captureaza traficul WiFi cu Wireshark: trebuie sa vezi TLS handshake, nu gesturi in plaintext.

## Limitari curente

- Bonding-ul persistent nu este activat cu `CONFIG_BT_SETTINGS`, ca sa evitam sa marim
  si mai mult imaginea ESP32 in etapa de prototip.
- Certificatul CA nu este inclus automat. Trebuie ales certificatul curent al brokerului
  folosit la demo.
- MQTT LWT si subscribe pentru comenzi din cloud raman pasii urmatori.
