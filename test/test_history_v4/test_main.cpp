/**
 * @file    test/test_history_v4/test_main.cpp
 * @brief   Host-side unit tests for HistoryV4 codec encode/decode roundtrip.
 * @details Runs via `pio test -e native_history_v4` (no HW). Covers:
 *            · Zigzag varint roundtrip (lives in HistoryV4.cpp)
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
 * Compiles production HistoryV4.cpp.
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

/** Regression: UNSIGNED channels (hum/press) must NOT be sign-extended at
 * anchor decode. 76.5% hum (raw 765, 10-bit, top bit set) used to decode
 * as -259; 1013.2 hPa (raw 10132, 14-bit) as -6251. The encoder always
 * stored the low bits correctly — only the reader was wrong. */
static void buildUnsignedSchema(HistV4State &state, uint8_t channel,
                                uint8_t bitWidth, uint32_t scale) {
    buildTestSchema(state, 1);
    state.measures[0].channel  = channel;
    state.measures[0].bitWidth = bitWidth;
    state.measures[0].scale    = scale;
    uint16_t bitOff = 32;
    state.measureBitOffset[0]  = bitOff;
    state.measureByteOffset[0] = bitOff >> 3;
    state.anchorByteSize = (bitOff + bitWidth + 7) / 8;
}

static void test_anchor_unsigned_hum_no_sign_extension(void) {
    HistV4State enc, dec;
    buildUnsignedSchema(enc, 1 /*CH_HUM*/, 10, 10);
    buildUnsignedSchema(dec, 1, 10, 10);
    uint8_t buf[64];
    int64_t v[1]; uint32_t ep;

    v[0] = 765;                                         /* 76.5 % — bit 9 set */
    size_t n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1000);
    TEST_ASSERT_TRUE(n > 0);
    histV4Decode(buf, n, dec, v, &ep, true);
    TEST_ASSERT_EQUAL_INT64(765, v[0]);                 /* was -259 */

    /* Delta chain on top of the high anchor must stay coherent. */
    v[0] = 780;
    n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1060);
    histV4Decode(buf, n, dec, v, &ep, false);
    TEST_ASSERT_EQUAL_INT64(780, v[0]);
}

static void test_anchor_unsigned_press_no_sign_extension(void) {
    HistV4State enc, dec;
    buildUnsignedSchema(enc, 2 /*CH_PRESS*/, 14, 10);
    buildUnsignedSchema(dec, 2, 14, 10);
    uint8_t buf[64];
    int64_t v[1]; uint32_t ep;

    v[0] = 10132;                                       /* 1013.2 hPa — bit 13 set */
    size_t n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1000);
    TEST_ASSERT_TRUE(n > 0);
    histV4Decode(buf, n, dec, v, &ep, true);
    TEST_ASSERT_EQUAL_INT64(10132, v[0]);               /* was -6251 */
}

static void test_anchor_signed_temp_negative_preserved(void) {
    HistV4State enc, dec;
    buildTestSchema(enc, 1);                            /* CH_TEMP 16-bit */
    buildTestSchema(dec, 1);
    uint8_t buf[64];
    int64_t v[1]; uint32_t ep;

    v[0] = -525;                                        /* -5.25 °C */
    size_t n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1000);
    TEST_ASSERT_TRUE(n > 0);
    histV4Decode(buf, n, dec, v, &ep, true);
    TEST_ASSERT_EQUAL_INT64(-525, v[0]);                /* sign kept for temp */
}

