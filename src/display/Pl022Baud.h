/**
 * @file display/Pl022Baud.h
 * @brief What an RP2040/RP2350 SPI block really runs at, for a request, from the clock it divides.
 *
 * The PL022 divides clk_peri by an even prescale (2..254) times a post-divider (1..256), so a request
 * lands on a rung of a ladder, never between two: the largest baud that does not exceed it, and clk_peri / 2
 * at the very most. This is the arithmetic of spi_set_baudrate( ) in pico-sdk (hardware_spi/spi.c, the SDK of
 * arduino-pico 5.6.1), kept as a pure function so the host can pin it. It exists because the panel asked for
 * 62.5 MHz for two months and ran at 24: the framework's set_sys_clock_khz( ) had moved clk_peri to the 48 MHz
 * USB PLL, whose ceiling is 24 MHz, and nobody had a way to see that from a request and a clock
 * (CHANGELOG, Unreleased; test_validators, the pl022 cases).
 *
 * The firmware does not call it: the SDK programs the block. The model is held to the SDK's own function by a
 * differential run over a grid (the PR description of 2026-10-15), and to the requests this firmware makes by
 * the native cases. Not modelled: the SDK's parameter assertions (a request above clk_peri gets the ceiling here,
 * and a request the divider cannot make gets baud 0 where the SDK programs a prescale of 256).
 *
 * Header-only, no Arduino, so a host test reaches it (test_validators).
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @license MIT License
 */

#pragma once

#include <stdint.h>

namespace pl022 {

/** The divider spi_set_baudrate( ) programs for a request, and the baud it gives. */
struct Rung {
	uint32_t prescale;   /**< CPSR: even, 2..254. 0 when the SDK cannot make the request (too low). */
	uint32_t postdiv;    /**< SCR + 1: 1..256 */
	uint32_t baud;       /**< clk_peri / ( prescale * postdiv ), in Hz; 0 when refused */
};

/** The rung `wantHz` gets from a block clocked at `periHz`. Same loops, same comparisons as the SDK. */
constexpr Rung rung( uint32_t periHz, uint32_t wantHz ) {
	/* The smallest prescale that puts the output inside the post-divider's reach. */
	uint32_t prescale = 2;
	for ( ; prescale <= 254; prescale += 2 ) {
		if ( periHz < prescale * 256ull * wantHz ) break;
	}
	if ( prescale > 254 ) return Rung{ 0, 0, 0 };   /* "frequency too low": the SDK's assertion */

	/* The largest post-divider that makes the output <= the request: strictly, so a request that is
	 * exactly a rung gets that rung and not the one below. */
	uint32_t postdiv = 256;
	for ( ; postdiv > 1; --postdiv ) {
		if ( periHz / ( prescale * ( postdiv - 1 ) ) > wantHz ) break;
	}
	return Rung{ prescale, postdiv, periHz / ( prescale * postdiv ) };
}

/** The baud alone. */
constexpr uint32_t baud( uint32_t periHz, uint32_t wantHz ) {
	return rung( periHz, wantHz ).baud;
}

}  // namespace pl022
