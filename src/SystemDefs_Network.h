/**
 * @file SystemDefs_Network.h
 * @brief Network resilience, rate-limiter, login state, BT/CLI/AP/cursor.
 * @details WebManager constants (rate-limit, login, sessions, web handler
 * deadline), AP mode timeout, CLI line max, Bluetooth auth buffer,
 * telemetry cursor coalesce. Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>

/* =========================================================================== */
/* NETWORK RESILIENCE CONSTANTS */
/* =========================================================================== */

/**
 * Socket timeout for TCP/TLS operations (ms).
 * Ensures no blocking network call exceeds the watchdog.
 * Must be significantly less than WATCHDOG_TIMEOUT_MS.
 */
constexpr uint32_t NET_SOCKET_TIMEOUT_MS = 4000;

/**
 * Minimum acceptable RSSI for heavy network operations (dBm).
 * Below this threshold, telemetry and uploads are deferred to avoid
 * timeouts that freeze the main loop. Dashboard and sensors continue.
 */
constexpr int32_t RSSI_MIN_THRESHOLD = -78;

/**
 * Minimum interval between MDNS.update() calls (ms).
 * mDNS does not need polling every loop — throttle avoids unnecessary
 * overhead on degraded networks.
 */
constexpr uint32_t MDNS_UPDATE_INTERVAL_MS = 2000;

/**
 * Maximum consecutive WiFi reconnect cycles before entering
 * long dormancy (10-minute backoff). Reset after success.
 */
constexpr uint8_t WIFI_MAX_CONNECT_CYCLES = 5;

/** Long dormancy backoff after exhausting WiFi attempts (ms). */
constexpr uint32_t WIFI_DORMANT_DELAY_MS = 600000;

/**
 * Ceiling for watchdog feeding in long-operation guards (ms).
 *
 * Applies to the repeating timers SendGuard (WebManager) and
 * TelemetryGuard (TelemetryManager). While a blocking operation
 * is in progress (TLS POST, large payload send), the guard feeds the
 * watchdog every 2 s — up to this ceiling. If exceeded, stops feeding
 * (watchdog acts as safety net against real deadlocks) AND signals
 * clean abort via shared flag, so the handler returns with error
 * instead of being killed by the watchdog.
 *
 * Sizing: value must cover the worst-case legitimate operation
 * (TLS handshake + 230 KB send over 2G link) — 60 s with margin.
 * Must be much larger than NET_SOCKET_TIMEOUT_MS to avoid false
 * positives and much smaller than uptime-ms-wrap (~49 d) by definition.
 */
constexpr uint32_t WDT_FEED_MAX_WINDOW_MS = 120000;

/**
 * Ceiling for NTP exponential backoff retry (ms).
 *
 * Sequence applied in NET_CONNECTED_WAIT_NTP: 20 s → 60 s → 5 min → 15 min.
 * After 3 consecutive failures, automatic fallback to pool.ntp.org.
 * Reset to zero (returns to 20 s) after first successful sync.
 */
constexpr uint32_t NTP_MAX_RETRY_DELAY_MS = 900000;

/**
 * Number of consecutive NTP failures before triggering fallback to
 * pool.ntp.org. If the configured server is already pool.ntp.org, fallback
 * is silently ignored.
 */
constexpr uint8_t NTP_FAILS_BEFORE_FALLBACK = 3;

/* ── Rate-limiter (WebManager) ── */

/** Number of rate-limiter slots per IP. */
constexpr uint8_t RATE_LIMIT_SLOTS = 16;

/** TTL of a rate-limiter entry (ms). Expired slot is treated as free. */
constexpr uint32_t RATE_LIMIT_TTL_MS = 900000;

/* ── Login state ── */

/** Number of slots for login state tracking (IP → failCount). */
constexpr uint8_t LOGIN_STATE_SLOTS = 8;

/** Number of HMAC-SHA256 iterations for password hashing.
 * OWASP 2023 recommends ≥600k; NIST recommends ≥10k. The RP2040
 * (Cortex-M0+ @133MHz) with 5000 rounds consumes ~400ms per operation —
 * acceptable for login (infrequent). Every 50 rounds feeds the WDT. */
constexpr uint16_t PASSWORD_HMAC_ROUNDS = 5000;

/* ── Bluetooth auth ── */

/** Maximum password input buffer size via Bluetooth. */
constexpr uint8_t BT_AUTH_BUFFER_MAX = 64;

/** Maximum line size for CLI (USB + BT post-auth).
 * Above this the buffer is discarded to prevent heap DoS from a stream
 * without line terminator. Real CLI lines (e.g. `tel dump json verbose`)
 * are well below this limit. */
constexpr size_t CLI_LINE_MAX = 256;

/* ── Web handlers ── */

/** Deadline for long handlers (history, logs, screenshot) in ms.
 * +50% margin absorbs concurrency/burst. */
constexpr uint32_t WEB_LONG_HANDLER_DEADLINE_MS = 15000;

/* ── AP mode ── */

/** AP mode timeout without clients before rebooting to STA (ms). */
constexpr uint32_t AP_MODE_TIMEOUT_MS = 900000;

/* ── Telemetry cursor ── */

/** Minimum time between telemetry cursor writes to flash (ms).
 * Multiple setLastSentTimestamp calls within this window coalesce into 1 write. */
constexpr uint32_t CURSOR_COALESCE_MS = 5000;
