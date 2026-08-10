/**
 * @file    test/test_history_v5/test_main.cpp
 * @brief   Host-side unit tests for the HistoryV5 codec.
 * @details Runs via `pio test -e native_history_v5` (no HW). Covers the §12
 *          "unit" row in full:
 *            · CRC-16/CCITT-FALSE, including the 0x29B1 vector
 *            · BitWriter/BitReader roundtrip, byte boundaries, overflow,
 *              underflow, streaming through a drain window
 *            · zigzag over the whole int16 range
 *            · the exact width of every value prefix (±4 / ±32 / ±512)
 *            · NAN: entering, staying, leaving; envelope handling
 *            · time symbols: dod 0, ±7 bit, ±12 bit, resync
 *            · RAW selection and its roundtrip
 *            · tail accessors for nCh in {1, 3, 12, 16}
 *            · SCHEMA framing, equality, rejection of corrupt chunks
 *            · file scanning, seek, and mid-day schema change
 *            · a property sweep of pseudo-random series
 *
 * `tools/history_v5.py --emit-vectors` produces the cross-check corpus that
 * `tools/check_history_v5_parity.py` runs against this same code; this file
 * is the part that needs no Python.
 *
 * @project SIMUT — HistoryV5 codec unit testing
 * @license MIT License
 */

#include <unity.h>
#include "HistoryV5.h"
#include <cstring>
#include <cstdio>

/* ----- Required by native_stubs/Arduino.h linker symbol ----- */
namespace simut_native {
    uint32_t fake_millis_value = 0;
}

/* ============================================================================
 *  HELPERS
 * ============================================================================ */

static H5ChannelDesc g_schema[H5_MAX_CHANNELS];

static void buildSchema(uint8_t n, uint8_t kind = H5_KIND_GENERIC, int8_t exp = 0) {
    for (uint8_t i = 0; i < n; i++) {
        g_schema[i].id = i;
        g_schema[i].kind = kind;
        g_schema[i].scaleExp = exp;
        g_schema[i].flags = 0;
    }
}

/** Collects a sealStream() into a flat buffer, so both seal paths are tested. */
struct Collector {
    uint8_t buf[H5_BLOCK_MAX_BYTES + 256];
    size_t len;
};

static bool collect(void* ctx, const uint8_t* data, size_t len) {
    Collector* c = (Collector*)ctx;
    if (c->len + len > sizeof(c->buf)) return false;
    memcpy(c->buf + c->len, data, len);
    c->len += len;
    return true;
}

/** Decode a whole chunk into caller arrays. @return records produced. */
static uint8_t decodeAll(const uint8_t* chunk, size_t len, uint8_t n,
                         uint32_t* epochs, int16_t (*vals)[H5_MAX_CHANNELS],
                         uint16_t nominal = 60) {
    HistoryV5Decoder dec;
    if (!dec.begin(chunk, len, g_schema, n, nominal)) return 0;
    uint8_t i = 0;
    while (dec.next(epochs[i], vals[i])) i++;
    return i;
}

/* ============================================================================
 *  CRC
 * ============================================================================ */

void test_crc_vector(void) {
    TEST_ASSERT_EQUAL_HEX16(0x29B1, h5Crc16((const uint8_t*)"123456789", 9));
}

void test_crc_empty_is_init(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, h5Crc16((const uint8_t*)"", 0));
}

void test_crc_is_resumable(void) {
    const uint8_t* s = (const uint8_t*)"123456789";
    uint16_t crc = h5Crc16(s, 4);
    crc = h5Crc16(s + 4, 5, crc);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, crc);
}

/* ============================================================================
 *  ZIGZAG
 * ============================================================================ */

void test_zigzag_roundtrip_full_range(void) {
    for (int32_t d = -32768; d <= 32767; d++) {
        TEST_ASSERT_EQUAL_INT16((int16_t)d, h5Unzigzag(h5Zigzag((int16_t)d)));
    }
}

void test_zigzag_ordering(void) {
    TEST_ASSERT_EQUAL_UINT16(0, h5Zigzag(0));
    TEST_ASSERT_EQUAL_UINT16(1, h5Zigzag(-1));
    TEST_ASSERT_EQUAL_UINT16(2, h5Zigzag(1));
    TEST_ASSERT_EQUAL_UINT16(3, h5Zigzag(-2));
    TEST_ASSERT_EQUAL_UINT16(7, h5Zigzag(-4));   /* widest 3-bit code */
    TEST_ASSERT_EQUAL_UINT16(6, h5Zigzag(3));
    TEST_ASSERT_EQUAL_UINT16(63, h5Zigzag(-32));
    TEST_ASSERT_EQUAL_UINT16(1023, h5Zigzag(-512));
}

/* ============================================================================
 *  BIT I/O
 * ============================================================================ */

void test_bitwriter_msb_first(void) {
    uint8_t buf[8];
    BitWriter bw;
    bw.begin(buf, sizeof(buf));
    bw.put(1, 1);
    bw.put(0x3, 3);          /* 011 */
    bw.put(0xABCD, 16);
    bw.put(0x3, 2);
    const size_t n = bw.flush( );
    TEST_ASSERT_FALSE(bw.overflow( ));
    TEST_ASSERT_EQUAL_UINT32(3, n);          /* 22 bits -> 3 bytes */
    TEST_ASSERT_EQUAL_HEX8(0xBA, buf[0]);    /* 1 011 1010 */

    BitReader br;
    br.begin(buf, n);
    TEST_ASSERT_EQUAL_UINT32(1, br.get(1));
    TEST_ASSERT_EQUAL_UINT32(0x3, br.get(3));
    TEST_ASSERT_EQUAL_UINT32(0xABCD, br.get(16));
    TEST_ASSERT_EQUAL_UINT32(0x3, br.get(2));
    TEST_ASSERT_FALSE(br.underflow( ));
}

