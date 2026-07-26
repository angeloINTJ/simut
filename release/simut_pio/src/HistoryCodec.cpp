/**
 * @file HistoryCodec.cpp
 * @brief Implementation of the v2/v3 codec (delta + sensor-mask + anchor).
 *
 * v2 format (version 0x0002): 40-byte anchor, 18-bit mask (3 bytes)
 *   Fields: ambientTemp, ambientHum, sensors[0..15]
 *
 * v3 format (version 0x0003): 74-byte anchor, 35-bit mask (5 bytes)
 *   Fields: ambientTemp, ambientHum, sensors[0..15],
 *           humidity[0..15], pressure
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
 s.fileVersion = 0; /* unknown until first anchor decode */
}

/* Total field count per version */
static inline int fieldCountV2() { return 2 + MAX_SENSORS; }          /* 18 */
static inline int fieldCountV3() { return 2 + MAX_SENSORS + MAX_SENSORS + 1; } /* 35 */
static inline int maskBytesForVersion(uint16_t ver) {
 return (ver >= HIST_V3_VERSION) ? 5 : 3;
}
static inline int fieldCountForVersion(uint16_t ver) {
 return (ver >= HIST_V3_VERSION) ? fieldCountV3() : fieldCountV2();
}

static inline void updateFieldValidityV2(HistoryCodecState& s,
 const BinaryHistoryRecord& rec) {
 s.fieldHasValid[0] = (rec.ambientTemp != HIST_NAN_SENTINEL);
 s.fieldHasValid[1] = (rec.ambientHum != HIST_NAN_SENTINEL);
 for (int i = 0; i < MAX_SENSORS; i++) {
 s.fieldHasValid[2 + i] = (rec.sensors[i] != HIST_NAN_SENTINEL);
 }
}

static inline void updateFieldValidityV3(HistoryCodecState& s,
 const BinaryHistoryRecord& rec) {
 updateFieldValidityV2(s, rec);
 for (int i = 0; i < MAX_SENSORS; i++) {
 s.fieldHasValid[18 + i] = (rec.humidity[i] != HIST_NAN_SENTINEL);
 }
 s.fieldHasValid[34] = (rec.pressure != HIST_NAN_SENTINEL);
}

/* ======================================================================== */
/* ENCODE */
/* ======================================================================== */

