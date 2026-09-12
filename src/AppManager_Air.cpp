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
 airSanitise(c);              /* chargerPin took over a dead field — see AirConfig.h */
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
/* The onboard LED is NOT the awake indicator on this build — the sensor power
 * pin is (AIR_SENSOR_POWER_PIN, high the whole time the device is awake, low
 * the whole time it sleeps, and the line the PicoHand probe times the cycle on).
 *
 * On the Pico W, LED_BUILTIN is PIN_LED = 64: a GPIO of the CYW43, not of the
 * RP2040. Writing it needs the wireless chip powered and initialised, so on a
 * reading-only wake — which exists precisely to keep that chip off — a single
 * digitalWrite would bring the radio up and spend the entire saving on a light
 * nobody is looking at. Hence the guard: the LED follows the radio, and the
 * awake state is read off the sensor power pin. */
void AppManager::airSetLed(bool on) {
#if defined(LED_BUILTIN)
 if (!_airRadioUp) return;
 pinMode(LED_BUILTIN, OUTPUT);
 digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#else
 (void)on;
#endif
}

/* Two schedules, one alarm.
 *
 * The device wakes on the history interval, always — that is the measurement
 * cadence and nothing may stretch it. The telemetry interval is expressed in
 * whole wakes of that cadence, which is what makes a send ALWAYS coincide with
 * a reading: the expensive part of a transmission is not the transmission, it
 * is being awake at all, and a wake that is already happening costs nothing
 * extra to reuse.
 *
 * Counting wakes rather than comparing clocks is deliberate. The clock on a
 * reading-only wake is provisional (no NTP without a radio), so a rule written
 * against epochs would be measuring the very thing it cannot trust. A wake
 * count is exact by construction.
 *
 * Rounding is up, and on purpose: 15 minutes of telemetry over a 2-minute
 * reading interval sends every 8 wakes (16 min), not every 7 (14 min). Sending
 * early would break the promise that the operator's interval is a floor. */
/* How long the FLUSH may run on this wake, counted from the start of the phase.
 *
 * The configured cap (flushTimeoutMs, 30 s) knows nothing about the reading
 * interval, so with readings every minute a telemetry wake ran 27 s of boot
 * plus 30 s of flush and woke up late for its own next reading — the OVERRUN
 * in the sleep log, measured at 57 s awake for a 60 s interval. The budget is
 * whichever is smaller: the cap, or the room between the start of the FLUSH
 * and the point where the wake has to be over.
 *
 * Only a real wake has that anchor: in M1 the boot IS the wake, so millis( ) —
 * and with it _airPhaseTimer — is time-since-wake. A cycle started by
 * `air hibernate` from M0 carries the whole M0 uptime and gets the cap, as
 * before. */
uint32_t AppManager::airFlushBudgetMs( ) const {
 uint32_t budget = (uint32_t)_airCfg.flushTimeoutMs;
 if (!_airWokeFromSleep) return budget;

 uint32_t histMs = (uint32_t)_storageMgr->getHistoryIntervalMin( ) * 60000UL;
 if (histMs == 0) return budget;

 /* What has to be left after the FLUSH: the tail above, plus a sleep worth
  * taking. Without the sleep term the arithmetic "fits" and the cycle still
  * overruns — 25 s of boot plus a 30 s flush leaves 3 s, under AIR_MIN_SLEEP_SEC,
  * and airEnterDormant floors it and logs OVERRUN. */
 const uint32_t reserve = (uint32_t)AIR_FLUSH_TAIL_MS
                        + (uint32_t)AIR_MIN_SLEEP_SEC * 1000UL;
 const uint32_t hardStop = (histMs > reserve * 2UL) ? (histMs - reserve)
                                                    : (histMs / 2UL);

 /* Relative to the START of the FLUSH, because that is what the caller
  * compares against (timeSince(_airPhaseTimer, budget)). Measuring "what is
  * left from now" instead makes the deadline recede as the phase runs and
  * cuts it in half: with the flush starting at 25 s and a 55 s stop, the
  * phase ended at 40 s — measured on the bench 2026-09-07 before this fix. */
 const uint32_t left = (hardStop > _airPhaseTimer) ? (hardStop - _airPhaseTimer) : 0UL;
 return (left < budget) ? left : budget;
}

