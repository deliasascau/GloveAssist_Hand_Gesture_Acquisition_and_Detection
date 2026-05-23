# Clarificare proiect GloveAssist

## 1. Procesare de date

Fluxul activ este:

1. STM32 citeste 4 senzori flex prin ADC.
2. Valorile sunt validate pentru open/short circuit.
3. Se aplica moving average.
4. Se clasifica gestul.
5. STM32 trimite gestul catre ESP32.
6. ESP32 il expune catre telefon prin BLE si catre medic/cloud prin WiFi + MQTT.

Gesturi implementate:

| ID | Nume | Regula actuala |
|----|------|----------------|
| 0 | NONE | niciun pattern stabil |
| 1 | INDEX | doar index indoit |
| 2 | MIDDLE | doar middle indoit |
| 3 | RING | doar ring indoit |
| 4 | PINKY | doar pinky indoit |
| 5 | FIST | toate cele 4 degete indoite |
| 6 | HELP | ring + pinky indoite, index + middle deschise |

Clasificarea este rule-based, nu AI. Pentru o manusa asistiva este potrivita in etapa de prototip deoarece este explicabila, predictibila si usor de calibrat. Pentru produs final se pot adauga modele ML doar dupa ce datele reale sunt colectate.

## 2. Comunicare STM32 - ESP32: UART vs SPI

### UART

Avantaje:

- simplu electric: TX, RX, GND;
- asincron, nu cere master/slave strict;
- mai usor de debug cu analizor logic sau adaptor USB-UART;
- suficient ca viteza pentru frame-uri de 12 bytes la 115200 baud în prototip;
- mai robust pentru prototip si cablaj pe manusa.

Dezavantaje:

- necesita resynchronization pe SOF daca se pierd bytes;
- nu are clock dedicat, deci baud-rate-ul trebuie sa fie stabil;
- throughput mai mic decat SPI, dar pentru acest proiect nu este o limita.

### SPI

Avantaje:

- throughput mare si timing determinist;
- full-duplex natural;
- bun daca STM32 trebuie sa faca polling strict si ESP32 doar raspunde.

Dezavantaje:

- mai multe fire: SCK, MOSI, MISO, CS, GND;
- cablaj mai sensibil pe o manusa;
- slave SPI pe ESP32 in Zephyr este mai greu de stabilizat decat UART;
- debugging mai greu.

### Alegere recomandata

Pentru proiectul actual: UART este alegerea recomandata.

Motivul principal: payload-ul este foarte mic. La 100 ms se trimit 12-24 bytes, deci UART la 115200 baud este suficient pentru prototip si mai tolerant la cablajul de pe manusa. SPI nu aduce un beneficiu real pentru acest volum de date, dar creste complexitatea hardware si software.

Protocolul intern poate ramane acelasi frame de 12 bytes:

`SOF | TYPE | SEQ | PAYLOAD[8] | CRC8`

Protocolul comun este implementat acum ca `frame_protocol.*`, deoarece este transport-agnostic si in build-ul curent ruleaza peste UART.

## 3. Protocol telefon

Telefonul comunica cu ESP32 prin BLE NUS:

- TX characteristic: ESP32 -> telefon;
- RX characteristic: telefon -> ESP32.

Notificare gest:

```text
GESTURE id=6 name=HELP conf=90 flex=120,118,44,41
```

Notificare calibrare/raw ADC:

```text
I:1195 M:1695 R:3671 P:1845
```

Comenzi telefon -> manusa, scrise binar pe RX characteristic:

| Byte | Semnificatie |
|------|--------------|
| 0 | command ID |
| 1 | finger index |
| 2 | value low byte |
| 3 | value high byte |

Comenzi:

| ID | Nume | Efect |
|----|------|-------|
| 0x01 | CMD_CALIBRATE | porneste calibrarea automata pe STM32 |
| 0x02 | CMD_SET_THRESH | seteaza pragul pentru un deget |
| 0x03 | CMD_RESET | revine la pragurile default |

## 4. Criptare si anti-sniffing

Ce exista acum:

- CRC-8 detecteaza coruperi accidentale ale frame-ului;
- payload-ul este XOR-obfuscat cu cheia derivata din SEQ;
- BLE are pairing SMP, dar configuratia actuala este apropiata de Just Works.

Limitare importanta:

XOR nu este criptare reala. Este doar obfuscare anti-sniffing casual. Nu protejeaza impotriva unui atacator care captureaza mai multe frame-uri.

Recomandarea practica:

- pentru BLE: folosire pairing cu passkey/LE Secure Connections;
- pentru UART local: CRC + sequence + optional MAC scurt daca este nevoie;
- pentru cloud: MQTT peste TLS, port 8883, nu MQTT plain pe 1883.

Pentru demonstratie academica, formularea corecta este:

> Protocolul local are integritate prin CRC si obfuscare anti-sniffing de baza. Canalul BLE/WiFi trebuie securizat cu mecanismele standard ale stack-ului: LE Secure Connections si TLS.

## 5. WiFi, MQTT si transmitere la medic

MQTT este potrivit pentru transmiterea datelor la distanta prin internet:

- ESP32 se conecteaza la WiFi local;
- publica gesturi/status catre broker MQTT;
- medicul vede datele intr-un dashboard sau aplicatie.

Pentru distanta mare nu se foloseste WiFi direct intre manusa si medic. WiFi acopera doar conexiunea locala pana la router. Distanta mare se rezolva prin internet:

Manusa -> WiFi -> Router -> Broker MQTT/Cloud -> Dashboard medic

Pentru produs real:

- MQTT TLS pe 8883;
- QoS 1 pentru alarme HELP;
- QoS 0 pentru date continue;
- publish on-change pentru gesturi;
- heartbeat/status la interval fix;
- Last Will Testament pentru detectarea deconectarii.

## 6. Cele patru directii mari

| Directie | Stare actuala | Ce mai trebuie |
|----------|---------------|----------------|
| Procesare de date | ADC, filtrare, clasificare, calibrare NVS | mai multe gesturi, testare pe mana reala |
| Protocoale de comunicatie | UART STM32-ESP32, BLE NUS, MQTT TLS, `frame_protocol` | testare hardware end-to-end |
| Criptare/sniffing | CRC + XOR + BLE SMP | passkey BLE, TLS, eventual MAC pe frame |
| Siguranta asistiva | watchdog, heartbeat, alarm local | validare hardware, teste de fault reale |
