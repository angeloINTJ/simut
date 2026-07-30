/**
 * @file HistoryV4.h
 * @brief V4 universal binary history format — self-describing schema, bit-packed anchors.
 *
 * Design goals:
 * - Self-describing: file header IS the schema (sensor table + measurement table + string pool)
 * - Universal: any sensor type, any measurement channel, any unit, configurable bit widths (1-64)
 * - Compact: bit-packed anchors (no wasted bytes) + zigzag-varint deltas with change mask
 * - Fast: pre-computed bit offsets, single-pass decode, Cortex-M0+ friendly
 * - No backward compat: clean break from v1/v2/v3 slot-indexed format
 *
 * File extension: .sim4
 * Directory: /history/YYYYMMDD.sim4 (one file per day)
 *
 * Measurement identification: {channel_prefix}{sensor_hwId}
 *   e.g. sensor "THD0001" → "tTHD0001" (temp), "uTHD0001" (humidity)
 *   Prefixes: t=TEMP, u=HUM, p=PRESS, l=LUX
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "SystemDefs.h"
#include "sensors/SensorChannelTable.h" /* channelInfo( ) — letters, widths, scales */

/* ============================================================================
 * V4 FILE FORMAT CONSTANTS
 * ============================================================================ */

/** Magic bytes "SIM4" as a uint32_t for fast comparison. */
constexpr uint32_t HIST_V4_MAGIC_U32 = 0x344D4953; /* "SIM4" little-endian */
constexpr char     HIST_V4_MAGIC[4]  = {'S','I','M','4'};
constexpr uint16_t HIST_V4_VERSION   = 0x0004;
constexpr uint16_t HIST_V4_HEADER_FIXED = 16; /* bytes before sensor table */
constexpr uint16_t HIST_V4_ANCHOR_PERIOD = 60;
constexpr size_t   HIST_V4_MAX_DELTA  = 256;  /* worst-case delta buffer */
constexpr size_t   HIST_V4_READ_BUF   = 256;  /* sliding read buffer */
constexpr size_t   HIST_V4_MAX_HEADER = 2048; /* worst-case header buffer */

/** Practical limits (fits RP2040 RAM budget). */
constexpr uint8_t  HIST_V4_MAX_SENSORS     = 32;
constexpr uint8_t  HIST_V4_MAX_MEASUREMENTS = 64;
constexpr uint8_t  HIST_V4_MAX_STRPOOL     = 200;

/** File extension for V4 history files. */
#define HISTORY_V4_FILE_EXT ".sim4"

/* ============================================================================
 * FIXED FILE HEADER (16 bytes)
 * ============================================================================ */

struct __attribute__((packed)) HistV4FileHeader {
    char     magic[4];       /* "SIM4" */
    uint16_t version;        /* 0x0004 */
    uint16_t headerSize;     /* total header bytes (incl. this 16B prefix) */
    uint16_t anchorPeriod;   /* default 60 */
    uint8_t  sensorCount;    /* M: number of sensors in sensor table */
    uint8_t  measureCount;   /* N: number of measurements in measurement table */
    uint8_t  flags;          /* bit 0: hasCrc32, bits 1-7: reserved */
    uint8_t  strPoolSize;    /* string pool size in bytes */
    uint16_t reserved;       /* = 0 */
};
static_assert(sizeof(HistV4FileHeader) == HIST_V4_HEADER_FIXED,
              "HistV4FileHeader must be 16 bytes");

/* ============================================================================
 * MEASUREMENT CHANNEL (extensible)
 *
 * Maps 1:1 with SensorChannel from SensorHelpers.h for values 0-3.
 * Extended range (4-255) for future channel types.
 * ============================================================================ */

/* These three used to be switch statements holding their own copy of the
 * channel facts. The letters in particular were duplicated by the calibration
 * code, which then grew a reader-side whitelist (`prefix != 't' && != 'u'`)
 * that silently refused every pressure row ever written. One table now, in
 * sensors/SensorChannelTable.h; the unknown-channel answers ('x', 16, 100) are
 * preserved by channelInfo( )'s fallback row. */

