#!/usr/bin/env python3
"""Tests tools/check_ui_boundary.py, the gate that keeps each touch-panel UI driver inside its walls.

Run: python3 tools/test_ui_boundary.py   (CI runs it as a gate, next to the gate itself)

A gate that finds nothing is the failure this test exists for, so each rule is proved the way a rule
is proved: a violation put into a clean tree gives exactly ONE finding, of that rule, at its line; the
same text where the compiler sees no code (a comment, a string, a raw string) gives none; and what is
fine (a capability, a driver's own text inside its directory, the defined( ) of UiCaps.h) gives none.

  * Synthetic trees, one per case (a temp directory with its own src/, tools/features.toml and
    tools/ui_boundary.json): the five rules (identity, no-ifdef, include-caps, cross-driver,
    driver-hook), what each lets off, the lexical traps that matter to them, the allowances (over,
    under, stale, no reason, unknown rule, a file that is not JSON), --update, --report, the manifest
    as the source of the identity macros, and the output format.
  * The premise of no-ifdef, with the host preprocessor: after the real src/ui/UiCaps.h,
    `#ifdef SIMUT_UI_ANGULO` is true on the original image and `#if SIMUT_UI_ANGULO` is false.
  * The real tree: it passes (zero findings over the allowances of tools/ui_boundary.json, each of
    which says which stage ends it), and one violation of each rule appended to a copy of it gives
    exactly one finding. The counts are held tight on a real file too: the copy is given an allowance
    of its own (two hook sites in a core file, allowed two, so that the check does not depend on
    which allowances the real file still lists), one site more fails ("grew"), one less fails
    ("lower the allowance"), and --update lowers it.
  * The gate never writes under src/: the sha256 of every scanned file is the same before and after
    every run, --update included (it writes tools/ui_boundary.json and nothing else).
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
GATE = os.path.join(HERE, "check_ui_boundary.py")
sys.path.insert(0, HERE)

FAILS = []
RAN = []


def check(name, cond, detail=""):
    RAN.append(name)
    tag = "ok  " if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" - {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


try:
    import check_ui_boundary as cub            # noqa: E402
except ImportError as e:                       # see it fail first, if the gate is ever lost
    check("tools/check_ui_boundary.py exists and imports", False, str(e))
    print(f"\n{len(FAILS)} of {len(RAN)} checks failed")
    sys.exit(1)

# ── a synthetic tree ────────────────────────────────────────────────────────────────────────

MANIFEST = """[ui.original]

[ui.angulo]
flags = ["-DSIMUT_UI_ANGULO=1"]
"""

CAPS = """#pragma once
#if defined(SIMUT_UI_ORIGINAL) || defined(SIMUT_UI_CUSTOM_THEMES)
#error "derived, never passed"
#endif
#ifndef SIMUT_UI_ANGULO
#define SIMUT_UI_ANGULO 0
#endif
#define SIMUT_UI_ORIGINAL (!SIMUT_UI_ANGULO)
#define SIMUT_UI_CUSTOM_THEMES (SIMUT_UI_ORIGINAL)
"""

CONFIG = """#pragma once
#include "ui/UiCaps.h"
#if SIMUT_UI_ANGULO && !SIMUT_DISPLAY_TFT
#error "SIMUT_UI_ANGULO needs SIMUT_DISPLAY_TFT"
#endif
"""

SURFACE = "src/Surface.cpp"
ANGULO_UNIT = "src/ui/angulo/Glue.cpp"


def surface(*body, head=('#include "simut_config.h"',)):
    """A source file that includes simut_config.h on line 1; body line k is on line len(head) + k + 1."""
    return "\n".join(list(head) + list(body)) + "\n"


def angulo(*body, head=('#include "../../simut_config.h"',)):
    """An Angulo unit that includes simut_config.h on line 1 (from src/ui/angulo/)."""
    return surface(*body, head=head)


# What the original's headers are derived from (docs/analysis/UI_DRIVERS_funcoes.tsv, the S0 classification of the units,
# and the "Original driver." item of docs/analysis/UI_DRIVERS.md section 2.5), as a toy: three units that are all
# original, one that is mixed, one that is core only, the class (shared), and two header-only files the item names.
TSV_HEAD = "unit\tline\tend\tsymbol\tclass\toriginal_lines\tcore_lines\tnote\n"
TSV_ROWS = (("DisplayManager.h", "mixed"), ("DisplayManager.cpp", "core"), ("DisplayManager.cpp", "original"),
            ("DisplayManager_Alarm.cpp", "core"), ("DisplayManager_Alarm.cpp", "original"),
            ("DisplayManager_Auth.cpp", "core"),
            ("Themes.cpp", "original"), ("UiWidgets.cpp", "original"), ("DisplayManager_Fonts.cpp", "original"))
OWNERSHIP_ITEM = ("- **Original driver.** Every `draw*` and `redraw*`, `render( )`; the geometry `CARD_*`\n"
                  "  (`DisplayManager.h:1095-1098`); `Themes.cpp`, `DisplayManager_Fonts.*`, `UiWidgets.*`,\n"
                  "  `PasswordKeyboard.h` and `sensors/SensorDrawing.h`.\n")
TSV_PATH = "docs/analysis/UI_DRIVERS_funcoes.tsv"
DOC_PATH = "docs/analysis/UI_DRIVERS.md"


def tsv(*rows, head=TSV_HEAD):
    """A classification table: one row per (unit, class)."""
    return head + "".join(f"{u}\t1\t2\tsym\t{c}\t\t\t\n" for u, c in rows)


def doc(item=OWNERSHIP_ITEM):
    """The ownership section with the "Original driver." item as given."""
    return ("### 2.5 Ownership\n\n- **Core.** The Core 1 lifecycle (`DisplayManager.cpp:160-1240`) and `DisplayManager_Auth.cpp`.\n"
            + item + "- **The mixed units** are settled by section 3.3.\n")


TOY_HEADERS = ("src/DisplayManager.h", "src/Themes.h", "src/UiWidgets.h", "src/DisplayManager_Fonts.h",
               "src/PasswordKeyboard.h", "src/sensors/SensorDrawing.h")
TOY_UNITS = ("src/DisplayManager.cpp", "src/DisplayManager_Alarm.cpp", "src/DisplayManager_Auth.cpp", "src/Themes.cpp",
             "src/UiWidgets.cpp", "src/DisplayManager_Fonts.cpp")


# Everything the rules forbid outside the driver is what the driver is made of: its own unit holds the
# identity macros, the hook names and the macro, and the clean tree has none of them anywhere else.
ANGULO_TEXT = ("#if SIMUT_UI_ANGULO", "#endif",
               "void DisplayManager::uaSujo( ) { _uaPintando = true; UA_DESVIO( ); }",
               "bool DisplayManager::uaToque( int &x, int &y ) { return false; }",
               "void DisplayManager::uaQuadro( ) { }",
               "void DisplayManager::uaOta( ) { }",
               "void DisplayManager::uaTemaAplicado( ) { }",
               "const ua::Tema &DisplayManager::uaTema( ) { }",
               "const char *uaAberturaLicenca( ) { return 0; }")
# What the driver declares in its header: a namespace and a type per kind of name, a global, a template, a macro,
# a namespace of its own that is not `ua`, and a namespace nested in `ua` (which is no top-level name).
ANGULO_HEADER = "src/ui/angulo/UiA.h"
ANGULO_HEADER_TEXT = """#pragma once
namespace ua {
struct Tema;
namespace tela {
}
}
namespace entrega {
}
struct UaQuadro { int x; };
enum class UaToque { NAO_E_MEU, TRATADO };
#define UA_VM_MAX 4096
extern int g_uaVm[UA_VM_MAX];
template <class T> T &uaVm( );
void registerUiARoutes( );
void handleApiUiA( );
"""

BASE = {
    "src/simut_config.h": CONFIG,
    "src/ui/UiCaps.h": CAPS,
    ANGULO_UNIT: angulo(*ANGULO_TEXT),
    ANGULO_HEADER: ANGULO_HEADER_TEXT,
    TSV_PATH: tsv(*TSV_ROWS),
    DOC_PATH: doc(),
    SURFACE: surface("int themes = SIMUT_UI_CUSTOM_THEMES;"),
    **{path: "#pragma once\n" for path in TOY_HEADERS},
    **{path: "int unit;\n" for path in TOY_UNITS},
}


def allow(**rules):
    """allow(**{"driver-hook": {"src/X.cpp": (2, "reason")}}) -> the JSON text of tools/ui_boundary.json."""
    data = {r: {p: {"count": n, "reason": why} for p, (n, why) in files.items()} for r, files in rules.items()}
    return json.dumps(data, indent=1, ensure_ascii=False) + "\n"


@contextlib.contextmanager
def tree(files=None, allowances=None, manifest=MANIFEST, base=BASE):
    """A temp root with the base files overridden/extended by `files`; yields its path."""
    with tempfile.TemporaryDirectory(prefix="ui_boundary_") as root:
        merged = dict(base)
        merged.update(files or {})
        for rel, text in merged.items():
            put(root, rel, text)
        if manifest is not None:
            put(root, "tools/features.toml", manifest)
        if allowances is not None:
            put(root, "tools/ui_boundary.json", allowances)
        yield root


def put(root, rel, text):
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as fh:
        fh.write(text)


def run(root, *args):
    """The gate as the CI runs it, from a directory that is not the repository's."""
    r = subprocess.run([sys.executable, "-B", GATE, "--root", root, *args], capture_output=True, text=True,
                       encoding="utf-8", cwd=tempfile.gettempdir())
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
          and last == f"check_ui_boundary: {len(want)} findings" and before == after)
    check(name, ok, f"want {sorted(want)} rc={1 if want else 0}; got {sorted(found)} rc={rc} last={last!r} "
                    f"bad={bad} err={err.strip()[:200]} src unchanged={before == after}")


