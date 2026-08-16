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

 /* ── History / storage ───────────────────────────────────────────────── */
 LOGPOL_RULE(APP_HISTORY_SAVED,        LOGGRP_HIST, 0),
 LOGPOL_RULE(STO_H5_SEALED,            LOGGRP_HIST, 0),
 LOGPOL_RULE(STO_H5_WIP,               LOGGRP_HIST, 0),
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
};

static const uint8_t EDGE_RULE_COUNT = sizeof(EDGE_RULES) / sizeof(EDGE_RULES[0]);

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
}

bool LogPolicy::shouldPersist(uint16_t code, uint8_t level, uint32_t nowMs) {
 const uint16_t rule = lookup(code);

 /* Arm the latch BEFORE the level shortcut below. SYS_TEL_RETRY is a
  * LOG_WARN, so a level-first order would return early and never mark the
  * family as failed — and the recovery that follows would then look like just
  * another routine success and be dropped, which is the one record that had
  * to survive. */
 if (rule && LOGPOL_RULE_FAULT(rule)) {
 _grp[LOGPOL_RULE_GROUP(rule)].faulty = true;
 return true;
 }

 /* Warnings, errors and fatals are never filtered. This also settles the
  * three STO_H5_WIP call sites on its own: the routine LOG_INFO snapshot is
  * filtered, while the LOG_WARN paths around adopting a stale .wip keep
  * writing. */
 if (level >= LOGPOL_LEVEL_WARN) return true;

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

 if (_suppressed == 0) {
 /* The reporting window starts at the first suppression, not at boot, so a
  * quiet device never emits an accounting record saying zero. */
 _lastReportMs = nowMs;
 _reportArmed = true;
 }
 _suppressed++;
 return false;
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