/** Channel prefix characters for measurement key generation. */
inline char histV4ChannelPrefix(uint8_t channel) {
    return channelInfo(channel).letter;
}

/** Default bit width for a given channel type. */
inline uint8_t histV4DefaultBitWidth(uint8_t channel) {
    return channelInfo(channel).bitWidth;
}

/** Default scale (multiplier) for a given channel type — raw = round(float * scale). */
inline uint32_t histV4DefaultScale(uint8_t channel) {
    return channelInfo(channel).scale;
}

/* ============================================================================
 * SCHEMA STRUCTURES (reconstructed from file header at read time)
 * ============================================================================ */

/**
 * @brief Describes one sensor present in the file.
 *
 * All strings are stored in the string pool; sensorDef stores (offset, len).
 * The consumer copies them out to null-terminated C strings on demand.
 */
struct HistV4SensorDef {
    uint8_t hwIdOffset;   /**< Byte offset in string pool */
    uint8_t hwIdLen;      /**< Length of hwId string (0-16) */
    uint8_t nameOffset;   /**< Byte offset in string pool */
    uint8_t nameLen;      /**< Length of friendly name (0-32) */
    uint8_t sensorType;   /**< SensorType enum value */
    uint8_t channelMask;  /**< Bitmask of channels present (bit0=TEMP, bit1=HUM, bit2=PRESS, bit3=LUX) */
    uint8_t flags;        /**< Reserved */
    uint8_t reserved[2];
};

/**
 * @brief Describes one measurement slot in the file.
 *
 * This is the core of the self-describing format. Each measurement declares
 * what sensor it belongs to, what physical quantity it measures, the unit,
 * display precision, and the storage bit width used in anchor records.
 */
struct HistV4MeasureDef {
    uint8_t  sensorIdx;   /**< Index into sensor table (0..sensorCount-1) */
    uint8_t  channel;     /**< SensorChannel: 0=TEMP, 1=HUM, 2=PRESS, 3=LUX, ... */
    uint8_t  bitWidth;    /**< 1..64: bits used for this field in anchor records */
    uint8_t  decimals;    /**< 0..6: decimal places for display */
    uint8_t  unitOffset;  /**< Byte offset in string pool */
    uint8_t  unitLen;     /**< Length of unit string */
    /** Multiplier applied on write: rawInteger = round(realValue * scale),
     *  so realValue = rawInteger / scale. See histV4FromFloat, which is the
     *  canonical conversion and also clamps per channel signedness.
     *  (This used to read "realValue = rawInteger / (scale/100)", which is not
     *  what the code does — following it decodes 23.60 C as 2360 C.) */
    uint32_t scale;
};

/* ============================================================================
 * CODEC STATE
 * ============================================================================ */

/**
 * @brief Runtime state for the V4 codec — encoder and decoder.
 *
 * At file-open time, the header is parsed and the schema (sensor table,
 * measurement table, string pool) is loaded into this struct. Bit offsets
 * for each measurement are pre-computed for fast anchor decode.
 *
 * The codec tracks lastAnchor values for delta reconstruction and a counter
 * for anchor-period detection. This state is ephemeral (RAM only) and is
 * rebuilt from the file at boot / day rollover via histV4ScanFile().
 */
struct HistV4State {
    /* ── Schema (populated by histV4ReadHeader) ── */
    uint8_t  measureCount;
    uint8_t  sensorCount;
    HistV4MeasureDef measures[HIST_V4_MAX_MEASUREMENTS];
    HistV4SensorDef  sensors[HIST_V4_MAX_SENSORS];
    uint8_t  strPool[HIST_V4_MAX_STRPOOL];
    uint8_t  strPoolSize;
    uint16_t anchorPeriod;

    /* ── Pre-computed decode offsets ── */
    uint16_t headerLen;                                   /**< Bytes the header occupies — where record 0 starts. RAM only. */
    uint16_t anchorByteSize;                              /**< ceil((32 + sum(bitWidth[i])) / 8) */
    uint16_t measureBitOffset[HIST_V4_MAX_MEASUREMENTS];  /**< Bit offset from start of anchor record (epoch=0..31) */
    uint8_t  measureByteOffset[HIST_V4_MAX_MEASUREMENTS]; /**< Byte offset from start of anchor record */