# ── the clean tree, and a rule at a time ─────────────────────────────────────────────────────

expect("the clean base tree (the driver's own text inside src/ui/angulo/, a capability outside) gives nothing", {}, [])

# A violation of each rule as text for a surface file that includes simut_config.h on line 1: exactly one
# finding, on line 2. (A rule's text in an Angulo unit, where the other rules are let off, is below.)
TEXT = {
    "identity": ["#if SIMUT_UI_ANGULO", "#if SIMUT_UI_ORIGINAL", "#elif SIMUT_UI_ANGULO", "#if !SIMUT_UI_ORIGINAL",
                 "bool a = SIMUT_UI_ANGULO;", "#if SIMUT_UI_ANGULO && SIMUT_DISPLAY_TFT"],
    "cross-driver": ['#include "ui/angulo/UiA.h"', "#include <ui/angulo/UiA.h>", '#include "ui/angulo/kit/angulo_tokens.h"'],
    "driver-hook": ["UA_DESVIO( );", "uaSujo( );", "if (uaToque(x, y)) return;", "uaQuadro( );", "uaOta( );",
                    "uaTemaAplicado( );", "uaTema( );", "bool b = _uaPintando;", "int g = g_uaVm[0];",
                    "const char *uaAberturaLicenca( ) { return 0; }",
                    "void (DisplayManager::*p)( ) = &DisplayManager::uaQuadro;",
                    # the review of 2026-10-14 measured these as 0 findings: a member prefix other than `_` and `g_`,
                    # and a UA_ macro other than UA_DESVIO
                    "int m_uaSujo = 0;", "#define UA_FOO 1", "int f = UA_FOO;",
                    # the driver's types and namespaces: names it introduces as much as its functions
                    "UaQuadro q;", "bool b = ( t == UaToque::TRATADO );", "char *p = (char *)&g_uaVm;",
                    "const ua::Tema *p = nullptr;", "using namespace entrega;", "int n = entrega::count;",
                    "w.registerUiARoutes( );", "void handleApiUiA( );"],
}

for rule, lines in TEXT.items():
    for line in lines:
        expect(f"{rule}: `{line}` in a core file is one finding",
               {SURFACE: surface(line)}, [(SURFACE, 2, rule)])

# no-ifdef: in an Angulo unit, where identity and driver-hook are let off and the include is there.
NOIFDEF = ["#ifdef SIMUT_UI_ANGULO", "#ifndef SIMUT_UI_ORIGINAL", "#  ifdef SIMUT_UI_ANGULO", "# ifndef SIMUT_UI_CUSTOM_THEMES",
           "#if defined(SIMUT_UI_ANGULO)", "#if defined( SIMUT_UI_ORIGINAL )", "#if defined SIMUT_UI_ANGULO",
           "#if SIMUT_AIR || defined(SIMUT_UI_ANGULO)", "#elif defined(SIMUT_UI_CUSTOM_THEMES)",
           "#elifdef SIMUT_UI_ANGULO", "#elifndef SIMUT_UI_ANGULO", "/* c */ #ifdef SIMUT_UI_ANGULO",
           "#if defined(SIMUT_UI_ANGULO_NEXT)"]
for line in NOIFDEF:
    expect(f"no-ifdef: `{line}` in an Angulo unit is one finding",
           {ANGULO_UNIT: angulo(line)}, [(ANGULO_UNIT, 2, "no-ifdef")])

expect("no-ifdef: two defined( ) on one line are two findings",
       {ANGULO_UNIT: angulo("#if defined(SIMUT_UI_ANGULO) || defined(SIMUT_UI_ORIGINAL)")},
       [(ANGULO_UNIT, 2, "no-ifdef")] * 2)
expect("no-ifdef: #ifdef on a macro that is not a UI one is fine",
       {SURFACE: surface("#ifdef SIMUT_THEMES_HEALTH", "#endif", "#if defined(SIMUT_NET_LAN) || defined(SIMUT_UIX)",
                         "#endif", "#ifndef SIMUT_UI", "#endif")}, [])
expect("no-ifdef: the same text in a core file is two findings, the macro being an identity there too",
       {SURFACE: surface("#ifdef SIMUT_UI_ANGULO")}, [(SURFACE, 2, "identity"), (SURFACE, 2, "no-ifdef")])
expect("no-ifdef: a capability in #ifdef in a core file is one finding (a capability is no identity)",
       {SURFACE: surface("#ifdef SIMUT_UI_CUSTOM_THEMES")}, [(SURFACE, 2, "no-ifdef")])
expect("no-ifdef: src/simut_config.h, let off the identity rule, is held to this one",
       {"src/simut_config.h": CONFIG + "#ifdef SIMUT_UI_ANGULO\n#endif\n"}, [("src/simut_config.h", 6, "no-ifdef")])
expect("no-ifdef: src/ui/UiCaps.h, where the macros are defined, is the one file let off (its defined( ) and #ifndef)",
       {"src/ui/UiCaps.h": CAPS + "#ifdef SIMUT_UI_ANGULO\n#endif\n#if defined(SIMUT_UI_CUSTOM_THEMES)\n#endif\n"}, [])
expect("no-ifdef: that exemption is the file's, not the directory's",
       {"src/ui/Other.h": '#include "UiCaps.h"\n' + "#ifdef SIMUT_UI_ANGULO\n#endif\n"}, [("src/ui/Other.h", 2, "no-ifdef")])
expect("identity: a longer macro name that starts with an identity is not the identity (it is a UI macro, and nothing else)",
       {SURFACE: surface("#if SIMUT_UI_ANGULO_NEXT", "#endif")}, [])
expect("identity and include-caps: a name that merely ends in a UI macro's name is neither an identity nor a use",
       {SURFACE: surface("int NOT_SIMUT_UI_ANGULO = 0; int MY_SIMUT_UI_CUSTOM_THEMES = 0;", head=())}, [])
expect("no-ifdef: a name that merely contains `defined` is not the operator",
       {ANGULO_UNIT: angulo("bool undefined_SIMUT_UI_ANGULO = false;", "int predefined(SIMUT_UI_ANGULO);")}, [])


