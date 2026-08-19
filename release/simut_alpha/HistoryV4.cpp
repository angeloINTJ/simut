/**
 * @file HistoryV4.cpp
 * @brief V4 universal binary history codec — implementation.
 *
 * Format:
 *   File = [16B fixed header] [sensor table (variable)] [measurement table
 *          (variable)] [string pool] [record...]
 *
 *   Anchor record: epoch (32b LE) + bit-packed measurement values, padded to byte
 *   Delta record: mask (ceil(N/8) bytes) + zigzag-varint Δepoch + varint per set bit
 */

#include "HistoryV4.h"
#include <string.h>

/* ============================================================================
 * ZIGZAG VARINT
 *
 * Moved here from HistoryCodec.cpp when the v2/v3 codec was deleted — these
 * two functions were the only part of that file V4 ever used.
 * ============================================================================ */

size_t writeVarintZ(int32_t v, uint8_t* buf) {
    /* zigzag: maps signed (-1,+1,-2,+2,...) -> unsigned (1,2,3,4,...) */
    /* The sign-smear `v >> 31` is the canonical zigzag idiom. It is
     * implementation-defined in the standard and GCC defines it as an
     * arithmetic shift, which is the behaviour this codec is specified
     * against (bit-exact parity with tools/history_v5.py). */
    /* cppcheck-suppress shiftNegativeLHS */
    /* cppcheck-suppress shiftTooManyBitsSigned */
    uint32_t u = ((uint32_t)v << 1) ^ ((uint32_t)(v >> 31));
    size_t n = 0;
    while (u >= 0x80) {
        buf[n++] = (uint8_t)(u | 0x80);
        u >>= 7;
    }
    buf[n++] = (uint8_t)u;
    return n;
}

size_t readVarintZ(const uint8_t* buf, size_t bufLen, int32_t& out) {
    uint32_t u = 0;
    size_t n = 0;
    int shift = 0;
    while (n < bufLen && n < 5) {
        uint8_t b = buf[n++];
        u |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            out = (int32_t)((u >> 1) ^ (~((u & 1) - 1)));
            return n;
        }
        shift += 7;
    }
    return 0; /* truncated or invalid varint (>5 bytes) */
}

/* ============================================================================
 * BIT PACKING — extract / insert at arbitrary bit offset
 *
 * Values are stored LSB-first within each byte.
 * Epoch (32 bits) occupies bits 0..31. Measurement values follow contiguously.
 * ============================================================================ */

int64_t histV4BitExtract(const uint8_t *buf, size_t bitOffset, uint8_t bitWidth) {
    if (bitWidth == 0) return 0;

    uint64_t raw = 0;
    size_t byteIdx = bitOffset >> 3;      /* bitOffset / 8 */
    uint8_t shift  = bitOffset & 0x7;     /* bitOffset % 8 */

    /* Read enough bytes to cover shift + bitWidth bits (may span byte boundaries) */
    uint8_t bytesNeeded = (shift + bitWidth + 7) / 8;
    for (uint8_t i = 0; i < bytesNeeded; i++) {
        raw |= ((uint64_t)buf[byteIdx + i]) << (i * 8);
    }

    /* Shift down to LSB-aligned and mask to bitWidth */
    raw >>= shift;
    if (bitWidth < 64) {
        raw &= (1ULL << bitWidth) - 1;
    }

    /* Sign-extend from bitWidth to int64_t.
     * Skip for 1-bit (both 0 and 1 are valid, no sign).
     * Skip if the value is the NAN sentinel (all bits set) —
     *   histV4IsNan handles this with masked comparison. */
    if (bitWidth > 1 && bitWidth < 64) {
        uint64_t nanMask = (1ULL << bitWidth) - 1;
        if (raw != nanMask) {  /* not NAN sentinel */
            uint64_t signBit = 1ULL << (bitWidth - 1);
            if (raw & signBit) {
                raw |= (~0ULL) << bitWidth;
            }
        }
    }

    return (int64_t)raw;
}

