/**
 * @file HistoryV5.cpp
 * @brief V5 history codec — bit I/O, block encoder/decoder, file scanner.
 *
 * Mirrors `tools/history_v5.py` symbol for symbol. When the two disagree the
 * native property test fails, which is the point of having both.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#include "HistoryV5.h"

/* ===========================================================================
 *  CRC-16/CCITT-FALSE
 * ======================================================================== */

/** CRC contribution of each high nibble, poly 0x1021. */
static const uint16_t kH5CrcNibble[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF
};

uint16_t h5Crc16(const uint8_t* data, size_t len, uint16_t crc) {
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        crc = (uint16_t)((crc << 4) ^ kH5CrcNibble[(crc >> 12) & 0x0F]);
        crc = (uint16_t)((crc << 4) ^ kH5CrcNibble[(crc >> 12) & 0x0F]);
    }
    return crc;
}

/* ===========================================================================
 *  BIT I/O
 * ======================================================================== */

void BitWriter::begin(uint8_t* buf, size_t cap, H5WriteFn drain, void* ctx) {
    _buf = buf; _cap = cap; _drain = drain; _ctx = ctx;
    _len = 0; _bits = 0; _acc = 0; _nacc = 0;
    _crc = 0xFFFF; _overflow = false;
}

void BitWriter::emitByte(uint8_t b) {
    _crc = h5Crc16(&b, 1, _crc);
    if (_len >= _cap) {
        /* Window full. With a sink it is a flush point, without one it is
         * the end of the road — mark and drop, never write past the buffer. */
        if (_drain && _len) {
            if (!_drain(_ctx, _buf, _len)) { _overflow = true; return; }
            _len = 0;
        } else {
            _overflow = true;
            return;
        }
    }
    _buf[_len++] = b;
}

void BitWriter::put(uint32_t value, uint8_t width) {
    for (int8_t i = (int8_t)width - 1; i >= 0; i--) {
        _acc = (uint8_t)((_acc << 1) | ((value >> i) & 1u));
        _nacc++;
        _bits++;
        if (_nacc == 8) { emitByte(_acc); _acc = 0; _nacc = 0; }
    }
}

void BitWriter::putPrefix(const char* bits) {
    for (const char* p = bits; *p; p++) put(*p == '1' ? 1u : 0u, 1);
}

size_t BitWriter::flush( ) {
    if (_nacc) {
        emitByte((uint8_t)(_acc << (8 - _nacc)));
        _acc = 0; _nacc = 0;
    }
    if (_drain && _len) {
        if (!_drain(_ctx, _buf, _len)) _overflow = true;
        _len = 0;
    }
    return (_bits + 7) / 8;
}

void BitReader::begin(const uint8_t* buf, size_t len) {
    _buf = buf; _len = len; _pos = 0; _underflow = false;
}

uint32_t BitReader::get(uint8_t width) {
    /* Byte at a time, not bit at a time. Decoding a block is ~700 symbols and
     * the widest is 32 bits, so the naive per-bit loop ran thousands of
     * iterations per block — it was the bulk of what a graph spent decoding.
     * Past the end the reader keeps appending zero bits, exactly as the
     * per-bit version did, and latches the sticky underflow flag. */
    uint32_t v = 0;
    uint8_t remaining = width;
    while (remaining) {
        const size_t byteIdx = _pos >> 3;
        if (byteIdx >= _len) {
            _underflow = true;
            _pos += remaining;
            return v << remaining;
        }
        const uint8_t bitOff = _pos & 7;
        uint8_t take = (uint8_t)(8 - bitOff);
        if (take > remaining) take = remaining;
        const uint8_t chunk = (uint8_t)(((uint8_t)(_buf[byteIdx] << bitOff)) >> (8 - take));
        v = (v << take) | chunk;
        _pos += take;
        remaining = (uint8_t)(remaining - take);
    }
    return v;
}

/* ===========================================================================
 *  ENCODER
 * ======================================================================== */

void HistoryV5Encoder::begin(const H5ChannelDesc* schema, uint8_t nCh,
                             uint16_t nominalIntervalS) {
    _schema  = schema;
    _nCh     = (nCh > H5_MAX_CHANNELS) ? H5_MAX_CHANNELS : nCh;
    _nominal = nominalIntervalS ? nominalIntervalS : 60;
    _count   = 0;
    _t0      = 0;
}

