---
title: SIMUT — Professional IoT Firmware for Raspberry Pi Pico W
description: Offline-first temperature monitoring with TFT dashboard, web interface, and OTA updates. Built for labs, pharmacies, and cold chain.
---

![SIMUT Dashboard](images/tft-dashboard.png)

# SIMUT

### Integrated Monitoring & Telemetry System

**Professional-grade IoT firmware for Raspberry Pi Pico W**  
Offline-first · Dual-core · TFT touch dashboard · OTA updates · RBAC · i18n

[![CI](https://github.com/angeloINTJ/simut/actions/workflows/build.yml/badge.svg)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/Version-v1.0.0-blue.svg)](https://github.com/angeloINTJ/simut/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/angeloINTJ/simut/blob/main/LICENSE)
[![Platform](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)

[Quick Start](#quick-start) · [Features](#features) · [Screenshots](#screenshots) · [Documentation](#documentation) · [Contribute](#contribute)

---

## Why SIMUT?

| Need | DIY Arduino Sketch | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Standalone with display | ⚠️ Manual coding | ❌ No TFT support | ✅ Built-in touch UI |
| Regulated environments | ❌ No audit trail | ❌ No user RBAC | ✅ Multi-user, audit logs |
| Cold chain (−80 °C to +45 °C) | ⚠️ Basic readings | ✅ Basic monitoring | ✅ Calibrated multi-sensor |
| 100% offline operation | ✅ Yes | ❌ Often cloud-only | ✅ Local web + display |
| OTA firmware updates | ❌ Manual reflash | ✅ OTA | ✅ OTA + backup/restore |
| Enterprise security | ❌ None | ⚠️ Basic | ✅ HMAC-SHA256 + RBAC + rate limiting |

**SIMUT is for:** laboratories, pharmacies, blood banks, vaccine storage, food cold chain — anywhere you need standalone, secure, auditable temperature monitoring.

**ESPHome / Tasmota may be better if:** you already have Home Assistant and don't need a local display.

---

## Screenshots

| TFT Dashboard | Web UI | TFT Demo |
|:---:|:---:|:---:|
| ![TFT](images/tft-dashboard.png) | ![Web](images/web-dashboard.png) | ![Demo](images/tft-demo.gif) |

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     Raspberry Pi Pico W                      │
│  ┌──────────────────────┐  ┌────────────────────────────────┐│
│  │      Core 0          │  │         Core 1                 ││
│  │  ◆ AppManager        │  │  ◆ DisplayManager (dedicated)  ││
│  │  ◆ SensorManager     │◄─┼─ state / snapshots             ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority               ││
│  │  ◆ TelemetryManager  │  │  ◆ 50 built-in themes          ││
│  │  ◆ CommandManager    │  │  ◆ i18n (PT / EN / ES)         ││
│  └──────────┬───────────┘  └────────────────────────────────┘│
│             │                                                │
│  ┌──────────┴──────────────────────────────────────────────┐ │
│  │  ◆ SPI → ILI9341 TFT 320×240 + XPT2046 Touch            │ │
│  │  ◆ 1-Wire (PIO) → DS18B20 ×10                           │ │
│  │  ◆ I2C → DHT22 Ambient                                  │ │
│  │  ◆ USB CDC → CLI Serial  ·  Bluetooth (BLE) → CLI       │ │
│  │  ◆ WiFi (CYW43439) → HTTP Server + Telemetry            │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

---

## Quick Start

1. **Get the hardware**  
   Raspberry Pi Pico W · ILI9341 TFT 320×240 · XPT2046 touch · DS18B20 sensors · DHT22 · Buzzer · [wiring guide](WIRING.md)

2. **Download firmware**  
   [Latest release](https://github.com/angeloINTJ/simut/releases/latest) → `firmware.uf2`

3. **Flash the device**  
   Hold BOOTSEL, connect USB, drag the `.uf2` file to the `RPI-RP2` drive

4. **Connect**  
   Open **`http://simut.local`** in your browser — default admin password prints once on USB Serial

---

## Features

| Category | Highlights |
|----------|-----------|
| Dual-core Engine | Core 0: sensors + network + web + telemetry. Core 1: dedicated display loop with lock-free snapshots |
| TFT Dashboard | Real-time readings, history graphs, touch-driven settings, **50 built-in themes** + custom theme support |
| Embedded Web Server | Full management interface, live graphs, dark/light themes, file manager, gzip-compressed assets |
| Multi-user RBAC | 10 permission bits, 3 simultaneous sessions, HMAC-SHA256 per-user random salt, brute-force protection |
| Dual Telemetry | HTTP POST + MQTT with JSON/CSV/custom templates, TLS/SSL, configurable endpoints |
| Dual-channel CLI | USB Serial + Bluetooth (BLE) with password-protected access via display PIN |
| OTA Updates | Firmware upload via web UI, config snapshot preservation, automatic reboot with rollback |
| Internationalization | English + Portuguese + Spanish via external language packs (LittleFS) — add your own |
| Enterprise Security | HMAC-SHA256 auth, per-user random salt, exponential backoff rate limiting, crash forensics via watchdog |
| Audit Trail | Persistent log system with rotation, CSV export, tamper-evident binary format |
| Backup & Restore | Full LittleFS backup (.bkp format), CRC32 integrity verification, chip-ID binding |

---

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040, dual-core Cortex-M0+) |
| Display | ILI9341 320×240 TFT (SPI) |
| Touch | XPT2046 resistive touchscreen |
| Sensors | DS18B20 1-Wire ×10 + DHT22 ambient (I2C) |
| Buzzer | Passive piezo (PIO-driven) |
| Storage | 2 MB internal flash (LittleFS) |

---

## Documentation

| Guide | Description |
|-------|-------------|
| [User Manual](MANUAL.md) | Complete hardware setup, display/web/CLI guide, troubleshooting |
| [OTA Update Guide](OTA_USAGE.md) | Firmware update over-the-air via web UI or `curl` |
| [Recovery Guide](RECOVERY.md) | Brick recovery after failed OTA, BOOTSEL rescue |
| [Wiring Diagram](WIRING.md) | Pinout and assembly instructions |
| [Security Policy](https://github.com/angeloINTJ/simut/blob/main/SECURITY.md) | Threat model, credential handling, incident response |

---

## Contribute

SIMUT is open source (MIT). Contributions are welcome!

- [Contributing Guide](https://github.com/angeloINTJ/simut/blob/main/CONTRIBUTING.md)
- [Code of Conduct](https://github.com/angeloINTJ/simut/blob/main/CODE_OF_CONDUCT.md)
- [Report a Bug](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- [Request a Feature](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
- [Security Advisory](https://github.com/angeloINTJ/simut/security/advisories/new)

---

**[⭐ Star on GitHub](https://github.com/angeloINTJ/simut)** · **[📥 Download Firmware](https://github.com/angeloINTJ/simut/releases/latest)**

*Improve this page — [edit on GitHub](https://github.com/angeloINTJ/simut/edit/main/docs/index.md)*
