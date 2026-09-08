/**
 * @file    test/test_log_policy/test_main.cpp
 * @brief   Host-side unit tests for the edge-triggered log filter.
 * @details Runs via `pio test -e native_logpolicy` (no HW). LogPolicy takes
 *          time as a parameter and touches no hardware, so every branch that
 *          would otherwise need an hour on the bench is reachable here:
 *            · first routine record after boot is written; the second is not
 *            · a failure arms the family, and the next success (the recovery)
 *              is written
 *            · LOG_WARN and above are never filtered
 *            · a WARN-level fault code still arms the latch — the ordering bug
 *              this test exists to prevent
 *            · one family's failure does not unlock another family
 *            · the hourly heartbeat fires, and re-arms
 *            · the suppressed counter reports hourly, saturates at int16, and
 *              never reports zero
 *            · millis( ) wrapping past 2^32 does not stall either cadence
 *
 * @project SIMUT — edge-triggered log filter unit testing
 * @license MIT License
 */

#include <unity.h>
#include "LogPolicy.h"
#include "SystemDefs_Logging.h"

/* ----- Required by native_stubs/Arduino.h linker symbol ----- */
namespace simut_native {
    uint32_t fake_millis_value = 0;
}

/* Mirror of LogLevel — LogManager.h is not host-safe (pico/mutex.h). The
 * production static_asserts in LogManager.cpp lock these to the real enum. */
static const uint8_t LVL_DEBUG = 0;
static const uint8_t LVL_INFO  = 1;
static const uint8_t LVL_WARN  = 2;
static const uint8_t LVL_ERROR = 3;

static const uint32_t HOUR = LOGPOL_HEARTBEAT_MS;

static LogPolicy pol;

void setUp(void)    { pol.reset( ); }
void tearDown(void) { }

/* ============================================================================
 *  THE CORE RULE — routine records only on a transition
 * ============================================================================ */

static void test_first_routine_record_is_written(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
}

static void test_repeats_are_dropped(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    for (uint32_t t = 2000; t < 60000; t += 1000) {
        TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t));
    }
    TEST_ASSERT_EQUAL_UINT32(58, pol.suppressedPending( ));
}

static void test_recovery_after_failure_is_written(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 2000));

    /* The failure itself always lands. */
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 3000));

    /* ...and the first success after it is the record that matters. */
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 4000));

    /* Back to quiet immediately after. */
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 5000));
}

/* The ordering bug this file exists to prevent: SYS_TEL_RETRY is a LOG_WARN and
 * a fault marker. A level-first implementation returns true and never arms the
 * latch, so the recovery that follows looks like an ordinary success and gets
 * dropped — losing the one record the whole feature is for. */
static void test_warn_level_fault_still_arms_the_latch(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 2000));

    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_RETRY, LVL_WARN, 3000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 4000));
}

