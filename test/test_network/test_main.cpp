/**
 * @file test_main.cpp
 * @brief The WiFi reconnect state machine, driven against a controllable radio.
 *
 * WHY THIS SUITE EXISTS
 * ---------------------
 * On 2026-09-08 a device in the field lost its link at 11:54:47, started one
 * scan six seconds later, and then emitted nothing at all for 3 h 41 min while
 * the rest of it stayed healthy. It came back when an operator power-cycled
 * it. The cause was found by reading that device's log, because there was no
 * test that could have found it — NetworkManager.cpp had none.
 *
 * The failure was a scan that never completed, in a state that returned on
 * scanComplete( ) == -1 with nothing to bound the wait. It is now
 * `WiFi.scanNeverCompletes` in the stub, and it is asserted on below.
 *
 * WHAT THESE TESTS ASSERT
 * -----------------------
 * Observable behaviour only: did it scan, did it try to associate, is it
 * connected. Not the private ladder counters — a test that reaches into those
 * passes while the device stays dark, which is exactly the failure being
 * guarded against. `joinAttempts` is the important one: the field bug was not
 * that the device joined badly, it is that it never tried.
 *
 * @project SIMUT
 * @license MIT License
 */

#include <unity.h>
#include <string.h>
#include "NetworkManager.h"
#include "MetricsManager.h"
#include <lwip/dns.h>
#include <vector>
#include <algorithm>

/* ── the radio, and the handful of symbols NetworkManager links against ──── */

FakeWiFiClass WiFi;
FakeSerial Serial;

/* Counted by the pico/cyw43_arch.h stub: a scan from AP mode has to bring the
 * STA interface up BEFORE the sweep, and the ordering is the point. */
unsigned g_cyw43StaModeEnables = 0;

namespace simut_native { uint32_t fake_millis_value = 0; }

/* The log is not under test here; these keep the linker happy and record
 * nothing, because asserting on log text would tie the suite to wording. */
LogManager::LogManager( ) {}
void LogManager::logCode(LogLevel, const char*, LogCode, int, String) {}
const char* LogManager::tr(const char* en) const { return en; }
void LogManager::safeReboot( ) {}
void MetricsManager::observeRssi(int32_t) {}
void dns_setserver(uint8_t, const ip_addr_t*) {}

/* ── driving the machine ─────────────────────────────────────────────────── */

/** Advance the clock by `ms`, calling update( ) every `stepMs` on the way.
 *
 * The step matters: NET_DISCONNECT_PENDING settles after 200 ms and the
 * connect attempt times out at 20 s, so a step that is too coarse can skip a
 * transition and a test would then pass or fail for the wrong reason. 50 ms
 * divides every interval in the machine. */
static void pump(NetworkManager& net, uint32_t ms, uint32_t stepMs = 50) {
    const uint32_t end = millis( ) + ms;
    while ((int32_t)(millis( ) - end) < 0) {
        set_native_millis(millis( ) + stepMs);
        net.update( );
    }
}

/** Pump, recording the clock at every association attempt.
 *
 * The gaps between attempts are what distinguish a dormancy that ends from one
 * that does not: the old behaviour retried every ten minutes forever, so it
 * would eventually reconnect too — testing only "does it come back" would pass
 * against the bug. What cannot happen under a permanent dormancy is two
 * attempts close together. */
static void pumpRecording(NetworkManager& net, uint32_t ms,
                          std::vector<uint32_t>& attemptsAt, uint32_t stepMs = 500) {
    const uint32_t end = millis( ) + ms;
    unsigned seen = WiFi.joinAttempts;
    while ((int32_t)(millis( ) - end) < 0) {
        set_native_millis(millis( ) + stepMs);
        net.update( );
        if (WiFi.joinAttempts != seen) { seen = WiFi.joinAttempts; attemptsAt.push_back(millis( )); }
    }
}

