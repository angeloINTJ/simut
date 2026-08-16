/**
 * @file WebManager_History.cpp
 * @brief History/log endpoints: binary history data, log viewer, screenshot, history days.
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "TouchPriority.h"
#include "FlashIrqProbe.h"
#include "backup.h" /* ota::crc32_update for screenshot_chunk */
#include <LittleFS.h>
#include <time.h>

/* The ~5.9 KB of V4 decode scratch that used to live here is gone: the V5
 * reader belongs to StorageManager, so the handlers in this file no longer
 * carry a codec, a header buffer or a refill window of their own. */

using ReadGuard = StorageManager::ReadGuard;

/* Fine-grained position trace, parked in watchdog scratch[7] — unused by
 * the HW-WDT autopsy class, whose report prints it as hp=N. TRACE_MOD says
 * which handler died; this says which STRETCH of it. Three send-path
 * parking spots (hp=720, 721, and the pre-header window) were located in
 * one bench run each with this channel after days of module-level
 * guessing. Keep stamps on the transitions a death would need localized:
 * they are single MMIO stores. */
#include <hardware/watchdog.h>
#define HPOS(v) do { watchdog_hw->scratch[7] = (uint32_t)(v); } while (0)

// Inline insertion sort (replaces std::sort, eliminates qsort ~1.4KB)
static void sortStrings(String* arr, int n, bool descending) {
 for (int i = 1; i < n; i++) {
 String key = arr[i];
 int j = i - 1;
 while (j >= 0 && (descending ? arr[j] < key : key < arr[j])) {
 arr[j + 1] = arr[j]; j--;
 }
 arr[j + 1] = key;
 }
}


/* =========================================================================== */
/* GET /api/history_multi?sensors=<csv>&range=<0..6>&end=<ep> */
/* =========================================================================== */
/* Multi-sensor version of history data endpoint. Returns 1 response with ALL
 * requested series, avoiding overhead of N sequential blocked fetches
 * guarded by the _inHistoryHandler atomic.
 *
 * Args:
 * sensors=-1,0,5 CSV of IDs (-1=ambient, 0..9=DS18B20). Default: -1.
 * range=0..6 Levels: 1h, 6h, 24h, 7d, 1M, 1Y, MAX (=all files).
 * end=<epoch> Anchor (default: now).
 *
 * JSON:
 * {"cutoff":..., "end":..., "now":...,
 * "sensors":[{"id":-1,"hwId":"AMB","name":"...","hasH":true}, ...],
 * "data":[{"t":epoch,"v":[float|null,...],"h":float}, ...],
 * "minT":..., "maxT":..., "tsMinT":..., "tsMaxT":...,
 * "rangeUsed":N}
 *
 * v[] is aligned by index with sensors[]. h only appears if ambient is
 * included and has a valid reading in the record.
 */

/* V4 history multi handler — decodes .sim4 file and emits generic JSON.
 * Called from handleApiHistoryMulti when a V4 file is detected.
 * Static helper keeps the V4 logic self-contained without header changes. */
void WebManager::handleApiHistoryMulti( ) {
 /* RAII so the early returns below (403/503) restore the caller's module. */
 LogManager::TraceScope _tHist(0, MOD_WEB_HIST);
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_HISTORY)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 if (TouchPriority::isActive( )) {
 _server->sendHeader("Retry-After", "3");
 _server->send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return;
 }
 if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
 _server->send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
 }
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
 _server->send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
 }

 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /* Three things are now owned by this handler, and every exit has to hand all
  * three back. Doing that by hand at the bottom only ever covered the exits
  * that reached the bottom — and the "extremes" tail has three returns on a
  * failed send that skipped the lot. The cost of one such abort, and an abort
  * there is routine because it is where the 15 s deadline tends to land on a
  * wide range: _inHistoryHandler stays true for the rest of the boot, so every
  * later /api/history_multi answers 503 "Already processing" and the graphs go
  * dead; setWebBusy stays true, so the display keeps its "web busy" overlay
  * and TOUCH STAYS BLOCKED; and _handlerDeadline never returns to the
  * caller's value.
  *
  * A destructor cannot be skipped by a return, so ownership is expressed
  * there instead — the reasoning behind Core1FlashPause and RenderGuard. A
  * local class inside a member function keeps the class's access rights. */
 struct HistUnwind {
  WebManager* w;
  uint32_t saved;
  ~HistUnwind( ) {
   /* Tried and REVERTED 2026-08-10: draining here (the chunked twin of the
    * safeStreamFile fix, on the theory that _finalizeResponse's terminator
    * write was the park) changed nothing — same 1 reboot in 5 windows, same
    * `C0=[WEB_POLL] hp=721`. The hypothesis is not supported, so the code is
    * not here. Do not re-add it without a measurement that moves. */
   w->_handlerDeadline = saved;
   if (w->_displayRef) w->_displayRef->setWebBusy(false);
   __atomic_store_n(&w->_inHistoryHandler, false, __ATOMIC_RELEASE);
  }
 } _unwind{this, savedDeadline};

 /* ── Parse sensors=... (CSV of IDs) ─────────────────────────────────── */
 int sensorIds[MAX_SENSORS]; /* up to 16 slots */
 int sensorCount = 0;
 String sArg = _server->arg("sensors"); /* empty = "pick a default below" */
 {
 int start = 0;
 while (start < (int)sArg.length( ) && sensorCount < MAX_SENSORS) {
 int comma = sArg.indexOf(',', start);
 String tok = (comma < 0) ? sArg.substring(start) : sArg.substring(start, comma);
 tok.trim( );
 if (tok.length( ) > 0) {
 int id = tok.toInt( );
 /* Accept 0..MAX_SENSORS-1. Filter duplicates. */
 if (id >= 0 && id < MAX_SENSORS) {
 bool dup = false;
 for (int i = 0; i < sensorCount; i++) if (sensorIds[i] == id) { dup = true; break; }
 if (!dup) {
 sensorIds[sensorCount] = id;
 sensorCount++;
 }
 }
 }
 if (comma < 0) break;
 start = comma + 1;
 }
 }
 /* No usable ?sensors= — fall back to the lowest active slot. It used to
  * default to slot 10 because that slot was the ambient sensor, so a request
  * without the parameter graphed an empty series on any board where 10 was
  * not provisioned. */
 if (sensorCount == 0) {
 const SystemConfig& cfgDef = _storageRef->getConfig( );
 for (int i = 0; i < MAX_SENSORS; i++) {
 if (cfgDef.sensors[i].active) { sensorIds[0] = i; sensorCount = 1; break; }
 }
 if (sensorCount == 0) { sensorIds[0] = 0; sensorCount = 1; }
 }

 /* ── Parse range/end ────────────────────────────────────────────────── */
 String reqRange = _server->arg("range");
 String reqEnd = _server->arg("end");

 static const time_t rangeDuration[] = { 3600, 21600, 86400, 604800, 2592000, 31536000, 0 };
 /* Decimation is now ADAPTIVE (computed after the file list is built):
  * fixed per-range factors emitted ZERO points whenever the dataset was
  * smaller than the factor (e.g. MAX=240 with 103 records). Target:
  * ~600 emitted points regardless of dataset size. */

 time_t now = _netRef->getEpoch( );
 time_t effectiveEnd = now;
 time_t cutoff = 0;
 int decimation = 1;
 time_t cutoffOverride = 0;
 /* Long ranges are drawn from block envelopes rather than sampled
  * records; settled once filesToRead is built. */
 bool useEnvelope = false;
 int rangeIdx = 2; /* Default: 24h */

 if (reqRange.length( ) > 0) {
 rangeIdx = reqRange.toInt( );
 if (rangeIdx < 0) rangeIdx = 0;
 if (rangeIdx > 6) rangeIdx = 6;
 }
 if (reqEnd.length( ) > 0) {
 effectiveEnd = (time_t)reqEnd.toInt( );
 if (effectiveEnd > now) effectiveEnd = now;
 }

 /* ?from=&to= — an explicit window, for a client that fetches a long range
  * in slices instead of one 500 KB response it cannot retry.
  *
  * `range` picks a duration backwards from `end`, which is fine for a
  * button but useless for "give me exactly this hour and nothing else".
  * With from/to the client owns the slicing, so a slice that fails is one
  * slice to redo rather than the whole graph. Both forms coexist; from/to
  * wins when present. */
 bool explicitWindow = false;
 if (_server->hasArg("from") && _server->hasArg("to")) {
 const time_t qFrom = (time_t)strtoul(_server->arg("from").c_str( ), nullptr, 10);
 const time_t qTo   = (time_t)strtoul(_server->arg("to").c_str( ), nullptr, 10);
 if (qTo > qFrom && qFrom > 0) {
 cutoffOverride = qFrom;
 effectiveEnd = (qTo > now) ? now : qTo;
 explicitWindow = true;
 }
 }
 /* decimation: adaptivo, calculado apos montar filesToRead (abaixo). */
 cutoff = explicitWindow ? cutoffOverride
        : ((rangeIdx == 6) ? 0 : (effectiveEnd - rangeDuration[rangeIdx]));