void histV4BitInsert(uint8_t *buf, size_t bitOffset, uint8_t bitWidth, int64_t value) {
    if (bitWidth == 0) return;

    uint64_t uval;
    if (bitWidth < 64) {
        uval = (uint64_t)value & ((1ULL << bitWidth) - 1);
    } else {
        uval = (uint64_t)value;
    }

    size_t byteIdx = bitOffset >> 3;
    uint8_t shift  = bitOffset & 0x7;

    /* Write LSB-first, byte by byte, merging with existing bits */
    while (bitWidth > 0) {
        uint8_t bitsInByte = 8 - shift;
        if (bitsInByte > bitWidth) bitsInByte = bitWidth;

        /* bitsInByte is 8 - (bitOffset & 7), so 1..8, and the clamp above
         * only ever lowers it. cppcheck reads `bitsInByte = bitWidth` without
         * honouring the `bitsInByte > bitWidth` guard and reports a 32-bit
         * shift that cannot occur. */
        /* cppcheck-suppress shiftTooManyBits */
        uint8_t mask = ((1U << bitsInByte) - 1) << shift;
        /* cppcheck-suppress shiftTooManyBits */
        uint8_t byteVal = (uint8_t)((uval & ((1U << bitsInByte) - 1)) << shift);
        buf[byteIdx] = (buf[byteIdx] & ~mask) | byteVal;

        uval >>= bitsInByte;
        bitWidth -= bitsInByte;
        shift = 0;
        byteIdx++;
    }
}

/* ============================================================================
 * HEADER I/O
 * ============================================================================ */

size_t histV4WriteHeaderBuf(uint8_t *buf, size_t bufSize,
                            const HistV4SensorDef *sensors, uint8_t sensorCount,
                            const HistV4MeasureDef *measures, uint8_t measureCount,
                            const uint8_t *strPool, uint8_t strPoolSize,
                            uint16_t anchorPeriod) {
    /* Compute total header size */
    uint16_t sensorTableSize = sensorCount * sizeof(HistV4SensorDef);
    uint16_t measureTableSize = measureCount * sizeof(HistV4MeasureDef);
    uint16_t totalSize = HIST_V4_HEADER_FIXED + sensorTableSize + measureTableSize + strPoolSize;
    if (bufSize < totalSize) return 0;

    /* Fixed header */
    HistV4FileHeader hdr;
    memcpy(hdr.magic, HIST_V4_MAGIC, 4);
    hdr.version      = HIST_V4_VERSION;
    hdr.headerSize   = totalSize;
    hdr.anchorPeriod = anchorPeriod;
    hdr.sensorCount  = sensorCount;
    hdr.measureCount = measureCount;
    hdr.flags        = 0;
    hdr.strPoolSize  = strPoolSize;
    hdr.reserved     = 0;

    size_t pos = 0;
    memcpy(buf + pos, &hdr, sizeof(hdr)); pos += sizeof(hdr);

    /* Sensor table */
    if (sensorCount > 0) {
        memcpy(buf + pos, sensors, sensorTableSize); pos += sensorTableSize;
    }

    /* Measurement table */
    if (measureCount > 0) {
        memcpy(buf + pos, measures, measureTableSize); pos += measureTableSize;
    }

    /* String pool */
    if (strPoolSize > 0) {
        memcpy(buf + pos, strPool, strPoolSize); pos += strPoolSize;
    }

    return pos;
}

