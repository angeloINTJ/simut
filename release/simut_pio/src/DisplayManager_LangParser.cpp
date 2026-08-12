/**
 * @file DisplayManager_LangParser.cpp
 * @brief .lng parser + _activeLang storage + unaccent.
 * @details Loads /lang/language_<code>.lng files at runtime to
 * enable UI translations without re-flashing the firmware. EN
 * stays hardcoded in DICTIONARY_EN (DisplayManager_i18n.cpp).
 * Only 1 active slot at a time; loadLangFile frees the previous one.
 *
 * .lng format (directives at column 0):
 * @NAME <display text>
 * @CODE <2-3 chars>
 * @DICT
 * <line 1 = TR_AMBIENT>
 * <line 2 = TR_CONFIG_MAIN>
 * ...
 * <line N = last LangKey before TR_KEYS_COUNT>
 * @HELP
 * <free text, multiline>
 * @LICENSE
 * <free text, multiline>
 *
 * Memory strategy: single allocation, sized to the file MINUS its @WEBDICT
 * section. Pointers in _activeLang.strings/helpText/licenseText point into
 * this buffer; null-termination done by modifying the buffer in-place.
 *
 * @WEBDICT is excluded on purpose. It is roughly half of a pack by bytes
 * (14 KB of 28 KB in pt-BR) and no firmware code path reads it — it exists
 * only to be served to the browser by GET /api/lang. Keeping it resident
 * spent 14 KB of the 128 KB heap for the whole uptime, so the parser records
 * its byte range (webDictOffset/webDictLen) and the web handler streams it
 * from flash on demand. The load still reads the whole file, so the peak
 * during parse is unchanged; only the steady state shrinks.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "LogManager.h"
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

DisplayManager::ActiveLang DisplayManager::_activeLang = {};
bool DisplayManager::_activeLangLoaded = false;

/* Defensive limits */
static constexpr size_t LANG_FILE_MIN = 64;
static constexpr size_t LANG_FILE_MAX = 32768; /* ~32 KB envelope (DICT+LOGCODES+TRL+HELP+LIC) */

uint32_t DisplayManager::fnv1a32(const char* s) {
 uint32_t h = 0x811c9dc5u;
 if (!s) return h;
 while (*s) {
 h ^= (uint8_t)*s++;
 h *= 0x01000193u;
 }
 return h;
}

void DisplayManager::unloadLang( ) {
 if (_activeLang.buffer) free(_activeLang.buffer);
 if (_activeLang.logcodes) free(_activeLang.logcodes);
 if (_activeLang.trls) free(_activeLang.trls);
 memset(&_activeLang, 0, sizeof(_activeLang));
 _activeLangLoaded = false;
}

/* qsort comparators */
static int cmpLogcode(const void* a, const void* b) {
 uint16_t ca = ((const DisplayManager::LogCodeEntry*)a)->code;
 uint16_t cb = ((const DisplayManager::LogCodeEntry*)b)->code;
 return (ca < cb) ? -1 : (ca > cb);
}
static int cmpTrl(const void* a, const void* b) {
 uint32_t ha = ((const DisplayManager::TrlEntry*)a)->hash;
 uint32_t hb = ((const DisplayManager::TrlEntry*)b)->hash;
 return (ha < hb) ? -1 : (ha > hb);
}

/* Counts non-empty lines in [start, end) — for sizing arrays. */
static uint16_t countNonEmptyLines(const char* buf, size_t start, size_t end) {
 uint16_t count = 0;
 bool inLine = false;
 for (size_t i = start; i < end; i++) {
 char c = buf[i];
 if (c == '\n') {
 if (inLine) count++;
 inLine = false;
 } else if (c != '\r') {
 inLine = true;
 }
 }
 if (inLine) count++;
 return count;
}

