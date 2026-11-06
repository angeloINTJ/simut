/**
 * @file    test/test_validators/boot_replay.cpp
 * @brief   The boot terminal's calls, recorded and played back (display/BootReplay.h): what the `screen boot` tags stand on.
 * @details The manual's boot figures cannot be captured while the device boots (the web server answers from loop( ), and the
 *          terminal is drawn during setup( )), so the panel records, call by call, what the boot said to it and a test tag plays
 *          the calls back through the same six functions. What is held here, on the host:
 *            - the record of a boot with a network, in the order of AppManager_Boot.cpp, played back at "Waiting for router" gives
 *              the five lines that were on the glass then, and the skip button;
 *            - played back at EVERY index, of four different boots, it gives the terminal the live panel had after that call. The
 *              expected state is not computed by the code under test: the live panel (the twin below) takes a snapshot after each
 *              call it receives, and the replay is compared with those;
 *            - a run of replaces of one key (the dots of "Waiting for router") and a run of progress calls (the gesture's bar) are
 *              one record, the last, and the `set` that opened the line stays its own record;
 *            - `cancel` adds exactly the two calls the boot makes when the gesture is let go, `<i> <pct>` adds the bar;
 *            - past its capacity the recorder ignores and counts, and an index outside the list is refused with nothing touched;
 *            - the replay does not record itself, and nothing is recorded after the boot is over;
 *            - the words after `screen boot` mean what the handler reads from them, and the list's lines are exact.
 *
 *          THE TWIN. DisplayManager.cpp (the six functions, the lines the header cites) needs the TFT, the mutex and the SDK, so
 *          the panel here is a copy of their statements with the recorder's line first, as the firmware has them under
 *          SIMUT_BOOT_TAGS. A copy can drift from what it copies; what keeps it honest is that its state is compared with hand-
 *          written text in two cases, and that the six statements are short. The sequences of calls below are AppManager_Boot.cpp's,
 *          each with its line, written on 2026-11-06 from the source at 95a1d2ae.
 *
 * @project SIMUT
 * @license MIT License
 */
#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "display/BootReplay.h"

using namespace bootreplay;

namespace {

/* Stand-ins for the LangKey values the boot uses. Their numbers are arbitrary: the record keeps whatever the caller says. */
enum Key : int16_t {
	T_HOLD_AP = 100, T_AP_CANCELLED, T_MOUNT_FS, T_START_LOG, T_START_CMD, T_LOAD_THEME_LANG, T_LOAD_PERIPH,
	T_START_AP, T_AP_NETWORK, T_AP_IP, T_START_WIFI, T_WIFI_SKIPPED, T_WAITING_ROUTER, T_SYNC_NTP, T_NET_TIMEOUT,
	T_NET_CONNECTED, T_START_TEL, T_START_WEB, T_REG_CALLBACKS, T_AP_ACTIVE, T_LOAD_MINMAX, T_WARMUP, T_CORRECT_TS,
	T_RELOAD_MINMAX, T_PREP_DASH, T_ALL_INIT, T_SYS_READY, T_OTA_DONE,
	T_RAW = 999   /* TR_KEYS_COUNT: a raw line, the suffix alone */
};

const char* const NAMES[] = {
	"HOLD_AP", "AP_CANCELLED", "MOUNT_FS", "START_LOG", "START_CMD", "LOAD_THEME_LANG", "LOAD_PERIPH",
	"START_AP", "AP_NETWORK", "AP_IP", "START_WIFI", "WIFI_SKIPPED", "WAITING_ROUTER", "SYNC_NTP", "NET_TIMEOUT",
	"NET_CONNECTED", "START_TEL", "START_WEB", "REG_CALLBACKS", "AP_ACTIVE", "LOAD_MINMAX", "WARMUP", "CORRECT_TS",
	"RELOAD_MINMAX", "PREP_DASH", "ALL_INIT", "SYS_READY", "OTA_DONE",
};

const char* keyName(int16_t k) {
	if (k == T_RAW) return "RAW";
	if (k >= T_HOLD_AP && k <= T_OTA_DONE) return NAMES[k - T_HOLD_AP];
	return "?";
}

struct Line { int16_t key; char suffix[SUFFIX_LEN]; };
struct State { Line logs[5]; bool skip; bool booting; int pct; };

/* The terminal as one line of text: the five lines, oldest first ("-" is an empty one), then the skip button, the bar, the mode. */
std::string show(const State& s) {
	std::string out;
	for (int i = 0; i < 5; i++) {
		if (i) out += "|";
		const Line& l = s.logs[i];
		if (l.key == T_RAW && !l.suffix[0]) { out += "-"; continue; }
		if (l.key != T_RAW) out += keyName(l.key);
		if (l.suffix[0]) { out += (l.key == T_RAW ? "'" : "+'"); out += l.suffix; out += "'"; }
	}
	out += s.skip ? " skip" : " noskip";
	out += " pct=" + std::to_string(s.pct);
	out += s.booting ? " booting" : " ended";
	return out;
}

void cp(char (&dst)[SUFFIX_LEN], const char* src) {   /* safeCopy( ): up to 39 bytes and a NUL */
	size_t i = 0;
	if (src) for (; i < sizeof(dst) - 1 && src[i]; i++) dst[i] = src[i];
	dst[i] = '\0';
}

/* The panel: the six functions as DisplayManager.cpp has them, the recorder's call first (as under SIMUT_BOOT_TAGS), and the sink the replay drives. */
template <int N>
struct FakeDisplay {
	Recorder<N> rec;
	State st;
	std::vector<std::string> calls;   /* what reached it, in order, as text */
	std::vector<State> snap;          /* the live terminal after the call each record stands for: the source of truth for a replay */
	int armed = 0;

