/**
 * @file LogPolicy.cpp
 * @brief Routing table and state machine for the edge-triggered log filter.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 *          SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "LogPolicy.h"
#include "SystemDefs_Logging.h"
#include <string.h>

/* ===========================================================================
 * THE TABLE
 *
 * Only routine codes appear here. Anything absent is persisted unconditionally,
 * which is the safe default: adding a new LogCode never silently disappears —
 * it has to be listed here on purpose.
 *
 * The fault side of each family is listed for one reason only: to arm the latch
 * so the NEXT success is recognised as a recovery and written. Those codes were
 * already being persisted before this filter existed and still are.
 * =========================================================================== */
static const uint16_t EDGE_RULES[] = {
 /* ── Telemetry ───────────────────────────────────────────────────────── */
 LOGPOL_RULE(SYS_TEL_SENT,             LOGGRP_TEL,  0),
 LOGPOL_RULE(SYS_TEL_MQTT_PUB,         LOGGRP_TEL,  0),
 LOGPOL_RULE(SYS_TEL_MQTT_CONN,        LOGGRP_TEL,  0),
 LOGPOL_RULE(SYS_TEL_FAIL,             LOGGRP_TEL,  1),
 LOGPOL_RULE(SYS_TEL_RETRY,            LOGGRP_TEL,  1),
 LOGPOL_RULE(SYS_TEL_QUEUE,            LOGGRP_TEL,  1),
 LOGPOL_RULE(SYS_TEL_MQTT_DISC,        LOGGRP_TEL,  1),
 LOGPOL_RULE(TEL_BACKOFF_SUPPRESSED,   LOGGRP_TEL,  1),
 /* A certificate the transport cannot use is re-read on every connect
  * attempt, so it repeats at the pace of the retries — the same shape as a
  * refused upload, and the same reason to say it once. TEL_CERT_MISSING is
  * deliberately absent: it is INFO, fires once at init, and on a plain-HTTP
  * device it is the normal state rather than a fault. */
 LOGPOL_RULE(TEL_CERT_EMPTY,           LOGGRP_TEL,  1),
 LOGPOL_RULE(TEL_CERT_READ_ERR,        LOGGRP_TEL,  1),

 /* ── Telemetry, alarm line ───────────────────────────────────────────── */
 LOGPOL_RULE(TEL_ALARM_SENT,           LOGGRP_TELALM, 0),
 LOGPOL_RULE(TEL_ALARM_ACK,            LOGGRP_TELALM, 0),
 LOGPOL_RULE(TEL_ALARM_FAIL,           LOGGRP_TELALM, 1),
 LOGPOL_RULE(TEL_ALARM_DROP,           LOGGRP_TELALM, 1),

 /* ── History / storage ───────────────────────────────────────────────── */
 LOGPOL_RULE(APP_HISTORY_SAVED,        LOGGRP_HIST, 0),
 LOGPOL_RULE(STO_H5_SEALED,            LOGGRP_HIST, 0),
 LOGPOL_RULE(STO_H5_WIP,               LOGGRP_HIST, 0),
 /* The recovery half of APP_HIST_NO_TIME_REF, which was routed as a fault
  * with nothing to clear it by name. Without this the clock coming back was
  * an ordinary INFO that happened to pass; now it is the transition. */
 LOGPOL_RULE(APP_HIST_TIME_REF_RECOVERED, LOGGRP_HIST, 0),
 LOGPOL_RULE(SYS_STORAGE_FAIL,         LOGGRP_HIST, 1),
 LOGPOL_RULE(APP_HIST_NO_TIME_REF,     LOGGRP_HIST, 1),
 LOGPOL_RULE(APP_HIST_NO_SCHEMA,       LOGGRP_HIST, 1),
 LOGPOL_RULE(APP_HIST_SCHEMA_MISMATCH, LOGGRP_HIST, 1),
 LOGPOL_RULE(STO_WRITE_FAILED,         LOGGRP_HIST, 1),
 LOGPOL_RULE(STO_SCHEMA_MISMATCH,      LOGGRP_HIST, 1),

 /* ── Network ─────────────────────────────────────────────────────────── */
 LOGPOL_RULE(SYS_WIFI_CONNECT,         LOGGRP_NET,  0),
 LOGPOL_RULE(SYS_IP_ACQUIRED,          LOGGRP_NET,  0),
 LOGPOL_RULE(SYS_WIFI_DISCONNECT,      LOGGRP_NET,  1),
 LOGPOL_RULE(NET_CONNECT_TIMEOUT,      LOGGRP_NET,  1),
 /* Both re-fire on every reconnect attempt: the announcement is retried each
  * time the link comes up, and a device with no SSID keeps saying so. */
 LOGPOL_RULE(NET_MDNS_FAIL,            LOGGRP_NET,  1),
 LOGPOL_RULE(NET_SSID_MISSING,         LOGGRP_NET,  1),
};