def class_include_cases():
    NOHEAD = ()
    for use in ("#if SIMUT_UI_CUSTOM_THEMES", "#if !SIMUT_UI_CUSTOM_THEMES", "#if SIMUT_UI_CUSTOM_THEMES && x",
                "bool b = SIMUT_UI_CUSTOM_THEMES || x;"):
        text = surface(use, "#endif" if use.startswith("#") else "", head=NOHEAD)
        expect(f"include-caps: `{use}` in a file that includes nothing is one finding",
               {SURFACE: text}, [(SURFACE, 1, "include-caps")])
    expect("include-caps: an identity macro in a bare core file is two findings, the identity and the include",
           {SURFACE: surface("#if SIMUT_UI_ANGULO", "#endif", head=())},
           [(SURFACE, 1, "identity"), (SURFACE, 1, "include-caps")])
    expect("include-caps: an Angulo unit is held to it too (its identity is no finding there, the missing include is)",
           {ANGULO_UNIT: surface("#if SIMUT_UI_ANGULO", "#endif", head=())}, [(ANGULO_UNIT, 1, "include-caps")])
    expect("include-caps: three uses in one file are one finding, at the first",
           {SURFACE: surface("int a;", "#if SIMUT_UI_CUSTOM_THEMES", "#endif", "#if SIMUT_UI_CUSTOM_THEMES", "#endif", head=NOHEAD)},
           [(SURFACE, 2, "include-caps")])
    expect("include-caps: the include after the first use is too late",
           {SURFACE: surface("#if SIMUT_UI_CUSTOM_THEMES", "#endif", '#include "simut_config.h"', head=NOHEAD)},
           [(SURFACE, 1, "include-caps")])
    expect("include-caps: simut_config.h reached through another header is not the file's own include",
           {SURFACE: surface('#include "DisplayManager.h"', "#if SIMUT_UI_CUSTOM_THEMES", "#endif", head=NOHEAD),
            "src/DisplayManager.h": '#include "simut_config.h"\n'},
           [(SURFACE, 2, "include-caps")])
    expect("include-caps: an angle-bracket include is not the file's own (the project's headers are quoted)",
           {SURFACE: surface("#include <simut_config.h>", "#if SIMUT_UI_CUSTOM_THEMES", "#endif", head=NOHEAD)},
           [(SURFACE, 2, "include-caps")])
    expect("include-caps: an include in a comment is not an include",
           {SURFACE: surface('// #include "simut_config.h"', "#if SIMUT_UI_CUSTOM_THEMES", "#endif", head=NOHEAD)},
           [(SURFACE, 2, "include-caps")])
    expect("include-caps: a use in a comment or a string asks for nothing",
           {SURFACE: surface("// #if SIMUT_UI_CUSTOM_THEMES", 'const char *s = "SIMUT_UI_ANGULO";', head=NOHEAD)}, [])
    for inc, path in (('#include "simut_config.h"', SURFACE), ('#include "ui/UiCaps.h"', SURFACE),
                      ('#include "../simut_config.h"', "src/ota/Apply.cpp"),
                      ('#include "../ui/UiCaps.h"', "src/ota/Apply.cpp"),
                      ('#include "UiCaps.h"', "src/ui/Other.h"),
                      ('#include "../UiCaps.h"', ANGULO_UNIT),
                      ('#include "../../simut_config.h"', ANGULO_UNIT)):
        expect(f"include-caps: `{inc}` before the use satisfies it ({path})",
               {path: surface(inc, "#if SIMUT_UI_CUSTOM_THEMES", "#endif", head=())}, [])


def cross_driver_cases():
    for inc in ('#include "../../Themes.h"', '#include "../../UiWidgets.h"', '#include "../../DisplayManager_Fonts.h"',
                '#include "../../PasswordKeyboard.h"', '#include "../../sensors/SensorDrawing.h"',
                '#include "Themes.h"', '#include "sensors/SensorDrawing.h"', "#include <UiWidgets.h>"):
        expect(f"cross-driver: `{inc}` in an Angulo unit is one finding",
               {ANGULO_UNIT: angulo(inc)}, [(ANGULO_UNIT, 2, "cross-driver")])
    for inc in ('#include "../../DisplayManager.h"', '#include "UiA.h"', '#include "telas/Base.h"',
                '#include "../../PinKeypad.h"', '#include "../../sensors/SensorChannels.h"'):
        expect(f"cross-driver: `{inc}` in an Angulo unit (the core, or its own) is fine",
               {ANGULO_UNIT: angulo(inc)}, [])
    expect("cross-driver: the original's headers are the core's and the original's to include",
           {SURFACE: surface('#include "Themes.h"', '#include "UiWidgets.h"', '#include "DisplayManager_Fonts.h"',
                             '#include "PasswordKeyboard.h"', '#include "sensors/SensorDrawing.h"',
                             '#include "ui/UiCaps.h"')}, [])
    for path, inc in ((SURFACE, '#include "ui/angulo/UiA.h"'), (SURFACE, "#include <ui/angulo/UiA.h>"),
                      ("src/ota/Apply.cpp", '#include "../ui/angulo/UiA.h"'),
                      ("src/ui/Other.cpp", '#include "angulo/UiA.h"'),
                      ("src/ui/Other.cpp", '#include "../ui/angulo/UiA.h"')):
        expect(f"cross-driver: `{inc}` in {path} is one finding (resolved the way the compiler does)",
               {path: surface(inc)}, [(path, 2, "cross-driver")])
    expect("cross-driver: an angle-bracket include does not search the including file's directory",
           {"src/ui/Other.cpp": surface("#include <angulo/UiA.h>")}, [])
    expect("cross-driver: an include that merely looks like a path to the driver is not one",
           {SURFACE: surface('#include "../ui/angulo/UiA.h"', '#include "other/angulo/UiA.h"')}, [])


def hook_cases():
    expect("driver-hook: every hook name is the Angulo driver's own text inside src/ui/angulo/ (the clean tree has them)",
           {ANGULO_UNIT: angulo(*TEXT["driver-hook"])}, [])
    expect("driver-hook: names that only contain `ua` are not hooks",
           {SURFACE: surface("int manualSave = 0; String visualAlarm; int equalA = 1; bool uart = false; int UART_RX = 1;",
                             "void actualize( ); int quaternionA; int uaa = 0; int Uasomething = 0; int mua_Sujo = 0;",
                             "int sensorsuaSujo = 0; int fooua_Bar = 0;")}, [])
    expect("driver-hook: a name the driver introduces is a hook behind any prefix: `_`, `g_`, `m_`, `s_` (the review measured m_uaSujo as 0)",
           {SURFACE: surface("int _uaSujo = 0;", "int g_uaVm = 0;", "int m_uaSujo = 0;", "int s_uaSujo = 0;")},
           [(SURFACE, 2, "driver-hook"), (SURFACE, 3, "driver-hook"), (SURFACE, 4, "driver-hook"), (SURFACE, 5, "driver-hook")])
    expect("driver-hook: every UA_ macro is the driver's, not only UA_DESVIO (the review measured UA_FOO as 0)",
           {SURFACE: surface("#define UA_FOO 1", "int x = UA_BAR_2 + UA_FOO;")},
           [(SURFACE, 2, "driver-hook"), (SURFACE, 3, "driver-hook"), (SURFACE, 3, "driver-hook")])
    expect("driver-hook: a name that looks like the driver's but is not one it introduces is no hook (a user agent)",
           {SURFACE: surface('String uaString = server.header("User-Agent");', 'bool curl = uaString.startsWith("curl");',
                             "int uaCount = 0; char *UaBuf = nullptr;")}, [])
    expect("driver-hook: ... and the same file with a real hook in it is one finding, the hook's",
           {SURFACE: surface("String uaString = x;", "uaSujo( );", "bool c = uaString.isEmpty( );")}, [(SURFACE, 3, "driver-hook")])
    # the verdict follows the driver's text, not an edit of the gate
    novo = "void DisplayManager::uaNovo( ) { }"
    expect("driver-hook: a name the driver starts to introduce is a hook at once (uaNovo, m_uaNovo, UaNovo)",
           {ANGULO_UNIT: angulo(*ANGULO_TEXT, novo, "struct UaNovoTipo { };"),
            SURFACE: surface("uaNovo( );", "int m_uaNovo = 0;", "UaNovoTipo t;")},
           [(SURFACE, 2, "driver-hook"), (SURFACE, 3, "driver-hook"), (SURFACE, 4, "driver-hook")])
    expect("driver-hook: ... and the same core file is clean while the driver does not introduce it",
           {SURFACE: surface("uaNovo( );", "int m_uaNovo = 0;", "UaNovoTipo t;")}, [])
    expect("driver-hook: a name that the driver only mentions in a comment or a string is not one it introduces",
           {ANGULO_UNIT: angulo(*ANGULO_TEXT, "// void uaFantasma( );", 'const char *s = "uaFantasma";'),
            SURFACE: surface("uaFantasma( );")}, [])
    expect("driver-hook: the driver's namespaces are its top-level ones: `ua` and `entrega`, and not the `tela` nested in `ua`",
           {SURFACE: surface("const ua::Tema *a = nullptr;", "int n = entrega::count;", "int t = tela::count;",
                             "using namespace ua;", "using namespace tela;")},
           [(SURFACE, 2, "driver-hook"), (SURFACE, 3, "driver-hook"), (SURFACE, 5, "driver-hook")])
    expect("driver-hook: a variable that happens to be called `ua` is no namespace, and neither is a longer name that ends in one",
           {SURFACE: surface('String ua = "Mozilla"; bool b = ua.startsWith("M");', "int q = Aqua::count;",
                             "using namespace Aqua;")}, [])
    with tree({ANGULO_UNIT: surface("int x;")}, base={k: v for k, v in BASE.items() if k != ANGULO_HEADER}) as root:
        rc, out, err = run(root)
    check("driver-hook: a driver whose sources introduce no name with its mark is refused (exit 2), as a driver with no source is "
          "(the review measured DisplayManager.h falling from 61 to 2 findings with nothing said)",
          rc == 2 and "introduce no name" in err, f"rc={rc} out={out[-2:]} err={err.strip()[:200]}")
    expect("driver-hook: a name with the second mark, UiA and a capital or the end, that the driver defines is a hook in the core "
           "(registerUiARoutes, handleApiUiA: the review planted them and got 0 findings)",
           {SURFACE: surface("w.registerUiARoutes( );", "w.handleApiUiA( );", "int m_handleApiUiA = 0;")},
           [(SURFACE, 2, "driver-hook"), (SURFACE, 3, "driver-hook"), (SURFACE, 4, "driver-hook")])
    expect("driver-hook: ... but plain UiA is no mark (UiArrowDir, UiAction), and a UiA name the driver does not define is not one",
           {SURFACE: surface("UiArrowDir dir = UI_UP; int UiAction = 0; int someUiAThing = 0;")}, [])
    expect("driver-hook: ... and a string, a comment or an include path does not count; the include is the cross-driver rule's",
           {SURFACE: surface('const char *s = "registerUiARoutes";', "// handleApiUiA( )", '#include "ui/angulo/UiA.h"')},
           [(SURFACE, 4, "cross-driver")])
    for alias in ("namespace u = ua;", "namespace u = ::ua;", "namespace u = ua::tela;", "namespace e = entrega;"):
        expect(f"driver-hook: `{alias}` in a core file is one finding, at the alias (the review planted one and got 0)",
               {SURFACE: surface(alias)}, [(SURFACE, 2, "driver-hook")])
    expect("driver-hook: ... the use through the alias is not looked for: the alias line is the finding",
           {SURFACE: surface("namespace u = ua;", "u::Tema *t = nullptr;")}, [(SURFACE, 2, "driver-hook")])
    expect("driver-hook: an alias of a namespace that is not the driver's is none (std::filesystem, Aqua, uas)",
           {SURFACE: surface("namespace fs = std::filesystem;", "namespace u = Aqua;", "namespace v = uas;")}, [])
    expect("driver-hook: the rule is `outside src/ui/angulo/`: another file of src/ui/ is held to it",
           {"src/ui/Other.cpp": surface("uaSujo( );")}, [("src/ui/Other.cpp", 2, "driver-hook")])
    expect("driver-hook: two names on one line are two findings",
           {SURFACE: surface("uaSujo( ); uaQuadro( );")}, [(SURFACE, 2, "driver-hook")] * 2)
    with tree({}, base={k: v for k, v in BASE.items() if not k.startswith("src/ui/angulo/")}) as root:
        rc, out, err = run(root)
    check("driver-hook: without any source under src/ui/angulo/ there is no driver to take the names from (exit 2)",
          rc == 2 and "src/ui/angulo/" in (err + "\n".join(out)), f"rc={rc} out={out} err={err.strip()[:200]}")