bool DisplayManager::loadLangFile(const char* path) {
 unloadLang( );
 if (!path) return false;

 File f = LittleFS.open(path, "r");
 if (!f) return false;

 size_t fsize = f.size( );
 if (fsize < LANG_FILE_MIN || fsize > LANG_FILE_MAX) {
 f.close( );
 return false;
 }

 /* +2: 1 to guarantee final \n terminator and 1 for closing '\0' */
 char* buf = (char*)malloc(fsize + 2);
 if (!buf) {
 f.close( );
 return false;
 }

 size_t n = f.readBytes(buf, fsize);
 f.close( );
 if (n == 0) { free(buf); return false; }
 buf[n] = '\n'; /* force line end even if missing */
 buf[n+1] = '\0';
 n++;

 /* Maps boundaries of each section. bodyStart=0 means "absent". */
 enum SecIdx { S_DICT = 0, S_HELP, S_LICENSE, S_LOGCODES, S_TRL, S_WEBDICT, S_COUNT };
 size_t secStart[S_COUNT] = { 0, 0, 0, 0, 0, 0 };
 size_t secEnd[S_COUNT] = { 0, 0, 0, 0, 0, 0 };

 int curSec = -1;
 size_t i = 0;

 while (i < n) {
 bool atColZero = (i == 0) || buf[i-1] == '\n';
 if (!(atColZero && buf[i] == '@')) { i++; continue; }

 /* Close current section (HELP/LICENSE/DICT) */
 if (curSec >= 0) secEnd[curSec] = i;

 /* Identify directive: @NAME, @CODE, @DICT, @HELP, @LICENSE */
 size_t dirStart = i + 1;
 size_t dirEnd = dirStart;
 while (dirEnd < n && buf[dirEnd] != ' ' && buf[dirEnd] != '\t' &&
 buf[dirEnd] != '\n' && buf[dirEnd] != '\r') dirEnd++;
 size_t dirLen = dirEnd - dirStart;

 /* Advance i past the \n of the directive line */
 size_t lineEnd = dirEnd;
 while (lineEnd < n && buf[lineEnd] != '\n') lineEnd++;
 size_t bodyAfter = (lineEnd < n) ? lineEnd + 1 : n;

 if (dirLen == 4 && memcmp(buf + dirStart, "NAME", 4) == 0) {
 curSec = -1;
 size_t v = dirEnd;
 while (v < lineEnd && (buf[v] == ' ' || buf[v] == '\t')) v++;
 size_t vEnd = lineEnd;
 while (vEnd > v && (buf[vEnd-1] == '\r' || buf[vEnd-1] == ' ' ||
 buf[vEnd-1] == '\t')) vEnd--;
 size_t copy = vEnd - v;
 if (copy >= sizeof(_activeLang.name)) copy = sizeof(_activeLang.name) - 1;
 memcpy(_activeLang.name, buf + v, copy);
 _activeLang.name[copy] = '\0';
 } else if (dirLen == 4 && memcmp(buf + dirStart, "CODE", 4) == 0) {
 curSec = -1;
 size_t v = dirEnd;
 while (v < lineEnd && (buf[v] == ' ' || buf[v] == '\t')) v++;
 size_t vEnd = lineEnd;
 while (vEnd > v && (buf[vEnd-1] == '\r' || buf[vEnd-1] == ' ' ||
 buf[vEnd-1] == '\t')) vEnd--;
 size_t copy = vEnd - v;
 if (copy >= sizeof(_activeLang.code)) copy = sizeof(_activeLang.code) - 1;
 memcpy(_activeLang.code, buf + v, copy);
 _activeLang.code[copy] = '\0';
 } else if (dirLen == 4 && memcmp(buf + dirStart, "DICT", 4) == 0) {
 curSec = S_DICT;
 secStart[S_DICT] = bodyAfter;
 } else if (dirLen == 4 && memcmp(buf + dirStart, "HELP", 4) == 0) {
 curSec = S_HELP;
 secStart[S_HELP] = bodyAfter;
 } else if (dirLen == 7 && memcmp(buf + dirStart, "LICENSE", 7) == 0) {
 curSec = S_LICENSE;
 secStart[S_LICENSE] = bodyAfter;
 } else if (dirLen == 8 && memcmp(buf + dirStart, "LOGCODES", 8) == 0) {
 curSec = S_LOGCODES;
 secStart[S_LOGCODES] = bodyAfter;
 } else if (dirLen == 3 && memcmp(buf + dirStart, "TRL", 3) == 0) {
 curSec = S_TRL;
 secStart[S_TRL] = bodyAfter;
 } else if (dirLen == 7 && memcmp(buf + dirStart, "WEBDICT", 7) == 0) {
 curSec = S_WEBDICT;
 secStart[S_WEBDICT] = bodyAfter;
 } else {
 curSec = -1; /* unknown directive — ignore */
 }

 i = bodyAfter;
 }
 if (curSec >= 0) secEnd[curSec] = n;

 /* DICT is mandatory; without it reject the file */
 if (secStart[S_DICT] == 0 || secEnd[S_DICT] <= secStart[S_DICT]) {
 free(buf);
 memset(&_activeLang, 0, sizeof(_activeLang));
 return false;
 }

 /* Partition the @DICT block into lines; line N is LangKey N.
 *
 * A pack shorter than TR_KEYS_COUNT used to be rejected whole, which meant
 * that adding one key to the firmware turned every already-deployed pack off
 * and reverted the entire UI to English. Missing keys are left null instead,
 * and tr() already answers a null entry from DICTIONARY_EN — so an old pack
 * keeps translating everything it knows and only the new strings come out in
 * English until it is updated.
 *
 * Empty lines are nulled for the same reason. That case is not hypothetical:
 * both shipped packs ended @DICT with a blank line, so a stale pack lined up
 * against a newer firmware filled the new slots with "" and drew blank labels
 * on the TFT — a worse failure than English, because nothing looks wrong,
 * there is just nothing there. */
 int dictIdx = 0;
 size_t lineStart = secStart[S_DICT];
 size_t dictEnd = secEnd[S_DICT];

 for (size_t k = lineStart; k <= dictEnd; k++) {
 if (k == dictEnd || buf[k] == '\n') {
 if (dictIdx < TR_KEYS_COUNT) {
 /* Mark end of line (if \n; \0 if k==dictEnd already guaranteed by buf[n]) */
 if (k < dictEnd) buf[k] = '\0';
 /* Strip trailing \r */
 size_t lastChar = k;
 if (lastChar > lineStart && buf[lastChar-1] == '\r') {
 buf[lastChar-1] = '\0';
 }
 char* line = buf + lineStart;
 _activeLang.strings[dictIdx++] = (line[0] == '\0') ? nullptr : line;
 }
 lineStart = k + 1;
 if (dictIdx >= TR_KEYS_COUNT) break;
 }
 }

 /* A file with no usable dictionary at all is still a bad file. */
 if (dictIdx == 0) {
 free(buf);
 memset(&_activeLang, 0, sizeof(_activeLang));
 return false;
 }
 for (int k = dictIdx; k < TR_KEYS_COUNT; k++) _activeLang.strings[k] = nullptr;
 if (dictIdx != TR_KEYS_COUNT) {
 LOG_CODE(LOG_WARN, "I18N", SYS_OK, dictIdx,
 TRL("Language pack is older than the firmware — missing strings show in English"));
 }

 /* HELP and LICENSE: preserve newlines, only null-terminate at end */
 if (secEnd[S_HELP] > secStart[S_HELP]) {
 _activeLang.helpText = buf + secStart[S_HELP];
 size_t e = secEnd[S_HELP];
 if (e > 0 && buf[e-1] == '\n') buf[e-1] = '\0';
 else if (e <= n) buf[e] = '\0';
 }
 if (secEnd[S_LICENSE] > secStart[S_LICENSE]) {
 _activeLang.licenseText = buf + secStart[S_LICENSE];
 size_t e = secEnd[S_LICENSE];
 if (e > 0 && buf[e-1] == '\n') buf[e-1] = '\0';
 else if (e <= n) buf[e] = '\0';
 }
 /* @WEBDICT: opaque JSON blob, served via GET /api/lang to browser.
  * Recorded as a byte range into the file, never as a pointer — the blob is
  * excised from the buffer further down. Offsets into `buf` ARE file offsets:
  * the buffer is a verbatim copy of the file from byte 0.
  * The trailing '\n' is dropped from the served length so the bytes match the
  * old in-RAM string exactly (it was terminated at that newline). */
 if (secEnd[S_WEBDICT] > secStart[S_WEBDICT]) {
 size_t e = secEnd[S_WEBDICT];
 _activeLang.webDictOffset = (uint32_t)secStart[S_WEBDICT];
 _activeLang.webDictLen = (uint32_t)((e - secStart[S_WEBDICT]) -
 ((e > 0 && buf[e-1] == '\n') ? 1 : 0));
 }

 /* @LOGCODES: each line "<decimal_id> <text>", split at first space.
 * Empty lines or lines without a space are skipped. */
 if (secEnd[S_LOGCODES] > secStart[S_LOGCODES]) {
 size_t s = secStart[S_LOGCODES], e = secEnd[S_LOGCODES];
 uint16_t cap = countNonEmptyLines(buf, s, e);
 if (cap > 0) {
 LogCodeEntry* arr = (LogCodeEntry*)malloc(sizeof(LogCodeEntry) * cap);
 if (!arr) { free(buf); memset(&_activeLang, 0, sizeof(_activeLang)); return false; }
 uint16_t idx = 0;
 size_t lineStart2 = s;
 for (size_t k = s; k <= e; k++) {
 if (k == e || buf[k] == '\n') {
 if (k > lineStart2) {
 if (k < e) buf[k] = '\0';
 if (k > lineStart2 && buf[k-1] == '\r') buf[k-1] = '\0';
 char* line = buf + lineStart2;
 char* sp = strchr(line, ' ');
 if (sp && idx < cap) {
 *sp = '\0';
 long codeVal = strtol(line, nullptr, 10);
 const char* text = sp + 1;
 if (codeVal >= 0 && codeVal <= 65535 && *text) {
 arr[idx].code = (uint16_t)codeVal;
 arr[idx].text = text;
 idx++;
 }
 }
 }
 lineStart2 = k + 1;
 }
 }
 if (idx > 0) {
 qsort(arr, idx, sizeof(LogCodeEntry), cmpLogcode);
 _activeLang.logcodes = arr;
 _activeLang.logcodesCount = idx;
 } else {
 free(arr);
 }
 }
 }

 /* @TRL: each line "<hex_hash> <text>" (hash in ASCII hex without 0x).
 * Allows generation via Python tooling: hex(fnv1a32(en)). */
 if (secEnd[S_TRL] > secStart[S_TRL]) {
 size_t s = secStart[S_TRL], e = secEnd[S_TRL];
 uint16_t cap = countNonEmptyLines(buf, s, e);
 if (cap > 0) {
 TrlEntry* arr = (TrlEntry*)malloc(sizeof(TrlEntry) * cap);
 if (!arr) {
 if (_activeLang.logcodes) { free(_activeLang.logcodes); _activeLang.logcodes = nullptr; _activeLang.logcodesCount = 0; }
 free(buf); memset(&_activeLang, 0, sizeof(_activeLang)); return false;
 }
 uint16_t idx = 0;
 size_t lineStart2 = s;
 for (size_t k = s; k <= e; k++) {
 if (k == e || buf[k] == '\n') {
 if (k > lineStart2) {
 if (k < e) buf[k] = '\0';
 if (k > lineStart2 && buf[k-1] == '\r') buf[k-1] = '\0';
 char* line = buf + lineStart2;
 char* sp = strchr(line, ' ');
 if (sp && idx < cap) {
 *sp = '\0';
 uint32_t h = (uint32_t)strtoul(line, nullptr, 16);
 const char* text = sp + 1;
 if (h != 0 && *text) {
 arr[idx].hash = h;
 arr[idx].text = text;
 idx++;
 }
 }
 }
 lineStart2 = k + 1;
 }
 }
 if (idx > 0) {
 qsort(arr, idx, sizeof(TrlEntry), cmpTrl);
 _activeLang.trls = arr;
 _activeLang.trlsCount = idx;
 } else {
 free(arr);
 }
 }
 }

 /* Excise @WEBDICT from the resident buffer.
  *
  * It is ~50% of a pack by bytes (14 KB of 28 KB in pt-BR) and no firmware
  * path ever reads it — it exists only to be handed to the browser by
  * GET /api/lang, which now streams it straight from this file. Holding it
  * cost 14 KB of a 128 KB heap for the entire uptime.
  *
  * Everything the parser kept points into `buf`, so the survivors are copied
  * around the hole and rebased. Nothing points inside the hole: the @WEBDICT
  * branch above stores offsets, not a pointer.
  *
  * On malloc failure the original buffer is kept — /api/lang still streams
  * from the file, so behaviour is identical and only the saving is lost. */
 if (_activeLang.webDictLen > 0) {
 const size_t wdStart = secStart[S_WEBDICT];
 const size_t wdEnd = secEnd[S_WEBDICT];
 const size_t wdLen = wdEnd - wdStart;
 const size_t newSize = n - wdLen + 1; /* +1 for the closing '\0' */

 char* nb = (char*)malloc(newSize);
 if (nb) {
 memcpy(nb, buf, wdStart);
 memcpy(nb + wdStart, buf + wdEnd, n - wdEnd);
 nb[newSize - 1] = '\0';

 /* Offsets below the hole are unchanged; those above shift down by
  * its size. Applied to every pointer the parser handed out. */
 auto rebase = [&](char* p) -> char* {
 if (!p) return nullptr;
 size_t o = (size_t)(p - buf);
 return nb + (o < wdStart ? o : o - wdLen);
 };
 for (int k = 0; k < TR_KEYS_COUNT; k++)
 _activeLang.strings[k] = rebase(_activeLang.strings[k]);
 _activeLang.helpText = rebase(_activeLang.helpText);
 _activeLang.licenseText = rebase(_activeLang.licenseText);
 for (uint16_t k = 0; k < _activeLang.logcodesCount; k++)
 _activeLang.logcodes[k].text = rebase((char*)_activeLang.logcodes[k].text);
 for (uint16_t k = 0; k < _activeLang.trlsCount; k++)
 _activeLang.trls[k].text = rebase((char*)_activeLang.trls[k].text);

 free(buf);
 buf = nb;
 n = newSize - 1;
 }
 }

 /* Path is kept so /api/lang can reopen the file to stream @WEBDICT. */
 strncpy(_activeLang.path, path, sizeof(_activeLang.path) - 1);
 _activeLang.path[sizeof(_activeLang.path) - 1] = '\0';

 _activeLang.buffer = buf;
 _activeLang.bufferSize = n;
 _activeLangLoaded = true;
 return true;
}