void HistoryV5Encoder::reset(uint32_t epoch, const int16_t* v) {
    _t0 = epoch;
    memcpy(_kf, v, (size_t)_nCh * sizeof(int16_t));
    _count = 1;
}

bool HistoryV5Encoder::add(uint32_t epoch, const int16_t* v) {
    if (_count == 0) { reset(epoch, v); return true; }
    if (_count >= H5_BLOCK_MAX_RECORDS) return false;

    /* RAW addresses records by a u16 offset from t0 (§3.6). A record outside
     * that reach would leave the block with no RAW form, and an incompressible
     * block with no RAW form has no bound at all — the guarantee in §14-1 that
     * a block always fits H5_BLOCK_MAX_BYTES rests on RAW always being
     * available. So the block closes here instead.
     *
     * This bounds §14-2 ("do not open a block because of time"): ordinary
     * jitter and NTP corrections still ride the resync symbol inside the
     * block. Only a jump past ~18 h, which would also make the block's
     * min/max envelope meaningless for graphs, forces a new one. */
    if (epoch < _t0 || (uint32_t)(epoch - _t0) > 0xFFFFu) return false;

    const uint8_t i = (uint8_t)(_count - 1);      /* slot for record _count+1 */
    _epoch[i] = epoch;
    memcpy(_v[i], v, (size_t)_nCh * sizeof(int16_t));
    _count++;
    return true;
}

uint32_t HistoryV5Encoder::lastEpoch( ) const {
    if (_count == 0) return 0;
    if (_count == 1) return _t0;
    return _epoch[_count - 2];
}

void HistoryV5Encoder::shiftTime(int32_t deltaS) {
    if (_count == 0 || deltaS == 0) return;
    _t0 = (uint32_t)((int32_t)_t0 + deltaS);
    for (uint8_t i = 0; i + 1 < _count; i++) {
        _epoch[i] = (uint32_t)((int32_t)_epoch[i] + deltaS);
    }
}

void HistoryV5Encoder::envelope(int16_t* mn, int16_t* mx) const {
    for (uint8_t c = 0; c < _nCh; c++) { mn[c] = H5_NAN_SENTINEL; mx[c] = H5_NAN_SENTINEL; }
    for (uint8_t r = 0; r < _count; r++) {
        const int16_t* row = (r == 0) ? _kf : _v[r - 1];
        for (uint8_t c = 0; c < _nCh; c++) {
            const int16_t val = row[c];
            if (val == H5_NAN_SENTINEL) continue;   /* NAN never sets the band */
            if (mn[c] == H5_NAN_SENTINEL || val < mn[c]) mn[c] = val;
            if (mx[c] == H5_NAN_SENTINEL || val > mx[c]) mx[c] = val;
        }
    }
}

size_t HistoryV5Encoder::rawSize( ) const {
    if (_count <= 1) return 0;
    return (size_t)(_count - 1) * H5_RAW_RECORD_SIZE(_nCh);
}

bool HistoryV5Encoder::rawUsable( ) const {
    /* add() refuses any record RAW could not address, so this holds by
     * construction. Kept as a belt-and-braces check because seal()'s size
     * guarantee — and with it R3's worst-case bound — depends on it. */
    for (uint8_t i = 0; i + 1 < _count; i++) {
        const uint32_t e = _epoch[i];
        if (e < _t0 || (e - _t0) > 0xFFFFu) return false;
    }
    return true;
}

