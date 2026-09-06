/**
 * @file AppManager_Air.cpp
 * @brief SIMUT Air — headless hibernation cycle (M0 operational / M1 dormant).
 *
 * M0 (cold boot): the full Alpha-like headless stack runs (web + serial + BT
 * config, sensors, telemetry). An inactivity timer (air idle-timeout, default
 * 5 min) or an explicit 'air hibernate' command transitions to M1.
 *
 * M1 (dormant cycle): on each RTC wake the firmware reads the sensors until
 * stable while the WiFi connects in parallel, always saves the sample into
 * local history, and then (if online) flushes pending telemetry with a
 * non-blocking send bounded by the telemetry interval. It then sleeps for
 * max(history interval, telemetry backoff) before the next wake.
 *
 * Air settings live in /config/air.bin (see air/AirConfig.h) — never in
 * SystemConfig, so CONFIG_VERSION stays frozen.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#if SIMUT_AIR

#include "AppManager.h"
#include "LogManager.h"
#include "NetworkManager.h"
#include "SensorManager.h"
#include "StorageManager.h"
#include "TelemetryManager.h"
#include "air/AirConfig.h"
#include "air/pico_sleep.h"
#include <hardware/clocks.h>

#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h> /* settimeofday */

#include <pico/cyw43_arch.h>
#include <hardware/gpio.h>
#include <hardware/rtc.h>
#include <hardware/structs/scb.h>
#include <hardware/structs/usb.h>
#include <hardware/structs/watchdog.h>
#include <hardware/watchdog.h>

extern AppManager app;

/* ────────────────────────────────────────────────────────────────────────────
 * Air config persistence (/config/air.bin) — LittleFS, atomic via .tmp rename.
 * ──────────────────────────────────────────────────────────────────────────── */
bool AppManager::airLoadConfig(AirConfig& out) {
 out = airDefaultConfig( );
 File f = LittleFS.open(AIR_CONFIG_PATH, "r");
 if (!f) return false;
 AirConfig c;
 const size_t n = f.read(reinterpret_cast<uint8_t*>(&c), sizeof(c));
 f.close( );
 if (n != sizeof(c)) return false;
 if (!airConfigValid(c)) return false;
 out = c;
 return true;
}

bool AppManager::airSaveConfig(const AirConfig& c) {
 AirConfig out = c;
 out.magic   = AIR_CONFIG_MAGIC;
 out.version = AIR_CONFIG_VERSION;
 out.crc32   = airComputeCrc(out);
 File f = LittleFS.open(AIR_CONFIG_TMP, "w");
 if (!f) return false;
 const size_t n = f.write(reinterpret_cast<const uint8_t*>(&out), sizeof(out));
 f.close( );
 if (n != sizeof(out)) return false;
 LittleFS.remove(AIR_CONFIG_PATH);
 return LittleFS.rename(AIR_CONFIG_TMP, AIR_CONFIG_PATH);
}

/* ────────────────────────────────────────────────────────────────────────────
 * Onboard LED (CYW43) — ON while awake, OFF while dormant.
 * ──────────────────────────────────────────────────────────────────────────── */
void AppManager::airSetLed(bool on) {
#if defined(LED_BUILTIN)
 pinMode(LED_BUILTIN, OUTPUT);
 digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#else
 (void)on;
#endif
}

/* Reset the M0 inactivity timer. Any serial/BT command or web request lands
 * here so a device being actively used is never hibernated out from under the
 * operator. */
void AppManager::airMarkActivity( ) {
 _airLastActivityMs = millis( );
}

/* ────────────────────────────────────────────────────────────────────────────
 * M1 helpers
 * ──────────────────────────────────────────────────────────────────────────── */
static bool airAllStable(SensorManager& sm) {
 const auto& sensors = sm.getRuntimeSensors( );
 uint8_t active = 0, stable = 0;
 for (const auto& s : sensors) {
  if (s.config.active) {
   active++;
   if (s.bufferFull( )) stable++;
  }
 }
 return active > 0 && stable == active;
}

/* Power the sensor bus on/off via the configured power-gating GPIO. */
static void airSensorPower(uint8_t pin, bool on) {
 if (pin == PIN_UNUSED) return;
 gpio_init(pin);
 gpio_set_dir(pin, GPIO_OUT);
 gpio_put(pin, on ? 1 : 0);
}

