/**
 * @file AppManager_Boot.cpp
 * @brief Boot sequence: filesystem, sensors, network, web server initialization.
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "AppManager.h"
#include "CommandManager.h"
#include "DisplayManager.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "SoundManager.h"
#include "StorageManager.h"
#include "SystemDefs.h"
#include "metadata.h"
#include "config_snapshot.h"
#include "backup.h"      /* crc32_update — verificação da imagem pós-apply */
#include "ota_layout.h"
#include "TelemetryManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include "WebManager.h"
#include <LittleFS.h>
#include <hardware/uart.h>
#include <hardware/gpio.h>
#include <SPI.h>
#include <time.h>

extern AppManager app;

/* BOOT_LOG fan-out to both Serial USB CDC and UART1 raw.
 * File-scope static helpers allow inlining at the callsite to save flash.
 * UART1 uses arduino-pico Serial2 = GP8/GP9.
 * Alpha display (parallel mode) uses GPIO 8/9 for HD44780 D6/D5 — UART1 disabled. */
#if SIMUT_DISPLAY_TFT
/* ── TFT build: UART1 active on GPIO 8/9 ─────────────────────────── */
static inline void _uart_init( ) {
 uart_init(uart1, 115200);
 gpio_set_function(8, GPIO_FUNC_UART); /* TX (UART1) */
 gpio_set_function(9, GPIO_FUNC_UART); /* RX (UART1) */
}
static inline void _uart_mark(char c) { uart_putc_raw(uart1, c); }
static void _bu_str(const char* s) {
 while (*s) uart_putc_raw(uart1, *s++);
}
static void _bu_u32(uint32_t v) {
 char buf[11]; int n = 0;
 if (!v) { uart_putc_raw(uart1, '0'); return; }
 while (v && n < 10) { buf[n++] = (char)('0' + (v % 10)); v /= 10; }
 while (n--) uart_putc_raw(uart1, buf[n]);
}
/* UART1-only boot log: USB Serial is intentionally disabled during boot
 * to prevent boot hangs when no USB host is reading (warm boot after flash).
 * Use hardware UART1 (GPIO 8 TX) with a 3.3V FTDI adapter to read debug logs. */
#define BLOG(s)   _bu_str(s)
#define BLOG_U(v) _bu_u32((uint32_t)(v))
#define BLOG_NL() uart_putc_raw(uart1, '\n')
#else
/* ── Alpha build: UART1 clock enabled (StorageManager needs it for
 * uart_putc_raw debug markers), but GPIO 8/9 kept free for HD44780 ── */
static inline void _uart_init( ) {
 uart_init(uart1, 115200); /* clock only — no GPIO takeover */
}
static inline void _uart_mark(char c) { uart_putc_raw(uart1, c); }
#define BLOG(s)    do { } while(0)
#define BLOG_U(v)  do { } while(0)
#define BLOG_NL( ) do { } while(0)
#endif // SIMUT_DISPLAY_TFT

/* scratch[5] magic — the orchestrator sets this before applier_reboot
 * to signal "next boot is post-OTA-apply, power-cycle CYW43".
 * scratch[5] survives watchdog_reboot. setup() clears it immediately
 * upon detection to avoid re-cycling on the next boot. */
#define POST_OTA_APPLY_MAGIC 0xC72BAB07u
#define WD_BASE_ADDR 0x40058000u
#define WD_SCRATCH5_OFFSET 0x20u

static inline uint32_t alpha30_read_scratch5( ) {
 return *(volatile uint32_t*)(WD_BASE_ADDR + WD_SCRATCH5_OFFSET);
}
static inline void alpha30_write_scratch5(uint32_t v) {
 *(volatile uint32_t*)(WD_BASE_ADDR + WD_SCRATCH5_OFFSET) = v;
}