/* ── List of files to read ────────────────────────────────────────── */
 /* Everything from here to the end of the decimation estimate runs before the
  * response starts, so no SendGuard is feeding the watchdog, and none of these
  * loops calls feedWatchdog( ) or isHandlerOvertime( ) — the 6 s handler cap
  * only exists in the record loops further down. For rangeIdx >= 4 both loops
  * scale with the NUMBER OF FILES in /history (a dir walk, then one
  * open+size+close each), which on this bench is ~50 days of history. That is
  * the shape of an unfed stretch long enough to reach the bare 15 s WDT, and
  * the autopsy already places the stall in this handler and outside the send
  * path. This scope is here to prove or refute exactly that. */
 std::vector<String> filesToRead;
 /* Total bytes, gathered during the directory walk. Dir::fileSize( ) reads
  * the entry the walk already fetched; LittleFS.open( ) per file costs an
  * open/close each, which over 91 days was most of a second — and it was
  * paid twice, once for the decimation estimate and once for the probe. */
 size_t histBytes = 0;
 {
 LogManager::TraceScope _tScan(0, MOD_WEB_HSCAN);
 const uint32_t scanStart = millis( );
 if (explicitWindow) {
 /* Exactly the days the window touches. A slice is usually one day or
  * less, so this is one or two files rather than the whole directory. */
 struct tm ftm; time_t cur = cutoff;
 localtime_r(&cur, &ftm);
 ftm.tm_hour = 0; ftm.tm_min = 0; ftm.tm_sec = 0;
 for (time_t day = mktime(&ftm); day <= effectiveEnd; ) {
 feedWdt( );
 struct tm dtm; localtime_r(&day, &dtm);
 char defPath[44];
 snprintf(defPath, sizeof(defPath), "%s/%04d%02d%02d%s", DIR_HISTORY,
          dtm.tm_year + 1900, dtm.tm_mon + 1, dtm.tm_mday, HISTORY_FILE_EXT);
 filesToRead.push_back(String(defPath));
 dtm.tm_mday += 1; dtm.tm_hour = 0; dtm.tm_min = 0; dtm.tm_sec = 0;
 const time_t next = mktime(&dtm);
 if (next <= day) break;            /* guard against a stuck mktime */
 day = next;
 if (filesToRead.size( ) > 40) break;
 }
 } else if (rangeIdx >= 4) {
 /* 1M, 1Y, MAX: list ALL files in directory (filter by epoch
 * later). Avoids iterating 365x exists( ) in vain. */
 ReadGuard rg(_storageRef);
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 /* Each next( ) walks lfs metadata off flash, and this runs once per FILE in
  * /history. feedWdt( ) and not feedWatchdog( ): the latter also fires the
  * light-yield, which allocates and can reach saveConfiguration( ) — a flash
  * write, started while we hold _fsReadMutex. */
 feedWdt( );
 if (dir.fileName( ).endsWith(HISTORY_FILE_EXT)) {
 filesToRead.push_back(String(DIR_HISTORY) + "/" + dir.fileName( ));
 histBytes += (size_t)dir.fileSize( );
 }
 }
 sortStrings(filesToRead.data( ), (int)filesToRead.size( ), false); /* YYYYMMDD ascending */
 } else {
 int daysToLoad = 1;
 switch (rangeIdx) {
 case 0: case 1: case 2: daysToLoad = 1; break;
 case 3: daysToLoad = 7; break;
 }
 /* Cross-midnight: may need +1 day */
 if (rangeIdx <= 2) {
 struct tm etm; localtime_r(&effectiveEnd, &etm);
 etm.tm_hour = 0; etm.tm_min = 0; etm.tm_sec = 0;
 time_t eMidnight = mktime(&etm);
 if (cutoff < eMidnight) daysToLoad++;
 }
 for (int d = daysToLoad - 1; d >= 0; d--) {
 time_t targetDay = effectiveEnd - (d * 86400);
 struct tm timeinfo; localtime_r(&targetDay, &timeinfo);
 char defPath[40];
 /* One extension. This used to push .bin AND .sim4 for every day
  * because the writer had moved to V4 while this list still named the
  * old one — ranges below 1M then read zero files. */
 snprintf(defPath, sizeof(defPath), "%s/%04d%02d%02d%s",
 DIR_HISTORY,
 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
 HISTORY_FILE_EXT);
 filesToRead.push_back(String(defPath));
 }
 }

 /* ── Adaptive decimation: target ~600 emitted points ────────────────
  * Estimate the record count from the total file bytes (~9 B/record
  * averaged over deltas + periodic anchors, headers ignored). Small
  * datasets stream whole (decimation 1); two months of 1-min data
  * stream at ~600 points. Replaces the fixed per-range table that
  * emitted ZERO points when the dataset was smaller than the factor. */
 {
 /* The estimate only exists to pick decimation and the read path. A
  * request that states ?mode= has already picked both, so sizing every
  * file for it is pure cost — and it is the sliced client that sends
  * ?mode=, once per slice, which is exactly where it hurts. The probe
  * still needs the number, so it is not skipped there. */
 const bool needEstimate = (_server->arg("probe") == "1") || !_server->hasArg("mode");
 if (histBytes == 0 && needEstimate) {
 ReadGuard rg(_storageRef);
 for (size_t fi = 0; fi < filesToRead.size( ); fi++) {
 feedWdt( );
 File hf = LittleFS.open(filesToRead[fi], "r");
 if (hf) { histBytes += (size_t)hf.size( ); hf.close( ); }
 }
 }
 /* Bytes per record, measured on real V5 files: 5.38 at 11 channels and
	  * 5.7 at 6, against the 9 this assumed from the V4 era. Underestimating
	  * records underestimates the payload, and a client sizing its slices from
	  * that asked for windows nearly twice as big as it meant to. */
	 size_t estRecs = histBytes / WEB_HISTORY_BYTES_PER_RECORD;
 decimation = (int)(estRecs / 600);
 if (decimation < 1) decimation = 1;
 
 /* ── Envelope vs decode (§10, R6) ────────────────────────────────
  * Sampling every Nth record is how the chart used to fit a month on
  * screen, and it drops whatever falls between the samples: a
  * one-minute spike over a month had about a 1-in-72 chance of being
  * drawn. A V5 block header already carries the true min and max of
  * every channel over its hour, so a long range emits those instead —
  * two points per block, no payload read at all. Peaks cannot vanish,
  * because the extreme IS the point.
  *
  * The threshold is exactly where decimation would start discarding
  * records. Below it the range streams whole and nothing is lost. */
 useEnvelope = (decimation > 1);
 }

 /* ?mode=decode|envelope overrides the choice. The two paths answer the
  * same question at different cost and fidelity, and running them over
  * identical data is the only way to check either against the budgets in
  * §10 — or to show that the envelope really keeps the peaks a decimated
  * decode drops. */
 {
  const String mode = _server->hasArg("mode") ? _server->arg("mode") : String("");
  if (mode == "decode") { useEnvelope = false; decimation = 1; }
  else if (mode == "envelope") useEnvelope = true;
 }

 {
  const uint32_t scanMs = millis( ) - scanStart;
  if (scanMs > g_webHistScanMaxMs) g_webHistScanMaxMs = scanMs;
 }
 } /* end MOD_WEB_HSCAN */

 /* ?probe=1 — how big would this answer be? Files, bytes and the path that
  * would be taken, with no data at all.
  *
  * A client that wants to slice a long range has to size the slices before
  * it asks for the first one, and the only honest way to do that is from the
  * same numbers the handler itself uses to pick decimation. This costs a
  * directory walk and a size( ) per file — the answer is a few hundred bytes
  * and arrives in milliseconds, against the ~500 KB the real query returns. */
 /* A response that streams for ten seconds is a Core-0 loop that spends ten
  * seconds inside sendContent, and the hardware watchdog ceiling is 8.4 s of
  * unfed loop. One such response survives on the SendGuard feeding; two or
  * three queued behind each other do not — measured on the bench with three
  * concurrent MAX requests, which is what a browser produces when the page
  * keeps polling while the chart downloads. The autopsy lands in
  * MOD_WEB_SEND every time.
  *
  * So an answer this big is refused rather than streamed. The client is
  * told what it would have cost and asked to come back in slices, which is
  * a path it already has (?from=&to=) and already prefers. A slice is ~48 KB
  * and under a second, and any number of those queued is harmless. */
 const bool sliceRequired = !explicitWindow && (_server->arg("probe") != "1")
                            && histBytes > 0
                            && (histBytes / WEB_HISTORY_BYTES_PER_RECORD
                                / (decimation > 0 ? decimation : 1))
                               * WEB_HISTORY_BYTES_PER_POINT > WEB_HISTORY_SINGLE_MAX;

 if (_server->arg("probe") == "1" || sliceRequired) {
  const size_t probeBytes = histBytes;
  /* MAX means "everything", and the handler expresses that as cutoff = 0.
   * That is fine for a single response — the record filter never fires —
   * but a client that slices by time would start at 1970 and walk half a
   * century of empty windows to reach the data. The oldest file name IS
   * the start of the data, so say so. */
  time_t probeCutoff = cutoff;
  if (probeCutoff == 0 && !filesToRead.empty( )) {
   const String& oldest = filesToRead.front( );
   const int slash = oldest.lastIndexOf('/');
   const String stem = (slash >= 0) ? oldest.substring(slash + 1) : oldest;
   if (stem.length( ) >= 8) {
    struct tm ftm; memset(&ftm, 0, sizeof(ftm));
    ftm.tm_year = stem.substring(0, 4).toInt( ) - 1900;
    ftm.tm_mon  = stem.substring(4, 6).toInt( ) - 1;
    ftm.tm_mday = stem.substring(6, 8).toInt( );
    ftm.tm_isdst = -1;
    const time_t midnight = mktime(&ftm);
    if (midnight > 0) probeCutoff = midnight;
   }
  }
  /* An envelope answer is 2 points per block; a decode answer is one per
   * surviving record. Both are estimates from file bytes, which is what
   * the decimation heuristic already trusts. */
  const unsigned long estRecs   = (unsigned long)(probeBytes / WEB_HISTORY_BYTES_PER_RECORD);
  const unsigned long estBlocks = estRecs / H5_BLOCK_MAX_RECORDS + 1;
  const unsigned long estPoints = useEnvelope ? (estBlocks * 2)
                                              : (estRecs / (decimation > 0 ? decimation : 1));
  char buf[288];
  snprintf(buf, sizeof(buf),
   "{\"probe\":1,\"sliceRequired\":%u,\"files\":%u,\"bytes\":%lu,\"estRecords\":%lu,"
   "\"estBlocks\":%lu,\"estPoints\":%lu,\"estPayload\":%lu,"
   "\"path\":\"%s\",\"cutoff\":%lu,\"end\":%lu}",
   sliceRequired ? 1u : 0u,
   (unsigned)filesToRead.size( ), (unsigned long)probeBytes, estRecs,
   estBlocks, estPoints,
   estPoints * WEB_HISTORY_BYTES_PER_POINT,
   useEnvelope ? "envelope" : "decode",
   (unsigned long)probeCutoff, (unsigned long)effectiveEnd);
  /* Keep-alive: the previous response's unread tail can hold the buffer,
   * and _server->send( ) writes headers straight into lwIP. */
  if (waitSendRoom(sizeof(buf) + 256, "hist/probe"))
   _server->send(200, "application/json", buf);
  return;                                /* ~HistUnwind does the unwind */
 }

 /* ── Accumulated stats (T of set, H of set) ─────────────────────────
  * Both are gathered PRE-decimation, over every record in range, so the
  * numbers do not depend on how many points the chart happens to draw.
  * The page used to take T from here and compute H in the browser from the
  * decimated series, which reported two different things under one label:
  * the true extreme for T and merely the largest surviving sample for H. */
 /* Extremes per channel. Was one named pair per quantity, with pressure simply
  * missing — the page drew a pressure series and had nothing to put above it.
  * An array indexed by channel means a new quantity gets its extremes for free.
  *
  * A separate seen[] rather than a magic initial value: any sentinel has to be
  * outside the channel's range to work, which silently makes "no reading" and
  * "reading outside the expected range" the same answer. The old code used
  * ±1000 for °C and would have needed a different one per quantity anyway. */
 float realMin[MAX_SENSOR_CHANNELS], realMax[MAX_SENSOR_CHANNELS];
 bool chSeen[MAX_SENSOR_CHANNELS];
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 realMin[c] = 0.0f; realMax[c] = 0.0f; chSeen[c] = false;
 }
 time_t tsRealMinT = 0, tsRealMaxT = 0;
 const SystemConfig& cfgRef = _storageRef->getConfig( );

 /* ── Response: header + sensors[] + data[] streaming ────────────────── */
 HPOS(5);
 if (!waitSendRoom(1024, "hist/hdr")) {
  return;                                /* ~HistUnwind does the unwind */
 }
 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 HPOS(6);
 _server->send(200, "application/json", "");
 HPOS(7);

 {
 char metaBuf[160];
 snprintf(metaBuf, sizeof(metaBuf),
 "{\"cutoff\":%lu,\"end\":%lu,\"now\":%lu,\"rangeUsed\":%d,\"sensors\":[",
 (unsigned long)cutoff, (unsigned long)effectiveEnd, (unsigned long)now, rangeIdx);
 safeSend(metaBuf);
 }

 /* sensors[] — emit metadata */
 {
 const SystemConfig& cfg = _storageRef->getConfig( );
 for (int i = 0; i < sensorCount; i++) {
 int id = sensorIds[i];
 char b[160];
 const char* hwId; const char* name; const char* type;
 bool hasH, hasP;
 {
 hwId = cfg.sensors[id].hwId;
 name = cfg.sensors[id].friendlyName;
 type = sensorTypeName((SensorType)cfg.sensors[id].sensorType);
 /* Channels come from the driver catalogue now. "type" used to be the
  * string "ambient" or "ds18b20" — a two-type world in which a BMP280
  * was just "ambient" and its pressure had nowhere to be announced. The
  * page then drew a humidity series for it (the type does claim CH_HUM)
  * that was null at every point, and no pressure series at all. */
 hasH = sensorHasChannel((SensorType)cfg.sensors[id].sensorType, CH_HUM);
 hasP = sensorHasChannel((SensorType)cfg.sensors[id].sensorType, CH_PRESS);
 }
 /* Minimal escape: double quotes become \" */
 char nameEsc[40]; size_t k = 0;
 for (size_t j = 0; name[j] && k < sizeof(nameEsc) - 2; j++) {
 if (name[j] == '"' || name[j] == '\\') { nameEsc[k++] = '\\'; }
 nameEsc[k++] = name[j];
 }
 nameEsc[k] = '\0';
 snprintf(b, sizeof(b),
 "%s{\"id\":%d,\"hwId\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"hasH\":%s,\"hasP\":%s}",
 (i == 0) ? "" : ",", id, hwId, nameEsc, type,
 hasH ? "true" : "false", hasP ? "true" : "false");
 safeSend(b);
 }
 }
 safeSend("],\"data\":[");

 bool firstPoint = true;
 static char chunkBuf[2048];
 chunkBuf[0] = '\0';
 int chunkLen = 0;
 bool aborted = false;
 int lineIdx = 0;
 uint32_t sinceBreath = 0; /* decoded records since the last respiro (both paths) */
 HPOS(1);
 unsigned filesOpened = 0, recsDecoded = 0; /* diagnostico no metaEnd */
 /* Records the answer took from the block still open in RAM. Reported apart
  * from recsDecoded because it is the one number that says whether the tail
  * ran at all — "recs" alone cannot tell a stale answer from a live one. */
 unsigned ramRecs = 0;
 unsigned winSkips = 0; /* records decoded but outside the from/to window */
 /* Blocks actually read off flash. The §10 budgets are per block, and a
  * count derived from records/60 is wrong exactly when it matters: every
  * reboot and every schema change leaves a PARTIAL block behind. */
 unsigned blocksRead = 0;
 /* Device-side read time, send path excluded. The §10 budgets are about
  * what the firmware does with flash, and an end-to-end HTTP timing is
  * mostly Wi-Fi: a 30-day envelope ships ~280 KB, which at bench link
  * speed is seconds no matter how fast the scan was. */
 uint32_t readUs = 0;
 /* The flash half of readUs: what the block loads cost. readUs - loadUs is
  * what the decoder costs. Without the split, a budget miss in §10 cannot be
  * attributed, and two sessions have now guessed at it from totals. */
 uint32_t loadUs = 0;
 /* One bracket around the whole record loop, against the 1 062 brackets that
  * make up readUs: the difference is what micros( ) itself costs, and it is
  * the only way to know that readUs is measuring the work and not itself. */
 uint32_t loopUs = 0;
 /* ?emit=0 decodes and measures without formatting or sending a single
  * point. Same flash reads, same decodes, none of the JSON — which is how
  * you find out whether a record costs what it costs because of the decoder
  * or because of what runs between two decodes. */
 const bool emitPoints = _server->arg("emit") != "0";
 unsigned rejected = 0;
 const char* pathUsed = "decode";

 for (size_t fi = 0; fi < filesToRead.size( ) && !aborted; fi++) {
 String path = filesToRead[fi];
 File f;
 bool fileOk = false;
 {
 ReadGuard rg(_storageRef);
 if (LittleFS.exists(path)) {
 f = LittleFS.open(path, "r");
 fileOk = (bool)f;
 }
 }
 if (!fileOk) continue;
 filesOpened++;

	 { ReadGuard rg(_storageRef); f.close( ); }

	 /* ── V5 decode + emit ─────────────────────────────────────────────
	  * One reader, owned by StorageManager. The measurement key the page
	  * consumes ("tSTM0009") is rebuilt here from the live config: a V5
	  * descriptor carries slot*MAX_SENSOR_CHANNELS + channel, and the
	  * hwId for that slot is the one the UI is showing right now. */
	 /* The envelope path never reads a payload, so verifying payload CRCs
	  * would triple its flash reads for a check on bytes it discards. The
	  * decode path always verifies. */
	 bool opened = false;
	 { ReadGuard rg(_storageRef); opened = _storageRef->h5OpenDay(path, !useEnvelope); }
	 if (!opened) continue;

	 /* Resolved ONCE per file, as the V4 path had to learn the hard way:
	  * doing this per record put a string search inside the tightest loop
	  * in the handler. */
	 const uint8_t CH_NONE = 0xFF;
	 uint8_t chOf[H5_MAX_CHANNELS];       /* channel, or CH_NONE if unselected */
	 uint8_t chAny[H5_MAX_CHANNELS];      /* channel regardless of selection   */
	 char    keyOf[H5_MAX_CHANNELS][32];
	 float   scaleOf[H5_MAX_CHANNELS];
	 uint8_t nCh = 0;
	 {
	 const H5ChannelDesc* schema = _storageRef->h5ReaderSchema( );
	 nCh = _storageRef->h5ReaderChannels( );
	 for (uint8_t c = 0; c < nCh && schema; c++) {
	 const uint8_t slot = (uint8_t)(schema[c].id / MAX_SENSOR_CHANNELS);
	 const uint8_t ch   = (uint8_t)(schema[c].id % MAX_SENSOR_CHANNELS);
	 chOf[c] = CH_NONE;
	 chAny[c] = channelValid(ch) ? ch : CH_NONE;
	 keyOf[c][0] = '\0';
	 scaleOf[c] = powf(10.0f, (float)schema[c].scaleExp);
	 if (slot >= MAX_SENSORS || !channelValid(ch)) continue;
	 snprintf(keyOf[c], sizeof(keyOf[c]), "%c%s",
	          channelInfo(ch).letter, cfgRef.sensors[slot].hwId);
	 for (int si = 0; si < sensorCount; si++) {
	 if (sensorIds[si] == (int)slot) { chOf[c] = ch; break; }
	 }
	 }
	 }

	 int16_t vals[H5_MAX_CHANNELS];
	 uint32_t epoch = 0;
	 bool fileHasMore = true;

	 /* Blocks are one-hour hops, so a window that starts late in a long
	  * file skips straight to it instead of decoding everything before. */
	 if (cutoff > 0) { ReadGuard rg(_storageRef); _storageRef->h5SeekTo((uint32_t)cutoff); }

	 if (useEnvelope) {
	 pathUsed = "envelope";
	 /* ── Envelope path ───────────────────────────────────────────────
	  * Header only: two points per block, carrying the block's true
	  * minimum and maximum per channel. Placed at t0 and at the middle
	  * of the block so the chart draws a band rather than a line, which
	  * is what a month of one-minute data honestly is at screen
	  * resolution. Nothing here reads a payload. */
	 while (!aborted) {
	 if (isClientGone( ) || isHandlerOvertime( )) { HPOS(900); aborted = true; break; }
	 H5DataHeader hdr;
	 const int16_t *mn = nullptr, *mx = nullptr;
	 bool got = false;
	 { const uint32_t t0us = micros( );
	   ReadGuard rg(_storageRef); got = _storageRef->h5NextBlock(hdr, mn, mx);
	   /* On this path every microsecond IS a block load — no payload is read
	    * and nothing is decoded — so the two counters coincide. */
	   const uint32_t d = micros( ) - t0us; readUs += d; loadUs += d; }
	 if (!got) break;

	 const time_t bt0 = (time_t)hdr.t0;
	 if (bt0 > effectiveEnd) continue;      /* skip, not abandon — see decode */
	 /* A block whose whole hour is before the cutoff has nothing to
	  * contribute; one that straddles it still does, so it is kept. */
	 if (cutoff > 0 && bt0 + 3600 < cutoff) continue;

	 /* Extremes are exact here, and cheaper than the decode path's:
	  * they come from the header rather than from every record. */
	 for (uint8_t c = 0; c < nCh; c++) {
	 const uint8_t ch = chOf[c];
	 if (ch == CH_NONE || mn[c] == H5_NAN_SENTINEL) continue;
	 const float lo = (float)mn[c] * scaleOf[c];
	 const float hi = (float)mx[c] * scaleOf[c];
	 if (!chSeen[ch]) {
	 chSeen[ch] = true;
	 realMin[ch] = lo; realMax[ch] = hi;
	 if (ch == CH_TEMP) { tsRealMinT = bt0; tsRealMaxT = bt0; }
	 continue;
	 }
	 if (lo < realMin[ch]) { realMin[ch] = lo; if (ch == CH_TEMP) tsRealMinT = bt0; }
	 if (hi > realMax[ch]) { realMax[ch] = hi; if (ch == CH_TEMP) tsRealMaxT = bt0; }
	 }

	 for (uint8_t half = 0; half < 2; half++) {
	 const int16_t* band = half ? mx : mn;
	 const time_t ts = bt0 + (half ? 1800 : 0);
	 if (ts > effectiveEnd) continue;
	 char ptBuf[1024]; int pp = 0;
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp,
	 "%s{\"t\":%lu,\"v\":{", firstPoint ? "" : ",", (unsigned long)ts);
	 bool fk = true;
	 for (uint8_t c = 0; c < nCh; c++) {
	 if (band[c] == H5_NAN_SENTINEL || keyOf[c][0] == '\0') continue;
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp, "%s\"%s\":%.2f",
	 fk ? "" : ",", keyOf[c], (double)((float)band[c] * scaleOf[c]));
	 fk = false;
	 }
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp, "}}");
	 if (pp >= (int)sizeof(ptBuf)) pp = (int)sizeof(ptBuf) - 1;
	 if (chunkLen + pp >= (int)WEB_STREAM_CHUNK_SOFT) {
	 if (!safeSend(chunkBuf)) { aborted = true; break; }
	 chunkBuf[0] = '\0'; chunkLen = 0;
	 streamBreath( );
	 }
	 memcpy(chunkBuf + chunkLen, ptBuf, (size_t)pp + 1);
	 chunkLen += pp;
	 firstPoint = false;
	 }
	 recsDecoded += hdr.pre.a;
	 blocksRead++;
	 if (++sinceBreath >= WEB_STREAM_BREATH_RECORDS) { sinceBreath = 0; feedWatchdog( ); }
	 }
	 rejected += _storageRef->h5ReaderRejected( );
	 { ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }
	 streamBreath( );
	 continue;
	 }

	 const uint32_t loop0us = micros( );
	 while (fileHasMore && !aborted) {
	 if (isClientGone( ) || isHandlerOvertime( )) { HPOS(900); aborted = true; break; }
	 {
	 /* One lock per BLOCK, not per record (§10). Records come out of the
	  * block already in RAM; the mutex is only owed to the flash read that
	  * brings the next block in. The shape inherited from V4 took and
	  * returned the mutex 60 times per block to no purpose. */
	 const uint32_t t0us = micros( );
	 HPOS(301);
	 bool got = _storageRef->h5DecodeNext(epoch, vals);
	 if (!got) {
	 const uint32_t l0us = micros( );
	 ReadGuard rg(_storageRef);
	 HPOS(300);
	 while (_storageRef->h5LoadNextBlock( )) {
	 blocksRead++;
	 /* A block whose records all fail the reader's gates (schema
	  * mismatch, epoch floor, future cap) decodes to nothing and this
	  * loop moves straight to the next one — under the guard, with no
	  * feed. feedWdt and not feedWatchdog: the ReadGuard is held (see
	  * the export handler's note). */
	 feedWdt( );
	 if ((got = _storageRef->h5DecodeNext(epoch, vals))) break;
	 }
	 loadUs += micros( ) - l0us;
	 }
	 readUs += micros( ) - t0us;
	 if (!got) { fileHasMore = false; break; }
	 recsDecoded++;
	 }

	 time_t ts = (time_t)epoch;
	 /* Out-of-window records still cost a decode each, and skipping them
	  * bypassed BOTH breath sites below — an explicit from/to window that
	  * sits weeks behind the newest data decodes every earlier record in
	  * the walked files with the watchdog unfed. Same lesson the
	  * decimation skip already carries; these two exits missed it. */
	 if (cutoff > 0 && ts < cutoff) {
	 winSkips++;
	 if (++sinceBreath >= WEB_STREAM_BREATH_RECORDS) { sinceBreath = 0; feedWatchdog( ); }
	 continue;
	 }
	 /* Skip, do not abandon. Stopping the file here assumes blocks are in
	  * time order, and a single block stamped past the window then hides
	  * every block behind it — which is how seven hours of history that was
	  * on flash the whole time came to be invisible on the bench. Files are
	  * ordered in normal operation, so this costs the tail of one day. */
	 if (ts > effectiveEnd) {
	 winSkips++;
	 if (++sinceBreath >= WEB_STREAM_BREATH_RECORDS) { sinceBreath = 0; feedWatchdog( ); }
	 continue;
	 }

	 /* Stats per channel, restricted to the selected sensors. */
	 for (uint8_t c = 0; c < nCh; c++) {
	 const uint8_t ch = chOf[c];
	 if (ch == CH_NONE || vals[c] == H5_NAN_SENTINEL) continue;
	 const float v = (float)vals[c] * scaleOf[c];
	 if (!chSeen[ch]) {
	 chSeen[ch] = true;
	 realMin[ch] = v; realMax[ch] = v;
	 if (ch == CH_TEMP) { tsRealMinT = ts; tsRealMaxT = ts; }
	 continue;
	 }
	 if (v < realMin[ch]) {
	 realMin[ch] = v;
	 if (ch == CH_TEMP) tsRealMinT = ts;
	 }
	 if (v > realMax[ch]) {
	 realMax[ch] = v;
	 /* Only temperature carries a timestamp with its extreme; the page
	  * shows "when was it coldest" for T alone. */
	 if (ch == CH_TEMP) tsRealMaxT = ts;
	 }
	 }

	 lineIdx++;
	 if (lineIdx % decimation != 0 || !emitPoints) {
	 /* Decimated-out record still costs a decode; breathe every N so a
	  * high-decimation range never runs yield-free. */
	 if (++sinceBreath >= WEB_STREAM_BREATH_RECORDS) { sinceBreath = 0; feedWatchdog( ); }
	 continue;
	 }
	 sinceBreath = 0;

	 char ptBuf[1024]; int pp = 0;
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp,
	 "%s{\"t\":%lu,\"v\":{", firstPoint ? "" : ",", (unsigned long)ts);
	 bool fk = true;
	 for (uint8_t c = 0; c < nCh; c++) {
	 if (vals[c] == H5_NAN_SENTINEL || keyOf[c][0] == '\0') continue;
	 const float v = (float)vals[c] * scaleOf[c];
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp,
	 "%s\"%s\":%.2f", fk ? "" : ",", keyOf[c], (double)v);
	 fk = false;
	 }
	 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp, "}}");
	 if (pp >= (int)sizeof(ptBuf)) pp = (int)sizeof(ptBuf) - 1; /* snprintf truncation guard */

	 /* Accumulate into the shared chunk buffer, flushing SMALL packets
	  * (WEB_STREAM_CHUNK_SOFT) with a breath between — instead of one lwIP
	  * write per point. The byte stream is identical; only the
	  * (client-transparent) chunk boundaries change. */
	 if (chunkLen + pp >= (int)WEB_STREAM_CHUNK_SOFT) {
	 if (!safeSend(chunkBuf)) { aborted = true; break; }
	 chunkBuf[0] = '\0'; chunkLen = 0;
	 streamBreath( );
	 }
	 memcpy(chunkBuf + chunkLen, ptBuf, (size_t)pp + 1);
	 chunkLen += pp;
	 firstPoint = false;
	 }
	 loopUs += micros( ) - loop0us;
	 rejected += _storageRef->h5ReaderRejected( );
	 { ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }
	 streamBreath( ); /* respiro entre arquivos */
	 (void)chAny;
 }

 /* ── The hour still open in RAM ──────────────────────────────────────
  * A V5 block reaches its day file only when it seals, which at one record a
  * minute is once an hour. Everything that reads .h5 therefore trails the
  * present by up to that hour — which is why a freshly opened chart was
  * missing its last few minutes. The samples were never absent: they are held
  * in the encoder, plain rather than bit-packed, so reaching them costs a copy
  * and no decode. The /history/.wip alongside them is a crash bound, not a
  * read path — boot adopts it into the day file and nothing else opens it,
  * and it never appears in filesToRead because it carries no HISTORY_FILE_EXT.
  *
  * Nothing is emitted twice: the seal appends the block to the file and
  * empties the encoder in one non-yielding step, so a record is in exactly
  * one of the two places.
  *
  * The walk itself does not yield — the history writer runs on this same
  * core, and letting it in could seal the block mid-read — but the emit
  * between two records does, exactly as the file path's does. That is safe
  * here for the same reason it is safe there: the sampler runs only from the
  * main loop, which is blocked for as long as this handler is running. */
 if (!aborted) {
 const uint8_t ramCount = _storageRef->h5RamCount( );
 const H5ChannelDesc* ramSchema = _storageRef->getH5Schema( );
 const uint8_t ramNCh = _storageRef->getH5ChannelCount( );
 if (ramCount > 0 && ramSchema && ramNCh > 0) {
 /* Same resolution the file path does per file, against the LIVE schema:
  * the open block is encoded with the sensor set in force right now, not
  * necessarily the one the newest file on flash was written with. */
 const uint8_t CH_NONE = 0xFF;
 uint8_t chOfR[H5_MAX_CHANNELS];
 char    keyOfR[H5_MAX_CHANNELS][32];
 float   scaleOfR[H5_MAX_CHANNELS];
 for (uint8_t c = 0; c < ramNCh; c++) {
 const uint8_t slot = (uint8_t)(ramSchema[c].id / MAX_SENSOR_CHANNELS);
 const uint8_t ch   = (uint8_t)(ramSchema[c].id % MAX_SENSOR_CHANNELS);
 chOfR[c] = CH_NONE;
 keyOfR[c][0] = '\0';
 scaleOfR[c] = powf(10.0f, (float)ramSchema[c].scaleExp);
 if (slot >= MAX_SENSORS || !channelValid(ch)) continue;
 snprintf(keyOfR[c], sizeof(keyOfR[c]), "%c%s",
          channelInfo(ch).letter, cfgRef.sensors[slot].hwId);
 for (int si = 0; si < sensorCount; si++) {
 if (sensorIds[si] == (int)slot) { chOfR[c] = ch; break; }
 }
 }

 /* The envelope path replaces decimation rather than applying it, so the
  * tail follows suit: at most H5_BLOCK_MAX_RECORDS points either way. */
 const int ramDecim = useEnvelope ? 1 : (decimation > 0 ? decimation : 1);

 int16_t vals[H5_MAX_CHANNELS];
 uint32_t epoch = 0;
 for (uint8_t i = 0; i < ramCount && !aborted; i++) {
 if (isClientGone( ) || isHandlerOvertime( )) { HPOS(901); aborted = true; break; }
 if (!_storageRef->h5RamRecord(i, epoch, vals)) break;

 const time_t ts = (time_t)epoch;
 if (cutoff > 0 && ts < cutoff) { winSkips++; continue; }
 if (ts > effectiveEnd)         { winSkips++; continue; }
 ramRecs++;

 for (uint8_t c = 0; c < ramNCh; c++) {
 const uint8_t ch = chOfR[c];
 if (ch == CH_NONE || vals[c] == H5_NAN_SENTINEL) continue;
 const float v = (float)vals[c] * scaleOfR[c];
 if (!chSeen[ch]) {
 chSeen[ch] = true;
 realMin[ch] = v; realMax[ch] = v;
 if (ch == CH_TEMP) { tsRealMinT = ts; tsRealMaxT = ts; }
 continue;
 }
 if (v < realMin[ch]) { realMin[ch] = v; if (ch == CH_TEMP) tsRealMinT = ts; }
 if (v > realMax[ch]) { realMax[ch] = v; if (ch == CH_TEMP) tsRealMaxT = ts; }
 }

 /* The cadence carries on from the file loop so the tail is spaced like
  * the rest of the series — except for the newest record, which is
  * emitted whatever the decimation says. Otherwise a range decimated 40:1
  * would still leave the right edge forty minutes stale, and the right
  * edge being current is the whole point of reading RAM at all. */
 lineIdx++;
 const bool newest = (i + 1 == ramCount);
 if (!emitPoints) continue;
 if (lineIdx % ramDecim != 0 && !newest) continue;

 char ptBuf[1024]; int pp = 0;
 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp,
 "%s{\"t\":%lu,\"v\":{", firstPoint ? "" : ",", (unsigned long)ts);
 bool fk = true;
 for (uint8_t c = 0; c < ramNCh; c++) {
 if (vals[c] == H5_NAN_SENTINEL || keyOfR[c][0] == '\0') continue;
 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp, "%s\"%s\":%.2f",
 fk ? "" : ",", keyOfR[c], (double)((float)vals[c] * scaleOfR[c]));
 fk = false;
 }
 pp += snprintf(ptBuf + pp, sizeof(ptBuf) - pp, "}}");
 if (pp >= (int)sizeof(ptBuf)) pp = (int)sizeof(ptBuf) - 1;

 if (chunkLen + pp >= (int)WEB_STREAM_CHUNK_SOFT) {
 if (!safeSend(chunkBuf)) { aborted = true; break; }
 chunkBuf[0] = '\0'; chunkLen = 0;
 streamBreath( );
 }
 memcpy(chunkBuf + chunkLen, ptBuf, (size_t)pp + 1);
 chunkLen += pp;
 firstPoint = false;
 }
 /* Only when the tail really contributed: a window that ends in the past
  * walks the block and discards every record, and reporting "+ram" for
  * that would say the answer is live when it is not. */
 if (ramRecs > 0) pathUsed = useEnvelope ? "envelope+ram" : "decode+ram";
 }
 }

 if (!aborted) {
 if (chunkLen > 0) safeSend(chunkBuf);
 if (chSeen[CH_TEMP]) {
 /* Generic form first: one entry per channel that produced a reading,
  * keyed by the channel's API key. A client that iterates this needs no
  * change when a quantity is added — which is the whole point, since
  * pressure existed in the data for months with no key to arrive under. */
 if (!safeSend("],\"extremes\":{")) return;
 bool firstCh = true;
 for (uint8_t c = 0; c < MAX_SENSOR_CHANNELS; c++) {
 if (!chSeen[c]) continue;
 char e[112];
 snprintf(e, sizeof(e), "%s\"%s\":{\"min\":%.2f,\"max\":%.2f,\"unit\":\"%s\",\"label\":\"%s\"}",
          firstCh ? "" : ",", channelInfo(c).key, realMin[c], realMax[c],
          channelInfo(c).display.unit, channelInfo(c).i18nKey);
 if (!safeSend(e)) return;
 firstCh = false;
 }
 if (!safeSend("}")) return;

 /* Legacy fixed keys, kept one release so a cached page keeps working. */
 char metaEnd[448];
 char hPart[64] = "";
 if (chSeen[CH_HUM]) {
 snprintf(hPart, sizeof(hPart), ",\"minH\":%.1f,\"maxH\":%.1f",
          realMin[CH_HUM], realMax[CH_HUM]);
 }
 char pPart[64] = "";
 if (chSeen[CH_PRESS]) {
 snprintf(pPart, sizeof(pPart), ",\"minP\":%.1f,\"maxP\":%.1f",
          realMin[CH_PRESS], realMax[CH_PRESS]);
 }
 HPOS(500);
 snprintf(metaEnd, sizeof(metaEnd),
 ",\"minT\":%.2f,\"maxT\":%.2f,\"tsMinT\":%lu,\"tsMaxT\":%lu%s%s,"
 "\"filesTried\":%u,\"filesOpened\":%u,\"recs\":%u,\"ram\":%u,\"blocks\":%u,"
 "\"path\":\"%s\",\"readMs\":%.1f,\"loadMs\":%.1f,\"loopMs\":%.1f,\"rejected\":%u,\"winSkips\":%u}",
 realMin[CH_TEMP], realMax[CH_TEMP],
 (unsigned long)tsRealMinT, (unsigned long)tsRealMaxT, hPart, pPart,
 (unsigned)filesToRead.size( ), filesOpened, recsDecoded, ramRecs, blocksRead,
 pathUsed, (double)readUs / 1000.0, (double)loadUs / 1000.0,
 (double)loopUs / 1000.0, rejected, winSkips);
 safeSend(metaEnd);
 } else {
 char metaEnd[208];
 snprintf(metaEnd, sizeof(metaEnd),
 "],\"filesTried\":%u,\"filesOpened\":%u,\"recs\":%u,\"ram\":%u,\"blocks\":%u,"
 "\"path\":\"%s\",\"readMs\":%.1f,\"loadMs\":%.1f,\"loopMs\":%.1f,\"rejected\":%u}",
 (unsigned)filesToRead.size( ), filesOpened, recsDecoded, ramRecs, blocksRead,
 pathUsed, (double)readUs / 1000.0, (double)loadUs / 1000.0,
 (double)loopUs / 1000.0, rejected);
 safeSend(metaEnd);
 }
 safeSend("");
 }
 /* Loop-top aborts (client gone / overtime) break without passing through
  * the funnel again — apply the same hard-close the funnel's aborts get. */
 if (aborted) dropAbortedStream("hm");
 /* deadline, web-busy and the _inHistoryHandler latch are released by
  * ~HistUnwind, which the tail's early returns cannot skip. */
}


