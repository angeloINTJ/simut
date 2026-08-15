/**
 * @file HistoryV5.h
 * @brief V5 history format — generic, self-describing, delta-compressed time series.
 *
 * The format is specified in `docs/HistoryV5_Instrucoes_Implementacao.md`.
 * That document is the source of truth: a disagreement between it and this
 * file is a bug in this file. `tools/history_v5.py` is the reference
 * implementation and the oracle the native tests compare against.
 *
 * What the format buys over V4 (.sim4):
 *  - the hot path never touches flash. Samples land in RAM and reach the
 *    device once an hour, so the Core-1 lockout windows that flash
 *    program/erase forces drop from ~1440/day to ~24 + 144 snapshots.
 *  - every block carries its own CRC, keyframe and per-channel min/max
 *    envelope, so a graph over weeks reads block headers instead of
 *    decoding payloads, and one corrupt block costs one hour, not a day.
 *  - the channel set, physical quantity and scale live in a SCHEMA chunk,
 *    so any reader that implements the document decodes a file without
 *    knowing the firmware that wrote it.
 *
 * Golden rule: changing ANY layout below requires bumping H5_VERSION. The
 * static_asserts turn a layout slip into a build error.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ===========================================================================
 *  FORMAT CONSTANTS (§3, §4)
 * ======================================================================== */

#define H5_MAGIC                0x4835              /* "H5" little-endian    */
#define H5_VERSION              0x02                /* on-disk version       */
#define HISTORY_FILE_EXT        ".h5"               /* /history/YYYYMMDD.h5  */

#define H5_CHUNK_SCHEMA         0x01
#define H5_CHUNK_DATA           0x02

#define H5_FLAG_RAW             0x01                /* DATA payload is RAW   */
#define H5_FLAG_PARTIAL         0x02                /* block sealed early    */

#define H5_MAX_CHANNELS         16                  /* compile-time ceiling  */
#define H5_BLOCK_MAX_RECORDS    60                  /* 1 rec/min -> 1 h      */
#define H5_NAN_SENTINEL         ((int16_t)0x8000)

/* Derived sizes (n = channels in the schema in force). */
#define H5_SCHEMA_CHUNK_SIZE(n) (8u + 4u * (n) + 2u)
#define H5_DATA_HEADER_SIZE(n)  (16u + 6u * (n))
#define H5_RAW_RECORD_SIZE(n)   (2u + 2u * (n))
#define H5_BLOCK_MAX_BYTES      (H5_DATA_HEADER_SIZE(H5_MAX_CHANNELS) +      \
                                 (H5_BLOCK_MAX_RECORDS - 1)                  \
                                 * H5_RAW_RECORD_SIZE(H5_MAX_CHANNELS))
                                                    /* = 2118 B              */

/* Policy, not format. The block still open in RAM is snapshotted to
 * /history/.wip once per record, inline in writeHistoryEntryV5, so R8's bound
 * is ONE record rather than a clock interval — the ten-minute timer this
 * replaced meant a power cut, and every reboot for configuration, discarded up
 * to ten measurements. Blocks themselves still close by COUNT, not by clock
 * (§14-5): 60 records at the default one-per-minute interval is the hour.
 *
 * The cost of the tighter bound is 1 440 .wip rewrites a day against 144 —
 * ten times the Core 1 lockout windows. Endurance is not the constraint
 * (~2,6k erases per block per year against 100k rated); the lockout duty
 * cycle is, which is why the write still yields to touch and heavy tasks.
 *
 * How soon a snapshot that yielded gets retried. Only ever reached when the
 * inline write was refused, so it costs nothing in the common case. */
#define H5_WIP_RETRY_MS         (2u * 1000u)

/* How many records in a row may be refused while a seal keeps failing before
 * the held block is written off and recording resumes on a fresh one.
 *
 * Both directions of this are a loss, which is why there is a bound and not a
 * choice: discarding the block on the first failure throws away up to 60
 * records for what is usually a transient FLASH_OP mutex timeout, and holding
 * it forever means a device that silently stops recording for good. Five
 * records is the interval's worth of patience — enough for a long web read to
 * release the filesystem, bounded well under the block it is protecting. */
#define H5_SEAL_MAX_FAILS       5u

/* ===========================================================================
 *  PROVISIONAL-CLOCK SEED PLAUSIBILITY
 * ======================================================================== */