size_t histV4ReadHeaderBuf(const uint8_t *buf, size_t bufLen, HistV4State &state) {
    /* Reset state */
    histV4Reset(state);

    /* Fixed header */
    if (bufLen < HIST_V4_HEADER_FIXED) return 0;
    const HistV4FileHeader *hdr = (const HistV4FileHeader*)buf;

    /* Validate */
    if (memcmp(hdr->magic, HIST_V4_MAGIC, 4) != 0) return 0;
    if (hdr->version != HIST_V4_VERSION) return 0;
    if (hdr->sensorCount > HIST_V4_MAX_SENSORS) return 0;
    if (hdr->measureCount > HIST_V4_MAX_MEASUREMENTS) return 0;
    if (hdr->headerSize > bufLen) return 0;

    state.sensorCount  = hdr->sensorCount;
    state.measureCount = hdr->measureCount;
    state.anchorPeriod = hdr->anchorPeriod;
    state.strPoolSize  = hdr->strPoolSize;

    size_t pos = HIST_V4_HEADER_FIXED;

    /* Sensor table */
    if (hdr->sensorCount > 0) {
        size_t stSize = (size_t)hdr->sensorCount * sizeof(HistV4SensorDef);
        if (pos + stSize > bufLen) return 0;
        memcpy(state.sensors, buf + pos, stSize);
        pos += stSize;
    }

    /* Measurement table */
    if (hdr->measureCount > 0) {
        size_t mtSize = (size_t)hdr->measureCount * sizeof(HistV4MeasureDef);
        if (pos + mtSize > bufLen) return 0;
        memcpy(state.measures, buf + pos, mtSize);
        pos += mtSize;
    }

    /* String pool */
    if (hdr->strPoolSize > 0 && hdr->strPoolSize <= HIST_V4_MAX_STRPOOL) {
        if (pos + hdr->strPoolSize > bufLen) return 0;
        memcpy(state.strPool, buf + pos, hdr->strPoolSize);
        pos += hdr->strPoolSize;
    }

    /* Pre-compute bit offsets for anchor decode */
    uint16_t bitOff = 32; /* epoch is always 32 bits first */
    for (uint8_t i = 0; i < state.measureCount; i++) {
        state.measureByteOffset[i] = bitOff >> 3;
        state.measureBitOffset[i]  = bitOff;
        bitOff += state.measures[i].bitWidth;
    }
    state.anchorByteSize = (bitOff + 7) / 8;
    state.headerLen = (uint16_t)pos;

    return pos; /* bytes consumed */
}

void histV4ResetCodec(HistV4State &state) {
    /* Clears ONLY the running codec fields, keeping the schema. A reader that
     * has to start over on a file it already parsed (the migration decodes the
     * same file twice: once to write, once to verify) must rewind the delta
     * chain without paying to re-read the header. histV4Reset would wipe the
     * schema with it. */
    memset(state.lastAnchor, 0, sizeof(state.lastAnchor));
    memset(state.fieldHasValid, 0, sizeof(state.fieldHasValid));
    state.recordsSinceAnchor = 0;
    state.initialized = false;
    state.lastEpoch = 0;
}

int64_t histV4RemapValue(int8_t srcIdx, const int64_t *srcValues,
                         const HistV4State &srcState,
                         const HistV4MeasureDef &dstDef) {
    /* A column with no counterpart in the source did not exist for this part
     * of the day. NaN is the honest answer, not zero. */
    if (srcIdx < 0) return histV4NanSentinel(dstDef.bitWidth);

    const HistV4MeasureDef &srcDef = srcState.measures[srcIdx];
    const int64_t raw = srcValues[srcIdx];

    if (histV4IsNan(raw, srcDef.bitWidth)) return histV4NanSentinel(dstDef.bitWidth);

    /* Same packing on both sides — the overwhelmingly common case, since both
     * defs come from histV4DefaultBitWidth/Scale for the same channel. Carry
     * the integer untouched: a float round trip here would only add error. */
    if (srcDef.bitWidth == dstDef.bitWidth && srcDef.scale == dstDef.scale) return raw;

    /* Width or scale genuinely changed. A raw integer is meaningless without
     * the def it was packed against, so go through the physical value. */
    return histV4FromFloat(histV4ToFloat(raw, srcDef), dstDef);
}

/* ============================================================================
 * DECODE NEXT (convenience wrapper for file scanning)
 * ============================================================================ */

size_t histV4DecodeNext(const uint8_t *buf, size_t bufLen,
                        HistV4State &state,
                        int64_t *outValues, uint32_t *outEpoch) {
    bool isAnchor = (state.recordsSinceAnchor == 0 ||
                     state.recordsSinceAnchor >= state.anchorPeriod);
    if (!state.initialized) isAnchor = true;
    return histV4Decode(buf, bufLen, state, outValues, outEpoch, isAnchor);
}

