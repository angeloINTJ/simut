/**
 * @file    DisplayManager_LangParser.cpp
 * @brief   F-LANGPACK Etapa 1: parser .lng + storage do _activeLang + unaccent.
 * @details Carrega arquivos /lang/language_<code>.lng em runtime para
 *          permitir traduções da UI sem reflashar o firmware. EN
 *          permanece hardcoded em DICTIONARY_EN (DisplayManager_i18n.cpp).
 *          Apenas 1 slot ativo por vez; loadLangFile libera o anterior.
 *
 *          Em Etapa 1 estas funções existem mas ainda não são chamadas
 *          (Etapa 2 adiciona o scan /lang/ no boot e a UI dinâmica).
 *
 *          Formato .lng (diretivas em coluna 0):
 *              @NAME <texto exibido>
 *              @CODE <2-3 chars>
 *              @DICT
 *              <linha 1 = TR_AMBIENT>
 *              <linha 2 = TR_CONFIG_MAIN>
 *              ...
 *              <linha N = última LangKey antes de TR_KEYS_COUNT>
 *              @HELP
 *              <texto livre, multilinhas>
 *              @LICENSE
 *              <texto livre, multilinhas>
 *
 *          Estratégia de memória: alocação única (malloc do tamanho do
 *          arquivo). Pointers em _activeLang.strings/helpText/licenseText
 *          apontam para dentro desse buffer; null-termination feita
 *          modificando o buffer in-place.
 *
 * @project SIMUT
 * @license MIT License
 */

#include "DisplayManager.h"
#include "LogManager.h"
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

DisplayManager::ActiveLang DisplayManager::_activeLang = {};
bool DisplayManager::_activeLangLoaded = false;

/* Limites defensivos */
static constexpr size_t LANG_FILE_MIN = 64;
static constexpr size_t LANG_FILE_MAX = 32768;   /* ~32 KB envelope (DICT+LOGCODES+TRL+HELP+LIC) */

uint32_t DisplayManager::fnv1a32(const char* s) {
    uint32_t h = 0x811c9dc5u;
    if (!s) return h;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x01000193u;
    }
    return h;
}

