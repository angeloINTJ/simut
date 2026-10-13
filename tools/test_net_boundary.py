#!/usr/bin/env python3
"""Tests tools/check_net_boundary.py, the gate that keeps a network chip's code in src/net/.

Run: python3 tools/test_net_boundary.py   (CI runs it as a gate, next to the gate itself)

A gate that finds nothing is the failure this test exists for, so each rule is proved the way
a rule is proved: a violation put into a clean tree gives exactly ONE finding, of that rule,
at its line; the same text where the compiler sees no code (a comment, a string, a raw string)
gives none; and what is fine on any lwIP interface (WiFiClient, WiFiUDP, ...) gives none.

  * Synthetic trees, one per case (a temp directory with its own src/, tools/features.toml and
    tools/net_boundary.json): the four rules, their lexical traps (a '"' character literal, a
    digit separator, "//" in a string, a comment continued by a backslash, a raw string), the
    allowances (over, under, stale, no reason, unknown rule), --update, --report, the
    manifest as the source of the identity macros, and the output format.
  * The real tree: it passes, and one violation of each rule appended to a copy of it gives
    exactly one finding. The counts in tools/net_boundary.json are held tight on the real
    files too: one site more in an allowed file fails ("grew"), one less fails ("lower the
    allowance").
  * The gate never writes under src/: the sha256 of every scanned file is the same before and
    after every run, --update included (it writes tools/net_boundary.json and nothing else).
"""
import contextlib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GATE = os.path.join(HERE, "check_net_boundary.py")
sys.path.insert(0, HERE)

FAILS = []
RAN = []


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


try:
    import check_net_boundary as cnb           # noqa: E402
except ImportError as e:                       # the gate is written after this test: see it fail first
    check("tools/check_net_boundary.py exists and imports", False, str(e))
    print(f"\n{len(FAILS)} of {len(RAN)} checks failed")
    sys.exit(1)

# ── a synthetic tree ────────────────────────────────────────────────────────────────────────

MANIFEST = """[net_class.wifi]

[net_class.lan]

[net.cyw43]
class = "wifi"

[net.enc28j60]
class = "lan"
"""

CAPS = """#pragma once
#if defined(SIMUT_NET_LAN) || defined(SIMUT_NET_WIFI)
#error "derived, never passed"
#endif
#ifndef SIMUT_NET_ENC28J60
#define SIMUT_NET_ENC28J60 0
#endif
#ifndef SIMUT_NET_CYW43
#define SIMUT_NET_CYW43 (!SIMUT_NET_ENC28J60)
#endif
#define SIMUT_NET_WIFI (SIMUT_NET_CYW43)
#define SIMUT_NET_LAN (SIMUT_NET_ENC28J60)
"""

CONFIG = """#pragma once
#include "net/NetCaps.h"
#ifndef SIMUT_ENC_CS
#define SIMUT_ENC_CS 13
#endif
#if SIMUT_NET_ENC28J60
#define SIMUT_PINS_ENC (1ull << SIMUT_ENC_CS)
#endif
#if SIMUT_NET_LAN
#endif
"""

# Everything the rules forbid outside src/net/ is what src/net/ is made of.
DRIVER = """#include <WiFi.h>
#include <pico/cyw43_arch.h>
#include <PicoENC28J60lwIP.h>
#include <LwipIntfDev.h>
#include "NetCaps.h"
void f( ) { WiFi.status( ); cyw43_arch_init( ); _cyw43_hal_generate_laa_mac(0, 0); }
#if SIMUT_NET_CYW43
#endif
#if SIMUT_NET_ENC28J60
int pins = SIMUT_ENC_CS + SIMUT_PINS_ENC;
#endif
"""

SURFACE = "src/Surface.cpp"


def surface(*body, head=('#include "simut_config.h"',)):
    """A source file that includes simut_config.h on line 1; body line k is on line len(head) + k + 1."""
    return "\n".join(list(head) + list(body)) + "\n"


BASE = {
    "src/simut_config.h": CONFIG,
    "src/net/NetCaps.h": CAPS,
    "src/net/Driver.cpp": DRIVER,
    SURFACE: surface("WiFiClient c;"),
}


