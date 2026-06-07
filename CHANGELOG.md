# Changelog

All notable changes to SIMUT firmware.

## v1.4.4-beta (2026-06-07)

### GPIO Resource Management — Guided Slot Assembly

- **`gpio` command** — GPIO resource map showing all 16 pins with allocation status (FREE or `[Slot XX] Type (Role)`), plus a consolidated free-GPIO list. GPIOs are now a visible, trackable limited resource.
- **`sensor <slot> create <type>`** — Guided slot creation. Sets the driver type, clears previous pin assignments, activates the slot, and shows: pin count, each pin's role and flags (e.g., `1-Wire (pull-up)`), available free GPIOs, and a hint for the next command (`sensor <slot> pin <idx>,<gpio>`).
- **`sensor <slot> type <type>`** — Now shows pin requirements and current GPIO assignments per pin after changing the type, so the user knows what to wire.
- **`sensor <slot> pin <idx>,<gpio>`** — Now shows the role label for context (e.g., `pin[0]=GPIO 3 (1-Wire)`). Detects when all required pins are assigned and suggests the next step (`sensor <slot> name "<name>"`).
- **`sensor <slot> active on`** — Validates prerequisites before activating: type must be set, driver must be compiled in, and all declared pins must be assigned. Reports exactly which pins are missing.
- **`show sensor types`** — Lists compiled-in sensor drivers with pin count, channel summary, and role labels (e.g., `BME280 | 2 pins | Temp+Hum+Press | SDA,SCL`).

### BME280 Driver — Temperature + Humidity + Pressure

- **`BME280Driver.h`** (~9KB flash) — Self-contained I2C driver using forced-mode measurements. No external library dependency (avoids Adafruit_BME280 at ~15KB).
- **Async state machine** — BME_IDLE → trigger forced measurement → BME_WAITING → read results, matching the DS18B20/DHT22 async pattern.
- **Compensation formulas** — Integer math per Bosch BME280 datasheet §4.2.3 for temperature, humidity, and pressure. Oversampling ×1 on all channels (~9ms per reading).
- **TFT panel rendering** — Temperature + humidity on dashboard (mirrors DHT22 layout), min/max panel support. Pressure available via API (`CH_PRESS` channel).
- **I2C auto-detect** — Probes 0x76 and 0x77 addresses. Hardware scan detects BME280 on the active I2C bus.
- **Multi-pin GPIO init** — `gpioInitForRole()` now called for ALL declared pins (not just `pins[0]`). I2C bus initialized once when the first I2C sensor is found. `ROLE_POWER` defaults to output LOW.

### Improved Diagnostics

- **`show sensors`** — Redesigned output: slot, GPIO assignments, driver type, channels (e.g., `T+H+P`), friendly name, ROM (1-Wire), HWID, alarm status, and alarm limits per channel.
- **`show sensor types`** — Available drivers with pin count, channel summary, and pin role labels.
- **`PIN_ONEWIRE_DEFAULT`** — Fixed preprocessor redefinition warning (8 instances eliminated).
- **All 4 sensor channels initialized** — `MAX_SENSOR_CHANNELS` loop sets `avgValue` to NAN and `calibrationOffset` to 0.

### Flash Budget

- **Without BME280**: 91.6% (957KB / 1044KB)
- **With BME280**: 92.3% (964KB / 1044KB) — ~7KB for BME280 driver
- **RAM**: 35.7-35.8% (~93.5KB / 262KB)

## v1.4.3-beta (2026-06-07)

### Flash Diet — 86KB Freed (97.8% → 91.2%)

- **LEAmDNS disabled by default** — Wrapped with `#ifdef SIMUT_MDNS`. Enable with `-DSIMUT_MDNS` in build_flags when needed. Saves ~196KB library from link.
- **BluetoothManager stub** — When `SIMUT_BLUETOOTH=0` (default), entire class is inline no-ops. `BluetoothManager.cpp` excluded from build. `SerialBT` library still compiled by framework but unused symbols are linker-stripped.
- **`sensor pin <slot> <index> <gpio>` CLI** — Assign specific GPIOs to sensor slots with conflict detection across all active sensors. Validates GPIO range (0-15) and pin index (< MAX_SENSOR_PINS).
- **Flash budget**: 91.2% (952KB / 1044KB) — 92KB free for future features.