static SystemConfig makeConfig( ) {
    SystemConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.wifiSsid, "bench-ap");
    strcpy(cfg.wifiPass, "hunter2hunter2");
    strcpy(cfg.deviceName, "simut-test");
    strcpy(cfg.ntpServer, "pool.ntp.org");
    cfg.useDhcp = true;
    cfg.timezoneOffset = -3;
    return cfg;
}

/** A manager that has reached NET_READY, which is where every reconnect
 *  scenario starts. NTP is off so the test does not depend on host time. */
static void bringUp(NetworkManager& net) {
    net.begin(makeConfig( ), /*dnsAuto=*/true, /*ntpEnabled=*/false, "");
    pump(net, 3000);
    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "setup: never reached NET_READY");
}

void setUp(void) {
    set_native_millis(100000);   /* away from 0, so wrap arithmetic is exercised */
    WiFi.reset( );
    g_cyw43StaModeEnables = 0;
}
void tearDown(void) {}

/* ══ the boot path ═══════════════════════════════════════════════════════ */

/* Boot associates without asking anyone's permission — no scan first. That
 * asymmetry with the reconnect path is what made a reboot the only cure. */
static void test_boot_associates_without_scanning_first(void) {
    NetworkManager net;
    net.begin(makeConfig( ), true, false, "");
    TEST_ASSERT_EQUAL_UINT(1, WiFi.joinAttempts);
    TEST_ASSERT_EQUAL_UINT(0, WiFi.scanStarts);
    pump(net, 3000);
    TEST_ASSERT_TRUE(net.isConnected( ));
}

static void test_a_missing_ssid_does_not_start_anything(void) {
    NetworkManager net;
    SystemConfig cfg = makeConfig( );
    cfg.wifiSsid[0] = '\0';
    net.begin(cfg, true, false, "");
    pump(net, 30000);
    TEST_ASSERT_EQUAL_UINT(0, WiFi.joinAttempts);
    TEST_ASSERT_FALSE(net.isConnected( ));
}

/* ══ an ordinary drop ════════════════════════════════════════════════════ */

static void test_a_dropped_link_comes_back_on_its_own(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.disconnect( );                    /* the AP goes away underneath us */
    const unsigned joinsBefore = WiFi.joinAttempts;

    pump(net, 60000);

    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ), "never reconnected");
    TEST_ASSERT_GREATER_THAN_UINT(joinsBefore, WiFi.joinAttempts);
}

static void test_a_drop_is_noticed_without_being_told(void) {
    NetworkManager net;
    bringUp(net);
    WiFi.disconnect( );
    pump(net, 10000);
    TEST_ASSERT_GREATER_THAN_UINT(0, WiFi.scanStarts);
}

/* ══ the 2026-09-08 field failure ════════════════════════════════════════ */

/* THE regression test. A scan that never completes used to park the state
 * machine for the rest of the boot: no further scans, no association attempt,
 * nothing in the log, and only a power cycle to escape. */
static void test_a_scan_that_never_finishes_does_not_park_the_device(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.scanNeverCompletes = true;
    WiFi.disconnect( );

    pump(net, 120000);

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        1, WiFi.scanStarts,
        "the machine started one scan and then stopped — this is the field bug");
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        0, WiFi.joinAttempts,
        "it never tried to associate; the deadline alone does not reconnect");
}

/* And it does not merely try — it gets back. The blind join is the escape,
 * because WiFi.begin( ) does not read the wedged scanner state. */
static void test_it_reconnects_even_while_the_scanner_stays_wedged(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.scanNeverCompletes = true;        /* the scanner never recovers */
    WiFi.disconnect( );

    pump(net, 120000);

    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ),
        "a wedged scanner must not keep a joinable AP out of reach");
}

/* A scan that fails outright is the same case, and must not be a dead end. */
static void test_a_failed_scan_is_treated_as_a_fruitless_one(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.scanFailsToStart = true;
    WiFi.disconnect( );

    pump(net, 120000);
    TEST_ASSERT_TRUE(net.isConnected( ));
}

