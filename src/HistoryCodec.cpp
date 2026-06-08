/**
 * @file HistoryCodec.cpp
 * @brief Implementation of the v2 codec (delta + sensor-mask + anchor).
 */

#include "HistoryCodec.h"
#include <string.h>

/* ======================================================================== */
/* VARINT zigzag */
/* ======================================================================== */

size_t writeVarintZ(int32_t v, uint8_t* buf) {
 /* zigzag: maps signed (-1,+1,-2,+2,...) -> unsigned (1,2,3,4,...) */
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

/* ======================================================================== */
/* STATE */
/* ======================================================================== */

void historyCodecReset(HistoryCodecState& s) {
 memset(&s, 0, sizeof(s));
 s.recordsSinceAnchor = 0;
 s.initialized = false;
}

static inline void updateFieldValidity(HistoryCodecState& s,
 const BinaryHistoryRecord& rec) {
 s.fieldHasValid[0] = (rec.ambientTemp != HIST_NAN_SENTINEL);
 s.fieldHasValid[1] = (rec.ambientHum != HIST_NAN_SENTINEL);
 for (int i = 0; i < MAX_SENSORS; i++) {
 s.fieldHasValid[2 + i] = (rec.sensors[i] != HIST_NAN_SENTINEL);
 }
}

/* ======================================================================== */
/* ENCODE */
/* ======================================================================== */

size_t historyEncodeRecord(const BinaryHistoryRecord& rec,
 HistoryCodecState& s,
 uint8_t* buf, size_t bufSize,
 bool* outIsAnchor) {
 bool emitAnchor = !s.initialized || (s.recordsSinceAnchor >= HIST_V2_ANCHOR_PERIOD);

 if (outIsAnchor) *outIsAnchor = emitAnchor;

 if (emitAnchor) {
 if (bufSize < sizeof(BinaryHistoryRecord)) return 0;
 memcpy(buf, &rec, sizeof(rec));
 s.lastValid = rec;
 updateFieldValidity(s, rec);
 s.initialized = true;
 s.recordsSinceAnchor = 1; /* anchor counts as record 0; next delta will be 1 */
 return sizeof(BinaryHistoryRecord);
 }

 /* DELTA */
 uint8_t* p = buf;

 /* Mask: bit 0=amb_temp, bit 1=amb_hum, bit 2..17=sensors[0..15].
  * Uses 3 bytes (18 bits) — uint32_t to fit all 16 sensor slots. */
 uint32_t mask = 0;
 if (rec.ambientTemp != HIST_NAN_SENTINEL) mask |= (1ul << 0);
 if (rec.ambientHum != HIST_NAN_SENTINEL) mask |= (1ul << 1);
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (rec.sensors[i] != HIST_NAN_SENTINEL) mask |= (1ul << (2 + i));
 }

 if ((size_t)(p - buf) + 3 > bufSize) return 0;
 p[0] = (uint8_t)(mask & 0xFF);
 p[1] = (uint8_t)((mask >> 8) & 0xFF);
 p[2] = (uint8_t)((mask >> 16) & 0x03); /* only bits 16-17 used */
 p += 3;

 /* Δepoch always present. In normal use = configured interval (1B). */
 int32_t depoch = (int32_t)rec.epoch - (int32_t)s.lastValid.epoch;
 if ((size_t)(p - buf) + 5 > bufSize) return 0;
 p += writeVarintZ(depoch, p);

 auto encField = [&](bool present, bool hasValid, int16_t cur, int16_t last) -> bool {
 if (!present) return true;
 int32_t d = hasValid ? ((int32_t)cur - (int32_t)last) : (int32_t)cur;
 if ((size_t)(p - buf) + 5 > bufSize) return false;
 p += writeVarintZ(d, p);
 return true;
 };

 if (!encField(mask & (1ul << 0), s.fieldHasValid[0], rec.ambientTemp, s.lastValid.ambientTemp)) return 0;
 if (!encField(mask & (1ul << 1), s.fieldHasValid[1], rec.ambientHum, s.lastValid.ambientHum)) return 0;
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!encField(mask & (1ul << (2 + i)), s.fieldHasValid[2 + i],
 rec.sensors[i], s.lastValid.sensors[i])) return 0;
 }

 /* Update state only for fields actually present. */
 s.lastValid.epoch = rec.epoch;
 if (mask & (1ul << 0)) { s.lastValid.ambientTemp = rec.ambientTemp; s.fieldHasValid[0] = true; }
 if (mask & (1ul << 1)) { s.lastValid.ambientHum = rec.ambientHum; s.fieldHasValid[1] = true; }
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (mask & (1ul << (2 + i))) {
 s.lastValid.sensors[i] = rec.sensors[i];
 s.fieldHasValid[2 + i] = true;
 }
 }

 s.recordsSinceAnchor++;
 return (size_t)(p - buf);
}

