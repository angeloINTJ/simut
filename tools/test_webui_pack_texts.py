#!/usr/bin/env python3
"""Texts the web page writes by itself, held to the language pack.

The page is written in English and the device serves the other language: every
visible text goes through window.t('key', 'English') (or a data-i18n attribute),
and the pack's @WEBDICT answers. A text typed into the page outside both stays
as typed, in whatever language its author wrote it, on every page of every
pack. The capture of the manual's figures on 2026-11-06 (v2.14.0, in Portuguese)
found four such places; each has its check here.

  * the export box of the history page (_showExpProgress( ), WebUI.h): its
    title said "Exporting..." in English and its button "Cancelar" in
    Portuguese, both typed into the markup, and the error counter said "err".
    The function runs under node, as written and as the build's minifier leaves
    it, against a small DOM that records every text the box shows. With a
    window.t( ) that wraps its answer in [key], a text with a letter left over
    (after the [key]s, the digits and the unit symbols h, m and s, and the word
    OK, which is the same in the three languages) is a text typed into the page.
    Then the keys the box asked for are held to both packs.

Each check takes the code it judges, so --mutations can put the violations back
by hand (one mutant per text) and expect each to be found; the clean code must
give none.

Run: python3 tools/test_webui_pack_texts.py [--webui PATH] [--mutations]
  --webui PATH   judge another copy of WebUI.h (the negative control: the one
                 before the fix, from `git show <commit>:WebUI.h`)
Exit status is 0 on pass, 1 on failure.
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
GEN = REPO / "tools" / "build_webui_gz.py"
PACK_FILES = {"pt-BR": REPO / "data" / "lang" / "language_pt-BR.lng",
              "es-ES": REPO / "data" / "lang" / "language_es-ES.lng"}
WEBUI = REPO / "WebUI.h"
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
    return cond


def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(GEN.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(GEN), "exec"), ns)
    return ns


def pack_dict(lang):
    s = PACK_FILES[lang].read_text(encoding="utf-8")
    m = re.search(r"^@WEBDICT\n(\{.*\})$", s, re.M)
    if not m:
        raise SystemExit(f"{PACK_FILES[lang].name}: no @WEBDICT line")
    return json.loads(m.group(1))


def run_node(js):
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed")
    r = subprocess.run([node, "-e", js], capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        raise RuntimeError("node rejected it: " + r.stderr[-1500:])
    return json.loads(r.stdout.strip().splitlines()[-1])


def stray_letters(text):
    """The letters of a visible text that no window.t( ) wrote: what is left
    after the [key] answers of the marker t( ), a number with its unit symbol
    (24h, ~2m5s) and the word OK."""
    rest = re.sub(r"\[[A-Za-z0-9_]+\]", "", text)
    rest = re.sub(r"\d+(?:\.\d+)?\s?[hms](?![A-Za-z])", "", rest)
    rest = re.sub(r"(?<![A-Za-z])OK(?![A-Za-z])", "", rest)
    return re.findall(r"[A-Za-z]+", rest)


# ── the export box ─────────────────────────────────────────────────────────

# A DOM just large enough for _showExpProgress( ): elements by id, innerHTML
# parsed into them, and every text the box shows (text nodes, textContent,
# and the attributes a reader sees) pushed to `texts` in the order it appears.
EXPORT_HARNESS = r"""
const MARK = new Proxy({}, { get: (_, k) => '[' + String(k) + ']' });
const STATES = [
    { pct: 0,   ok: 0, fail: 0, chunkSecs: 86400, etaMs: -1 },
    { pct: 40,  ok: 3, fail: 2, chunkSecs: 86400, etaMs: 45000 },
    { pct: 55,  ok: 4, fail: 0, chunkSecs: 43200, etaMs: 125000 },
    { pct: 70,  ok: 5, fail: 0, chunkSecs: 86400, etaMs: 1500000 },
    { pct: 100, ok: 7, fail: 0, chunkSecs: 86400, etaMs: 0 },
];
function run(dict) {
    const asked = {}, texts = [], byId = {};
    const unesc = s => s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, '&');
    function El(tag) { this.tag = tag; this.id = ''; this.className = ''; this.style = {}; this._t = ''; this.onclick = null; this.disabled = false; }
    for (const p of ['textContent', 'innerText'])
        Object.defineProperty(El.prototype, p, { get() { return this._t; }, set(v) { this._t = String(v); texts.push(this._t); } });
    Object.defineProperty(El.prototype, 'innerHTML', { get() { return ''; }, set(h) {
        const re = /<(\/?)([a-z0-9]+)([^>]*)>|([^<]+)/gi;
        let m;
        while ((m = re.exec(h))) {
            if (m[4] !== undefined) { if (m[4].trim() !== '') texts.push(unesc(m[4])); continue; }
            if (m[1]) continue;
            const el = new El(m[2]);
            for (const a of m[3].matchAll(/\b([a-z-]+)="([^"]*)"/gi)) {
                if (a[1] === 'id') { el.id = a[2]; byId[el.id] = el; }
                else if (['title', 'aria-label', 'placeholder', 'alt'].includes(a[1])) texts.push(unesc(a[2]));
            }
        }
    } });
    El.prototype.remove = function () { delete byId[this.id]; };
    const document = {
        createElement: tag => new El(tag),
        getElementById: id => byId[id] || null,
        body: { appendChild: el => { if (el.id) byId[el.id] = el; } },
    };
    const window = { t: (k, d) => { asked[k] = d; return dict && dict[k] ? dict[k] : d; } };
    const escHtml = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    let _expAbort = null, _expCancelled = false;
    __CODE__
    for (const st of STATES) _showExpProgress(st);
    const x = document.getElementById('expX');
    if (x && x.onclick) x.onclick();
    _hideExpProgress();
    return { texts, asked, gone: !document.getElementById('expOv') };
}
const out = { marker: run(MARK), dicts: {} };
for (const [lang, d] of Object.entries(__DICTS__)) out.dicts[lang] = run(d);
console.log(JSON.stringify(out));
"""


def export_box_code(src):
    """_fmtEta( ), _showExpProgress( ) and _hideExpProgress( ), cut out of WebUI.h by
    their names: the box and everything it calls that the page does not share."""
    a = src.find("function _fmtEta(")
    m = re.search(r"function _hideExpProgress\(\) \{.*?\n        \}", src[a:], re.S) if a >= 0 else None
    if a < 0 or m is None or src.count("function _showExpProgress(") != 1:
        return None
    return src[a:a + m.end()]


def export_box_findings(code, ns, dicts):
    """What is wrong with the export box in this code: [] when nothing."""
    found = []
    for label, js in (("as written", code), ("minified", ns["_minify_web_block"](code, "js"))):
        try:
            res = run_node(EXPORT_HARNESS.replace("__CODE__", js).replace("__DICTS__", json.dumps(dicts)))
        except RuntimeError as e:
            found.append(f"{label}: {e}")
            continue
        marker = res["marker"]
        if not marker["gone"]:
            found.append(f"{label}: _hideExpProgress( ) leaves the box on the page")
        typed = {}
        for t in marker["texts"]:
            if stray_letters(t):
                typed.setdefault(" ".join(stray_letters(t)), set()).add(t)
        for words, shown in sorted(typed.items()):
            found.append(f"{label}: the box shows {words!r}, typed into the page and not asked of window.t( ) "
                         f"(in {', '.join(repr(t) for t in sorted(shown)[:3])})")
        for lang, d in dicts.items():
            for k, en in res["dicts"][lang]["asked"].items():
                tr = d.get(k, "")
                if not tr.strip():
                    found.append(f"{label}: {lang}: @WEBDICT has no {k!r} (the box asks for it)")
                elif tr == en:
                    found.append(f"{label}: {lang}: @WEBDICT {k!r} is the English {en!r}, not a translation")
            shown = res["dicts"][lang]["texts"]
            for k, en in res["dicts"][lang]["asked"].items():
                if d.get(k, en) != en and en in shown:
                    found.append(f"{label}: {lang}: the box still shows the English {en!r} of {k!r}")
    return sorted(set(found))


def test_export_box(src, ns, dicts):
    code = export_box_code(src)
    if not check(code is not None, "_showExpProgress( ), _fmtEta( ) or _hideExpProgress( ) is not in WebUI.h "
                                   "(or _showExpProgress is defined twice). If they moved or were renamed, "
                                   "move this test with them."):
        return
    for f in export_box_findings(code, ns, dicts):
        fails.append(f"export box: {f}")


# Each mutant puts one violation back into the fixed code and must give exactly
# the findings listed (the as-written and minified runs of one violation count
# as one): a typed text is one finding; a key no pack has is one per pack.
EXPORT_MUTANTS = [
    ("the title typed in English",
     r"escHtml\(window\.t\('exp_title', 'Exporting\.\.\.'\)\)", "'Exporting...'", 1),
    ("the Cancel button typed in Portuguese",
     r"escHtml\(window\.t\('hist_cancel', 'Cancel'\)\)", "'Cancelar'", 1),
    ("the error counter typed as 'err'",
     r"' ' \+ window\.t\('dash_metr_err', 'err'\)", "' err'", 1),
    ("a key that no pack has", r"'exp_title'", "'exp_titulo_que_nao_existe'", 2),
]


def test_export_mutants(src, ns, dicts):
    code = export_box_code(src)
    if code is None:
        return
    check(export_box_findings(code, ns, dicts) == [], "export mutants: the code they start from is not clean")
    for name, pat, rep, want in EXPORT_MUTANTS:
        mutated, n = re.subn(pat, lambda m, r=rep: r, code)
        if not check(n == 1, f"export mutant {name!r}: its pattern matches {n} times in the code, not once "
                             f"(the mutant no longer describes the code: update it)"):
            continue
        got = {re.sub(r"^(as written|minified): ", "", f) for f in export_box_findings(mutated, ns, dicts)}
        check(len(got) == want, f"export mutant {name!r}: {len(got)} findings, not {want}: {sorted(got)}")


def main():
    global WEBUI
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--webui", help="judge this copy of WebUI.h")
    ap.add_argument("--mutations", action="store_true", help="also put each violation back and expect it found")
    args = ap.parse_args()
    if args.webui:
        WEBUI = Path(args.webui)
    src = WEBUI.read_text(encoding="utf-8")
    ns = generator()
    dicts = {lang: pack_dict(lang) for lang in PACK_FILES}
    tests = [("test_export_box", lambda: test_export_box(src, ns, dicts))]
    if args.mutations:
        tests.append(("test_export_mutants", lambda: test_export_mutants(src, ns, dicts)))
    for name, t in tests:
        before = len(fails)
        try:
            t()
        except SystemExit as e:
            if str(e).startswith("SKIP"):
                print(e)
                return 0
            fails.append(f"{name}: {e}")
        except Exception as e:  # a crash is a failure with its reason
            fails.append(f"{name}: {type(e).__name__}: {e}")
        print(f"  {'ok  ' if len(fails) == before else 'FAIL'} {name}")
    for f in fails:
        print(f"  FAIL {f}")
    print("PASS" if not fails else f"FAIL ({len(fails)})")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
