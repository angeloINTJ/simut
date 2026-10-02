/**
 * @file TelemetryPosition.h
 * @brief Where telemetry stopped sending, as a write position: day file,
 *        block offset, record.
 *
 * @details The cursor used to be one epoch: a record was unsent when it was
 * newer than the newest one sent. A block written after another but stamped
 * before it — the clock went back: the provisional boot clock corrected by NTP,
 * a manual set — stayed behind that mark and was never offered. Measured: 6 of
 * 75,778 records on the rig (2026-09-21), 1 of 13,671 in the Air's drain
 * (2026-09-23). The record stayed on flash; only telemetry lost it, and said
 * nothing (docs/analysis/A04_CURSOR_POR_POSICAO.md, PLANO_STABLE's B10).
 *
 * A day file only grows at its end — a sealed block is appended, never
 * rewritten — so the byte offset of a block and the index of a record in it
 * say where the record was WRITTEN, whatever its stamp says. The cursor keeps,
 * per day file, the position of the next record not yet sent:
 *
 *   - a file before `floorDay` is done and never read again;
 *   - a file with a slot sends what lies past the slot's position;
 *   - a file with no slot sends everything — except under the old cursor's
 *     rule, for the files that existed when it was migrated (`legacyDay`),
 *     and only while that cursor is not ahead of the clock
 *     (telDropLegacyAhead( ));
 *   - the block still open in RAM already has its position: it will be
 *     appended at the end of its day file.
 *
 * The floor trails today by TEL_POS_KEEP_DAYS and never passes a file that
 * still has something unsent. A block that lands in a file older than that is
 * still skipped: the clock has to go back more than three days for it.
 *
 * A position is only as good as what still sits there. Each slot keeps the
 * stamp of the last record it counted as sent, and the collector checks it
 * before trusting the slot (telSlotHolds( )). Records sent out of RAM and lost
 * with the power before the .wip caught up leave their indices to the next
 * readings; a seal that failed leaves its offset to the next block; a day
 * file deleted, or put back by a backup restore, starts over under the slot.
 * In each case something else now sits where the sent records were, and the
 * whole file is sent again.
 *
 * Where it cannot know, it re-sends rather than skips. The server keys ingest
 * by stamp (TelemetryCursor.h): a duplicate costs a write, a gap costs the
 * measurement.
 *
 * Header-only and dependency-free, so the host can play a small flash through
 * it: see test/test_validators/test_main.cpp.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Day files that keep a position: the K watched days, the one being drained,
 *  and room for late blocks. A full table evicts its oldest day (re-sent). */
constexpr uint8_t  TEL_POS_SLOTS = 8;
/** How long a fully sent file stays watched for a block that lands late. */
constexpr uint8_t  TEL_POS_KEEP_DAYS = 3;
constexpr uint32_t TEL_CURSOR_MAGIC = 0x32504354u;   /* "TCP2" */

/** One day file's position: the block the next unsent record is in (byte
 *  offset of its DATA chunk) and how many of that block's records went. */
struct TelPos {
	uint32_t day;    /**< YYYYMMDD of the file; 0 = free slot */
	uint32_t off;
	uint16_t rec;
	uint16_t pad;
	uint32_t epoch;  /**< stamp of record rec-1: what must still be there */
};
static_assert(sizeof(TelPos) == 16, "TelPos is stored as is");

/** The whole cursor, stored as is in /config/t_cursor.bin. */
struct TelCursorState {
	/** FIRST, on purpose: v2.9.0 and older read these four bytes as their whole
	 *  cursor, so a downgrade still finds the newest epoch delivered. */
	uint32_t lastEpoch;
	uint32_t magic;
	uint32_t floorDay;     /**< files before this day are done; 0 = not set */
	uint32_t legacyEpoch;  /**< the migrated epoch cursor ... */
	uint32_t legacyDay;    /**< ... and the last day file it governs; 0 = none */
	TelPos   pos[TEL_POS_SLOTS];
};
static_assert(sizeof(TelCursorState) == 20 + 16 * TEL_POS_SLOTS, "TelCursorState is stored as is");

/** Where a record of a batch came from. */
struct TelRecPos {
	uint32_t day;
	uint32_t off;
	uint16_t idx;
	uint32_t epoch;
};