size_t historyEncodeRecord(const BinaryHistoryRecord& rec,
 HistoryCodecState& s,
 uint8_t* buf, size_t bufSize,
 bool* outIsAnchor) {
 /* Default to v3 if not initialized */
 if (!s.initialized && s.fileVersion == 0) {
 s.fileVersion = HIST_V3_VERSION;
 }
 uint16_t ver = s.fileVersion;
 int totalFields = fieldCountForVersion(ver);

 bool emitAnchor = !s.initialized || (s.recordsSinceAnchor >= HIST_V2_ANCHOR_PERIOD);

 if (outIsAnchor) *outIsAnchor = emitAnchor;

 if (emitAnchor) {
 if (bufSize < sizeof(BinaryHistoryRecord)) return 0;
 memcpy(buf, &rec, sizeof(rec));
 s.lastValid = rec;
 if (ver >= HIST_V3_VERSION) {
 updateFieldValidityV3(s, rec);
 } else {
 updateFieldValidityV2(s, rec);
 }
 s.initialized = true;
 s.recordsSinceAnchor = 1;
 return sizeof(BinaryHistoryRecord);
 }

 /* DELTA */
 uint8_t* p = buf;

 /* Build mask. Layout:
  * v2 (18 bits in 3 bytes):
  *   bits 0-1:  ambient
  *   bits 2-17: sensors[0..15]
  * v3 (35 bits in 5 bytes):
  *   bits 0-1:  ambient
  *   bits 2-17: sensors[0..15]
  *   bits 18-33: humidity[0..15]
  *   bit 34:     pressure
  */
 uint64_t mask = 0; /* 64-bit to safely hold 35 bits */
 if (rec.ambientTemp != HIST_NAN_SENTINEL) mask |= (1ull << 0);
 if (rec.ambientHum != HIST_NAN_SENTINEL) mask |= (1ull << 1);
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (rec.sensors[i] != HIST_NAN_SENTINEL) mask |= (1ull << (2 + i));
 }
 if (ver >= HIST_V3_VERSION) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (rec.humidity[i] != HIST_NAN_SENTINEL) mask |= (1ull << (18 + i));
 }
 if (rec.pressure != HIST_NAN_SENTINEL) mask |= (1ull << 34);
 }

 int maskBytes = maskBytesForVersion(ver);
 if ((size_t)(p - buf) + maskBytes > bufSize) return 0;
 for (int b = 0; b < maskBytes; b++) {
 p[b] = (uint8_t)((mask >> (b * 8)) & 0xFF);
 }
 p += maskBytes;

 /* Δepoch always present */
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

 /* Encode fields in mask order */
 if (!encField(mask & (1ull << 0), s.fieldHasValid[0], rec.ambientTemp, s.lastValid.ambientTemp)) return 0;
 if (!encField(mask & (1ull << 1), s.fieldHasValid[1], rec.ambientHum, s.lastValid.ambientHum)) return 0;
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!encField(mask & (1ull << (2 + i)), s.fieldHasValid[2 + i],
 rec.sensors[i], s.lastValid.sensors[i])) return 0;
 }
 if (ver >= HIST_V3_VERSION) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!encField(mask & (1ull << (18 + i)), s.fieldHasValid[18 + i],
 rec.humidity[i], s.lastValid.humidity[i])) return 0;
 }
 if (!encField(mask & (1ull << 34), s.fieldHasValid[34],
 rec.pressure, s.lastValid.pressure)) return 0;
 }

 /* Update state */
 s.lastValid.epoch = rec.epoch;
 if (mask & (1ull << 0)) { s.lastValid.ambientTemp = rec.ambientTemp; s.fieldHasValid[0] = true; }
 if (mask & (1ull << 1)) { s.lastValid.ambientHum = rec.ambientHum; s.fieldHasValid[1] = true; }
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (mask & (1ull << (2 + i))) {
 s.lastValid.sensors[i] = rec.sensors[i];
 s.fieldHasValid[2 + i] = true;
 }
 }
 if (ver >= HIST_V3_VERSION) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (mask & (1ull << (18 + i))) {
 s.lastValid.humidity[i] = rec.humidity[i];
 s.fieldHasValid[18 + i] = true;
 }
 }
 if (mask & (1ull << 34)) {
 s.lastValid.pressure = rec.pressure;
 s.fieldHasValid[34] = true;
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
 /* Auto-detect version from anchor size on first decode.
  * v2 anchor = 40 bytes, v3 anchor = 74 bytes.
  * Once set, version is locked for the file. */
 if (isAnchor && s.fileVersion == 0) {
 if (bufLen >= sizeof(BinaryHistoryRecord)) {
 s.fileVersion = HIST_V3_VERSION;
 } else {
 s.fileVersion = HIST_V2_VERSION;
 }
 }
 uint16_t ver = s.fileVersion;

 if (isAnchor) {
 size_t anchorSize = (ver >= HIST_V3_VERSION)
 ? sizeof(BinaryHistoryRecord) : (size_t)40;
 if (bufLen < anchorSize) return 0;
 /* For v2 anchors, only copy the v2 portion (40 bytes); remaining stays NAN */
 memset(&outRec, 0, sizeof(outRec));
 outRec.clear();
 memcpy(&outRec, buf, anchorSize);
 s.lastValid = outRec;
 if (ver >= HIST_V3_VERSION) {
 updateFieldValidityV3(s, outRec);
 } else {
 updateFieldValidityV2(s, outRec);
 }
 s.initialized = true;
 s.recordsSinceAnchor = 1;
 return anchorSize;
 }

 int maskBytes = maskBytesForVersion(ver);
 if (bufLen < (size_t)maskBytes) return 0;
 const uint8_t* p = buf;

 uint64_t mask = 0;
 for (int b = 0; b < maskBytes; b++) {
 mask |= ((uint64_t)p[b]) << (b * 8);
 }
 p += maskBytes;

 int32_t depoch = 0;
 size_t n = readVarintZ(p, bufLen - (size_t)(p - buf), depoch);
 if (n == 0) return 0;
 p += n;
 outRec.epoch = (uint32_t)((int32_t)s.lastValid.epoch + depoch);

 /* Init all fields to NAN, then decode present ones */
 outRec.clear();
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

 int16_t tmp;

 /* Ambient temp + hum */
 if (!decField(mask & (1ull << 0), s.fieldHasValid[0], s.lastValid.ambientTemp, &tmp)) return 0;
 outRec.ambientTemp = tmp;
 if (!decField(mask & (1ull << 1), s.fieldHasValid[1], s.lastValid.ambientHum, &tmp)) return 0;
 outRec.ambientHum = tmp;

 /* Sensors (temperature) */
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!decField(mask & (1ull << (2 + i)), s.fieldHasValid[2 + i],
 s.lastValid.sensors[i], &tmp)) return 0;
 outRec.sensors[i] = tmp;
 }

 /* Humidity + pressure (v3 only) */
 if (ver >= HIST_V3_VERSION) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (!decField(mask & (1ull << (18 + i)), s.fieldHasValid[18 + i],
 s.lastValid.humidity[i], &tmp)) return 0;
 outRec.humidity[i] = tmp;
 }
 if (!decField(mask & (1ull << 34), s.fieldHasValid[34],
 s.lastValid.pressure, &tmp)) return 0;
 outRec.pressure = tmp;
 }

 /* Update state */
 s.lastValid.epoch = outRec.epoch;
 if (mask & (1ull << 0)) { s.lastValid.ambientTemp = outRec.ambientTemp; s.fieldHasValid[0] = true; }
 if (mask & (1ull << 1)) { s.lastValid.ambientHum = outRec.ambientHum; s.fieldHasValid[1] = true; }
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (mask & (1ull << (2 + i))) {
 s.lastValid.sensors[i] = outRec.sensors[i];
 s.fieldHasValid[2 + i] = true;
 }
 }
 if (ver >= HIST_V3_VERSION) {
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (mask & (1ull << (18 + i))) {
 s.lastValid.humidity[i] = outRec.humidity[i];
 s.fieldHasValid[18 + i] = true;
 }
 }
 if (mask & (1ull << 34)) {
 s.lastValid.pressure = outRec.pressure;
 s.fieldHasValid[34] = true;
 }
 }

 s.recordsSinceAnchor++;
 return (size_t)(p - buf);
}
