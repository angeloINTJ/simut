/**
 * @file DisplayManager_Fonts.cpp
 * @brief Single-owner of the Adafruit_GFX fonts used by DisplayManager.
 * @details Defines `simutFont{9,12,24}pt` as external references that
 * point to the PROGMEM structs defined by the headers
 * included here. DisplayManager sub-files use these
 * names via DisplayManager_Fonts.h without re-including the font
 * headers (which would duplicate the bitmaps in flash).
 *
 * @project SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager_Fonts.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "FreeSansBold24pt7b_subset.h"

const GFXfont &simutFont9pt = FreeSansBold9pt7b;
const GFXfont &simutFont12pt = FreeSansBold12pt7b;
const GFXfont &simutFont24pt = FreeSansBold24pt7b;