/* =========================================================================== */
/* GET /api/export/history.bin?from=<epoch>&to=<epoch> */
/* =========================================================================== */
/* Emits .simx bundle kind='H' (CRC32 trailer) for the browser to expand into CSV
 * locally. Hard cap of 31 days. PAYLOAD = N x BinaryHistoryRecord (70 B
 * packed) raw, without reformatting. Sensor filtering is on the client.
 *
 * The record is built from the day's .sim4 — the bundle stays slot-indexed
 * because SENSOR_TABLE names the slots and the browser expands by them.
 *
 * Format (all LE):
 * HEADER (32 B): "SIMX" | ver=1 | kind='H' | rsv | recSize=70 | rsv |
 * rangeFrom u32 | rangeTo u32 | sensorTblSize u32 | rsv x2
 * SENSOR_TABLE (variable): per active slot: idx u8, hwidLen u8, hwid[],
 * friendlyLen u8, friendly[]
 * PAYLOAD (variable): N x BinaryHistoryRecord (70 B each)
 * TRAILER (4 B): crc32 u32 (over HEADER+TABLE+PAYLOAD)
 */
namespace {
constexpr uint32_t SIMX_MAX_RANGE_SECS = 31u * 86400u; /* cap 31 days */
struct __attribute__((packed)) SimxHeader {
 char magic[4]; /* "SIMX" */
 uint8_t version; /* 1 */
 uint8_t kind; /* 'H' history, 'L' logs */
 uint16_t reserved0;
 uint16_t recordSize; /* 70 (history) or 12 (logs) — the reader uses THIS,
                       * not a hardcoded constant */
 uint16_t reserved1;
 uint32_t rangeFrom;
 uint32_t rangeTo;
 uint32_t sensorTableSize;
 uint32_t reserved2;
 uint32_t reserved3;
};
static_assert(sizeof(SimxHeader) == 32, "SimxHeader must be 32 bytes");
}

