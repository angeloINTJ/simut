/**
 * @file    test/test_lan/test_main.cpp
 * @brief   Host-side tests for the console parser of a wired build (the SIMUT
 *          Wire: SIMUT_NET_LAN=1, eleven sensor slots, the emergency console).
 * @details A Wire has no radio, so no setup access point, no SSID and no Wi-Fi
 *          password: `ap`, `system ssid` and `system pass` are not commands
 *          there. With them gone, the emergency console must still give a unit
 *          on a network without DHCP an address, so `ip` and `dns` reach it
 *          (2026-10-13). The rest of the emergency console is unchanged.
 *          The translated help follows: a pack's @HELPWIRE is this build's
 *          help, and its @HELP, written for the radio, is not.
 *          Runs via `pio test -e native_lan`.
 *
 * @project SIMUT
 * @license MIT License
 */

#include <unity.h>
#include "CommandParser.h"
#include "SystemDefs_Records.h"
#include "SystemDefs_Network.h"
#include "LangPackIndex.h"
#include <cstring>
#include <string>

namespace simut_native {
    uint32_t fake_millis_value = 0;
}

static CliDemand parse(const char* line) {
    return parseCliCommand(String(line));
}

void setUp(void) {}
void tearDown(void) {}

/* The build under test is the one it claims to be. */
void test_build_is_a_wired_emergency_console(void) {
    TEST_ASSERT_EQUAL(1, SIMUT_NET_LAN);
    TEST_ASSERT_EQUAL(0, SIMUT_NET_WIFI);
    TEST_ASSERT_EQUAL(0, SIMUT_CLI_FULL);
    TEST_ASSERT_EQUAL(11, MAX_SENSORS);
}

/* ---- what belongs to the radio is not a command ---- */

void test_ap_is_unknown(void) {
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("ap").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("apmode").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("ap-mode").type);
}

void test_ssid_and_pass_are_unknown(void) {
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("system ssid MyNet").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("system pass secret123").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("conf system ssid MyNet").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("wifi ssid MyNet").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("wifi pass secret123").type);
}

/* ---- the address, from the emergency console ---- */

void test_ip_mode(void) {
    CliDemand d = parse("ip dhcp");
    TEST_ASSERT_EQUAL(CMD_IP_CFG, d.type);
    TEST_ASSERT_EQUAL(0, d.intVal1);
    d = parse("ip static");
    TEST_ASSERT_EQUAL(CMD_IP_CFG, d.type);
    TEST_ASSERT_EQUAL(1, d.intVal1);
}

void test_ip_fields(void) {
    const struct { const char* line; int sub; const char* val; } cases[] = {
        { "ip addr 192.168.1.50",     2, "192.168.1.50" },
        { "ip mask 255.255.255.0",    3, "255.255.255.0" },
        { "ip gateway 192.168.1.1",   4, "192.168.1.1" },
        { "ip dns 1.1.1.1",           5, "1.1.1.1" },
        { "conf ip addr 10.0.0.7",    2, "10.0.0.7" },
    };
    for (const auto& c : cases) {
        CliDemand d = parse(c.line);
        TEST_ASSERT_EQUAL_MESSAGE(CMD_IP_CFG, d.type, c.line);
        TEST_ASSERT_EQUAL_MESSAGE(c.sub, d.intVal1, c.line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.val, d.strVal1, c.line);
    }
}

void test_dns(void) {
    CliDemand d = parse("dns auto");
    TEST_ASSERT_EQUAL(CMD_SET_DNS_CFG, d.type);
    TEST_ASSERT_EQUAL(0, d.intVal1);
    d = parse("dns manual 1.1.1.1 8.8.8.8");
    TEST_ASSERT_EQUAL(CMD_SET_DNS_CFG, d.type);
    TEST_ASSERT_EQUAL(1, d.intVal1);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", d.strVal1);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", d.strVal2);
    d = parse("net dns manual 9.9.9.9");
    TEST_ASSERT_EQUAL(CMD_SET_DNS_CFG, d.type);
    TEST_ASSERT_EQUAL(1, d.intVal1);
    TEST_ASSERT_EQUAL_STRING("9.9.9.9", d.strVal1);
}

