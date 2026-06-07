# SIMUT — Wiring Guide

## Pinout Diagram — ILI9341 TFT + XPT2046 Touch

```
                    Raspberry Pi Pico W
                 ┌────────────────────────┐
                 │  ╭──────────────────╮  │
 DS18B20 SLOT0 ──┤0 │                  │  │
 DS18B20 SLOT1 ──┤1 │                  │  │
 DS18B20 SLOT2 ──┤2 │                  │  │
 DS18B20 SLOT3 ──┤3 │                  │  │
 DS18B20 SLOT4 ──┤4 │                  │  │
 DS18B20 SLOT5 ──┤5 │                  │  │
 DS18B20 SLOT6 ──┤6 │                  │  │
 DS18B20 SLOT7 ──┤7 │                  │  │
 DS18B20 SLOT8 ──┤8 │                  │  │
 DS18B20 SLOT9 ──┤9 │                  │  │
 Slot 10 DHT22  ──┤10│     RP2040       │  │
                 │  │                  │  │
                 │  │    Dual-Core     │  │
    TFT MISO   ──┤16│    133 MHz       │  │
    Touch CS   ──┤17│                  │  │── GND ──── Common Ground
    TFT SCK    ──┤18│                  │  │── 3V3 ──── Display VCC
    TFT MOSI   ──┤19│                  │  │── 3V3 ──── Sensors VCC
    Touch IRQ  ──┤20│                  │  │
                 │  │                  │  │
    Buzzer     ──┤22│                  │  │
                 │  │                  │  │
    TFT RST    ──┤26│                  │  │
    TFT DC     ──┤27│                  │  │
    TFT CS     ──┤28│                  │  │
                 │  ╰──────────────────╯  │
                 │        ┌──────┐        │
                 │        │ micro│        │
                 │        │ USB  │        │
                 │        └──────┘        │
                 └────────────────────────┘
```

## Pinout Diagram — HD44780 16×2 (I2C Mode)

```
                    Raspberry Pi Pico W
                 ┌────────────────────────┐
                 │  ╭──────────────────╮  │
 DS18B20 SLOT0 ──┤0 │                  │  │
 DS18B20 SLOT1 ──┤1 │                  │  │
 DS18B20 SLOT2 ──┤2 │                  │  │
 DS18B20 SLOT3 ──┤3 │                  │  │
 DS18B20 SLOT4 ──┤4 │                  │  │
 DS18B20 SLOT5 ──┤5 │                  │  │
 DS18B20 SLOT6 ──┤6 │                  │  │
 DS18B20 SLOT7 ──┤7 │                  │  │
 DS18B20 SLOT8 ──┤8 │                  │  │
 DS18B20 SLOT9 ──┤9 │                  │  │
 Slot 10 DHT22  ──┤10│     RP2040       │  │
                 │  │                  │  │
                 │  │    Dual-Core     │  │
                 │  │    133 MHz       │  │── GND ──── Common Ground
                 │  │                  │  │── 5V  ──── LCD VCC (via backpack)
                 │  │                  │  │── 3V3 ──── Sensors VCC
                 │  │                  │  │
    Buzzer     ──┤22│                  │  │
                 │  │                  │  │
    LCD SCL    ──┤27│                  │  │
    LCD SDA    ──┤26│                  │  │
                 │  ╰──────────────────╯  │
                 │        ┌──────┐        │
                 │        │ micro│        │
                 │        │ USB  │        │
                 │        └──────┘        │
                 └────────────────────────┘
```

## Pinout Diagram — HD44780 16×2 (Parallel 4-bit Mode)

