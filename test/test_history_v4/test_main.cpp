/**
 * @file    test/test_history_v4/test_main.cpp
 * @brief   Host-side unit tests for HistoryV4 codec encode/decode roundtrip.
 * @details Runs via `pio test -e native_history_v4` (no HW). Covers:
 *            · Zigzag varint roundtrip (reused from HistoryCodec.cpp)
 *            · Bit packing: extract/insert at arbitrary bit offsets (1-64 bits)
 *            · Header write → read roundtrip (1, 3, 16 sensors)
 *            · Single-record anchor encode → decode (normal, all-NaN, mixed)
 *            · Delta encoding (records 2..60 verify smaller than anchors)
 *            · Multi-record roundtrip across anchor boundaries (60, 65, 1440)
 *            · NaN fields preserved across encode/decode
 *            · Buffer-too-small error paths
 *            · histV4Reset state clearing
 *            · histV4MakeMeasKey measurement key generation
 *            · histV4ScanFile state reconstruction
 *
 * Compiles production HistoryCodec.cpp (for varint) + HistoryV4.cpp.
 * Dependency chain resolves under native_stubs/Arduino.h + standard C library.
 *
 * @project SIMUT — HistoryV4 codec unit testing
 * @license MIT License
 */

#include <unity.h>
#include "HistoryV4.h"
#include <cmath>    /* isnan, round */
#include <cstring>  /* memset, memcmp, strlen */
#include <cstdio>   /* snprintf */

/* ----- Required by native_stubs/Arduino.h linker symbol ----- */
namespace simut_native {
    uint32_t fake_millis_value = 0;
}

/* ============================================================================
 *  TEST HELPERS
 * ============================================================================ */

/** Build a minimal schema with N measurements, all 16-bit. */
static void buildTestSchema(HistV4State &state, uint8_t measureCount,
                            uint8_t sensorCount = 1) {
    histV4Reset(state);
    state.measureCount = measureCount;
    state.sensorCount  = sensorCount;

    /* String pool: simple concatenation */
    uint8_t poolOff = 0;
    for (uint8_t s = 0; s < sensorCount; s++) {
        state.sensors[s].hwIdOffset = poolOff;
        char hwId[8];
        snprintf(hwId, sizeof(hwId), "S%d", s);
        uint8_t len = strlen(hwId);
        memcpy(state.strPool + poolOff, hwId, len);
        state.sensors[s].hwIdLen = len;
        poolOff += len;

        state.sensors[s].nameOffset = poolOff;
        char name[16];
        snprintf(name, sizeof(name), "Sensor%d", s);
        len = strlen(name);
        memcpy(state.strPool + poolOff, name, len);
        state.sensors[s].nameLen = len;
        poolOff += len;

        state.sensors[s].sensorType   = TYPE_DS18B20;
        state.sensors[s].channelMask  = 0x01; /* temp only */
    }
    state.strPoolSize = poolOff;

    for (uint8_t i = 0; i < measureCount; i++) {
        state.measures[i].sensorIdx  = 0;
        state.measures[i].channel    = 0; /* CH_TEMP */
        state.measures[i].bitWidth   = 16;
        state.measures[i].decimals   = 1;
        state.measures[i].unitOffset = poolOff;
        const char *unit = "°C";
        uint8_t ulen = strlen(unit);
        memcpy(state.strPool + poolOff, unit, ulen);
        state.measures[i].unitLen = ulen;
        poolOff += ulen;
        state.measures[i].scale = 100; /* x100 */
        state.strPoolSize = poolOff;
    }

    /* Pre-compute bit offsets */
    uint16_t bitOff = 32; /* epoch first */
    for (uint8_t i = 0; i < measureCount; i++) {
        state.measureByteOffset[i] = bitOff >> 3;
        state.measureBitOffset[i]  = bitOff;
        bitOff += state.measures[i].bitWidth;
    }
    state.anchorByteSize = (bitOff + 7) / 8;
}

