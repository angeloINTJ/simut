#!/usr/bin/env python3
"""check_net_boundary.py — the network is one driver per image, and the rest of src/ does not know which.

An image has ONE network driver: the CYW43 radio of the Pico W and the Pico 2 W, or a wired module
such as the ENC28J60 (`[net.<id>]` in tools/features.toml; src/net/NetCaps.h turns the driver into
the class the rest of the firmware reads, SIMUT_NET_WIFI or SIMUT_NET_LAN). Adding a board must be
one driver unit in src/net/ and one manifest entry, and must not touch the rest of src/. That only
stays true if the rest of src/ never reaches for a chip, so this gate holds four rules over
src/**/*.{c,cpp,h,hpp}, outside src/net/ (src/simut_config.h, where the module's pins are, is
let off the first rule only):

  identity        an identity macro: SIMUT_NET_<ID> for each [net.<id>] of tools/features.toml
                  (SIMUT_NET_CYW43, SIMUT_NET_ENC28J60, ...), or one of the ENC28J60's pin and PIO
                  macros (SIMUT_ENC_*, SIMUT_PINS_ENC*). Ask the class, or net/NetLink.h, or
                  net/NetBoot.h; or move the code into src/net/.
  radio-api       the radio's API: the global `WiFi.` object (WiFiClient, WiFiUDP, WiFiServer and
                  WiFiClientSecure run on any lwIP interface and are fine), `cyw43_` / `_cyw43_`
                  identifiers, an #include of a cyw43 header (pico/cyw43_arch.h).
  driver-library  an #include of a driver library: a header whose name ends in lwIP.h
                  (PicoENC28J60lwIP.h, W5500lwIP.h, ...), is lwIP_*.h, LwipIntfDev.h or
                  PicoENC28J60*.h.
  class-include   a file that uses SIMUT_NET_WIFI or SIMUT_NET_LAN and does not #include
                  simut_config.h or net/NetCaps.h ITSELF before the first use. `#if X` with X
                  undefined is 0, silently, and a feature drops out of the image: it happened on
                  2026-10-13, when a pure header testing SIMUT_NET_WIFI took the feature out of
                  the radio images. (net/NetCaps.h, which defines them, is the one exception.)

Comments and the contents of strings, characters and raw strings are stripped before any rule
looks (a character state machine, so that `'"'`, `1'000`, `"//"` and `R"x(...)x"` are read as the
compiler reads them; the lines stay, so a finding has the file's line number). The names in
#include directives are kept apart and read by the include rules. It is a pattern gate, not a
parser: `#if 0` is scanned as code, and the include of <WiFi.h> itself is not a finding, only
calls on the `WiFi` object are.

    python3 tools/check_net_boundary.py            # the gate: `path:line: rule: text`, exit 1 on any finding
    python3 tools/check_net_boundary.py --report   # every finding and the counts per rule and file, exit 0
    python3 tools/check_net_boundary.py --update   # rewrite the counts of the allowances already listed
    python3 tools/check_net_boundary.py --root DIR # another tree (DIR/src, DIR/tools/...): the test uses it

ALLOWANCES. A file that has to break a rule today is listed in tools/net_boundary.json, by hand,
with the reason it may and what ends it:

    {"radio-api": {"src/SomeFile.cpp": {"count": 3, "reason": "..."}}}

Since 2026-10-13 the file lists nothing: the last allowance, the radio's state machine in
src/NetworkManager.cpp, ended when it moved into its own unit, src/net/NetworkManager_Cyw43.cpp.

The number stays tight, like tools/feature_sprawl.json: a file with MORE findings than its count
fails ("grew"), a file with FEWER fails too ("lower the allowance"), and an allowed file with none
is "stale" and the entry goes. `--update` rewrites the counts of the files already listed and drops
the stale ones; it never adds a file, because a new allowance is a decision and the decision has a
reason. The allowance file is the only thing this script ever writes: never anything under src/.

Exit status: 0 clean, 1 findings, 2 the tree cannot be read (no src/, no [net.<id>] in the manifest,
an allowance file that is not JSON): a gate that cannot look must not pass.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import fnmatch
import json
import os
import re
import sys

try:
    import tomllib
except ModuleNotFoundError:                    # Python < 3.11: the CI and the dev machines have it
    sys.exit("check_net_boundary: needs Python 3.11 or newer (tomllib)")



class CannotRead(Exception):
    """The tree cannot be read (no src/, no manifest, an allowance file that is not JSON): exit status 2."""


DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = ("identity", "radio-api", "driver-library", "class-include")
EXTS = (".c", ".cpp", ".h", ".hpp")
NET_DIR = "src/net/"                 # the drivers: every rule is let off here
CONFIG = "src/simut_config.h"        # the module's pins: let off the identity rule only
CAPS = "src/net/NetCaps.h"           # defines SIMUT_NET_WIFI / SIMUT_NET_LAN: no include of its own to be asked for
CONFIG_HEADERS = ("simut_config.h", "NetCaps.h")
ALLOWANCES = "tools/net_boundary.json"
MANIFEST = "tools/features.toml"

# The ENC28J60's pins and PIO block: chip macros that are not SIMUT_NET_<ID>.
CHIP_MACROS = (r"\bSIMUT_ENC_\w*", r"\bSIMUT_PINS_ENC\w*")
RADIO_CALL = re.compile(r"\bWiFi\s*\.|\b_?cyw43_\w*")
CLASS_USE = re.compile(r"\bSIMUT_NET_(?:WIFI|LAN)\b")
CYW43_HEADER = re.compile(r"_?cyw43[_.]")


# ── the stripper ───────────────────────────────────────────────────────────────────────────────

_SPECIAL = re.compile(r"//|/\*|\"|'|(?:u8|u|U|L)?R\"|#")
_INCLUDE = re.compile(r"#[ \t]*(?:include_next|include|import)[ \t]*(?:<([^>\n]*)>|\"([^\"\n]*)\")")
_NOT_NEWLINE = re.compile(r"[^\n\r]")
_STRING_STOP = {'"': re.compile(r'[\\\n"]'), "'": re.compile(r"[\\\n']")}


def _blank(span: str) -> str:
    return _NOT_NEWLINE.sub(" ", span)


def _line_comment_end(text: str, k: int) -> int:
    """Where a `//` comment whose text starts at k ends: at the newline, unless a backslash escapes it."""
    n = len(text)
    while True:
        e = text.find("\n", k)
        if e < 0:
            return n
        j = e - 1
        if j >= k and text[j] == "\r":
            j -= 1
        if j >= k and text[j] == "\\":
            k = e + 1
            continue
        return e


def _quoted_end(text: str, k: int, quote: str) -> int:
    """The index of the quote that closes the literal whose contents start at k; an unescaped newline (or the
    end of the file) ends a literal that was never closed, and is returned instead, so one stray quote cannot
    swallow the rest of the file."""
    n = len(text)
    stop = _STRING_STOP[quote]
    while True:
        m = stop.search(text, k)
        if m is None:
            return n
        ch = m.group()
        if ch == "\\":
            k = m.start() + 2
            if text.startswith("\r\n", m.start() + 1):
                k += 1
            continue
        return m.start()


def _is_digit_separator(text: str, s: int) -> bool:
    """The apostrophe at s is the separator of a number (1'000), and not the start of a character literal."""
    j = s
    while j > 0 and (text[j - 1].isalnum() or text[j - 1] in "_.'"):
        j -= 1
    tok = text[j:s]
    if not tok or s + 1 >= len(text) or not text[s + 1].isalnum():
        return False
    return tok[0].isdigit() or (tok[0] == "." and len(tok) > 1 and tok[1].isdigit())


def strip_source(text: str):
    """(code, includes). `code` is `text` with every comment and the contents of every string, character and
    raw string replaced by spaces (newlines kept: same length, same lines), the quotes of a literal and the
    `#include` keyword left; the header name of an #include directive is blanked too and returned in `includes`
    as (line, name, quoted), where quoted is True for "name" and False for <name>."""
    n = len(text)
    out = []
    includes = []
    newlines = [m.start() for m in re.finditer("\n", text)]
    pos = 0                       # text[:pos] is already in `out`
    i = 0

    def literal(q: int, quote: str):
        """A normal literal whose opening quote is at q."""
        nonlocal pos, i
        e = _quoted_end(text, q + 1, quote)
        out.append(text[pos:q + 1])
        out.append(_blank(text[q + 1:e]))
        pos = e
        i = e + 1 if e < n and text[e] == quote else e

    while True:
        m = _SPECIAL.search(text, i)
        if m is None:
            break
        s, tok = m.start(), m.group()
        if tok == "//":
            e = _line_comment_end(text, s + 2)
            out.append(text[pos:s])
            out.append(_blank(text[s:e]))
            pos = i = e
        elif tok == "/*":
            e = text.find("*/", s + 2)
            e = n if e < 0 else e + 2
            out.append(text[pos:s])
            out.append(_blank(text[s:e]))
            pos = i = e
        elif tok == "'":
            if _is_digit_separator(text, s):
                i = s + 1
            else:
                literal(s, "'")
        elif tok == '"':
            literal(s, '"')
        elif tok.endswith('R"'):
            q = s + len(tok) - 1
            raw = not (s > 0 and (text[s - 1].isalnum() or text[s - 1] == "_"))
            if raw:
                p = text.find("(", q + 1)
                delim = text[q + 1:p] if p >= 0 else None
                raw = delim is not None and len(delim) <= 16 and not any(c in delim for c in ' \t\r\n\\)"')
            if not raw:                    # an identifier that ends in R, followed by a normal string
                literal(q, '"')
            else:
                end_marker = ")" + delim + '"'
                e = text.find(end_marker, p + 1)
                if e < 0:                  # never closed: the rest of the file is the string
                    out.append(text[pos:q + 1])
                    out.append(_blank(text[q + 1:]))
                    pos = i = n
                else:
                    e += len(end_marker) - 1           # the closing quote
                    out.append(text[pos:q + 1])
                    out.append(_blank(text[q + 1:e]))
                    pos = e
                    i = e + 1
        else:                                          # '#': a directive only at the start of a line
            ls = text.rfind("\n", 0, s) + 1
            mi = _INCLUDE.match(text, s) if text[ls:s].strip(" \t") == "" else None
            if mi is None:
                i = s + 1
            else:
                g = 1 if mi.group(1) is not None else 2
                a, b = mi.span(g)
                includes.append((bisect.bisect_left(newlines, s) + 1, mi.group(g), g == 2))
                out.append(text[pos:a])
                out.append(_blank(text[a:b]))
                pos = b
                i = mi.end()
    out.append(text[pos:])
    return "".join(out), includes


# ── the rules ──────────────────────────────────────────────────────────────────────────────────

Finding = collections.namedtuple("Finding", "path line rule text")


def manifest_identities(root: str):
    """The identity macros of the manifest: SIMUT_NET_<ID> for each [net.<id>]."""
    path = os.path.join(root, MANIFEST)
    try:
        with open(path, "rb") as fh:
            data = tomllib.load(fh)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise CannotRead(f"cannot read {MANIFEST}: {e}")
    ids = sorted(data.get("net", {}))
    if not ids:
        raise CannotRead(f"{MANIFEST} has no [net.<id>]: the identity rule would look for nothing")
    return ["SIMUT_NET_" + i.upper().replace("-", "_") for i in ids]


def identity_regex(macros) -> re.Pattern:
    named = r"\b(?:" + "|".join(re.escape(m) for m in macros) + r")\b"
    return re.compile("|".join([named, *CHIP_MACROS]))


def is_driver_library(header: str) -> bool:
    base = header.rsplit("/", 1)[-1]
    return (base.endswith("lwIP.h") or fnmatch.fnmatchcase(base, "lwIP_*.h") or base == "LwipIntfDev.h"
            or fnmatch.fnmatchcase(base, "PicoENC28J60*.h"))


def scan_text(rel: str, text: str, ident: re.Pattern):
    """The findings of one file, before any allowance."""
    code, includes = strip_source(text)
    newlines = [m.start() for m in re.finditer("\n", code)]
    lines = text.split("\n")          # a finding shows the line as written, strings and all

    def line_of(k: int) -> int:
        return bisect.bisect_left(newlines, k) + 1

    def ctx(line: int) -> str:
        return " ".join(lines[line - 1].split())

    found = []
    if not rel.startswith(NET_DIR):
        if rel != CONFIG:
            for m in ident.finditer(code):
                ln = line_of(m.start())
                found.append(Finding(rel, ln, "identity", ctx(ln)))
        for m in RADIO_CALL.finditer(code):
            ln = line_of(m.start())
            found.append(Finding(rel, ln, "radio-api", ctx(ln)))
        for ln, name, quoted in includes:
            shown = f'#include "{name}"' if quoted else f"#include <{name}>"
            if CYW43_HEADER.match(name.rsplit("/", 1)[-1]):
                found.append(Finding(rel, ln, "radio-api", shown))
            elif is_driver_library(name):
                found.append(Finding(rel, ln, "driver-library", shown))
    if rel != CAPS:
        use = CLASS_USE.search(code)
        if use:
            first = line_of(use.start())
            own = [ln for ln, name, quoted in includes
                   if quoted and name.rsplit("/", 1)[-1] in CONFIG_HEADERS and ln < first]
            if not own:
                found.append(Finding(rel, first, "class-include",
                                     f"{use.group()} is used with no #include of simut_config.h or net/NetCaps.h "
                                     f"of this file before it: {ctx(first)}"))
    return found


def scan(root: str, ident: re.Pattern):
    src = os.path.join(root, "src")
    if not os.path.isdir(src):
        raise CannotRead(f"no src/ under {root}")
    found = []
    for dirpath, dirs, files in os.walk(src):
        dirs.sort()
        for f in sorted(files):
            if not f.endswith(EXTS):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as fh:
                found.extend(scan_text(rel, fh.read(), ident))
    return found


# ── the allowances ─────────────────────────────────────────────────────────────────────────────

def load_allowances(root: str) -> dict:
    path = os.path.join(root, ALLOWANCES)
    if not os.path.exists(path):
        return {}
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, ValueError) as e:
        raise CannotRead(f"cannot read {ALLOWANCES}: {e}")
    if not isinstance(data, dict):
        raise CannotRead(f"{ALLOWANCES} is not a JSON object of rules")
    return data