/* ─────────────────────────────────────────────────────────────────
 * Lookups: binary search on tables built by the parser.
 * Return nullptr when .lng is not loaded, when the caller
 * is in EN, or when the key has no entry. Caller should fallback
 * to EN inline.
 * ───────────────────────────────────────────────────────────────── */
const char* DisplayManager::logcodeLookup(uint16_t code) {
 if (!_activeLangLoaded || !_activeLang.logcodes) return nullptr;
 int lo = 0, hi = (int)_activeLang.logcodesCount - 1;
 while (lo <= hi) {
 int mid = (lo + hi) >> 1;
 uint16_t c = _activeLang.logcodes[mid].code;
 if (c == code) return _activeLang.logcodes[mid].text;
 if (c < code) lo = mid + 1; else hi = mid - 1;
 }
 return nullptr;
}

const char* DisplayManager::trlLookup(const char* en) {
 if (!_activeLangLoaded || !_activeLang.trls || !en) return nullptr;
 uint32_t h = fnv1a32(en);
 int lo = 0, hi = (int)_activeLang.trlsCount - 1;
 while (lo <= hi) {
 int mid = (lo + hi) >> 1;
 uint32_t mh = _activeLang.trls[mid].hash;
 if (mh == h) return _activeLang.trls[mid].text;
 if (mh < h) lo = mid + 1; else hi = mid - 1;
 }
 return nullptr;
}

