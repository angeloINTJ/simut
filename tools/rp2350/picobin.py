"""
The PICOBIN blocks of an RP2350 image, as the boot ROM reads them, and the
try-before-you-buy (TBYB) bit of the image an update installs.

The ROM looks for a block in an image's first 4 KB, on word boundaries: a start
marker, items, a LAST item that gives their length, a link to the next block of
the loop, an end marker. The image definition is the block that carries an
IMAGE_TYPE item (src/ota/picobin.h reads the same thing on the board).

An update goes in flagged for trial (docs/analysis/OTA_AB_RP2350.md, step 5):
bit 0x8000 of IMAGE_TYPE. The ROM boots such an image only right after the
FLASH_UPDATE reboot that installs it, and only keeps it if the image buys
itself; from a plain load it never starts. So the flag goes into the over-the-
air image alone, after the link and before the signature, which covers it: the
.uf2 a board takes over USB stays unflagged. check_rp2350_image.py writes the
flagged copy next to the build, as firmware_ota.bin.

Project: SIMUT
License: MIT
"""

import struct

MARK_START, MARK_END = 0xFFFFDED3, 0xAB123579
ITEM_IMAGE_TYPE, ITEM_LAST = 0x42, 0x7F
TBYB = 0x8000
# Items whose bytes a later change to the block would break: the ROM leaves the
# TBYB bit out of the hash, but a block this tool has never seen hashed or
# signed is a block it does not touch (S3 decides how a sealed image is marked).
SEALED = {0x47: "HASH_DEF", 0x09: "SIGNATURE", 0x4B: "HASH_VALUE"}
MAX_LOOP = 16


def parse_block(image, off):
    """(items, link) of the block that starts at off, or (None, None) when the
    words there do not make a whole block. items maps an item type to a list of
    (position of its first word, that word)."""
    items, i = {}, off + 4
    while i + 4 <= len(image):
        word = struct.unpack_from("<I", image, i)[0]
        typ = word & 0x7F
        size = (word >> 8) & (0xFFFF if word & 0x80 else 0xFF)
        if typ == ITEM_LAST:
            # LAST carries the block's length; the link to the next block and
            # the end marker follow it.
            end = i + 8
            if end + 4 <= len(image) and struct.unpack_from("<I", image, end)[0] == MARK_END:
                return items, struct.unpack_from("<i", image, i + 4)[0]
            break
        if size == 0:
            break
        items.setdefault(typ, []).append((i, word))
        i += size * 4
    return None, None


def first_block(image):
    """(offset, items) of the first PICOBIN block in the first 4 KB, the way
    the boot ROM looks for it; items maps an item type to its first word.
    (None, None) without a start marker; (offset, None) when the first marker
    starts no whole block."""
    for off in range(0, min(len(image), 4096) - 8, 4):
        if struct.unpack_from("<I", image, off)[0] != MARK_START:
            continue
        items, _ = parse_block(image, off)
        if items is None:
            return off, None
        return off, {t: v[0][1] for t, v in items.items()}
    return None, None


def loop_blocks(image, first):
    """[(offset, items)] of the block loop that starts at first: each block's
    link is the next one's distance from it, and the loop ends back at first."""
    blocks, off = [], first
    for _ in range(MAX_LOOP):
        if not 0 <= off <= len(image) - 4 or struct.unpack_from("<I", image, off)[0] != MARK_START:
            raise ValueError(f"the block loop leads to {off:#x}, where no block starts")
        items, link = parse_block(image, off)
        if items is None:
            raise ValueError(f"the block at {off:#x} is not whole")
        blocks.append((off, items))
        off += link
        if off == first:
            return blocks
    raise ValueError(f"the block loop from {first:#x} does not come back within {MAX_LOOP} blocks")


def mark_trial(image):
    """The image with the TBYB bit set in its image definition. Refuses an
    image whose first block is not the one image definition of its loop, one
    already flagged, and one whose blocks are hashed or signed."""
    off, items = first_block(image)
    if off is None or items is None:
        raise ValueError("no complete PICOBIN block in the first 4 KB")
    blocks = loop_blocks(image, off)
    defs = [b for b, its in blocks if ITEM_IMAGE_TYPE in its]
    if defs != [off]:
        raise ValueError(f"the image definitions of the loop are at {[hex(d) for d in defs]}, "
                         f"want the first block ({off:#x}) alone")
    for b, its in blocks:
        sealed = sorted(SEALED[t] for t in its if t in SEALED)
        if sealed:
            raise ValueError(f"the block at {b:#x} carries {', '.join(sealed)}: a sealed image "
                             "is not marked here")
    pos, word = blocks[0][1][ITEM_IMAGE_TYPE][0]
    if (word >> 16) & TBYB:
        raise ValueError("the image is already flagged for trial")
    out = bytearray(image)
    struct.pack_into("<I", out, pos, word | (TBYB << 16))
    return bytes(out)
