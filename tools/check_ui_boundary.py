#!/usr/bin/env python3
"""check_ui_boundary.py — the touch panel's look is one UI driver per image, and each driver stays in its walls.

An image has ONE UI driver: the original, the panel every release ships, or the Ângulo, the new one
(`[ui.<id>]` in tools/features.toml; src/ui/UiCaps.h turns the driver into the macros the rest of the
firmware reads, SIMUT_UI_ORIGINAL and SIMUT_UI_ANGULO for the identity, SIMUT_UI_CUSTOM_THEMES for what
the original can do and the Ângulo cannot). Adding a driver must be one directory in src/ui/ and one
manifest entry, and swapping one for another must not need an edit outside them. That only stays true if
the core never reaches for a driver's name and the drivers never reach for each other, so this gate holds
five rules over src/**/*.{c,cpp,h,hpp}:

  identity      an identity macro (SIMUT_UI_<ID> for each [ui.<id>] of tools/features.toml: SIMUT_UI_ANGULO,
                SIMUT_UI_ORIGINAL) outside src/ui/ and src/simut_config.h. Ask a capability
                (SIMUT_UI_CUSTOM_THEMES) or a driver hook; or move the code into the driver's directory.
  no-ifdef      `#ifdef`, `#ifndef`, `#elifdef`, `#elifndef` or `defined( )` on any SIMUT_UI_* macro, in
                any file. After src/ui/UiCaps.h every one of them is defined (SIMUT_UI_ANGULO is 0 on the
                original), so `#ifdef SIMUT_UI_ANGULO` is true on every image: shown with the host
                preprocessor in the review of 2026-10-14. Test them with `#if`. One file is let off,
                src/ui/UiCaps.h itself: it is where they are defined, its `defined( )` refuses a -D of
                a derived macro and its `#ifndef` gives the identity its default.
  include-caps  a file that uses a SIMUT_UI_* macro and does not #include simut_config.h or ui/UiCaps.h
                ITSELF before the first use. `#if X` with X undefined is 0, silently, and a feature drops
                out of the image: it happened to the network class on 2026-10-13 (the same rule, in
                tools/check_net_boundary.py). src/ui/UiCaps.h, which defines them, is the one exception.
  cross-driver  a unit under src/ui/angulo/ that includes a header only the original uses (Themes.h,
                UiWidgets.h, DisplayManager_Fonts.h, PasswordKeyboard.h, sensors/SensorDrawing.h), or a
                unit outside src/ui/angulo/ that includes a header under it (the include is resolved
                the way the compiler does: relative to the including file, then from src/).
  driver-hook   a name the Ângulo driver introduces, used outside src/ui/angulo/. The names are taken from
                the driver, not from a pattern on the other files: every identifier in src/ui/angulo/
                that carries the driver's mark, `ua` and a capital for a function or a member (uaSujo,
                uaToque, ...) or `Ua` and a capital for a type (UaToque, UaQuadro), split at its
                underscores, so that _uaPintando, g_uaVm and m_uaSujo are all uaPintando, uaVm and uaSujo;
                and the top-level namespaces it declares (`ua`, `entrega`). Outside the driver an
                identifier is a hook when one of its underscore-separated parts is such a name, or when
                it is a namespace of the driver used with `::` or `using namespace`. A new hook the
                driver adds is therefore found without an edit here, and `uaString`, a user agent in
                the web server, is not: the driver does not introduce it. One pattern remains, for the
                macros: every UA_<anything> (UA_DESVIO is defined in a shared header, so the driver's
                text does not give it). The gate refuses a tree whose src/ui/angulo/ has no source
                (exit 2): with no driver there would be no names, and a gate must not pass for that.

Comments and the contents of strings, characters and raw strings are stripped before any rule looks,
by the state machine of tools/check_net_boundary.py (imported, so there is one: `'"'`, `1'000`, `"//"`
and `R"x(...)x"` are read as the compiler reads them; the lines stay, so a finding has the file's line
number; the names in #include directives are kept apart). It is a pattern gate, not a parser: `#if 0`
is scanned as code, a backslash-newline is followed only between `#ifdef` (or `defined`) and its macro,
an `#include` after a comment on its line is not read, and a driver that reaches the other through a
third header is not seen, only the include that makes it possible.

    python3 tools/check_ui_boundary.py            # the gate: `path:line: rule: text`, exit 1 on any finding
    python3 tools/check_ui_boundary.py --report   # every finding and the counts per rule and file, exit 0
    python3 tools/check_ui_boundary.py --update   # rewrite the counts of the allowances already listed
    python3 tools/check_ui_boundary.py --root DIR # another tree (DIR/src, DIR/tools/...): the test uses it

ALLOWANCES. A file that has to break a rule today is listed in tools/ui_boundary.json, by hand, with the
reason it may and the stage that ends it:

    {"driver-hook": {"src/SomeFile.cpp": {"count": 3, "reason": "..."}}}

Since 2026-10-14 the file lists the transitional shape of the Ângulo driver (stage S2a of the UI drivers
work; docs/analysis/UI_DRIVERS.md lists the hooks and the stages that remove them): its screens paint
beside the original's, and a hook in each shared file sends the old draw entry points to them. Each stage
that replaces a hook with the driver contract lowers its count (S4 the loop hooks, S10b the draw bypass)
and the last one empties the file.

The number stays tight, like tools/net_boundary.json and tools/feature_sprawl.json: a file with MORE
findings than its count fails ("grew"), a file with FEWER fails too ("lower the allowance"), and an
allowed file with none is "stale" and the entry goes. `--update` rewrites the counts of the files already
listed and drops the stale ones; it never adds a file, because a new allowance is a decision and the
decision has a reason. The allowance file is the only thing this script ever writes: never anything
under src/.

Exit status: 0 clean, 1 findings, 2 the tree cannot be read (no src/, no [ui.<id>] in the manifest, an
allowance file that is not JSON): a gate that cannot look must not pass.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import json
import os
import posixpath
import re
import sys

try:
    import tomllib
except ModuleNotFoundError:                    # Python < 3.11: the CI and the dev machines have it
    sys.exit("check_ui_boundary: needs Python 3.11 or newer (tomllib)")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_net_boundary as cnb               # noqa: E402  (its source stripper: one lexer for both gates)


class CannotRead(Exception):
    """The tree cannot be read (no src/, no manifest, an allowance file that is not JSON): exit status 2."""


DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RULES = ("identity", "no-ifdef", "include-caps", "cross-driver", "driver-hook")
EXTS = cnb.EXTS
UI_DIR = "src/ui/"                   # the drivers' directory: the identity rule is let off here
ANGULO_DIR = "src/ui/angulo/"        # the Ângulo driver: cross-driver and driver-hook look at who is outside it
CONFIG = "src/simut_config.h"        # includes ui/UiCaps.h: let off the identity rule only
CAPS = "src/ui/UiCaps.h"             # defines the macros: no include of its own is asked for, and its defined( ) and
                                     # #ifndef are the guard against a -D and the default (let off no-ifdef too)
CONFIG_HEADERS = ("simut_config.h", "UiCaps.h")
ALLOWANCES = "tools/ui_boundary.json"
MANIFEST = "tools/features.toml"

# What only the original panel draws with: an Ângulo unit that included one would link the original's drawing
# into the driver that is meant to replace it. Matched by the file's name, however the include spells the path
# (src/ is on the include path, so a bare "Themes.h" reaches it from anywhere).
ORIGINAL_ONLY = ("Themes.h", "UiWidgets.h", "DisplayManager_Fonts.h", "PasswordKeyboard.h", "SensorDrawing.h")

UI_MACRO = r"SIMUT_UI_\w+"
USE = re.compile(r"\b" + UI_MACRO)
# `#ifdef X` and its siblings, at the start of a line (what is before the `#` is blanks or a stripped comment);
# `defined X` and `defined( X )`. Backslashes are let through between the words: a continued line is one directive.
IFDEF = re.compile(r"(?m)^[ \t]*#[ \t]*(?:ifdef|ifndef|elifdef|elifndef)\b[\s\\]*(" + UI_MACRO + ")")
DEFINED = re.compile(r"\bdefined\b[\s\\(]*(" + UI_MACRO + ")")

# The Angulo driver's own names (driver-hook). A name is looked for as a whole identifier, and an identifier is
# split at its underscores: `_uaSujo`, `g_uaVm` and `m_uaSujo` have the parts "uaSujo" and "uaVm".
WORD = re.compile(r"[A-Za-z_]\w*")
MARK = re.compile(r"(?:ua|Ua)[A-Z]\w*\Z")        # a part the driver marks as its own: uaSujo (a function, a member), UaToque (a type)
UA_MACRO = re.compile(r"UA_\w+\Z")               # the macros are the one pattern: UA_DESVIO is defined in a shared header
NS_TOKEN = re.compile(r"\bnamespace\s+(\w+)(?:\s*::\s*\w+)*\s*\{|\bnamespace\s*\{|[{}]")   # `namespace x {` and the braces


# ── the rules ──────────────────────────────────────────────────────────────────────────────────

Finding = collections.namedtuple("Finding", "path line rule text")


def manifest_identities(root: str):
    """The identity macros of the manifest: SIMUT_UI_<ID> for each [ui.<id>]."""
    path = os.path.join(root, MANIFEST)
    try:
        with open(path, "rb") as fh:
            data = tomllib.load(fh)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise CannotRead(f"cannot read {MANIFEST}: {e}")
    ui = data.get("ui", {})
    ids = sorted(ui) if isinstance(ui, dict) else []
    if not ids:
        raise CannotRead(f"{MANIFEST} has no [ui.<id>]: the identity rule would look for nothing")
    return ["SIMUT_UI_" + i.upper().replace("-", "_") for i in ids]


def identity_regex(macros) -> re.Pattern:
    return re.compile(r"\b(?:" + "|".join(re.escape(m) for m in macros) + r")\b")


Vocabulary = collections.namedtuple("Vocabulary", "names spaces")
Env = collections.namedtuple("Env", "ident vocab ns_use")


def marker_parts(word: str):
    """The parts of `word`, split at its underscores, that carry the driver's mark."""
    if "ua" not in word and "Ua" not in word:
        return []
    return [p for p in word.split("_") if MARK.match(p)]