/* =========================================================================== */
/* GET /api/history/open — the hour still open in RAM, as a V5 stream */
/* =========================================================================== */
void WebManager::handleApiHistoryOpen( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_HISTORY)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 /* Nothing open is a normal answer, not an error — a device in the minute
  * after a seal has an empty encoder. 204 rather than an empty 200 body:
  * in HTTP chunked a zero-length chunk IS the terminator, and answering
  * "nothing" by starting a chunked response and sending no bytes is the
  * same trap that once truncated /api/config. */
 if (_storageRef->h5RamCount( ) == 0) { _server->send(204, "application/octet-stream", ""); return; }

 /* At most H5_BLOCK_MAX_BYTES + a schema chunk — a couple of KiB, once per
  * export. No HeavyTaskGuard: this touches no flash and takes no lock, it
  * copies out of the encoder. */
 _server->sendHeader("Content-Disposition", "attachment; filename=\"open.h5\"");
 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 _server->send(200, "application/octet-stream", "");

 /* sealStream hands the payload over in 64 B windows. Sending each one as
  * its own chunk would be ~34 tiny lwIP writes for 2 KiB, the same PBUF
  * pressure the point-by-point graph send was fixed for; accumulating to
  * WEB_STREAM_CHUNK_SOFT makes it four. A local struct so the sink keeps
  * this member function's access to safeSend. */
 struct Sink {
  WebManager* w;
  uint8_t buf[WEB_STREAM_CHUNK_SOFT];
  size_t len;
  bool ok;
  static bool write(void* ctx, const uint8_t* d, size_t n) {
   Sink* s = (Sink*)ctx;
   while (n > 0) {
    const size_t room = sizeof(s->buf) - s->len;
    const size_t take = (n < room) ? n : room;
    memcpy(s->buf + s->len, d, take);
    s->len += take; d += take; n -= take;
    if (s->len == sizeof(s->buf) && !s->flush( )) return false;
   }
   return true;
  }
  bool flush( ) {
   if (len == 0) return true;             /* never an empty chunk */
   ok = w->safeSend((const char*)buf, len);
   len = 0;
   return ok;
  }
 } sink{this, {}, 0, true};

 const size_t n = _storageRef->h5StreamOpenBlock(&Sink::write, &sink);
 if (n == 0 || !sink.flush( )) { dropAbortedStream("ho"); return; }
 safeSend("");
}


