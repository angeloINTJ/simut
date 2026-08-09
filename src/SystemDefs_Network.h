/**
 * @file SystemDefs_Network.h
 * @brief Network resilience, rate-limiter, login state, BT/CLI/AP/cursor.
 * @details WebManager constants (rate-limit, login, sessions, web handler
 * deadline), AP mode timeout, CLI line max, Bluetooth auth buffer,
 * telemetry cursor coalesce. Sub-header of SystemDefs.h (facade).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
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
 * Overall budget for a TLS handshake (ms), passed to setTLSConnectTimeout.
 *
 * Deliberately LARGER than WATCHDOG_TIMEOUT_MS, which is only safe because of
 * the framework patch in tools/arduino_pico_overrides: upstream the handshake
 * has no overall deadline at all (each _run_until restarts its own timer), so a
 * peer that accepts TCP without completing the handshake wedges Core 0 forever
 * — measured on the bench, and it took a wrong telemetry port to expose it.
 * The patch bounds the loop AND feeds the watchdog inside it; a bounded loop is
 * the only place where feeding does not mask a hang.
 *
 * Sized for a real handshake: BearSSL on a 133 MHz Cortex-M0+ spends seconds on
 * the asymmetric crypto, so the old 4 s ceiling cut off legitimate connections.
 */
constexpr uint32_t NET_TLS_HANDSHAKE_MS = 15000;

/**
 * Minimum acceptable RSSI for heavy network operations (dBm).
 * Below this threshold, telemetry and uploads are deferred to avoid
 * timeouts that freeze the main loop. Dashboard and sensors continue.
 */
constexpr int32_t RSSI_MIN_THRESHOLD = -78;

/* Plausible range for a received-signal RSSI, in dBm. Anything outside it means
 * the cyw43 ioctl is not returning real data — measured on the bench as +4 dBm
 * while the device was completely off the network (host ARP INCOMPLETE, 100%
 * ICMP loss) and WiFi.status( ) still reported WL_CONNECTED. Used by
 * NetworkManager as a second liveness signal, because a non-negative reading
 * otherwise sails past RSSI_MIN_THRESHOLD and confirms health instead of
 * denying it. */
constexpr int32_t RSSI_IMPLAUSIBLE_HIGH = 0;
constexpr int32_t RSSI_IMPLAUSIBLE_LOW  = -120;

#ifdef SIMUT_MDNS
/** Minimum interval between MDNS.update() calls (ms). */
constexpr uint32_t MDNS_UPDATE_INTERVAL_MS = 2000;
#endif

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

/* ── Streaming cooperative pacing ("respiros") ──
 * Long chart/file downloads run to completion inside a single Core-0 handler.
 * Without breaks, Core 0 monopolises the heap allocator, the SPI/QSPI arbiter
 * and the lwIP PBUF pool for the whole transfer, so Core 1 (display + touch)
 * stutters and, on the largest ranges, the live clock freezes behind the
 * download. These knobs slice each transfer into small packets with a breath
 * between them: the watchdog stays fed, PBUFs drain, and Core 1 gets the bus
 * back periodically. The emitted bytes are unchanged — only chunk boundaries
 * (transparent to the HTTP client) and timing shift.
 *
 * WEB_STREAM_CHUNK_SOFT — flush the JSON accumulation buffer once it reaches
 *   this many bytes. Smaller ⇒ more, smaller packets, each followed by a
 *   breath (smoother display, lower PBUF pressure); larger ⇒ fewer TCP writes.
 * WEB_STREAM_BREATH_RECORDS — on high-decimation ranges most decoded records
 *   are dropped without a send; breathe every N decoded records so a long
 *   decimation stride never runs yield-free (keeps the clock alive on MAX).
 * WEB_STREAM_BREATH_DELAY_MS — micro-pause after each flushed packet; lets
 *   lwIP drain the PBUF pool and hands the heap/SPI arbiter to Core 1. */
constexpr size_t   WEB_STREAM_CHUNK_SOFT      = 512;

/* WEB_SEND_STALL_MS — how long safeSend waits for the socket's send buffer
 * to absorb one slice before dropping the client. The wait loop feeds the
 * watchdog, so this is a POLICY bound on slow readers, not a survival bound:
 * 528 B within 4 s is a 132 B/s floor that any real client clears. A reader
 * below it used to park a single lwIP write past the WDT window — the
 * SendGuard's 2 s timer feeds demonstrably never reached the watchdog during
 * those blocks (autopsy: "no feed in WDT window" with C0=[WEB_SEND]) — and
 * the device rebooted mid-response. Now the stalled client is the one that
 * pays. */
constexpr uint32_t WEB_SEND_STALL_MS          = 4000;
constexpr uint32_t WEB_STREAM_BREATH_RECORDS  = 64;

/* Largest history answer served in one response, in estimated payload
 * bytes. Past this the handler refuses and asks the client to fetch the
 * range in slices (?from=&to=).
 *
 * The number is not about memory — the response streams. It is about how
 * long Core 0 stays inside sendContent: ~55 KB/s on this link makes 64 KB
 * about one second, against a hardware watchdog that fires after 8.4 s of
 * unfed loop. A 530 KB answer took ten seconds, and three of them queued
 * by a browser that polls while it charts reset the device every time. */
constexpr uint32_t WEB_HISTORY_SINGLE_MAX     = 64u * 1024u;

/* Calibration for the payload estimate, measured on real V5 files:
 * 5.4 B per record at 11 channels, 5.7 at 6, and ~126 B per emitted JSON
 * point. The old figure was 9 B/record from the V4 era — it made the
 * estimate read barely half the truth, and a client sizing its slices
 * from it asked for windows twice as large as it intended. */
constexpr uint32_t WEB_HISTORY_BYTES_PER_RECORD = 6u;
constexpr uint32_t WEB_HISTORY_BYTES_PER_POINT  = 126u;
constexpr uint32_t WEB_STREAM_BREATH_DELAY_MS = 2;

/* ── AP mode ── */

/** AP mode timeout without clients before rebooting to STA (ms). */
constexpr uint32_t AP_MODE_TIMEOUT_MS = 900000;

/* ── Telemetry cursor ── */

/** Minimum time between telemetry cursor writes to flash (ms).
 * Multiple setLastSentTimestamp calls within this window coalesce into 1 write. */
constexpr uint32_t CURSOR_COALESCE_MS = 5000;
