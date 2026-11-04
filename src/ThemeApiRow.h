/* ThemeApiRow.h - one row of GET /api/themes, as a pure function (2026-10-28).
 *
 * The route lists the themes as
 *     [{"id":<index>,"code":"<idName>","name":"<displayName>"}, ...]
 * "code" is the theme's @CODE, the key the web page builds its translation from
 * (thm_<code> in the active language pack's @WEBDICT); "name" is what the device
 * keeps: English for the stock themes (tools/gen_theme_collection.py), whatever a
 * user's .thm says for the others. A page that finds no key shows the name.
 *
 * It lives here, not in WebManager_Api.cpp, so that the native suite compiles the
 * function the handler runs (test/test_validators, test_theme_row_*). */
#pragma once

#include <stddef.h>
#include <stdio.h>

/* The longest row, ,{"id":99,"code":"<15 B>","name":"<31 B>"}, is 76 B and 77 with
 * the NUL; an id is an index and stays far below 1000. The native suite sweeps
 * ids, code lengths and name lengths and shows no row is cut at 96. */
constexpr size_t THEME_ROW_MAX = 96;

/* Copies `src` into `dst` (`cap` bytes, NUL included) without the bytes that would
 * break a JSON string: '"', '\\' and anything below 32. UTF-8 bytes pass through. It
 * stops at cap - 1 bytes, which may split a multi-byte character, as it always did.
 * Returns the length written; a null `src` gives "". */
inline int themeCleanCopy(char* dst, size_t cap, const char* src) {
    size_t n = 0;
    if (src) {
        for (; *src && n + 1 < cap; src++) {
            unsigned char c = (unsigned char)*src;
            if (c >= 32 && c != '"' && c != '\\') dst[n++] = (char)c;
        }
    }
    dst[n] = '\0';
    return (int)n;
}

/* One row, led by the comma that separates it from the row before when index > 0.
 * A name with nothing left in it becomes "Theme <index>". Returns snprintf's value,
 * the length of the row, which is >= outSize only if the row was cut. */
inline int formatThemeRow(char* out, size_t outSize, int index, const char* idName, const char* displayName) {
    char code[16];
    char name[32];
    themeCleanCopy(code, sizeof(code), idName);
    if (themeCleanCopy(name, sizeof(name), displayName) == 0) snprintf(name, sizeof(name), "Theme %d", index);
    return snprintf(out, outSize, "%s{\"id\":%d,\"code\":\"%s\",\"name\":\"%s\"}",
                    index > 0 ? "," : "", index, code, name);
}
