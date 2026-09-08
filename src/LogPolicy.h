/**
 * @file LogPolicy.h
 * @brief Edge-triggered persistence filter for the binary system log.
 * @details Decides which log records earn a slot in /system.blog. The file is
 * 800 records of 12 B, twice (current + old) — 1600 total. A device
 * sending telemetry every few seconds spends 65-78% of that window
 * on SYS_TEL_SENT saying, over and over, that nothing changed; the
 * whole forensic window collapses to about one hour.
 *
 * The rule this class implements: an event is persisted only when it
 * is a state TRANSITION. For a routine event that means the first
 * one after boot, or the first success after a failure. For a
 * FAILURE it means the first one after the family was healthy — a
 * collector that stops answering writes one record, not one per
 * attempt.
 *
 * Failures were exempt until 2026-09-07, and the exemption turned
 * out to be the bigger leak: a dead collector produced SYS_TEL_FAIL
 * and SYS_TEL_RETRY on EVERY attempt. Because those two alternate,
 * latching per CODE would have suppressed nothing at all — which is
 * why the latch is per FAMILY.
 *
 * Security events and configuration changes are still never
 * filtered, and neither is anything at FATAL.
 *
 * Two things keep the channel honest under that rule. A per-family
 * heartbeat re-persists one routine record per hour, so "healthy and
 * quiet" stays distinguishable from "dead"; and the suppressed count
 * is reported hourly as SYS_LOG_SUPPRESSED, so nothing disappears
 * silently.
 *
 * Deliberately free of Arduino.h — the whole decision is a pure
 * function of (code, level, now), which is what makes it testable on
 * the host via `pio test -e native_logpolicy`. Time comes in as a
 * parameter for the same reason.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stdint.h>

/** Health families tracked by the filter.
 *
 * A family is a set of codes that report on the same subsystem: some say it
 * worked, some say it failed. The filter only needs to know which of the two
 * the family last said. Sensors are absent on purpose — SensorManager already
 * tracks them per instance, and a family latch would let a failing sensor mask
 * another one recovering. */
enum LogHealthGroup : uint8_t {
 LOGGRP_NONE = 0,
 LOGGRP_TEL  = 1,  /**< telemetry upload / MQTT publish */
 LOGGRP_HIST = 2,  /**< history record, block seal, .wip snapshot */
 LOGGRP_NET  = 3,  /**< WiFi association and address */
 /** The alarm line is its own family, not part of LOGGRP_TEL.
  *
  * It is a second transport with its own queue and its own failures, and one
  * latch for both would let a stuck alarm line hide the moment measurement
  * telemetry recovered — the record the whole filter exists to keep. */
 LOGGRP_TELALM = 4,
 LOGGRP_COUNT
};

/** One entry of the routing table, packed into a single uint16_t.
 *
 * Every LogCode in use is below 1024, so the top bits of the field are free:
 * bits 12-14 carry the family and bit 15 the fault flag. A {uint16, uint8,
 * uint8} struct would be padded to 4 bytes per entry, and this table lives in
 * a build whose test profile has three digits of flash headroom left. */
#define LOGPOL_RULE(code, grp, fault) \
 ((uint16_t)((uint16_t)(code) | ((uint16_t)(grp) << 12) | ((uint16_t)(fault) << 15)))

#define LOGPOL_RULE_CODE(r)  ((uint16_t)((r) & 0x0FFF))
#define LOGPOL_RULE_GROUP(r) ((uint8_t)(((r) >> 12) & 0x07))
#define LOGPOL_RULE_FAULT(r) (((r) & 0x8000u) != 0)

/** Re-persist one routine record per family at this cadence, so a healthy
 * system still leaves a trail. 96 records/day across three families plus the
 * hourly accounting — about 6% of the window, against the 73-89% the raw
 * stream was spending. */
#define LOGPOL_HEARTBEAT_MS 3600000UL

/** Cadence of the SYS_LOG_SUPPRESSED accounting record. */
#define LOGPOL_REPORT_MS 3600000UL

/** Records at this level or above are never filtered — UNLESS the code is
 * routed as a fault below, in which case the family latch decides.
 *
 * Numeric rather than `LOG_WARN` because LogLevel lives in LogManager.h, which
 * drags in pico/mutex.h and hardware/watchdog.h — and the point of this file is
 * that it compiles on the host. LogManager.cpp static_asserts the two agree. */
#define LOGPOL_LEVEL_WARN 2

/** A fatal is the record the whole forensic window exists for. It is never
 * filtered, whatever the table says — the fault latch below must not be able
 * to swallow the one line that explains a reset. */
