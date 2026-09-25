/**
 * @file UiStudy.cpp
 * @brief Ângulo panel study — the token tables and three dashboard layouts.
 * @details Bench image only (SIMUT_UI_STUDY, env:pico_w_uistudy). Everything
 * here is painted through the 6-strip renderer: each strip composes the whole
 * screen with a row offset into the shared 320x45 canvas, which clips what
 * falls outside, so one drawing routine per layout serves all six strips and
 * there is no partial-repaint bookkeeping to get wrong in a study. A full
 * frame is ~6 x (compose + 3.7 ms of wire) and is only painted when the
 * signature of what is visible changes.
 *
 * The three layouts answer one question each:
 *   A  two cards   — the shipped information architecture under the standard
 *   B  list        — every sensor at once, one row each, dense
 *   C  focus       — one reading, readable across a room
 *
 * Touch is NOT remapped: the shipped zones (DisplayManager_Touch.cpp) stay,
 * and A and C keep the two-card + footer geometry they expect. B's rows do
 * not match those zones; it is a visual study.
 *
 * Geometry is the 4-px grid of ANGULO.md §3.3 inside the panel's safe area
 * (x 4..315, y 4..235): top bar 4..27, cards from 36, footer 196..235,
 * gutters 4, paddings 8/12/16, radii 6 (controls) and 12 (cards).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#include "simut_config.h"
#if SIMUT_UI_STUDY && SIMUT_DISPLAY_TFT

#include "UiStudy.h"
#include "DisplayManager.h"
#include "DisplayManager_Fonts.h"
#include "DisplayManager_FmtFloat.h"
#include "Themes.h"
#include "display/PendingLabel.h"
#include "sensors/SensorDrawing.h" /* minMaxBtnX, MINMAX_BTN_W: the graph button's hot zone */
#include <string.h>
#include <math.h>

volatile uint8_t g_uiStudyVariant = 0;

/* ── Tokens (ANGULO.md §3.1), RGB565 ────────────────────────────────────────
 * RGB565 keeps the top 5/6/5 bits, and the panel readback expands them
 * WITHOUT replicating (AGENTS.md §1), so a capture is checked against these
 * values as (r & 0xF8, g & 0xFC, b & 0xF8) — e.g. claro's acento #1f6355
 * comes back as (24, 96, 80). */
#define TOK(r, g, b) RGB565(r, g, b)

static const AnguloTokens kClaro = {
	TOK(246, 246, 244), TOK(255, 255, 255), TOK(236, 235, 230),   /* fundo, superficie, superficie-2 */
	TOK(32, 30, 26),    TOK(95, 91, 84),    TOK(220, 218, 211), TOK(130, 126, 118), /* tinta, tinta-2, linha, linha-forte */
	TOK(31, 99, 85),    TOK(23, 77, 66),    TOK(241, 250, 246),   /* acento, acento-forte, acento-tinta */
	TOK(32, 120, 78),   TOK(225, 240, 231),                       /* positivo, positivo-suave */
	TOK(138, 97, 22),   TOK(243, 234, 210),                       /* alerta, alerta-suave */
	TOK(179, 56, 46),   TOK(247, 227, 224), TOK(255, 245, 243),   /* perigo, perigo-suave, perigo-tinta */
};

static const AnguloTokens kEscuro = {
	TOK(22, 21, 19),    TOK(32, 30, 27),    TOK(42, 39, 35),
	TOK(235, 231, 223), TOK(163, 156, 144), TOK(56, 52, 48),  TOK(120, 113, 106),
	TOK(95, 179, 154),  TOK(124, 199, 178), TOK(14, 33, 27),
	TOK(111, 190, 142), TOK(36, 53, 43),
	TOK(217, 168, 78),  TOK(56, 48, 28),
	TOK(224, 120, 98),  TOK(56, 34, 32),    TOK(43, 16, 12),
};

const AnguloTokens& anguloTokens( ) {
	return (currentTheme.idName && strcmp(currentTheme.idName, "angulo_claro") == 0)
	       ? kClaro : kEscuro;
}

bool uiStudyThemeActive( ) {
	return currentTheme.idName && strncmp(currentTheme.idName, "angulo_", 7) == 0;
}