# The same text where the compiler sees no code gives nothing.
def inert_cases():
    every = ([t for lines in TEXT.values() for t in lines] + NOIFDEF + ["#if SIMUT_UI_CUSTOM_THEMES"]
             + ['#include "../../Themes.h"', "#include <PasswordKeyboard.h>"])
    # a file with no include at all: the include rule would fire on code, so it proves that rule inert too
    bare = ()

    def wrap(kind):
        if kind == "line comment":
            return [f"// {t}" for t in every]
        if kind == "block comment":
            return ["/*"] + [t for t in every if "*/" not in t] + ["*/"] + [f"/* {t} */" for t in every if "/*" not in t]
        if kind == "string":
            return [f'const char* s{i} = "{t.replace(chr(34), chr(92) + chr(34))}";' for i, t in enumerate(every)]
        if kind == "raw string":
            return [f'const char* r{i} = R"x({t})x";' for i, t in enumerate(every)]
        if kind == "raw string over lines":
            return ['const char* r = R"x('] + every + [')x";']
    for kind in ("line comment", "block comment", "string", "raw string", "raw string over lines"):
        expect(f"nothing in a {kind}: every rule's text is inert", {SURFACE: surface(*wrap(kind), head=bare)}, [])
        expect(f"nothing in a {kind}, in an Angulo unit with no include",
               {ANGULO_UNIT: surface(*wrap(kind), head=bare)}, [])


