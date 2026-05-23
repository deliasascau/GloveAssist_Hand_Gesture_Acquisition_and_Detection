# Faza 8 — OLED SSD1306 (Character Framebuffer) + PWM Haptic

**Status:** 🔜 PLANIFICATĂ  
**Dependințe:** Faza 5 completă (poate fi paralelizată cu Faza 6–7)  
**Blocat de:** Pinctrl PWM TIM2/TIM3 pe STM32 (de rezolvat)

> **Notă resurse:** LVGL a fost eliminat — necesită ~150 KB RAM pentru framebuffer grafic, iar
> STM32F103C8 are doar 20 KB RAM total. Se folosește **Character Framebuffer (CFB)**:
> text-only, consum ~1 KB RAM.

---

## Obiective

- Re-activare **SPI1** pe STM32 pentru SSD1306 OLED 128×64
- Afișare text cu **Character Framebuffer (CFB)** — `CONFIG_CHARACTER_FRAMEBUFFER=y`
- Mesaje simple: gest curent, stare eroare, stare link
- Re-activare **PWM** (TIM4_CH1 motor vibrație, TIM3_CH1 buzzer)

---

## Hardware

| Pin STM32 | Funcție | SSD1306 |
|-----------|---------|---------|
| PA5 (SPI1 SCK) | Clock | CLK |
| PA7 (SPI1 MOSI) | Data | MOSI |
| PA4 (GPIO CS) | Chip Select | CS |
| PB0 (GPIO DC) | Data/Command | DC |
| PB1 (GPIO RST) | Reset | RST |

---

## Re-activare în Overlay

```dts
/* stm32_min_dev_blue.overlay */
&spi1 {
    status = "okay";
    pinctrl-0 = <&spi1_sck_master_pa5 &spi1_mosi_master_pa7>;
    pinctrl-names = "default";
    cs-gpios = <&gpioa 4 GPIO_ACTIVE_LOW>;

    ssd1306: ssd1306@0 {
        compatible = "solomon,ssd1306fb";
        reg = <0>;
        spi-max-frequency = <8000000>;
        width  = <128>;
        height = <64>;
        segment-offset  = <0>;
        page-offset     = <0>;
        display-offset  = <0>;
        multiplex-ratio = <63>;
        prechargep      = <0x22>;
        dc-gpios   = <&gpiob 0 GPIO_ACTIVE_HIGH>;
        reset-gpios = <&gpiob 1 GPIO_ACTIVE_LOW>;
    };
};
```

### Kconfig display

```kconfig
CONFIG_DISPLAY=y
CONFIG_SSD1306=y
CONFIG_CHARACTER_FRAMEBUFFER=y
# Nu CONFIG_LVGL — prea multa RAM (150 KB) pentru 20 KB RAM total
```

---

## Ecrane CFB (text)

### 1. Ecran Normal (NORMAL_OP)
```
┌────────────────────────────┐
│ GloveAssist                │
│ Gest: FIST                 │
│ Link: OK                   │
└────────────────────────────┘
```

### 2. Ecran Eroare
```
┌────────────────────────────┐
│ SENSOR FAULT               │
│ Finger: MIDDLE             │
│ Status: INTERPOLATED       │
└────────────────────────────┘
```

### 3. Ecran Boot
```
┌────────────────────────────┐
│ GloveAssist v1.0           │
│ Initializing...            │
└────────────────────────────┘
```

---

## PWM Re-activare (dezactivat în Faza 1)

Problema din Faza 1: `st,stm32-pwm.yaml` cere `pinctrl-0` mandatory.

```dts
/* TIM4_CH1 PB6 motor vibratie */
&timers4 {
    status = "okay";
    st,prescaler = <0>;
    pwm4: pwm {
        status = "okay";
        pinctrl-0 = <&tim4_ch1_pwm_out_pb6>;
        pinctrl-names = "default";
    };
};

/* TIM3_CH1 PB4 buzzer */
&timers3 {
    status = "okay";
    st,prescaler = <0>;
    pwm3: pwm {
        status = "okay";
        pinctrl-0 = <&tim3_ch1_pwm_out_pb4>;
        pinctrl-names = "default";
    };
};
```

---

## Plan de lucru

1. Rezolvare pinctrl TIM4/TIM3 (debug overlay)
2. Re-activare SPI1 în overlay
3. Adaugă `CONFIG_DISPLAY=y`, `CONFIG_SSD1306=y`, `CONFIG_CHARACTER_FRAMEBUFFER=y`
4. Implementare afișare text CFB în `haptic_ui.c`
5. Creare pattern-uri PWM în `haptic_ui.c`

---

## Criterii de Acceptare

- [ ] OLED afișează gestul curent ca text (ex: "FIST", "INDEX", "NO LINK")
- [ ] Eroare senzor afișată vizibil (ecran dedicat text)
- [ ] Motor vibrație și buzzer funcționali cu PWM
