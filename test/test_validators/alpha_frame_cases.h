/**
 * @file    test/test_validators/alpha_frame_cases.h
 * @brief   What display/AlphaFrame.h writes for the radio's screens, pinned cell by cell (a case list of test_main.cpp).
 * @details The expected rows are read from the code the cases replace, DisplayManager::loopCore1( ) of the alpha build
 *          at 34fc44a9 (2026-11-04), path by path, and not from running the function they test: the cells of a big
 *          digit come from BigFont_HD44780.h's showDigit( ), the text from the loop's own literals. Every screen is
 *          drawn over a previous frame of 'x', so a cell a screen does not write shows as an 'x' and a clear that
 *          went missing, or one added to a screen that never had it, shows as a different row.
 *          The cable's differences are test_lan's (alpha_frame_wired cases). The pass these run is AlphaPass's.
 *          Included by test_main.cpp, which lists the cases in main( ): the README gate counts RUN_TEST( ) lines
 *          in the .cpp files of the suite.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once
#include "AlphaFrameCheck.h"

static_assert(SIMUT_NET_WIFI == 1 && SIMUT_NET_LAN == 0, "these cases are the radio's: the cable's differences are test_lan's");

/* ── The CGRAM: what is loaded, and when slot 7 is loaded again ─────────────────────────────────────────────── */

static void test_alpha_frame_cgram_starts_with_the_big_digit_pieces_and_the_x(void) {
	AlphaPass p;
	alphaExpectGlyph(p.lcd, 0, BF_LT,    "slot 0 is the top-left piece");
	alphaExpectGlyph(p.lcd, 1, BF_UB,    "slot 1 is the upper bar");
	alphaExpectGlyph(p.lcd, 2, BF_RT,    "slot 2 is the top-right piece");
	alphaExpectGlyph(p.lcd, 3, BF_LL,    "slot 3 is the bottom-left piece");
	alphaExpectGlyph(p.lcd, 4, BF_LB,    "slot 4 is the lower bar");
	alphaExpectGlyph(p.lcd, 5, BF_LR,    "slot 5 is the bottom-right piece");
	alphaExpectGlyph(p.lcd, 6, BF_UMB,   "slot 6 is the upper-and-middle bar");
	alphaExpectGlyph(p.lcd, 7, BF_WIFI0, "slot 7 starts as the X of no signal");
	for (int s = 0; s < 8; s++) TEST_ASSERT_EQUAL_UINT_MESSAGE(1, p.lcd.glyphLoads[s], "each slot is loaded once by begin( )");
}