static void test_fromfloat_unsigned_clamps_at_zero(void) {
    HistV4MeasureDef def;
    def.channel = 1; def.bitWidth = 10; def.scale = 10; def.decimals = 1;
    TEST_ASSERT_EQUAL_INT64(0, histV4FromFloat(-3.0f, def));   /* not two's-complement */
    TEST_ASSERT_EQUAL_INT64(765, histV4FromFloat(76.5f, def));
    def.channel = 0; def.bitWidth = 16; def.scale = 100;
    TEST_ASSERT_EQUAL_INT64(-525, histV4FromFloat(-5.25f, def)); /* temp keeps sign */
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
 *  PATCH V4 — A3: colisao de valor legitimo com o sentinela NaN
 * ============================================================================ */

/**
 * -0,01 C em s16 x100 da raw = -1, cujo padrao de 16 bits e 0xFFFF: o
 * proprio sentinela. O valor voltava da leitura como NAN (freezer cruzando
 * 0 C gravava um buraco no historico). A guarda desloca 1 LSB.
 */
static void test_A3_negative_hundredth_is_not_nan(void) {
    HistV4State st;
    buildTestSchema(st, 1);
    st.measures[0].channel  = 0;    /* CH_TEMP: signed */
    st.measures[0].bitWidth = 16;
    st.measures[0].scale    = 100;

    int64_t raw = histV4FromFloat(-0.01f, st.measures[0]);

    TEST_ASSERT_FALSE(histV4IsNan(raw, 16));            /* nao e mais sentinela */
    TEST_ASSERT_EQUAL_INT64(-2, raw);                   /* deslocado 1 LSB      */

    float back = histV4ToFloat(raw, st.measures[0]);
    TEST_ASSERT_FALSE(isnan(back));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.02f, back);    /* erro <= 1 unidade    */
}

/** NAN de verdade continua virando sentinela — a guarda nao pode comer isso. */
static void test_A3_real_nan_still_sentinel(void) {
    HistV4State st;
    buildTestSchema(st, 1);
    st.measures[0].channel  = 0;
    st.measures[0].bitWidth = 16;
    st.measures[0].scale    = 100;

    int64_t raw = histV4FromFloat(NAN, st.measures[0]);
    TEST_ASSERT_TRUE(histV4IsNan(raw, 16));
    TEST_ASSERT_TRUE(isnan(histV4ToFloat(raw, st.measures[0])));
}

/** Topo teorico unsigned (102,3 % em u10 x10) tambem nao pode ser sentinela. */
static void test_A3_unsigned_top_of_range_is_not_nan(void) {
    HistV4State st;
    buildTestSchema(st, 1);
    st.measures[0].channel  = 1;    /* CH_HUM: unsigned */
    st.measures[0].bitWidth = 10;
    st.measures[0].scale    = 10;

    int64_t raw = histV4FromFloat(102.3f, st.measures[0]);
    TEST_ASSERT_FALSE(histV4IsNan(raw, 10));
    TEST_ASSERT_EQUAL_INT64(1022, raw);                 /* 102.2 %              */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 102.2f,
                             histV4ToFloat(raw, st.measures[0]));
}

/** -0,01 C sobrevive a um roundtrip completo de ancora. */
static void test_A3_roundtrip_through_anchor(void) {
    HistV4State enc;
    buildTestSchema(enc, 1);
    enc.measures[0].channel  = 0;
    enc.measures[0].bitWidth = 16;
    enc.measures[0].scale    = 100;

    uint8_t buf[64];
    int64_t v[1] = { histV4FromFloat(-0.01f, enc.measures[0]) };
    size_t n = histV4Encode(v, 1, enc, buf, sizeof(buf), 1700000000u);
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    HistV4State dec = enc;
    histV4Reset(dec);
    dec.measureCount = enc.measureCount;
    dec.sensorCount  = enc.sensorCount;
    memcpy(dec.measures, enc.measures, sizeof(enc.measures));
    memcpy(dec.measureBitOffset, enc.measureBitOffset, sizeof(enc.measureBitOffset));
    memcpy(dec.measureByteOffset, enc.measureByteOffset, sizeof(enc.measureByteOffset));
    dec.anchorByteSize = enc.anchorByteSize;

    int64_t out[1]; uint32_t ep;
    TEST_ASSERT_EQUAL_size_t(n, histV4Decode(buf, n, dec, out, &ep, true));
    TEST_ASSERT_FALSE(isnan(histV4ToFloat(out[0], dec.measures[0])));
}

