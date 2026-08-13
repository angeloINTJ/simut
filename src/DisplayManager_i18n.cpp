/**
 * @file DisplayManager_i18n.cpp
 * @brief i18n: DICTIONARY_EN (hardcoded), dynamic tr(), language settings screen.
 * @details DICTIONARY is now 1D EN only. Slot 1
 * of the menu shows _activeLang.name/code dynamically (via
 * unaccent for ASCII on the TFT). Without .lng loaded, slot 1
 * stays hidden (gated by _activeLangLoaded).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "LogManager.h"
#include "sensors/SensorChannelTable.h"   /* CH_COUNT, channelInfo, channelValid */
#include "UiWidgets.h"

static const char* const DICTIONARY_EN[TR_KEYS_COUNT] = {
 "AMBIENT", "Settings > Main", "Settings > Themes", "Settings > Language", "EXIT",
 "APPLY", "CANCEL", "Security Authentication", "ACCESS BLOCKED", "Reboot required",
 "Attempts Exceeded", "Wait %ld seconds...", "Invalid Password!", "Loading...", "Reading History...",
 "No Data", "MAX", "MIN", "Temperature", "Humidity",
 "PLOT CHART", "1. Visual Themes", "2. Alarm Limits", "3. Alarm Sounds", "4. System Language",
 "Applying Theme...", "SAVE", "Alarm Limits", "Temp Min", "Temp Max",
 "Hum Min", "Hum Max", "ENTER", "SKIP", "5. Change Password",
 "New Password", "6. Touch Calibration", "Touch Calibration", "Touch the crosshair", "Calibration Done!",
 "Imprecise touches! Try again.", "Confirm Password", "Password too short! (min 4)", "Passwords don't match!", "Password saved!",
 "UNDERSTOOD", "Sound Settings", "Touch Click", "Confirmation", "Error Sound",
 "Alarm Sound", "Mute All", "Sys Volume", "Alarm Vol", "ON",
 "OFF", "Web Access", "Melody", "7. License", "MIT License",
 "ACTIVE", "Silence 120s", "Deactivate", "Min/Max", "Silenced",
 "%RH", "7. Touch Sensitivity", "Touch Sensitivity", "Tap %d/%d", "Calibration Done!",
 /* "AVG", not "AVERAGE": the graph detail rows fit label + value + unit +
 * full date/time stamp in 320 px only with the short form. Text-only
 * change — .lng packs are positional and unaffected. */
 "AVG", "STD DEV", "Error", "Configuration Mode", "8. System Status",
 "System Status",
 "9. Display Alignment", "Display Alignment", "Adjust +/-4 px. Saving clears touch calibration.",
 /* v3.31.2 boot terminal i18n */
 "Hold screen for AP Mode...", "AP Mode Cancelled.",
 "Mounting File System...", "Starting Log Manager...", "Starting Command Interface...",
 "Loading Theme & Language...", "Touch calibration required...",
 "Loading Peripherals & Sensors...", "Starting Access Point (AP)...",
 "Connect to network SIMUT_SETUP", "Access on mobile: 192.168.4.1",
 "Starting Wi-Fi Interface...", "Connection Skipped by User.",
 "Waiting for router", "Syncing Global Clock",
 "Network timeout. Starting Offline...", "Network Connected & Synced!",
 "Starting Telemetry Server...", "Starting Web Server...", "Registering Callbacks...",
 "AP Active! Reboot board to exit.",
 "Loading daily Min/Max cache...", "Warming up sensors...", "Correcting timestamps (NTP)...",
 "Reloading Min/Max cache...", "Preparing dashboard data...",
 "All subsystems initialized.", "System Ready! Entering Dashboard.",
 "Applying settings...", "Rebooting system...",
 /* Channel names — appended, matching the tail of enum LangKey. */
 "Pressure", "Luminosity"
};

/* Channel -> label key. The table in SensorChannelTable.h carries an i18nKey
 * string ("ch_press") that the web APIs use, but the TFT dictionary is indexed
 * by enum, not looked up by name, so the bridge lives here. Adding a channel
 * without adding its label breaks the build on the static_assert rather than
 * silently drawing an empty row. */
static const LangKey CHANNEL_LABEL[] = {
 /* CH_TEMP  */ TR_TEMP,
 /* CH_HUM   */ TR_HUMIDITY,
 /* CH_PRESS */ TR_CH_PRESSURE,
 /* CH_LUX   */ TR_CH_LUMINOSITY,
};
static_assert(sizeof(CHANNEL_LABEL) / sizeof(CHANNEL_LABEL[0]) == CH_COUNT,
 "every channel in SensorChannelTable.h needs a label key here");

const char* DisplayManager::channelLabel(uint8_t ch) {
 if (!channelValid(ch)) return channelInfo(ch).name;
 return tr(CHANNEL_LABEL[ch]);
}