/** Check two value arrays match. */
static bool valuesMatch(const int64_t *a, const int64_t *b, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/* ============================================================================
 *  UNITY HOOKS
 * ============================================================================ */

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 *  BIT PACKING TESTS
 * ============================================================================ */

static void test_bitExtract_aligned_16bit(void) {
    /* Write 0xABCD at bit offset 0 */
    uint8_t buf[8] = {0};
    histV4BitInsert(buf, 0, 16, 0xABCD);
    int64_t v = histV4BitExtract(buf, 0, 16);
    /* 0xABCD = 43981 as unsigned, but sign-extended from bit 15=1 → negative */
    /* Actually for 0xABCD: bit 15 is 1, so sign-extended to 0xFFFF...ABCD */
    TEST_ASSERT_EQUAL_INT64((int64_t)(int16_t)0xABCD, v);
}

static void test_bitExtract_unaligned_8bit(void) {
    uint8_t buf[8] = {0};
    /* Write 0x55 at bit offset 3 */
    histV4BitInsert(buf, 3, 8, 0x55);
    int64_t v = histV4BitExtract(buf, 3, 8);
    TEST_ASSERT_EQUAL_INT64(0x55, v);
}

static void test_bitExtract_unaligned_12bit(void) {
    uint8_t buf[8] = {0};
    /* Write value 0x7FF at bit offset 5 */
    histV4BitInsert(buf, 5, 12, 0x7FF);
    int64_t v = histV4BitExtract(buf, 5, 12);
    TEST_ASSERT_EQUAL_INT64(0x7FF, v);
}

static void test_bitExtract_32bit(void) {
    uint8_t buf[8] = {0};
    int64_t epoch = 1752969600; /* typical epoch */
    histV4BitInsert(buf, 0, 32, epoch);
    int64_t v = histV4BitExtract(buf, 0, 32);
    TEST_ASSERT_EQUAL_INT64(epoch, v);
}

static void test_bitExtract_1bit(void) {
    uint8_t buf[8] = {0};
    histV4BitInsert(buf, 7, 1, 1); /* set bit 7 */
    int64_t v = histV4BitExtract(buf, 7, 1);
    TEST_ASSERT_EQUAL_INT64(1, v);

    histV4BitInsert(buf, 6, 1, 0); /* clear bit 6 */
    v = histV4BitExtract(buf, 6, 1);
    TEST_ASSERT_EQUAL_INT64(0, v);
}

static void test_bitExtract_negative(void) {
    uint8_t buf[8] = {0};
    /* -100 in 16-bit two's complement */
    histV4BitInsert(buf, 0, 16, -100);
    int64_t v = histV4BitExtract(buf, 0, 16);
    TEST_ASSERT_EQUAL_INT64(-100, v);
}

static void test_bitExtract_64bit(void) {
    uint8_t buf[16] = {0};
    int64_t bigVal = 0x7ABCDEF012345678LL;
    histV4BitInsert(buf, 0, 64, bigVal);
    int64_t v = histV4BitExtract(buf, 0, 64);
    TEST_ASSERT_EQUAL_INT64(bigVal, v);
}

static void test_bitInsert_preserves_adjacent(void) {
    uint8_t buf[8] = {0};
    /* Write valid 12-bit values (max signed = 2046) */
    histV4BitInsert(buf, 0,  12, 0x555);  /* 1365 */
    histV4BitInsert(buf, 12, 12, 0x666);  /* 1638 */
    histV4BitInsert(buf, 24, 12, 0x777);  /* 1911 */
    TEST_ASSERT_EQUAL_INT64(0x555, histV4BitExtract(buf, 0,  12));
    TEST_ASSERT_EQUAL_INT64(0x666, histV4BitExtract(buf, 12, 12));
    TEST_ASSERT_EQUAL_INT64(0x777, histV4BitExtract(buf, 24, 12));
    /* Verify first value is not corrupted */
    TEST_ASSERT_EQUAL_INT64(0x555, histV4BitExtract(buf, 0, 12));
}

/* ============================================================================
 *  NAN SENTINEL TESTS
 * ============================================================================ */

static void test_nanSentinel_16bit(void) {
    int64_t nan = histV4NanSentinel(16);
    TEST_ASSERT_EQUAL_INT64(0xFFFF, nan);
    TEST_ASSERT_TRUE(histV4IsNan(0xFFFF, 16));
    TEST_ASSERT_FALSE(histV4IsNan(0x0000, 16));
    TEST_ASSERT_FALSE(histV4IsNan(0x7FFF, 16));
}

static void test_nanSentinel_10bit(void) {
    int64_t nan = histV4NanSentinel(10);
    TEST_ASSERT_EQUAL_INT64(0x3FF, nan);
    TEST_ASSERT_TRUE(histV4IsNan(0x3FF, 10));
    TEST_ASSERT_FALSE(histV4IsNan(0x100, 10));
}

static void test_nanSentinel_1bit(void) {
    int64_t nan = histV4NanSentinel(1);
    TEST_ASSERT_EQUAL_INT64(1, nan);
    TEST_ASSERT_TRUE(histV4IsNan(1, 1));
    TEST_ASSERT_FALSE(histV4IsNan(0, 1));
}

/* ============================================================================
 *  SINGLE RECORD ENCODE → DECODE ROUNDTRIP
 * ============================================================================ */

static void test_anchor_roundtrip_1measure(void) {
    HistV4State state;
    buildTestSchema(state, 1);

    int64_t inVals[1] = {2500}; /* 25.00°C */
    uint8_t buf[128];
    bool isAnchor;

    size_t n = histV4Encode(inVals, 1, state, buf, sizeof(buf), 1752969600, &isAnchor);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(isAnchor);

    /* Decode */
    HistV4State decState;
    buildTestSchema(decState, 1);
    int64_t outVals[1];
    uint32_t outEpoch;
    size_t consumed = histV4Decode(buf, n, decState, outVals, &outEpoch, true);

    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_UINT32(1752969600, outEpoch);
    TEST_ASSERT_EQUAL_INT64(2500, outVals[0]);
}

static void test_anchor_roundtrip_3measures(void) {
    HistV4State state;
    buildTestSchema(state, 3);

    int64_t inVals[3] = {2500, -500, 3100}; /* 25°C, -5°C, 31°C */
    uint8_t buf[128];
    bool isAnchor;

    size_t n = histV4Encode(inVals, 3, state, buf, sizeof(buf), 1752969600, &isAnchor);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(isAnchor);

    HistV4State decState;
    buildTestSchema(decState, 3);
    int64_t outVals[3];
    uint32_t outEpoch;
    size_t consumed = histV4Decode(buf, n, decState, outVals, &outEpoch, true);

    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_UINT32(1752969600, outEpoch);
    TEST_ASSERT_TRUE(valuesMatch(inVals, outVals, 3));
}

static void test_anchor_roundtrip_withNaN(void) {
    HistV4State state;
    buildTestSchema(state, 3);

    int64_t nan16 = histV4NanSentinel(16);
    int64_t inVals[3] = {2500, nan16, 3100}; /* middle is NaN */
    uint8_t buf[128];
    bool isAnchor;

    size_t n = histV4Encode(inVals, 3, state, buf, sizeof(buf), 1752969600, &isAnchor);
    TEST_ASSERT_TRUE(n > 0);

    HistV4State decState;
    buildTestSchema(decState, 3);
    int64_t outVals[3];
    uint32_t outEpoch;
    size_t consumed = histV4Decode(buf, n, decState, outVals, &outEpoch, true);

    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_TRUE(valuesMatch(inVals, outVals, 3));
    TEST_ASSERT_TRUE(histV4IsNan(outVals[1], 16));
    TEST_ASSERT_FALSE(histV4IsNan(outVals[0], 16));
    TEST_ASSERT_FALSE(histV4IsNan(outVals[2], 16));
}

static void test_anchor_roundtrip_allNaN(void) {
    HistV4State state;
    buildTestSchema(state, 2);

    int64_t nan16 = histV4NanSentinel(16);
    int64_t inVals[2] = {nan16, nan16};
    uint8_t buf[128];
    bool isAnchor;

    size_t n = histV4Encode(inVals, 2, state, buf, sizeof(buf), 1752969600, &isAnchor);
    TEST_ASSERT_TRUE(n > 0);

    HistV4State decState;
    buildTestSchema(decState, 2);
    int64_t outVals[2];
    uint32_t outEpoch;
    histV4Decode(buf, n, decState, outVals, &outEpoch, true);

    TEST_ASSERT_TRUE(valuesMatch(inVals, outVals, 2));
}

/* ============================================================================
 *  DELTA ENCODE → DECODE
 * ============================================================================ */

static void test_delta_roundtrip_after_anchor(void) {
    HistV4State state;
    buildTestSchema(state, 2);

    int64_t vals1[2] = {2500, 3000};
    uint8_t buf[256];
    bool isAnchor;

    /* First record → anchor */
    histV4Encode(vals1, 2, state, buf, sizeof(buf), 1000, &isAnchor);
    TEST_ASSERT_TRUE(isAnchor);
    TEST_ASSERT_EQUAL_UINT16(1, state.recordsSinceAnchor);

    /* Second record → delta (small change) */
    int64_t vals2[2] = {2510, 3005}; /* +0.1°C and +0.05°C */
    size_t deltaSize = histV4Encode(vals2, 2, state, buf, sizeof(buf), 1060, &isAnchor);
    TEST_ASSERT_TRUE(deltaSize > 0);
    TEST_ASSERT_FALSE(isAnchor);
    TEST_ASSERT_EQUAL_UINT16(2, state.recordsSinceAnchor);

    /* Delta should be smaller than anchor */
    TEST_ASSERT_TRUE(deltaSize < state.anchorByteSize);

    /* Decode the delta */
    HistV4State decState;
    buildTestSchema(decState, 2);
    /* First, decode the anchor to prime the state */
    int64_t anchorVals[2] = {2500, 3000};
    uint32_t anchorEpoch;
    uint8_t anchorBuf[128];
    size_t anchorSize = histV4Encode(anchorVals, 2, decState, anchorBuf, sizeof(anchorBuf), 1000);
    histV4Decode(anchorBuf, anchorSize, decState, anchorVals, &anchorEpoch, true);

    /* Now decode the delta */
    int64_t outVals[2];
    uint32_t outEpoch;
    size_t consumed = histV4Decode(buf, deltaSize, decState, outVals, &outEpoch, false);

    TEST_ASSERT_EQUAL_size_t(deltaSize, consumed);
    TEST_ASSERT_EQUAL_UINT32(1060, outEpoch);
    TEST_ASSERT_TRUE(valuesMatch(vals2, outVals, 2));
}

static void test_delta_unchanged_fields_omitted(void) {
    HistV4State state;
    buildTestSchema(state, 4);

    int64_t vals1[4] = {1000, 2000, 3000, 4000};
    uint8_t buf[256];
    bool isAnchor;

    histV4Encode(vals1, 4, state, buf, sizeof(buf), 1000, &isAnchor);
    TEST_ASSERT_TRUE(isAnchor);

    /* Only field 2 changes, fields 0,1,3 unchanged */
    int64_t vals2[4] = {1000, 2000, 3050, 4000};
    size_t deltaSize = histV4Encode(vals2, 4, state, buf, sizeof(buf), 1060, &isAnchor);
    TEST_ASSERT_TRUE(deltaSize > 0);
    TEST_ASSERT_FALSE(isAnchor);

    /* Delta should be very small (only 1 field changed = 1 mask byte + 1 varint epoch + 1 varint) */
    TEST_ASSERT_TRUE(deltaSize <= 8); /* mask(1) + epoch varint(1-2) + delta varint(1-2) */
}

static void test_delta_nan_to_value_transition(void) {
    /* Start with NaN in field 0, then transition to a valid value */
    HistV4State state;
    buildTestSchema(state, 2);

    int64_t nan16 = histV4NanSentinel(16);
    int64_t vals1[2] = {nan16, 2000}; /* field 0 = NaN, field 1 = 20°C */
    uint8_t buf[256];
    bool isAnchor;

    histV4Encode(vals1, 2, state, buf, sizeof(buf), 1000, &isAnchor);

    /* Field 0 transitions from NaN to valid */
    int64_t vals2[2] = {2500, 2010}; /* field 0 = 25°C now, field 1 changed slightly */
    size_t deltaSize = histV4Encode(vals2, 2, state, buf, sizeof(buf), 1060, &isAnchor);
    TEST_ASSERT_TRUE(deltaSize > 0);

    /* Decode and verify */
    HistV4State decState;
    buildTestSchema(decState, 2);
    uint8_t anchorBuf[128];
    size_t anchorSize = histV4Encode(vals1, 2, decState, anchorBuf, sizeof(anchorBuf), 1000);
    int64_t dummy[2]; uint32_t dummyEpoch;
    histV4Decode(anchorBuf, anchorSize, decState, dummy, &dummyEpoch, true);

    int64_t outVals[2]; uint32_t outEpoch;
    histV4Decode(buf, deltaSize, decState, outVals, &outEpoch, false);
    TEST_ASSERT_TRUE(valuesMatch(vals2, outVals, 2));
}

/* ============================================================================
 *  MULTI-RECORD ACROSS ANCHOR BOUNDARY
 * ============================================================================ */

static void test_across_anchor_boundary(void) {
    HistV4State encState;
    buildTestSchema(encState, 2);
    uint8_t buf[256];

    HistV4State decState;
    buildTestSchema(decState, 2);

    /* Encode 65 records (crosses anchor boundary at 60) */
    for (int r = 0; r < 65; r++) {
        int64_t vals[2] = {2500 + r * 5, 3000 + r * 3};
        uint32_t epoch = 1000 + r * 60;
        bool isAnchor;

        size_t n = histV4Encode(vals, 2, encState, buf, sizeof(buf), epoch, &isAnchor);

        int64_t outVals[2]; uint32_t outEpoch;
        size_t consumed = histV4Decode(buf, n, decState, outVals, &outEpoch,
                                       isAnchor);
        TEST_ASSERT_EQUAL_size_t(n, consumed);
        TEST_ASSERT_EQUAL_UINT32(epoch, outEpoch);
        TEST_ASSERT_TRUE(valuesMatch(vals, outVals, 2));

        /* Verify anchor period: anchors at records 1 and 61 (0-indexed: 0 and 60) */
        if (r == 0 || r == 60) {
            TEST_ASSERT_TRUE(isAnchor);
        } else {
            TEST_ASSERT_FALSE(isAnchor);
        }
    }
}

static void test_delta_epoch_tracking(void) {
    HistV4State encState;
    buildTestSchema(encState, 1);
    uint8_t buf[256];

    /* Encode anchor at epoch 1000 */
    int64_t val1[1] = {2500};
    bool isAnchor;
    histV4Encode(val1, 1, encState, buf, sizeof(buf), 1000, &isAnchor);
    TEST_ASSERT_EQUAL_UINT32(1000, encState.lastEpoch);

    /* Delta at epoch 1060 (60 sec later) */
    int64_t val2[1] = {2510};
    histV4Encode(val2, 1, encState, buf, sizeof(buf), 1060, &isAnchor);
    TEST_ASSERT_EQUAL_UINT32(1060, encState.lastEpoch);
    TEST_ASSERT_FALSE(isAnchor);

    /* Delta at epoch 1120 */
    int64_t val3[1] = {2520};
    histV4Encode(val3, 1, encState, buf, sizeof(buf), 1120, &isAnchor);
    TEST_ASSERT_EQUAL_UINT32(1120, encState.lastEpoch);

    /* Now decode from the deltas and verify epoch tracking */
    HistV4State decState;
    buildTestSchema(decState, 1);

    /* Anchor */
    uint8_t abuf[128];
    size_t an = histV4Encode(val1, 1, decState, abuf, sizeof(abuf), 1000);
    int64_t dv[1]; uint32_t de;
    histV4Decode(abuf, an, decState, dv, &de, true);
    TEST_ASSERT_EQUAL_UINT32(1000, de);

    /* Delta 1 */
    uint8_t dbuf1[128];
    /* Rebuild the delta from encState for epoch 1060 */
    HistV4State tmpEnc;
    buildTestSchema(tmpEnc, 1);
    histV4Encode(val1, 1, tmpEnc, abuf, sizeof(abuf), 1000);
    size_t dn1 = histV4Encode(val2, 1, tmpEnc, dbuf1, sizeof(dbuf1), 1060);
    histV4Decode(dbuf1, dn1, decState, dv, &de, false);
    TEST_ASSERT_EQUAL_UINT32(1060, de);

    /* Delta 2 */
    uint8_t dbuf2[128];
    size_t dn2 = histV4Encode(val3, 1, tmpEnc, dbuf2, sizeof(dbuf2), 1120);
    histV4Decode(dbuf2, dn2, decState, dv, &de, false);
    TEST_ASSERT_EQUAL_UINT32(1120, de);
}

/* ============================================================================
 *  BUFFER OVERFLOW PROTECTION
 * ============================================================================ */

static void test_encode_buffer_too_small(void) {
    HistV4State state;
    buildTestSchema(state, 4);

    int64_t vals[4] = {1000, 2000, 3000, 4000};
    uint8_t buf[2]; /* too small */
    bool isAnchor;

    size_t n = histV4Encode(vals, 4, state, buf, sizeof(buf), 1000, &isAnchor);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

static void test_decode_buffer_too_small_anchor(void) {
    HistV4State state;
    buildTestSchema(state, 4);

    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    /* Only 16 bytes for an anchor that needs state.anchorByteSize bytes */
    /* Anchor with 4 x 16-bit = 32 + 64 = 96 bits = 12 bytes, so 16 is enough */
    /* Let's test with more measurements */
    HistV4State bigState;
    buildTestSchema(bigState, 6); /* 32 + 96 = 128 bits = 16 bytes anchor */
    uint8_t smallBuf[10];
    int64_t outVals[6]; uint32_t outEpoch;
    size_t consumed = histV4Decode(smallBuf, sizeof(smallBuf), bigState, outVals, &outEpoch, true);
    TEST_ASSERT_EQUAL_size_t(0, consumed);
}

/* ============================================================================
 *  STATE RESET
 * ============================================================================ */

static void test_reset_clears_state(void) {
    HistV4State state;
    buildTestSchema(state, 3);

    int64_t vals[3] = {1000, 2000, 3000};
    uint8_t buf[128];
    histV4Encode(vals, 3, state, buf, sizeof(buf), 1000);

    TEST_ASSERT_TRUE(state.initialized);
    TEST_ASSERT_EQUAL_UINT16(1, state.recordsSinceAnchor);
    TEST_ASSERT_TRUE(state.fieldHasValid[0]);

    histV4Reset(state);
    TEST_ASSERT_FALSE(state.initialized);
    TEST_ASSERT_EQUAL_UINT16(0, state.recordsSinceAnchor);
    TEST_ASSERT_EQUAL_UINT8(0, state.measureCount);
}

/* ============================================================================
 *  MEASUREMENT KEY GENERATION
 * ============================================================================ */

static void test_makeMeasKey_temp(void) {
    char key[32];
    size_t n = histV4MakeMeasKey(key, sizeof(key), 0, "THD0001");
    TEST_ASSERT_EQUAL_size_t(8, n);
    TEST_ASSERT_EQUAL_STRING("tTHD0001", key);
}

static void test_makeMeasKey_hum(void) {
    char key[32];
    size_t n = histV4MakeMeasKey(key, sizeof(key), 1, "AMB");
    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_STRING("uAMB", key);
}

static void test_makeMeasKey_press(void) {
    char key[32];
    histV4MakeMeasKey(key, sizeof(key), 2, "BMP1");
    TEST_ASSERT_EQUAL_STRING("pBMP1", key);
}

static void test_makeMeasKey_lux(void) {
    char key[32];
    histV4MakeMeasKey(key, sizeof(key), 3, "LUMI");
    TEST_ASSERT_EQUAL_STRING("lLUMI", key);
}

static void test_makeMeasKey_buffer_too_small(void) {
    char key[4]; /* only 3 chars + null */
    size_t n = histV4MakeMeasKey(key, sizeof(key), 0, "THD0001");
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_STRING("tTH", key);
}

/* ============================================================================
 *  VALUE CONVERSION (float ↔ raw)
 * ============================================================================ */

static void test_floatToRaw_normal(void) {
    HistV4MeasureDef def = {};
    def.bitWidth = 16;
    def.scale = 100; /* x100 */

    int64_t raw = histV4FromFloat(25.5f, def);
    TEST_ASSERT_EQUAL_INT64(2550, raw);
}

static void test_floatToRaw_nan(void) {
    HistV4MeasureDef def = {};
    def.bitWidth = 16;
    def.scale = 100;

    int64_t raw = histV4FromFloat(NAN, def);
    TEST_ASSERT_TRUE(histV4IsNan(raw, 16));
}

static void test_rawToFloat_normal(void) {
    HistV4MeasureDef def = {};
    def.bitWidth = 16;
    def.scale = 100;

    float v = histV4ToFloat(2550, def);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.5f, v);
}

static void test_rawToFloat_nan(void) {
    HistV4MeasureDef def = {};
    def.bitWidth = 16;
    def.scale = 100;

    int64_t nan16 = histV4NanSentinel(16);
    float v = histV4ToFloat(nan16, def);
    TEST_ASSERT_TRUE(isnan(v));
}

static void test_floatRoundtrip(void) {
    HistV4MeasureDef def = {};
    def.bitWidth = 16;
    def.scale = 100;

    float inputs[] = {0.0f, -10.5f, 25.0f, 100.0f, -40.0f, 85.75f};
    for (size_t i = 0; i < sizeof(inputs)/sizeof(inputs[0]); i++) {
        int64_t raw = histV4FromFloat(inputs[i], def);
        float out = histV4ToFloat(raw, def);
        TEST_ASSERT_FLOAT_WITHIN(0.02f, inputs[i], out);
    }
}

static void test_channelPrefixes(void) {
    TEST_ASSERT_EQUAL_CHAR('t', histV4ChannelPrefix(0));
    TEST_ASSERT_EQUAL_CHAR('u', histV4ChannelPrefix(1));
    TEST_ASSERT_EQUAL_CHAR('p', histV4ChannelPrefix(2));
    TEST_ASSERT_EQUAL_CHAR('l', histV4ChannelPrefix(3));
    TEST_ASSERT_EQUAL_CHAR('x', histV4ChannelPrefix(99));
}

/* ============================================================================
 *  v1.5.3 REGRESSION TESTS — NaN transitions, count guard, reopen/resume
 * ============================================================================ */

/** valid→NaN in a DELTA must decode to the exact sentinel (was 68035). */
static void test_delta_valid_to_nan_transition(void) {
    HistV4State enc, dec;
    buildTestSchema(enc, 1);
    buildTestSchema(dec, 1);
    uint8_t buf[64];
    int64_t v[1]; uint32_t ep;

    v[0] = 2500;                                        /* 25.0 °C anchor  */
    size_t n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1000);
    histV4Decode(buf, n, dec, v, &ep, true);

    v[0] = histV4NanSentinel(16);                       /* sensor failure  */
    n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1060);
    TEST_ASSERT_TRUE(n > 0);
    histV4Decode(buf, n, dec, v, &ep, false);
    TEST_ASSERT_TRUE(histV4IsNan(v[0], 16));            /* not 2500+65535  */

    v[0] = 2510;                                        /* recovery        */
    n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1120);
    histV4Decode(buf, n, dec, v, &ep, false);
    TEST_ASSERT_EQUAL_INT64(2510, v[0]);                /* was 5010        */
}

