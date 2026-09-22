/**
 * @file AlphaMarquee.h
 * @brief One 16-column window onto a string that may be wider than the LCD.
 *
 * @details The alpha build's setup-AP screen has to name the network the
 * operator must find in their phone's list. A device name is 32 bytes and the
 * SSID appends "_SETUP", so that name can be 37 characters on a display with
 * sixteen columns — and a truncated SSID is worse than none, because it is
 * wrong rather than absent.
 *
 * The window arithmetic lives here, apart from the HD44780, so the host test
 * can check the properties that matter (padding, wrap, the gap that keeps the
 * end from reading as one word with the beginning) without a display.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/** Columns on the LCD this is written for. */
#define ALPHA_LCD_COLS 16

/** Blank columns between the end of the string and its repeat. Two, because
 * one reads as a word break and none reads as a different word. */
#define ALPHA_MARQUEE_GAP 2

/**
 * @brief Fill @p out with the ALPHA_LCD_COLS characters that belong on the
 *        line at @p step.
 *
 * A string that fits is written once and space-padded — it does not scroll,
 * because a name that is already readable has nothing to gain from moving.
 * A longer one scrolls by one column per step and wraps through the gap.
 *
 * @param s    the text; a null or empty string yields a blank line.
 * @param step advances the window; wraps on its own, so a caller may count up
 *             for ever (a uint16_t step and a 37-character span do not divide,
 *             and the seam that causes is one frame of a scroll).
 * @param out  at least ALPHA_LCD_COLS + 1 bytes; always NUL-terminated.
 */
inline void alphaMarqueeWindow(const char* s, uint16_t step, char* out) {
	if (!out) return;
	size_t n = 0;
	while (s && s[n]) n++;
	if (n == 0) {
		for (size_t i = 0; i < ALPHA_LCD_COLS; i++) out[i] = ' ';
		out[ALPHA_LCD_COLS] = '\0';
		return;
	}
	if (n <= ALPHA_LCD_COLS) {
		for (size_t i = 0; i < ALPHA_LCD_COLS; i++) out[i] = (i < n) ? s[i] : ' ';
		out[ALPHA_LCD_COLS] = '\0';
		return;
	}
	const size_t span = n + ALPHA_MARQUEE_GAP;
	const size_t off  = (size_t)step % span;
	for (size_t i = 0; i < ALPHA_LCD_COLS; i++) {
		const size_t k = (off + i) % span;
		out[i] = (k < n) ? s[k] : ' ';
	}
	out[ALPHA_LCD_COLS] = '\0';
}