size_t HistoryV5Encoder::emitCompressed(BitWriter* bw) const {
    /* One pass over records 2..count. With bw == nullptr this only measures,
     * which is how seal() learns payloadLen before it can build the header.
     * A zero-capacity writer counts bits and keeps its CRC exact — emitByte
     * hashes the byte before it looks for room — while writing nowhere. */
    BitWriter sizer;
    BitWriter* w = bw;
    if (!w) { sizer.begin(nullptr, 0); w = &sizer; }

    int16_t  prev[H5_MAX_CHANNELS];
    memcpy(prev, _kf, (size_t)_nCh * sizeof(int16_t));
    int32_t  prevDelta = (int32_t)_nominal;
    uint32_t prevEpoch = _t0;

    for (uint8_t r = 0; r + 1 < _count; r++) {
        const uint32_t epoch = _epoch[r];
        const int16_t* row   = _v[r];

        /* ── time symbol (§3.5) ── */
        const int32_t delta = (int32_t)(epoch - prevEpoch);
        const int32_t dod   = delta - prevDelta;
        if (dod == 0) {
            w->put(0, 1);
            prevDelta = delta;
        } else if (dod >= -64 && dod <= 63) {
            w->put(0x2, 2);                       /* "10" */
            w->put((uint32_t)dod & 0x7Fu, 7);
            prevDelta = delta;
        } else if (dod >= -2048 && dod <= 2047) {
            w->put(0x6, 3);                       /* "110" */
            w->put((uint32_t)dod & 0xFFFu, 12);
            prevDelta = delta;
        } else {
            w->put(0x7, 3);                       /* "111" — resync */
            w->put(epoch, 32);
            prevDelta = (int32_t)_nominal;
        }
        prevEpoch = epoch;

        /* ── value symbols (§3.5) ── */
        for (uint8_t c = 0; c < _nCh; c++) {
            const int16_t cur = row[c];
            const int16_t p   = prev[c];
            if (p == H5_NAN_SENTINEL && cur == H5_NAN_SENTINEL) {
                w->put(0, 1);                     /* still NAN: 1 bit         */
            } else if (p == H5_NAN_SENTINEL || cur == H5_NAN_SENTINEL) {
                w->put(0xF, 4);                   /* "1111" — restart chain   */
                w->put((uint16_t)cur, 16);
            } else {
                const int32_t d = (int32_t)cur - (int32_t)p;
                if (d == 0) {
                    w->put(0, 1);
                } else if (d >= -4 && d <= 3) {
                    w->put(0x2, 2);               /* "10"   */
                    w->put(h5Zigzag((int16_t)d), 3);
                } else if (d >= -32 && d <= 31) {
                    w->put(0x6, 3);               /* "110"  */
                    w->put(h5Zigzag((int16_t)d), 6);
                } else if (d >= -512 && d <= 511) {
                    w->put(0xE, 4);               /* "1110" */
                    w->put(h5Zigzag((int16_t)d), 10);
                } else {
                    w->put(0xF, 4);               /* "1111" — absolute        */
                    w->put((uint16_t)cur, 16);
                }
            }
            prev[c] = cur;
        }
    }
    return w->flush( );
}

size_t HistoryV5Encoder::emitRaw(H5WriteFn sink, void* ctx, uint16_t* crc) const {
    uint8_t rec[H5_RAW_RECORD_SIZE(H5_MAX_CHANNELS)];
    const size_t recLen = H5_RAW_RECORD_SIZE(_nCh);
    size_t total = 0;
    for (uint8_t r = 0; r + 1 < _count; r++) {
        const uint16_t dt = (uint16_t)(_epoch[r] - _t0);
        memcpy(rec, &dt, 2);
        memcpy(rec + 2, _v[r], (size_t)_nCh * sizeof(int16_t));
        if (crc) *crc = h5Crc16(rec, recLen, *crc);
        if (sink && !sink(ctx, rec, recLen)) return 0;
        total += recLen;
    }
    return total;
}

size_t HistoryV5Encoder::buildFixedAndTails(uint8_t* out, uint8_t flags,
                                            uint16_t payloadLen, uint16_t crc) const {
    H5DataHeader h;
    h.pre.magic   = H5_MAGIC;
    h.pre.version = H5_VERSION;
    h.pre.type    = H5_CHUNK_DATA;
    h.pre.flags   = flags;
    h.pre.a       = _count;
    h.pre.b       = _nCh;
    h.pre.rsv     = 0xFF;
    h.t0          = _t0;
    h.payloadLen  = payloadLen;
    h.crc16       = crc;
    memcpy(out, &h, sizeof(h));

    int16_t mn[H5_MAX_CHANNELS], mx[H5_MAX_CHANNELS];
    envelope(mn, mx);
    memcpy(h5Keyframe(out, _nCh), _kf, (size_t)_nCh * sizeof(int16_t));
    memcpy(h5ChMin  (out, _nCh), mn,  (size_t)_nCh * sizeof(int16_t));
    memcpy(h5ChMax  (out, _nCh), mx,  (size_t)_nCh * sizeof(int16_t));
    return H5_DATA_HEADER_SIZE(_nCh);
}