```
                    Raspberry Pi Pico W
                 ┌────────────────────────┐
                 │  ╭──────────────────╮  │
 DS18B20 SLOT0 ──┤0 │                  │  │
 DS18B20 SLOT1 ──┤1 │                  │  │
 DS18B20 SLOT2 ──┤2 │                  │  │
 DS18B20 SLOT3 ──┤3 │                  │  │
 DS18B20 SLOT4 ──┤4 │                  │  │
 DS18B20 SLOT5 ──┤5 │                  │  │
 DS18B20 SLOT6 ──┤6 │                  │  │
 DS18B20 SLOT7 ──┤7 │                  │  │
 DS18B20 SLOT8 ──┤8 │                  │  │
 DS18B20 SLOT9 ──┤9 │                  │  │
 Slot 10 DHT22  ──┤10│     RP2040       │  │
                 │  │                  │  │
                 │  │    Dual-Core     │  │
    LCD RS     ──┤16│    133 MHz       │  │
    LCD EN     ──┤17│                  │  │── GND ──── Common Ground
    LCD D4     ──┤18│                  │  │── 5V  ──── LCD VCC
    LCD D5     ──┤19│                  │  │── 3V3 ──── Sensors VCC
    LCD D6     ──┤20│                  │  │
    LCD D7     ──┤21│                  │  │
    Buzzer     ──┤22│                  │  │
                 │  ╰──────────────────╯  │
                 │        ┌──────┐        │
                 │        │ micro│        │
                 │        │ USB  │        │
                 │        └──────┘        │
                 └────────────────────────┘
```

## Pin Reference — ILI9341 TFT + XPT2046 Touch *(default)*

| GPIO | Function               | Connection                               |
|------|------------------------|------------------------------------------|
| 0    | DS18B20 SLOT0 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 1    | DS18B20 SLOT1 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 2    | DS18B20 SLOT2 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 3    | DS18B20 SLOT3 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 4    | DS18B20 SLOT4 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 5    | DS18B20 SLOT5 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 6    | DS18B20 SLOT6 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 7    | DS18B20 SLOT7 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 8    | DS18B20 SLOT8 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 9    | DS18B20 SLOT9 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 10   | Slot 10 DHT22 (1-Wire)  | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 16   | SPI0 MISO              | TFT MISO + Touch MISO (shared bus)       |
| 17   | SPI0 CS0               | XPT2046 Touch CS                         |
| 18   | SPI0 SCK               | TFT SCK + Touch SCK (shared bus)         |
| 19   | SPI0 MOSI              | TFT MOSI + Touch MOSI (shared bus)       |
| 20   | GPIO                   | XPT2046 Touch IRQ (PENIRQ)               |
| 22   | PIO                    | Passive Buzzer (+)                       |
| 26   | GPIO                   | TFT RST (Reset)                          |
| 27   | GPIO                   | TFT DC (Data/Command)                    |
| 28   | GPIO                   | TFT CS (Chip Select)                     |

## Pin Reference — HD44780 16×2 I2C *(SIMUT_DISPLAY_ALPHA, default)*

| GPIO | Function               | Connection                               |
|------|------------------------|------------------------------------------|
| 0    | DS18B20 SLOT0 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 1    | DS18B20 SLOT1 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 2    | DS18B20 SLOT2 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 3    | DS18B20 SLOT3 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 4    | DS18B20 SLOT4 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 5    | DS18B20 SLOT5 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 6    | DS18B20 SLOT6 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 7    | DS18B20 SLOT7 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 8    | DS18B20 SLOT8 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 9    | DS18B20 SLOT9 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 10   | Slot 10 DHT22 (1-Wire)  | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 22   | PIO                    | Passive Buzzer (+)                       |
| 26   | I2C1 SDA               | LCD SDA (PCF8574 backpack)               |
| 27   | I2C1 SCL               | LCD SCL (PCF8574 backpack)               |

> **Note:** Uses I2C1 bus on GPIO 26/27 — keeps GPIO 0-15 free for sensors. PCF8574 backpack at address `0x27` (default). Set `-DHD44780_I2C_ADDR=0x3F` in build flags if your backpack uses the alternative address. I2C pull-up resistors (4.7 kΩ) are built into the backpack — no external resistors needed. **All 10 sensor slots + DHT22 available.**

## Pin Reference — HD44780 16×2 Parallel *(SIMUT_DISPLAY_ALPHA, -DHD44780_MODE_PARALLEL=1)*