/** Nominal sampling interval in seconds, clamped to what a u16 can carry.
 *  The interval is configurable up to 24 h; the encoder only uses this to
 *  predict the next timestamp, so a clamp costs one wider time symbol per
 *  record rather than any loss. */
static inline uint16_t h5NominalSeconds(uint16_t intervalMin) {
    const uint32_t s = (uint32_t)intervalMin * 60u;
    return (s > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)s;
}

/**
 * @brief Latest epoch a snapshot belonging to day D may legitimately carry.
 *
 * A block belonging to file D starts inside D, and blocks close by COUNT
 * (§14-5), so the newest record a .wip can hold is one block span past its
 * own t0 — at worst one span past midnight, in the window between the day
 * rollover and the seal that closes the block. Clamped to a day so this is
 * never looser than the flat +24 h it replaces.
 *
 * The bound matters because this value seeds the provisional clock at boot
 * (StorageManager::getLastRecordedTimestamp). On 2026-08-14 a seed 4 h 43 min
 * past the day it belonged to started the clock in the future, and every
 * record written before NTP arrived inherited the error — 121 measurements
 * filed under the following day, with the graph showing a hole where the
 * device had in fact been recording normally. The old ceiling of dayEnd+24 h
 * admitted it; at the one-minute interval this one leaves an hour.
 *
 * @param dayEnd   Midnight ending the day the file is named for (epoch).
 * @param nominalS Nominal sampling interval in seconds; 0 reads as 60.
 */
static inline uint32_t h5SeedCeiling(uint32_t dayEnd, uint16_t nominalS) {
    uint32_t span = (uint32_t)H5_BLOCK_MAX_RECORDS * (nominalS ? nominalS : 60u);
    if (span > 86400u) span = 86400u;
    return dayEnd + span;
}

/**
 * @brief True when @p epoch is plausible enough to seed the provisional clock.
 *
 * A zero window means the filename could not be parsed into a day; there is
 * nothing to judge against, so the caller's other guards stand alone.
 */
static inline bool h5SeedPlausible(uint32_t epoch, uint32_t dayStart,
                                   uint32_t dayEnd, uint16_t nominalS) {
    if (dayStart == 0 || dayEnd == 0) return true;
    return epoch >= dayStart && epoch < h5SeedCeiling(dayEnd, nominalS);
}

/** Physical quantities. Adding values is free; renumbering is forbidden. */
enum H5Kind : uint8_t {
    H5_KIND_TEMP_C    = 0x01,   /* °C   — typical scaleExp -2 (x100)        */
    H5_KIND_HUM_PCT   = 0x02,   /* % RH — typical -1 on this firmware       */
    H5_KIND_PRESS_HPA = 0x03,   /* hPa  — typical -1 (x10)                  */
    H5_KIND_CO2_PPM   = 0x04,   /* ppm  — typical 0                         */
    H5_KIND_VOC_IDX   = 0x05,   /* dimensionless index — typical 0          */
    H5_KIND_GENERIC   = 0x7E,   /* UI shows raw x 10^scaleExp, no unit      */
    /* 0x80..0xFF reserved for non-linear families (e.g. log-lux)           */
};

/** Channel descriptor — lives in the SCHEMA chunk, 4 B per channel. */
struct __attribute__((packed)) H5ChannelDesc {
    uint8_t id;                 /* stable channel identity on this device   */
    uint8_t kind;               /* H5Kind                                   */
    int8_t  scaleExp;           /* real = raw x 10^scaleExp                 */
    uint8_t flags;              /* reserved (0)                             */
};
static_assert(sizeof(H5ChannelDesc) == 4, "channel descriptor layout broken");

/** Preamble common to every chunk (§3.1). */
struct __attribute__((packed)) H5ChunkPreamble {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  flags;
    uint8_t  a;                 /* SCHEMA: nCh   · DATA: count              */
    uint8_t  b;                 /* SCHEMA: seq   · DATA: nCh                */
    uint8_t  rsv;               /* 0xFF                                     */
};
static_assert(sizeof(H5ChunkPreamble) == 8, "chunk preamble layout broken");

/** Fixed part of the DATA header (§3.3); tails via the accessors below. */
struct __attribute__((packed)) H5DataHeader {
    H5ChunkPreamble pre;
    uint32_t t0;                /* epoch UTC (s) of the block's 1st record  */
    uint16_t payloadLen;        /* bitstream bytes, after the tails         */
    uint16_t crc16;             /* §3.4 — covers everything but this field  */
};
static_assert(sizeof(H5DataHeader) == 16, "DATA fixed header layout broken");