size_t HistoryV5Encoder::seal(uint8_t* out, size_t cap, uint8_t extraFlags) {
    if (_count == 0 || !_schema || _nCh == 0) return 0;

    const size_t hdrLen = H5_DATA_HEADER_SIZE(_nCh);
    if (cap < hdrLen) return 0;

    /* Pass 1: how big would each form be? */
    const size_t compLen = emitCompressed(nullptr);
    const size_t rawLen  = rawSize( );
    const bool   useRaw  = rawUsable( ) && rawLen && rawLen < compLen;
    const size_t payLen  = useRaw ? rawLen : compLen;
    if (payLen > 0xFFFFu || cap < hdrLen + payLen) return 0;

    /* Pass 2: produce the payload where it belongs. */
    if (useRaw) {
        uint8_t* p = out + hdrLen;
        for (uint8_t r = 0; r + 1 < _count; r++) {
            const uint16_t dt = (uint16_t)(_epoch[r] - _t0);
            memcpy(p, &dt, 2);
            memcpy(p + 2, _v[r], (size_t)_nCh * sizeof(int16_t));
            p += H5_RAW_RECORD_SIZE(_nCh);
        }
    } else if (compLen) {
        BitWriter bw;
        bw.begin(out + hdrLen, cap - hdrLen);
        emitCompressed(&bw);
        if (bw.overflow( )) return 0;
    }

    const uint8_t flags = (uint8_t)(extraFlags | (useRaw ? H5_FLAG_RAW : 0));
    buildFixedAndTails(out, flags, (uint16_t)payLen, 0);

    /* §3.4: everything but the crc16 field itself, in file order. */
    uint16_t crc = h5Crc16(out, 14);
    crc = h5Crc16(out + 16, hdrLen - 16 + payLen, crc);
    memcpy(out + 14, &crc, 2);
    return hdrLen + payLen;
}

size_t HistoryV5Encoder::sealStream(H5WriteFn sink, void* ctx, uint8_t extraFlags) {
    if (_count == 0 || !_schema || _nCh == 0 || !sink) return 0;

    const size_t hdrLen = H5_DATA_HEADER_SIZE(_nCh);
    const size_t compLen = emitCompressed(nullptr);
    const size_t rawLen  = rawSize( );
    const bool   useRaw  = rawUsable( ) && rawLen && rawLen < compLen;
    const size_t payLen  = useRaw ? rawLen : compLen;
    if (payLen > 0xFFFFu) return 0;

    const uint8_t flags = (uint8_t)(extraFlags | (useRaw ? H5_FLAG_RAW : 0));
    uint8_t hdr[H5_DATA_HEADER_SIZE(H5_MAX_CHANNELS)];
    buildFixedAndTails(hdr, flags, (uint16_t)payLen, 0);

    /* Pass 2: the CRC of a chunk that is never whole in RAM. Header first,
     * then the payload as it would be emitted — nothing is kept. */
    uint16_t crc = h5Crc16(hdr, 14);
    crc = h5Crc16(hdr + 16, hdrLen - 16, crc);
    if (useRaw) {
        emitRaw(nullptr, nullptr, &crc);
    } else if (compLen) {
        BitWriter bw;
        bw.begin(nullptr, 0);           /* CRC the payload without storing it */
        bw.seedCrc(crc);
        emitCompressed(&bw);
        crc = bw.crc( );
    }
    memcpy(hdr + 14, &crc, 2);

    /* Pass 3: emit for real. */
    if (!sink(ctx, hdr, hdrLen)) return 0;
    if (useRaw) {
        if (emitRaw(sink, ctx, nullptr) != rawLen) return 0;
    } else if (compLen) {
        uint8_t window[64];
        BitWriter bw;
        bw.begin(window, sizeof(window), sink, ctx);
        emitCompressed(&bw);
        if (bw.overflow( )) return 0;
    }
    return hdrLen + payLen;
}