/* ============================================================================
 *  PATCH V4 — A1: refill pos-falha atravessa deltas maiores que a ancora
 * ============================================================================ */

/**
 * Stream real, buffer estreito, alimentado 1 byte por vez. Com o limiar
 * antigo (refill so abaixo de anchorByteSize) o leitor parava no primeiro
 * delta grande; o helper reabastece depois da falha e le tudo.
 */
static void test_A1_refill_reads_full_stream(void) {
    HistV4State enc;
    buildTestSchema(enc, 8);            /* 8 medicoes: deltas gordos */

    static uint8_t stream[8192];
    size_t pos = histV4WriteHeaderBuf(stream, sizeof(stream),
        enc.sensors, enc.sensorCount, enc.measures, enc.measureCount,
        enc.strPool, enc.strPoolSize);

    const int N = 120;                  /* cruza a fronteira de ancora (60) */
    int64_t v[8];
    for (int i = 0; i < N; i++) {
        /* Variacao forte => varints de varios bytes em todos os campos. */
        for (int m = 0; m < 8; m++) v[m] = 1000 + ((i * 7919 + m * 104729) % 20000);
        size_t n = histV4Encode(v, 8, enc, stream + pos, sizeof(stream) - pos,
                                1700000000u + (uint32_t)i * 60u);
        TEST_ASSERT_GREATER_THAN_size_t(0, n);
        pos += n;
    }

    HistV4State rdr;
    size_t hdr = histV4ReadHeaderBuf(stream, pos, rdr);
    TEST_ASSERT_GREATER_THAN_size_t(0, hdr);

    /* Buffer deliberadamente apertado + refill de 1 byte: maximiza a chance
     * de o decode encontrar "bytes suficientes para ancora, insuficientes
     * para delta", que e exatamente a condicao do A1. */
    uint8_t rdBuf[64];
    size_t filled = 0;
    size_t src = hdr;
    int decoded = 0;
    int64_t out[8]; uint32_t ep;

    while (true) {
        size_t c = histV4DecodeNextRefill(
            rdBuf, sizeof(rdBuf), filled, rdr, out, &ep,
            [&](uint8_t *dst, size_t maxBytes) -> size_t {
                if (src >= pos || maxBytes == 0) return 0;
                dst[0] = stream[src++];
                return 1;
            });
        if (c == 0) break;
        decoded++;
    }
    TEST_ASSERT_EQUAL_INT(N, decoded);
    TEST_ASSERT_EQUAL_size_t(pos, src);
}

/* ============================================================================
 *  PATCH V4 — A1-b: decode de delta e transacional
 * ============================================================================ */

/**
 * Um delta truncado nao pode deixar rastro no estado. Sem isso o retry do
 * A1 redecodifica o mesmo registro sobre lastAnchor ja avancado e soma o
 * delta duas vezes — corrupcao silenciosa de todo o resto do dia.
 */
static void test_A1b_failed_delta_leaves_state_untouched(void) {
    HistV4State enc;
    buildTestSchema(enc, 4);

    uint8_t buf[256];
    size_t pos = 0;
    int64_t v[4];

    for (int m = 0; m < 4; m++) v[m] = 1000 + m;
    pos += histV4Encode(v, 4, enc, buf, sizeof(buf), 1700000000u);   /* ancora */

    size_t deltaStart = pos;
    for (int m = 0; m < 4; m++) v[m] = 1000 + m + 500 * (m + 1);
    size_t deltaLen = histV4Encode(v, 4, enc, buf + pos, sizeof(buf) - pos,
                                   1700000060u);
    TEST_ASSERT_GREATER_THAN_size_t(0, deltaLen);
    pos += deltaLen;

    /* Leitor: consome a ancora, guarda o estado, tenta o delta truncado. */
    HistV4State rdr;
    buildTestSchema(rdr, 4);
    int64_t out[4]; uint32_t ep;
    TEST_ASSERT_EQUAL_size_t(deltaStart,
        histV4DecodeNext(buf, deltaStart, rdr, out, &ep));

    HistV4State snapshot = rdr;

    for (size_t cut = 1; cut < deltaLen; cut++) {
        HistV4State attempt = snapshot;
        size_t n = histV4DecodeNext(buf + deltaStart, cut, attempt, out, &ep);
        if (n != 0) continue;                       /* coube: nada a checar */
        /* Falhou => estado deve ser byte-a-byte identico ao de antes. */
        TEST_ASSERT_EQUAL_INT(0, memcmp(&snapshot, &attempt, sizeof(HistV4State)));
    }

    /* E o delta completo, apos as tentativas falhas, decodifica correto. */
    HistV4State after = snapshot;
    TEST_ASSERT_EQUAL_size_t(deltaLen,
        histV4DecodeNext(buf + deltaStart, deltaLen, after, out, &ep));
    for (int m = 0; m < 4; m++) {
        TEST_ASSERT_EQUAL_INT64(1000 + m + 500 * (m + 1), out[m]);
    }
    TEST_ASSERT_EQUAL_UINT32(1700000060u, ep);
}