void test_bitwriter_overflow_is_sticky_and_safe(void) {
    uint8_t buf[2];
    uint8_t guard = 0xA5;
    BitWriter bw;
    bw.begin(buf, sizeof(buf));
    for (int i = 0; i < 40; i++) bw.put(0xFF, 8);
    bw.flush( );
    TEST_ASSERT_TRUE(bw.overflow( ));
    TEST_ASSERT_EQUAL_HEX8(0xA5, guard);     /* nothing wrote past the buffer */
}

void test_bitreader_underflow_is_sticky(void) {
    const uint8_t buf[1] = { 0xFF };
    BitReader br;
    br.begin(buf, 1);
    br.get(8);
    TEST_ASSERT_FALSE(br.underflow( ));
    br.get(1);
    TEST_ASSERT_TRUE(br.underflow( ));
    TEST_ASSERT_TRUE(br.eof( ));
}

void test_bitwriter_drains_through_a_window(void) {
    /* A 4-byte window must produce the same bytes as a buffer big enough
     * to hold everything — that equivalence is what lets sealStream() emit
     * a 2 KiB payload without a 2 KiB buffer. */
    uint8_t big[64];
    BitWriter direct;
    direct.begin(big, sizeof(big));
    for (int i = 0; i < 30; i++) direct.put((uint32_t)(i * 7), 7);
    const size_t nDirect = direct.flush( );

    Collector c; c.len = 0;
    uint8_t window[4];
    BitWriter streamed;
    streamed.begin(window, sizeof(window), collect, &c);
    for (int i = 0; i < 30; i++) streamed.put((uint32_t)(i * 7), 7);
    const size_t nStreamed = streamed.flush( );

    TEST_ASSERT_EQUAL_UINT32(nDirect, nStreamed);
    TEST_ASSERT_EQUAL_UINT32(nDirect, c.len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(big, c.buf, nDirect));
    TEST_ASSERT_EQUAL_HEX16(direct.crc( ), streamed.crc( ));
}

/* ============================================================================
 *  VALUE SYMBOL WIDTHS
 * ============================================================================ */

/** @return payload bytes for a 2-record, 1-channel block with delta @p d. */
static size_t payloadBytesForDelta(int32_t d) {
    buildSchema(1);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    int16_t v0 = 0, v1 = (int16_t)d;
    enc.reset(1000, &v0);
    enc.add(1060, &v1);
    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t n = enc.seal(out, sizeof(out), 0);
    TEST_ASSERT_TRUE(n > 0);
    return ((const H5DataHeader*)out)->payloadLen;
}

void test_value_prefix_widths(void) {
    /* 1 bit of time symbol (dod = 0) plus the value symbol. */
    TEST_ASSERT_EQUAL_UINT32(1, payloadBytesForDelta(0));       /* 1 + 1  */
    TEST_ASSERT_EQUAL_UINT32(1, payloadBytesForDelta(3));       /* 1 + 5  */
    TEST_ASSERT_EQUAL_UINT32(1, payloadBytesForDelta(-4));      /* 1 + 5  */
    TEST_ASSERT_EQUAL_UINT32(2, payloadBytesForDelta(4));       /* 1 + 9  */
    TEST_ASSERT_EQUAL_UINT32(2, payloadBytesForDelta(31));      /* 1 + 9  */
    TEST_ASSERT_EQUAL_UINT32(2, payloadBytesForDelta(-32));     /* 1 + 9  */
    TEST_ASSERT_EQUAL_UINT32(2, payloadBytesForDelta(32));      /* 1 + 14 */
    TEST_ASSERT_EQUAL_UINT32(2, payloadBytesForDelta(511));     /* 1 + 14 */
    TEST_ASSERT_EQUAL_UINT32(2, payloadBytesForDelta(-512));    /* 1 + 14 */
    TEST_ASSERT_EQUAL_UINT32(3, payloadBytesForDelta(512));     /* 1 + 20 */
    TEST_ASSERT_EQUAL_UINT32(3, payloadBytesForDelta(-513));    /* 1 + 20 */
}

void test_every_delta_class_roundtrips(void) {
    buildSchema(1);
    const int32_t deltas[] = { 0, 1, -1, 3, -4, 4, -32, 31, 32, -512, 511,
                               512, -513, 20000, -20000 };
    const uint8_t n = sizeof(deltas) / sizeof(deltas[0]);

    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    int16_t v = 0;
    int16_t expect[64];
    expect[0] = 0;
    enc.reset(1000, &v);
    for (uint8_t i = 0; i < n; i++) {
        int32_t nv = (int32_t)v + deltas[i];
        if (nv > 32767) nv = 32767;
        if (nv < -32767) nv = -32767;
        v = (int16_t)nv;
        expect[i + 1] = v;
        TEST_ASSERT_TRUE(enc.add(1000 + 60 * (i + 1), &v));
    }
    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), 0);

    uint32_t epochs[64];
    int16_t vals[64][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(n + 1, decodeAll(out, len, 1, epochs, vals));
    for (uint8_t i = 0; i <= n; i++) {
        TEST_ASSERT_EQUAL_INT16(expect[i], vals[i][0]);
        TEST_ASSERT_EQUAL_UINT32(1000u + 60u * i, epochs[i]);
    }
}

/* ============================================================================
 *  NAN
 * ============================================================================ */