/* ───────────────────────────────────────────────────────────────────────────
 * WHAT IS DELIBERATELY NOT HERE, and why — the audit of 2026-09-07.
 *
 * Sensors (ERR_SENSOR_*). SensorManager already tracks health per instance,
 * and one family latch across all of them would let a failing sensor hide
 * another one recovering. That reasoning predates this file and still holds.
 *
 * Security (SEC_*) and configuration changes. Never filtered by policy: the
 * log is the only account of who did what. SEC_CONFIG_CHANGED does arrive in
 * bursts — three in one second while a commit writes three sections — but
 * those three are three different facts, not one repeated.
 *
 * Web client disconnects (WEB_DISCONNECT_FILE, WEB_DISCONNECT_HISTORY,
 * WEB_CLIENT_DISCONNECT). They repeat under a flaky browser, but a client
 * hanging up is not the server entering a failed state, so there is no
 * recovery event that would ever clear the latch — a family here would go
 * quiet after the first one and stay quiet for the rest of the boot. If they
 * ever need taming it should be by their own rule, not by this one.
 *
 * The stall codes (APP_YIELD_STUCK, APP_CORE1_DEAD, APP_FLASH_BUSY). Each one
 * is a distinct incident with its own context, and they are exactly what an
 * autopsy reads. Left unconditional.
 * ─────────────────────────────────────────────────────────────────────────── */

static const uint8_t EDGE_RULE_COUNT = sizeof(EDGE_RULES) / sizeof(EDGE_RULES[0]);

/* ===========================================================================
 * THE BOOT PREAMBLE
 *
 * The fixed sequence setup( ) emits on its way up. On a normal device it costs
 * eight records once; on SIMUT Air, where every wake is a full boot, it cost
 * eight records A MINUTE. Measured on the rig over 108 boots: these eight were
 * 68.2% of the whole 1600-record window, in this exact order every time —
 *
 *   524 441 590 407 549 540 567 404
 *
 * — which is what a script looks like, not what a system telling you something
 * looks like.
 *
 * The list is the measured signature, nothing broader. Two of the codes can
 * also be raised at runtime by an operator (APP_UI_LANG_CHANGED from the CLI
 * or the language parser, APP_SENSORS_CALIBRATED from `calib` and from
 * /api/calib), and those must never be silenced — which is why the filter is
 * armed for the duration of setup( ) only. A code appearing here is NOT a
 * statement that it is unimportant; it is a statement that its FIRST copy,
 * written by the cold boot, already said it.
 * =========================================================================== */