void DisplayManager::unloadLang() {
    if (_activeLang.buffer)   free(_activeLang.buffer);
    if (_activeLang.logcodes) free(_activeLang.logcodes);
    if (_activeLang.trls)     free(_activeLang.trls);
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

/* Conta linhas não-vazias em [start, end) — para dimensionar arrays. */
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
    unloadLang();
    if (!path) return false;

    File f = LittleFS.open(path, "r");
    if (!f) return false;

    size_t fsize = f.size();
    if (fsize < LANG_FILE_MIN || fsize > LANG_FILE_MAX) {
        f.close();
        return false;
    }

    /* +2: 1 para garantir terminador final \n e 1 para '\0' de fechamento */
    char* buf = (char*)malloc(fsize + 2);
    if (!buf) {
        f.close();
        return false;
    }

    size_t n = f.readBytes(buf, fsize);
    f.close();
    if (n == 0) { free(buf); return false; }
    buf[n]   = '\n';   /* força fim de linha mesmo se faltava */
    buf[n+1] = '\0';
    n++;

    /* Mapeia limites de cada seção. bodyStart=0 significa "ausente". */
    enum SecIdx { S_DICT = 0, S_HELP, S_LICENSE, S_LOGCODES, S_TRL, S_COUNT };
    size_t secStart[S_COUNT] = { 0, 0, 0, 0, 0 };
    size_t secEnd[S_COUNT]   = { 0, 0, 0, 0, 0 };

    int   curSec  = -1;
    size_t i      = 0;

    while (i < n) {
        bool atColZero = (i == 0) || buf[i-1] == '\n';
        if (!(atColZero && buf[i] == '@')) { i++; continue; }

        /* Fecha seção corrente (HELP/LICENSE/DICT) */
        if (curSec >= 0) secEnd[curSec] = i;

        /* Identifica diretiva: @NAME, @CODE, @DICT, @HELP, @LICENSE */
        size_t dirStart = i + 1;
        size_t dirEnd   = dirStart;
        while (dirEnd < n && buf[dirEnd] != ' ' && buf[dirEnd] != '\t' &&
               buf[dirEnd] != '\n' && buf[dirEnd] != '\r') dirEnd++;
        size_t dirLen = dirEnd - dirStart;

        /* Avança i até depois do \n da linha da diretiva */
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
            curSec = -1;     /* diretiva desconhecida — ignora */
        }

        i = bodyAfter;
    }
    if (curSec >= 0) secEnd[curSec] = n;

    /* DICT é obrigatório; sem ele rejeita o arquivo */
    if (secStart[S_DICT] == 0 || secEnd[S_DICT] <= secStart[S_DICT]) {
        free(buf);
        memset(&_activeLang, 0, sizeof(_activeLang));
        return false;
    }

    /* Particiona o bloco @DICT em linhas; cada linha vira uma string.
     * Exatamente TR_KEYS_COUNT linhas exigidas. */
    int dictIdx   = 0;
    size_t lineStart = secStart[S_DICT];
    size_t dictEnd   = secEnd[S_DICT];

    for (size_t k = lineStart; k <= dictEnd; k++) {
        if (k == dictEnd || buf[k] == '\n') {
            if (dictIdx < TR_KEYS_COUNT) {
                /* Marca fim da linha (se for \n; \0 se k==dictEnd já garantido por buf[n]) */
                if (k < dictEnd) buf[k] = '\0';
                /* Strip \r final */
                size_t lastChar = k;
                if (lastChar > lineStart && buf[lastChar-1] == '\r') {
                    buf[lastChar-1] = '\0';
                }
                _activeLang.strings[dictIdx++] = buf + lineStart;
            }
            lineStart = k + 1;
            if (dictIdx >= TR_KEYS_COUNT) break;
        }
    }

    if (dictIdx != TR_KEYS_COUNT) {
        free(buf);
        memset(&_activeLang, 0, sizeof(_activeLang));
        return false;
    }

    /* HELP e LICENSE: preserva newlines, apenas null-termina no fim */
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

    /* @LOGCODES: cada linha "<decimal_id> <texto>", split no primeiro espaço.
     * Linhas vazias ou sem espaço são puladas. */
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

    /* @TRL: cada linha "<hex_hash> <texto>" (hash em ASCII hex sem 0x).
     * Permite gerar via tooling Python: hex(fnv1a32(en)). */
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

    _activeLang.buffer     = buf;
    _activeLang.bufferSize = n;
    _activeLangLoaded      = true;
    return true;
}

/* ─────────────────────────────────────────────────────────────────
 * Lookups: binary search nas tabelas montadas pelo parser.
 * Retornam nullptr quando .lng não está carregado, quando o caller
 * está em EN, ou quando a key não tem entrada. Caller deve fallback
 * para o EN inline.
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
 * findAndLoadLangFile (Etapa 2): scan /lang/ por arquivos
 * "language_*.lng", carrega o primeiro alfabeticamente. Loga warning
 * se houver extras (mais de 1 arquivo encontrado). Apenas Core 0.
 * ───────────────────────────────────────────────────────────────── */
