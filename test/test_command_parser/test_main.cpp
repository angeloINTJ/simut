/**
 * @file    test/test_command_parser/test_main.cpp
 * @brief   Host-side tests for parseCliCommand() — issue #42.
 * @details USB serial CLI has no auth gate; parser bugs can brick config.
 *          Runs via `pio test -e native_cli`.
 *
 * @project SIMUT
 * @license MIT License
 */

#include <unity.h>
#include "CommandParser.h"
#include "SystemDefs_Records.h"
#include "SystemDefs_Network.h"
#include "display/BootReplay.h"   /* what the handler makes of the words after `screen boot` */
#include <cstring>

namespace simut_native {
    uint32_t fake_millis_value = 0;
}

static CliDemand parse(const char* line) {
    return parseCliCommand(String(line));
}

static void assertStr1(const CliDemand& d, const char* expected) {
    TEST_ASSERT_EQUAL_STRING(expected, d.strVal1);
}

void setUp(void) {}
void tearDown(void) {}

/* ---- basics ---- */

void test_empty_is_unknown(void) {
    CliDemand d = parse("");
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, d.type);
}

void test_garbage_unknown(void) {
    CliDemand d = parse("not_a_real_command lol");
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, d.type);
}

void test_help_variants(void) {
    TEST_ASSERT_EQUAL(CMD_HELP, parse("help").type);
    TEST_ASSERT_EQUAL(CMD_HELP, parse("HELP").type);
    TEST_ASSERT_EQUAL(CMD_HELP, parse("ajuda").type);
    TEST_ASSERT_EQUAL(CMD_HELP, parse("?").type);
}

void test_show_system_info(void) {
    CliDemand d = parse("show system info");
    TEST_ASSERT_EQUAL(CMD_SHOW_SYSINFO, d.type);
}

void test_show_with_extra_spaces(void) {
    CliDemand d = parse("  show   sensors  ");
    TEST_ASSERT_EQUAL(CMD_SHOW_SENSORS, d.type);
}

/* ---- destructive confirm suffix ---- */

void test_clear_log_confirm(void) {
    CliDemand d = parse("clear log confirm");
    TEST_ASSERT_EQUAL(CMD_CLEAR_LOGS, d.type);
    TEST_ASSERT_TRUE(d.confirmed);
}

void test_clear_log_no_confirm(void) {
    CliDemand d = parse("clear log");
    TEST_ASSERT_EQUAL(CMD_CLEAR_LOGS, d.type);
    TEST_ASSERT_FALSE(d.confirmed);
}

/* ---- user management (cold-chain labs often share one admin) ---- */

void test_user_perm_role(void) {
    CliDemand d = parse("user perm fieldtech admin");
    TEST_ASSERT_EQUAL(CMD_USER_PERM, d.type);
    assertStr1(d, "fieldtech");
    TEST_ASSERT_EQUAL_STRING("admin", d.strVal2);
}

void test_user_perm_hex_mask(void) {
    CliDemand d = parse("user perm bob 0x00FF");
    TEST_ASSERT_EQUAL(CMD_USER_PERM, d.type);
    assertStr1(d, "bob");
    /* Role/mask is matched case-insensitively, so the token is lowercased. */
    TEST_ASSERT_EQUAL_STRING("0x00ff", d.strVal2);
}

void test_user_perm_without_value_is_not_perm(void) {
    /* Missing the role must not fall through to a half-built CMD_USER_PERM. */
    CliDemand d = parse("user perm bob");
    TEST_ASSERT_NOT_EQUAL(CMD_USER_PERM, d.type);
}

/* v25: `user policy <min> <keypad> <alphabet>`. The three values travel as ONE
 * string because they are only valid together, so the parser has to carry all
 * THREE tokens. It carried two, `user policy 4 3 0` arrived as "4 3", and the
 * handler's two-separator guard then rejected every well-formed call — the
 * verb had never once run when the rig found it on 2026-09-20. This asserts
 * the string the handler is given, which is the whole contract between them. */
void test_user_policy_carries_all_three_values(void) {
    CliDemand d = parse("user policy 4 3 0");
    TEST_ASSERT_EQUAL(CMD_USER_POLICY, d.type);
    assertStr1(d, "4 3 0");
}