/** Runs a batch keeps; a batch that would need more ends early. */
constexpr uint8_t TEL_RUNS_MAX = 16;

/** Consecutive records of one block, as they sit in a batch: batch[start..
 *  start+len) are records firstIdx.. of the block at (day, off). A batch of
 *  250 records from hour-long blocks is five runs; a position per record
 *  would have been 4 KB of heap next to the TLS handshake. */
struct TelRun {
	uint32_t day;
	uint32_t off;
	uint32_t lastEpoch;   /**< stamp of the run's last record */
	uint8_t  firstIdx;
	uint8_t  start;
	uint8_t  len;
	uint8_t  pad;
};

/** Today as the day files name it (YYYYMMDD, local time), with the two days
 *  the floor is measured against. The caller has the clock and the calendar
 *  (StorageManager::historyDayOf); this header has neither. */
struct TelToday {
	uint32_t day;        /**< 0: no clock at all — nothing moves */
	uint32_t window;     /**< TEL_POS_KEEP_DAYS before today */
	uint32_t tomorrow;
	bool     trusted;    /**< NTP or a hand-set clock, not the boot's seed */
};

/* ── The state ───────────────────────────────────────────────────────────── */

inline void telCursorReset(TelCursorState& c, uint32_t floorDay) {
	memset(&c, 0, sizeof(c));
	c.magic = TEL_CURSOR_MAGIC;
	c.floorDay = floorDay;
}

/** From the old epoch cursor: its own rule keeps governing the files up to
 *  @p legacyDay, so nothing already sent goes again and nothing it would have
 *  sent is held back on the day the firmware changes. */
inline void telCursorLegacy(TelCursorState& c, uint32_t legacyEpoch, uint32_t legacyDay,
                            uint32_t floorDay) {
	telCursorReset(c, floorDay);
	c.lastEpoch = legacyEpoch;
	c.legacyEpoch = legacyEpoch;
	c.legacyDay = legacyDay;
}

/**
 * Read a stored cursor. The old 4-byte epoch loads as a legacy cursor that
 * governs EVERY file (`legacyDay` at its maximum) — exactly the old rule —
 * until the caller narrows it with telCursorLegacy( ), which needs the local
 * calendar this header does not have.
 *
 * @return true when @p buf held a cursor; anything else resets @p c.
 */
inline bool telCursorLoad(TelCursorState& c, const uint8_t* buf, size_t len, bool& legacy) {
	legacy = false;
	if (len == 4) {
		uint32_t e;
		memcpy(&e, buf, 4);
		telCursorLegacy(c, e, 0xFFFFFFFFu, 0);
		legacy = true;
		return true;
	}
	if (len == sizeof(TelCursorState)) {
		memcpy(&c, buf, sizeof(c));
		if (c.magic == TEL_CURSOR_MAGIC) return true;
	}
	telCursorReset(c, 0);
	return false;
}

inline const TelPos* telFind(const TelCursorState& c, uint32_t day) {
	if (!day) return nullptr;
	for (uint8_t i = 0; i < TEL_POS_SLOTS; i++) {
		if (c.pos[i].day == day) return &c.pos[i];
	}
	return nullptr;
}

/* ── The rule ────────────────────────────────────────────────────────────── */

/**
 * The old cursor ahead of a clock that can be believed. The epoch it delivered
 * was clamped to the clock of that moment, so it is ahead now only because the
 * clock went back since — and its rule then holds back every record written
 * after, until the clock catches up, and skips them. Its files go to the
 * position rule instead: sent again from the start, a duplicate, not a gap.
 * @p now 0: no clock. @return true when the old rule was dropped.
 */
inline bool telDropLegacyAhead(TelCursorState& c, uint32_t now, bool trusted) {
	if (!trusted || !now || !c.legacyDay || c.legacyEpoch <= now) return false;
	c.legacyDay = 0;
	c.legacyEpoch = 0;
	return true;
}

/**
 * Does the slot still count from the records it was given? Asked with what the
 * file (or the encoder) holds where the slot points: whether a block starts at
 * its offset, how many records that block has, and the stamp of its record
 * rec-1. Anything else there means the records it counted are gone, and the
 * caller sends the file again (telForgetDay( )).
 */
