/**
 * @file test_main.cpp
 * @brief The wired network path (the SIMUT Wire), driven against a fake of the
 *        ENC28J60 module.
 *
 * WHY THIS SUITE EXISTS
 * ---------------------
 * src/NetworkManager_Wired.cpp holds the NetworkManager methods a build with
 * SIMUT_NET_LAN=1 runs, and no test ran them: test_network compiles the Wi-Fi
 * path and test_lan the wired console. What the unit does when the chip never
 * answers, when the cable is pulled or when the lease changes was not exercised
 * on the host at all. This suite pins what the code does today, written before
 * the network becomes a driver, so that move is proved by behaviour that did
 * not change.
 *
 * WHAT THESE TESTS ASSERT
 * -----------------------
 * Observable behaviour only: the public API of NetworkManager, and what the
 * module was asked to do (how many times begin( ) was called, when, with which
 * arguments). Not the private states: a test that reads them passes while the
 * unit is unreachable, which is the failure a network manager is judged by.
 *
 * THE MODULE
 * ----------
 * net/WiredLink.h is all that stands between NetworkManager and the chip, so
 * this file DEFINES its functions: the module becomes a few knobs (what the
 * cable and the DHCP server are doing) and a few counters (what the manager
 * asked of it). WiredLink.cpp is not built here.
 *
 * @project SIMUT
 * @license MIT License
 */

#include <unity.h>
#include <string.h>
#include "NetworkManager.h"
#include "MetricsManager.h"
#include "net/WiredLink.h"
#include <lwip/dns.h>

#if !SIMUT_NET_LAN
#error "test_wired runs the wired path: build it with -DSIMUT_NET_LAN=1 (see [env:native_wired])"
#endif

/* ── the radio, and the handful of symbols NetworkManager links against ──── */

/* NetworkManager.cpp compiles its radio functions in this build too, so the
 * Wi-Fi stub is still linked. A wired unit never reaches it: here it is the
 * witness that nothing asked a radio that is not on the board. */
FakeWiFiClass WiFi;
FakeSerial Serial;

namespace simut_native { uint32_t fake_millis_value = 0; }

/* The log is not under test here; these keep the linker happy and record
 * nothing, because asserting on log text would tie the suite to wording. */
LogManager::LogManager( ) {}
void LogManager::logCode(LogLevel, const char*, LogCode, int, String) {}
const char* LogManager::tr(const char* en) const { return en; }
/* safeReboot( ) is [[noreturn]], so this stub must not return either (the same
 * stub in test_network records why: a return is undefined behaviour). In
 * NetworkManager only the radio's paths restart the unit, the AP limit and a
 * refused join, and a wired unit never reaches them: if a case ever gets here,
 * the throw stops the run instead of letting it go on. */
struct SafeRebootCalled {};
void LogManager::safeReboot( ) { throw SafeRebootCalled{ }; }
void MetricsManager::observeRssi(int32_t) {}
void dns_setserver(uint8_t, const ip_addr_t*) {}

/* ── the module: a fake of net/WiredLink.h ───────────────────────────────── */

/* Every begin( ) is counted, including the ones the real driver would ignore
 * after a success ("a second call does nothing", WiredLink.h): that the manager
 * never calls it twice for a cable that comes and goes is what the cases on
 * the cable are about, and a fake that swallowed the repeat would hide it. */
struct FakeModule {
    /* knobs: what the chip, the cable and the DHCP server are doing */
    bool beginFails = false;        /**< the chip does not answer: begin( ) says no */
    bool linked = false;            /**< the cable is in and the PHY sees a partner */
    bool hasAddress = false;        /**< the interface has been given an address */
    IPAddress ip, mask, gw, dns;    /**< what localIP( ) and its siblings answer */
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};

    /* observations: what the manager asked */
    bool started = false;
    unsigned beginCalls = 0;
    uint32_t beginAt[16] = {};      /**< millis( ) of each begin( ), the first 16 */
    char lastHostname[32] = "";
    IPAddress lastIp, lastGw, lastMask, lastDns;   /**< the arguments of the last begin( ) */
};
static FakeModule g_mod;