/* ─────────────────────────────────────────────────────────────────
 * findAndLoadLangFile: scan /lang/ for "language_*.lng"
 * files, load the first alphabetically. Logs warning
 * if there are extras (more than 1 file found). Core 0 only.
 * ───────────────────────────────────────────────────────────────── */
bool DisplayManager::findAndLoadLangFile( ) {
 char firstName[40] = {0};
 int count = 0;

 Dir dir = LittleFS.openDir("/lang");
 while (dir.next( )) {
 String fn = dir.fileName( );
 /* Accept "language_*.lng" exactly; case-sensitive intentionally. */
 if (!fn.startsWith("language_") || !fn.endsWith(".lng")) continue;
 count++;
 if (count == 1) {
 strncpy(firstName, fn.c_str( ), sizeof(firstName) - 1);
 } else {
 /* Keep the smallest (alphabetically). LittleFS::openDir does not
 * guarantee order; manual comparison covers it. */
 if (strcmp(fn.c_str( ), firstName) < 0) {
 strncpy(firstName, fn.c_str( ), sizeof(firstName) - 1);
 firstName[sizeof(firstName) - 1] = '\0';
 }
 }
 }

 if (count == 0) return false;
 if (count > 1) {
 LOG_CODE(LOG_WARN, "I18N", SYS_OK, count,
 TRL("Multiple .lng files in /lang/ — loading first alphabetically"));
 }

 char path[64];
 snprintf(path, sizeof(path), "/lang/%s", firstName);
 bool ok = loadLangFile(path);
 if (ok) {
 LOG_CODE(LOG_INFO, "I18N", APP_UI_LANG_CHANGED, _activeLang.trlsCount,
 String(TRL("Language pack loaded: ")) + _activeLang.name);
 } else {
 LOG_CODE(LOG_ERROR, "I18N", SYS_STORAGE_FAIL, 0,
 String(TRL("Failed to parse language pack: ")) + path);
 }
 return ok;
}

