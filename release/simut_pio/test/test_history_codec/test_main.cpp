/**
 * @file    test/test_history_codec/test_main.cpp
 * @brief   Host-side unit tests for HistoryCodec v2 encode/decode roundtrip.
 * @details Runs via `pio test -e native_history` (no HW). Covers:
 *            · writeVarintZ / readVarintZ zigzag roundtrip (zero, positive,
 *              negative, extremes, truncated buffer)
 *            · Single-record encode → decode roundtrip (normal, all-NaN,
 *              mixed NaN, full-range values)
 *            · Anchor frame emission (first record, period boundary at 61)
 *            · Delta encoding (records 2..60 are smaller than anchors)
 *            · Multi-record roundtrip across anchor boundaries (60, 65 records)
 *            · Edge cases: empty history, single entry, large epoch delta
 *            · NaN fields omitted from delta mask (compression verification)
 *            · Buffer-too-small error paths (anchor and delta)
 *            · historyCodecReset state clearing
 *            · HistoryFileHeaderV2 compile-time size sanity check
 *
 * Unlike test_validators which copies small functions to avoid deps,
 * this suite compiles production HistoryCodec.cpp directly. Its only
 * dependency chain (Arduino.h → SystemDefs.h → sub-headers) resolves
 * fully under native_stubs/Arduino.h + standard C library.
 *
 * @project SIMUT — HistoryCodec v2 unit testing
 * @author  Contributor
 * @license MIT License
 */

#include <unity.h>
#include "HistoryCodec.h"
#include <cmath>    /* isnan */
#include <cstring>  /* memset, memcmp */

/* ----- Required by native_stubs/Arduino.h linker symbol ----- */
namespace simut_native {
	uint32_t fake_millis_value = 0;
}


/* =========================================================================== */
/*  HELPER: build a BinaryHistoryRecord with given values                      */
/* =========================================================================== */

/** @brief Creates a record with specified ambient + uniform sensor values. */
static BinaryHistoryRecord makeRecord(uint32_t epoch,
                                      int16_t ambTemp,
                                      int16_t ambHum,
                                      int16_t sensorVal) {
	BinaryHistoryRecord rec;
	rec.epoch = epoch;
	rec.ambientTemp = ambTemp;
	rec.ambientHum = ambHum;
	for (int i = 0; i < MAX_SENSORS; i++) {
		rec.sensors[i] = sensorVal;
	}
	return rec;
}

/** @brief Compares two records field-by-field for exact equality. */
static bool recordsEqual(const BinaryHistoryRecord& a,
                         const BinaryHistoryRecord& b) {
	if (a.epoch != b.epoch) return false;
	if (a.ambientTemp != b.ambientTemp) return false;
	if (a.ambientHum != b.ambientHum) return false;
	for (int i = 0; i < MAX_SENSORS; i++) {
		if (a.sensors[i] != b.sensors[i]) return false;
	}
	return true;
}


/* =========================================================================== */
/*                              UNITY HOOKS                                    */
/* =========================================================================== */

void setUp(void) {}
void tearDown(void) {}


/* =========================================================================== */
/*  VARINT ZIGZAG — writeVarintZ / readVarintZ                                 */
/* =========================================================================== */

void test_varintZ_roundtrip_zero(void) {
	uint8_t buf[5];
	size_t written = writeVarintZ(0, buf);
	TEST_ASSERT_EQUAL_size_t(1, written); /* 0 encodes as single byte */

	int32_t out = -1;
	size_t consumed = readVarintZ(buf, written, out);
	TEST_ASSERT_EQUAL_size_t(written, consumed);
	TEST_ASSERT_EQUAL_INT32(0, out);
}

void test_varintZ_roundtrip_positive(void) {
	int32_t values[] = { 1, 63, 64, 127, 128, 16383, 16384 };
	size_t count = sizeof(values) / sizeof(values[0]);

	for (size_t i = 0; i < count; i++) {
		uint8_t buf[5];
		size_t written = writeVarintZ(values[i], buf);
		TEST_ASSERT_GREATER_THAN(0, written);

		int32_t out = 0;
		size_t consumed = readVarintZ(buf, written, out);
		TEST_ASSERT_EQUAL_size_t(written, consumed);
		TEST_ASSERT_EQUAL_INT32(values[i], out);
	}
}