/* ══ joinable but invisible ══════════════════════════════════════════════ */

/* The weak-signal and hidden-SSID case: the AP answers a join but does not
 * show up in a scan. The old reconnect path refused to try, forever. */
static void test_an_ap_that_no_scan_lists_is_still_joined(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.visible = false;                  /* hidden, or simply too far to hear */
    WiFi.joinable = true;
    WiFi.disconnect( );

    pump(net, 120000);

    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ),
        "a hidden SSID must reconnect, as it does at boot");
}

/* The scan is still worth doing first: it is cheap, and a visible AP should be
 * joined off the back of it rather than after two wasted rounds. */
static void test_a_visible_ap_is_joined_off_the_first_scan(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.disconnect( );
    const unsigned joinsBefore = WiFi.joinAttempts;
    pump(net, 20000);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, WiFi.scanStarts,
        "one scan should have been enough for a visible AP");
    TEST_ASSERT_EQUAL_UINT(joinsBefore + 1, WiFi.joinAttempts);
}

/* ══ giving up, and un-giving up ═════════════════════════════════════════ */

/* With nothing on the air, the device must not spin: attempts get further
 * apart, and the consecutive-failure counter SIMUT Air reads keeps climbing. */
static void test_an_absent_network_backs_off_instead_of_spinning(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.visible = false;
    WiFi.joinable = false;                 /* the network is genuinely gone */
    WiFi.disconnect( );

    pump(net, 200000);
    const unsigned attemptsInFirstStretch = WiFi.joinAttempts;
    TEST_ASSERT_GREATER_THAN_UINT(0, attemptsInFirstStretch);
    TEST_ASSERT_GREATER_THAN_UINT(0, net.getConnectCycles( ));

    /* Each attempt costs a 20 s connect timeout, so an unbounded retry loop
     * would show far more than this in 200 s. */
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(12, attemptsInFirstStretch,
        "backing off is not happening — the ladder is not growing");
}

/* Dormancy is a rest, not a retirement. It used to be entered and never left,
 * because the counter that opened it was only ever cleared by success — so an
 * access point that came back was found on a ten-minute grid at best.
 *
 * Note what is asserted, and what is not: "does it eventually reconnect" would
 * pass against the old behaviour too, since a ten-minute retry does come round.
 * The property that only the fix has is a SHORT gap between two attempts after
 * dormancy has already been entered — proof the ladder restarted rather than
 * staying pinned. */
static void test_dormancy_ends_and_the_ladder_restarts(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.visible = false;
    WiFi.joinable = false;                 /* the network is genuinely gone */
    WiFi.disconnect( );

    std::vector<uint32_t> at;
    /* Long enough to exhaust the fast ladder, serve every dormant wait, and
     * come out the other side. */
    pumpRecording(net, 2 * (WIFI_DORMANT_MAX_WAITS + 2) * WIFI_DORMANT_DELAY_MS, at);

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(WIFI_MAX_CONNECT_CYCLES, at.size( ),
        "never got past the fast ladder into dormancy");

    /* Gaps after dormancy was entered. Under a permanent dormancy every one of
     * them is a dormant delay; the escape puts a short one among them. */
    bool sawShortGapAfterDormancy = false;
    for (size_t i = WIFI_MAX_CONNECT_CYCLES; i + 1 < at.size( ); i++) {
        if (at[i + 1] - at[i] < WIFI_DORMANT_DELAY_MS / 2) sawShortGapAfterDormancy = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawShortGapAfterDormancy,
        "every attempt after dormancy was a dormant delay apart — it never left");
}

/* And once it has left, an access point that comes back is found without
 * waiting out another dormant stretch. */
