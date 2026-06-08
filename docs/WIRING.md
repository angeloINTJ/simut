# SIMUT — Wiring Guide

## Pinout Diagram — ILI9341 TFT + XPT2046 Touch

```
                    Raspberry Pi Pico W
                  ┌────────────────────────┐
                  │  ╭──────────────────╮  │
    SLOT  0 ──────┤0 │                  │  │
    SLOT  1 ──────┤1 │                  │  │
    SLOT  2 ──────┤2 │                  │  │
    SLOT  3 ──────┤3 │                  │  │
    SLOT  4 ──────┤4 │                  │  │
    SLOT  5 ──────┤5 │                  │  │
    SLOT  6 ──────┤6 │                  │  │
    SLOT  7 ──────┤7 │                  │  │
    SLOT  8 ──────┤8 │                  │  │
    SLOT  9 ──────┤9 │                  │  │
    SLOT 10 ──────┤10│     RP2040       │  │
    SLOT 11 ──────┤11│                  │  │
    SLOT 12 ──────┤12│                  │  │
    SLOT 13 ──────┤13│                  │  │
    SLOT 14 ──────┤14│                  │  │
    SLOT 15 ──────┤15│                  │  │
                  │  │    Dual-Core     │  │
     TFT MISO   ──┤16│    133 MHz       │  │── GND ──── Common Ground
     Touch CS   ──┤17│                  │  │── 3V3 ──── Display VCC
     TFT SCK    ──┤18│                  │  │── 3V3 ──── Sensors VCC
     TFT MOSI   ──┤19│                  │  │
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

> **GPIO 0–15 are universal configurable slots.** Each slot accepts any sensor
> type (DS18B20, DHT22, BME280). The user assigns sensor type and GPIOs via CLI
> before physical assembly. See [Slot Configuration](#slot-configuration) below.

## Pinout Diagram — BME280 on I2C (Example)

```
                    Raspberry Pi Pico W
                  ┌────────────────────────┐
                  │  ╭──────────────────╮  │    BME280
    SLOT  0 ──────┤0 │                  │  │   ┌──────────┐
    SLOT  1 ──────┤1 │                  │  │   │ VCC → 3V3│
    SLOT  2 ──────┤2 │                  │  │   │ GND → GND│
    SLOT  3 ──────┤3 │                  │  │   │ SDA → GP4│
   ╭ BME280 SDA ──┤4 │     RP2040       │  │   │ SCL → GP5│
   │ BME280 SCL ──┤5 │                  │  │   │ CSB → 3V3│
   │  SLOT  6 ────┤6 │    Dual-Core     │  │   │ SDO → GND│
   │  SLOT  7 ────┤7 │    133 MHz       │  │── GND ────┤
   │  SLOT  8 ────┤8 │                  │  │── 3V3 ────┤
   │  SLOT  9 ────┤9 │                  │  │   └──────────┘
   │  SLOT 10 ────┤10│                  │  │
   │  SLOT 11 ────┤11│                  │  │   2× 4.7 kΩ pull-up
   │  SLOT 12 ────┤12│                  │  │   on SDA and SCL to 3V3
   │  SLOT 13 ────┤13│                  │  │   (usually built into module)
   │  SLOT 14 ────┤14│                  │  │
   ╰─ SLOT 15 ────┤15│                  │  │
                  │  │                  │  │
    TFT MISO   ───┤16│                  │  │
    ...            ...                  ...  ...
                  └────────────────────────┘
