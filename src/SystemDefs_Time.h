/**
 * @file SystemDefs_Time.h
 * @brief Timing constants + safeCopy + wrap-safe millis helpers.
 * @details Boot timing, sensor timeouts, UI timing, safeCopy, timeReached/
 * timeSince/timeRemaining. Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>

/* =========================================================================== */
/* BOOT TIMING CONSTANTS */
/* =========================================================================== */

/** Time the user must hold touch to enter AP Mode (ms). */
constexpr uint32_t AP_HOLD_DURATION_MS = 3000;

/** Detection window for touch start at boot (ms). */
constexpr uint32_t AP_DETECT_WINDOW_MS = 3500;

/** Delay between boot stages for visual feedback (ms). */
constexpr uint32_t BOOT_STEP_DELAY_MS = 800;

/** Polling delay during boot wait loops (ms). */
constexpr uint32_t BOOT_POLL_INTERVAL_MS = 50;

/**
 * Hardware watchdog timeout in milliseconds.
 * Sized to cover the worst-case Flash write + WiFi scan
 * without triggering false reset during legitimate I/O operations.
 */
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 15000;

/** Missed touches tolerated before canceling AP hold. */
constexpr int AP_HOLD_MAX_MISSED = 5;


/* =========================================================================== */
/* SENSOR TIMEOUTS */
/* =========================================================================== */

/**
 * DS18B20 conversion time before reading result (ms).
 *
 * Datasheet: worst case 12-bit = 750ms. Lower resolutions complete sooner
 * (9-bit=94ms, 10-bit=188ms, 11-bit=375ms). Since cfg.ds18Resolution is
 * configurable (9..12), we use the worst case as safe fallback — cost is
 * up to ~600ms extra in the critical path when 9-bit is selected, but
 * it simplifies the state machine (single fixed timer) and tolerates any
 * flash GC/interrupt variance.
 */
constexpr uint32_t DS18B20_CONVERSION_TIME_MS = 750;

/**
 * Single-shot DHT22 read timeout (ms).
 *
 * Datasheet: full cycle ~20ms. The 150ms value gives margin against
 * PIO state machine scheduling delays, without unnecessarily lengthening
 * the scan. If the sensor has not responded in 150ms it is considered
 * absent or faulty.
 */
constexpr uint32_t DHT22_READ_TIMEOUT_MS = 150;


/* =========================================================================== */
/* BOOT & UI TIMING */
/* =========================================================================== */

/** Interval between "..." animation increments on the boot waiting screen
 * (AppManager waiting for WiFi/NTP). One dot every 800 ms gives visual feedback
 * without redraw noise. */
constexpr uint32_t BOOT_WAIT_DOT_INTERVAL_MS = 800;

/** Auto-rotation interval per slot on the dashboard when 2+ alarms are
 * simultaneously active. 3 s gives the user time to read each slot. */
constexpr uint32_t ALARM_ROTATE_INTERVAL_MS = 3000;

/** Half-period of alarm flash on the dashboard (ms). Full cycle = 2×
 * this value (on→off→on). 600 ms yields ~0.83 Hz — visible but not
 * aggressive. */
constexpr uint32_t ALARM_FLASH_INTERVAL_MS = 600;

/** Duration of the "Web: <user>" toast in the dashboard header after a
 * successful web login (ms). */
constexpr uint32_t WEB_NOTIFY_DURATION_MS = 5000;



/* =========================================================================== */
/* SAFE STRING COPY UTILITY */
/* =========================================================================== */

/**
 * @brief Copies a string to a fixed-size buffer with guaranteed null-termination.
 *
 * Replaces the unsafe strncpy-without-terminator pattern.
 * Typical usage: safeCopy(cfg.deviceName, source, sizeof(cfg.deviceName));
 *
 * @param dst Destination buffer.
 * @param src Source string (can be nullptr — results in empty string).
 * @param dstSize Total size of destination buffer (including '\0').
 */
inline void safeCopy(char* dst, const char* src, size_t dstSize) {
 if (dstSize == 0) return;
 if (!src) {
 dst[0] = '\0';
 return;
 }
 strncpy(dst, src, dstSize - 1);
 dst[dstSize - 1] = '\0';
}


/* =========================================================================== */
/* WRAP-SAFE MILLIS() COMPARISON */
/* =========================================================================== */

/**
 * @brief Safely checks if a deadline (based on millis()) has been reached.
 *
 * ALWAYS use this function instead of `millis() > deadline` or `millis() < deadline`.
 * The millis() counter is a uint32_t that wraps around every ~49.7 days; direct
 * comparison inverts the result after wrap, causing eternal timeouts
 * (lockouts that never expire, handlers that freeze, etc.).
 *
 * Subtraction in signed arithmetic handles wraparound correctly:
 * - returns >= 0 when now has reached/passed deadline
 * - returns < 0 when not yet reached
 *
 * Typical usage:
 * if (timeReached(_lockoutUntil)) forceDashboard(); // unlock
 * if (!timeReached(_deadline)) _pending = true; // still waiting
 *
 * @param deadline Absolute millis() value to compare against "now".
 * @return true if millis() has already reached or passed deadline (wrap-safe).
 */
inline bool timeReached(uint32_t deadline) {
 return (int32_t)(millis( ) - deadline) >= 0;
}

/**
 * @brief Checks if an interval has elapsed since a start timestamp.
 *
 * Wrap-safe equivalent to `millis() - start >= duration` — uses subtraction
 * in int32_t to correctly handle millis() wraparound (~49.7 days).
 *
 * Typical usage:
 * if (timeSince(_lastPoll, 1000)) doPoll(); // 1s since poll
 * if (!timeSince(_lastTouch, 30000)) return; // < 30s since touch
 *
 * Difference from timeReached(): this helper compares elapsed intervals
 * since an event; timeReached() is for absolute deadlines
 * (e.g. _lockoutUntil).
 *
 * @param start millis() of the initial event.
 * @param duration Interval (ms) after which it returns true.
 * @return true if millis() - start has already reached or passed duration.
 */
inline bool timeSince(uint32_t start, uint32_t duration) {
 return (int32_t)(millis( ) - start) >= (int32_t)duration;
}

/**
 * @brief Time remaining until deadline, in milliseconds. Wrap-safe.
 *
 * Returns 0 if the deadline has already passed. Replaces the unsafe pattern
 * `deadline - millis()`, which suffers underflow (returns huge value) after
 * millis() wrap and produces absurd "seconds remaining" in the UI.
 *
 * @param deadline Absolute millis() value to compare against "now".
 * @return Milliseconds until deadline, or 0 if already reached.
 */
inline uint32_t timeRemaining(uint32_t deadline) {
 int32_t diff = (int32_t)(deadline - millis( ));
 return (diff > 0) ? (uint32_t)diff : 0;
}
