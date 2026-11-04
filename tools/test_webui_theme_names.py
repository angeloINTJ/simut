#!/usr/bin/env python3
"""The theme select of the web page, in the device's language, run under node.

GET /api/themes sends every theme as {"id", "code", "name"}. The stock names
are English, and the code is the theme's @CODE (nordic, cafe, ...). The page
asks the active language pack's @WEBDICT for thm_<code> and keeps the name the
device sent when the pack has no such key: a user's own .thm, the English page,
a dictionary that has not arrived yet, a firmware that sends no code. Decision
of 2026-10-24: the device sends English, the page translates.

This test takes the pure part of the select, everything between the `th: the
pure part` comment and the `th: end of the pure part` comment, out of WebUI.h,
the source the build compresses, and runs the cases below against it twice
under node: as written, and as the build's own minifier leaves it. The
minifier has deleted shipped code before and reported success
(tools/test_webui_minify.py tells that story), so passing as written is not
enough. The pure part runs together with the t( ), escHtml( ), escAttr( ) and
applyLang( ) of lang.js (LANG_JS in WebUI.h), the script the page loads, so the cases
exercise what the browser runs: the key lookup, the escaping and the
relabelling of an <option>.

The data checks need no node: every stock theme has a thm_<code> key in both
language packs, every data/themes/*.thm carries the @NAME and @CODE that
tools/gen_theme_collection.py writes (it runs in a scratch tree, data/themes is
never touched; the colours have drifted from the generator, and the check says
so), and every stock name fits the 24 B the panel keeps for it.

Run: python3 tools/test_webui_theme_names.py
Exit status is 0 on pass, 1 on failure.
"""

import functools
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WEBUI = REPO / "WebUI.h"
BUILD_GZ = REPO / "tools" / "build_webui_gz.py"
THEME_GEN = REPO / "tools" / "gen_theme_collection.py"
THEMES_CPP = REPO / "src" / "ui" / "original" / "Themes.cpp"   # S11: the original's units live in src/ui/original/
ANGULO_TEMAS = REPO / "src" / "ui" / "angulo" / "Temas.cpp"   # the Ângulo's catalogue: a file of the UI-drivers branch, absent on main
THEMES_DIR = REPO / "data" / "themes"
PACKS = [REPO / "data" / "lang" / "language_pt-BR.lng",
         REPO / "data" / "lang" / "language_es-ES.lng"]
START = "/* th: the pure part"
END = "/* th: end of the pure part */"
HELPERS = ("t", "escHtml", "escAttr", "applyLang")
NAME_MAX = 23  # a theme's @NAME is kept in 24 B: 23 characters and the NUL

# "Simut Default" (simut_def) is compiled into every image and is the one stock
# theme with no @WEBDICT key: the page shows it as the device sent it, in every
# language, as it always did (the maintainer's answer of 2026-10-28, Q3). It is
# exempt by name, so a second compiled theme without a key is still caught.
EXEMPT = {"simut_def": "kept as it is, with no key, in every language (Q3 of 2026-10-28)"}