static void test_an_ap_that_returns_is_found_again(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.visible = false;
    WiFi.joinable = false;
    WiFi.disconnect( );
    pump(net, (WIFI_DORMANT_MAX_WAITS + 2) * WIFI_DORMANT_DELAY_MS, 500);

    WiFi.visible = true;
    WiFi.joinable = true;
    /* Generous on purpose: the worst case is arriving just after a dormant
     * wait began, and that ceiling is the deliberate battery trade-off. */
    pump(net, WIFI_DORMANT_DELAY_MS + 120000, 500);

    TEST_ASSERT_TRUE_MESSAGE(net.isConnected( ),
        "an access point that came back was never found again");
}

/* The consecutive-failure counter is what SIMUT Air reads to stop pumping the
 * network inside a wake. It must not roll over to zero and retract that. */
static void test_the_failure_counter_saturates_instead_of_wrapping(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.visible = false;
    WiFi.joinable = false;
    WiFi.disconnect( );

    uint8_t highest = 0;
    for (int i = 0; i < 400; i++) {
        pump(net, 30000, 500);
        const uint8_t c = net.getConnectCycles( );
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8_MESSAGE(highest, c,
            "the failure counter went backwards without a success");
        highest = c;
        if (highest == 255) break;
    }
    TEST_ASSERT_GREATER_THAN_UINT8(0, highest);
}

/* A connection clears the slate, so the next outage starts from short delays
 * rather than inheriting a backoff earned by a network that is no longer the
 * one in front of us. */
static void test_a_success_clears_the_failure_counter(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.visible = false;
    WiFi.joinable = false;
    WiFi.disconnect( );
    pump(net, 90000);
    TEST_ASSERT_GREATER_THAN_UINT(0, net.getConnectCycles( ));

    WiFi.visible = true;
    WiFi.joinable = true;
    pump(net, 180000, 500);

    TEST_ASSERT_TRUE(net.isConnected( ));
    TEST_ASSERT_EQUAL_UINT8(0, net.getConnectCycles( ));
}

/* A second outage after a recovery must be handled as briskly as the first —
 * the observable proof that the ladder was reset and not merely capped. */
static void test_a_second_outage_recovers_as_fast_as_the_first(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.disconnect( );
    const uint32_t t0 = millis( );
    pump(net, 60000);
    const uint32_t firstRecovery = millis( ) - t0;
    TEST_ASSERT_TRUE(net.isConnected( ));

    WiFi.disconnect( );
    const uint32_t t1 = millis( );
    pump(net, 60000);
    const uint32_t secondRecovery = millis( ) - t1;
    TEST_ASSERT_TRUE(net.isConnected( ));

    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(firstRecovery + 5000, secondRecovery,
        "the second recovery was slower — the ladder did not reset");
}

/* ══ the radio is not asked for nonsense ═════════════════════════════════ */

/* An implausible RSSI means the driver is no longer answering with real data.
 * Reporting it as a healthy signal is worse than reporting nothing, because
 * isNetworkHealthy( ) would wave heavy work through onto a dead link. */
static void test_an_impossible_signal_reading_is_not_reported_as_health(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.rssi = 4;                         /* positive dBm: measured on the bench */
    TEST_ASSERT_FALSE(net.isNetworkHealthy( ));

    WiFi.rssi = -60;
    TEST_ASSERT_TRUE(net.isNetworkHealthy( ));

    WiFi.rssi = -95;                       /* plausible, but too weak to work */
    TEST_ASSERT_FALSE(net.isNetworkHealthy( ));
}


/* ══ the scan the web page asks for ══════════════════════════════════════
 *
 * This is a SECOND scanner, beside the reconnect one, and the two must not
 * fight over the radio. It exists because of AP mode: a device that has never
 * been configured shows a setup page over its own access point, and until now
 * the SSID had to be typed from memory there — the one place the device is
 * guaranteed not to be on the network whose name is being asked for.
 */