namespace WiredLink {

bool begin(const char* hostname, const IPAddress& ip, const IPAddress& gateway,
           const IPAddress& mask, const IPAddress& dns) {
    if (g_mod.beginCalls < sizeof(g_mod.beginAt) / sizeof(g_mod.beginAt[0]))
        g_mod.beginAt[g_mod.beginCalls] = millis( );
    g_mod.beginCalls++;
    strlcpy(g_mod.lastHostname, hostname ? hostname : "", sizeof(g_mod.lastHostname));
    g_mod.lastIp = ip; g_mod.lastGw = gateway; g_mod.lastMask = mask; g_mod.lastDns = dns;
    g_mod.started = !g_mod.beginFails;
    return !g_mod.beginFails;
}
bool started( ) { return g_mod.started; }
bool linkUp( ) { return g_mod.started && g_mod.linked; }
bool connected( ) { return g_mod.started && g_mod.linked && g_mod.hasAddress; }
IPAddress localIP( ) { return g_mod.ip; }
IPAddress subnetMask( ) { return g_mod.mask; }
IPAddress gatewayIP( ) { return g_mod.gw; }
IPAddress dnsIP( ) { return g_mod.dns; }
void macAddress(uint8_t mac[6]) { memcpy(mac, g_mod.mac, 6); }
const char* lastError( ) { return g_mod.beginFails ? "the chip does not answer" : ""; }
void end( ) { g_mod.started = false; }

}  // namespace WiredLink

/* ── driving the machine ─────────────────────────────────────────────────── */

/** Advance the clock by `ms`, calling update( ) every `stepMs` on the way.
 *
 * A wired state changes on the first update( ) after the module's answer does,
 * so the step only matters to the two things that wait on the clock: the retry
 * of a module that did not start, and the 30 s after which a link with no
 * address says so. 50 ms divides both, and the 500 ms of the long pumps does
 * too, so no transition is skipped and no gap is measured a step late. */
static void pump(NetworkManager& net, uint32_t ms, uint32_t stepMs = 50) {
    const uint32_t end = millis( ) + ms;
    while ((int32_t)(millis( ) - end) < 0) {
        set_native_millis(millis( ) + stepMs);
        net.update( );
    }
}

/** DHCP, as a unit nobody configured has it, and with two things filled that a
 *  wired unit must not use. The Wi-Fi fields: the configuration is one struct
 *  for both builds, and the radio is to be left alone whatever it finds in
 *  them. The static address: the factory default fills it in too, with DHCP on
 *  (StorageManager::loadDefaults( )), so every unit carries one it must not
 *  take. Its values are ones no case uses, so that a leak of them shows. */
static SystemConfig makeConfig( ) {
    SystemConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.wifiSsid, "bench-ap");
    strcpy(cfg.wifiPass, "hunter2hunter2");
    strcpy(cfg.deviceName, "simut-test");
    strcpy(cfg.ntpServer, "pool.ntp.org");
    cfg.useDhcp = true;
    strcpy(cfg.staticIp, "10.9.8.7");
    strcpy(cfg.staticMask, "255.255.0.0");
    strcpy(cfg.staticGateway, "10.9.8.1");
    strcpy(cfg.staticDns, "1.1.1.1");
    cfg.timezoneOffset = -3;
    return cfg;
}

/** A manager that has reached NET_READY on a live cable at 192.168.3.40, which
 *  is where every scenario after the way up starts. NTP is off, so READY needs
 *  no time server and the host's clock does not matter. */
static void bringUp(NetworkManager& net) {
    g_mod.linked = g_mod.hasAddress = true;
    g_mod.ip = IPAddress(192, 168, 3, 40);
    net.begin(makeConfig( ), /*dnsAuto=*/true, /*ntpEnabled=*/false, "");
    pump(net, 3000);
    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "setup: never reached NET_READY");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_mod.beginCalls, "setup: the module was started more than once");
}

/** Compare an address as the dotted text it prints, so that a failure shows
 *  the address and not a 32-bit number. */
static void expectIp(const char* expected, const IPAddress& actual, const char* what) {
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, actual.toString( ).c_str( ), what);
}

void setUp(void) {
    set_native_millis(100000);   /* away from 0: the constructor's timers are 0, and must not look fresh */
    WiFi.reset( );
    g_mod = FakeModule( );
    MetricsManager::instance( ).data( ).wifiReconnects = 0;
}
void tearDown(void) {}