/* Set the RTC time, holding the LOAD bit past the clk_rtc synchroniser.
 * The SDK rtc_set_datetime() writes LOAD then immediately ENABLE (which clears
 * LOAD). At 46875 Hz a register write takes 2 clk_rtc periods (~43 us) to cross
 * into the RTC domain, so a LOAD held for a single clk_sys cycle (~7 ns) is
 * usually dropped — the RTC keeps its previous (garbage) time and the alarm
 * fires at the wrong instant. Hold LOAD for 1 ms instead (datasheet 4.8.4). */
static void airRtcSetDatetime(const datetime_t* t) {
 rtc_hw->ctrl = 0; /* disable */
 while (rtc_running()) { tight_loop_contents(); }
 rtc_hw->setup_0 = (((uint32_t)t->year)  << RTC_SETUP_0_YEAR_LSB ) |
                   (((uint32_t)t->month) << RTC_SETUP_0_MONTH_LSB) |
                   (((uint32_t)t->day)   << RTC_SETUP_0_DAY_LSB  );
 rtc_hw->setup_1 = (((uint32_t)t->dotw)  << RTC_SETUP_1_DOTW_LSB) |
                   (((uint32_t)t->hour)  << RTC_SETUP_1_HOUR_LSB) |
                   (((uint32_t)t->min)   << RTC_SETUP_1_MIN_LSB  ) |
                   (((uint32_t)t->sec)   << RTC_SETUP_1_SEC_LSB  );
 rtc_hw->ctrl = RTC_CTRL_LOAD_BITS;      /* pulse LOAD */
 delay(1);                               /* hold past the 43us synchroniser */
 rtc_hw->ctrl = RTC_CTRL_RTC_ENABLE_BITS; /* enable (clears LOAD) */
 while (!rtc_running()) { tight_loop_contents(); }
}

/* ────────────────────────────────────────────────────────────────────────────
 * M0 -> M1 transition (command or idle timeout).
 * ──────────────────────────────────────────────────────────────────────────── */
void AppManager::airStartHibernate( ) {
 /* Persist everything before the deepest sleep: the open history block and
  * the telemetry cursor must be on flash because dormant loses SRAM. */
 _storageMgr->flushWipV5( );
 _storageMgr->flushCursorIfDirty( );
 _airActive = true;
 _airPhase = AIR_PHASE_WARMUP;
 _airPhaseTimer = millis( );
 airMarkActivity( );
}

/* ────────────────────────────────────────────────────────────────────────────
 * M1 pump — called from AppManager::loop( ) when _airActive.
 * ──────────────────────────────────────────────────────────────────────────── */