/** Drive only the scan poller, which is what the web request does. */
static void pumpScan(NetworkManager& net, uint32_t ms, uint32_t stepMs = 50) {
    const uint32_t end = millis( ) + ms;
    while ((int32_t)(millis( ) - end) < 0) {
        set_native_millis(millis( ) + stepMs);
        net.update( );
    }
}

static void test_a_scan_lists_what_the_air_carries(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.nets = { {"far",   -80, 4, 1},
                  {"near",  -40, 4, 6},
                  {"middle",-60, 0, 11} };

    TEST_ASSERT_TRUE(net.startScan( ));
    TEST_ASSERT_EQUAL_UINT(NetworkManager::SCAN_RUNNING, net.scanState( ));
    pumpScan(net, 2000);
    TEST_ASSERT_EQUAL_UINT(NetworkManager::SCAN_DONE, net.scanState( ));

    WifiNet out[WIFI_SCAN_MAX_NETS];
    TEST_ASSERT_EQUAL_UINT(3, net.scanResults(out, WIFI_SCAN_MAX_NETS));

    /* Descending signal, because that is the order someone picking a network
     * on a phone reads it in. */
    TEST_ASSERT_EQUAL_STRING("near",   out[0].ssid);
    TEST_ASSERT_EQUAL_STRING("middle", out[1].ssid);
    TEST_ASSERT_EQUAL_STRING("far",    out[2].ssid);
    TEST_ASSERT_EQUAL_INT(-40, out[0].rssi);
    TEST_ASSERT_EQUAL_UINT(0,  out[1].enc);   /* open, and it stays open */
    TEST_ASSERT_EQUAL_UINT(11, out[1].channel);
}

static void test_a_mesh_does_not_fill_the_list_with_itself(void) {
    NetworkManager net;
    bringUp(net);

    /* One SSID on three radios is one network to whoever is choosing. */
    WiFi.nets = { {"home", -70, 4, 1},
                  {"home", -45, 4, 6},
                  {"other",-65, 4, 11},
                  {"home", -85, 4, 13} };

    TEST_ASSERT_TRUE(net.startScan( ));
    pumpScan(net, 2000);

    WifiNet out[WIFI_SCAN_MAX_NETS];
    TEST_ASSERT_EQUAL_UINT(2, net.scanResults(out, WIFI_SCAN_MAX_NETS));
    TEST_ASSERT_EQUAL_STRING("home", out[0].ssid);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-45, out[0].rssi,
        "kept a weaker radio of the same SSID than the one it saw");
    TEST_ASSERT_EQUAL_STRING("other", out[1].ssid);
}

static void test_a_hidden_network_is_not_offered(void) {
    NetworkManager net;
    bringUp(net);

    /* A hidden SSID scans as an empty name: there is nothing to tap, and a
     * blank row in the list reads as a bug. */
    WiFi.nets = { {"", -35, 4, 1}, {"visible", -70, 4, 6} };

    TEST_ASSERT_TRUE(net.startScan( ));
    pumpScan(net, 2000);

    WifiNet out[WIFI_SCAN_MAX_NETS];
    TEST_ASSERT_EQUAL_UINT(1, net.scanResults(out, WIFI_SCAN_MAX_NETS));
    TEST_ASSERT_EQUAL_STRING("visible", out[0].ssid);
}

static void test_a_crowded_band_keeps_the_strongest(void) {
    NetworkManager net;
    bringUp(net);

    /* More networks than the list holds. The weakest are the ones to drop —
     * the buffer is fixed because it is copied out of the driver's map before
     * scanDelete( ) frees it. */
    char names[WIFI_SCAN_MAX_NETS + 6][8];
    for (int i = 0; i < WIFI_SCAN_MAX_NETS + 6; i++) {
        snprintf(names[i], sizeof(names[i]), "n%02d", i);
        WiFi.nets.push_back({ names[i], -30 - i, 4, (uint8_t)(1 + i % 11) });
    }

    TEST_ASSERT_TRUE(net.startScan( ));
    pumpScan(net, 2000);

    WifiNet out[WIFI_SCAN_MAX_NETS];
    TEST_ASSERT_EQUAL_UINT(WIFI_SCAN_MAX_NETS, net.scanResults(out, WIFI_SCAN_MAX_NETS));
    TEST_ASSERT_EQUAL_STRING("n00", out[0].ssid);
    TEST_ASSERT_EQUAL_STRING("n11", out[WIFI_SCAN_MAX_NETS - 1].ssid);
}