| GPIO | Function               | Connection                               |
|------|------------------------|------------------------------------------|
| 0    | DS18B20 SLOT0 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 1    | DS18B20 SLOT1 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 2    | DS18B20 SLOT2 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 3    | DS18B20 SLOT3 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 4    | DS18B20 SLOT4 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 5    | DS18B20 SLOT5 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 6    | DS18B20 SLOT6 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 7    | DS18B20 SLOT7 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 8    | DS18B20 SLOT8 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 9    | DS18B20 SLOT9 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 10   | Slot 10 DHT22 (1-Wire)  | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 16   | LCD RS (register sel)  | HD44780 pin 4                            |
| 17   | LCD EN (enable)        | HD44780 pin 6                            |
| 18   | LCD D4 (data bit 4)    | HD44780 pin 11                           |
| 19   | LCD D5 (data bit 5)    | HD44780 pin 12                           |
| 20   | LCD D6 (data bit 6)    | HD44780 pin 13                           |
| 21   | LCD D7 (data bit 7)    | HD44780 pin 14                           |
| 22   | PIO                    | Passive Buzzer (+)                       |

> **Note:** Uses GPIO 16-21 (SPI0 pins, free in alpha build) — keeps GPIO 0-15 free for sensors. **All 10 sensor slots + DHT22 available.** LCD VCC (pin 2) and contrast (VO, pin 3 via 10 kΩ pot) connect to 5V. R/W (pin 5) connects to GND.

## Power

- Power the Pico W via **USB** (5 V)
- TFT display and sensors powered from Pico W's **3V3** pin
- HD44780 LCD powered from **5V** (VBUS pin or external) — **do not** use 3V3 for the LCD
- PCF8574 I2C backpack has its own 3.3V regulator — power from 5V
- Average consumption: 80–120 mA @ 5 V (TFT display active, WiFi connected)
- Peak consumption: ~250 mA (telemetry burst + TFT render)
- HD44780 consumption: ~20 mA (backlight off) to ~80 mA (backlight on)

## Wiring Checklist — ILI9341 TFT

- [ ] All DS18B20 sensors have **4.7 kΩ pull-up resistor** on data line to 3V3
- [ ] TFT shares SPI bus with Touch controller (separate CS pins: GPIO 28 for TFT, GPIO 17 for Touch)
- [ ] Common ground between Pico W, display, and all sensors
- [ ] Passive buzzer connected between GPIO 22 and GND (no resistor needed — PIO-driven)
- [ ] Use quality USB cable (≥22 AWG) — voltage drops cause reboots

## Wiring Checklist — HD44780 I2C

- [ ] LCD VCC powered from **5V** (VBUS or external), **not** 3V3
- [ ] PCF8574 backpack SDA → GPIO 26, SCL → GPIO 27
- [ ] I2C address matches build flag (default `0x27`, alternative `0x3F`)
- [ ] All sensor slots 0-10 available (GPIO 0-15 untouched)
- [ ] All DS18B20 sensors have **4.7 kΩ pull-up resistor** on data line to 3V3
- [ ] Common ground between Pico W, LCD backpack, and all sensors
- [ ] Passive buzzer connected between GPIO 22 and GND

## Wiring Checklist — HD44780 Parallel

- [ ] LCD VCC (pin 2) powered from **5V**, **not** 3V3
- [ ] LCD VO (pin 3) → 10 kΩ potentiometer wiper (ends to 5V and GND) for contrast
- [ ] LCD R/W (pin 5) → GND
- [ ] LCD RS (pin 4) → GPIO 16, EN (pin 6) → GPIO 17
- [ ] LCD D4 (pin 11) → GPIO 18, D5 (pin 12) → GPIO 19
- [ ] LCD D6 (pin 13) → GPIO 20, D7 (pin 14) → GPIO 21
- [ ] All sensor slots 0-10 available (GPIO 0-15 untouched)
- [ ] All DS18B20 sensors have **4.7 kΩ pull-up resistor** on data line to 3V3
- [ ] Common ground between Pico W, LCD, and all sensors
- [ ] Passive buzzer connected between GPIO 22 and GND

