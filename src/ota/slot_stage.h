/**
 * @file    src/ota/slot_stage.h
 * @brief   The RP2350's update into the inactive slot, with its first sector last.
 * @details docs/analysis/OTA_AB_RP2350.md, step 4. The boot ROM boots a slot
 *          only if its first 4 KB carry an image definition. So the writer
 *          erases that sector before anything else, holds the image's first
 *          4 KB in RAM while the rest goes to flash, and writes them only at
 *          the commit. The commit is the apply's, right before the reboot
 *          into the slot, not the stage's: a stage that checked out only marks
 *          the image ready. A cut or a reset at any point before the commit
 *          leaves a slot with no image definition, and the ROM stays on the
 *          one that runs. That matters going from B to A: with both slots
 *          whole and no versions, the ROM takes A, so a first sector written
 *          at the stage would install the update at the next reset, apply or
 *          not. The RP2040's applier did the opposite (applier.cpp), which is
 *          why a cut there needed BOOTSEL.
 *          Pure: the flash comes in through SlotFlashOps, so native_otasig
 *          runs it on a simulated NOR flash. staging.cpp hands it the SDK's.
 *          Header-only, so the RP2040's images, which never include it, link
 *          nothing new and stay byte-identical: two .cpp files here, even
 *          compiled empty, renumbered the link's input sections and reordered
 *          45 of the 60 linker veneers (measured 2026-10-03).
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#pragma once
#include <stdint.h>
#include <string.h>

namespace ota {

constexpr uint32_t SLOT_SECTOR = 4096u;
constexpr uint32_t SLOT_BLOCK  = 65536u;
constexpr uint32_t SLOT_NONE   = 0xFFFFFFFFu;

/** The flash, by physical offset. Programs come in whole 256 B pages. */
struct SlotFlashOps {
	bool (*erase_sector)(uint32_t phys);
	bool (*erase_block)(uint32_t phys);   /**< 64 KB; @p phys aligned to it */
	bool (*program)(uint32_t phys, const uint8_t* data, uint32_t len);
	void (*read)(uint32_t phys, uint8_t* dst, uint32_t len);
};

struct SlotStage {
	const SlotFlashOps* ops;
	uint32_t slot_off;         /**< physical offset of the slot being written */
	uint32_t slot_size;
	uint32_t prepared;         /**< bytes from the slot's start known erased */
	bool     active;
	bool     ready;            /**< in whole and checked; the first sector waits for the apply */
	bool     committed;        /**< the first sector is in flash */
	uint8_t  first[SLOT_SECTOR];   /**< the image's first 4 KB until the commit */
};

/** The slot an update writes, from the partition the ROM booted
 *  (rom_get_boot_info): A (0) writes B, B (1) writes A. Anything else, no
 *  partition table or a data partition, has none: SLOT_NONE. */
inline uint32_t slot_inactive_offset(int booted_partition, uint32_t slot_a_off, uint32_t slot_b_off) {
	if (booted_partition == 0) return slot_b_off;
	if (booted_partition == 1) return slot_a_off;
	return SLOT_NONE;
}

/** Starts a slot: erases its first sector, so from here on it has no image. */
inline bool slot_stage_begin(SlotStage& s, const SlotFlashOps* ops, uint32_t slot_off, uint32_t slot_size) {
	memset(&s, 0, sizeof(s));
	if (!ops || slot_off == SLOT_NONE || slot_off % SLOT_SECTOR ||
	    slot_size < SLOT_SECTOR || slot_size % SLOT_SECTOR) return false;
	s.ops = ops;
	s.slot_off = slot_off;
	s.slot_size = slot_size;
	memset(s.first, 0xFF, sizeof(s.first));
	/* The slot may still hold an image, an older one the ROM could pick. Its
	 * definition goes before any byte of the new one is written. */
	if (!ops->erase_sector(slot_off)) return false;
	s.prepared = SLOT_SECTOR;
	s.active = true;
	return true;
}

/** Erases the sector at @p off in the slot; the first is already erased. */
inline bool slot_stage_erase(SlotStage& s, uint32_t off) {
	if (!s.active || s.ready || off % SLOT_SECTOR || off >= s.slot_size) return false;
	if (off < s.prepared) return true;   /* erased at the begin, or by prepare */
	return s.ops->erase_sector(s.slot_off + off);
}