void test_varintZ_roundtrip_negative(void) {
	int32_t values[] = { -1, -64, -128, -16384 };
	size_t count = sizeof(values) / sizeof(values[0]);

	for (size_t i = 0; i < count; i++) {
		uint8_t buf[5];
		size_t written = writeVarintZ(values[i], buf);
		TEST_ASSERT_GREATER_THAN(0, written);

		int32_t out = 0;
		size_t consumed = readVarintZ(buf, written, out);
		TEST_ASSERT_EQUAL_size_t(written, consumed);
		TEST_ASSERT_EQUAL_INT32(values[i], out);
	}
}

void test_varintZ_roundtrip_extremes(void) {
	/* INT32_MIN and INT32_MAX require 5 bytes in zigzag encoding */
	int32_t extremes[] = { INT32_MIN, INT32_MAX };

	for (int i = 0; i < 2; i++) {
		uint8_t buf[5];
		size_t written = writeVarintZ(extremes[i], buf);
		TEST_ASSERT_EQUAL_size_t(5, written); /* worst-case: 5 bytes */

		int32_t out = 0;
		size_t consumed = readVarintZ(buf, written, out);
		TEST_ASSERT_EQUAL_size_t(5, consumed);
		TEST_ASSERT_EQUAL_INT32(extremes[i], out);
	}
}

void test_varintZ_truncated_buffer(void) {
	/* Empty buffer → should return 0 (error) */
	int32_t out = 42;
	TEST_ASSERT_EQUAL_size_t(0, readVarintZ(nullptr, 0, out));

	/* Write a multi-byte varint, then try reading with truncated length */
	uint8_t buf[5];
	size_t written = writeVarintZ(16384, buf);
	TEST_ASSERT_GREATER_THAN(1, written); /* needs >1 byte */

	/* Offer only 1 byte of a multi-byte varint — first byte has continuation
	 * bit set, so readVarintZ will exhaust bufLen without finding terminator */
	size_t consumed = readVarintZ(buf, 1, out);
	TEST_ASSERT_EQUAL_size_t(0, consumed);
}


/* =========================================================================== */
/*  SINGLE RECORD ROUNDTRIP                                                    */
/* =========================================================================== */

void test_singleRecord_roundtrip(void) {
	/* Encode a typical sensor reading, then decode and verify exact match */
	BinaryHistoryRecord original = makeRecord(1717500000, 2345, 6120, 1850);

	HistoryCodecState encState, decState;
	historyCodecReset(encState);
	historyCodecReset(decState);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	bool isAnchor = false;
	size_t written = historyEncodeRecord(original, encState, buf, sizeof(buf), &isAnchor);
	TEST_ASSERT_TRUE(isAnchor); /* first record is always anchor */
	TEST_ASSERT_EQUAL_size_t(sizeof(BinaryHistoryRecord), written);

	BinaryHistoryRecord decoded;
	decoded.clear();
	size_t consumed = historyDecodeRecord(buf, written, decState, decoded, true);
	TEST_ASSERT_EQUAL_size_t(written, consumed);
	TEST_ASSERT_TRUE(recordsEqual(original, decoded));
}

void test_singleRecord_allNan(void) {
	/* All fields set to NaN sentinel — must survive roundtrip unchanged */
	BinaryHistoryRecord original;
	original.epoch = 1717500000;
	original.ambientTemp = HIST_NAN_SENTINEL;
	original.ambientHum = HIST_NAN_SENTINEL;
	for (int i = 0; i < MAX_SENSORS; i++) {
		original.sensors[i] = HIST_NAN_SENTINEL;
	}

	HistoryCodecState encState, decState;
	historyCodecReset(encState);
	historyCodecReset(decState);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	size_t written = historyEncodeRecord(original, encState, buf, sizeof(buf), nullptr);
	TEST_ASSERT_GREATER_THAN(0, written);

	BinaryHistoryRecord decoded;
	decoded.clear();
	size_t consumed = historyDecodeRecord(buf, written, decState, decoded, true);
	TEST_ASSERT_EQUAL_size_t(written, consumed);
	TEST_ASSERT_TRUE(recordsEqual(original, decoded));
}