# The cases, as data: inputs travel to node through JSON, so no string here has
# to survive two levels of escaping. `want` lists the <option>s in order as
# {value, key (the data-i18n attribute, null when absent), text (the inner HTML)}.
# `calls` lists the calls the pure part makes to the page's global t( ).
CASES = [
    {"id": "a", "title": "a pt dictionary with thm_cafe shows the pt name",
     "lang": "pt", "dict": {"thm_cafe": "Café com leite"},
     "rows": [{"id": 3, "code": "cafe", "name": "Coffee with milk"}],
     "want": [{"value": "3", "key": "thm_cafe", "text": "Café com leite"}],
     "calls": [["thm_cafe", "Coffee with milk"]]},
    {"id": "b1", "title": "no dictionary yet shows the English the device sent, and keeps the key for applyLang( )",
     "lang": "pt", "dict": {},
     "rows": [{"id": 3, "code": "cafe", "name": "Coffee with milk"}],
     "want": [{"value": "3", "key": "thm_cafe", "text": "Coffee with milk"}]},
    {"id": "b2", "title": "the English page shows the English name with the dictionary loaded",
     "lang": "en", "dict": {"thm_cafe": "Café com leite"},
     "rows": [{"id": 3, "code": "cafe", "name": "Coffee with milk"}],
     "want": [{"value": "3", "key": "thm_cafe", "text": "Coffee with milk"}]},
    {"id": "c", "title": "a code without a key (a user's .thm) shows the name as written",
     "lang": "pt", "dict": {"thm_cafe": "Café com leite"},
     "rows": [{"id": 12, "code": "meu_tema", "name": "Meu tema"}],
     "want": [{"value": "12", "key": "thm_meu_tema", "text": "Meu tema"}]},
    {"id": "d", "title": "escuro-petroleo becomes thm_escuro_petroleo",
     "lang": "pt", "dict": {"thm_escuro_petroleo": "Escuro, petróleo"},
     "rows": [{"id": 5, "code": "escuro-petroleo", "name": "Dark, petrol"}],
     "want": [{"value": "5", "key": "thm_escuro_petroleo", "text": "Escuro, petróleo"}],
     "calls": [["thm_escuro_petroleo", "Dark, petrol"]]},
    {"id": "e", "title": "a name, a code and a translation with < > & \" are escaped, and a code cannot leave its attribute",
     "lang": "pt", "dict": {"thm_x___img_src_x_": "<i>pt</i>"},
     "rows": [{"id": 1, "code": "ok", "name": "A <b>\"&</b>"},
              {"id": 2, "code": "x\"><img src=x>", "name": "Hostile"}],
     "want": [{"value": "1", "key": "thm_ok", "text": "A &lt;b&gt;\"&amp;&lt;/b&gt;"},
              {"value": "2", "key": "thm_x___img_src_x_", "text": "&lt;i&gt;pt&lt;/i&gt;"}],
     "forbid": ["<b>", "<i>", "<img", "\"><"]},
    {"id": "f", "title": "a row without code (old firmware), with code \"\" and with code null shows the name and asks t( ) for nothing",
     "lang": "pt", "dict": {"thm_cafe": "Café com leite"},
     "rows": [{"id": 0, "name": "Simut Default"}, {"id": 1, "name": "Nordico", "code": ""},
              {"id": 2, "name": "Cafe", "code": None}],
     "want": [{"value": "0", "key": None, "text": "Simut Default"},
              {"value": "1", "key": None, "text": "Nordico"},
              {"value": "2", "key": None, "text": "Cafe"}],
     "calls": []},
    {"id": "g", "title": "the function calls the page's global t( ) with ('thm_' + code, the device's name), once per coded row: it does not shadow t",
     "lang": "pt", "dict": {"thm_cafe": "Café com leite"},
     "rows": [{"id": 0, "code": "cafe", "name": "Coffee with milk"},
              {"id": 1, "code": "nordic", "name": "Nordic"},
              {"id": 2, "name": "No code"}],
     "want": [{"value": "0", "key": "thm_cafe", "text": "Café com leite"},
              {"value": "1", "key": "thm_nordic", "text": "Nordic"},
              {"value": "2", "key": None, "text": "No code"}],
     "calls": [["thm_cafe", "Coffee with milk"], ["thm_nordic", "Nordic"]]},
    {"id": "h", "title": "the page's applyLang( ) relabels an <option data-i18n>, restores the English on the English page and keeps the text when the key is missing",
     "kind": "apply",
     "dict": {"thm_cafe": "Café com leite"},
     "options": [["thm_cafe", "Coffee with milk"], ["thm_meu_tema", "Meu tema"]],
     "after_pt": ["Café com leite", "Meu tema"], "after_en": ["Coffee with milk", "Meu tema"]},
    {"id": "h2", "title": "options born translated (dictionary first) or translated later go back to the device's English, a name with & < > whole",
     "kind": "order",
     "dict": {"thm_cafe": "Café com leite"},
     "rows": [{"id": 3, "code": "cafe", "name": "Coffee with milk"},
              {"id": 12, "code": "meu_tema", "name": "Meu tema"},
              {"id": 13, "code": "mix", "name": "A <b> & c"}],
     "after_pt": ["Café com leite", "Meu tema", "A &lt;b&gt; &amp; c"],
     "after_en": ["Coffee with milk", "Meu tema", "A &lt;b&gt; &amp; c"]},
]