static void test_warn_and_above_are_never_filtered(void) {
    /* STO_H5_WIP is in the table on the OK side, but its WARN call sites carry
     * real information about adopting a stale .wip and must keep writing. */
    TEST_ASSERT_TRUE(pol.shouldPersist(STO_H5_WIP, LVL_INFO, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(STO_H5_WIP, LVL_INFO, 2000));
    for (uint32_t t = 3000; t < 10000; t += 1000) {
        TEST_ASSERT_TRUE(pol.shouldPersist(STO_H5_WIP, LVL_WARN, t));
    }
}

static void test_unknown_codes_are_always_written(void) {
    /* Anything absent from the rules table stays visible — a new LogCode must
     * be added on purpose to go quiet, never by omission. */
    for (uint32_t t = 1000; t < 10000; t += 1000) {
        TEST_ASSERT_TRUE(pol.shouldPersist(SEC_CONFIG_CHANGED, LVL_INFO, t));
        TEST_ASSERT_TRUE(pol.shouldPersist(SEC_LOGIN_SUCCESS,  LVL_INFO, t));
        TEST_ASSERT_TRUE(pol.shouldPersist(SYS_BOOT,           LVL_INFO, t));
        TEST_ASSERT_TRUE(pol.shouldPersist(APP_ALARM_TRIGGERED, LVL_INFO, t));
    }
    TEST_ASSERT_EQUAL_UINT32(0, pol.suppressedPending( ));
}

/* SYS_LOG_SUPPRESSED must never be filtered by the thing it accounts for. */
static void test_accounting_code_is_never_filtered(void) {
    for (uint32_t t = 1000; t < 20000; t += 1000) {
        TEST_ASSERT_TRUE(pol.shouldPersist(SYS_LOG_SUPPRESSED, LVL_INFO, t));
    }
}

/* ============================================================================
 *  FAMILIES ARE INDEPENDENT
 * ============================================================================ */

static void test_families_do_not_leak_into_each_other(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT,      LVL_INFO, 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_HISTORY_SAVED, LVL_INFO, 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_WIFI_CONNECT,  LVL_INFO, 1000));

    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT,      LVL_INFO, 2000));
    TEST_ASSERT_FALSE(pol.shouldPersist(APP_HISTORY_SAVED, LVL_INFO, 2000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_WIFI_CONNECT,  LVL_INFO, 2000));

    /* A storage failure must not make telemetry look like it recovered. */
    TEST_ASSERT_TRUE(pol.shouldPersist(STO_WRITE_FAILED, LVL_ERROR, 3000));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_HISTORY_SAVED, LVL_INFO, 4000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT,     LVL_INFO, 4000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_WIFI_CONNECT, LVL_INFO, 4000));
}

/* Every OK code in a family shares one latch — the family is the unit, not the
 * code. An MQTT publish after an MQTT disconnect is a recovery. */
static void test_family_members_share_the_latch(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT,     LVL_INFO, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_MQTT_PUB, LVL_INFO, 2000));

    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_MQTT_DISC, LVL_ERROR, 3000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_MQTT_CONN, LVL_INFO,  4000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT,     LVL_INFO,  5000));
}

/* ============================================================================
 *  HEARTBEAT — "healthy and quiet" must stay distinguishable from "dead"
 * ============================================================================ */

static void test_heartbeat_fires_once_per_hour(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));

    /* One second short of the hour: still quiet. */
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000 + HOUR - 1000));
    /* On the hour: beat. */
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000 + HOUR));
    /* And the window restarts from the beat, not from boot. */
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000 + HOUR + 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000 + 2 * HOUR));
}

/* A recovery also restarts the heartbeat window — it is a fresh anchor. */
static void test_recovery_rearms_the_heartbeat_window(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, HOUR / 2));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, HOUR / 2 + 1000));

    /* The original hour elapses, but the anchor moved: no beat yet. */
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, HOUR + 2000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, HOUR / 2 + 1000 + HOUR));
}

/* ============================================================================
 *  ACCOUNTING — nothing disappears silently
 * ============================================================================ */

static void test_report_is_silent_when_nothing_was_suppressed(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    TEST_ASSERT_EQUAL_UINT16(0, pol.takeSuppressedReport(1000 + 5 * HOUR));
}

static void test_report_fires_an_hour_after_the_first_suppression(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    /* First suppression at t=2000 starts the reporting window. */
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 2000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 3000));

    TEST_ASSERT_EQUAL_UINT16(0, pol.takeSuppressedReport(2000 + HOUR - 1));
    TEST_ASSERT_EQUAL_UINT16(2, pol.takeSuppressedReport(2000 + HOUR));

    /* Reading clears it, and the window disarms until the next suppression. */
    TEST_ASSERT_EQUAL_UINT32(0, pol.suppressedPending( ));
    TEST_ASSERT_EQUAL_UINT16(0, pol.takeSuppressedReport(2000 + 3 * HOUR));
}