/* Does THIS wake raise the radio?
 *
 * The rule is the amount of data waiting, not the clock: the CYW43 comes up
 * when the pending count has reached the configured minimum batch. A device
 * with nothing to say never powers the radio, and one that has been offline
 * for a while sends as soon as it has enough — which is what the operator
 * asked for when they set the minimum.
 *
 * The second term is the part a count alone cannot express. With a dead
 * collector the queue only grows, so the trigger would be true on every single
 * wake and the radio would burn the battery answering nobody. The M0 backoff
 * cannot help: it lives in RAM and every wake is a boot. So a failed telemetry
 * wake books a number of wakes to skip, kept in the same scratch field that
 * used to hold the wake counter, and doubling per failure — the same escalation
 * the mains path does in milliseconds, expressed in wakes. */
/* Is the charger plugged in?
 *
 * A divider off the 5 V rail drives the pin high while charging. On the charger
 * there is no battery to protect, so the device stops hibernating and stays
 * awake and reachable — which is what an operator wants from a bench device.
 * Reading it costs one GPIO read; the pin is configured once in setup( ). */
bool AppManager::airOnCharger( ) const {
 const uint8_t pin = _airCfg.chargerPin;
 if (pin == PIN_UNUSED) return false;
 const bool high = gpio_get(pin);
#if AIR_CHARGER_ACTIVE_HIGH
 return high;
#else
 return !high;
#endif
}

/* The wake's version of TelemetryManager::telemetryDue( ), with one term the
 * mains path does not need: the sample this wake has not taken yet.
 *
 * The decision is made in setup( ), before the cycle reaches DECIDE, so the
 * count it reads is always one short of what the queue will hold by the time
 * the radio would be used — and a wake never skips its reading, so that record
 * is not a guess. Without the term, t_int is off by one wake in the direction
 * that costs the most: t_int=1 ("send every reading") sent on every OTHER wake,
 * because the wake right after a send saw an empty queue.
 *
 * Erring early rather than late is also the cheap direction. A wake that raises
 * the radio one reading early sends a batch one short; a wake that raises it
 * late leaves the operator's cadence quietly stretched. */
