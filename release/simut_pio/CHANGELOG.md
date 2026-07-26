# Changelog

**English** | [Português](CHANGELOG.pt-BR.md)

All notable changes to SIMUT firmware.

## v1.5.3-beta (2026-07-25)

Stability and telemetry release. Most of it comes from chasing reboots to their
actual cause rather than to the first plausible one — several entries below
record a hypothesis that measurement killed, because those are the ones most
likely to be re-proposed.

### Core-1 lifecycle and reboots (class R1)

- **Core 1 was being hard-reset while healthy** — `getHeartbeat()` guarded on `_isPausedForFlash`, a flag declared, cleared in five places, read there, and **never set true**. Every millisecond of flash lockout read as staleness; past 10 s the watchdog killed a working core, wedging Core 0 and breaking in-flight HTTP responses.
- **Flash writes without a Core-1 pause** — `writeHistoryEntryFlashV4` programmed flash while Core 1 fetched from XIP, hanging the QSPI arbiter. Fixed with a refcounted `Core1FlashPause` RAII guard.
- **The crash autopsy printed a constant** — `scratch[3]` and `scratch[5]` were both destroyed within the first instants of `setup()`, so every reboot classified as a HW watchdog stall in `C0=[BOOT]`. Two sessions were spent reading a forensic channel that returned the same answer whatever had happened. Now snapshotted before anything can overwrite it.
- **The watchdog window was never 15 s** — the RP2040 load register caps at 8.388 s, so every `WdtWindow` asking for more got exactly the default. The class stays, its comment no longer lies, and long operations are sized by *feeding* the watchdog.
- **Core-1 lifecycle visible in `show metrics`** — phase markers, per-phase worst stalls, QSPI latency, lockout accounting.

### Telemetry

- **TLS handshakes could wedge Core 0 forever** — `_wait_for_handshake()` upstream has no overall deadline: `_run_until()` restarts its own timer on every call, so `setTLSConnectTimeout()` bounds one iteration and never the handshake. Against a peer that accepts TCP without completing the handshake — a wrong port was enough — Core 0 spun there permanently. Patched in `tools/arduino_pico_overrides`, which now also feeds the watchdog inside the bounded loop.
- **BearSSL asked for 16 KB contiguous and the heap had 11.3** — `setBufferSizes(4096, 512)` drops the receive buffer to what actually fits. Measured at the moment of the attempt: 31,900 B free, 11,370 B contiguous. Freeing memory does not help when the heap is fragmented; the block is what matters.
- **`TelemetryGuard` removed, not repaired** — it claimed to feed the watchdog during blocking network calls via a 2 s timer. Measured: the timer ticks correctly right up to `http.POST()` and stops the instant it blocks. It never worked in any build. Repairing it would have been worse — a guard that fed through a wedged handshake converts a recoverable reboot into a permanent freeze.
- **Templates rejected `{u..}` and `{p..}`** — `{pAMB}` compared 7 bytes against a 6-character token, so it resolved only at the very end of a template. Per-slot pressure `{p0}`..`{p15}` added, resolving against the slot that actually reports it, so the rewritten key matches the V4 history key for that channel.
- **Live Preview matched the firmware** — the editor knew only single-digit `{t0}`..`{t9}`, so every `{u..}`, `{p..}` and `{t10}`..`{t15}` was echoed literally and a working template looked broken. `/api/config` now exposes per-slot `hum`/`press` so the preview resolves channels the way the firmware does.

### History (V4)

- **Records were written with a timestamp and no data, and reported as success** — a mid-day sensor identity change stops every value from being recorded: the schema lives in the `.sim4` header and values match by `hwId`, while `ensureV4Schema` restores that header from the existing file instead of rebuilding it. `writeHistoryEntryV4` succeeded regardless, so the log kept saying "History record saved" once a minute. An empty row is worse than a gap because it looks like data. Now refused, with `APP_HIST_SCHEMA_MISMATCH` (code 515) warning once.
- **`sensor reschema confirm`** — new privileged command that rebinds the day's history to the slots as currently configured. Destructive: it recreates today's file, so the day's earlier records are lost.
- **Codec fixes** — post-failure refill, transactional two-pass decode, midnight rollover, and a `-0.01 °C` value colliding with the NaN sentinel.
- **Chart streaming ran Core 1 dry** — large ranges decimated tens of thousands of records with no watchdog feed between emissions.

### Memory

- **The language pack held 14 KB of heap for the browser's benefit** — the `.lng` loader mallocs the whole file and never frees it; `@WEBDICT` is half of it and no firmware path reads it. Now excised from the buffer and streamed from LittleFS on demand. Measured 14,052 B recovered against 14,124 B predicted; dashboard RAM went 81% → 70%.
- **`/config` moved to the filesystem** — the app slot had 660 bytes left. Serving the page gzipped from LittleFS took real headroom back to 8,852 B.

