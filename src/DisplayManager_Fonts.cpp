/**
 * @file DisplayManager_Fonts.cpp
 * @brief Single-owner das fontes Adafruit_GFX usadas pelo DisplayManager.
 * @details Define `simutFont{9,12,24}pt` como referências externas que
 * apontam para os structs PROGMEM definidos pelos headers
 * incluídos aqui. Sub-arquivos do DisplayManager usam esses
 * nomes via DisplayManager_Fonts.h sem re-incluir os headers
 * de fonte (o que duplicaria os bitmaps em flash).
 *
 * REF-001 / F17 (split DisplayManager.cpp).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
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
