/**
 * @file HistoryCodec.h
 * @brief Encoder/decoder for binary history v2 (delta + sensor-mask + anchor).
 * @details Format replaces the fixed 40 B/record of v1 with:
 * - 16 B header per file (magic SIM2 + version + anchorPeriod)
 * - 1 anchor (full 40 B record) every N=60 records
 * - 59 variable deltas between anchors (typical ~7-19 B each)
 * - 3-byte field mask (18 bits: 2 ambient + 16 sensor slots)
 *
 * Typical compression 3-4x for common usage (few active sensors +
 * small temperature variation between samples). Reader is linear:
 * fast decode (varint + zigzag), compatible with chunked-read.
 *
 * No backward compatibility: v1 files (28 B fixed) need to be
 * converted via tools/history_v1_to_v2.py before upload.
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs.h"

/* ======================================================================== */
/* V2 FILE FORMAT */
/* ======================================================================== */

constexpr char HIST_V2_MAGIC[4] = {'S','I','M','2'};
constexpr uint16_t HIST_V2_VERSION = 0x0002;
constexpr uint16_t HIST_V2_ANCHOR_PERIOD = 60; /* 1 anchor + 59 deltas (= 1 hour @ 1 min) */
constexpr size_t HIST_V2_HEADER_SIZE = 16;
constexpr size_t HIST_V2_MAX_DELTA_SIZE = 62; /* worst-case: 3B mask + 5B Δepoch + 18*3B varints */

struct __attribute__((packed)) HistoryFileHeaderV2 {
 char magic[4]; /* "SIM2" */
 uint16_t version; /* 0x0002 */
 uint16_t anchorPeriod; /* 60 */
 uint32_t flags; /* reserved, 0 */
 uint32_t recordCount; /* optional, 0 = unknown */
};
static_assert(sizeof(HistoryFileHeaderV2) == HIST_V2_HEADER_SIZE,
 "HistoryFileHeaderV2 must be 16 bytes");

/* ======================================================================== */
/* CODEC STATE */
/* ======================================================================== */

/** Holds the last valid value of each field between consecutive records.
 * fieldHasValid[0]=ambientTemp, [1]=ambientHum, [2..17]=sensors[0..15].
 * When false, the next delta with bit set encodes the ABSOLUTE value
 * (not delta). */
struct HistoryCodecState {
 BinaryHistoryRecord lastValid;
 bool fieldHasValid[2 + MAX_SENSORS]; /* 18 bools (2 ambient + 16 slots) */
 uint16_t recordsSinceAnchor;
 bool initialized;
};

void historyCodecReset(HistoryCodecState& s);

/* ======================================================================== */
/* ENCODER / DECODER */
/* ======================================================================== */

/** Encode a record. Automatically decides between anchor (40 B fixed) or
 * variable delta based on recordsSinceAnchor.
 *
 * @param rec Input record (in-memory uncompressed).
 * @param s Encoder state (updated in-place).
 * @param buf Output buffer (must have >= HIST_V2_MAX_DELTA_SIZE bytes or
 * sizeof(BinaryHistoryRecord) — whichever is larger).
 * @param bufSize Buffer size.
 * @param outIsAnchor (out, optional) true if this record was emitted as
 * anchor.
 * @return Bytes written to buffer, or 0 on error (buffer too small).
 */
size_t historyEncodeRecord(const BinaryHistoryRecord& rec,
 HistoryCodecState& s,
 uint8_t* buf, size_t bufSize,
 bool* outIsAnchor = nullptr);

/** Decode a record. Caller informs whether the next record is an anchor (based
 * on the file counter: 1st, 61st, 121st, etc).
 *
 * @param buf Bytes read from file (enough for 1 record).
 * @param bufLen Buffer size.
 * @param s Decoder state (updated in-place).
 * @param outRec Reconstructed record.
 * @param isAnchor true if this record is an anchor (40 B fixed).
 * @return Bytes consumed, or 0 on error (truncated buffer).
 */
size_t historyDecodeRecord(const uint8_t* buf, size_t bufLen,
 HistoryCodecState& s,
 BinaryHistoryRecord& outRec,
 bool isAnchor);

/* ======================================================================== */
/* VARINT (zigzag, 7 bits/byte) */
/* ======================================================================== */

/** Write int32 as zigzag varint. Returns bytes written (1..5). */
size_t writeVarintZ(int32_t v, uint8_t* buf);

/** Read zigzag varint. Returns bytes consumed (0 = error/truncated). */
size_t readVarintZ(const uint8_t* buf, size_t bufLen, int32_t& out);