void AppManager::setup( ) {
 /* Initialize UART1 first for early diagnostics from setup() entry.
	 * Boot markers: '@' (entry), '$' (post-uart-init), '%' (pre-Serial)
	 * allow tracing where a post-watchdog-reboot boot hangs (UART1 does
	 * not depend on USB CDC or PLL_USB, only hardware UART). */
 _uart_init();
 _uart_mark('@');

 /* FIRST STATEMENT WITH ANY SIDE EFFECT — do not move anything above it.
  *
  * The two forensic channels of this firmware live in watchdog scratch
  * registers and both are destroyed within microseconds of this point:
  * scratch[5] by the unconditional clear in the CYW43 block just below, and
  * scratch[3] by the TraceScope inside startCore1( ) (whose destructor restores
  * module 0 = "BOOT" out of freshly zeroed RAM) plus Core 1's own TRACE_MOD.
  * Reading them any later than here yields this boot's own state — which is
  * exactly what the rc15 autopsy did, reporting a constant
  * "C0=[BOOT] C1=[DISPLAY] sc3=0x80088000" for every reboot class alike. */
 LogManager::instance( ).captureBootSnapshot( );

 /* Always power-cycle CYW43 during setup().
	 *
	 * Previously: only power-cycled if scratch[5] == POST_OTA_APPLY_MAGIC
	 * (set by the orchestrator before applier_reboot). That covered the
	 * OTA boot-loop path but not reboot via picotool load -x — the SDK
	 * reset preserves scratch without setting the magic, leaving CYW43
	 * in an indeterminate state after BOOTSEL-via-PicoHand.
	 *
	 * Solution: always cycle. Adds ~600 ms to boot (~18 -> ~19 s) but
	 * eliminates the bug for all reboot paths (UF2 flash, OTA apply,
	 * watchdog, hand RESET, picotool reboot). We clear scratch[5] to
	 * preserve compatibility with POST_OTA_APPLY_MAGIC diagnostics. */
 {
 alpha30_write_scratch5(0); /* always clear; magic is no longer a gate */
 _uart_mark('#'); /* power-cycle entry marker */
 gpio_init(23);
 gpio_set_dir(23, GPIO_OUT);
 gpio_put(23, 0);
 busy_wait_ms(500);
 gpio_set_dir(23, GPIO_IN);
 gpio_disable_pulls(23);
 busy_wait_ms(100);
 _uart_mark('*'); /* power-cycle done */
 }
 _uart_mark('$'); /* pre Serial.begin */

 Serial.begin(115200);










 _uart_mark('%'); /* post Serial.begin */

 delay(1000);
 _uart_mark('&'); /* post delay(1000) */

 /* Log the firmware version BEFORE any init that could hang — ensures
	 * the user always knows which firmware is running, even if boot
	 * stalls immediately afterward. BOOT_LOGF writes to both USB CDC and
	 * UART1 (snprintf-based), allowing exact localization of where a
	 * post-apply boot hangs without needing to reflash. */
 BLOG("\n==============================================\n");
 BLOG(" SIMUT firmware "); BLOG(SIMUT_VERSION); BLOG_NL( );
 BLOG("==============================================\n");
 /* Check if last reboot was WDT-triggered */
 if (watchdog_caused_reboot()) {
  BLOG("[BOOT] *** Last reboot: WATCHDOG TIMEOUT ***\n");
 Serial.println("[BOOT] WATCHDOG_REBOOT detected");
 }
 BLOG("[BOOT step] 1: pos-banner @ "); BLOG_U(millis( )); BLOG_NL( );

 /*
	 * Do NOT call TRACE_MOD here — scratch[4] must retain the previous
	 * crash module until the autopsy reads it (in LogManager::begin below).
	 * TRACE_BEAT(0) is safe: it only touches RAM (_coreHeartbeat), not scratch.
	 */
 TRACE_BEAT(0);

 BLOG("[BOOT step] 2: _displayMgr->begin( ) @ "); BLOG_U(millis( )); BLOG_NL( );
 _displayMgr->begin( );
 /* startCore1 deferred until AFTER _storageMgr->begin().
	 * Without Core 1 active, flash_safe_execute uses the single-core
	 * path (local disable_interrupts only), avoiding the multicore_lockout
	 * IRQ that often hangs on post-OTA boot. Trade-off: the TFT does not
	 * show boot status messages until later (~10-15s). */
 _uart_mark('D'); /* startCore1 deferred */

 /* Wait for Core 1 to be READY before proceeding.
	 * _displayMgr->startCore1() returns after multicore_launch_core1,
	 * but Core 1 still needs to run loopCore1::multicore_lockout_victim_init()
	 * to set _core1Ready. If Core 0 calls enterFlashSafeMode before that
	 * setpoint, the multicore_lockout timeout hits 10s + recovery.
	 *
	 * Wait with a safety timeout (1500ms): victim_init takes a few
	 * microseconds normally. If not ready in 1.5s, Core 1 is defunct —
	 * boot continues without it (display stays off, firmware does not hang). */
 LOG_CODE(LOG_INFO, "APP", APP_DISPLAY_LAUNCHED, 0, TRL("Display UI Launched on Core 1."));

 delay(BOOT_STEP_DELAY_MS);

#if SIMUT_DISPLAY_TFT
 /* Configure XPT2046 + SPI bus pins for reliable PENIRQ detection.
  * PENIRQ (GPIO 20): input with pull-up, LOW = touched.
  * TOUCH_CS (GPIO 17): HIGH = deselected, required for XPT2046 to scan.
  * TFT_CS (GPIO 28): HIGH = deselected, prevents SPI bus contention.
  * SPI SCK (GPIO 18), MOSI (GPIO 19): output LOW, stable idle state.
  * Without stable SPI lines, the XPT2046 may enter an undefined state
  * where PENIRQ does not respond to touch. */
 gpio_init(20); gpio_set_dir(20, GPIO_IN); gpio_pull_up(20);
 gpio_init(17); gpio_set_dir(17, GPIO_OUT); gpio_put(17, 1);
 gpio_init(28); gpio_set_dir(28, GPIO_OUT); gpio_put(28, 1);
 gpio_init(18); gpio_set_dir(18, GPIO_OUT); gpio_put(18, 0);
 gpio_init(19); gpio_set_dir(19, GPIO_OUT); gpio_put(19, 0);
 gpio_init(16); gpio_set_dir(16, GPIO_IN);

 /* XPT2046 touch-detect circuit is always active from power-up.
  * No SPI wake-up needed — PENIRQ (GPIO 20) asserts LOW on touch.
  * Reading gpio_get(20) directly is sufficient for AP-mode detection.
  *
  * SPI bus is left uninitialized here; Core 1 will set it up later. */
#endif // SIMUT_DISPLAY_TFT

 bool forceAP = false;
 _displayMgr->setBootStatusKey(TR_BOOT_HOLD_AP);

 /* Touch settle + sanity gate: some XPT2046 controllers report
	 * touched()=true permanently right after boot (controller in
	 * indeterminate state before first Z-axis sample, or electrical
	 * noise on PENIRQ). Without this gate, the boot detects that
	 * stale-true as an AP-hold gesture and enters AP mode on every
	 * reboot.
	 *
	 * Strategy: wait for 200ms of consecutive quiet (up to 1500ms cap).
	 * - Quiet seen -> touch functional -> AP detection window normal.
	 * - Quiet NOT seen -> touch stuck-true (HW/calibration bug) ->
	 *   bypass the AP detection window.
	 * AP-by-touch is unavailable while stuck; fix is recalibrating
	 * touch (via CLI or Settings) or a clean power cycle. */
 bool touch_settled __attribute__((unused)) = false;
 {
 unsigned long settle_start = millis( );
 unsigned long quiet_since = 0;
 while (millis( ) - settle_start < 1500) {
 TRACE_BEAT(0);
 if (_displayMgr->isScreenTouched( )) {
 quiet_since = 0;
 } else {
 if (quiet_since == 0) quiet_since = millis( );
 if (millis( ) - quiet_since >= 200) { touch_settled = true; break; }
 }
 delay(20);
 }

 }

 {
 unsigned long waitStart = millis( );
 while (millis( ) - waitStart < AP_DETECT_WINDOW_MS) {
 TRACE_BEAT(0);
 if (_displayMgr->isScreenTouched( )) {

 unsigned long holdStart = millis( );
 bool held = true;
 int missedTouches = 0;

 while (millis( ) - holdStart < AP_HOLD_DURATION_MS) {
 TRACE_BEAT(0);
 if (!_displayMgr->isScreenTouched( )) {
 missedTouches++;
 if (missedTouches > AP_HOLD_MAX_MISSED) {
 held = false;

 _displayMgr->setApProgress(-1);
 _displayMgr->setBootStatusKey(TR_BOOT_AP_CANCELLED, nullptr, false);
 delay(800);
 break;
 }
 } else {
 missedTouches = 0;
 }
 int pct = map(millis( ) - holdStart, 0, AP_HOLD_DURATION_MS, 0, 100);
 _displayMgr->setApProgress(pct);
 delay(50);
 }
 if (held) forceAP = true;
 break;
 }
 delay(50);
 }
 }
 

 _displayMgr->setApProgress(-1);

 _storageMgr->setLockCallback([](bool lock) {
 app.pauseDisplayForFlash(lock);
 });

 LogManager::instance( ).setLockCallback([](bool lock) {
 app.pauseDisplayForFlash(lock);
 });

 /* Wire cooperative quiet mode for saveConfiguration.
	 * Core 0 signals, Core 1 freezes in a RAM-only loop, Core 0 does
	 * flash operations without cascading lockout IRQ stuck. Returns
	 * true only if Core 1 ACKed. */
 _storageMgr->setBigSaveQuietCallback([](bool enable) -> bool {
 return app.requestDisplayQuietMode(enable);
 });

 BLOG("[BOOT step] 5: pre _storageMgr->begin( ) @ "); BLOG_U(millis( )); BLOG_NL( );
 _displayMgr->setBootStatusKey(TR_BOOT_MOUNT_FS);
 bool fsOk = _storageMgr->begin( );
 BLOG("[BOOT step] 6: pos _storageMgr->begin( ) fsOk="); BLOG_U(fsOk ? 1 : 0);
 BLOG(" @ "); BLOG_U(millis( )); BLOG_NL( );

 /* Now it is safe to start Core 1 — mountFS, mkdirs, snapshot
	 * restore, and loadConfiguration have completed with Core 1
	 * INACTIVE, i.e. without IRQ-based multicore_lockout hangs. */
 _uart_mark('C'); /* deferred startCore1 */
 _displayMgr->startCore1( );
 {
 unsigned long wait_start = millis( );
 while (!_displayMgr->isCore1Ready( ) && millis( ) - wait_start < 1500) {
 tight_loop_contents( );
 }
 _uart_mark(_displayMgr->isCore1Ready( ) ? 'R' : 'X');
 }
 Serial.print("[TCH] c="); Serial.println(gpio_get(20));


 /* DisplayManager needs the config pointer to render the dashboard
	 * (buildDashLayout filters inactive slots). Set once at boot — the
	 * config lives in BSS (member of StorageManager) and is never relocated. */
 _displayMgr->setSysConfig(&_storageMgr->getConfig( ));

 _displayMgr->setBootStatusKey(TR_BOOT_START_LOG);
 LogManager::instance( ).begin(fsOk, LOG_DEBUG);

 /* First thing the logger can usefully say. 2.0.0 accepts one config schema
  * and migrates nothing, so a device coming from 1.6.x wakes up on defaults —
  * no WiFi, no users, no sensor slots. That has to be stated somewhere, and
  * StorageManager detected it several steps ago, before this logger existed. */
 {
 size_t rejected = _storageMgr->takeRejectedConfigSize( );
 if (rejected != 0) {
 LOG_CODE(LOG_WARN, "STO", SYS_STORAGE_MIGRATED, (int)rejected,
          TRL("Config from an older schema discarded — defaults loaded"));
 }
 }

 /* Autopsy has already read scratch[4]. Now we can set MOD_BOOT to
	 * track stalls that happen during the remainder of setup. */
 TRACE_MOD(0, MOD_BOOT);

 /* OTA: post-apply detection.
	 *
	 * If metadata.state == APPLYING or POST_BOOT, this boot occurred
	 * right after the orchestrator triggered watchdog_reboot.
	 * We log via Serial + LOG_CODE for traceability — this line should
	 * only appear on post-apply boot; any other occasion is a bug.
	 *
	 * ota_metadata_clear does flash_range_erase — requires Core 1 paused
	 * (it is already running at this point via _displayMgr->startCore1).
	 * Wrap with enterFlashSafeMode/exit. */
 {
 ota::UpdateMetadata m;
 if (ota::ota_metadata_read(m) &&
 (m.state == ota::STATE_APPLYING || m.state == ota::STATE_POST_BOOT)) {
 const bool snap_present = ota::ota_snapshot_present( );
 BLOG("[BOOT] OTA post-apply detected: state="); BLOG_U(m.state);
 BLOG(" attempts="); BLOG_U(m.attempts);
 BLOG(" snapshot="); BLOG(snap_present ? "present" : "absent"); BLOG_NL( );
 LOG_CODE(LOG_WARN, "OTA", SEC_CONFIG_CHANGED, 0,
 String("post-apply boot, state=") + (int)m.state +
 " attempts=" + (int)m.attempts +
 (snap_present ? " snap=ok" : " snap=absent"));

 /* Verify the image the applier actually wrote.
	 *
	 * The applier computes this CRC too, but it runs from SRAM with
	 * interrupts off and has no way to report anything — it discarded the
	 * result and rebooted. That blind spot is why three separate applier
	 * bugs survived several releases: every layer reported success and
	 * nothing ever compared the app slot against what was staged.
	 *
	 * Here we are in ordinary code with logging up, and the metadata is
	 * still on flash (it is cleared a few lines below), so the expected
	 * CRC is available. Only runs on a post-apply boot.
	 *
	 * The expected CRC covers the bytes that were uploaded; the applier
	 * copies whole 256 B pages, so the app slot ends with up to 255 bytes
	 * of 0xFF padding that the CRC does not include. The exact boundary is
	 * in uncompressed_size, but only for images staged by a firmware that
	 * records it — an update staged by an older build reports the padded
	 * length in both size fields, and there is no way to recover the real
	 * one from the metadata.
	 *
	 * So we accept any length in the final page: one pass to the start of
	 * the window, then a byte at a time, finalising as we go. That covers
	 * both metadata layouts with a single code path. It also means 256
	 * chances to match instead of one, which on CRC32 is a false accept
	 * around 6e-8 — far below the odds of the flash write itself being
	 * wrong in a way CRC32 misses at all. */
 if (m.compressed_size > 0 && m.compressed_size <= OTA_APP_MAX_SIZE) {
 const uint8_t* img = (const uint8_t*)(XIP_BASE + OTA_APP_OFFSET);
 const uint32_t total = m.compressed_size;
 const uint32_t lo = (total > 256u) ? (total - 256u) : 0u;
 uint32_t crc = 0xFFFFFFFFu;
 for (uint32_t off = 0; off < lo; off += 4096u) {
 const uint32_t n = (lo - off) < 4096u ? (lo - off) : 4096u;
 crc = crc32_update(crc, img + off, n);
 }
 uint32_t len = lo;
 bool crc_ok = ((crc ^ 0xFFFFFFFFu) == m.uncompressed_crc32);
 while (!crc_ok && len < total) {
 crc = crc32_update(crc, img + len, 1);
 len++;
 crc_ok = ((crc ^ 0xFFFFFFFFu) == m.uncompressed_crc32);
 }
 BLOG("[BOOT] OTA image CRC "); BLOG(crc_ok ? "ok" : "MISMATCH"); BLOG_NL( );
 LOG_CODE(crc_ok ? LOG_INFO : LOG_ERROR, "OTA", SEC_CONFIG_CHANGED, 0,
 crc_ok ? String("image verified, ") + (unsigned)len + " B"
 : String("image CRC mismatch over ") + (unsigned)total +
 " B, want " + String(m.uncompressed_crc32, HEX));
 }

 /* Snapshot was already consumed by StorageManager::begin (restore
	 * before loadConfiguration). Clear the metadata partition now —
	 * erases UpdateMetadata + snapshot region together (factory state). */
 _storageMgr->enterFlashSafeMode( );
 ota::ota_metadata_clear( );
 _storageMgr->exitFlashSafeMode( );
 }
 }

 BLOG("[BOOT step] 7: pos OTA detect @ "); BLOG_U(millis( )); BLOG_NL( );

 /* Collect a /calib.tmp stranded by a reset between its write and the rename.
  * Has to happen before loadAndCalibrateSensors( ) further down so the
  * recovered offsets are the ones the sensors come up with. */
 if (fsOk) _storageMgr->recoverCalibrationTmp( );

 LogManager::instance( ).setHeavyTaskChecker([]( ) -> bool {
 return app._storageMgr->isHeavyTaskLocked( );
 });

 /* Single touch-priority provider shared by Log, Storage, and Web.
	 * Set here before any manager queries TouchPriority::isActive()
	 * during boot. */
 TouchPriority::setProvider([]( ) -> bool {
 return app.isUserInteracting( );
 });

 BLOG("[BOOT step] 8: pre _cmdMgr->begin @ "); BLOG_U(millis( )); BLOG_NL( );
 _displayMgr->setBootStatusKey(TR_BOOT_START_CMD);

 /* Bluetooth visible name now comes from cfg.deviceName
	 * (configurable via /config) instead of the default "PicoW Serial"
	 * from the SerialBT library. Changes via web require a reboot. */
 _cmdMgr->begin(_storageMgr->getConfig( ).deviceName);
 

 _cmdMgr->setBtValidator([this](String attempt) -> bool {
 SystemConfig &cfg = _storageMgr->getConfig( );
 if (!cfg.users[0].active) return false;
 /* Frontend sends SHA256(plaintext) before hashPassword;
	 * sha256Hex mirrors that behaviour (UTF-8 -> Latin-1). */
 String preHash = _storageMgr->sha256Hex(attempt);
 String storedHash = String(cfg.users[0].password);
 /* Supports both legacy (30 chars) and v1 (32 chars) hashes. */
 if (cfg.users[0].hashVersion == 0 && storedHash.length( ) == 30) {
 String legacyHash = _storageMgr->hashPasswordLegacy(
 String(cfg.users[0].username), preHash);
 return (legacyHash == storedHash);
 } else {
 String hashed = _storageMgr->hashPasswordV1(
 String(cfg.users[0].username), preHash, cfg.users[0].salt);
 return (hashed == storedHash);
 }
 });

 if (!fsOk) LOG_CODE(LOG_ERROR, "APP", APP_STORAGE_CRITICAL, 0, TRL("Storage Critical Failure!"));
 

 /* If the device booted in factory defaults (config missing or corrupt
	 * in both banks), display the random initial password on Serial —
	 * requires physical USB access. Also logged via LOG_CODE for audit
	 * trail. Plaintext is never persisted to flash. */
 if (_storageMgr->isFactoryDefaults( )) {
 const char* pw = _storageMgr->getInitialAdminPassword( );
 if (pw && pw[0] != '\0') {
 /* OTP via UART bridge removed — use Serial USB CDC to
	 * capture the one-time password. */
 Serial.println(F("\n=============================================="));
 Serial.println(F(" SEC-003: FACTORY DEFAULTS ATIVADO"));
 Serial.print (F(" Senha ADMIN inicial: "));
 Serial.println(pw);
 Serial.println(F(" Trocar no primeiro login (forcado)."));
 Serial.println(F("=============================================="));
 LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
 TRL("Factory defaults active; initial admin pass on USB/serial."));
 } else {
 /* Rare case: factory defaults detected but plaintext is not
	 * in RAM (loadConfiguration cleared after fallback). Warn
	 * without leaking credentials. */
 LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, 0,
 TRL("Factory defaults active; password regen required."));
 }
 }

 
 uint32_t lastTs = _storageMgr->getLastRecordedTimestamp( );
 
 _netMgr->setProvisionalTime(lastTs);
 _netMgr->setTimeSyncCallback([](uint32_t bootTs, int32_t delta) {

 app._timeSyncBootTs = bootTs;
 app._timeSyncDelta = delta;
 __dmb( ); /* Memory barrier: ensures bootTs/delta are visible before the flag */
 app._pendingTimeSync = true;
 });

 
 SystemConfig &cfg = _storageMgr->getConfig( );
 _displayMgr->setBootStatusKey(TR_BOOT_LOAD_THEME_LANG);
 
 scanCustomThemes( );
 
 loadTheme(cfg.themeIndex);
 
 _displayMgr->refreshTheme( );
 
 DisplayManager::findAndLoadLangFile( );
 
 _displayMgr->setLanguage(cfg.displayLang);
 

 _soundMgr->begin( );
 
 {
 const SoundConfigData* sndCfg = reinterpret_cast<const SoundConfigData*>(
 cfg.reserved + sizeof(TouchCalData));
 _soundMgr->loadConfig(sndCfg);
 }
 

 /* Display positioning offset — applied before any subsequent screen
	 * so that boot status messages already reflect the saved alignment. */
 {
 const DisplayOffsetData* ofs = reinterpret_cast<const DisplayOffsetData*>(
 cfg.reserved + sizeof(TouchCalData) + sizeof(SoundConfigData));
 _displayMgr->loadDisplayOffset(ofs);
 }

 /* CLI mode (debug/config). Default = CONFIG (debug OFF) when magic is invalid. */
 {
 const CliConfigData* cli = reinterpret_cast<const CliConfigData*>(
 cfg.reserved + CLI_CONFIG_OFFSET);
 bool debugOn = (cli->magic == CLI_CONFIG_MAGIC) && (cli->debugMode != 0);
 LogManager::instance( ).setConsoleStream(debugOn);
 _cmdMgr->setDebugMode(debugOn);
 }

 /* CLI language reuses cfg.displayLang (single source of truth).
	 * Also propagated to LogManager for translateCode labels. */
 _cmdMgr->setCliLang(cfg.displayLang);
 LogManager::instance( ).setLanguage(cfg.displayLang);

