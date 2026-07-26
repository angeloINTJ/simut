#!/usr/bin/env python3
"""Single source of truth for the SIMUT log-code tables.

A log code has to be declared in five places that must agree, and nothing
enforced that: the LogCode enum, translateCodeEn( ) in LogManager.cpp, the
EVT_NAMES_EN / EVT_NAMES_PT objects in WebUI.h, and the @LOGCODES block of
each language pack. v1.5.6-beta shipped five codes (512-515, 575) present in
the enum and missing from the browser tables, and 575 turned out to be missing
from translateCodeEn as well, so it rendered as "?" on every channel in every
language. Nobody noticed because adding a code is five manual edits and the
fifth is easy to skip.

tools/logcodes.tsv is the canonical list. This script regenerates the derived
blocks from it and, in --check mode, fails the build when any of the five
drifts. --check compares TEXT, not just presence: editing a label in one table
and not the others is the same class of bug and used to be invisible too.

Columns, tab-separated:
    code    numeric value
    enum    LogCode enumerator name
    en      English — feeds translateCodeEn( ) and EVT_NAMES_EN
    js_pt   Portuguese for EVT_NAMES_PT, historically written WITHOUT accents
    pt      Portuguese for the pt-BR pack @LOGCODES, with accents
    es      Spanish for the es-ES pack @LOGCODES

js_pt and pt are separate columns because the two tables genuinely differ
today; collapsing them would be a content change, not a refactor.

Usage:
    gen_logcodes.py --check      verify all five tables; non-zero on drift
    gen_logcodes.py --cpp        emit the translateCodeEn( ) case block
    gen_logcodes.py --js         emit EVT_NAMES_EN / EVT_NAMES_PT
    gen_logcodes.py --lng        emit both @LOGCODES blocks
    gen_logcodes.py --sync-lng   write the @LOGCODES blocks into data/lang/

Project: SIMUT
License: MIT
"""

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TSV = ROOT / "tools" / "logcodes.tsv"
ENUM_H = ROOT / "src" / "SystemDefs_Logging.h"
LOGMGR = ROOT / "src" / "LogManager.cpp"
WEBUI = ROOT / "WebUI.h"
LANGS = {"pt-BR": "pt", "es-ES": "es"}

FIELDS = ("code", "enum", "en", "js_pt", "pt", "es")


# ── loading ────────────────────────────────────────────────────────────────

def load_tsv():
    rows = []
    for n, line in enumerate(TSV.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != len(FIELDS):
            sys.exit(f"{TSV}:{n}: {len(parts)} columns, expected {len(FIELDS)}")
        row = dict(zip(FIELDS, parts))
        row["code"] = int(row["code"])
        rows.append(row)
    rows.sort(key=lambda r: r["code"])
    seen = {}
    for r in rows:
        if r["code"] in seen:
            sys.exit(f"duplicate code {r['code']}: {seen[r['code']]} and {r['enum']}")
        seen[r["code"]] = r["enum"]
    return rows


def parse_enum():
    """{enumerator: value} from the C++ LogCode enum."""
    text = ENUM_H.read_text(encoding="utf-8")
    body = re.search(r"enum LogCode\s*\{(.*?)\n\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", body, flags=re.S))
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"(\w+)\s*=\s*(\d+)", body)}


def parse_cpp():
    """{enumerator: english} from translateCodeEn( )."""
    text = LOGMGR.read_text(encoding="utf-8")
    body = re.search(r"static const char\* translateCodeEn\(uint16_t code\) \{(.*?)\n\}",
                     text, re.S).group(1)
    return {m.group(1): unescape_c(m.group(2))
            for m in re.finditer(r'case\s+(\w+)\s*:\s*return\s+"((?:[^"\\]|\\.)*)"', body)}