# What node does not have: the page's storage, its DOM (only what t( ), escHtml( )
# and applyLang( ) touch) and the dictionary. createElement( ) answers like a
# real element for a text node: & < > are escaped, a quote is not.
PREAMBLE = r"""
var __store = {};
Object.defineProperty(globalThis, 'localStorage', { configurable: true, writable: true, value: {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(__store, k) ? __store[k] : null; },
    setItem: function (k, v) { __store[k] = String(v); } } });
var __els = [];
Object.defineProperty(globalThis, 'document', { configurable: true, writable: true, value: {
    createElement: function () { var s = ''; return {
        set textContent(v) { s = String(v); },
        get innerHTML() { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); } }; },
    querySelectorAll: function (sel) { return sel === '[data-i18n]' ? __els : []; } } });
var window = globalThis;
var dict = { pt: {} };
"""

HARNESS = r"""
var __cases = __CASES__;
var __results = [];
var __realT = t;
function __options(html) {
    var out = [], re = /<option\b([^>]*)>([\s\S]*?)<\/option>/g, m;
    while ((m = re.exec(html))) {
        var v = /\bvalue="([^"]*)"/.exec(m[1]), k = /\bdata-i18n="([^"]*)"/.exec(m[1]);
        out.push({ value: v ? v[1] : null, key: k ? k[1] : null, text: m[2] });
    }
    return out;
}
function __opt(key, html) {
    var attrs = { 'data-i18n': key }, state = html;
    return { tagName: 'OPTION',
             get innerHTML() { return state; }, set innerHTML(v) { state = String(v); },
             get textContent() { return state.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&'); },
             set textContent(v) { state = String(v).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); },
             getAttribute: function (n) { return Object.prototype.hasOwnProperty.call(attrs, n) ? attrs[n] : null; },
             setAttribute: function (n, v) { attrs[n] = String(v); },
             hasAttribute: function (n) { return Object.prototype.hasOwnProperty.call(attrs, n); } };
}
function __attrDecode(s) { return s.replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&'); }
function __dom(html) {
    var out = [], re = /<option\b([^>]*)>([\s\S]*?)<\/option>/g, m;
    while ((m = re.exec(html))) {
        var attrs = {}, ar = /([\w-]+)="([^"]*)"/g, a, state = m[2];
        while ((a = ar.exec(m[1]))) attrs[a[1]] = __attrDecode(a[2]);
        out.push((function (attrs, state) { return { tagName: 'OPTION',
             get innerHTML() { return state; }, set innerHTML(v) { state = String(v); },
             get textContent() { return state.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&'); },
             set textContent(v) { state = String(v).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); },
             getAttribute: function (n) { return Object.prototype.hasOwnProperty.call(attrs, n) ? attrs[n] : null; },
             setAttribute: function (n, v) { attrs[n] = String(v); },
             hasAttribute: function (n) { return Object.prototype.hasOwnProperty.call(attrs, n); } }; })(attrs, state));
    }
    return out.filter(function (e) { return e.hasAttribute('data-i18n'); });
}
function __j(o) { return JSON.stringify(o); }
__cases.forEach(function (c) {
    var fails = [];
    try {
        Object.keys(__store).forEach(function (k) { delete __store[k]; });
        dict.pt = c.dict || {};
        if (c.kind === 'apply') {
            __els = c.options.map(function (o) { return __opt(o[0], o[1]); });
            localStorage.setItem('simut_lang', 'pt'); applyLang();
            var pt = __els.map(function (e) { return e.innerHTML; });
            if (__j(pt) !== __j(c.after_pt)) fails.push('on the pt page the options read ' + __j(pt) + ', wanted ' + __j(c.after_pt));
            localStorage.setItem('simut_lang', 'en'); applyLang();
            var en = __els.map(function (e) { return e.innerHTML; });
            if (__j(en) !== __j(c.after_en)) fails.push('on the English page the options read ' + __j(en) + ', wanted ' + __j(c.after_en));
        } else if (c.kind === 'order') {
            ['dict-first', 'themes-first'].forEach(function (order) {
                Object.keys(__store).forEach(function (k) { delete __store[k]; });
                __els = []; dict.pt = {};
                localStorage.setItem('simut_lang', 'pt');
                var check = function (label, want) {
                    var got = __els.map(function (e) { return e.innerHTML; });
                    if (__j(got) !== __j(want)) fails.push(order + ', ' + label + ': the options read ' + __j(got) + ', wanted ' + __j(want));
                };
                if (order === 'dict-first') {
                    dict.pt = c.dict; applyLang();                                     /* lang.js loaded the dictionary at start */
                    __els = __dom(themeOptions(JSON.parse(__j(c.rows))));              /* loadThemes( ) at DOMContentLoaded */
                    check('as created', c.after_pt);
                } else {
                    __els = __dom(themeOptions(JSON.parse(__j(c.rows))));              /* /api/themes answered first */
                    check('as created', c.after_en);
                    dict.pt = c.dict; applyLang(); check('after the dictionary arrived', c.after_pt);
                }
                [['en', c.after_en], ['pt', c.after_pt], ['en', c.after_en]].forEach(function (st) {
                    localStorage.setItem('simut_lang', st[0]); applyLang(); check('after switching to ' + st[0], st[1]);
                });
            });
        } else {
            if (c.lang) localStorage.setItem('simut_lang', c.lang);
            var calls = [];
            t = function (key, fallback, vars) { calls.push([key, fallback]); return __realT(key, fallback, vars); };
            var html = themeOptions(JSON.parse(__j(c.rows)));
            t = __realT;
            var got = __options(html);
            if (__j(got) !== __j(c.want)) fails.push('the options are ' + __j(got) + ', wanted ' + __j(c.want));
            if (c.calls && __j(calls) !== __j(c.calls)) fails.push('t( ) was called as ' + __j(calls) + ', wanted ' + __j(c.calls));
            (c.forbid || []).forEach(function (s) { if (html.indexOf(s) >= 0) fails.push('the markup carries ' + __j(s)); });
        }
    } catch (e) { t = __realT; fails.push('threw ' + e.name + ': ' + e.message); }
    __results.push({ id: c.id, fails: fails });
});
console.log(__j(__results));
"""