/* ── Frame: everything a layout reads, gathered once per paint ─────────── */
namespace {

constexpr int16_t SX = 4, SW = 312;           /* safe area, x */
constexpr int16_t TOP_Y = 4, TOP_H = 24;      /* rows 4..27 */
constexpr int16_t FOOT_Y = 196, FOOT_H = 40;  /* rows 196..235 */
constexpr int16_t R_CARD = 12, R_CTRL = 6;    /* raio-cartao, raio-controle */
constexpr int16_t P1 = 4, P2 = 8, P3 = 12, P4 = 16; /* espaco-1..4 */

/* Study-only microcopy, pt-BR, Latin-1 escapes for the 8-bit fonts. A shipped
 * version would be TR keys in the eight packs; the study is not localised. */
constexpr const char* TXT_ALARM   = "Alarme";
constexpr const char* TXT_ERROR   = "Erro";
constexpr const char* TXT_NOREAD  = "Sem leitura";
constexpr const char* TXT_MIN     = "M\xEDnimo";
constexpr const char* TXT_MAX     = "M\xE1ximo";
constexpr const char* TXT_SILENT  = "Silenciado";
constexpr const char* TXT_WEBBUSY = "Web em uso";
constexpr const char* TXT_GRAPH   = "Ver gr\xE1" "fico";
constexpr const char* TXT_CONFIG  = "Configura\xE7\xF5" "es";
constexpr const char* TXT_CONFIG_SHORT = "Ajustes";
constexpr const char* TXT_NOMINMAX = "sem dados do dia";

struct SlotView {
	int8_t idx = -1;
	SensorType type = TYPE_NONE;
	bool valid = false;
	float t = NAN, h = NAN, p = NAN;
	const char* name = "";
	uint8_t state = 0;        /* 0 normal, 1 limit alarm, 2 sensor error */
};

struct FootBtn { int8_t kind = -1; int8_t slotId = -1; };  /* 0 slot, 1 config, 2 page */

struct Frame {
	const AnguloTokens* T = nullptr;
	const SystemState* st = nullptr;
	bool flash = false;                 /* alarm flash phase, already gated by silence */
	bool silenced = false; uint32_t silenceLeft = 0;
	bool webBusy = false;
	int pending = 0; bool sendFailed = false;
	int rssi = -100;
	SlotView top, sel;
	bool topMinMax = false, selMinMax = false;
	float topMin = NAN, topMax = NAN, selMin = NAN, selMax = NAN;
	SlotView all[MAX_SENSORS]; uint8_t n = 0;
	FootBtn foot[5]; int pages = 1; int page = 0; bool paging = false;
	bool alarmsElsewhere = false;
	char nameBuf[MAX_SENSORS][12];      /* "Sensor N" fallbacks */
};

/* ── text helpers ──────────────────────────────────────────────────────── */
uint16_t textW(Adafruit_GFX* g, const char* s, const GFXfont* f) {
	int16_t x1, y1; uint16_t w, h;
	g->setFont(f); g->setTextSize(1);
	g->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
	return w;
}

void textAt(GFXcanvas16* cv, int16_t x, int16_t base, const char* s,
            const GFXfont* f, uint16_t c) {
	cv->setFont(f); cv->setTextSize(1); cv->setTextColor(c);
	cv->setCursor(x, base); cv->print(s);
}

/* Right-aligned: returns the x the ink starts at, so a caller can stack
 * more text to the left of it. */
int16_t textRight(GFXcanvas16* cv, int16_t right, int16_t base, const char* s,
                  const GFXfont* f, uint16_t c) {
	int16_t x1, y1; uint16_t w, h;
	cv->setFont(f); cv->setTextSize(1);
	cv->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
	const int16_t x = right - (int16_t)w - x1;
	cv->setTextColor(c); cv->setCursor(x, base); cv->print(s);
	return x;
}

/* Cuts `src` to fit maxW in font f, appending "..." when it had to. */
void fitText(Adafruit_GFX* g, const char* src, char* out, size_t outSize,
             int16_t maxW, const GFXfont* f) {
	size_t n = strlen(src);
	if (n >= outSize) n = outSize - 1;
	memcpy(out, src, n); out[n] = '\0';
	if (textW(g, out, f) <= (uint16_t)maxW) return;
	while (n > 1) {
		n--;
		out[n] = '\0';
		if (n + 3 < outSize) { out[n] = '.'; out[n + 1] = '.'; out[n + 2] = '.'; out[n + 3] = '\0'; }
		if (textW(g, out, f) <= (uint16_t)maxW) return;
	}
}

/* §4.3 selo: 6-px dot, soft background, firm text, raio-total; padding
 * espaco-1 x espaco-2; text `rotulo` (13/600 -> the 9 pt bold). Height 20. */
int16_t seloW(Adafruit_GFX* g, const char* txt) {
	return (int16_t)(P2 + 6 + P1 + textW(g, txt, &simutFont9pt) + P2);
}
void selo(GFXcanvas16* cv, int16_t x, int16_t y, const char* txt,
          uint16_t bg, uint16_t fg) {
	const int16_t w = seloW(cv, txt);
	cv->fillRoundRect(x, y, w, 20, 10, bg);
	cv->fillCircle(x + P2 + 3, y + 10, 3, fg);
	textAt(cv, x + P2 + 6 + P1, y + 16, txt, &simutFont9pt, fg);
}

/* The unit of a reading, Latin-1 for the 8-bit faces. */
const char* unitTemp( ) { return "\xB0" "C"; }

/* One or two secondary readings ("58 %", "1013 hPa") right-aligned at
 * `right`; returns the x where they start. */
int16_t secondaries(GFXcanvas16* cv, int16_t right, int16_t base,
                    const SlotView& s, const GFXfont* f, uint16_t c) {
	char buf[16];
	int16_t x = right;
	if (!isnan(s.p)) {
		formatSensorValue(buf, sizeof(buf), s.p, 0);
		strncat(buf, " hPa", sizeof(buf) - strlen(buf) - 1);
		x = textRight(cv, x, base, buf, f, c) - P3;
	}
	if (!isnan(s.h)) {
		formatSensorValue(buf, sizeof(buf), s.h, 0);
		strncat(buf, " %", sizeof(buf) - strlen(buf) - 1);
		x = textRight(cv, x, base, buf, f, c) - P3;
	}
	return x;
}

/* State chrome of a sensor surface: fill, border, ink, selo. */
struct Chrome {
	uint16_t fill, border, ink, ink2;
	const char* seloTxt; uint16_t seloBg, seloFg;
};
Chrome chromeFor(const Frame& F, const SlotView& s, bool selected) {
	const AnguloTokens& T = *F.T;
	Chrome c = { T.superficie, selected ? T.acento : T.linha, T.tinta, T.tinta2, nullptr, 0, 0 };
	if (s.state == 1) {
		c.seloTxt = TXT_ALARM; c.seloBg = T.perigoSuave; c.seloFg = T.perigo; c.border = T.perigo;
		if (F.flash) { c.fill = T.perigo; c.ink = T.perigoTinta; c.ink2 = T.perigoTinta;
		               c.seloBg = T.perigoTinta; c.seloFg = T.perigo; }
	} else if (s.state == 2) {
		c.seloTxt = TXT_ERROR; c.seloBg = T.alertaSuave; c.seloFg = T.alerta; c.border = T.alerta;
		if (F.flash) { c.fill = T.alerta; c.ink = T.fundo; c.ink2 = T.fundo;
		               c.seloBg = T.fundo; c.seloFg = T.alerta; }
	}
	return c;
}

/* ── shared: top bar ───────────────────────────────────────────────────── */
void topBar(GFXcanvas16* cv, int16_t yo, const Frame& F) {
	const AnguloTokens& T = *F.T;
	const int16_t base = TOP_Y + 18 + yo;   /* 9 pt cap 13 in a 24-px bar */

	/* The mark (§6): the name in the display face, 600, tinta. */
	textAt(cv, SX, TOP_Y + 20 + yo, "Simut", &simutFontDisplay12, T.tinta);

	int16_t x = SX + SW;   /* exclusive right edge, 316 */

	if (F.webBusy) {
		/* Touch is refused while a web client holds the device: said in the
		 * bar, as the shipped one does, as an alerta selo. */
		const int16_t w = seloW(cv, TXT_WEBBUSY);
		selo(cv, x - w, TOP_Y + 2 + yo, TXT_WEBBUSY, T.alertaSuave, T.alerta);
		x -= w + P3;
	} else if (F.silenced) {
		char buf[24];
		snprintf(buf, sizeof(buf), "%s %lu s", TXT_SILENT, (unsigned long)F.silenceLeft);
		x = textRight(cv, x, base, buf, &simutFont9pt, T.alerta) - P3;
	} else {
		/* "dd/mm/yy - HH:MM": the time firm, the date as apoio. */
		char datePart[24] = "", timePart[16] = "";
		const char* sep = strstr(F.st->timeString, " - ");
		if (sep) {
			size_t dlen = (size_t)(sep - F.st->timeString);
			if (dlen >= sizeof(datePart)) dlen = sizeof(datePart) - 1;
			memcpy(datePart, F.st->timeString, dlen); datePart[dlen] = '\0';
			strncpy(timePart, sep + 3, sizeof(timePart) - 1);
		} else {
			strncpy(datePart, F.st->timeString, sizeof(datePart) - 1);
		}
		if (timePart[0]) x = textRight(cv, x, base, timePart, &simutFont9pt, T.tinta) - P2;
		x = textRight(cv, x, base, datePart, &simutFontText9, T.tinta2) - P3;
	}

	/* Wi-Fi: four bars, the lit ones in tinta, the rest in linha. */
	int bars = 0;
	if (F.rssi > -100) bars = (F.rssi > -55) ? 4 : (F.rssi > -65) ? 3 : (F.rssi > -75) ? 2 : 1;
	x -= 11;
	for (int i = 0; i < 4; i++)
		cv->fillRect(x + i * 3, base - (4 + i * 2) + 1, 2, 4 + i * 2, (i < bars) ? T.tinta : T.linha);
	x -= P3;

	/* Pending telemetry: count + a single-stroke arrow; perigo when the last
	 * send failed, tinta-2 otherwise. */
	if (F.pending > 0) {
		char pk[PENDING_LABEL_MAX];
		pendingLabel((uint16_t)F.pending, pk, sizeof(pk));
		const uint16_t c = F.sendFailed ? T.perigo : T.tinta2;
		const int16_t ay = base - 5;
		cv->drawFastHLine(x - 10, ay, 8, c); cv->drawFastHLine(x - 10, ay + 1, 8, c);
		cv->drawLine(x - 6, ay - 4, x - 2, ay, c); cv->drawLine(x - 6, ay + 5, x - 2, ay + 1, c);
		cv->drawLine(x - 5, ay - 4, x - 1, ay, c); cv->drawLine(x - 5, ay + 5, x - 1, ay + 1, c);
		x -= 10 + P1;
		x = textRight(cv, x, base, pk, &simutFont9pt, c) - P3;
	}
}

/* ── shared: footer chips (A and C) ─────────────────────────────────────
 * Five 56-px chips on the shipped 63-px touch pitch: x = 4 + 64 i, so every
 * chip lies inside its zone ((x - 5) / 63). raio-controle, secondary chrome;
 * the selected sensor is the one accent fill on the screen. */
void footerChips(GFXcanvas16* cv, int16_t yo, const Frame& F) {
	const AnguloTokens& T = *F.T;
	for (int i = 0; i < 5; i++) {
		const FootBtn& b = F.foot[i];
		if (b.kind < 0) continue;
		const int16_t x = SX + i * 64, y = FOOT_Y + yo, w = 56;
		uint16_t fill = T.superficie, border = T.linhaForte, ink = T.tinta;
		char label[12];
		const GFXfont* f = &simutFont12pt;
		if (b.kind == 0) {
			snprintf(label, sizeof(label), "S%d", (int)b.slotId);
			const SlotView* sv = nullptr;
			for (int k = 0; k < F.n; k++) if (F.all[k].idx == b.slotId) { sv = &F.all[k]; break; }
			const uint8_t stt = sv ? sv->state : 0;
			if (stt == 1 && F.flash)      { fill = T.perigo; border = T.perigo; ink = T.perigoTinta; }
			else if (stt == 1)            { border = T.perigo; ink = T.perigo; }
			else if (stt == 2 && F.flash) { fill = T.alerta; border = T.alerta; ink = T.fundo; }
			else if (stt == 2)            { border = T.alerta; ink = T.alerta; }
			else if (b.slotId == F.sel.idx) { fill = T.acento; border = T.acento; ink = T.acentoTinta; }
		} else if (b.kind == 1) {
			f = &simutFont9pt;
			strncpy(label, ((int)textW(cv, TXT_CONFIG_SHORT, f) <= (int)(w - 2 * P1)) ? TXT_CONFIG_SHORT : "Menu", sizeof(label) - 1);
			label[sizeof(label) - 1] = '\0';
		} else {
			f = &simutFont9pt;
			snprintf(label, sizeof(label), "%d/%d", F.page + 1, F.pages);
			if (F.alarmsElsewhere && F.flash) { fill = T.perigo; border = T.perigo; ink = T.perigoTinta; }
			else if (F.alarmsElsewhere)      { border = T.perigo; ink = T.perigo; }
		}
		cv->fillRoundRect(x, y, w, FOOT_H, R_CTRL, fill);
		cv->drawRoundRect(x, y, w, FOOT_H, R_CTRL, border);
		int16_t x1, y1; uint16_t tw, th;
		cv->setFont(f); cv->setTextSize(1);
		cv->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
		cv->setTextColor(ink);
		cv->setCursor(x + (w - (int16_t)tw) / 2 - x1, y + (FOOT_H - (int16_t)th) / 2 - y1);
		cv->print(label);
	}
}

/* ── shared: a sensor card ─────────────────────────────────────────────
 * Name as `rotulo` (tinta-2), the reading in the display face as the one
 * `display` number of the card, unit and secondaries as apoio, state as a
 * selo top-right, min/max as two labelled columns when that mode is on.
 * Paddings: 8 top/bottom (a 72-px card cannot afford 16), 16 sides. */
void card(GFXcanvas16* cv, int16_t yo, const Frame& F, const SlotView& s,
          int16_t y, int16_t h, bool minmax, float mn, float mx, bool big,
          bool selected) {
	const AnguloTokens& T = *F.T;
	const Chrome c = chromeFor(F, s, selected);
	const int16_t yy = y + yo;
	cv->fillRoundRect(SX, yy, SW, h, R_CARD, c.fill);
	cv->drawRoundRect(SX, yy, SW, h, R_CARD, c.border);

	/* selo first: the name gets the width it leaves */
	int16_t nameMax = SW - 2 * P4;
	if (c.seloTxt) {
		const int16_t w = seloW(cv, c.seloTxt);
		selo(cv, SX + SW - P4 - w, yy + P2, c.seloTxt, c.seloBg, c.seloFg);
		nameMax -= w + P2;
	}
	char name[32];
	fitText(cv, s.name, name, sizeof(name), nameMax, &simutFont9pt);
	textAt(cv, SX + P4, yy + P2 + 13, name, &simutFont9pt, (s.state && F.flash) ? c.ink : c.ink2);

	const int16_t base = yy + h - P2;   /* number baseline */
	char buf[16];

	if (minmax) {
		/* Two labelled columns; NaN = the day has no history yet, said so.
		 * The graph button the touch code keeps in min/max mode
		 * (DisplayManager_Touch.cpp: CARD_X + minMaxBtnX(CARD_W), MINMAX_BTN_W
		 * wide) is drawn where it is hot — a zone with no affordance is a
		 * state that does not exist on screen (rule 8). */
		{
			const int16_t bx = SX + minMaxBtnX(SW), bw = MINMAX_BTN_W, bh = 28, by = yy + h - P2 - bh;
			cv->fillRoundRect(bx, by, bw, bh, R_CTRL, c.fill == T.superficie ? T.superficie : c.fill);
			cv->drawRoundRect(bx, by, bw, bh, R_CTRL, T.linhaForte);
			int16_t x1, y1; uint16_t tw, th;
			cv->setFont(&simutFont9pt); cv->setTextSize(1);
			cv->getTextBounds("Gr\xE1" "fico", 0, 0, &x1, &y1, &tw, &th);
			cv->setTextColor(c.ink);
			cv->setCursor(bx + (bw - (int16_t)tw) / 2 - x1, by + (bh - (int16_t)th) / 2 - y1);
			cv->print("Gr\xE1" "fico");
		}
		const int16_t colW = (SW - 2 * P4 - MINMAX_BTN_W - P4) / 2;
		const int16_t lblBase = base - 24, valBase = base;
		if (isnan(mn) && isnan(mx)) {
			textAt(cv, SX + P4, valBase, TXT_NOMINMAX, &simutFontText9, c.ink2);
		} else {
			textAt(cv, SX + P4, lblBase, TXT_MIN, &simutFont9pt, c.ink2);
			textAt(cv, SX + P4 + colW, lblBase, TXT_MAX, &simutFont9pt, c.ink2);
			formatSensorValue(buf, sizeof(buf), mn, 1);
			int16_t x = SX + P4;
			textAt(cv, x, valBase, buf, &simutFont12pt, c.ink);
			x += textW(cv, buf, &simutFont12pt) + P1;
			textAt(cv, x, valBase, unitTemp( ), &simutFont9pt, c.ink2);
			formatSensorValue(buf, sizeof(buf), mx, 1);
			x = SX + P4 + colW;
			textAt(cv, x, valBase, buf, &simutFont12pt, c.ink);
			x += textW(cv, buf, &simutFont12pt) + P1;
			textAt(cv, x, valBase, unitTemp( ), &simutFont9pt, c.ink2);
		}
		return;
	}

	if (!s.valid) {
		/* "todo estado existe": an invalid reading is a sentence, not a dash. */
		textAt(cv, SX + P4, base, TXT_NOREAD, &simutFontText9, c.ink2);
		return;
	}

	formatSensorValue(buf, sizeof(buf), s.t, 1);
	const GFXfont* numFont = big ? &simutFontDisplay36 : &simutFontDisplay24;
	int16_t x = SX + P4;
	textAt(cv, x, base, buf, numFont, c.ink);
	x += textW(cv, buf, numFont) + P1;
	textAt(cv, x, base, unitTemp( ), &simutFontDisplay12, c.ink2);
	secondaries(cv, SX + SW - P4, base, s, &simutFont12pt, c.ink2);
}

/* ── layout A: two cards ────────────────────────────────────────────────
 * Cards at 36..107 and 116..187 (inside the shipped 35..110 / 115..190
 * zones), footer chips at 196. The bottom card is the selected sensor and
 * its chip is the accent fill; the card itself carries no accent so the
 * screen has one. */
void layoutA(GFXcanvas16* cv, int16_t yo, const Frame& F) {
	card(cv, yo, F, F.top, 36, 72, F.topMinMax, F.topMin, F.topMax, false, false);
	card(cv, yo, F, F.sel, 116, 72, F.selMinMax, F.selMin, F.selMax, false, false);
	footerChips(cv, yo, F);
}

/* ── layout B: list ───────────────────────────────────────────────────────
 * One 28-px row per active sensor from y=36, separated by `linha`; the
 * selected row on superficie-2 with the accent border (§4.4 selecionado).
 * Footer: two secondary buttons, verb + object. Touch is not remapped. */
void layoutB(GFXcanvas16* cv, int16_t yo, const Frame& F) {
	const AnguloTokens& T = *F.T;
	const int16_t rowH = 28;
	const uint8_t rows = F.n < 5 ? F.n : 5;
	for (uint8_t i = 0; i < rows; i++) {
		const SlotView& s = F.all[i];
		const bool selected = (s.idx == F.sel.idx);
		const int16_t y = 36 + i * rowH + yo;
		const Chrome c = chromeFor(F, s, selected);
		if (selected || (s.state && F.flash)) {
			cv->fillRoundRect(SX, y, SW, rowH - 1, R_CTRL, (s.state && F.flash) ? c.fill : T.superficie2);
			cv->drawRoundRect(SX, y, SW, rowH - 1, R_CTRL, c.border);
		} else {
			cv->drawFastHLine(SX, y + rowH - 1, SW, T.linha);
		}
		const int16_t base = y + 20;
		/* reading first — value 12 pt, unit 9 pt, secondaries as apoio,
		 * right-aligned — so the name gets exactly the width it leaves. The
		 * first capture had the name cut at a fixed 150 px and "BMP" sitting
		 * under "1009 hPa"; the width is measured now, not assumed. */
		int16_t x = SX + SW - P3;
		if (s.valid) {
			char buf[16];
			x = textRight(cv, x, base, unitTemp( ), &simutFont9pt, c.ink2) - P1;
			formatSensorValue(buf, sizeof(buf), s.t, 1);
			x = textRight(cv, x, base, buf, &simutFont12pt, c.ink) - P4;
			x = secondaries(cv, x, base, s, &simutFontText9, c.ink2);
		} else {
			x = textRight(cv, x, base, TXT_NOREAD, &simutFontText9, c.ink2);
		}
		/* name, then the state selo right after it, inside what is left */
		int16_t nameMax = x - (SX + P3) - P2;
		if (c.seloTxt) nameMax -= seloW(cv, c.seloTxt) + P2;
		char name[32];
		fitText(cv, s.name, name, sizeof(name), nameMax, &simutFont9pt);
		textAt(cv, SX + P3, base, name, &simutFont9pt, c.ink);
		if (c.seloTxt) {
			const int16_t nx = SX + P3 + textW(cv, name, &simutFont9pt) + P2;
			selo(cv, nx, y + 3, c.seloTxt, c.seloBg, c.seloFg);
		}
	}
	if (F.n == 0) {
		/* the empty state (§7): what to do about it */
		textAt(cv, SX + P3, 36 + 20 + yo, "Nenhum sensor. Cadastre um em Configura\xE7\xF5" "es.",
		       &simutFontText9, T.tinta2);
	}
	/* footer: two secondary buttons on the grid, 152 wide, 8 apart */
	const int16_t y = FOOT_Y + yo;
	const char* labels[2] = { TXT_GRAPH, TXT_CONFIG };
	for (int i = 0; i < 2; i++) {
		const int16_t x = SX + i * 160, w = 152;
		cv->fillRoundRect(x, y, w, FOOT_H, R_CTRL, T.superficie);
		cv->drawRoundRect(x, y, w, FOOT_H, R_CTRL, T.linhaForte);
		int16_t x1, y1; uint16_t tw, th;
		cv->setFont(&simutFont9pt); cv->setTextSize(1);
		cv->getTextBounds(labels[i], 0, 0, &x1, &y1, &tw, &th);
		cv->setTextColor(T.tinta);
		cv->setCursor(x + (w - (int16_t)tw) / 2 - x1, y + (FOOT_H - (int16_t)th) / 2 - y1);
		cv->print(labels[i]);
	}
}

/* ── layout C: focus ──────────────────────────────────────────────────────
 * One card, 36..171, with the selected sensor: the number in the 36 pt
 * display digits (readable across a room), the unit and secondaries beside
 * it, min/max of the day as a labelled line under it. Same footer as A. */
void layoutC(GFXcanvas16* cv, int16_t yo, const Frame& F) {
	const AnguloTokens& T = *F.T;
	const SlotView& s = F.sel;
	const int16_t y = 36, h = 136, yy = y + yo;
	const Chrome c = chromeFor(F, s, false);
	cv->fillRoundRect(SX, yy, SW, h, R_CARD, c.fill);
	cv->drawRoundRect(SX, yy, SW, h, R_CARD, c.border);

	int16_t nameMax = SW - 2 * P4;
	if (c.seloTxt) {
		const int16_t w = seloW(cv, c.seloTxt);
		selo(cv, SX + SW - P4 - w, yy + P3, c.seloTxt, c.seloBg, c.seloFg);
		nameMax -= w + P2;
	}
	char name[32];
	fitText(cv, s.name, name, sizeof(name), nameMax, &simutFont9pt);
	textAt(cv, SX + P4, yy + P3 + 13, name, &simutFont9pt, (s.state && F.flash) ? c.ink : c.ink2);

	const int16_t numBase = yy + 92;
	char buf[16];
	if (s.valid) {
		formatSensorValue(buf, sizeof(buf), s.t, 1);
		int16_t x = SX + P4;
		textAt(cv, x, numBase, buf, &simutFontDisplay36, c.ink);
		x += textW(cv, buf, &simutFontDisplay36) + P1;
		textAt(cv, x, numBase, unitTemp( ), &simutFontDisplay12, c.ink2);
		secondaries(cv, SX + SW - P4, numBase, s, &simutFont12pt, c.ink2);
	} else {
		textAt(cv, SX + P4, numBase, TXT_NOREAD, &simutFontText9, c.ink2);
	}

	/* min / max line, apoio with firm values */
	const int16_t mmBase = yy + h - P4;
	if (isnan(F.selMin) && isnan(F.selMax)) {
		char line[48];
		snprintf(line, sizeof(line), "%s / %s: %s", TXT_MIN, TXT_MAX, TXT_NOMINMAX);
		textAt(cv, SX + P4, mmBase, line, &simutFontText9, c.ink2);
	} else {
		int16_t x = SX + P4;
		textAt(cv, x, mmBase, TXT_MIN, &simutFontText9, c.ink2);
		x += textW(cv, TXT_MIN, &simutFontText9) + P2;
		formatSensorValue(buf, sizeof(buf), F.selMin, 1);
		strncat(buf, " \xB0" "C", sizeof(buf) - strlen(buf) - 1);
		textAt(cv, x, mmBase, buf, &simutFont9pt, c.ink);
		x += textW(cv, buf, &simutFont9pt) + P4;
		textAt(cv, x, mmBase, TXT_MAX, &simutFontText9, c.ink2);
		x += textW(cv, TXT_MAX, &simutFontText9) + P2;
		formatSensorValue(buf, sizeof(buf), F.selMax, 1);
		strncat(buf, " \xB0" "C", sizeof(buf) - strlen(buf) - 1);
		textAt(cv, x, mmBase, buf, &simutFont9pt, c.ink);
	}
	(void)T;
	footerChips(cv, yo, F);
}

/* FNV-1a over the bytes that decide what a frame looks like. */
uint32_t fnv(uint32_t h, const void* p, size_t n) {
	const uint8_t* b = (const uint8_t*)p;
	while (n--) { h ^= *b++; h *= 16777619u; }
	return h;
}

} /* namespace */