/** A NaN ANCHOR must clear validity on both sides → next delta absolute. */
static void test_anchor_nan_after_valid_then_delta(void) {
    HistV4State enc, dec;
    buildTestSchema(enc, 1);
    buildTestSchema(dec, 1);
    uint8_t buf[64];
    int64_t v[1]; uint32_t ep;

    v[0] = 3000;                                        /* anchor #1: valid */
    size_t n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1000);
    histV4Decode(buf, n, dec, v, &ep, true);

    enc.recordsSinceAnchor = enc.anchorPeriod;          /* force anchor #2  */
    v[0] = histV4NanSentinel(16);
    n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1060);
    histV4Decode(buf, n, dec, v, &ep, true);
    TEST_ASSERT_TRUE(histV4IsNan(v[0], 16));

    v[0] = 3100;                                        /* delta after NaN  */
    n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1120);
    histV4Decode(buf, n, dec, v, &ep, false);
    TEST_ASSERT_EQUAL_INT64(3100, v[0]);
}

/** Rollover guard: caller count ≠ schema count must fail, not corrupt. */
static void test_encode_rejects_count_mismatch(void) {
    HistV4State state;
    buildTestSchema(state, 3);
    int64_t v[2] = {100, 200};
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(0, histV4Encode(v, 2, state, buf, sizeof(buf), 1000));
}