static void test_counter_saturates_at_int16_max(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 1000));
    /* 40k suppressions inside one heartbeat window. */
    for (uint32_t i = 0; i < 40000; i++) {
        pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 2000);
    }
    TEST_ASSERT_EQUAL_UINT32(40000, pol.suppressedPending( ));
    /* ctx is int16 — saturate, never wrap into a small plausible number. */
    TEST_ASSERT_EQUAL_UINT16(32767, pol.takeSuppressedReport(2000 + HOUR));
}

/* ============================================================================
 *  millis( ) WRAP — 49.7 days in, neither cadence may stall
 * ============================================================================ */

static void test_heartbeat_survives_millis_wrap(void) {
    const uint32_t before = 0xFFFFFFFFUL - (HOUR / 2);   /* half an hour to go */

    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, before));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, before + 1000));

    /* Past the wrap, still inside the hour: quiet. */
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, HOUR / 4));
    /* An hour after the anchor, having wrapped in between: beat. */
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, HOUR / 2));
}

static void test_report_survives_millis_wrap(void) {
    const uint32_t before = 0xFFFFFFFFUL - (HOUR / 2);

    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, before));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, before + 1000));

    TEST_ASSERT_EQUAL_UINT16(0, pol.takeSuppressedReport(HOUR / 4));
    TEST_ASSERT_EQUAL_UINT16(1, pol.takeSuppressedReport(before + 1000 + HOUR));
}

/* ============================================================================
 *  RESET — a reboot re-opens every family
 * ============================================================================ */

static void test_reset_reopens_every_family(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT,      LVL_INFO, 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_HISTORY_SAVED, LVL_INFO, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_SENT,     LVL_INFO, 2000));

    pol.reset( );

    TEST_ASSERT_EQUAL_UINT32(0, pol.suppressedPending( ));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT,      LVL_INFO, 3000));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_HISTORY_SAVED, LVL_INFO, 3000));
}

/* ============================================================================
 *  END-TO-END — the story a reader should find in the log
 * ============================================================================ */

static void test_the_shape_of_a_real_hour(void) {
    uint32_t t = 1000;
    int written = 0;

    /* Boot: first send lands. */
    if (pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t)) written++;

    /* 30 minutes of a 4 s telemetry cycle, all healthy. */
    for (int i = 0; i < 450; i++) {
        t += 4000;
        if (pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t)) written++;
    }
    TEST_ASSERT_EQUAL_INT(1, written);   /* the boot record, nothing else */

    /* The server starts rejecting. The FAILURE is the transition and lands;
     * the retry that comes with it is the same family already known to be
     * failing, and does not. */
    t += 4000;
    if (pol.shouldPersist(SYS_TEL_FAIL,  LVL_ERROR, t)) written++;
    if (pol.shouldPersist(SYS_TEL_RETRY, LVL_WARN,  t)) written++;
    TEST_ASSERT_EQUAL_INT(2, written);

    /* It stays down for a hundred more attempts. This is the whole point of
     * the change: the pair used to be written every time, and a collector
     * that stopped answering filled the forensic window on its own. */
    for (int i = 0; i < 100; i++) {
        t += 4000;
        if (pol.shouldPersist(SYS_TEL_FAIL,  LVL_ERROR, t)) written++;
        if (pol.shouldPersist(SYS_TEL_RETRY, LVL_WARN,  t)) written++;
    }
    TEST_ASSERT_EQUAL_INT(2, written);

    /* It comes back: the recovery lands, the rest goes quiet again. */
    t += 60000;
    if (pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t)) written++;
    TEST_ASSERT_EQUAL_INT(3, written);

    for (int i = 0; i < 300; i++) {
        t += 4000;
        if (pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t)) written++;
    }
    TEST_ASSERT_EQUAL_INT(3, written);

    /* 752 send cycles and 101 failed attempts produced THREE records: the
     * boot, the moment it broke, and the moment it came back. The raw stream
     * would have written 954. */
    TEST_ASSERT_EQUAL_UINT32(951, pol.suppressedPending( ));
}

