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
 * @WEBDICT is excluded on purpose. It is roughly 60% of a pack by bytes
 * (19 KB of 32 KB in es-ES) and no firmware code path reads it — it exists
 * only to be served to the browser by GET /api/lang. The loader first
 * locates the @WEBDICT marker by streaming the file through a small stack
 * chunk, then mallocs and reads ONLY the resident prefix; the blob's byte
 * range (webDictOffset/webDictLen) is recorded and the web handler streams
 * it from flash on demand. The blob must therefore be the file's suffix —
 * tools/check_lang_packs.py enforces that section order at build time.
 * Peak heap during load equals the resident prefix, not the file size,
 * which is what lets LANG_FILE_MAX exceed what the heap could ever hold.
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
/* Two ceilings since the @WEBDICT suffix stopped being read into RAM.
 * LANG_RESIDENT_MAX bounds the malloc that lives for the whole uptime —
 * every section except @WEBDICT. LANG_FILE_MAX only bounds the file on
 * flash: the @WEBDICT majority of a pack is streamed to the browser by
 * GET /api/lang and never touches the heap, so the old single 32768
 * ceiling was charging web translations against RAM they never used. */
static constexpr size_t LANG_RESIDENT_MAX = 16384;
static constexpr size_t LANG_FILE_MAX = 49152;

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
 memset(&_activeLang, 0, sizeof(_activeLang));
 _activeLangLoaded = false;
}

/* Locates the "@WEBDICT" marker by streaming the file through a small stack
 * chunk — the point is knowing where the resident prefix ends WITHOUT paying
 * a file-sized malloc first. Outputs:
 *   markerAt: file offset of the marker line's '@' (0 = no @WEBDICT; offset
 *             0 itself can never hold it, packs open with a comment header),
 *   bodyAt:   file offset just past the marker line's '\n' (== fsize when
 *             the marker line is the last line of the file).
 * Returns false when any section directive follows the @WEBDICT body: the
 * blob must be the file's suffix or the resident prefix is not contiguous.
 * tools/check_lang_packs.py refuses to ship such a pack; rejecting it here
 * keeps the device rule identical to the repo rule. */
static bool findWebDictSuffix(File& f, size_t fsize, size_t& markerAt, size_t& bodyAt) {
 markerAt = 0;
 bodyAt = 0;
 char chunk[256];
 char dir[8]; /* longest directive we care about: "WEBDICT" */
 size_t dirLen = 0;
 size_t dirAt = 0, pos = 0;
 bool inDir = false, wantEol = false, atCol0 = true;
 f.seek(0);
 while (pos < fsize) {
 size_t got = f.readBytes(chunk, sizeof(chunk));
 if (got == 0) break;
 for (size_t k = 0; k < got; k++, pos++) {
 char c = chunk[k];
 if (markerAt && !wantEol && bodyAt && atCol0 && c == '@') return false;
 if (inDir) {
 if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
 if (dirLen == 7 && memcmp(dir, "WEBDICT", 7) == 0 && !markerAt) {
 markerAt = dirAt;
 wantEol = (c != '\n');
 if (!wantEol) bodyAt = pos + 1;
 }
 inDir = false;
 } else if (dirLen < sizeof(dir)) {
 dir[dirLen++] = c;
 } else {
 inDir = false; /* longer than any directive — not ours */
 }
 } else if (wantEol) {
 if (c == '\n') {
 bodyAt = pos + 1;
 wantEol = false;
 }
 } else if (atCol0 && c == '@') {
 inDir = true;
 dirLen = 0;
 dirAt = pos;
 }
 atCol0 = (c == '\n');
 }
 }
 if (markerAt && bodyAt == 0) bodyAt = fsize; /* marker line lacked a newline */
 return true;
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

 /* Find where the resident prefix ends BEFORE allocating anything — the
  * @WEBDICT suffix is served from flash by /api/lang and never loads. */
 size_t wdMarkerAt = 0, wdBodyAt = 0;
 if (!findWebDictSuffix(f, fsize, wdMarkerAt, wdBodyAt)) {
 f.close( );
 return false;
 }
 size_t residSize = wdMarkerAt ? wdMarkerAt : fsize;
 if (residSize > LANG_RESIDENT_MAX) {
 f.close( );
 return false;
 }

 /* +2: 1 to guarantee final \n terminator and 1 for closing '\0' */
 char* buf = (char*)malloc(residSize + 2);
 if (!buf) {
 f.close( );
 return false;
 }

 f.seek(0);
 size_t n = f.readBytes(buf, residSize);
 f.close( );
 if (n == 0) { free(buf); return false; }
 buf[n] = '\n'; /* force line end even if missing */
 buf[n+1] = '\0';
 n++;

 /* Maps boundaries of each section. bodyStart=0 means "absent". */
 enum SecIdx { S_DICT = 0, S_HELP, S_LICENSE, S_LOGCODES, S_TRL, S_COUNT };
 size_t secStart[S_COUNT] = { 0, 0, 0, 0, 0 };
 size_t secEnd[S_COUNT] = { 0, 0, 0, 0, 0 };

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
 size_t dictSize = secEnd[S_DICT] - secStart[S_DICT];
 char* dictBuf = (char*)malloc(dictSize + 2);
 if (!dictBuf) {
 free(buf);
 memset(&_activeLang, 0, sizeof(_activeLang));
 return false;
 }
 memcpy(dictBuf, buf + secStart[S_DICT], dictSize);
 dictBuf[dictSize] = '\n';
 dictBuf[dictSize + 1] = '\0';
 size_t dictN = dictSize + 1;

 int dictIdx = 0;
 size_t lineStart = 0;
 size_t dictEnd = dictN;

 for (size_t k = lineStart; k <= dictEnd; k++) {
 if (k == dictEnd || dictBuf[k] == '\n') {
 if (dictIdx < TR_KEYS_COUNT) {
 /* Mark end of line (if \n; \0 if k==dictEnd already guaranteed) */
 if (k < dictEnd) dictBuf[k] = '\0';
 /* Strip trailing \r */
 size_t lastChar = k;
 if (lastChar > lineStart && dictBuf[lastChar-1] == '\r') {
 dictBuf[lastChar-1] = '\0';
 }
 char* line = dictBuf + lineStart;
 _activeLang.strings[dictIdx++] = (line[0] == '\0') ? nullptr : line;
 }
 lineStart = k + 1;
 if (dictIdx >= TR_KEYS_COUNT) break;
 }
 }

 /* A file with no usable dictionary at all is still a bad file. */
 if (dictIdx == 0) {
 free(dictBuf);
 free(buf);
 memset(&_activeLang, 0, sizeof(_activeLang));
 return false;
 }
 for (int k = dictIdx; k < TR_KEYS_COUNT; k++) _activeLang.strings[k] = nullptr;
 if (dictIdx != TR_KEYS_COUNT) {
 LOG_CODE(LOG_WARN, "I18N", SYS_OK, dictIdx,
 TRL("Language pack is older than the firmware — missing strings show in English"));
 }

 /* @HELP / @LICENSE: byte ranges into the file, lazy-read on demand. */
 if (secEnd[S_HELP] > secStart[S_HELP]) {
 _activeLang.helpOffset = (uint32_t)secStart[S_HELP];
 _activeLang.helpLen = (uint32_t)(secEnd[S_HELP] - secStart[S_HELP]);
 }
 if (secEnd[S_LICENSE] > secStart[S_LICENSE]) {
 _activeLang.licenseOffset = (uint32_t)secStart[S_LICENSE];
 _activeLang.licenseLen = (uint32_t)(secEnd[S_LICENSE] - secStart[S_LICENSE]);
 }
 /* @WEBDICT: opaque JSON blob, served via GET /api/lang to browser.
  * Recorded as a byte range into the FILE — the suffix was never read into
  * the buffer, findWebDictSuffix() located it up front. The length runs to
  * end-of-file, which is byte-for-byte what the old excision formula served
  * (it only ever dropped the '\n' the loader itself appended). */
 if (wdMarkerAt) {
 _activeLang.webDictOffset = (uint32_t)wdBodyAt;
 _activeLang.webDictLen = (uint32_t)(fsize - wdBodyAt);
 }
 /* @LOGCODES / @TRL are intentionally not resident: their lookups fall
  * back to inline English. Free the transient prefix now that @DICT has
  * been copied out. */
 free(buf);

 /* Path is kept so /api/lang can reopen the file to stream @WEBDICT. */
 strncpy(_activeLang.path, path, sizeof(_activeLang.path) - 1);
 _activeLang.path[sizeof(_activeLang.path) - 1] = '\0';

 _activeLang.buffer = dictBuf;
 _activeLang.bufferSize = dictN;
 _activeLangLoaded = true;
 return true;
}

