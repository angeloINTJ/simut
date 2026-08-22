<p align="center">
  <img src="docs/images/logo-wordmark.svg" alt="SIMUT" height="76">
</p>

# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Integrated Universal Monitoring and Telemetry System

> Professional-grade IoT firmware for Raspberry Pi Pico W

[English](README.md) | [Português](README.pt-BR.md) | [Español](README.es-ES.md)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://arduino-pico.readthedocs.io/)
[![CI](https://github.com/angeloINTJ/simut/actions/workflows/build.yml/badge.svg)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/angeloINTJ/simut?label=Release&color=blue)](https://github.com/angeloINTJ/simut/releases/latest)
[![Docs](https://img.shields.io/badge/Docs-GitHub_Pages-34D058.svg)](https://angelointj.github.io/simut/)
[![Contributors](https://img.shields.io/badge/All_Contributors-5-orange.svg)](#contributors-)
[![Contributions Welcome](https://img.shields.io/badge/Contributions-Welcome-brightgreen.svg)](CONTRIBUTING.md)

<p align="center">
  <img src="docs/images/tft-tour.gif" alt="SIMUT TFT tour — dashboard, history graphs, calendar and settings" width="400">
</p>

## Overview

SIMUT is a professional-grade IoT firmware for the **Raspberry Pi Pico W** that provides real-time temperature, humidity and pressure monitoring through a dual-core architecture. It features a local TFT touchscreen dashboard, an embedded web interface with role-based access control, binary on-device history with client-side graphing, telemetry upload (HTTP/MQTT, with Home Assistant MQTT Discovery), a Prometheus `/metrics` endpoint, remote syslog forwarding (RFC 5424), OTA updates, and a CLI over USB serial.

## Why SIMUT?

| Need | DIY Arduino Sketch | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Standalone with display | ⚠️ Manual coding | ❌ No TFT support | ✅ Built-in touch UI |
| Regulated environments | ❌ No audit trail | ❌ No user RBAC | ✅ Multi-user, audit logs |
| Cold chain (−55 °C and below-freezing probes) | ⚠️ Basic readings | ✅ Basic monitoring | ✅ Calibrated multi-sensor |
| Offline operation | ✅ Yes | ❌ Often cloud-dependent | ✅ Full local web + display |
| OTA updates | ❌ Manual reflash | ✅ OTA | ✅ OTA + backup/restore |
| Security | ❌ None | ⚠️ Basic | ✅ HMAC-SHA256, RBAC, rate limiting, optional HTTPS |
| Home Assistant | ⚠️ Manual setup | ✅ Native | ✅ MQTT Discovery (opt-in) |
| Prometheus metrics | ❌ None | ✅ Built-in | ✅ `/metrics` endpoint |
| Remote audit log | ❌ None | ⚠️ Add-on | ✅ Syslog (RFC 5424 / UDP) |

**SIMUT is for you if:** you need a standalone, secure, auditable temperature monitoring system that works with or without internet — typical in laboratories, pharmacies, blood banks, vaccine storage, and food cold chains.

**ESPHome/Tasmota may be better if:** you don't need a local display and prefer YAML configuration over a built-in web UI. (If what kept you there was Home Assistant: SIMUT now speaks MQTT Discovery.)

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico W                   │
│  ┌──────────────────────┐  ┌────────────────────────────┐│
│  │      Core 0          │  │        Core 1              ││
│  │  (Main Loop)         │  │  (Display Loop)            ││
│  │                      │  │                            ││
│  │  ◆ AppManager ───────┼──┼─ state/snapshots ──────┐   ││
│  │  ◆ SensorManager     │  │  ◆ DisplayManager ◄────┘   ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority           ││
│  │  ◆ TelemetryManager  │  │  ◆ DMA canvas renderer     ││
│  │  ◆ CommandManager    │  │  ◆ Themes                  ││
│  │  ◆ StorageManager    │  │  ◆ i18n (EN/PT/ES packs)   ││
│  │  ◆ NetworkManager    │  │                            ││
│  └──────────┬───────────┘  └────────────────────────────┘│
│             │                                            │
│  ┌──────────┴──────────────────────────────────────────┐ │
│  │  Hardware Interfaces                                │ │
│  │  ◆ SPI → ILI9341 TFT 320×240 + XPT2046 Touch        │ │
│  │  ◆ GP0–GP15 → 16 universal sensor slots:            │ │
│  │      DS18B20 (1-Wire) · DHT22 · BMP280/BME280 (I2C) │ │
│  │  ◆ USB CDC → CLI Serial                             │ │
│  │  ◆ WiFi (CYW43439) → HTTP Server + Telemetry        │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                   │
    ┌────┴────┐          ┌───┴────┐         ┌────┴───────┐
    │ Sensors │          │ Web UI │         │  Telemetry │
    │ DS18B20 │          │ Browser│         │  HTTP/MQTT │
    │  DHT22  │          │ (RBAC) │         │    Server  │
    │ BMx280  │          └────────┘         └────────────┘
    └─────────┘
```

## Screenshots

| TFT Dashboard | TFT History Graph | Web Dashboard | Early Alpha |
|:---:|:---:|:---:|:---:|
| ![TFT dashboard](docs/images/screens/dashboard.png) | ![TFT graph](docs/images/screens/graph.png) | ![Web dashboard](docs/images/web-dashboard.png) | [![Alpha video](https://img.youtube.com/vi/wLjghqId8nE/hqdefault.jpg)](https://youtu.be/wLjghqId8nE) |

> 📸 Every display screen, captured off the real panel framebuffer: [docs/images/screens/screens.md](docs/images/screens/screens.md).
>
> 🎥 The **Early Alpha** video shows the first TFT + touch prototype — the UI has been redesigned since.

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040, dual-core) |
| Display | ILI9341 320×240 TFT (SPI, DMA-driven) |
| Touch | XPT2046 resistive touchscreen |
| Sensors | **16 universal slots on GP0–GP15** — any mix of DS18B20 (1-Wire), DHT22, BMP280/BME280 (I2C, 2 pins) |
| Buzzer | Passive piezo (PIO-driven) |
| Storage | 2 MB internal flash (1 MB firmware slot + 1 MB LittleFS) |

See the **[Wiring Guide](docs/WIRING.md)** for the complete pinout and connection diagrams.

## Key Features

### Sensing
- **16 universal sensor slots** — GP0–GP15, each slot accepts DS18B20, DHT22 or BMP280/BME280; type and pins assigned at runtime, no recompile
- **Temperature, humidity and pressure** as first-class channels, per-sensor calibration offsets and multi-point calibration curves
- **Zero-trust sensor pipeline** — ROM verification, hardware mismatch detection, error hysteresis
- **Per-sensor alarms** — thresholds with buzzer melodies and visual TFT feedback

### Display & UI
- **320×240 ILI9341 TFT** — dashboard, bucketed history graphs with min/max band, statistics, calendar, touch-driven settings
- **DMA rendering fast path** — canvas compositing at wire speed, zero tearing
- **Fingertip password keyboard** — 8 group keys + popup, any of 91 characters in exactly two taps
- **4 px safe area everywhere** — the screen-alignment offset (±4 px per axis) can never crop content
- **Custom themes** loaded from LittleFS (up to 8, offline editor in `tools/theme-editor/`); compile-time theme packs available
- **Sound system** — Touch / Confirmation / Error / Alarm / Attention classes with configurable melodies and volume

### Connectivity & Web
- **Embedded web server** — multi-user sessions, RBAC (10 permission bits), file manager, live dashboard with a display-capture panel
- **gzip-compressed WebUI** — minified inline pages, browser-cacheable, light & dark themes
- **History graphs decimated in the browser** — the page downloads the raw binary day files and does min/max bucketing client-side; the device only serves bytes
- **CSV export in the browser** — decoded from the same raw files by the page itself
- **Telemetry** — HTTP POST and MQTT with JSON / CSV / custom payload templates, TLS support, adaptive batch sizing

### Time & Storage
- **NTP time sync** — exponential backoff, multi-server fallback, virtual RTC seeded from history across reboots
- **Compact binary history (V5)** — delta + anchor encoding at ~5.4 bytes/record ≈ 116 days of records in flash (11 channels at 1-minute cadence)
- **LittleFS** — CRC32 dual-bank config, per-day history files, rotating compact log

### Security
- **Hardened authentication** — HMAC-SHA256 with per-user random salt, 5000 rounds
- **Random admin password on factory reset** — 8 chars shown once on the TFT, never persisted
- **Rate limiter** — 16-slot LRU with 15-min TTL, lockout-aware eviction, exponential backoff
- **Path-traversal-safe uploads** — `..`, percent-encoding, control bytes and reserved chars blocked
- **[SECURITY.md](SECURITY.md)** with threat model, rotation policy, and incident response

### Resilience & Forensics
- **Crash forensics** — black-box profiler with watchdog scratch-register autopsy on every boot
- **Dual-core flash discipline** — Core 1 provably paused around every flash write (measured, not assumed)
- **Watchdog discipline** — feeds around every LittleFS operation; slow HTTP clients cannot starve the loop

### OTA Updates
- **OTA firmware update** — upload via web UI, applied in-place with config snapshot preservation (Wi-Fi, users and sensor slots survive)
- **Backup & restore** — full LittleFS backup/restore with CRC32 integrity verification
- **[Recovery guide](docs/RECOVERY.md)** — BOOTSEL and picotool paths for every failure mode

### Internationalization
- **3 interface languages** — English built-in; Portuguese and Spanish via external `.lng` language packs loaded from LittleFS at boot

## Quick Start

### Prerequisites
- [PlatformIO](https://platformio.org/) (Core 6.x or later)
- Raspberry Pi Pico W
- No local toolchain? `docker compose run build` builds in a container — the path [CONTRIBUTING.md](CONTRIBUTING.md) recommends for new contributors

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Build firmware
pio run -e pico_w_release

# Flash to Pico W (auto-reset via 1200 bps touch; BOOTSEL works too)
pio run -e pico_w_release -t upload

# First flash only: upload LittleFS data (language packs, favicon).
# ⚠️ uploadfs REFORMATS the LittleFS partition — on a device already in
# service it destroys history, config and calibration. Never run it again
# after the device has data; language packs can be uploaded later from the
# web file manager instead.
pio run -e pico_w_release -t uploadfs
```

Prefer not to build? Every [release](https://github.com/angeloINTJ/simut/releases/latest) ships a ready `simut_vX.Y.Z.uf2` (drag-and-drop with BOOTSEL held), plus PlatformIO and Arduino IDE source bundles.

### First Boot
1. The device boots to the dashboard and, on a factory-fresh unit, shows a **random 8-character admin password on the TFT** — write it down, it is never shown again.
2. Configure Wi-Fi from the touch display's settings, **or** hold a finger on the screen for ~3 s during boot to start setup AP mode — the device broadcasts **`simut_SETUP`** for 15 minutes.
3. Open the web interface at `http://simut.local` (mDNS) or the IP shown on the display, and log in as `admin` with the password from step 1. You will be asked to change it.
4. Add sensors in **Config → Sensors & GPIO** (or watch them auto-appear with *Scan for probes*).

## Project Structure

```
simut/
├── src/                    # All firmware source
│   ├── main.cpp            # Entry point
│   ├── AppManager*         # Application state machine
│   ├── DisplayManager*     # TFT display, touch, themes (Core 1)
│   ├── WebManager*         # Web server, API, OTA endpoints
│   ├── StorageManager*     # LittleFS, config, history
│   ├── SensorManager*      # DS18B20 / DHT22 / BMx280 drivers
│   ├── NetworkManager*     # WiFi, mDNS, AP setup mode
│   ├── TelemetryManager*   # MQTT and HTTP telemetry
│   ├── CommandManager*     # CLI parser
│   ├── LogManager*         # Logging and crash forensics
│   ├── history/            # V5 history codec
│   └── SystemDefs*.h       # System constants and limits
├── data/                   # LittleFS assets (language packs, favicon)
├── test/                   # Native unit tests (Unity)
├── tools/                  # screen_mapper, release scripts, theme editor…
├── docs/                   # Documentation + GitHub Pages site
├── WebUI.h                 # Web UI source (gzipped into WebUI_GZ.h at build)
└── platformio.ini          # Build configuration
```

## Building

### Environments

| Environment | Purpose |
|-------------|---------|
| `pico_w_release` | Production firmware — **the image releases ship** |
| `pico_w_test` | Same firmware + full 56-command CLI for bench suites |
| `pico_w_asserts` | Release + concurrency assertions |
| `pico_w_alpha` | Headless build (16×2 char LCD, no TFT) |
| `native`, `native_history_v4/v5`, `native_cli` | Host-side unit tests |
| `native_logpolicy` | Edge-triggered log-persistence filter (18 tests) |

> `pico_w_debug` exists but does not link — at `-Og` the image overflows the 1020 KB app slot. Flash is tight: the release image uses ~97 % of the slot.

### Build Flags
- `-Os` — optimize for size
- `-Wall -Wextra` — elevated warnings
- `-specs=nano.specs` — newlib-nano for smaller binary
- LTO is disabled (toolchain limitation with earlephilhower Arduino-Pico)

## Configuration

### CLI
A command-line interface is available via USB Serial (115200 baud).

- The **release image** ships a minimal 10-command emergency console: `show net status`, `show system info`, `show system log`, `debug on|off`, `system admin reset`, `system format`, `system factory`, `system https off`, `reload`, `help`.
- The **`pico_w_test` image** ships the full Cisco-style CLI (56 commands, `enable` / `configure terminal` modes) — see the [CLI Manual](docs/CLI-Manual.md) (in Portuguese).

Day-to-day configuration is designed to happen on the touch display and the web UI, which are always full-featured.

### Web API
The device exposes a REST API at `http://<device-ip>/api/`. The full route table is in the [User Manual](docs/MANUAL.md).

## Testing

```bash
# Host-side unit tests
pio test -e native            # validators, CRC, float conversion, time logic
pio test -e native_history_v5 # V5 history codec (54 tests)
pio test -e native_cli        # CLI parser

# V5 codec reference checks (Python vs C++, 20k random cases)
python3 tools/check_history_v5_parity.py --cases 20000
python3 tools/history_v5.py --selftest --trials 200000
```

## Documentation

| Document | Description |
|----------|-------------|
| [User Manual](docs/MANUAL.md) | Hardware setup, display/web/CLI guide, OTA, API reference, troubleshooting |
| [Manual do Usuário (pt-BR)](docs/MANUAL.pt-BR.html) | Manual completo em português, com telas reais |
| [Wiring Guide](docs/WIRING.md) | Complete pinout and connection diagrams |
| [Recovery Guide](docs/RECOVERY.md) | Brick recovery — BOOTSEL, picotool, 1200 bps reset |
| [CLI Manual](docs/CLI-Manual.md) | Full command reference for the `pico_w_test` console (in Portuguese) |
| [Security Policy](SECURITY.md) | Threat model, credential handling, incident response |
| [Changelog](CHANGELOG.md) | Version history and feature changes |

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, code conventions, and the pull request process.

All contributors must follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Support

- **Bug reports:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- **Feature requests:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
- **Security vulnerabilities:** See [SECURITY.md](SECURITY.md) — do not open a public issue
- **Questions:** Open a discussion or issue

## Contributors ✨

Thanks goes to these wonderful people:

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
<!-- prettier-ignore-start -->
<!-- markdownlint-disable -->
<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/angeloINTJ"><img src="https://avatars.githubusercontent.com/u/117550822?v=4?s=100" width="100px;" alt="Angelo Moises Alves"/><br /><sub><b>Angelo Moises Alves</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Documentation">📖</a> <a href="#design-angeloINTJ" title="Design">🎨</a> <a href="#hardware-angeloINTJ" title="Hardware">🔌</a> <a href="#security-angeloINTJ" title="Security">🛡️</a> <a href="#maintenance-angeloINTJ" title="Maintenance">🚧</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/LorenzoLongaretto"><img src="https://avatars.githubusercontent.com/u/165825895?v=4?s=100" width="100px;" alt="Lorenzo Longaretto"/><br /><sub><b>Lorenzo Longaretto</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=LorenzoLongaretto" title="Tests">🧪</a> <a href="https://github.com/angeloINTJ/simut/commits?author=LorenzoLongaretto" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/JohnMartin0301"><img src="https://avatars.githubusercontent.com/u/112761826?v=4?s=100" width="100px;" alt="John Martin"/><br /><sub><b>John Martin</b></sub></a><br /><a href="#infra-JohnMartin0301" title="Infrastructure">🚇</a> <a href="https://github.com/angeloINTJ/simut/commits?author=JohnMartin0301" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/f-p-0"><img src="https://avatars.githubusercontent.com/u/239882173?v=4?s=100" width="100px;" alt="f p"/><br /><sub><b>f p</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=f-p-0" title="Documentation">📖</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/drmikecrypto"><img src="https://avatars.githubusercontent.com/u/91358784?v=4?s=100" width="100px;" alt="Mike"/><br /><sub><b>Mike</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Tests">🧪</a> <a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Documentation">📖</a></td>
    </tr>
  </tbody>
</table>
<!-- markdownlint-restore -->
<!-- prettier-ignore-end -->
<!-- ALL-CONTRIBUTORS-LIST:END -->

This project follows the [all-contributors](https://allcontributors.org) specification.

## Powered by SIMUT

Is your product or project using SIMUT? Add this badge to your README, documentation, or product page:

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)

**Large version** (for presentations, posters, or product packaging):

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)

---

## License

MIT License — see [LICENSE](LICENSE) for details.

Copyright © 2026 Angelo Moises Alves
