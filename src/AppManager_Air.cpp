/**
 * @file AppManager_Air.cpp
 * @brief SIMUT Air — headless hibernation cycle (M0 operational / M1 dormant).
 *
 * M0 (cold boot): the full Alpha-like headless stack runs (web + serial + BT
 * config, sensors, telemetry). An inactivity timer (air idle-timeout, default
 * 5 min) or an explicit 'air hibernate' command transitions to M1.
 *
 * M1 (dormant cycle): on each RTC wake the firmware reads the sensors until
 * stable, checks whether the configured WiFi SSID is present, and then either
 * persists the sample (no network) or connects and flushes pending telemetry,
 * before powering down into dormant mode again.
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

#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h> /* settimeofday */

#include <pico/cyw43_arch.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/rtc.h>
#include <hardware/structs/scb.h>
#include <hardware/structs/watchdog.h>
#include <hardware/watchdog.h>
#include <hardware/xosc.h>
#include <hardware/regs/m0plus.h>

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
static bool airSsidPresent(const SystemConfig& cfg) {
 const int n = WiFi.scanNetworks( );
 for (int i = 0; i < n; i++) {
  if (strcmp(WiFi.SSID(i), cfg.wifiSsid) == 0) return true;
 }
 return false;
}

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
  const bool stable = airAllStable(*_sensorMgr);
  const bool timedOut = timeSince(_airPhaseTimer, (uint32_t)_airCfg.stabTimeoutMs);
  if (stable || timedOut) {
   _airWifiPresent = airSsidPresent(_storageMgr->getConfig( ));
   _airPhase = AIR_PHASE_DECIDE;
  }
  break;
 }

 case AIR_PHASE_DECIDE: {
  _airPhase = _airWifiPresent ? AIR_PHASE_CONNECT : AIR_PHASE_PERSIST;
  _airPhaseTimer = millis( );
  break;
 }

 case AIR_PHASE_PERSIST: {
  /* No network: write this wake's sample into local history. The telemetry
   * cursor does not advance, so it stays pending for the next online wake. */
  processHistoryLogging( );
  _storageMgr->flushWipV5( );
  _storageMgr->flushCursorIfDirty( );
  _airPhase = AIR_PHASE_SLEEP;
  break;
 }

 case AIR_PHASE_CONNECT: {
  _netMgr->update( );
  const bool ok = _netMgr->isTimeSynced( ) || _netMgr->isConnected( );
  if (ok || timeSince(_airPhaseTimer, (uint32_t)_airCfg.connectTimeoutMs)) {
   _airPhase = ok ? AIR_PHASE_FLUSH : AIR_PHASE_SLEEP;
   _airPhaseTimer = millis( );
  }
  break;
 }

 case AIR_PHASE_FLUSH: {
  _telemetryMgr->forceSync( );
  _telemetryMgr->update( );
  const bool done = (_telemetryMgr->getPendingEstimate( ) == 0);
  if (done || timeSince(_airPhaseTimer, (uint32_t)_airCfg.flushTimeoutMs)) {
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
 * Dormant entry. RP2040 has no pico/sleep.h in this framework, so the sequence
 * is done by hand (same steps the SDK's sleep_goto_dormant_until performs):
 *   RTC alarm -> clocks to XOSC -> xosc_dormant -> SLEEPDEEP -> __wfi.
 * Wake is a full reset; scratch[0] (always-on domain) carries the Air marker.
 * ──────────────────────────────────────────────────────────────────────────── */
void AppManager::airEnterDormant( ) {
 airSetLed(false);
 airSensorPower(_airCfg.sensorPowerPin, false);

 /* Stop WiFi and power the CYW43 down so it does not drain the battery. */
 WiFi.disconnect(true);
 WiFi.end( );
 cyw43_arch_deinit( );

 /* Program the RTC alarm for the next wake.
 *
 * The provisional clock is VIRTUAL (NetworkManager::getEpoch keeps a base +
 * millis offset and never calls settimeofday), so time(nullptr) and
 * rtc_get_datetime() are still 0 on a device that never synced NTP -- arming
 * the alarm against them lands it in year 0 and it fires immediately. Fix:
 * settle the SDK RTC to the best-known epoch first, then arm the alarm
 * interval minutes ahead of it. */
 {
  time_t base = _netMgr->getEpoch( );
  if (base < 1600000000) base = SIMUT_BUILD_EPOCH;
  struct timeval tv;
  tv.tv_sec = base;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  /* The wake period follows the telemetry interval (cfg.telInterval, ms):
   * one knob drives both how often telemetry is sent and how often the device
   * wakes. 0 (telemetry off) falls back to the compile-time default. */
  uint32_t telMs = _storageMgr->getConfig( ).telInterval;
  if (telMs == 0) telMs = (uint32_t)AIR_WAKE_INTERVAL_MIN * 60UL * 1000UL;
  uint32_t wakeSec = telMs / 1000UL;
  if (wakeSec == 0) wakeSec = 1;
  const time_t wake = base + (time_t)wakeSec;
  struct tm wt;
  gmtime_r(&wake, &wt);
  datetime_t t;
  t.year  = (int16_t)(wt.tm_year + 1900);
  t.month = (int8_t)(wt.tm_mon + 1);
  t.day   = (int8_t)wt.tm_mday;
  t.dotw  = (int8_t)wt.tm_wday;
  t.hour  = (int8_t)wt.tm_hour;
  t.min   = (int8_t)wt.tm_min;
  t.sec   = (int8_t)wt.tm_sec;
  rtc_set_alarm(&t, nullptr);
 }

 /* Run the system clock from the crystal and put the XOSC in dormant mode so
  * the PLLs and everything else can power down. The RTC keeps running. */
 xosc_init( );
 clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                 CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                 12 * MHZ, 12 * MHZ);
 clock_stop(clk_usb);
 clock_stop(clk_adc);
 clock_configure(clk_rtc, 0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 0, 0);
 clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, 12 * MHZ, 12 * MHZ);

 /* Disarm the watchdog (it is in the always-on domain and would fire during
  * dormant, masking the RTC wake as a watchdog reset). */
 hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

 /* Marker: on wake scratch[0] survives (always-on domain); a power cycle
  * zeroes it. This is the M1-vs-M0 discriminator read in setup( ). */
 watchdog_hw->scratch[0] = AIR_DORMANT_MAGIC;

 scb_hw->scr |= M0PLUS_SCR_SLEEPDEEP_BITS;
 xosc_dormant( );
 __wfi( );

 /* Not reached — dormant wake is a full reset. */
 while (true) { tight_loop_contents( ); }
}

#endif /* SIMUT_AIR */