void test_singleRecord_mixedNan(void) {
	/* Mix of valid and NaN fields — both must survive roundtrip */
	BinaryHistoryRecord original;
	original.epoch = 1717500000;
	original.ambientTemp = 2345;           /* valid */
	original.ambientHum = HIST_NAN_SENTINEL; /* NaN */
	for (int i = 0; i < MAX_SENSORS; i++) {
		original.sensors[i] = (i % 2 == 0) ? (int16_t)(1000 + i * 100) : HIST_NAN_SENTINEL;
	}

	HistoryCodecState encState, decState;
	historyCodecReset(encState);
	historyCodecReset(decState);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	size_t written = historyEncodeRecord(original, encState, buf, sizeof(buf), nullptr);

	BinaryHistoryRecord decoded;
	decoded.clear();
	historyDecodeRecord(buf, written, decState, decoded, true);
	TEST_ASSERT_TRUE(recordsEqual(original, decoded));
}


/* =========================================================================== */
/*  ANCHOR FRAME BEHAVIOR                                                      */
/* =========================================================================== */

void test_anchorEmittedFirst(void) {
	/* First record after reset MUST be an anchor (full 28 B) */
	HistoryCodecState state;
	historyCodecReset(state);

	BinaryHistoryRecord rec = makeRecord(1717500000, 2345, 6120, 1850);
	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	bool isAnchor = false;

	size_t written = historyEncodeRecord(rec, state, buf, sizeof(buf), &isAnchor);
	TEST_ASSERT_TRUE(isAnchor);
	TEST_ASSERT_EQUAL_size_t(sizeof(BinaryHistoryRecord), written);
}

void test_deltaAfterAnchor(void) {
	/* Records 2..60 must be deltas (variable size, typically < 28 B) */
	HistoryCodecState state;
	historyCodecReset(state);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	bool isAnchor = false;
	BinaryHistoryRecord rec = makeRecord(1717500000, 2345, 6120, 1850);

	/* Record 1 = anchor */
	historyEncodeRecord(rec, state, buf, sizeof(buf), &isAnchor);
	TEST_ASSERT_TRUE(isAnchor);

	/* Record 2 = delta (small change in epoch only) */
	rec.epoch += 60;
	rec.ambientTemp += 10; /* +0.10°C change */
	size_t written = historyEncodeRecord(rec, state, buf, sizeof(buf), &isAnchor);
	TEST_ASSERT_FALSE(isAnchor);
	/* Delta with small changes should be significantly smaller than anchor */
	TEST_ASSERT_LESS_THAN(sizeof(BinaryHistoryRecord), written);
}

void test_anchorAtPeriodBoundary(void) {
	/* Record 61 must force a new anchor (anchorPeriod = 60) */
	HistoryCodecState state;
	historyCodecReset(state);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	bool isAnchor = false;
	BinaryHistoryRecord rec = makeRecord(1717500000, 2345, 6120, 1850);

	/* Encode 60 records (1 anchor + 59 deltas) */
	for (int i = 0; i < 60; i++) {
		rec.epoch = 1717500000 + (uint32_t)i * 60;
		historyEncodeRecord(rec, state, buf, sizeof(buf), &isAnchor);
		if (i == 0) {
			TEST_ASSERT_TRUE(isAnchor);
		} else {
			TEST_ASSERT_FALSE(isAnchor);
		}
	}

	/* Record 61 = new anchor */
	rec.epoch = 1717500000 + 60 * 60;
	size_t written = historyEncodeRecord(rec, state, buf, sizeof(buf), &isAnchor);
	TEST_ASSERT_TRUE(isAnchor);
	TEST_ASSERT_EQUAL_size_t(sizeof(BinaryHistoryRecord), written);
}


/* =========================================================================== */
/*  MULTI-RECORD ROUNDTRIP                                                     */
/* =========================================================================== */

