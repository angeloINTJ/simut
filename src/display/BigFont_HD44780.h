/**
 * @file display/BigFont_HD44780.h
 * @brief Dígitos grandes (3×2 chars, 15×16 px) em LCD HD44780 16×2.
 *
 * Usa 8 slots CGRAM para compor dígitos 0-9 com altura de 2 linhas.
 * Compatível com LiquidCrystal e Hd44780_16x2 (SIMUT alpha).
 *
 * Layout de cada dígito (3 colunas × 2 linhas):
 *   R0: [LT] [UB/UMB] [RT]
 *   R1: [LL] [LB]     [LR]
 *
 * Inspirado na função "numerao" do Calibrador_quente_frio1_7_1.ino.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT
 */
#pragma once
#include <Arduino.h>

/* ═══ Bitmaps CGRAM 5×8 pixels ═══════════════════════════════════════ */

const uint8_t BF_LT[8]  = { B00111, B01111, B11111, B11111, B11111, B11111, B11111, B11111 };
const uint8_t BF_UB[8]  = { B11111, B11111, B00000, B00000, B00000, B00000, B00000, B00000 };
const uint8_t BF_RT[8]  = { B11100, B11110, B11111, B11111, B11111, B11111, B11111, B11111 };
const uint8_t BF_LL[8]  = { B11111, B11111, B11111, B11111, B11111, B11111, B01111, B00111 };
const uint8_t BF_LB[8]  = { B00000, B00000, B00000, B00000, B00000, B00000, B11111, B11111 };
const uint8_t BF_LR[8]  = { B11111, B11111, B11111, B11111, B11111, B11111, B11110, B11100 };
const uint8_t BF_UMB[8] = { B11111, B11111, B00000, B00000, B00000, B00000, B11111, B11111 };
const uint8_t BF_GR[8]  = { B01100, B10010, B10010, B01100, B00000, B00000, B00000, B00000 };

/* WiFi signal-strength icons (slot 7).  Bars grow from bottom up. */
const uint8_t BF_WIFI0[8] = { B00000, B00000, B00000, B00000, B00000, B00000, B00000, B10001 };  /* X */
const uint8_t BF_WIFI1[8] = { B00000, B00000, B00000, B00000, B00000, B00000, B00000, B11111 };  /* ▁ */
const uint8_t BF_WIFI2[8] = { B00000, B00000, B00000, B00000, B00000, B00000, B11111, B11111 };  /* ▂ */
const uint8_t BF_WIFI3[8] = { B00000, B00000, B00000, B00000, B00000, B11111, B11111, B11111 };  /* ▄ */
const uint8_t BF_WIFI4[8] = { B00000, B00000, B00000, B00000, B11111, B11111, B11111, B11111 };  /* ▆ */
const uint8_t BF_WIFI5[8] = { B00000, B00000, B00000, B11111, B11111, B11111, B11111, B11111 };  /* █ */

enum BigFontSlot : uint8_t {
	BFS_LT  = 0, BFS_UB = 1, BFS_RT = 2, BFS_LL = 3,
	BFS_LB  = 4, BFS_LR = 5, BFS_UMB= 6, BFS_WIFI = 7
};

/* ═══ Classe BigFont_HD44780 ═════════════════════════════════════════ */

class BigFont_HD44780 {
public:
	/** Carrega os 8 caracteres CGRAM. Template aceita LiquidCrystal,
	 *  Hd44780_16x2 ou qualquer classe com createChar(num, bitmap). */
	template <typename LCD>
	void begin(LCD& lcd) {
		lcd.createChar(BFS_LT,   const_cast<uint8_t*>(BF_LT));
		lcd.createChar(BFS_UB,   const_cast<uint8_t*>(BF_UB));
		lcd.createChar(BFS_RT,   const_cast<uint8_t*>(BF_RT));
		lcd.createChar(BFS_LL,   const_cast<uint8_t*>(BF_LL));
		lcd.createChar(BFS_LB,   const_cast<uint8_t*>(BF_LB));
		lcd.createChar(BFS_LR,   const_cast<uint8_t*>(BF_LR));
		lcd.createChar(BFS_UMB,  const_cast<uint8_t*>(BF_UMB));
		lcd.createChar(BFS_WIFI, const_cast<uint8_t*>(BF_WIFI0));
		_loaded = true;
		_lastRssi = -999;  /* force first redraw */
	}

