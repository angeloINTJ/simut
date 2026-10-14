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

/** First delay between reconnect attempts (ms); the ladder doubles from here. */
constexpr uint32_t WIFI_RECONNECT_BASE_MS = 5000;

/**
 * Maximum consecutive WiFi reconnect cycles before entering
 * long dormancy (10-minute backoff). Reset after success.
 */
constexpr uint8_t WIFI_MAX_CONNECT_CYCLES = 5;

/** Long dormancy backoff after exhausting WiFi attempts (ms). */
constexpr uint32_t WIFI_DORMANT_DELAY_MS = 600000;

/**
 * How many dormant-length waits are served before the ladder restarts.
 *
 * Dormancy exists so a network that is genuinely gone is not chased every five
 * seconds. It must not be a one-way door, though: it used to be pinned for the
 * rest of the boot, because the counter that opened it was never decayed, so an
 * access point that came back was found on a ten-minute grid at best and the
 * device looked dead to anyone watching. Three waits gives the absent-network
 * case its half hour of quiet and then costs one fast burst to check.
 */
constexpr uint8_t WIFI_DORMANT_MAX_WAITS = 3;

/**
 * Failed scans tolerated before associating without one (see NetworkManager).
 *
 * The reconnect path used to require the SSID to appear in a scan, while the
 * boot path called WiFi.begin( ) with no such condition. That asymmetry is the
 * whole bug: a scan hears a probe response inside a dwell of tens of
 * milliseconds per channel, whereas the join has the driver's own retries
 * behind it and tolerates a far worse signal — so at the edge of a cell the
 * device would scan every five seconds forever and never once try to
 * associate, and only a reboot (which takes the unconditional path) brought it
 * back. The scan is kept as an optimisation: two cheap misses are allowed to
 * spare a futile twenty-second association attempt, and then we attempt it
 * anyway. This is also what makes a hidden SSID reconnect at all — it never
 * appears in a scan, at any signal strength.
 */
constexpr uint8_t WIFI_SCANS_BEFORE_BLIND_JOIN = 2;

/**
 * Deadline for an asynchronous scan to complete (ms).
 *
 * cyw43_wifi_scan( ) sets wifi_scan_state = 1 BEFORE issuing the low-level
 * scan and never clears it if that call fails, and nothing in the SDK times the
 * state out. So a failed start — or one lost escan-complete event — leaves
 * WiFi.scanComplete( ) returning -1 forever. The state machine used to return
 * on that value with nothing to bound the wait, which made NET_SCANNING_RETRY a
 * terminal state that only a reboot left. A real scan of all channels finishes
 * in a few seconds; this is generous by threefold and still bounded.
 */
constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 15000;

/**
 * A radio that will not take a join, and what is done about it.
 *
 * WiFi.begin( ) queues the join and returns at once; the association finishes
 * in the background. It answers WL_IDLE_STATUS only when the radio would not
 * take the request at all: cyw43_wifi_join( ) failing for the whole wait in the
 * framework's CYW43::begin( ). A weak signal is not that, since a weak link
 * still takes the request and fails to associate afterwards. A refusal is the
 * driver or the chip in a state a join cannot leave.
 *
 * Field log of 2026-09-30 22:13, a device at -79 dBm: the link dropped, two
 * scans never finished, and the blind join that followed was refused. The
 * framework retried it for 15 s, Core 0 sat past the 8.4 s watchdog, and the
 * watchdog rebooted the device. The reboot is what brought the radio back, but
 * it came from the watchdog: at whatever point Core 1 had reached, with nothing
 * persisted first. The framework patch (tools/arduino_pico_overrides,
 * cyw43_join_budget.patch) now bounds that wait. These two constants keep the
 * recovery and plan it:
 * - after WIFI_JOIN_REFUSALS_BEFORE_RESTART refusals in a row,
 *   NetworkManager logs SYS_WIFI_CONNECT ctx 3 and reboots by safeReboot( );
 * - never within WIFI_RADIO_RESTART_MIN_UPTIME_MS of boot, so a radio that
 *   stays broken costs one reboot per half hour, not one per attempt.
 */
constexpr uint8_t  WIFI_JOIN_REFUSALS_BEFORE_RESTART = 3;
constexpr uint32_t WIFI_RADIO_RESTART_MIN_UPTIME_MS = 30UL * 60UL * 1000UL;

