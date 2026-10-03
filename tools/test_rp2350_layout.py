#!/usr/bin/env python3
"""The RP2350's flash layout, held together in the places that state it.

docs/analysis/OTA_AB_RP2350.md (step 3) splits the Pico 2 W's 4 MB into an
8 KB partition table, slots A and B of 1,532 KB, LittleFS of 1,020 KB and the
4 KB EEPROM sector arduino-pico reserves at the top. Four files say it, each
to a different reader:

  * tools/rp2350/partition_table.json, to picotool, which writes the table
    the boot ROM reads;
  * src/ota/ota_layout.h, to the firmware (its RP2350 branch);
  * tools/rp2350/memmap_slot.ld, to the linker: the program is one slot long,
    and LittleFS is where the table puts it;
  * tools/generated/profiles.ini, to PlatformIO: that linker script, and the
    filesystem size the builder turns into LittleFS's start for buildfs and
    uploadfs.

A number changed in one and not the others is a board that boots one layout
and reads another. This test holds the four to each other. What only a build
can show (the image's first block, the stub gone, the framework's template)
is tools/check_rp2350_image.py's, after every pico2_w_release link.

Run: python3 tools/test_rp2350_layout.py
Exit status is 0 on pass, 1 on failure.
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PT = REPO / "tools" / "rp2350" / "partition_table.json"
HDR = REPO / "src" / "ota" / "ota_layout.h"
LD = REPO / "tools" / "rp2350" / "memmap_slot.ld"
PROFILES = REPO / "tools" / "generated" / "profiles.ini"
PATCH = REPO / "tools" / "arduino_pico_overrides" / "patches" / "littlefs_rp2350_untranslated.patch"

XIP = 0x10000000
FLASH = 4 * 1024 * 1024
SECTOR = 4096
EEPROM = 4096          # arduino-pico's, at the top of the flash

# What the maintainer decided on 2026-10-03, in bytes from the start of the flash.
EXPECT = {
    "pt": (0x000000, 0x002000),
    "A": (0x002000, 0x17F000),
    "B": (0x181000, 0x17F000),
    "LittleFS": (0x300000, 0x0FF000),
    "EEPROM": (0x3FF000, 0x001000),
}

fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
    return cond


def kb(v):
    """A partition_table.json size or start: an integer, or a string like "1532K"."""
    if isinstance(v, int):
        return v
    m = re.fullmatch(r"(\d+)[kK]", str(v))
    return int(m.group(1)) * 1024 if m else None


def partitions():
    t = json.loads(PT.read_text(encoding="utf-8"))
    out = {}
    for p in t.get("partitions", []):
        name = p.get("name")
        start, size = kb(p.get("start")), kb(p.get("size"))
        check(start is not None, f"partition_table.json: {name} has no explicit start "
              "(picotool would place it, and this test could not say where)")
        out[name] = {"start": start, "size": size, "raw": p}
    return t, out


def test_table():
    t, parts = partitions()
    check(list(parts) == ["A", "B", "LittleFS", "EEPROM"],
          f"partition_table.json: partitions are {list(parts)}, want A, B, LittleFS, EEPROM in that order")
    for name, (start, size) in EXPECT.items():
        if name == "pt":
            continue
        p = parts.get(name)
        if not check(p is not None, f"partition_table.json: no partition {name}"):
            continue
        check((p["start"], p["size"]) == (start, size),
              f"partition_table.json: {name} is {p['start']:#x}+{p['size']:#x}, want {start:#x}+{size:#x}")
    # Contiguous from the end of the table to the end of the flash, sector-aligned.
    end = EXPECT["pt"][0] + EXPECT["pt"][1]
    for name in ("A", "B", "LittleFS", "EEPROM"):
        p = parts.get(name)
        if not p:
            continue
        check(p["start"] % SECTOR == 0 and p["size"] % SECTOR == 0,
              f"partition_table.json: {name} is not sector-aligned")
        check(p["start"] == end, f"partition_table.json: {name} starts at {p['start']:#x}, the previous region ends at {end:#x}")
        end = p["start"] + p["size"]
    check(end == FLASH, f"partition_table.json: the partitions end at {end:#x}, the flash at {FLASH:#x}")
    # A and B are a pair: B links to A, so the boot ROM chooses between them.
    b = parts.get("B", {}).get("raw", {})
    check(b.get("link") == ["a", 0], f"partition_table.json: B links {b.get('link')}, want [\"a\", 0] (B is A's pair)")
    for name in ("A", "B"):
        fam = parts.get(name, {}).get("raw", {}).get("families")
        check(fam == ["rp2350-arm-s"], f"partition_table.json: {name} accepts {fam}, want [\"rp2350-arm-s\"]: the images SIMUT builds")
    # LittleFS and the EEPROM sector hold no program: the boot ROM is told not to
    # look for one there.
    for name in ("LittleFS", "EEPROM"):
        raw = parts.get(name, {}).get("raw", {})
        check(raw.get("ignored_during_arm_boot") is True and raw.get("ignored_during_riscv_boot") is True,
              f"partition_table.json: {name} is not ignored during boot")
    # picotool refuses the RP2350-E10 absolute block (this board is an A2) unless
    # unpartitioned space takes the absolute family.
    un = t.get("unpartitioned", {})
    check("absolute" in un.get("families", []),
          "partition_table.json: unpartitioned space must accept \"absolute\" (picotool's RP2350-E10 fix)")


def header_macros():
    text = HDR.read_text(encoding="utf-8")
    m = re.search(r"#if defined\(PICO_RP2350\) && PICO_RP2350\s*\n/\* The RP2350's map(.*?)#endif", text, re.S)
    if not check(m is not None, "ota_layout.h: no RP2350 map block (\"/* The RP2350's map\" under #if PICO_RP2350)"):
        return {}
    vals = {}
    for name, val in re.findall(r"#define\s+(OTA_RP2350_\w+)\s+\(?\s*(0x[0-9A-Fa-f]+)u?\s*\)?", m.group(1)):
        vals[name] = int(val, 16)
    return vals


def test_header():
    v = header_macros()
    want = {
        "OTA_RP2350_PT_OFFSET": EXPECT["pt"][0], "OTA_RP2350_PT_SIZE": EXPECT["pt"][1],
        "OTA_RP2350_SLOT_A_OFFSET": EXPECT["A"][0], "OTA_RP2350_SLOT_B_OFFSET": EXPECT["B"][0],
        "OTA_RP2350_SLOT_SIZE": EXPECT["A"][1],
        "OTA_RP2350_FS_OFFSET": EXPECT["LittleFS"][0], "OTA_RP2350_FS_SIZE": EXPECT["LittleFS"][1],
        "OTA_RP2350_EEPROM_OFFSET": EXPECT["EEPROM"][0],
    }
    for k, w in want.items():
        check(v.get(k) == w, f"ota_layout.h: {k} is {v.get(k)!r}, want {w:#x}")


def test_ldscript():
    if not check(LD.exists(), f"{LD.relative_to(REPO)} is missing"):
        return
    text = LD.read_text(encoding="utf-8")
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    m = re.search(r"FLASH\(rx\)\s*:\s*ORIGIN\s*=\s*0x10000000,\s*LENGTH\s*=\s*(\d+)", code)
    check(m and int(m.group(1)) == EXPECT["A"][1],
          f"memmap_slot.ld: FLASH LENGTH is {m.group(1) if m else None}, want the slot, {EXPECT['A'][1]}")
    for sym, want in (("_FS_start", XIP + EXPECT["LittleFS"][0]),
                      ("_FS_end", XIP + EXPECT["LittleFS"][0] + EXPECT["LittleFS"][1]),
                      ("_EEPROM_start", XIP + EXPECT["EEPROM"][0])):
        m = re.search(r"PROVIDE\s*\(\s*%s\s*=\s*(\d+)\s*\)" % sym, code)
        check(m and int(m.group(1)) == want, f"memmap_slot.ld: {sym} is {m.group(1) if m else None}, want {want} ({want:#x})")
    # The stub and the table it reads are what this script exists to drop. The
    # builder still links ota.o, so its .OTA section has to be discarded by name:
    # left unplaced, it is an orphan the linker puts where it likes.
    discard = re.search(r"/DISCARD/\s*:\s*\{\s*\*\(\.OTA\)\s*\}", code)
    check(discard is not None, "memmap_slot.ld: no /DISCARD/ for .OTA (the builder links ota.o anyway)")
    rest = code.replace(discard.group(0), "") if discard else code
    check(not re.search(r"\.ota\s*:|\.OTA|ota\.o", rest), "memmap_slot.ld: still places arduino-pico's OTA stub (.ota / .OTA / ota.o)")
    check(not re.search(r"\.partition\s*:", code), "memmap_slot.ld: still has the stub's .partition block")
    check("__FLASH_LENGTH__" not in code and "__FS_START__" not in code,
          "memmap_slot.ld: has template placeholders; PlatformIO does not fill a custom script")


def test_profile():
    text = PROFILES.read_text(encoding="utf-8")
    m = re.search(r"\[env:pico2_w_release\]\n(.*?)(?=\n\[|\Z)", text, re.S)
    if not check(m is not None, "profiles.ini: no [env:pico2_w_release]"):
        return
    env = m.group(1)
    ld = re.search(r"^board_build\.ldscript\s*=\s*(\S+)", env, re.M)
    fs = re.search(r"^board_build\.filesystem_size\s*=\s*(\S+)", env, re.M)
    check(ld and ld.group(1) == "tools/rp2350/memmap_slot.ld",
          f"profiles.ini: pico2_w_release's ldscript is {ld.group(1) if ld else None}, want tools/rp2350/memmap_slot.ld")
    if check(fs is not None, "profiles.ini: pico2_w_release sets no board_build.filesystem_size"):
        size = kb(fs.group(1))
        # The builder's own formula (platform-raspberrypi builder/main.py, fetch_fs_size):
        # FS_START = 0x10000000 + flash - 4096 - filesystem_size.
        check(size is not None and XIP + FLASH - EEPROM - size == XIP + EXPECT["LittleFS"][0],
              f"profiles.ini: filesystem_size {fs.group(1)} puts the builder's FS_START at "
              f"{(XIP + FLASH - EEPROM - (size or 0)):#x}, the table at {XIP + EXPECT['LittleFS'][0]:#x}")
    # Every other image stays on the framework's own script.
    for other in re.finditer(r"\[env:(\w+)\]\n(.*?)(?=\n\[|\Z)", text, re.S):
        if other.group(1) != "pico2_w_release":
            check("board_build.ldscript" not in other.group(2),
                  f"profiles.ini: {other.group(1)} sets board_build.ldscript; only the RP2350 boots from a slot")


def test_patch():
    if not check(PATCH.exists(), f"{PATCH.relative_to(REPO)} is missing"):
        return
    text = PATCH.read_text(encoding="utf-8")
    added = "\n".join(l[1:] for l in text.splitlines() if l.startswith("+") and not l.startswith("+++"))
    check("libraries/LittleFS/src/LittleFS.cpp" in text, "the LittleFS patch does not touch libraries/LittleFS/src/LittleFS.cpp")
    check("SIMUT override" in added, "the LittleFS patch carries no 'SIMUT override' marker for patch.sh to find")
    check(re.search(r"#if\s+defined\(PICO_RP2350\)", added) is not None,
          "the LittleFS patch is not limited to the RP2350 (#if defined(PICO_RP2350)): the RP2040 images must not change")
    check("XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE" in added,
          "the LittleFS patch does not read through XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE")
    sh = (REPO / "tools" / "arduino_pico_overrides" / "patch.sh").read_text(encoding="utf-8")
    check("littlefs_rp2350_untranslated.patch" in sh, "patch.sh does not apply littlefs_rp2350_untranslated.patch")


def main():
    for t in (test_table, test_header, test_ldscript, test_profile, test_patch):
        before = len(fails)
        try:
            t()
        except Exception as e:  # a missing file is a failure, not a crash
            fails.append(f"{t.__name__}: {type(e).__name__}: {e}")
        print(f"  {'ok  ' if len(fails) == before else 'FAIL'} {t.__name__}")
    for f in fails:
        print(f"  FAIL {f}")
    print("PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