### Web and UI

- **Sensors configurable from `/config`** — the dashboard goes back to being status only.
- **`/api/logs` sent unguarded, and two handlers self-deadlocked** on the read lock.
- **Top-panel graph asked for sensor -1**, found nothing, and rebooted the device.
- **Full redraw painted 90% of its pixels twice** — 254 ms → 126 ms.
- **Touch failures now say why** instead of blanking the screen.

### i18n

- **pt-BR pack completed** — every sensor key and 35 log messages were missing.

### Known limitation

The Core-1 heartbeat race under heavy flash load (class R1, `APP_CORE1_DEAD` → soft panic) is **not** closed. It is rare and orthogonal to everything above, and it is the remaining stability gap.

## v1.5.1-beta (2026-07-19)

### AP Mode Fix — Touch Hold at Boot

- **XPT2046 SPI wake-up removed** — The manual SPI transaction (`0x90`) at boot was putting the XPT2046 into power-down mode with PENIRQ disabled (PD0=0). The pipelined data bytes inherited PD0=0, keeping PENIRQ permanently disabled and deadlocking AP-mode-via-touch-hold. The XPT2046 touch-detect circuit is always active from power-up — no SPI initialization is needed. Fixes: AP mode now activates correctly when holding touch at boot.

### Calibration Persistence Fixes

- **Calibration changes now persist through reboot** — `commit_all` reboot path correctly saves calibration data. Previously lost on watchdog-triggered reboot.
- **Skip calib.csv rewrite when `nChanges==0`** — Avoids unnecessary flash writes when no calibration data has changed.
- **Fast calib save for non-ROM sensors** — No quiet mode hang when saving calibration for sensors without ROM identifiers.
- **Calibration hwId/name changes now instant** — Changes take effect in 0.4s instead of requiring a full sensor reload.

### Dashboard & UI Fixes

- **Top-panel slot-0 persistence** — Slot 0 now correctly persists in the top panel after display offset or theme changes.
- **Auto-switch bottom panel** — When the top panel slot changes, the bottom panel now auto-switches to the next available slot.

### Arduino IDE Release Packages

- **`tools/build_release.sh`** — Automated script to generate Arduino IDE-compatible `.zip` releases for both `simut_tft` (ILI9341) and `simut_alpha` (HD44780) variants.
- **Flattened file structure** — All source files at sketch root; `ota/`, `display/`, `sensors/` subdirectory includes rewritten to flat paths.
- **Both variants compile with arduino-cli** — TFT: 911.888 bytes (87%), Alpha: 819.636 bytes (78%) on RP2040 Pico W with 1 MB filesystem.

### OTA Update Files

- **Firmware binaries** — `release/simut_v1.5.1-beta.bin` (OTA update) and `release/simut_v1.5.1-beta.uf2` (USB mass-storage flash).

## v1.5.0-beta (2026-07-19)

### Centralized Hardware Configuration — `simut_config.h`

- **Single config file** — All user-configurable options now live in `src/simut_config.h`: display type, pin assignments, sensor enable/disable, Bluetooth, mDNS, theme packs, buzzer pin, and advanced system limits. Previously scattered across 8+ files.
- **9 documented sections** — Display type, TFT pins, Alpha/HD44780 pins (I2C and parallel), buzzer, sensors, communication, theme packs, 1-Wire default pin, advanced limits. Each option has explanatory comments.
- **`#ifndef` guards throughout** — Every define supports compile-time override via `-D` flags in `platformio.ini`. Defaults match the existing release configuration.
- **Backward compatible** — Existing config headers (`DisplayConfig.h`, `SensorConfig.h`) delegate to `simut_config.h`. All `#include` chains preserved. No breaking changes.
- **Arduino IDE support** — `__has_include("simut_arduino_config.h")` guard at the top of `simut_config.h` for release packages. Release configs simplified to set overrides before including.

### Build System Cleanup

- **`platformio.ini` deduplicated** — Sensor and feature flags removed from `[pico_base]` (now in `simut_config.h`). Only environment-specific overrides remain in `[env:pico_w_alpha]`.
- **Release packages simplified** — `release/*/simut_arduino_config.h` now includes `simut_config.h` instead of duplicating all defines.

### Bug Fixes

- **BluetoothManager.cpp** — Added missing `#if SIMUT_BLUETOOTH` guard around all method implementations. Prevents redefinition errors when `SIMUT_BLUETOOTH=0` and the file is compiled (debug builds).
- **HD44780_16x2.h** — Wrapped `_initLcd()` and its call site in `#if HD44780_MODE_PARALLEL`. The 4-bit parallel init sequence was incorrectly compiled in I2C mode.

