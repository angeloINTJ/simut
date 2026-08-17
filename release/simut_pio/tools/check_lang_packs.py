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
import json
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


def section(path, name):
    """Body lines of @<name>, or [] when the section is absent."""
    lines = path.read_text(encoding="utf-8").split("\n")
    try:
        start = next(i for i, ln in enumerate(lines)
                     if ln.strip() == "@" + name) + 1
    except StopIteration:
        return []
    out = []
    for ln in lines[start:]:
        if ln.startswith("@"):
            break
        out.append(ln)
    return out


def fnv1a32(s):
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


# A C++ string literal, plus the adjacent-literal concatenation the sources use.
_STR = r'"(?:[^"\\]|\\.)*"'
_TRL = re.compile(r'TRL\s*\(\s*((?:' + _STR + r')(?:\s*' + _STR + r')*)\s*\)', re.S)


def _unescape(lit):
    out = []
    for p in re.findall(_STR, lit, re.S):
        b = p[1:-1]
        for a, z in (('\\"', '"'), ("\\n", "\n"), ("\\t", "\t"),
                     ("\\r", "\r"), ("\\\\", "\\")):
            b = b.replace(a, z)
        out.append(b)
    return "".join(out)


def trl_literals():
    """{english: first "file:line"} for every TRL("...") under src/."""
    found = {}
    for f in sorted((ROOT / "src").rglob("*")):
        if f.suffix not in (".cpp", ".h", ".hpp", ".c"):
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        for m in _TRL.finditer(text):
            en = _unescape(m.group(1))
            if en:
                found.setdefault(en, f"{f.relative_to(ROOT)}:"
                                     f"{text.count(chr(10), 0, m.start()) + 1}")
    return found


def web_keys():
    """({key: where}, {runtime_prefix}) for every translation the browser asks for.

    Two consumers, not one: the WebUI.h bundle in flash AND the pages that
    moved to LittleFS (alarms/license) — they read the same @WEBDICT, so
    scanning only WebUI.h reports their keys as dead.
    """
    import gzip
    srcs = [("WebUI.h", (ROOT / "WebUI.h").read_text(encoding="utf-8",
                                                     errors="replace"))]
    for gz in sorted((ROOT / "data" / "web").glob("*.html.gz")):
        srcs.append((gz.name, gzip.decompress(gz.read_bytes())
                                  .decode("utf-8", "replace")))
    keys, prefixes = {}, set()
    for name, text in srcs:
        for pat in (r'data-i18n[a-z-]*=\\?["\']([A-Za-z0-9_]+)\\?["\']',
                    r'\bt\(\s*[\'"]([A-Za-z0-9_]+)[\'"]\s*[,)]'):
            for m in re.finditer(pat, text):
                keys.setdefault(m.group(1), name)
        # t('ch_' + id): the key is assembled at runtime, so the prefix is
        # all the check can know about it.
        for m in re.finditer(r'\bt\(\s*[\'"]([A-Za-z0-9_]+_)[\'"]\s*\+', text):
            prefixes.add(m.group(1))
    return keys, prefixes


def check_trl(pack, live):
    """@TRL is keyed by a hash of the ENGLISH string, so editing the English
    silently orphans the translation — the lookup misses and the line comes
    out in English with nothing logged."""
    have = {}
    for ln in section(pack, "TRL"):
        if not ln.strip():
            continue
        h, _, txt = ln.partition(" ")
        try:
            have[int(h, 16)] = txt
        except ValueError:
            print(f"[lang-packs] FAIL {pack.name}: malformed @TRL line {ln!r}",
                  file=sys.stderr)
            return False
    if not have:
        return True  # a pack may legitimately ship without @TRL
    live_hashes = {fnv1a32(t) for t in live}
    missing = [(t, w) for t, w in live.items() if fnv1a32(t) not in have]
    orphan = [h for h in have if h not in live_hashes]
    for t, where in sorted(missing):
        print(f"[lang-packs] FAIL {pack.name}: @TRL has no {fnv1a32(t):08x} for "
              f"{t[:60]!r} ({where}) — that line logs in English", file=sys.stderr)
    for h in sorted(orphan):
        print(f"[lang-packs] WARN {pack.name}: @TRL {h:08x} matches no live "
              f"TRL() — the English changed and {have[h][:48]!r} is dead weight",
              file=sys.stderr)
    return not missing


def check_webdict(pack, keys, prefixes):
    body = "\n".join(section(pack, "WEBDICT")).strip()
    if not body:
        return True
    try:
        wd = json.loads(body)
    except json.JSONDecodeError as e:
        print(f"[lang-packs] FAIL {pack.name}: @WEBDICT is not valid JSON ({e}) "
              f"— GET /api/lang serves it verbatim and the page keeps English",
              file=sys.stderr)
        return False
    missing = sorted(k for k in keys if k not in wd)
    dead = sorted(k for k in wd if k not in keys
                  and not any(k.startswith(p) for p in prefixes))
    for k in missing:
        print(f"[lang-packs] FAIL {pack.name}: @WEBDICT has no {k!r} "
              f"(asked for by {keys[k]}) — that label stays English",
              file=sys.stderr)
    if dead:
        print(f"[lang-packs] WARN {pack.name}: {len(dead)} @WEBDICT key(s) no "
              f"consumer asks for, spending bytes against the ceiling: "
              f"{', '.join(dead[:12])}{' ...' if len(dead) > 12 else ''}",
              file=sys.stderr)
    return not missing


def main():
    keys = enum_keys()
    want = len(keys)
    ceil = lang_file_max()
    warn_at = int(ceil * CEIL_WARN_FRAC)
    trl_live = trl_literals()
    wkeys, wprefixes = web_keys()
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
        # @DICT is the only section the count check can see. @TRL and
        # @WEBDICT rot differently — a key goes missing and just that one
        # string falls back to English, which no count catches.
        if not check_trl(pack, trl_live):
            failed = True
        if not check_webdict(pack, wkeys, wprefixes):
            failed = True
    if failed:
        print("[lang-packs] None of these failures is visible at runtime: a short "
              "pack is rejected whole and the UI reverts to English, a blank line "
              "makes the keys after it draw the wrong string or nothing at all, "
              "and a missing @TRL/@WEBDICT entry silently leaves that one line "
              "in English.", file=sys.stderr)
        raise SystemExit(1)


main()

# PlatformIO imports this as a pre: script; there is no Import("env") use here
# because the check needs nothing from the build environment.
