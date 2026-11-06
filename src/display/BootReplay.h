/**
 * @file display/BootReplay.h
 * @brief The boot terminal's calls, recorded as the boot makes them and played back on request: what the `screen boot`
 *        test tags stand on (2026-11-06).
 *
 * @details WHY. The manual shows the original panel's boot terminal during "Waiting for router" with its skip button, the
 * line that asks for the long press, the bar of that gesture and "AP mode cancelled". None of it can be captured through
 * GET /api/screenshot while it is on the glass: setup( ) draws it, and the web server answers from loop( ). But the
 * terminal is shared state (SystemState: bootLogs[5], showSkipButton, apProgressPct, isBooting), written by six
 * DisplayManager functions and by nothing else, and Core 1 draws it from that state. So the panel keeps what the boot
 * said to those six, in order, and a test tag says it again through the same six once the boot is over: what lands on
 * the glass is drawn by the boot's own code, not by a lookalike of it.
 *
 * WHERE. Only in an image with the full console AND the TFT (SIMUT_BOOT_TAGS: pico_w_test and pico_w_test_https). The
 * whole file is inside that switch, so every other image compiles it to nothing, and what they carry is unchanged: the
 * nine other firmware.bin are the same bytes, sha256 on a build from scratch on each side (the PR says which).
 *
 * WHAT IS KEPT. One Rec per call, with the operation, the LangKey (TR_KEYS_COUNT for a raw line), the suffix as the
 * 40-byte line would hold it, the skip button and the percent. Two kinds of call are folded into the record before them,
 * because what they say is only the last of them:
 *   - a run of replaces of ONE key (the dots of "Waiting for router" and of "Syncing NTP", AppManager_Boot.cpp:1266-1275)
 *     is the last replace. The `set` that opened the line stays its own record, so the line as the boot drew it first,
 *     with no dot, can still be recomposed;
 *   - a run of setApProgress( ) (the held gesture calls it every 50 ms, some 60 times in its 3 s) is the last value.
 * A call that is neither folds into nothing, even a second `set` of the same key: each shifts the window.
 *
 * HOW MUCH. BOOT_REC_CAP records of sizeof(Rec) = 46 bytes: 1,840 B in .bss of the two test images. The longest boot that
 * goes straight on (the gesture started and let go, an update just installed, a legacy history purged, a time-sync
 * correction, the router wait ending in a timeout) makes 29 records, counted on 2026-11-06 in AppManager_Boot.cpp
 * (test_bootrec_the_longest_straight_boot_fits_with_room_to_spare), and each flap of the link adds up to four. 40 leaves
 * eleven. What does not fit is ignored, and counted in `dropped`, which `screen boot` prints.
 *
 * UNTIL WHEN. The recorder is closed at the end of setup( ), the one exit that the boot into the AP shares with the
 * ordinary boot (the AP boot never calls endBoot( ): "the panel keeps the AP box"). Later calls to the six (the `ap`
 * command, a web reboot, an update) are not the boot and are not kept.
 *
 * ALL ZERO ON PURPOSE. The recorder is a static, and `closed` and not `open` is the flag so that its initial state is
 * all zero bytes: a `true` default would move its 1,840 B from .bss to .data, and .data is stored in the flash image.
 *
 * THE SINK. play( ) drives whatever has: reset( ) (an empty window: five empty lines, no skip, no bar, booting, as the
 * constructor leaves it), the six calls under their operation names (setRaw, replaceRaw, setKey, replaceKey, progress,
 * end) and arm( ) (the panel in the mode that draws the terminal). DisplayManager passes the real six; the host test
 * passes a copy of them and compares the result with the live terminal it snapshotted after each call.
 *
 * Pure and header-only: `pio test -e native` covers it (test_validators/boot_replay.cpp), and `pio test -e native_cli`
 * the words after `screen boot` that reach it through the parser.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "simut_config.h"     /* SIMUT_DISPLAY_TFT */
#include "SystemDefs_Cli.h"   /* SIMUT_CLI_FULL, and the default of 1 the host tests rely on */

/* The console that asks and the panel that draws: no other image has either half to use. */
#if SIMUT_CLI_FULL && SIMUT_DISPLAY_TFT
#define SIMUT_BOOT_TAGS 1
#else
#define SIMUT_BOOT_TAGS 0
#endif

