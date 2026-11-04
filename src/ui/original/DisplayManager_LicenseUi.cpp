/**
 * @file DisplayManager_LicenseUi.cpp
 * @brief The licence screen's draw: the original's share of DisplayManager.cpp, moved word for word in S10c D4 (docs/analysis/UI_DRIVERS.md).
 *
 * drawSettingsLicense( ) is called only by the original UI (UiOriginal.h). The Ângulo draws a licence screen of its own
 * (ui/angulo/DisplayManager_UiA_Menus.cpp), which reads the same opening through DisplayManager::licenseOpening( ), so this unit is listed in
 * [ui.original].units (tools/features.toml): the two Ângulo images do not compile it, and a dependency they gain on this draw is a link error.
 * The LCD builds exclude it too (the function was under #if !SIMUT_DISPLAY_ALPHA; the unit is not compiled there, so it has no frame here).
 * What stays in DisplayManager.cpp is core: showSettingsLicense( ), the entry that composes the opening on Core 0 (composeLicenseOpening( )),
 * and the buffer itself, _licOpening, which this unit reads through display/LicenseOpening.h.
 */

#include "DisplayManager.h"
#include "display/LicenseOpening.h"
#include "HelpLicenseEN.h" /* LICENSE_TEXT_EN inline in PROGMEM */
#include "TextWrap.h"
#include "UiWidgets.h"
#include "Utf8Fold.h"

void DisplayManager::drawSettingsLicense( ) {
	bool fullRedraw = _forceSettingsRedraw;

	/* The opening (RAM, composed on Core 0) and the licence with the list of
	 * third-party software (flash), laid out as one text. */
	const char* const parts[] = { _licOpening, LICENSE_TEXT_EN };

	const int MAX_COLS = 50;
	const int LINE_H = 9;
	const int TEXT_Y0 = 36;
	const int MAX_VIS = 17;

	TextWrap count;
	for (const char* part : parts) {
		count.walk(part, MAX_COLS, [](int, int, const char*, size_t) { return true; });
	}
	int totalLines = count.lines( );

	/* Calculate total pages */
	_licenseTotalPages = (totalLines + MAX_VIS - 1) / MAX_VIS;
	if (_licenseTotalPages < 1) _licenseTotalPages = 1;
	if (_licensePage >= _licenseTotalPages) _licensePage = _licenseTotalPages - 1;
	if (_licensePage < 0) _licensePage = 0;

	if (fullRedraw) {
		fastClearScreen(C_BG_MAIN);

		/* Header with title and page dots — the pagination idiom every
		 * paged screen shares now (status already used dots). */
		blitTitleBar(tr(TR_LICENSE_TITLE), _licensePage, _licenseTotalPages);

		/* Bottom buttons — composed in the canvas, one full-width band. */
		if (_driver.canvas) {
			_driver.canvas->fillScreen(C_BG_MAIN);
			uiNavArrow(_driver.canvas, 5, 0, 100, 40, UI_UP);
			uiNavArrow(_driver.canvas, 110, 0, 100, 40, UI_DOWN);
			/* T1.2: heap-free render path. */
			uiButton(_driver.canvas, 215, 0, 100, 40, tr(TR_BACK), UI_BTN_SECONDARY);
			blitCanvas(_driver.canvas, 0, 195, 320, 45);
		}
	}

	/* Clear text area */
	fastFillRect(0, TEXT_Y0, 320, MAX_VIS * LINE_H, C_BG_MAIN);
	_driver.tft->setFont(NULL); _driver.tft->setTextSize(1);
	_driver.tft->setTextColor(C_TEXT_SUB);

	/* Render current page: each word is folded to the CP437 font as it is
	 * drawn, so the text itself can be UTF-8. */
	const int startLine = _licensePage * MAX_VIS;
	Adafruit_ILI9341* tft = _driver.tft;
	TextWrap tw;
	auto draw = [&](int line, int col, const char* w, size_t n) {
		if (line >= startLine + MAX_VIS) return false;
		if (line >= startLine) {
			char raw[MAX_COLS * 4 + 1];
			char word[MAX_COLS + 1];
			const size_t m = (n < sizeof(raw) - 1) ? n : sizeof(raw) - 1;
			memcpy(raw, w, m);
			raw[m] = '\0';
			utf8FoldAscii(raw, word, sizeof(word));
			tft->setCursor(10 + col * 6, TEXT_Y0 + (line - startLine) * LINE_H);
			tft->print(word);
		}
		return true;
	};
	for (const char* part : parts) {
		if (!tw.walk(part, MAX_COLS, draw)) break;
	}

	/* Update page dots in header (without redrawing everything): repaint
	 * only the dot strip region, then redraw the dots. */
	if (!fullRedraw) {
		_driver.tft->fillRect(212, 10, 100, 20, C_CARD_BG);
		uiPageDots(_driver.tft, 20, _licensePage, _licenseTotalPages);
	}

	_forceSettingsRedraw = false;
}