/**
 * A radio that does not answer at all, and how it is told apart from one that
 * merely lost a scan.
 *
 * Two scans that never finish precede two different radios. One answered the
 * scan ioctl and lost the completion event: the driver's wifi_scan_state is
 * stuck, the chip is fine, and the blind join of WIFI_SCANS_BEFORE_BLIND_JOIN
 * is the right escape (the 2026-09-08 device, 3 h 41 min dark). The other did
 * not answer the ioctl: the chip is silent on the SDPCM bus, and WiFi.begin( )
 * on it is not a queued join. On the first join of a boot CYW43::begin( )
 * issues 15 synchronous ioctls BEFORE the wait that cyw43_join_budget.patch
 * bounds — the allmulti iovar, the two of the multicast filter (one on later
 * joins, which find the address already there; two more with IPv6), then the
 * twelve of cyw43_ll_wifi_join( ) for a WPA2 network (eleven for an open one),
 * which ignores every return value and keeps going — and seven more in
 * noLowPowerMode( ) after it. Each one against a silent chip runs the driver's
 * response timeout or its SDPCM credit stall, and both are 1 s here:
 * CYW43_IOCTL_TIMEOUT_US defaults to 500 ms in the driver, but pico-sdk's
 * cyw43_configport.h sets it to 1,000,000 us first, and the prebuilt driver of
 * this framework (cyw43_ll.c.o in lib/rp2040/liblwip.a, cyw43-driver dd75682)
 * carries 1,000,000 and no 500,000 (read 2026-10-13). That is about 15 s with
 * no watchdog_update( ) in reach of this firmware: past the 8388 ms ceiling,
 * every time.
 *
 * Field log of 2026-10-11 09:28, on v2.11.0 (the join-wait patch on board):
 * link lost 09:27:18, scans abandoned 09:27:40 and 09:28:01 (SYS_WIFI_SCAN
 * ctx 1, 2), blind join 09:28:01 (SYS_WIFI_CONNECT ctx 1), then the autopsy —
 * ctx 202 (Core 0 in MOD_WIFI), 1008 (Core 1 in DISPLAY), 9749 (3 d 23 h 49 m
 * up), 2740 (web server idle). The same sequence as 2026-09-30. The two radios
 * look identical to scanComplete( ), which answers -1 for both, but not to the
 * clock: WiFi.scanNetworks(true) returns in milliseconds when the chip answers
 * the "escan" iovar and only after the driver's own timeout when it does not —
 * and the first scan of 2026-10-11 was abandoned 16 s after it was logged, not
 * 15, which is the deadline plus the 1 s credit stall (LogPolicy latched the
 * second scan's own line; the 21 s between the two abandon records fit the
 * same 16 plus the ladder's 5). So the start of every scan is timed:
 * - a start that takes WIFI_SCAN_START_SILENT_MS or longer is a silent radio:
 *   a quarter of the driver's 1 s timeouts, and far above the one round trip
 *   of a chip that answers (not yet timed on the bench: only an abandoned scan
 *   logs how long its start took). A scan started on a silent radio that then
 *   never finishes does NOT count toward the blind join;
 * - after WIFI_SILENT_SCANS_BEFORE_RESTART of those in a row, NetworkManager
 *   logs SYS_WIFI_CONNECT ctx 4 and reboots by safeReboot( ) — the log
 *   persisted and Core 1 parked, instead of the watchdog's reboot — under the
 *   same WIFI_RADIO_RESTART_MIN_UPTIME_MS guard as the refusals. Inside the
 *   guard the ladder keeps scanning and never blind-joins: each attempt costs
 *   at most one ioctl timeout, which the watchdog window absorbs.
 * The restart recovers the chip the way the watchdog did on 2026-09-30 and
 * 2026-10-11; resetting the radio alone (cyw43_arch_deinit/init under a live
 * lwIP) is a larger change than this fix, and is deliberately not attempted.
 */
constexpr uint32_t WIFI_SCAN_START_SILENT_MS = 250;
constexpr uint8_t  WIFI_SILENT_SCANS_BEFORE_RESTART = 2;