#if SIMUT_BOOT_TAGS

namespace bootreplay {

/** BootLogEntry::suffix (DisplayManager.h): what a record's suffix has to hold to give the line back as it was. */
constexpr int SUFFIX_LEN = 40;

/** Records the panel keeps. See HOW MUCH above; the count is a byte. */
constexpr int BOOT_REC_CAP = 40;
static_assert(BOOT_REC_CAP > 0 && BOOT_REC_CAP < 256, "the recorder counts its records in a byte");

/** The six calls that write the boot terminal (DisplayManager.cpp). */
enum Op : uint8_t {
	OP_SET_RAW,       /**< setBootStatus( msg, showSkip ): the legacy raw line, which shifts the window unless msg is empty */
	OP_REPLACE_RAW,   /**< replaceBootStatus( msg, showSkip ) */
	OP_SET_KEY,       /**< setBootStatusKey( key, suffix, showSkip ): a new bottom line, the others move up */
	OP_REPLACE_KEY,   /**< replaceBootStatusKey( key, suffix, showSkip ): the bottom line again */
	OP_PROGRESS,      /**< setApProgress( pct ): the gesture's bar, -1 for none */
	OP_END            /**< endBoot( ) */
};

struct Rec {
	uint8_t op;                  /**< Op */
	bool    skip;                /**< showSkip, for the four calls that carry it */
	int16_t key;                 /**< LangKey, or TR_KEYS_COUNT for a raw line */
	int16_t pct;                 /**< OP_PROGRESS only; the boot says -1 and 0..100 */
	char    suffix[SUFFIX_LEN];  /**< cut where BootLogEntry cuts it: 39 bytes and a NUL */
};

template <int N>
struct Recorder {
	Rec      rec[N] = {};
	uint8_t  n = 0;           /**< records kept */
	uint16_t dropped = 0;     /**< calls that found the list full, saturating */
	bool     closed = false;  /**< the boot is over: nothing more is the boot's */
	bool     paused = false;  /**< a replay is calling the six: they are not new calls */

	/** One call to one of the six. Kept, folded into the record before it, counted as dropped, or ignored. */
	void note(Op op, int16_t key, const char* suffix, bool skip, int pct) {
		if (closed || paused) return;
		if (n > 0) {
			Rec& last = rec[n - 1];
			const bool folds = (op == OP_PROGRESS)
			                 ? (last.op == OP_PROGRESS)
			                 : ((op == OP_REPLACE_KEY || op == OP_REPLACE_RAW) && last.op == op && last.key == key);
			if (folds) { fill(last, op, key, suffix, skip, pct); return; }
		}
		if (n >= N) { if (dropped != 0xFFFF) dropped++; return; }
		fill(rec[n++], op, key, suffix, skip, pct);
	}

	void close( ) { closed = true; }

private:
	static void fill(Rec& r, Op op, int16_t key, const char* suffix, bool skip, int pct) {
		r.op = op; r.key = key; r.skip = skip; r.pct = (int16_t)pct;
		size_t i = 0;
		if (suffix) for (; i < sizeof(r.suffix) - 1 && suffix[i]; i++) r.suffix[i] = suffix[i];
		r.suffix[i] = '\0';
	}
};

/** What the words after `screen boot` ask for. */
enum Verb : uint8_t {
	V_BAD,        /**< not a command: the handler refuses it */
	V_LIST,       /**< `screen boot`: print the record */
	V_END,        /**< `screen boot end`: give the panel back */
	V_AT,         /**< `screen boot <i>`: the terminal after call i */
	V_PROGRESS,   /**< `screen boot <i> <pct>`: and the bar at pct */
	V_CANCEL      /**< `screen boot <i> cancel`: and what the boot does when the gesture is let go */
};

struct Cmd {
	Verb verb = V_BAD;
	int  idx = 0;
	int  pct = 0;
};

/** A whole number of one to three digits, no sign, no space, no more than maxv. */
inline bool num(const char* s, int maxv, int& out) {
	if (!s || !*s) return false;
	int v = 0, digits = 0;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' || ++digits > 3) return false;
		v = v * 10 + (*s - '0');
	}
	if (v > maxv) return false;
	out = v;
	return true;
}

