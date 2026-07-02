# Tag Glossary — SIMUT

**English** | [Português](GLOSSARY.pt-BR.md) | [Español](GLOSSARY.es-ES.md)

> Dictionary of inline tags used in source code comments. Consult this file before interpreting any tag such as `F-*`, `BUG-*`, `SEC-*`, `Patch X`, `Phase N`, `#N` found in the sources.

## How to use

Each tag appears in comments in the format:

```cpp
/* F-LOCKOUT-STUCK: short explanation of what this block solves. */
/* Patch C: signed cast to tolerate cross-core race. */
```

This glossary decodes the meaning of each tag. If you find a tag not listed here, it may be new — add it following the pattern below.

---

## Feature Tags (F-*)

| Tag | Description |
|-----|-----------|
| `F-LOCKOUT-STUCK` | Cross-core refactor: Core 0 couldn't acquire `multicore_lockout` while Core 1 was on a heavy path. Solution: cooperative quiet mode with Core 1 hard-reset (`multicore_reset_core1`) + re-launch. Touch re-init on each launch, TFT begin only-on-first. |
| `F-I18N-TRIM.1` | Reduction from 8 to 2 languages on the TFT display (EN + PT) to save flash. Firmware since v3.22.0. |
| `F-I18N-TRIM.2` | Removal of 6 dead languages (es/de/fr/it/ru/zh) from `WebUI.h`, consistent with F-I18N-TRIM.1. |
| `F-NET-TIME.1` | `NetworkTimeData` overlay in `reserved[28..47]` — network/time configuration data. |
| `F-NET-TIME.2` | Consumer in `NetworkManager` — DNS/NTP flags read from overlay. |
| `F-NET-TIME.3a` | Back-end web: GET/POST `/api/set_time` + `setManualTime()`. |
| `F-NET-TIME.3b` | Front-end: `/network` page with separate DNS + `/config` with date/time + PT i18n. |
| `F-NET-TIME.4` | CLI: `conf ntp`, `conf time`, `conf net dns` commands + tokenizer 5→6 slots. |
| `F-NET-TIME.5a` | Future-cursor auto-reset + `t_int=0` hint in the UI. |
| `F-NET-TIME.5b` | Closure — cumulative regression along the F-NET-TIME path. |
| `F-IP-FIX` | Fix for `WiFi.config` signature: arduino-pico uses `(ip, dns, gateway, subnet)`, different from ESP32. |
| `F-BT-LOGIN` | Deferred flash on Bluetooth login: `LOG_CODE` buffered in RAM during `_btMgr.update()`, eliminating Core 1 lockout + WDT reset risk. |

## Bug Tags (BUG-*)

| Tag | Description |
|-----|-----------|
| `BUG-002` | Cross-core publish order: producer must write **data** before the **flag**, with `__dmb()` between them. Consumer reads flag + `__dmb()` + data. |
| `BUG-003` | Granular chunking of `enterFlashSafeMode`/`exitFlashSafeMode`. Before: 1 lockout covered everything. Now: 1 lockout per LittleFS operation, Core 1 renders between chunks. Macro `FLASH_OP(...)` in `StorageManager.cpp`. |
| `BUG-004` | Mutex `_stateMutex` may fail `mutex_try_enter`; use `_lastWebBusy` as sticky fallback — avoids flicker of the "web busy" overlay. |
| `BUG-005` | `captureBootSnapshot()` public + explicit call in `LogManager::begin()`. `setModule` no longer captures opportunistically. Guard `_autopsyPerformed` in `performCrashAutopsy()` prevents false autopsy on subsequent `begin()` calls (e.g., `clear log`). |

## Security Tags (SEC-*)