/**
 * One access point, as the "pick a network" list needs it.
 *
 * Copied OUT of the driver's results, because WiFi.scanDelete( ) frees those
 * and the web reads the list one request later. Twelve of them is 432 B of
 * RAM held for the life of the device — enough for any room somebody is
 * standing in with a Pico W, and the cheapest bound that is not a guess.
 */
constexpr uint8_t WIFI_SCAN_MAX_NETS = 12;

struct WifiNet {
	char    ssid[33];   /**< 32 + terminator; a hidden AP reports ""       */
	int16_t rssi;       /**< dBm, negative                                 */
	uint8_t enc;        /**< the driver's auth mode, 0 = open              */
	uint8_t channel;
};

/**
 * Ceiling for watchdog feeding in long-operation guards (ms).
 *
 * Applies to the SendGuard repeating timer (WebManager). While a blocking
 * operation is in progress (TLS POST, large payload send), the guard feeds the
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

/**
 * Size of NTP correction above which the sync is logged as a warning rather
 * than information.
 *
 * A correction is normal after a power cut — it is the length of the outage.
 * What it must not be is routine, because the same number also measures how
 * far the provisional clock had drifted from real time, and every record
 * written before the sync carries that error. One hour is chosen to sit far
 * above ordinary drift and far below the 4 h 43 min seen on 2026-08-14, when a
 * bad seed filed 121 measurements under the following day.
 *
 * The correction is applied either way: refusing a large one would leave the
 * timestamps wrong forever after a genuinely long outage. This only makes the
 * event visible in `show system log` after the fact.
 */
constexpr int32_t NTP_SUSPECT_DELTA_S = 3600;

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
 * (Cortex-M0+ @133MHz) spends ~645 ms on 5000 rounds, with Core 0 held for
 * all of it: on the rig on 2026-10-02 (v2.9.0), `user pass` took 646 ms
 * against 5 ms for `user perm`, and a /metrics read with Basic 699 ms against
 * 49 ms with a cookie. Acceptable for login (infrequent); SECURITY.md §3 has
 * the trade-off. Every 50 rounds feeds the WDT. */
constexpr uint16_t PASSWORD_HMAC_ROUNDS = 5000;

/* ── Authentication lockout (web + Bluetooth) ── */

/** Ceiling for the consecutive-failure counter.
 * The counter saturates here instead of running free. Two reasons, and the
 * second one is the bug that made this a constant: (1) beyond 9 the penalty is
 * already at its ceiling, so counting further buys nothing; (2) the penalty is
 * computed by shifting, and an unbounded counter walks the shift straight off
 * the end of a 32-bit unsigned. Measured for the web path as it stood on
 * 2026-09-07: failCount 29, 30 and 31 produce a penalty of exactly ZERO —
 * (1U << 29) * 1000 is 536870912000, which is 125 * 2^32 — so an attacker who
 * sits through the escalation is handed three free attempts, and from 32 on
 * the shift itself is undefined. 12 keeps the counter meaningful in the
 * sessions listing while staying far from either cliff. */
constexpr uint8_t AUTH_FAIL_CAP = 12;

/** Ceiling of the lockout penalty (ms). */
constexpr uint32_t AUTH_LOCKOUT_MAX_MS = 300000;

/** Exponential backoff for a failed authentication: (1 << failCount) seconds,
 * capped at AUTH_LOCKOUT_MAX_MS. failCount is the count AFTER the failure, so
 * the first one costs 2 s and the ninth reaches the 300 s ceiling.
 *
 * Pure and header-inline so the native tests can call it without a device.
 * The shift is clamped before it happens rather than the product afterwards:
 * clamping afterwards is what let the overflow through, because a wrapped
 * product can land below the ceiling and read as a legitimate short penalty. */
inline uint32_t authLockoutMs(uint8_t failCount) {
	uint8_t n = (failCount > AUTH_FAIL_CAP) ? AUTH_FAIL_CAP : failCount;
	uint32_t ms = 1000UL << n;
	return (ms > AUTH_LOCKOUT_MAX_MS) ? AUTH_LOCKOUT_MAX_MS : ms;
}