def top_level_namespaces(code: str):
    """The namespaces declared at brace depth 0 (`ua`, but not the `tela` inside it)."""
    depth, names = 0, set()
    for m in NS_TOKEN.finditer(code):
        tok = m.group()
        if tok == "{":
            depth += 1
        elif tok == "}":
            depth = max(depth - 1, 0)
        else:                                  # a namespace opens: it is a top-level one if nothing encloses it
            if m.group(1) and depth == 0:
                names.add(m.group(1))
            depth += 1
    return names


def driver_vocabulary(codes) -> Vocabulary:
    """The names the Angulo driver introduces, from its own text (`codes`: path -> source with comments and strings
    stripped): every marked part of every identifier under src/ui/angulo/, and its top-level namespaces."""
    names, spaces = set(), set()
    for rel, code in codes.items():
        if not rel.startswith(ANGULO_DIR):
            continue
        for m in WORD.finditer(code):
            names.update(marker_parts(m.group()))
        spaces |= top_level_namespaces(code)
    return Vocabulary(frozenset(names), frozenset(spaces))


def namespace_regex(spaces):
    if not spaces:
        return None
    alt = "|".join(re.escape(n) for n in sorted(spaces))
    return re.compile(r"\b(" + alt + r")\s*::|\busing\s+namespace\s+(" + alt + r")\b")