@functools.lru_cache(maxsize=None)
def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(BUILD_GZ.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(BUILD_GZ), "exec"), ns)
    return ns


def pure_part(src: str):
    """The pure part of the select, or None when WebUI.h does not carry it."""
    a = src.find(START)
    b = src.find(END, a + 1) if a >= 0 else -1
    return src[a:b] if a >= 0 and b >= 0 else None


def block(src: str, name: str) -> str:
    """One page or asset of WebUI.h: from its declaration to the end of its raw string."""
    a = src.find(f"static const char {name}[] PROGMEM")
    return src[a:src.find(')raw";', a)] if a >= 0 else ""


def helpers(src: str):
    """(code, missing): the one-line window.t, window.escHtml and window.applyLang of
    lang.js (LANG_JS), which the dashboard page loads."""
    lang = block(src, "LANG_JS")
    found, missing = [], []
    for name in HELPERS:
        m = re.search(rf"^[ \t]*(window\.{name} = function.*)$", lang, re.M)
        if m:
            found.append(m.group(1))
        else:
            missing.append(name)
    return "\n".join(found), missing


def page_of(src: str, pos: int):
    """The name of the page or asset of WebUI.h that holds position `pos`."""
    last = None
    for m in re.finditer(r"static const char (\w+)\[\] PROGMEM", src):
        if m.start() > pos:
            break
        last = m.group(1)
    return last