static void test_a_scan_that_never_finishes_is_abandoned(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.scanNeverCompletes = true;
    TEST_ASSERT_TRUE(net.startScan( ));

    /* Still running just before the deadline — a poller that gave up early
     * would report a failure on every slow scan. */
    pumpScan(net, WIFI_SCAN_TIMEOUT_MS - 1000);
    TEST_ASSERT_EQUAL_UINT(NetworkManager::SCAN_RUNNING, net.scanState( ));

    pumpScan(net, 2000);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(NetworkManager::SCAN_FAILED, net.scanState( ),
        "nothing in the SDK times a scan out; without this the page polls forever");
}

static void test_a_refused_scan_says_so_without_waiting(void) {
    NetworkManager net;
    bringUp(net);

    /* cyw43_wifi_scan( ) refusing is the call whose failure leaves
     * wifi_scan_state at 1 for the rest of the boot. Reporting it at once
     * beats fifteen seconds of polling a sweep that never started. */
    WiFi.scanRefusesToStart = true;
    TEST_ASSERT_FALSE(net.startScan( ));
    TEST_ASSERT_EQUAL_UINT(NetworkManager::SCAN_FAILED, net.scanState( ));
}

static void test_the_reconnect_scanner_keeps_the_radio(void) {
    NetworkManager net;
    bringUp(net);

    /* Drop the link and let the machine start its own scan; while that one
     * owns the radio, the web's scan must be refused rather than starting a
     * second sweep on top of it. */
    WiFi.scanNeverCompletes = true;
    WiFi.disconnect( );
    const uint32_t end = millis( ) + 60000;
    while (WiFi.scanStarts == 0 && (int32_t)(millis( ) - end) < 0) {
        set_native_millis(millis( ) + 50);
        net.update( );
    }
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, WiFi.scanStarts,
        "setup: the reconnect scanner never took the radio");

    const unsigned before = WiFi.scanStarts;
    TEST_ASSERT_FALSE(net.startScan( ));
    TEST_ASSERT_EQUAL_UINT(before, WiFi.scanStarts);
}

static void test_a_refusal_leaves_the_previous_list_in_the_buffer(void) {
    NetworkManager net;
    bringUp(net);

    WiFi.nets = { {"earlier", -50, 4, 6} };
    TEST_ASSERT_TRUE(net.startScan( ));
    pumpScan(net, 2000);
    TEST_ASSERT_EQUAL_UINT(NetworkManager::SCAN_DONE, net.scanState( ));

    /* Now the reconnect scanner takes the radio. startScan( ) refuses, and the
     * state stays SCAN_DONE because the earlier sweep's result is still in the
     * buffer — which is the trap the web handler has to know about: answering
     * a `again` request with THAT list shows minutes-old networks with nothing
     * saying so. handleApiWifiScan( ) sends 503 on any refusal for this
     * reason; this test is what says the precondition is real. */
    const unsigned mine = WiFi.scanStarts;
    WiFi.scanNeverCompletes = true;
    WiFi.disconnect( );
    const uint32_t end = millis( ) + 60000;
    while (WiFi.scanStarts == mine && (int32_t)(millis( ) - end) < 0) {
        set_native_millis(millis( ) + 50);
        net.update( );
    }
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(mine, WiFi.scanStarts,
        "setup: the reconnect scanner never took the radio");

    TEST_ASSERT_FALSE(net.startScan( ));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(NetworkManager::SCAN_DONE, net.scanState( ),
        "a refusal cleared the buffer; then the handler's 503 would be the only answer");
    WifiNet out[WIFI_SCAN_MAX_NETS];
    TEST_ASSERT_EQUAL_UINT(1, net.scanResults(out, WIFI_SCAN_MAX_NETS));
    TEST_ASSERT_EQUAL_STRING("earlier", out[0].ssid);
}