/* ===========================================================================
 *  DECODER
 * ======================================================================== */

bool HistoryV5Decoder::begin(const uint8_t* chunk, size_t len,
                             const H5ChannelDesc* schema, uint8_t nCh,
                             uint16_t nominalIntervalS) {
    _hdr = nullptr;
    if (!chunk || len < sizeof(H5DataHeader) || !schema || nCh == 0) return false;

    const H5DataHeader* h = (const H5DataHeader*)chunk;
    if (h->pre.magic != H5_MAGIC || h->pre.version != H5_VERSION) return false;
    if (h->pre.type != H5_CHUNK_DATA) return false;
    if (h->pre.b != nCh || nCh > H5_MAX_CHANNELS) return false;   /* §3.7-3 */
    if (h->pre.a == 0 || h->pre.a > H5_BLOCK_MAX_RECORDS) return false;

    const size_t hdrLen = H5_DATA_HEADER_SIZE(nCh);
    if (len < hdrLen + h->payloadLen) return false;

    uint16_t crc = h5Crc16(chunk, 14);
    crc = h5Crc16(chunk + 16, hdrLen - 16 + h->payloadLen, crc);
    if (crc != h->crc16) return false;                            /* §3.7-4 */

    _hdr = h;
    _chunk = chunk;
    _nCh = nCh;
    _idx = 0;
    _raw = (h->pre.flags & H5_FLAG_RAW) != 0;
    _nominal = nominalIntervalS ? nominalIntervalS : 60;
    _prevEpoch = h->t0;
    _prevDelta = (int32_t)_nominal;
    memcpy(_prev, h5Keyframe((uint8_t*)chunk, nCh), (size_t)nCh * sizeof(int16_t));
    _br.begin(chunk + hdrLen, h->payloadLen);
    return true;
}

bool HistoryV5Decoder::next(uint32_t& epoch, int16_t* v) {
    if (!_hdr || _idx >= _hdr->pre.a) return false;

    if (_idx == 0) {                       /* record 1 is t0 + the keyframe */
        epoch = _hdr->t0;
        memcpy(v, _prev, (size_t)_nCh * sizeof(int16_t));
        _idx++;
        return true;
    }

    if (_raw) {
        const uint8_t* p = _chunk + H5_DATA_HEADER_SIZE(_nCh)
                         + (size_t)(_idx - 1) * H5_RAW_RECORD_SIZE(_nCh);
        uint16_t dt;
        memcpy(&dt, p, 2);
        epoch = _hdr->t0 + dt;
        memcpy(v, p + 2, (size_t)_nCh * sizeof(int16_t));
        _idx++;
        return true;
    }

    /* ── time symbol ── */
    if (_br.get(1) == 0) {
        epoch = _prevEpoch + (uint32_t)_prevDelta;
    } else if (_br.get(1) == 0) {
        int32_t dod = (int32_t)_br.get(7);
        if (dod & 0x40) dod -= 128;
        _prevDelta += dod;
        epoch = _prevEpoch + (uint32_t)_prevDelta;
    } else if (_br.get(1) == 0) {
        int32_t dod = (int32_t)_br.get(12);
        if (dod & 0x800) dod -= 4096;
        _prevDelta += dod;
        epoch = _prevEpoch + (uint32_t)_prevDelta;
    } else {
        epoch = _br.get(32);
        _prevDelta = (int32_t)_nominal;
    }
    _prevEpoch = epoch;

    /* ── value symbols ── */
    for (uint8_t c = 0; c < _nCh; c++) {
        int16_t val;
        if (_br.get(1) == 0) {
            val = _prev[c];
        } else if (_br.get(1) == 0) {
            val = (int16_t)(_prev[c] + h5Unzigzag((uint16_t)_br.get(3)));
        } else if (_br.get(1) == 0) {
            val = (int16_t)(_prev[c] + h5Unzigzag((uint16_t)_br.get(6)));
        } else if (_br.get(1) == 0) {
            val = (int16_t)(_prev[c] + h5Unzigzag((uint16_t)_br.get(10)));
        } else {
            val = (int16_t)_br.get(16);
        }
        _prev[c] = val;
        v[c] = val;
    }

    if (_br.underflow( )) { _idx = _hdr->pre.a; return false; }
    _idx++;
    return true;
}