def entry_problem(entry) -> str | None:
    if not isinstance(entry, dict):
        return 'the allowance is not {"count": n, "reason": "..."}'
    count = entry.get("count")
    if isinstance(count, bool) or not isinstance(count, int) or count < 1:
        return f"the allowance's count must be a positive whole number, not {count!r}"
    reason = entry.get("reason")
    if not isinstance(reason, str) or not reason.strip():
        return "the allowance has no reason: say why the file may break the rule, and what ends it"
    return None


def apply_allowances(found, allowances):
    """What is left to report: the findings no allowance covers, and the allowances that no longer fit."""
    by_file = collections.defaultdict(list)
    for f in found:
        by_file[(f.rule, f.path)].append(f)
    left = []
    covered = set()
    for rule, files in allowances.items():
        if not isinstance(files, dict):
            left.append(Finding(ALLOWANCES, 1, str(rule), "the rule's allowances are not an object of paths"))
            continue
        for path, entry in files.items():
            here = by_file.get((rule, path), [])
            if rule not in RULES:
                left.append(Finding(path, 1, str(rule), f"unknown rule; the rules are {', '.join(RULES)}"))
                continue
            problem = entry_problem(entry)
            if problem:
                left.append(Finding(path, 1, rule, problem))
                continue
            covered.add((rule, path))
            n = entry["count"]
            sites = ", ".join(str(f.line) for f in here[:20]) + (", ..." if len(here) > 20 else "")
            if not here:
                left.append(Finding(path, 1, rule, f"stale: no findings, remove the allowance ({ALLOWANCES}, or --update)"))
            elif len(here) > n:
                left.append(Finding(path, here[0].line, rule,
                                    f"grew: {len(here)} findings, {n} allowed in {ALLOWANCES} (lines {sites})"))
            elif len(here) < n:
                left.append(Finding(path, here[0].line, rule,
                                    f"lower the allowance: {len(here)} findings, {n} allowed in {ALLOWANCES}: "
                                    f"set the count to {len(here)} (or --update)"))
    for key, here in by_file.items():
        if key not in covered:
            left.extend(here)
    return sorted(left, key=lambda f: (f.path, f.line, f.rule, f.text))