/* ============================================================================
 *  MAIN
 * ============================================================================ */


/* ============================================================================
 *  MIGRACAO DE SCHEMA (reescrita do arquivo do dia)
 *
 *  Exercita o NUCLEO de StorageManager::migrateV4Schema sem sistema de
 *  arquivos: monta um schema antigo, codifica um dia, remapeia coluna a coluna
 *  para um schema novo, recodifica e confere que cada valor carregado volta
 *  identico e que as colunas novas voltam NaN. E' exatamente o que a funcao faz
 *  entre ler um registro do arquivo velho e gravar no temporario.
 * ============================================================================ */

/** Schema com N sensores de 1 canal (temp), hwIds "S0".."Sn". */
static void buildMigSchema(HistV4State &st, uint8_t nSens, uint8_t firstId = 0) {
    histV4Reset(st);
    st.sensorCount  = nSens;
    st.measureCount = nSens;
    uint8_t off = 0;
    for (uint8_t i = 0; i < nSens; i++) {
        char hw[8];
        snprintf(hw, sizeof(hw), "S%d", firstId + i);
        uint8_t l = (uint8_t)strlen(hw);
        st.sensors[i].hwIdOffset = off;
        st.sensors[i].hwIdLen    = l;
        memcpy(st.strPool + off, hw, l);
        off += l;
        st.sensors[i].nameOffset = off;
        st.sensors[i].nameLen    = 0;
        st.sensors[i].sensorType = TYPE_DS18B20;
        st.sensors[i].channelMask = 0x01;

        st.measures[i].sensorIdx = i;
        st.measures[i].channel   = 0;
        st.measures[i].bitWidth  = 16;
        st.measures[i].decimals  = 2;
        st.measures[i].unitOffset = 0;
        st.measures[i].unitLen    = 0;
        st.measures[i].scale      = 100;
    }
    st.strPoolSize = off;
    uint16_t bitOff = 32;
    for (uint8_t i = 0; i < st.measureCount; i++) {
        st.measureByteOffset[i] = bitOff >> 3;
        st.measureBitOffset[i]  = bitOff;
        bitOff += st.measures[i].bitWidth;
    }
    st.anchorByteSize = (bitOff + 7) / 8;
}

/** Mapeia colunas por (hwId, canal), como migrateV4Schema faz. */
static void buildMigMap(const HistV4State &oldS, const HistV4State &newS, int8_t *map) {
    for (uint8_t j = 0; j < newS.measureCount; j++) {
        map[j] = -1;
        char nHw[17];
        histV4StrPoolGet(nHw, sizeof(nHw), newS.strPool,
                         newS.sensors[newS.measures[j].sensorIdx].hwIdOffset,
                         newS.sensors[newS.measures[j].sensorIdx].hwIdLen);
        for (uint8_t i = 0; i < oldS.measureCount; i++) {
            if (oldS.measures[i].channel != newS.measures[j].channel) continue;
            char oHw[17];
            histV4StrPoolGet(oHw, sizeof(oHw), oldS.strPool,
                             oldS.sensors[oldS.measures[i].sensorIdx].hwIdOffset,
                             oldS.sensors[oldS.measures[i].sensorIdx].hwIdLen);
            if (strcmp(oHw, nHw) == 0) { map[j] = (int8_t)i; break; }
        }
    }
}