/* ===========================================================================
 *  FILE SCANNER
 * ======================================================================== */

void HistoryV5Scan::begin(H5ReadFn reader, void* ctx, uint32_t size,
                          bool verifyPayload) {
    _reader = reader; _ctx = ctx; _size = size; _verifyPayload = verifyPayload;
    _pos = 0; _schema = nullptr; _nCh = 0; _seq = 0;
    _chunkOff = 0; _chunkLen = 0; _rejected = 0;
    memset(&_hdr, 0, sizeof(_hdr));
}

bool HistoryV5Scan::read(uint32_t off, uint8_t* buf, size_t len) {
    if (!_reader || off + len > _size) return false;
    return _reader(_ctx, off, buf, len) == (int)len;
}

bool HistoryV5Scan::verifyDataCrc(uint32_t off, const H5DataHeader& hdr) {
    /* The CRC covers the payload, so checking it means reading the payload.
     * A caller walking a month of envelopes asked for headers only and would
     * pay 3x the flash reads for a check it does not need — the header fields
     * it consumes were already framed against the file size by the caller. */
    if (!_verifyPayload) return true;

    const size_t hdrLen = H5_DATA_HEADER_SIZE(hdr.pre.b);
    uint8_t buf[64];

    /* Fixed part minus the crc field, then the tails, then the payload. */
    if (!read(off, buf, 14)) return false;
    uint16_t crc = h5Crc16(buf, 14);
    uint32_t p = off + 16;
    uint32_t remaining = (uint32_t)(hdrLen - 16) + hdr.payloadLen;
    while (remaining) {
        const size_t n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        if (!read(p, buf, n)) return false;
        crc = h5Crc16(buf, n, crc);
        p += n;
        remaining -= (uint32_t)n;
    }
    return crc == hdr.crc16;
}

H5ScanChunk HistoryV5Scan::next( ) {
    /* One read per chunk, not two. Reading the preamble to learn the chunk's
     * width and then reading the header again cost a second LittleFS seek
     * per block, which is most of what a header walk spends: the widest
     * header a chunk can have is 112 B, so pulling that much up front and
     * parsing out of it halves the I/O for the envelope path. */
    uint8_t buf[H5_DATA_HEADER_SIZE(H5_MAX_CHANNELS)];
    static_assert(sizeof(buf) >= H5_SCHEMA_CHUNK_SIZE(H5_MAX_CHANNELS),
                  "scan buffer must hold the widest chunk header");

    while (_pos + sizeof(H5ChunkPreamble) <= _size) {
        size_t want = sizeof(buf);
        if (_pos + want > _size) want = _size - _pos;
        if (!read(_pos, buf, want)) return H5_SCAN_END;
        H5ChunkPreamble pre;
        memcpy(&pre, buf, sizeof(pre));
        if (pre.magic != H5_MAGIC || pre.version != H5_VERSION) {
            /* Resyncing on garbage invents records; stopping loses at most
             * the tail, which the next append rebuilds. */
            _rejected++;
            return H5_SCAN_END;
        }

        if (pre.type == H5_CHUNK_SCHEMA) {
            const uint8_t n = pre.a;
            if (n == 0 || n > H5_MAX_CHANNELS) { _rejected++; return H5_SCAN_END; }
            const size_t sz = H5_SCHEMA_CHUNK_SIZE(n);
            if (sz > want) return H5_SCAN_END;
            uint16_t stored;
            memcpy(&stored, buf + 8 + 4u * n, 2);
            if (h5Crc16(buf, 8 + 4u * n) != stored) {
                _rejected++;
                _pos += sz;
                continue;
            }
            memcpy(_schemaBuf, buf + 8, 4u * n);
            _schema = _schemaBuf;
            _nCh = n;
            _seq = pre.b;
            _chunkOff = _pos;
            _chunkLen = (uint32_t)sz;
            _pos += sz;
            return H5_SCAN_SCHEMA;
        }

        if (pre.type == H5_CHUNK_DATA) {
            const uint8_t n = pre.b;
            if (n == 0 || n > H5_MAX_CHANNELS || pre.a == 0
                || pre.a > H5_BLOCK_MAX_RECORDS) {
                _rejected++;
                return H5_SCAN_END;
            }
            const size_t hdrLen = H5_DATA_HEADER_SIZE(n);
            if (hdrLen > want) return H5_SCAN_END;
            memcpy(&_hdr, buf, sizeof(H5DataHeader));
            const uint32_t total = (uint32_t)(hdrLen + _hdr.payloadLen);
            if (_pos + total > _size) { _rejected++; return H5_SCAN_END; }
            if (!verifyDataCrc(_pos, _hdr)) {
                _rejected++;
                _pos += total;
                continue;
            }
            memcpy(_kf, h5Keyframe(buf, n), (size_t)n * sizeof(int16_t));
            memcpy(_mn, h5ChMin  (buf, n), (size_t)n * sizeof(int16_t));
            memcpy(_mx, h5ChMax  (buf, n), (size_t)n * sizeof(int16_t));
            _chunkOff = _pos;
            _chunkLen = total;
            _pos += total;
            return H5_SCAN_DATA;
        }

        _rejected++;
        return H5_SCAN_END;                 /* unknown type: frame is lost */
    }
    return H5_SCAN_END;
}