/** How many times unauthenticated traffic may extend the hibernation timer,
 * counted per boot. Only SIMUT Air installs the activity callback, so on every
 * other image this is inert; the constant lives here because WebManager is
 * generic and must not know which image it is compiled into.
 *
 * Three at the default 300 s idle is up to 15 minutes for an operator to
 * finish logging in after the device wakes — and the same 15 minutes, once,
 * for an anonymous poller that would otherwise have held the radio up forever
 * (V-03). */
constexpr uint8_t WEB_PREAUTH_MAX_EXT = 3;

/* ── Bluetooth auth ── */

/** Maximum password input buffer size via Bluetooth. */
constexpr uint8_t BT_AUTH_BUFFER_MAX = 64;

/** How long after boot the device stays DISCOVERABLE over Bluetooth (ms).
 *
 * Finding V-01b, decision D-1, option (b). SerialBT::begin calls
 * gap_discoverable_control(1) and never turns it off, so an alpha or Air unit
 * advertised itself to every scan in range for its entire uptime — and pairing
 * takes no confirmation on the device, so the password prompt was the only
 * barrier between a passer-by's scan and the CLI.
 *
 * Closing discovery does NOT close connectability: a phone that has already
 * paired, or anyone who noted the address, still connects. That is the
 * deliberate limit of this option — it removes the device from casual scans,
 * and the lockout of V-01a is what handles someone who is actually trying.
 * Option (a), consent on pairing, needs a framework patch and is the next step
 * if these units ever go somewhere public.
 *
 * Five minutes is sized against the job: pairing a phone with a device you are
 * standing next to. Rebooting reopens the window, which is the recovery path —
 * cheaper in flash than a CLI command, and available on a unit whose CLI is
 * exactly what you cannot reach yet. */
constexpr uint32_t BT_DISCOVERABLE_MS = 300000;

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

/* Bytes of one /api/screen_stream frame buffered before it is handed to
 * safeSend, or 0 for the per-strip writes the route shipped with.
 *
 * A frame is 30 strips and the old code made two safeSend calls per strip, each
 * paying a client setTimeout, a feedWatchdog and a waitSendRoom before the first
 * byte moves. The payload is 3-13 kB, so one buffer swallows almost any frame
 * whole. Sized to the measurement, not to the worst case: a strip that falls
 * back to raw is 5,123 B and goes out on its own rather than dictating a buffer
 * every frame would carry.
 *
 * 1 kB and not more, and that is the counter-intuitive part. With the blocking
 * read the frame got monotonically better with a bigger buffer (61 writes to 5
 * was worth 80 ms). With the DMA pipeline it inverts: the transfer of the next
 * strip runs while Core 0 sends this one, so a big buffer means Core 0 sits
 * idle for most strips and then blocks the bus for one long flush. Measured on
 * the dashboard (rig, 2026-09-19): 0 -> 222.2 ms, 512 -> 208.3, 1024 -> 210.7,
 * 2048 -> 211.7, 4096 -> 232.3. 512..2048 is a plateau and 1024 sits on it with
 * a third of the write calls of 512. See ESPELHO_DELTA.md §11. */
constexpr size_t   SCREEN_SEND_COALESCE     = 1024;

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

/* WEB_WRITE_TIMEOUT_MS — how long one write of a response may go without
 * handing lwIP any of its bytes (ClientContext::_timeout_ms, set before every
 * streamed body). Over plain TCP, WEB_SEND_STALL_MS above has already waited
 * for the send buffer's room, so this runs out when the room is there and
 * the write still cannot finish; over TLS nothing waits for room first. A
 * write that runs out comes back short, and the framework now ends that
 * response (override 2m, webserver_short_write.patch). On the bench
 * (2026-10-14, release 2.11.1, /api/status every 0.2 s) 24 of 2,000 replies
 * lost whole writes that way at night, each one 1.2 to 1.8 s late, and 1 of
 * 2,000 in the morning. Why a write stalls is still open: `show net status`
 * says whether lwIP ran out of segments or memory. The 500 ms dates from
 * v1.0.0, when the wait did not feed the watchdog; the override 2d-bis feeds
 * on both sides of it now. */
constexpr uint32_t WEB_WRITE_TIMEOUT_MS       = 500;
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
 * Every delivery inside this window coalesces into 1 write. */
constexpr uint32_t CURSOR_COALESCE_MS = 5000;
