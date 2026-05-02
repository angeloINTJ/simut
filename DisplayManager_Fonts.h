/**
 * @file    DisplayManager_Fonts.h
 * @brief   Font wrappers compartilhados entre DisplayManager_*.cpp.
 * @details C++ trata `const GFXfont` em namespace scope como internal linkage
 *          (file-static), então cada .cpp que `#include <Fonts/...>` recebe
 *          sua própria cópia dos bitmaps em flash. Para evitar duplicação,
 *          as fontes são definidas UMA VEZ em DisplayManager_Fonts.cpp e
 *          re-exportadas como referencias com external linkage.
 *
 *          Use `&simutFont9pt`, `&simutFont12pt`, `&simutFont24pt` em vez
 *          de `&FreeSansBoldXpt7b` em todos os sub-arquivos do
 *          DisplayManager. Cada referência aponta para o struct definido
 *          em DisplayManager_Fonts.cpp; bitmaps/glyphs vivem só nesse TU.
 *
 *          REF-001 / F17 etapa 8 (split DisplayManager.cpp).
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Adafruit_GFX.h>

extern const GFXfont &simutFont9pt;
extern const GFXfont &simutFont12pt;
extern const GFXfont &simutFont24pt;