## v1.4.2-beta (2026-06-07)

### Sensor Entity Architecture — Driver-based Pin Roles

- **PinRole enum** — Each GPIO pin now has a declared role (`ROLE_DATA`, `ROLE_I2C_SDA`, `ROLE_I2C_SCL`, `ROLE_SPI_MOSI`, `ROLE_SPI_MISO`, `ROLE_SPI_SCK`, `ROLE_SPI_CS`, `ROLE_UART_TX`, `ROLE_UART_RX`, `ROLE_ANALOG`, `ROLE_POWER`).
- **PinRequirement in SensorFormat** — Each driver declares pin count, role, label, and flags (pull-up, open-drain) via `SensorFormat::forType()`. No hardcoded per-type GPIO setup.
- **`gpioInitForRole()`** — Auto-configures GPIO direction, pulls, and function based on declared role. Replaces `#if SIMUT_SENSOR_DHT22` hardcoded init blocks.
- **API pin metadata** — `/api/status` now returns `pc` (pin count) and `pr` (role labels: "Data", "SDA,SCL") per sensor.
- **WebUI pin info** — Dashboard table shows pin count + roles next to sensor type (e.g., `DHT22 ⚡1p Data`, `BME280 ⚡2p SDA,SCL`).
- **Adding a new sensor** now requires only a driver file + `SensorFormat::forType()` entry — display, API, calibration, and GPIO init all follow the format metadata automatically.

## v1.4.1-beta (2026-06-07)

### Universal Slot Architecture — 16 GPIO Slots

- **16 universal sensor slots** — `MAX_SENSORS` expanded from 10 to 16, covering GPIO0–GPIO15. All slots are now uniform with configurable type, hwId, friendlyName, pins, and alarm limits.
- **Ambient sensor eliminated** — The special `ambientSensor` field in `SystemConfig` has been removed. Slot 10 (GPIO10) is now a regular universal slot, treated identically to all others. The `idx: -1` API convention is replaced by standard slot index `10`.
- **Sensor channels generalization** — `RuntimeSensor` now uses `avgValue[4]`, `buffers[4]`, and `calibrationOffset[4]` arrays with `SensorChannel` enum (CH_TEMP, CH_HUM, CH_PRESS, CH_LUX). Each sensor driver declares its channels via `SensorFormat::forType()`. Adding a new sensor type (e.g. BMP280 pressure) requires only a driver — display, web API, and calibration adapt automatically.
- **Web dashboard sensor type column** — Table now shows driver type (DHT22/DS18B20) per sensor. Calibration form conditionally shows humidity fields per-sensor based on `hasHum` flag.
- **Unified alarm system** — Per-slot alarm mask now covers all 16 slots. The separate `ambTempAlarm`/`ambHumAlarm` flags are removed.
- **Config migration v16→v17** — Automatic migration: `ambientSensor` moved to `sensors[10]`, slots 11–15 initialized as inactive.

### Fixes

- **Boot hang after flash** — Eliminated blocking `Serial` calls in boot path (`BLOG`, `LogManager`, `CommandManager`, `SoundManager`). Removed `Serial.ignoreFlowControl(true)` that caused 1s delays per log line.
- **Stack overflow prevention** — `SystemConfig` allocations moved to heap (`tempConfig`, `encBuf`) to avoid RP2040 4KB stack limit with the larger v17 struct.
- **Bluetooth disabled** — `SerialBT.begin()` hardfaults on CYW43 after warm boot (picotool reset). Bluetooth is now disabled to ensure reliable boot. USB Serial + Web interface provide equivalent functionality.
- **API JSON fixes** — Restored missing `first = false` and `if (!safeSend(buf))` calls in `/api/sensors`, `/api/status`, and `/api/users` that caused invalid JSON (missing commas between objects).
- **WebUI calibration** — Removed duplicate ambient card. All sensors rendered uniformly with type-aware fields.

### Breaking Changes