    /* ── Running codec state ── */
    int64_t  lastAnchor[HIST_V4_MAX_MEASUREMENTS];   /**< Last known absolute values */
    bool     fieldHasValid[HIST_V4_MAX_MEASUREMENTS]; /**< Has this field ever been valid? */
    uint16_t recordsSinceAnchor;                      /**< 0 = next is anchor, anchorPeriod = force anchor */
    bool     initialized;                             /**< false until first anchor decoded */
    uint32_t lastEpoch;                               /**< Last decoded/encoded epoch (for delta computation) */
};

/* ============================================================================
 * UNIFIED DECODED RECORD (for consumers)
 *
 * All consumers (graph, web API, telemetry, preload) work with DecodedRecord.
 * The dispatch layer converts from v2/v3 or v4 format into this uniform struct.
 * ============================================================================ */

struct DecodedRecord {
    uint32_t epoch;
    float    values[HIST_V4_MAX_MEASUREMENTS]; /**< NAN = no reading */
    uint8_t  measureCount;
    const HistV4MeasureDef* measureDefs;       /**< Schema reference (pointer into HistV4State) */
};

/* ============================================================================
 * API — HEADER / SCHEMA
 * ============================================================================ */

/**
 * @brief Serialize a V4 file header into a byte buffer.
 *
 * @param buf       Output buffer (must be large enough).
 * @param bufSize   Buffer capacity.
 * @param sensors   Sensor definitions array.
 * @param sensorCount Number of sensors.
 * @param measures  Measurement definitions array.
 * @param measureCount Number of measurements.
 * @param strPool   String pool bytes (concatenated strings).
 * @param strPoolSize Size of string pool.
 * @param anchorPeriod Anchor period (default 60).
 * @return Bytes written, or 0 if buffer too small.
 */
size_t histV4WriteHeaderBuf(uint8_t *buf, size_t bufSize,
                            const HistV4SensorDef *sensors, uint8_t sensorCount,
                            const HistV4MeasureDef *measures, uint8_t measureCount,
                            const uint8_t *strPool, uint8_t strPoolSize,
                            uint16_t anchorPeriod = HIST_V4_ANCHOR_PERIOD);

/**
 * @brief Parse a V4 file header from a byte buffer, populating the codec state.
 *
 * Validates magic "SIM4", version 0x0004. Pre-computes bit offsets for
 * all measurement slots.
 *
 * @param buf       Buffer containing the header bytes.
 * @param bufLen    Buffer length.
 * @param state     Output codec state (schema populated, running state reset).
 * @return Bytes consumed, or 0 on error.
 */
size_t histV4ReadHeaderBuf(const uint8_t *buf, size_t bufLen, HistV4State &state);

/* ============================================================================
 * API — ENCODE / DECODE
 * ============================================================================ */

/**
 * @brief Encode one V4 record. Automatically decides anchor vs delta.
 *
 * @param values     Array of measureCount signed integers (use nanSentinel for missing).
 * @param count      Number of measurements (= state.measureCount).
 * @param state      Codec state (updated in-place).
 * @param buf        Output buffer (must be >= HIST_V4_MAX_DELTA bytes).
 * @param bufSize    Buffer capacity.
 * @param epoch      Unix timestamp for this record.
 * @param outIsAnchor (out, optional) Set to true if anchor was emitted.
 * @return Bytes written, or 0 on error (buffer too small).
 */
size_t histV4Encode(const int64_t *values, uint8_t count,
                    HistV4State &state,
                    uint8_t *buf, size_t bufSize,
                    uint32_t epoch,
                    bool *outIsAnchor = nullptr);

/**
 * @brief Decode one V4 record.
 *
 * @param buf       Raw bytes from file.
 * @param bufLen    Available bytes.
 * @param state     Codec state (updated in-place).
 * @param outValues Output values array (caller-provided, measureCount elements).
 * @param outEpoch  Output epoch.
 * @param isAnchor  true if this record is an anchor (caller determines from state.recordsSinceAnchor).
 * @return Bytes consumed, or 0 on error.
 */