#if SIMUT_DISPLAY_TFT
 
 {
 const TouchCalData* cal = reinterpret_cast<const TouchCalData*>(cfg.reserved);
 _displayMgr->loadTouchCalibration(cal);
 
 if (!_displayMgr->isTouchCalibrated( )) {
 /* Auto-calibrate with default values for headless testing.
  *
  * RAM ONLY. This used to call saveConfiguration( ) here, and that is a
  * flash write in the worst place in the whole boot. startCore1( ) is
  * deliberately deferred until after _storageMgr->begin( ) so that boot-time
  * flash work runs on the single-core path — read the comment there, it says
  * the multicore_lockout IRQ "often hangs on post-OTA boot". This block runs
  * ~190 lines LATER, with Core 1 already rendering, so the write took exactly
  * that path.
  *
  * And it only ran when the calibration was invalid — which is precisely what
  * adjusting the display offset does on purpose (AppManager_Events.cpp clears
  * cal->magic so the user re-maps touch against the shifted image). So
  * "change the offset, restart" armed a flash write in that window on the
  * very next boot. Core 1 owns the TFT: lose it and the panel stops being
  * driven, which reads as a white screen.
  *
  * Dropping the save costs nothing. These are compile-time constants, so
  * recomputing them each boot is free, and the next saveConfiguration( ) for
  * any other reason carries them along. Persisting them was arguably wrong
  * anyway: it made a placeholder indistinguishable from a calibration the
  * user actually performed. */
 TouchCalData calOut;
 memset(&calOut, 0, sizeof(calOut));
 calOut.magic = 0xCA;
 calOut.flags = 0;
 calOut.xMin = 300; calOut.xMax = 3800;
 calOut.yMin = 200; calOut.yMax = 3700;
 calOut.zThreshold = 400;
 memcpy(cfg.reserved, &calOut, sizeof(TouchCalData));
 _displayMgr->loadTouchCalibration(&calOut);
 LOG_CODE(LOG_WARN, "APP", APP_TOUCH_CAL_REQUIRED, 0,
  TRL("Touch calibration auto-set (headless, RAM only)"));
 }
 }