/* Tail accessors — the ONLY place that knows the variable layout. Every
 * access to keyframe/min/max/payload goes through these.                  */
static inline int16_t* h5Keyframe(uint8_t* d, uint8_t n)
                       { (void)n; return (int16_t*)(d + 16); }
static inline int16_t* h5ChMin  (uint8_t* d, uint8_t n)
                       { return (int16_t*)(d + 16 + 2u * n); }
static inline int16_t* h5ChMax  (uint8_t* d, uint8_t n)
                       { return (int16_t*)(d + 16 + 4u * n); }
static inline uint8_t* h5Payload(uint8_t* d, uint8_t n)
                       { return d + H5_DATA_HEADER_SIZE(n); }

/* ===========================================================================
 *  CRC-16/CCITT-FALSE (§3.4)
 *  Poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000.
 *  Required test vector: h5Crc16("123456789", 9) == 0x29B1.
 * ======================================================================== */

/**
 * @brief Running CRC over @p len bytes, seeded with @p crc.
 * @details Nibble-table variant: two lookups per byte against a 32 B table,
 *          instead of eight shifts or a 512 B table. Seeding lets a caller
 *          CRC a chunk it never holds whole in RAM.
 */
uint16_t h5Crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);

/* ===========================================================================
 *  ZIGZAG (§3.5)
 * ======================================================================== */

/** int16 delta -> unsigned, small magnitudes first. */
static inline uint16_t h5Zigzag(int16_t d) {
    return (uint16_t)(((uint16_t)d << 1) ^ (uint16_t)(d >> 15));
}

/** Inverse of h5Zigzag. */
static inline int16_t h5Unzigzag(uint16_t z) {
    return (int16_t)((uint16_t)(z >> 1) ^ (uint16_t)(-(int16_t)(z & 1)));
}

/* ===========================================================================
 *  BIT I/O — MSB first (§3.5)
 * ======================================================================== */

/**
 * @brief Sink for streamed output: returns false to abort the write.
 * @details Lets an encoder produce a chunk larger than any buffer it owns —
 *          the flash path streams straight into a File, the tests collect
 *          into an array, and neither needs a copy of the other's storage.
 */
typedef bool (*H5WriteFn)(void* ctx, const uint8_t* data, size_t len);

/**
 * @brief MSB-first bit writer over a caller-provided buffer.
 *
 * Bit 7 of each byte is written first; a multi-bit field is emitted from its
 * most significant bit down. Overflow sets a sticky flag and writes nothing
 * past the buffer — the writer never corrupts memory it was not given.
 *
 * With a drain function the buffer becomes a window: it is handed to the sink
 * and reused whenever it fills, so a 64 B window can emit a 2 KiB payload.
 */
class BitWriter {
public:
    void begin(uint8_t* buf, size_t cap, H5WriteFn drain = nullptr, void* ctx = nullptr);

    /** Emit the low @p width bits of @p value, most significant first. */
    void put(uint32_t value, uint8_t width);
    /** Emit a literal prefix such as "1110". */
    void putPrefix(const char* bits);

    /** Bits emitted so far, including any still in the partial byte. */
    size_t bitCount( ) const { return _bits; }
    /** Bytes a flush() would produce. */
    size_t byteCount( ) const { return (_bits + 7) / 8; }
    /** CRC over every byte completed so far (seed it via begin's caller). */
    uint16_t crc( ) const { return _crc; }
    void seedCrc(uint16_t c) { _crc = c; }

    /** Zero-pad the final byte and drain what is left. @return total bytes. */
    size_t flush( );

    bool overflow( ) const { return _overflow; }

private:
    void emitByte(uint8_t b);

    uint8_t*  _buf = nullptr;
    size_t    _cap = 0;
    size_t    _len = 0;        /* bytes currently in the window             */
    size_t    _bits = 0;       /* total bits emitted                        */
    uint8_t   _acc = 0;
    uint8_t   _nacc = 0;       /* bits in the partial byte                  */
    uint16_t  _crc = 0xFFFF;
    H5WriteFn _drain = nullptr;
    void*     _ctx = nullptr;
    bool      _overflow = false;
};

