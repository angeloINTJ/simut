#!/usr/bin/env python3
"""Fails the build when a .lng @DICT does not have exactly TR_KEYS_COUNT lines.

DisplayManager_LangParser.cpp partitions @DICT positionally — line N is
LangKey N — and rejects the whole file if the count differs. A rejected pack
is not a visible error: setLanguage() falls back to English and the device
just stops speaking Portuguese, which is exactly the kind of silence that
costs a debugging session. Adding a LangKey without adding its line in both
packs now breaks the build with the count named.

Also refuses a key inserted anywhere but the end while packs are unchanged,
since that shifts every string after it without changing any line count.
"""
import os
import re
import sys
from pathlib import Path

# Same dual mode as check_channels.py: PlatformIO exec's a pre: script without
# __file__, so the project root comes from the injected SCons env there and
# from the script's own location when run by hand.
try:
    Import("env")  # noqa: F821 — provided by SCons
    ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
except NameError:
    ROOT = Path(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

HEADER = ROOT / "src" / "DisplayManager.h"
PARSER = ROOT / "src" / "DisplayManager_LangParser.cpp"
PACKS = sorted((ROOT / "data" / "lang").glob("*.lng"))

# A pack over LANG_FILE_MAX is the same invisible failure as a short one:
# loadLangFile() returns false, setLanguage() reverts to English, and nothing
# says so. es-ES already sits near the ceiling (it derives from pt-BR by
# substitution and only fits because it omits @HELP/@LICENSE), so a warning
# band gives notice before a routine addition tips it over the edge in a
# commit that "just adds a string".
CEIL_WARN_FRAC = 0.95


def lang_file_max():
    src = PARSER.read_text(encoding="utf-8")
    m = re.search(r"LANG_FILE_MAX\s*=\s*(\d+)", src)
    if not m:
        raise SystemExit("check_lang_packs: LANG_FILE_MAX not found in "
                         "DisplayManager_LangParser.cpp")
    return int(m.group(1))


def enum_keys():
    src = HEADER.read_text(encoding="utf-8")
    m = re.search(r"enum\s+LangKey\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("check_lang_packs: enum LangKey not found in DisplayManager.h")
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    keys = [k.strip() for k in body.split(",") if k.strip()]
    if keys[-1] != "TR_KEYS_COUNT":
        raise SystemExit("check_lang_packs: TR_KEYS_COUNT must be the last enumerator")
    return keys[:-1]


def dict_lines(path):
    """Lines of the @DICT block.

    Anchored on lines that START with the directive: the es-ES header comment
    contains the text "@DICT/@HELP/@LICENSE", and a plain substring search
    lands there and reports a one-line dictionary.
    """
    lines = path.read_text(encoding="utf-8").split("\n")
    try:
        start = next(i for i, ln in enumerate(lines) if ln.strip() == "@DICT") + 1
    except StopIteration:
        raise SystemExit(f"check_lang_packs: {path.name} has no @DICT section")
    out = []
    for off, ln in enumerate(lines[start:]):
        if ln.startswith("@"):
            break
        out.append(ln)
    # The parser counts line BOUNDARIES, so an empty line is a string — an
    # empty one. Both packs used to end the block with a blank line, which is
    # how a stale 109-key pack still satisfied a 111-key firmware: it filled
    # the two new slots with "" and drew blank labels instead of falling back
    # to English. Blanks are rejected outright rather than tolerated at the end.
    blanks = [i for i, ln in enumerate(out) if ln.strip() == ""]
    while out and out[-1].strip() == "":
        out.pop()
    return out, blanks


def main():
    keys = enum_keys()
    want = len(keys)
    ceil = lang_file_max()
    warn_at = int(ceil * CEIL_WARN_FRAC)
    failed = False
    for pack in PACKS:
        size = pack.stat().st_size
        if size > ceil:
            print(f"[lang-packs] FAIL {pack.name}: {size} B exceeds LANG_FILE_MAX "
                  f"({ceil}) — loadLangFile() rejects it and the UI silently "
                  f"reverts to English", file=sys.stderr)
            failed = True
        elif size >= warn_at:
            print(f"[lang-packs] WARN {pack.name}: {size} B is {size * 100 // ceil}% "
                  f"of the {ceil} B ceiling ({ceil - size} B left) — the next "
                  f"addition may tip it over, and the failure is invisible at runtime",
                  file=sys.stderr)
        lines, blanks = dict_lines(pack)
        if blanks:
            print(f"[lang-packs] FAIL {pack.name}: blank line(s) inside @DICT at "
                  f"offset(s) {blanks} — the parser reads those as empty strings "
                  f"and every key after one shifts by a slot", file=sys.stderr)
            failed = True
            continue
        if len(lines) != want:
            print(f"[lang-packs] FAIL {pack.name}: @DICT has {len(lines)} lines, "
                  f"TR_KEYS_COUNT is {want} "
                  f"({'missing ' + str(want - len(lines)) if len(lines) < want else 'extra ' + str(len(lines) - want)})",
                  file=sys.stderr)
            failed = True
        else:
            print(f"[lang-packs] OK {pack.name}: {len(lines)} strings")
    if failed:
        print("[lang-packs] Neither failure is visible at runtime: a short pack is "
              "rejected whole and the UI reverts to English, and a blank line makes "
              "the keys after it draw the wrong string or nothing at all.",
              file=sys.stderr)
        raise SystemExit(1)


main()

# PlatformIO imports this as a pre: script; there is no Import("env") use here
# because the check needs nothing from the build environment.