	/** Marca como carregado (CGRAM carregada externamente). */
	void begin( ) { _loaded = true; }
	bool isReady( ) const { return _loaded; }

	/* ── Renderização ────────────────────────────────────────────── */

	/** Desenha um dígito 0-9 em (col, row). O dígito ocupa 3 colunas. */
	template <typename LCD>
	void showDigit(LCD& lcd, uint8_t digit, uint8_t col) {
		if (!_loaded || digit > 9) return;
		if (col > 13) col = 13;

		switch (digit) {
		case 0:
			lcd.setCursor(col, 0); lcd.write(BFS_LT); lcd.write(BFS_UB); lcd.write(BFS_RT);
			lcd.setCursor(col, 1); lcd.write(BFS_LL); lcd.write(BFS_LB); lcd.write(BFS_LR);
			break;
		case 1:
			lcd.setCursor(col + 1, 0); lcd.write(BFS_RT);
			lcd.setCursor(col + 1, 1); lcd.write(BFS_LR);
			break;
		case 2:
			lcd.setCursor(col, 0); lcd.write(BFS_UMB); lcd.write(BFS_UMB); lcd.write(BFS_RT);
			lcd.setCursor(col, 1); lcd.write(BFS_LL);  lcd.write(BFS_LB);  lcd.write(BFS_LB);
			break;
		case 3:
			lcd.setCursor(col, 0); lcd.write(BFS_UMB); lcd.write(BFS_UMB); lcd.write(BFS_RT);
			lcd.setCursor(col, 1); lcd.write(BFS_LB);  lcd.write(BFS_LB);  lcd.write(BFS_LR);
			break;
		case 4:
			lcd.setCursor(col, 0); lcd.write(BFS_LL); lcd.write(BFS_LB); lcd.write(BFS_RT);
			lcd.setCursor(col + 2, 1); lcd.write(BFS_LR);
			break;
		case 5:
			lcd.setCursor(col, 0); lcd.write(BFS_LT);  lcd.write(BFS_UMB); lcd.write(BFS_UMB);
			lcd.setCursor(col, 1); lcd.write(BFS_LB);  lcd.write(BFS_LB);  lcd.write(BFS_LR);
			break;
		case 6:
			lcd.setCursor(col, 0); lcd.write(BFS_LT);  lcd.write(BFS_UMB); lcd.write(BFS_UMB);
			lcd.setCursor(col, 1); lcd.write(BFS_LL);  lcd.write(BFS_LB);  lcd.write(BFS_LR);
			break;
		case 7:
			lcd.setCursor(col, 0); lcd.write(BFS_UB); lcd.write(BFS_UB); lcd.write(BFS_RT);
			lcd.setCursor(col + 1, 1); lcd.write(BFS_LT);
			break;
		case 8:
			lcd.setCursor(col, 0); lcd.write(BFS_LT);  lcd.write(BFS_UMB); lcd.write(BFS_RT);
			lcd.setCursor(col, 1); lcd.write(BFS_LL);  lcd.write(BFS_LB);  lcd.write(BFS_LR);
			break;
		case 9:
			lcd.setCursor(col, 0); lcd.write(BFS_LT);  lcd.write(BFS_UMB); lcd.write(BFS_RT);
			lcd.setCursor(col + 2, 1); lcd.write(BFS_LR);
			break;
		}
	}