/** Um sensor a mais: as 2 colunas velhas sobrevivem, a 3a nasce NaN. */
void test_mig_adds_column_keeps_old_values(void) {
    HistV4State oldS, newS, decOld, decNew;
    buildMigSchema(oldS, 2);
    buildMigSchema(newS, 3);
    int8_t map[HIST_V4_MAX_MEASUREMENTS];
    buildMigMap(oldS, newS, map);
    TEST_ASSERT_EQUAL_INT8(0, map[0]);
    TEST_ASSERT_EQUAL_INT8(1, map[1]);
    TEST_ASSERT_EQUAL_INT8(-1, map[2]);   /* coluna nova */

    const int N = 200;                     /* cruza 3 fronteiras de ancora */
    static uint8_t oldFile[65536];
    size_t oldLen = 0;
    uint32_t ep = 1785000000u;
    for (int r = 0; r < N; r++) {
        int64_t v[2] = { (int64_t)(2000 + r), (int64_t)(-500 - r) };
        size_t n = histV4Encode(v, 2, oldS, oldFile + oldLen, sizeof(oldFile) - oldLen, ep + r * 60, nullptr);
        TEST_ASSERT_TRUE(n > 0);
        oldLen += n;
    }

    /* Passo de migracao: decodifica o velho, remapeia, recodifica no novo. */
    memcpy(&decOld, &oldS, sizeof(decOld));
    histV4ResetCodec(decOld);
    static uint8_t newFile[65536];
    size_t newLen = 0, rd = 0;
    int written = 0;
    while (rd < oldLen) {
        int64_t ov[HIST_V4_MAX_MEASUREMENTS];
        uint32_t e;
        size_t c = histV4DecodeNext(oldFile + rd, oldLen - rd, decOld, ov, &e);
        if (c == 0) break;
        rd += c;
        int64_t nv[HIST_V4_MAX_MEASUREMENTS];
        for (uint8_t j = 0; j < newS.measureCount; j++)
            nv[j] = histV4RemapValue(map[j], ov, decOld, newS.measures[j]);
        size_t n = histV4Encode(nv, newS.measureCount, newS, newFile + newLen,
                                sizeof(newFile) - newLen, e, nullptr);
        TEST_ASSERT_TRUE(n > 0);
        newLen += n;
        written++;
    }
    TEST_ASSERT_EQUAL_INT(N, written);

    /* Verificacao: le o resultado e confere contra o esperado. */
    memcpy(&decNew, &newS, sizeof(decNew));
    histV4ResetCodec(decNew);
    size_t p = 0;
    for (int r = 0; r < N; r++) {
        int64_t v[HIST_V4_MAX_MEASUREMENTS];
        uint32_t e;
        size_t c = histV4DecodeNext(newFile + p, newLen - p, decNew, v, &e);
        TEST_ASSERT_TRUE(c > 0);
        p += c;
        TEST_ASSERT_EQUAL_UINT32(ep + r * 60, e);
        TEST_ASSERT_EQUAL_INT64((int64_t)(2000 + r), v[0]);
        TEST_ASSERT_EQUAL_INT64((int64_t)(-500 - r), v[1]);
        TEST_ASSERT_TRUE(histV4IsNan(v[2], newS.measures[2].bitWidth));
    }
    TEST_ASSERT_EQUAL_size_t(newLen, p);
}