	FakeDisplay( ) { clear( ); st.booting = true; }   /* DisplayManager::DisplayManager( ): booting, no skip, no bar, an empty window */

	void clear( ) {
		for (int i = 0; i < 5; i++) { st.logs[i].key = T_RAW; st.logs[i].suffix[0] = '\0'; }
		st.skip = false; st.pct = -1;
	}
	void shift( ) { for (int i = 0; i < 4; i++) st.logs[i] = st.logs[i + 1]; }

	/* After a call: the live terminal belongs to the record the call was recorded as, or folded into. A call the recorder refused or
	 * was not listening to belongs to none. */
	void track(uint8_t n0, uint16_t d0) {
		(void)n0;
		if (rec.closed || rec.paused) return;
		if (rec.dropped != d0) return;
		if (snap.size() < rec.n) snap.resize(rec.n);
		snap[rec.n - 1] = st;
	}

	/* -- the six -- */
	void setBootStatus(const char* msg, bool k) {
		const uint8_t n0 = rec.n; const uint16_t d0 = rec.dropped;
		rec.note(OP_SET_RAW, T_RAW, msg, k, 0);
		calls.push_back(std::string("setraw '") + msg + "'" + (k ? " skip" : " noskip"));
		if (msg[0]) { shift( ); st.logs[4].key = T_RAW; cp(st.logs[4].suffix, msg); }
		st.skip = k; st.booting = true;
		track(n0, d0);
	}
	void replaceBootStatus(const char* msg, bool k) {
		const uint8_t n0 = rec.n; const uint16_t d0 = rec.dropped;
		rec.note(OP_REPLACE_RAW, T_RAW, msg, k, 0);
		calls.push_back(std::string("replraw '") + msg + "'" + (k ? " skip" : " noskip"));
		if (msg[0]) { st.logs[4].key = T_RAW; cp(st.logs[4].suffix, msg); }
		st.skip = k; st.booting = true;
		track(n0, d0);
	}
	void setBootStatusKey(int16_t key, const char* suffix = nullptr, bool k = false) {
		const uint8_t n0 = rec.n; const uint16_t d0 = rec.dropped;
		rec.note(OP_SET_KEY, key, suffix, k, 0);
		calls.push_back(std::string("setkey ") + keyName(key) + " '" + (suffix ? suffix : "") + "'" + (k ? " skip" : " noskip"));
		shift( ); st.logs[4].key = key;
		if (suffix && suffix[0]) cp(st.logs[4].suffix, suffix); else st.logs[4].suffix[0] = '\0';
		st.skip = k; st.booting = true;
		track(n0, d0);
	}
	void replaceBootStatusKey(int16_t key, const char* suffix = nullptr, bool k = false) {
		const uint8_t n0 = rec.n; const uint16_t d0 = rec.dropped;
		rec.note(OP_REPLACE_KEY, key, suffix, k, 0);
		calls.push_back(std::string("replkey ") + keyName(key) + " '" + (suffix ? suffix : "") + "'" + (k ? " skip" : " noskip"));
		st.logs[4].key = key;
		if (suffix && suffix[0]) cp(st.logs[4].suffix, suffix); else st.logs[4].suffix[0] = '\0';
		st.skip = k; st.booting = true;
		track(n0, d0);
	}
	void setApProgress(int pct) {
		const uint8_t n0 = rec.n; const uint16_t d0 = rec.dropped;
		rec.note(OP_PROGRESS, 0, nullptr, false, pct);
		calls.push_back("progress " + std::to_string(pct));
		st.pct = pct; st.booting = true;
		track(n0, d0);
	}
	void endBoot( ) {
		const uint8_t n0 = rec.n; const uint16_t d0 = rec.dropped;
		rec.note(OP_END, 0, nullptr, false, 0);
		calls.push_back("end");
		st.booting = false;
		track(n0, d0);
	}

	/* -- the sink bootreplay::play( ) drives: the six under their op names, a window reset and the closing move -- */
	void reset( ) { calls.push_back("reset"); clear( ); st.booting = true; }
	void setRaw(const char* s, bool k) { setBootStatus(s, k); }
	void replaceRaw(const char* s, bool k) { replaceBootStatus(s, k); }
	void setKey(int16_t key, const char* s, bool k) { setBootStatusKey(key, s, k); }
	void replaceKey(int16_t key, const char* s, bool k) { replaceBootStatusKey(key, s, k); }
	void progress(int pct) { setApProgress(pct); }
	void end( ) { endBoot( ); }
	void arm( ) { calls.push_back("arm"); armed++; }
};

using Live = FakeDisplay<BOOT_REC_CAP>;
typedef void (*Seq)(Live&);

std::string join(const std::vector<std::string>& v) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) { if (i) out += "; "; out += v[i]; }
	return out;
}

/* -- the boots. Each call carries its line in AppManager_Boot.cpp. -- */

/* A boot with a network configured: nobody touches the screen in the AP-hold window, the router wait shows its dots and then waits for the time,
 * and no update was just installed. 25 calls, 22 records. */