/** MSB-first bit reader. Reading past the end sets a sticky underflow flag. */
class BitReader {
public:
    void begin(const uint8_t* buf, size_t len);
    uint32_t get(uint8_t width);
    bool eof( ) const { return _pos >= _len * 8; }
    bool underflow( ) const { return _underflow; }

private:
    const uint8_t* _buf = nullptr;
    size_t _len = 0;
    size_t _pos = 0;
    bool   _underflow = false;
};

/* ===========================================================================
 *  ENCODER (§5)
 * ======================================================================== */

/**
 * @brief Compresses one DATA block in RAM for a caller-supplied schema.
 *
 * add() is the hot path and does no compression: it stores the raw sample
 * and returns. All the work happens in seal(), once an hour, well inside the
 * 30 ms budget. Keeping the samples rather than an incremental bitstream is
 * what makes the RAW fallback (§3.6, R3) possible at all — a block that turns
 * out to be incompressible can still be emitted verbatim, which an encoder
 * that had thrown the samples away could not do.
 *
 * No heap, no FPU. Static footprint is the sample store: 59 x (4 + 2*16) B.
 */
class HistoryV5Encoder {
public:
    /** Bind a schema. Safe to call again; drops any block in progress. */
    void begin(const H5ChannelDesc* schema, uint8_t nCh, uint16_t nominalIntervalS);

    /** Start a new block whose first record is (@p epoch, @p v[nCh]). */
    void reset(uint32_t epoch, const int16_t* v);

    /** Append a record. @return false when the block is full (seal it). */
    bool add(uint32_t epoch, const int16_t* v);

    /**
     * @brief Assemble the chunk into @p out.
     * @param extraFlags H5_FLAG_PARTIAL when the block did not fill.
     * @return total chunk bytes, or 0 on error (including @p cap too small).
     */
    size_t seal(uint8_t* out, size_t cap, uint8_t extraFlags);

    /**
     * @brief Assemble the chunk straight into @p sink, holding only a window.
     * @details Three passes over the samples: size the payload, CRC it, emit
     *          it. The header carries the CRC of what follows it, so it can
     *          only be written once the payload is known — and re-running a
     *          few hundred symbol emissions is far cheaper than the 2 KiB
     *          buffer that holding the payload would cost.
     * @return total chunk bytes written, or 0 on error.
     */
    size_t sealStream(H5WriteFn sink, void* ctx, uint8_t extraFlags);

    uint8_t  count( ) const { return _count; }
    bool     empty( ) const { return _count == 0; }
    bool     full ( ) const { return _count >= H5_BLOCK_MAX_RECORDS; }
    uint32_t t0   ( ) const { return _t0; }
    uint8_t  nCh  ( ) const { return _nCh; }
    /** Epoch of the newest record held, or 0 when the block is empty. */
    uint32_t lastEpoch( ) const;

    /**
     * @brief Read back record @p i (0-based) still held in RAM.
     *
     * The samples are kept plain, not bit-packed — packing only happens in
     * seal( ) — so a reader costs a copy and no decode. Telemetry uses this to
     * reach the hour in progress: a block only lands in the day file once it
     * fills, which at one record a minute means the newest hour is invisible
     * to anything that reads .h5 files, and telemetry ran an hour behind.
     *
     * @param i     0 .. count( )-1.
     * @param epoch Receives the record's timestamp.
     * @param out   Receives nCh( ) values; must hold H5_MAX_CHANNELS.
     * @return false if @p i is past the end.
     */
    bool sample(uint8_t i, uint32_t& epoch, int16_t* out) const {
        if (i >= _count || !out) return false;
        /* Record 0 is the keyframe in _kf/_t0; the rest live in the arrays. */
        const int16_t* src = (i == 0) ? _kf : _v[i - 1];
        epoch = (i == 0) ? _t0 : _epoch[i - 1];
        for (uint8_t c = 0; c < _nCh; c++) out[c] = src[c];
        return true;
    }

    /**
     * @brief Shift every timestamp held in RAM by @p deltaS (§7.3).
     * @details The retroactive clock fix rewrites t0 in the DATA headers on
     *          flash; the block still open in RAM has to move with them or it
     *          would land in the file with the old clock.
     */
    void shiftTime(int32_t deltaS);

private:
    /* One pass over the samples. `bw` may be null to only measure. */
    size_t emitCompressed(BitWriter* bw) const;
    size_t emitRaw(H5WriteFn sink, void* ctx, uint16_t* crc) const;
    size_t rawSize( ) const;
    bool   rawUsable( ) const;
    void   envelope(int16_t* mn, int16_t* mx) const;
    size_t buildFixedAndTails(uint8_t* out, uint8_t flags,
                              uint16_t payloadLen, uint16_t crc) const;