size_t histV4Decode(const uint8_t *buf, size_t bufLen,
                    HistV4State &state,
                    int64_t *outValues, uint32_t *outEpoch,
                    bool isAnchor);

/* ============================================================================
 * API — FILE SCAN / STATE RECONSTRUCTION
 * ============================================================================ */

/**
 * @brief Decode consecutive records from a buffer to advance codec state.
 *
 * Used during file scanning: call in a loop, feeding record data from the file.
 * Updates state.recordsSinceAnchor, state.lastAnchor[], etc.
 *
 * This is a convenience wrapper around histV4Decode that also handles
 * anchor vs delta determination.
 *
 * @param buf       Raw bytes (one record worth).
 * @param bufLen    Available bytes.
 * @param state     Codec state (updated in-place).
 * @param outValues Decoded values.
 * @param outEpoch  Decoded epoch.
 * @return Bytes consumed, or 0 on error.
 */
size_t histV4DecodeNext(const uint8_t *buf, size_t bufLen,
                        HistV4State &state,
                        int64_t *outValues, uint32_t *outEpoch);

/**
 * @brief Decodifica UM registro de um buffer deslizante, reabastecendo-o
 *        sob demanda, e consome os bytes lidos do buffer.
 *
 * @details Substitui a família de laços copiados nos consumidores V4
 * (preload, gráfico TFT, web, telemetria, scan). O padrão antigo era:
 *
 * @code
 *   if (filled < state.anchorByteSize && f.available() > 0) { ...read... }
 *   size_t n = histV4DecodeNext(buf, filled, state, vals, &epoch);
 *   if (n == 0) break;
 * @endcode
 *
 * O limiar de reabastecimento era o tamanho da ÂNCORA, mas um registro
 * DELTA pode ser maior que ela (`ceil(N/8) + 5 + 5*N` bytes no pior caso,
 * ver histV4MaxDeltaSize). Com `anchorByteSize <= filled < tamanho do
 * delta`, o decode devolvia 0, o refill não disparava e o leitor parava
 * (ou girava) no meio do arquivo: gráfico truncado, preload parcial, lote
 * de telemetria vazio e — o pior caso — scanHistoryFileV4 classificando o
 * resto do dia como "cauda rasgada" e mandando truncar o arquivo.
 *
 * Aqui a decisão de reabastecer é tomada DEPOIS da falha do decode, que é
 * o único momento em que se sabe de quantos bytes o registro precisava.
 *
 * @tparam RefillFn  Callable `size_t(uint8_t *dst, size_t maxBytes)` que
 *                   escreve até `maxBytes` em `dst` e devolve quantos
 *                   bytes foram lidos (0 = fim de arquivo / falha).
 *
 * @param buf        Buffer deslizante.
 * @param bufCap     Capacidade total de `buf`.
 * @param filled     [in/out] Bytes válidos em `buf`; decrementado do que
 *                   for consumido pelo registro decodificado.
 * @param state      Estado do codec (atualizado apenas em caso de sucesso).
 * @param outValues  Saída: valores decodificados (measureCount elementos).
 * @param outEpoch   Saída: epoch do registro.
 * @param refill     Functor de reabastecimento (ver acima).
 * @return Bytes consumidos, ou 0 quando não há mais registro decodificável
 *         (fim real do arquivo ou cauda rasgada/corrompida).
 */
template <typename RefillFn>
inline size_t histV4DecodeNextRefill(uint8_t *buf, size_t bufCap, size_t &filled,
                                     HistV4State &state,
                                     int64_t *outValues, uint32_t *outEpoch,
                                     RefillFn refill) {
    /* 1ª tentativa com o que já está no buffer. */
    size_t consumed = (filled > 0)
                    ? histV4DecodeNext(buf, filled, state, outValues, outEpoch)
                    : 0;

    /* Falhou: pode ser fome de bytes. Reabastece e tenta de novo, até o
     * buffer encher (limite superior) ou a fonte secar. O laço termina
     * sempre: `filled` cresce monotonicamente e é limitado por `bufCap`. */
    while (consumed == 0 && filled < bufCap) {
        const size_t got = refill(buf + filled, bufCap - filled);
        if (got == 0) break;                 /* EOF ou erro de leitura */
        filled += got;
        consumed = histV4DecodeNext(buf, filled, state, outValues, outEpoch);
    }

    if (consumed > 0) {
        memmove(buf, buf + consumed, filled - consumed);
        filled -= consumed;
    }
    return consumed;
}

