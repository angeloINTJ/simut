/**
 * @file    test/test_lan/alpha_frame_wired_cases.h
 * @brief   What display/AlphaFrame.h writes differently on the cable (the SIMUT Wire's 16x2): a case list of test_main.cpp.
 * @details The screens the radio and the cable share are test_validators' (alpha_frame_cases.h); here are the four
 *          places the original loop branched on SIMUT_NET_LAN, and the one it did not have: the cell the link's glyph
 *          sits in (a space and the cable, never a "W", and no "AP"), the offline text ("Sem rede"), the glyph
 *          slot 7 takes (the cable up, or the X), and the setup AP's screen, which a wired build does not draw.
 *          Expected rows are read from the loop at 34fc44a9 as in the radio's cases; AlphaPass is AlphaFrameCheck.h's.
 *
 * @project SIMUT
 * @license MIT License
 */
#pragma once
#include "AlphaFrameCheck.h"

static_assert(SIMUT_NET_LAN == 1 && SIMUT_NET_WIFI == 0, "these cases are the cable's: the radio's are test_validators'");

static void test_alpha_frame_wired_status_cell_is_the_cable(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 1; in.slot = 0; in.channel = CH_TEMP; in.valid = true; in.value = 23.4f;
	in.apMode = true;                       /* a wired build has no setup AP: the flag is not drawn */
	p.pass(-50, in);
	alphaExpectGlass(p.lcd,
		" " GL_LINK "  " GL_UMB GL_UMB GL_RT GL_UMB GL_UMB GL_RT " " GL_LL GL_LB GL_RT "o ",
		"    " GL_LL GL_LB GL_LB GL_LB GL_LB GL_LR "," "  " GL_LR " C",
		"the cable's cell: a space and the glyph, no W and no AP");
	alphaExpectGlyph(p.lcd, 7, BF_WIRED, "link up: the cable glyph");
}

static void test_alpha_frame_wired_link_glyph_is_the_cable_or_the_x(void) {
	AlphaPass p;
	TEST_ASSERT_EQUAL_UINT(1, p.lcd.glyphLoads[7]);                       /* begin( ): the X */
	p.pass(-50, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	alphaExpectGlyph(p.lcd, 7, BF_WIRED, "-50 dBm reads as up");
	TEST_ASSERT_EQUAL_UINT(2, p.lcd.glyphLoads[7]);
	p.pass(-99, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	alphaExpectGlyph(p.lcd, 7, BF_WIRED, "-99 dBm is the last that reads as up");
	TEST_ASSERT_EQUAL_UINT_MESSAGE(2, p.lcd.glyphLoads[7], "up again is not loaded again");
	p.pass(-100, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	alphaExpectGlyph(p.lcd, 7, BF_WIFI0, "-100 dBm reads as down: the X");
	TEST_ASSERT_EQUAL_UINT(3, p.lcd.glyphLoads[7]);
	p.pass(-127, AlphaFrameIn(ALPHA_SCREEN_CLEAR));
	TEST_ASSERT_EQUAL_UINT_MESSAGE(3, p.lcd.glyphLoads[7], "down again is not loaded again");
}

static void test_alpha_frame_wired_offline_says_rede(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_NET);
	in.online = false;
	p.pass(-127, in);
	alphaExpectGlass(p.lcd, " Sem rede       ", "    Offline     ", "offline on the cable");
}

static void test_alpha_frame_wired_has_no_setup_ap_screen(void) {
	AlphaPass p; p.previousFrame( );
	AlphaFrameIn in(ALPHA_SCREEN_AP);
	in.apPage = 1; in.apValue = "simut_SETUP";
	p.pass(-50, in);
	alphaExpectGlass(p.lcd, "xxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxx", "the setup AP's screen draws nothing on the cable");
}

/* What the other core reads from the cable's glass (display/LcdGlass.h): the same shadow as the radio's, with the glyph of the cable in slot 7.
 * The glyph is published when it is loaded, ahead of the frame it belongs to, and again only when the link changes. */
static void test_alpha_frame_wired_glass_shadow_follows_the_cable_glyph(void) {
	AlphaPass p; p.previousFrame( );
	LcdGlass g;
	TEST_ASSERT_EQUAL_INT8_MESSAGE(LCD_GLASS_NONE, p.lcd.readGlass(g), "begin( ) and the glyph load: no frame yet");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(BF_WIFI0, g.cgram[7], 8, "the X is in slot 7 before the first pass");
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * 9, g.seq, "the clear and the eight glyphs: nine publishes");
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = 1; in.slot = 0; in.channel = CH_TEMP; in.valid = true; in.value = 23.4f;
	p.lcd.nowMs = 4000;
	p.pass(-50, in);                        /* link up: slot 7 takes the cable, then the frame is blitted */
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, p.lcd.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(BF_WIRED, g.cgram[7], 8, "link up: the cable's glyph is what a reader gets");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(p.lcd.ddram[0], g.ddram[0], 16, "row 0 is the glass's");
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(p.lcd.ddram[1], g.ddram[1], 16, "row 1 is the glass's");
	TEST_ASSERT_EQUAL_UINT32(4000, g.blitMs);
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * 11, g.seq, "the glyph and the blit: two more publishes");
	p.pass(-50, in);                        /* the link did not change: no glyph, one blit */
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, p.lcd.readGlass(g));
	TEST_ASSERT_EQUAL_UINT32_MESSAGE(2 * 12, g.seq, "a pass that loads no glyph publishes once");
	p.pass(-127, in);                       /* link down: the X, then the blit */
	TEST_ASSERT_EQUAL_INT8(LCD_GLASS_OK, p.lcd.readGlass(g));
	TEST_ASSERT_EQUAL_MEMORY_MESSAGE(BF_WIFI0, g.cgram[7], 8, "link down: the X");
	TEST_ASSERT_EQUAL_UINT32(2 * 14, g.seq);
	for (int s = 0; s < 8; s++) TEST_ASSERT_EQUAL_MEMORY_MESSAGE(p.lcd.cgram[s], g.cgram[s], 8, "every glyph is the glass's");
}
