#!/usr/bin/env python3
"""Tests the flash number the savings floor compares (tools/measure_savings.py --check, --update).

Run: python3 tools/test_measure_savings.py   (CI runs it in the gates job; the floor itself runs nightly)

The BTstack flash bank (__bluetooth_tlv, 8 KB aligned to a 4 KB sector) is the last thing in .rodata of
every image with Bluetooth, and PlatformIO's `used` counts the fill the linker puts in front of it. The
floor measures past that fill, read from the linker map. Measured with it, the alpha's Bluetooth saving fell
2,912 B in three weeks while the code its switch removes grew 8 B, and the Air's tel_tls saving rose
4,096 B between two trees that removed the same symbols (2026-10-22). So:

  * bank_pad( ) reads the fill from a real map (pico_w_alpha on main 25e169df: 0x224, 548 B); gives 0
    when the bank sits right after the section before it, when the image has no bank, and when the bank is
    only under "Discarded input sections"; skips the zero-size entries a map lists; does not take a fill
    from further up; and refuses, rather than answer 0, a map it cannot read and a map that has the bank's
    object (btstack_flash_bank.cpp.o) with no bank under the bank's name, as a framework that renamed it
    would leave.
  * pio_build( ) asks for the map, reads the fill, and refuses a build that left no map, ran without
    zopfli or left a map it cannot follow: a fill nobody read is not a fill of zero.
  * measure( ) takes each side's fill off: the saving is (used - fill) of the base minus (used - fill) of
    the variant.
"""
import os
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import measure_savings as ms                        # noqa: E402

FAILS = []
RAN = []


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


HEADER = "Linker script and memory map\n\nLOAD /x/crt0.o\n.rodata         0x10086000    0x6a000\n"
# The end of .rodata of pico_w_alpha on main 25e169df, as the linker wrote it (-Wl,-Map). The three
# SPI sections share an address: merged strings, so "the end of the entry before" is not a rule.
REAL_TAIL = """\
 .rodata.btstack_link_key_db_tlv
                0x100edd64       0x24 /x/liblwip-bt.a(btstack_link_key_db_tlv.c.o)
 .rodata._ZN14SPIClassRP20405setRXEh.str1.1
                0x100edd88       0x67 .pio/build/pico_w_alpha/lib530/libSPI.a(SPI.cpp.o)
 .rodata._ZN14SPIClassRP20405beginEb.str1.1
                0x100edd88       0x23 .pio/build/pico_w_alpha/lib530/libSPI.a(SPI.cpp.o)
 .rodata._ZTV14SPIClassRP2040
                0x100edd88       0x40 .pio/build/pico_w_alpha/lib530/libSPI.a(SPI.cpp.o)
                0x100edd88                _ZTV14SPIClassRP2040
 .rodata._ZL28pico_flash_bank_instance_obj
                0x100eddc8       0x14 .pio/build/pico_w_alpha/libFrameworkArduino.a(btstack_flash_bank.cpp.o)
 *fill*         0x100edddc      0x224
 .rodata._ZL15__bluetooth_tlv
                0x100ee000     0x2000 .pio/build/pico_w_alpha/libFrameworkArduino.a(btstack_flash_bank.cpp.o)
                0x100f0000                        . = ALIGN (0x4)
 *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
                0x100f0000                        . = ALIGN (0x4)
"""
BANK = (" .rodata._ZL15__bluetooth_tlv\n"
        "                0x100ee000     0x2000 /x/libFrameworkArduino.a(btstack_flash_bank.cpp.o)\n")
DISCARDED_BANK = ("Discarded input sections\n\n"
                  " .text          0x00000000        0x0 /x/crt0.o\n"
                  " .rodata._ZL15__bluetooth_tlv\n"
                  "                0x00000000     0x2000 /x/libFrameworkArduino.a(btstack_flash_bank.cpp.o)\n\n")
SIZES_OUT = ("RAM:   [=====     ]  47.9% (used 125680 bytes from 262144 bytes)\n"
             "Flash: [========= ]  93.5% (used 976236 bytes from 1044480 bytes)\n")


def pad(text):
    """bank_pad( ), with a refusal as a value: a case that should give a number and raises fails as a case."""
    try:
        return ms.bank_pad(text)
    except ValueError as e:
        return f"refused ({e})"


def pad_raises(text):
    return str(pad(text)).startswith("refused")