	/** Temperatura grande: valor × 10 com vírgula decimal e °C.
	 *  Ex: 234 → " 23.4°C". Layout ocupa colunas 5-15. */
	template <typename LCD>
	void showNumber(LCD& lcd, int value) {
		if (!_loaded) return;
		bool neg = (value < 0);
		unsigned int mod = abs(value);

		uint8_t d1 = mod / 100;          /* centenas (dezena real) */
		uint8_t d2 = (mod / 10) % 10;    /* dezenas (unidade real) */
		uint8_t d3 = mod % 10;           /* unidades (décimo)       */

		/* Sinal e limpeza da área inicial */
		if (neg) { lcd.setCursor(4, 0); lcd.write(BFS_LB); }
		else     { lcd.setCursor(4, 0); lcd.write(' '); }

		/* D1 — centenas (suprimido se for 0) */
		if (d1 > 0) { showDigit(lcd, d1, 5); }
		else {
			lcd.setCursor(5,0); lcd.write(' ');lcd.write(' ');lcd.write(' ');
			lcd.setCursor(5,1); lcd.write(' ');lcd.write(' ');lcd.write(' ');
		}

		/* D2 — dezenas */
		showDigit(lcd, d2, 8);

		/* Vírgula decimal entre D2 e D3 */
		lcd.setCursor(11, 1); lcd.write(',');

		/* D3 — décimos */
		showDigit(lcd, d3, 12);

		/* No ° symbol — slot 7 is now WiFi icon.
		   Temperature is clear from context (big digits + comma). */
	}

	/** Inteiro grande (0–999) sem vírgula, com sufixo personalizado.
	 *  Ex: 58 → " 58%". Sufixo é escrito em caractere normal. */
	template <typename LCD>
	void showInteger(LCD& lcd, int value, uint8_t suffixCol, char suffix) {
		if (!_loaded) return;
		bool neg = (value < 0);
		unsigned int mod = abs(value);
		if (mod > 999) mod = 999;

		uint8_t d1 = mod / 100;
		uint8_t d2 = (mod / 10) % 10;
		uint8_t d3 = mod % 10;

		/* Centrado: 3 dígitos → col 4, 2 dígitos → col 6, 1 dígito → col 8 */
		uint8_t startCol;
		if (d1 > 0)      startCol = 3;  /* 3 dígitos */
		else if (d2 > 0) startCol = 6;  /* 2 dígitos */
		else             startCol = 9;  /* 1 dígito  */

		/* Sinal */
		if (neg) { lcd.setCursor(startCol - 1, 0); lcd.write(BFS_LB); }
		else     { lcd.setCursor(startCol - 1, 0); lcd.write(' '); }

		/* D1 (suprimido se 0) */
		if (d1 > 0) { showDigit(lcd, d1, startCol); startCol += 3; }
		else {
			lcd.setCursor(startCol,0); lcd.write(' ');lcd.write(' ');lcd.write(' ');
			lcd.setCursor(startCol,1); lcd.write(' ');lcd.write(' ');lcd.write(' ');
		}

		/* D2 */
		if (d1 > 0 || d2 > 0) {
			showDigit(lcd, d2, startCol);
			startCol += 3;
		}

		/* D3 — sempre visível (mesmo que 0, ex: "  0%") */
		showDigit(lcd, d3, startCol);

		/* Sufixo (%, C, etc.) */
		lcd.setCursor(suffixCol, 1);
		lcd.write(suffix);
	}

	/** Atualiza o ícone WiFi (slot 7) conforme o RSSI.
	 *  RSSI >= -50 → 5 barras, >= -60 → 4, >= -70 → 3,
	 *  >= -80 → 2,  >= -90 → 1,  < -90 → 0 (X).
	 *  @returns true se o CGRAM foi alterado. */
	template <typename LCD>
	bool showWiFi(LCD& lcd, int32_t rssi) {
		if (!_loaded) return false;
		int level;
		if      (rssi >= -50) level = 5;
		else if (rssi >= -60) level = 4;
		else if (rssi >= -70) level = 3;
		else if (rssi >= -80) level = 2;
		else if (rssi >= -90) level = 1;
		else                 level = 0;

		if (level == _lastLevel) return false;
		_lastLevel = level;

		const uint8_t* bm;
		switch (level) {
		case 0: bm = BF_WIFI0; break;
		case 1: bm = BF_WIFI1; break;
		case 2: bm = BF_WIFI2; break;
		case 3: bm = BF_WIFI3; break;
		case 4: bm = BF_WIFI4; break;
		default: bm = BF_WIFI5; break;
		}
		lcd.createChar(BFS_WIFI, const_cast<uint8_t*>(bm));
		return true;
	}

private:
	bool    _loaded = false;
	int32_t _lastRssi = -999;
	int8_t  _lastLevel = -1;
};