static void test_a_scan_from_ap_mode_brings_the_sta_interface_up_first(void) {
    NetworkManager net;
    net.beginAP("simut-test");
    TEST_ASSERT_TRUE(net.isApConfig( ));

    WiFi.nets = { {"neighbour", -55, 4, 6} };
    TEST_ASSERT_TRUE(net.startScan( ));

    /* The sweep runs on the STA interface, which AP mode leaves down. Asking
     * anyway is the documented way to wedge wifi_scan_state. */
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_cyw43StaModeEnables,
        "scanned from AP mode without bringing the STA interface up");
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_AP_STA, WiFi.lastMode,
        "dropped the access point to scan — that is the page the user is on");

    /* And it is still collected: pollScan( ) runs at the TOP of update( ),
     * before the AP-mode branch returns. */
    pumpScan(net, 2000);
    TEST_ASSERT_EQUAL_UINT(NetworkManager::SCAN_DONE, net.scanState( ));
    WifiNet out[WIFI_SCAN_MAX_NETS];
    TEST_ASSERT_EQUAL_UINT(1, net.scanResults(out, WIFI_SCAN_MAX_NETS));
    TEST_ASSERT_EQUAL_STRING("neighbour", out[0].ssid);
}

static void test_ap_mode_enables_the_sta_interface_once(void) {
    NetworkManager net;
    net.beginAP("simut-test");

    WiFi.nets = { {"neighbour", -55, 4, 6} };
    TEST_ASSERT_TRUE(net.startScan( ));
    pumpScan(net, 2000);
    TEST_ASSERT_TRUE(net.startScan( ));
    pumpScan(net, 2000);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_cyw43StaModeEnables,
        "re-enabled the STA interface on every scan; it is already up");
}

/* ── beginAP takes the radio from the station before using it ─────────────
 *
 * Measured on the rig 2026-09-22: `ap` issued while the station was hunting
 * for an SSID that is not there raised an access point the host could SEE at
 * 94% signal and could not associate with — 45 s and a timeout, twice,
 * against 2,6 s from a device that was connected. One radio serves both, and
 * cyw43_wifi_scan( ) owns the chip until its sweep finishes. The fallback
 * added in 2.7.1 opens the AP exactly when the ladder has been failing, which
 * is exactly when a sweep is in flight, so this is a gate and not a detail.
 */
static void test_ap_waits_for_a_sweep_before_taking_the_radio(void) {
    NetworkManager net;
    WiFi.reset( );
    WiFi.scanDurationMs = 2500;
    WiFi.nets = { {"neighbour", -55, 4, 6} };

    net.begin(makeConfig( ), true, false, "");
    TEST_ASSERT_TRUE(net.startScan( ));          /* a sweep is now in flight */
    TEST_ASSERT_EQUAL_INT(SCAN_RUNNING, WiFi.scanComplete( ));

    const uint32_t t0 = millis( );
    TEST_ASSERT_TRUE(net.beginAP("simut-test"));
    const uint32_t waited = millis( ) - t0;

    TEST_ASSERT_TRUE_MESSAGE(waited >= 2500,
        "took the radio while a sweep was still running");
    TEST_ASSERT_TRUE_MESSAGE(WiFi.scanDeletes >= 1,
        "left the finished sweep's result behind for the next reader");
    TEST_ASSERT_TRUE_MESSAGE(WiFi.disconnects >= 1,
        "never disconnected the station");
}

/* And the wedge does not hold the recovery path hostage: a sweep that never
 * finishes is the documented failure (cyw43's wifi_scan_state, 2026-09-08),
 * and an AP that waits for it for ever would be worse than one that starts a
 * little early. */