const char* DisplayManager::getActiveHelpText( ) {
 return _activeLangLoaded ? _activeLang.helpText : nullptr;
}
const char* DisplayManager::getActiveLicenseText( ) {
 return _activeLangLoaded ? _activeLang.licenseText : nullptr;
}
bool DisplayManager::getActiveWebDictSource(const char** path, uint32_t* offset, uint32_t* len) {
 if (!_activeLangLoaded || _activeLang.webDictLen == 0) return false;
 if (path) *path = _activeLang.path;
 if (offset) *offset = _activeLang.webDictOffset;
 if (len) *len = _activeLang.webDictLen;
 return true;
}
bool DisplayManager::isLangLoaded( ) { return _activeLangLoaded; }
/* Active .lng metadata for web language selector. */
const char* DisplayManager::getActiveLangName( ) { return _activeLangLoaded ? _activeLang.name : ""; }
const char* DisplayManager::getActiveLangCode( ) { return _activeLangLoaded ? _activeLang.code : ""; }

/* ─────────────────────────────────────────────────────────────────
 * unaccent: UTF-8 (Latin-1 subset) -> ASCII 7-bit.
 *
 * Covers common accents in PT/ES/FR/DE: à á â ã ä å æ ç è é ê ë ì í î
 * ï ñ ò ó ô õ ö ø ù ú û ü ý ÿ + corresponding uppercase.
 *
 * UTF-8 represents these chars in 2 bytes: 0xC2/0xC3 + second byte.
 * ASCII characters (< 0x80) are copied literally. Multi-byte
 * UTF-8 sequences outside the table are skipped (1 byte only, preventing
 * infinite loop) — acceptable behavior for the display's limited font.
 * ───────────────────────────────────────────────────────────────── */