void bootWithNetwork(Live& d) {
	d.setBootStatusKey(T_MOUNT_FS);                                  /* :356 */
	d.setBootStatusKey(T_HOLD_AP);                                   /* :545 */
	d.setApProgress(-1);                                             /* :615, the window closed with no touch */
	d.setBootStatusKey(T_START_LOG);                                 /* :626 */
	d.setBootStatusKey(T_START_CMD);                                 /* :874 */
	d.setBootStatusKey(T_LOAD_THEME_LANG);                           /* :986 */
	d.setBootStatusKey(T_LOAD_PERIPH);                               /* :1122 */
	d.setBootStatusKey(T_START_WIFI);                                /* :1195 */
	d.setBootStatusKey(T_WAITING_ROUTER, nullptr, true);             /* :1266 */
	d.replaceBootStatusKey(T_WAITING_ROUTER, ".", true);             /* :1268, once per dot step */
	d.replaceBootStatusKey(T_WAITING_ROUTER, "..", true);
	d.replaceBootStatusKey(T_WAITING_ROUTER, "...", true);
	d.setBootStatusKey(T_SYNC_NTP, nullptr, true);                   /* :1273 */
	d.replaceBootStatusKey(T_SYNC_NTP, ".", true);                   /* :1275 */
	d.replaceBootStatusKey(T_SYNC_NTP, "..", true);
	d.setBootStatusKey(T_NET_CONNECTED);                             /* :1290 */
	d.setBootStatusKey(T_START_TEL);                                 /* :1318 */
	d.setBootStatusKey(T_START_WEB);                                 /* :1333 */
	d.setBootStatusKey(T_REG_CALLBACKS);                             /* :1360 */
	d.setBootStatusKey(T_LOAD_MINMAX);                               /* :1427 */
	d.setBootStatusKey(T_WARMUP);                                    /* :1440 */
	d.setBootStatusKey(T_PREP_DASH);                                 /* :1481 */
	d.setBootStatusKey(T_ALL_INIT);                                  /* :1490 */
	d.setBootStatusKey(T_SYS_READY);                                 /* :1491 */
	d.endBoot( );                                                    /* :1512 */
}

/* The longest boot that goes straight on, every optional line in: the gesture started and let go, an update just installed, a legacy history purged,
 * a time-sync correction, and the router wait ending in a timeout that the router beat by a hair. 28 records without the timeout line, 29 with it. */
void bootLongest(Live& d) {
	d.setBootStatusKey(T_MOUNT_FS);                                  /* :356 */
	d.setBootStatusKey(T_HOLD_AP);                                   /* :545 */
	for (int pct = 4; pct <= 60; pct += 4) d.setApProgress(pct);     /* :605, every 50 ms of the hold: one record */
	d.setApProgress(-1);                                             /* :596, let go */
	d.setBootStatusKey(T_AP_CANCELLED, nullptr, false);              /* :597 */
	d.setApProgress(-1);                                             /* :615 */
	d.setBootStatusKey(T_START_LOG);                                 /* :626 */
	d.setBootStatusKey(T_OTA_DONE, ": v2.14.0");                     /* :800 */
	d.setBootStatusKey(T_START_CMD);                                 /* :874 */
	d.setBootStatusKey(T_LOAD_THEME_LANG);                           /* :986 */
	d.setBootStatusKey(T_LOAD_PERIPH);                               /* :1122 */
	d.setBootStatusKey(T_START_WIFI);                                /* :1195 */
	d.setBootStatusKey(T_WAITING_ROUTER, nullptr, true);             /* :1266 */
	d.replaceBootStatusKey(T_WAITING_ROUTER, "....", true);          /* :1268 */
	d.setBootStatusKey(T_SYNC_NTP, nullptr, true);                   /* :1273 */
	d.replaceBootStatusKey(T_SYNC_NTP, ".", true);                   /* :1275 */
	d.setBootStatusKey(T_NET_TIMEOUT);                               /* :1282 */
	d.setBootStatusKey(T_NET_CONNECTED);                             /* :1290 */
	d.setBootStatusKey(T_START_TEL);                                 /* :1318 */
	d.setBootStatusKey(T_START_WEB);                                 /* :1333 */
	d.setBootStatusKey(T_REG_CALLBACKS);                             /* :1360 */
	d.setBootStatusKey(T_LOAD_MINMAX);                               /* :1405, the purge */
	d.setBootStatusKey(T_LOAD_MINMAX);                               /* :1427 */
	d.setBootStatusKey(T_WARMUP);                                    /* :1440 */
	d.setBootStatusKey(T_CORRECT_TS);                                /* :1458 */
	d.setBootStatusKey(T_RELOAD_MINMAX);                             /* :1464 */
	d.setBootStatusKey(T_PREP_DASH);                                 /* :1481 */
	d.setBootStatusKey(T_ALL_INIT);                                  /* :1490 */
	d.setBootStatusKey(T_SYS_READY);                                 /* :1491 */
	d.endBoot( );                                                    /* :1512 */
}