bool DisplayManager::findAndLoadLangFile() {
    char firstName[40] = {0};
    int  count = 0;

    Dir dir = LittleFS.openDir("/lang");
    while (dir.next()) {
        String fn = dir.fileName();
        /* Aceita "language_*.lng" exatamente; case-sensitive proposital. */
        if (!fn.startsWith("language_") || !fn.endsWith(".lng")) continue;
        count++;
        if (count == 1) {
            strncpy(firstName, fn.c_str(), sizeof(firstName) - 1);
        } else {
            /* Mantém o menor (alfabético). LittleFS::openDir não
             * garante ordem; comparação manual cobre o caso. */
            if (strcmp(fn.c_str(), firstName) < 0) {
                strncpy(firstName, fn.c_str(), sizeof(firstName) - 1);
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

/* ─────────────────────────────────────────────────────────────────
 * unaccent: UTF-8 (Latin-1 subset) → ASCII 7-bit.
 *
 * Cobre acentos comuns em PT/ES/FR/DE: à á â ã ä å æ ç è é ê ë ì í î
 * ï ñ ò ó ô õ ö ø ù ú û ü ý ÿ + maiúsculas correspondentes.
 *
 * UTF-8 representa esses chars em 2 bytes: 0xC2/0xC3 + segundo byte.
 * Caracteres ASCII (< 0x80) são copiados literalmente. Sequências
 * UTF-8 multi-byte fora da tabela são pulados (1 byte só, prevenindo
 * loop infinito) — comportamento aceitável para fonte limitada do
 * display.
 *
 * NÃO chamado em Etapa 1 (UI/CLI ainda servem do DICTIONARY_EN ASCII).
 * Existe para Etapa 2/3 quando _activeLang carrega UTF-8 do .lng.
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
        if (c == 0xC3) {  /* 0xC0..0xFF */
            switch (c2) {
                case 0x80: case 0x81: case 0x82: case 0x83:
                case 0x84: case 0x85:                 repl = 'A'; break;
                case 0x86:                            repl = 'A'; break;  /* Æ */
                case 0x87:                            repl = 'C'; break;
                case 0x88: case 0x89: case 0x8A:
                case 0x8B:                            repl = 'E'; break;
                case 0x8C: case 0x8D: case 0x8E:
                case 0x8F:                            repl = 'I'; break;
                case 0x91:                            repl = 'N'; break;
                case 0x92: case 0x93: case 0x94:
                case 0x95: case 0x96: case 0x98:      repl = 'O'; break;
                case 0x99: case 0x9A: case 0x9B:
                case 0x9C:                            repl = 'U'; break;
                case 0x9D:                            repl = 'Y'; break;
                case 0xA0: case 0xA1: case 0xA2: case 0xA3:
                case 0xA4: case 0xA5:                 repl = 'a'; break;
                case 0xA6:                            repl = 'a'; break;  /* æ */
                case 0xA7:                            repl = 'c'; break;
                case 0xA8: case 0xA9: case 0xAA:
                case 0xAB:                            repl = 'e'; break;
                case 0xAC: case 0xAD: case 0xAE:
                case 0xAF:                            repl = 'i'; break;
                case 0xB1:                            repl = 'n'; break;
                case 0xB2: case 0xB3: case 0xB4:
                case 0xB5: case 0xB6: case 0xB8:      repl = 'o'; break;
                case 0xB9: case 0xBA: case 0xBB:
                case 0xBC:                            repl = 'u'; break;
                case 0xBD: case 0xBF:                 repl = 'y'; break;
                default:                              repl = '?'; break;
            }
            out[o++] = repl;
            p += 2;
        } else if (c == 0xC2) {
            /* Latin-1 supplement (0x80..0xBF): símbolos como °, ±, ², ³, ©.
             * Substituições simples; resto vira '?'. */
            switch (c2) {
                case 0xA9: repl = 'C'; break;  /* © */
                case 0xAE: repl = 'R'; break;  /* ® */
                case 0xB0: repl = 'o'; break;  /* ° */
                case 0xB1: repl = '+'; break;  /* ± */
                case 0xB2: repl = '2'; break;  /* ² */
                case 0xB3: repl = '3'; break;  /* ³ */
                default:   repl = '?'; break;
            }
            out[o++] = repl;
            p += 2;
        } else {
            /* UTF-8 multi-byte fora do alvo: avança 1 byte, marca '?' */
            out[o++] = '?';
            p++;
        }
    }
    out[o] = '\0';
}
