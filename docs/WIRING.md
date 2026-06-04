# SIMUT — Wiring Guide

## Pinout Diagram

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
 DHT22 SLOT10  ──┤10│     RP2040       │  │
                 |  │                  │  │
                 │  │    Dual-Core     │  │
    TFT MISO   ──┤16│    133 MHz       │  │
    TFT CS     ──┤17│                  │  │── GND ──── Common Ground
    TFT SCK    ──┤18│                  │  │── 3V3 ──── Display VCC
    TFT MOSI   ──┤19│                  │  │── 3V3 ──── Sensors VCC
    TFT DC     ──┤20│                  │  │
    TFT RST    ──┤21│                  │  │
    Touch CS   ──┤22│                  │  │
                 │  │                  │  │
    Buzzer     ──┤26│                  │  │
    Touch IRQ  ──┤27│                  │  │
                 │  ╰──────────────────╯  │
                 │        ┌──────┐        │
                 │        │ micro│        │
                 │        │ USB  │        │
                 │        └──────┘        │
                 └────────────────────────┘
```

## Pin Reference

| GPIO | Function               | Connection                               |
|------|------------------------|------------------------------------------|
| 0    | DS18B20 SLOT0 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
...
| 9    | DS18B20 SLOT9 (1-Wire) | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 10   | DHT22 SLOT10 (1-Wire)  | Sensor data line + 4.7 kΩ pull-up to 3V3 |
| 16   | SPI MISO               | TFT MISO                                 |
| 17   | SPI CS                 | TFT CS                                   |
| 18   | SPI SCK                | TFT SCK                                  |
| 19   | SPI MOSI               | TFT MOSI                                 |
| 20   | GPIO                   | TFT DC (Data/Command)                    |
| 21   | GPIO                   | TFT RST (Reset)                          |
| 22   | SPI CS                 | XPT2046 Touch CS                         |
| 26   | PIO                    | Passive Buzzer (+)                       |
| 27   | GPIO                   | XPT2046 Touch IRQ                        |

## Power

- Power the Pico W via **USB** (5 V)
- Display and sensors powered from Pico W's **3V3** pin
- Average consumption: 80–120 mA @ 5 V (display active, WiFi connected)
- Peak consumption: ~250 mA (telemetry burst + TFT render)

## Wiring Checklist

- [ ] All DS18B20 sensors have **4.7 kΩ pull-up resistor** on data line to 3V3
- [ ] TFT shares SPI bus with Touch controller (separate CS pins: GPIO 17 and GPIO 22)
- [ ] Common ground between Pico W, display, and all sensors
- [ ] Passive buzzer connected between GPIO 26 and GND (no resistor needed — PIO-driven)
- [ ] Use quality USB cable (≥22 AWG) — voltage drops cause reboots

---

See [User Manual](MANUAL.md) for complete hardware setup instructions.