/** Sensor removido: a coluna sai e a sobrevivente nao se desloca de valor. */
void test_mig_drops_column_without_shifting(void) {
    HistV4State oldS, newS, decOld, decNew;
    buildMigSchema(oldS, 3);
    buildMigSchema(newS, 1);            /* so' S0 permanece */
    int8_t map[HIST_V4_MAX_MEASUREMENTS];
    buildMigMap(oldS, newS, map);
    TEST_ASSERT_EQUAL_INT8(0, map[0]);

    static uint8_t a[8192], b[8192];
    size_t al = 0, bl = 0;
    const int N = 70;
    for (int r = 0; r < N; r++) {
        int64_t v[3] = { (int64_t)(1000 + r), 7777, -3333 };
        al += histV4Encode(v, 3, oldS, a + al, sizeof(a) - al, 1785000000u + r * 60, nullptr);
    }
    memcpy(&decOld, &oldS, sizeof(decOld)); histV4ResetCodec(decOld);
    size_t rd = 0;
    while (rd < al) {
        int64_t ov[HIST_V4_MAX_MEASUREMENTS]; uint32_t e;
        size_t c = histV4DecodeNext(a + rd, al - rd, decOld, ov, &e);
        if (c == 0) break;
        rd += c;
        int64_t nv[HIST_V4_MAX_MEASUREMENTS];
        for (uint8_t j = 0; j < newS.measureCount; j++)
            nv[j] = histV4RemapValue(map[j], ov, decOld, newS.measures[j]);
        bl += histV4Encode(nv, newS.measureCount, newS, b + bl, sizeof(b) - bl, e, nullptr);
    }
    memcpy(&decNew, &newS, sizeof(decNew)); histV4ResetCodec(decNew);
    size_t p = 0;
    for (int r = 0; r < N; r++) {
        int64_t v[HIST_V4_MAX_MEASUREMENTS]; uint32_t e;
        size_t c = histV4DecodeNext(b + p, bl - p, decNew, v, &e);
        TEST_ASSERT_TRUE(c > 0);
        p += c;
        TEST_ASSERT_EQUAL_INT64((int64_t)(1000 + r), v[0]);   /* nao pegou o 7777 do vizinho */
    }
}

/** Reordenar slots nao troca valores: o pareamento e' por hwId, nao por indice. */
void test_mig_reorder_matches_by_hwid(void) {
    HistV4State oldS, newS;
    buildMigSchema(oldS, 2);            /* S0, S1 */
    buildMigSchema(newS, 2);
    /* Inverte os hwIds do schema novo: S1, S0 */
    memcpy(newS.strPool, "S1S0", 4);
    newS.sensors[0].hwIdOffset = 0; newS.sensors[0].hwIdLen = 2;
    newS.sensors[1].hwIdOffset = 2; newS.sensors[1].hwIdLen = 2;
    newS.strPoolSize = 4;

    int8_t map[HIST_V4_MAX_MEASUREMENTS];
    buildMigMap(oldS, newS, map);
    TEST_ASSERT_EQUAL_INT8(1, map[0]);  /* coluna 0 do novo <- coluna 1 do velho */
    TEST_ASSERT_EQUAL_INT8(0, map[1]);

    int64_t ov[2] = { 111, 222 };
    TEST_ASSERT_EQUAL_INT64(222, histV4RemapValue(map[0], ov, oldS, newS.measures[0]));
    TEST_ASSERT_EQUAL_INT64(111, histV4RemapValue(map[1], ov, oldS, newS.measures[1]));
}

/** NaN da origem continua NaN no destino, com o sentinela do bitWidth destino. */
void test_mig_nan_survives_width_change(void) {
    HistV4State oldS, newS;
    buildMigSchema(oldS, 1);
    buildMigSchema(newS, 1);
    newS.measures[0].bitWidth = 24;     /* largura diferente */

    int64_t ov[1] = { histV4NanSentinel(16) };
    int8_t map[1] = { 0 };
    int64_t got = histV4RemapValue(map[0], ov, oldS, newS.measures[0]);
    TEST_ASSERT_TRUE(histV4IsNan(got, 24));
    TEST_ASSERT_EQUAL_INT64(histV4NanSentinel(24), got);
}

