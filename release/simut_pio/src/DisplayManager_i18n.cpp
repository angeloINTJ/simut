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
 "AVERAGE", "STD DEV", "Error", "Configuration Mode", "8. System Status",
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
 /* TFT renders only ASCII (font has no Latin-1
 * glyphs). Applies unaccent in rotating scratch of 4 slots —
 * supports up to 4 concurrent calls in the same expression (e.g.
 * snprintf("%s %s ...", tr(A), tr(B), ...)). */
 static char scratch[4][96];
 static uint8_t scratchIdx = 0;
 char* buf = scratch[scratchIdx];
 scratchIdx = (scratchIdx + 1) & 3;
 unaccent(_activeLang.strings[key], buf, sizeof(scratch[0]));
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
 * transliterated (unaccent) for the TFT, which renders only ASCII
 * in the current font. */
 int activeSlots = _activeLangLoaded ? LANG_COUNT : 1;
 char slot1Name[40] = "(install .lng)";
 char slot1Code[12] = "??";
 if (_activeLangLoaded && _activeLang.name[0]) {
 unaccent(_activeLang.name, slot1Name, sizeof(slot1Name));
 }
 if (_activeLangLoaded && _activeLang.code[0]) {
 unaccent(_activeLang.code, slot1Code, sizeof(slot1Code));
 }
 int totalPages = (activeSlots + 3) / 4;
 if (_langPage >= totalPages) _langPage = totalPages - 1;
 if (_langPage < 0) _langPage = 0;


 if (fullRedraw) {
 _driver.tft->fillScreen(C_BG_MAIN);


 _driver.tft->fillRect(4, 4, 312, 32, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt);
 _driver.tft->setTextColor(C_TEXT_MAIN);
 _driver.tft->setCursor(10, 22);
 _driver.tft->print(tr(TR_CONFIG_LANG));


 int btnY = 195; int btnH = 40;
 int16_t bx, by; uint16_t bw, bh;


 _driver.tft->fillRoundRect(5, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(36, btnY + 12, 26, btnY + 26, 46, btnY + 26, C_TEXT_MAIN);


 _driver.tft->fillRoundRect(73, btnY, 62, btnH, 8, C_CARD_BG);
 _driver.tft->fillTriangle(104, btnY + 26, 94, btnY + 12, 114, btnY + 12, C_TEXT_MAIN);


 _driver.tft->fillRoundRect(141, btnY, 75, btnH, 8, C_CARD_BG);
 _driver.tft->setFont(&simutFont9pt);
 _driver.tft->setTextColor(C_TEXT_MAIN);
 const char* backTxt = tr(TR_BACK);
 _driver.tft->getTextBounds(backTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(141 + (75 - bw) / 2, btnY + 25);
 _driver.tft->print(backTxt);


 _driver.tft->fillRoundRect(222, btnY, 93, btnH, 8, C_ACCENT);
 _driver.tft->setTextColor(C_BG_MAIN);
 String appTxt = tr(TR_APPLY);
 _driver.tft->getTextBounds(appTxt, 0, 0, &bx, &by, &bw, &bh);
 _driver.tft->setCursor(222 + (93 - bw) / 2, btnY + 25);
 _driver.tft->print(appTxt);
 }


 if (fullRedraw || pageChanged) {
 int trackX = 302; int trackY = 40;
 int trackW = 8; int trackH = 146;

 _driver.tft->fillRoundRect(trackX, trackY, trackW, trackH, 4, C_CARD_BG);
 _driver.tft->drawRoundRect(trackX, trackY, trackW, trackH, 4, C_TEXT_SUB);

 int thumbH = trackH / totalPages;
 if (thumbH < 20) thumbH = 20;
 int thumbY = trackY;
 if (totalPages > 1) {
 thumbY += (_langPage * (trackH - thumbH)) / (totalPages - 1);
 }
 _driver.tft->fillRoundRect(trackX, thumbY, trackW, thumbH, 4, C_ACCENT);
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
