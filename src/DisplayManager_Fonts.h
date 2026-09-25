/**
 * @file DisplayManager_Fonts.h
 * @brief Font wrappers shared across DisplayManager_*.cpp.
 * @details C++ treats `const GFXfont` at namespace scope as internal linkage
 * (file-static), so each .cpp that `#include <Fonts/...>` gets
 * its own copy of the bitmaps in flash. To avoid duplication,
 * fonts are defined ONCE in DisplayManager_Fonts.cpp and
 * re-exported as references with external linkage.
 *
 * Use `&simutFont9pt`, `&simutFont12pt`, `&simutFont24pt` instead
 * of `&FreeSansBoldXpt7b` in all DisplayManager
 * sub-files. Each reference points to the struct defined
 * in DisplayManager_Fonts.cpp; bitmaps/glyphs live only in that TU.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Adafruit_GFX.h>
#include "simut_config.h" /* SIMUT_UI_STUDY */

extern const GFXfont &simutFont9pt;
extern const GFXfont &simutFont12pt;
extern const GFXfont &simutFont24pt;

#if SIMUT_UI_STUDY
/* The Ângulo study adds the two families the standard names (§3.2): the
 * display face for titles, the mark and the main number, and a regular
 * weight of the text face so `corpo`/`apoio` (400) can differ from `rotulo`
 * and buttons (600/500 — the bold above). Sizes are measured, not chosen:
 * 12 pt Bricolage is the title/mark, 24 pt digits the card number, 36 pt
 * digits the focus layout's number. */
extern const GFXfont &simutFontDisplay12;
extern const GFXfont &simutFontDisplay24;
extern const GFXfont &simutFontDisplay36;
extern const GFXfont &simutFontText9;
#endif
