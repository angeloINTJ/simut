/**
 * @file ScreenRle.h
 * @brief Palette RLE for TFT strips — the wire format behind /api/screen_stream.
 * @details Header-only and free of Arduino dependencies so the native suite
 * (test/test_validators) compiles the same encoder the firmware ships.
 *
 * @project SIMUT — Integrated Universal Monitoring and Telemetry System
 * @author Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * WHY a codec at all. A 320x240 frame is 153,600 B of RGB565, and the mirror
 * pays for it twice: once on the SPI wire reading the panel back, once on the
 * air. This touches only the second half — but that half was the larger one.
 * Measured against the 17 bench captures in docs/images/screens (reduced to
 * the native 320x240; the files there are saved at --scale 2) and the 221 KB/s
 * download rate measured in the netstorm campaign:
 *
 *   BMP 24 bpp, what /api/screenshot sends   230,454 B   1,018 ms
 *   raw RGB565                               153,600 B     679 ms
 *   this codec, average frame                  9,454 B      41 ms
 *   this codec, worst frame measured           18,139 B      79 ms
 *
 * On the rig (2026-09-18) a frame came out between 3.4 and 13.3 kB across six
 * screens and the raw fallback never fired once — but the wire turned out not
 * to be the thing that matters: the panel read is 91% of a mirror frame and
 * the whole payload is 3%. The codec buys the network back; nothing more.
 *
 * WHY a palette rather than the colour itself. The TFT UI draws from a theme,
 * not from photographs: across those 17 screens a whole frame holds 5..12
 * distinct colours and an 8-row strip holds at most 8. One index byte per run
 * beats two colour bytes by 33% (9,454 vs 13,612 B per frame) and in practice
 * never needs more than 16 palette entries. The 256 ceiling is what the index
 * byte allows; a strip over it goes out raw, which never happened in the
 * measurements but is the reason the escape exists.
 *
 * FORMAT — little-endian throughout. The raw fallback is a straight copy of
 * the frame buffer, and byte-swapping 2,560 pixels per strip to satisfy a
 * big-endian header would cost more than a consistent header is worth. This
 * is the one place the project differs from /api/screenshot_chunk, whose
 * 12-byte header is big-endian.
 *
 *   frame:  'S' 'R' '1' | w:u16 | h:u16 | stripRows:u8 | strips:u8
 *   strip:  enc:u8 | len:u16 | payload[len]        (repeated `strips` times)
 *
 *   enc 0 — ENC_RAW565: payload is stripRows*w pixels, u16 each, top-down.
 *   enc 1 — ENC_PAL_RLE:
 *       ncol-1:u8 | palette[ncol]:u16 | pairs...
 *       pair = index:u8, follow:u8. The run covers follow+1 pixels, so one
 *       pair carries up to 256 and a longer run simply opens another pair.
 *
 * A frame can tear: strips are read under separate Core 1 pauses, so two of
 * them can straddle a repaint. That is what any capture without vsync does,
 * and the alternative — one pause for the whole frame — freezes the renderer
 * for the second the frame takes, which would make the mirror show a still
 * image of a panel that is not moving because we stopped it.
 */
namespace screenrle {

constexpr uint8_t ENC_RAW565  = 0;
constexpr uint8_t ENC_PAL_RLE = 1;

constexpr size_t MAX_PALETTE  = 256;  /* the index byte */
constexpr size_t MAX_RUN      = 256;  /* the count byte: follow+1 */
constexpr size_t FRAME_HEADER = 9;
constexpr size_t STRIP_HEADER = 3;

/**
 * Encodes `n` RGB565 pixels into `out`, writing at most `cap` bytes.
 *
 * Returns the number of bytes written, or 0 when the strip cannot be encoded
 * smaller than `cap` — either because it holds more than MAX_PALETTE colours
 * or because the pairs ran past the budget. The caller ships the strip raw in
 * that case, so passing `cap` = n*2 (the raw size) makes the return value
 * exactly the rule "send whichever is smaller", and makes the worst case of
 * the whole format the raw size plus a 3-byte header instead of the 2x that
 * an unguarded RLE costs on pathological content.
 */
inline size_t encodeStrip(const uint16_t* px, size_t n, uint8_t* out, size_t cap) {
	if (!px || !out || n == 0) return 0;

	/* Pass 1 — the palette. The lookup runs once per RUN, not once per pixel,
	 * which is what makes a linear search affordable: an 8-row strip of these
	 * screens holds ~150 runs, so even a full 256-entry palette costs ~38k
	 * comparisons against the 2,560 the run detection itself needs — one per
	 * pixel of a 320x8 strip. */
	uint16_t pal[MAX_PALETTE];
	size_t   ncol = 0;
	{
		uint16_t cur = px[0];
		for (size_t i = 1; ; i++) {
			const bool end = (i == n);
			if (end || px[i] != cur) {
				size_t k = 0;
				while (k < ncol && pal[k] != cur) k++;
				if (k == ncol) {
					if (ncol == MAX_PALETTE) return 0;
					pal[ncol++] = cur;
				}
				if (end) break;
				cur = px[i];
			}
		}
	}

	const size_t head = 1 + ncol * 2;
	if (head >= cap) return 0;

	size_t o = 0;
	out[o++] = (uint8_t)(ncol - 1);
	for (size_t k = 0; k < ncol; k++) {
		out[o++] = (uint8_t)(pal[k] & 0xFF);
		out[o++] = (uint8_t)(pal[k] >> 8);
	}

	/* Pass 2 — the pairs. Bails the moment it would reach `cap`: a guard that
	 * fires late is not a guard, and the caller's fallback is free. */
	uint16_t cur = px[0];
	size_t   run = 1;
	for (size_t i = 1; ; i++) {
		const bool end = (i == n);
		if (!end && px[i] == cur) { run++; continue; }

		size_t k = 0;
		while (k < ncol && pal[k] != cur) k++;
		if (k == ncol) return 0; /* unreachable: pass 1 interned every run */

		while (run > 0) {
			const size_t take = (run > MAX_RUN) ? MAX_RUN : run;
			if (o + 2 > cap) return 0;
			out[o++] = (uint8_t)k;
			out[o++] = (uint8_t)(take - 1);
			run -= take;
		}

		if (end) break;
		cur = px[i];
		run = 1;
	}

	return o;
}

} /* namespace screenrle */