/* A boot the gesture took into the AP: the hold to the end, the AP's lines, the four of showApOnPanel( ), and no endBoot( ): "the panel keeps the AP box". */
void bootIntoAp(Live& d) {
	d.setBootStatusKey(T_MOUNT_FS);                                  /* :356 */
	d.setBootStatusKey(T_HOLD_AP);                                   /* :545 */
	for (int pct = 2; pct <= 98; pct += 2) d.setApProgress(pct);     /* :605 */
	d.setApProgress(-1);                                             /* :615 */
	d.setBootStatusKey(T_START_LOG);                                 /* :626 */
	d.setBootStatusKey(T_START_CMD);                                 /* :874 */
	d.setBootStatusKey(T_LOAD_THEME_LANG);                           /* :986 */
	d.setBootStatusKey(T_LOAD_PERIPH);                               /* :1122 */
	d.setBootStatusKey(T_START_AP);                                  /* :1148 */
	d.setBootStatusKey(T_AP_NETWORK, "  PSK k3x9q2m7");              /* :1174 */
	d.setBootStatusKey(T_AP_IP, ":80");                              /* :1176 */
	d.setBootStatusKey(T_START_TEL);                                 /* :1318 */
	d.setBootStatusKey(T_START_WEB);                                 /* :1333 */
	d.setBootStatusKey(T_REG_CALLBACKS);                             /* :1360 */
	d.setBootStatusKey(T_RAW, "simut_SETUP");                        /* showApOnPanel( ), AppManager_Commands.cpp:124 */
	d.setBootStatusKey(T_RAW, "PSK k3x9q2m7");                       /* :126 */
	d.setBootStatusKey(T_AP_IP, ":80");                              /* :128 */
	d.setBootStatusKey(T_AP_ACTIVE, nullptr, false);                 /* :129 */
}

/* The legacy raw-string pair, which nothing calls today but the recorder answers for: an empty message does not shift the window, a replace overwrites it. */
void bootWithRawLines(Live& d) {
	d.setBootStatusKey(T_MOUNT_FS);
	d.setBootStatus("hello", false);
	d.setBootStatus("", true);
	d.replaceBootStatus("hello2", true);
	d.replaceBootStatus("hello3", false);
	d.setBootStatusKey(T_AP_IP, ":80");
	d.replaceBootStatus("", true);
	d.endBoot( );
}

/* What a panel shows when a replay lands on it: not the empty terminal of a fresh boot. */
void dirty(Live& p) {
	p.setBootStatusKey(T_SYS_READY, "left over", true);
	p.setBootStatusKey(T_NET_TIMEOUT, "...", true);
	p.setBootStatus("raw leftover", true);
	p.setApProgress(77);
	p.endBoot( );
	p.calls.clear( );
	p.armed = 0;
}

int indexOf(const Live& d, uint8_t op, int16_t key) {
	for (int i = 0; i < d.rec.n; i++) if (d.rec.rec[i].op == op && d.rec.rec[i].key == key) return i;
	return -1;
}

void everyIndexReplaysTheLiveTerminal(const char* what, Seq seq) {
	Live live;
	seq(live);
	live.rec.close( );
	TEST_ASSERT_TRUE_MESSAGE(live.rec.n > 0, what);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(live.rec.n, live.snap.size( ), what);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, live.rec.dropped, what);

	Live panel;
	panel.rec.close( );
	for (int i = 0; i < live.rec.n; i++) {
		dirty(panel);
		Cmd c; c.verb = V_AT; c.idx = i; c.pct = 0;
		char msg[96]; snprintf(msg, sizeof(msg), "%s, record %d", what, i);
		TEST_ASSERT_TRUE_MESSAGE(play(live.rec, c, T_AP_CANCELLED, panel), msg);
		TEST_ASSERT_EQUAL_STRING_MESSAGE(show(live.snap[i]).c_str( ), show(panel.st).c_str( ), msg);
	}

	/* On the live panel itself, the way the firmware does it: the recorder is closed by then, and the replay must leave the list as it was. */
	const uint8_t n0 = live.rec.n;
	for (int i = 0; i < n0; i++) {
		Cmd c; c.verb = V_AT; c.idx = i; c.pct = 0;
		char msg[96]; snprintf(msg, sizeof(msg), "%s, on itself, record %d", what, i);
		TEST_ASSERT_TRUE_MESSAGE(play(live.rec, c, T_AP_CANCELLED, live), msg);
		TEST_ASSERT_EQUAL_STRING_MESSAGE(show(live.snap[i]).c_str( ), show(live.st).c_str( ), msg);
	}
	TEST_ASSERT_EQUAL_UINT_MESSAGE(n0, live.rec.n, what);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(0, live.rec.dropped, what);
}

}  // namespace

/* ------------------------------------------------------------------------------------------------------------- the replay */

void test_bootrec_replay_at_waiting_router_gives_the_five_lines_and_the_skip_button(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );

	const int at = indexOf(live, OP_SET_KEY, T_WAITING_ROUTER);
	TEST_ASSERT_EQUAL_INT(8, at);   /* MOUNT_FS, HOLD_AP, progress, START_LOG, START_CMD, LOAD_THEME_LANG, LOAD_PERIPH, START_WIFI, then it */

	Live panel;
	panel.rec.close( );
	dirty(panel);
	Cmd c; c.verb = V_AT; c.idx = at; c.pct = 0;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	/* Written by hand: the last five lines the boot had said, the button its wait asks for, no bar, still booting. */
	TEST_ASSERT_EQUAL_STRING("START_CMD|LOAD_THEME_LANG|LOAD_PERIPH|START_WIFI|WAITING_ROUTER skip pct=-1 booting", show(panel.st).c_str( ));
	TEST_ASSERT_EQUAL_INT(1, panel.armed);

	/* The record after it is the dots, still on the same line, still with the button. */
	c.idx = at + 1;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	TEST_ASSERT_EQUAL_STRING("START_CMD|LOAD_THEME_LANG|LOAD_PERIPH|START_WIFI|WAITING_ROUTER+'...' skip pct=-1 booting", show(panel.st).c_str( ));
}

