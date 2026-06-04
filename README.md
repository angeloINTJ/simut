# SIMUT — Integrated Monitoring and Telemetry System

> Professional-grade IoT firmware for Raspberry Pi Pico W

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://arduino-pico.readthedocs.io/)
[![CI](https://github.com/angeloINTJ/simut/actions/workflows/build.yml/badge.svg)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)

## Overview

SIMUT is a professional-grade IoT firmware for the **Raspberry Pi Pico W** that provides real-time temperature and humidity monitoring through a dual-core architecture. It features a local TFT touchscreen dashboard, an embedded web interface with role-based access control, telemetry upload (HTTP/MQTT), a CLI accessible via USB and Bluetooth, and an externalized language-pack system.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico W                    │
│  ┌─────────────────────┐  ┌─────────────────────────────┐│
│  │      Core 0          │  │        Core 1               ││
│  │  (Main Loop)         │  │  (Display Loop)             ││
│  │                      │  │                             ││
│  │  ◆ AppManager ───────┼──┼─ state/snapshots ──────┐   ││
│  │  ◆ SensorManager     │  │  ◆ DisplayManager ◄────┘   ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority           ││
│  │  ◆ TelemetryManager  │  │  ◆ Themes (50 built-in)    ││
│  │  ◆ CommandManager    │  │  ◆ i18n (PT/EN/ES)         ││
│  │  ◆ StorageManager    │  │                             ││
│  │  ◆ NetworkManager    │  │                             ││
│  └──────────┬───────────┘  └─────────────────────────────┘│
│             │                                              │
│  ┌──────────┴───────────────────────────────────────────┐ │
│  │  Hardware Interfaces                                  │ │
│  │  ◆ SPI → ILI9341 TFT 320×240 + XPT2046 Touch         │ │
│  │  ◆ 1-Wire (PIO) → DS18B20 ×10                         │ │
│  │  ◆ I2C → DHT22 Ambient                                │ │
│  │  ◆ USB CDC → CLI Serial                              │ │
│  │  ◆ Bluetooth (BLE) → CLI Remote                       │ │
│  │  ◆ WiFi (CYW43439) → HTTP Server + Telemetry          │ │
│  └──────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                   │
    ┌────┴────┐         ┌───┴────┐         ┌────┴─────┐
    │ Sensors  │         │ Web UI  │         │ Telemetry │
    │ DS18B20  │         │ Browser │         │ HTTP/MQTT │
    │   DHT22  │         │ (RBAC)  │         │  Server   │
    └─────────┘         └─────────┘         └──────────┘
```

## Screenshots

| TFT Dashboard | Web UI Dashboard |
|:---:|:---:|
| ![TFT](docs/images/tft-dashboard.png) | ![Web](docs/images/web-dashboard.png) |

> 📸 **Add screenshots:** See [docs/images/README.md](docs/images/README.md) for instructions on capturing images from your device.

## Key Features

### Sensing & Control
- **Multi-sensor support** — up to 10 DS18B20 (1-Wire/PIO) + 1 DHT22 ambient sensor
- **Zero-trust sensor pipeline** — ROM verification every 5 readings, hardware mismatch detection, error hysteresis
- **Per-sensor alarms** — temperature/humidity thresholds with buzzer melodies and visual TFT feedback
- **Web-based calibration UI** — calibration mode gated by `PERM_CALIB`; reference value input calculates offset automatically
- **Ambient calibration via picoUID** — `calib.csv` supports custom ID, name, and offsets for the DHT22

### Display & UI
- **320×240 ILI9341 TFT** — dashboard, real-time graphs, statistics, touch-driven settings (XPT2046)
- **Touch-priority scheduler** — UI input always wins over background operations
- **50 built-in themes** + up to 8 custom themes loaded from LittleFS (offline editor in `tools/theme-editor/`)
- **Dynamic dashboard layout** — slot-based, theme-aware
- **Atomic screen rendering** — canvas-based off-screen compositing, zero tearing
- **Sound system** — Touch / Confirmation / Error / Alarm / Attention classes with configurable melodies and volume
- **Light & dark themes** for the Web UI with `localStorage` persistence

### Connectivity & Web
- **Embedded web server** — multi-user sessions, RBAC (10 permission bits), file manager, live dashboard
- **gzip-compressed WebUI** — minified inline pages with shared CSS/JS, browser-cacheable
- **Self-service password change** on the login screen with strength meter
- **Telemetry** — HTTP POST and MQTT with JSON / CSV / custom templates, TLS/SSL support, adaptive batch sizing
- **Multi-sensor history graph** — endpoint returning multiple series in one response; configurable range
- **CSV export** — binary `.simx` bundle with magic, version, sensor table, records, and CRC32 trailer
- **Chunked export with adaptive retry** — split on failure with automatic recovery

### CLI & Bluetooth
- **Dual-channel CLI** — USB Serial + Bluetooth with password-protected sessions
- **Custom BT device name** — configurable via web/CLI
- **Deferred-flush logging** during BT login to avoid flash contention

### Time & Storage
- **NTP time sync** — exponential backoff, multi-server fallback, virtual RTC with automatic correction
- **Manual time entry** via Web UI when no NTP is available
- **History codec** — delta + sensor-mask + anchor encoding for compact binary storage
- **LittleFS** — CRC32 dual-bank config, history files, rotating compact log

### Security
- **Hardened authentication** — HMAC-SHA256 with per-user random salt, 5000 rounds, 128-bit hash
- **Random admin password on factory reset** — 8-char shown on TFT, never persisted in flash
- **Rate limiter** — 16-slot LRU with 15-min TTL, lockout-aware eviction, exponential backoff
- **Path-traversal-safe uploads** — `..`, percent-encoding, control bytes and reserved chars blocked
- **`SECURITY.md`** with threat model, rotation policy, and incident response

### Resilience & Forensics
- **Crash forensics** — black-box profiler with watchdog scratch register autopsy
- **Safe reboot path** — USB-friendly reset that keeps the serial port reachable
- **Soft-panic detection** — cross-core health monitoring
- **Watchdog discipline** — feeds around every LittleFS operation and during flash operations

### OTA Updates
- **OTA firmware update** — upload new firmware via web UI, applied in-place with config preservation
- **Backup & restore** — full LittleFS backup/restore with CRC32 integrity verification
- **Snapshot-based config preservation** — critical settings survive firmware apply

### Internationalization
- **2 display languages** — English (inline) + Portuguese/Spanish via external language packs
- **Hot-loadable language packs** from LittleFS
- **i18n inline fallback** for keys not persisted in device language files

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040) |
| Display | ILI9341 TFT 320×240 (SPI) |
| Touch | XPT2046 (SPI) |
| Sensors | DS18B20 (1-Wire) + DHT22 |
| Storage | 2 MB flash (1 MB firmware + 1 MB LittleFS) |
| Buzzer | Passive piezo (PIO-driven) |

## Quick Start

### Prerequisites
- [PlatformIO](https://platformio.org/) (Core 6.x or later)
- Raspberry Pi Pico W

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/angeloINTJ/SIMUT.git
cd SIMUT

# Build firmware
pio run -e pico_w_release

# Flash to Pico W (hold BOOTSEL, connect USB)
pio run -e pico_w_release -t upload

# Upload LittleFS data (language packs, favicon)
pio run -e pico_w_release -t uploadfs
```

### First Boot
1. The device boots and shows the setup screen on the TFT
2. A random 8-character admin password is displayed on the TFT
3. Connect to the SIMUT WiFi access point or connect via USB Serial at 115200 baud
4. Log in via the web interface (`http://simut.local` or the device IP)

## Project Structure

```
SIMUT/
├── src/                    # All source code
│   ├── main.cpp            # Entry point
│   ├── AppManager*.cpp/h   # Application state machine
│   ├── DisplayManager*.cpp/h  # TFT display, touch, themes
│   ├── WebManager*.cpp/h   # Web server, API, OTA endpoints
│   ├── StorageManager.cpp/h   # LittleFS, config, history
│   ├── SensorManager.cpp/h # DS18B20 and DHT22 drivers
│   ├── NetworkManager.cpp/h   # WiFi, mDNS
│   ├── TelemetryManager.cpp/h # MQTT and HTTP telemetry
│   ├── CommandManager.cpp/h   # CLI parser (USB + Bluetooth)
│   ├── LogManager.cpp/h    # Logging and crash forensics
│   ├── SystemDefs*.h       # System constants and limits
│   └── ota/                # OTA update subsystem
├── data/                   # LittleFS assets
│   ├── favicon.ico
│   └── lang/               # Language packs
├── test/                   # Unit tests (Unity framework)
├── tools/                  # Build and development tools
├── docs/                   # Documentation
├── platformio.ini          # Build configuration
├── WebUI.h                 # Web UI source (compressed at build time)
└── LICENSE
```

## Building

### Environments

| Environment | Description |
|-------------|-------------|
| `pico_w_release` | Production firmware (default) |
| `pico_w_debug` | Debug build with extra logging |
| `native` | Host-side unit tests (Unity) |

### Build Flags
- `-Os` — optimize for size (flash is tight at ~98.7%)
- `-Wall -Wextra` — elevated warnings
- `-specs=nano.specs` — newlib-nano for smaller binary
- LTO is disabled (toolchain limitation with earlephilhower Arduino-Pico)

## Configuration

### CLI Commands
A command-line interface is available via USB Serial (115200 baud) and Bluetooth. Key command groups:

- `help` — show available commands
- `conf system` — view/edit system configuration
- `conf sensor` — view/edit sensor configuration
- `conf net` — view/edit network settings
- `conf user` — manage user accounts
- `write memory` — persist changes to flash
- `reload` — reboot the device

### Web API
The device exposes a REST API at `http://<device-ip>/api/`. See [OTA Usage Guide](docs/OTA_USAGE.md) for OTA-specific endpoints.

## Documentation

| Document | Description |
|----------|-------------|
| [User Manual](docs/MANUAL.md) | Complete hardware setup, display/web/CLI guide, troubleshooting |
| [OTA Update Guide](docs/OTA_USAGE.md) | Firmware update over-the-air via web UI or curl |
| [Recovery Guide](docs/RECOVERY.md) | Brick recovery after failed OTA — BOOTSEL and picotool |
| [Security Policy](SECURITY.md) | Threat model, credential handling, incident response |
| [Changelog](CHANGELOG.md) | Version history and feature changes |

## Testing

```bash
# Run unit tests (validators, CRC, float conversion, time logic)
pio test -e native
```

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, code conventions, and the pull request process.

All contributors must follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Support

- **Bug reports:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- **Feature requests:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
- **Security vulnerabilities:** See [SECURITY.md](SECURITY.md) — do not open a public issue
- **Questions:** Open a discussion or issue

## License

MIT License — see [LICENSE](LICENSE) for details.

Copyright © 2026 Angelo Moises Alves