void WebManager::handleApiExportHistory( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_HISTORY)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 if (!_server->hasArg("from") || !_server->hasArg("to")) {
 _server->send(400, "application/json", "{\"error\":\"Missing from/to params\"}"); return;
 }
 uint32_t rangeFrom = (uint32_t)strtoul(_server->arg("from").c_str( ), nullptr, 10);
 uint32_t rangeTo = (uint32_t)strtoul(_server->arg("to").c_str( ), nullptr, 10);
 if (rangeFrom == 0 || rangeTo == 0 || rangeFrom >= rangeTo) {
 _server->send(400, "application/json", "{\"error\":\"Invalid range\"}"); return;
 }
 if (rangeTo - rangeFrom > SIMX_MAX_RANGE_SECS) {
 _server->send(400, "application/json", "{\"error\":\"Range exceeds 31 days\"}"); return;
 }

 if (TouchPriority::isActive( )) {
 _server->sendHeader("Retry-After", "3");
 _server->send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}"); return;
 }
 if (__atomic_exchange_n(&_inHistoryHandler, true, __ATOMIC_ACQ_REL)) {
 _server->send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
 }
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
 _server->send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
 }

 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /* Build SENSOR_TABLE in RAM buffer (one entry per active slot; there is no
 * ambient pseudo-slot any more). Worst case: 16 slots x (1+1+16+1+32) =
 * 816 B — the old 640 B cap was sized for 11 entries and silently dropped
 * the tail, so the browser could not name the last sensors. */
 uint8_t sensorTbl[832];
 size_t sensorTblLen = 0;
 {
 const SystemConfig& cfg = _storageRef->getConfig( );
 auto appendSensor = [&](uint8_t idx, const char* hwId, const char* friendly) {
 size_t hwLen = hwId ? strnlen(hwId, 16) : 0;
 size_t frLen = friendly ? strnlen(friendly, 32) : 0;
 size_t need = 1 + 1 + hwLen + 1 + frLen;
 if (sensorTblLen + need > sizeof(sensorTbl)) return;
 sensorTbl[sensorTblLen++] = idx;
 sensorTbl[sensorTblLen++] = (uint8_t)hwLen;
 memcpy(sensorTbl + sensorTblLen, hwId, hwLen); sensorTblLen += hwLen;
 sensorTbl[sensorTblLen++] = (uint8_t)frLen;
 memcpy(sensorTbl + sensorTblLen, friendly, frLen); sensorTblLen += frLen;
 };
 for (int i = 0; i < MAX_SENSORS; i++) {
 const SensorRecord& s = cfg.sensors[i];
 if (!s.active) continue;
 appendSensor((uint8_t)i, s.hwId, s.friendlyName);
 }
 /* V4: no special ambient slot — each sensor indexed independently */
 }

 /* Streaming CRC32 accumulator (covers HEADER + TABLE + PAYLOAD; trailer is the CRC). */
 uint32_t crc = crc32_init( );

 _server->sendHeader("Content-Disposition", "attachment; filename=\"simut_history.simx\"");
 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 _server->send(200, "application/octet-stream", "");

 /* Emit HEADER */
 SimxHeader hdr = {};
 memcpy(hdr.magic, "SIMX", 4);
 hdr.version = 0x01;
 hdr.kind = 'H';
 hdr.recordSize = (uint16_t)sizeof(BinaryHistoryRecord); /* 70 */
 hdr.rangeFrom = rangeFrom;
 hdr.rangeTo = rangeTo;
 hdr.sensorTableSize = (uint32_t)sensorTblLen;
 crc = crc32_update(crc, (const uint8_t*)&hdr, sizeof(hdr));
 safeSend((const char*)&hdr, sizeof(hdr));

 /* Emit SENSOR_TABLE */
 if (sensorTblLen > 0) {
 crc = crc32_update(crc, sensorTbl, sensorTblLen);
 safeSend((const char*)sensorTbl, sensorTblLen);
 }

 /* Iterate history files in range. Day-aligned in LOCALTIME (files
 * are named YYYYMMDD by local date, not UTC) — fix for the bug where
 * the current day was left out when user is in timezone != UTC. */
 bool aborted = false;
 time_t fromT = (time_t)rangeFrom;
 time_t toDayEnd = (time_t)rangeTo;
 struct tm tFrom; localtime_r(&fromT, &tFrom);
 tFrom.tm_hour = 0; tFrom.tm_min = 0; tFrom.tm_sec = 0;
 time_t dayStart = mktime(&tFrom);
 while (dayStart <= toDayEnd && !aborted) {
 /* Calculate nextDay BEFORE any continue — guard against infinite
 * loop when file doesn't exist. */
 struct tm dtNext; localtime_r(&dayStart, &dtNext);
 dtNext.tm_mday += 1;
 dtNext.tm_hour = 0; dtNext.tm_min = 0; dtNext.tm_sec = 0;
 time_t nextDay = mktime(&dtNext);
 time_t curDay = dayStart;
 dayStart = nextDay; /* advance ALWAYS — subsequent continues are safe */

 /* Cooperative abort: client gone or deadline exceeded */
 if (isClientGone( ) || isHandlerOvertime( )) { aborted = true; break; }

	 struct tm dtm; localtime_r(&curDay, &dtm);
	 char dayPath[44];
	 snprintf(dayPath, sizeof(dayPath), "%s/%04d%02d%02d%s",
	          DIR_HISTORY,
	          dtm.tm_year + 1900, dtm.tm_mon + 1, dtm.tm_mday,
	          HISTORY_FILE_EXT);

	 File f;
	 bool fileOk = false;
	 {
	  ReadGuard rg(_storageRef);
	  if (LittleFS.exists(dayPath)) { f = LittleFS.open(dayPath, "r"); fileOk = (bool)f; }
	 }
	 if (!fileOk) continue;

	 /* V5 -> flat record.
	  *
	  * The bundle stays slot-indexed because that is what the sensor table
	  * above describes and what the browser expands. With V5 the slot IS
	  * the descriptor id (slot*MAX_SENSOR_CHANNELS + channel), so the hwId
	  * string search the V4 path ran for every measurement of every record
	  * is gone. */
	 { ReadGuard rg(_storageRef); f.close( ); }
	 bool opened = false;
	 { ReadGuard rg(_storageRef); opened = _storageRef->h5OpenDay(dayPath); }
	 if (!opened) continue;

	 uint8_t slotOf[H5_MAX_CHANNELS], chOf[H5_MAX_CHANNELS];
	 float   scaleOf[H5_MAX_CHANNELS];
	 uint8_t nCh = 0;
	 {
	  const H5ChannelDesc* schema = _storageRef->h5ReaderSchema( );
	  nCh = _storageRef->h5ReaderChannels( );
	  for (uint8_t c = 0; c < nCh && schema; c++) {
	   slotOf[c] = (uint8_t)(schema[c].id / MAX_SENSOR_CHANNELS);
	   chOf[c]   = (uint8_t)(schema[c].id % MAX_SENSOR_CHANNELS);
	   scaleOf[c] = powf(10.0f, (float)schema[c].scaleExp);
	  }
	 }

	 int16_t vals[H5_MAX_CHANNELS];
	 uint32_t recEpoch = 0;
	 bool fileHasMore = true;
	 uint32_t emitted = 0;

	 { ReadGuard rg(_storageRef); _storageRef->h5SeekTo(rangeFrom); }

	 while (fileHasMore && !aborted) {
	  if (isClientGone( ) || isHandlerOvertime( )) {
	   LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
	   aborted = true; break;
	  }

	  {
	   ReadGuard rg(_storageRef);
	   if (!_storageRef->h5NextRecord(recEpoch, vals)) { fileHasMore = false; break; }
	  }

	  if (recEpoch < rangeFrom) continue;
	  if (recEpoch > rangeTo) { fileHasMore = false; break; }

	  BinaryHistoryRecord rec;
	  rec.clear( );
	  rec.epoch = recEpoch;
	  for (uint8_t c = 0; c < nCh; c++) {
	   if (vals[c] == H5_NAN_SENTINEL) continue;
	   const uint8_t slot = slotOf[c];
	   if (slot >= MAX_SENSORS) continue;
	   const float v = (float)vals[c] * scaleOf[c];
	   if (chOf[c] == CH_TEMP)       rec.sensors[slot]  = BinaryHistoryRecord::floatToI16(v);
	   else if (chOf[c] == CH_HUM)   rec.humidity[slot] = BinaryHistoryRecord::floatToI16(v);
	   else if (chOf[c] == CH_PRESS) rec.pressure       = BinaryHistoryRecord::floatToI16x10(v);
	  }

	  crc = crc32_update(crc, (const uint8_t*)&rec, sizeof(rec));
	  if (!safeSend((const char*)&rec, sizeof(rec))) { aborted = true; break; }

	  /* Breathe every 20 records. */
	  if ((++emitted % 20) == 0) {
	   if (_lightYieldCb) _lightYieldCb( );
	   delay(2);
	   watchdog_update( );
	  }
	 }
	 { ReadGuard rg(_storageRef); _storageRef->h5CloseDay( ); }
	 /* dayStart already advanced at top of loop (infinite loop protection) */
	 }

 /* TRAILER: final CRC32 */
 if (!aborted) {
 uint32_t crcFinal = crc32_final(crc);
 safeSend((const char*)&crcFinal, sizeof(crcFinal));
 safeSend("");
 } else dropAbortedStream("hx");

 _handlerDeadline = savedDeadline;
 if (_displayRef) _displayRef->setWebBusy(false);
 __atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);
}