#define LOGPOL_LEVEL_FATAL 4

/** Records below this level never reach flash at all — the floor logCode( )
 * applies, matching what log( ) has always done. Kept here so the filter and
 * the level gate are read together. */
#define LOGPOL_LEVEL_INFO 1

class LogPolicy {
public:
 LogPolicy( ) { reset( ); }

 /** Clear every family latch. Called from LogManager::begin( ), so the first
  * routine event of each family after a boot always reaches flash — that
  * record is the proof the subsystem works at all. */
 void reset( );

 /**
  * @brief Decide whether a record earns a slot in flash.
  * @param code  LogCode of the event.
  * @param level LogLevel. LOG_WARN and above are never filtered UNLESS the
  *              code is routed as a fault, in which case the family latch
  *              decides; LOG_FATAL is never filtered at all.
  * @param nowMs Monotonic milliseconds (millis( )). NOT epoch: the epoch
  *              clock falls back to the build stamp and can regress, so it
  *              cannot carry a heartbeat deadline.
  * @return true to persist, false to drop (the console line is emitted either
  *         way — the caller only gates the flash write).
  */
 bool shouldPersist(uint16_t code, uint8_t level, uint32_t nowMs);

 /**
  * @brief Silence the deterministic boot preamble of a hibernation wake.
  *
  * On SIMUT Air every wake is a full boot, so the same eight init records —
  * measured, in this exact order: NET_PROVISIONAL_TIME, APP_UI_LANG_CHANGED,
  * SENSOR_RUNTIME_LOADED, APP_SENSORS_CALIBRATED, TEL_ALARM_LINE_ON,
  * TEL_HTTP_INIT, STO_H5_WIP, APP_READY — were rewritten once a minute. They
  * were 68% of the forensic window and every copy said the same thing.
  *
  * A wake is left with one record rather than none: STO_H5_WIP is emitted a
  * second time by flushWipV5( ) after the window closes, and because a
  * suppressed record does not mark its family as seen, that copy lands as
  * LOGGRP_HIST's first transition — the cycle proving it did the work it woke
  * up for, with the block's record count in ctx. See the note in LogPolicy.cpp.
  *
  * Armed ONLY when the boot came out of hibernation, and disarmed the moment
  * setup( ) ends. Both halves matter: a cold boot keeps the whole preamble,
  * because that burst is the record of a power interruption; and the window
  * closing at the end of setup( ) is what keeps two of those codes
  * (APP_UI_LANG_CHANGED, APP_SENSORS_CALIBRATED) loggable when an operator
  * triggers them later from the CLI or the web.
  *
  * @param on true while the preamble of a wake is being emitted.
  */
 void setQuietPreamble(bool on) { _quietPreamble = on; }

 /** Whether the preamble filter is currently armed — for the CLI and tests. */
 bool quietPreamble( ) const { return _quietPreamble; }

 /**
  * @brief Hourly accounting, called from outside the log path.
  * @return Number of records suppressed since the last report, or 0 when the
  *         hour is not up yet or nothing was suppressed. Reading it clears
  *         the counter, so the caller MUST emit the record it gets back.
  *
  * Cannot be folded into shouldPersist: emitting a record from inside the
  * decision would re-enter LogManager::logCode( ) under its own mutex.
  */
 uint16_t takeSuppressedReport(uint32_t nowMs);

 /** Suppressed records accumulated since the last report — observability for
  * the CLI/metrics without consuming the counter. */
 uint32_t suppressedPending( ) const { return _suppressed; }

private:
 struct GroupState {
 uint32_t lastPersistMs;
 bool faulty; /**< family is in the failed state; next OK is a recovery */
 bool seen;   /**< an OK record from this family already reached flash */
 };

 /** @return the packed rule for `code`, or 0 when it has none (0 is not a
  *  valid rule: SYS_OK is never routed). */
 static uint16_t lookup(uint16_t code);

 /** @return true when `code` belongs to the fixed init sequence every boot
  *  emits. Separate from the family table because these are not a health
  *  signal at all — they are a script, and the only question about them is
  *  whether this boot is the first one to run it. */
 static bool isBootPreamble(uint16_t code);

 /** Count one dropped record and arm the hourly accounting on the first. */
 void countSuppressed(uint32_t nowMs);

 GroupState _grp[LOGGRP_COUNT];
 uint32_t _suppressed;
 uint32_t _lastReportMs;
 bool _reportArmed;    /**< false until the first suppression starts the clock */
 bool _quietPreamble;  /**< this boot is a wake; its init records are redundant */
};
