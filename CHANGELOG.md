# Changelog

All notable changes to SIMUT firmware.

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