- **Config format v17** — `SystemConfig` layout changed. v16 configs are auto-migrated on first boot. Downgrade to ≤v1.3.x requires factory reset.
- **API `/api/sensors`** — Ambient sensor no longer reported as `idx: -1`. Slot 10 appears in the standard sensor array.
- **History format** — `BinaryHistoryRecord` changed from 28 to 40 bytes. Existing `.bin` files are incompatible.
- **Bluetooth removed** — `SerialBT` disabled due to CYW43 warm-boot hardfault. Use USB Serial or Web interface instead.
- **`/api/status` sensor format** — Added `type` and `ch` fields. Humidity field now uses generic `sensorHasChannel()` instead of hardcoded `TYPE_DHT22` check.

## v1.3.0-beta (2026-06-07)

### Alpha Display — HD44780 16×2 Alphanumeric Support

- **HD44780 dual-mode driver** — I2C (PCF8574 backpack) and 4-bit parallel GPIO, selectable via `HD44780_MODE_I2C` / `HD44780_MODE_PARALLEL` build flags
- **Compile-time display selection** — `SIMUT_DISPLAY_TFT` and `SIMUT_DISPLAY_ALPHA` flags allow building for ILI9341 TFT (default) or HD44780 16×2 (alpha), mutually exclusive
- **I2C mode** — Uses I2C1 on GPIO 26 (SDA) / GPIO 27 (SCL), address 0x27 (configurable via `HD44780_I2C_ADDR`). Zero sensor slot conflicts — all 10× DS18B20 + DHT22 available
- **Parallel 4-bit mode** — RS=GPIO 16, EN=GPIO 17, D4=GPIO 18, D5=GPIO 19, D6=GPIO 20, D7=GPIO 21. Also zero sensor slot conflicts
- **GPIO 0-15 reserved for sensors** — Display pins mapped to GPIO 16+ exclusively, no sensor displacement
- **Alpha display loop on Core 1** — Character framebuffer with blit(), auto-cycling temperature/humidity display
- **GFX library exclusion** — Adafruit GFX Library, ILI9341, and XPT2046 excluded from alpha build via `lib_ignore`. SPI init and touch detection guarded with `#if SIMUT_DISPLAY_TFT`
- **UART1 clock preserved** — `uart_init()` called in alpha mode (clock only, no GPIO takeover) to keep StorageManager debug markers safe
- **WiFi skip timeout** — Alpha builds without touch skip button get a 30-second WiFi connection timeout to prevent infinite boot hang
- **`pico_w_alpha` build environment** — Clean build at 89.0% flash (929 KB), 34.6% RAM (90 KB). Saves ~84 KB vs release build

### Fixes

- **Touch calibration infinite loop** — Guarded with `#if SIMUT_DISPLAY_TFT`; alpha build has no touch controller
- **SPI pin conflict on alpha parallel** — `SPI.begin()` was configuring GPIO 16-19 before HD44780 init, causing boot failure in parallel mode
- **Flash storage corruption recovery** — Full `picotool erase` resolves corrupted filesystem partition after repeated flashing

### Documentation

- **WIRING.md** — Complete rewrite with three pinout diagrams (ILI9341 TFT, HD44780 I2C, HD44780 Parallel), comparison table, HD44780 pin reference, and wiring checklists for each mode

## v1.2.1-beta (2026-06-06)

### Dual Independent Dash Panels

- **Unified panel architecture** — Both dash panels use the same `drawSlotPanel()` function. The dedicated ambient panel (`drawAmbientPanel`) eliminated (~280 lines saved).
- **Top panel: fixed/interactive modes** — Long-press (1s) toggles between fixed (pinned sensor, normal styling) and interactive mode (dark gray background + white elements, follows slot selector to choose which sensor to pin).
- **Bottom panel: always interactive** — Short tap toggles min/max only. Always follows the bottom SLOT buttons.
- **S10 button** — Added slot 10 (ambient DHT22 on GPIO 10) to the bottom button bar. Hidden when top panel is fixed on it.
- **Min/max rendering moved to drivers** — `DS18B20_renderMinMax()` and `DHT22_renderMinMax()` in respective drivers, dispatched via `sensorRenderMinMax()`. Shared primitives in `SensorDrawing.h` reuse existing icons.
- **Slot humidity min/max tracking** — Per-slot humidity arrays with real-time accumulation every loop cycle.
- **Independent top panel data** — `topSlot*` fields in `SystemState` with dedicated `setTopSlotData()`/`setTopSlotMinMax()` setters.
- **Instant panel updates** — Incremental render now compares `topSlot*` fields. `pullSnapshot()` keeps `topSlotIdx` synced for AppManager mirroring.
- **Alarm flash fix** — Top panel alarm flash checks `isSlotAlarming(topSlotIdx)` instead of old ambient flags.
- **Border color fix** — Normal mode content strip uses `borderColor` instead of hardcoded `C_TEXT_SUB`.
- **Background fill fix** — Content strip uses `panelBg` instead of `C_BG_MAIN` for correct alarm red and selection mode gray.