/* ── DisplayManager glue ─────────────────────────────────────────────────── */

void DisplayManager::studyMarkDirty( ) {
	mutex_enter_blocking(&_stateMutex);
	_isDirty = true;
	mutex_exit(&_stateMutex);
}

void DisplayManager::renderStudyDashboard(const SystemState& state) {
	/* Signature: everything the layouts read that can change between frames. */
	uint32_t h = 2166136261u;
	const uint8_t variant = g_uiStudyVariant;
	h = fnv(h, &variant, 1);
	h = fnv(h, &currentTheme.idName, sizeof(currentTheme.idName));
	h = fnv(h, state.timeString, strlen(state.timeString));
	int32_t ints[] = {
		state.wifiRssi > -55 ? 4 : state.wifiRssi > -65 ? 3 : state.wifiRssi > -75 ? 2 : state.wifiRssi > -100 ? 1 : 0,
		state.pendingPkts, state.selectedSlotIdx, state.topSlotIdx,
		(int32_t)_alarmSlotMask, (int32_t)_alarmErrMask,
		(int32_t)(_alarmFlashPhase && !_alarmSilenced),
		(int32_t)(_alarmSilenced && _alarmSilenceEnd > millis( ) ? (_alarmSilenceEnd - millis( )) / 1000 : 0),
		(int32_t)_topPanel.showMinMax, (int32_t)_bottomPanel.showMinMax,
		(int32_t)_lastWebBusy, (int32_t)(_pktArrowState == 2), _currentPage,
	};
	h = fnv(h, ints, sizeof(ints));
	for (int i = 0; i < MAX_SENSORS; i++) {
		const SlotSnapshot& s = _slotSnapshots[i];
		int32_t v[] = { (int32_t)s.type, (int32_t)s.valid,
		                isnan(s.temp) ? INT32_MIN : (int32_t)lroundf(s.temp * 10.0f),
		                isnan(s.hum)  ? INT32_MIN : (int32_t)lroundf(s.hum),
		                isnan(s.pres) ? INT32_MIN : (int32_t)lroundf(s.pres) };
		h = fnv(h, v, sizeof(v));
	}
	if (_sysConfigPtr) {
		for (int i = 0; i < MAX_SENSORS; i++)
			h = fnv(h, _sysConfigPtr->sensors[i].friendlyName, strnlen(_sysConfigPtr->sensors[i].friendlyName, 31));
	}

	if (!_forceFullRedraw && h == _studySig) { _lastRenderedState = state; return; }
	_studySig = h;
	_forceFullRedraw = false;
	studyPaint(state);
	_lastRenderedState = state;
}