static void test_a_wedged_sweep_does_not_block_the_ap(void) {
    NetworkManager net;
    WiFi.reset( );
    WiFi.scanNeverCompletes = true;
    net.begin(makeConfig( ), true, false, "");
    TEST_ASSERT_TRUE(net.startScan( ));

    const uint32_t t0 = millis( );
    TEST_ASSERT_TRUE(net.beginAP("simut-test"));
    const uint32_t waited = millis( ) - t0;

    TEST_ASSERT_TRUE_MESSAGE(waited < 6000,
        "waited on a sweep that never finishes");
}

/* A radio that refuses the AP must say so. The return used to be dropped, so
 * a failed softAP( ) still left NET_AP_CONFIG behind and the console still
 * printed "AP mode started" — the operator was sent to a network that was not
 * on the air. */
static void test_a_refused_ap_is_reported_and_not_latched(void) {
    NetworkManager net;
    WiFi.reset( );
    WiFi.softApFails = true;
    net.begin(makeConfig( ), true, false, "");

    TEST_ASSERT_FALSE_MESSAGE(net.beginAP("simut-test"),
        "reported success for an AP the radio refused");
    TEST_ASSERT_FALSE_MESSAGE(net.isApConfig( ),
        "left the state machine in AP mode with no AP");
    WiFi.softApFails = false;
}

int main(int, char**) {
    UNITY_BEGIN( );

    RUN_TEST(test_boot_associates_without_scanning_first);
    RUN_TEST(test_a_missing_ssid_does_not_start_anything);

    RUN_TEST(test_a_dropped_link_comes_back_on_its_own);
    RUN_TEST(test_a_drop_is_noticed_without_being_told);

    RUN_TEST(test_a_scan_that_never_finishes_does_not_park_the_device);
    RUN_TEST(test_it_reconnects_even_while_the_scanner_stays_wedged);
    RUN_TEST(test_a_failed_scan_is_treated_as_a_fruitless_one);

    RUN_TEST(test_an_ap_that_no_scan_lists_is_still_joined);
    RUN_TEST(test_a_visible_ap_is_joined_off_the_first_scan);

    RUN_TEST(test_an_absent_network_backs_off_instead_of_spinning);
    RUN_TEST(test_dormancy_ends_and_the_ladder_restarts);
    RUN_TEST(test_an_ap_that_returns_is_found_again);
    RUN_TEST(test_the_failure_counter_saturates_instead_of_wrapping);
    RUN_TEST(test_a_success_clears_the_failure_counter);
    RUN_TEST(test_a_second_outage_recovers_as_fast_as_the_first);

    RUN_TEST(test_an_impossible_signal_reading_is_not_reported_as_health);

    RUN_TEST(test_a_scan_lists_what_the_air_carries);
    RUN_TEST(test_a_mesh_does_not_fill_the_list_with_itself);
    RUN_TEST(test_a_hidden_network_is_not_offered);
    RUN_TEST(test_a_crowded_band_keeps_the_strongest);
    RUN_TEST(test_a_scan_that_never_finishes_is_abandoned);
    RUN_TEST(test_a_refused_scan_says_so_without_waiting);
    RUN_TEST(test_the_reconnect_scanner_keeps_the_radio);
    RUN_TEST(test_a_refusal_leaves_the_previous_list_in_the_buffer);
    RUN_TEST(test_a_scan_from_ap_mode_brings_the_sta_interface_up_first);
    RUN_TEST(test_ap_mode_enables_the_sta_interface_once);
    RUN_TEST(test_ap_waits_for_a_sweep_before_taking_the_radio);
    RUN_TEST(test_a_wedged_sweep_does_not_block_the_ap);
    RUN_TEST(test_a_refused_ap_is_reported_and_not_latched);

    return UNITY_END( );
}
