# GloveAssist — System Architecture

## Overview

GloveAssist is a smart glove system for bidirectional communication using
finger gestures and haptic feedback. It runs on two MCUs connected via UART:

| MCU | Role | RTOS |
|-----|------|------|
| **STM32F103C8** (Blue Pill) | Sensor acquisition, rule-based gesture inference, OLED display, haptic safety | Zephyr RTOS |
| **ESP32-WROOM** (DevKit 30-pin) | BLE gateway, WiFi/MQTT cloud gateway, heartbeat responder | Zephyr RTOS |

## Block Diagram

```
┌──────────────────────────────────────────────┐
│                  STM32F103C8                  │
│                                              │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐  │
│  │ ADC      │  │ Gesture  │  │ OLED I2C  │  │
│  │ PA0..PA3 │→│ Classify │  │ SSD1306   │  │
│  │ 4× Flex  │  │ Classify │  │ 128×64    │  │
│  └──────────┘  └────┬─────┘  └───────────┘  │
│                     │                         │
│              ┌──────┴──────┐                  │
│              │ UART TX     │                  │
│              │ 115200 baud │                  │
│              │ 12B frame   │                  │
│              └──────┬──────┘                  │
└─────────────────────┼────────────────────────┘
                      │ PA9/PA10 ↔ GPIO16/GPIO17
┌─────────────────────┼────────────────────────┐
│              ┌──────┴──────┐                  │
│              │ UART RX     │                  │
│              │ Frame parse │                  │
│              └──────┬──────┘                  │
│                     │                         │
│  ┌──────────┐  ┌───┴──────┐  ┌───────────┐  │
│  │ WiFi     │  │ BLE NUS  │  │ MQTT      │  │
│  │ Router   │  │ GATT     │  │ Cloud     │  │
│  │ Link     │  │ Notify   │  │ Publish   │  │
│  └──────────┘  └──────────┘  └───────────┘  │
│                                              │
│                  ESP32-WROOM                  │
└──────────────────────────────────────────────┘
```

## Thread Architecture

### STM32 Threads
| Thread | Priority | Stack | Role |
|--------|----------|-------|------|
| `safety_thread` | 2 | 512 B | Watchdog feed, ESP32 heartbeat monitor |
| `uart_comm_thread` | 4 | 1024 B | Send gesture frames, receive heartbeat and commands |
| `sensor_thread` | 5 | 2048 B | ADC sampling, moving average, gesture classification |
| `haptic_thread` | 6 | 512 B | OLED refresh, motor and active buzzer events |

### ESP32 Threads
| Thread | Priority | Stack | Role |
|--------|----------|-------|------|
| `uart_comm_thread` | 4 | 1024 B | Receive gesture frames, send heartbeat and commands |
| BLE main init | main | 2048 B | BLE NUS GATT notifications |
| `wifi_mqtt_thread` | 8 | 8192 B | WiFi connection and MQTT publish |

## UART Protocol Frame

```
Byte  0:    SOF = 0xAA
Byte  1:    TYPE (gesture, command, heartbeat, raw ADC)
Byte  2:    SEQ
Bytes 3-10: PAYLOAD[8] XOR-obfuscated with SEQ
Byte  11:   CRC-8/CCITT over plaintext TYPE, SEQ, PAYLOAD
Total: 12 bytes
```

## Safety Mechanisms (IEC 61508)

- **Watchdog**: IWDG, 2 s timeout, reset on timeout
- **Sensor validation**: Open-circuit (<0.4 V) / short-circuit (>3.0 V) detection
- **Fallback**: Last known good value on single-sensor fault
- **Lockdown**: All sensors faulted → motor OFF, BLE stopped, OLED shows FAULT
- **Motor isolation**: ULN2003 low-side driver for the vibration motor