void test_bootrec_replay_at_every_index_is_the_terminal_the_live_panel_had(void) {
	everyIndexReplaysTheLiveTerminal("boot with a network", bootWithNetwork);
	everyIndexReplaysTheLiveTerminal("the longest boot", bootLongest);
	everyIndexReplaysTheLiveTerminal("a boot into the AP", bootIntoAp);
	everyIndexReplaysTheLiveTerminal("raw lines", bootWithRawLines);
}

void test_bootrec_replay_does_not_depend_on_what_the_panel_showed_before(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	Live clean, messy;
	clean.rec.close( ); messy.rec.close( );
	dirty(messy);
	Cmd c; c.verb = V_AT; c.idx = 10; c.pct = 0;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, clean));
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, messy));
	TEST_ASSERT_EQUAL_STRING(show(clean.st).c_str( ), show(messy.st).c_str( ));
}

void test_bootrec_replay_at_the_end_record_leaves_the_boot_ended(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	const int last = live.rec.n - 1;
	TEST_ASSERT_EQUAL_UINT8(OP_END, live.rec.rec[last].op);
	Live panel;
	panel.rec.close( );
	Cmd c; c.verb = V_AT; c.idx = last; c.pct = 0;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	TEST_ASSERT_FALSE(panel.st.booting);   /* the boot's own last call: the terminal is gone */
}

void test_bootrec_cancel_adds_exactly_the_two_calls_the_boot_makes(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	const int hold = indexOf(live, OP_SET_KEY, T_HOLD_AP);
	TEST_ASSERT_EQUAL_INT(1, hold);

	Live panel;
	panel.rec.close( );
	dirty(panel);
	Cmd c; c.verb = V_CANCEL; c.idx = hold; c.pct = 0;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	/* AppManager_Boot.cpp:596-597: setApProgress(-1), then setBootStatusKey(TR_BOOT_AP_CANCELLED, nullptr, false). Nothing else, and in that order. */
	TEST_ASSERT_EQUAL_STRING("reset; setkey MOUNT_FS '' noskip; setkey HOLD_AP '' noskip; progress -1; setkey AP_CANCELLED '' noskip; arm",
	                         join(panel.calls).c_str( ));
	TEST_ASSERT_EQUAL_STRING("-|-|MOUNT_FS|HOLD_AP|AP_CANCELLED noskip pct=-1 booting", show(panel.st).c_str( ));
}

void test_bootrec_a_percent_adds_the_bar_after_the_replay(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	Live panel;
	panel.rec.close( );
	dirty(panel);
	Cmd c; c.verb = V_PROGRESS; c.idx = 1; c.pct = 50;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	TEST_ASSERT_EQUAL_STRING("reset; setkey MOUNT_FS '' noskip; setkey HOLD_AP '' noskip; progress 50; arm", join(panel.calls).c_str( ));
	TEST_ASSERT_EQUAL_STRING("-|-|-|MOUNT_FS|HOLD_AP noskip pct=50 booting", show(panel.st).c_str( ));
}

void test_bootrec_an_index_outside_the_list_is_refused_and_touches_nothing(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	Live panel;
	panel.rec.close( );
	dirty(panel);
	const std::string before = show(panel.st);
	const int n = live.rec.n;
	const int bad[] = { -1, n, n + 1, n + 50, 255, 999 };
	for (int idx : bad) {
		Cmd c; c.verb = V_AT; c.idx = idx; c.pct = 0;
		char msg[48]; snprintf(msg, sizeof(msg), "index %d", idx);
		TEST_ASSERT_FALSE_MESSAGE(play(live.rec, c, T_AP_CANCELLED, panel), msg);
		c.verb = V_CANCEL;
		TEST_ASSERT_FALSE_MESSAGE(play(live.rec, c, T_AP_CANCELLED, panel), msg);
		c.verb = V_PROGRESS; c.pct = 50;
		TEST_ASSERT_FALSE_MESSAGE(play(live.rec, c, T_AP_CANCELLED, panel), msg);
	}
	TEST_ASSERT_EQUAL_STRING(before.c_str( ), show(panel.st).c_str( ));
	TEST_ASSERT_TRUE(panel.calls.empty( ));    /* not even the reset */
	TEST_ASSERT_EQUAL_INT(0, panel.armed);
	TEST_ASSERT_FALSE(live.rec.paused);        /* a refusal does not leave the recorder deaf */
}

void test_bootrec_play_refuses_a_percent_outside_zero_to_one_hundred_and_the_verbs_that_are_not_replays(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	Live panel;
	panel.rec.close( );
	Cmd c; c.verb = V_PROGRESS; c.idx = 1;
	c.pct = 101; TEST_ASSERT_FALSE(play(live.rec, c, T_AP_CANCELLED, panel));
	c.pct = -1;  TEST_ASSERT_FALSE(play(live.rec, c, T_AP_CANCELLED, panel));
	c.pct = 0;   TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	c.pct = 100; TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, panel));
	panel.calls.clear( );
	const Verb notReplays[] = { V_BAD, V_LIST, V_END };
	for (Verb v : notReplays) {
		c.verb = v; c.idx = 1; c.pct = 0;
		TEST_ASSERT_FALSE(play(live.rec, c, T_AP_CANCELLED, panel));
	}
	TEST_ASSERT_TRUE(panel.calls.empty( ));
}