static const uint16_t BOOT_PREAMBLE[] = {
 NET_PROVISIONAL_TIME,     /* 524 */
 APP_UI_LANG_CHANGED,      /* 441 — runtime-reachable; see the note above */
 SENSOR_RUNTIME_LOADED,    /* 590 */
 APP_SENSORS_CALIBRATED,   /* 407 — runtime-reachable; see the note above */
 TEL_ALARM_LINE_ON,        /* 549 */
 TEL_HTTP_INIT,            /* 540 */
 STO_H5_WIP,               /* 567 — see the note below */
 APP_READY,                /* 404 */
 APP_READY_AP,             /* 405 — the same record on a device forced into AP */
};

/* STO_H5_WIP deserves its own note, because it is the reason a wake still
 * leaves a trace and not silence.
 *
 * There are two INFO call sites. One is inside setup( ) — the boot adopting or
 * resuming the open block — and it is the one this list silences. The other is
 * flushWipV5( ), which runs during the cycle, AFTER the window has closed.
 * Because a suppressed preamble record does not mark its family as seen, that
 * second one arrives as LOGGRP_HIST's first transition of the boot and IS
 * written, carrying the block's record count in ctx.
 *
 * So a wake writes one record instead of eight, and the one it keeps is the
 * one describing the work it woke up to do. That is a better outcome than
 * silence: a healthy Air still proves itself once per cycle. */

static const uint8_t BOOT_PREAMBLE_COUNT =
 sizeof(BOOT_PREAMBLE) / sizeof(BOOT_PREAMBLE[0]);

bool LogPolicy::isBootPreamble(uint16_t code) {
 for (uint8_t i = 0; i < BOOT_PREAMBLE_COUNT; i++) {
 if (BOOT_PREAMBLE[i] == code) return true;
 }
 return false;
}

/* The packing assumes every routed code fits in 12 bits. Nothing in the enum
 * comes close today (the highest is 999), but a future code above 4095 would
 * silently alias onto another family instead of failing to build. */
static_assert(ERR_UNKNOWN < 0x1000, "LogCode outgrew the 12-bit field in LOGPOL_RULE");

uint16_t LogPolicy::lookup(uint16_t code) {
 for (uint8_t i = 0; i < EDGE_RULE_COUNT; i++) {
 if (LOGPOL_RULE_CODE(EDGE_RULES[i]) == code) return EDGE_RULES[i];
 }
 return 0;
}

/** Wrap-safe elapsed test, same signed-subtract idiom as timeSince( ) in
 * SystemDefs_Time.h. Correct across the millis( ) wrap for any interval below
 * ~24 days, which both cadences here are by three orders of magnitude. */
static inline bool elapsed(uint32_t now, uint32_t since, uint32_t interval) {
 return (int32_t)(now - since) >= (int32_t)interval;
}

void LogPolicy::reset( ) {
 /* Every field's initial value is the all-zero one: no family seen, none
  * faulty, no anchor, nothing suppressed. */
 memset(_grp, 0, sizeof(_grp));
 _suppressed = 0;
 _lastReportMs = 0;
 _reportArmed = false;
 /* Cleared here on purpose: reset( ) runs from LogManager::begin( ), and the
  * caller arms the preamble filter right after, only when it knows this boot
  * came out of hibernation. Defaulting to "log everything" means a build that
  * forgets to arm it behaves exactly as before. */
 _quietPreamble = false;
}