/* ---------------------------------------------------------------------------
 * Failures are edge-triggered (2026-09-07)
 *
 * Positive control, run once when these were written: reverting shouldPersist( )
 * to the old "a fault always persists" behaviour fails five of them —
 * the_shape_of_a_real_hour, a_sustained_outage, a_second_failure_mode,
 * a_long_outage and the_alarm_line. The two that keep passing are the two that
 * guard the OPPOSITE mistake: recovery_still_lands and a_fatal_is_never_filtered
 * exist to catch over-suppression, and the old code did not over-suppress.
 * ------------------------------------------------------------------------- */

static void test_a_sustained_outage_writes_one_record(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000));
    for (uint32_t t = 2000; t < 600000; t += 2000) {
        TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, t));
    }
}

static void test_a_second_failure_mode_shares_the_latch(void) {
    /* SYS_TEL_FAIL and SYS_TEL_RETRY alternate on a failing upload, which is
     * exactly why the latch is per family: a per-code latch would let both
     * through on every attempt and suppress nothing. */
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL,  LVL_ERROR, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_RETRY, LVL_WARN, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_FAIL,  LVL_ERROR, 2000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_RETRY, LVL_WARN, 2000));
}

static void test_a_long_outage_still_beats_once_an_hour(void) {
    /* Silence has to stay distinguishable from recovery: a family that has
     * been failing for an hour says so again. */
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000 + 3599000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000 + 3600000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000 + 3601000));
}

static void test_recovery_still_lands_after_a_suppressed_outage(void) {
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000));
    for (uint32_t t = 2000; t < 100000; t += 2000) {
        pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, t);
    }
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 100000));
}

static void test_a_fatal_is_never_filtered(void) {
    const uint8_t LVL_FATAL = 4;
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000));
    /* The family is latched now, and a fatal must still get through it. */
    for (uint32_t t = 2000; t < 20000; t += 2000) {
        TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_FATAL, t));
    }
}