void test_nan_enter_stay_leave(void) {
    buildSchema(1, H5_KIND_TEMP_C, -2);
    const int16_t seq[] = { 2350, 2351, H5_NAN_SENTINEL, H5_NAN_SENTINEL,
                            H5_NAN_SENTINEL, 2360, 2361, H5_NAN_SENTINEL, 2400 };
    const uint8_t n = sizeof(seq) / sizeof(seq[0]);

    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    enc.reset(1000, &seq[0]);
    for (uint8_t i = 1; i < n; i++) enc.add(1000 + 60 * i, &seq[i]);
    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), 0);

    uint32_t epochs[16];
    int16_t vals[16][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(n, decodeAll(out, len, 1, epochs, vals));
    for (uint8_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_INT16(seq[i], vals[i][0]);

    /* The envelope reports the band of real readings, not the sentinel. */
    TEST_ASSERT_EQUAL_INT16(2350, h5ChMin(out, 1)[0]);
    TEST_ASSERT_EQUAL_INT16(2400, h5ChMax(out, 1)[0]);
}

void test_all_nan_channel_costs_one_bit(void) {
    buildSchema(1);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    const int16_t nan = H5_NAN_SENTINEL;
    enc.reset(1000, &nan);
    for (uint8_t i = 1; i < 10; i++) enc.add(1000 + 60 * i, &nan);
    uint8_t out[H5_BLOCK_MAX_BYTES];
    enc.seal(out, sizeof(out), 0);

    /* 9 records x (1 time bit + 1 value bit) = 18 bits = 3 bytes. */
    TEST_ASSERT_EQUAL_UINT32(3, ((const H5DataHeader*)out)->payloadLen);
    TEST_ASSERT_EQUAL_INT16(H5_NAN_SENTINEL, h5ChMin(out, 1)[0]);
    TEST_ASSERT_EQUAL_INT16(H5_NAN_SENTINEL, h5ChMax(out, 1)[0]);
}

/* ============================================================================
 *  TIME SYMBOLS
 * ============================================================================ */

void test_time_symbols_roundtrip(void) {
    buildSchema(1);
    const uint32_t t0 = 1700000000u;
    /*                dod 0   dod 0    dod +3   dod -3   dod +97  resync   dod 0 */
    const uint32_t times[] = { t0 + 60, t0 + 120, t0 + 183, t0 + 243,
                               t0 + 400, t0 + 40000, t0 + 40060 };
    const uint8_t n = sizeof(times) / sizeof(times[0]);

    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    int16_t v = 0;
    enc.reset(t0, &v);
    for (uint8_t i = 0; i < n; i++) TEST_ASSERT_TRUE(enc.add(times[i], &v));
    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), 0);

    uint32_t epochs[16];
    int16_t vals[16][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(n + 1, decodeAll(out, len, 1, epochs, vals));
    TEST_ASSERT_EQUAL_UINT32(t0, epochs[0]);
    for (uint8_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_UINT32(times[i], epochs[i + 1]);
}

void test_resync_restores_nominal_delta(void) {
    buildSchema(1);
    const uint32_t t0 = 1700000000u;
    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    int16_t v = 0;
    enc.reset(t0, &v);
    enc.add(t0 + 60, &v);           /* dod 0            -> 1 bit  */
    enc.add(t0 + 40000, &v);        /* resync           -> 35 bits */
    enc.add(t0 + 40060, &v);        /* nominal again    -> 1 bit  */
    uint8_t out[H5_BLOCK_MAX_BYTES];
    enc.seal(out, sizeof(out), 0);
    /* (1+1) + (35+1) + (1+1) = 40 bits = 5 bytes. */
    TEST_ASSERT_EQUAL_UINT32(5, ((const H5DataHeader*)out)->payloadLen);
}

/* ============================================================================
 *  RAW FALLBACK
 * ============================================================================ */

static uint32_t prng(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;      /* xorshift32 */
    return s;
}

void test_incompressible_block_falls_back_to_raw(void) {
    buildSchema(12);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 12, 60);
    uint32_t s = 0xC0FFEE;
    int16_t row[H5_MAX_CHANNELS];
    int16_t expect[H5_BLOCK_MAX_RECORDS][H5_MAX_CHANNELS];

    for (uint8_t c = 0; c < 12; c++) row[c] = (int16_t)(prng(s) % 60000) - 30000;
    memcpy(expect[0], row, sizeof(row));
    enc.reset(1000, row);
    for (uint8_t i = 1; i < H5_BLOCK_MAX_RECORDS; i++) {
        for (uint8_t c = 0; c < 12; c++) row[c] = (int16_t)(prng(s) % 60000) - 30000;
        memcpy(expect[i], row, sizeof(row));
        TEST_ASSERT_TRUE(enc.add(1000 + 60 * i, row));
    }
    TEST_ASSERT_FALSE(enc.add(1000 + 60 * H5_BLOCK_MAX_RECORDS, row));  /* full */

    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), 0);
    const H5DataHeader* h = (const H5DataHeader*)out;
    TEST_ASSERT_TRUE(h->pre.flags & H5_FLAG_RAW);
    TEST_ASSERT_EQUAL_UINT32(59u * H5_RAW_RECORD_SIZE(12), h->payloadLen);
    TEST_ASSERT_TRUE(len <= H5_BLOCK_MAX_BYTES);

    uint32_t epochs[H5_BLOCK_MAX_RECORDS];
    int16_t vals[H5_BLOCK_MAX_RECORDS][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(H5_BLOCK_MAX_RECORDS,
                            decodeAll(out, len, 12, epochs, vals));
    for (uint8_t i = 0; i < H5_BLOCK_MAX_RECORDS; i++) {
        TEST_ASSERT_EQUAL_INT(0, memcmp(expect[i], vals[i], 12 * sizeof(int16_t)));
    }
}

void test_compressible_block_stays_compressed(void) {
    buildSchema(12);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 12, 60);
    int16_t row[H5_MAX_CHANNELS];
    for (uint8_t c = 0; c < 12; c++) row[c] = (int16_t)(2000 + c);
    enc.reset(1000, row);
    for (uint8_t i = 1; i < H5_BLOCK_MAX_RECORDS; i++) enc.add(1000 + 60 * i, row);
    uint8_t out[H5_BLOCK_MAX_BYTES];
    enc.seal(out, sizeof(out), 0);
    const H5DataHeader* h = (const H5DataHeader*)out;
    TEST_ASSERT_FALSE(h->pre.flags & H5_FLAG_RAW);
    /* 59 records x 13 bits (1 time + 12 values, all unchanged) = 96 bytes. */
    TEST_ASSERT_EQUAL_UINT32(96, h->payloadLen);
}