void test_multiRecord_roundtrip_60(void) {
	/* Full anchor period: 60 records (1 anchor + 59 deltas) roundtrip */
	static const int N = 60;
	BinaryHistoryRecord originals[N];

	/* Build records with realistic gradual temperature drift */
	for (int i = 0; i < N; i++) {
		originals[i].epoch = 1717500000 + (uint32_t)i * 60;
		originals[i].ambientTemp = (int16_t)(2300 + i * 5);  /* gradual rise */
		originals[i].ambientHum = (int16_t)(6000 - i * 3);
		for (int s = 0; s < MAX_SENSORS; s++) {
			originals[i].sensors[s] = (int16_t)(1800 + s * 100 + i * 2);
		}
	}

	/* Encode all */
	HistoryCodecState encState;
	historyCodecReset(encState);
	uint8_t stream[N * sizeof(BinaryHistoryRecord)]; /* generous buffer */
	size_t totalBytes = 0;

	for (int i = 0; i < N; i++) {
		size_t written = historyEncodeRecord(originals[i], encState,
		                                     stream + totalBytes,
		                                     sizeof(stream) - totalBytes,
		                                     nullptr);
		TEST_ASSERT_GREATER_THAN(0, written);
		totalBytes += written;
	}

	/* Verify compression: total should be less than N * 28 uncompressed */
	TEST_ASSERT_LESS_THAN((size_t)(N * sizeof(BinaryHistoryRecord)), totalBytes);

	/* Decode all and verify exact match */
	HistoryCodecState decState;
	historyCodecReset(decState);
	size_t offset = 0;

	for (int i = 0; i < N; i++) {
		bool isAnchor = (i == 0); /* first record is anchor, rest are deltas */
		BinaryHistoryRecord decoded;
		decoded.clear();

		size_t consumed = historyDecodeRecord(stream + offset,
		                                      totalBytes - offset,
		                                      decState, decoded, isAnchor);
		TEST_ASSERT_GREATER_THAN(0, consumed);
		TEST_ASSERT_TRUE_MESSAGE(recordsEqual(originals[i], decoded),
		                         "Mismatch in 60-record roundtrip");
		offset += consumed;
	}
	TEST_ASSERT_EQUAL_size_t(totalBytes, offset); /* consumed everything */
}

void test_multiRecord_crossAnchor_65(void) {
	/* 65 records: spans 1 anchor boundary (anchor at 1 and 61) */
	static const int N = 65;
	BinaryHistoryRecord originals[N];
	bool anchorFlags[N];

	for (int i = 0; i < N; i++) {
		originals[i].epoch = 1717500000 + (uint32_t)i * 60;
		originals[i].ambientTemp = (int16_t)(2300 + i * 3);
		originals[i].ambientHum = (int16_t)(5500 + i * 2);
		for (int s = 0; s < MAX_SENSORS; s++) {
			originals[i].sensors[s] = (int16_t)(2000 + s * 50 + i);
		}
	}

	/* Encode */
	HistoryCodecState encState;
	historyCodecReset(encState);
	uint8_t stream[N * sizeof(BinaryHistoryRecord)];
	size_t totalBytes = 0;

	for (int i = 0; i < N; i++) {
		bool isAnc = false;
		size_t written = historyEncodeRecord(originals[i], encState,
		                                     stream + totalBytes,
		                                     sizeof(stream) - totalBytes,
		                                     &isAnc);
		TEST_ASSERT_GREATER_THAN(0, written);
		anchorFlags[i] = isAnc;
		totalBytes += written;
	}

	/* Verify anchor positions: 0 and 60 */
	TEST_ASSERT_TRUE(anchorFlags[0]);
	TEST_ASSERT_TRUE(anchorFlags[60]);
	for (int i = 1; i < 60; i++) TEST_ASSERT_FALSE(anchorFlags[i]);
	for (int i = 61; i < N; i++) TEST_ASSERT_FALSE(anchorFlags[i]);

	/* Decode and verify */
	HistoryCodecState decState;
	historyCodecReset(decState);
	size_t offset = 0;

	for (int i = 0; i < N; i++) {
		BinaryHistoryRecord decoded;
		decoded.clear();

		size_t consumed = historyDecodeRecord(stream + offset,
		                                      totalBytes - offset,
		                                      decState, decoded,
		                                      anchorFlags[i]);
		TEST_ASSERT_GREATER_THAN(0, consumed);
		TEST_ASSERT_TRUE_MESSAGE(recordsEqual(originals[i], decoded),
		                         "Mismatch in 65-record cross-anchor roundtrip");
		offset += consumed;
	}
}


/* =========================================================================== */
/*  EDGE CASES                                                                 */
/* =========================================================================== */

void test_emptyHistory(void) {
	/* No records encoded — state must remain uninitialized */
	HistoryCodecState state;
	historyCodecReset(state);
	TEST_ASSERT_FALSE(state.initialized);
	TEST_ASSERT_EQUAL_UINT16(0, state.recordsSinceAnchor);
}