def wiring(src: str) -> bool:
    """What node cannot see, read off the source."""
    checks = []  # (name, ok, why)
    n = src.count(START)
    raw = pure_part(src)
    checks.append(("W1 the pure part is in WebUI.h, once, between the th: comments",
                   raw is not None and n == 1,
                   f"{n} start marker(s) and "
                   f"{'an' if raw is not None else 'no'} end marker: add themeOptions( ) between "
                   f"'{START} ...' and '{END}'"))
    a = src.find(START)
    b = src.find(END, a + 1) if a >= 0 else -1
    i = src.rfind("/* @IF", 0, a) if a >= 0 else -1
    j = src.find("/* @ENDIF", i) if i >= 0 else -1
    checks.append(("W2 the pure part sits inside /* @IF tft */",
                   a >= 0 and i >= 0 and re.match(r"/\* @IF tft \*/", src[i:i + 16]) is not None
                   and j > b >= 0,
                   "the panel-only code must stay inside an @IF tft block, or the alpha and the Air carry it"))
    code = re.sub(r"/\*.*?\*/", "", raw or "", flags=re.S)
    checks.append(("W3 the call is literally t('thm_' + ...",
                   re.search(r"\bt\(\s*['\"]thm_['\"]\s*\+", code) is not None,
                   "check_lang_packs.py reads t('prefix_' + ...) as a dynamic prefix; any other form "
                   "makes every thm_ key look dead"))
    literal = re.findall(r'data-i18n[a-z-]*=\\?["\']([A-Za-z0-9_]+)\\?["\']', code)
    checks.append(("W7 no data-i18n key is written as a literal in the pure part",
                   raw is not None and not literal,
                   "check_lang_packs.py reads " + repr(literal) + " as keys the pack must have, and fails the build; "
                   "assemble the attribute at run time, in a template literal (data-i18n=\"thm_${c}\")"))
    shadow = [p for p in (r"\bt\s*=>", r"[(,]\s*t\s*[,)]\s*=>", r"\bfunction\b[^(]*\([^)]*\bt\b[^)]*\)",
                          r"\b(?:let|const|var)\s+t\b", r"\bcatch\s*\(\s*t\s*\)")
              if re.search(p, code)]
    checks.append(("W4 no parameter or variable named t in the pure part",
                   raw is not None and not shadow,
                   "a binding named t hides the page's t( ) and the call throws"))
    k = src.find("async function loadThemes")
    body = src[k:src.find("async function saveTheme", k)] if k >= 0 else ""
    checks.append(("W5 loadThemes( ) builds the options through themeOptions( )",
                   "themeOptions(" in body and "forEach(t" not in body,
                   "loadThemes( ) must call the tested function, not carry its own loop"))
    _, lost = helpers(src)
    page = page_of(src, a) if a >= 0 else None
    checks.append(("W6 the pure part is in DASH_PAGE, which loads lang.js, where t( ), escHtml( ), escAttr( ) and applyLang( ) are defined",
                   page == "DASH_PAGE" and '<script src="/lang.js">' in block(src, "DASH_PAGE") and not lost,
                   f"the pure part is in {page}; missing from LANG_JS: {', '.join(lost) or 'nothing'}; "
                   f"the harness runs the helpers of the script this page loads"))
    ok = True
    for name, good, why in checks:
        if good:
            print(f"  ok   wiring: {name}")
        else:
            ok = False
            print(f"  FAIL wiring: {name} ({why})")
    return ok