static void test_the_alarm_line_has_its_own_latch(void) {
    /* A stuck alarm line must not hide measurement telemetry recovering. */
    TEST_ASSERT_TRUE(pol.shouldPersist(TEL_ALARM_FAIL, LVL_ERROR, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(TEL_ALARM_FAIL, LVL_ERROR, 2000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 3000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, 4000));
    /* ...and the alarm line is still latched, untouched by any of that. */
    TEST_ASSERT_FALSE(pol.shouldPersist(TEL_ALARM_FAIL, LVL_ERROR, 5000));
    TEST_ASSERT_TRUE(pol.shouldPersist(TEL_ALARM_SENT, LVL_INFO, 6000));
}

static void test_unrouted_warnings_are_still_unconditional(void) {
    /* The safe default has not moved: only codes listed in the table can be
     * filtered, so a new failure code stays visible until someone classifies
     * it on purpose. */
    for (uint32_t t = 1000; t < 10000; t += 1000) {
        TEST_ASSERT_TRUE(pol.shouldPersist(ERR_SENSOR_TIMEOUT, LVL_ERROR, t));
        TEST_ASSERT_TRUE(pol.shouldPersist(SEC_CONFIG_CHANGED, LVL_INFO, t));
    }
}

/* ============================================================================
 *  THE BOOT PREAMBLE — a wake is a boot that already happened
 *
 *  Measured on the rig over 108 boots: these eight codes, in this order, were
 *  68.2% of the whole forensic window. On SIMUT Air every wake is a full boot,
 *  so setup( ) reran the same script once a minute and wrote it down every
 *  time.
 *
 *  Positive control, both directions, run before this file was committed:
 *    · gate removed (nothing is ever filtered) → 4 fail: _a_wake_writes_none_
 *      of_it, _suppressed_preamble_is_counted, _closing_the_window_..., and
 *      _a_silenced_preamble_does_not_open_its_family.
 *    · setter wired to ignore its argument (filter permanently on) → 2 fail:
 *      _a_cold_boot_writes_the_whole_preamble and _closing_the_window_....
 *  The second direction is the one that matters: a filter stuck on silences a
 *  device nobody is watching, and only these two notice.
 * ============================================================================ */

/* The measured signature of a wake, in the order the rig emitted it. */
static const uint16_t WAKE_PREAMBLE[] = {
    NET_PROVISIONAL_TIME, APP_UI_LANG_CHANGED, SENSOR_RUNTIME_LOADED,
    APP_SENSORS_CALIBRATED, TEL_ALARM_LINE_ON, TEL_HTTP_INIT,
    STO_H5_WIP, APP_READY,
};
static const size_t WAKE_PREAMBLE_N =
    sizeof(WAKE_PREAMBLE) / sizeof(WAKE_PREAMBLE[0]);

static void test_the_preamble_filter_is_off_by_default(void) {
    TEST_ASSERT_FALSE(pol.quietPreamble( ));
}

static void test_a_cold_boot_writes_the_whole_preamble(void) {
    /* Explicitly disarmed rather than relying on the default, so this also
     * fails if the setter is ever wired to ignore its argument. */
    pol.setQuietPreamble(false);
    for (size_t i = 0; i < WAKE_PREAMBLE_N; i++) {
        TEST_ASSERT_TRUE(pol.shouldPersist(WAKE_PREAMBLE[i], LVL_INFO, 1000 + i));
    }
    TEST_ASSERT_EQUAL_UINT32(0, pol.suppressedPending( ));
}

static void test_a_wake_writes_none_of_it(void) {
    pol.setQuietPreamble(true);
    for (size_t i = 0; i < WAKE_PREAMBLE_N; i++) {
        TEST_ASSERT_FALSE(pol.shouldPersist(WAKE_PREAMBLE[i], LVL_INFO, 1000 + i));
    }
}

static void test_suppressed_preamble_is_counted(void) {
    pol.setQuietPreamble(true);
    for (size_t i = 0; i < WAKE_PREAMBLE_N; i++) {
        pol.shouldPersist(WAKE_PREAMBLE[i], LVL_INFO, 1000 + i);
    }
    /* Nothing disappears silently: the hourly accounting still owns them. */
    TEST_ASSERT_EQUAL_UINT32(WAKE_PREAMBLE_N, pol.suppressedPending( ));
}

static void test_reset_disarms_the_preamble(void) {
    pol.setQuietPreamble(true);
    pol.reset( );
    TEST_ASSERT_FALSE(pol.quietPreamble( ));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_READY, LVL_INFO, 1000));
}

static void test_closing_the_window_makes_operator_actions_visible_again(void) {
    /* Two of the preamble codes are also raised by a person: the language
     * change from the CLI and the calibration from /api/calib. Silencing those
     * would be a real loss, and the only thing standing between them and the
     * filter is endBootPreamble( ) being called at the end of setup( ). */
    pol.setQuietPreamble(true);
    TEST_ASSERT_FALSE(pol.shouldPersist(APP_UI_LANG_CHANGED, LVL_INFO, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(APP_SENSORS_CALIBRATED, LVL_INFO, 1100));

    pol.setQuietPreamble(false);
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_UI_LANG_CHANGED, LVL_INFO, 60000));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_SENSORS_CALIBRATED, LVL_INFO, 60100));
}

static void test_a_failing_preamble_step_still_writes(void) {
    /* The point is to drop the copy that says the script ran, never the one
     * that says it did not. */
    pol.setQuietPreamble(true);
    TEST_ASSERT_TRUE(pol.shouldPersist(STO_H5_WIP, LVL_WARN, 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_READY, LVL_ERROR, 1100));
}