def original_cases():
    """The headers only the original uses are derived (S0 classification, ownership item, manifest, src/ui/original/)."""
    hit = [(ANGULO_UNIT, 2, "cross-driver")]
    expect("cross-driver: a header named after a unit that has an original row is the original's, DisplayManager_Alarm.h "
           "(the review measured it as 0)", {ANGULO_UNIT: angulo('#include "../../DisplayManager_Alarm.h"')}, hit)
    expect("cross-driver: ... the same when that header exists",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Alarm.h"'), "src/DisplayManager_Alarm.h": "#pragma once\n"}, hit)
    expect("cross-driver: ... and spelled from src/", {ANGULO_UNIT: angulo('#include "DisplayManager_Alarm.h"')}, hit)
    expect("cross-driver: ... and so is a .hpp next to the unit", {ANGULO_UNIT: angulo('#include "../../DisplayManager_Alarm.hpp"')}, hit)
    expect("cross-driver: a header named after a unit with only core rows is the core's, DisplayManager_Auth.h",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Auth.h"')}, [])
    expect("cross-driver: the class header is shared although its unit has original rows, DisplayManager.h",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager.h"', '#include "DisplayManager.h"')}, [])
    expect("cross-driver: a unit the table gains is followed at once (Calendar gets an original row)",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Calendar.h"'),
            TSV_PATH: tsv(*TSV_ROWS, ("DisplayManager_Calendar.cpp", "original"))}, hit)
    expect("cross-driver: ... and one the table gives back (Alarm turns core only) is let go",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Alarm.h"'),
            TSV_PATH: tsv(*[r for r in TSV_ROWS if r != ("DisplayManager_Alarm.cpp", "original")])}, [])
    expect("cross-driver: a mixed row is enough (the table has no original row for the unit, only a mixed one)",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Touch.h"'),
            TSV_PATH: tsv(*TSV_ROWS, ("DisplayManager_Touch.cpp", "mixed"))}, hit)
    expect("cross-driver: everything under src/ui/original/ is the original's",
           {ANGULO_UNIT: angulo('#include "../original/UiOriginal.h"')}, hit)
    expect("cross-driver: ... spelled from src/ too", {ANGULO_UNIT: angulo('#include "ui/original/UiOriginal.h"')}, hit)
    expect("cross-driver: the core and the original may include what is under src/ui/original/",
           {SURFACE: surface('#include "ui/original/UiOriginal.h"')}, [])

    with_units = MANIFEST.replace("[ui.original]\n", '[ui.original]\nunits = ["DisplayManager_Graph.cpp"]\n', 1)
    expect("cross-driver: a unit [ui.original] lists makes its header the original's (S10b fills `units`)",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Graph.h"'), "src/DisplayManager_Graph.cpp": "int unit;\n"},
           hit, manifest=with_units)
    expect("cross-driver: ... and the same header is nobody's while the manifest lists nothing",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Graph.h"'), "src/DisplayManager_Graph.cpp": "int unit;\n"}, [])
    with_glob = MANIFEST.replace("[ui.original]\n", '[ui.original]\ninclude = ["DisplayManager_Gr*.cpp", "ui/original/*.cpp"]\n', 1)
    expect("cross-driver: ... by a glob of `include` too, over the units that exist",
           {ANGULO_UNIT: angulo('#include "../../DisplayManager_Graph.h"'), "src/DisplayManager_Graph.cpp": "int unit;\n"},
           hit, manifest=with_glob)

    keyboard = OWNERSHIP_ITEM.replace("`PasswordKeyboard.h`", "`PasswordKeyboard.h`, `Keyboard2.h`")
    expect("cross-driver: a header-only file the ownership item names is the original's without an edit of the gate",
           {ANGULO_UNIT: angulo('#include "../../Keyboard2.h"'), "src/Keyboard2.h": "#pragma once\n", DOC_PATH: doc(keyboard)}, hit)
    for tok, files in (("`Keyboard3.*`", {"src/Keyboard3.cpp": "int unit;\n"}), ("`Keyboard3.cpp`", {"src/Keyboard3.cpp": "int unit;\n"})):
        expect(f"cross-driver: the item names a stem with {tok} and its header is the original's, the table not knowing the unit",
               {ANGULO_UNIT: angulo('#include "../../Keyboard3.h"'), "src/Keyboard3.h": "#pragma once\n",
                DOC_PATH: doc(OWNERSHIP_ITEM.replace("`PasswordKeyboard.h`", "`PasswordKeyboard.h`, " + tok)), **files}, hit)
    expect("cross-driver: ... and the same file is nobody's while the item does not name it",
           {ANGULO_UNIT: angulo('#include "../../Keyboard2.h"'), "src/Keyboard2.h": "#pragma once\n"}, [])
    only_item = ("- **Original driver.** Every `draw*`; `Themes.cpp`, `UiWidgets.*`, `PasswordKeyboard.h` and `sensors/SensorDrawing.h`.\n")
    expect("cross-driver: the item is the only source for a header-only file: dropped from it, SensorDrawing.h is nobody's",
           {ANGULO_UNIT: angulo('#include "../../sensors/SensorDrawing.h"'),
            DOC_PATH: doc(only_item.replace(" and `sensors/SensorDrawing.h`", ""))}, [])

    def refuses(name, files, words, base=BASE, manifest=MANIFEST):
        with tree(files, None, manifest, base) as root:
            rc, out, err = run(root)
        check(name, rc == 2 and all(w in err for w in words), f"rc={rc} out={out} err={err.strip()[:300]}")

    def without(path):
        return {k: v for k, v in BASE.items() if k != path}

    refuses("cross-driver: no classification table is an error (exit 2), not a header list of nothing", {},
            ["UI_DRIVERS_funcoes.tsv"], base=without(TSV_PATH))
    refuses("cross-driver: a table without the `unit` and `class` columns is an error (exit 2)",
            {TSV_PATH: "a\tb\n1\t2\n"}, ["UI_DRIVERS_funcoes.tsv", "unit"])
    refuses("cross-driver: a table with no original or mixed row is an error (exit 2)",
            {TSV_PATH: tsv(("DisplayManager_Auth.cpp", "core"))}, ["UI_DRIVERS_funcoes.tsv", "original"])
    refuses("cross-driver: no ownership document is an error (exit 2)", {}, ["UI_DRIVERS.md"], base=without(DOC_PATH))
    refuses("cross-driver: an ownership document without the \"Original driver.\" item is an error (exit 2)",
            {DOC_PATH: "### 2.5 Ownership\n\n- **Core.** Everything else.\n"}, ["UI_DRIVERS.md", "Original driver"])
    refuses("cross-driver: an item that names no file is an error (exit 2)",
            {DOC_PATH: doc("- **Original driver.** Every `draw*` and `render( )`.\n")}, ["UI_DRIVERS.md", "names no file"])
    refuses("cross-driver: an item that names a header that is not in src/ is an error (exit 2): the contract and the code agree",
            {DOC_PATH: doc(OWNERSHIP_ITEM.replace("`PasswordKeyboard.h`", "`Gone.h`"))}, ["UI_DRIVERS.md", "Gone.h"])
    refuses("cross-driver: ... and so is a unit or a `X.*` whose files are all gone",
            {DOC_PATH: doc(OWNERSHIP_ITEM.replace("`UiWidgets.*`", "`Ghost.*`"))}, ["UI_DRIVERS.md", "Ghost"])


def extension_cases():
    """Every C/C++ source, header and include-only file is read: a hook or an identity in a .inl is as much a leak."""
    for ext in (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp"):
        path = "src/Planted" + ext
        expect(f"extensions: a hook in a core {ext} file is one finding", {path: surface("uaSujo( );")}, [(path, 2, "driver-hook")])
    expect("extensions: an identity macro in a core .inl is one finding (the review planted one and got 0)",
           {"src/Hooks.inl": surface("#if SIMUT_UI_ANGULO", "#endif")}, [("src/Hooks.inl", 2, "identity")])
    expect("extensions: a name that a driver .inl defines is a hook in the core (the review planted one and got 0)",
           {"src/ui/angulo/Extra.inl": angulo("void DisplayManager::uaExtra( ) { }"), SURFACE: surface("uaExtra( );")},
           [(SURFACE, 2, "driver-hook")])
    expect("extensions: a file that is not C or C++ is not read (.txt, .inc)",
           {"src/Notes.txt": "uaSujo( );\n#if SIMUT_UI_ANGULO\n", "src/Hooks.inc": "uaSujo( );\n"}, [])


def lexical_cases():
    expect("lexing: a '\"' character literal does not hide code after it",
           {SURFACE: surface("char q = '\"'; uaSujo( );")}, [(SURFACE, 2, "driver-hook")])
    expect("lexing: a digit separator does not hide code after it",
           {SURFACE: surface("int n = 1'000; uaSujo( );")}, [(SURFACE, 2, "driver-hook")])
    expect('lexing: "//" in a string does not hide code after it',
           {SURFACE: surface('const char* u = "http://x"; uaSujo( );')}, [(SURFACE, 2, "driver-hook")])
    expect("lexing: an escaped quote does not end the string early",
           {SURFACE: surface('const char* e = "a\\"uaSujo( )"; uaQuadro( );')}, [(SURFACE, 2, "driver-hook")])
    expect("lexing: a comment continued by a backslash hides the next line, and not the one after",
           {SURFACE: surface("// x \\", "uaSujo( );", "uaQuadro( );")}, [(SURFACE, 4, "driver-hook")])
    expect("lexing: the line of a finding after a block comment is the file's line",
           {SURFACE: surface("/* a", "b */", "uaQuadro( );")}, [(SURFACE, 4, "driver-hook")])
    expect("lexing: the line of a finding after a raw string over three lines is the file's line",
           {SURFACE: surface('const char* r = R"x(', "a", ')x";', "uaQuadro( );")}, [(SURFACE, 5, "driver-hook")])
    expect("lexing: an apostrophe in a #error does not hide the next line",
           {SURFACE: surface("#error don't", "uaQuadro( );")}, [(SURFACE, 3, "driver-hook")])
    expect("lexing: the finding is where the hook is called, in a long file",
           {SURFACE: surface(*(["int x;"] * 200 + ["uaQuadro( );"]))}, [(SURFACE, 202, "driver-hook")])
    expect("lexing: a #ifdef after a block comment on its line is still a directive",
           {ANGULO_UNIT: angulo("/* a */  #ifdef SIMUT_UI_ANGULO")}, [(ANGULO_UNIT, 2, "no-ifdef")])
    expect("lexing: a #ifdef in a line the compiler continues is read where it starts",
           {ANGULO_UNIT: angulo("#ifdef \\", "  SIMUT_UI_ANGULO")}, [(ANGULO_UNIT, 2, "no-ifdef")])


def manifest_cases():
    expect("the identity macros come from the manifest: a [ui.third] makes SIMUT_UI_THIRD one",
           {SURFACE: surface("#if SIMUT_UI_THIRD", "#endif")}, [(SURFACE, 2, "identity")],
           manifest=MANIFEST + '\n[ui.third]\nflags = ["-DSIMUT_UI_THIRD=1"]\n')
    expect("the identity macros come from the manifest: an id with a hyphen is upper-cased with an underscore",
           {SURFACE: surface("#if SIMUT_UI_SOME_DRIVER", "#endif")}, [(SURFACE, 2, "identity")],
           manifest=MANIFEST + '\n[ui.some-driver]\nflags = []\n')
    expect("an identity macro that is not in the manifest is not an identity (it is still a UI macro)",
           {SURFACE: surface("#if SIMUT_UI_THIRD", "#endif")}, [])
    expect("the tables that merely start with `ui_` (the configurator's) are not drivers",
           {SURFACE: surface("#if SIMUT_UI_PRODUCTS", "#endif")}, [],
           manifest=MANIFEST + '\n[ui_products.pico_wiretft]\nbench = true\n')
    with tree({}, manifest=None) as root:
        rc, out, err = run(root)
    check("a tree with no manifest is an error (exit 2), not a clean pass",
          rc == 2 and "features.toml" in (err + "\n".join(out)), f"rc={rc} out={out} err={err.strip()[:200]}")
    with tree({}, manifest="[ui_panel]\n") as root:
        rc, out, err = run(root)
    check("a manifest with no [ui.<id>] is an error (exit 2): the identity rule would look for nothing",
          rc == 2 and "[ui." in (err + "\n".join(out)), f"rc={rc} out={out} err={err.strip()[:200]}")
    with tree({}, manifest='ui = "angulo"\n') as root:
        rc, out, err = run(root)
    check("a manifest whose `ui` is not a table of drivers is an error (exit 2)", rc == 2, f"rc={rc} out={out} err={err.strip()[:200]}")
    with tree({}, manifest="[ui.original\n") as root:
        rc, out, err = run(root)
    check("a manifest that is not TOML is an error (exit 2)", rc == 2, f"rc={rc} out={out} err={err.strip()[:200]}")
    with tree({}, base={}) as root:
        rc, out, err = run(root)
    check("a tree with no src/ is an error (exit 2), not a clean pass",
          rc == 2 and "no src/" in err, f"rc={rc} out={out} err={err.strip()[:200]}")
    for text, what in (("{", "not JSON"), ('["driver-hook"]', "a JSON array"), ('{"driver-hook": 3}', "a rule that is not an object")):
        with tree({}, allowances=text) as root:
            rc, out, err = run(root)
        want_rc = 1 if what == "a rule that is not an object" else 2
        check(f"an allowance file that is {what} is not a pass (exit {want_rc})", rc == want_rc, f"rc={rc} out={out} err={err.strip()[:200]}")


# ── the allowances ───────────────────────────────────────────────────────────────────────────

TWO = {SURFACE: surface("uaSujo( );", "uaQuadro( );")}      # two driver-hook findings, lines 2 and 3
WHY = "transitional hook of the Ângulo driver (S2a); removed in S4–S10b: the test's own; ends in S4"


def message_cases():
    def problem(name, files, allowances, words, rule="driver-hook", path=SURFACE):
        with tree(files, allowances) as root:
            rc, out, err = run(root)
        _found, _last, bad = parse(out)
        mine = [l for l in out[:-1] if l.startswith(path + ":") and f": {rule}: " in l]
        ok = rc == 1 and not bad and any(all(w in l for w in words) for l in mine)
        check(name, ok, f"rc={rc} out={out}")

    expect("an allowance equal to the findings makes them pass", TWO, [],
           allowances=allow(**{"driver-hook": {SURFACE: (2, WHY)}}))
    problem("an allowance below the findings fails: grew", TWO, allow(**{"driver-hook": {SURFACE: (1, WHY)}}), ["grew"])
    problem("an allowance above the findings fails: lower the allowance", TWO,
            allow(**{"driver-hook": {SURFACE: (3, WHY)}}), ["lower the allowance"])
    problem("an allowed file with no findings is stale", {SURFACE: surface("int x;")},
            allow(**{"driver-hook": {SURFACE: (1, WHY)}}), ["stale"])
    problem("an allowed file that is gone is stale", {},
            allow(**{"driver-hook": {"src/Gone.cpp": (1, WHY)}}), ["stale"], path="src/Gone.cpp")
    problem("an allowance for the other rule does not cover the finding, and is stale itself", TWO,
            allow(**{"identity": {SURFACE: (2, WHY)}}), ["stale"], rule="identity")
    expect("an allowance for the other rule leaves the findings reported",
           TWO, [(SURFACE, 2, "driver-hook"), (SURFACE, 3, "driver-hook"), (SURFACE, 1, "identity")],
           allowances=allow(**{"identity": {SURFACE: (2, WHY)}}))
    problem("an allowance with no reason fails", TWO, allow(**{"driver-hook": {SURFACE: (2, "")}}), ["reason"])
    problem("an allowance with a count that is not a positive number fails", TWO,
            allow(**{"driver-hook": {SURFACE: (0, WHY)}}), ["count"])
    problem("an allowance whose count is `true` is not a number", TWO,
            '{"driver-hook": {"%s": {"count": true, "reason": "x"}}}\n' % SURFACE, ["count"])
    problem("an allowance under a rule that does not exist fails", TWO,
            allow(**{"made-up-rule": {SURFACE: (2, WHY)}}), ["unknown rule"], rule="made-up-rule")
    expect("one allowance covers one (rule, file): the same file's finding of another rule is still reported",
           {SURFACE: surface("uaSujo( );", "#if SIMUT_UI_ANGULO")},
           [(SURFACE, 3, "identity")], allowances=allow(**{"driver-hook": {SURFACE: (1, WHY)}}))
    expect("a core file may be allowed a cross-driver include, the count held the same way",
           {SURFACE: surface('#include "ui/angulo/UiA.h"')}, [],
           allowances=allow(**{"cross-driver": {SURFACE: (1, WHY)}}))


def update_cases():
    def read(root):
        with open(os.path.join(root, "tools", "ui_boundary.json"), encoding="utf-8") as fh:
            return json.load(fh)

    # lowers a count, keeps the reason (and its non-ASCII text as it is, not as \u escapes)
    with tree(TWO, allow(**{"driver-hook": {SURFACE: (3, WHY)}})) as root:
        before = tree_hashes(root)
        rc, out, err = run(root, "--update")
        after = tree_hashes(root)
        data = read(root)
        with open(os.path.join(root, "tools", "ui_boundary.json"), encoding="utf-8") as fh:
            raw = fh.read()
        rc2, out2, _ = run(root)
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    check("--update lowers a count to the findings and keeps the reason",
          rc == 0 and data == {"driver-hook": {SURFACE: {"count": 2, "reason": WHY}}} and rc2 == 0,
          f"rc={rc} data={data} rc2={rc2} out={out}")
    check("--update writes tools/ui_boundary.json and nothing else",
          changed == [os.path.join("tools", "ui_boundary.json")], f"changed={changed}")
    check("--update writes the reason as it is (UTF-8, not \\u escapes) and ends the file with a newline",
          "Ângulo" in raw and "S4–S10b" in raw and raw.endswith("}\n"), raw[:200])

    # never adds a file
    files = dict(TWO)
    files["src/Other.cpp"] = surface("uaSujo( );")
    with tree(files, allow(**{"driver-hook": {SURFACE: (3, WHY)}})) as root:
        rc, out, err = run(root, "--update")
        data = read(root)
        rc2, out2, _ = run(root)
    found, last, _bad = parse(out2)
    check("--update never adds a file: a new allowance is written by hand, with its reason",
          rc == 0 and list(data["driver-hook"]) == [SURFACE] and data["driver-hook"][SURFACE]["count"] == 2
          and rc2 == 1 and found == [("src/Other.cpp", 2, "driver-hook")],
          f"rc={rc} data={data} rc2={rc2} found={found}")

    # drops what is stale
    with tree({SURFACE: surface("int x;")}, allow(**{"driver-hook": {SURFACE: (1, WHY)}})) as root:
        rc, out, err = run(root, "--update")
        data = read(root)
        rc2, out2, _ = run(root)
    check("--update drops an allowance whose file has no findings left", rc == 0 and data == {} and rc2 == 0,
          f"rc={rc} data={data} rc2={rc2}")

    # a bad file is not rewritten into something that looks fine
    with tree(TWO, allow(**{"driver-hook": {SURFACE: (2, "")}})) as root:
        rc, out, err = run(root, "--update")
        data = read(root)
    check("--update does not invent a reason", data["driver-hook"][SURFACE]["reason"] == "", f"data={data}")

    # nothing to change: the file is left as it is, byte for byte
    with tree(TWO, allow(**{"driver-hook": {SURFACE: (2, WHY)}})) as root:
        path = os.path.join(root, "tools", "ui_boundary.json")
        h0 = file_hash(path)
        rc, out, err = run(root, "--update")
        h1 = file_hash(path)
    check("--update with the counts already right leaves the file byte for byte", rc == 0 and h0 == h1
          and out == ["check_ui_boundary: the counts already match"], f"rc={rc} out={out}")


def report_cases():
    with tree(TWO, allow(**{"driver-hook": {SURFACE: (1, WHY)}})) as root:
        rc, out, err = run(root, "--report")
    text = "\n".join(out)
    check("--report prints the findings and the counts and exits 0, even over an allowance",
          rc == 0 and f"{SURFACE}:2: driver-hook:" in text and f"{SURFACE}:3: driver-hook:" in text
          and re.search(r"driver-hook\s+" + re.escape(SURFACE) + r"\s+2\s+\(allowed 1\)", text) is not None
          and "findings per rule: identity 0, no-ifdef 0, include-caps 0, cross-driver 0, driver-hook 2" in text
          and out[-1] == "check_ui_boundary: 2 findings", f"rc={rc} out={out}")


def format_cases():
    files = {SURFACE: surface("uaSujo( );", '#include "ui/angulo/UiA.h"', "#if SIMUT_UI_ANGULO", "#endif"),
             "src/Bare.cpp": "#ifdef SIMUT_UI_CUSTOM_THEMES\n#endif\n"}
    with tree(files) as root:
        rc, out, err = run(root)
    found, last, bad = parse(out)
    rules = sorted(r for _p, _l, r in found)
    check("the output is `path:line: rule: text` per finding and `check_ui_boundary: N findings` last",
          rc == 1 and not bad and rules == ["cross-driver", "driver-hook", "identity", "include-caps", "no-ifdef"]
          and last == "check_ui_boundary: 5 findings", f"rc={rc} out={out}")
    check("the findings come in path and line order",
          [(p, l) for p, l, _r in found] == sorted((p, l) for p, l, _r in found), f"found={found}")
    check("the hints for the rules that fired go to stderr, once each",
          all(f"check_ui_boundary: {r}: " in err for r in rules) and err.count("check_ui_boundary: identity: ") == 1, err[:300])
    check("a finding is `file:line`, the rule and one sentence that names the macro or the hook",
          any(l.startswith(f"{SURFACE}:2: driver-hook: uaSujo ") for l in out)
          and any(l.startswith(f"{SURFACE}:4: identity: SIMUT_UI_ANGULO ") for l in out)
          and any(l.startswith("src/Bare.cpp:1: no-ifdef: SIMUT_UI_CUSTOM_THEMES ") for l in out), f"out={out}")


# ── the premise of no-ifdef, with the host preprocessor ──────────────────────────────────────

def premise_cases():
    cpp = shutil.which("cpp")
    if cpp is None:
        check("the premise of no-ifdef (needs the host's cpp, which the CI has)", False, "no cpp on PATH")
        return
    probe = ('#include "ui/UiCaps.h"\n#ifdef SIMUT_UI_ANGULO\nPROBE_IFDEF 1\n#else\nPROBE_IFDEF 0\n#endif\n'
             "#if SIMUT_UI_ANGULO\nPROBE_IF 1\n#else\nPROBE_IF 0\n#endif\n"
             "#if defined(SIMUT_UI_ORIGINAL)\nPROBE_DEF 1\n#else\nPROBE_DEF 0\n#endif\n")
    with tempfile.TemporaryDirectory(prefix="ui_boundary_cpp_") as tmp:
        path = os.path.join(tmp, "probe.c")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(probe)
        res = {}
        for label, flags in (("the original image (no flag)", []), ("the Angulo image", ["-DSIMUT_UI_ANGULO=1"])):
            r = subprocess.run([cpp, "-P", f"-I{os.path.join(ROOT, 'src')}"] + flags + [path], capture_output=True, text=True)
            res[label] = dict(re.findall(r"PROBE_(\w+) ([01])", r.stdout))
    orig, ang = res["the original image (no flag)"], res["the Angulo image"]
    check("premise: on the original image `#ifdef SIMUT_UI_ANGULO` is TRUE and `#if SIMUT_UI_ANGULO` is false "
          "(the reason the rule exists)", orig == {"IFDEF": "1", "IF": "0", "DEF": "1"}, str(orig))
    check("premise: on the Angulo image both are true, so `#ifdef` cannot tell the two images apart",
          ang == {"IFDEF": "1", "IF": "1", "DEF": "1"}, str(ang))


# ── the real tree ───────────────────────────────────────────────────────────────────────────

# What the test, and not the gate, takes for a file that has nothing of the driver in it, to pick the files the
# mutations go into: any `ua`/`Ua` and a capital, any UA_ macro, any `ua::`.
LOOKS_LIKE_A_HOOK = re.compile(r"(?:\b|_)(?:ua|Ua)[A-Z]|\bUA_|\bua\s*::")


def real_tree_cases():
    src_before = tree_hashes(os.path.join(ROOT, "src"))
    json_before = file_hash(os.path.join(ROOT, "tools", "ui_boundary.json"))
    r = subprocess.run([sys.executable, "-B", GATE], capture_output=True, text=True, encoding="utf-8",
                       cwd=tempfile.gettempdir())
    check("the real tree passes (the gate with no --root, from another directory)",
          r.returncode == 0 and r.stdout.splitlines()[-1:] == ["check_ui_boundary: 0 findings"],
          r.stdout[-600:] + r.stderr[-300:])
    r2 = subprocess.run([sys.executable, "-B", GATE, "--report"], capture_output=True, text=True, encoding="utf-8",
                        cwd=tempfile.gettempdir())
    check("--report on the real tree exits 0", r2.returncode == 0, r2.stdout[-300:] + r2.stderr[-300:])
    src_after = tree_hashes(os.path.join(ROOT, "src"))
    json_after = file_hash(os.path.join(ROOT, "tools", "ui_boundary.json"))
    check("the gate never writes under src/ (sha256 of the scanned files before and after), nor the allowances "
          "without --update", src_before == src_after and json_before == json_after,
          str([k for k in src_after if src_before.get(k) != src_after.get(k)][:5]))

    with open(os.path.join(ROOT, "tools", "ui_boundary.json"), encoding="utf-8") as fh:
        real_allow = json.load(fh)
    no_stage = [f"{rule} {p}" for rule, files in real_allow.items() for p, e in files.items()
                if not re.search(r"ends in S\d+", e.get("reason", ""))]
    check("every real allowance says which stage ends it (`ends in S<n>`)", not no_stage, str(no_stage[:5]))
    check("every real allowance is for a rule of the gate", all(rule in cub.RULES for rule in real_allow), str(list(real_allow)))

    # the original's headers, derived on the real tree: the five the gate was first given by hand, and more
    real_files = cub.load_tree(ROOT)
    derived = cub.original_headers(ROOT, set(real_files), cub.read_manifest(ROOT))
    five = {"src/Themes.h", "src/UiWidgets.h", "src/DisplayManager_Fonts.h", "src/PasswordKeyboard.h", "src/sensors/SensorDrawing.h"}
    check("the original's headers derived from the S0 table and section 2.5 include the five (Themes.h, UiWidgets.h, "
          "DisplayManager_Fonts.h, PasswordKeyboard.h, sensors/SensorDrawing.h) and DisplayManager_Alarm.h, and not the class",
          five <= derived and "src/DisplayManager_Alarm.h" in derived and "src/DisplayManager.h" not in derived
          and "src/DisplayManager_Auth.h" not in derived, str(sorted(derived))[:300])
    fonts = {"src/FreeSansBold9pt8b_latin1.h", "src/FreeSansBold12pt8b_latin1.h", "src/FreeSansBold24pt7b_subset.h"}
    left_out = {"src/DisplayManager_FmtFloat.h", "src/OtaScreen.h", "src/TextWrap.h", "src/display/ClockEntry.h",
                "src/display/CountInText.h", "src/display/DisplayDriver.h", "src/display/SettingsMenu.h",
                "src/sensors/SensorPanelDispatch.h"}
    check("the original's font data is in the derived set, and the eight headers that mixed units include too are left "
          "out, as section 2.5 says", fonts <= derived and not (left_out & derived),
          f"missing {sorted(fonts - derived)} wrongly in {sorted(left_out & derived)}")

    # a copy of the real tree: one violation of each rule appended to a file, one finding each
    with tempfile.TemporaryDirectory(prefix="ui_boundary_real_") as root:
        shutil.copytree(os.path.join(ROOT, "src"), os.path.join(root, "src"))
        os.makedirs(os.path.join(root, "tools"))
        for f in ("features.toml", "ui_boundary.json"):
            shutil.copy(os.path.join(ROOT, "tools", f), os.path.join(root, "tools", f))
        os.makedirs(os.path.join(root, "docs", "analysis"))
        for f in ("UI_DRIVERS.md", "UI_DRIVERS_funcoes.tsv"):
            shutil.copy(os.path.join(ROOT, "docs", "analysis", f), os.path.join(root, "docs", "analysis", f))
        base_hashes = tree_hashes(os.path.join(root, "src"))
        listed = {p for files in real_allow.values() for p in files}

        # pick the real files the mutations go into, by what they are and not by name
        core_clean = bare = angulo_cfg = angulo_any = None
        for dirpath, _d, files in sorted(os.walk(os.path.join(root, "src"))):
            for f in sorted(files):
                if not f.endswith((".h", ".cpp")):
                    continue
                p = os.path.join(dirpath, f)
                rel = os.path.relpath(p, root).replace(os.sep, "/")
                with open(p, encoding="utf-8", errors="ignore") as fh:
                    code, inc = cub.cnb.strip_source(fh.read())
                own = any(q and os.path.basename(n) in cub.CONFIG_HEADERS for _l, n, q in inc)
                uses = re.search(r"\bSIMUT_UI_", code) is not None
                hooks = LOOKS_LIKE_A_HOOK.search(code) is not None
                if rel.startswith(cub.ANGULO_DIR):
                    angulo_any = angulo_any or rel
                    if own and not uses and not angulo_cfg:
                        angulo_cfg = rel
                elif not rel.startswith(cub.UI_DIR) and rel != cub.CONFIG and not uses and not hooks and rel not in listed:
                    if own and not core_clean and f.endswith(".cpp"):
                        core_clean = rel
                    if not own and not bare and f.endswith(".cpp"):
                        bare = rel
        check("the real tree has the files the mutations go into (a clean core file, a bare one, an Angulo unit that "
              "includes the config, any Angulo unit)",
              all((core_clean, bare, angulo_cfg, angulo_any)), str((core_clean, bare, angulo_cfg, angulo_any)))
        if not all((core_clean, bare, angulo_cfg, angulo_any)):
            return

        def mutated(rel, extra, want_rule, name, expected_findings=1):
            path = os.path.join(root, rel)
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
                  and all(r == want_rule for _p, _l, r in found) and last == f"check_ui_boundary: {expected_findings} findings")
            check(name, ok, f"rc={rc} found={found} last={last!r} (the appended text starts on line {nlines})")
            return found, nlines

        found, n = mutated(core_clean, "\n#if SIMUT_UI_ORIGINAL\n#endif\n", "identity",
                           f"real tree + an identity macro in {core_clean}: one finding")
        check("  ... and it is on the appended line", found[0][:2] == (core_clean, n + 1) if found else False, str(found))
        mutated(core_clean, "\n#if SIMUT_UI_ANGULO\n#endif\n", "identity", f"real tree + SIMUT_UI_ANGULO in {core_clean}: one finding")
        mutated(core_clean, "\nstatic int boundary_probe = uaQuadro( );\n", "driver-hook",
                f"real tree + `uaQuadro( )` in {core_clean}: one finding")
        mutated(core_clean, "\nUA_DESVIO( );\n", "driver-hook", f"real tree + `UA_DESVIO( );` in {core_clean}: one finding")
        mutated(core_clean, "\nstatic int m_uaSujo = 0;\n", "driver-hook", f"real tree + `m_uaSujo` in {core_clean}: one finding")
        mutated(core_clean, "\n#define UA_FOO 1\n", "driver-hook", f"real tree + `UA_FOO` in {core_clean}: one finding")
        mutated(core_clean, "\nstatic void *boundary_probe = (void *)sizeof(UaToque);\n", "driver-hook",
                f"real tree + the type `UaToque` in {core_clean}: one finding")
        mutated(core_clean, "\nstatic void *boundary_probe = (void *)&registerUiARoutes;\n", "driver-hook",
                f"real tree + `registerUiARoutes`, a driver name with no ua mark, in {core_clean}: one finding")
        mutated(core_clean, "\nstatic const ua::Tema *boundary_probe = nullptr;\n", "driver-hook",
                f"real tree + the namespace `ua::` in {core_clean}: one finding")
        mutated(core_clean, "\nnamespace boundary_alias = ua;\n", "driver-hook",
                f"real tree + the alias `namespace boundary_alias = ua;` in {core_clean}: one finding")
        with open(os.path.join(root, core_clean), encoding="utf-8", newline="") as fh:
            clean_text = fh.read()
        try:
            with open(os.path.join(root, core_clean), "w", encoding="utf-8", newline="") as fh:
                fh.write(clean_text + '\nstatic const char *uaString = "Mozilla/5.0";\nstatic int boundary_arrow = (int)sizeof(UiArrowDir);\n')
            rc, out, err = run(root)
        finally:
            with open(os.path.join(root, core_clean), "w", encoding="utf-8", newline="") as fh:
                fh.write(clean_text)
        check(f"real tree + a user agent `uaString` and the core's `UiArrowDir` in {core_clean}: still passes (the driver introduces neither)",
              rc == 0 and out[-1:] == ["check_ui_boundary: 0 findings"], f"rc={rc} out={out[-3:]}")
        mutated(core_clean, "\nstatic bool boundary_probe = _uaPintando;\n", "driver-hook",
                f"real tree + `_uaPintando` in {core_clean}: one finding")
        mutated(core_clean, '\n#include "ui/angulo/UiA.h"\n', "cross-driver",
                f"real tree + an include of an Angulo header in {core_clean}: one finding")
        mutated(angulo_any, '\n#include "../../Themes.h"\n', "cross-driver",
                f"real tree + an include of Themes.h in the Angulo unit {angulo_any}: one finding")
        mutated(angulo_any, '\n#include "../../sensors/SensorDrawing.h"\n', "cross-driver",
                f"real tree + an include of SensorDrawing.h in the Angulo unit {angulo_any}: one finding")
        for h in ("DisplayManager_Alarm.h", "PasswordKeyboard.h", "UiWidgets.h", "DisplayManager_Fonts.h", "FreeSansBold9pt8b_latin1.h",
                  "FreeSansBold12pt8b_latin1.h", "FreeSansBold24pt7b_subset.h"):
            mutated(angulo_any, f'\n#include "../../{h}"\n', "cross-driver",
                    f"real tree + an include of {h} in the Angulo unit {angulo_any}: one finding")
        mutated(bare, "\n#if SIMUT_UI_CUSTOM_THEMES\n#endif\n", "include-caps",
                f"real tree + a capability test in {bare} (which includes neither header): one finding")
        mutated("src/simut_config.h", "\n#ifdef SIMUT_UI_ANGULO\n#endif\n", "no-ifdef",
                "real tree + `#ifdef SIMUT_UI_ANGULO` in src/simut_config.h: one finding")
        mutated(angulo_cfg, "\n#if defined(SIMUT_UI_CUSTOM_THEMES)\n#endif\n", "no-ifdef",
                f"real tree + `defined( )` on a UI macro in the Angulo unit {angulo_cfg}: one finding")
        with open(os.path.join(root, "src", "ui", "UiCaps.h"), encoding="utf-8", newline="") as fh:
            caps_original = fh.read()
        try:
            with open(os.path.join(root, "src", "ui", "UiCaps.h"), "w", encoding="utf-8", newline="") as fh:
                fh.write(caps_original + "\n#ifdef SIMUT_UI_ANGULO\n#endif\n#if defined(SIMUT_UI_CUSTOM_THEMES)\n#endif\n")
            rc, out, err = run(root)
        finally:
            with open(os.path.join(root, "src", "ui", "UiCaps.h"), "w", encoding="utf-8", newline="") as fh:
                fh.write(caps_original)
        check("real tree + #ifdef and defined( ) appended to src/ui/UiCaps.h, the one file let off no-ifdef: passes",
              rc == 0 and out[-1:] == ["check_ui_boundary: 0 findings"], f"rc={rc} out={out[-3:]}")

        # the counts are tight on a real file. The copy is given an allowance of its own, so that this does not
        # depend on which allowances the real file still lists (the last stage empties it): two hook sites
        # appended to a clean core file, allowed two.
        apath = os.path.join(root, "tools", "ui_boundary.json")
        with open(apath, encoding="utf-8") as fh:
            allowances_text = fh.read()
        allowed = json.loads(allowances_text)
        path = os.path.join(root, core_clean)
        with open(path, encoding="utf-8", newline="") as fh:
            original = fh.read()
        base = original + "\nstatic int boundary_seed_a = uaQuadro( );\nstatic int boundary_seed_b = uaSujo( );\n"
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(base)
        allowed.setdefault("driver-hook", {})[core_clean] = {"count": 2, "reason": "the test's own: two hook sites, allowed two; ends in S0"}
        with open(apath, "w", encoding="utf-8") as fh:
            json.dump(allowed, fh)
        rc, out, _e = run(root)
        check(f"real tree + two hook sites in {core_clean} and an allowance of two: passes", rc == 0, f"{out[-3:]}")
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(base + "\nstatic int boundary_probe = uaQuadro( );\n")
        rc, out, _e = run(root)
        check(f"real tree: one hook site more in {core_clean}, which is allowed, fails: grew",
              rc == 1 and any("grew" in l and l.startswith(core_clean + ":") for l in out), f"{out[-3:]}")
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(base.replace("static int boundary_seed_b = uaSujo( );", "static int removed_site = 0;"))
        rc, out, _e = run(root)
        check(f"real tree: one hook site fewer in {core_clean}, which is allowed, fails: lower the allowance",
              rc == 1 and any("lower the allowance" in l and l.startswith(core_clean + ":") for l in out), f"{out[-3:]}")
        rc, out, _e = run(root, "--update")
        rc2, out2, _e2 = run(root)
        with open(apath, encoding="utf-8") as fh:
            after_update = json.load(fh)
        check("real tree: --update takes the lower count and the gate passes again; every other allowance is left alone",
              rc == 0 and rc2 == 0 and after_update["driver-hook"][core_clean]["count"] == 1
              and all(after_update[r][p] == e for r, files in real_allow.items() for p, e in files.items()),
              f"{out[-3:]} {out2[-3:]}")
        with open(path, "w", encoding="utf-8", newline="") as fh:
            fh.write(original)
        with open(apath, "w", encoding="utf-8") as fh:
            fh.write(allowances_text)
        after = tree_hashes(os.path.join(root, "src"))
        check("the copy's sources are back as they were after the mutations (the instrument is sound)",
              {k: v for k, v in after.items() if k in base_hashes} == base_hashes)


# ── run ────────────────────────────────────────────────────────────────────────────────────

class_include_cases()
cross_driver_cases()
original_cases()
hook_cases()
extension_cases()
inert_cases()
lexical_cases()
manifest_cases()
message_cases()
update_cases()
report_cases()
format_cases()
premise_cases()
real_tree_cases()

print(f"\n{len(RAN) - len(FAILS)} of {len(RAN)} checks passed")
if FAILS:
    print("FAILED:\n  " + "\n  ".join(FAILS))
    sys.exit(1)
