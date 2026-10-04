/**
 * @file    src/ota/picobin.h
 * @brief   What the RP2350's boot ROM takes from the start of an image.
 * @details The ROM looks for a PICOBIN block in an image's first 4 KB, on word
 *          boundaries: a start marker, items, a LAST item that gives their
 *          length, a link to the next block, an end marker. Its IMAGE_TYPE
 *          item says what the image is. Before an update writes the first
 *          sector of a slot (docs/analysis/OTA_AB_RP2350.md, step 4), the
 *          staged image has to be one the ROM boots on this board; an RP2040
 *          image, which starts with boot2, carries no such block at all.
 *          Pure: no SDK, so the host suite (native_otasig) runs it on the
 *          block pico2_w_release links. Header-only, as slot_stage.h is, and
 *          for the same reason: the RP2040's images stay byte-identical.
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <string.h>

namespace ota {

constexpr uint32_t PICOBIN_MARK_START = 0xFFFFDED3u;   /* boot/picobin.h */
constexpr uint32_t PICOBIN_MARK_END   = 0xAB123579u;
constexpr uint16_t PICOBIN_TBYB       = 0x8000u;       /* the try-before-you-buy bit of IMAGE_TYPE */

/* An Arm executable for the RP2350 in secure mode: what pico2_w_release links
 * (EXE 0x1, security S 0x20, CPU Arm 0x000, chip RP2350 0x1000). */
constexpr uint16_t PICOBIN_RP2350_ARM_EXE = 0x1021u;

struct PicobinBlock {
	bool     found;           /**< a whole block: start marker, items, LAST, link, end marker */
	uint32_t offset;          /**< where it starts in the image */
	bool     has_image_type;  /**< it carries an IMAGE_TYPE item */
	uint16_t image_type;      /**< that item's flags, as written */
};

inline uint32_t picobin_rd32(const uint8_t* p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* One block that starts at @p off, or found == false. Item headers: the type in
 * bits 0..6; bit 7 says whether the size, in words, takes one byte (bits 8..15)
 * or two (bits 8..23). LAST (0x7F) carries the items' total size, and two words
 * follow it: the link to the next block and the end marker. */
inline PicobinBlock picobin_parse_block(const uint8_t* image, uint32_t len, uint32_t off) {
	PicobinBlock b;
	memset(&b, 0, sizeof(b));
	uint32_t i = off + 4;
	while (i + 4 <= len) {
		const uint32_t w = picobin_rd32(image + i);
		const uint32_t type = w & 0x7Fu;
		const uint32_t size = (w & 0x80u) ? ((w >> 8) & 0xFFFFu) : ((w >> 8) & 0xFFu);
		if (type == 0x7Fu) {
			if (size == (i - (off + 4)) / 4 && i + 12 <= len && picobin_rd32(image + i + 8) == PICOBIN_MARK_END) {
				b.found = true;
				b.offset = off;
				return b;
			}
			break;
		}
		if (size == 0) break;
		if (type == 0x42u && size == 1 && !b.has_image_type) {
			b.has_image_type = true;
			b.image_type = (uint16_t)(w >> 16);
		}
		i += size * 4;
	}
	memset(&b, 0, sizeof(b));
	return b;
}

/** The first whole block in the first 4 KB of @p image, as the ROM looks for it. */
inline PicobinBlock picobin_first_block(const uint8_t* image, uint32_t len) {
	PicobinBlock none;
	memset(&none, 0, sizeof(none));
	if (!image) return none;
	const uint32_t window = len < 4096u ? len : 4096u;
	for (uint32_t off = 0; off + 4 <= window; off += 4) {
		if (picobin_rd32(image + off) != PICOBIN_MARK_START) continue;
		PicobinBlock b = picobin_parse_block(image, len, off);
		if (b.found) return b;
	}
	return none;
}

/** Whether that block defines an image of the kind this board boots. The TBYB
 *  bit is masked: it marks a trial, not another image, and after the buy the
 *  ROM clears it in flash. */
inline bool picobin_is_rp2350_arm_exe(const PicobinBlock& b) {
	return b.found && b.has_image_type &&
	       (uint16_t)(b.image_type & (uint16_t)~PICOBIN_TBYB) == PICOBIN_RP2350_ARM_EXE;
}

} /* namespace ota */