bool HistoryV5Scan::nextSchema(H5ChannelDesc* out, uint8_t& nCh, uint8_t& seq) {
    for (;;) {
        const H5ScanChunk t = next( );
        if (t == H5_SCAN_END) return false;
        if (t != H5_SCAN_SCHEMA) continue;
        if (out) memcpy(out, _schemaBuf, (size_t)_nCh * sizeof(H5ChannelDesc));
        nCh = _nCh;
        seq = _seq;
        return true;
    }
}

bool HistoryV5Scan::nextData(H5DataHeader& hdr, int16_t* kf, int16_t* mn, int16_t* mx) {
    for (;;) {
        const H5ScanChunk t = next( );
        if (t == H5_SCAN_END) return false;
        if (t != H5_SCAN_DATA) continue;
        hdr = _hdr;
        const size_t bytes = (size_t)_hdr.pre.b * sizeof(int16_t);
        if (kf) memcpy(kf, _kf, bytes);
        if (mn) memcpy(mn, _mn, bytes);
        if (mx) memcpy(mx, _mx, bytes);
        return true;
    }
}

bool HistoryV5Scan::seek(uint32_t epoch) {
    /* The answer is the last block whose t0 is at or before `epoch` — but only
     * while blocks are in time order, which is an assumption about the writer,
     * not a property of the format. A boot with a mis-seeded clock writes
     * blocks stamped ahead of the ones that follow them (2026-08-14), and then
     * the rule breaks twice over: the walk used to stop at the first block past
     * `epoch` and never look further, and more than one block can straddle the
     * cutoff, of which this keeps only the last.
     *
     * So the walk now runs to the end and checks the assumption while it is
     * there. Header-only hops, no payload reads — 24 for a day, which is why
     * checking costs nothing worth measuring. Out of order, it refuses to skip
     * anything and hands back the start of the file: the callers all filter by
     * time anyway, so the cost is decode work, and the alternative is silently
     * skipping records that are on flash. In an ordered file the answer, and
     * the fast path, are exactly what they were. */
    _pos = 0;
    _schema = nullptr;
    uint32_t bestOff = 0, bestLen = 0;
    bool found = false;
    bool ordered = true;
    uint32_t prevT0 = 0;
    bool havePrev = false;
    for (;;) {
        const H5ScanChunk t = next( );
        if (t == H5_SCAN_END) break;
        if (t != H5_SCAN_DATA) continue;
        if (havePrev && _hdr.t0 < prevT0) ordered = false;
        prevT0 = _hdr.t0;
        havePrev = true;
        if (_hdr.t0 <= epoch) {
            bestOff = _chunkOff; bestLen = _chunkLen; found = true;
        }
    }
    if (!ordered) {
        _pos = 0;
        _schema = nullptr;
        _nCh = 0;
        return false;
    }
    if (!found) {
        /* Nothing at or before `epoch` — every block in this file is NEWER
         * than the target. Rewind rather than leave the scanner parked at
         * EOF: a range query whose cutoff falls before a file's first block
         * wants the whole file, and the caller cannot tell "positioned at the
         * start" from "positioned past the end" without walking it. Leaving
         * it at EOF silently dropped the first file of every long range. */
        _pos = 0;
        _schema = nullptr;
        _nCh = 0;
        return false;
    }
    /* Re-walk so the schema in force at that block is the one loaded. */
    _pos = 0;
    _schema = nullptr;
    for (;;) {
        const H5ScanChunk t = next( );
        if (t == H5_SCAN_END) return false;
        if (t == H5_SCAN_DATA && _chunkOff == bestOff) {
            _pos = bestOff;                 /* leave it as the next chunk */
            _chunkLen = bestLen;
            return true;
        }
    }
}