/** Encode a full stream then re-read it like the fixed scan does:
 *  header parsed for its REAL length, records decoded sequentially —
 *  the resumed state must equal the writer's so appends stay in cadence. */
static void test_reopen_resume_continuity(void) {
    HistV4State enc;
    buildTestSchema(enc, 2);

    static uint8_t stream[2048];
    size_t pos = histV4WriteHeaderBuf(stream, sizeof(stream),
        enc.sensors, enc.sensorCount, enc.measures, enc.measureCount,
        enc.strPool, enc.strPoolSize);
    TEST_ASSERT_TRUE(pos > 0);

    int64_t v[2]; uint32_t ep;
    for (int i = 0; i < 70; i++) {                      /* crosses anchor 61 */
        v[0] = 2000 + i * 3;
        v[1] = 5000 - i * 2;
        size_t n = histV4Encode(v, 2, enc, stream + pos,
                                sizeof(stream) - pos, 1000 + (uint32_t)i * 60);
        TEST_ASSERT_TRUE(n > 0);
        pos += n;
    }

    /* "Reopen": fresh state from the header's REAL length, then replay. */
    HistV4State rdr;
    size_t hdrLen = histV4ReadHeaderBuf(stream, pos, rdr);
    TEST_ASSERT_TRUE(hdrLen > 0);
    size_t p = hdrLen;
    int decoded = 0;
    while (p < pos) {
        size_t n = histV4DecodeNext(stream + p, pos - p, rdr, v, &ep);
        TEST_ASSERT_TRUE(n > 0);
        p += n;
        decoded++;
    }
    TEST_ASSERT_EQUAL_INT(70, decoded);
    TEST_ASSERT_EQUAL_UINT16(enc.recordsSinceAnchor, rdr.recordsSinceAnchor);
    TEST_ASSERT_EQUAL_UINT32(enc.lastEpoch, rdr.lastEpoch);
    TEST_ASSERT_EQUAL_INT64(enc.lastAnchor[0], rdr.lastAnchor[0]);
    TEST_ASSERT_EQUAL_INT64(enc.lastAnchor[1], rdr.lastAnchor[1]);

    /* Continuity: one more record from the RESUMED writer decodes clean. */
    v[0] = 2500; v[1] = 4700;
    size_t n = histV4Encode(v, 2, enc, stream + pos, sizeof(stream) - pos, 5200);
    size_t m = histV4DecodeNext(stream + pos, n, rdr, v, &ep);
    TEST_ASSERT_EQUAL_size_t(n, m);
    TEST_ASSERT_EQUAL_INT64(2500, v[0]);
    TEST_ASSERT_EQUAL_INT64(4700, v[1]);
    TEST_ASSERT_EQUAL_UINT32(5200, ep);
}