/* ============================================================================
 * ENCODE
 * ============================================================================ */

size_t histV4Encode(const int64_t *values, uint8_t count,
                    HistV4State &state,
                    uint8_t *buf, size_t bufSize,
                    uint32_t epoch,
                    bool *outIsAnchor) {
    if (outIsAnchor) *outIsAnchor = false;

    /* v1.5.3 GUARD: a day-rollover can rebuild the schema in-place between
     * the caller snapshotting measureCount and this call. Encoding values
     * ordered by the OLD schema against the NEW bit widths/offsets would
     * silently corrupt the file — fail instead (caller logs and retries on
     * the next history tick with a fresh schema pointer). */
    if (count != state.measureCount) return 0;

    bool emitAnchor = !state.initialized ||
                      state.recordsSinceAnchor == 0 ||
                      state.recordsSinceAnchor >= state.anchorPeriod;

    if (emitAnchor) {
        /* ── ANCHOR ── */
        uint16_t anchorBytes = state.anchorByteSize;
        if (anchorBytes < 4) anchorBytes = 4;
        if (bufSize < anchorBytes) return 0;

        memset(buf, 0, anchorBytes);

        /* Epoch (32 bits, offset 0) */
        histV4BitInsert(buf, 0, 32, (int64_t)(uint32_t)epoch);

        /* Measurement values at their pre-computed bit offsets (offset 32+) */
        for (uint8_t i = 0; i < count; i++) {
            int64_t raw = values[i];
            uint8_t bw = state.measures[i].bitWidth;
            histV4BitInsert(buf, state.measureBitOffset[i], bw, raw);

            /* Update running state.
             * v1.5.3 FIX: ASSIGN validity (decoder does the same). The old
             * set-only form left fieldHasValid=true after a NaN anchor,
             * while the decoder cleared it — the two state machines then
             * disagreed on delta-vs-absolute for every later record. */
            state.lastAnchor[i] = raw;
            state.fieldHasValid[i] = !histV4IsNan(raw, bw);
        }

        state.recordsSinceAnchor = 1;
        state.initialized = true;
        state.lastEpoch = epoch;

        if (outIsAnchor) *outIsAnchor = true;
        return anchorBytes;
    }

    /* ── DELTA ── */
    /* Build mask bits: ceil(count / 8) bytes */
    uint8_t maskBytes = (count + 7) / 8;
    uint8_t maskBuf[8]; /* up to 64 measurements → 8 bytes */
    memset(maskBuf, 0, sizeof(maskBuf));

    size_t pos = 0;
    if (pos + maskBytes > bufSize) return 0;
    /* Reserve space for mask — fill at end */
    uint8_t *maskPos = buf + pos;
    pos += maskBytes;

    /* Delta epoch: zigzag varint of (epoch - lastEpoch) */
    int32_t dEpoch = (int32_t)(epoch - state.lastEpoch);
    size_t n = writeVarintZ(dEpoch, buf + pos);
    if (n == 0 || pos + n > bufSize) return 0;
    pos += n;

    /* Emit delta values for changed measurements */
    for (uint8_t i = 0; i < count; i++) {
        int64_t raw = values[i];
        uint8_t bw = state.measures[i].bitWidth;
        bool isNan = histV4IsNan(raw, bw);
        bool prevValid = state.fieldHasValid[i];

        if (isNan && !prevValid) {
            /* Still never valid — no change, skip */
            continue;
        }
        if (!isNan && prevValid && raw == state.lastAnchor[i]) {
            /* Value unchanged — skip (save bytes) */
            continue;
        }

        /* Mark this measurement as present in the mask */
        maskBuf[i >> 3] |= (1 << (i & 7));

        /* Encode: delta whenever the decoder will ADD (prevValid),
         * absolute only when it will take the value as-is (!prevValid).
         * v1.5.3 FIX: the valid→NaN transition used to go through the
         * absolute branch while the decoder (fieldHasValid still true)
         * added it as a delta — 25.00 °C → NaN decoded as 2500+65535 =
         * 68035 and the field diverged until the next anchor. Riding the
         * delta path (raw here IS the sentinel) makes the unchanged
         * decoder land exactly on lastAnchor + Δ = sentinel.
         * Known limit (pre-existing): Δ is int32-clamped, so bitWidth ≥ 32
         * cannot represent the sentinel jump — practical widths are ≤ 24. */
        int32_t encoded;
        if (prevValid) {
            int64_t delta = raw - state.lastAnchor[i];
            /* Clamp to int32 for varint encoding */
            if (delta > 2147483647LL) delta = 2147483647LL;
            if (delta < -2147483647LL) delta = -2147483647LL;
            encoded = (int32_t)delta;
        } else {
            /* Absolute: first valid value after never/NaN — the decoder
             * agrees because its fieldHasValid is false here too. */
            /* Clamp to int32 */
            if (raw > 2147483647LL) raw = 2147483647LL;
            if (raw < -2147483647LL) raw = -2147483647LL;
            encoded = (int32_t)raw;
        }

        n = writeVarintZ(encoded, buf + pos);
        if (n == 0 || pos + n > bufSize) return 0;
        pos += n;

        /* Update running state.
         * v1.5.3 FIX: ASSIGN validity to mirror the decoder. The old
         * keep-as-is comment was the second half of the asymmetry: after
         * a NaN the encoder stayed "valid" (next record as delta) while
         * the decoder went invalid (next record as absolute). */
        state.lastAnchor[i] = raw;
        state.fieldHasValid[i] = !isNan;
    }

    /* Write mask bytes (now that we know which bits are set) */
    memcpy(maskPos, maskBuf, maskBytes);

    state.recordsSinceAnchor++;
    state.lastEpoch = epoch;
    return pos;
}