/* ─────────────────────────────────────────────────────────────────
 * Lookups. @LOGCODES / @TRL are no longer resident — their tables
 * were dropped from the pack loader to free heap, so both lookups
 * always answer "absent" and the callers fall back to inline EN.
 * ───────────────────────────────────────────────────────────────── */
const char* DisplayManager::logcodeLookup(uint16_t code) {
 (void)code;
 return nullptr;
}

const char* DisplayManager::trlLookup(const char* en) {
 (void)en;
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
 LOG_CODE(LOG_INFO, "I18N", APP_UI_LANG_CHANGED, 0,
 String(TRL("Language pack loaded: ")) + _activeLang.name);
 } else {
 LOG_CODE(LOG_ERROR, "I18N", SYS_STORAGE_FAIL, 0,
 String(TRL("Failed to parse language pack: ")) + path);
 }
 return ok;
}

/* Lazy-read scratch for @HELP / @LICENSE — these sections are no longer
 * resident, so they are read from LittleFS only when a consumer asks. */
static char _lazyReadBuf[2048];

static const char* lazyRead(const char* path, uint32_t offset, uint32_t len) {
 if (!path || len == 0) return nullptr;
 char* out = _lazyReadBuf;
 File f = LittleFS.open(path, "r");
 if (!f) return nullptr;
 f.seek(offset);
 size_t want = (len < sizeof(_lazyReadBuf) - 1) ? len : (sizeof(_lazyReadBuf) - 1);
 size_t n = f.readBytes(out, want);
 f.close();
 if (n == 0) return nullptr;
 out[n] = '\0';
 return out;
}

const char* DisplayManager::getActiveHelpText( ) {
 if (!_activeLangLoaded) return nullptr;
 return lazyRead(_activeLang.path, _activeLang.helpOffset, _activeLang.helpLen);
}
const char* DisplayManager::getActiveLicenseText( ) {
 if (!_activeLangLoaded) return nullptr;
 return lazyRead(_activeLang.path, _activeLang.licenseOffset, _activeLang.licenseLen);
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
 /* Spanish opening marks have no 7-bit form worth printing. Dropping
 * them reads right ("Sistema Listo!"); the default '?' below did not
 * ("?Sistema Listo!"), and the closing mark already tells the reader
 * whether the sentence is a question or an exclamation. */
 if (c2 == 0xA1 || c2 == 0xBF) { p += 2; continue; }
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