def allow(**rules):
    """allow(**{"radio-api": {"src/X.cpp": (2, "reason")}}) -> the JSON text of tools/net_boundary.json."""
    data = {r: {p: {"count": n, "reason": why} for p, (n, why) in files.items()} for r, files in rules.items()}
    return json.dumps(data, indent=1) + "\n"


@contextlib.contextmanager
def tree(files=None, allowances=None, manifest=MANIFEST, base=BASE):
    """A temp root with the base files overridden/extended by `files`; yields its path."""
    with tempfile.TemporaryDirectory(prefix="net_boundary_") as root:
        merged = dict(base)
        merged.update(files or {})
        for rel, text in merged.items():
            put(root, rel, text)
        if manifest is not None:
            put(root, "tools/features.toml", manifest)
        if allowances is not None:
            put(root, "tools/net_boundary.json", allowances)
        yield root


def put(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def run(root, *args):
    """The gate as the CI runs it, from a directory that is not the repository's."""
    r = subprocess.run([sys.executable, "-B", GATE, "--root", root, *args], capture_output=True, text=True,
                       cwd=tempfile.gettempdir())
    return r.returncode, r.stdout.splitlines(), r.stderr


LINE = re.compile(r"^(?P<path>[^:\s]+):(?P<line>\d+): (?P<rule>[a-z-]+): (?P<text>.+)$")


def parse(out):
    """(findings as (path, line, rule), the last line, the lines that are not in the output format)."""
    found, bad = [], []
    for l in out[:-1]:
        m = LINE.match(l)
        if m:
            found.append((m["path"], int(m["line"]), m["rule"]))
        else:
            bad.append(l)
    return found, (out[-1] if out else ""), bad


def file_hash(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def tree_hashes(root):
    h = {}
    for dirpath, _d, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            with open(p, "rb") as fh:
                h[os.path.relpath(p, root)] = hashlib.sha256(fh.read()).hexdigest()
    return h


def expect(name, files, want, allowances=None, manifest=MANIFEST):
    """The gate over a tree gives exactly the findings `want` ((path, line, rule), any order), in the output format."""
    with tree(files, allowances, manifest) as root:
        before = tree_hashes(os.path.join(root, "src"))
        rc, out, err = run(root)
        after = tree_hashes(os.path.join(root, "src"))
    found, last, bad = parse(out)
    ok = (sorted(found) == sorted(want) and rc == (1 if want else 0) and not bad
          and last == f"check_net_boundary: {len(want)} findings" and before == after)
    check(name, ok, f"want {sorted(want)} rc={1 if want else 0}; got {sorted(found)} rc={rc} last={last!r} "
                    f"bad={bad} err={err.strip()[:200]} src unchanged={before == after}")


# ── the stripper: comments and the contents of strings and characters go, the lines stay ──────

def strip_cases():
    def code_of(text):
        code, inc = cnb.strip_source(text)
        return code, inc

    t = "a // WiFi.x\nb\n"
    code, _ = code_of(t)
    check("strip: a line comment is blanked and its newline stays",
          len(code) == len(t) and code.count("\n") == 2 and "WiFi" not in code and code.startswith("a "))

    t = "a /* WiFi.x\n WiFi.y\n */ WiFi.z\n"
    code, _ = code_of(t)
    check("strip: a block comment over three lines keeps three lines and the code after it",
          len(code) == len(t) and code.count("\n") == 3 and "WiFi.x" not in code and "WiFi.y" not in code
          and "WiFi.z" in code and code.splitlines()[2].strip() == "WiFi.z")

    t = 'x = "WiFi.begin(";\n'
    code, _ = code_of(t)
    check("strip: the contents of a string go, the quotes stay", "WiFi" not in code and code.count('"') == 2
          and len(code) == len(t))

    t = "char c = '\"'; WiFi.begin( );\n"
    code, _ = code_of(t)
    check("strip: a '\"' character literal does not open a string", "WiFi.begin" in code)

    t = "int n = 1'000; WiFi.begin( );\n"        # one apostrophe: read as a character literal it would run to the end of the line
    code, _ = code_of(t)
    check("strip: a digit separator is not a character literal", "WiFi.begin" in code)
    t = "int h = 0xFF'FF; int n = 1'000'000; WiFi.begin( );\n"
    code, _ = code_of(t)
    check("strip: hex and repeated digit separators are not character literals either", "WiFi.begin" in code)

    t = 'const char* s = "http://x"; WiFi.begin( );\n'
    code, _ = code_of(t)
    check('strip: "//" inside a string is not a comment', "WiFi.begin" in code)

    t = 'const char* s = "a\\"WiFi.x"; WiFi.end( );\n'
    code, _ = code_of(t)
    check("strip: an escaped quote does not end the string", "WiFi.x" not in code and "WiFi.end" in code)

    t = 'auto r = R"x(a )" WiFi.begin( ) )x"; WiFi.end( );\n'
    code, _ = code_of(t)
    check("strip: a raw string ends at its own delimiter", "WiFi.begin" not in code and "WiFi.end" in code)

    t = 'auto r = u8R"(WiFi.x\nWiFi.y)"; WiFi.end( );\n'
    code, _ = code_of(t)
    check("strip: a prefixed raw string over two lines keeps the lines",
          "WiFi.x" not in code and "WiFi.y" not in code and "WiFi.end" in code and code.count("\n") == 2)

    t = 'auto s = FOOR"WiFi.x"; WiFi.end( );\n'
    code, _ = code_of(t)
    check("strip: an R at the end of an identifier does not start a raw string",
          "WiFi.x" not in code and "WiFi.end" in code)

    t = "// continued \\\nWiFi.x\nWiFi.y\n"
    code, _ = code_of(t)
    check("strip: a line comment continued by a backslash takes the next line too",
          "WiFi.x" not in code and "WiFi.y" in code and code.count("\n") == 3)

    t = "#error don't\nWiFi.end( );\n"
    code, _ = code_of(t)
    check("strip: an unpaired apostrophe ends at the end of its line", "WiFi.end" in code)

    t = '#include <WiFi.h>\n#  include "net/NetCaps.h"\n// #include <a.h>\nconst char* s = "#include <b.h>";\n' \
        'auto r = R"(\n#include <c.h>\n)";\n'
    code, inc = code_of(t)
    check("strip: #include directives are kept apart from the code and nothing inside a comment, a string or a "
          "raw string is one",
          inc == [(1, "WiFi.h", False), (2, "net/NetCaps.h", True)] and "WiFi.h" not in code
          and "NetCaps" not in code and "#include" in code,
          f"includes={inc}")


def real_strip_cases():
    """The stripper over the real sources: same length, same lines, and no comment marker left in the code."""
    wrong, left = [], []
    for dirpath, _d, files in os.walk(os.path.join(ROOT, "src")):
        for f in sorted(files):
            if not f.endswith(cnb.EXTS):
                continue
            path = os.path.join(dirpath, f)
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            code, _inc = cnb.strip_source(text)
            if len(code) != len(text) or code.count("\n") != text.count("\n"):
                wrong.append(os.path.relpath(path, ROOT))
            if any(t in code for t in ("//", "/*", "*/")):
                left.append(os.path.relpath(path, ROOT))
    check("strip: every real source file keeps its length and its lines", not wrong, str(wrong[:5]))
    check("strip: no comment marker is left in the code of any real source file", not left, str(left[:5]))


# ── the cases ────────────────────────────────────────────────────────────────────────────────

# A violation of each rule, as text for a surface file: exactly one finding of that rule.
TEXT = {
    "identity": ["#if SIMUT_NET_CYW43", "#ifdef SIMUT_NET_ENC28J60", "int cs = SIMUT_ENC_CS;",
                 "unsigned long long m = SIMUT_PINS_ENC;", "#if defined( SIMUT_NET_CYW43 )"],
    "radio-api": ["WiFi.RSSI( );", "int s = WiFi .status( );", "cyw43_arch_init( );",
                  "_cyw43_hal_generate_laa_mac(0, mac);", "#include <pico/cyw43_arch.h>",
                  '#include "pico/cyw43_arch.h"'],
    "driver-library": ["#include <PicoENC28J60lwIP.h>", "#include <PicoENC28J60.h>", "#include <W5500lwIP.h>",
                       '#include "ENC28J60lwIP.h"', "#include <LwipIntfDev.h>", "#include <lwIP_WINC1500.h>"],
}

for rule, lines in TEXT.items():
    for line in lines:
        expect(f"{rule}: `{line}` in a surface file is one finding",
               {SURFACE: surface(line)}, [(SURFACE, 2, rule)])


def class_include_cases():
    NOHEAD = ()
    for use in ("#if SIMUT_NET_LAN", "#if SIMUT_NET_WIFI", "#ifndef SIMUT_NET_LAN", "#if defined(SIMUT_NET_WIFI)",
                "#if !SIMUT_NET_LAN", "bool b = SIMUT_NET_LAN || x;"):
        text = surface(use, "#endif" if use.startswith("#") else "", head=NOHEAD)
        expect(f"class-include: `{use}` in a file that includes nothing is one finding",
               {SURFACE: text}, [(SURFACE, 1, "class-include")])
    expect("class-include: three uses in one file are one finding, at the first",
           {SURFACE: surface("int a;", "#if SIMUT_NET_LAN", "#endif", "#if SIMUT_NET_WIFI", "#endif", head=NOHEAD)},
           [(SURFACE, 2, "class-include")])
    expect("class-include: the include after the first use is too late",
           {SURFACE: surface("#if SIMUT_NET_LAN", "#endif", '#include "simut_config.h"', head=NOHEAD)},
           [(SURFACE, 1, "class-include")])
    expect("class-include: simut_config.h reached through another header is not the file's own include",
           {SURFACE: surface('#include "AppManager.h"', "#if SIMUT_NET_LAN", "#endif", head=NOHEAD),
            "src/AppManager.h": '#include "simut_config.h"\n'},
           [(SURFACE, 2, "class-include")])
    expect("class-include: an include in a comment is not an include",
           {SURFACE: surface('// #include "simut_config.h"', "#if SIMUT_NET_LAN", "#endif", head=NOHEAD)},
           [(SURFACE, 2, "class-include")])
    for inc, path in (('#include "simut_config.h"', SURFACE), ('#include "net/NetCaps.h"', SURFACE),
                      ('#include "../simut_config.h"', "src/ota/Apply.cpp"),
                      ('#include "../net/NetCaps.h"', "src/ota/Apply.cpp"),
                      ('#include "NetCaps.h"', "src/net/Other.h")):
        expect(f"class-include: `{inc}` before the use satisfies it ({path})",
               {path: surface(inc, "#if SIMUT_NET_LAN", "#endif", head=NOHEAD)}, [])


# The same text where the compiler sees no code gives nothing.
def inert_cases():
    every = [t for lines in TEXT.values() for t in lines] + ["#if SIMUT_NET_LAN", "#if SIMUT_NET_WIFI"]
    # a file with no include at all: the class rule would fire on code, so it proves the class rule too
    bare = ()
    def wrap(kind):
        if kind == "line comment":
            return [f"// {t}" for t in every]
        if kind == "block comment":
            return ["/*"] + every + ["*/"] + [f"/* {t} */" for t in every]
        if kind == "string":
            return [f'const char* s{i} = "{t.replace(chr(34), chr(92) + chr(34))}";' for i, t in enumerate(every)]
        if kind == "raw string":
            return [f'const char* r{i} = R"x({t})x";' for i, t in enumerate(every)]
        if kind == "raw string over lines":
            return ['const char* r = R"x('] + every + [')x";']
    for kind in ("line comment", "block comment", "string", "raw string", "raw string over lines"):
        expect(f"nothing in a {kind}: every rule's text is inert", {SURFACE: surface(*wrap(kind), head=bare)}, [])


def fine_cases():
    expect("WiFiClient, WiFiUDP, WiFiServer and WiFiClientSecure are fine outside src/net/ (any lwIP interface)",
           {SURFACE: surface("#include <WiFi.h>", "#include <WiFiClient.h>", "#include <WiFiUdp.h>",
                             "#include <WiFiServer.h>", "#include <WiFiClientSecure.h>",
                             "WiFiClient c; WiFiUDP u; WiFiServer s(80); WiFiClientSecure sc;",
                             "void g( ) { c.connect(\"h\", 80); u.begin(0); s.begin( ); sc.setInsecure( ); }")}, [])
    expect("src/net/ may hold all four kinds of text, and so may src/simut_config.h's identity and pin macros",
           {}, [])
    expect("an identity macro that is not in the manifest is not an identity",
           {SURFACE: surface("#if SIMUT_NET_W5500", "#endif")}, [])


def lexical_cases():
    expect("lexing: a '\"' character literal does not hide code after it",
           {SURFACE: surface("char q = '\"'; WiFi.begin( );")}, [(SURFACE, 2, "radio-api")])
    expect("lexing: a digit separator does not hide code after it",
           {SURFACE: surface("int n = 1'000; WiFi.begin( );")}, [(SURFACE, 2, "radio-api")])
    expect('lexing: "//" in a string does not hide code after it',
           {SURFACE: surface('const char* u = "http://x"; WiFi.begin( );')}, [(SURFACE, 2, "radio-api")])
    expect("lexing: an escaped quote does not end the string early",
           {SURFACE: surface('const char* e = "a\\"WiFi.x"; WiFi.begin( );')}, [(SURFACE, 2, "radio-api")])
    expect("lexing: a comment continued by a backslash hides the next line, and not the one after",
           {SURFACE: surface("// x \\", "WiFi.begin( );", "WiFi.end( );")}, [(SURFACE, 4, "radio-api")])
    expect("lexing: the line of a finding after a block comment is the file's line",
           {SURFACE: surface("/* a", "b */", "WiFi.end( );")}, [(SURFACE, 4, "radio-api")])
    expect("lexing: the line of a finding after a raw string over three lines is the file's line",
           {SURFACE: surface('const char* r = R"x(', "a", ')x";', "WiFi.end( );")}, [(SURFACE, 5, "radio-api")])
    expect("lexing: an apostrophe in a #error does not hide the next line",
           {SURFACE: surface("#error don't", "WiFi.end( );")}, [(SURFACE, 3, "radio-api")])
    expect("lexing: two sites on one line are two findings",
           {SURFACE: surface("WiFi.mode(1); WiFi.begin( );")}, [(SURFACE, 2, "radio-api")] * 2)
    expect("lexing: the finding is where the radio is called, in a long file",
           {SURFACE: surface(*(["int x;"] * 200 + ["WiFi.end( );"]))}, [(SURFACE, 202, "radio-api")])


def manifest_cases():
    expect("the identity macros come from the manifest: a [net.w5500] makes SIMUT_NET_W5500 one",
           {SURFACE: surface("#if SIMUT_NET_W5500", "#endif")}, [(SURFACE, 2, "identity")],
           manifest=MANIFEST + '\n[net.w5500]\nclass = "lan"\n')
    expect("the identity macros come from the manifest: an id with a hyphen is upper-cased with an underscore",
           {SURFACE: surface("#if SIMUT_NET_SOME_CHIP", "#endif")}, [(SURFACE, 2, "identity")],
           manifest=MANIFEST + '\n[net.some-chip]\nclass = "lan"\n')
    with tree({}, manifest=None) as root:
        rc, out, err = run(root)
    check("a tree with no manifest is an error (exit 2), not a clean pass",
          rc == 2 and "features.toml" in (err + "\n".join(out)), f"rc={rc} out={out} err={err.strip()[:200]}")
    with tree({}, manifest="[net_class.lan]\n") as root:
        rc, out, err = run(root)
    check("a manifest with no [net.<id>] is an error (exit 2): the identity rule would look for nothing",
          rc == 2 and "[net." in (err + "\n".join(out)), f"rc={rc} out={out} err={err.strip()[:200]}")
    with tree({}, base={}) as root:
        rc, out, err = run(root)
    check("a tree with no src/ is an error (exit 2), not a clean pass", rc == 2, f"rc={rc} out={out} err={err.strip()[:200]}")


# ── the allowances ───────────────────────────────────────────────────────────────────────────

TWO = {SURFACE: surface("WiFi.mode(1);", "WiFi.begin( );")}      # two radio-api findings, lines 2 and 3
WHY = "the radio's state machine lives here until it moves to src/net/"


def message_cases():
    def problem(name, files, allowances, words, rule="radio-api", path=SURFACE):
        with tree(files, allowances) as root:
            rc, out, err = run(root)
        _found, _last, bad = parse(out)
        mine = [l for l in out[:-1] if l.startswith(path + ":") and f": {rule}: " in l]
        ok = rc == 1 and not bad and any(all(w in l for w in words) for l in mine)
        check(name, ok, f"rc={rc} out={out}")

    expect("an allowance equal to the findings makes them pass", TWO, [],
           allowances=allow(**{"radio-api": {SURFACE: (2, WHY)}}))
    problem("an allowance below the findings fails: grew", TWO, allow(**{"radio-api": {SURFACE: (1, WHY)}}), ["grew"])
    problem("an allowance above the findings fails: lower the allowance", TWO,
            allow(**{"radio-api": {SURFACE: (3, WHY)}}), ["lower the allowance"])
    problem("an allowed file with no findings is stale", {SURFACE: surface("int x;")},
            allow(**{"radio-api": {SURFACE: (1, WHY)}}), ["stale"])
    problem("an allowed file that is gone is stale", {},
            allow(**{"radio-api": {"src/Gone.cpp": (1, WHY)}}), ["stale"], path="src/Gone.cpp")
    problem("an allowance for the other rule does not cover the finding, and is stale itself", TWO,
            allow(**{"identity": {SURFACE: (2, WHY)}}), ["stale"], rule="identity")
    expect("an allowance for the other rule leaves the findings reported",
           TWO, [(SURFACE, 2, "radio-api"), (SURFACE, 3, "radio-api"), (SURFACE, 1, "identity")],
           allowances=allow(**{"identity": {SURFACE: (2, WHY)}}))
    problem("an allowance with no reason fails", TWO, allow(**{"radio-api": {SURFACE: (2, "")}}), ["reason"])
    problem("an allowance with a count that is not a positive number fails", TWO,
            allow(**{"radio-api": {SURFACE: (0, WHY)}}), ["count"])
    problem("an allowance under a rule that does not exist fails", TWO,
            allow(**{"made-up-rule": {SURFACE: (2, WHY)}}), ["unknown rule"], rule="made-up-rule")


def update_cases():
    def read(root):
        with open(os.path.join(root, "tools", "net_boundary.json"), encoding="utf-8") as fh:
            return json.load(fh)

    # lowers a count, keeps the reason
    with tree(TWO, allow(**{"radio-api": {SURFACE: (3, WHY)}})) as root:
        before = tree_hashes(root)
        rc, out, err = run(root, "--update")
        after = tree_hashes(root)
        data = read(root)
        rc2, out2, _ = run(root)
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    check("--update lowers a count to the findings and keeps the reason",
          rc == 0 and data == {"radio-api": {SURFACE: {"count": 2, "reason": WHY}}} and rc2 == 0,
          f"rc={rc} data={data} rc2={rc2} out={out}")
    check("--update writes tools/net_boundary.json and nothing else",
          changed == [os.path.join("tools", "net_boundary.json")], f"changed={changed}")

    # never adds a file
    files = dict(TWO)
    files["src/Other.cpp"] = surface("WiFi.begin( );")
    with tree(files, allow(**{"radio-api": {SURFACE: (3, WHY)}})) as root:
        rc, out, err = run(root, "--update")
        data = read(root)
        rc2, out2, _ = run(root)
    found, last, _bad = parse(out2)
    check("--update never adds a file: a new allowance is written by hand, with its reason",
          rc == 0 and list(data["radio-api"]) == [SURFACE] and data["radio-api"][SURFACE]["count"] == 2
          and rc2 == 1 and found == [("src/Other.cpp", 2, "radio-api")],
          f"rc={rc} data={data} rc2={rc2} found={found}")

    # drops what is stale
    with tree({SURFACE: surface("int x;")}, allow(**{"radio-api": {SURFACE: (1, WHY)}})) as root:
        rc, out, err = run(root, "--update")
        data = read(root)
        rc2, out2, _ = run(root)
    check("--update drops an allowance whose file has no findings left", rc == 0 and data == {} and rc2 == 0,
          f"rc={rc} data={data} rc2={rc2}")

    # a bad file is not rewritten into something that looks fine
    with tree(TWO, allow(**{"radio-api": {SURFACE: (2, "")}})) as root:
        rc, out, err = run(root, "--update")
        data = read(root)
    check("--update does not invent a reason", data["radio-api"][SURFACE]["reason"] == "", f"data={data}")


def report_cases():
    with tree(TWO, allow(**{"radio-api": {SURFACE: (1, WHY)}})) as root:
        rc, out, err = run(root, "--report")
    text = "\n".join(out)
    check("--report prints the findings and the counts and exits 0, even over an allowance",
          rc == 0 and f"{SURFACE}:2: radio-api:" in text and f"{SURFACE}:3: radio-api:" in text
          and re.search(r"radio-api\s+" + re.escape(SURFACE) + r"\s+2\b", text) is not None
          and out[-1] == "check_net_boundary: 2 findings", f"rc={rc} out={out}")


def format_cases():
    files = {SURFACE: surface("WiFi.begin( );", "#include <PicoENC28J60lwIP.h>", "#if SIMUT_NET_CYW43", "#endif"),
             "src/Bare.cpp": "#if SIMUT_NET_LAN\n#endif\n"}
    with tree(files) as root:
        rc, out, err = run(root)
    found, last, bad = parse(out)
    rules = sorted(r for _p, _l, r in found)
    check("the output is `path:line: rule: text` per finding and `check_net_boundary: N findings` last",
          rc == 1 and not bad and rules == ["class-include", "driver-library", "identity", "radio-api"]
          and last == "check_net_boundary: 4 findings", f"rc={rc} out={out}")
    check("the findings come in path and line order",
          [(p, l) for p, l, _r in found] == sorted((p, l) for p, l, _r in found), f"found={found}")


# ── the real tree ───────────────────────────────────────────────────────────────────────────

def real_tree_cases():
    src_before = tree_hashes(os.path.join(ROOT, "src"))
    json_before = file_hash(os.path.join(ROOT, "tools", "net_boundary.json"))
    r = subprocess.run([sys.executable, "-B", GATE], capture_output=True, text=True, cwd=tempfile.gettempdir())
    check("the real tree passes (the gate with no --root, from another directory)",
          r.returncode == 0 and r.stdout.splitlines()[-1:] == ["check_net_boundary: 0 findings"],
          r.stdout[-600:] + r.stderr[-300:])
    r2 = subprocess.run([sys.executable, "-B", GATE, "--report"], capture_output=True, text=True,
                        cwd=tempfile.gettempdir())
    check("--report on the real tree exits 0", r2.returncode == 0, r2.stdout[-300:] + r2.stderr[-300:])
    src_after = tree_hashes(os.path.join(ROOT, "src"))
    json_after = file_hash(os.path.join(ROOT, "tools", "net_boundary.json"))
    check("the gate never writes under src/ (sha256 of the scanned files before and after), nor the allowances "
          "without --update", src_before == src_after and json_before == json_after,
          str([k for k in src_after if src_before.get(k) != src_after.get(k)][:5]))

    # a copy of the real tree: one violation of each rule appended to a file, one finding each
    with tempfile.TemporaryDirectory(prefix="net_boundary_real_") as root:
        shutil.copytree(os.path.join(ROOT, "src"), os.path.join(root, "src"))
        os.makedirs(os.path.join(root, "tools"))
        for f in ("features.toml", "net_boundary.json"):
            shutil.copy(os.path.join(ROOT, "tools", f), os.path.join(root, "tools", f))
        base_hashes = tree_hashes(os.path.join(root, "src"))

        def mutated(rel, extra, want_rule, name, expected_findings=1):
            path = os.path.join(root, "src", rel)
            with open(path, encoding="utf-8", newline="") as fh:
                original = fh.read()
            try:
                with open(path, "w", encoding="utf-8", newline="") as fh:
                    fh.write(original + extra)
                rc, out, err = run(root)
            finally:
                with open(path, "w", encoding="utf-8", newline="") as fh:
                    fh.write(original)
            found, last, bad = parse(out)
            nlines = original.count("\n") + 1
            ok = (rc == 1 and not bad and len(found) == expected_findings
                  and all(r == want_rule for _p, _l, r in found) and last == f"check_net_boundary: {expected_findings} findings")
            check(name, ok, f"rc={rc} found={found} last={last!r} (the appended text starts on line {nlines})")
            return found, nlines

        found, n = mutated("SyslogManager.cpp", "\n#if SIMUT_NET_ENC28J60\n#endif\n", "identity",
                           "real tree + an identity macro in SyslogManager.cpp: one finding")
        check("  ... and it is on the appended line", found[0][:2] == ("src/SyslogManager.cpp", n + 1) if found else False,
              str(found))
        mutated("SyslogManager.cpp", "\nstatic int boundary_probe = WiFi.RSSI( );\n", "radio-api",
                "real tree + `WiFi.RSSI( )` in SyslogManager.cpp: one finding")
        mutated("SyslogManager.cpp", "\n#include <PicoENC28J60lwIP.h>\n", "driver-library",
                "real tree + a driver-library include in SyslogManager.cpp: one finding")

        # a class test in a file that includes neither simut_config.h nor net/NetCaps.h
        bare = None
        for dirpath, _d, files in sorted(os.walk(os.path.join(root, "src"))):
            for f in sorted(files):
                p = os.path.join(dirpath, f)
                rel = os.path.relpath(p, os.path.join(root, "src"))
                if rel.startswith("net" + os.sep) or not f.endswith((".h", ".cpp")):
                    continue
                with open(p, encoding="utf-8", errors="ignore") as fh:
                    code, inc = cnb.strip_source(fh.read())
                if not re.search(r"\bSIMUT_NET_(WIFI|LAN)\b", code) and not any(
                        os.path.basename(n) in ("simut_config.h", "NetCaps.h") for _l, n, _q in inc):
                    bare = rel.replace(os.sep, "/")
                    break
            if bare:
                break
        check("the real tree has a file with no class test and no config include to put the class mutation in",
              bare is not None)
        if bare:
            mutated(bare, "\n#if SIMUT_NET_LAN\n#endif\n", "class-include",
                    f"real tree + a class test in {bare} (which includes neither): one finding")

        # the counts are tight on the real files
        with open(os.path.join(root, "tools", "net_boundary.json"), encoding="utf-8") as fh:
            allowed = json.load(fh)
        ra = allowed.get("radio-api", {})
        if ra:
            rel = max(ra, key=lambda p: ra[p]["count"])
            short = rel[len("src/"):]
            path = os.path.join(root, "src", short)
            with open(path, encoding="utf-8", newline="") as fh:
                original = fh.read()
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(original + "\nstatic int boundary_probe = WiFi.RSSI( );\n")
            rc, out, _e = run(root)
            check(f"real tree: one radio call more in {rel}, which is allowed, fails: grew",
                  rc == 1 and any("grew" in l and l.startswith(rel + ":") for l in out), f"{out[-3:]}")
            lines = original.split("\n")
            code_lines = cnb.strip_source(original)[0].split("\n")
            for i, l in enumerate(code_lines):
                if re.search(r"\bWiFi\s*\.", l):
                    lines[i] = "int removed_site = 0;"
                    break
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write("\n".join(lines))
            rc, out, _e = run(root)
            check(f"real tree: one radio call fewer in {rel}, which is allowed, fails: lower the allowance",
                  rc == 1 and any("lower the allowance" in l and l.startswith(rel + ":") for l in out), f"{out[-3:]}")
            rc, out, _e = run(root, "--update")
            rc2, out2, _e2 = run(root)
            check("real tree: --update takes the lower count and the gate passes again", rc == 0 and rc2 == 0,
                  f"{out[-3:]} {out2[-3:]}")
            with open(path, "w", encoding="utf-8", newline="") as fh:
                fh.write(original)
        else:
            check("the real tools/net_boundary.json allows some radio calls (NetworkManager.cpp)", False)
        after = tree_hashes(os.path.join(root, "src"))
        check("the copy's sources are back as they were after the mutations (the instrument is sound)",
              {k: v for k, v in after.items() if k in base_hashes} == base_hashes)


# ── run ────────────────────────────────────────────────────────────────────────────────────

strip_cases()
real_strip_cases()
class_include_cases()
inert_cases()
fine_cases()
lexical_cases()
manifest_cases()
message_cases()
update_cases()
report_cases()
format_cases()
real_tree_cases()

print(f"\n{len(RAN) - len(FAILS)} of {len(RAN)} checks passed")
if FAILS:
    print("FAILED:\n  " + "\n  ".join(FAILS))
    sys.exit(1)