void test_singleEntry(void) {
	/* One record only (anchor) → roundtrip exact, same as test_singleRecord
	 * but explicitly named to match acceptance criteria */
	BinaryHistoryRecord original = makeRecord(1717500000, 2345, 6120, 1850);

	HistoryCodecState encState, decState;
	historyCodecReset(encState);
	historyCodecReset(decState);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	bool isAnchor = false;
	size_t written = historyEncodeRecord(original, encState, buf, sizeof(buf), &isAnchor);
	TEST_ASSERT_TRUE(isAnchor);

	BinaryHistoryRecord decoded;
	decoded.clear();
	size_t consumed = historyDecodeRecord(buf, written, decState, decoded, true);
	TEST_ASSERT_EQUAL_size_t(written, consumed);
	TEST_ASSERT_TRUE(recordsEqual(original, decoded));

	/* State should be initialized after one record */
	TEST_ASSERT_TRUE(encState.initialized);
	TEST_ASSERT_TRUE(decState.initialized);
}

void test_fullRangeValues(void) {
	/* Fields at INT16_MAX, INT16_MIN+1 (not HIST_NAN_SENTINEL), and 0 */
	BinaryHistoryRecord original;
	original.epoch = UINT32_MAX; /* max epoch */
	original.ambientTemp = INT16_MAX;         /* 32767 = +327.67°C */
	original.ambientHum = INT16_MIN + 1;      /* -32767 = -327.67% (not sentinel) */
	for (int i = 0; i < MAX_SENSORS; i++) {
		if (i % 3 == 0) original.sensors[i] = INT16_MAX;
		else if (i % 3 == 1) original.sensors[i] = INT16_MIN + 1;
		else original.sensors[i] = 0;
	}

	HistoryCodecState encState, decState;
	historyCodecReset(encState);
	historyCodecReset(decState);

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	size_t written = historyEncodeRecord(original, encState, buf, sizeof(buf), nullptr);

	BinaryHistoryRecord decoded;
	decoded.clear();
	historyDecodeRecord(buf, written, decState, decoded, true);
	TEST_ASSERT_TRUE(recordsEqual(original, decoded));
}

void test_largeEpochDelta(void) {
	/* Epoch jumps > 1 day (86400 seconds) between consecutive records.
	 * Tests that varint handles large deltas correctly. */
	HistoryCodecState encState, decState;
	historyCodecReset(encState);
	historyCodecReset(decState);

	BinaryHistoryRecord rec1 = makeRecord(1717500000, 2345, 6120, 1850);
	BinaryHistoryRecord rec2 = makeRecord(1717500000 + 86400 * 7, 2345, 6120, 1850); /* +7 days */

	uint8_t buf1[128], buf2[128]; /* enough for v3 anchor (74 bytes) */
	bool isAnc = false;

	/* Anchor */
	size_t w1 = historyEncodeRecord(rec1, encState, buf1, sizeof(buf1), &isAnc);
	TEST_ASSERT_TRUE(isAnc);

	/* Delta with large epoch gap */
	size_t w2 = historyEncodeRecord(rec2, encState, buf2, sizeof(buf2), &isAnc);
	TEST_ASSERT_FALSE(isAnc);

	/* Decode both */
	BinaryHistoryRecord d1, d2;
	d1.clear(); d2.clear();
	historyDecodeRecord(buf1, w1, decState, d1, true);
	historyDecodeRecord(buf2, w2, decState, d2, false);

	TEST_ASSERT_TRUE(recordsEqual(rec1, d1));
	TEST_ASSERT_TRUE(recordsEqual(rec2, d2));
}


/* =========================================================================== */
/*  NaN MASK COMPRESSION VERIFICATION                                          */
/* =========================================================================== */

void test_nanFieldsOmittedFromDelta(void) {
	/* When all sensor fields are NaN, the delta mask should omit them,
	 * resulting in a very small delta (just mask + epoch varint) */
	HistoryCodecState state;
	historyCodecReset(state);

	BinaryHistoryRecord rec;
	rec.clear(); /* zero-init all fields including v3 humidity/pressure */
	rec.epoch = 1717500000;
	rec.ambientTemp = HIST_NAN_SENTINEL;
	rec.ambientHum = HIST_NAN_SENTINEL;
	for (int i = 0; i < MAX_SENSORS; i++) rec.sensors[i] = HIST_NAN_SENTINEL;

	uint8_t buf[128]; /* enough for v3 anchor (74 bytes) + delta */
	/* Record 1: anchor (74 B) */
	historyEncodeRecord(rec, state, buf, sizeof(buf), nullptr);

	/* Record 2: delta — all fields still NaN, mask = 0 */
	rec.epoch += 60;
	size_t written = historyEncodeRecord(rec, state, buf, sizeof(buf), nullptr);

	/* v3 delta: 5B mask + 1B epoch varint = 6 bytes */
	TEST_ASSERT_EQUAL_size_t(6, written);
}