void test_user_policy_missing_alphabet_leaves_the_field_empty(void) {
    /* Not an error HERE — the handler owns the usage message — but the third
     * field must arrive EMPTY and not absent, or a missing argument reads as
     * alphabet 0, a policy the operator never asked for. */
    CliDemand d = parse("user policy 4 3");
    TEST_ASSERT_EQUAL(CMD_USER_POLICY, d.type);
    assertStr1(d, "4 3 ");
}

void test_conf_user_add(void) {
    CliDemand d = parse("conf user add fieldtech s3cret");
    TEST_ASSERT_EQUAL(CMD_USER_ADD, d.type);
    assertStr1(d, "fieldtech");
    TEST_ASSERT_EQUAL_STRING("s3cret", d.strVal2);
}

void test_conf_user_add_missing_pass_still_parses(void) {
    CliDemand d = parse("conf user add solo");
    TEST_ASSERT_EQUAL(CMD_USER_ADD, d.type);
    assertStr1(d, "solo");
}

void test_conf_user_del(void) {
    CliDemand d = parse("conf user del guest");
    TEST_ASSERT_EQUAL(CMD_USER_DEL, d.type);
    assertStr1(d, "guest");
}

/* ---- sensor / telemetry paths ---- */

void test_sensor_scan(void) {
    TEST_ASSERT_EQUAL(CMD_SCAN_SENSORS, parse("sensor scan").type);
}

void test_conf_tel_server_preserves_case(void) {
    CliDemand d = parse("conf tel server MyLab-Gateway.local");
    TEST_ASSERT_EQUAL(CMD_SET_TEL_SERVER, d.type);
    assertStr1(d, "MyLab-Gateway.local");
}

void test_conf_tel_mode_json(void) {
    CliDemand d = parse("conf tel mode json");
    TEST_ASSERT_EQUAL(CMD_SET_TEL_MODE, d.type);
    TEST_ASSERT_EQUAL(TEL_MODE_JSON, d.intVal1);
}

void test_sensor_slot_field(void) {
    CliDemand d = parse("sensor 4 tmin -5.0");
    TEST_ASSERT_EQUAL(CMD_SENSOR_FIELD, d.type);
    TEST_ASSERT_TRUE(d.intVal1Valid);
    TEST_ASSERT_EQUAL(4, d.intVal1);
    TEST_ASSERT_EQUAL_STRING("tmin", d.strVal1);
    TEST_ASSERT_EQUAL_STRING("-5.0", d.strVal2);
}

void test_conf_sensor_tmax_invalid_gpio(void) {
    CliDemand d = parse("conf sensor tmax abc 40");
    TEST_ASSERT_EQUAL(CMD_SENSOR_FIELD, d.type);
    TEST_ASSERT_FALSE(d.intVal1Valid);
}

/* ---- partial / incomplete commands ---- */

void test_conf_alone_unknown(void) {
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("conf").type);
}

void test_show_alone_unknown(void) {
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("show").type);
}

void test_conf_system_factory(void) {
    CliDemand d = parse("conf system factory confirm");
    TEST_ASSERT_EQUAL(CMD_FACTORY_RESET, d.type);
    TEST_ASSERT_TRUE(d.confirmed);
}

/* ---- edge: unicode bytes in SSID shouldn't crash parser ---- */

void test_wifi_ssid_utf8_bytes(void) {
    CliDemand d = parse("conf system ssid café-π");
    TEST_ASSERT_EQUAL(CMD_SET_WIFI_SSID, d.type);
    TEST_ASSERT_TRUE(strlen(d.strVal1) > 0);
}

/* ---- the radio's commands, where there is a radio ----
 * The control for test/test_lan: the wired build answers "unknown" to these,
 * and the guard that does it (`#if SIMUT_NET_WIFI`) must keep them here. */

void test_radio_commands_parse(void) {
    TEST_ASSERT_EQUAL(CMD_AP, parse("ap").type);
    TEST_ASSERT_EQUAL(CMD_AP, parse("apmode").type);
    TEST_ASSERT_EQUAL(CMD_AP, parse("ap-mode").type);
    CliDemand d = parse("wifi ssid MyNet");
    TEST_ASSERT_EQUAL(CMD_SET_WIFI_SSID, d.type);
    assertStr1(d, "MyNet");
    TEST_ASSERT_EQUAL(CMD_SET_WIFI_PASS, parse("wifi pass benchsecret").type);
    TEST_ASSERT_EQUAL(CMD_SET_WIFI_SSID, parse("system ssid MyNet").type);
    TEST_ASSERT_EQUAL(CMD_SET_WIFI_PASS, parse("system pass benchsecret").type);
}