    const H5ChannelDesc* _schema = nullptr;
    uint8_t  _nCh = 0;
    uint16_t _nominal = 60;
    uint32_t _t0 = 0;
    uint8_t  _count = 0;

    /* Record 1 lives in _kf/_t0; records 2..count live in the arrays. */
    int16_t  _kf[H5_MAX_CHANNELS];
    uint32_t _epoch[H5_BLOCK_MAX_RECORDS - 1];
    int16_t  _v[H5_BLOCK_MAX_RECORDS - 1][H5_MAX_CHANNELS];
};

/* ===========================================================================
 *  DECODER (§5)
 * ======================================================================== */

/**
 * @brief Iterates the records of one DATA chunk that is already in RAM.
 *
 * begin() validates magic, version, type, CRC and that the chunk's nCh
 * matches the schema in force (§3.7-3). A chunk that fails any of those is
 * rejected whole — the payload is never read (§3.7-4).
 */
class HistoryV5Decoder {
public:
    bool begin(const uint8_t* chunk, size_t len,
               const H5ChannelDesc* schema, uint8_t nCh,
               uint16_t nominalIntervalS = 60);

    /** @return false once the block is exhausted or the payload underflows. */
    bool next(uint32_t& epoch, int16_t* v);

    const H5DataHeader& header( ) const { return *_hdr; }
    uint8_t  count( ) const { return _hdr ? _hdr->pre.a : 0; }
    uint8_t  index( ) const { return _idx; }

private:
    const H5DataHeader* _hdr = nullptr;
    const uint8_t* _chunk = nullptr;
    uint8_t  _nCh = 0;
    uint8_t  _idx = 0;
    bool     _raw = false;
    uint16_t _nominal = 60;
    uint32_t _prevEpoch = 0;
    int32_t  _prevDelta = 60;
    int16_t  _prev[H5_MAX_CHANNELS];
    BitReader _br;
};

/**
 * @brief Newest epoch in a .wip snapshot that may seed the provisional clock.
 *
 * The snapshot is the block that was still open when the device went down, so
 * at boot it holds the newest measurements in existence — which is exactly why
 * it seeds the clock, and exactly why a bad one is dangerous: the seed decides
 * what every record written before NTP arrives is stamped with.
 *
 * Three gates, in order:
 *   1. magic and version, so a foreign or truncated file is not read as one;
 *   2. h5SeedPlausible on t0, bounding how far past its own day a snapshot
 *      may reach (see h5SeedCeiling);
 *   3. the CRC, via the decoder — nothing here trusts a field the integrity
 *      check has not covered. t0 sits inside the CRC's first span, so a
 *      forged one fails this gate rather than reaching the clock.
 *
 * Records are then walked to the newest plausible one, which tightens the seed
 * from "start of the open block" to "last measurement taken".
 *
 * @return 0 when the snapshot fails any gate; the caller keeps whatever the
 *         sealed day file gave it.
 */
static inline uint32_t h5SeedFromSnapshot(const uint8_t* chunk, size_t len,
                                          const H5ChannelDesc* schema,
                                          uint8_t nCh,
                                          uint32_t dayStart, uint32_t dayEnd,
                                          uint16_t nominalS) {
    if (!chunk || len < sizeof(H5DataHeader) || !schema || nCh == 0) return 0;
    const H5DataHeader* h = (const H5DataHeader*)chunk;
    if (h->pre.magic != H5_MAGIC || h->pre.version != H5_VERSION) return 0;
    if (!h5SeedPlausible(h->t0, dayStart, dayEnd, nominalS)) return 0;

    HistoryV5Decoder dec;
    if (!dec.begin(chunk, len, schema, nCh, nominalS)) return 0;

    uint32_t seed = h->t0;
    uint32_t epoch = 0;
    int16_t vals[H5_MAX_CHANNELS];
    while (dec.next(epoch, vals)) {
        if (epoch > seed && h5SeedPlausible(epoch, dayStart, dayEnd, nominalS)) {
            seed = epoch;
        }
    }
    return seed;
}

/* ===========================================================================
 *  FILE SCANNER (§5)
 * ======================================================================== */

enum H5ScanChunk : uint8_t {
    H5_SCAN_END = 0,
    H5_SCAN_SCHEMA,
    H5_SCAN_DATA,
};