static void test_alpha_frame_link_glyph_follows_the_rssi(void) {
	static const struct { int32_t rssi; const uint8_t* glyph; const char* what; } cases[] = {
		{ -45, BF_WIFI5, "-45 dBm: five bars" },  { -50, BF_WIFI5, "-50 dBm is still five" },
		{ -51, BF_WIFI4, "-51 dBm: four" },       { -60, BF_WIFI4, "-60 dBm is still four" },
		{ -65, BF_WIFI3, "-65 dBm: three" },      { -75, BF_WIFI2, "-75 dBm: two" },
		{ -85, BF_WIFI1, "-85 dBm: one" },        { -90, BF_WIFI1, "-90 dBm is still one" },
		{ -91, BF_WIFI0, "-91 dBm: the X" },      { -127, BF_WIFI0, "-127 dBm: the X" },
	};
	for (const auto &c : cases) {
		AlphaPass p;
		p.pass(c.rssi, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
		alphaExpectGlyph(p.lcd, 7, c.glyph, c.what);
		/* nothing else of the CGRAM moves */
		alphaExpectGlyph(p.lcd, 0, BF_LT, c.what);
		alphaExpectGlyph(p.lcd, 6, BF_UMB, c.what);
	}
}

static void test_alpha_frame_link_glyph_loads_only_when_the_level_changes(void) {
	AlphaPass p;
	TEST_ASSERT_EQUAL_UINT(1, p.lcd.glyphLoads[7]);                       /* begin( ) */
	p.pass(-55, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	TEST_ASSERT_EQUAL_UINT(2, p.lcd.glyphLoads[7]);                       /* four bars */
	p.pass(-58, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	p.pass(-60, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	TEST_ASSERT_EQUAL_UINT_MESSAGE(2, p.lcd.glyphLoads[7], "the same level is not loaded again");
	p.pass(-45, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	TEST_ASSERT_EQUAL_UINT(3, p.lcd.glyphLoads[7]);
	for (int s = 0; s < 7; s++) TEST_ASSERT_EQUAL_UINT_MESSAGE(1, p.lcd.glyphLoads[s], "slots 0-6 are loaded once, by begin( )");
}

/* ── The splash and the boot ────────────────────────────────────────────────────────────────────────────────── */

static void test_alpha_frame_splash_owns_the_whole_screen(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SPLASH);
	in.banner = ALPHA_TEST_BANNER;
	p.draw(in);
	alphaExpectGlass(p.lcd, "SIMUT 2.13.0    ", "  Inicializando ", "the splash");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(1, p.lcd.glyphLoads[7], "the splash comes before the loop and loads no link glyph");
	TEST_ASSERT_EQUAL_UINT(1, p.lcd.blits);
}

static void test_alpha_frame_boot_bar_half_full(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_BOOT);
	in.banner = ALPHA_TEST_BANNER; in.bootBar = 7;
	p.pass(-60, in);
	/* row 0 keeps the previous frame past the banner: the boot has never cleared (columns 12-15) */
	alphaExpectGlass(p.lcd, "SIMUT 2.13.0xxxx", "[#######       ]", "the boot bar at 7 of 14");
}

/* Bar 0 is a frame the loop never draws: barTimer starts at 0 and the splash waits 1,200 ms, so the first boot pass always advances the
 * bar (the review's loop-level A/B, 2,500 runs of the old loop per build, 2026-11-04). It is pinned so that alphaDrawFrame( ) draws any
 * value right, not as a state for the loop to reach. */
static void test_alpha_frame_boot_bar_empty_and_full(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_BOOT);
	in.banner = ALPHA_TEST_BANNER;
	in.bootBar = 0;  p.pass(-60, in);
	alphaExpectGlass(p.lcd, "SIMUT 2.13.0xxxx", "[              ]", "the boot bar at 0");
	in.bootBar = 1;  p.pass(-60, in);
	alphaExpectGlass(p.lcd, "SIMUT 2.13.0xxxx", "[#             ]", "the boot bar at 1");
	in.bootBar = 14; p.pass(-60, in);
	alphaExpectGlass(p.lcd, "SIMUT 2.13.0xxxx", "[##############]", "the boot bar at 14");
}

static void test_alpha_frame_boot_full_ignores_the_counter_and_does_not_clear(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_BOOT_FULL);
	in.banner = ALPHA_TEST_BANNER; in.bootBar = 3;
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "SIMUT 2.13.0xxxx", "[##############]", "the full bar of the transition");
}

/* ── The network screen of the end of the boot ──────────────────────────────────────────────────────────────── */

static void test_alpha_frame_net_connected_with_the_address(void) {
	AlphaPass p;
	AlphaFrameIn waiting(ALPHA_SCREEN_NET);
	waiting.online = true;
	p.pass(-60, waiting);                          /* the frame before it: "Obtendo IP..." */
	AlphaFrameIn in(ALPHA_SCREEN_NET);
	in.online = true; in.hasIp = true; in.ip = "192.168.0.25";
	p.pass(-60, in);
	/* the second row is blanked before the address, or the dots of "Obtendo IP..." would survive it */
	alphaExpectGlass(p.lcd, " Conectado!     ", "192.168.0.25    ", "connected with an address");
}

static void test_alpha_frame_net_waiting_for_the_address(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_NET);
	in.online = true; in.hasIp = false; in.ip = "";
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, " Conectado!     ", "  Obtendo IP... ", "online without an address");
}

static void test_alpha_frame_net_offline(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_NET);
	in.online = false; in.hasIp = false;
	p.pass(-127, in);
	alphaExpectGlass(p.lcd, " Sem WiFi       ", "    Offline     ", "offline");
	p.previousFrame( );
	in.hasIp = true; in.ip = "192.168.0.25";       /* an address the link no longer backs */
	p.pass(-127, in);
	alphaExpectGlass(p.lcd, " Sem WiFi       ", "    Offline     ", "offline wins over a stale address");
}