void AppManager::airLoop( ) {
 /* Debug: log phase transitions so a hang in the M1 cycle is localisable. */
 static AirPhase _lastLogged = AIR_PHASE_OFF;
 if (_airPhase != _lastLogged) {
  _lastLogged = _airPhase;
  static const char* _names[] = {"OFF","WARMUP","SAMPLE","DECIDE","PERSIST","CONNECT","FLUSH","SLEEP"};
  Serial.printf("[AIR] phase=%s @%lu\n", _names[(int)_airPhase], (unsigned long)millis());
 }
 switch (_airPhase) {
 case AIR_PHASE_WARMUP: {
  airSensorPower(_airCfg.sensorPowerPin, true);
  airSetLed(true);
  if (timeSince(_airPhaseTimer, 400)) {
   _airPhase = AIR_PHASE_SAMPLE;
   _airPhaseTimer = millis( );
  }
  break;
 }

 case AIR_PHASE_SAMPLE: {
  _sensorMgr->update( );
  /* Search for / connect to the configured WiFi IN PARALLEL with the sensor
   * sampling, so the link is (usually) already up by the time the sensors
   * stabilise. */
  _netMgr->update( );
  const bool stable = airAllStable(*_sensorMgr);
  const bool timedOut = timeSince(_airPhaseTimer, (uint32_t)_airCfg.stabTimeoutMs);
  if (stable || timedOut) {
   _airPhase = AIR_PHASE_DECIDE;
   _airPhaseTimer = millis( );
  }
  break;
 }

 case AIR_PHASE_DECIDE: {
  /* Always write this wake's sample into local history (the primary job of
   * the wake). The telemetry cursor is untouched; pending packets are sent in
   * CONNECT/FLUSH when the WiFi came up during SAMPLE. Only a live STA link
   * means "online" — isTimeSynced() is true even offline (provisional clock
   * from flash), so it must NOT gate the send path. */
  processHistoryLogging( );
  _storageMgr->flushWipV5( );
  const bool online = _netMgr->isConnected( );
  _airPhase = online ? AIR_PHASE_CONNECT : AIR_PHASE_SLEEP;
  _airPhaseTimer = millis( );
  break;
 }

 case AIR_PHASE_PERSIST: {
  /* Legacy no-op: the history save now happens unconditionally in DECIDE. */
  _airPhase = AIR_PHASE_SLEEP;
  break;
 }

 case AIR_PHASE_CONNECT: {
  _netMgr->update( );
  const bool ok = _netMgr->isConnected( );
  if (ok || timeSince(_airPhaseTimer, (uint32_t)_airCfg.connectTimeoutMs)) {
   _airPhase = ok ? AIR_PHASE_FLUSH : AIR_PHASE_SLEEP;
   _airPhaseTimer = millis( );
  }
  break;
 }

 case AIR_PHASE_FLUSH: {
  /* Persistent NON-blocking send: update() honours the backoff and sends one
   * batch per its internal cadence (= cfg.telInterval). We keep pumping until
   * the queue drains (done), a send fails (update() escalates the backoff ->
   * getBackoffRemainingMs()>0), or the WiFi drops. forceSync() would block
   * the whole loop on the HTTP upload; update() does not, so the watchdog is
   * fed between batches and a long backlog just keeps the device awake a
   * little longer instead of hanging it. */
  _netMgr->update( );
  _telemetryMgr->update( );
  _telemetryMgr->refreshPendingCount( );
  const bool done = (_telemetryMgr->getPendingEstimate( ) == 0);
  const bool serverLost = (_telemetryMgr->getBackoffRemainingMs( ) > 0);
  const bool netLost = !_netMgr->isConnected( );
  if (done || serverLost || netLost) {
   _storageMgr->flushCursorIfDirty( );
   _airPhase = AIR_PHASE_SLEEP;
  }
  break;
 }

 case AIR_PHASE_SLEEP: {
  airEnterDormant( );
  break;
 }

 case AIR_PHASE_OFF:
 default:
  break;
 }
}

/* ────────────────────────────────────────────────────────────────────────────
 * Sleep entry (RP2040 datasheet 2.11.5.1): clock the RTC from the XOSC, run
 * clk_sys/clk_ref from the XOSC, stop the PLLs, arm the RTC alarm, then WFI.
 * Wake is a RESUME; we soft-reset so the boot ROM re-inits the clocks and the
 * firmware boots back into M1 (scratch[0], always-on domain, carries the
 * Air marker).
 * ──────────────────────────────────────────────────────────────────────────── */