void test_add_refuses_epoch_out_of_raw_reach(void) {
    /* RAW addresses records as a u16 offset from t0. A record it could not
     * reach would leave an incompressible block with no bounded form at all,
     * so the block closes instead — which is what keeps seal() from ever
     * needing more than H5_BLOCK_MAX_BYTES (§14-1, R3). */
    buildSchema(1);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    const uint32_t t0 = 1700000000u;
    int16_t v = 0;
    enc.reset(t0, &v);
    TEST_ASSERT_TRUE(enc.add(t0 + 65535u, &v));      /* exactly at the edge */
    TEST_ASSERT_FALSE(enc.add(t0 + 65536u, &v));     /* one past it         */
    TEST_ASSERT_FALSE(enc.add(t0 - 1, &v));          /* before t0           */
    TEST_ASSERT_EQUAL_UINT8(2, enc.count( ));
}

void test_sample_reads_back_the_open_block(void) {
    /* sample( ) is how telemetry reaches the hour still open in RAM. It must
     * hand back exactly what add( ) took — keyframe included, NaN sentinel
     * included — because whatever it returns goes straight to the server. */
    buildSchema(3);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 3, 60);

    uint32_t epoch = 0;
    int16_t out[H5_MAX_CHANNELS];
    TEST_ASSERT_FALSE(enc.sample(0, epoch, out));        /* empty encoder */

    const uint32_t t0 = 1700000000u;
    int16_t rows[4][3] = { { 2350, 610, H5_NAN_SENTINEL },
                           { 2351, 612, 9871 },
                           { H5_NAN_SENTINEL, 615, 9872 },
                           { 2348, 620, 9870 } };
    enc.reset(t0, rows[0]);
    for (uint8_t i = 1; i < 4; i++) enc.add(t0 + i * 60u, rows[i]);

    for (uint8_t i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(enc.sample(i, epoch, out));
        TEST_ASSERT_EQUAL_UINT32(t0 + i * 60u, epoch);
        TEST_ASSERT_EQUAL_INT16_ARRAY(rows[i], out, 3);
    }
    TEST_ASSERT_FALSE(enc.sample(4, epoch, out));        /* one past count  */
    TEST_ASSERT_FALSE(enc.sample(0, epoch, nullptr));    /* no destination  */

    /* After seal + reset the old records are gone; only the new keyframe
     * answers — the RAM walk must never see a stale block. */
    uint8_t buf[H5_BLOCK_MAX_BYTES];
    TEST_ASSERT_TRUE(enc.seal(buf, sizeof(buf), 0) > 0);
    int16_t fresh[3] = { 100, 200, 300 };
    enc.reset(t0 + 3600u, fresh);
    TEST_ASSERT_TRUE(enc.sample(0, epoch, out));
    TEST_ASSERT_EQUAL_UINT32(t0 + 3600u, epoch);
    TEST_ASSERT_EQUAL_INT16_ARRAY(fresh, out, 3);
    TEST_ASSERT_FALSE(enc.sample(1, epoch, out));
}

void test_worst_case_block_fits_the_bound(void) {
    /* The size guarantee, exercised at its worst point: the widest schema,
     * a full block and values that defeat every delta prefix. */
    buildSchema(H5_MAX_CHANNELS);
    HistoryV5Encoder enc;
    enc.begin(g_schema, H5_MAX_CHANNELS, 60);
    uint32_t s = 0xBADF00D;
    int16_t row[H5_MAX_CHANNELS];
    for (uint8_t c = 0; c < H5_MAX_CHANNELS; c++) row[c] = (int16_t)(prng(s) % 60000) - 30000;
    enc.reset(1700000000u, row);
    for (uint8_t i = 1; i < H5_BLOCK_MAX_RECORDS; i++) {
        for (uint8_t c = 0; c < H5_MAX_CHANNELS; c++) {
            row[c] = (int16_t)(prng(s) % 60000) - 30000;
        }
        TEST_ASSERT_TRUE(enc.add(1700000000u + i * 60u, row));
    }
    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), 0);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_UINT32(H5_BLOCK_MAX_BYTES, len);   /* the bound, exactly */
    TEST_ASSERT_TRUE(((const H5DataHeader*)out)->pre.flags & H5_FLAG_RAW);
}

/* ============================================================================
 *  FRAMING, TAILS AND SCHEMA
 * ============================================================================ */

void test_tail_accessors_for_each_width(void) {
    const uint8_t widths[] = { 1, 3, 12, 16 };
    for (uint8_t w = 0; w < 4; w++) {
        const uint8_t n = widths[w];
        buildSchema(n, H5_KIND_TEMP_C, -2);
        HistoryV5Encoder enc;
        enc.begin(g_schema, n, 60);
        int16_t a[H5_MAX_CHANNELS], b[H5_MAX_CHANNELS];
        for (uint8_t c = 0; c < n; c++) { a[c] = (int16_t)(100 + c); b[c] = (int16_t)(90 + c); }
        enc.reset(1000, a);
        enc.add(1060, b);
        uint8_t out[H5_BLOCK_MAX_BYTES];
        const size_t len = enc.seal(out, sizeof(out), 0);

        TEST_ASSERT_EQUAL_UINT32(16u + 6u * n, H5_DATA_HEADER_SIZE(n));
        TEST_ASSERT_TRUE(len >= H5_DATA_HEADER_SIZE(n));
        for (uint8_t c = 0; c < n; c++) {
            TEST_ASSERT_EQUAL_INT16((int16_t)(100 + c), h5Keyframe(out, n)[c]);
            TEST_ASSERT_EQUAL_INT16((int16_t)(90 + c),  h5ChMin(out, n)[c]);
            TEST_ASSERT_EQUAL_INT16((int16_t)(100 + c), h5ChMax(out, n)[c]);
        }
        /* Decoding proves the CRC over the whole frame agrees. */
        uint32_t epochs[4];
        int16_t vals[4][H5_MAX_CHANNELS];
        TEST_ASSERT_EQUAL_UINT8(2, decodeAll(out, len, n, epochs, vals));
    }
}