| Tag | Description |
|-----|-----------|
| `SEC-001` | Path traversal in upload: `isSafeUploadFilename()` blocks `..`, `\`, `:`, `<`, `>`, `|`, `?`, `*`, `%`, control chars, len>64. |
| `SEC-002` | Bypass of `replace("..","")` with `....` or `%2e%2e`: rejection by `indexOf("..")>=0` + `indexOf('%')>=0`. |
| `SEC-003` | Random 8-char admin password `[A-Z2-9]` via `rp2040.hwrand32()` on factory reset. Plaintext in RAM (`_initialAdminPassword[9]`), never in flash. |
| `SEC-004` | PIN `1234` forces change: `FLAG_MUST_CHANGE_PIN` flag in `reserved[26..27]`, `SetupFlagsData` overlay with magic `0xBE`. |
| `SEC-005` | CLI line limit: `CLI_LINE_MAX=256`, helper `appendCharWithLimit()`, anti-spam warning. |
| `SEC-006` | LRU eviction of `_loginStates[16]` preserves slots with active non-expired lockout. |
| `SEC-007` | Password hash 120→128 bits (32 hex chars). Transparent migration: login detects old hash (30 chars), validates with truncate, silent re-hash. |
| `SEC-008` | `PASSWORD_HMAC_ROUNDS` — number of HMAC-SHA256 rounds for password hashing. |
| `SEC-009` | Random per-user salt (`UserAccount.salt[8]`) via `hwrand32()`. Bump `CONFIG_VERSION` with migration routine. |

## Consistency Tags (CON-*)

| Tag | Description |
|-----|-----------|
| `CON-001` | Authoritative `SCRATCH REGISTER MAP` block in `LogManager.cpp` — single map of watchdog scratch registers. |
| `CON-002` | `LanguageCode` enum with `LANG_EN`, `LANG_PT`, `LANG_COUNT` sentinel + `static_assert` against `LANG_NAMES`. |
| `CON-003` | Headers "8 languages" → "2 languages (EN + PT)" in `DisplayManager.{h,cpp}`. |
| `CON-004` | `_lastSavedCrc` as private member of `StorageManager` (was `static` local). Skip save when CRC32 identical. |
| `CON-005a` | `LoginState.nonce` as `char[65]` instead of `String`. |
| `CON-005b` | `CliDemand.strVal1/strVal2` as `char[64]` with helpers `setStrVal1/setStrVal2`. |
| `CON-006` | `DS18B20_CONVERSION_TIME_MS`, `DHT22_READ_TIMEOUT_MS` moved from local macros to `SystemDefs.h`. |

## Documentation Tags (DOC-*)

| Tag | Description |
|-----|-----------|
| `DOC-002` | Named timing constants: `BOOT_WAIT_DOT_INTERVAL_MS`, `ALARM_ROTATE_INTERVAL_MS`, `ALARM_FLASH_INTERVAL_MS`, `WEB_NOTIFY_DURATION_MS`. |
| `DOC-003` | `SECURITY.md` at the root with threat model, defenses, operations, and incident response. |

## Refactoring Tags (REF-*)

| Tag | Description |
|-----|-----------|
| `REF-004` | `TouchPriority` singleton with `setProvider`/`isActive` — replaces 3 setters + 3 members + 3 duplicated lambdas across 5 managers. |
| `REF-007` | Decomposition of `handleApiLogin` (~130 lines) into 6 helpers: `findLoginStateForIp`, `checkLockout`, `validateNonce`, `verifyPasswordFor`, `allocSessionSlot`, `completeLogin`. |

## Patches (Patch X)

| Tag | Description |
|-----|-----------|
| `Patch A` | Autopsy: stores real `elapsed` (time since last heartbeat) in `scratch[7]` instead of `now - moduleStartTime`. |
| `Patch B` | (Reserved — not implemented) |
| `Patch C` | Signed cast `(int32_t)(now - lastBeat)` in `checkCrossCoreHealth` and `AppManager:446`. Tolerates cross-core race where `lastBeat` is slightly ahead → underflow ≈ UINT32_MAX was causing false-positive soft panic. |

## Phases (Phase N)

| Tag | Description |
|-----|-----------|
| `Phase 1` | Graph: remove old points that left the view window. |
| `Phase 2` | Graph: read new records from the binary history file. |
| `Phase 3` | Graph: recalculate statistics (ignoring NaNs). |
| `Phase 4` | CLI deferral during touch: USB/BT commands enqueued in a 2-slot ring buffer while touch active. Drain 1-per-loop at the top of `update()`. |
| `Phase 5` | Post-touch coordinated flush: on touch-active→touch-free transition, triggers serial flush of logs → hist record → cursor. Closes the "data in RAM not in flash" window from minutes to <100ms. |

## Numeric Tickets (#N)

| Tag | Description |
|-----|-----------|
| `#4` | Mandatory `confirm` suffix on CLI for sensitive commands. |
| `#5` | Obfuscation of sensitive fields in logs and console. |
| `#7` | CLI ↔ Web parity: commands available on both channels. |
| `#8` | Heap high-water mark: tracking of historical minimum free heap. |
| `#11` | `intVal1Valid` flag in `CliDemand` to distinguish "not provided" from "value 0". |

## Other

| Tag | Description |
|-----|-----------|
| `U3` | Configurable web port (not hardcoded 80). |
| `U11` | Suppressed failure heartbeat: 1 log/hour after telemetry log suppression. |
| `U14` | Watchdog false-positive: underflow in unsigned cross-core subtract. Fixed with signed cast (Patch C). |
| `U15` | WDT feeds between LittleFS operations in `writeCompactToFlash`/`flushPendingLogs`. |
| `U16` | Web save bursts: 30s watchdog, CRC skip, 1s rate-limit, client-side dirty tracker. |
| `U17` | Toast notification in web UI: shared infra across 9 pages, PT i18n. |
| `U18` | CLI deferral during touch (Phase 4): 2-slot ring buffer, post-touch drain. |
| `U19` | Post-touch flush (Phase 5): logs, hist record, cursor — closes vulnerability window. |
| `U23` | Autopsy instrumentation: `MOD_SAVE_CONFIG`, `MOD_LOG_FLASH`, `MOD_HIST_FLASH`, `MOD_CORE1_LOCK` + `TraceScope` RAII. |
| `U24` | Batch commit-all: web interface accumulates changes in `sessionStorage`, single "Save and Reboot" button. |
| `U25` | Deferred flash on Bluetooth login: `setForceBuffer(true/false)` wrapping `_btMgr.update()`. |
| `WEB-001` | JSON escape in `/api/ls`: filename and dirname escaped for control bytes (0x00-0x1F/0x7F). |
| `N9` | TLS cert >16 KB rejected at boot to prevent OOM. |
| `F12.1–F12.5` | Security hardening (see SEC-001 through SEC-005). |
| `F13.1–F13.4` | Latent bugs (see BUG-002 through BUG-005). |
| `F15.1–F15.2.a` | Hash migration: LRU eviction (SEC-006), schema bump v14→v15 (SEC-009). |

---

## How to add new tags

1. Choose a prefix consistent with the category: `F-` for features, `BUG-` for bugs, `SEC-` for security, `CON-` for consistency, `DOC-` for documentation, `REF-` for refactoring, `EXT-` for external findings.
2. Add the tag to this file with one line describing what it means.
3. Use the tag in inline comments in the format: `/* TAG: short description. */`
4. Keep this glossary as the authoritative source — if a tag becomes obsolete, mark it with `[OBSOLETE]` instead of removing it.

> **Note:** `EXT-*` tags refer to external audit findings. They are converted to the appropriate prefix (`F-`, `SEC-`, etc.) when implemented.