/* ---- the rest of the full CLI stays out ---- */

void test_full_cli_commands_stay_unknown(void) {
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("ntp on").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("user add bob").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("write memory").type);
    TEST_ASSERT_EQUAL(CMD_UNKNOWN, parse("show sensors").type);
}

/* ---- the emergency console's other commands are where they were ---- */

void test_emergency_commands_unchanged(void) {
    TEST_ASSERT_EQUAL(CMD_HELP, parse("help").type);
    TEST_ASSERT_EQUAL(CMD_RELOAD, parse("reload").type);
    TEST_ASSERT_EQUAL(CMD_SHOW_NET, parse("show net status").type);
    TEST_ASSERT_EQUAL(CMD_SHOW_SYSINFO, parse("show system info").type);
    TEST_ASSERT_EQUAL(CMD_SHOW_LOGS, parse("show system log").type);
    TEST_ASSERT_EQUAL(CMD_RESET_ADMIN, parse("system admin reset").type);
    TEST_ASSERT_EQUAL(CMD_FACTORY_RESET, parse("system factory").type);
    TEST_ASSERT_EQUAL(CMD_FORMAT_FS, parse("system format").type);
    TEST_ASSERT_EQUAL(CMD_HTTPS_OFF, parse("system https off").type);
    TEST_ASSERT_EQUAL(CMD_SET_CORS_ORIGIN, parse("system cors off").type);
    TEST_ASSERT_EQUAL(CMD_SET_TIME, parse("time 2026-10-13 12:00:00").type);
}

/* ---- the translated help: @HELPWIRE, not @HELP ---- */

static LangPackIndex packIndex(const char* s) {
    LangPackScanner sc;
    sc.feed(s, strlen(s));
    LangPackIndex ix;
    sc.finish(ix);
    return ix;
}

static std::string packHelp(const char* s) {
    const LangPackIndex ix = packIndex(s);
    return std::string(s + ix.start[LANG_SEC_HELP], s + ix.end[LANG_SEC_HELP]);
}

void test_pack_help_is_helpwire(void) {
    TEST_ASSERT_EQUAL_STRING("cable\n",
        packHelp("@DICT\na\n@HELP\nradio\n@HELPWIRE\ncable\n@WEBDICT\n{}").c_str());
    TEST_ASSERT_EQUAL_STRING("cable\n",
        packHelp("@DICT\na\n@HELPWIRE\ncable\n@HELP\nradio\n@WEBDICT\n{}").c_str());
}

/* A pack from before @HELPWIRE: no help at all here, so `help` prints the
 * built-in English, which is the wired text, instead of the radio's commands
 * in Portuguese. The dictionary is where it was. */
void test_pack_without_helpwire_has_no_help(void) {
    const char* s = "@DICT\na\n@HELP\nradio\n@WEBDICT\n{}";
    const LangPackIndex ix = packIndex(s);
    TEST_ASSERT_FALSE(ix.present[LANG_SEC_HELP]);
    TEST_ASSERT_EQUAL_STRING("a\n", std::string(s + ix.start[LANG_SEC_DICT],
                                                s + ix.end[LANG_SEC_DICT]).c_str());
}

int main(int, char**) {
    UNITY_BEGIN( );
    RUN_TEST(test_build_is_a_wired_emergency_console);
    RUN_TEST(test_ap_is_unknown);
    RUN_TEST(test_ssid_and_pass_are_unknown);
    RUN_TEST(test_ip_mode);
    RUN_TEST(test_ip_fields);
    RUN_TEST(test_dns);
    RUN_TEST(test_full_cli_commands_stay_unknown);
    RUN_TEST(test_emergency_commands_unchanged);
    RUN_TEST(test_pack_help_is_helpwire);
    RUN_TEST(test_pack_without_helpwire_has_no_help);
    return UNITY_END( );
}