static void test_alpha_frame_clear_empties_both_rows(void) {
	AlphaPass p; p.previousFrame( );
	p.pass(-60, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	alphaExpectGlass(p.lcd, "                ", "                ", "the cleared screen");
}

/* ── The sensor screen ──────────────────────────────────────────────────────────────────────────────────────── */

static void test_alpha_frame_temperature_with_the_degree_and_the_link_glyph(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 2; in.slot = 0; in.channel = CH_TEMP; in.valid = true; in.value = 23.4f;
	p.pass(-55, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK "  " GL_UMB GL_UMB GL_RT GL_UMB GL_UMB GL_RT " " GL_LL GL_LB GL_RT "o ",
		"S0  " GL_LL GL_LB GL_LB GL_LB GL_LB GL_LR "," "  " GL_LR " C",
		"23.4 on slot 0 of 2");
	alphaExpectGlyph(p.lcd, 7, BF_WIFI4, "-55 dBm is four bars");
}

static void test_alpha_frame_temperature_below_zero(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 1; in.slot = 0; in.channel = CH_TEMP; in.valid = true; in.value = -18.5f;
	p.pass(-70, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK " " GL_LB " " GL_RT " " GL_LT GL_UMB GL_RT " " GL_LT GL_UMB GL_UMB "o ",
		"     " GL_LR " " GL_LL GL_LB GL_LR "," GL_LB GL_LB GL_LR " C",
		"-18.5 alone and nothing pending");
}

static void test_alpha_frame_humidity(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 2; in.slot = 1; in.channel = CH_HUM; in.valid = true; in.value = 58.0f;
	p.pass(-70, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK "     " GL_LT GL_UMB GL_UMB GL_LT GL_UMB GL_RT "   ",
		"S1     " GL_LB GL_LB GL_LR GL_LL GL_LB GL_LR "%UR",
		"58 % on slot 1 of 2");
}

static void test_alpha_frame_pressure_in_plain_characters(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 3; in.slot = 2; in.channel = CH_PRESS; in.valid = true; in.value = 1013.2f;
	p.pass(-70, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK " 1013.2 hPa   ",
		"S2              ",
		"1013.2 hPa on slot 2 of 3");
	/* the tenth is rounded half up: 1013.25 is exact in a float and reads 1013.3 */
	in.value = 1013.25f;
	p.previousFrame( );
	p.pass(-70, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK " 1013.3 hPa   ",
		"S2              ",
		"1013.25 rounds up");
}

static void test_alpha_frame_error_when_no_slot_is_active(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 0; in.pending = 37;                  /* a count is not shown with no slot: there is nothing to say it of */
	p.pass(-60, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK " " GL_LT GL_UMB GL_UMB GL_LT GL_UB GL_RT GL_LT GL_UB GL_RT GL_LT GL_UB GL_RT " ",
		"   " GL_LL GL_LB GL_LB GL_LL GL_UB GL_RT GL_LL GL_UB GL_RT GL_LL GL_LB GL_LR " ",
		"ERRO with no slot");
}

static void test_alpha_frame_error_for_a_reading_that_is_not_one(void) {
	for (int why = 0; why < 3; why++) {
		AlphaPass p; p.previousFrame( );
		AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
		in.slots = 1; in.slot = 0; in.pending = 5;
		in.channel = (why == 2) ? CH_LUX : CH_TEMP;   /* a channel the screen has no digits for */
		in.valid   = (why != 0);
		in.value   = (why == 1) ? NAN : 23.4f;
		p.pass(-60, in);
		alphaExpectGlass(p.lcd,
			"W" GL_LINK " " GL_LT GL_UMB GL_UMB GL_LT GL_UB GL_RT GL_LT GL_UB GL_RT GL_LT GL_UB GL_RT " ",
			"5  " GL_LL GL_LB GL_LB GL_LL GL_UB GL_RT GL_LL GL_UB GL_RT GL_LL GL_LB GL_LR " ",
			why == 0 ? "ERRO for an invalid reading" : why == 1 ? "ERRO for a NaN" : "ERRO for a channel without digits");
	}
}

static void test_alpha_frame_pending_count_with_one_slot(void) {
	static const struct { uint16_t n; const char* label; } cases[] = { {0, "    "}, {37, "37  "}, {999, "999 "}, {1000, "1k  "}, {2500, "2k  "} };
	for (const auto &c : cases) {
		AlphaPass p; p.previousFrame( );
		AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
		in.slots = 1; in.slot = 0; in.channel = CH_TEMP; in.valid = true; in.value = 23.4f; in.pending = c.n;
		p.pass(-55, in);
		char row1[17];
		snprintf(row1, sizeof(row1), "%s%c%c%c%c%c%c,  %c C", c.label, 3, 4, 4, 4, 4, 5, 5);   /* label, the digits' bottom row, the comma, the tenth */
		char row0[17];
		snprintf(row0, sizeof(row0), "W%c  %c%c%c%c%c%c %c%c%co ", 7, 6, 6, 2, 6, 6, 2, 3, 4, 2);
		alphaExpectRow(p.lcd, 0, row0, c.label);
		alphaExpectRow(p.lcd, 1, row1, c.label);
	}
}

static void test_alpha_frame_slot_id_replaces_the_pending_count(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 3; in.slot = 1; in.channel = CH_TEMP; in.valid = true; in.value = 23.4f; in.pending = 37;
	p.pass(-55, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK "  " GL_UMB GL_UMB GL_RT GL_UMB GL_UMB GL_RT " " GL_LL GL_LB GL_RT "o ",
		"S1  " GL_LL GL_LB GL_LB GL_LB GL_LB GL_LR "," "  " GL_LR " C",
		"three slots: S1, not the count");
	in.slot = 10;                                   /* the Wire has eleven slots: three columns */
	p.previousFrame( );
	p.pass(-55, in);
	alphaExpectGlass(p.lcd,
		"W" GL_LINK "  " GL_UMB GL_UMB GL_RT GL_UMB GL_UMB GL_RT " " GL_LL GL_LB GL_RT "o ",
		"S10 " GL_LL GL_LB GL_LB GL_LB GL_LB GL_LR "," "  " GL_LR " C",
		"slot 10");
}

/* A frame the loop does not reach: the AP branch comes before the sensor branch, so the sensor screen is drawn with apMode false (before
 * the move, a Core 0 store between the loop's two reads of apMode could show it for one pass). Pinned so that alphaDrawFrame( ) draws the
 * field right, not as a state for the loop to reach (the review of 2026-11-04 found no blitted row of the old loop with it). */
static void test_alpha_frame_status_cell_says_ap_in_ap_mode(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 1; in.slot = 0; in.channel = CH_TEMP; in.valid = true; in.value = 23.4f; in.apMode = true;
	p.pass(-55, in);
	alphaExpectGlass(p.lcd,
		"AP  " GL_UMB GL_UMB GL_RT GL_UMB GL_UMB GL_RT " " GL_LL GL_LB GL_RT "o ",
		"    " GL_LL GL_LB GL_LB GL_LB GL_LB GL_LR "," "  " GL_LR " C",
		"AP replaces the link cell");
}

/* ── The setup network's three pages (a radio build only) ───────────────────────────────────────────────────── */

static void test_alpha_frame_ap_pages(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_AP);
	in.apPage = 0; in.apValue = "192.168.4.1"; in.apStep = 0;
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Modo AP  1 de 3 ", "192.168.4.1     ", "AP page 1: the address");
	p.previousFrame( );
	in.apPage = 1; in.apValue = "simut_SETUP";
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Rede:    2 de 3 ", "simut_SETUP     ", "AP page 2: the network");
	p.previousFrame( );
	in.apPage = 2; in.apValue = "(aberta)";
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Senha:   3 de 3 ", "(aberta)        ", "AP page 3: the key");
}