def run_cases(label: str, code, note: str) -> bool:
    if code is None:
        for c in CASES:
            print(f"  FAIL {label} ({c['id']}) {c['title']}: {note}")
        return False
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed; the theme select cannot be exercised")
    js = PREAMBLE + "\n" + code + "\n" + HARNESS.replace("__CASES__", json.dumps(CASES))
    r = subprocess.run([node, "-e", js], capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        for c in CASES:
            print(f"  FAIL {label} ({c['id']}) {c['title']}: node rejected the program")
        print(r.stderr[-1500:])
        return False
    by_id = {x["id"]: x["fails"] for x in json.loads(r.stdout.strip().splitlines()[-1])}
    ok = True
    for c in CASES:
        fails = by_id.get(c["id"], ["no result"])
        if fails:
            ok = False
            for f in fails:
                print(f"  FAIL {label} ({c['id']}) {c['title']}: {f}")
        else:
            print(f"  ok   {label} ({c['id']}) {c['title']}")
    return ok


def webdict(pack: Path) -> dict:
    """The @WEBDICT of a language pack: one line of JSON under its header."""
    lines = pack.read_text(encoding="utf-8").split("\n")
    return json.loads(lines[lines.index("@WEBDICT") + 1])


@functools.lru_cache(maxsize=None)
def generated():
    """The generator's output, made in a scratch tree so that data/themes is never
    touched: {file name: bytes}. Raises RuntimeError when the generator fails."""
    with tempfile.TemporaryDirectory() as td:
        (Path(td) / "tools").mkdir()
        (Path(td) / "data").mkdir()
        shutil.copy(THEME_GEN, Path(td) / "tools" / THEME_GEN.name)
        r = subprocess.run([sys.executable, "-I", str(Path(td) / "tools" / THEME_GEN.name)],
                           capture_output=True, text=True, timeout=120, cwd=td)
        if r.returncode != 0:
            raise RuntimeError(f"the generator exited {r.returncode}: {(r.stdout + r.stderr)[-400:]}")
        return {f.name: f.read_bytes() for f in sorted((Path(td) / "data" / "themes").glob("*.thm"))}


def thm_fields(blob: bytes):
    """(@NAME, @CODE) of a .thm."""
    text = blob.decode("utf-8")
    name = re.search(r"^@NAME (.*)$", text, re.M)
    code = re.search(r"^@CODE (.*)$", text, re.M)
    return (name.group(1) if name else None, code.group(1) if code else None)


def compiled_themes():
    """[(code, name)] of the themes src/ui/original/Themes.cpp compiles: the entries outside every
    #if, and those inside an #ifdef some build file defines."""
    cfg = "".join((REPO / f).read_text(encoding="utf-8", errors="replace")
                  for f in ("platformio.ini", "tools/features.toml", "tools/generated/profiles.ini"))
    stack, out = [], []
    for line in THEMES_CPP.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        m = re.match(r"#\s*ifdef\s+(\w+)", s)
        if m:
            stack.append(m.group(1))
        elif re.match(r"#\s*if", s):
            stack.append(None)
        elif re.match(r"#\s*endif\b", s):
            if stack:
                stack.pop()
        else:
            m = re.match(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*"([^"]+)"\s*,', s)
            if m and all(g is not None and g in cfg for g in stack):
                out.append((m.group(1), m.group(2)))
    return out


def angulo_themes():
    """[(id, English name)] of the Ângulo's 18 choices (src/ui/angulo/Temas.cpp), [] when the tree has no such file. The page keys them like any other code: thm_ and the id with every
    character outside [A-Za-z0-9_] as _ (claro-petroleo -> thm_claro_petroleo)."""
    if not ANGULO_TEMAS.exists():
        return []
    return re.findall(r'TEMA\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', ANGULO_TEMAS.read_text(encoding="utf-8"))