static void test_a_silenced_preamble_does_not_open_its_family(void) {
    /* STO_H5_WIP is both a preamble step and the HIST family's routine record.
     * Dropping it must NOT count as "HIST already proved itself this boot",
     * or the cycle's real work — the record it went to sleep to save — would
     * inherit the silence. */
    pol.setQuietPreamble(true);
    TEST_ASSERT_FALSE(pol.shouldPersist(STO_H5_WIP, LVL_INFO, 1000));
    pol.setQuietPreamble(false);
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_HISTORY_SAVED, LVL_INFO, 2000));
}

static void test_the_cold_boot_notice_is_never_filtered(void) {
    /* The one line a wake never writes, so that when it appears it means the
     * device restarted without coming from hibernation. */
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_AIR_COLD_BOOT, LVL_INFO, 1000));
    pol.setQuietPreamble(true);
    TEST_ASSERT_TRUE(pol.shouldPersist(APP_AIR_COLD_BOOT, LVL_INFO, 2000));
}

static void test_a_fault_during_a_quiet_preamble_still_lands(void) {
    /* A wake whose collector is down still reports it on the first attempt —
     * the preamble gate must not stand in front of the fault latch. */
    pol.setQuietPreamble(true);
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 1000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_TEL_FAIL, LVL_ERROR, 2000));
}

int main(int, char**) {
    UNITY_BEGIN( );

    RUN_TEST(test_first_routine_record_is_written);
    RUN_TEST(test_repeats_are_dropped);
    RUN_TEST(test_recovery_after_failure_is_written);
    RUN_TEST(test_warn_level_fault_still_arms_the_latch);
    RUN_TEST(test_warn_and_above_are_never_filtered);
    RUN_TEST(test_unknown_codes_are_always_written);
    RUN_TEST(test_accounting_code_is_never_filtered);

    RUN_TEST(test_families_do_not_leak_into_each_other);
    RUN_TEST(test_family_members_share_the_latch);

    RUN_TEST(test_heartbeat_fires_once_per_hour);
    RUN_TEST(test_recovery_rearms_the_heartbeat_window);

    RUN_TEST(test_report_is_silent_when_nothing_was_suppressed);
    RUN_TEST(test_report_fires_an_hour_after_the_first_suppression);
    RUN_TEST(test_counter_saturates_at_int16_max);

    RUN_TEST(test_heartbeat_survives_millis_wrap);
    RUN_TEST(test_report_survives_millis_wrap);

    RUN_TEST(test_reset_reopens_every_family);
    RUN_TEST(test_the_shape_of_a_real_hour);

    RUN_TEST(test_a_sustained_outage_writes_one_record);
    RUN_TEST(test_a_second_failure_mode_shares_the_latch);
    RUN_TEST(test_a_long_outage_still_beats_once_an_hour);
    RUN_TEST(test_recovery_still_lands_after_a_suppressed_outage);
    RUN_TEST(test_a_fatal_is_never_filtered);
    RUN_TEST(test_the_alarm_line_has_its_own_latch);
    RUN_TEST(test_unrouted_warnings_are_still_unconditional);

    RUN_TEST(test_the_preamble_filter_is_off_by_default);
    RUN_TEST(test_a_cold_boot_writes_the_whole_preamble);
    RUN_TEST(test_a_wake_writes_none_of_it);
    RUN_TEST(test_suppressed_preamble_is_counted);
    RUN_TEST(test_reset_disarms_the_preamble);
    RUN_TEST(test_closing_the_window_makes_operator_actions_visible_again);
    RUN_TEST(test_a_failing_preamble_step_still_writes);
    RUN_TEST(test_a_silenced_preamble_does_not_open_its_family);
    RUN_TEST(test_the_cold_boot_notice_is_never_filtered);
    RUN_TEST(test_a_fault_during_a_quiet_preamble_still_lands);

    return UNITY_END( );
}
