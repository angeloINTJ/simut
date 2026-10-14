#!/usr/bin/env python3
"""The over-the-air image of the RP2350, flagged for trial, and nothing else.

docs/analysis/OTA_AB_RP2350.md, step 5: an update boots on trial and stays only
if it buys itself. The flag is bit 0x8000 of the IMAGE_TYPE item in the image
definition, set after the link by tools/rp2350/picobin.py (mark_trial), which
tools/check_rp2350_image.py runs to write firmware_ota.bin. A board never boots
a flagged image from a plain load, so the flag has to reach the OTA image and
only it, and set exactly that bit:

  * mark_trial changes one byte, the flags' high byte of IMAGE_TYPE, and nothing
    else; the image keeps its length;
  * it refuses an image already flagged, one with no block, one whose block
    loop holds a second image definition, and one with a hashed or signed block;
  * check_rp2350_image.py marks the copy, never firmware.bin, and still refuses
    a flagged firmware.bin;
  * clear_trial gives back what mark_trial took, trailer and all: the bytes the
    boot ROM leaves in flash when an update buys itself, which the release's
    factory .uf2 is made of (step 7, tools/release_manifest.py). It refuses an
    image that is not flagged, and what mark_trial refuses;
  * first_block, moved from check_rp2350_image.py to picobin.py, still reads
    what the boot ROM reads.

Run: python3 tools/test_rp2350_trial.py
Exit status is 0 on pass, 1 on failure.
"""

import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools" / "rp2350"))
import picobin  # noqa: E402  (tools/rp2350/picobin.py)

CHECK = REPO / "tools" / "check_rp2350_image.py"
EXE_RP2350 = 0x1021            # EXE | security S | Arm | RP2350
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
    return cond


def block(items, link):
    """The words of one block: start marker, items, LAST, link, end marker."""
    words = [picobin.MARK_START]
    n = 0
    for typ, payload in items:
        size = 1 + len(payload)
        words.append(typ | (size << 8))
        words.extend(payload)
        n += size
    words += [0xFF | (n << 8), link & 0xFFFFFFFF, picobin.MARK_END]
    return struct.pack(f"<{len(words)}I", *words)


def image(first_items=None, second_items=None, size=0x8000):
    """An image laid out as pico2_w_release links it: the image definition at
    0x124, and a second block of the loop near the end of the program."""
    img = bytearray((i * 31 + 7) & 0xFF for i in range(size))
    first = 0x124
    items = first_items if first_items is not None else [(picobin.ITEM_IMAGE_TYPE, [])]
    if second_items is None:
        img[first:first + 20] = block(items, 0)   # a loop of one block
    else:
        second = 0x7000
        b1 = block(items, second - first)
        b2 = block(second_items, first - second)
        img[first:first + len(b1)] = b1
        img[second:second + len(b2)] = b2
    return bytes(img)


def with_type(img, flags):
    """img with the IMAGE_TYPE word of the block at 0x124 set to flags."""
    out = bytearray(img)
    struct.pack_into("<I", out, 0x128, picobin.ITEM_IMAGE_TYPE | (1 << 8) | (flags << 16))
    return bytes(out)


def refused(img, why, fn=picobin.mark_trial):
    try:
        fn(img)
    except ValueError as e:
        return check(why in str(e), f"{fn.__name__} refused, but said '{e}', want '{why}'")
    return check(False, f"{fn.__name__} accepted an image it must refuse ({why})")


def test_one_bit_changes():
    for img in (with_type(image(), EXE_RP2350),
                with_type(image(second_items=[(0x7E, [0])]), EXE_RP2350)):
        out = picobin.mark_trial(img)
        check(len(out) == len(img), "mark_trial changed the image's length")
        diff = [i for i in range(len(img)) if img[i] != out[i]]
        check(diff == [0x12B], f"mark_trial changed the bytes at {[hex(d) for d in diff]}, want 0x12b alone")
        check(out[0x12B] == img[0x12B] | 0x80, "mark_trial did not set bit 15 of the flags")
        off, items = picobin.first_block(out)
        check(off == 0x124 and items[picobin.ITEM_IMAGE_TYPE] >> 16 == EXE_RP2350 | picobin.TBYB,
              "the marked image's first block does not read as a flagged RP2350 executable")


