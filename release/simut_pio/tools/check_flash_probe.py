"""
PlatformIO post-build guard — FlashIrqProbe wrappers must live in SRAM.

`ota_applier_run` erases the whole application slot while executing from SRAM.
Its flash_range_erase/program calls are redirected to our `--wrap` shims, so a
shim placed in the app slot would be fetched from an erased sector mid-update
and brick the device. `__not_in_flash_func` is what keeps them in RAM; this
check is what stops a refactor from silently dropping it.

RP2040 map: XIP flash 0x10000000+, SRAM 0x20000000+.

Project: SIMUT
License: MIT
"""

import subprocess
import sys

Import("env")

SRAM_BASE = 0x20000000
SRAM_END = 0x20042000
REQUIRED = ('__wrap_flash_range_erase', '__wrap_flash_range_program')


def _nm_path():
    cc = env.subst("$CC")
    for a, b in (("gcc", "nm"), ("g++", "nm")):
        if cc.endswith(a):
            return cc[: -len(a)] + b
    return "arm-none-eabi-nm"


def check_flash_probe(source, target, env):
    elf = str(target[0])
    try:
        out = subprocess.check_output([_nm_path(), elf], text=True)
    except Exception as exc:
        print(f"[flash-probe] WARNING: could not run nm ({exc}) — check skipped")
        return

    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in REQUIRED:
            found[parts[2]] = int(parts[0], 16)

    missing = [s for s in REQUIRED if s not in found]
    if missing:
        print(f"[flash-probe] FATAL: wrapper symbols absent from ELF: {missing}")
        print("[flash-probe] FlashIrqProbe.cpp must be compiled into the image.")
        sys.exit(1)

    bad = {s: a for s, a in found.items() if not (SRAM_BASE <= a < SRAM_END)}
    if bad:
        print("[flash-probe] FATAL: wrapper(s) linked outside SRAM:")
        for s, a in bad.items():
            print(f"[flash-probe]   {s} @ 0x{a:08x}")
        print("[flash-probe] A flash-resident wrapper bricks the device during")
        print("[flash-probe] OTA apply (app slot is erased while it runs).")
        print("[flash-probe] Restore __not_in_flash_func on both wrappers.")
        sys.exit(1)

    for s, a in sorted(found.items()):
        print(f"[flash-probe] OK {s} @ 0x{a:08x} (SRAM)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", check_flash_probe)