void test_schema_chunk_framing(void) {
    for (uint8_t n = 1; n <= H5_MAX_CHANNELS; n++) {
        buildSchema(n, H5_KIND_PRESS_HPA, -1);
        uint8_t buf[H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS)];
        const size_t sz = h5BuildSchemaChunk(buf, sizeof(buf), g_schema, n, 3);
        TEST_ASSERT_EQUAL_UINT32(H5_SCHEMA_CHUNK_SIZE(n), sz);
        const H5ChunkPreamble* p = (const H5ChunkPreamble*)buf;
        TEST_ASSERT_EQUAL_HEX16(H5_MAGIC, p->magic);
        TEST_ASSERT_EQUAL_UINT8(H5_VERSION, p->version);
        TEST_ASSERT_EQUAL_UINT8(H5_CHUNK_SCHEMA, p->type);
        TEST_ASSERT_EQUAL_UINT8(n, p->a);
        TEST_ASSERT_EQUAL_UINT8(3, p->b);
        TEST_ASSERT_EQUAL_HEX8(0xFF, p->rsv);
        uint16_t stored;
        memcpy(&stored, buf + 8 + 4 * n, 2);
        TEST_ASSERT_EQUAL_HEX16(h5Crc16(buf, 8 + 4 * n), stored);
    }
}

void test_schema_equality(void) {
    H5ChannelDesc a[2] = { {0, H5_KIND_TEMP_C, -2, 0}, {1, H5_KIND_HUM_PCT, -1, 0} };
    H5ChannelDesc b[2] = { {0, H5_KIND_TEMP_C, -2, 0}, {1, H5_KIND_HUM_PCT, -1, 0} };
    TEST_ASSERT_TRUE(h5SchemaEquals(a, 2, b, 2));
    b[1].scaleExp = -2;
    TEST_ASSERT_FALSE(h5SchemaEquals(a, 2, b, 2));
    TEST_ASSERT_FALSE(h5SchemaEquals(a, 2, a, 1));
}

void test_count_one_block_is_legal(void) {
    buildSchema(3, H5_KIND_TEMP_C, -2);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 3, 60);
    const int16_t v[3] = { 1234, -5, H5_NAN_SENTINEL };
    enc.reset(1000, v);
    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), H5_FLAG_PARTIAL);
    const H5DataHeader* h = (const H5DataHeader*)out;
    TEST_ASSERT_EQUAL_UINT32(0, h->payloadLen);
    TEST_ASSERT_EQUAL_UINT8(1, h->pre.a);
    TEST_ASSERT_TRUE(h->pre.flags & H5_FLAG_PARTIAL);
    TEST_ASSERT_EQUAL_UINT32(H5_DATA_HEADER_SIZE(3), len);

    uint32_t epochs[2];
    int16_t vals[2][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(1, decodeAll(out, len, 3, epochs, vals));
    TEST_ASSERT_EQUAL_INT16(1234, vals[0][0]);
    TEST_ASSERT_EQUAL_INT16(H5_NAN_SENTINEL, vals[0][2]);
}

/* ============================================================================
 *  REJECTION
 * ============================================================================ */

void test_decoder_rejects_corruption(void) {
    buildSchema(2, H5_KIND_TEMP_C, -2);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 2, 60);
    const int16_t a[2] = { 10, 20 }, b[2] = { 11, 21 };
    enc.reset(1000, a);
    enc.add(1060, b);
    uint8_t good[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(good, sizeof(good), 0);

    HistoryV5Decoder dec;
    TEST_ASSERT_TRUE(dec.begin(good, len, g_schema, 2));

    uint8_t bad[H5_BLOCK_MAX_BYTES];

    memcpy(bad, good, len); bad[0] ^= 0xFF;              /* magic */
    TEST_ASSERT_FALSE(dec.begin(bad, len, g_schema, 2));

    memcpy(bad, good, len); bad[2] = 0x01;               /* version */
    TEST_ASSERT_FALSE(dec.begin(bad, len, g_schema, 2));

    memcpy(bad, good, len); bad[3] = H5_CHUNK_SCHEMA;    /* wrong type */
    TEST_ASSERT_FALSE(dec.begin(bad, len, g_schema, 2));

    memcpy(bad, good, len); bad[18] ^= 0x01;             /* a tail byte */
    TEST_ASSERT_FALSE(dec.begin(bad, len, g_schema, 2));

    memcpy(bad, good, len);                              /* nCh disagrees */
    TEST_ASSERT_FALSE(dec.begin(bad, len, g_schema, 3));

    memcpy(bad, good, len); bad[5] = H5_BLOCK_MAX_RECORDS + 1;
    TEST_ASSERT_FALSE(dec.begin(bad, len, g_schema, 2));

    TEST_ASSERT_FALSE(dec.begin(good, len - 1, g_schema, 2));   /* truncated */
}

/* ============================================================================
 *  FILE SCANNER
 * ============================================================================ */

struct MemFile {
    uint8_t buf[16384];
    uint32_t len;
};

static MemFile g_file;

static int memRead(void* ctx, uint32_t off, uint8_t* dst, size_t len) {
    MemFile* f = (MemFile*)ctx;
    if (off + len > f->len) return -1;
    memcpy(dst, f->buf + off, len);
    return (int)len;
}

static void fileAppend(const uint8_t* d, size_t n) {
    memcpy(g_file.buf + g_file.len, d, n);
    g_file.len += (uint32_t)n;
}