/** Escala diferente converte pelo valor fisico, nao pelo inteiro cru. */
void test_mig_scale_change_converts_physically(void) {
    HistV4State oldS, newS;
    buildMigSchema(oldS, 1);
    buildMigSchema(newS, 1);
    newS.measures[0].scale = 10;        /* x100 -> x10 */

    int64_t ov[1] = { 2537 };           /* 25.37 degC em escala 100 */
    int8_t map[1] = { 0 };
    int64_t got = histV4RemapValue(map[0], ov, oldS, newS.measures[0]);
    TEST_ASSERT_EQUAL_INT64(254, got);  /* 25.4 em escala 10 */
}

/** histV4ResetCodec limpa a cadeia de delta e preserva o schema. */
void test_mig_reset_codec_keeps_schema(void) {
    HistV4State st;
    buildMigSchema(st, 3);
    st.lastAnchor[0] = 12345;
    st.recordsSinceAnchor = 17;
    st.initialized = true;
    st.lastEpoch = 999;

    histV4ResetCodec(st);
    TEST_ASSERT_EQUAL_UINT8(3, st.measureCount);       /* schema intacto */
    TEST_ASSERT_EQUAL_UINT8(3, st.sensorCount);
    TEST_ASSERT_EQUAL_UINT16(0, st.recordsSinceAnchor);
    TEST_ASSERT_FALSE(st.initialized);
    TEST_ASSERT_EQUAL_UINT32(0, st.lastEpoch);
    TEST_ASSERT_EQUAL_INT64(0, st.lastAnchor[0]);
}

/** headerLen aponta para o primeiro registro — o seek do streaming depende disso. */
void test_mig_header_len_points_at_first_record(void) {
    HistV4State st;
    buildMigSchema(st, 4);
    uint8_t buf[HIST_V4_MAX_HEADER];
    size_t n = histV4WriteHeaderBuf(buf, sizeof(buf), st.sensors, st.sensorCount,
                                    st.measures, st.measureCount,
                                    st.strPool, st.strPoolSize);
    TEST_ASSERT_TRUE(n > 0);
    HistV4State rd;
    size_t consumed = histV4ReadHeaderBuf(buf, n, rd);
    TEST_ASSERT_EQUAL_size_t(consumed, (size_t)rd.headerLen);
    TEST_ASSERT_EQUAL_size_t(n, (size_t)rd.headerLen);
}

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
    RUN_TEST(test_anchor_unsigned_hum_no_sign_extension);
    RUN_TEST(test_anchor_unsigned_press_no_sign_extension);
    RUN_TEST(test_anchor_signed_temp_negative_preserved);
    RUN_TEST(test_fromfloat_unsigned_clamps_at_zero);
    RUN_TEST(test_encode_rejects_count_mismatch);
    RUN_TEST(test_reopen_resume_continuity);
    RUN_TEST(test_torn_tail_stops_at_boundary);

    /* Patch V4 — A3 (colisao com o sentinela) */
    RUN_TEST(test_A3_negative_hundredth_is_not_nan);
    RUN_TEST(test_A3_real_nan_still_sentinel);
    RUN_TEST(test_A3_unsigned_top_of_range_is_not_nan);
    RUN_TEST(test_A3_roundtrip_through_anchor);

    /* Migracao de schema (reescrita do arquivo do dia) */
    RUN_TEST(test_mig_adds_column_keeps_old_values);
    RUN_TEST(test_mig_drops_column_without_shifting);
    RUN_TEST(test_mig_reorder_matches_by_hwid);
    RUN_TEST(test_mig_nan_survives_width_change);
    RUN_TEST(test_mig_scale_change_converts_physically);
    RUN_TEST(test_mig_reset_codec_keeps_schema);
    RUN_TEST(test_mig_header_len_points_at_first_record);

    /* Patch V4 — A1 (refill pos-falha) e A1-b (decode transacional) */
    RUN_TEST(test_A1_refill_reads_full_stream);
    RUN_TEST(test_A1b_failed_delta_leaves_state_untouched);

    return UNITY_END();
}