inline bool telSlotHolds(const TelPos& p, bool blockAtOff, uint8_t count, uint32_t epochAtLastSent) {
	if (p.rec == 0) return true;
	return blockAtOff && count >= p.rec && epochAtLastSent == p.epoch;
}

inline bool telFileDone(const TelCursorState& c, uint32_t day) {
	return c.floorDay != 0 && day < c.floorDay;
}

inline bool telLegacyCovers(const TelCursorState& c, uint32_t day) {
	return c.legacyDay != 0 && day <= c.legacyDay;
}

/** Is the record written at (@p day, @p off, @p idx) still to be sent?
 *  @p epoch matters only under the old cursor's rule. */
inline bool telUnsent(const TelCursorState& c, uint32_t day, uint32_t off, uint16_t idx,
                      uint32_t epoch) {
	if (telFileDone(c, day)) return false;
	const TelPos* p = telFind(c, day);
	if (p) return off > p->off || (off == p->off && idx >= p->rec);
	if (telLegacyCovers(c, day)) return epoch > c.legacyEpoch;
	return true;
}

/**
 * How many of a block's @p count records are still to be sent — the pending
 * count, from the header alone. Under the old cursor's rule the header has
 * only the block's first stamp @p t0: a block that starts at or before that
 * epoch counts as sent, so the one that straddles it is short of its tail
 * until the file gets a slot. An estimate's error, not the collector's: that
 * one decides record by record (telUnsent( )).
 */
inline uint8_t telBlockUnsent(const TelCursorState& c, uint32_t day, uint32_t off, uint8_t count,
                              uint32_t t0) {
	if (telFileDone(c, day)) return 0;
	const TelPos* p = telFind(c, day);
	if (p) {
		if (off > p->off) return count;
		if (off < p->off) return 0;
		return (p->rec >= count) ? 0 : (uint8_t)(count - p->rec);
	}
	if (telLegacyCovers(c, day)) return (t0 > c.legacyEpoch) ? count : 0;
	return count;
}

/* ── Moving it ───────────────────────────────────────────────────────────── */

/** Close every file before @p day: the floor rises to it. */
inline void telRaiseFloor(TelCursorState& c, uint32_t day) {
	if (day <= c.floorDay) return;
	c.floorDay = day;
	for (uint8_t i = 0; i < TEL_POS_SLOTS; i++) {
		if (c.pos[i].day && c.pos[i].day < day) memset(&c.pos[i], 0, sizeof(TelPos));
	}
	if (c.legacyDay && c.legacyDay < day) { c.legacyDay = 0; c.legacyEpoch = 0; }
}

/**
 * The slot for @p day, taking a free one or, the table full, the oldest day's.
 *
 * An oldest day before @p day is closed, not forgotten. A batch is collected in
 * day order, so a file before one being delivered was drained by the same
 * batch or an earlier one. Forgetting it would send it again — and a drain
 * whose batches span more days than there are slots would then go round for
 * ever, one file of duplicates per batch. Only a late block in that file is
 * lost, the same as behind the floor.
 *
 * An oldest day after @p day (a late block in an old file, the table full of
 * newer ones) is forgotten: that file is sent again — a duplicate, not a gap.
 */
inline TelPos* telSlotFor(TelCursorState& c, uint32_t day) {
	TelPos* free = nullptr;
	TelPos* oldest = nullptr;
	for (uint8_t i = 0; i < TEL_POS_SLOTS; i++) {
		TelPos& s = c.pos[i];
		if (s.day == day) return &s;
		if (!s.day) { if (!free) free = &s; continue; }
		if (!oldest || s.day < oldest->day) oldest = &s;
	}
	TelPos* s = free;
	if (!s) {
		s = oldest;
		if (oldest->day < day) telRaiseFloor(c, (uint32_t)(oldest->day + 1u));
	}
	memset(s, 0, sizeof(*s));
	s->day = day;
	return s;
}

/** Records @p p[0..n) reached the server: each file's position moves past the
 *  last of them. A batch is in position order per file, so the last one wins;
 *  a position never moves back. */
inline void telAdvance(TelCursorState& c, const TelRecPos* p, size_t n) {
	for (size_t i = 0; i < n; i++) {
		TelPos* s = telSlotFor(c, p[i].day);
		const uint16_t next = (uint16_t)(p[i].idx + 1u);
		if (p[i].off > s->off || (p[i].off == s->off && next > s->rec)) {
			s->off = p[i].off;
			s->rec = next;
			s->epoch = p[i].epoch;
		}
	}
}

