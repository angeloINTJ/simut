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