void DisplayManager::unaccent(const char* utf8, char* out, size_t outSize) {
 if (!out || outSize == 0) return;
 if (!utf8) { out[0] = '\0'; return; }

 size_t o = 0;
 const unsigned char* p = (const unsigned char*)utf8;

 while (*p && o + 1 < outSize) {
 unsigned char c = *p;
 if (c < 0x80) {
 out[o++] = (char)c;
 p++;
 continue;
 }
 unsigned char c2 = p[1];
 char repl = '?';
 if (c == 0xC3) { /* 0xC0..0xFF */
 switch (c2) {
 case 0x80: case 0x81: case 0x82: case 0x83:
 case 0x84: case 0x85: repl = 'A'; break;
 case 0x86: repl = 'A'; break; /* AE */
 case 0x87: repl = 'C'; break;
 case 0x88: case 0x89: case 0x8A:
 case 0x8B: repl = 'E'; break;
 case 0x8C: case 0x8D: case 0x8E:
 case 0x8F: repl = 'I'; break;
 case 0x91: repl = 'N'; break;
 case 0x92: case 0x93: case 0x94:
 case 0x95: case 0x96: case 0x98: repl = 'O'; break;
 case 0x99: case 0x9A: case 0x9B:
 case 0x9C: repl = 'U'; break;
 case 0x9D: repl = 'Y'; break;
 case 0xA0: case 0xA1: case 0xA2: case 0xA3:
 case 0xA4: case 0xA5: repl = 'a'; break;
 case 0xA6: repl = 'a'; break; /* ae */
 case 0xA7: repl = 'c'; break;
 case 0xA8: case 0xA9: case 0xAA:
 case 0xAB: repl = 'e'; break;
 case 0xAC: case 0xAD: case 0xAE:
 case 0xAF: repl = 'i'; break;
 case 0xB1: repl = 'n'; break;
 case 0xB2: case 0xB3: case 0xB4:
 case 0xB5: case 0xB6: case 0xB8: repl = 'o'; break;
 case 0xB9: case 0xBA: case 0xBB:
 case 0xBC: repl = 'u'; break;
 case 0xBD: case 0xBF: repl = 'y'; break;
 default: repl = '?'; break;
 }
 out[o++] = repl;
 p += 2;
 } else if (c == 0xC2) {
 /* Latin-1 supplement (0x80..0xBF): symbols like degree, +-, squared, cubed, copyright.
 * Simple substitutions; remainder becomes '?'. */
 switch (c2) {
 case 0xA9: repl = 'C'; break; /* copyright */
 case 0xAE: repl = 'R'; break; /* registered */
 case 0xB0: repl = 'o'; break; /* degree */
 case 0xB1: repl = '+'; break; /* plus-minus */
 case 0xB2: repl = '2'; break; /* squared */
 case 0xB3: repl = '3'; break; /* cubed */
 default: repl = '?'; break;
 }
 out[o++] = repl;
 p += 2;
 } else {
 /* UTF-8 multi-byte outside target: advance 1 byte, mark '?' */
 out[o++] = '?';
 p++;
 }
 }
 out[o] = '\0';
}

