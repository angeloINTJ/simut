/**
 * @file DisplayManager_i18nUi.cpp
 * @brief The language screen's draw: the original share of DisplayManager_i18n.cpp, moved word for word in S10a (docs/analysis/UI_DRIVERS.md).
 */

#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "UiWidgets.h"
#include "LogManager.h"
#include "sensors/SensorChannelTable.h"   /* CH_COUNT, channelInfo, channelValid */

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