/** Whole pages at @p off in the slot; what falls in the first sector is held. */
inline bool slot_stage_write(SlotStage& s, uint32_t off, const uint8_t* data, uint32_t len) {
	if (!s.active || s.ready || !data || len == 0 || off % 256u || len % 256u) return false;
	if (off > s.slot_size || len > s.slot_size - off) return false;
	if (off < SLOT_SECTOR) {
		uint32_t n = SLOT_SECTOR - off;
		if (n > len) n = len;
		memcpy(s.first + off, data, n);
		off += n;
		data += n;
		len -= n;
		if (len == 0) return true;
	}
	return s.ops->program(s.slot_off + off, data, len);
}

/** The slot as the image will be: the held sector until the commit, flash after. */
inline void slot_stage_read(const SlotStage& s, uint32_t off, uint8_t* dst, uint32_t len) {
	if (!dst || len == 0) return;
	if (!s.active) {
		memset(dst, 0xFF, len);
		return;
	}
	if (!s.committed && off < SLOT_SECTOR) {
		uint32_t n = SLOT_SECTOR - off;
		if (n > len) n = len;
		memcpy(dst, s.first + off, n);
		off += n;
		dst += n;
		len -= n;
		if (len == 0) return;
	}
	s.ops->read(s.slot_off + off, dst, len);
}

/** The image is in and checked; nothing more is written to the slot. Its first
 *  sector stays in RAM: a reset before the commit leaves the slot unbootable. */
/** Erases what an image of @p len bytes will cover, before any of it arrives:
 *  64 KB blocks where the flash is aligned to them, sectors elsewhere, and
 *  never past the slot. Erased as the bytes came, a sector erase held
 *  interrupts off every 4 KB and the stage ran at ~47 KB/s. Erased first,
 *  ~1 MB took 1.4 s (14 blocks of up to 99 ms, 29 sectors of up to 35 ms), and
 *  the upload, page programs alone, ran at ~79 KB/s with no gap between pages
 *  over 95 ms (the Pico 2 W, 2026-10-03). */
inline bool slot_stage_prepare(SlotStage& s, uint32_t len) {
	if (!s.active || s.ready) return false;
	uint32_t end = len > s.slot_size - SLOT_SECTOR ? s.slot_size
	             : (len + SLOT_SECTOR - 1) / SLOT_SECTOR * SLOT_SECTOR;
	while (s.prepared < end) {
		const uint32_t phys = s.slot_off + s.prepared;
		if (phys % SLOT_BLOCK == 0 && end - s.prepared >= SLOT_BLOCK) {
			if (!s.ops->erase_block(phys)) return false;
			s.prepared += SLOT_BLOCK;
		} else {
			if (!s.ops->erase_sector(phys)) return false;
			s.prepared += SLOT_SECTOR;
		}
	}
	return true;
}

inline bool slot_stage_mark_ready(SlotStage& s) {
	if (!s.active || s.ready) return false;
	s.ready = true;
	return true;
}

/** The apply: writes the first sector of a ready image and reads it back. Only
 *  after this can the slot boot. A read-back that differs erases it again. */
inline bool slot_stage_commit(SlotStage& s) {
	if (!s.active || !s.ready || s.committed) return false;
	bool ok = s.ops->erase_sector(s.slot_off) &&
	          s.ops->program(s.slot_off, s.first, SLOT_SECTOR);
	uint8_t back[256];
	for (uint32_t o = 0; ok && o < SLOT_SECTOR; o += sizeof(back)) {
		s.ops->read(s.slot_off + o, back, sizeof(back));
		ok = memcmp(back, s.first + o, sizeof(back)) == 0;
	}
	if (!ok) {
		/* A first sector that did not read back may still carry a whole image
		 * definition, over code that is not the image's. Erased, the slot is
		 * one the ROM skips, as before the commit. */
		(void)s.ops->erase_sector(s.slot_off);
		return false;
	}
	s.committed = true;
	return true;
}

} /* namespace ota */