/** The two words after `screen boot` ("" or null when absent; the parser hands them over lowercase, strVal2 and strVal3). */
inline Cmd parse(const char* a, const char* b) {
	Cmd c;
	const bool hasA = a && *a, hasB = b && *b;
	if (!hasA) { if (!hasB) c.verb = V_LIST; return c; }
	if (!strcmp(a, "end")) { if (!hasB) c.verb = V_END; return c; }
	int i = 0, p = 0;
	if (!num(a, 999, i)) return c;
	if (!hasB) { c.verb = V_AT; c.idx = i; return c; }
	if (!strcmp(b, "cancel")) { c.verb = V_CANCEL; c.idx = i; return c; }
	if (num(b, 100, p)) { c.verb = V_PROGRESS; c.idx = i; c.pct = p; }
	return c;
}

/**
 * The terminal as it was after record c.idx, then what the verb adds: V_AT nothing, V_PROGRESS the bar at c.pct, V_CANCEL
 * the two calls the boot makes when the gesture is let go (AppManager_Boot.cpp:596-597: setApProgress( -1 ), then
 * setBootStatusKey( TR_BOOT_AP_CANCELLED, nullptr, false ), `cancelKey` being that key). The window is emptied first and
 * records 0..idx are applied through the sink's own six calls, so what is drawn is what the boot drew.
 *
 * false, with the sink untouched, for a verb that is not a replay, an index off the list or a percent off the bar.
 * The recorder is paused for the length of the replay: the six calls it makes are not new calls, even if the list is
 * still open.
 */
template <int N, class Sink>
bool play(Recorder<N>& r, const Cmd& c, int16_t cancelKey, Sink& s) {
	if (c.verb != V_AT && c.verb != V_PROGRESS && c.verb != V_CANCEL) return false;
	if (c.idx < 0 || c.idx >= r.n) return false;
	if (c.verb == V_PROGRESS && (c.pct < 0 || c.pct > 100)) return false;
	r.paused = true;
	s.reset( );
	for (int k = 0; k <= c.idx; k++) {
		const Rec& e = r.rec[k];
		switch (e.op) {
		case OP_SET_RAW:     s.setRaw(e.suffix, e.skip); break;
		case OP_REPLACE_RAW: s.replaceRaw(e.suffix, e.skip); break;
		case OP_SET_KEY:     s.setKey(e.key, e.suffix[0] ? e.suffix : nullptr, e.skip); break;
		case OP_REPLACE_KEY: s.replaceKey(e.key, e.suffix[0] ? e.suffix : nullptr, e.skip); break;
		case OP_PROGRESS:    s.progress(e.pct); break;
		case OP_END:         s.end( ); break;
		default: break;
		}
	}
	if (c.verb == V_PROGRESS) s.progress(c.pct);
	if (c.verb == V_CANCEL) { s.progress(-1); s.setKey(cancelKey, nullptr, false); }
	s.arm( );
	r.paused = false;
	return true;
}

/** The operation's name in the list. */
inline const char* opName(uint8_t op) {
	static const char* const NAMES[] = { "setraw", "replraw", "setkey", "replkey", "progress", "end" };
	return op < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[op] : "?";
}

/**
 * One line of `screen boot`: the index, the operation, and for the four that write a line the key, the skip button, the
 * line's text in English (`text`, null for a raw line) and the suffix; for a progress call the percent. Cut to `cap`, with
 * its NUL; a zero cap writes nothing. Returns what snprintf says.
 */
inline int format(const Rec& r, int idx, const char* text, char* out, size_t cap) {
	if (!out || cap == 0) return 0;
	switch (r.op) {
	case OP_PROGRESS: return snprintf(out, cap, "%2d %-8s pct=%d", idx, opName(r.op), (int)r.pct);
	case OP_END:      return snprintf(out, cap, "%2d %s", idx, opName(r.op));
	default:          return snprintf(out, cap, "%2d %-8s key=%d skip=%d text=\"%s\" suffix=\"%s\"",
	                                  idx, opName(r.op), (int)r.key, r.skip ? 1 : 0, text ? text : "", r.suffix);
	}
}

}  // namespace bootreplay

#endif  // SIMUT_BOOT_TAGS