def data_checks() -> bool:
    ok = True
    try:
        gen = generated()
    except RuntimeError as e:
        print(f"  FAIL data: {e}")
        return False
    stock = {}  # code -> name
    for blob in gen.values():
        name, code = thm_fields(blob)
        stock[code] = name
    for code, name in compiled_themes():
        stock.setdefault(code, name)
    for code, name in angulo_themes():
        stock.setdefault(code, name)
    # D1: every stock code has a non-empty key in both packs.
    miss = []
    for pack in PACKS:
        d = webdict(pack)
        for code in sorted(stock):
            if code in EXEMPT:
                continue
            key = "thm_" + re.sub(r"[^A-Za-z0-9_]", "_", code)   # as the page builds it (WebUI.h, themeOptions( ))
            v = d.get(key)
            if not isinstance(v, str) or not v.strip():
                miss.append(f"{pack.name} has no non-empty {key}")
    if miss:
        ok = False
        for m in miss:
            print(f"  FAIL data D1: {m}")
    else:
        print(f"  ok   data D1: {len(stock) - len([c for c in stock if c in EXEMPT])} stock codes have thm_<code> in "
              f"{len(PACKS)} packs ({', '.join(sorted(EXEMPT))} exempt: {'; '.join(EXEMPT.values())})")
    # D2: data/themes carries the generator's names and codes, file for file. The files are NOT
    # the generator's output byte for byte, and have not been since 2137ea15 (2026-08-23): it
    # changed the limit-alarm trio (alarmBg, alarmText, alarmTextDim) in the eleven .thm and in
    # src/ui/original/Themes.cpp but not in the generator, whose contrast rules the new trio fails (36 pairs
    # on 2026-10-28), so a regeneration would turn every stock theme's alarm red again. Until
    # the generator is synced this holds what this feature depends on, the @NAME and the @CODE
    # of each file, and says out loud which colour fields differ.
    have = {f.name: f.read_bytes() for f in sorted(THEMES_DIR.glob("*.thm"))}
    bad = [f"{n} is not a generated file" for n in sorted(set(have) - set(gen))] + \
          [f"{n} is missing" for n in sorted(set(gen) - set(have))]
    drift = set()
    for n in sorted(set(have) & set(gen)):
        if thm_fields(have[n]) != thm_fields(gen[n]):
            bad.append(f"{n} says @NAME/@CODE {thm_fields(have[n])}, the generator {thm_fields(gen[n])}")
        ca, cb = (dict(re.findall(r"^(\w+)=(#[0-9A-Fa-f]{6})$", x.decode("utf-8"), re.M)) for x in (have[n], gen[n]))
        drift |= {k for k in ca.keys() | cb.keys() if ca.get(k) != cb.get(k)}
    if bad:
        ok = False
        for m in bad:
            print(f"  FAIL data D2: {m} (change the name in tools/gen_theme_collection.py and in the .thm together)")
    else:
        print(f"  ok   data D2: the {len(have)} data/themes/*.thm carry the generator's names and codes")
    if drift:
        print(f"  note data D2: the colour fields {', '.join(sorted(drift))} of the files differ from the "
              f"generator's output (2137ea15 changed the files, not the generator)")
    # D3: every stock name fits the panel's 24 B.
    long_ = [f"{c} {n!r} is {len(n.encode('utf-8'))} B" for c, n in sorted(stock.items())
             if len(n.encode("utf-8")) > NAME_MAX]
    if long_:
        ok = False
        for m in long_:
            print(f"  FAIL data D3: {m} (the limit is {NAME_MAX})")
    else:
        print(f"  ok   data D3: the {len(stock)} stock names are at most {NAME_MAX} B")
    return ok


def main() -> int:
    src = WEBUI.read_text(encoding="utf-8", errors="replace")
    ok = wiring(src)
    raw = pure_part(src)
    hlp, missing = helpers(src)
    code = None if raw is None or missing else hlp + "\n" + raw
    note = ("the page has no pure part between the th: comments" if raw is None
            else f"LANG_JS has no one-line window.{', window.'.join(missing)}" if missing else "")
    ok = run_cases("as written", code, note) and ok
    mini = None
    if code is not None:
        try:
            mini = generator()["_minify_js"](code)
        except Exception as e:  # the minifier refusing is a failure of this check
            note = f"the minifier raised {type(e).__name__}: {e}"
    ok = run_cases("minified", mini, note) and ok
    ok = data_checks() and ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