/* ─────────────────────────────────────────────────────────────────
 * utf8ToLatin1: UTF-8 -> ISO-8859-1 bytes for the TFT fonts.
 *
 * The 8-bit GFX fonts (FreeSansBold*8b_latin1.h) index glyphs by
 * Latin-1 code, so a 2-byte UTF-8 sequence 0xC2/0xC3 + cc maps to one
 * output byte. Anything outside Latin-1 (3/4-byte sequences) degrades
 * through unaccent()'s policy: '?'. Output is never longer than input,
 * so in-place-sized buffers stay safe. The CLI keeps using unaccent()
 * — its consumer is a 7-bit serial terminal, not these fonts.
 * ───────────────────────────────────────────────────────────────── */
void DisplayManager::utf8ToLatin1(const char* utf8, char* out, size_t outSize) {
 if (!out || outSize == 0) return;
 if (!utf8) { out[0] = '\0'; return; }

 size_t o = 0;
 const unsigned char* p = (const unsigned char*)utf8;

 while (*p && o + 1 < outSize) {
 unsigned char c = *p;
 if (c < 0x80) {
 out[o++] = (char)c;
 p++;
 } else if ((c == 0xC2 || c == 0xC3) && (p[1] & 0xC0) == 0x80) {
 out[o++] = (char)(((c & 0x03) << 6) | (p[1] & 0x3F));
 p += 2;
 } else {
 /* Outside Latin-1: consume the whole sequence, emit '?'. */
 out[o++] = '?';
 p++;
 while ((*p & 0xC0) == 0x80) p++;
 }
 }
 out[o] = '\0';
}