static bool fileSink(void* ctx, const uint8_t* d, size_t n) {
    (void)ctx;
    if (g_file.len + n > sizeof(g_file.buf)) return false;
    fileAppend(d, n);
    return true;
}

/** Build a day: SCHEMA, then `blocks` sealed blocks of 60 records each. */
static void buildFile(uint8_t n, uint8_t blocks, uint32_t t0) {
    g_file.len = 0;
    buildSchema(n, H5_KIND_TEMP_C, -2);
    uint8_t sc[H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS)];
    fileAppend(sc, h5BuildSchemaChunk(sc, sizeof(sc), g_schema, n, 0));

    HistoryV5Encoder enc;
    enc.begin(g_schema, n, 60);
    for (uint8_t b = 0; b < blocks; b++) {
        int16_t row[H5_MAX_CHANNELS];
        for (uint8_t c = 0; c < n; c++) row[c] = (int16_t)(2000 + b * 10 + c);
        enc.reset(t0 + b * 3600u, row);
        for (uint8_t i = 1; i < H5_BLOCK_MAX_RECORDS; i++) {
            for (uint8_t c = 0; c < n; c++) row[c] = (int16_t)(2000 + b * 10 + c + i);
            enc.add(t0 + b * 3600u + i * 60u, row);
        }
        enc.sealStream(fileSink, nullptr, 0);
    }
}

void test_scan_walks_every_chunk(void) {
    buildFile(4, 5, 1700000000u);
    HistoryV5Scan sc;
    sc.begin(memRead, &g_file, g_file.len);

    H5ChannelDesc got[H5_MAX_CHANNELS];
    uint8_t n = 0, seq = 0;
    TEST_ASSERT_TRUE(sc.nextSchema(got, n, seq));
    TEST_ASSERT_EQUAL_UINT8(4, n);
    TEST_ASSERT_EQUAL_UINT8(0, seq);
    TEST_ASSERT_TRUE(h5SchemaEquals(got, n, g_schema, 4));

    uint8_t blocks = 0;
    H5DataHeader hdr;
    int16_t kf[H5_MAX_CHANNELS], mn[H5_MAX_CHANNELS], mx[H5_MAX_CHANNELS];
    while (sc.nextData(hdr, kf, mn, mx)) {
        TEST_ASSERT_EQUAL_UINT8(H5_BLOCK_MAX_RECORDS, hdr.pre.a);
        TEST_ASSERT_EQUAL_UINT8(4, hdr.pre.b);
        TEST_ASSERT_TRUE(mn[0] <= mx[0]);
        blocks++;
    }
    TEST_ASSERT_EQUAL_UINT8(5, blocks);
    TEST_ASSERT_EQUAL_UINT16(0, sc.rejected( ));
}

void test_scan_skips_a_corrupt_block_and_continues(void) {
    buildFile(4, 5, 1700000000u);
    /* Break the second DATA chunk: find it, flip a payload byte. */
    HistoryV5Scan probe;
    probe.begin(memRead, &g_file, g_file.len);
    H5DataHeader hdr;
    TEST_ASSERT_TRUE(probe.nextData(hdr, nullptr, nullptr, nullptr));
    TEST_ASSERT_TRUE(probe.nextData(hdr, nullptr, nullptr, nullptr));
    g_file.buf[probe.chunkOffset( ) + H5_DATA_HEADER_SIZE(4) + 2] ^= 0xFF;

    HistoryV5Scan sc;
    sc.begin(memRead, &g_file, g_file.len);
    uint8_t blocks = 0;
    while (sc.nextData(hdr, nullptr, nullptr, nullptr)) blocks++;
    TEST_ASSERT_EQUAL_UINT8(4, blocks);
    TEST_ASSERT_EQUAL_UINT16(1, sc.rejected( ));
}

void test_scan_seek_lands_on_the_right_block(void) {
    buildFile(4, 6, 1700000000u);
    HistoryV5Scan sc;
    sc.begin(memRead, &g_file, g_file.len);
    /* 10 minutes into the 4th hour. */
    TEST_ASSERT_TRUE(sc.seek(1700000000u + 3 * 3600u + 600u));
    H5DataHeader hdr;
    TEST_ASSERT_TRUE(sc.nextData(hdr, nullptr, nullptr, nullptr));
    TEST_ASSERT_EQUAL_UINT32(1700000000u + 3 * 3600u, hdr.t0);

    TEST_ASSERT_TRUE(sc.seek(1700000000u));
    TEST_ASSERT_TRUE(sc.nextData(hdr, nullptr, nullptr, nullptr));
    TEST_ASSERT_EQUAL_UINT32(1700000000u, hdr.t0);

    /* A target before the first block is a miss, but the scanner must be left
     * at the START, not at EOF: a range query whose cutoff precedes the file
     * wants the whole file. Parking at EOF dropped it without a word. */
    TEST_ASSERT_FALSE(sc.seek(1699999999u));
    uint8_t after = 0;
    while (sc.nextData(hdr, nullptr, nullptr, nullptr)) after++;
    TEST_ASSERT_EQUAL_UINT8(6, after);
}

