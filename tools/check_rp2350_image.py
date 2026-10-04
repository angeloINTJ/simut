"""
PlatformIO post-build guard and packager for the RP2350 image of a slot.

pico2_w_release boots from slot A of a partition table (docs/analysis/
OTA_AB_RP2350.md, step 3). Three things in it can be wrong while the link
still succeeds, and each one is a board that does not start:

  * the boot ROM takes the first image definition it finds in the slot's first
    4 KB. arduino-pico's OTA stub puts its own there, and that stub mounts
    LittleFS through XIP outside the slot's window;
  * the framework reads LittleFS through the translated window, which a slot
    does not reach, unless tools/arduino_pico_overrides/patch.sh applied the
    LittleFS patch (2k) to this framework;
  * tools/rp2350/memmap_slot.ld is derived from the framework's template, and
    a framework update changes the template under it.

After the link this checks each of those, the symbols the layout rests on, and
the variant tag an update is checked against (step 4: its env ends in "two",
simut_config.h). Then it writes, next to firmware.uf2:

  * partition_table.uf2: the table alone, for `picotool load`;
  * firmware_factory.uf2: the table and the program in slot A, every block in
    the absolute family, for a board whose flash holds nothing.

Every other image is left alone. Run with `pio run -e pico2_w_release`.

Project: SIMUT
License: MIT
"""

import os
import re
import struct
import subprocess
import sys

Import("env")

ROOT = env.subst("$PROJECT_DIR")
sys.path.insert(0, os.path.join(ROOT, "tools", "rp2350"))
import gen_memmap  # noqa: E402  (tools/rp2350/gen_memmap.py)

LDSCRIPT = "tools/rp2350/memmap_slot.ld"
XIP = 0x10000000
MARK_START, MARK_END = 0xFFFFDED3, 0xAB123579
ITEM_IMAGE_TYPE, ITEM_LAST = 0x42, 0x7F
# EXE | security S | CPU Arm | chip RP2350, and no try-before-you-buy bit: an
# image flagged for trial never starts from a plain load (picobin.h).
IMAGE_TYPE_WANT = 0x1 | (0x2 << 4) | (0x0 << 8) | (0x1 << 12)
TBYB = 0x8000
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
# BuildIdentity.cpp; the env is [a-z], and the RP2350's ends in the chip's suffix.
ENV_TAG = re.compile(rb"SIMUT-ENV:([a-z]+);")
ENV_CHIP = "two"


def is_slot_image():
    return env.GetProjectOption("board_build.ldscript", "") == LDSCRIPT


def fail(msg):
    print(f"[rp2350-image] FATAL: {msg}")
    sys.exit(1)


def tool(name):
    cc = env.subst("$CC")
    return cc[: -len("gcc")] + name if cc.endswith("gcc") else "arm-none-eabi-" + name


def picotool():
    p = env.PioPlatform().get_package_dir("tool-picotool-rp2040-earlephilhower")
    return os.path.join(p, "picotool") if p else "picotool"


def symbols(elf):
    out = subprocess.check_output([tool("nm"), elf], text=True)
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def first_block(image):
    """(offset, items) of the first PICOBIN block in the first 4 KB, the way
    the boot ROM looks for it; items maps an item type to its first word."""
    for off in range(0, min(len(image), 4096) - 8, 4):
        if struct.unpack_from("<I", image, off)[0] != MARK_START:
            continue
        items, i = {}, off + 4
        while i + 4 <= len(image):
            word = struct.unpack_from("<I", image, i)[0]
            typ = word & 0x7F
            size = (word >> 8) & (0xFFFF if word & 0x80 else 0xFF)
            if typ == ITEM_LAST:
                # LAST carries the block's length; the end marker follows its
                # second word, the link to the next block.
                end = i + 8
                if end + 4 <= len(image) and struct.unpack_from("<I", image, end)[0] == MARK_END:
                    return off, items
                break
            if size == 0:
                break
            items.setdefault(typ, word)
            i += size * 4
        return off, None
    return None, None


def uf2_blocks(path):
    data = open(path, "rb").read()
    if len(data) % 512:
        fail(f"{path} is not a whole number of UF2 blocks")
    return [bytearray(data[i:i + 512]) for i in range(0, len(data), 512)]


def merge_uf2(paths, out):
    """One UF2 file from several: the blocks in order, numbered 0..n-1 of n."""
    blocks = [b for p in paths for b in uf2_blocks(p)]
    for n, b in enumerate(blocks):
        m0, m1 = struct.unpack_from("<2I", b, 0)
        if (m0, m1) != UF2_MAGIC[:2] or struct.unpack_from("<I", b, 508)[0] != UF2_MAGIC[2]:
            fail(f"{out}: block {n} is not a UF2 block")
        struct.pack_into("<2I", b, 20, n, len(blocks))
    with open(out, "wb") as fh:
        for b in blocks:
            fh.write(b)
    return len(blocks)