/* ======================================================================== */
/* DECODE */
/* ======================================================================== */

size_t historyDecodeRecord(const uint8_t* buf, size_t bufLen,
 HistoryCodecState& s,
 BinaryHistoryRecord& outRec,
 bool isAnchor) {
 if (isAnchor) {
 if (bufLen < sizeof(BinaryHistoryRecord)) return 0;
 memcpy(&outRec, buf, sizeof(outRec));
 s.lastValid = outRec;
 updateFieldValidity(s, outRec);
 s.initialized = true;
 s.recordsSinceAnchor = 1;
 return sizeof(BinaryHistoryRecord);
 }

 if (bufLen < 3) return 0;
 const uint8_t* p = buf;

 uint32_t mask = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
 p += 3;

 int32_t depoch = 0;
 size_t n = readVarintZ(p, bufLen - (size_t)(p - buf), depoch);
 if (n == 0) return 0;
 p += n;
 outRec.epoch = (uint32_t)((int32_t)s.lastValid.epoch + depoch);

 auto decField = [&](bool present, bool hasValid, int16_t lastVal,
 int16_t* outVal) -> bool {
 if (!present) { *outVal = HIST_NAN_SENTINEL; return true; }
 int32_t d;
 size_t nn = readVarintZ(p, bufLen - (size_t)(p - buf), d);
 if (nn == 0) return false;
 p += nn;
 *outVal = hasValid ? (int16_t)((int32_t)lastVal + d) : (int16_t)d;
 return true;
 };

 int16_t tmpTemp = HIST_NAN_SENTINEL, tmpHum = HIST_NAN_SENTINEL;
 if (!decField(mask & (1ul << 0), s.fieldHasValid[0], s.lastValid.ambientTemp, &tmpTemp)) return 0;
 if (!decField(mask & (1ul << 1), s.fieldHasValid[1], s.lastValid.ambientHum, &tmpHum)) return 0;
 outRec.ambientTemp = tmpTemp;
 outRec.ambientHum = tmpHum;
 for (int i = 0; i < MAX_SENSORS; i++) {
 int16_t tmpS = HIST_NAN_SENTINEL;
 if (!decField(mask & (1ul << (2 + i)), s.fieldHasValid[2 + i],
 s.lastValid.sensors[i], &tmpS)) return 0;
 outRec.sensors[i] = tmpS;
 }

 s.lastValid.epoch = outRec.epoch;
 if (mask & (1ul << 0)) { s.lastValid.ambientTemp = outRec.ambientTemp; s.fieldHasValid[0] = true; }
 if (mask & (1ul << 1)) { s.lastValid.ambientHum = outRec.ambientHum; s.fieldHasValid[1] = true; }
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (mask & (1ul << (2 + i))) {
 s.lastValid.sensors[i] = outRec.sensors[i];
 s.fieldHasValid[2 + i] = true;
 }
 }

 s.recordsSinceAnchor++;
 return (size_t)(p - buf);
}
