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

SIMUT is IoT firmware for the **Raspberry Pi Pico W** that monitors temperature, humidity and pressure on up to 16 sensors, and keeps working with or without a network. One source tree builds three published devices:

- a **touch-panel monitor** (320×240 TFT);
- a **character-LCD monitor** (16×2, the *alpha*);
- a **battery logger** that hibernates between readings (the *Air*, experimental).

They share one core:
- binary on-device history and per-channel alarms;
- 32 user accounts with 13 permission bits;
- an embedded web interface;
- telemetry over HTTP(S) or MQTT(S), with a separate line for alarms;
- Home Assistant MQTT Discovery, a Prometheus `/metrics` endpoint and remote syslog (RFC 5424);
- over-the-air updates and a serial console.

## Project status

| | |
|---|---|
| **Current release** | **v2.7.1** (2026-09-22). The 2.7 line left beta with v2.7.0, on measurements: an 8.18 h soak with 0 reboots, and 6 of 6 over-the-air updates with nothing lost. |
| **Published images** | Three images, each as `.uf2` and `.bin`: `release` (TFT touch panel), `alpha` (16×2 LCD with a Bluetooth console) and `air` (headless battery logger). The pt-BR and es-ES language packs and an OTA manifest ship alongside. |
| **On `main`, not yet released** | <ul><li>The Air carries its clock across the sleep: stamps stay within ±0.09 s instead of drifting 0.8 s per wake.</li><li>Telemetry payloads are built one whole record at a time, so a long queue no longer goes out as invalid JSON or skips records.</li><li>With one sensor, the alpha's LCD shows the pending telemetry count, and its Wi-Fi icon fills left to right.</li></ul> |
| **Maturity** | <ul><li>`release`: **stable**.</li><li>`alpha`: published and bench-tested, except its LCD output, which host tests cover — the bench has no HD44780.</li><li>`air`: **experimental**. Its one long soak failed: a sleep in cycle 119 never woke (F28). A watchdog across the wake now mitigates it; the root cause is not confirmed.</li></ul> |
| **Tests** | Every pull request runs 408 host test cases in 7 suites, 60 s of fuzzing and static analysis, and builds all six firmware images from a cold cache. Behaviour on real hardware is verified on a bench — see [Verification](#verification-on-hardware). |

**Known limitations.** Each one is documented where it applies.
- **Updates.** An update over the air reformats the filesystem:
  - Wi-Fi, accounts and sensor slots are carried across. History, calibration files and language packs are not, so the web page downloads a backup before it starts, and restoring it brings them back.
  - There is one firmware slot and no rollback. A bad flash is recovered with BOOTSEL and a USB cable.
- **Unexplained resets.** A watchdog reset with an empty trace (`ctx=209`/`ctx=455`) appeared three times on the bench image on 20–21 September and not since. Both cores are now instrumented to explain the next one.
- **Idle connections.** During the v2.7.0 soak, 7.1 % of responses on an idle keep-alive connection arrived cut short. The device drops a stream it cannot send for 4 s.
- **User list.** Every save of the user list reboots the device, about 25 s each time.
- **Telemetry cursor.** The cursor is a single timestamp, so a record stamped out of order on flash is skipped: 6 of 75,778 records in one measurement.
- **Not a certified instrument.** SIMUT is not a certified metrological instrument. Validate it against your own reference before relying on it for regulated storage.

## Why SIMUT?

| Need | DIY Arduino Sketch | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Standalone with display | ⚠️ Manual coding | ❌ No TFT support | ✅ Built-in touch UI, or a 16×2 LCD |
| Regulated environments | ❌ No audit trail | ❌ No user RBAC | ✅ 32 accounts, per-account panel PIN, signed audit trail |
| Cold chain (probes down to −50 °C) | ⚠️ Basic readings | ✅ Basic monitoring | ✅ Calibrated multi-sensor, maintenance windows |
| Offline operation | ✅ Yes | ❌ Often cloud-dependent | ✅ Full local web + display |
| OTA updates | ❌ Manual reflash | ✅ OTA | ✅ OTA + backup/restore |
| Security | ❌ None | ⚠️ Basic | ✅ HMAC-SHA256, 13-bit RBAC, lockouts, optional HTTPS |
| Home Assistant | ⚠️ Manual setup | ✅ Native | ✅ MQTT Discovery (opt-in) |
| Prometheus metrics | ❌ None | ✅ Built-in | ✅ `/metrics` endpoint |
| Remote audit log | ❌ None | ⚠️ Add-on | ✅ Syslog (RFC 5424 / UDP) |

**SIMUT is for you if:** you need a standalone, secure, auditable temperature monitoring system that works with or without internet — typical in laboratories, pharmacies, blood banks, vaccine storage, and food cold chains.

**ESPHome/Tasmota may be better if:** you don't need a local display and prefer YAML configuration over a built-in web UI. (If what kept you there was Home Assistant: SIMUT speaks MQTT Discovery.)

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
│  │    (alpha: HD44780 16×2 LCD · Air: no display)      │ │
│  │  ◆ GP0–GP15 → 16 universal sensor slots:            │ │
│  │      DS18B20 (1-Wire) · DHT22 · BMP280/BME280 (I2C) │ │
│  │  ◆ USB CDC → serial console (+ Bluetooth: alpha/Air)│ │
│  │  ◆ WiFi (CYW43439) → HTTP(S) server + telemetry     │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                   │
    ┌────┴────┐          ┌───┴────┐         ┌────┴───────┐
    │ Sensors │          │ Web UI │         │  Telemetry │
    │ DS18B20 │          │ Browser│         │ HTTP(S) /  │
    │  DHT22  │          │ (RBAC) │         │  MQTT(S)   │
    │ BMx280  │          └────────┘         └────────────┘
    └─────────┘
```

## Screenshots

| TFT Dashboard | TFT History Graph | Web Dashboard | Early Alpha |
|:---:|:---:|:---:|:---:|
| ![TFT dashboard](docs/images/screens/dashboard.png) | ![TFT graph](docs/images/screens/graph.png) | ![Web dashboard](docs/images/web-dashboard.png) | [![Alpha video](https://img.youtube.com/vi/wLjghqId8nE/hqdefault.jpg)](https://youtu.be/wLjghqId8nE) |

> Every display screen, captured off the real panel framebuffer: [docs/images/screens/screens.md](docs/images/screens/screens.md).
>
> The **Early Alpha** video shows the first TFT + touch prototype — the UI has been redesigned since.

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040, dual-core) |
| Display | `release`: ILI9341 320×240 TFT (SPI, DMA-driven) · `alpha`: HD44780 16×2 character LCD (4-bit) · `air`: none |
| Touch | XPT2046 resistive touchscreen (`release`) |
| Sensors | **16 universal slots on GP0–GP15** — any mix of DS18B20 (1-Wire), DHT22 and BMP280/BME280. A BMx280 is I²C and takes two pins; two of them can share a pair (0x76/0x77) |
| Buzzer | Passive piezo (PIO-driven) — not on the Air |
| Storage | 2 MB internal flash (1 MB firmware slot + 1 MB LittleFS) |

See the **[Wiring Guide](docs/WIRING.md)** for the complete pinout and connection diagrams.

> **SIMUT PCB — layout available for download** — the KiCad board design (`.kicad_pcb`, `.kicad_sch`) lives in [`PCB_test/`](PCB_test/), and the ready-to-fab package (Gerbers + PTH/NPTH drills, no paste layers) is published as a public release: **[simut-pcb-v1.0 — `simut_pcb_fabrication.zip`](https://github.com/angeloINTJ/simut/releases/tag/simut-pcb-v1.0)**.

## Key Features

### Sensing and alarms
- **16 universal sensor slots** — GP0–GP15. Each slot takes a DS18B20, a DHT22 or a BMP280/BME280; a BMx280 is retyped automatically from its chip ID. Type and pins are assigned at runtime, with no recompile.
- **Temperature, humidity and pressure** as first-class channels.
- **Calibration** — per-sensor offsets and curves of up to 5 points per channel, linear or smooth.
- **Zero-trust sensor pipeline:**
  - DS18B20 ROM verification, with a swapped probe quarantined until the right one returns;
  - error hysteresis: 3 failures to enter, 5 successes to leave;
  - out-of-range readings rejected.
- **Alarms on every channel:**
  - low and high limits per channel;
  - a fault alarm that fires even when a sensor's limits are off;
  - a 120 s silence and a global mute;
  - buzzer melodies and visual feedback on the display.
- **Maintenance windows** — per sensor, up to 30 days, set from the panel or by a server. While one is open, alarms are suppressed, and the window's start and end are reported as `maint_on` / `maint_off`.

### Touch panel (`release`)
- **320×240 ILI9341 touch panel** — dashboard, history graphs with a min/max band, statistics, calendar, settings.
- **Identity at the panel** — the operator picks an account, then types that account's PIN:
  - 32 accounts, each with its own PIN;
  - a configurable PIN policy: minimum length, 1–3 glyphs per key, digits or 0-9A-Z;
  - a keypad that is re-dealt after every tap;
  - a lockout per account: the sixth failure locks the account, and 20 failures in total lock the panel.
- **Administration on the glass:**
  - a Users item creates accounts and sets their permission bits and PINs;
  - the 12 settings rows are filtered by what the account may do;
  - Settings → 12 starts the setup access point.
- **Top-panel gestures** — a tap toggles min/max, a 3 s hold pins the selection.
- **DMA rendering fast path** — canvas compositing over 62.5 MHz SPI.
- **4 px safe area everywhere** — the screen-alignment offset (±4 px per axis) can never crop content.
- **Themes** — up to 8 loaded from LittleFS (11 ship in `data/themes/`); the editor in `tools/theme-editor/` previews on a live device.
- **Sound system** — Touch, Confirmation, Error, Alarm and Attention classes, 6 melodies each, with separate system and alarm volumes.

### Character LCD (`alpha`)
- **Readings** — cycles every active slot and channel every 3 s, with big digits for temperature and humidity and an `S<n>` tag naming the slot.
- **Setup access point** — shows the address, the SSID and the key, scrolling long values.
- **Bluetooth console** — see the security note under [Environments](#environments).
- **On `main`, not yet released** — with a single sensor, the bottom-left corner shows the pending telemetry count (`N`, or `Nk` from a thousand up), and the Wi-Fi icon fills left to right.

### Web interface
- **11 pages** — gzip-compressed (zopfli) in flash, with light and dark themes that follow the system preference, a file manager, and multi-user sessions that expire after 15 minutes idle.
- **Live panel mirror** (`release`) — the panel's current frame in the browser, 213 ms per frame. A click on it is a touch on the glass.
- **Changes say what they cost** — three buttons:
  - *Test*: applied, not saved;
  - *Apply now*: saved, no reboot;
  - *Save and restart*.

  The device classifies each change with a dry run before the page offers them.
- **Wi-Fi scan** — pick the network from a list, even from inside the setup access point.
- **History graphs and CSV export in the browser** — the page downloads the raw binary day files, then decodes, buckets (min/max/mean) and exports them itself. The recent, unsealed hour comes from `/api/history/open`. The chart renderer is embedded — no CDN.
- **HTTP API** — 61 routes. Each one is either gated by a permission or public by design, and CI checks it.

### Telemetry and integrations
- **Four transports** — HTTP, HTTPS, MQTT and MQTTS:
  - payloads in JSON, CSV or a custom template;
  - TLS 1.2 (ECDHE with AES-GCM), with the server certificate checked against an uploaded `/cert.pem`.
- **Batching by quantity:**
  - `t_int` is the minimum batch: the radio stays off until that many records wait (0 = off);
  - `t_bat` is the maximum per request, a ceiling that free memory can lower;
  - the batch size adapts to successes and failures, and the server's response time paces the next one.
- **A second line for alarms:**
  - alarm, fault and maintenance events travel in their own queue (32 by default, up to 64);
  - each event leaves the queue only when the server acknowledges it (HTTP 2xx or an MQTT ack);
  - each carries the name of the account that acted.
- **Integrations** — Home Assistant MQTT Discovery (opt-in), Prometheus `/metrics` (session or HTTP Basic), and remote syslog (RFC 5424 over UDP).
- **Fleet hooks:**
  - `X-SIMUT-*` identity headers on uploads;
  - on the `release` image, an mDNS `_simut._tcp` service with id, version, image and TLS in its TXT record;
  - Bearer tokens and a configurable CORS origin.

### Network and time
- **Wi-Fi that reconnects itself:**
  - a retry ladder: 5 s, doubling to 120 s, then dormancy and a new round;
  - hidden SSIDs and signal-quality checks;
  - static IP, two DNS servers, a custom NTP server or a manual clock, and a configurable web port.
- **Setup access point:**
  - named `<device name>_SETUP` — `simut_SETUP` from the factory;
  - WPA2, with a per-device key shown on the USB console, the TFT boot screen and the alpha's LCD;
  - a captive portal at `http://192.168.4.1`.

  Five ways in:
  - a unit with no network configured opens it by itself (not the Air);
  - an automatic fallback opens it when the network is lost (not the Air);
  - Settings → 12 on the panel;
  - the `ap` console command (USB, or Bluetooth on the alpha and the Air);
  - a 3 s hold on the panel during boot.
- **NTP** — the retry backoff grows from 20 s to 15 min, with a fallback to `pool.ntp.org`. Until NTP syncs, a provisional clock is seeded from the newest stored record.

### Storage and history
- **Compact binary history (V5)** — delta + anchor encoding at 5.38 bytes/record, about 116 days in the 1 MB filesystem (11 channels at a 1-minute cadence, measured on bench files on 2026-07-31):
  - blocks of 60 records, each with its own CRC;
  - the open block is saved after every record;
  - past 86 % full, the oldest day is deleted.
- **Configuration** — CRC32-checked, written to a temporary file and renamed, with a `.bak` fallback. Secrets are obfuscated at rest.
- **Event log** — 2 × 800 records and 155 event codes:
  - routine events are persisted on state changes, with an hourly heartbeat and a count of what was suppressed;
  - security, configuration and fatal records are never filtered.

### Security
- **Accounts and permissions:**
  - 32 accounts, 13 permission bits;
  - nobody can grant a bit they do not hold;
  - backup, restore, OTA and certificate install require the full-admin mask.
- **Passwords:**
  - HMAC-SHA256, 5000 rounds, an 8-byte hardware-random salt per user and a board-bound pepper;
  - a factory-fresh unit generates a random 8-character admin password, prints it once on the USB console and forces a change at the first login.
- **Brute-force limits:**
  - login lockout from 2 s to 300 s per client, with `429` once every lockout slot is taken;
  - per-IP throttling on the heavy routes;
  - the Bluetooth console has its own exponential lockout and stops advertising 5 minutes after boot.
- **Sessions** — an `HttpOnly; SameSite=Strict` cookie (`Secure` over HTTPS), or a Bearer token.
- **Uploads** — path traversal, percent-encoding, control bytes and reserved names are refused, and `/config` is out of the file manager's reach.
- **Optional HTTPS** (`release`) — install the certificate pair with `POST /api/tls`. TLS 1.2, ECDHE with AES-GCM.
- **Audits** — the audits of 2026-08-16, of v2.3.6-beta and of 2026-09-07 are closed. The last finding, V-09 (a restricted account could create one with more bits than it held), was fixed in v2.7.0, and both it and the 2026-09-07 fixes were verified on hardware. See **[SECURITY.md](SECURITY.md)**.

### Resilience and forensics
- **Crash autopsy on every boot** — the watchdog scratch registers name the stalled module on each core. Since v2.7.0, three more records also persist Core 1's module, the free heap and the uptime at the stall.
- **Dual-core flash discipline** — Core 1 is paused around every flash write (measured, not assumed).
- **Watchdog discipline** — the watchdog is fed around every filesystem operation, so slow HTTP clients cannot starve the loop.

### Updates, backup and recovery
- **OTA from the web page:**
  - admin only;
  - the image is checked before it is committed (size, boot2 CRC, image variant) and again on the next boot;
  - Wi-Fi, accounts and sensor slots are carried across, and the rest of the filesystem is reformatted, so the page downloads a backup first;
  - the device is back in under a minute: 52–56 s in the v2.7.0 campaign.
- **Backup & restore** — the whole filesystem in one file, CRC32-checked and bound to the chip.
- **[Recovery guide](docs/RECOVERY.md)** — BOOTSEL, picotool and 1200 bps paths for every failure mode.

### SIMUT Air (experimental)
- **Two modes, no display, no buzzer:**
  - **M0** is awake: web, console, Bluetooth and sensors;
  - **M1** is the cycle: sleep on the RTC alarm, wake, read, write history, and sleep again.
- **The radio only when it pays** — it comes up only when `t_int` records are waiting. A reading wake takes 9.31 s with a DS18B20, and with a 60 s interval the device is awake about 13 % of the time.
- **Charger pin** — a charger-detect pin (GP17 by default) keeps it awake while powered.
- **Full console** — the only published image with the full console.

### Internationalization
- **3 interface languages** — English built in; Portuguese (pt-BR) and Spanish (es-ES) come as `.lng` packs on the filesystem. A device runs English plus the one pack installed.

## Quick Start

### Prerequisites
- [PlatformIO](https://platformio.org/) (Core 6.x or later)
- `pip install zopfli` — optional; the web pages compress 2,888 B smaller with it, and the flash budgets are measured with it
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

# First flash only: upload LittleFS data (language packs, themes, favicon).
# ⚠️ uploadfs REFORMATS the LittleFS partition — on a device already in
# service it destroys history, config and calibration. Never run it again
# after the device has data; language packs can be uploaded later from the
# web file manager instead.
pio run -e pico_w_release -t uploadfs
```

Prefer not to build? Every [release](https://github.com/angeloINTJ/simut/releases/latest) ships `simut_vX.Y.Z_release.uf2`, `_alpha.uf2` and `_air.uf2` (drag-and-drop with BOOTSEL held), the matching `.bin` for over-the-air updates, and the pt-BR and es-ES language packs.

### First Boot
1. **Capture the admin password.** A factory-fresh unit prints a random 8-character admin password **once on the USB serial console** (115200 baud). It is never stored in plain text. If you miss it, `system admin reset confirm` over USB prints a new one.
2. **Join it to your network.** A unit with no network configured opens its setup access point by itself. The Air does not: type `ap` on its console instead.
   - Join `<name>_SETUP` (`simut_SETUP` from the factory). It is WPA2, and its per-device key is printed on the USB console, the TFT boot screen and the alpha's LCD.
   - The portal opens at `http://192.168.4.1`.

   Without a screen, you can use the console instead: `system ssid <name>`, `system pass <secret>`, then `reload confirm`.
3. **Open the web interface** at the address the device got — on the `release` image also `http://simut.local` — and log in as `admin` with the password from step 1. You will be asked to choose a new one.
4. **Add sensors** in **Config → Sensors & GPIO**, or let *Scan for probes* find them.
5. **On the touch panel**, Settings asks for an account and its PIN. The factory admin PIN is `1234`, and it must be changed on first use.

## Project Structure

```
simut/
├── src/                    # Firmware source (C++17)
│   ├── main.cpp            # Entry point
│   ├── AppManager*         # Application state machine, boot, alarms, Air cycle
│   ├── DisplayManager*     # TFT panel and alpha LCD (Core 1), touch, themes
│   ├── WebManager*         # Web server, HTTP API, sessions, OTA, TLS
│   ├── StorageManager*     # LittleFS, config, accounts, history files
│   ├── SensorManager*      # DS18B20 / DHT22 / BMx280 drivers
│   ├── NetworkManager*     # Wi-Fi, reconnect ladder, setup AP, mDNS, NTP
│   ├── TelemetryManager*   # HTTP(S)/MQTT(S) telemetry and the alarm line
│   ├── CommandManager*     # Serial and Bluetooth console
│   ├── LogManager*         # Event log and crash forensics
│   ├── HistoryV5.*         # V5 history codec
│   ├── air/                # SIMUT Air configuration
│   ├── display/            # Keypads, fonts and labels shared by the displays
│   ├── sensors/            # Channel table, calibration curves
│   ├── ota/                # Update staging, validation and applier
│   └── SystemDefs*.h       # System constants and limits
├── data/                   # LittleFS assets (language packs, themes, favicon)
├── PCB_test/               # KiCad PCB design + Gerber/DRL fabrication files
├── test/                   # Native unit tests (Unity), seven suites
├── tools/                  # Build gates, bench suites, PicoHand, release scripts, theme editor
├── docs/                   # Documentation + GitHub Pages site
├── WebUI.h                 # Web UI source (gzipped into src/WebUI_GZ.h at build)
├── AGENTS.md               # Bench manual: flashing, the Air, measuring (Portuguese)
└── platformio.ini          # Build configuration
```

## Building

### Environments

| Environment | Purpose | Published |
|-------------|---------|:---:|
| `pico_w_release` | Production image for the TFT panel: emergency console, HTTPS server, mDNS | ✅ `…_release` |
| `pico_w_alpha` | 16×2 character LCD (HD44780), no touch; emergency console + Bluetooth console | ✅ `…_alpha` |
| `pico_w_air` | **Experimental** — SIMUT Air: headless, no buzzer, hibernation cycle (M0 awake / M1 wake-read-send-sleep); full console + Bluetooth. See §17 of the [User Manual](docs/MANUAL.md) | ✅ `…_air` |
| `pico_w_test` | Bench image: the full console for the test suites; no HTTPS, no mDNS | — |
| `pico_w_test_https` | `pico_w_test` plus the HTTPS server, for TLS validation; three of its pages are served from LittleFS to fit | — |
| `pico_w_asserts` | Release + concurrency assertions | — |
| seven `native*` envs | Host-side unit tests — see [Testing](#testing) | — |

> **Security note for `pico_w_alpha` and `pico_w_air`:** both compile the
> Bluetooth SPP console in (`SIMUT_BLUETOOTH=1`), so on those two images it is
> live attack surface. It is authenticated by the **admin web password**, with
> an exponential lockout that survives a reconnect, and a discovery window that
> closes 5 minutes after boot. Recovery commands are restricted to USB.
>
> The setup access point is WPA2 on every image, with a per-device key shown on
> the console and, where there is one, on the display. See [SECURITY.md](SECURITY.md) §2 and §8.

> There is no debug environment. `pico_w_debug` was removed in v2.4.1 after never once linking: at `-Og` the image overflowed the 1020 KB app slot by ~100 KB. Flash is tight. The release image uses 97.2 % of the 1,044,480 B program slot, and its `.bin` sits 13,196 B under the 1,040,384 B over-the-air ceiling. A GDB target would have to be built by cutting features. For the concurrency tripwire on hardware, use `pico_w_asserts`.

### Build Flags
- `-Os` — optimize for size
- `-Wall -Wextra`, and `-Werror` on `src/` (third-party libraries are not held to it)
- `-specs=nano.specs` — newlib-nano for smaller binary
- `-DNDEBUG` on every image
- LTO is disabled (toolchain limitation with earlephilhower Arduino-Pico)
- The framework is pinned to arduino-pico 5.6.1 and patched by `tools/arduino_pico_overrides/patch.sh`

## Configuration

### Console (CLI)
A serial console is available over USB (115200 baud), and over Bluetooth SPP on the alpha and the Air.

- **The emergency console** runs on the `release` and `alpha` images. Its 14 commands:
  - `show net status`, `show system info`, `show system log`
  - `debug on|off`
  - `system admin reset`, `system format`, `system factory`, `system https off`
  - `system ssid <name>`, `system pass <secret>`, `system cors <origin|off>`
  - `ap`, `reload`, `help`

  Destructive commands ask for `confirm`, and the four recoveries (`system factory`, `system format`, `system admin reset`, `system https off`) are refused over Bluetooth.
- **The full Cisco-style console** (`enable` / `configure terminal`) runs on the `air` image and the `pico_w_test` bench images — see the [CLI Manual](docs/CLI-Manual.md) (in Portuguese). The Air adds `air status | hibernate | stop | idle <sec> | charger <gpio|off>`.

**Where configuration happens:**
- **The web interface** is the day-to-day tool.
- **The touch panel** covers what an operator needs at the device: themes, alarms, sounds, language, their own PIN, users, the PIN policy, touch calibration, display offset, status and the setup access point.

### Web API
The device exposes a REST API at `http://<device-ip>/api/`:
- **61 routes** — 51 gated by a permission, 10 public by design, 0 ungated, checked by `tools/check_authz.py` in CI;
- the route table is in the [User Manual](docs/MANUAL.md);
- [docs/AUTHORIZATION.md](docs/AUTHORIZATION.md) maps each route to its permission;
- [docs/API_POST.md](docs/API_POST.md) documents the POST bodies.

## Testing

### Host tests

```bash
pio test -e native             # validators, telemetry cursor, labels, parsers (185 cases)
pio test -e native_history_v5  # V5 history codec (63)
pio test -e native_cli         # CLI parser (31)
pio test -e native_logpolicy   # edge-triggered log persistence (45)
pio test -e native_alarmqueue  # alarm telemetry queue (39)
pio test -e native_network     # Wi-Fi reconnect state machine (29)
pio test -e native_air         # SIMUT Air persistent config (16)

# V5 codec reference checks (Python vs C++, 20k random cases)
python3 tools/check_history_v5_parity.py --cases 20000
python3 tools/history_v5.py --selftest --trials 200000
```

### Continuous integration

Every push and pull request to `main` runs four jobs:
- **gates** — the host suites plus these checks:
  - secret scan, log-code tables, authorization matrix;
  - licence consistency, filesystem guard, Air consistency;
  - history day-merge tests.
- **firmware** — all six images, built from a cold cache:
  - each is checked against its flash budget and the over-the-air ceiling;
  - the build itself enforces `-Werror` and the web UI, CLI help, log-code, channel-table and language-pack gates.
- **fuzz** — 60 s of libFuzzer against the web-API validators, with contract oracles.
- **static analysis** — cppcheck, at a pinned version.

`main` is protected: eight of these checks must pass before anything merges.

### Verification on hardware

**The bench:**
- a Pico W with the TFT panel and touch;
- a second Pico, the *PicoHand*, which works the target's RESET and BOOTSEL lines, times its awake/asleep line and fakes a charger (see [AGENTS.md](AGENTS.md), in Portuguese);
- bench suites in `tools/` for the web API, the panel, telemetry, OTA, Wi-Fi outages and the Air cycle.

What has been measured on real hardware, latest first:

| Date | What | Result |
|---|---|---|
| 2026-09-23 | Air clock across the sleep (`main`) | Stamps within −0.085 … +0.030 s over 10 wakes (v2.7.1 lost 0.8 s per wake); the NTP correction fell from 9–10 s to 0.08 s |
| 2026-09-23 | Long telemetry queues on the Air (`main`) | 0 invalid bodies; 13,681 of 13,682 records delivered awake, 13,670 of 13,671 hibernating (v2.7.1: 68 of 69 bodies were invalid JSON) |
| 2026-09-22 | v2.7.0 soak | 8.18 h, 0 reboots; the largest free heap block moved −42 B |
| 2026-09-22 | v2.7.0 over-the-air updates | 6 of 6 applied; 57 files restored, 0 records missing |
| 2026-09-22 | Setup access point (v2.7.1) | A client joins in 4.1 s, on `release` and on `alpha` with Bluetooth live, also with a randomised MAC. The automatic fallback opens after 6–7 min without a network |
| 2026-09-22 | V-09 fix | 10 of 10 verdicts, with positive controls |
| 2026-09-21 | Collector down for 3 h 58 min | 237 records queued, 0 reboots; drained in one round with 0 missing, plus 25 alarm-line records |
| 2026-09-21 | Web suites | 67/67 as admin, 87/87 as a restricted account; 500 flash-writing commits, 0 reboots |
| 2026-09-21 | Wi-Fi scan | 18 of 18, 0.94 s per sweep, also from inside the access point |
| 2026-09-20 | Panel accounts, PINs and policy | 32/32 |
| 2026-09-19 | Panel mirror | 613 → 213 ms per frame; pixel-exact against the framebuffer (0 of 76,800 differ) |
| 2026-09-11 | Power cut during an update | Only the ~25 s apply window leaves the device needing BOOTSEL |
| 2026-08-10 | History across resets | 10 of 10 hardware resets and 10 of 10 reboots lost 0 records |

The 16×2 LCD is the one output not validated on glass. The bench has no HD44780, so host tests check what it is handed.

## Documentation

| Document | Description |
|----------|-------------|
| [User Manual](docs/MANUAL.md) | Hardware setup, display/web/console guide, OTA, API reference, troubleshooting — kept current |
| [Manual do Usuário (pt-BR)](docs/MANUAL.pt-BR.md) | The same manual, in Portuguese |
| [Illustrated manual (pt-BR)](docs/MANUAL.pt-BR.html) | Product manual with real screenshots — **depicts v2.1.10**; the manuals above describe what the firmware does now |
| [Wiring Guide](docs/WIRING.md) | Complete pinout and connection diagrams |
| [Over-the-air updates](docs/OTA_USAGE.md) | Updating from the web page, and what survives it |
| [Recovery Guide](docs/RECOVERY.md) | Brick recovery — BOOTSEL, picotool, 1200 bps reset |
| [CLI Manual](docs/CLI-Manual.md) | Full console reference, the Air's included (in Portuguese) |
| [Authorization matrix](docs/AUTHORIZATION.md) | Every HTTP route and the permission it requires |
| [Security Policy](SECURITY.md) | Threat model, credential handling, incident response |
| [Documentation index](docs/README.md) | Which documents are kept current and which are snapshots |
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
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/angeloINTJ"><img src="https://avatars.githubusercontent.com/u/117550822?v=4?s=100" width="100px;" alt="Ângelo Moisés Alves"/><br /><sub><b>Ângelo Moisés Alves</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Documentation">📖</a> <a href="#design-angeloINTJ" title="Design">🎨</a> <a href="#hardware-angeloINTJ" title="Hardware">🔌</a> <a href="#security-angeloINTJ" title="Security">🛡️</a> <a href="#maintenance-angeloINTJ" title="Maintenance">🚧</a></td>
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

Copyright © 2026 Ângelo Moisés Alves