/* ══ the module that does not start ══════════════════════════════════════ */

/* A missing or miswired ENC28J60 answers begin( ) in milliseconds, so a minute
 * between tries costs nothing (NetworkManager_Wired.cpp). The unit keeps
 * trying for as long as it takes, on a ladder that starts at 10 s, doubles on
 * every failure and stops growing at that minute. The gaps are what is pinned:
 * the ladder must neither stay at its first step nor stop, because a unit that
 * gave up would stay off the network until somebody noticed. */
static void test_a_module_that_does_not_start_is_retried_with_doubling_gaps(void) {
    g_mod.beginFails = true;

    NetworkManager net;
    net.begin(makeConfig( ), /*dnsAuto=*/true, /*ntpEnabled=*/false, "");
    TEST_ASSERT_EQUAL_UINT(1, g_mod.beginCalls);
    pump(net, 4UL * 60UL * 1000UL, 500);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(6, g_mod.beginCalls,
        "four minutes of failures should hold six starts: at 0, 10, 30, 70, 130 and 190 s");
    static const uint32_t expectedGapMs[] = { 10000, 20000, 40000, 60000, 60000 };
    for (unsigned i = 0; i < 5; i++) {
        const uint32_t gap = g_mod.beginAt[i + 1] - g_mod.beginAt[i];
        char what[64];
        snprintf(what, sizeof(what), "gap %u between two starts, expected %u ms",
                 i + 1, (unsigned)expectedGapMs[i]);
        TEST_ASSERT_UINT32_WITHIN_MESSAGE(1000, expectedGapMs[i], gap, what);
    }
    TEST_ASSERT_FALSE(net.isConnected( ));
    TEST_ASSERT_FALSE(net.isLinkUp( ));

    /* The chip answers at last, with the cable in. The next retry is what
     * brings the unit up, and it is the only start it needs. */
    g_mod.beginFails = false;
    g_mod.linked = g_mod.hasAddress = true;
    g_mod.ip = IPAddress(192, 168, 3, 40);
    const unsigned startsBefore = g_mod.beginCalls;
    pump(net, 60000, 500);

    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "the retry never brought the unit up");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(startsBefore + 1, g_mod.beginCalls,
        "a module that has started must not be started again");
}

/* ══ the way up ══════════════════════════════════════════════════════════ */

/* Cable in, address given: the unit is on the network. DHCP is on, so the
 * module is started with no address at all, though the configuration holds one
 * (makeConfig( )): an address passed here would be a static one, silently. */
static void test_link_and_address_reach_ready(void) {
    g_mod.linked = g_mod.hasAddress = true;
    g_mod.ip = IPAddress(192, 168, 3, 40);

    NetworkManager net;
    net.begin(makeConfig( ), /*dnsAuto=*/true, /*ntpEnabled=*/false, "");
    pump(net, 3000);

    TEST_ASSERT_TRUE(net.isConnected( ));
    TEST_ASSERT_TRUE(net.isLinkUp( ));
    TEST_ASSERT_EQUAL_STRING("192.168.3.40", net.getIpAddress( ).c_str( ));

    /* The buffer overload is what the panel's status screen calls. */
    char buf[16];
    net.getIpAddress(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("192.168.3.40", buf);

    TEST_ASSERT_EQUAL_UINT(1, g_mod.beginCalls);
    expectIp("0.0.0.0", g_mod.lastIp, "ip");
    expectIp("0.0.0.0", g_mod.lastGw, "gateway");
    expectIp("0.0.0.0", g_mod.lastMask, "mask");
    expectIp("0.0.0.0", g_mod.lastDns, "dns");
}

/* ══ the cable comes and goes ════════════════════════════════════════════ */

/* A cable out is not a module gone. The interface stays up and lwIP renews the
 * lease when the link returns (NetworkManager_Wired.cpp), so the manager goes
 * back to waiting and nothing more: starting the module again is for a module
 * that did not start, and the real driver would ignore it anyway. The count of
 * begin( ) calls is what tells a unit that waited from one that restarted. */
static void test_a_pulled_cable_goes_back_to_waiting_and_returns_without_restarting_the_module(void) {
    NetworkManager net;
    bringUp(net);

    g_mod.linked = false;                  /* somebody pulls the cable */
    pump(net, 2000);
    TEST_ASSERT_FALSE(net.isConnected( ));
    TEST_ASSERT_FALSE(net.isLinkUp( ));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_mod.beginCalls, "restarted the module while the cable was out");

    g_mod.linked = true;                   /* and puts it back */
    pump(net, 2000);
    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "never came back with the cable");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_mod.beginCalls, "restarted the module when the cable came back");
    TEST_ASSERT_TRUE(g_mod.started);
}