void test_bootrec_a_replay_does_not_record_itself(void) {
	/* The recorder is still open here, which the firmware never lets happen (it closes at the end of setup( )), so this is the belt over the braces:
	 * the six functions a replay calls would otherwise append their own calls to the list they are reading. */
	Live live;
	bootWithNetwork(live);
	const uint8_t n0 = live.rec.n;
	TEST_ASSERT_FALSE(live.rec.closed);
	Cmd c; c.verb = V_AT; c.idx = 9; c.pct = 0;
	TEST_ASSERT_TRUE(play(live.rec, c, T_AP_CANCELLED, live));
	TEST_ASSERT_EQUAL_UINT8(n0, live.rec.n);
	TEST_ASSERT_EQUAL_UINT16(0, live.rec.dropped);
	TEST_ASSERT_FALSE(live.rec.paused);        /* and listening again afterwards */
	live.setBootStatusKey(T_SYS_READY);
	TEST_ASSERT_EQUAL_UINT8(n0 + 1, live.rec.n);
}

/* ---------------------------------------------------------------------------------------------------------- the recorder */

void test_bootrec_the_dots_of_one_key_collapse_into_the_last_and_the_set_stays_its_own_record(void) {
	Live live;
	bootWithNetwork(live);
	TEST_ASSERT_EQUAL_UINT8(22, live.rec.n);            /* 25 calls */
	const Rec& set = live.rec.rec[8];
	TEST_ASSERT_EQUAL_UINT8(OP_SET_KEY, set.op);
	TEST_ASSERT_EQUAL_INT16(T_WAITING_ROUTER, set.key);
	TEST_ASSERT_EQUAL_STRING("", set.suffix);           /* the clean line, "Waiting for router" before any dot, can still be recomposed */
	TEST_ASSERT_TRUE(set.skip);
	const Rec& dots = live.rec.rec[9];
	TEST_ASSERT_EQUAL_UINT8(OP_REPLACE_KEY, dots.op);
	TEST_ASSERT_EQUAL_INT16(T_WAITING_ROUTER, dots.key);
	TEST_ASSERT_EQUAL_STRING("...", dots.suffix);       /* three replaces, the last */
	TEST_ASSERT_TRUE(dots.skip);
	const Rec& ntp = live.rec.rec[11];
	TEST_ASSERT_EQUAL_UINT8(OP_REPLACE_KEY, ntp.op);
	TEST_ASSERT_EQUAL_INT16(T_SYNC_NTP, ntp.key);
	TEST_ASSERT_EQUAL_STRING("..", ntp.suffix);         /* two replaces, the last */
	TEST_ASSERT_EQUAL_UINT16(0, live.rec.dropped);
}

void test_bootrec_only_a_replace_of_the_same_key_folds(void) {
	Recorder<8> r;
	r.note(OP_SET_KEY, 5, nullptr, false, 0);
	r.note(OP_REPLACE_KEY, 5, ".", false, 0);       /* a replace after a set: its own record */
	r.note(OP_REPLACE_KEY, 5, "..", false, 0);      /* the same key again: folds */
	TEST_ASSERT_EQUAL_UINT8(2, r.n);
	r.note(OP_REPLACE_KEY, 6, ".", false, 0);       /* another key: its own record */
	TEST_ASSERT_EQUAL_UINT8(3, r.n);
	r.note(OP_SET_KEY, 6, nullptr, false, 0);       /* two sets of one key shift the window twice: two records */
	r.note(OP_SET_KEY, 6, nullptr, false, 0);
	TEST_ASSERT_EQUAL_UINT8(5, r.n);
	r.note(OP_REPLACE_RAW, 999, "a", false, 0);     /* raw replaces fold with each other, whatever the text */
	r.note(OP_REPLACE_RAW, 999, "b", true, 0);
	TEST_ASSERT_EQUAL_UINT8(6, r.n);
	TEST_ASSERT_EQUAL_STRING("b", r.rec[5].suffix);
	TEST_ASSERT_TRUE(r.rec[5].skip);
	r.note(OP_REPLACE_KEY, 999, "c", false, 0);     /* a keyed replace does not fold into a raw one... */
	TEST_ASSERT_EQUAL_UINT8(7, r.n);
	r.note(OP_REPLACE_RAW, 999, "d", false, 0);     /* ...nor the reverse */
	TEST_ASSERT_EQUAL_UINT8(8, r.n);
	TEST_ASSERT_EQUAL_STRING("..", r.rec[1].suffix);
}

void test_bootrec_a_run_of_progress_calls_is_one_record_with_the_last_value(void) {
	Live live;
	live.setBootStatusKey(T_MOUNT_FS);
	live.setBootStatusKey(T_HOLD_AP);
	for (int pct = 1; pct <= 97; pct++) live.setApProgress(pct);   /* the hold, a call every 50 ms */
	live.setApProgress(-1);
	TEST_ASSERT_EQUAL_UINT8(3, live.rec.n);
	TEST_ASSERT_EQUAL_UINT8(OP_PROGRESS, live.rec.rec[2].op);
	TEST_ASSERT_EQUAL_INT16(-1, live.rec.rec[2].pct);
	TEST_ASSERT_EQUAL_UINT16(0, live.rec.dropped);
	live.setBootStatusKey(T_AP_CANCELLED);
	live.setApProgress(-1);                                         /* after another line it is a record again */
	TEST_ASSERT_EQUAL_UINT8(5, live.rec.n);
}

