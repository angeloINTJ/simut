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
├── SIMUT.ino                       # Entry point (setup + loop, watchdog feed)
│
├── SystemDefs.h                    # Facade — agrega os 7 sub-headers abaixo
├── SystemDefs_Limits.h             # SIMUT_VERSION, MAX_SENSORS, PERM_* bitmasks (+PERM_CALIB)
├── SystemDefs_Records.h            # SystemConfig, UserAccount, BinaryHistoryRecord, UiMode
├── SystemDefs_Network.h            # NetworkTimeData, WebConfigData, DisplayOffsetData overlays
├── SystemDefs_Time.h               # timeReached/timeRemaining/timeSince + safeCopy
├── SystemDefs_Logging.h            # LOG_CODE macros, scratch register map (autópsia WDT)
├── SystemDefs_Cli.h                # CliDemand, CLI_LINE_MAX, CMD_* enum
├── SystemDefs_Validate.h           # isValidName/Ip/UploadFilename/CfgString helpers
│
├── AppManager.h
├── AppManager_Boot.cpp             # Sequência de boot do Core 0
├── AppManager_Loop.cpp             # Main loop, dispatch
├── AppManager_Core.cpp             # Construtor, getters, init
├── AppManager_Events.cpp           # Fila de UiEvent + handlers
├── AppManager_Graph.cpp            # Renderização de gráfico no TFT
├── AppManager_Sensors.cpp          # loadAndCalibrateSensors (incl. ambient via picoUID)
├── AppManager_Commands.cpp         # Dispatch de CliDemand
├── AppManager_HistoryAlarm.cpp     # Histórico + checagem de alarmes
│
├── DisplayManager.h
├── DisplayManager.cpp              # Core: bootstrap Core 1, render loop, mutex
├── DisplayManager_Dashboard.cpp    # Painel principal (slot panels, ambient, top bar)
├── DisplayManager_Graph.cpp        # Tela de gráfico
├── DisplayManager_Settings.cpp     # Menus de Configurações (Sons, Status, Display Offset, etc.)
├── DisplayManager_Auth.cpp         # Tela de senha (numeric pad + lockout)
├── DisplayManager_Calibration.cpp  # Tela calibração de touch + sensitivity
├── DisplayManager_Alarm.cpp        # Tela MODE_ALARM_ACTION (silenciar/desativar)
├── DisplayManager_Calendar.cpp     # Calendário do histórico
├── DisplayManager_Touch.cpp        # Roteador de touch + helpers (acceptTouch, hold, slide)
├── DisplayManager_i18n.cpp         # tr(), DICTIONARY_EN, helpers
├── DisplayManager_LangParser.cpp   # Parser de .lng (DICT/LOGCODES/HELP/LICENSE/WEBDICT)
├── DisplayManager_Fonts.cpp/.h     # Carregamento de fontes (subset 24pt + simutFont9pt/12pt)
├── DisplayManager_FmtFloat.h       # fmtFloat1 helper inline
├── FreeSansBold24pt7b_subset.h     # Fonte subset (~22 chars, -8KB)
│
├── WebManager.h
├── WebManager_Core.cpp             # Construtor, begin, rotas, update loop, rate limit
├── WebManager_Auth.cpp             # Login, sessions, RBAC, password change, page handlers
├── WebManager_Api.cpp              # /api/perms, /api/config, /api/status, /api/alarms, /api/lang
├── WebManager_Calib.cpp            # /api/calib GET/POST (v3.34.0 — F-CALIB-UI)
├── WebManager_Files.cpp            # /api/ls, /api/upload, /api/mkdir, /api/delete, /download
├── WebManager_History.cpp          # /api/history_multi, /api/export/{history,logs}.bin
├── WebManager_Commit.cpp           # /api/commit_all (atomic save-all + reboot)
├── WebManager_Send.cpp             # safeSend* (gzip stream, SendGuard com WDT feed)
├── WebManager_Util.cpp             # /lang.js, /style.css, /favicon.ico, helpers JSON
│
├── SensorManager.h/.cpp            # DS18B20 (ROM scan + leitura) + DHT22 (PIO async)
├── StorageManager.h/.cpp           # LittleFS, SystemConfig CRC32 dual-bank, calib.csv, salt
├── NetworkManager.h/.cpp           # WiFi STA/AP, NTP backoff, DNS independente, virtual RTC
├── TelemetryManager.h/.cpp         # HTTP/MQTT upload, batch dinâmico, cursor persistente
├── CommandManager.h/.cpp           # CLI parser + USB/BT output dual
├── BluetoothManager.h/.cpp         # BT Serial com auth + nome customizável (cfg.deviceName)
├── SoundManager.h/.cpp             # BuzzerPIO, 5 classes (Touch/Confirm/Error/Alarm/Attention)
├── LogManager.h/.cpp               # CompactLogRecord 12B, autópsia WDT, sinks USB/BT, ring buf
├── MetricsManager.h/.cpp           # Heap probes, sensorReads, metrics pra /api/status
├── HistoryCodec.h/.cpp             # Codec v2: header SIM2 + delta/zigzag/varint encoding
├── Themes.h/.cpp                   # 50 paletas built-in + scanner /themes/*.thm
├── TftWithOffset.h                 # Wrapper Adafruit_ILI9341 com offset configurável
├── TouchPriority.h                 # Singleton checker (UI input vence operações pesadas)
├── HelpLicenseEN.h                 # Help text inline (PROGMEM, fallback EN)
├── SystemUtils.cpp                 # dallasCrc8, validators, CRC32 incremental
├── WebUI.h                         # Embedded HTML/CSS/JS (PROGMEM, @LANG/@WEBDICT markers)
├── WebUI_GZ.h                      # Auto-gerado pelo build (gzip blobs + STYLE_CSS_GZ)
├── SECURITY.md                     # Threat model + rotação + resposta a incidentes
├── STABILITY_PLAN.md               # Fases F1..F17 + master findings + bonus features
│
├── data/                           # LittleFS image (uploadfs)
│   ├── favicon.ico                 # v3.34.0: migrado de PROGMEM (-11KB flash)
│   ├── lang/                       # language_{pt-BR,es-ES}.lng
│   ├── system/                     # help_{pt,en}.txt, license_{pt,en}.txt (opcional)
│   ├── themes/                     # custom *.thm (opcional, editor offline em tools/)
│   └── history/                    # YYYYMMDD.bin (codec v2 + cursor t_cursor.bin)
│
├── tools/
│   ├── build_webui_gz.py           # PlatformIO pre-build: minify + gzip → WebUI_GZ.h
│   ├── build_lang_pack.py          # Compila language_pt-BR.lng a partir do WebUI.h + sources
│   ├── build_lang_pack_es.py       # PT → ES via dict de tradução (gera language_es-ES.lng)
│   ├── build_favicon_header.py     # (legado) PNG favicon → header PROGMEM (substituído por FS)
│   ├── compressor.py               # (legado) gerador alternativo do WebUI_GZ.h
│   ├── subset_font.py              # Subset de 24pt7b (~22 chars usados, -8KB flash)
│   ├── history_v1_to_v2.py         # Conversor offline: history v1 → v2 (~45% menor)
│   ├── backup.sh                   # Snapshot tarball + git bundle
│   ├── debug_ls_history.sh         # Listagem rápida de /history/ via /api/ls
│   ├── theme-editor/               # Editor offline de .thm (HTML/JS standalone)
│   ├── test-server/                # Mock HTTPS server para testes de telemetria
│   ├── stress_test/                # Toolkit completo: gera history + drain + restore + run
│   ├── favicon-source/             # PNG/ICO source para gerar data/favicon.ico
│   ├── hw_test_lib.sh              # Helpers shell (login, fetch, asserts) para HW tests
│   ├── test_f12_*.sh               # SEC findings (SEC-001/002/003/005)
│   ├── test_f15_*.sh               # SEC-006 (LRU evict)
│   ├── test_f_csv_*.sh             # F-CSV.2..5 export validation
│   ├── test_chunk_*.sh             # Calibração empírica de chunk size do export
│   ├── test_perf.sh                # 5-dimension perf audit
│   ├── test_stress.sh              # Stress test do export (descobriu loop infinito)
│   └── test_web001.sh              # WEB-001 (escape JSON em /api/ls)
│
├── test/                           # EXT-009: host-side unit tests (pio test -e native)
│   ├── native_stubs/               # Arduino.h stub (String + millis()) p/ build host
│   └── test_validators/            # Unity asserts (25/25 PASSED em 0.82s)
│
├── docs/
│   └── GLOSSARY.md                 # Dicionário de tags (BUG/SEC/CON/DOC/F-/Patch/#N)
│
├── audits/
│   └── SIMUT_ANALISE_TECNICA_v1.md # Auditoria técnica externa cooperativa (2026-04-22)
│
├── platformio.ini                  # 3 envs: pico_w_release / pico_w_debug / native
├── LICENSE                         # MIT
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