/** The same, for a batch kept as runs: each run reached its last record. */
inline void telAdvanceRuns(TelCursorState& c, const TelRun* r, size_t n) {
	for (size_t i = 0; i < n; i++) {
		if (!r[i].len) continue;
		const TelRecPos p = { r[i].day, r[i].off,
		                      (uint16_t)(r[i].firstIdx + r[i].len - 1u), r[i].lastEpoch };
		telAdvance(c, &p, 1);
	}
}

/**
 * Record that batch[@p k] is record @p idx of the block at (@p day, @p off),
 * stamped @p epoch: it extends the last run, or opens one.
 * @return false when it needs a run and all TEL_RUNS_MAX are taken — the
 *         batch ends before this record.
 */
inline bool telRunPush(TelRun* runs, uint8_t& n, uint32_t day, uint32_t off, uint8_t idx,
                       uint8_t k, uint32_t epoch) {
	if (n) {
		TelRun& r = runs[n - 1];
		if (r.day == day && r.off == off && (uint32_t)r.start + r.len == k
		    && (uint32_t)r.firstIdx + r.len == idx) {
			r.len++;
			r.lastEpoch = epoch;
			return true;
		}
	}
	if (n >= TEL_RUNS_MAX) return false;
	runs[n++] = TelRun{ day, off, epoch, idx, k, 1, 0 };
	return true;
}

/** Keep only what batch[0..@p count) covers — what buildPayload kept, or what
 *  a broker took before the connection broke. @p lastEpoch is the stamp of
 *  batch[count-1], where a run is cut. */
inline void telRunsTrim(TelRun* runs, uint8_t& n, uint8_t count, uint32_t lastEpoch) {
	uint8_t keep = 0;
	for (uint8_t i = 0; i < n; i++) {
		TelRun& r = runs[i];
		if (r.start >= count) break;
		if ((uint32_t)r.start + r.len > count) {
			r.len = (uint8_t)(count - r.start);
			r.lastEpoch = lastEpoch;
		}
		keep = (uint8_t)(i + 1);
	}
	n = keep;
}

/**
 * Bring the floor up to the window, TEL_POS_KEEP_DAYS behind today — never
 * past @p firstUnsentDay, the first file a collection found anything unsent in
 * (0 = none). A floor already past today means the clock ran ahead and came
 * back; it is pulled down to the window, and the files between are re-sent.
 *
 * On a trusted clock, a slot for a day past tomorrow is dropped: no record
 * that far ahead is ever sent (the collector's own bound), so its file needs
 * no watching, and a clock that ran ahead and came back would otherwise leave
 * those slots to fill the table. Not on the provisional clock, which may be
 * the one that is behind.
 */
inline void telAdvanceFloor(TelCursorState& c, const TelToday& t, uint32_t firstUnsentDay) {
	if (!t.day) return;
	if (t.trusted) {
		for (uint8_t i = 0; i < TEL_POS_SLOTS; i++) {
			if (c.pos[i].day > t.tomorrow) memset(&c.pos[i], 0, sizeof(TelPos));
		}
	}
	if (c.floorDay > t.day) c.floorDay = t.window;
	uint32_t target = t.window;
	if (firstUnsentDay && firstUnsentDay < target) target = firstUnsentDay;
	telRaiseFloor(c, target);
}

/** A position past the end of its file means the file is not the one it was
 *  counted in — deleted and made again, or put back by a backup restore:
 *  forget it, and the file goes again. */
inline void telForgetIfBeyond(TelCursorState& c, uint32_t day, uint32_t fileSize) {
	for (uint8_t i = 0; i < TEL_POS_SLOTS; i++) {
		if (c.pos[i].day == day && c.pos[i].off > fileSize) memset(&c.pos[i], 0, sizeof(TelPos));
	}
}

/** Forget @p day outright — the caller knows its file was rewritten. */
inline void telForgetDay(TelCursorState& c, uint32_t day) {
	for (uint8_t i = 0; i < TEL_POS_SLOTS; i++) {
		if (c.pos[i].day == day) memset(&c.pos[i], 0, sizeof(TelPos));
	}
}