void test_scan_reads_a_chunk_back_for_the_decoder(void) {
    buildFile(4, 3, 1700000000u);
    HistoryV5Scan sc;
    sc.begin(memRead, &g_file, g_file.len);
    H5DataHeader hdr;
    TEST_ASSERT_TRUE(sc.nextData(hdr, nullptr, nullptr, nullptr));

    uint8_t chunk[H5_BLOCK_MAX_BYTES];
    size_t len = 0;
    TEST_ASSERT_TRUE(sc.readChunk(chunk, sizeof(chunk), len));
    uint32_t epochs[H5_BLOCK_MAX_RECORDS];
    int16_t vals[H5_BLOCK_MAX_RECORDS][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(H5_BLOCK_MAX_RECORDS,
                            decodeAll(chunk, len, 4, epochs, vals));
    TEST_ASSERT_EQUAL_UINT32(1700000000u, epochs[0]);
    TEST_ASSERT_EQUAL_INT16(2000, vals[0][0]);
}

void test_mid_day_schema_change(void) {
    /* §3.7-2: seal PARTIAL, write SCHEMA seq+1, keep going in the same file. */
    g_file.len = 0;
    uint8_t sc1[H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS)];
    buildSchema(2, H5_KIND_TEMP_C, -2);
    H5ChannelDesc first[2];
    memcpy(first, g_schema, sizeof(first));
    fileAppend(sc1, h5BuildSchemaChunk(sc1, sizeof(sc1), first, 2, 0));

    HistoryV5Encoder enc;
    enc.begin(first, 2, 60);
    int16_t a[2] = { 2000, 2100 };
    enc.reset(1000, a);
    enc.add(1060, a);
    enc.sealStream(fileSink, nullptr, H5_FLAG_PARTIAL);

    buildSchema(3, H5_KIND_HUM_PCT, -1);
    fileAppend(sc1, h5BuildSchemaChunk(sc1, sizeof(sc1), g_schema, 3, 1));
    enc.begin(g_schema, 3, 60);
    int16_t b[3] = { 700, 710, 720 };
    enc.reset(1120, b);
    enc.add(1180, b);
    enc.sealStream(fileSink, nullptr, H5_FLAG_PARTIAL);

    HistoryV5Scan sc;
    sc.begin(memRead, &g_file, g_file.len);
    uint8_t schemas = 0, datas = 0;
    uint8_t lastNch = 0;
    for (;;) {
        const H5ScanChunk t = sc.next( );
        if (t == H5_SCAN_END) break;
        if (t == H5_SCAN_SCHEMA) { schemas++; lastNch = sc.nCh( ); }
        else {
            datas++;
            /* Every DATA agrees with the SCHEMA that precedes it (§3.7-3). */
            TEST_ASSERT_EQUAL_UINT8(lastNch, sc.header( ).pre.b);
            TEST_ASSERT_TRUE(sc.header( ).pre.flags & H5_FLAG_PARTIAL);
        }
    }
    TEST_ASSERT_EQUAL_UINT8(2, schemas);
    TEST_ASSERT_EQUAL_UINT8(2, datas);
    TEST_ASSERT_EQUAL_UINT16(0, sc.rejected( ));
}

/* ============================================================================
 *  SEAL PATH EQUIVALENCE AND TIME SHIFT
 * ============================================================================ */

void test_seal_and_sealstream_agree(void) {
    /* sealStream() computes the CRC of a chunk it never holds; if that ever
     * diverges from seal(), files written to flash would fail their own
     * validation on read. */
    for (uint8_t n = 1; n <= H5_MAX_CHANNELS; n += 5) {
        buildSchema(n, H5_KIND_TEMP_C, -2);
        uint32_t s = 99 + n;
        HistoryV5Encoder enc;
        enc.begin(g_schema, n, 60);
        int16_t row[H5_MAX_CHANNELS];
        for (uint8_t c = 0; c < n; c++) row[c] = (int16_t)(prng(s) % 1000);
        enc.reset(1700000000u, row);
        for (uint8_t i = 1; i < 45; i++) {
            for (uint8_t c = 0; c < n; c++) {
                row[c] = (int16_t)(row[c] + (int16_t)(prng(s) % 9) - 4);
            }
            enc.add(1700000000u + i * 60u, row);
        }
        uint8_t direct[H5_BLOCK_MAX_BYTES];
        const size_t a = enc.seal(direct, sizeof(direct), H5_FLAG_PARTIAL);

        Collector c; c.len = 0;
        const size_t b = enc.sealStream(collect, &c, H5_FLAG_PARTIAL);
        TEST_ASSERT_EQUAL_UINT32(a, b);
        TEST_ASSERT_EQUAL_UINT32(a, c.len);
        TEST_ASSERT_EQUAL_INT(0, memcmp(direct, c.buf, a));
    }
}

void test_seal_refuses_a_short_buffer(void) {
    buildSchema(4, H5_KIND_TEMP_C, -2);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 4, 60);
    int16_t row[4] = { 1, 2, 3, 4 };
    enc.reset(1000, row);
    for (uint8_t i = 1; i < 30; i++) { row[0] = (int16_t)(row[0] + 700); enc.add(1000 + i * 60u, row); }
    uint8_t tiny[H5_DATA_HEADER_SIZE(4)];
    TEST_ASSERT_EQUAL_UINT32(0, enc.seal(tiny, sizeof(tiny), 0));
}

void test_shift_time_moves_the_whole_block(void) {
    buildSchema(1);
    HistoryV5Encoder enc;
    enc.begin(g_schema, 1, 60);
    int16_t v = 5;
    enc.reset(1000, &v);
    enc.add(1060, &v);
    enc.add(1120, &v);
    enc.shiftTime(1700000000);
    TEST_ASSERT_EQUAL_UINT32(1700001000u, enc.t0( ));
    TEST_ASSERT_EQUAL_UINT32(1700001120u, enc.lastEpoch( ));

    uint8_t out[H5_BLOCK_MAX_BYTES];
    const size_t len = enc.seal(out, sizeof(out), 0);
    uint32_t epochs[4];
    int16_t vals[4][H5_MAX_CHANNELS];
    TEST_ASSERT_EQUAL_UINT8(3, decodeAll(out, len, 1, epochs, vals));
    TEST_ASSERT_EQUAL_UINT32(1700001000u, epochs[0]);
    TEST_ASSERT_EQUAL_UINT32(1700001060u, epochs[1]);
    TEST_ASSERT_EQUAL_UINT32(1700001120u, epochs[2]);
}

/* ============================================================================
 *  PROPERTY SWEEP
 * ============================================================================ */

