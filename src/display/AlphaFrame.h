/**
 * @file display/AlphaFrame.h
 * @brief One frame of the alpha build's 16x2 LCD as a function of what it reads: the composition that sat inside
 *        DisplayManager::loopCore1( ), apart from the display, so a host test can run it against a fake LCD.
 *
 * @details Until 2026-11-04 every character the alpha LCD shows was written by the Core 1 loop in
 * DisplayManager_Alpha.cpp, between a mutex, a handful of atomics and the sleeps, and could be seen only on a board:
 * a column that moved, a glyph that changed slot or a clear that went missing reached nobody until someone looked at
 * the glass, and the manual's LCD figures had no way to be made without one. The composition is here now and the
 * loop is left with what is not composition: the mutex, the atomics, Core 1's park, the timers and their static
 * state, the sleeps, the `continue`s and the blit( ).
 *
 * What the glass shows is two things, and this file writes both:
 *   - the CGRAM: slots 0-6 hold the big-digit pieces and slot 7 the link's glyph. BigFont_HD44780::begin( ) loads
 *     all eight when the loop starts; alphaLoadLinkGlyph( ) replaces slot 7 from the link, first thing in every pass;
 *   - the framebuffer: alphaDrawFrame( ) writes one screen into it, and the loop's blit( ) pushes it to the DDRAM.
 * A pass is those two, in that order, then the blit. Nothing here reads a global or a clock, and nothing here blits:
 * what a screen needs comes in AlphaFrameIn, and the loop (or a test's fake LCD) decides when the glass is refreshed.
 *
 * Which cells a screen writes, and which it leaves as they were, is part of the contract and the test pins it
 * (test_validators, test_lan). SPLASH, SENSOR and CLEAR own the whole screen. BOOT and BOOT_FULL do not clear: they
 * write "SIMUT " and the version at the start of row 0 and all of row 1, and the cells of row 0 past the version
 * keep the previous frame (columns 12-15 with a six-character version). NET, AP and OTA write all thirty-two
 * cells without a clear. A clear added to the boot would be a change of behavior, however small, which this move
 * does not make.
 *
 * Compiled by the alpha display only (DisplayManager_Alpha.cpp) and by the host tests. The two build switches the
 * original tested stay compile-time, as in the original: SIMUT_NET_LAN (the cable's cell and " Sem rede") and
 * SIMUT_NET_WIFI (the setup AP's three pages; a wired build has no setup AP). Both come from net/NetCaps.h, which is
 * the FIRST include: a header that tests them and does not include it reads them as 0, silently, and the wired
 * build would be drawn as a radio (tools/check_net_boundary.py, rule class-include, holds that in CI).
 *
 * The functions are `inline __attribute__((always_inline))`, as the ticks of DisplayManager_Ticks.h are, and meant to be
 * called where their code used to sit: a call whose AlphaFrameIn has a constant screen folds the switch away and
 * leaves that screen's code.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */
#pragma once
#include "../net/NetCaps.h"   /* FIRST: SIMUT_NET_LAN and SIMUT_NET_WIFI are tested below (see the note above) */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "../OtaScreen.h"
#include "../sensors/SensorChannels.h"
#include "AlphaMarquee.h"
#include "BigFont_HD44780.h"
#include "PendingLabel.h"

/* One value per branch of the loop that writes to the LCD. */
enum AlphaScreen : uint8_t {
	ALPHA_SCREEN_SPLASH,      /**< "SIMUT <version>" over "  Inicializando ", on a cleared screen */
	ALPHA_SCREEN_OTA,         /**< the firmware update's two lines (OtaScreen.h) */
	ALPHA_SCREEN_BOOT,        /**< "SIMUT <version>" over the boot bar, bootBar of its 14 cells filled; no clear */
	ALPHA_SCREEN_BOOT_FULL,   /**< the same with every cell filled: the first six passes after the boot; no clear */
	ALPHA_SCREEN_NET,         /**< "Conectado!" with the address, or "Obtendo IP...", or the offline pair; no clear */
	ALPHA_SCREEN_AP,          /**< the setup network's three pages (nothing is drawn in a wired build) */
	ALPHA_SCREEN_SENSOR,      /**< a reading, or ERRO: clear, status cell, slot id or pending count, big digits */
	ALPHA_SCREEN_CLEAR,       /**< the framebuffer emptied: an update's ending expired, the network screen's last pass */
};