/* The lease changes under a link that never went down, or the cable comes
 * back on another network after lwIP kept the old address. getIpAddress( )
 * reads the module on every call, so it answers the new address whether or not
 * the manager noticed. What tells the two apart is the announcement: the new
 * address is treated as one that has just arrived (the log, the DNS, mDNS, the
 * clock) and counted in wifiReconnects, once. */
static void test_a_lease_change_under_a_live_link_is_followed(void) {
    NetworkManager net;
    bringUp(net);
    const uint32_t announcedBefore = MetricsManager::instance( ).data( ).wifiReconnects;

    g_mod.ip = IPAddress(192, 168, 3, 77);  /* a new lease; the link stays up */
    pump(net, 2000);

    TEST_ASSERT_TRUE(net.isConnected( ));
    TEST_ASSERT_EQUAL_STRING("192.168.3.77", net.getIpAddress( ).c_str( ));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_mod.beginCalls, "a new lease restarted the module");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(announcedBefore + 1,
        MetricsManager::instance( ).data( ).wifiReconnects,
        "the new address was not announced exactly once");
}

/* ══ what a cable has instead of a radio ═════════════════════════════════ */

/* A cable has no signal strength. A link that is up answers with a stand-in,
 * and -100 means no network, as on Wi-Fi: the screens draw the cable from it.
 * The stand-in has to clear RSSI_MIN_THRESHOLD, the floor isNetworkHealthy( )
 * applies, or a wired unit on a good cable would be refused the heavy work
 * (telemetry, uploads) that gate guards. */
static void test_signal_stand_in_is_minus_40_when_ready_and_minus_100_otherwise(void) {
    NetworkManager net;
    TEST_ASSERT_EQUAL_INT32_MESSAGE(-100, net.getRssi( ), "before begin( )");

    g_mod.linked = true;                   /* the cable is in, no lease yet */
    g_mod.ip = IPAddress(192, 168, 3, 40);
    net.begin(makeConfig( ), /*dnsAuto=*/true, /*ntpEnabled=*/false, "");
    pump(net, 3000);
    TEST_ASSERT_FALSE(net.isConnected( ));
    TEST_ASSERT_EQUAL_INT32_MESSAGE(-100, net.getRssi( ), "waiting for the address");
    TEST_ASSERT_FALSE(net.isNetworkHealthy( ));

    g_mod.hasAddress = true;               /* the lease arrives */
    pump(net, 3000);
    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "setup: never reached NET_READY");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(-40, net.getRssi( ), "ready");
    TEST_ASSERT_TRUE(net.isNetworkHealthy( ));

    g_mod.linked = false;                  /* the cable is pulled */
    pump(net, 2000);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(-100, net.getRssi( ), "after the cable is pulled");
    TEST_ASSERT_FALSE(net.isNetworkHealthy( ));
}

/* The Wire is configured over the cable or the console, so a request for the
 * setup AP or for a scan has nothing to answer with. Refusing has to be all it
 * does: the unit stays on the network, and nothing reaches the radio stub
 * (the Wi-Fi fields of the configuration are filled, see makeConfig( )). */
static void test_no_access_point_and_no_scan_on_a_cable(void) {
    NetworkManager net;
    bringUp(net);

    TEST_ASSERT_FALSE(net.beginAP("x"));
    TEST_ASSERT_FALSE(net.startScan( ));
    TEST_ASSERT_EQUAL_STRING("", net.getApPsk( ));
    TEST_ASSERT_EQUAL_STRING("", net.getApSsid( ));
    TEST_ASSERT_FALSE(net.isApConfig( ));
    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "asking for an AP took the unit off the network");

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, WiFi.scanStarts, "a scan reached the radio");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, WiFi.scanDeletes, "a scan result was cleared on the radio");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, WiFi.joinAttempts, "a join reached the radio");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, WiFi.disconnects, "a disconnect reached the radio");
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_OFF, WiFi.lastMode, "the radio was switched to a mode");
}