void AppManager::airEnterDormant( ) {
 /* Disarm the watchdog FIRST: the CYW43 teardown + RTC setup below take several
  * seconds without feeding it, and the watchdog would otherwise fire before we
  * reach the sleep (a false 'Core 0 stalled' reset). Once we are here we are
  * committed to hibernating, so there is nothing left for the watchdog to guard. */
 hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

 airSetLed(false);
 airSensorPower(_airCfg.sensorPowerPin, false);

 /* Stop WiFi and power the CYW43 down via WL_REG_ON (GPIO23).
  * Do NOT call cyw43_arch_deinit(): it hangs on the second call and leaves the
  * CYW43 in a state only a power cycle recovers (see ota/orchestrator.cpp
  * "Fix #2 ... REVERTIDO"). The next boot power-cycles the CYW43 anyway, so the
  * driver does not need a clean teardown — pulling WL_REG_ON LOW is the
  * hardware power-down that actually saves the current. */
 WiFi.disconnect(true);
 WiFi.end( );
 gpio_init(23);
 gpio_set_dir(23, GPIO_OUT);
 gpio_put(23, 0); /* WL_REG_ON LOW -> CYW43 powered off */

 /* Anchor the RTC to a valid base and arm the wake alarm interval ahead of it.
  * The RTC is only a wake timer here, so a fixed date is fine. */
 {
  /* Wake interval = history save interval (the primary job of the wake).
   * If the telemetry backoff (punishment) is larger, sleep for the backoff
   * instead, so the device does not wake just to be told to wait again. */
  uint32_t histMs = (uint32_t)_storageMgr->getHistoryIntervalMin( ) * 60000UL;
  if (histMs == 0) histMs = (uint32_t)AIR_WAKE_INTERVAL_MIN * 60UL * 1000UL;
  uint32_t backoffMs = _telemetryMgr->getBackoffRemainingMs( );
  uint32_t sleepMs = (backoffMs > histMs) ? backoffMs : histMs;
  uint32_t wakeSec = sleepMs / 1000UL;
  if (wakeSec == 0) wakeSec = 1;

  datetime_t t;
  t.year  = 2026; t.month = 1; t.day = 1; t.dotw = 4; t.hour = 0; t.min = 0; t.sec = 0;
  /* arduino-pico keeps time in software and leaves the SDK RTC in reset.
   * Bring it up: clock it from the XOSC and divide it to 46875 Hz
   * (12 MHz / 256) so rtc_init()'s 1-second divider (clkdiv_m1) stays within
   * its 16-bit range and rtc_set_alarm actually ticks at 1 s. */
  clock_configure(clk_rtc, 0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 12 * MHZ, 46875u);
  rtc_init( );

  airRtcSetDatetime(&t);
  {
   datetime_t dbg;
   delay(2); /* RTC write sync: up to 3 slow-domain cycles (~64us at 46875Hz) */
   rtc_get_datetime(&dbg);
   Serial.printf("[AIR] rtc after set:  %04d-%02d-%02d %02d:%02d:%02d dotw=%d\n", dbg.year, dbg.month, dbg.day, dbg.hour, dbg.min, dbg.sec, dbg.dotw);
  }
  t.sec  = (int8_t)(wakeSec % 60);
  t.min  = (int8_t)((wakeSec / 60) % 60);
  t.hour = (int8_t)((wakeSec / 3600) % 24);

  /* M1-vs-M0 discriminator: survives the wake, zeroed on power cycle. */
  watchdog_hw->scratch[0] = AIR_DORMANT_MAGIC;

  /* The wake is an INTENTIONAL SYSRESETREQ, but the watchdog REASON register is
   * read-only and retains a TIMER bit set by any historical watchdog fire
   * across soft resets (only a power cycle clears it). Without this mark, the
   * next boot's autopsy would misreport every dormant wake as "HW WATCHDOG:
   * Core 0 loop stalled" (a false FATAL polluting the persisted log). Mark the
   * wake as a clean reboot so the autopsy stays silent (see markCleanReboot). */
  LogManager::instance( ).markCleanReboot( );

  Serial.printf("[AIR] alarm: %02d:%02d:%02d wakeSec=%lu\n", t.hour, t.min, t.sec, (unsigned long)wakeSec);
  {
   datetime_t dbg;
   rtc_get_datetime(&dbg);
   Serial.printf("[AIR] pre-sleep rtc=%02d:%02d:%02d irq0=0x%lx\n", dbg.hour, dbg.min, dbg.sec, (unsigned long)rtc_hw->irq_setup_0);
  }

  /* Clean USB detach BEFORE sleep_run_from_xosc() stops clk_usb. Clearing the
   * D+ pull-up makes the host see a proper disconnect (SE0) and remove the
   * ttyACM device cleanly; without it, clock_stop(clk_usb) freezes the USB
   * controller with the pull-up still asserted and the host sees an
   * unresponsive device — the Linux cdc_acm driver then wedges and ttyACM
   * stops re-enumerating after a wake until the hub is power-cycled. This is
   * the same register write TinyUSB's dcd_disconnect() does. */
  Serial.flush( );
  hw_clear_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_PULLUP_EN_BITS);
  delay(100); /* let the host process the disconnect before clk_usb stops */

  /* Vendored pico-sdk deep-sleep: clk_sys/clk_ref -> XOSC, stop PLLs,
   * rtc_set_alarm, then WFI. Waking is a RESUME (RP2040 datasheet 2.11.5.1),
   * so this call returns with the system still on the XOSC and the
   * PLLs/USB/WiFi down. */
  sleep_goto_sleep_until(&t, nullptr);
 }

 /* Woke from DORMANT. Soft-reset so the boot ROM re-initialises the clocks
  * and the firmware boots back into M1 (watchdog scratch[0] still carries the
  * Air marker; it is only cleared on a power cycle). */
 scb_hw->aircr = 0x05FA0004u; /* VECTKEY | SYSRESETREQ (NVIC system reset) */
 while (true) { tight_loop_contents( ); } /* reset is immediate; not reached */
}

#endif /* SIMUT_AIR */