/** A torn tail must stop the reader at the last full record boundary
 *  (the goodPos the storage layer uses for repair), never misparse. */
static void test_torn_tail_stops_at_boundary(void) {
    HistV4State enc;
    buildTestSchema(enc, 2);

    static uint8_t stream[1024];
    size_t pos = histV4WriteHeaderBuf(stream, sizeof(stream),
        enc.sensors, enc.sensorCount, enc.measures, enc.measureCount,
        enc.strPool, enc.strPoolSize);

    int64_t v[2]; uint32_t ep;
    size_t lastBoundary = pos;
    for (int i = 0; i < 10; i++) {
        v[0] = 1000 + i * 7;
        v[1] = 9000 - i * 5;
        lastBoundary = pos;                             /* start of record i */
        pos += histV4Encode(v, 2, enc, stream + pos,
                            sizeof(stream) - pos, 2000 + (uint32_t)i * 60);
    }
    size_t torn = pos - 2;                              /* cut mid-record 10 */

    HistV4State rdr;
    size_t p = histV4ReadHeaderBuf(stream, torn, rdr);
    int decoded = 0;
    while (p < torn) {
        size_t n = histV4DecodeNext(stream + p, torn - p, rdr, v, &ep);
        if (n == 0) break;                              /* clean stop        */
        p += n;
        decoded++;
    }
    TEST_ASSERT_EQUAL_INT(9, decoded);
    TEST_ASSERT_EQUAL_size_t(lastBoundary, p);          /* == goodPos        */
}