bool AppManager::airTelemetryDue( ) const {
 if (_airSkipWakes > 0) return false;          /* still serving a failed wake's penalty */
 const uint32_t minBatch = _storageMgr->getConfig( ).telInterval;
 if (minBatch == 0) return false;              /* telemetry off */
 return (uint32_t)_telemetryMgr->getPendingEstimate( ) + 1UL >= minBatch;
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

/* Power the sensor bus on/off via the power-gating GPIO.
 *
 * gpio_init() is done ONCE per pin, not on every call: the SDK's gpio_init
 * drives the pin to input and low before handing it back, so calling it on
 * every pump of the WARMUP phase glitched the sensor supply (and the logic
 * analyser probe on the same line) several times per wake.
 *
 * Not static: AppManager::setup( ) asserts the line before the sensors are
 * probed, so the Air build powers its sensors for the whole time it is awake —
 * including the operational mode M0, where nothing used to turn them on. */
void AppManager::airSensorPower(uint8_t pin, bool on) {
 if (pin == PIN_UNUSED) return;
 static uint32_t initialised = 0;   /* bitmask of pins already gpio_init'd */
 if (pin < 32 && !(initialised & (1u << pin))) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  initialised |= (1u << pin);
 }
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
  * the telemetry cursor must be on flash because dormant loses SRAM.
  * force=true, or the cursor's coalescing window swallows the write. */
 _storageMgr->flushWipV5( );
 _storageMgr->flushCursorIfDirty(true);

 /* Arm the cycle in flash. This is the operator's intent, and it is what brings
  * the device back if a reset interrupts the cycle — the watchdog marker is
  * cleared on every boot on purpose and cannot carry it (plan F25). Written
  * only on the transition, which happens once per M0 session, never per wake:
  * an M1 wake re-enters the cycle from setup( ) without coming through here. */
 if (!airCycleArmed(_airCfg)) {
  _airCfg.flags |= AIR_FLAG_CYCLE_ARMED;
  airSaveConfig(_airCfg);
 }
 _airResumeGraceSec = 0;  /* the grace did its job; back to the configured idle */

 _airActive = true;
 _airNetGaveUp = false;   /* fresh cycle, fresh allowance of WiFi attempts */
 /* Entering the cycle from M0, the radio is whatever this boot brought up. If
  * it is up, spend it: the association is already paid for, so this first cycle
  * may as well flush before sleeping. From the next wake on, the schedule in
  * airTelemetryDue( ) decides. */
 _airRadioWake = _airRadioUp;
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
  /* From here to DECIDE the sensors read back to back. The wake is not a
   * display refreshing once a second: it has one record to take and every
   * millisecond between conversions is the core awake doing nothing. The
   * filter and the values are unchanged — SAMPLE still waits for the same
   * full window, it just stops idling between the samples that fill it. */
  _sensorMgr->setFastSampling(true);
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
   * stabilise.
   *
   * Bounded, though: with the SSID out of range the network must not hold the
   * wake open. After AIR_MAX_CONNECT_ATTEMPTS failed attempts the pump stops
   * for the rest of this wake — the sensors still finish, the history is still
   * written, and the device hibernates. The next wake is a fresh boot, so it
   * starts over with a clean counter and tries again. */
  if (_airRadioWake && !_airNetGaveUp) {
   _netMgr->update( );
   if (!_netMgr->isConnected( ) &&
       _netMgr->getConnectCycles( ) >= AIR_MAX_CONNECT_ATTEMPTS) {
    _airNetGaveUp = true;
    Serial.printf("[AIR] wifi: no link after %u attempt(s) — sampling on, will retry next wake\n",
                  (unsigned)_netMgr->getConnectCycles( ));
   }
  }
  const bool stable = airAllStable(*_sensorMgr);
  const bool timedOut = timeSince(_airPhaseTimer, (uint32_t)_airCfg.stabTimeoutMs);
  if (stable || timedOut) {
   _airPhase = AIR_PHASE_DECIDE;
   _airPhaseTimer = millis( );
  }
  break;
 }

 case AIR_PHASE_DECIDE: {
  /* The window is full (or timed out): back to the ordinary cadence, so an
   * `air stop` landing in this wake hands the operator a normal M0. */
  _sensorMgr->setFastSampling(false);
  /* Always write this wake's sample into local history (the primary job of
   * the wake). The telemetry cursor is untouched; pending packets are sent in
   * CONNECT/FLUSH when the WiFi came up during SAMPLE. "Online" is a live STA
   * link with an IP — isLinkUp( ), NOT isConnected( ) (plan F08): isConnected( )
   * is NET_READY, reached only after NTP returns, and nothing times that wait
   * out, so on a network without internet the send path gated on it never ran
   * and the wake slept without trying. isTimeSynced( ) must not gate it either
   * — the provisional clock stamps the records well enough, and a late stamp
   * beats a lost measurement. */
  processHistoryLogging( );
  _storageMgr->flushWipV5( );

  /* Plan F11. M1 never runs StorageManager::update( ) (the M0 loop's job) nor
   * the deferred-log flush, so in M1-only use the FS budget is never enforced
   * and logs deferred by a flash gate never land — the partition fills. Do
   * both here, in the awake window, with the watchdog armed and fed
   * (drainStorageLimit feeds it per pass). Deliberately NOT at sleep entry:
   * a first cut put the drain in airEnterDormant, next to the clock teardown,
   * and it went 0 wakes / 240 s against the release's 2 / 240 s back-to-back
   * (2026-09-09) — the wake path does not tolerate flash work beside it. Here
   * a history write already happens two lines up, so this is the same safe
   * context. Bounded to 4 files a wake; a bigger backlog drains over wakes. */
  {
   const uint8_t freed = _storageMgr->drainStorageLimit(4);
   if (freed) LOG_CODE(LOG_INFO, "STO", STO_ENFORCE_BUDGET, (int)freed, "m1");
  }
  LogManager::instance( ).flushPendingIfAny( );

  /* A reading-only wake is done here: the radio was never started, so there is
   * nothing to connect and nothing to flush. Giving up on the WiFi is likewise
   * final for this wake — even if the link came up in the meantime, chasing it
   * now would spend awake time the reading no longer needs. The data is on
   * flash and the next telemetry wake will send it. */
  const bool online = _airRadioWake && !_airNetGaveUp && _netMgr->isLinkUp( );
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
  /* isLinkUp( ), not isConnected( ): the link carries the upload as soon as
   * DHCP is done (NET_CONNECTED_WAIT_NTP), and waiting for NTP here is exactly
   * the F08 stall — connectTimeoutMs already bounds this, but the wait is for
   * the link, not the clock. */
  const bool ok = _netMgr->isLinkUp( );
  if (ok || timeSince(_airPhaseTimer, (uint32_t)_airCfg.connectTimeoutMs)) {
   _airPhase = ok ? AIR_PHASE_FLUSH : AIR_PHASE_SLEEP;
   _airPhaseTimer = millis( );
   /* The wake's job is to empty the queue as fast as the server takes it:
    * telInterval already had its say in airTelemetryDue( ), and inside the
    * wake it only ever held the radio on for nothing (see the comment on
    * _drainMode in TelemetryManager::update). */
   if (ok) _telemetryMgr->setDrainMode(true);
  }
  break;
 }

 case AIR_PHASE_FLUSH: {
  /* Persistent NON-blocking send: update( ) sends one batch per call, at the
   * pace the server sets (drain mode is on, so telInterval — which already had
   * its say in airTelemetryDue( ) — does not gate anything here). We keep
   * pumping until the queue drains (done), a send fails (update( ) escalates
   * the backoff -> getBackoffRemainingMs()>0), the WiFi drops, or the wake runs
   * out of budget. forceSync( ) would block the whole loop on the HTTP upload;
   * update( ) does not, so the watchdog is fed between batches and a long
   * backlog just keeps the device awake a little longer instead of hanging it. */

  /* Telemetry switched off is the factory default (telInterval == 0), and
   * TelemetryManager::update( ) returns immediately in that state — it never
   * sends, never fails, never escalates a backoff. Without this gate the three
   * exit conditions below can none of them become true and the phase spins
   * forever: the device stays awake, on battery, with nothing to do. */
  if (_storageMgr->getConfig( ).telInterval == 0) {
   _airPhase = AIR_PHASE_SLEEP;
   break;
  }

  _netMgr->update( );
  _telemetryMgr->update( );
  _telemetryMgr->refreshPendingCount( );
  const bool done = (_telemetryMgr->getPendingEstimate( ) == 0);
  const bool serverLost = (_telemetryMgr->getBackoffRemainingMs( ) > 0);
  /* isLinkHealthy( ), not isNetworkHealthy( ): both apply the RSSI floor, but
   * the healthy check is built on isConnected( ) (NET_READY), so without NTP it
   * would read "unhealthy" the instant the link came up and end the phase
   * before a single batch went out — the F08 stall, one layer down. The link
   * plus the RSSI floor is what actually gates an upload. */
  const bool netLost = !_netMgr->isLinkHealthy( );
  /* Wall-clock budget. Everything above depends on the uploader reaching a
   * verdict; this one does not, so no future stall in that path can hold the
   * device awake indefinitely. Derived from the reading interval, not just the
   * configured cap — see airFlushBudgetMs( ). */
  const uint32_t budgetMs = airFlushBudgetMs( );
  const bool timedOut = timeSince(_airPhaseTimer, budgetMs);
  /* Hibernate and continue later, which is the other half of the request:
   * when the uploader wants a gap the rest of this wake cannot cover, waiting
   * it out with the radio on costs more than sleeping. The records stay on
   * flash — 116 days of them fit — and the next telemetry wake picks the drain
   * up from the cursor. */
  const uint32_t waitMs = _telemetryMgr->getNextSendDelayMs( );
  const uint32_t usedMs = millis( ) - _airPhaseTimer;
  const bool notWorthWaiting = (waitMs > 0) && ((usedMs + waitMs) >= budgetMs);
  if (done || serverLost || netLost || timedOut || notWorthWaiting) {
   _telemetryMgr->setDrainMode(false);
   /* force: the send that just advanced the cursor happened milliseconds ago,
    * so the coalescing window has not elapsed and never will — the next stop
    * is deep sleep, which loses the RAM copy. */
   _storageMgr->flushCursorIfDirty(true);
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

 /* Last chance to put the telemetry cursor on flash: this function is the one
  * choke point every path into sleep goes through, and after it the RAM copy
  * is gone. A no-op when nothing moved. */
 _storageMgr->flushCursorIfDirty(true);

 /* Reaching here means the wake did its whole job, so the crash-loop guard's
  * count is stale (plan F25). One flash write to clear it, and only when there
  * is something to clear — the healthy path never touches air.bin. */
 if (airDirtyBoots(_airCfg) != 0) {
  airSetDirtyBoots(_airCfg, 0);
  airSaveConfig(_airCfg);
 }

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

  /* Anchor the cadence on the WAKE, not on this moment.
   *
   * The history interval is a sampling cadence: "save a record every h_int".
   * Sleeping h_int from here makes the real period h_int PLUS the time this
   * wake spent awake, because the awake window sits between two sleeps. That
   * was measured on 2026-09-06: 147 s of real period for 120 s configured —
   * about 22% fewer samples per day than asked for, and growing with anything
   * that lengthens the wake (slow sensors, a big telemetry backlog).
   *
   * millis( ) is exactly the elapsed time since this wake, because an M1 wake
   * IS a boot. Subtracting it makes boot-to-boot equal h_int.
   *
   * Only when this boot really was a wake: after a cold boot (or an `air stop`
   * that returned to M0) millis( ) counts operator time, not cycle time, and
   * there is no previous wake to anchor to. */
  uint32_t histSleepMs = histMs;
  const uint32_t awakeMs = millis( );
  if (_airWokeFromSleep) {
   histSleepMs = (awakeMs + AIR_MIN_SLEEP_SEC * 1000UL < histMs)
                     ? (histMs - awakeMs)
                     : (AIR_MIN_SLEEP_SEC * 1000UL);
  }

  /* The backoff is NOT compensated: getBackoffRemainingMs( ) already counts
   * from now, and shortening a punishment would defeat it. */
  uint32_t backoffMs = _telemetryMgr->getBackoffRemainingMs( );
  uint32_t sleepMs = (backoffMs > histSleepMs) ? backoffMs : histSleepMs;
  /* Round, do not truncate. The RTC alarm only has second granularity, so this
   * division is where the period loses its last fraction — and truncating threw
   * away up to a full second on EVERY cycle, always in the same direction.
   * Measured 2026-09-06 with the device reporting its own slept time: asking for
   * 91817 ms gave 91 s, a 0.817 s shortfall that showed up as a 118.9 s cycle
   * for a 120 s interval. Rounding centres the error and bounds it at the half
   * second the RTC's resolution costs. */
  uint32_t wakeSec = (sleepMs + 500UL) / 1000UL;
  {
    /* Plan F10: the alarm is time-of-day, so a full day aliases to "now". A
     * 1440-minute interval — the most h_int accepts — is the one value that
     * reaches this; it sleeps one second short and says so. */
    const uint32_t asked = wakeSec;
    wakeSec = airSleepSecBounded(wakeSec);
    if (wakeSec != asked) {
      LOG_CODE(LOG_WARN, "AIR", APP_AIR_SLEEP_CLAMPED, (int)(asked / 60UL), "");
    }
  }

  datetime_t t;
  t.year  = 2026; t.month = 1; t.day = 1; t.dotw = 4; t.hour = 0; t.min = 0; t.sec = 0;
  /* arduino-pico keeps time in software and leaves the SDK RTC in reset.
   * Bring it up: clock it from the XOSC and divide it to 46875 Hz
   * (12 MHz / 256) so rtc_init()'s 1-second divider (clkdiv_m1) stays within
   * its 16-bit range and rtc_set_alarm actually ticks at 1 s. */
  clock_configure(clk_rtc, 0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 12 * MHZ, 46875u);
  rtc_init( );

  airRtcSetDatetime(&t);

  /* Arm the alarm relative to what the RTC ACTUALLY reads, not to the zero that
   * was just written to it.
   *
   * Measured on the bench 2026-09-06: right after airRtcSetDatetime(00:00:00)
   * the counter already reads 00:00:01 — the load lands a tick of its own, and
   * it does so on every cycle, not now and then. An alarm armed at wakeSec was
   * therefore only wakeSec-1 ticks away, and the device woke about a second
   * early EVERY time. The PicoHand probe put a number on it: 119.31 s and
   * 118.69 s of measured period against 120.22 s and 119.59 s of intended
   * period, a flat 0.90 s lost per cycle that no amount of rounding in the
   * millisecond arithmetic could recover, because the loss happens after the
   * arithmetic.
   *
   * Reading the base back and adding to it is immune to whatever the load pulse
   * does — if a future SDK stops landing that tick, baseSec is simply 0 and the
   * alarm is where it always was. */
  uint32_t baseSec = 0;
  {
   datetime_t dbg;
   delay(2); /* RTC write sync: up to 3 slow-domain cycles (~64us at 46875Hz) */
   rtc_get_datetime(&dbg);
   baseSec = (uint32_t)dbg.hour * 3600u + (uint32_t)dbg.min * 60u + (uint32_t)dbg.sec;
   Serial.printf("[AIR] rtc after set:  %04d-%02d-%02d %02d:%02d:%02d dotw=%d base=%lus\n",
                 dbg.year, dbg.month, dbg.day, dbg.hour, dbg.min, dbg.sec, dbg.dotw,
                 (unsigned long)baseSec);
  }

  const uint32_t alarmSec = baseSec + wakeSec;
  t.sec  = (int8_t)(alarmSec % 60);
  t.min  = (int8_t)((alarmSec / 60) % 60);
  t.hour = (int8_t)((alarmSec / 3600) % 24);

  /* M1-vs-M0 discriminator: survives the wake, zeroed on power cycle. */
  watchdog_hw->scratch[0] = AIR_DORMANT_MAGIC;

  /* The wake is an INTENTIONAL SYSRESETREQ, but the watchdog REASON register is
   * read-only and retains a TIMER bit set by any historical watchdog fire
   * across soft resets (only a power cycle clears it). Without this mark, the
   * next boot's autopsy would misreport every dormant wake as "HW WATCHDOG:
   * Core 0 loop stalled" (a false FATAL polluting the persisted log). Mark the
   * wake as a clean reboot so the autopsy stays silent (see markCleanReboot). */
  LogManager::instance( ).markCleanReboot( );

  /* wip= is the flash cost of this wake: how many times the open history block
   * was written WHOLE to /history/.wip. It goes on this line because this line
   * is the one thing every wake prints last — the console answers early in the
   * window, well before the record is even written, so `air status` cannot see
   * it (measured 2026-09-07: three wakes polled, all reporting wip=0 from
   * inside SAMPLE). */
  Serial.printf("[AIR] alarm: %02d:%02d:%02d wakeSec=%lu awake=%lums target=%lums wip=%u%s\n",
                t.hour, t.min, t.sec, (unsigned long)wakeSec,
                (unsigned long)awakeMs, (unsigned long)histMs,
                (unsigned)_storageMgr->h5WipWrites( ),
                (_airWokeFromSleep && awakeMs + AIR_MIN_SLEEP_SEC * 1000UL >= histMs)
                    ? " OVERRUN" : "");
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

  /* How long the sleep ACTUALLY lasted, straight from the RTC that timed it.
   *
   * Everything else about the cycle is inferred: USB enumeration lags the boot,
   * the probe line moves before the WiFi teardown, millis( ) restarts at every
   * wake. The RTC is the one clock that ran across the sleep, so the distance it
   * covered IS the sleep, to the second. Parked in scratch[1] (always-on,
   * survives the SYSRESETREQ below) and printed by the next boot next to what
   * was requested — the two together are what makes the period auditable
   * instead of derived.
   *
   * Measured from baseSec, not from zero: the load pulse lands a tick, so the
   * counter starts at 1 and reporting its raw value overstated the sleep by a
   * second. That overstatement is what made the alarm look exact while the
   * probe was measuring a cycle a second short. */
  {
   datetime_t got;
   rtc_get_datetime(&got);
   const uint32_t nowSec = (uint32_t)got.hour * 3600u +
                           (uint32_t)got.min * 60u + (uint32_t)got.sec;
   const uint32_t sleptSec = (nowSec >= baseSec) ? (nowSec - baseSec) : nowSec;
   /* The telemetry penalty rides along in the same register.
    *
    * With the trigger being "enough records are waiting", a collector that
    * stops answering would arm it on every single wake — the queue only grows
    * — and the radio would burn the battery talking to nobody. The mains
    * backoff cannot help here: it lives in RAM and every wake is a boot. So a
    * wake whose send failed books AIR_TEL_FAIL_SKIP_WAKES reading wakes of
    * silence, counted down here, one per wake. A wake that succeeded (or had
    * nothing to send) clears it.
    *
    * The penalty is flat, not doubling: the escalation would need a second
    * counter to survive the sleep, and the only field left in this register is
    * the sleep seconds, which has to keep its full range for the provisional
    * clock. Flat still turns a dead collector from one radio wake per reading
    * into one per six. */
   uint8_t skip;
   if (_airRadioWake) {
    const bool sendFailed = _telemetryMgr->getBackoffRemainingMs( ) > 0;
    skip = sendFailed ? (uint8_t)AIR_TEL_FAIL_SKIP_WAKES : 0;
   } else {
    skip = (_airSkipWakes > 0) ? (uint8_t)(_airSkipWakes - 1) : 0;
   }
   watchdog_hw->scratch[1] = airScratch1Pack(sleptSec, skip);
  }
 }

 /* Woke from DORMANT. Soft-reset so the boot ROM re-initialises the clocks
  * and the firmware boots back into M1 (watchdog scratch[0] still carries the
  * Air marker; it is only cleared on a power cycle). */
 scb_hw->aircr = 0x05FA0004u; /* VECTKEY | SYSRESETREQ (NVIC system reset) */
 while (true) { tight_loop_contents( ); } /* reset is immediate; not reached */
}

#endif /* SIMUT_AIR */