/* =========================================================================== */
/* GET /api/export/logs.bin?from=<epoch>&to=<epoch>&level=... */
/* =========================================================================== */
/* Emits .simx bundle kind='L' (CRC32 trailer). PAYLOAD = N x CompactLogRecord
 * (12 B packed) raw, same as existing /api/logs, but filtered by epoch and
 * level. Hard cap 31 days. SENSOR_TABLE empty (sensorTableSize=0).
 *
 * level=err -> LOG_ERROR only (3)
 * level=inf -> LOG_INFO only (1)
 * level=all -> everything (default)
 *
 * Client decodes records, looks up `code` -> text via /api/lang and
 * generates CSV in browser.
 */
void WebManager::handleApiExportLogs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_LOGS)) { _server->send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

 if (!_server->hasArg("from") || !_server->hasArg("to")) {
 _server->send(400, "application/json", "{\"error\":\"Missing from/to params\"}"); return;
 }
 uint32_t rangeFrom = (uint32_t)strtoul(_server->arg("from").c_str( ), nullptr, 10);
 uint32_t rangeTo = (uint32_t)strtoul(_server->arg("to").c_str( ), nullptr, 10);
 if (rangeFrom == 0 || rangeTo == 0 || rangeFrom >= rangeTo) {
 _server->send(400, "application/json", "{\"error\":\"Invalid range\"}"); return;
 }
 if (rangeTo - rangeFrom > SIMX_MAX_RANGE_SECS) {
 _server->send(400, "application/json", "{\"error\":\"Range exceeds 31 days\"}"); return;
 }

 /* Level filter: 0 = all, 1 = INFO only, 3 = ERROR only.
 * Keep numeric LogLevel code for direct comparison. */
 String levelArg = _server->hasArg("level") ? _server->arg("level") : "all";
 uint8_t levelFilter = 0xFF; /* 0xFF = no filter */
 if (levelArg == "err") levelFilter = LOG_ERROR;
 else if (levelArg == "inf") levelFilter = LOG_INFO;
 else if (levelArg != "all") {
 _server->send(400, "application/json", "{\"error\":\"Invalid level (use err|inf|all)\"}"); return;
 }

 if (TouchPriority::isActive( )) {
 _server->sendHeader("Retry-After", "3");
 _server->send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}"); return;
 }
 if (__atomic_exchange_n(&_inExportLogsHandler, true, __ATOMIC_ACQ_REL)) {
 _server->send(503, "application/json", "{\"error\":\"Already processing\"}"); return;
 }
 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 __atomic_store_n(&_inExportLogsHandler, false, __ATOMIC_RELEASE);
 _server->send(503, "application/json", "{\"error\":\"System Busy.\"}"); return;
 }

 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /* Streaming CRC32 accumulator (covers HEADER + PAYLOAD; sensorTblSize = 0). */
 uint32_t crc = crc32_init( );

 _server->sendHeader("Content-Disposition", "attachment; filename=\"simut_logs.simx\"");
 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 _server->send(200, "application/octet-stream", "");

 /* Emit HEADER */
 SimxHeader hdr = {};
 memcpy(hdr.magic, "SIMX", 4);
 hdr.version = 0x01;
 hdr.kind = 'L';
 hdr.recordSize = (uint16_t)LOG_RECORD_SIZE; /* 12 */
 hdr.rangeFrom = rangeFrom;
 hdr.rangeTo = rangeTo;
 hdr.sensorTableSize = 0;
 crc = crc32_update(crc, (const uint8_t*)&hdr, sizeof(hdr));
 safeSend((const char*)&hdr, sizeof(hdr));

 /* Iterate /system.old.blog first (oldest) then /system.blog */
 bool aborted = false;
 auto streamFiltered = [&](const char* path) -> bool {
 File f;
 {
 ReadGuard rg(_storageRef);
 if (!LittleFS.exists(path)) return true;
 f = LittleFS.open(path, "r");
 }
 if (!f) return true;

 uint8_t batch[480]; /* 40 records x 12B */
 while (f.available( ) >= LOG_RECORD_SIZE && !aborted) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 LOG_CODE(LOG_WARN, "WEB", WEB_DISCONNECT_HISTORY, 0, "");
 aborted = true; break;
 }

 int bytesRead = 0;
 {
 ReadGuard rg(_storageRef);
 while (f.available( ) >= LOG_RECORD_SIZE &&
 bytesRead + LOG_RECORD_SIZE <= (int)sizeof(batch)) {
 if (f.read(batch + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
 bytesRead += LOG_RECORD_SIZE;
 }
 }
 }

 for (int off = 0; off < bytesRead && !aborted; off += LOG_RECORD_SIZE) {
 const CompactLogRecord* rec = (const CompactLogRecord*)(batch + off);
 if (rec->epoch < rangeFrom || rec->epoch > rangeTo) continue;
 if (levelFilter != 0xFF && rec->getLevel( ) != levelFilter) continue;

 crc = crc32_update(crc, (const uint8_t*)rec, LOG_RECORD_SIZE);
 if (!safeSend((const char*)rec, LOG_RECORD_SIZE)) { aborted = true; break; }
 }

 if (_lightYieldCb) _lightYieldCb( );
 delay(2);
 watchdog_update( );
 }
 { ReadGuard rg(_storageRef); f.close( ); }
 return !aborted;
 };

 if (streamFiltered(LOG_FILE_OLD)) {
 streamFiltered(LOG_FILE_CURRENT);
 }

 /* TRAILER: final CRC32 */
 if (!aborted) {
 uint32_t crcFinal = crc32_final(crc);
 safeSend((const char*)&crcFinal, sizeof(crcFinal));
 safeSend("");
 } else dropAbortedStream("hx");

 _handlerDeadline = savedDeadline;
 if (_displayRef) _displayRef->setWebBusy(false);
 __atomic_store_n(&_inExportLogsHandler, false, __ATOMIC_RELEASE);
}

