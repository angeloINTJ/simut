#!/usr/bin/env python3
"""bytes_original.py -- how many bytes of a firmware.elf are the original UI driver.

usage: bytes_original.py [--tsv FILE] [--src WORKTREE] [--nm PATH] [--top N] [--unmapped] ELF [ELF...]

  --tsv FILE   the classification (default: this repository's docs/analysis/UI_DRIVERS_funcoes.tsv)
  --src DIR    a worktree; with it, the original share of each mixed function is also estimated from
               the code lines (comments and blanks do not count) that the TSV puts in each column.
               The line reader is ui_funcs.py, the stage S0 scanner, which is not in this repository
               yet: without it beside this script, --src stops with exit 2 and a message
  --top N      list the N largest original symbols
  --unmapped   list the symbols of class DisplayManager that no TSV row accounts for

How it works. `arm-none-eabi-nm -S -C` lists every symbol with its size. A symbol is attached to a
TSV row by its demangled name without the parameter list ("DisplayManager::drawTopBar(...)" ->
"DisplayManager::drawTopBar"); a function-local static or a lambda belongs to its function; the
compiler's clones (.constprop, .isra, .part, .cold) belong to the function they came from. The three
GFX font rows match by prefix, so FreeSansBold9pt8b also takes ...Bitmaps and ...Glyphs.
A row that names several things ("A, B, C") names several symbols.

Counted whole: every row of class `original`. Counted apart, never added in: every row of class
`mixed` (a function that holds both, as a whole). `core` rows are tallied only so the three add up to
what the units hold. Per-mode rows of the touch handlers (symbol contains " [") are views into a
function and are skipped.

Flash and RAM. Types T t W w R r V v D d at a flash address (0x10......) count as flash. Types B b
are RAM only (.bss) and are reported apart. What this cannot see: code the compiler inlined into
another function (it is in the caller's size), the merged string pool (.rodata.str*, no symbols), and
the constants and literals that have no symbol.
"""
import argparse
import csv
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from elf_funcoes import TOOL  # noqa: E402  (one place says where the PlatformIO toolchain is)

NM_DEFAULT = str(TOOL / "arm-none-eabi-nm")
FLASH_TYPES = set("TtWwRrDdVv")
RAM_TYPES = set("BbSs")
FONT_ROWS = ("FreeSansBold9pt8b", "FreeSansBold12pt8b", "FreeSansBold24pt7b")


def run_nm(nm, elf):
    out = subprocess.run([nm, "-S", "-C", elf], capture_output=True, text=True, check=True).stdout
    syms = []
    for ln in out.splitlines():
        m = re.match(r"^([0-9a-f]+) ([0-9a-f]+) (\S) (.*)$", ln)
        if m:
            syms.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3), m.group(4)))
    return syms


def key_of(name):
    name = re.sub(r" \[clone [^\]]+\]$", "", name)
    name = re.sub(r"::\{lambda\(.*$", "", name)
    pre = ""
    if name.startswith("(anonymous namespace)::"):
        pre = "(anonymous namespace)::"
        name = name[len(pre):]
    i = name.find("(")
    if i >= 0:
        name = name[:i]
    return pre + name