bool LogPolicy::shouldPersist(uint16_t code, uint8_t level, uint32_t nowMs) {
 /* Nothing filters a fatal. It is the record the forensic window exists for,
  * and the latch below must never be able to swallow the line that explains a
  * reset. Checked first so no table entry can ever outrank it. */
 if (level >= LOGPOL_LEVEL_FATAL) return true;

 const uint16_t rule = lookup(code);

 /* Failures are edge-triggered too, and this branch stays BEFORE the level
  * shortcut below for two reasons. SYS_TEL_RETRY is a LOG_WARN, so a
  * level-first order would return early and never mark the family as failed —
  * and the recovery that follows would then look like just another routine
  * success and be dropped, which is the one record that had to survive. And
  * SYS_TEL_FAIL is a LOG_ERROR, so the shortcut would also make the
  * suppression below unreachable for the very code that motivated it. */
 if (rule && LOGPOL_RULE_FAULT(rule)) {
 GroupState& gf = _grp[LOGPOL_RULE_GROUP(rule)];

 /* Healthy -> failing is the transition, and it is written. */
 if (!gf.faulty) {
 gf.faulty = true;
 gf.lastPersistMs = nowMs;
 return true;
 }

 /* Already failing. Every attempt after the first says the same thing, and
  * a collector that has been refusing connections for an hour used to fill
  * the whole window saying so — the pair SYS_TEL_FAIL + SYS_TEL_RETRY,
  * once per attempt, at whatever pace the backoff allows.
  *
  * The latch is per FAMILY, not per code, because those two codes
  * ALTERNATE: a per-code latch would have let both through every time and
  * suppressed nothing. The cost is that a second failure mode inside the
  * same family stays off flash until the hour is up; the console line is
  * still emitted for every one of them, and the hourly SYS_LOG_SUPPRESSED
  * record says how many there were. */
 if (elapsed(nowMs, gf.lastPersistMs, LOGPOL_HEARTBEAT_MS)) {
 gf.lastPersistMs = nowMs;
 return true;
 }
 countSuppressed(nowMs);
 return false;
 }

 /* Warnings and errors that are NOT routed are never filtered — the safe
  * default for a code nobody has classified. This also settles the three
  * STO_H5_WIP call sites on its own: the routine LOG_INFO snapshot is
  * filtered, while the LOG_WARN paths around adopting a stale .wip keep
  * writing. */
 if (level >= LOGPOL_LEVEL_WARN) return true;

 /* The boot preamble of a wake.
  *
  * Placed AFTER the WARN shortcut so that a preamble step which fails still
  * writes: the point is to drop the copy that says the script ran, never the
  * one that says it did not. And placed BEFORE the unrouted default below,
  * because most of these codes have no family rule at all — that default is
  * exactly what was letting them through, once a minute, forever. */
 if (_quietPreamble && isBootPreamble(code)) {
 countSuppressed(nowMs);
 return false;
 }

 /* Not a routine code we know about — persist. New codes are visible by
  * default and only go quiet by being added to the table. */
 if (!rule) return true;

 GroupState& g = _grp[LOGPOL_RULE_GROUP(rule)];

 /* First of its family since boot, or the first success after a failure.
  * Both are transitions, and transitions are the whole point. */
 if (!g.seen || g.faulty) {
 g.seen = true;
 g.faulty = false;
 g.lastPersistMs = nowMs;
 return true;
 }

 /* Heartbeat: one routine record per hour per family, so a log that has gone
  * quiet still proves the subsystem was alive rather than stopped. */
 if (elapsed(nowMs, g.lastPersistMs, LOGPOL_HEARTBEAT_MS)) {
 g.lastPersistMs = nowMs;
 return true;
 }

 countSuppressed(nowMs);
 return false;
}

void LogPolicy::countSuppressed(uint32_t nowMs) {
 if (_suppressed == 0) {
 /* The reporting window starts at the first suppression, not at boot, so a
  * quiet device never emits an accounting record saying zero. */
 _lastReportMs = nowMs;
 _reportArmed = true;
 }
 _suppressed++;
}

uint16_t LogPolicy::takeSuppressedReport(uint32_t nowMs) {
 if (!_reportArmed || _suppressed == 0) return 0;
 if (!elapsed(nowMs, _lastReportMs, LOGPOL_REPORT_MS)) return 0;

 uint32_t n = _suppressed;
 _suppressed = 0;
 _reportArmed = false;

 /* CompactLogRecord::context is int16 — saturate rather than wrap, so a huge
  * number reads as "at least this many" instead of as a small plausible one. */
 return (n > 32767UL) ? (uint16_t)32767 : (uint16_t)n;
}