/* ============================================================================
 * API — HELPERS
 * ============================================================================ */

/** Reset codec state to initial (keep schema, clear running state). */
void histV4Reset(HistV4State &state);

/**
 * @brief Rewind the running codec state, keeping the parsed schema.
 * @details For a reader that must decode the same file from the top again.
 * histV4Reset would clear the schema too, forcing a header re-read.
 */
void histV4ResetCodec(HistV4State &state);

/**
 * @brief Carry one value from a source schema into a destination measurement.
 *
 * @param srcIdx    Index into the source schema, or <0 when the destination
 *                  column has no counterpart (returns the NaN sentinel).
 * @param srcValues Decoded values of the source record.
 * @param srcState  Source schema (for the source measurement def).
 * @param dstDef    Destination measurement def.
 * @return Raw integer ready to encode against @p dstDef.
 *
 * Raw is carried verbatim when (bitWidth, scale) match on both sides, and only
 * goes through float when they genuinely differ — a raw integer means nothing
 * without the def it was packed against.
 */
int64_t histV4RemapValue(int8_t srcIdx, const int64_t *srcValues,
                         const HistV4State &srcState,
                         const HistV4MeasureDef &dstDef);

/** @return Anchor record size in bytes (pre-computed from schema). */
inline uint16_t histV4AnchorSize(const HistV4State &state) {
    return state.anchorByteSize;
}

/** @return Maximum possible delta record size for the current schema. */
uint16_t histV4MaxDeltaSize(const HistV4State &state);

/** @return The NAN sentinel for a given bit width (all bits set). */
inline int64_t histV4NanSentinel(uint8_t bitWidth) {
    if (bitWidth >= 64) return (int64_t)0xFFFFFFFFFFFFFFFFULL;
    return (1ULL << bitWidth) - 1;
}

/** @return true if the raw value equals the NAN sentinel (masked comparison, sign-extension-safe). */
inline bool histV4IsNan(int64_t raw, uint8_t bitWidth) {
    if (bitWidth >= 64) return raw == (int64_t)0xFFFFFFFFFFFFFFFFULL;
    uint64_t mask = (1ULL << bitWidth) - 1;
    return ((uint64_t)raw & mask) == mask;
}

/**
 * @brief Convert raw integer → float using measurement definition.
 * @return Float value, or NAN if sentinel.
 */
inline float histV4ToFloat(int64_t raw, const HistV4MeasureDef &def) {
    if (histV4IsNan(raw, def.bitWidth)) return NAN;
    return (float)((double)raw / (double)def.scale);
}

/**
 * @brief Convert float → raw integer using measurement definition.
 * @return Raw integer, or NAN sentinel for NAN input.
 */