```

## Pinout Diagram — HD44780 16×2 (I2C Mode)

```
                    Raspberry Pi Pico W
                  ┌────────────────────────┐
                  │  ╭──────────────────╮  │
    SLOT  0 ──────┤0 │                  │  │
    SLOT  1 ──────┤1 │                  │  │
    SLOT  2 ──────┤2 │                  │  │
    SLOT  3 ──────┤3 │                  │  │
    SLOT  4 ──────┤4 │                  │  │
    SLOT  5 ──────┤5 │                  │  │
    SLOT  6 ──────┤6 │                  │  │
    SLOT  7 ──────┤7 │                  │  │
    SLOT  8 ──────┤8 │                  │  │
    SLOT  9 ──────┤9 │                  │  │
    SLOT 10 ──────┤10│     RP2040       │  │
    SLOT 11 ──────┤11│                  │  │
    SLOT 12 ──────┤12│                  │  │
    SLOT 13 ──────┤13│                  │  │
    SLOT 14 ──────┤14│                  │  │
    SLOT 15 ──────┤15│                  │  │
                  │  │                  │  │
                  │  │    Dual-Core     │  │── GND ──── Common Ground
                  │  │    133 MHz       │  │── 5V  ──── LCD VCC (via backpack)
                  │  │                  │  │── 3V3 ──── Sensors VCC
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
    SLOT  0 ──────┤0 │                  │  │
    SLOT  1 ──────┤1 │                  │  │
    SLOT  2 ──────┤2 │                  │  │
    SLOT  3 ──────┤3 │                  │  │
    SLOT  4 ──────┤4 │                  │  │
    SLOT  5 ──────┤5 │                  │  │
    SLOT  6 ──────┤6 │                  │  │
    SLOT  7 ──────┤7 │                  │  │
    SLOT  8 ──────┤8 │                  │  │
    SLOT  9 ──────┤9 │                  │  │
    SLOT 10 ──────┤10│     RP2040       │  │
    SLOT 11 ──────┤11│                  │  │
    SLOT 12 ──────┤12│                  │  │
    SLOT 13 ──────┤13│                  │  │
    SLOT 14 ──────┤14│                  │  │
    SLOT 15 ──────┤15│                  │  │
                  │  │                  │  │
     LCD RS     ──┤16│    Dual-Core     │  │
     LCD EN     ──┤17│    133 MHz       │  │── GND ──── Common Ground
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

## Slot Configuration

Slots are **not pre-assigned** to a specific sensor type. Use the CLI to
configure each slot before wiring:

```
gpio                          # Show GPIO resource map
sensor <n> create <type>      # Create a slot (shows pin requirements)
sensor <n> pin <idx>,<gpio>   # Assign GPIO to a pin
sensor <n> name "<name>"      # Set friendly name
sensor <n> active on          # Activate (validates all pins assigned)
write memory                  # Persist to flash
```

### Sensor type GPIO requirements

| Type | GPIOs needed | Pin roles | Max on 16 GPIOs |
|------|-------------|-----------|-----------------|
| DS18B20 | 1 | 1-Wire (pull-up) | 16 |
| DHT22 | 1 | Data (pull-up) | 16 |
| BME280 | 2 | SDA, SCL (pull-up) | 8 |

> **Example:** 10× DS18B20 + 2× BME280 = 10 + 4 = 14 GPIOs. 2 GPIOs remain free.

## BME280 Wiring (I2C)

The BME280 module typically comes on a breakout board with 6 pins:

| BME280 Pin | Connect to |
|-----------|-----------|
| VCC | 3V3 (Pico W pin 36) |
| GND | GND (Pico W pin 38) |
| SDA | GPIO assigned to slot pin[0] (SDA role) |
| SCL | GPIO assigned to slot pin[1] (SCL role) |
| CSB | 3V3 (I2C mode — chip select tied high) |
| SDO | GND (address 0x76) or 3V3 (address 0x77) |

> **I2C pull-ups**: Most BME280 modules include 4.7 kΩ pull-ups on SDA/SCL.
> If yours doesn't, add external 4.7 kΩ resistors from SDA to 3V3 and SCL to 3V3.

Example CLI setup for BME280 on GPIO 4 (SDA) + GPIO 5 (SCL):
```
sensor 0 create bme280
sensor 0 pin 0,4    # SDA → GPIO 4
sensor 0 pin 1,5    # SCL → GPIO 5
sensor 0 name "Lab Pressure"
sensor 0 active on
write memory
```

## Pin Reference — ILI9341 TFT + XPT2046 Touch *(default)*

| GPIO | Function               | Connection                               |
|------|------------------------|------------------------------------------|
| 0–15 | **SLOT 0–15** (configurable) | Any sensor type: DS18B20 (1-Wire), DHT22 (Data), or BME280 (I2C SDA/SCL) |
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
| 0–15 | **SLOT 0–15** (configurable) | Any sensor type: DS18B20, DHT22, or BME280 |
| 22   | PIO                    | Passive Buzzer (+)                       |
| 26   | I2C1 SDA               | LCD SDA (PCF8574 backpack)               |
| 27   | I2C1 SCL               | LCD SCL (PCF8574 backpack)               |

