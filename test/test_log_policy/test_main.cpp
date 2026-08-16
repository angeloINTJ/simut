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

    /* The server starts rejecting: failure + retry both land. */
    t += 4000;
    if (pol.shouldPersist(SYS_TEL_FAIL,  LVL_ERROR, t)) written++;
    if (pol.shouldPersist(SYS_TEL_RETRY, LVL_WARN,  t)) written++;
    TEST_ASSERT_EQUAL_INT(3, written);

    /* It comes back: the recovery lands, the rest goes quiet again. */
    t += 60000;
    if (pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t)) written++;
    TEST_ASSERT_EQUAL_INT(4, written);

    for (int i = 0; i < 300; i++) {
        t += 4000;
        if (pol.shouldPersist(SYS_TEL_SENT, LVL_INFO, t)) written++;
    }
    TEST_ASSERT_EQUAL_INT(4, written);

    /* 752 send cycles produced 2 records (boot + recovery); with the failure
     * pair that is 4 records where the raw stream would have written 754. */
    TEST_ASSERT_EQUAL_UINT32(750, pol.suppressedPending( ));
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

    return UNITY_END( );
}
