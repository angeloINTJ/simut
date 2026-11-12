/**
 * @file DisplayManager_Fonts.cpp
 * @brief Single-owner of the Adafruit_GFX fonts used by DisplayManager.
 * @details Defines `simutFont{9,12,24}pt` as external references that
 * point to the PROGMEM structs defined by the headers
 * included here. DisplayManager sub-files use these
 * names via DisplayManager_Fonts.h without re-including the font
 * headers (which would duplicate the bitmaps in flash).
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager_Fonts.h"
/* Latin-1 bitmaps of Liberation Sans Bold (OFL-1.1; the first line of each
 * header names the TTF, its version and sha256): 0x20-0xFF at 9 and 12 pt,
 * digits and a few letters at 24 pt, yAdvance kept at 22, 29 and 56 so no
 * line moves. The .lng packs carry UTF-8: tr() maps it to Latin-1. */
#include "SansBold9pt8b_latin1.h"
#include "SansBold12pt8b_latin1.h"
#include "SansBold24pt7b_subset.h"

const GFXfont &simutFont9pt = SansBold9pt8b_latin1;
const GFXfont &simutFont12pt = SansBold12pt8b_latin1;
const GFXfont &simutFont24pt = SansBold24pt7b_subset;