def test_refusals():
    img = with_type(image(), EXE_RP2350)
    refused(picobin.mark_trial(img), "already flagged")
    refused(bytes(0x8000), "no complete PICOBIN block")
    refused(with_type(image(second_items=[(picobin.ITEM_IMAGE_TYPE, [])]), EXE_RP2350),
            "image definitions of the loop")
    for typ, name in ((0x47, "HASH_DEF"), (0x09, "SIGNATURE"), (0x4B, "HASH_VALUE")):
        refused(with_type(image(first_items=[(picobin.ITEM_IMAGE_TYPE, []), (typ, [0, 0])]), EXE_RP2350), name)
        refused(with_type(image(second_items=[(typ, [0])]), EXE_RP2350), name)
    broken = bytearray(with_type(image(second_items=[(0x7E, [0])]), EXE_RP2350))
    broken[0x7000:0x7004] = b"\0\0\0\0"   # the link leads nowhere
    refused(bytes(broken), "no block starts")


def test_clear_is_the_inverse():
    trailer = bytes(range(241))   # the signature's length; nothing in it is read
    for img in (with_type(image(), EXE_RP2350),
                with_type(image(second_items=[(0x7E, [0])]), EXE_RP2350)):
        marked = picobin.mark_trial(img)
        check(picobin.clear_trial(marked) == img, "clear_trial(mark_trial(x)) is not x")
        check(picobin.clear_trial(marked + trailer) == img + trailer,
              "clear_trial on a signed image changed more than the flag")


def test_clear_refusals():
    clear = picobin.clear_trial
    refused(with_type(image(), EXE_RP2350), "not flagged", clear)
    refused(bytes(0x8000), "no complete PICOBIN block", clear)
    flagged = EXE_RP2350 | picobin.TBYB
    refused(with_type(image(second_items=[(picobin.ITEM_IMAGE_TYPE, [])]), flagged),
            "image definitions of the loop", clear)
    for typ, name in ((0x47, "HASH_DEF"), (0x09, "SIGNATURE"), (0x4B, "HASH_VALUE")):
        refused(with_type(image(first_items=[(picobin.ITEM_IMAGE_TYPE, []), (typ, [0, 0])]), flagged),
                name, clear)
        refused(with_type(image(second_items=[(typ, [0])]), flagged), name, clear)


def test_first_block_reads_what_the_rom_reads():
    off, items = picobin.first_block(with_type(image(), EXE_RP2350))
    check(off == 0x124 and items == {picobin.ITEM_IMAGE_TYPE: picobin.ITEM_IMAGE_TYPE | (1 << 8) | (EXE_RP2350 << 16)},
          f"first_block read {off}, {items}")
    check(picobin.first_block(bytes(0x2000)) == (None, None), "first_block found a block in zeros")
    cut = bytearray(with_type(image(), EXE_RP2350))
    cut[0x124 + 16:0x124 + 20] = b"\0\0\0\0"   # no end marker
    check(picobin.first_block(bytes(cut)) == (0x124, None), "first_block took a block without its end marker")
    late = bytearray(0x2000)
    late[0x1000:0x1014] = with_type(image(), EXE_RP2350)[0x124:0x138]
    check(picobin.first_block(bytes(late)) == (None, None), "first_block looked past the first 4 KB")


def test_the_build_marks_the_copy():
    text = CHECK.read_text(encoding="utf-8")
    check("from picobin import" in text or "import picobin" in text,
          "check_rp2350_image.py does not take first_block and mark_trial from tools/rp2350/picobin.py")
    check("def first_block" not in text, "check_rp2350_image.py still has its own first_block")
    check("mark_trial(" in text, "check_rp2350_image.py does not mark the OTA image")
    check("firmware_ota.bin" in text, "check_rp2350_image.py does not write firmware_ota.bin")
    check("flags & TBYB" in text, "check_rp2350_image.py no longer refuses a flagged firmware.bin")
    check("uf2.factory(" in text,
          "check_rp2350_image.py does not write firmware_factory.uf2 with the release's writer "
          "(tools/rp2350/uf2.py)")


def main():
    for t in (test_one_bit_changes, test_refusals, test_clear_is_the_inverse, test_clear_refusals,
              test_first_block_reads_what_the_rom_reads, test_the_build_marks_the_copy):
        before = len(fails)
        try:
            t()
        except Exception as e:  # a crash is a failure with its reason
            fails.append(f"{t.__name__}: {type(e).__name__}: {e}")
        print(f"  {'ok  ' if len(fails) == before else 'FAIL'} {t.__name__}")
    for f in fails:
        print(f"  FAIL {f}")
    print("PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