---

## Display Mode Comparison

| Pin | ILI9341 TFT *(default)* | HD44780 I2C *(alpha)* | HD44780 Parallel *(alpha)* |
|-----|--------------------------|------------------------|-----------------------------|
| **GPIO 0** | DS18B20 Slot 0 | DS18B20 Slot 0 | DS18B20 Slot 0 |
| **GPIO 1** | DS18B20 Slot 1 | DS18B20 Slot 1 | DS18B20 Slot 1 |
| **GPIO 2** | DS18B20 Slot 2 | DS18B20 Slot 2 | DS18B20 Slot 2 |
| **GPIO 3** | DS18B20 Slot 3 | DS18B20 Slot 3 | DS18B20 Slot 3 |
| **GPIO 4** | DS18B20 Slot 4 | DS18B20 Slot 4 | DS18B20 Slot 4 |
| **GPIO 5** | DS18B20 Slot 5 | DS18B20 Slot 5 | DS18B20 Slot 5 |
| **GPIO 6** | DS18B20 Slot 6 | DS18B20 Slot 6 | DS18B20 Slot 6 |
| **GPIO 7** | DS18B20 Slot 7 | DS18B20 Slot 7 | DS18B20 Slot 7 |
| **GPIO 8** | DS18B20 Slot 8 | DS18B20 Slot 8 | DS18B20 Slot 8 |
| **GPIO 9** | DS18B20 Slot 9 | DS18B20 Slot 9 | DS18B20 Slot 9 |
| **GPIO 10** | DHT22 (slot 10) | DHT22 (slot 10) | DHT22 (slot 10) |
| **GPIO 16** | SPI0 MISO | — | **LCD RS** |
| **GPIO 17** | Touch CS | — | **LCD EN** |
| **GPIO 18** | SPI0 SCK | — | **LCD D4** |
| **GPIO 19** | SPI0 MOSI | — | **LCD D5** |
| **GPIO 20** | Touch IRQ | — | **LCD D6** |
| **GPIO 21** | — | — | **LCD D7** |
| **GPIO 22** | Buzzer | Buzzer | Buzzer |
| **GPIO 26** | TFT RST | **SDA (I2C1)** | — |
| **GPIO 27** | TFT DC | **SCL (I2C1)** | — |
| **GPIO 28** | TFT CS | — | — |
| **Max sensors** | 10× DS18B20 + DHT22 | 10× DS18B20 + DHT22 | 10× DS18B20 + DHT22 |
| **Build flag** | *(default)* | `-DSIMUT_DISPLAY_ALPHA=1` | `-DSIMUT_DISPLAY_ALPHA=1 -DHD44780_MODE_PARALLEL=1` |

> **Design rule:** GPIO 0-15 are reserved for sensors. All display functions use GPIO 16+ only.
> This ensures zero conflicts — every display mode supports the full 10× DS18B20 + DHT22 sensor complement.

## HD44780 Pinout Reference

| HD44780 Pin | Symbol | I2C Mode | Parallel Mode |
|------------|--------|-----------|---------------|
| 1 | VSS | GND | GND |
| 2 | VDD | 5V (via backpack) | 5V |
| 3 | VO | Contrast (backpack pot) | 10 kΩ pot wiper |
| 4 | RS | — | GPIO 16 |
| 5 | R/W | GND (via backpack) | GND |
| 6 | EN | — | GPIO 17 |
| 7 | D0 | — | — |
| 8 | D1 | — | — |
| 9 | D2 | — | — |
| 10 | D3 | — | — |
| 11 | D4 | — | GPIO 18 |
| 12 | D5 | — | GPIO 19 |
| 13 | D6 | — | GPIO 20 |
| 14 | D7 | — | GPIO 21 |
| 15 | LED+ | 5V (via backpack, jumper) | 5V (via 100 Ω resistor) |
| 16 | LED- | GND (via backpack, jumper) | GND |

See [User Manual](MANUAL.md) for complete hardware setup instructions.