def hook_sites(code: str, env: Env):
    """(offset, finding text) of every use of a name of the driver in `code`."""
    for m in WORD.finditer(code):
        w = m.group()
        if UA_MACRO.match(w):
            yield m.start(), f"{w} is a UA_ macro of the Angulo driver"
        else:
            hit = [p for p in marker_parts(w) if p in env.vocab.names]
            if hit:
                yield m.start(), f"{w} is a name the Angulo driver introduces"
    if env.ns_use is not None:
        for m in env.ns_use.finditer(code):
            yield m.start(), f"{m.group(1) or m.group(2)} is a namespace of the Angulo driver"


def is_original_only(name: str) -> bool:
    return name.rsplit("/", 1)[-1] in ORIGINAL_ONLY


def reaches_angulo(rel: str, name: str, quoted: bool) -> bool:
    """The include resolves to a file under src/ui/angulo/: relative to the including file (a quoted include
    only), or from src/ (-I src, for quoted and angle includes alike)."""
    from_src = posixpath.normpath(posixpath.join("src", name))
    if from_src.startswith(ANGULO_DIR):
        return True
    return quoted and posixpath.normpath(posixpath.join(posixpath.dirname(rel), name)).startswith(ANGULO_DIR)


def scan_text(rel: str, text: str, code: str, includes, env: Env):
    """The findings of one file, before any allowance. `code` and `includes` are cnb.strip_source(text)."""
    newlines = [m.start() for m in re.finditer("\n", code)]
    lines = text.split("\n")          # a finding shows the line as written, strings and all

    def line_of(k: int) -> int:
        return bisect.bisect_left(newlines, k) + 1

    def ctx(line: int) -> str:
        return " ".join(lines[line - 1].split())

    in_ui = rel.startswith(UI_DIR)
    in_angulo = rel.startswith(ANGULO_DIR)
    found = []

    if not in_ui and rel != CONFIG:
        for m in env.ident.finditer(code):
            ln = line_of(m.start())
            found.append(Finding(rel, ln, "identity",
                                 f"{m.group()} is the driver's identity, read only inside src/ui/ and src/simut_config.h: {ctx(ln)}"))

    if rel != CAPS:
        for pat in (IFDEF, DEFINED):
            for m in pat.finditer(code):
                ln = line_of(m.start())
                found.append(Finding(rel, ln, "no-ifdef",
                                     f"{m.group(1)} is defined on every image (0 or 1), so testing that it is defined "
                                     f"says nothing; use #if: {ctx(ln)}"))

        use = USE.search(code)
        if use:
            first = line_of(use.start())
            own = [ln for ln, name, quoted in includes
                   if quoted and name.rsplit("/", 1)[-1] in CONFIG_HEADERS and ln < first]
            if not own:
                found.append(Finding(rel, first, "include-caps",
                                     f"{use.group()} is used with no #include of simut_config.h or ui/UiCaps.h "
                                     f"of this file before it: {ctx(first)}"))

    for ln, name, quoted in includes:
        shown = f'#include "{name}"' if quoted else f"#include <{name}>"
        if in_angulo:
            if is_original_only(name):
                found.append(Finding(rel, ln, "cross-driver",
                                     f"{shown}: the Angulo driver includes a header only the original uses"))
        elif reaches_angulo(rel, name, quoted):
            found.append(Finding(rel, ln, "cross-driver",
                                 f"{shown}: a header of the Angulo driver included from outside src/ui/angulo/"))

    if not in_angulo:
        for pos, what in hook_sites(code, env):
            ln = line_of(pos)
            found.append(Finding(rel, ln, "driver-hook", f"{what}, used outside src/ui/angulo/: {ctx(ln)}"))
    return found