/* Everything a screen reads. Only the fields of the screen in `screen` are read; the others keep their defaults. */
struct AlphaFrameIn {
	AlphaScreen screen;

	/* SPLASH, BOOT, BOOT_FULL: row 0's first words, "SIMUT " and the version. The caller owns the string. */
	const char* banner = "";

	/* OTA: the phase the update is in and, for a refusal, why (OtaScreen.h). */
	OtaPhase otaPhase = OTA_PH_NONE;
	OtaWhy   otaWhy   = OTA_WHY_NONE;

	/* BOOT: cells of the progress bar that are filled, 0-14. */
	uint8_t bootBar = 0;

	/* NET: the link is up (RSSI above -100), the network has given an address, and the address. hasIp is the
	 * loop's own reading of ip[0], taken once for its counter and for this screen, as the original had it; ip is
	 * read when the screen is drawn, not copied. */
	bool        online = false;
	bool        hasIp  = false;
	const char* ip     = "";

	/* AP: the page (0 the address, 1 the network, 2 the key), the text of row 1 and the scroll position. apValue
	 * is what the loop chose BEFORE it turned the page: in the pass that turns it the title is the new page's and
	 * the value is the old one's, for one frame (AP_SCROLL_MS), and this draws exactly what it is given. */
	uint8_t     apPage  = 0;
	const char* apValue = "";
	uint16_t    apStep  = 0;

	/* SENSOR: how many slots are active, the slot and the channel on screen (SensorChannel), whether the reading
	 * is valid and its value, the setup-AP flag (a Wi-Fi build only draws it) and the pending telemetry count. */
	uint8_t  slots   = 0;
	int8_t   slot    = -1;
	uint8_t  channel = CH_TEMP;
	bool     valid   = false;
	float    value   = NAN;
	bool     apMode  = false;
	uint16_t pending = 0;

	explicit AlphaFrameIn(AlphaScreen s = ALPHA_SCREEN_CLEAR) : screen(s) {}
};

/* Write one window of `s` on `row`. The arithmetic and the reason for it are
 * in display/AlphaMarquee.h, where a host test can reach them. */
template <typename LCD>
inline __attribute__((always_inline)) void alphaMarquee(LCD &lcd, uint8_t row, const char* s, uint16_t step) {
	char win[ALPHA_LCD_COLS + 1];
	alphaMarqueeWindow(s, step, win);
	lcd.setCursor(0, row);
	lcd.print(win);
}

/* The link's glyph (slot 7), from the RSSI: first thing in every pass, before any screen. */
template <typename LCD>
inline __attribute__((always_inline)) void alphaLoadLinkGlyph(LCD &lcd, BigFont_HD44780 &big, int32_t rssi) {
#if SIMUT_NET_LAN
	big.showWired(lcd, rssi > -100);
#else
	big.showWiFi(lcd, rssi);
#endif
}