### Theme Pack Selection

- **Moved to `simut_config.h`** — Theme packs (`SIMUT_THEMES_HEALTH`, `_PRO`, `_MEDICAL`, `_SAFETY`, `_RETRO`, `_NATURE`, `_UTILITY`) are now enabled by uncommenting lines in the config file, not by editing `Themes.cpp`.
- **`Themes.h` includes `simut_config.h`** — Theme flags are visible wherever `Themes.h` is included.

### PIO Resource Coexistence — Multi-Sensor Conflict Resolution

- **pio0 conflict identified** — OneWirePIO (DS18B20, 27 instruction slots) + WirePIO (BME280 I2C, 32 slots) = 59 > 32 available. WirePIO loaded first, blocking OneWirePIO entirely (DS18B20 dead — no GPIO fallback).
- **pio1 SM saturation** — 2× DHT22 (2 SMs) + CYW43 WiFi (1 SM) + BuzzerPIO (2 SMs) = 5 > 4 SMs. Resolved by BuzzerPIO auto-fallback to pio0.
- **`BME280Driver.h` fix** — Added `forceGPIO(true)` before each `begin()` call. BMx280PIO now uses GPIO bit-bang I2C only (skips PIO+DMA), keeping pio0 instruction slots free for OneWirePIO. GPIO mode is slightly slower but fully reliable.
- **`docs/PIO_ANALYSIS.md`** — Comprehensive PIO resource allocation analysis covering all libraries (OneWirePIO, DHTBus, WirePIO, BuzzerPIO, CYW43), instruction slot budgets per block, state machine counts, DMA channels, conflict scenarios, and resolution mechanisms.

### Hardware Validation — 4-Sensor Coexistence Test

Tested on Pico W with TFT display + buzzer + WiFi:

| Sensor | GPIOs | Type | Status |
|--------|-------|------|--------|
| BMP280 | GP0 (SDA), GP1 (SCL) | BME280 driver | ✅ Reading (GPIO bit-bang) |
| DHT22 #1 | GP2 | DHT22 | ✅ Detected, reading |
| DHT22 #2 | GP3 | DHT22 | ✅ Detected, reading |
| DS18B20 | GP4 | DS18B20 | ✅ Detected (ROM: 283C21…), reading |

- **WiFi**: Connected (RSSI -45 dBm), web server responding
- **PIO after fix**: pio0 31/32 slots (OneWirePIO + BuzzerPIO fallback), pio1 23/32 slots (DHTBus×2 + CYW43)
- **Heap**: 94.3 KB stable, no leaks over 11+ minutes of continuous operation
- **Sensor readings**: 857/916 OK (93.2%), 59 errors concentrated during initial setup
- All 4 sensors configured and activated via CLI, configuration persisted to flash

### Flash Budget

- **Release (TFT + all sensors + mDNS)**: 94.1% (982604 / 1044480 bytes)
- **Alpha (HD44780 parallel + all sensors + mDNS)**: 85.4% (891920 / 1044480 bytes)
- **RAM (release)**: 35.8% (93760 / 262144 bytes)

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

### Other Changes

- **mDNS enabled by default** — `-DSIMUT_MDNS=1` in platformio.ini. Device accessible via `http://simut.local`. Cost: ~15KB flash, negligible RAM.
- **I2C0/I2C1 auto-detection** — `i2cPeripheralForPins()` selects the correct peripheral at runtime. Any GPIO 0-15 pair works for I2C sensors (hardware permitting).
- **`checkAndAutoHealSensors()`** — No longer reports false "Sensor missing" warnings for non-DS18B20 sensor types (DHT22, BME280).
- **BME280 boot guard** — I2C timeout (50ms) + ACK probe prevents boot hang when BME280 is configured but not physically connected.
- **Hardcoded GPIO assumptions removed** — DHT22 `begin()` no longer references GPIO 10. DS18B20 legacy methods use first active sensor's pin. Zero fixed GPIO-to-type coupling.

### Flash Budget

- **Release (DS18B20 + DHT22 + mDNS)**: 93.1% (972KB / 1044KB) — ~72KB free
- **With BME280**: 93.7% (979KB / 1044KB) — ~65KB free
- **RAM**: 35.7% (~93.7KB / 262KB)

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

- **Third community contribution** 🎉 — Complete Spanish documentation suite by [@f-p-0](https://github.com/f-p-0): README.md (337 lines, PR #66), CONTRIBUTING.md (140 lines, PR #68), and CODE_OF_CONDUCT.md (39 lines, PR #68), making SIMUT accessible to Spanish-speaking users worldwide
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