def bank_pad_cases():
    real = HEADER + REAL_TAIL
    check("real map: the fill before the bank is 548 B", pad(real) == 548, pad(real))
    flush = HEADER + (" .rodata.a\n                0x100edc00      0x400 /x/a.o\n") + BANK
    check("bank right after the section before it: 0", pad(flush) == 0, pad(flush))
    check("image with no bank: 0", pad(HEADER + " .rodata.a\n                0x100edc00"
                                               "      0x400 /x/a.o\n") == 0)
    check("bank only in Discarded input sections: 0",
          pad(DISCARDED_BANK + HEADER + " .rodata.a\n                0x100edc00      0x400 /x/a.o\n") == 0)
    both = DISCARDED_BANK + real
    check("a discarded bank above the map is not the bank: 548", pad(both) == 548, pad(both))
    further_up = HEADER + (" *fill*         0x100edbf0       0x10 \n"
                           " .rodata.a\n                0x100edc00      0x400 /x/a.o\n") + BANK
    check("a fill further up is not the bank's: 0", pad(further_up) == 0, pad(further_up))
    zero = HEADER + (" .rodata.a\n                0x100edc00      0x100 /x/a.o\n"
                     " *fill*         0x100edd00      0x300 \n"
                     " .rodata.empty  0x100ee000        0x0 /x/b.o\n") + BANK
    check("zero-size entries between the fill and the bank are skipped: 768",
          pad(zero) == 0x300, pad(zero))
    short = HEADER + (" .rodata.a\n                0x100edc00      0x100 /x/a.o\n"
                      " *fill*         0x100edd00      0x200 \n") + BANK
    check("a fill that does not reach the bank is refused, not 0", pad_raises(short))
    check("a map with no memory map is refused, not 0", pad_raises(REAL_TAIL))
    renamed = HEADER + (" .text._ZL24pico_flash_bank_get_sizePv\n"
                        "                0x10012340        0x6 /x/libFrameworkArduino.a(btstack_flash_bank.cpp.o)\n"
                        " *fill*         0x100edddc      0x224 \n"
                        " .rodata._ZL15__bluetooth_bank\n"
                        "                0x100ee000     0x2000 /x/libFrameworkArduino.a(btstack_flash_bank.cpp.o)\n")
    check("the bank's object with no bank under its name is refused, not 0", pad_raises(renamed))


class FakeBuild:
    """Stands in for `pio run`: records how it was asked and writes the map it is given."""

    def __init__(self, root, stdout=SIZES_OUT, returncode=0, map_text=None):
        self.root, self.stdout, self.rc, self.map_text = root, stdout, returncode, map_text
        self.calls = []

    def __call__(self, env, linker_map=False):
        self.calls.append((env, linker_map))
        if self.map_text is not None:
            d = os.path.join(self.root, ".pio", "build", env)
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, "firmware.map"), "w") as fh:
                fh.write(self.map_text)
        return types.SimpleNamespace(returncode=self.rc, stdout=self.stdout, stderr="")


def pio_build_cases():
    saved = ms.ROOT, ms.pio_run
    try:
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = fake = FakeBuild(root, map_text=HEADER + REAL_TAIL)
            got = ms.pio_build("pico_w_alpha")
            check("pio_build asks for the linker map", fake.calls == [("pico_w_alpha", True)], fake.calls)
            check("pio_build reads used, RAM and the fill",
                  got == {"flash": 976236, "ram": 125680, "pad": 548}, got)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, map_text=None)
            check("a build that left no map is refused", ms.pio_build("pico_w_alpha") is None)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, map_text=HEADER + (" .rodata.a\n                0x100edc00      0x100 /x/a.o\n"
                                                            " *fill*         0x100edd00      0x200 \n") + BANK)
            check("a build whose map this reader cannot follow is refused", ms.pio_build("pico_w_alpha") is None)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, stdout=ms.NO_ZOPFLI + "\n" + SIZES_OUT, map_text=HEADER + REAL_TAIL)
            check("a build without zopfli is refused", ms.pio_build("pico_w_alpha") is None)
        with tempfile.TemporaryDirectory() as root:
            ms.ROOT = root
            ms.pio_run = FakeBuild(root, returncode=1, map_text=HEADER + REAL_TAIL)
            check("a failed build is refused", ms.pio_build("pico_w_alpha") is None)
    finally:
        ms.ROOT, ms.pio_run = saved


def pio_run_cases():
    seen = {}

    def fake_run(cmd, **kw):
        seen.update(kw)
        return types.SimpleNamespace(returncode=0, stdout="", stderr="")

    saved_run, saved_flags = ms.subprocess.run, os.environ.get("PLATFORMIO_BUILD_FLAGS")
    try:
        ms.subprocess.run = fake_run
        os.environ["PLATFORMIO_BUILD_FLAGS"] = "-DX=1"
        ms.pio_run("pico_w_alpha", linker_map=True)
        flags = (seen.get("env") or {}).get("PLATFORMIO_BUILD_FLAGS", "")
        check("the map flag reaches the build, after the flags already set",
              flags == "-DX=1 " + ms.MAP_FLAG, flags)
        seen.clear()
        ms.pio_run("pico_w_alpha")
        check("without linker_map the build inherits the environment untouched", seen.get("env") is None)
    finally:
        ms.subprocess.run = saved_run
        if saved_flags is None:
            os.environ.pop("PLATFORMIO_BUILD_FLAGS", None)
        else:
            os.environ["PLATFORMIO_BUILD_FLAGS"] = saved_flags


