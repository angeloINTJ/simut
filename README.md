# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Integrated Universal Temperature Monitoring System for Raspberry Pi Pico W

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://arduino-pico.readthedocs.io/)
[![Version](https://img.shields.io/badge/Version-v3.34.0-blueviolet.svg)](https://github.com/angeloINTJ/SIMUT/releases)

## Overview

SIMUT is a professional-grade IoT firmware for the **Raspberry Pi Pico W** that provides real-time temperature and humidity monitoring through a dual-core architecture. It features a local TFT touchscreen dashboard, an embedded web interface with role-based access control, telemetry upload (HTTP/MQTT), a CLI accessible via USB and Bluetooth, and an externalized language-pack system.

### Key Features

#### Sensing & control
- **Multi-sensor support** — Up to 10 DS18B20 (1-Wire/PIO) + 1 DHT22 ambient sensor
- **Zero-trust sensor pipeline** — ROM verification every 5 readings, hardware mismatch detection, error hysteresis (3 failures to flag, 5 successes to recover)
- **Per-sensor alarms** — Temperature/humidity thresholds with buzzer melodies and visual TFT feedback
- **Web-based calibration UI** (v3.34.0) — Dashboard ganha toggle "Modo Calibração" (gated by `PERM_CALIB`); informa valor real medido pelo padrão de calibração e o sistema calcula `novo_offset = atual + (ref − leitura)`; reescreve `calib.csv` com `VERSION=epoch` (NTP obrigatório). Aplica-se ao DHT22 ambient e a cada DS18B20 ativo.
- **Ambient calibration via picoUID** (v3.33.2) — `calib.csv` aceita linhas `<picoUID>,t<id>,<offT>,<name>` e `<picoUID>,u<id>,<offH>,<name>` para customizar ID, nome e offsets do DHT22; telemetria emite o ID custom em `{tAMB}`/`{uAMB}` (fallback `boardSerial` quando ausente).

#### Display & UI
- **320×240 ILI9341 TFT** — Dashboard, real-time graphs, statistics, settings (touch-driven via XPT2046)
- **Touch-priority scheduler** — UI input always wins over background CLI/flash operations
- **50 built-in themes** + up to **8 custom themes** loaded from `/themes/*.thm` on LittleFS (offline editor in `tools/theme-editor/`)
- **Dynamic dashboard layout** — slot-based, theme-aware
- **Light & dark themes** for the Web UI with `localStorage`-persisted toggle
- **Atomic screen rendering** (v3.31) — telas refeitas via canvas off-screen + strips de 40px (zero efeito top-down em renders TFT)
- **Sound system com 5 classes** (v3.32) — Touch / Confirmation / Error / Alarm / **Attention** (notificação para telas de confirmação) com 6 melodias cada, volume sistema/alarme separados, mute global com tela de confirmação dedicada
- **Mute Global parity** (v3.32) — TFT e Web sincronizados: ligar Mudo Global desliga todos os sons individuais; ligar qualquer som desliga Mudo Global

#### Connectivity & Web
- **Embedded web server** — multi-user sessions, RBAC (10 perms incluindo `PERM_CALIB`), file manager, live dashboard with operational metrics in real time
- **Externalized WebUI** — gzip-compressed, minified, with shared "Save & Reboot" UX (`/api/commit_all`)
- **CSS/JS dedup** (v3.34.0) — `/style.css` e drawer HTML compartilhados (extraídos de 8 páginas), browser cache 7d
- **Self-service password change** on the login screen with strength meter (POST `/api/login_chpass`)
- **Telemetry** — HTTP POST and MQTT with JSON / CSV / custom templates, TLS/SSL support, dynamic batch sizing keyed to free heap
- **Telemetry Live Preview** — `/config` mostra a linha real (com hwId customizado do ambient)
- **Multi-sensor history graph** (v3.27) — endpoint `/api/history_multi` retorna múltiplas séries em uma única resposta; range expandido (1h, 6h, 24h, 7d, 1M, 1A, MAX)
- **CSV export `.simx`** (v3.28) — bundle binário com magic + version + kind ('H'/'L') + sensor table + records + CRC32 trailer; UI client-side decodifica e gera CSV (BOM UTF-8, ISO-8601). Endpoints: `/api/export/history.bin` e `/api/export/logs.bin` (cap 31 dias, server-side filter por epoch + level)
- **Chunked export with adaptive retry** (v3.27) — split em falha (24h→12h→6h→3h→1h), recovery após 5 OK, AbortController + cancel parcial
- **Configurable history recording interval** — 1 min … 24 h via Web UI or CLI

#### CLI & Bluetooth
- **CLI interface** — USB Serial + Bluetooth (BLE) with password-protected sessions and bounded line buffers (256 chars max)
- **Custom BT device name** (v3.33.1) — nome BT visível na rede = `cfg.deviceName` (configurável via web/CLI), substitui o default `"PicoW Serial XX:XX:..."` da lib
- **Deferred-flush logging during BT login** — eliminates flash contention on auth path

#### Time & storage
- **NTP time sync** — exponential backoff, multi-server fallback (`pool.ntp.org`), virtual RTC with provisional timestamps and automatic correction
- **Manual time entry** — set date/time via Web UI when no NTP is available
- **DNS configurable separately from gateway** in static IP mode
- **History codec v2** — delta + sensor-mask + anchor encoding, ~45% size reduction vs v1 (binary `.bin`, header `SIM2`, 1 anchor every 60 records + 59 zigzag-varint deltas). Offline converter `tools/history_v1_to_v2.py`.
- **Flash storage** — LittleFS with CRC32 dual-bank config, history `.bin` files, and rotating compact log

#### Security
- **Hardened authentication** — HMAC-SHA256 with per-user random salt (8 bytes via `hwrand32`), 5000 rounds, 128-bit hash
- **Random admin password on factory reset** — 8-char `[A-Z2-9]` shown on TFT; never persisted in flash
- **Rate limiter** — 16-slot LRU with 15-min TTL, lockout-aware eviction, exponential backoff
- **Path-traversal-safe uploads** — `..`, percent-encoding, control bytes and reserved chars blocked
- **`SECURITY.md`** with threat model, rotation policy, and incident response

#### Resilience & forensics
- **Crash forensics** — black-box profiler with watchdog scratch register autopsy; `0xA11FA1E5` "alive magic" distinguishes post-flash artifacts, external resets, and real crashes
- **`safeReboot()`** — reset path that keeps `/dev/ttyACM0` reachable after reload (USB-friendly)
- **Soft-panic detection** — cross-core health monitoring with signed-arithmetic guard
- **Watchdog discipline** — feeds inserted around every LittleFS open/write/close and during `multicore_lockout`

#### Internationalization
- **2 display languages** — English (inline PROGMEM) + Portuguese / Spanish via external language packs (`data/lang/*.lng`)
- **Hot-loadable language packs** — `GET /api/lang` exposes the active dictionary; help and license texts loaded from `data/system/help_*.txt` and `license_*.txt`
- **Drawer i18n correto** (v3.31.10/11) — ícones do menu hamburguer preservados em PT/qualquer idioma (refator HTML separa `<span class="ico">` de `<span data-i18n>`)
- **i18n inline fallback** — `LANG_JS dict.pt` com chaves não persistidas no `.lng` do device (visíveis sem `uploadfs`)

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
| TFT CS / DC / Touch CS / SPI | *See `DisplayManager.h`* |

## Software Dependencies

### Arduino libraries

- [arduino-pico](https://github.com/earlephilhower/arduino-pico) — RP2040 Arduino core
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit ILI9341](https://github.com/adafruit/Adafruit_ILI9341)
- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- LittleFS — built into arduino-pico

### Custom PIO libraries (by Ângelo Moisés Alves, MIT, RP2040)

- [OneWirePIO_RP2040](https://github.com/angeloINTJ/OneWirePIO_RP2040) — PIO-based 1-Wire driver (components: `OneWirePIO`, `DS18B20PIO` with ROM validation)
- [DHT22PIO_RP2040](https://github.com/angeloINTJ/DHT22PIO_RP2040) — PIO-based DHT22 driver (components: `DHT22PIO`, `DHTBus`)
- [BuzzerPIO_RP2040](https://github.com/angeloINTJ/BuzzerPIO_RP2040) — Dual-SM PIO buzzer with PWM ultrasonic volume control

Also available via Arduino IDE Library Manager (search by exact name).

## Building

### Arduino IDE

1. Install [arduino-pico](https://arduino-pico.readthedocs.io/en/latest/install.html) board support.
2. Select **Raspberry Pi Pico W** as the target board.
3. Set **Flash Size** to `2MB (Sketch: 1MB, FS: 1MB)` or similar split.
4. Install all required libraries.
5. Open `SIMUT.ino` and compile/upload.

### PlatformIO

The repo ships with a complete [`platformio.ini`](platformio.ini) with three envs:

```bash
pio run  -e pico_w_release          # build firmware (release)
pio run  -e pico_w_release -t upload # build + flash via picotool
pio run  -e pico_w_debug             # build firmware (debug, extra logging)
pio test -e native                   # run host-side unit tests via Unity
```

All library versions are pinned exactly (registry `@version` for upstream Adafruit/Knolleary, GitHub URL with full SHA for `PaulStoffregen/XPT2046_Touchscreen` and the three custom PIO libs). For a hand-rolled minimal config without pins, the `lib_deps` should include:

```ini
lib_deps =
    adafruit/Adafruit GFX Library
    adafruit/Adafruit ILI9341
    knolleary/PubSubClient
    https://github.com/PaulStoffregen/XPT2046_Touchscreen.git
    https://github.com/angeloINTJ/OneWirePIO_RP2040.git
    https://github.com/angeloINTJ/DHT22PIO_RP2040.git
    https://github.com/angeloINTJ/BuzzerPIO_RP2040.git
```

The pre-build hook `tools/build_webui_gz.py` regenerates `WebUI_GZ.h` from `WebUI.h` (HTML + CSS + JS minify, then gzip).

### LittleFS image (`data/`)

Upload the `data/` directory to LittleFS using **arduino-pico's "Upload Filesystem Image"** action (or `pio run -t uploadfs`):

- `data/lang/language_pt-BR.lng` — Portuguese display + web dictionary
- `data/lang/language_es-ES.lng` — Spanish display + web dictionary
- `data/system/help_{pt,en}.txt` — CLI help text
- `data/system/license_{pt,en}.txt` — license text shown on the TFT
- `data/themes/*.thm` *(optional)* — custom theme files (offline editor in `tools/theme-editor/`)

## Project Structure

```
SIMUT/
├── SIMUT.ino                    # Main entry point (setup + loop, watchdog feed)
│
├── SystemDefs.h                 # Facade aggregating all sub-headers
├── SystemDefs_Limits.h          # SIMUT_VERSION, MAX_*, perm bitmasks
├── SystemDefs_Records.h         # SystemConfig, UserAccount, on-flash structs
├── SystemDefs_Network.h         # NetworkTimeData, NetworkConfig overlays
├── SystemDefs_Time.h            # timeReached/timeRemaining/timeSince helpers
├── SystemDefs_Logging.h         # LOG_CODE macros, scratch register map
├── SystemDefs_Cli.h             # CliDemand, CLI_LINE_MAX
├── SystemDefs_Validate.h        # isValidName/Ip/UploadFilename helpers
│
├── AppManager.h + 8 split .cpp  # Boot, Loop, Core, Events, Graph, Sensors, Commands, HistoryAlarm
├── DisplayManager.h + 10 split  # Dashboard, Graph, Settings, Auth, Calibration, Alarm, Calendar, Touch, i18n, LangParser, Fonts
├── WebManager.h + 9 split .cpp  # Core, Auth, Api, Calib, Files, History, Commit, Send, Util
│
├── SensorManager.h/.cpp         # DS18B20/DHT22 driver with async reads
├── StorageManager.h/.cpp        # LittleFS config, history v2, calibration, salt
├── NetworkManager.h/.cpp        # WiFi STA/AP, NTP, DNS, virtual RTC, manual time
├── TelemetryManager.h/.cpp      # HTTP/MQTT upload with backoff, batch sizing
├── CommandManager.h/.cpp        # CLI parser + dual USB/BT output
├── BluetoothManager.h/.cpp      # BLE serial with auth + auto-logout
├── SoundManager.h/.cpp          # PIO buzzer, melodies, alarm system
├── LogManager.h/.cpp            # Logger, crash forensics, ring buffer, deferred flush
├── MetricsManager.h/.cpp        # Heap probe, metrics for /api/status
├── Themes.h/.cpp                # 50 built-in palettes + .thm custom loader
├── SystemUtils.cpp              # CRC8, filename validation utilities
├── WebUI.h                      # Embedded HTML/CSS/JS source (PROGMEM, with @LANG/@WEBDICT markers)
├── WebUI_GZ.h                   # Auto-generated gzip blob (do not edit; inclui STYLE_CSS_GZ compartilhado)
├── SECURITY.md                  # Threat model + rotation + incident response
├── STABILITY_PLAN.md            # Audit phases F1..F17 + master findings table
│
├── data/                        # LittleFS image
│   ├── favicon.ico             # v3.34.0: migrado de PROGMEM (-11KB flash)
│   ├── lang/    # language_{pt-BR,es-ES}.lng
│   ├── system/  # help_*.txt, license_*.txt
│   ├── themes/  # custom *.thm (optional)
│   └── history/ # daily YYYYMMDD.bin (codec v2)
│
├── tools/
│   ├── compressor.py             # Legacy WebUI.h → WebUI_GZ.h generator
│   ├── build_webui_gz.py         # PlatformIO pre-build hook (minify + gzip)
│   ├── build_lang_pack.py        # Build language_pt-BR.lng from WebUI.h + sources
│   ├── build_lang_pack_es.py     # Build language_es-ES.lng (PT → ES via dict)
│   ├── build_favicon_header.py   # PNG favicon → PROGMEM header
│   ├── subset_font.py            # Subset 24pt7b font (~22 chars, -8 KB flash)
│   ├── history_v1_to_v2.py       # Offline converter: history v1 → v2 (~45% smaller)
│   ├── backup.sh                 # Snapshot tarball + git bundle
│   ├── theme-editor/             # Offline .thm editor (HTML/JS)
│   ├── test-server/              # Local HTTPS test server for telemetry
│   └── test_*.sh / hw_test_lib.sh # HW validation scripts (SEC + WEB findings)
│
├── docs/
│   └── GLOSSARY.md               # Tag dictionary (BUG/SEC/CON/DOC/F-/Patch/#N)
├── audits/                       # External audit reports
├── LICENSE
├── .gitignore
└── README.md
```

## CLI Commands

Connect via USB Serial (115200 baud) or Bluetooth.

### Diagnostics
| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `show system info` | Device name, firmware version, network state |
| `show system log` | Dump compact event log from flash |
| `show metrics` | Heap, largest block, telemetry retry rate |
| `show sensors` | List mapped sensor database |
| `show history` | Dump history records (binary → human) |

### Sensors
| Command | Description |
|---------|-------------|
| `sensor scan` | Hardware scan for connected sensors |
| `sensor accept <gpio>` | Authorize new sensor on a slot |
| `sensor wipe <gpio>` | Remove sensor from slot |

### Configuration
| Command | Description |
|---------|-------------|
| `conf system name <value>` | Set device friendly name |
| `conf system ssid <name>` | Set WiFi SSID |
| `conf system pass <pass>` | Set WiFi password |
| `conf system timezone <val>` | Set UTC offset (e.g. `-3`) |
| `conf system theme <id>` | Set UI theme by name or index (built-in or custom) |
| `conf system factory` | Trigger factory reset (regenerates random admin password) |
| `conf system admin reset` | Regenerate random admin password (shown on TFT) |
| `conf net dhcp on/off` | Toggle DHCP |
| `conf net ip/mask/gw/dns <addr>` | Static IP fields (DNS independent of gateway) |
| `conf ntp <server>` | NTP server |
| `conf time <YYYY-MM-DD HH:MM:SS>` | Set time manually |
| `conf history interval <minutes>` | History recording interval (1..1440 min) |

### Persistence & control
| Command | Description |
|---------|-------------|
| `write memory` | Persist RAM config to flash |
| `tel sync` | Force telemetry sync now |
| `tel dump` | Stream pending telemetry payload |
| `tel reset` | Invalidate telemetry cursor (RAM + flash) sem reboot (v3.30.4) |
| `clear log` | Wipe compact event log |
| `reload` | Reboot system (uses `safeReboot` — USB-friendly) |

## Web Interface

After joining WiFi, access the device at `http://<device-ip>/` (or the configured port).

**Default credentials after factory reset:**
- **Admin** — username `admin`, password is **randomly generated** on first boot and shown on the TFT for 5 minutes (or until first login). The 8-char password uses `[A-Z2-9]` (no `O/0/I/1`). It is held only in RAM and zeroed after first login.
- **Viewer** — `viewer` / `viewer` (read-only dashboard + history; forced to change password on first login).

Pages: `/`, `/history`, `/files`, `/config`, `/alarms`, `/users`, `/network`, `/license`. All four configuration pages share the same **"Save & Reboot"** UX — pending changes accumulate client-side and are committed in a single atomic `/api/commit_all` POST followed by a clean reboot.

**Calibration UI** (v3.34.0) — integrada ao `/dashboard`. Toggle "🎯 Modo Calibração" aparece se o usuário tem `PERM_CALIB`; abre inputs de Nome, ID e valor de referência por sensor (DHT22 ambient + DS18B20 ativos). Atualiza `calib.csv` via `/api/calib` (NTP-gated). Aceitação de novo DS18B20 via CLI `sensor accept N`.

Themes: built-in dark themes + a refined **light theme** (slate + cyan-700 AA-contrast). Toggle persists in `localStorage`. Custom themes from `data/themes/*.thm` appear in the theme picker once uploaded.

## Architecture Highlights

- **Cross-core safety** — Mutex-protected shared state, lock-free event queue, `multicore_lockout` for flash writes, `__dmb()` barriers on cross-core data/flag pairs.
- **Flash protection** — Two-tier locking (ReadGuard for reads, SafeMode with Core 1 pause for writes); cooperative quiet-mode handshake with hard-reset fallback (`multicore_reset_core1`) for the savings path.
- **Budget timers** — All long operations (graph render, storage cleanup, NTP correction, telemetry POST) have configurable time budgets to prevent watchdog timeout. `WDT_FEED_MAX_WINDOW_MS = 60s` covers TLS POSTs on poor networks.
- **Heap discipline** — `HEAP_RESERVE` differentiated for TLS vs. plain HTTP; no per-request graph caches; `heapLargestBlock` exposed via `/api/status` and `show metrics`.
- **Watchdog autopsy** — `0xA11FA1E5` alive-magic + module trace + soft-panic distinguishing post-flash artifacts, external resets, soft panics, and real HW watchdog events.
- **Audit trail** — phase-by-phase stability roadmap in [`STABILITY_PLAN.md`](STABILITY_PLAN.md); F1..F17 closed (auditoria completa); see [`docs/GLOSSARY.md`](docs/GLOSSARY.md) for inline tag dictionary.

## Security

See [`SECURITY.md`](SECURITY.md) for the full threat model, rotation policy, and incident response. In short: HMAC-SHA256 with per-user random salt, 5000 rounds, 128-bit hash, hardware-randomized factory passwords, path-traversal-safe uploads, rate-limited login with lockout-aware LRU eviction.

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.
