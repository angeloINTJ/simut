/**
 * @file Temas.cpp
 * @brief The Ângulo's theme catalogue: its 18 choices, the three modes (light, dark, automatic) each with one accent of the palette. They give cfg.themeIndex its names where the web (/api/themes) and the
 *        console list them, and index t keeps the meaning ua::escolhaDoIndice( t ) gives it (src/ui/angulo/Angulo.cpp): the mode is the remainder and the accent the quotient of a division by the number of
 *        modes. The first three are the stock accent and keep the ids and names they always had. Defines the six functions of src/ThemeCatalog.h; the original's definitions (src/Themes.cpp, the palettes and the .thm
 *        files) are not linked into this image. The colours are the default's: nothing this build puts on the glass is drawn with them. A .thm uploaded to this build is stored and neither offered nor applied: scanCustomThemes( ) does
 *        nothing, and the upload and delete handlers do not call it here (SIMUT_UI_CUSTOM_THEMES is 0; S10c, A'4); only boot does.
 */
#include "../../ThemeCatalog.h"   /* core: the type and the six functions; Themes.h is the original's */

#include <Arduino.h>   // the tokens header includes it: a system header first included inside the namespace below would land in it
namespace entrega {
#include "kit/angulo_tokens.h"   // the palette's size, for the static_assert below
}  // namespace entrega

/* RGB565 and the stock state block as Themes.h and Themes.cpp define them for the original: the rows below are theirs, word for word. */
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define THM_STATE_DARK \
 RGB565(255, 230, 0), RGB565(0, 0, 0), RGB565(70, 70, 75), RGB565(255, 60, 60), RGB565(180, 90, 0), RGB565(50, 50, 55), RGB565(190, 170, 60)
/* Every row carries the same colour block, the default's. */
#define THM_PADRAO \
 RGB565(18, 18, 20), RGB565(35, 38, 45), RGB565(245, 245, 245), RGB565(180, 180, 185), RGB565(90, 90, 100), RGB565(0, 150, 255), RGB565(50, 200, 255), RGB565(50, 50, 60), \
 RGB565(255, 60, 60), RGB565(255, 170, 0), RGB565(0, 255, 2), RGB565(0, 200, 255), RGB565(0, 121, 255) , RGB565(180, 180, 185), RGB565(245, 245, 245), RGB565(245, 245, 245) , RGB565(0, 0, 0) , THM_STATE_DARK
#define TEMA(id, nome) { id, nome, THM_PADRAO }

namespace {
/* Index t is mode t % 3 with accent t / 3: light, dark, automatic for the stock accent, then for each of the others. The ids are ASCII, at most 15 B (the .thm
 * code's size); the names are at most 23 B of UTF-8 (its name's size); tools/tft_ui/test_temas.py holds both, and each row to its index.
 * The colour follows the comma in lower case, as temaCor( ) (telas/MenusConfig.cpp) writes it in pt-BR: sentence case, ANGULO.md section 7 (S10c, A'3b).
 * The names are not looked up (UA_TXT): GET /api/themes serves them to the web page as they are and the page writes them into its select as it receives them (WebUI.h,
 * loadThemes( )), so the web shows these pt-BR words in every language, as it always did; a catalogue that follows the language changes that API's contract and is not
 * decided here (S10c, B4a; tools/ui_texts_allow.json names the reason). tools/tft_ui/test_temas.py holds each name equal to the pt-BR pack's words for the Tema screen's. */
const ThemePalette kTemas[] = {
 TEMA( "claro", "Claro" ),
 TEMA( "escuro", "Escuro" ),
 TEMA( "auto", "Automático" ),
 TEMA( "claro-petroleo", "Claro, petróleo" ),
 TEMA( "escuro-petroleo", "Escuro, petróleo" ),
 TEMA( "auto-petroleo", "Automático, petróleo" ),
 TEMA( "claro-azul", "Claro, azul" ),
 TEMA( "escuro-azul", "Escuro, azul" ),
 TEMA( "auto-azul", "Automático, azul" ),
 TEMA( "claro-indigo", "Claro, índigo" ),
 TEMA( "escuro-indigo", "Escuro, índigo" ),
 TEMA( "auto-indigo", "Automático, índigo" ),
 TEMA( "claro-violeta", "Claro, violeta" ),
 TEMA( "escuro-violeta", "Escuro, violeta" ),
 TEMA( "auto-violeta", "Automático, violeta" ),
 TEMA( "claro-magenta", "Claro, magenta" ),
 TEMA( "escuro-magenta", "Escuro, magenta" ),
 TEMA( "auto-magenta", "Automático, magenta" ),
};
constexpr int NUM_TEMAS = sizeof( kTemas ) / sizeof( kTemas[0] );
static_assert( NUM_TEMAS == 3 * entrega::ACENTOS_N, "every mode comes with every accent of the palette: index t is mode t % 3, accent t / 3 (ua::escolhaDoIndice)" );
}   // namespace

const ThemePalette* getThemePalette(int index) { return &kTemas[ ( index >= 0 && index < NUM_TEMAS ) ? index : 0 ]; }

void scanCustomThemes( ) { }

void loadTheme(int index) { (void)index; }

int getThemeCount( ) { return NUM_TEMAS; }

String getThemeId(int index) { return String(getThemePalette(index)->idName); }

int getThemeIndexByName(String name) {
 int total = getThemeCount( );
 for (int i = 0; i < total; i++) {
  if (String(getThemePalette(i)->idName).equalsIgnoreCase(name)) return i;
 }
 for (int i = 0; i < total; i++) {
  if (String(getThemePalette(i)->idName).indexOf(name) >= 0) return i;
 }
 return -1;
}