void WebManager::handleApiLogs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_LOGS)) { _server->send(403, "text/plain", "Forbidden"); return; }
 if (isRateLimited(200)) { _server->send(429, "text/plain", "Too Fast"); return; }


 if (TouchPriority::isActive( )) {
 _server->sendHeader("Retry-After", "3");
 _server->send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return;
 }

 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) {
 _server->send(503, "text/plain", "System Busy.");
 return;
 }


 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;
 if (_displayRef) _displayRef->setWebBusy(true, _currentUserName.c_str( ));

 /*
 * Send raw binary logs (12 bytes/record) for maximum
 * transfer efficiency. Translation happens in the browser.
 * Format: application/octet-stream, N × CompactLogRecord(12 bytes).
 * ~10x smaller than the previous translated CSV.
 */
 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 _server->send(200, "application/octet-stream", "");

 auto streamRawLog = [&](const char* path) -> bool {
 File f;
 {
 ReadGuard rg(_storageRef);
 if (!LittleFS.exists(path)) return true;
 f = LittleFS.open(path, "r");
 }

 if (!f) return true;

 int count = 0;
 while (f.available( ) >= LOG_RECORD_SIZE) {
 if (count > 0 && count % 80 == 0) {
 if (isClientGone( ) || isHandlerOvertime( )) {
 f.close( );
 return false;
 }
 }

 /* Read batch of up to 40 records (480 bytes) and send at once */
 uint8_t buf[480];
 int bytesRead = 0;
 {
 ReadGuard rg(_storageRef);
 while (f.available( ) >= LOG_RECORD_SIZE && bytesRead + LOG_RECORD_SIZE <= (int)sizeof(buf)) {
 if (f.read(buf + bytesRead, LOG_RECORD_SIZE) == LOG_RECORD_SIZE) {
 bytesRead += LOG_RECORD_SIZE;
 }
 }
 }

 if (bytesRead > 0) {
 /* safeSend, not _server->sendContent. This was the only raw sendContent
  * outside WebManager_Send.cpp, and the difference is the SendGuard: the 2 s
  * repeating timer in WebManager_Core feeds the watchdog ONLY while
  * _sendGuardActive, which only SendGuard sets. Without it nothing fed during
  * the send, and the send itself is unbounded — ClientContext restarts its
  * timeout on every partial write, so a browser that stops draining the socket
  * (busy parsing the history JSON it just fetched, on its single thread) blocks
  * here indefinitely. The watchdog_update( ) below runs only AFTER the send
  * returns, which is exactly when it is not needed. */
 if (!safeSend((const char*)buf, bytesRead)) break;
 count += bytesRead / LOG_RECORD_SIZE;
 }

 if (_lightYieldCb) _lightYieldCb( );
 delay(2);
 watchdog_update( );
 }
 f.close( );
 return true;
 };

 if (streamRawLog(LOG_FILE_OLD)) {
 streamRawLog(LOG_FILE_CURRENT);
 }

 safeSend("");
 _handlerDeadline = savedDeadline;
 if (_displayRef) _displayRef->setWebBusy(false);
}

void WebManager::handleApiClearLogs( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_LOGS) || !(perms & PERM_SYS_CONFIG)) { _server->send(403, "text/plain", "Forbidden"); return; }
 if (isPasswordChangeRequired( )) return;
 if (rejectIfTouchPriority( )) return;

 {
 RenderGuard rg(_displayRef);
 LittleFS.remove(LOG_FILE_CURRENT);
 LittleFS.remove(LOG_FILE_OLD);
 /* Also remove legacy CSV logs */
 LittleFS.remove("/system.log");
 LittleFS.remove("/system.old");
 LogManager::instance( ).resetAfterExternalWipe( );
 }

 LOG_CODE(LOG_WARN, "SEC", SEC_CONFIG_CHANGED, _currentUserId, TRL("Admin erased System Logs"));
 _server->send(200, "application/json", "{\"status\":\"ok\"}");
}

/* Shared helper for handleApiScreenshot + handleApiScreenshotChunk.
 * Reads N rows from TFT (3x each with majority vote) and converts RGB565→BGR888 in
 * out_bgr. chunk_start_bmp_y is the offset IN THE BMP IMAGE (bottom-up), display_y
 * is mapped to TFT order (top-down). Caller is responsible for pause/unpause
 * of Core 1 around the call. */