def parse_js(which):
    """{code: label} from a WebUI EVT_NAMES_* object."""
    text = WEBUI.read_text(encoding="utf-8", errors="replace")
    m = re.search(which + r"\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        sys.exit(f"{which} not found in {WEBUI.name}")
    return {int(k): unescape_js(v)
            for k, v in re.findall(r"'(\d+)'\s*:\s*'((?:[^'\\]|\\.)*)'", m.group(1))}


def parse_lng(lang):
    """{code: label} from a pack's @LOGCODES block."""
    text = (ROOT / "data" / "lang" / f"language_{lang}.lng").read_text(encoding="utf-8")
    block = text.split("@LOGCODES\n", 1)[1].split("\n@TRL", 1)[0]
    out = {}
    for line in block.split("\n"):
        if line.strip():
            code, _, label = line.partition(" ")
            out[int(code)] = label
    return out


# ── escaping ───────────────────────────────────────────────────────────────

def escape_c(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def unescape_c(s):
    return s.replace('\\"', '"').replace("\\\\", "\\")


def escape_js(s):
    return s.replace("\\", "\\\\").replace("'", "\\'")


def unescape_js(s):
    return s.replace("\\'", "'").replace("\\\\", "\\")


# ── emitters ───────────────────────────────────────────────────────────────

def emit_cpp(rows):
    for r in rows:
        print(f'\t\tcase {r["enum"]}: return "{escape_c(r["en"])}";')


def emit_js(rows):
    for name, col in (("EVT_NAMES_EN", "en"), ("EVT_NAMES_PT", "js_pt")):
        pairs = ", ".join(f"'{r['code']}':'{escape_js(r[col])}'" for r in rows)
        print(f"        const {name} = {{ {pairs} }};")


def lng_block(rows, col):
    return "".join(f"{r['code']} {r[col]}\n" for r in rows)


def emit_lng(rows):
    for lang, col in LANGS.items():
        print(f"--- @LOGCODES ({lang}) ---")
        print(lng_block(rows, col), end="")


def sync_lng(rows):
    for lang, col in LANGS.items():
        p = ROOT / "data" / "lang" / f"language_{lang}.lng"
        text = p.read_text(encoding="utf-8")
        head, sep, rest = text.partition("@LOGCODES\n")
        if not sep:
            sys.exit(f"{p.name}: no @LOGCODES section")
        _, sep2, tail = rest.partition("\n@TRL")
        if not sep2:
            sys.exit(f"{p.name}: no @TRL after @LOGCODES")
        p.write_text(head + "@LOGCODES\n" + lng_block(rows, col) + sep2.lstrip("\n") + tail,
                     encoding="utf-8")
        print(f"{p.name}: {len(rows)} codes written")


# ── check ──────────────────────────────────────────────────────────────────

def check(rows):
    problems = []
    by_code = {r["code"]: r for r in rows}
    by_name = {r["enum"]: r for r in rows}

    enum = parse_enum()
    for name, val in enum.items():
        if name not in by_name:
            problems.append(f"enum has {name} = {val}, TSV does not")
        elif by_name[name]["code"] != val:
            problems.append(f"{name}: enum says {val}, TSV says {by_name[name]['code']}")
    for r in rows:
        if r["enum"] not in enum:
            problems.append(f"TSV has {r['enum']} = {r['code']}, enum does not")

    cpp = parse_cpp()
    for r in rows:
        if r["enum"] not in cpp:
            problems.append(f"translateCodeEn: no case for {r['enum']} ({r['code']})")
        elif cpp[r["enum"]] != r["en"]:
            problems.append(f"translateCodeEn {r['enum']}: {cpp[r['enum']]!r} != TSV {r['en']!r}")
    for name in cpp:
        if name not in by_name:
            problems.append(f"translateCodeEn has a case for {name}, TSV does not")

    for which, col in (("EVT_NAMES_EN", "en"), ("EVT_NAMES_PT", "js_pt")):
        table = parse_js(which)
        for r in rows:
            if r["code"] not in table:
                problems.append(f"{which}: missing {r['code']} ({r['enum']})")
            elif table[r["code"]] != r[col]:
                problems.append(f"{which} {r['code']}: {table[r['code']]!r} != TSV {r[col]!r}")
        for code in table:
            if code not in by_code:
                problems.append(f"{which} has {code}, TSV does not")

    for lang, col in LANGS.items():
        table = parse_lng(lang)
        for r in rows:
            if r["code"] not in table:
                problems.append(f"@LOGCODES {lang}: missing {r['code']} ({r['enum']})")
            elif table[r["code"]] != r[col]:
                problems.append(f"@LOGCODES {lang} {r['code']}: {table[r['code']]!r} != TSV {r[col]!r}")
        for code in table:
            if code not in by_code:
                problems.append(f"@LOGCODES {lang} has {code}, TSV does not")

    if problems:
        print(f"[logcodes] FATAL: {len(problems)} divergence(s) from tools/logcodes.tsv")
        for p in problems[:40]:
            print(f"[logcodes]   {p}")
        if len(problems) > 40:
            print(f"[logcodes]   ... and {len(problems) - 40} more")
        print("[logcodes] Edit tools/logcodes.tsv, then regenerate:")
        print("[logcodes]   python3 tools/gen_logcodes.py --sync-lng")
        print("[logcodes]   python3 tools/gen_logcodes.py --cpp   # paste into LogManager.cpp")
        print("[logcodes]   python3 tools/gen_logcodes.py --js    # paste into WebUI.h")
        return False

    print(f"[logcodes] OK {len(rows)} codes, five tables in agreement")
    return True


def main():
    ap = argparse.ArgumentParser(description="SIMUT log-code table generator")
    # Real flags, not a positional whose values start with "--": argparse reads
    # those as options and refuses them as a positional choice.
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true", help="verify the five tables; non-zero on drift")
    g.add_argument("--cpp", action="store_true", help="emit the translateCodeEn( ) case block")
    g.add_argument("--js", action="store_true", help="emit EVT_NAMES_EN / EVT_NAMES_PT")
    g.add_argument("--lng", action="store_true", help="emit both @LOGCODES blocks")
    g.add_argument("--sync-lng", action="store_true", help="write @LOGCODES into data/lang/")
    a = ap.parse_args()
    rows = load_tsv()
    if a.check:
        sys.exit(0 if check(rows) else 1)
    elif a.cpp:
        emit_cpp(rows)
    elif a.js:
        emit_js(rows)
    elif a.lng:
        emit_lng(rows)
    else:
        sync_lng(rows)


if __name__ == "__main__":
    main()