void test_property_random_series_roundtrip(void) {
    uint32_t s = 20260731;
    int16_t expect[H5_BLOCK_MAX_RECORDS][H5_MAX_CHANNELS];
    uint32_t expectT[H5_BLOCK_MAX_RECORDS];
    uint8_t out[H5_BLOCK_MAX_BYTES];
    uint32_t epochs[H5_BLOCK_MAX_RECORDS];
    int16_t vals[H5_BLOCK_MAX_RECORDS][H5_MAX_CHANNELS];

    for (int trial = 0; trial < 4000; trial++) {
        const uint8_t n = (uint8_t)(1 + prng(s) % H5_MAX_CHANNELS);
        const uint8_t count = (uint8_t)(1 + prng(s) % H5_BLOCK_MAX_RECORDS);
        const uint8_t mode = (uint8_t)(prng(s) % 5);
        const uint16_t nominal = (uint16_t)((prng(s) % 3) == 0 ? 30 : 60);
        buildSchema(n, H5_KIND_TEMP_C, -2);

        HistoryV5Encoder enc;
        enc.begin(g_schema, n, nominal);
        int16_t row[H5_MAX_CHANNELS];
        for (uint8_t c = 0; c < n; c++) row[c] = (int16_t)((prng(s) % 65535) - 32767);
        uint32_t t = 1600000000u + prng(s) % 100000000u;
        enc.reset(t, row);
        memcpy(expect[0], row, sizeof(row));
        expectT[0] = t;

        for (uint8_t i = 1; i < count; i++) {
            for (uint8_t c = 0; c < n; c++) {
                int32_t v;
                switch (mode) {
                    case 0: v = row[c] + (int32_t)(prng(s) % 7) - 3; break;   /* ramp  */
                    case 1: v = row[c] + ((prng(s) % 8) ? 0 : 900); break;    /* step  */
                    case 2: v = (int32_t)(prng(s) % 65535) - 32767; break;    /* noise */
                    case 3: v = row[c]; break;                               /* flat  */
                    default:                                                 /* NAN   */
                        if (prng(s) % 3 == 0) { row[c] = H5_NAN_SENTINEL; continue; }
                        v = (row[c] == H5_NAN_SENTINEL)
                          ? (int32_t)(prng(s) % 65535) - 32767
                          : row[c] + (int32_t)(prng(s) % 81) - 40;
                        break;
                }
                if (v > 32767) v = 32767;
                if (v < -32767) v = -32767;
                row[c] = (int16_t)v;
            }
            const uint32_t steps[] = { 60, 60, 60, 59, 61, 120, 3600, 100000 };
            t += steps[prng(s) % 8];
            if (!enc.add(t, row)) break;   /* out of RAW's reach: block closes */
            memcpy(expect[i], row, sizeof(row));
            expectT[i] = t;
        }

        const uint8_t sealed = enc.count( );
        const size_t len = enc.seal(out, sizeof(out), 0);
        TEST_ASSERT_TRUE_MESSAGE(len > 0, "seal returned 0");
        TEST_ASSERT_TRUE_MESSAGE(len <= H5_BLOCK_MAX_BYTES, "chunk over the bound");
        const uint8_t got = decodeAll(out, len, n, epochs, vals, nominal);
        TEST_ASSERT_EQUAL_UINT8(sealed, got);
        for (uint8_t i = 0; i < sealed; i++) {
            TEST_ASSERT_EQUAL_UINT32(expectT[i], epochs[i]);
            TEST_ASSERT_EQUAL_INT(0, memcmp(expect[i], vals[i], n * sizeof(int16_t)));
        }
    }
}

/* ============================================================================
 *  RUNNER
 * ============================================================================ */

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN( );

    RUN_TEST(test_crc_vector);
    RUN_TEST(test_crc_empty_is_init);
    RUN_TEST(test_crc_is_resumable);

    RUN_TEST(test_zigzag_roundtrip_full_range);
    RUN_TEST(test_zigzag_ordering);

    RUN_TEST(test_bitwriter_msb_first);
    RUN_TEST(test_bitwriter_overflow_is_sticky_and_safe);
    RUN_TEST(test_bitreader_underflow_is_sticky);
    RUN_TEST(test_bitwriter_drains_through_a_window);

    RUN_TEST(test_value_prefix_widths);
    RUN_TEST(test_every_delta_class_roundtrips);

    RUN_TEST(test_nan_enter_stay_leave);
    RUN_TEST(test_all_nan_channel_costs_one_bit);

    RUN_TEST(test_time_symbols_roundtrip);
    RUN_TEST(test_resync_restores_nominal_delta);

    RUN_TEST(test_incompressible_block_falls_back_to_raw);
    RUN_TEST(test_compressible_block_stays_compressed);
    RUN_TEST(test_add_refuses_epoch_out_of_raw_reach);
    RUN_TEST(test_sample_reads_back_the_open_block);
    RUN_TEST(test_worst_case_block_fits_the_bound);

    RUN_TEST(test_tail_accessors_for_each_width);
    RUN_TEST(test_schema_chunk_framing);
    RUN_TEST(test_schema_equality);
    RUN_TEST(test_count_one_block_is_legal);

    RUN_TEST(test_decoder_rejects_corruption);

    RUN_TEST(test_scan_walks_every_chunk);
    RUN_TEST(test_scan_skips_a_corrupt_block_and_continues);
    RUN_TEST(test_scan_seek_lands_on_the_right_block);
    RUN_TEST(test_scan_reads_a_chunk_back_for_the_decoder);
    RUN_TEST(test_mid_day_schema_change);

    RUN_TEST(test_seal_and_sealstream_agree);
    RUN_TEST(test_seal_refuses_a_short_buffer);
    RUN_TEST(test_shift_time_moves_the_whole_block);

    RUN_TEST(test_property_random_series_roundtrip);

    return UNITY_END( );
}
