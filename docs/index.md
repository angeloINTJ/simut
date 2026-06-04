---
title: SIMUT Documentation
description: Professional IoT firmware for Raspberry Pi Pico W
---

# SIMUT — Integrated Monitoring and Telemetry System

Professional-grade IoT firmware for **Raspberry Pi Pico W** — real-time temperature and humidity monitoring.

[![GitHub](https://img.shields.io/badge/GitHub-angeloINTJ%2Fsimut-181717.svg?logo=github)](https://github.com/angeloINTJ/simut)
[![Version](https://img.shields.io/badge/Version-v1.0.0-blue.svg)](https://github.com/angeloINTJ/simut/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/angeloINTJ/simut/blob/main/LICENSE)

## Quick Start

1. **Get the hardware:** Raspberry Pi Pico W + ILI9341 TFT 320×240 + XPT2046 touch + DS18B20 sensors + DHT22
2. **Download firmware:** [Latest release](https://github.com/angeloINTJ/simut/releases/latest) → `firmware.uf2`
3. **Flash:** Hold BOOTSEL, connect USB, drag UF2 to RPI-RP2 drive
4. **Connect:** Open `http://simut.local` in your browser

## Documentation

| Guide | Description |
|-------|-------------|
| [User Manual](MANUAL.md) | Complete hardware setup, display/web/CLI guide, troubleshooting |
| [OTA Update Guide](OTA_USAGE.md) | Firmware update over-the-air via web UI or curl |
| [Recovery Guide](RECOVERY.md) | Brick recovery after failed OTA |
| [Security Policy](../SECURITY.md) | Threat model, credential handling, incident response |

## Contribute

- [Contributing Guide](../CONTRIBUTING.md)
- [Code of Conduct](../CODE_OF_CONDUCT.md)
- [Report a Bug](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- [Request a Feature](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040) |
| Display | ILI9341 320×240 TFT (SPI) |
| Touch | XPT2046 resistive touchscreen |
| Sensors | DS18B20 (1-Wire, up to 10) + DHT22 (ambient) |
| Buzzer | Passive piezo (PIO-driven) |
| Storage | 2 MB internal flash |

## Features

- **Dual-core architecture** — Core 0: sensors/network/web/telemetry, Core 1: display/touch
- **TFT dashboard** — Real-time temperature, humidity, WiFi status, 50 built-in themes
- **Web interface** — Multi-user RBAC, file manager, live graphs, dark/light themes
- **Telemetry** — HTTP/MQTT with JSON/CSV/custom templates
- **CLI** — USB Serial + Bluetooth with password protection
- **OTA updates** — Firmware update via web UI with config preservation
- **Security** — HMAC-SHA256 auth, rate limiting, crash forensics

---

[View on GitHub](https://github.com/angeloINTJ/simut) · [Download Firmware](https://github.com/angeloINTJ/simut/releases/latest)