void test_bootrec_past_the_capacity_the_calls_are_ignored_and_counted(void) {
	Recorder<4> r;
	for (int k = 0; k < 6; k++) r.note(OP_SET_KEY, (int16_t)(10 + k), nullptr, false, 0);
	TEST_ASSERT_EQUAL_UINT8(4, r.n);
	TEST_ASSERT_EQUAL_UINT16(2, r.dropped);
	for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT16(10 + i, r.rec[i].key);   /* the first four stay as they were */
	r.note(OP_END, 0, nullptr, false, 0);
	TEST_ASSERT_EQUAL_UINT16(3, r.dropped);
	TEST_ASSERT_EQUAL_UINT8(OP_SET_KEY, r.rec[3].op);

	/* A call that folds into the last record needs no room, so it is not an overflow. */
	Recorder<3> f;
	f.note(OP_SET_KEY, 1, nullptr, false, 0);
	f.note(OP_SET_KEY, 2, nullptr, false, 0);
	f.note(OP_REPLACE_KEY, 2, ".", true, 0);
	TEST_ASSERT_EQUAL_UINT8(3, f.n);
	f.note(OP_REPLACE_KEY, 2, "..", true, 0);
	TEST_ASSERT_EQUAL_UINT8(3, f.n);
	TEST_ASSERT_EQUAL_UINT16(0, f.dropped);
	TEST_ASSERT_EQUAL_STRING("..", f.rec[2].suffix);
	f.note(OP_SET_KEY, 3, nullptr, false, 0);
	TEST_ASSERT_EQUAL_UINT16(1, f.dropped);
}

void test_bootrec_an_overflowed_list_still_replays_what_it_kept(void) {
	FakeDisplay<5> d;
	d.setBootStatusKey(T_MOUNT_FS);
	d.setBootStatusKey(T_HOLD_AP);
	d.setBootStatusKey(T_START_LOG);
	d.setBootStatusKey(T_START_CMD);
	d.setBootStatusKey(T_LOAD_PERIPH);
	d.setBootStatusKey(T_START_WIFI);     /* dropped */
	d.setBootStatusKey(T_SYS_READY);      /* dropped */
	d.rec.close( );
	TEST_ASSERT_EQUAL_UINT8(5, d.rec.n);
	TEST_ASSERT_EQUAL_UINT16(2, d.rec.dropped);
	FakeDisplay<5> panel;
	panel.rec.close( );
	Cmd c; c.verb = V_AT; c.idx = 4; c.pct = 0;
	TEST_ASSERT_TRUE(play(d.rec, c, T_AP_CANCELLED, panel));
	TEST_ASSERT_EQUAL_STRING("MOUNT_FS|HOLD_AP|START_LOG|START_CMD|LOAD_PERIPH noskip pct=-1 booting", show(panel.st).c_str( ));
	c.idx = 5;
	TEST_ASSERT_FALSE(play(d.rec, c, T_AP_CANCELLED, panel));   /* what was dropped is not on the list */
}

void test_bootrec_nothing_is_recorded_after_the_boot_is_over(void) {
	Live live;
	bootWithNetwork(live);
	live.rec.close( );
	const uint8_t n0 = live.rec.n;
	live.setBootStatusKey(T_START_AP);              /* the AP command, a web reboot, an update: boot calls after the boot */
	live.setBootStatusKey(T_AP_ACTIVE, nullptr, false);
	live.setApProgress(10);
	live.endBoot( );
	TEST_ASSERT_EQUAL_UINT8(n0, live.rec.n);
	TEST_ASSERT_EQUAL_UINT16(0, live.rec.dropped);  /* not an overflow, so not counted as one */
}

void test_bootrec_the_longest_straight_boot_fits_with_room_to_spare(void) {
	Live live;
	bootLongest(live);
	TEST_ASSERT_EQUAL_UINT16(0, live.rec.dropped);
	TEST_ASSERT_EQUAL_UINT8(29, live.rec.n);        /* counted on 2026-11-06 in AppManager_Boot.cpp, with every optional line in */
	TEST_ASSERT_TRUE_MESSAGE(BOOT_REC_CAP - live.rec.n >= 8, "room for two more flaps of the link, four records each");
	TEST_ASSERT_TRUE(BOOT_REC_CAP < 256);           /* the count is a byte */
}

void test_bootrec_a_suffix_longer_than_the_line_is_cut_where_the_line_cuts_it(void) {
	Recorder<4> r;
	char long60[61];
	for (int i = 0; i < 60; i++) long60[i] = (char)('a' + (i % 26));
	long60[60] = '\0';
	r.note(OP_SET_KEY, 7, long60, false, 0);
	TEST_ASSERT_EQUAL_INT(SUFFIX_LEN - 1, (int)strlen(r.rec[0].suffix));   /* BootLogEntry::suffix is 40 bytes: 39 and a NUL */
	TEST_ASSERT_EQUAL_INT(0, strncmp(r.rec[0].suffix, long60, SUFFIX_LEN - 1));
	r.note(OP_SET_KEY, 8, nullptr, false, 0);
	TEST_ASSERT_EQUAL_STRING("", r.rec[1].suffix);
}