> **Note:** Uses I2C1 bus on GPIO 26/27 — keeps GPIO 0-15 free for sensors.
> PCF8574 backpack at address `0x27` (default). Set `-DHD44780_I2C_ADDR=0x3F`
> in build flags if your backpack uses the alternative address. I2C pull-up
> resistors (4.7 kΩ) are built into the backpack — no external resistors needed.
> **All 16 sensor slots available.**

## Pin Reference — HD44780 16×2 Parallel *(SIMUT_DISPLAY_ALPHA, -DHD44780_MODE_PARALLEL=1)*

| GPIO | Function               | Connection                               |
|------|------------------------|------------------------------------------|
| 0–15 | **SLOT 0–15** (configurable) | Any sensor type: DS18B20, DHT22, or BME280 |
| 16   | LCD RS (register sel)  | HD44780 pin 4                            |
| 17   | LCD EN (enable)        | HD44780 pin 6                            |
| 18   | LCD D4 (data bit 4)    | HD44780 pin 11                           |
| 19   | LCD D5 (data bit 5)    | HD44780 pin 12                           |
| 20   | LCD D6 (data bit 6)    | HD44780 pin 13                           |
| 21   | LCD D7 (data bit 7)    | HD44780 pin 14                           |
| 22   | PIO                    | Passive Buzzer (+)                       |

> **All 16 sensor slots available** — display uses GPIO 16-21 (SPI0 pins,
> free in alpha build). LCD VCC (pin 2) and contrast (VO, pin 3 via 10 kΩ pot)
> connect to 5V. R/W (pin 5) connects to GND.

## Power

- Power the Pico W via **USB** (5 V)
- TFT display and sensors powered from Pico W's **3V3** pin
- HD44780 LCD powered from **5V** (VBUS pin or external) — **do not** use 3V3 for the LCD
- PCF8574 I2C backpack has its own 3.3V regulator — power from 5V
- BME280 sensor module powered from **3V3**
- Average consumption: 80–120 mA @ 5 V (TFT display active, WiFi connected)
- Peak consumption: ~250 mA (telemetry burst + TFT render)
- HD44780 consumption: ~20 mA (backlight off) to ~80 mA (backlight on)

## Wiring Checklist — ILI9341 TFT

- [ ] All DS18B20 sensors have **4.7 kΩ pull-up resistor** on data line to 3V3
- [ ] BME280 modules have I2C pull-ups (usually built-in; verify with multimeter)
- [ ] TFT shares SPI bus with Touch controller (separate CS pins: GPIO 28 for TFT, GPIO 17 for Touch)
- [ ] Common ground between Pico W, display, and all sensors
- [ ] Passive buzzer connected between GPIO 22 and GND (no resistor needed — PIO-driven)
- [ ] Use quality USB cable (>=22 AWG) — voltage drops cause reboots
- [ ] Configure slots via CLI **before** physical wiring — use `gpio` to see the resource map

## Wiring Checklist — HD44780 I2C

- [ ] LCD VCC powered from **5V** (VBUS or external), **not** 3V3
- [ ] PCF8574 backpack SDA → GPIO 26, SCL → GPIO 27
- [ ] I2C address matches build flag (default `0x27`, alternative `0x3F`)
- [ ] All 16 sensor slots available (GPIO 0-15 untouched)
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
- [ ] All 16 sensor slots available (GPIO 0-15 untouched)
- [ ] All DS18B20 sensors have **4.7 kΩ pull-up resistor** on data line to 3V3
- [ ] Common ground between Pico W, LCD, and all sensors
- [ ] Passive buzzer connected between GPIO 22 and GND

---

## Display Mode Comparison

| Pin | ILI9341 TFT *(default)* | HD44780 I2C *(alpha)* | HD44780 Parallel *(alpha)* |
|-----|--------------------------|------------------------|-----------------------------|
| **GPIO 0–15** | SLOT 0–15 (configurable) | SLOT 0–15 (configurable) | SLOT 0–15 (configurable) |
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
| **Max sensors** | Up to 16 (type-dependent) | Up to 16 (type-dependent) | Up to 16 (type-dependent) |
| **Build flag** | *(default)* | `-DSIMUT_DISPLAY_ALPHA=1` | `-DSIMUT_DISPLAY_ALPHA=1 -DHD44780_MODE_PARALLEL=1` |

> **Design rule:** GPIO 0-15 are reserved for sensors. All display functions use
> GPIO 16+ only. This ensures zero conflicts — every display mode supports the
> full sensor slot complement. **Each slot is configurable** via CLI before
> physical assembly — slots are no longer fixed to a specific sensor type.

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
