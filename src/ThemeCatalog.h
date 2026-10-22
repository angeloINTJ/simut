/**
 * @file ThemeCatalog.h
 * @brief The theme catalogue's interface: the palette type and the six functions that count, name, load and scan themes. Core: every image calls
 *        them (the web list, the console, the boot, the saved choice) and every image links one definition. The original's is src/Themes.cpp, with its
 *        palettes and the .thm files; the Ângulo's is src/ui/angulo/Temas.cpp, its three choices. Moved word for word out of src/Themes.h (S10b).
 */
#pragma once

#include <Arduino.h>

/** Color palette definition for the TFT display theme system. */
struct ThemePalette {
 const char* idName;
 const char* displayName;

 uint16_t bgMain;
 uint16_t cardBg;
 uint16_t textMain;
 uint16_t textSub;
 uint16_t textOff;
 uint16_t accent;
 uint16_t accentHigh;
 uint16_t barBg;

 uint16_t tempHot;
 uint16_t tempWarm;
 uint16_t tempOk;
 uint16_t tempCold;
 uint16_t humidity;

 /* Additional colors: text type separation. Allows custom
 * themes to differentiate button labels, titles and sensor names. */
 uint16_t btnText; /**< Text for inactive buttons (S0..S9, CFG, page) */
 uint16_t titleText; /**< Top bar (date/time) and menu titles */
 uint16_t sensorName; /**< Sensor name in slot panel */
 uint16_t btnTextActive; /**< Text of SELECTED slot button (over accentHigh) */

 /* State colors — alarm/caution/selection chrome and the graph stamp.
 * Historically hardcoded in the render code; themeable since the
 * blind-spot sweep. Every text/bg pair here must hold contrast on
 * its own: alarmText/alarmTextDim sit on alarmBg (amarelo brilhante do
 * alarme de LIMITE — preto p/ contraste); selBg e cautionBg desenham o
 * próprio texto claro (C_TEXT_MAIN / C_ALARM_ERR_TEXT); alarmBorder
 * (vermelho) senta em bgMain e cardBg e anela o BOTÃO DE PÁGINA com
 * alarmes em outras páginas — a moldura do painel em alarme alterna
 * NORMAL ↔ amarelo (alarmBg) no limite / branco no erro. */
 uint16_t alarmBg; /**< Alarm fill: flashing panel/buttons, destructive confirm */
 uint16_t alarmText; /**< Primary text over alarmBg (preto no amarelo) */
 uint16_t alarmTextDim; /**< Secondary text/icons over alarmBg */
 uint16_t alarmBorder; /**< Alarm outline: panel border, page-button ring */
 uint16_t cautionBg; /**< Caution action fill (silence button) */
 uint16_t selBg; /**< Top-panel background while selecting a slot */
 uint16_t stampText; /**< Date/time stamps in the graph detail rows */
};

void loadTheme(int index);
int getThemeCount( );
String getThemeId(int index);
int getThemeIndexByName(String name);

/** Returns palette (built-in or custom RAM). NEVER nullptr — out of
 * range falls to theme 0 (simut_def). Use this instead of availableThemes[i]. */
const ThemePalette* getThemePalette(int index);

/** Scans .thm files in /themes/ on LittleFS and populates the custom themes
 * array. Call once at boot (after FS mount) and on hot-reload after upload. */
void scanCustomThemes( );
