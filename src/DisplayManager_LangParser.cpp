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
 *
 * Memory strategy: @DICT is the only section that reaches the heap. The
 * loader streams the file once through a small stack chunk, and
 * LangPackScanner (LangPackIndex.h) records where every section starts and
 * ends; then it mallocs and reads @DICT alone, and _activeLang.strings point
 * into that buffer, null-terminated in place. Every other section stays on
 * flash as a byte range: @HELP is read when the CLI asks for it,
 * GET /api/lang streams @WEBDICT and GET /api/logcodes scans for @LOGCODES on
 * its own. @TRL is read by nothing on the device. Packs carried a @LICENSE
 * section until 2026-10-02; the License screen draws the firmware's own text
 * now, so in an older pack that section is an unknown directive and skipped.
 *
 * Until 2026-10-02 the loader read every byte before @WEBDICT into one malloc
 * (16,351 B for es-ES), copied @DICT out of it and freed the rest at once, so
 * its 16 KB ceiling was charged for bytes it threw away. The boot peak is now
 * the dictionary itself (2,912 B for es-ES), and LANG_DICT_MAX bounds what
 * actually stays.
 *
 * @WEBDICT must still be the file's suffix — the device refuses a pack with a
 * directive after it, and tools/check_lang_packs.py enforces the same order
 * at build time.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "LangPackIndex.h"
#include "LogManager.h"
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

DisplayManager::ActiveLang DisplayManager::_activeLang = {};
bool DisplayManager::_activeLangLoaded = false;

/* Defensive limits */
static constexpr size_t LANG_FILE_MIN = 64;
/* LANG_DICT_MAX bounds the one allocation a pack keeps for the whole uptime:
 * its @DICT. es-ES is 2,910 B and pt-BR 2,815 B, so a pack can grow by more
 * than its whole dictionary before this bites, while a hostile one — the pack
 * is a file anyone with PERM_FILE_UPLOAD can put here — can no longer hold the
 * 16 KB the old prefix ceiling allowed. LANG_FILE_MAX only bounds the file on
 * flash: everything outside @DICT is read from there, if at all. */
static constexpr size_t LANG_DICT_MAX = 6144;
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

/* @NAME / @CODE value, from the range the index found on the directive line.
 * Sanitised, not copied raw: these strings are served by /api/perms, which is
 * the first request every page of the UI makes, and the pack is a file anyone
 * with PERM_FILE_UPLOAD can put here. A quote in @NAME broke that response and
 * took the whole interface down until the next boot with a different pack
 * (V-04). Dropping the byte keeps the pack usable. The value is read through a
 * 64 B window: the shipped names are 24 B, and the field keeps 15 at most. */
static void readLangIdent(File& f, const LangPackIndex& ix, LangSection sec,
                          char* out, size_t cap) {
 if (!ix.present[sec]) return;
 char raw[64];
 size_t len = ix.end[sec] - ix.start[sec];
 if (len > sizeof(raw)) len = sizeof(raw);
 size_t got = 0;
 if (len > 0 && f.seek(ix.start[sec])) got = f.readBytes(raw, len);
 langIdentSanitize(raw, got, out, cap);
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

 /* Index every section BEFORE allocating anything: one pass through a stack
  * chunk, and only @DICT is read into RAM afterwards. */
 LangPackScanner scan;
 char chunk[256];
 f.seek(0);
 for (size_t seen = 0; seen < fsize; ) {
 size_t got = f.readBytes(chunk, sizeof(chunk));
 if (got == 0) break;
 scan.feed(chunk, got);
 seen += got;
 }
 LangPackIndex ix;
 scan.finish(ix);
 if (ix.tailAfterWebDict) {
 f.close( );
 return false;
 }

 /* DICT is mandatory; without it reject the file.
  *
  * A body longer than LANG_DICT_MAX is read only up to the ceiling. Lines past
  * TR_KEYS_COUNT were always ignored, and a pack that lost a directive line
  * has the next section run on into its @DICT — the prefix loader took that
  * pack and translated everything, so this one must too. It is refused only
  * when the ceiling cuts into the TR_KEYS_COUNT lines themselves: then the
  * dictionary really is too big. */
 size_t dictSize = ix.present[LANG_SEC_DICT]
 ? ix.end[LANG_SEC_DICT] - ix.start[LANG_SEC_DICT] : 0;
 if (dictSize == 0) {
 f.close( );
 return false;
 }
 const bool clipped = dictSize > LANG_DICT_MAX;
 if (clipped) dictSize = LANG_DICT_MAX;

 /* +2: 1 to guarantee final \n terminator and 1 for closing '\0' */
 char* dictBuf = (char*)malloc(dictSize + 2);
 if (!dictBuf) {
 f.close( );
 return false;
 }
 size_t n = f.seek(ix.start[LANG_SEC_DICT]) ? f.readBytes(dictBuf, dictSize) : 0;
 size_t lines = 0;
 if (clipped) {
 for (size_t k = 0; k < n; k++) lines += (dictBuf[k] == '\n');
 }
 if (n != dictSize || (clipped && lines < (size_t)TR_KEYS_COUNT)) {
 free(dictBuf);
 f.close( );
 return false;
 }
 readLangIdent(f, ix, LANG_SEC_NAME, _activeLang.name, sizeof(_activeLang.name));
 readLangIdent(f, ix, LANG_SEC_CODE, _activeLang.code, sizeof(_activeLang.code));
 f.close( );
 dictBuf[dictSize] = '\n';
 dictBuf[dictSize + 1] = '\0';
 size_t dictN = dictSize + 1;

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
 memset(&_activeLang, 0, sizeof(_activeLang));
 return false;
 }
 for (int k = dictIdx; k < TR_KEYS_COUNT; k++) _activeLang.strings[k] = nullptr;
 if (dictIdx != TR_KEYS_COUNT) {
 LOG_CODE(LOG_WARN, "I18N", SYS_OK, dictIdx,
 TRL("Language pack is older than the firmware — missing strings show in English"));
 }

 /* @HELP: byte range into the file, lazy-read on demand. */
 if (ix.end[LANG_SEC_HELP] > ix.start[LANG_SEC_HELP]) {
 _activeLang.helpOffset = ix.start[LANG_SEC_HELP];
 _activeLang.helpLen = ix.end[LANG_SEC_HELP] - ix.start[LANG_SEC_HELP];
 }
 /* @WEBDICT: opaque JSON blob, served via GET /api/lang to the browser
  * straight from flash. Its range runs to the end of the file, which is
  * byte for byte what the excision and the prefix loaders served before. */
 if (ix.end[LANG_SEC_WEBDICT] > ix.start[LANG_SEC_WEBDICT]) {
 _activeLang.webDictOffset = ix.start[LANG_SEC_WEBDICT];
 _activeLang.webDictLen = ix.end[LANG_SEC_WEBDICT] - ix.start[LANG_SEC_WEBDICT];
 }

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

/* Lazy-read scratch for @HELP — the section is not resident, so it is read
 * from LittleFS only when the CLI asks for it. */
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