const char* DisplayManager::tr(LangKey key) {
 if (_activeLangLoaded && _currentLangIdx != LANG_EN &&
 _activeLang.strings[key] != nullptr) {
 /* The TFT fonts carry the full Latin-1 range now, so the pack's
 * UTF-8 maps to Latin-1 bytes with accents KEPT (it used to be
 * transliterated to ASCII). Rotating scratch of 4 slots —
 * supports up to 4 concurrent calls in the same expression (e.g.
 * snprintf("%s %s ...", tr(A), tr(B), ...)). */
 static char scratch[4][96];
 static uint8_t scratchIdx = 0;
 char* buf = scratch[scratchIdx];
 scratchIdx = (scratchIdx + 1) & 3;
 utf8ToLatin1(_activeLang.strings[key], buf, sizeof(scratch[0]));
 return buf;
 }
 return DICTIONARY_EN[key];
}

void DisplayManager::showSettingsLang(int currentLang) {
 mutex_enter_blocking(&_stateMutex);
 _uiMode = MODE_SETTINGS_LANG;
 _previewLangIdx = currentLang;
 _langPage = currentLang / 4;
 _forceSettingsRedraw = true;
 _lastLangPage = -1;
 _repaintSettings = true;
 mutex_exit(&_stateMutex);
}

void DisplayManager::drawSettingsLang( ) {
 if (!_driver.canvas) return;

 bool fullRedraw = _forceSettingsRedraw;
 bool pageChanged = (_langPage != _lastLangPage);


 /* Slot 1 visible when .lng loaded; shows _activeLang.name/code
 * mapped to Latin-1 for the TFT fonts (accents kept). */
 int activeSlots = _activeLangLoaded ? LANG_COUNT : 1;
 char slot1Name[40] = "(install .lng)";
 char slot1Code[12] = "??";
 if (_activeLangLoaded && _activeLang.name[0]) {
 utf8ToLatin1(_activeLang.name, slot1Name, sizeof(slot1Name));
 }
 if (_activeLangLoaded && _activeLang.code[0]) {
 utf8ToLatin1(_activeLang.code, slot1Code, sizeof(slot1Code));
 }
 int totalPages = (activeSlots + 3) / 4;
 if (_langPage >= totalPages) _langPage = totalPages - 1;
 if (_langPage < 0) _langPage = 0;


 if (fullRedraw) {
 fastClearScreen(C_BG_MAIN);
 blitTitleBar(tr(TR_CONFIG_LANG));
 blitFooterMenu(tr(TR_BACK), tr(TR_APPLY)); /* T1.2: no heap */
 }


 if (fullRedraw || pageChanged) {
 uiScrollbar(_driver.tft, 302, 40, 8, 146, totalPages, _langPage);
 }


 int startIdx = _langPage * 4;
 int yBase = 40;
 int itemW = 285;

 for (int i = 0; i < 4; i++) {
 int actualIdx = startIdx + i;
 int y = yBase + (i * 38);


 if (!fullRedraw && !pageChanged) {
 if (actualIdx != _previewLangIdx && actualIdx != _lastPreviewLangIdx) continue;
 }

 _driver.canvas->fillScreen(C_BG_MAIN);

 if (actualIdx < activeSlots) {
 bool isSelected = (actualIdx == _previewLangIdx);
 uint16_t bg = isSelected ? C_ACCENT : C_CARD_BG;
 uint16_t txt = isSelected ? C_BG_MAIN : C_TEXT_MAIN;


 _driver.canvas->fillRoundRect(0, 0, itemW, 34, 8, bg);
 if (!isSelected) _driver.canvas->drawRoundRect(0, 0, itemW, 34, 8, C_TEXT_SUB);


 _driver.canvas->setFont(&simutFont9pt);
 _driver.canvas->setTextColor(txt);
 _driver.canvas->setCursor(10, 24);
 _driver.canvas->print(actualIdx == LANG_EN ? "English" : slot1Name);

 /* Code right-aligned: measure text width and position cursor so
 * the last letter is 10px from the right edge. Previously cursor
 * was fixed at (itemW-35) -> long codes like "pt-BR" went off screen. */
 const char* code = (actualIdx == LANG_EN) ? "EN" : slot1Code;
 int16_t cbx, cby; uint16_t cbw, cbh;
 _driver.canvas->getTextBounds(code, 0, 0, &cbx, &cby, &cbw, &cbh);
 int codeX = itemW - (int)cbw - 10;
 if (codeX < 100) codeX = 100;
 _driver.canvas->setCursor(codeX, 24);
 _driver.canvas->print(code);
 }

 blitCanvas(_driver.canvas, 10, y, itemW, 34);
 }


 _forceSettingsRedraw = false;
 _lastLangPage = _langPage;
 _lastPreviewLangIdx = _previewLangIdx;
}