static void test_alpha_frame_ap_draws_the_title_and_the_value_it_is_given(void) {
	/* The loop chooses the value before it turns the page, so the pass that turns it draws the new title over the old
	 * page's value (one frame, AP_SCROLL_MS). The frame draws exactly what it is handed: this is the loop's data, kept. */
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_AP);
	in.apPage = 1; in.apValue = "192.168.4.1"; in.apStep = 0;
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Rede:    2 de 3 ", "192.168.4.1     ", "the pass that turns the page");
}

static void test_alpha_frame_ap_scrolls_a_value_wider_than_the_screen(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_AP);
	in.apPage = 1; in.apValue = "ABCDEFGHIJKLMNOPQRSTUVWX";   /* 24 characters */
	in.apStep = 0;  p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Rede:    2 de 3 ", "ABCDEFGHIJKLMNOP", "step 0");
	in.apStep = 5;  p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Rede:    2 de 3 ", "FGHIJKLMNOPQRSTU", "step 5");
	in.apStep = 12; p.pass(-60, in);                           /* the end, the two-column gap, the beginning */
	alphaExpectGlass(p.lcd, "Rede:    2 de 3 ", "MNOPQRSTUVWX  AB", "step 12 wraps through the gap");
}

/* ── The firmware update ────────────────────────────────────────────────────────────────────────────────────── */

static void test_alpha_frame_ota_lines(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_OTA);
	in.otaPhase = OTA_PH_RECEIVING;
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Atualizando...  ", "Nao desligue!   ", "update: receiving");
	p.previousFrame( );
	in.otaPhase = OTA_PH_REFUSED; in.otaWhy = OTA_WHY_UNSIGNED;
	p.pass(-60, in);
	alphaExpectGlass(p.lcd, "Atualiz.recusada", "sem assinatura  ", "update: refused, unsigned");
}
