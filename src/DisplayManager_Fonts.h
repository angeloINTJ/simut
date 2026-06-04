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
 * @project SIMUT - Integrated Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Adafruit_GFX.h>

extern const GFXfont &simutFont9pt;
extern const GFXfont &simutFont12pt;
extern const GFXfont &simutFont24pt;
