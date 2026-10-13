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
 *          help, and its @HELP, written for the radio, is not. So does the
 *          panel's Settings menu: no Configuration Mode row. And the log
 *          names the cable's events with codes of their own, which the
 *          edge-triggered filter treats as the radio's network family.
 *          Runs via `pio test -e native_lan`.
 *
 * @project SIMUT
 * @license MIT License
 */

#include <unity.h>
/* The two pure headers that test SIMUT_NET_LAN come FIRST, before anything that
 * brings simut_config.h (CommandParser.h and the SystemDefs_*.h do). Each has to
 * include net/NetCaps.h itself: an undefined macro reads as 0, and a wired build
 * would be given the radio's help and the radio's menu with nothing to say so.
 * Whatever comes earlier defines the macros for the headers after it and hides
 * the loss, which is how no suite noticed one. That also limits this order to
 * the first of the two: it is LangPackIndex.h that the pack cases below can
 * catch. display/SettingsMenu.h is first in a unit of its own,
 * standalone_settings_menu.cpp. */
#include "LangPackIndex.h"
#include "display/SettingsMenu.h"
#include "CommandParser.h"
#include "SystemDefs_Records.h"
#include "SystemDefs_Network.h"
#include "LogPolicy.h"
#include "SystemDefs_Logging.h"
#include <cstring>
#include <string>
#include <vector>

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

/* ---- the panel's Settings menu: no setup access point ---- */

static std::vector<int> menuFor(uint16_t perms) {
    uint8_t out[MENU_ITEM_COUNT];
    const uint8_t n = settingsMenuBuild(perms, out);
    return std::vector<int>(out, out + n);
}

static void assertMenu(const std::vector<int>& want, uint16_t perms) {
    const std::vector<int> got = menuFor(perms);
    TEST_ASSERT_EQUAL_UINT(want.size( ), got.size( ));
    TEST_ASSERT_EQUAL_INT_ARRAY(want.data( ), got.data( ), want.size( ));
}

void test_menu_has_no_setup_ap(void) {
    /* The ids are the radio build's: EVT_MENU_SELECT and the row icon key on
     * them. Only the row goes. */
    TEST_ASSERT_EQUAL_INT(11, MENU_SETUP_AP);
    TEST_ASSERT_EQUAL_INT(12, MENU_CLOCK);
    TEST_ASSERT_EQUAL_INT(13, MENU_ITEM_COUNT);
    TEST_ASSERT_EQUAL_INT(12, MENU_BUILD_COUNT);
    const std::vector<int> all = {
        MENU_ALARMS, MENU_SOUNDS, MENU_STATUS, MENU_CLOCK,
        MENU_OWN_PIN, MENU_USERS, MENU_PIN_POLICY,
        MENU_LANG, MENU_THEMES, MENU_TOUCH_CAL, MENU_DISPLAY_OFFSET,
        MENU_LICENSE,
    };
    assertMenu(all, 0xFFFF);
    assertMenu(all, 0x1FFF);
    /* The bit that opened only the setup AP opens nothing here. */
    assertMenu({ MENU_STATUS, MENU_OWN_PIN, MENU_LICENSE }, PERM_NET_CONFIG);
}

void test_menu_numbers_every_row_this_build_has(void) {
    char buf[40];
    /* Twelve rows are the whole menu here, so they are numbered, 1 to 12... */
    TEST_ASSERT_EQUAL_STRING("1. Alarm Limits",
        settingsMenuRowText("Alarm Limits", 0, MENU_BUILD_COUNT, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("12. License",
        settingsMenuRowText("License", 11, MENU_BUILD_COUNT, buf, sizeof(buf)));
    /* ...and a list filtered by the session's bits still numbers nothing. */
    TEST_ASSERT_EQUAL_STRING("License",
        settingsMenuRowText("License", 2, 3, buf, sizeof(buf)));
}

/* ---- the cable's log codes, through the edge-triggered filter ----
 * A fault is persisted when the family was healthy, its repeats are filtered,
 * and the first routine record after it is the recovery. The fault has to be
 * listed in the filter's table for that last part: a code it does not know is
 * persisted every time and arms nothing, so the address that comes back would
 * read as routine and be dropped. */

static const uint8_t LVL_INFO = 1;   /* LogLevel, as test/test_log_policy mirrors it */
static const uint8_t LVL_WARN = 2;
static const uint8_t LVL_ERROR = 3;

static void assertNetFault(uint16_t fault, uint8_t level) {
    LogPolicy pol;
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_IP_ACQUIRED, LVL_INFO, 1000));
    TEST_ASSERT_TRUE(pol.shouldPersist(fault, level, 2000));
    TEST_ASSERT_FALSE(pol.shouldPersist(fault, level, 3000));
    TEST_ASSERT_TRUE(pol.shouldPersist(SYS_IP_ACQUIRED, LVL_INFO, 4000));
    TEST_ASSERT_FALSE(pol.shouldPersist(SYS_IP_ACQUIRED, LVL_INFO, 5000));
}

void test_cable_faults_are_network_faults(void) {
    assertNetFault(NET_WIRED_LOST, LVL_WARN);
    assertNetFault(NET_WIRED_NO_ADDRESS, LVL_WARN);
    assertNetFault(NET_WIRED_MODULE_FAIL, LVL_ERROR);
}

/* The codes sit in the network range, next to the radio's, and are new ones:
 * a code is never reused, because a log already on a device reads by it. */
void test_cable_codes_are_their_own(void) {
    TEST_ASSERT_EQUAL(529, NET_WIRED_STARTING);
    TEST_ASSERT_EQUAL(530, NET_WIRED_MODULE_FAIL);
    TEST_ASSERT_EQUAL(531, NET_WIRED_NO_ADDRESS);
    TEST_ASSERT_EQUAL(532, NET_WIRED_LOST);
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
    RUN_TEST(test_menu_has_no_setup_ap);
    RUN_TEST(test_menu_numbers_every_row_this_build_has);
    RUN_TEST(test_cable_faults_are_network_faults);
    RUN_TEST(test_cable_codes_are_their_own);
    return UNITY_END( );
}