def update_allowances(root: str, allowances: dict, found) -> list[str]:
    """Rewrite the counts of the files already listed; drop the stale ones; add nothing. Returns what changed."""
    counts = collections.Counter((f.rule, f.path) for f in found)
    changed = []
    for rule in list(allowances):
        files = allowances[rule]
        if rule not in RULES or not isinstance(files, dict):
            continue
        for path in list(files):
            k = counts.get((rule, path), 0)
            entry = files[path]
            if k == 0:
                del files[path]
                changed.append(f"dropped {rule} {path} (no findings)")
            elif isinstance(entry, dict) and entry.get("count") != k:
                changed.append(f"{rule} {path}: count {entry.get('count')!r} -> {k}")
                entry["count"] = k
        if not files:
            del allowances[rule]
    if changed:
        with open(os.path.join(root, ALLOWANCES), "w", encoding="utf-8") as fh:
            fh.write(json.dumps(allowances, indent=1, ensure_ascii=False) + "\n")
    return changed


# ── main ───────────────────────────────────────────────────────────────────────────────────────

HINTS = {
    "identity": "ask the class (SIMUT_NET_WIFI / SIMUT_NET_LAN), net/NetLink.h or net/NetBoot.h, or move the code into src/net/",
    "radio-api": "ask net/NetLink.h (net::linkUp( ), net::stopForFlash( ), net::powerDownForSleep( )) or NetworkManager, or move the code into src/net/",
    "driver-library": "a driver library is included by its own unit in src/net/ and nowhere else",
    "class-include": 'add #include "simut_config.h" (or "net/NetCaps.h") to the file itself, before the first use',
}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="The network driver's boundary: see the module docstring.")
    ap.add_argument("--root", default=DEFAULT_ROOT, help="the tree to look at (default: this repository)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--update", action="store_true", help="rewrite the counts of the allowances already listed")
    mode.add_argument("--report", action="store_true", help="print every finding and the counts, exit 0")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.root)
    try:
        found = scan(root, identity_regex(manifest_identities(root)))
        allowances = load_allowances(root)
    except CannotRead as e:
        print(f"check_net_boundary: {e}", file=sys.stderr)
        return 2

    if args.report:
        for f in sorted(found, key=lambda f: (f.path, f.line, f.rule)):
            print(f"{f.path}:{f.line}: {f.rule}: {f.text}")
        counts = collections.Counter((f.rule, f.path) for f in found)
        if counts:
            print("\ncounts per rule and file:")
        for (rule, path), k in sorted(counts.items(), key=lambda kv: (RULES.index(kv[0][0]), kv[0][1])):
            entry = allowances.get(rule, {}).get(path) if isinstance(allowances.get(rule), dict) else None
            allowed = f"  (allowed {entry.get('count')})" if isinstance(entry, dict) else ""
            print(f"  {rule:<15} {path}  {k}{allowed}")
        print(f"check_net_boundary: {len(found)} findings")
        return 0

    if args.update:
        changed = update_allowances(root, allowances, found)
        print("\n".join(changed) if changed else "check_net_boundary: the counts already match")
        return 0

    left = apply_allowances(found, allowances)
    for f in left:
        print(f"{f.path}:{f.line}: {f.rule}: {f.text}")
    if left:
        for rule in RULES:
            if any(f.rule == rule for f in left):
                print(f"check_net_boundary: {rule}: {HINTS[rule]}", file=sys.stderr)
        print(f"check_net_boundary: an allowance is tools/net_boundary.json, written by hand with its reason "
              f"(see the docstring of tools/check_net_boundary.py)", file=sys.stderr)
    print(f"check_net_boundary: {len(left)} findings")
    return 1 if left else 0


if __name__ == "__main__":
    sys.exit(main())