/* ══ what the module is told, and what it says ═══════════════════════════ */

/** What the module was last started with: the hostname, then the static address
 *  of test_a_static_address_reaches_the_module_in_order. */
static void expectStaticStart(void) {
    TEST_ASSERT_EQUAL_STRING("simut-test", g_mod.lastHostname);
    expectIp("192.168.3.40", g_mod.lastIp, "ip");
    expectIp("192.168.3.1", g_mod.lastGw, "gateway");
    expectIp("255.255.255.0", g_mod.lastMask, "mask");
    expectIp("8.8.8.8", g_mod.lastDns, "dns");
}

/* The four addresses are distinct on purpose, so that two of them swapped on
 * the way to the module cannot pass. WiredLink::begin( ) takes (ip, gateway,
 * mask, dns) and the Wi-Fi stack's WiFi.config( ) takes (ip, dns, gateway,
 * mask): NetworkManager::begin( ) carries a comment on what the wrong order did
 * there, a mask that was the DNS address and a radio that never associated.
 *
 * And the address is kept for the retries of a module that did not start
 * (NetworkManager.h): a retry that started it with none would turn a unit
 * configured for a static address into a DHCP one, with nothing to say so. */
static void test_a_static_address_reaches_the_module_in_order(void) {
    SystemConfig cfg = makeConfig( );
    cfg.useDhcp = false;
    strcpy(cfg.staticIp, "192.168.3.40");
    strcpy(cfg.staticMask, "255.255.255.0");
    strcpy(cfg.staticGateway, "192.168.3.1");
    strcpy(cfg.staticDns, "8.8.8.8");

    NetworkManager net;
    net.begin(cfg, /*dnsAuto=*/true, /*ntpEnabled=*/false, "");

    TEST_ASSERT_EQUAL_UINT(1, g_mod.beginCalls);
    expectStaticStart( );

    /* A second unit, whose first start fails and whose retry succeeds. */
    g_mod = FakeModule( );
    g_mod.beginFails = true;
    NetworkManager again;
    again.begin(cfg, /*dnsAuto=*/true, /*ntpEnabled=*/false, "");
    TEST_ASSERT_EQUAL_UINT(1, g_mod.beginCalls);
    expectStaticStart( );

    g_mod.beginFails = false;
    pump(again, 11000, 500);                /* the first retry is due 10 s after the failure */
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2, g_mod.beginCalls, "the retry never came");
    TEST_ASSERT_TRUE(g_mod.started);
    expectStaticStart( );
}

/* The MAC is the board's, not the chip's, so it answers before the cable does.
 * Upper case and colons: the format WiFi.macAddress( ) has on the Wi-Fi images,
 * so the two builds print the same kind of text. */
static void test_the_mac_is_upper_case_colon_separated(void) {
    static const uint8_t mac[6] = { 0x2a, 0x15, 0xe3, 0x4e, 0x18, 0x24 };
    memcpy(g_mod.mac, mac, sizeof(mac));

    NetworkManager net;
    TEST_ASSERT_EQUAL_STRING("2A:15:E3:4E:18:24", net.getMacAddress( ).c_str( ));

    char buf[18];
    net.getMacAddress(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2A:15:E3:4E:18:24", buf);
}

int main(int, char**) {
    UNITY_BEGIN( );

    RUN_TEST(test_a_module_that_does_not_start_is_retried_with_doubling_gaps);

    RUN_TEST(test_link_and_address_reach_ready);

    RUN_TEST(test_a_pulled_cable_goes_back_to_waiting_and_returns_without_restarting_the_module);
    RUN_TEST(test_a_lease_change_under_a_live_link_is_followed);

    RUN_TEST(test_signal_stand_in_is_minus_40_when_ready_and_minus_100_otherwise);
    RUN_TEST(test_no_access_point_and_no_scan_on_a_cable);

    RUN_TEST(test_a_static_address_reaches_the_module_in_order);
    RUN_TEST(test_the_mac_is_upper_case_colon_separated);

    return UNITY_END( );
}