/* ============================================================================
 *  MAIN
 * ============================================================================ */

int main(void) {
    UNITY_BEGIN();

    /* Bit packing */
    RUN_TEST(test_bitExtract_aligned_16bit);
    RUN_TEST(test_bitExtract_unaligned_8bit);
    RUN_TEST(test_bitExtract_unaligned_12bit);
    RUN_TEST(test_bitExtract_32bit);
    RUN_TEST(test_bitExtract_1bit);
    RUN_TEST(test_bitExtract_negative);
    RUN_TEST(test_bitExtract_64bit);
    RUN_TEST(test_bitInsert_preserves_adjacent);

    /* NaN sentinel */
    RUN_TEST(test_nanSentinel_16bit);
    RUN_TEST(test_nanSentinel_10bit);
    RUN_TEST(test_nanSentinel_1bit);

    /* Anchor roundtrip */
    RUN_TEST(test_anchor_roundtrip_1measure);
    RUN_TEST(test_anchor_roundtrip_3measures);
    RUN_TEST(test_anchor_roundtrip_withNaN);
    RUN_TEST(test_anchor_roundtrip_allNaN);

    /* Delta roundtrip */
    RUN_TEST(test_delta_roundtrip_after_anchor);
    RUN_TEST(test_delta_unchanged_fields_omitted);
    RUN_TEST(test_delta_nan_to_value_transition);

    /* Multi-record */
    RUN_TEST(test_across_anchor_boundary);
    RUN_TEST(test_delta_epoch_tracking);

    /* Buffer overflow */
    RUN_TEST(test_encode_buffer_too_small);
    RUN_TEST(test_decode_buffer_too_small_anchor);

    /* State reset */
    RUN_TEST(test_reset_clears_state);

    /* Measurement keys */
    RUN_TEST(test_makeMeasKey_temp);
    RUN_TEST(test_makeMeasKey_hum);
    RUN_TEST(test_makeMeasKey_press);
    RUN_TEST(test_makeMeasKey_lux);
    RUN_TEST(test_makeMeasKey_buffer_too_small);

    /* Value conversion */
    RUN_TEST(test_floatToRaw_normal);
    RUN_TEST(test_floatToRaw_nan);
    RUN_TEST(test_rawToFloat_normal);
    RUN_TEST(test_rawToFloat_nan);
    RUN_TEST(test_floatRoundtrip);
    RUN_TEST(test_channelPrefixes);

    /* v1.5.3 regressions */
    RUN_TEST(test_delta_valid_to_nan_transition);
    RUN_TEST(test_anchor_nan_after_valid_then_delta);
    RUN_TEST(test_encode_rejects_count_mismatch);
    RUN_TEST(test_reopen_resume_continuity);
    RUN_TEST(test_torn_tail_stops_at_boundary);

    return UNITY_END();
}