def load_tsv(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        for r in csv.DictReader(fh, delimiter="\t"):
            rows.append(r)
    return rows


def ranges(spec):
    out = []
    for part in (spec or "").split(","):
        part = part.strip()
        if not part:
            continue
        a, _, b = part.partition("-")
        out.append((int(a), int(b or a)))
    return out


def code_lines(src_root, unit):
    """Set of line numbers that hold code (not blank, not only a comment) in a unit."""
    try:
        import ui_funcs
    except ImportError:
        print("bytes_original.py: --src needs ui_funcs.py (the stage S0 scanner, not in this repository yet) "
              "beside this script; run without --src for the sums by class", file=sys.stderr)
        sys.exit(2)
    path = os.path.join(src_root, "src", unit)
    text = open(path, encoding="utf-8", errors="replace").read()
    return {i + 1 for i, ln in enumerate(ui_funcs.blank(text).split("\n")) if ln.strip()}


def where(addr, typ):
    if typ in RAM_TYPES:
        return "ram"
    if 0x10000000 <= addr < 0x11000000:
        return "flash"
    if addr >= 0x20000000:
        return "ramimg"      # code or data that runs from RAM and is stored in the flash image
    return "other"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", nargs="+")
    ap.add_argument("--tsv", default=os.path.join(HERE, "..", "docs", "analysis", "UI_DRIVERS_funcoes.tsv"))
    ap.add_argument("--src")
    ap.add_argument("--nm", default=NM_DEFAULT)
    ap.add_argument("--top", type=int, default=0)
    ap.add_argument("--unmapped", action="store_true")
    a = ap.parse_args()

    rows = load_tsv(a.tsv)
    # symbol key -> (class, unit, row)
    table = {}
    for r in rows:
        if " [" in r["symbol"]:
            continue
        for nm in [s.strip() for s in r["symbol"].split(", ")]:
            table.setdefault(nm, []).append(r)
    cls_of = {}
    for nm, rs in table.items():
        cs = {r["class"] for r in rs}
        cls_of[nm] = cs.pop() if len(cs) == 1 else "mixed"

    shares = {}
    if a.src:
        cache = {}
        for r in rows:
            if r["class"] != "mixed" or " [" in r["symbol"]:
                continue
            u = r["unit"]
            if u not in cache:
                cache[u] = code_lines(a.src, u)
            o = sum(1 for lo, hi in ranges(r["original_lines"]) for n in range(lo, hi + 1) if n in cache[u])
            c = sum(1 for lo, hi in ranges(r["core_lines"]) for n in range(lo, hi + 1) if n in cache[u])
            shares[r["symbol"]] = (o, c)

    for elf in a.elf:
        syms = run_nm(a.nm, elf)
        tot = {}
        per_unit = {}
        matched_rows = set()
        mapped_idx = set()
        top = []
        def add(bucket, unit, kind, size):
            tot[(bucket, kind)] = tot.get((bucket, kind), 0) + size
            per_unit.setdefault((unit, bucket), {}).setdefault(kind, 0)
            per_unit[(unit, bucket)][kind] += size
        for idx, (addr, size, typ, name) in enumerate(syms):
            k = key_of(name)
            hit = None
            if k in table:
                hit = k
            else:
                for f in FONT_ROWS:
                    if k == f or (k.startswith(f) and k[len(f):] in ("Bitmaps", "Glyphs")):
                        hit = f
                        break
            if hit is None:
                continue
            mapped_idx.add(idx)
            matched_rows.add(hit)
            bucket = cls_of[hit]
            unit = table[hit][0]["unit"]
            w = where(addr, typ)
            kind = "ram" if w == "ram" else ("text" if typ in "TtWw" else "data")
            add(bucket, unit, kind, size)
            if bucket == "original":
                top.append((size, typ, name[:90], unit))
        def tl(bucket, kinds=("text", "data")):
            return sum(tot.get((bucket, k), 0) for k in kinds)
        print("== %s" % elf)
        print("   original rows, counted whole : %8d B flash  (text %d, rodata+data %d)   + %d B RAM (.bss)" % (
            tl("original"), tot.get(("original", "text"), 0), tot.get(("original", "data"), 0), tot.get(("original", "ram"), 0)))
        print("   mixed rows, whole functions   : %8d B flash  (text %d, rodata+data %d)   + %d B RAM   [kept apart, NOT in the line above]" % (
            tl("mixed"), tot.get(("mixed", "text"), 0), tot.get(("mixed", "data"), 0), tot.get(("mixed", "ram"), 0)))
        print("   core rows                     : %8d B flash  (text %d, rodata+data %d)   + %d B RAM" % (
            tl("core"), tot.get(("core", "text"), 0), tot.get(("core", "data"), 0), tot.get(("core", "ram"), 0)))
        # subtotals of the original class
        def unit_total(units):
            return sum(tl_u for (u, b), kinds in per_unit.items() if b == "original" and u in units
                       for tl_u in [kinds.get("text", 0) + kinds.get("data", 0)])
        fonts = unit_total({"DisplayManager_Fonts.cpp"})
        themes = unit_total({"Themes.cpp"})
        widgets = unit_total({"UiWidgets.cpp"})
        print("   of the original: fonts %d B, themes %d B, UiWidgets %d B, drawing in DisplayManager*.cpp %d B" % (
            fonts, themes, widgets, tl("original") - fonts - themes - widgets))
        print("   by unit (original, flash):")
        for (u, b), kinds in sorted(per_unit.items()):
            if b == "original":
                print("      %-34s text %6d  data %6d  ram %5d" % (u, kinds.get("text", 0), kinds.get("data", 0), kinds.get("ram", 0)))
        # mixed estimate
        if shares:
            est = 0.0
            print("   mixed functions in this image, with the original share of their code lines (estimate):")
            for (addr, size, typ, name) in syms:
                k = key_of(name)
                if k in table and cls_of[k] == "mixed" and typ in "TtWw" and k in shares:
                    o, c = shares[k]
                    f = o / (o + c) if (o + c) else 0.0
                    est += size * f
                    print("      %-44s %6d B  original %3d / core %3d code lines -> %5.0f B" % (k[:44], size, o, c, size * f))
            print("   estimated original share of the mixed functions: %.0f B (an estimate from line counts, not a measurement)" % est)
        # unmatched original rows (no symbol in this image)
        miss = sorted(nm for nm, rs in table.items() if cls_of[nm] == "original" and nm not in matched_rows)
        print("   original rows with no symbol in this image (inlined, constant, or compiled out): %d" % len(miss))
        if a.top:
            print("   largest original symbols:")
            for size, typ, name, unit in sorted(top, reverse=True)[:a.top]:
                print("      %6d %s %s  (%s)" % (size, typ, name, unit))
        if a.unmapped:
            print("   DisplayManager:: symbols no row accounts for:")
            for idx, (addr, size, typ, name) in enumerate(syms):
                if idx not in mapped_idx and name.startswith("DisplayManager::"):
                    print("      %6d %s %s" % (size, typ, name[:100]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