inline int64_t histV4FromFloat(float v, const HistV4MeasureDef &def) {
    /* isfinite: +-INFINITY passed the old isnan check, scaled to inf and
     * the int64 cast clamped it to a plausible-looking max (102.2%). */
    if (!isfinite(v)) return histV4NanSentinel(def.bitWidth);
    double scaled = (double)v * (double)def.scale;
    int64_t raw = (int64_t)round(scaled);
    // Clamp per channel signedness (exclude sentinel at top):
    // a signed channel has a symmetric range; an unsigned one is 0..max, where
    // a negative raw would be stored as two's-complement low bits and decode as
    // a huge positive. Which channels are signed comes from the channel table,
    // not from `channel == 0` — that test made "signed" a property of being
    // first rather than of the quantity, so any future signed channel would
    // have silently reproduced the decode bug that hit humidity and pressure.
    const bool chSigned = channelInfo(def.channel).isSigned;
    int64_t maxVal = histV4NanSentinel(def.bitWidth) - 1;
    int64_t minVal = chSigned ? (-(maxVal / 2) - 1) : 0;
    if (chSigned) {
        int64_t maxSigned = maxVal / 2;  // top bit reserved for sign
        if (raw > maxSigned) raw = maxSigned;
    } else if (raw > maxVal) {
        raw = maxVal;
    }
    if (raw < minVal) raw = minVal;

    /* ── A3: o padrão all-ones é RESERVADO ao sentinela NaN ──────────────
     * `raw` é gravado truncado em `bitWidth` bits. Um valor legítimo cujo
     * padrão truncado seja all-ones colide com o sentinela e volta da
     * leitura como NAN — um buraco no histórico.
     *
     * Caso real do produto: -0,01 °C em s16 x100 → raw = -1 → 0xFFFF =
     * sentinela de 16 bits. Freezer cruzando 0 °C perdia o ponto.
     *
     * Deslocamos 1 LSB para longe do padrão. O erro máximo é 1 unidade da
     * escala (0,01 °C aqui) — determinístico e documentado, muito melhor
     * que um NaN. Não há risco de estourar o piso: o padrão de `minVal` é
     * 100…0 (signed) ou 000…0 (unsigned), nunca all-ones, logo `raw` só
     * entra aqui quando ainda há folga para decrementar. */
    if (histV4IsNan(raw, def.bitWidth)) raw -= 1;

    return raw;
}

/**
 * @brief Build a measurement key string: "{prefix}{hwId}"
 *
 * @param out      Output buffer.
 * @param outSize  Buffer capacity.
 * @param channel  SensorChannel (0=TEMP→'t', 1=HUM→'u', 2=PRESS→'p', 3=LUX→'l').
 * @param hwId     Sensor hardware ID (null-terminated).
 * @return Number of characters written (excluding null terminator).
 */
size_t histV4MakeMeasKey(char *out, size_t outSize, uint8_t channel, const char *hwId);

/**
 * @brief Copy a string from the string pool into a null-terminated buffer.
 *
 * @param out      Output buffer.
 * @param outSize  Buffer capacity (includes null terminator).
 * @param strPool  String pool bytes.
 * @param offset   Start offset in pool.
 * @param len      Number of bytes to copy.
 * @return Number of bytes copied (excluding null).
 */
inline size_t histV4StrPoolGet(char *out, size_t outSize,
                               const uint8_t *strPool, uint8_t offset, uint8_t len) {
    if (len >= outSize) len = outSize - 1;
    if (len > 0) memcpy(out, strPool + offset, len);
    out[len] = '\0';
    return len;
}

/* ============================================================================
 * BIT PACKING PRIMITIVES (anchor record encode/decode)
 *
 * Values are bit-packed consecutively: epoch (32 bits) + measurement[0..N-1].
 * LSB is packed first within each byte (Cortex-M0+ native byte order).
 * ============================================================================ */

/**
 * @brief Extract a signed integer from a bit-packed buffer.
 *
 * @param buf       Source buffer.
 * @param bitOffset Bit offset from start of buffer (0 = LSB of buf[0]).
 * @param bitWidth  Number of bits to extract (1..64).
 * @return Sign-extended value.
 */
int64_t histV4BitExtract(const uint8_t *buf, size_t bitOffset, uint8_t bitWidth);

/**
 * @brief Insert a signed integer into a bit-packed buffer.
 *
 * @param buf       Destination buffer (must have enough bytes allocated).
 * @param bitOffset Bit offset from start of buffer.
 * @param bitWidth  Number of bits to write (1..64).
 * @param value     The value to pack (truncated to bitWidth bits).
 */
void histV4BitInsert(uint8_t *buf, size_t bitOffset, uint8_t bitWidth, int64_t value);

/* ============================================================================
 * ZIGZAG VARINT
 *
 * Lived in HistoryCodec.cpp until the v2/v3 codec was removed; they were the
 * only part of it V4 ever used, so they moved here with it.
 * ============================================================================ */

/** Write int32 as zigzag varint. Returns bytes written (1..5). */
size_t writeVarintZ(int32_t v, uint8_t* buf);

/** Read zigzag varint. Returns bytes consumed (0 = error/truncated). */
size_t readVarintZ(const uint8_t* buf, size_t bufLen, int32_t& out);