def measure_cases():
    """measure( ) with the builds faked, and the temporary environments written to a scratch file."""
    sizes = {"pico_w_alpha": {"flash": 976236, "ram": 125680, "pad": 548},
             "_sav_bluetooth_off": {"flash": 838880, "ram": 106332, "pad": 0}}
    saved = ms.PROFILES_INI, ms.pio_build
    with tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False) as fh:
        fh.write("; scratch\n")
    try:
        ms.PROFILES_INI = fh.name
        ms.pio_build = lambda env: dict(sizes[env])
        r = ms.measure(["bluetooth"])["bluetooth"]
        check("the saving takes each side's fill off: (976236-548) - (838880-0)",
              r["flash_saved"] == 976236 - 548 - 838880, r)
        check("the raw numbers and both fills are kept for the table",
              (r["flash_base"], r["flash_off"], r["pad_base"], r["pad_off"]) == (976236, 838880, 548, 0), r)
        with open(fh.name) as f2:
            check("the temporary environments are taken out again", f2.read() == "; scratch\n")
    finally:
        ms.PROFILES_INI, ms.pio_build = saved
        os.unlink(fh.name)


def flips_cases():
    """The cost matrix's flips for the look of the touch display (2026-10-29): `ui` is a choice, like the network, so a product's flips
    have one `ui=<id>` per [ui.<id>] of the manifest that is not its own, built where the rules allow it and recorded with the rule
    where they do not. Read from the real manifest: these are the four published products the configurator prices."""
    gf = ms.gf
    M = gf.load_manifest()
    check("a choice's key carries its value: ui=angulo", ms.flip_key("ui", "angulo") == "ui=angulo", ms.flip_key("ui", "angulo"))
    check("the network's key is unchanged: net=enc28j60", ms.flip_key("net", "enc28j60") == "net=enc28j60")
    check("a switch's key is its name", ms.flip_key("buzzer", True) == "buzzer")
    check("the build env of a ui flip has no '=' (PlatformIO refuses it)", "=" not in ms.cost_env("pico2_w_release", "ui=angulo"))
    p2 = gf.resolve_profile("pico2_w_release", M["profiles"])
    flips = dict(ms.flips_of(p2, M))
    check("pico2_w_release flips to the Ângulo", "ui=angulo" in flips, sorted(k for k in flips if "=" in k))
    check("the flipped profile asks for the Ângulo and keeps the rest",
          "ui=angulo" in flips and flips["ui=angulo"].get("ui") == "angulo"
          and {k: v for k, v in flips["ui=angulo"].items() if k != "ui"} == {k: v for k, v in p2.items() if k != "ui"})
    check("no flip to the product's own look", "ui=" + p2.get("ui", gf.DEFAULT_UI) not in flips)
    only = ms.flips_of(p2, M, ["ui"])
    check("--only ui gives the ui flips alone", [k for k, _ in only] == ["ui=angulo"], [k for k, _ in only])
    plans = {}
    for prod in ("pico_w_release", "pico_w_alpha", "pico_w_air", "pico2_w_release"):
        prof = gf.resolve_profile(prod, M["profiles"])
        blocks, plan = [], []
        for key, flipped in ms.flips_of(prof, M, ["ui"]):
            ms.plan_flip(blocks, plan, prod, key, flipped, M)
        plans[prod] = (blocks, plan)
    b, pl = plans["pico2_w_release"]
    check("the Ângulo on the Pico 2 W is built: one env, no rule", len(b) == 1 and pl[0][2] and pl[0][3] is None, pl)
    for prod, rule in (("pico_w_release", "angulo-cyw43-needs-rp2350"), ("pico_w_alpha", "angulo-needs-tft"),
                       ("pico_w_air", "angulo-needs-tft")):
        b, pl = plans[prod]
        check(f"the Ângulo on {prod} is not built and names its rule ({rule})",
              not b and len(pl) == 1 and pl[0][2] is None and pl[0][3] == rule, pl)


bank_pad_cases()
pio_build_cases()
pio_run_cases()
measure_cases()
flips_cases()
print(f"\n{len(RAN) - len(FAILS)}/{len(RAN)} ok")
if FAILS:
    sys.exit(1)
