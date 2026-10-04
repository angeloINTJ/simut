"""
The factory .uf2 of the Pico 2 W: what a board whose flash holds nothing takes
from BOOTSEL.

It is the partition table and the program in slot A, every block in the
absolute family and numbered as one download (docs/analysis/OTA_AB_RP2350.md,
step 3). Two places write it:

  * tools/check_rp2350_image.py, after every pico2_w_release link, from
    firmware.bin. picotool is at hand there, and the build holds this writer
    to what picotool writes;
  * tools/release_manifest.py, in the release's publish job, from the signed
    update with its trial flag cleared (step 7). picotool is not at hand there.

One writer for both, so the file a release publishes is made the way the
bench's is.

No RP2350-E10 block. picotool adds one only to a file whose blocks go to a
partition by family (`family_id != ABSOLUTE_FAMILY_ID`, elf2uf2.cpp), and this
file is absolute throughout.

Project: SIMUT
License: MIT
"""

import struct

MAGIC_START0, MAGIC_START1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY = 0x2000
FAMILY_ABSOLUTE = 0xE48BFF57
XIP = 0x10000000
PAGE, BLOCK = 256, 512


def blocks(data):
    """The 512-byte blocks of a UF2 file, each one checked for its magic words."""
    if not data or len(data) % BLOCK:
        raise ValueError(f"{len(data)} B is not a whole number of UF2 blocks")
    out = []
    for i in range(0, len(data), BLOCK):
        b = bytearray(data[i:i + BLOCK])
        if struct.unpack_from("<2I", b, 0) != (MAGIC_START0, MAGIC_START1) or \
                struct.unpack_from("<I", b, 508)[0] != MAGIC_END:
            raise ValueError(f"block {i // BLOCK} is not a UF2 block")
        out.append(b)
    return out


def image_blocks(image, base, family):
    """The image in 256-byte pages from base, the last one padded with zeros,
    the way picotool's `uf2 convert` writes a .bin. join( ) numbers them."""
    out = []
    for off in range(0, len(image), PAGE):
        page = image[off:off + PAGE]
        b = bytearray(BLOCK)
        struct.pack_into("<8I", b, 0, MAGIC_START0, MAGIC_START1, FLAG_FAMILY,
                         base + off, PAGE, 0, 0, family)
        b[32:32 + len(page)] = page
        struct.pack_into("<I", b, 508, MAGIC_END)
        out.append(b)
    return out


def join(parts):
    """One UF2 file from blocks in order, numbered 0..n-1 of n: one download."""
    for n, b in enumerate(parts):
        struct.pack_into("<2I", b, 20, n, len(parts))
    return b"".join(bytes(b) for b in parts)


def factory(table_uf2, image, slot_a):
    """firmware_factory.uf2: the table, as picotool's `partition create` wrote
    it (partition_table.uf2), then the image at slot_a. Refuses a table file
    whose blocks are not absolute and in front of slot A."""
    table = blocks(table_uf2)
    for b in table:
        flags, addr, size = struct.unpack_from("<3I", b, 8)
        family = struct.unpack_from("<I", b, 28)[0]
        if not flags & FLAG_FAMILY or family != FAMILY_ABSOLUTE or \
                not XIP <= addr or addr + size > slot_a:
            raise ValueError(f"block at {addr:#x}, family {family:#x}: this is not the partition "
                             f"table, absolute and in front of slot A ({slot_a:#x})")
    return join(table + image_blocks(image, slot_a, FAMILY_ABSOLUTE))
