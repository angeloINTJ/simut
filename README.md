# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Integrated Universal Temperature Monitoring System for Raspberry Pi Pico W

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://arduino-pico.readthedocs.io/)

## Overview

SIMUT is a professional-grade IoT firmware for the **Raspberry Pi Pico W** that provides real-time temperature and humidity monitoring through a dual-core architecture. It features a local TFT touchscreen dashboard, an embedded web interface with role-based access control, telemetry upload (HTTP/MQTT), and a CLI accessible via USB and Bluetooth.

### Key Features

- **Multi-sensor support** — Up to 10 DS18B20 (1-Wire/PIO) + 1 DHT22 ambient sensor
- **Dual-core architecture** — Core 0 handles logic/network; Core 1 drives the TFT display
- **320×240 ILI9341 TFT** — Dashboard, real-time graphs, stats, settings (touch-driven)
- **50+ color themes** — Including health awareness months and professional palettes
- **Embedded web server** — Multi-user sessions, RBAC, file manager, live dashboard
- **Telemetry** — HTTP POST and MQTT with JSON/CSV/custom templates, TLS/SSL support
- **CLI interface** — USB Serial + Bluetooth with password-protected sessions
- **Alarm system** — Per-sensor temperature/humidity thresholds with buzzer melodies
- **NTP time sync** — Virtual RTC with provisional timestamps and automatic correction
- **Flash storage** — LittleFS with CRC32 dual-bank config, history CSV, and log rotation
- **Crash forensics** — Black-box profiler with watchdog scratch register autopsy
- **i18n** — English, Portuguese, and Spanish display languages

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040) |
| Display | ILI9341 320×240 TFT SPI |
| Touch | XPT2046 resistive touchscreen |
| Temp sensors | DS18B20 (1-Wire, up to 10) |
| Ambient sensor | DHT22 (GPIO 10) |
| Buzzer | Passive buzzer (GPIO 22, PIO-driven) |
| Storage | Internal 2MB Flash (LittleFS) |

### Default GPIO Mapping

| Function | GPIO |
|----------|------|
| DS18B20 (1-Wire bus) | 0 |
| DHT22 (ambient) | 10 |
| Buzzer | 22 |
| TFT CS | *See DisplayManager.h* |
| TFT DC | *See DisplayManager.h* |
| Touch CS | *See DisplayManager.h* |
| SPI CLK/MOSI/MISO | *See DisplayManager.h* |

## Software Dependencies

### Arduino Libraries (install via Library Manager or PlatformIO)

- [arduino-pico](https://github.com/earlephilhower/arduino-pico) — RP2040 Arduino core
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit ILI9341](https://github.com/adafruit/Adafruit_ILI9341)
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- [LittleFS](https://github.com/earlephilhower/arduino-pico) — Built into arduino-pico

### Custom PIO Libraries (included or linked separately)

- **OneWirePIO_RP2040** — PIO-based 1-Wire driver for DS18B20
- **DS18B20PIO** — DS18B20 temperature sensor with ROM validation
- **DHTBus / DHT22PIO** — PIO-based DHT22 driver
- **BuzzerPIO_RP2040** — Dual-SM PIO buzzer with PWM amplitude control

## Building

### Arduino IDE

1. Install [arduino-pico](https://arduino-pico.readthedocs.io/en/latest/install.html) board support
2. Select **Raspberry Pi Pico W** as the target board
3. Set **Flash Size** to `2MB (Sketch: 1MB, FS: 1MB)` or similar split
4. Install all required libraries
5. Open `SIMUT.ino` and compile/upload

### PlatformIO

```ini
; platformio.ini (example)
[env:pico_w]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = rpipicow
framework = arduino
board_build.core = earlephilhower
board_build.filesystem_size = 1m
lib_deps =
    adafruit/Adafruit GFX Library
    adafruit/Adafruit ILI9341
    paulstoffregen/XPT2046_Touchscreen
    knolleary/PubSubClient
monitor_speed = 115200
```

### Generating Compressed Web Assets

The web interface HTML is stored in `WebUI.h` as raw string literals. To generate the gzip-compressed version:

```bash
cd tools/
python3 compressor.py
# Outputs WebUI_GZ.h in the project root
```

## Project Structure

```
SIMUT/
├── SIMUT.ino              # Main entry point (setup + loop)
├── SystemDefs.h           # Global types, enums, structs, constants
├── SystemUtils.cpp        # CRC8, filename validation utilities
├── AppManager.h/.cpp      # Application orchestrator (boot, loop, events)
├── SensorManager.h/.cpp   # DS18B20/DHT22 driver with async reads
├── StorageManager.h/.cpp  # LittleFS config, history, calibration
├── NetworkManager.h/.cpp  # WiFi STA/AP, NTP, Virtual RTC
├── WebManager.h/.cpp      # HTTP server, sessions, RBAC, API
├── TelemetryManager.h/.cpp # HTTP/MQTT upload with backoff
├── DisplayManager.h/.cpp  # Core 1 TFT rendering + touch UI
├── CommandManager.h/.cpp  # CLI parser + dual USB/BT output
├── BluetoothManager.h/.cpp # BLE serial with auth + auto-logout
├── SoundManager.h/.cpp    # PIO buzzer, melodies, alarm system
├── LogManager.h/.cpp      # Logger, crash forensics, ring buffer
├── Themes.h/.cpp          # 50+ RGB565 color palettes
├── WebUI.h                # Embedded HTML/CSS/JS (PROGMEM)
├── tools/
│   └── compressor.py      # WebUI.h → WebUI_GZ.h generator
├── LICENSE
├── .gitignore
└── README.md
```

## CLI Commands

Connect via USB Serial (115200 baud) or Bluetooth:

| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `show system info` | Device name, firmware, config |
| `show system log` | Dump system event log from Flash |
| `show sensors` | List mapped sensor database |
| `sensor scan` | Hardware scan for connected sensors |
| `sensor accept <gpio>` | Authorize new sensor on a slot |
| `conf system name <value>` | Set device friendly name |
| `conf system ssid <name>` | Set WiFi SSID |
| `conf system pass <pass>` | Set WiFi password |
| `conf system timezone <val>` | Set UTC offset (e.g., -3) |
| `conf system theme <id>` | Set UI theme by name or index |
| `write memory` | Persist RAM config to Flash |
| `reload` | Reboot system |

## Web Interface

After connecting to WiFi, access the web interface at `http://<device-ip>/`

**Default credentials:**
- **Admin:** `admin` / `simut` (must change on first login)
- **Viewer:** `viewer` / `viewer` (read-only dashboard + history)

## Architecture Highlights

- **Cross-core safety:** Mutex-protected shared state, lock-free event queue, multicore lockout for Flash writes
- **Flash protection:** Two-tier locking (ReadLock for reads, SafeMode with Core 1 pause for writes)
- **Budget timers:** All long operations (graph render, storage cleanup, NTP correction) have configurable time budgets to prevent watchdog timeout
- **Zero-trust sensors:** ROM verification every 5 readings, hardware mismatch detection, error hysteresis (3 failures to flag, 5 successes to recover)

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