### Community & Docs

- **Third community contribution** 🎉 — Complete Spanish translation of README.md (337 lines) by [@f-p-0](https://github.com/f-p-0), making SIMUT accessible to Spanish-speaking users worldwide
- **Second community contribution** 🎉 — Docker development environment so contributors can build and test without installing PlatformIO locally ([@JohnMartin0301](https://github.com/JohnMartin0301))
- **First community contribution** 🎉 — 672-line HistoryCodec v2 test suite covering roundtrip encoding, anchor frame boundaries, NaN compression, and buffer overflow ([@LorenzoLongaretto](https://github.com/LorenzoLongaretto))

### Flash Budget

| Configuration | Flash |
|---|---|
| Both sensors ON | 1030872 (98.7%) |

## v1.2.0-beta (2026-06-06)

### OTA Subsystem — Full Upgrade to v4.6.2

- **F-OTA-BOOTLOOP fixed** — Loop20 OTA 100% PASS. Root cause: reentrant LittleFS deadlock during README.md write + Core 1 startup deferred to post-WiFi + safeReboot uses MMIO identical to applier_reboot.
- **F-RESTORE** — Reliable backup/restore via API (98/100 PASS). Config snapshot preserved across OTA apply with CRC32 integrity. Atomic rewrite of calib.csv with VERSION=epoch.
- **F-RAM-SLIM** — RAM usage 49.6% → 33.7% (-41 KB / -16pp). Eliminated graph caches, removed unused font glyphs, shared buffer pools.
- **F-TEL-HTTPS-RESILIENT** — Fix crash + reboot when HTTPS server drops. More conservative heap budget for TLS connections.
- **F-OTA-STAGE-NOBLOCK + F-FLASH-DIET** — Fix TCP drop during OTA firmware staging. Non-blocking upload with adaptive chunk sizing.
- **F-DISPLAY-MARGINS** — `fillMarginsBlack` + `fillScreen` override in `TftWithOffset` for clean display edges.
- **F-BOOT-CYW43-CYCLE** — Power-cycle `WL_REG_ON` always in `setup()` for reliable WiFi initialization.
- **F-SCREENSHOT-INTEGRITY** — Eliminate row loss/corruption in `/api/screenshot` via multi-sample readRow with majority vote.
- **F-OTA-ADMIN-ONLY** — OTA endpoints require `PERM_FULL_ADMIN`.
- **F-TEL-ADAPTIVE** — Adaptive-throughput telemetry (backend-only batch sizing).
- **F-UI-OTA-FLOW** — User-facing OTA + restore UX messages with progress feedback.

### Documentation & Tooling

- **Glossary** — `docs/GLOSSARY.md` decoding all inline tags (F-\*, BUG-\*, SEC-\*, CON-\*, DOC-\*, REF-\*) used in source comments.
- **Comment cleaner** — `tools/cleanup_comments.py` strips version history references and changelog markers from source comments for release preparation.

### Flash Budget

| Configuration | Flash |
|---|---|
| Both sensors ON | 1031464 (98.8%) |
| DS18B20 only | ~1028400 (98.5%) |
| DHT22 only | ~1029500 (98.6%) |
| Both OFF | ~1024900 (98.1%) |

### Tests

49/49 tests passing (27 validators + 22 HistoryCodec).

## v1.1.0-beta (2026-06-06)

### Sensor Architecture — Modular Driver System

- **Compile-time sensor feature flags** — `SIMUT_SENSOR_DS18B20`, `SIMUT_SENSOR_DHT22`, `SIMUT_SENSOR_BME280` in `platformio.ini` allow disabling unused drivers to reclaim flash (DS18B20: -2.7 KB, DHT22: -1.6 KB, both: -6.1 KB)
- **Universal slot configuration** — `SensorRecord` v16 with explicit `sensorType` field + multi-pin support (`pins[4]`), ready for I2C, SPI, ADC, and UART sensors
- **Sensor drivers organized** — `src/sensors/` directory with `DS18B20Driver.h`, `DHT22Driver.h`, `SensorConfig.h`, `SensorHelpers.h`
- **Flash migration v15→v16** — Automatic schema upgrade preserving all sensor configs, ROM-based type detection during migration
- **SensorPresets catalog** — 130+ predefined display formats in `sensors/SensorPresets.h` covering 30+ physical quantities (temperature, humidity, pressure, weight, light, chemistry, electrical, flow, etc.)
- **SensorFormat system** — `SensorValueFormat` (unit, decimals, icon) + `SensorFormat` (1-3 values per sensor) + factory `forType()` in `sensors/SensorHelpers.h`

### Display — Driver-Owned Panel Rendering

- **Icon drawing in drivers** — `sensors/SensorDrawing.h` with procedural icons (thermometer, drop, gauge, bulb, ruler, vial, bolt, pulse, pipe, compass, flag, atom, battery, etc.) guarded by compile flags
- **Driver-based panel rendering** — `DHT22_renderPanel()` and `DS18B20_renderPanel()` handle full panel layout (icons, formatting, units) via `sensorRenderPanel()` dispatch
- **Slot panel now shows humidity** — DHT22 in any slot displays both temperature and humidity with drop icon and translated suffix (%RH/%UR)
- **Theme-aware colors** — Drivers receive `C_TEXT_SUB`, `C_TEMP_OK`, `C_TEMP_HOT`, `C_HUMIDITY` from active theme; icons follow theme changes
- **Exact original positioning** — `textAnchor=92`, `iconX=14`, `rightMargin=15` matched from original `drawAmbientPanel`
- **Generic value formatter** — `formatSensorValue()` in `DisplayManager_FmtFloat.h` handles NaN and variable decimal places

### Bug Fixes

- **AP Mode via touch at boot** — XPT2046 receives SPI wake-up command during early boot; PENIRQ pin read directly via `gpio_get()`. AP window always opens regardless of settle state.
- **Mandatory touch calibration on first boot** — Full sensitivity + 4-point position calibration runs before dashboard when `magic != 0xCA`. Cancel during boot applies safe defaults.
- **`sensor define` command** — Extended syntax accepts sensor type: `sensor define <gpio> <rom> <type> <hwId> <name>`. Legacy 4-token syntax auto-detects from ROM.
- **`sensor accept` command** — Sets `sensorType` explicitly on accepted DS18B20 sensors.

### Flash Budget

| Configuration | Flash |
|---|---|
| Both sensors ON | 1031464 (98.8%) |
| DS18B20 only | ~1028400 (98.5%) |
| DHT22 only | ~1029500 (98.6%) |
| Both OFF | ~1024900 (98.1%) |

### Tests

49/49 tests passing (27 validators + 22 HistoryCodec).

## v1.0.0 (2026-06-03)

### Initial Public Release

- **Multi-sensor support** — Up to 10 DS18B20 (1-Wire) + 1 DHT22 ambient sensor
- **Zero-trust sensor pipeline** — ROM verification, hardware mismatch detection, error hysteresis
- **320×240 ILI9341 TFT display** — Dashboard, real-time graphs, touch-driven settings (XPT2046)
- **50 built-in themes** + custom theme support via LittleFS
- **Embedded web server** — Multi-user sessions, RBAC (10 permission bits), file manager
- **gzip-compressed WebUI** — Minified inline pages with shared CSS/JS
- **Telemetry** — HTTP POST and MQTT with JSON/CSV/custom templates, TLS/SSL
- **Dual-channel CLI** — USB Serial + Bluetooth (BLE)
- **NTP time sync** — Exponential backoff, multi-server fallback, virtual RTC
- **History codec v2** — Delta + sensor-mask + anchor encoding, ~45% size reduction
- **Hardened authentication** — HMAC-SHA256, per-user random salt, 5000 rounds
- **OTA firmware updates** — Upload via web UI, config snapshot preservation, auto-reboot
- **Backup & restore** — Full LittleFS backup/restore with CRC32 integrity (BKP1 format)
- **Crash forensics** — Watchdog scratch register autopsy with cross-core health monitoring
- **Internationalization** — English + Portuguese/Spanish via external language packs
