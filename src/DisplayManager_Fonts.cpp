/**
 * @file DisplayManager_Fonts.cpp
 * @brief Single-owner of the Adafruit_GFX fonts used by DisplayManager.
 * @details Defines `simutFont{9,12,24}pt` as external references that
 * point to the PROGMEM structs defined by the headers
 * included here. DisplayManager sub-files use these
 * names via DisplayManager_Fonts.h without re-including the font
 * headers (which would duplicate the bitmaps in flash).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager_Fonts.h"
/* Latin-1 builds of the same GNU FreeSansBold the stock 7b fonts came
 * from: ASCII glyphs are bit-identical, 0xA0-0xFF adds real accents and
 * the degree sign. The .lng packs already carry UTF-8 — tr() now maps
 * them to Latin-1 instead of transliterating to ASCII. */
#include "FreeSansBold9pt8b_latin1.h"
#include "FreeSansBold12pt8b_latin1.h"
#include "FreeSansBold24pt7b_subset.h"

const GFXfont &simutFont9pt = FreeSansBold9pt8b;
const GFXfont &simutFont12pt = FreeSansBold12pt8b;
const GFXfont &simutFont24pt = FreeSansBold24pt7b;

#if SIMUT_UI_STUDY
/* Bricolage Grotesque SemiBold (OFL, docs/assets/fonts/) rendered by
 * tools/gen_gfx_font.py at the same 141 DPI as the faces above, then cut to
 * the glyph list FreeSansBold9pt8b_latin1.h ships (128 glyphs) or to the
 * thirteen digit glyphs 0x2D..0x39. FreeSans regular comes from the same GNU
 * FreeFont release the bold does. Measured cost, .bin: 12 pt 2,872 B of
 * bitmaps + 1,568 B of table; 24 pt digits 1,146 B; 36 pt digits 2,440 B;
 * FreeSans 9 pt 1,675 + 1,568 B. */
#include "Bricolage600_12pt8b_latin1.h"
#include "Bricolage600_24pt8b_digits.h"
#include "Bricolage600_36pt8b_digits.h"
#include "FreeSans9pt8b_latin1.h"
const GFXfont &simutFontDisplay12 = Bricolage600_12pt8b;
const GFXfont &simutFontDisplay24 = Bricolage600_24pt8b;
const GFXfont &simutFontDisplay36 = Bricolage600_36pt8b;
const GFXfont &simutFontText9 = FreeSans9pt8b;
#endif
