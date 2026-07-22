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

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_is_unknown);
    RUN_TEST(test_garbage_unknown);
    RUN_TEST(test_help_variants);
    RUN_TEST(test_show_system_info);
    RUN_TEST(test_show_with_extra_spaces);
    RUN_TEST(test_clear_log_confirm);
    RUN_TEST(test_clear_log_no_confirm);
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
    RUN_TEST(test_long_line_still_parses_help_prefix);
    return UNITY_END();
}