/* One screen into the framebuffer. */
template <typename LCD>
inline __attribute__((always_inline)) void alphaDrawFrame(LCD &lcd, BigFont_HD44780 &big, const AlphaFrameIn &in) {
	switch (in.screen) {
	case ALPHA_SCREEN_SPLASH:
		lcd.clear( );
		lcd.setCursor(0, 0);
		lcd.print(in.banner);
		lcd.setCursor(0, 1);
		lcd.print("  Inicializando ");
		break;

	case ALPHA_SCREEN_OTA: {
		char row0[17], row1[17];
		otaLcdLines(in.otaPhase, in.otaWhy, row0, row1);
		lcd.setCursor(0, 0); lcd.print(row0);
		lcd.setCursor(0, 1); lcd.print(row1);
		break;
	}

	case ALPHA_SCREEN_BOOT:
		lcd.setCursor(0, 0);
		lcd.print(in.banner);
		/* Render bar: [############      ] */
		lcd.setCursor(0, 1);
		lcd.write('[');
		for (uint8_t i = 0; i < 14; i++)
			lcd.write(i < in.bootBar ? '#' : ' ');
		lcd.write(']');
		break;

	case ALPHA_SCREEN_BOOT_FULL:
		lcd.setCursor(0, 0);
		lcd.print(in.banner);
		lcd.setCursor(0, 1);
		lcd.write('[');
		for (uint8_t i = 0; i < 14; i++) lcd.write('#');
		lcd.write(']');
		break;

	case ALPHA_SCREEN_NET:
		lcd.setCursor(0, 0);
		if (in.online && in.hasIp) {
			lcd.print(" Conectado!     ");
			lcd.setCursor(0, 1);
			lcd.print("                ");
			lcd.setCursor(0, 1);
			lcd.print(in.ip);
		} else if (in.online && !in.hasIp) {
			lcd.print(" Conectado!     ");
			lcd.setCursor(0, 1);
			lcd.print("  Obtendo IP... ");
		} else {
#if SIMUT_NET_LAN
			lcd.print(" Sem rede       ");
#else
			lcd.print(" Sem WiFi       ");
#endif
			lcd.setCursor(0, 1);
			lcd.print("    Offline     ");
		}
		break;

	case ALPHA_SCREEN_AP:
#if SIMUT_NET_WIFI   /* the wired build has no setup AP (2026-10-13) */
		lcd.setCursor(0, 0);
		switch (in.apPage) {
			case 0: lcd.print("Modo AP  1 de 3"); break;
			case 1: lcd.print("Rede:    2 de 3"); break;
			default: lcd.print("Senha:   3 de 3"); break;
		}
		lcd.write(' ');
		alphaMarquee(lcd, 1, in.apValue, in.apStep);
#endif
		break;

	case ALPHA_SCREEN_SENSOR:
		lcd.clear( );
		/* Status top-left: "AP" in Access Point mode, else WiFi icon. A
		 * wired build has no AP, and its cell is the cable glyph, no "W". */
		lcd.setCursor(0, 0);
#if SIMUT_NET_LAN
		lcd.write(' ');
		lcd.write(BFS_WIFI);
#else
		if (in.apMode) {
			lcd.print("AP");
		} else {
			lcd.write('W');
			lcd.write(BFS_WIFI);
		}
#endif

		if (in.slots == 0) {
			/* No active sensors — show big ERRO. */
			big.showError(lcd);
		} else {
			/* Line 1, columns 0-2: the only cells no screen below draws on
			 * (the big digits start at column 3 or 4, "ERRO" at 3). With
			 * more than one slot they say which slot is on screen; with one
			 * there is nothing to identify, and they carry the pending
			 * telemetry count instead, the way the TFT top bar does —
			 * nothing when the queue is empty, "Nk" past a thousand. */
			if (in.slots > 1) {
				char id[8];
				snprintf(id, sizeof(id), "S%d", (int)in.slot);
				lcd.setCursor(0, 1);
				lcd.print(id);
			} else if (in.pending > 0) {
				char pend[PENDING_LABEL_MAX];
				pendingLabel(in.pending, pend, sizeof(pend));
				lcd.setCursor(0, 1);
				lcd.print(pend);
			}

			float v = in.value;

			if (!in.valid || isnan(v)) {
				/* Sensor in error — big ERRO (avgValue may be stale). */
				big.showError(lcd);
			} else switch (in.channel) {
			case CH_TEMP: {
				int raw = (int)(v * 10.0f);
				big.showNumber(lcd, raw);
				lcd.setCursor(14, 0); lcd.write('o');
				lcd.setCursor(15, 1); lcd.write('C');
				break;
			}
			case CH_HUM: {
				int hum = (int)v;
				big.showInteger(lcd, hum, 13, '%');
				lcd.setCursor(14, 1); lcd.print("UR");
				break;
			}
			case CH_PRESS: {
				/* Pressão em caracteres normais, montada com aritmética
				 * inteira — o %f do printf corrompia o LCD no Core 1.
				 * Dígitos grandes ficam para uma etapa futura. */
				char b[16];
				int tenths = (int)(v * 10.0f + 0.5f);
				int ip = tenths / 10;
				int dp = tenths % 10;
				/* The firmware builds with -Wno-format-truncation (platformio.ini); the host tests do not, and
				 * the compiler cannot know that a pressure is five digits. Not a change to the line. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
				snprintf(b, sizeof(b), "%d.%d hPa", ip, dp);
#pragma GCC diagnostic pop
				lcd.setCursor(3, 0);
				lcd.print(b);
				break;
			}
			default:
				big.showError(lcd);
				break;
			}
		}
		break;

	case ALPHA_SCREEN_CLEAR:
		lcd.clear( );
		break;
	}
}