/* =========================================================================== */
/*  BUFFER TOO SMALL                                                           */
/* =========================================================================== */

void test_bufferTooSmall_anchor(void) {
	/* Anchor requires sizeof(BinaryHistoryRecord) = 74 bytes for v3.
	 * Offering less must return 0 (error). */
	HistoryCodecState state;
	historyCodecReset(state);

	BinaryHistoryRecord rec = makeRecord(1717500000, 2345, 6120, 1850);
	uint8_t buf[27]; /* far short of 74-byte v3 anchor */

	size_t written = historyEncodeRecord(rec, state, buf, sizeof(buf), nullptr);
	TEST_ASSERT_EQUAL_size_t(0, written);
}

void test_bufferTooSmall_delta(void) {
	/* Delta needs at least maskBytes + epoch varint.
	 * Offering 1 byte for a delta must return 0. */
	HistoryCodecState state;
	historyCodecReset(state);

	BinaryHistoryRecord rec = makeRecord(1717500000, 2345, 6120, 1850);
	uint8_t bigBuf[128]; /* v3 anchor fits here */

	/* First record (anchor) succeeds */
	historyEncodeRecord(rec, state, bigBuf, sizeof(bigBuf), nullptr);

	/* Second record (delta) with tiny buffer */
	rec.epoch += 60;
	uint8_t tinyBuf[1];
	size_t written = historyEncodeRecord(rec, state, tinyBuf, sizeof(tinyBuf), nullptr);
	TEST_ASSERT_EQUAL_size_t(0, written);
}


/* =========================================================================== */
/*  STATE MANAGEMENT                                                           */
/* =========================================================================== */

void test_codecReset(void) {
	/* historyCodecReset must clear ALL state fields */
	HistoryCodecState state;
	/* Dirty the struct with non-zero data */
	memset(&state, 0xFF, sizeof(state));

	historyCodecReset(state);
	TEST_ASSERT_FALSE(state.initialized);
	TEST_ASSERT_EQUAL_UINT16(0, state.recordsSinceAnchor);

	/* fieldHasValid must all be false */
	for (int i = 0; i < 2 + MAX_SENSORS; i++) {
		TEST_ASSERT_FALSE(state.fieldHasValid[i]);
	}
}

void test_fileHeader_size(void) {
	/* Runtime sanity check: HistoryFileHeaderV2 must be exactly 16 bytes.
	 * The static_assert in HistoryCodec.h catches this at compile time,
	 * but this test documents the contract in the test suite. */
	TEST_ASSERT_EQUAL_size_t(HIST_V2_HEADER_SIZE, sizeof(HistoryFileHeaderV2));
	TEST_ASSERT_EQUAL_size_t(16, sizeof(HistoryFileHeaderV2));
}


/* =========================================================================== */
/*                                  MAIN                                       */
/* =========================================================================== */
int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();

	/* varint zigzag */
	RUN_TEST(test_varintZ_roundtrip_zero);
	RUN_TEST(test_varintZ_roundtrip_positive);
	RUN_TEST(test_varintZ_roundtrip_negative);
	RUN_TEST(test_varintZ_roundtrip_extremes);
	RUN_TEST(test_varintZ_truncated_buffer);

	/* single record roundtrip */
	RUN_TEST(test_singleRecord_roundtrip);
	RUN_TEST(test_singleRecord_allNan);
	RUN_TEST(test_singleRecord_mixedNan);

	/* anchor frame behavior */
	RUN_TEST(test_anchorEmittedFirst);
	RUN_TEST(test_deltaAfterAnchor);
	RUN_TEST(test_anchorAtPeriodBoundary);

	/* multi-record roundtrip */
	RUN_TEST(test_multiRecord_roundtrip_60);
	RUN_TEST(test_multiRecord_crossAnchor_65);

	/* edge cases */
	RUN_TEST(test_emptyHistory);
	RUN_TEST(test_singleEntry);
	RUN_TEST(test_fullRangeValues);
	RUN_TEST(test_largeEpochDelta);

	/* NaN compression */
	RUN_TEST(test_nanFieldsOmittedFromDelta);

	/* buffer overflow protection */
	RUN_TEST(test_bufferTooSmall_anchor);
	RUN_TEST(test_bufferTooSmall_delta);

	/* state management */
	RUN_TEST(test_codecReset);
	RUN_TEST(test_fileHeader_size);

	return UNITY_END();
}
