/**
 * @file Temas.cpp
 * @brief The Ângulo's theme catalogue: its three choices (light, dark, automatic), which give cfg.themeIndex its names where the web (/api/themes) and the
 *        console list them. Defines the six functions of src/ThemeCatalog.h; the original's definitions (src/Themes.cpp, the palettes and the .thm files) are not
 *        linked into this image. The colours are the default's: nothing this build puts on the glass is drawn with them. A .thm uploaded to this build is stored and neither offered nor applied: scanCustomThemes( ) does nothing, and the upload and delete handlers do not call it here (SIMUT_UI_CUSTOM_THEMES is 0; S10c, A'4); only boot does.
 */
#include "../../ThemeCatalog.h"   /* core: the type and the six functions; Themes.h is the original's */

/* RGB565 and the stock state block as Themes.h and Themes.cpp define them for the original: the rows below are theirs, word for word. */
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define THM_STATE_DARK \
 RGB565(255, 230, 0), RGB565(0, 0, 0), RGB565(70, 70, 75), RGB565(255, 60, 60), RGB565(180, 90, 0), RGB565(50, 50, 55), RGB565(190, 170, 60)

namespace {
const ThemePalette kTemas[] = {
 { "claro", "Claro",
 RGB565(18, 18, 20), RGB565(35, 38, 45), RGB565(245, 245, 245), RGB565(180, 180, 185), RGB565(90, 90, 100), RGB565(0, 150, 255), RGB565(50, 200, 255), RGB565(50, 50, 60),
 RGB565(255, 60, 60), RGB565(255, 170, 0), RGB565(0, 255, 2), RGB565(0, 200, 255), RGB565(0, 121, 255) , RGB565(180, 180, 185), RGB565(245, 245, 245), RGB565(245, 245, 245) , RGB565(0, 0, 0) , THM_STATE_DARK },
 { "escuro", "Escuro",
 RGB565(18, 18, 20), RGB565(35, 38, 45), RGB565(245, 245, 245), RGB565(180, 180, 185), RGB565(90, 90, 100), RGB565(0, 150, 255), RGB565(50, 200, 255), RGB565(50, 50, 60),
 RGB565(255, 60, 60), RGB565(255, 170, 0), RGB565(0, 255, 2), RGB565(0, 200, 255), RGB565(0, 121, 255) , RGB565(180, 180, 185), RGB565(245, 245, 245), RGB565(245, 245, 245) , RGB565(0, 0, 0) , THM_STATE_DARK },
 { "auto", "Automático",
 RGB565(18, 18, 20), RGB565(35, 38, 45), RGB565(245, 245, 245), RGB565(180, 180, 185), RGB565(90, 90, 100), RGB565(0, 150, 255), RGB565(50, 200, 255), RGB565(50, 50, 60),
 RGB565(255, 60, 60), RGB565(255, 170, 0), RGB565(0, 255, 2), RGB565(0, 200, 255), RGB565(0, 121, 255) , RGB565(180, 180, 185), RGB565(245, 245, 245), RGB565(245, 245, 245) , RGB565(0, 0, 0) , THM_STATE_DARK },
};
constexpr int NUM_TEMAS = sizeof( kTemas ) / sizeof( kTemas[0] );
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