/* ------------------------------------------------------------------------------------------------------ the words and the list */

void test_bootrec_parse_reads_list_end_index_percent_and_cancel(void) {
	TEST_ASSERT_EQUAL(V_LIST, parse("", "").verb);
	TEST_ASSERT_EQUAL(V_LIST, parse(nullptr, nullptr).verb);
	TEST_ASSERT_EQUAL(V_END, parse("end", "").verb);
	Cmd a = parse("3", "");
	TEST_ASSERT_EQUAL(V_AT, a.verb);
	TEST_ASSERT_EQUAL_INT(3, a.idx);
	Cmd p = parse("3", "50");
	TEST_ASSERT_EQUAL(V_PROGRESS, p.verb);
	TEST_ASSERT_EQUAL_INT(3, p.idx);
	TEST_ASSERT_EQUAL_INT(50, p.pct);
	Cmd z = parse("0", "0");
	TEST_ASSERT_EQUAL(V_PROGRESS, z.verb);
	TEST_ASSERT_EQUAL_INT(0, z.idx);
	TEST_ASSERT_EQUAL_INT(0, z.pct);
	TEST_ASSERT_EQUAL_INT(100, parse("12", "100").pct);
	Cmd c = parse("3", "cancel");
	TEST_ASSERT_EQUAL(V_CANCEL, c.verb);
	TEST_ASSERT_EQUAL_INT(3, c.idx);
	TEST_ASSERT_EQUAL_INT(3, parse("03", "").idx);       /* a leading zero is still the number */
	TEST_ASSERT_EQUAL_INT(999, parse("999", "").idx);
}

void test_bootrec_parse_refuses_what_is_not_an_index_a_percent_or_cancel(void) {
	const char* const bad[][2] = {
		{ "x", "" }, { "3", "x" }, { "-1", "" }, { "3.5", "" }, { "3 ", "" }, { " 3", "" }, { "+3", "" }, { "0x3", "" },
		{ "1000", "" }, { "0001", "" },                   /* four digits: no list is that long, and the digit count is what keeps the sum from overflowing */
		{ "99999999999999999999", "" }, { "3", "00100" },
		{ "3", "101" }, { "3", "1000" }, { "3", "-1" }, { "3", "5.0" }, { "3", "Cancel" }, { "3", "cancel " },
		{ "cancel", "" }, { "cancel", "3" },              /* cancel needs the index it follows */
		{ "end", "3" }, { "end", "cancel" }, { "End", "" },
	};
	for (const auto& row : bad) {
		char msg[48]; snprintf(msg, sizeof(msg), "'%s' '%s'", row[0], row[1]);
		TEST_ASSERT_EQUAL_MESSAGE(V_BAD, parse(row[0], row[1]).verb, msg);
	}
}

void test_bootrec_list_lines_are_exact(void) {
	char buf[160];
	Rec r; memset(&r, 0, sizeof(r));

	r.op = OP_SET_KEY; r.key = 84; r.skip = true; r.suffix[0] = '\0';
	format(r, 8, "Waiting for router", buf, sizeof(buf));
	TEST_ASSERT_EQUAL_STRING(" 8 setkey   key=84 skip=1 text=\"Waiting for router\" suffix=\"\"", buf);

	r.op = OP_REPLACE_KEY; r.skip = true; strcpy(r.suffix, "..");
	format(r, 13, "Waiting for router", buf, sizeof(buf));
	TEST_ASSERT_EQUAL_STRING("13 replkey  key=84 skip=1 text=\"Waiting for router\" suffix=\"..\"", buf);

	r.op = OP_SET_RAW; r.key = 999; r.skip = false; strcpy(r.suffix, "hello");
	format(r, 3, nullptr, buf, sizeof(buf));
	TEST_ASSERT_EQUAL_STRING(" 3 setraw   key=999 skip=0 text=\"\" suffix=\"hello\"", buf);

	r.op = OP_REPLACE_RAW; r.skip = true;
	format(r, 4, nullptr, buf, sizeof(buf));
	TEST_ASSERT_EQUAL_STRING(" 4 replraw  key=999 skip=1 text=\"\" suffix=\"hello\"", buf);

	r.op = OP_PROGRESS; r.pct = -1;
	format(r, 2, nullptr, buf, sizeof(buf));
	TEST_ASSERT_EQUAL_STRING(" 2 progress pct=-1", buf);

	r.op = OP_END;
	format(r, 21, nullptr, buf, sizeof(buf));
	TEST_ASSERT_EQUAL_STRING("21 end", buf);
}

void test_bootrec_list_line_never_writes_past_its_buffer(void) {
	Rec r; memset(&r, 0, sizeof(r));
	r.op = OP_SET_KEY; r.key = 84; r.skip = true;
	memset(r.suffix, 'z', SUFFIX_LEN - 1);
	char buf[32];
	memset(buf, 0xA5, sizeof(buf));
	format(r, 8, "Waiting for router", buf, 16);
	TEST_ASSERT_EQUAL_INT(15, (int)strlen(buf));            /* cut to the cap, with its NUL */
	for (int i = 16; i < 32; i++) TEST_ASSERT_EQUAL_HEX8(0xA5, (uint8_t)buf[i]);
	format(r, 8, "Waiting for router", buf, 0);             /* a zero cap writes nothing */
	TEST_ASSERT_EQUAL_HEX8(0xA5, (uint8_t)buf[16]);
}