void DisplayManager::studyPaint(const SystemState& st) {
	Frame F;
	F.T = &anguloTokens( );
	F.st = &st;
	F.silenced = _alarmSilenced && _alarmSilenceEnd > millis( );
	F.silenceLeft = F.silenced ? (_alarmSilenceEnd - millis( )) / 1000 : 0;
	F.flash = _alarmFlashPhase && !_alarmSilenced;
	F.webBusy = _lastWebBusy;
	F.pending = st.pendingPkts;
	F.sendFailed = (_pktArrowState == 2);
	F.rssi = st.wifiRssi;

	/* every active slot, in slot order — the list layout and the chips */
	F.n = 0;
	if (_sysConfigPtr) {
		for (int i = 0; i < MAX_SENSORS && F.n < MAX_SENSORS; i++) {
			if (!_sysConfigPtr->sensors[i].active) continue;
			SlotView& v = F.all[F.n];
			const SlotSnapshot& s = _slotSnapshots[i];
			v.idx = (int8_t)i; v.type = s.type; v.valid = s.valid;
			v.t = s.temp; v.h = s.hum; v.p = s.pres;
			const char* nm = _sysConfigPtr->sensors[i].friendlyName;
			if (nm[0]) v.name = nm;
			else { snprintf(F.nameBuf[F.n], sizeof(F.nameBuf[F.n]), "Sensor %d", i); v.name = F.nameBuf[F.n]; }
			v.state = isSlotAlarming(i) ? 1 : isSlotErrAlarming(i) ? 2 : 0;
			F.n++;
		}
	}
	/* the two the shipped dashboard shows, from the same snapshot the shipped
	 * drawers get (names and values travel in SystemState) */
	auto view = [&](int idx, const char* name, float t, float hh, float p, bool valid, SensorType type) {
		SlotView v; v.idx = (int8_t)idx; v.name = name; v.t = t; v.h = hh; v.p = p; v.valid = valid; v.type = type;
		v.state = (idx >= 0) ? (isSlotAlarming(idx) ? 1 : isSlotErrAlarming(idx) ? 2 : 0) : 0;
		if (!name[0]) { static char nb[2][12]; char* b = nb[idx == st.topSlotIdx ? 0 : 1]; snprintf(b, 12, "Sensor %d", idx); v.name = b; }
		return v;
	};
	F.top = view(st.topSlotIdx, st.topSlotName, st.topSlotTemp, st.topSlotHum, st.topSlotPres, st.topSlotValid, st.topSlotType);
	F.sel = view(st.selectedSlotIdx, st.slotName, st.slotTemp, st.slotHum, st.slotPres, st.slotValid, st.slotType);
	F.topMinMax = _topPanel.showMinMax; F.selMinMax = _bottomPanel.showMinMax;
	F.topMin = _topPanel.minTemp; F.topMax = _topPanel.maxTemp;
	F.selMin = _bottomPanel.minTemp; F.selMax = _bottomPanel.maxTemp;

	/* footer, from the same layout rule the shipped buttons use */
	{
		DashBtn btns[5];
		int pages = 1; bool paging = false;
		buildDashLayout(btns, &pages, &paging);
		for (int i = 0; i < 5; i++) { F.foot[i].kind = btns[i].kind; F.foot[i].slotId = btns[i].slotId; }
		F.pages = pages; F.paging = paging; F.page = _currentPage;
		if (paging && (_alarmSlotMask | _alarmErrMask)) {
			for (int k = 0; k < F.n; k++) {
				if (!F.all[k].state) continue;
				bool here = false;
				for (int i = 0; i < 5; i++) if (btns[i].kind == 0 && btns[i].slotId == F.all[k].idx) here = true;
				if (!here) { F.alarmsElsewhere = true; break; }
			}
		}
	}

	GFXcanvas16* cv = beginScreenRender( );
	if (!cv) return;
	for (int strip = 0; strip < 6; strip++) {
		cv->fillScreen(F.T->fundo);
		const int16_t yo = (int16_t)(-strip * RENDER_STRIP_H);
		topBar(cv, yo, F);
		switch (g_uiStudyVariant) {
		case 1: layoutA(cv, yo, F); break;
		case 2: layoutB(cv, yo, F); break;
		default: layoutC(cv, yo, F); break;
		}
		commitScreenStrip((int16_t)strip);
	}
	endScreenRender( );
}

#endif /* SIMUT_UI_STUDY && SIMUT_DISPLAY_TFT */