#endif // SIMUT_DISPLAY_TFT
 

 _displayMgr->setBootStatusKey(TR_BOOT_LOAD_PERIPH);

 if (Serial) { Serial.println("[DBG] sensor begin..."); Serial.flush(); }
 _sensorMgr->begin( );

 if (Serial) { Serial.println("[DBG] loadAndCalibrateSensors..."); Serial.flush(); }
 loadAndCalibrateSensors( );
#if SIMUT_SENSOR_DS18B20
 if (Serial) { Serial.println("[DBG] setDs18Resolution..."); Serial.flush(); }
 _sensorMgr->setDs18Resolution((DS18B20PIO::Resolution)cfg.ds18Resolution);
#endif

 BLOG("[BOOT step] 9: pre _netMgr (forceAP="); BLOG_U(forceAP ? 1 : 0);
 BLOG(") @ "); BLOG_U(millis( )); BLOG_NL( );
 if (Serial) { Serial.println("[DBG] network begin..."); Serial.flush(); }
 if (forceAP) {
 LOG_CODE(LOG_WARN, "APP", APP_AP_MODE_TRIGGERED, 0, TRL("User triggered AP mode."));
 _displayMgr->setBootStatusKey(TR_BOOT_START_AP);
 _displayMgr->setBootStatusKey(TR_BOOT_AP_NETWORK);
 _displayMgr->setBootStatusKey(TR_BOOT_AP_IP);
 _netMgr->beginAP(cfg.deviceName);
 for (int i = 0; i < 35; i++) { delay(100); feedWdt( ); }
 } else {
 _displayMgr->setBootStatusKey(TR_BOOT_START_WIFI);
 BLOG("[BOOT step] 10: pre _netMgr->begin( ) @ "); BLOG_U(millis( )); BLOG_NL( );
 watchdog_update();
 _netMgr->begin(cfg,
 _storageMgr->isDnsAuto( ),
 _storageMgr->isNtpEnabled( ),
 _storageMgr->getSecondaryDns( ));
 BLOG("[BOOT step] 11: pos _netMgr->begin( ) @ "); BLOG_U(millis( )); BLOG_NL( );

 unsigned long netWait = millis( );
 unsigned long lastMsg = 0;
 bool skipped = false;

 int dotCount = 0;
 int waitState = 0;

 while (!_netMgr->isConnected( ) || !_netMgr->isTimeSynced( )) {
 TRACE_BEAT(0);
 watchdog_update( );
 _netMgr->update( );

 if (_displayMgr->isSkipPressed( )) {
 _displayMgr->setBootStatusKey(TR_BOOT_WIFI_SKIPPED);
 skipped = true;
 delay(1000);
 break;
 }
#if !SIMUT_DISPLAY_TFT
 /* Alpha build: no touch skip button, timeout after 30s without WiFi */
 if (millis( ) - netWait > 30000 && !_netMgr->isConnected( )) {
 skipped = true;
 break;
 }
#endif

 if (timeSince(lastMsg, BOOT_WAIT_DOT_INTERVAL_MS)) {
 dotCount++;
 if (dotCount > 4) dotCount = 0;
 String dots = "";
 for (int i = 0; i < dotCount; i++) dots += ".";

 if (!_netMgr->isConnected( )) {
 if (waitState != 1) {
 waitState = 1; dotCount = 0;
 _displayMgr->setBootStatusKey(TR_BOOT_WAITING_ROUTER, nullptr, true);
 } else {
 _displayMgr->replaceBootStatusKey(TR_BOOT_WAITING_ROUTER, dots.c_str( ), true);
 }
 } else if (!_netMgr->isTimeSynced( )) {
 if (waitState != 2) {
 waitState = 2; dotCount = 0;
 _displayMgr->setBootStatusKey(TR_BOOT_SYNC_NTP, nullptr, true);
 } else {
 _displayMgr->replaceBootStatusKey(TR_BOOT_SYNC_NTP, dots.c_str( ), true);
 }
 }
 lastMsg = millis( );
 }

 if (timeSince(netWait, 30000)) {
 _displayMgr->setBootStatusKey(TR_BOOT_NET_TIMEOUT);
 delay(1000);
 break;
 }
 delay(50);
 }

 if (!skipped && _netMgr->isConnected( )) {
 _displayMgr->setBootStatusKey(TR_BOOT_NET_CONNECTED);
 delay(500);
 }
 }

	/* Bluetooth: only start when WiFi connected successfully.
	 * After a failed WiFi connection, the CYW43 chip is in a bad state
	 * and SerialBT.begin() causes a hardfault. Power-cycling the chip
	 * before BT also kills any live WiFi connection. Safest approach:
	 * only enable BT when we know the chip is in a clean state (post-WiFi-success). */
	/* Install console sink regardless of Bluetooth state so CLI output
	 * always reaches USB Serial. Without this, offline boots have no
	 * serial output because emitLine skips Serial when buffer is full. */
	LogManager::instance().setConsoleSink([this](const char* line) {
	 _cmdMgr->consolePrintln(String(line));
	});

	/* Bluetooth DISABLED: SerialBT.begin() hardfaults on CYW43 after
	 * warm boot (picotool reset), regardless of WiFi state. The chip
	 * can't reliably run both WiFi and BT without a full power-cycle
	 * (cold boot). USB Serial + Web interface replace BT functionality.
	 * To re-enable: uncomment the block below. */
	// if (_netMgr->isConnected()) {
	//  _cmdMgr->beginBluetooth(_storageMgr->getConfig().deviceName);
	// }

 _displayMgr->setBootStatusKey(TR_BOOT_START_TEL);
 _telemetryMgr->begin(_storageMgr.get( ), _netMgr.get( ));

 LogManager::instance( ).setEpochSource([]( ) -> time_t { return time(nullptr); });

 BLOG("[BOOT step] 12: pre _webMgr->begin( ) @ "); BLOG_U(millis( )); BLOG_NL( );
 _displayMgr->setBootStatusKey(TR_BOOT_START_WEB);
 _webMgr->begin(_storageMgr.get( ), _sensorMgr.get( ), _netMgr.get( ), _displayMgr.get( ), _telemetryMgr.get( ), _soundMgr.get( ));
 BLOG("[BOOT step] 13: pos _webMgr->begin( ) @ "); BLOG_U(millis( )); BLOG_NL( );
 /* marker pre-callbacks */

 _displayMgr->setBootStatusKey(TR_BOOT_REG_CALLBACKS);
 /* pos setBootStatusKey */
 _webMgr->setYieldCallback([this]( ) { this->core0Yield( ); });
 _webMgr->setLightYieldCallback([this]( ) {
 feedWdt( );

 /* Not while a response is streaming. update( ) reads real sensors and
  * updateLiveDisplay( ) can fire the panel-save debounce — a quiet-mode
  * kill+relaunch of Core 1 in the middle of the stream. The sensor
  * values change every second, so the debounce arms itself; every long
  * download rolled these dice once per 3 s of transfer. The next loop
  * tick catches the work up. */
 if (_webMgr->isStreamingNow( )) return;

 static uint32_t lastLiveUpdate = 0;
 uint32_t now = millis( );
 if (now - lastLiveUpdate > 3000) {
 lastLiveUpdate = now;
 _sensorMgr->update( );
 updateLiveDisplay( );
 }
 });

 /* TouchPriority uses the singleton provider set above. */

 /* ── V5 history: purge the legacy format, then adopt the .wip ─────────
  * §11: the firmware has no reader for anything but V5, so /history is
  * swept of everything else exactly once — the .sim4 files this update
  * replaces, and any junk. Preserving old data is the user's job BEFORE
  * updating (export the .bin/.sim4, convert on the host with
  * tools/history_v5.py); nothing is reimported to the device.
  *
  * Then §7.2: a .wip left by a power cut is the last ten minutes of the
  * block that was open. It is validated as an ordinary chunk and appended
  * to its own day's file, or discarded — never repaired. */
 {
  const uint16_t purged = _storageMgr->purgeNonV5History( );
  if (purged) {
   _displayMgr->setBootStatusKey(TR_BOOT_LOAD_MINMAX);
   LOG_CODE(LOG_WARN, "STO", STO_LEGACY_PURGED, (int)purged, "boot_purge");
  }
  _storageMgr->recoverWipV5( );
  _storageMgr->ensureH5Schema( );
 }

 /* pre forceAP branch */
 if (forceAP) {
 _isApMode = true;
 _displayMgr->setBootStatusKey(TR_BOOT_AP_ACTIVE, nullptr, false);
 LOG_CODE(LOG_INFO, "APP", APP_READY_AP, 0, TRL("System ready (AP mode)."));
 } else {
 /* pre preloadMinMax */
 _displayMgr->setBootStatusKey(TR_BOOT_LOAD_MINMAX);
 delay(80);
 preloadMinMax( );
 /* pos preloadMinMax */

 _displayMgr->setBootStatusKey(TR_BOOT_WARMUP);
 {
 unsigned long warmStart = millis( );

 while (millis( ) - warmStart < 2000) {
 feedWdt( );
 _sensorMgr->update( );

 if (timeSince(warmStart, 900)) break;

 delay(10);
 }

 updateLiveDisplay( );
 refreshSelectedSlot( );
 }

 if (_pendingTimeSync) {
 _displayMgr->setBootStatusKey(TR_BOOT_CORRECT_TS);
 delay(80);
 handleTimeSync(_timeSyncBootTs, _timeSyncDelta);

 /* Reload min/max with corrected timestamps */
 _displayMgr->setBootStatusKey(TR_BOOT_RELOAD_MINMAX);
 delay(80);
 for (int i = 0; i < MINMAX_SLOT_COUNT; i++) {
 _cachedMin[i] = 1000.0f; _cachedMax[i] = -1000.0f;
 _preloadMin[i] = 1000.0f; _preloadMax[i] = -1000.0f;
 _cachedHumMin[i] = 1000.0f; _cachedHumMax[i] = -1000.0f;
 _preloadHumMin[i] = 1000.0f; _preloadHumMax[i] = -1000.0f;
 }
 preloadMinMax( );
 }

 /* pre warmup-end + prep-dash */
 _displayMgr->setBootStatusKey(TR_BOOT_PREP_DASH);
 
 _sensorMgr->update( );
 
 updateLiveDisplay( );
 
 refreshSelectedSlot( );
 

 _displayMgr->setBootStatusKey(TR_BOOT_ALL_INIT);
 _displayMgr->setBootStatusKey(TR_BOOT_SYS_READY);
 delay(800);
 LOG_CODE(LOG_INFO, "APP", APP_READY, 0, TRL("System ready."));

 /* Prime network status so alpha LCD can show IP immediately
    without waiting for the main loop to call updateSystemStatus. */
 {
  SystemStatusData sd;
  memset(&sd, 0, sizeof(sd));
  sd.wifiConnected = _netMgr->isConnected( );
  sd.rssi          = _netMgr->getRssi( );
  char ip[16]; _netMgr->getIpAddress(ip, sizeof(ip));
  safeCopy(sd.ip, ip, sizeof(sd.ip));
  _displayMgr->updateSystemStatus(sd);
 }

 _displayMgr->endBoot( );
 _bootCompletedAt = millis( );
 BLOG("[BOOT step] 14: SYS READY @ "); BLOG_U(millis( )); BLOG_NL( );

 _soundMgr->play(SND_CONFIRM);
 }

 /*
	 * Enable cross-core health monitoring AFTER boot completes.
	 * Force-refresh heartbeats from both cores to avoid false
	 * detection of a stalled heartbeat during boot.
	 * The 5-second grace period starts counting from here.
	 */
 LogManager::instance( ).enableHealthCheck( );

 TRACE_MOD(0, MOD_IDLE);
 _cmdMgr->printPrompt( );
}

/* =========================================================================== */
/* MAIN LOOP — PRIORITY-BASED TASK SCHEDULING */
/* =========================================================================== */
/**
 * @brief Main application loop with cross-core health monitoring.
 *
 * Execution order (every cycle):
 * 1. Cross-core health check + pause watchdog
 * 2. CLI input processing
 * 3. Network keepalive (always runs)
 * 4. Web server request handling
 * 5. Telemetry upload (deferred during menu/touch/heavy tasks)
 * 6. Sensor auto-heal check (every 3s)
 * 7. NTP timestamp correction (if pending)
 * 8. History CSV logging (every 60s)
 * 9. UI event dispatch + sound processing
 */