/* ---- sensor define: optional trailing explicit type ---- */

void test_sensor_define_explicit_bme280(void) {
    CliDemand d = parse("sensor define 4 0000000000000000 BMP28000 \"BMP280 GP4-5\" bme280");
    TEST_ASSERT_EQUAL(CMD_DEFINE_SENSOR, d.type);
    TEST_ASSERT_TRUE(d.intVal1Valid);
    TEST_ASSERT_EQUAL(4, d.intVal1);
    TEST_ASSERT_EQUAL_STRING("BMP28000", d.strVal1);
    TEST_ASSERT_EQUAL_STRING("BMP280 GP4-5", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("bme280", d.strVal3);
}

void test_sensor_define_no_type_keeps_name_and_empty_strval3(void) {
    CliDemand d = parse("sensor define 2 0000000000000000 DHT2202 \"DHT22 GP2\"");
    TEST_ASSERT_EQUAL(CMD_DEFINE_SENSOR, d.type);
    TEST_ASSERT_EQUAL_STRING("DHT22 GP2", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("", d.strVal3);
}

void test_sensor_define_type_only_no_name(void) {
    CliDemand d = parse("sensor define 4 0000000000000000 BMP28000 bme280");
    TEST_ASSERT_EQUAL(CMD_DEFINE_SENSOR, d.type);
    TEST_ASSERT_EQUAL_STRING("bme280", d.strVal3);
}

void test_sensor_remove_confirm(void) {
    CliDemand d = parse("sensor remove 5 confirm");
    TEST_ASSERT_EQUAL(CMD_REMOVE_SENSOR, d.type);
    TEST_ASSERT_TRUE(d.intVal1Valid);
    TEST_ASSERT_EQUAL(5, d.intVal1);
    TEST_ASSERT_TRUE(d.confirmed);
}

void test_sensor_remove_without_confirm(void) {
    CliDemand d = parse("sensor remove 5");
    TEST_ASSERT_EQUAL(CMD_REMOVE_SENSOR, d.type);
    TEST_ASSERT_FALSE(d.confirmed);
}

void test_system_format_confirm(void) {
    CliDemand d = parse("system format confirm");
    TEST_ASSERT_EQUAL(CMD_FORMAT_FS, d.type);
    TEST_ASSERT_TRUE(d.confirmed);
}

/* ---- 256-char boundary (CLI_LINE_MAX) ---- */

void test_long_line_still_parses_help_prefix(void) {
    char buf[CLI_LINE_MAX + 8];
    memset(buf, 'x', CLI_LINE_MAX - 5);
    memcpy(buf, "help", 4);
    buf[4] = ' ';
    buf[CLI_LINE_MAX - 1] = '\0';
    CliDemand d = parse(buf);
    /* tokenizer only keeps first token — still recognizes help */
    TEST_ASSERT_EQUAL(CMD_HELP, d.type);
}

void test_touch_hold_with_ms(void) {
    /* 'touch hold X Y MS' — x/y in strVal1/strVal2, MS in intVal1 (valid). */
    CliDemand d = parse("touch hold 160 70 3500");
    TEST_ASSERT_EQUAL(CMD_TOUCH_HOLD, d.type);
    assertStr1(d, "160");
    TEST_ASSERT_EQUAL_STRING("70", d.strVal2);
    TEST_ASSERT_TRUE(d.intVal1Valid);
    TEST_ASSERT_EQUAL_INT(3500, d.intVal1);
}

void test_touch_hold_without_ms_marks_intval_invalid(void) {
    /* MS omitted: still a hold, but intVal1Valid is false so the handler
     * applies its default duration instead of a stray 0. */
    CliDemand d = parse("touch hold 160 70");
    TEST_ASSERT_EQUAL(CMD_TOUCH_HOLD, d.type);
    assertStr1(d, "160");
    TEST_ASSERT_EQUAL_STRING("70", d.strVal2);
    TEST_ASSERT_FALSE(d.intVal1Valid);
}

/* ---- `screen` and the boot tags (2026-11-06) ----
 *
 * `screen <tag>` carried one word, and the handler (AppManager_Commands.cpp) read it from strVal1. The boot tags take
 * two more: `screen boot 3 50` recomposes the boot terminal after call 3 and draws the bar at 50 %, `screen boot 3
 * cancel` adds what the boot does when the gesture is let go, `screen boot end` gives the panel back. The parser does
 * not judge them: it hands the two words over in strVal2 and strVal3, lowercase like every matched token, and
 * bootreplay::parse( ) (display/BootReplay.h, held on the host in test_validators) says what they mean. The last case
 * here ties the two halves, because a word that arrives in the wrong field reads as a different command. */

void test_screen_tag_alone_carries_only_the_tag(void) {
    CliDemand d = parse("screen dash");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    assertStr1(d, "dash");
    TEST_ASSERT_EQUAL_STRING("", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("", d.strVal3);
}

void test_screen_boot_without_argument_lists(void) {
    CliDemand d = parse("screen boot");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    assertStr1(d, "boot");
    TEST_ASSERT_EQUAL_STRING("", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("", d.strVal3);
}

void test_screen_boot_index_arrives_in_strval2(void) {
    CliDemand d = parse("screen boot 3");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    assertStr1(d, "boot");
    TEST_ASSERT_EQUAL_STRING("3", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("", d.strVal3);
}

void test_screen_boot_index_and_percent_arrive_in_strval2_and_3(void) {
    CliDemand d = parse("screen boot 3 50");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    assertStr1(d, "boot");
    TEST_ASSERT_EQUAL_STRING("3", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("50", d.strVal3);
}

void test_screen_boot_index_and_cancel_arrive_lowercase(void) {
    /* The matched tokens are lowercased, and bootreplay::parse( ) compares against lowercase words. */
    CliDemand d = parse("SCREEN Boot 3 CANCEL");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    assertStr1(d, "boot");
    TEST_ASSERT_EQUAL_STRING("3", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("cancel", d.strVal3);
}

void test_screen_boot_end_and_clkboot(void) {
    CliDemand e = parse("screen boot end");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, e.type);
    assertStr1(e, "boot");
    TEST_ASSERT_EQUAL_STRING("end", e.strVal2);
    CliDemand c = parse("screen clkboot");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, c.type);
    assertStr1(c, "clkboot");
}

void test_screen_boot_end_with_a_word_after_it_hands_both_over_and_is_refused_there(void) {
    /* `screen boot end 3` is not `end`. The parser does not know that: it has to hand over "end" in strVal2 AND "3" in strVal3, because a parser that
     * dropped the third word for `end` would turn this line into a plain `end` and the handler would give the panel back on a typo. */
    CliDemand d = parse("screen boot end 3");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    assertStr1(d, "boot");
    TEST_ASSERT_EQUAL_STRING("end", d.strVal2);
    TEST_ASSERT_EQUAL_STRING("3", d.strVal3);
    TEST_ASSERT_EQUAL(bootreplay::V_BAD, bootreplay::parse(d.strVal2, d.strVal3).verb);
    CliDemand w = parse("screen boot END Cancel");
    TEST_ASSERT_EQUAL_STRING("end", w.strVal2);
    TEST_ASSERT_EQUAL_STRING("cancel", w.strVal3);
    TEST_ASSERT_EQUAL(bootreplay::V_BAD, bootreplay::parse(w.strVal2, w.strVal3).verb);
}

void test_screen_boot_an_invalid_word_reaches_the_handler_and_is_refused_there(void) {
    /* The parser does not reject `screen boot x`; the handler's bootreplay::parse( ) does, and it must see "x". */
    CliDemand d = parse("screen boot x");
    TEST_ASSERT_EQUAL(CMD_GOTO_SCREEN, d.type);
    TEST_ASSERT_EQUAL_STRING("x", d.strVal2);
    TEST_ASSERT_EQUAL(bootreplay::V_BAD, bootreplay::parse(d.strVal2, d.strVal3).verb);
}

void test_screen_boot_words_mean_what_the_handler_reads_from_them(void) {
    /* The whole contract between the parser and the handler: the line, then what bootreplay::parse( ) makes of the two fields. */
    struct Row { const char* line; bootreplay::Verb verb; int idx; int pct; };
    const Row rows[] = {
        { "screen boot",           bootreplay::V_LIST,     0,  0 },
        { "screen boot end",       bootreplay::V_END,      0,  0 },
        { "screen boot 3",         bootreplay::V_AT,       3,  0 },
        { "screen boot 3 50",      bootreplay::V_PROGRESS, 3, 50 },
        { "screen boot 12 100",    bootreplay::V_PROGRESS, 12, 100 },
        { "screen boot 3 CANCEL",  bootreplay::V_CANCEL,   3,  0 },
        { "screen boot 3 101",     bootreplay::V_BAD,      0,  0 },
        { "screen boot cancel",    bootreplay::V_BAD,      0,  0 },
        { "screen boot end 3",     bootreplay::V_BAD,      0,  0 },   /* `end` takes nothing after it: the word must reach parse( ), which refuses the pair */
        { "screen boot end cancel", bootreplay::V_BAD,     0,  0 },
    };
    for (const Row& r : rows) {
        CliDemand d = parse(r.line);
        TEST_ASSERT_EQUAL_MESSAGE(CMD_GOTO_SCREEN, d.type, r.line);
        const bootreplay::Cmd c = bootreplay::parse(d.strVal2, d.strVal3);
        TEST_ASSERT_EQUAL_MESSAGE(r.verb, c.verb, r.line);
        if (r.verb == bootreplay::V_AT || r.verb == bootreplay::V_PROGRESS || r.verb == bootreplay::V_CANCEL)
            TEST_ASSERT_EQUAL_INT_MESSAGE(r.idx, c.idx, r.line);
        if (r.verb == bootreplay::V_PROGRESS)
            TEST_ASSERT_EQUAL_INT_MESSAGE(r.pct, c.pct, r.line);
    }
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_is_unknown);
    RUN_TEST(test_garbage_unknown);
    RUN_TEST(test_help_variants);
    RUN_TEST(test_show_system_info);
    RUN_TEST(test_show_with_extra_spaces);
    RUN_TEST(test_clear_log_confirm);
    RUN_TEST(test_clear_log_no_confirm);
    RUN_TEST(test_user_perm_role);
    RUN_TEST(test_user_perm_hex_mask);
    RUN_TEST(test_user_perm_without_value_is_not_perm);
    RUN_TEST(test_user_policy_carries_all_three_values);
    RUN_TEST(test_user_policy_missing_alphabet_leaves_the_field_empty);
    RUN_TEST(test_conf_user_add);
    RUN_TEST(test_conf_user_add_missing_pass_still_parses);
    RUN_TEST(test_conf_user_del);
    RUN_TEST(test_sensor_scan);
    RUN_TEST(test_conf_tel_server_preserves_case);
    RUN_TEST(test_conf_tel_mode_json);
    RUN_TEST(test_sensor_slot_field);
    RUN_TEST(test_conf_sensor_tmax_invalid_gpio);
    RUN_TEST(test_conf_alone_unknown);
    RUN_TEST(test_show_alone_unknown);
    RUN_TEST(test_conf_system_factory);
    RUN_TEST(test_wifi_ssid_utf8_bytes);
    RUN_TEST(test_sensor_define_explicit_bme280);
    RUN_TEST(test_sensor_define_no_type_keeps_name_and_empty_strval3);
    RUN_TEST(test_sensor_define_type_only_no_name);
    RUN_TEST(test_sensor_remove_confirm);
    RUN_TEST(test_sensor_remove_without_confirm);
    RUN_TEST(test_system_format_confirm);
    RUN_TEST(test_long_line_still_parses_help_prefix);
    RUN_TEST(test_touch_hold_with_ms);
    RUN_TEST(test_touch_hold_without_ms_marks_intval_invalid);
    RUN_TEST(test_radio_commands_parse);
    RUN_TEST(test_screen_tag_alone_carries_only_the_tag);
    RUN_TEST(test_screen_boot_without_argument_lists);
    RUN_TEST(test_screen_boot_index_arrives_in_strval2);
    RUN_TEST(test_screen_boot_index_and_percent_arrive_in_strval2_and_3);
    RUN_TEST(test_screen_boot_index_and_cancel_arrive_lowercase);
    RUN_TEST(test_screen_boot_end_and_clkboot);
    RUN_TEST(test_screen_boot_end_with_a_word_after_it_hands_both_over_and_is_refused_there);
    RUN_TEST(test_screen_boot_an_invalid_word_reaches_the_handler_and_is_refused_there);
    RUN_TEST(test_screen_boot_words_mean_what_the_handler_reads_from_them);
    return UNITY_END();
}
