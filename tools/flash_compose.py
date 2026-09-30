#!/usr/bin/env python3
"""
flash_compose.py — where the flash goes, from a linker map.

    python3 tools/flash_compose.py --env pico_w_release             # the table
    python3 tools/flash_compose.py --env pico_w_air --objects        # per object, + RAM
    python3 tools/flash_compose.py <firmware.map> <firmware.bin> [--objects]

Attributes every input section that lands in flash (VMA in the XIP window,
plus .data, whose load address is there) to the archive or object that
contributed it, and prints the table docs/analysis/DIETA_FLASH.md is built on.
--objects breaks `src` and every archive down to the object file, and adds the
static RAM each one holds (.data + .bss) — the view the resource tracking in
docs/analysis/ECONOMIA_DE_RECURSOS.md is built on. Heap is not in any map: the
managers are unique_ptr members of AppManager, so their size is sizeof( ), not
a section (see that document for how it was measured).

--env <name> gets the map without rebuilding anything: it asks PlatformIO for
the link command of an environment that is already built, links the same
objects again with -Map into a scratch directory, and refuses to go on unless
the .bin that link produces is byte-identical to the one in .pio/build — the
map must be of the image the build made. (The toolchain on PATH may not be
PlatformIO's; the relink runs with PlatformIO's toolchain first on PATH, or it
links a different libstdc++ and the bin grows 4 kB.)

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

The older recipe — PLATFORMIO_BUILD_FLAGS="-Wl,-Map=/tmp/x.map" pio run -e ENV —
still works, but that variable changes the project checksum, so the build
directory is wiped and every environment is rebuilt from scratch.
"""
import collections
import glob
import os
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

XIP_LO, XIP_HI = 0x10000000, 0x10200000
# Static RAM: what PlatformIO's "RAM used" adds up. .data is also in flash (its
# load image); .bss and the uninitialised sections are RAM only.
RAM_SECTIONS = (".data", ".bss", ".noinit", ".uninitialized_data")

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
        if size and ((XIP_LO <= addr < XIP_HI) or outsec in RAM_SECTIONS):
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


def object_of(obj):
    """The object file inside its archive, or the path under src/."""
    if "(" in obj:
        arc = obj.split("(")[0].split("/")[-1]
        return f"{arc}:{obj.split('(', 1)[1].rstrip(')')}"
    if "/src/" in obj:
        return "src/" + obj.split("/src/", 1)[1].rsplit(".o", 1)[0]
    return obj.split("/")[-1]


def print_objects(recs, pool, top):
    """Per object file: flash (as the table counts it) and static RAM."""
    flash, ram, pages = collections.Counter(), collections.Counter(), collections.Counter()
    for outsec, sec, addr, size, obj in recs:
        o = object_of(obj)
        if outsec in RAM_SECTIONS:
            ram[o] += size
        if ((XIP_LO <= addr < XIP_HI) or outsec == ".data") and not mergeable(sec):
            flash[o] += size
            if "WebUI_GZ" in sec:
                pages[o] += size
    rows = sorted(set(flash) | set(ram), key=lambda k: -(flash[k] + ram[k]))
    print(f"{'flash':>9} {'ram':>8}  object   (merged string pool, {pool:,} B, not split)")
    for o in rows[:top]:
        note = f"  (web pages {pages[o]:,})" if pages[o] else ""
        print(f"{flash[o]:>9,} {ram[o]:>8,}  {o}{note}")
    print(f"{sum(flash.values()):>9,} {sum(ram.values()):>8,}  total over {len(rows)} objects")


def _toolchain_bin():
    hits = sorted(glob.glob(os.path.expanduser(
        "~/.platformio/packages/toolchain-rp2040-earlephilhower*/bin")))
    if not hits:
        sys.exit("flash_compose: PlatformIO's RP2040 toolchain not found under ~/.platformio")
    return hits[-1]


def relink_with_map(env):
    """(map, bin) of the image `env` already built, from a relink with -Map.

    Deleting firmware.elf makes `pio run -v` link again (and only link) and
    print the command; the objects are the ones the build made. The relink
    goes to a scratch directory and its .bin has to match the build's byte
    for byte, or the map would describe some other image."""
    build = os.path.join(ROOT, ".pio", "build", env)
    built_bin = os.path.join(build, "firmware.bin")
    if not os.path.exists(built_bin):
        sys.exit(f"flash_compose: {env} is not built — run `pio run -e {env}` first")
    with open(built_bin, "rb") as fh:
        want = fh.read()
    os.remove(os.path.join(build, "firmware.elf"))
    pio = os.path.expanduser("~/.platformio/penv/bin/pio")
    r = subprocess.run([pio if os.path.exists(pio) else "pio", "run", "-v", "-e", env],
                       cwd=ROOT, capture_output=True, text=True)
    elf_arg = f".pio/build/{env}/firmware.elf"
    line = next((l for l in r.stdout.splitlines()
                 if l.startswith("arm-none-eabi-g++ -o " + elf_arg)), None)
    if r.returncode != 0 or line is None:
        sys.exit(f"flash_compose: no link command in `pio run -v -e {env}`")
    tmp = tempfile.mkdtemp(prefix=f"flash_compose_{env}_")
    elf, mapf, binf = (os.path.join(tmp, n) for n in ("firmware.elf", "firmware.map", "firmware.bin"))
    args = shlex.split(line)
    i = args.index("-o")
    args[i + 1:i + 2] = [elf, f"-Wl,-Map={mapf}"]
    tc = _toolchain_bin()
    envp = dict(os.environ, PATH=tc + os.pathsep + os.environ.get("PATH", ""))
    subprocess.run(args, cwd=ROOT, check=True, env=envp)
    subprocess.run([os.path.join(tc, "arm-none-eabi-objcopy"), "-O", "binary", elf, binf],
                   check=True)
    with open(binf, "rb") as fh:
        if fh.read() != want:
            sys.exit(f"flash_compose: the relink of {env} is not the image in .pio/build "
                     f"(different .bin) — the map would not describe it. Scratch: {tmp}")
    return mapf, built_bin


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    objects = "--objects" in sys.argv
    top = 60
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])
        args = [a for a in args if a != str(top)]
    if "--env" in sys.argv:
        env = sys.argv[sys.argv.index("--env") + 1]
        mapf, binf = relink_with_map(env)
    elif len(args) >= 2:
        mapf, binf = args[0], args[1]
    else:
        sys.exit(__doc__)
    recs, outs, fill = parse(mapf)
    binsz = os.path.getsize(binf)
    ro_out = outs.get(".rodata", (0, 0))[1]
    ro_nonmerge = sum(s for o, sec, a, s, obj in recs if o == ".rodata" and not mergeable(sec))
    if objects:
        print_objects(recs, ro_out - ro_nonmerge - fill[".rodata"], top)
        return
    recs = [r for r in recs if (XIP_LO <= r[2] < XIP_HI) or r[0] == ".data"]
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