bool HistoryV5Scan::readChunk(uint8_t* buf, size_t cap, size_t& outLen, bool verify) {
    if (_chunkLen == 0 || cap < _chunkLen) return false;
    if (!read(_chunkOff, buf, _chunkLen)) return false;

    /* §3.4 over the bytes already in RAM. A caller that decodes wants the
     * chunk AND its CRC, and paying for both in one read is the difference
     * between two flash accesses per block and a dozen: verifyDataCrc( )
     * walks the payload in 64 B pieces off the filesystem, and readChunk( )
     * then read the very same bytes a second time. Same guarantee (§3.7-4:
     * a chunk that fails is never decoded), a fraction of the I/O. */
    if (verify) {
        uint16_t crc = h5Crc16(buf, 14);
        crc = h5Crc16(buf + 16, _chunkLen - 16, crc);
        uint16_t stored;
        memcpy(&stored, buf + 14, sizeof(stored));
        if (crc != stored) { _rejected++; return false; }
    }
    outLen = _chunkLen;
    return true;
}

/* ===========================================================================
 *  SCHEMA HELPERS
 * ======================================================================== */

size_t h5BuildSchemaChunk(uint8_t* out, size_t cap,
                          const H5ChannelDesc* schema, uint8_t nCh, uint8_t seq) {
    if (!out || !schema || nCh == 0 || nCh > H5_MAX_CHANNELS) return 0;
    const size_t sz = H5_SCHEMA_CHUNK_SIZE(nCh);
    if (cap < sz) return 0;

    H5ChunkPreamble pre;
    pre.magic   = H5_MAGIC;
    pre.version = H5_VERSION;
    pre.type    = H5_CHUNK_SCHEMA;
    pre.flags   = 0;
    pre.a       = nCh;
    pre.b       = seq;
    pre.rsv     = 0xFF;
    memcpy(out, &pre, sizeof(pre));
    memcpy(out + 8, schema, 4u * nCh);
    const uint16_t crc = h5Crc16(out, 8 + 4u * nCh);
    memcpy(out + 8 + 4u * nCh, &crc, 2);
    return sz;
}

bool h5SchemaEquals(const H5ChannelDesc* a, uint8_t na,
                    const H5ChannelDesc* b, uint8_t nb) {
    if (na != nb) return false;
    if (!a || !b) return a == b;
    return memcmp(a, b, (size_t)na * sizeof(H5ChannelDesc)) == 0;
}

const char* h5KindUnit(uint8_t kind) {
    switch (kind) {
        case H5_KIND_TEMP_C:    return "\xC2\xB0" "C";   /* UTF-8 degree sign */
        case H5_KIND_HUM_PCT:   return "%";
        case H5_KIND_PRESS_HPA: return "hPa";
        case H5_KIND_CO2_PPM:   return "ppm";
        case H5_KIND_VOC_IDX:   return "idx";
        default:                return "";
    }
}

const char* h5KindName(uint8_t kind) {
    switch (kind) {
        case H5_KIND_TEMP_C:    return "temp";
        case H5_KIND_HUM_PCT:   return "hum";
        case H5_KIND_PRESS_HPA: return "press";
        case H5_KIND_CO2_PPM:   return "co2";
        case H5_KIND_VOC_IDX:   return "voc";
        default:                return "gen";
    }
}