def check_and_package(source, target, env):
    build = env.subst("$BUILD_DIR")
    elf = os.path.join(build, env.subst("${PROGNAME}.elf"))
    binf = os.path.join(build, env.subst("${PROGNAME}.bin"))

    # 1. The linker script is still what the framework's template gives.
    template = os.path.join(env.PioPlatform().get_package_dir("framework-arduinopico"),
                            "lib", "rp2350", "memmap_default.ld")
    try:
        want = gen_memmap.derive(open(template, encoding="utf-8").read())
    except ValueError as e:
        fail(f"{template}: {e}")
    if open(os.path.join(ROOT, LDSCRIPT), encoding="utf-8").read() != want:
        fail(f"{LDSCRIPT} is not what {template} gives: the framework changed. Read "
             "the new template, then run python3 tools/rp2350/gen_memmap.py.")

    # 2. The framework reads LittleFS through the untranslated window.
    lfs = os.path.join(env.PioPlatform().get_package_dir("framework-arduinopico"),
                       "libraries", "LittleFS", "src", "LittleFS.cpp")
    if "SIMUT override — on the RP2350, read through the untranslated window" not in \
            open(lfs, encoding="utf-8").read():
        fail(f"{lfs} has no SIMUT override 2k: this image would fault mounting LittleFS "
             "from a slot. Run bash tools/arduino_pico_overrides/patch.sh, then build again.")

    # 3. The symbols the layout rests on.
    t = gen_memmap.layout()
    slot = int(t["__FLASH_LENGTH__"])
    syms = symbols(elf)
    for name, val in (("_FS_start", int(t["__FS_START__"])), ("_FS_end", int(t["__FS_END__"]))):
        if syms.get(name) != val:
            fail(f"{name} is {syms.get(name)!r}, the partition table says {val:#x}")
    if "_binary_ota_bin_start" in syms:
        fail("arduino-pico's OTA stub (ota.o) is still in the image")

    # 4. The image fits its slot, and the first block the boot ROM finds is the
    # image's own definition. (__flash_binary_end is a PROVIDE nothing refers
    # to, so the .bin's length is the end of the program.)
    image = open(binf, "rb").read()
    end = len(image)
    if not 0 < end <= slot:
        fail(f"firmware.bin is {end} B, and a slot holds {slot} B")
    off, items = first_block(image)
    if off is None or items is None:
        fail("no complete PICOBIN block in the first 4 KB of firmware.bin")
    itype = items.get(ITEM_IMAGE_TYPE)
    if itype is None:
        fail(f"the first block (at {off:#x}) has no IMAGE_TYPE item: it is not an image definition")
    flags = itype >> 16
    if flags & TBYB:
        fail("the image is flagged try-before-you-buy; from a plain load it would never start")
    if flags != IMAGE_TYPE_WANT:
        fail(f"the image type is {flags:#06x}, want {IMAGE_TYPE_WANT:#06x} (Arm, secure, RP2350, executable)")

    # 4b. The variant tag. The stage on this board checks a staged image's tag
    # against its own, and the signature binds it: with the chip's suffix, a
    # Pico W's release is another model here. The validator is what keeps the
    # tag in the link; an image without one could not install a signed update.
    envs = sorted({m.group(1).decode() for m in ENV_TAG.finditer(image)})
    if len(envs) != 1 or not envs[0].endswith(ENV_CHIP):
        fail(f"firmware.bin carries the env tags {envs}, want one ending in '{ENV_CHIP}' "
             "(SIMUT_ENV_NAME, src/simut_config.h)")

    # 5. The table, and the image a blank board takes from BOOTSEL.
    pt_json = os.path.join(ROOT, "tools", "rp2350", "partition_table.json")
    pt_uf2 = os.path.join(build, "partition_table.uf2")
    app_uf2 = os.path.join(build, "firmware_slot_a.uf2")
    factory = os.path.join(build, "firmware_factory.uf2")
    slot_a = XIP + gen_memmap.partitions()["A"][0]
    pt = picotool()
    for cmd in ([pt, "partition", "create", pt_json, pt_uf2, "--abs-block"],
                [pt, "uf2", "convert", binf, app_uf2, "--offset", hex(slot_a), "--family", "absolute"]):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            fail(f"{' '.join(cmd)}: {r.stdout}{r.stderr}")
    n = merge_uf2([pt_uf2, app_uf2], factory)
    os.remove(app_uf2)
    print(f"[rp2350-image] OK: image definition first (at {off:#x}, type {flags:#06x}), no OTA stub, "
          f"env {envs[0]}, {end} of {slot} B of slot, LittleFS at {syms['_FS_start']:#x}; "
          f"wrote partition_table.uf2 and firmware_factory.uf2 ({n} blocks)")


if is_slot_image():
    # The table goes into firmware_factory.uf2, so a changed table has to
    # remake the .bin and run this again; nothing else in the build reads it.
    env.Depends("$BUILD_DIR/${PROGNAME}.bin", os.path.join(ROOT, "tools", "rp2350", "partition_table.json"))
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_and_package)
