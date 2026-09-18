#!/usr/bin/env python3
"""
flash_compose.py — where the flash goes, from a linker map.

    PLATFORMIO_BUILD_FLAGS="-Wl,-Map=/tmp/release.map" pio run -e pico_w_release
    python3 tools/flash_compose.py /tmp/release.map .pio/build/pico_w_release/firmware.bin

Attributes every input section that lands in flash (VMA in the XIP window,
plus .data, whose load address is there) to the archive or object that
contributed it, and prints the table docs/analysis/DIETA_FLASH.md is built on.

Two things this deliberately does NOT trust in the map:

  * The per-object size of a mergeable string section (.rodata.str1.1 and the
    per-function .rodata.<fn>.str1.1 that -fdata-sections emits). The linker
    lists each contribution at its UNMERGED size and, on this toolchain, hands
    the whole merged pool to the first object that contributed — which is how
    AppManager_Boot.cpp.o once appeared to own 39 kB of strings. The pool is
    computed here as `.rodata output size - non-mergeable inputs - fill` and
    reported once, not per object.
  * The 'used' number PlatformIO prints, which is a sum of sections and sits
    ~12 kB under the .bin (the .data load address is aligned to 4 KiB). The
    .bin is what the slot holds and what the OTA ceiling is compared against.

Setting PLATFORMIO_BUILD_FLAGS changes the project checksum, so the build
directory is wiped and every environment is rebuilt from scratch — expect a
full build, and expect the other environments' artifacts to be gone after it.
"""
import collections
import os
import re
import sys

XIP_LO, XIP_HI = 0x10000000, 0x10200000

FULL = re.compile(r'^ (\.[\w.$]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
NAME = re.compile(r'^ (\.[\w.$]+)\s*$')
CONT = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
OSEC = re.compile(r'^(\.[\w.]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
FILL = re.compile(r'^\s*\*fill\*\s+0x[0-9a-f]+\s+0x([0-9a-f]+)')


def parse(path):
    lines = open(path, errors="replace").read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("Linker script and memory map"))
    outsec, cur, recs, outs, fill = None, None, [], {}, collections.Counter()
    for l in lines[start:]:
        m = OSEC.match(l)
        if m:
            outsec = m.group(1)
            outs[outsec] = (int(m.group(2), 16), int(m.group(3), 16))
            continue
        m = FILL.match(l)
        if m:
            fill[outsec] += int(m.group(1), 16)
            continue
        m = FULL.match(l)
        if m:
            sec, addr, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        else:
            m = NAME.match(l)
            if m:
                cur = m.group(1)
                continue
            m = CONT.match(l)
            if not (m and cur):
                continue
            sec, addr, size, obj = cur, int(m.group(1), 16), int(m.group(2), 16), m.group(3)
            cur = None
        if size and ((XIP_LO <= addr < XIP_HI) or outsec == ".data"):
            recs.append((outsec, sec, addr, size, obj))
    return recs, outs, fill


def origin(obj):
    if "(" in obj:
        return obj.split("(")[0].split("/")[-1]
    if "/src/" in obj:
        return "src"
    return obj.split("/")[-1]


def mergeable(sec):
    return ".str1" in sec or ".cst" in sec


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    recs, outs, fill = parse(sys.argv[1])
    binsz = os.path.getsize(sys.argv[2])
    by = collections.Counter()
    kinds = collections.Counter()
    for outsec, sec, addr, size, obj in recs:
        if mergeable(sec):
            continue  # counted once below, as the merged pool
        o = origin(obj)
        by[o] += size
        k = ("pages" if "WebUI_GZ" in sec else "fonts" if "Fonts.cpp" in obj else
             "text" if sec.startswith(".text") or outsec == ".text" else
             "rodata" if sec.startswith(".rodata") or outsec == ".rodata" else outsec)
        kinds[(o, k)] += size
    ro_out = outs.get(".rodata", (0, 0))[1]
    ro_nonmerge = sum(s for o, sec, a, s, obj in recs if o == ".rodata" and not mergeable(sec))
    pool = ro_out - ro_nonmerge - fill[".rodata"]
    by["<merged string pool>"] = pool
    total = sum(by.values())
    print(f".bin {binsz:,} B | sections attributed {total:,} B | "
          f"unattributed (fill, boot2 pad, alignment) {binsz - total:,} B")
    print(f"\n{'bytes':>9} {'%':>6}  origin")
    for o, s in by.most_common():
        detail = ", ".join(f"{k}={v:,}" for (oo, k), v in
                           sorted(kinds.items(), key=lambda kv: -kv[1]) if oo == o and v >= 256)
        print(f"{s:>9,} {100 * s / binsz:5.1f}%  {o:<30} {detail}")
    print("\noutput sections in flash:")
    for o, (a, s) in outs.items():
        if s and ((XIP_LO <= a < XIP_HI) or o == ".data"):
            print(f"  {o:<12} {s:>9,}")


if __name__ == "__main__":
    main()