static void read_chunk_bgr(DisplayManager* d, int chunk_start_bmp_y,
 int rows_this, int w, int h, uint8_t* out_bgr) {
 uint16_t pixelRow1[320], pixelRow2[320], pixelRow3[320];
 for (int i = 0; i < rows_this; i++) {
 int display_y = h - 1 - (chunk_start_bmp_y + i);
 if (display_y < 0) break;
 d->readRow(display_y, pixelRow1, w);
 d->readRow(display_y, pixelRow2, w);
 d->readRow(display_y, pixelRow3, w);
 uint8_t* row_dst = out_bgr + i * w * 3;
 for (int x = 0; x < w; x++) {
 uint16_t a = pixelRow1[x], b = pixelRow2[x], c = pixelRow3[x];
 uint16_t color = (a == b) ? a : (b == c ? b : (a == c ? a : b));
 row_dst[x*3 + 0] = (color & 0x001F) << 3; /* B */
 row_dst[x*3 + 1] = ((color & 0x07E0) >> 5) << 2; /* G */
 row_dst[x*3 + 2] = ((color & 0xF800) >> 11) << 3;/* R */
 }
 }
}

void WebManager::handleApiScreenshot( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "text/plain", "Forbidden"); return; }


 if (TouchPriority::isActive( )) {
 _server->sendHeader("Retry-After", "3");
 _server->send(503, "application/json", "{\"error\":\"Display in use. Retry shortly.\"}");
 return;
 }


 if (__atomic_exchange_n(&_isProcessingScreenshot, true, __ATOMIC_ACQ_REL)) {
 /* Screenshot in progress: signal cancellation and return 409 */
 _cancelScreenshot = true;
 _server->send(409, "application/json", "{\"error\":\"Screenshot in progress, cancelling.\"}");
 return;
 }
 _cancelScreenshot = false;

 if (!_displayRef) {
 __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
 _server->send(500, "text/plain", "Display offline");
 return;
 }


 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;

 uint32_t w = 320;
 uint32_t h = 240;
 uint32_t rowSize = 960;
 uint32_t imgSize = rowSize * h;
 uint32_t fileSize = 54 + imgSize;

 uint8_t bmpHeader[54] = {
 'B', 'M',
 (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
 (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
 (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
 1, 0, 24, 0, 0, 0, 0, 0,
 (uint8_t)(imgSize), (uint8_t)(imgSize >> 8), (uint8_t)(imgSize >> 16), (uint8_t)(imgSize >> 24),
 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
 };

 _server->setContentLength(fileSize);
 _server->send(200, "image/bmp", "");
 safeSend((const char*)bmpHeader, 54);

 /* Uses the pattern from screenshot_chunk — pause 1x per 16 rows, read each
 * row 3x with majority vote (helper read_chunk_bgr), stream in chunks. */
 constexpr int ROWS_PER_CHUNK = 16;
 uint8_t* chunkBuf = (uint8_t*)malloc(ROWS_PER_CHUNK * 320 * 3); /* 15360 */
 if (!chunkBuf) {
 __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
 _handlerDeadline = savedDeadline;
 return;
 }
 bool clientDisconnected = false;
 for (int chunk_start = 0; chunk_start < (int)h; chunk_start += ROWS_PER_CHUNK) {
 int rows_this = ((int)h - chunk_start < ROWS_PER_CHUNK) ? ((int)h - chunk_start) : ROWS_PER_CHUNK;
 if (!_server->client( ).connected( ) || isHandlerOvertime( ) || _cancelScreenshot) {
 clientDisconnected = true; break;
 }
 _displayRef->pauseRendering(true);
 read_chunk_bgr(_displayRef, chunk_start, rows_this, w, h, chunkBuf);
 _displayRef->pauseRendering(false);
 watchdog_update( );
 safeSend((const char*)chunkBuf, rows_this * 320 * 3);
 if (_lightYieldCb) _lightYieldCb( );
 }

 free(chunkBuf);
 __atomic_store_n(&_isProcessingScreenshot, false, __ATOMIC_RELEASE);
 _handlerDeadline = savedDeadline;
 if (clientDisconnected) LOG_CODE(LOG_WARN, "WEB", WEB_SCREENSHOT_ABORTED, 0, "");
}

/* Chunked screenshot with CRC32 — verifiable integrity.
 *
 * GET /api/screenshot_chunk?n=N returns 1 chunk of 16 rows from the TFT display.
 * Total chunks: 15 (240/16). Each chunk:
 * - 16 rows × 320 cols × 3 bytes BGR = 15360 bytes payload
 * - 12-byte binary header:
 * [0..3] uint32 chunk_index (big-endian)
 * [4..7] uint32 payload_size (big-endian)
 * [8..11] uint32 crc32 EDB88320 of payload (big-endian)
 *
 * Client: requests N=0..14 sequentially. Verifies CRC32 each chunk; if
 * fails, re-requests that N. Reassembles BMP locally (header + 240 rows).
 *
 * Advantages vs /api/screenshot full:
 * - Localized failure (individual chunk, not entire BMP)
 * - Integrity verification (CRC32)
 * - Can retry corrupted chunks
 * - Client can pause/resume
 *
 * MIME response: application/octet-stream (pure binary). */
void WebManager::handleApiScreenshotChunk( ) {
 uint16_t perms = getAuthPerms( );
 if (!(perms & PERM_SYS_CONFIG)) { _server->send(403, "text/plain", "Forbidden"); return; }

 if (!_displayRef) { _server->send(500, "text/plain", "Display offline"); return; }

 /* Parse ?n=N */
 int n = -1;
 if (_server->hasArg("n")) n = _server->arg("n").toInt( );
 constexpr int W = 320;
 constexpr int H = 240;
 constexpr int ROWS_PER_CHUNK = 16;
 constexpr int TOTAL_CHUNKS = (H + ROWS_PER_CHUNK - 1) / ROWS_PER_CHUNK; /* 15 */
 if (n < 0 || n >= TOTAL_CHUNKS) {
 _server->send(416, "text/plain", "Invalid chunk index (use n=0..14)");
 return;
 }

 /* Bump handler deadline (same pattern as handleApiScreenshot).
 * 16 rows × ~50ms readRow + CRC32 + send = can take >2s; default deadline
 * kills handler before finishing = empty body. */
 uint32_t savedDeadline = _handlerDeadline;
 _handlerDeadline = millis( ) + WEB_LONG_HANDLER_DEADLINE_MS;

 /* Calculate row range for this chunk */
 int row_start = n * ROWS_PER_CHUNK;
 int rows_this = (row_start + ROWS_PER_CHUNK > H) ? (H - row_start) : ROWS_PER_CHUNK;
 int payload_size = rows_this * W * 3;

 /* Temporary buffer for rows (16 rows × 320 cols × 3 bytes = 15360 max).
 * Heap-alloc on demand instead of static BSS — saves 15 KB
 * permanent (endpoint used rarely: debug + manual capture). Fail
 * with 503 if heap insufficient; client can retry. */
 uint8_t* payload = (uint8_t*)malloc(ROWS_PER_CHUNK * W * 3);
 if (!payload) {
 _handlerDeadline = savedDeadline;
 _server->send(503, "text/plain", "Out of memory");
 return;
 }
 /* Multi-sample 3x + majority vote via shared helper
 * read_chunk_bgr. ILI9341
 * read protocol is fragile; reads each row 3x and takes majority value (2/3),
 * fallback on middle sample. Reduces line defects by ~95%. */
 _displayRef->pauseRendering(true);
 read_chunk_bgr(_displayRef, row_start, rows_this, W, H, payload);
 _displayRef->pauseRendering(false);
 watchdog_update( );

 /* Compute CRC32 EDB88320 (gzip-compatible) */
 uint32_t crc = ota::crc32_update(0xFFFFFFFFu, payload, payload_size) ^ 0xFFFFFFFFu;

 /* Header big-endian */
 uint8_t hdr[12];
 hdr[0] = (n >> 24) & 0xFF; hdr[1] = (n >> 16) & 0xFF; hdr[2] = (n >> 8) & 0xFF; hdr[3] = n & 0xFF;
 hdr[4] = (payload_size >> 24) & 0xFF; hdr[5] = (payload_size >> 16) & 0xFF;
 hdr[6] = (payload_size >> 8) & 0xFF; hdr[7] = payload_size & 0xFF;
 hdr[8] = (crc >> 24) & 0xFF; hdr[9] = (crc >> 16) & 0xFF;
 hdr[10] = (crc >> 8) & 0xFF; hdr[11] = crc & 0xFF;

 _server->setContentLength(12 + payload_size);
 _server->send(200, "application/octet-stream", "");
 safeSend((const char*)hdr, 12);
 safeSend((const char*)payload, payload_size);

 free(payload);
 _handlerDeadline = savedDeadline;
}

void WebManager::handleApiHistoryDays( ) {
 if ((getAuthPerms( ) & PERM_HISTORY) == 0) { _server->send(403); return; }


 HeavyTaskGuard htg(_storageRef);
 if (!htg.isLocked( )) { _server->send(503, "application/json", "{\"error\":\"System Busy\"}"); return; }

 std::vector<String> files;
 {

 ReadGuard rg(_storageRef);
 Dir dir = LittleFS.openDir(DIR_HISTORY);
 while (dir.next( )) {
 /* feedWdt( ) and not feedWatchdog( ): we hold _fsReadMutex here, and
  * feedWatchdog( ) runs the light yield, which every 3 s calls
  * updateLiveDisplay( ) -> refreshPendingCount( ) -> enterFlashReadLock( ).
  * That is mutex_enter_blocking on a NON-recursive mutex this very core
  * already owns: it never returns, and nothing feeds the watchdog while it
  * waits. Same distinction as the history scan in 116c7f8; this call site and
  * handleApiLs were left behind. */
 feedWdt( );
 if (dir.fileName( ).endsWith(HISTORY_FILE_EXT)) {
 files.push_back(dir.fileName( ));
 }
 }
 }

 sortStrings(files.data( ), (int)files.size( ), true); /* descending */

 _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _chunkedResponse = true;
 _server->send(200, "application/json", "");
 safeSend("[");
 for (size_t i = 0; i < files.size( ); i++) {
 /* Strip .sim4 BEFORE .sim: replace(".sim") on "20260722.sim4" left
  * "202607224" — malformed dates, calendar never marked V4 days. */
 files[i].replace(HISTORY_FILE_EXT, "");
 files[i].replace(HISTORY_FILE_EXT, "");
 String entry = (i > 0 ? ",\"" : "\"") + files[i] + "\"";
 safeSend(entry);
 }
 safeSend("]");
 safeSend("");
}