/* ============================================================================
 * DECODE
 * ============================================================================ */

size_t histV4Decode(const uint8_t *buf, size_t bufLen,
                    HistV4State &state,
                    int64_t *outValues, uint32_t *outEpoch,
                    bool isAnchor) {
    if (isAnchor) {
        /* ── ANCHOR DECODE ── */
        if (bufLen < state.anchorByteSize) return 0;

        /* Extract epoch */
        *outEpoch = (uint32_t)histV4BitExtract(buf, 0, 32);

        /* Extract each measurement from its pre-computed bit offset */
        for (uint8_t i = 0; i < state.measureCount; i++) {
            int64_t raw = histV4BitExtract(buf, state.measureBitOffset[i],
                                           state.measures[i].bitWidth);
            uint8_t bw = state.measures[i].bitWidth;
            /* Per-channel signedness (design: CH_TEMP is signed
             * "-327.68..+327.66"; hum/press/lux are UNSIGNED "0..max").
             * histV4BitExtract sign-extends universally, which mangled
             * every anchor whose unsigned value had the top bit set:
             * 76.5% hum (raw 765, 10-bit) decoded as -25.9; 1013.2 hPa
             * (raw 10132, 14-bit) decoded as -625.1. The encoder always
             * stored the low bits correctly — undoing the extension here
             * retroactively repairs existing files. */
            if (!channelInfo(state.measures[i].channel).isSigned && raw < 0 &&
                !histV4IsNan(raw, bw) && bw < 64) {
                raw &= (int64_t)((1ULL << bw) - 1);
            }
            /* Normalize: all-bits-set extracted value → canonical nanSentinel */
            if (histV4IsNan(raw, bw)) {
                raw = histV4NanSentinel(bw);
            }
            outValues[i] = raw;
            state.lastAnchor[i] = raw;
            state.fieldHasValid[i] = !histV4IsNan(raw, bw);
        }

        state.recordsSinceAnchor = 1;
        state.initialized = true;
        state.lastEpoch = *outEpoch;
        return state.anchorByteSize;
    }

    /* ── DELTA DECODE ── */
    uint8_t maskBytes = (state.measureCount + 7) / 8;
    if (bufLen < (size_t)(maskBytes + 1)) return 0; /* at least mask + 1 byte varint */

    /* Read mask */
    size_t pos = 0;
    const uint8_t *mask = buf + pos;
    pos += maskBytes;

    /* Read delta epoch */
    int32_t dEpoch;
    size_t n = readVarintZ(buf + pos, bufLen - pos, dEpoch);
    if (n == 0) return 0;
    pos += n;
    const uint32_t newEpoch = state.lastEpoch + (uint32_t)dEpoch;

    /* ── A1-b: DECODE DE DELTA É TRANSACIONAL ───────────────────────────
     * A versão anterior gravava `state.lastAnchor[i]`/`fieldHasValid[i]`
     * campo a campo DENTRO do laço de parsing e ainda podia sair com
     * `return 0` no meio (varint truncado). O estado ficava meio aplicado.
     *
     * Enquanto o único tratamento de falha era `break`, isso passava
     * despercebido — o leitor abandonava o arquivo logo em seguida. Com o
     * retry pós-refill de histV4DecodeNextRefill (A1), o MESMO registro é
     * decodificado de novo sobre um estado já parcialmente avançado: os
     * campos aplicados na 1ª passada somariam o delta duas vezes,
     * corrompendo silenciosamente todos os valores seguintes do dia.
     *
     * Duas passadas resolvem: parse (só lê o estado) e, apenas em caso de
     * sucesso completo, commit. Falha ⇒ estado idêntico ao da entrada. */

    /* Passada 1 — parsing puro. Campos fora da máscara herdam a âncora. */
    for (uint8_t i = 0; i < state.measureCount; i++) {
        outValues[i] = state.lastAnchor[i];
    }

    for (uint8_t i = 0; i < state.measureCount; i++) {
        if (!(mask[i >> 3] & (1 << (i & 7)))) continue;

        int32_t decoded;
        n = readVarintZ(buf + pos, bufLen - pos, decoded);
        if (n == 0) return 0;   /* estado intacto — seguro para retry */
        pos += n;

        int64_t v = state.fieldHasValid[i]
                  ? state.lastAnchor[i] + (int64_t)decoded  /* delta */
                  : (int64_t)decoded;                       /* 1º válido: absoluto */

        /* Normalize sentinel */
        const uint8_t bw = state.measures[i].bitWidth;
        if (histV4IsNan(v, bw)) v = histV4NanSentinel(bw);

        outValues[i] = v;
    }

    /* Passada 2 — commit. Só chega aqui se TODOS os varints foram lidos. */
    for (uint8_t i = 0; i < state.measureCount; i++) {
        if (!(mask[i >> 3] & (1 << (i & 7)))) continue;
        state.lastAnchor[i]    = outValues[i];
        state.fieldHasValid[i] = !histV4IsNan(outValues[i],
                                              state.measures[i].bitWidth);
    }

    *outEpoch = newEpoch;
    state.recordsSinceAnchor++;
    state.lastEpoch = newEpoch;
    return pos;
}

/* ============================================================================
 * HELPERS
 * ============================================================================ */

void histV4Reset(HistV4State &state) {
    memset(&state, 0, sizeof(HistV4State));
    state.anchorPeriod = HIST_V4_ANCHOR_PERIOD;
}

uint16_t histV4MaxDeltaSize(const HistV4State &state) {
    /* Mask: ceil(N/8) + max varint epoch (5) + N * max varint (5 each) */
    uint8_t maskBytes = (state.measureCount + 7) / 8;
    return maskBytes + 5 + (uint16_t)state.measureCount * 5;
}

size_t histV4MakeMeasKey(char *out, size_t outSize, uint8_t channel, const char *hwId) {
    if (outSize == 0) return 0;
    size_t pos = 0;

    /* Prefix */
    if (pos < outSize - 1) {
        out[pos++] = histV4ChannelPrefix(channel);
    }

    /* hwId */
    while (*hwId && pos < outSize - 1) {
        out[pos++] = *hwId++;
    }

    out[pos] = '\0';
    return pos;
}