/**
 * @brief Random-access byte source for the scanner.
 * @return bytes actually read, or a negative value on error.
 */
typedef int (*H5ReadFn)(void* ctx, uint32_t offset, uint8_t* buf, size_t len);

/**
 * @brief Walks a file's chunks without decoding payloads.
 *
 * DATA chunks are skipped via payloadLen, so a month of history is 720 hops
 * rather than 43 200 record decodes. SCHEMAs and DATA headers (tails
 * included) come out in file order. Every chunk handed to the caller has had
 * its CRC checked; a chunk that fails is skipped and counted, never
 * propagated (§3.7-4).
 */
class HistoryV5Scan {
public:
    /**
     * @param verifyPayload false checks only the parts the scanner reads
     *        anyway. The envelope path over long ranges never touches a
     *        payload, and reading every one of them just to CRC it is the
     *        difference between 60 KiB and 200 KiB of flash reads.
     */
    void begin(H5ReadFn reader, void* ctx, uint32_t size, bool verifyPayload = true);

    /** Advance one chunk. @return what it was, H5_SCAN_END at EOF. */
    H5ScanChunk next( );

    /** Advance until the next SCHEMA. @return false at EOF. */
    bool nextSchema(H5ChannelDesc* out, uint8_t& nCh, uint8_t& seq);
    /** Advance until the next DATA. @return false at EOF. */
    bool nextData(H5DataHeader& hdr, int16_t* kf, int16_t* mn, int16_t* mx);

    /** Position on the block containing @p epoch. @return false if none. */
    bool seek(uint32_t epoch);

    /* Whatever the last next() produced. */
    const H5DataHeader&  header( ) const { return _hdr; }
    const H5ChannelDesc* schema( ) const { return _schema; }
    uint8_t nCh( )     const { return _nCh; }
    uint8_t schemaSeq( ) const { return _seq; }
    const int16_t* keyframe( ) const { return _kf; }
    const int16_t* chMin( )    const { return _mn; }
    const int16_t* chMax( )    const { return _mx; }
    uint32_t chunkOffset( ) const { return _chunkOff; }
    uint32_t chunkSize( )   const { return _chunkLen; }
    /** Chunks skipped for a bad CRC or a broken frame since begin(). */
    uint16_t rejected( ) const { return _rejected; }
    /**
     * @brief Read the current DATA chunk whole, for the decoder.
     * @param verify check §3.4 over the bytes just read. A reader that
     *        decodes should pass true and begin( ) with verifyPayload
     *        false: same guarantee, one flash access instead of a dozen.
     */
    bool readChunk(uint8_t* buf, size_t cap, size_t& outLen, bool verify = false);

private:
    bool read(uint32_t off, uint8_t* buf, size_t len);
    bool verifyDataCrc(uint32_t off, const H5DataHeader& hdr);

    H5ReadFn _reader = nullptr;
    void*    _ctx = nullptr;
    uint32_t _size = 0;
    uint32_t _pos = 0;
    bool     _verifyPayload = true;

    H5DataHeader  _hdr{};
    H5ChannelDesc _schemaBuf[H5_MAX_CHANNELS];
    const H5ChannelDesc* _schema = nullptr;
    uint8_t  _nCh = 0;
    uint8_t  _seq = 0;
    int16_t  _kf[H5_MAX_CHANNELS];
    int16_t  _mn[H5_MAX_CHANNELS];
    int16_t  _mx[H5_MAX_CHANNELS];
    uint32_t _chunkOff = 0;
    uint32_t _chunkLen = 0;
    uint16_t _rejected = 0;
};

/* ===========================================================================
 *  SCHEMA HELPERS
 * ======================================================================== */

/** Serialize a SCHEMA chunk into @p out. @return bytes written, or 0. */
size_t h5BuildSchemaChunk(uint8_t* out, size_t cap,
                          const H5ChannelDesc* schema, uint8_t nCh, uint8_t seq);

/** @return true when the two schemas are byte-for-byte identical (§14-7). */
bool h5SchemaEquals(const H5ChannelDesc* a, uint8_t na,
                    const H5ChannelDesc* b, uint8_t nb);

/** Unit string for a kind, for CSV headers and graph axes. Never null. */
const char* h5KindUnit(uint8_t kind);
/** Short machine-readable name for a kind ("temp", "hum", ...). Never null. */
const char* h5KindName(uint8_t kind);