def load_tree(root: str):
    """{path: (text, code, includes)} for every source and header under src/, in path order."""
    src = os.path.join(root, "src")
    if not os.path.isdir(src):
        raise CannotRead(f"no src/ under {root}")
    tree = {}
    for dirpath, dirs, files in os.walk(src):
        dirs.sort()
        for f in sorted(files):
            if not f.endswith(EXTS):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            code, includes = cnb.strip_source(text)
            tree[rel] = (text, code, includes)
    return tree


def scan(root: str):
    ident = identity_regex(manifest_identities(root))
    tree = load_tree(root)
    if not any(rel.startswith(ANGULO_DIR) for rel in tree):
        raise CannotRead(f"no source under {ANGULO_DIR}: the Angulo driver's own names cannot be taken from it, and "
                         f"a gate with no names to look for would pass on anything")
    vocab = driver_vocabulary({rel: v[1] for rel, v in tree.items()})
    env = Env(ident, vocab, namespace_regex(vocab.spaces))
    found = []
    for rel, (text, code, includes) in tree.items():
        found.extend(scan_text(rel, text, code, includes, env))
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
    "identity": "ask a capability (SIMUT_UI_CUSTOM_THEMES) or a driver hook, or move the code into the driver's directory under src/ui/",
    "no-ifdef": "test it with `#if SIMUT_UI_...`: after src/ui/UiCaps.h every one is defined, 0 or 1, so #ifdef is true on every image",
    "include-caps": 'add #include "simut_config.h" (or "ui/UiCaps.h") to the file itself, before the first use',
    "cross-driver": "a driver reaches the core, never the other driver: take the shared part into the core, or leave it out",
    "driver-hook": "src/ui/angulo/ introduces the name: use it from there, or list the transitional site in tools/ui_boundary.json",
}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="The UI drivers' boundary: see the module docstring.")
    ap.add_argument("--root", default=DEFAULT_ROOT, help="the tree to look at (default: this repository)")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--update", action="store_true", help="rewrite the counts of the allowances already listed")
    mode.add_argument("--report", action="store_true", help="print every finding and the counts, exit 0")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.root)
    try:
        found = scan(root)
        allowances = load_allowances(root)
    except CannotRead as e:
        print(f"check_ui_boundary: {e}", file=sys.stderr)
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
            print(f"  {rule:<13} {path}  {k}{allowed}")
        per_rule = collections.Counter(f.rule for f in found)
        print("\nfindings per rule: " + ", ".join(f"{r} {per_rule.get(r, 0)}" for r in RULES))
        print(f"check_ui_boundary: {len(found)} findings")
        return 0

    if args.update:
        changed = update_allowances(root, allowances, found)
        print("\n".join(changed) if changed else "check_ui_boundary: the counts already match")
        return 0

    left = apply_allowances(found, allowances)
    for f in left:
        print(f"{f.path}:{f.line}: {f.rule}: {f.text}")
    if left:
        for rule in RULES:
            if any(f.rule == rule for f in left):
                print(f"check_ui_boundary: {rule}: {HINTS[rule]}", file=sys.stderr)
        print(f"check_ui_boundary: an allowance is tools/ui_boundary.json, written by hand with its reason "
              f"(see the docstring of tools/check_ui_boundary.py)", file=sys.stderr)
    print(f"check_ui_boundary: {len(left)} findings")
    return 1 if left else 0


if __name__ == "__main__":
    sys.exit(main())
