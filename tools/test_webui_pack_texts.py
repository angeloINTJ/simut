#!/usr/bin/env python3
"""Texts the web page writes by itself, held to the language pack.

The page is written in English and the device serves the other language: every
visible text goes through window.t('key', 'English') (or a data-i18n attribute),
and the pack's @WEBDICT answers. A text typed into the page outside both stays
as typed, in whatever language its author wrote it, on every page of every
pack. The capture of the manual's figures on 2026-11-06 (v2.14.0, in Portuguese)
found the places below; each has its check here.

  * the export box of the history page (_showExpProgress( ), WebUI.h): its
    title said "Exporting..." in English and its button "Cancelar" in
    Portuguese, both typed into the markup, and the error counter said "err".
    The function runs under node, as written and as the build's minifier leaves
    it, against a small DOM that records every text the box shows. With a
    window.t( ) that wraps its answer in [key], a text with a letter left over
    (after the [key]s, the digits and the unit symbols h, m and s, and the word
    OK, which is the same in the three languages) is a text typed into the page.
    Then the keys the box asked for are held to both packs.

  * the link state of the network page (loadNet( ), NET_PAGE): the card says
    Connected or Disconnected, and the page painted it once, from window.t( ),
    when /api/network answered. The dictionary comes from /api/lang, a second
    request, and the device answers one request at a time: when /api/network
    answered first, t( ) had no dictionary yet, the word stayed in English, and
    applyLang( ) (which relabels only the elements that carry data-i18n) never
    came back to it (the Portuguese page of the v2.14.0 capture, 2026-11-06).
    loadNet( ) runs under node, as written and minified, with lang.js's own
    t( ), escHtml( ), applyLang( ) and the line that fetches the dictionary, in
    the orders the device can answer in: dictionary first, network first, and a
    first visit (no language stored yet). The card must read the pack's word at
    the end of each, and switch with the language.

  * the melody names of Alarms & Sounds (ALARMS_PAGE): thirty <option>s in five
    selects, three of them with a data-i18n key and the rest typed in English,
    so the Portuguese page showed Click, Ascending, Descending, Dual Beep and
    Notify. Every option now has a key, both packs carry it, and its word is
    the one the Ângulo's panel draws for the same melody (@ANGULO, by the
    select and the index, read off nomeMelodia( ) in AjustesSons.cpp): web and
    panel say the same. A key alone did not show: the custom select
    (_makeCustomSelect, lang.js) copies the options' text when it is built and
    when .value is set, and applyLang( ) translates the options after that, so
    even 'Ascending' kept its English on the button. applyLang( ) runs under
    node (lang.js's own line) against selects that record when they are synced:
    it must sync again every select whose options it relabelled, after the
    relabelling, and none when nothing changed (a poll must not rebuild an open
    menu).

  * the probe scan of the sensor page (sensScan( ), CFG_PAGE): a BME280 or BMP280
    came back from the firmware as {"pin":255,...} and the page printed "GP255".
    The firmware's JSON is built by the firmware's own formatter
    (src/sensors/ScanResultJson.h, compiled here with g++ the way the native
    suite compiles it), and sensScan( ) runs under node, as written and
    minified, against it: the line must be what the JSON says, pins and chip
    (GP4/GP5 (BMP280, I²C)), with the pins of a pair no constant in the page
    knows (GP8/GP9), the 1-Wire and DHT lines as they were, and never GP255.

  * the Origem selector of the history page (_updateSensorMselLabel( ),
    HIST_PAGE): its label said "Carregando..." over the sensor's name, with the
    list open and the chart drawn. The label carried the markup's
    data-i18n="hist_loading" for good, so applyLang( ), which rewrites every
    element that carries data-i18n, put the word back when /api/lang answered
    after /api/status, and at a language switch in any language (the English
    returned as the data-en the first applyLang( ) had recorded: "Loading...").
    The function runs under node, as written and minified, with lang.js's own
    t( ), escHtml( ), applyLang( ) and its /api/lang line, against the button as
    the page's markup has it and a menu of two sensors, in the orders the device
    can answer in (dictionary first, dictionary late, first visit), with one
    sensor, two and none. Then the language is switched to English and back, and
    the selection moved to each of the other two states and back to the first,
    with the switches at each. What the selector reads (the count's text and the
    label's) must be the sensor's "ID — name" at every moment, "2 selecionados"
    / "2 seleccionados" / "2 selected" for two, "Nenhum" / "Ninguno" / "None"
    for none.

  * the Max and Min badges above the history chart (renderExtremes( ),
    HIST_PAGE): the page in Portuguese read "Max TEMP: 27.7 °C" on the desktop
    capture and "Max Temp: 28.3 °C" on the phone's. "Max " and "Min " were typed
    into the markup, outside window.t( ), and the channel's name was painted
    once from t( ) with key.toUpperCase() as its English: when /api/lang
    answered after the history data the badge kept "TEMP", and nothing relabelled
    it, since no element there carried data-i18n. renderExtremes( ) runs under
    node, as written and minified, with lang.js's own t( ), escHtml( ),
    applyLang( ) and its /api/lang line, against a box that parses the badges'
    markup into what a reader sees, with the extremes the page measures (three
    channels, with their labels, and a fourth with none, so the name's key is
    built from the channel's) and with the minT/maxT form of an older firmware;
    in the orders the device can answer in, then the language switched to
    English and back. Before the dictionary and in English a badge reads "Max
    Temp:", "Max Hum:", "Max Press:" (the key capitalized, the word the packs
    translate); in the pack's language, the pack's words ("Máx Umid:" in pt-BR,
    "Máx Pres:" in es-ES). The packs must carry hist_max and hist_min, not the
    English.

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
    { pct: 50,  ok: 3, fail: 1, chunkSecs: 86400, etaMs: 60000 },
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
            for (const a of m[3].matchAll(/\b([a-z][a-z0-9-]*)="([^"]*)"/gi)) {
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
    const counters = [];   /* what the failure counter says after each state, with the number of failures that state holds */
    for (const st of STATES) { _showExpProgress(st); counters.push([st.fail, document.getElementById('expN').textContent]); }
    const x = document.getElementById('expX');
    if (x && x.onclick) x.onclick();
    _hideExpProgress();
    return { texts, asked, counters, gone: !document.getElementById('expOv') };
}
const out = { marker: run(MARK), english: run(null), dicts: {} };
for (const [lang, d] of Object.entries(__DICTS__)) out.dicts[lang] = run(d);
console.log(JSON.stringify(out));
"""


# The failure counter says "<n> <word>": a word that is the same for 1 and for many, in each language. The dashboard's
# dash_metr_err ("err", "erro", "error") was reused here and wrote "2 erro" and "2 error".
COUNTER_WORDS = {"English": "failed", "pt-BR": "com falha", "es-ES": "con fallo"}


def export_class(finding):
    """The kind of finding, for the mutants: what a violation must show up as."""
    f = re.sub(r"^(as written|minified): ", "", finding)
    for tag, pat in (("typed", "typed into the page"), ("counter", "failure counter reads"), ("key", "does not ask for exp_failed"),
                     ("packkey", "@WEBDICT has no"), ("copy", "is the English"), ("leak", "still shows the English"),
                     ("left", "leaves the box")):
        if pat in f:
            return tag
    return "other"


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
        asked = marker["asked"]
        if "exp_failed" not in asked or "dash_metr_err" in asked:
            found.append(f"{label}: the failure counter does not ask for exp_failed, or still asks for the dashboard's dash_metr_err "
                         f"(the box asks for {sorted(asked)}): that word is a noun that is wrong for a count ('2 erro')")
        for who, run_ in [("English", res["english"])] + [(lang, res["dicts"][lang]) for lang in dicts]:
            word = COUNTER_WORDS[who]
            want = [[n, f"{n} {word}"] for n, _ in run_["counters"]]
            if run_["counters"] != want:
                found.append(f"{label}: {who}: the failure counter reads {[t for _, t in run_['counters']]} for "
                             f"{[n for n, _ in run_['counters']]} failures, wanted {[t for _, t in want]}: one word that reads right for 1 and for many")
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


# Each mutant puts one violation back into the fixed code and must show up as
# exactly the classes of finding listed (the as-written and minified runs, and the
# packs, of one violation count as one class).
EXPORT_MUTANTS = [
    ("the title typed in English",
     r"escHtml\(window\.t\('exp_title', 'Exporting\.\.\.'\)\)", "'Exporting...'", {"typed"}),
    ("the Cancel button typed in Portuguese",
     r"escHtml\(window\.t\('hist_cancel', 'Cancel'\)\)", "'Cancelar'", {"typed"}),
    ("the error counter typed as 'err'",
     r"' ' \+ window\.t\('exp_failed', 'failed'\)", "' err'", {"typed", "counter", "key"}),
    ("the error counter back on the dashboard's noun (the review's finding: '2 erro')",
     r"window\.t\('exp_failed', 'failed'\)", "window.t('dash_metr_err', 'err')", {"counter", "key"}),
    ("a key that no pack has", r"'exp_title'", "'exp_titulo_que_nao_existe'", {"packkey"}),
    ("a counter key that no pack has", r"'exp_failed'", "'exp_falhou_que_nao_existe'", {"packkey", "counter", "key"}),
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
        got = {export_class(f) for f in export_box_findings(mutated, ns, dicts)}
        check(got == want, f"export mutant {name!r}: found as {sorted(got)}, wanted {sorted(want)}")


# ── the link state of the network page ─────────────────────────────────────

def block(src, name):
    """One page or asset of WebUI.h: from its declaration to the end of its raw string."""
    a = src.find(f"static const char {name}[] PROGMEM")
    return src[a:src.find(')raw";', a)] if a >= 0 else ""


def lang_js_parts(src):
    """(code, missing): lang.js's own one-line window.t, window.escHtml and
    window.applyLang, and the line that asks /api/lang for the dictionary."""
    lang = block(src, "LANG_JS")
    found, missing = [], []
    for name, pat in (("window.t", r"window\.t = function.*"), ("window.escHtml", r"window\.escHtml = function.*"),
                      ("window.applyLang", r"window\.applyLang = function.*"),
                      ("the /api/lang fetch", r"fetch\('/api/lang'\).*")):
        m = re.search(rf"^[ \t]*({pat})$", lang, re.M)
        (found if m else missing).append(m.group(1) if m else name)
    return found, missing


def load_net_code(src):
    """loadNet( ) of the network page, cut out by its name and the next function's."""
    net = block(src, "NET_PAGE")
    a = net.find("async function loadNet() {")
    b = net.find("function wireNetPendingListeners() {", a) if a >= 0 else -1
    return net[a:b] if a >= 0 and b > a else None


# What node does not have: the page's storage and DOM (the elements loadNet( )
# and applyLang( ) touch, made on demand by id), the two requests, and the
# dictionary. fetch( ) is lang.js's own: it answers /api/lang when the scenario
# says the dictionary has arrived. A scenario is the order the device can answer
# in; what the card reads is the text of #lbl_stat.
LINK_HARNESS = r"""
var __store = {};
Object.defineProperty(globalThis, 'localStorage', { configurable: true, writable: true, value: {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(__store, k) ? __store[k] : null; },
    setItem: function (k, v) { __store[k] = String(v); } } });
var __els = {};
class __El {
    constructor(id) { this.id = id; this.tagName = 'DIV'; this.style = {}; this.value = ''; this.checked = false; this._a = {}; this._h = ''; }
    get innerHTML() { return this._h; }  set innerHTML(v) { this._h = String(v); }
    get innerText() { return this._h; }  set innerText(v) { this._h = String(v); }
    get textContent() { return this._h; }  set textContent(v) { this._h = String(v); }
    getAttribute(n) { return Object.prototype.hasOwnProperty.call(this._a, n) ? this._a[n] : null; }
    setAttribute(n, v) { this._a[n] = String(v); }
    hasAttribute(n) { return Object.prototype.hasOwnProperty.call(this._a, n); }
}
Object.defineProperty(globalThis, 'document', { configurable: true, writable: true, value: {
    getElementById: function (id) { return __els[id] || (__els[id] = new __El(id)); },
    querySelectorAll: function (sel) {
        return sel === '[data-i18n]' ? Object.keys(__els).map(function (k) { return __els[k]; }).filter(function (e) { return e.hasAttribute('data-i18n'); }) : [];
    },
    createElement: function () { var s = ''; return {
        set textContent(v) { s = String(v); },
        get innerHTML() { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); } }; } } });
var window = globalThis;
var dict = { pt: {} };
var Pending = { getSection: function () { return {}; } };
function toggleIpFields() {}
function toggleDnsFields() {}
function wireNetPendingListeners() {}
var __net = null, __pack = null, __arrive = null;
globalThis.fetchSafe = async function () { return { json: async function () { return __net; } }; };
globalThis.fetch = function () { return new Promise(function (res) { __arrive = function () { res({ json: function () { return __pack; } }); }; }); };
__LANG_JS__
function __startLang() {
__FETCH_LINE__
}
__CODE__
var __tick = function () { return new Promise(function (r) { setTimeout(r, 0); }); };
var __status = function () { return __els['lbl_stat'] ? __els['lbl_stat'].innerText : null; };
async function __scenario(kind, connected, pack) {
    __store = {}; __els = {}; dict.pt = {};
    __net = { ip: '192.168.3.33', mask: '255.255.255.0', gw: '192.168.3.1', dns: '192.168.3.1', mac: '88:A2:9E:76:B6:B0',
              connected: connected, ssid: 'Net', use_dhcp: true, web_port: 8080, ntp_enabled: true };
    __pack = pack;
    if (kind !== 'first-visit') localStorage.setItem('simut_lang', 'pt');
    __startLang();                                        /* lang.js asks for the dictionary as it loads */
    var obs = {};
    if (kind === 'dict-first') { __arrive(); await __tick(); await loadNet(); obs.painted = __status(); }
    else { await loadNet(); obs.painted = __status(); __arrive(); await __tick(); obs.arrived = __status(); }
    localStorage.setItem('simut_lang', 'en'); applyLang(); obs.en = __status();
    localStorage.setItem('simut_lang', 'pt'); applyLang(); obs.pt = __status();
    return obs;
}
(async function () {
    var out = {}, packs = __PACKS__;
    for (var lang of Object.keys(packs))
        for (var kind of ['dict-first', 'network-first', 'first-visit'])
            for (var conn of [true, false])
                out[lang + '|' + kind + '|' + conn] = await __scenario(kind, conn, packs[lang]);
    console.log(JSON.stringify(out));
})().catch(function (e) { console.log(JSON.stringify({ error: String((e && e.stack) || e) })); });
"""


def link_state_findings(code, parts, ns, dicts):
    """What is wrong with the card's status in this loadNet( ): [] when nothing."""
    found = []
    for label, js in (("as written", code), ("minified", ns["_minify_web_block"](code, "js"))):
        prog = (LINK_HARNESS.replace("__LANG_JS__", "\n".join(parts[:3])).replace("__FETCH_LINE__", parts[3])
                .replace("__CODE__", js).replace("__PACKS__", json.dumps(dicts)))
        try:
            res = run_node(prog)
        except RuntimeError as e:
            found.append(f"{label}: {e}")
            continue
        if "error" in res:
            found.append(f"{label}: the scenarios threw {res['error'][:300]}")
            continue
        for key, obs in sorted(res.items()):
            lang, kind, conn = key.split("|")
            on = conn == "true"
            tr = dicts[lang].get("net_conn" if on else "net_off", "")
            en = "Connected" if on else "Disconnected"
            who = f"{lang}, {'connected' if on else 'disconnected'}, {kind}"
            if not tr.strip() or tr == en:
                found.append(f"{label}: {who}: @WEBDICT has {'net_conn' if on else 'net_off'} = {tr!r}, "
                             f"which is not a translation of {en!r}")
                continue
            for when, got, want in ((("painted", obs.get("painted"), tr) if kind == "dict-first" else
                                      ("arrived", obs.get("arrived"), tr)),
                                    ("switched to English", obs.get("en"), en),
                                    ("switched back", obs.get("pt"), tr)):
                if got != want:
                    found.append(f"{label}: {who}: the card reads {got!r} {when}, wanted {want!r}")
    return sorted(set(found))


def test_link_state(src, ns, dicts):
    parts, missing = lang_js_parts(src)
    code = load_net_code(src)
    if not check(not missing, f"lang.js lacks {missing} as one line each; this test runs lang.js's own"):
        return
    if not check(code is not None, "loadNet( ) is not in the network page of WebUI.h (or wireNetPendingListeners( ) "
                                   "no longer follows it). If they moved or were renamed, move this test with them."):
        return
    for f in link_state_findings(code, parts, ns, dicts):
        fails.append(f"link state: {f}")


# Each mutant takes one piece of the fix out. What it must give is the set of
# moments at which the card reads wrong, whatever the pack or the state: a card
# that cannot be relabelled misses `arrived` and both switches; one with no
# English of its own gives the translated word for the English; one painted from
# the literal misses only `painted`.
LINK_MUTANTS = [
    ("no data-i18n on the card: applyLang( ) cannot come back to it",
     r"s\.setAttribute\('data-i18n', up \? 'net_conn' : 'net_off'\);\s*", "", {"arrived", "switched to English", "switched back"}),
    ("no data-en: the first applyLang( ) takes the translated text for the English",
     r"s\.setAttribute\('data-en', up \? 'Connected' : 'Disconnected'\);\s*", "", {"switched to English"}),
    ("painted from the English literal, not from t( )",
     r"up \? window\.t\('net_conn', 'Connected'\) : window\.t\('net_off', 'Disconnected'\)", "(up ? 'Connected' : 'Disconnected')", {"painted"}),
]


def test_link_mutants(src, ns, dicts):
    parts, missing = lang_js_parts(src)
    code = load_net_code(src)
    if missing or code is None:
        return
    check(link_state_findings(code, parts, ns, dicts) == [], "link mutants: the code they start from is not clean")
    for name, pat, rep, want in LINK_MUTANTS:
        mutated, n = re.subn(pat, lambda m, r=rep: r, code)
        if not check(n == 1, f"link mutant {name!r}: its pattern matches {n} times in the code, not once "
                             f"(the mutant no longer describes the code: update it)"):
            continue
        got = {m.group(1) for f in link_state_findings(mutated, parts, ns, dicts)
               for m in [re.search(r"reads '[^']*' (painted|arrived|switched to English|switched back), wanted", f)] if m}
        check(got == want, f"link mutant {name!r}: the card reads wrong at {sorted(got)}, wanted {sorted(want)}")


# ── the melody names of the sound settings ─────────────────────────────────

ANGULO_SONS = REPO / "src" / "ui" / "angulo" / "telas" / "AjustesSons.cpp"
SOM_IDS = {"TOQUE": "touch", "CONFIRMACAO": "confirm", "ERRO": "error", "ALARME": "alarm", "ATENCAO": "attention"}
# The one word that is the same in the three languages (the musical term), so
# the packs keep it as it is, the Ângulo's too.
SAME_WORD = {"Staccato"}
_angulo_cache = {}


def fnv1a32(s):
    h = 0x811c9dc5
    for b in s.encode("utf-8"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def angulo_texts(lang):
    """{FNV-1a of the English: text} of the pack's @ANGULO section."""
    if lang not in _angulo_cache:
        lines = PACK_FILES[lang].read_text(encoding="utf-8").split("\n")
        out = {}
        for ln in lines[lines.index("@ANGULO") + 1:]:
            if ln.startswith("@"):
                break
            if ln.strip():
                h, _, txt = ln.partition(" ")
                out[int(h, 16)] = txt
        _angulo_cache[lang] = out
    return _angulo_cache[lang]


def angulo_melodies():
    """{select id: {index: the Ângulo's English}} read off nomeMelodia( )."""
    text = ANGULO_SONS.read_text(encoding="utf-8")
    a = text.index("const char *nomeMelodia(")
    body = text[a:text.index("\n}\n", a)]
    out = {}
    for m in re.finditer(r"case SOM_(\w+):\s*switch \( melodia \) \{(.*?)\}\s*break;", body, re.S):
        out[SOM_IDS[m.group(1)]] = {int(i): en for i, en in re.findall(r'case (\d+): return UA_TXT\( "([^"]+)" \);', m.group(2))}
    return out


def melody_selects(src):
    """{id: [(value, data-i18n key or None, English text)]} for each select.mel-sel of the alarms page."""
    page = block(src, "ALARMS_PAGE")
    out = {}
    for m in re.finditer(r'<select id="mel_(\w+)" class="mel-sel">(.*?)</select>', page, re.S):
        opts = []
        for o in re.finditer(r'<option value="(\d+)"([^>]*)>([^<]*)</option>', m.group(2)):
            k = re.search(r'data-i18n="([^"]+)"', o.group(2))
            opts.append((int(o.group(1)), k.group(1) if k else None, o.group(3)))
        out[m.group(1)] = opts
    return out


def melody_findings(src, dicts):
    """What is wrong with the melody names in this page and these packs: [] when nothing."""
    found = []
    sels, ang = melody_selects(src), angulo_melodies()
    if sorted(sels) != sorted(ang) or any(len(o) != len(ang[i]) for i, o in sels.items()):
        return [f"the alarms page has the melody selects {sorted(sels)} with {[len(o) for o in sels.values()]} options, "
                f"the Ângulo {sorted(ang)} with {[len(v) for v in ang.values()]}: the two lists no longer line up"]
    for sid, opts in sels.items():
        for val, key, en in opts:
            where = f"#mel_{sid} option {val} ({en!r})"
            if not key:
                found.append(f"{where} has no data-i18n key, so it stays in English on every pack")
                continue
            for lang, d in dicts.items():
                word = d.get(key, "")
                panel = angulo_texts(lang).get(fnv1a32(ang[sid][val]))
                if not word.strip():
                    found.append(f"{lang}: @WEBDICT has no {key!r} ({where})")
                elif panel is None:
                    found.append(f"{lang}: @ANGULO has no text for {ang[sid][val]!r}, the Ângulo's name for {where}")
                elif word != panel:
                    found.append(f"{lang}: {where} reads {word!r} on the web and {panel!r} on the Ângulo's panel")
                elif word == en and en not in SAME_WORD:
                    # the web and the panel agree, and both are English: the pack never translated it
                    found.append(f"{lang}: {where} is the English {en!r} on the web and on the panel: not a translation")
    return sorted(set(found))


# What node does not have: the page's storage, the elements applyLang( ) walks
# ([data-i18n], each option knowing its select) and the custom select. The
# custom select is the part this test stands in for: _cselSync( ) here only
# records what the select's options say when it is called, as the real one
# copies the options' text into the button and the menu (and, like the real one,
# does nothing for a select whose wrapper is not built).
SYNC_HARNESS = r"""
var __store = {};
Object.defineProperty(globalThis, 'localStorage', { configurable: true, writable: true, value: {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(__store, k) ? __store[k] : null; },
    setItem: function (k, v) { __store[k] = String(v); } } });
var __opts = [], __synced = [];
Object.defineProperty(globalThis, 'document', { configurable: true, writable: true, value: {
    querySelectorAll: function (sel) { return sel === '[data-i18n]' ? __opts : []; },
    createElement: function () { var s = ''; return {
        set textContent(v) { s = String(v); },
        get innerHTML() { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); } }; } } });
var window = globalThis;
var dict = { pt: {} };
__LANG_JS__
globalThis._cselSync = function (s) {
    if (!s._cw) return;
    __synced.push({ id: s.id, labels: s.options.map(function (o) { return o.innerHTML; }) });
};
function __make(sels) {
    __opts.length = 0; __synced.length = 0;
    return sels.map(function (d) {
        var sel = { id: d.id, tagName: 'SELECT', _cw: d.wrapped ? {} : undefined, options: [] };
        d.options.forEach(function (od) {
            var attrs = od.key ? { 'data-i18n': od.key } : {}, html = od.en;
            var o = { tagName: 'OPTION', parentNode: sel,
                get innerHTML() { return html; }, set innerHTML(v) { html = String(v); },
                getAttribute: function (n) { return Object.prototype.hasOwnProperty.call(attrs, n) ? attrs[n] : null; },
                setAttribute: function (n, v) { attrs[n] = String(v); },
                hasAttribute: function (n) { return Object.prototype.hasOwnProperty.call(attrs, n); } };
            sel.options.push(o);
            if (od.key) __opts.push(o);
        });
        return sel;
    });
}
var __labels = function (sels) { return sels.map(function (s) { return s.options.map(function (o) { return o.innerHTML; }); }); };
(function () {
    var spec = __SPEC__, out = {};
    /* the page as the browser parses it: English options, the dictionary not here yet */
    var sels = __make(spec.selects);
    localStorage.setItem('simut_lang', 'pt');
    applyLang();                                         /* loadAlarms( ) calls it before /api/lang has answered */
    out.beforeDict = { syncs: __synced.length, labels: __labels(sels) };
    dict.pt = spec.pack; applyLang();                    /* the dictionary arrives */
    out.afterDict = { synced: __synced.slice(), labels: __labels(sels) };
    var n = __synced.length;
    applyLang(); applyLang();                            /* the dashboard-style repeated calls: nothing changed */
    out.repeated = __synced.length - n;
    n = __synced.length;
    localStorage.setItem('simut_lang', 'en'); applyLang();
    out.toEnglish = { synced: __synced.slice(n), labels: __labels(sels) };
    n = __synced.length;
    localStorage.setItem('simut_lang', 'pt'); applyLang();
    out.backToPt = { synced: __synced.slice(n), labels: __labels(sels) };
    /* the dictionary first: lang.js applied it before the options were in the DOM, then the page's first applyLang( ) */
    dict.pt = spec.pack; sels = __make(spec.selects);
    applyLang();
    out.dictFirst = { synced: __synced.slice(), labels: __labels(sels) };
    console.log(JSON.stringify(out));
})();
"""


def apply_lang_line(src):
    m = re.search(r"^[ \t]*(window\.applyLang = function.*)$", block(src, "LANG_JS"), re.M)
    return m.group(1) if m else None


def lang_helpers(src):
    """lang.js's own one-line window.escHtml and window.applyLang."""
    lang = block(src, "LANG_JS")
    got = [re.search(rf"^[ \t]*(window\.{n} = function.*)$", lang, re.M) for n in ("escHtml", "applyLang")]
    return [g.group(1) for g in got] if all(got) else None


def sync_findings(helpers, src, ns, dicts):
    """What is wrong with applyLang( ) and the custom selects of the melody names: [] when nothing."""
    sels = melody_selects(src)
    found = []
    for lang, d in dicts.items():
        keys = {k for opts in sels.values() for _, k, _ in opts if k}
        spec = {"pack": {k: d[k] for k in keys if k in d},
                "selects": [{"id": f"mel_{sid}", "wrapped": True,
                             "options": [{"key": k, "en": en} for _, k, en in opts]} for sid, opts in sels.items()]
                         # a select the custom wrapper has not been built for, and one with no keyed option
                         + [{"id": "unwrapped", "wrapped": False, "options": [{"key": "alm_mel_asc", "en": "Ascending"}]},
                            {"id": "plain", "wrapped": True, "options": [{"key": None, "en": "Plain"}]}]}
        want = [[d.get(k, en) if k else en for _, k, en in opts] for opts in sels.values()] + [[d.get("alm_mel_asc", "Ascending")], ["Plain"]]
        english = [[en for _, k, en in opts] for opts in sels.values()] + [["Ascending"], ["Plain"]]
        for label, line in (("as written", helpers), ("minified", [ns["_minify_web_block"](h, "js") for h in helpers])):
            try:
                res = run_node(SYNC_HARNESS.replace("__LANG_JS__", "\n".join(line)).replace("__SPEC__", json.dumps(spec)))
            except RuntimeError as e:
                found.append(f"{label}: {lang}: {e}")
                continue
            ids = [f"mel_{sid}" for sid in sels]
            for when in ("afterDict", "dictFirst"):
                got = res[when]
                synced = {x["id"] for x in got["synced"]}
                missing = [i for i in ids if i not in synced]
                if missing:
                    found.append(f"{label}: {lang}: {when}: the custom select of {missing} is not synced after applyLang( ) relabelled its options")
                for x in got["synced"]:
                    k = ids.index(x["id"]) if x["id"] in ids else None
                    if k is not None and x["labels"] != want[k]:
                        found.append(f"{label}: {lang}: {when}: {x['id']} was synced while its options read {x['labels']}, wanted {want[k]}")
                if {"unwrapped", "plain"} & synced:
                    found.append(f"{label}: {lang}: {when}: a select with no wrapper, or with no relabelled option, was synced")
                if got["labels"] != want:
                    found.append(f"{label}: {lang}: {when}: the options read {got['labels']}, wanted {want}")
            if res["beforeDict"]["syncs"]:
                found.append(f"{label}: {lang}: applyLang( ) with no dictionary changed nothing, and still synced {res['beforeDict']['syncs']} selects")
            if res["repeated"]:
                found.append(f"{label}: {lang}: two more applyLang( ) calls with nothing changed synced {res['repeated']} selects again: "
                             f"an open menu would be rebuilt at every poll")
            for when, goal in (("toEnglish", english), ("backToPt", want)):
                got = res[when]
                missing = [i for i in ids if i not in {x["id"] for x in got["synced"]}]
                if missing:
                    found.append(f"{label}: {lang}: {when}: the custom select of {missing} is not synced after the language switch")
                if got["labels"] != goal:
                    found.append(f"{label}: {lang}: {when}: the options read {got['labels']}, wanted {goal}")
    return sorted(set(found))


def test_melody_names(src, ns, dicts):
    for f in melody_findings(src, dicts):
        fails.append(f"melody names: {f}")
    helpers = lang_helpers(src)
    if not check(helpers is not None, "lang.js lacks window.escHtml or window.applyLang as one line each; this test runs lang.js's own"):
        return
    for f in sync_findings(helpers, src, ns, dicts):
        fails.append(f"melody names, custom select: {f}")


# Each mutant breaks one thing; what it must give is listed by the part of the
# check that sees it.
def test_melody_mutants(src, ns, dicts):
    helpers = lang_helpers(src)
    if helpers is None:
        return
    check(melody_findings(src, dicts) == [] and sync_findings(helpers, src, ns, dicts) == [],
          "melody mutants: the code they start from is not clean")
    # (a) an option loses its key
    m, n = re.subn(r'<option value="3" data-i18n="alm_mel_snap">', '<option value="3">', src, count=1)
    if check(n == 1, "melody mutant 'an option without its key': its pattern matches nothing (update it)"):
        got = melody_findings(m, dicts)
        check(len(got) == 1 and "has no data-i18n key" in got[0], f"melody mutant 'an option without its key': {got}")
    # (b) a pack word that is not the panel's
    bad = {lang: dict(d, alm_mel_asc="Ascendente") for lang, d in dicts.items()}
    got = melody_findings(src, {"pt-BR": bad["pt-BR"]})
    check(len(got) == 1 and "'Ascendente' on the web and 'Subida' on the Ângulo's panel" in got[0], f"melody mutant 'a word other than the panel's': {got}")
    # (c) a pack that lacks a key, and one that keeps the English
    got = melody_findings(src, {"es-ES": {k: v for k, v in dicts["es-ES"].items() if k != "alm_mel_bell"}})
    check(len(got) == 1 and "has no 'alm_mel_bell'" in got[0], f"melody mutant 'a key missing from a pack': {got}")
    got = melody_findings(src, {"pt-BR": dict(dicts["pt-BR"], alm_mel_bubble="Bubble")})
    check(len(got) == 1 and "reads 'Bubble' on the web and 'Bolha' on the Ângulo's panel" in got[0],
          f"melody mutant 'the English kept in a pack': {got}")
    # (d) applyLang( ) without the sync, and one that syncs at every call
    line = helpers[1]
    for name, pat, rep, must in (
            ("no sync after the relabelling", r"relabeled\.forEach\(s => \{ if \(window\._cselSync\) _cselSync\(s\); \}\);", "", "is not synced after"),
            ("a sync at every call", r" && el\.innerHTML !== h", "", "an open menu would be rebuilt")):
        mut, k = re.subn(pat, lambda mm, r=rep: r, line, count=1)
        if not check(k == 1, f"melody mutant {name!r}: its pattern matches nothing in applyLang( ) (update it)"):
            continue
        got = sync_findings([helpers[0], mut], src, ns, dicts)
        check(got != [] and any(must in f for f in got), f"melody mutant {name!r}: the test did not find it ({len(got)} findings)")


# ── the probe scan of the sensor page ──────────────────────────────────────

# The scans the firmware can finish, as the firmware's own formatter writes them.
# The bus of the second I2C part is a pair no page and no driver constant knows:
# a page that printed 4/5 from a copy would fail on it.
SCAN_CPP = r"""
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <hardware/gpio.h>
#include "sensors/ScanResultJson.h"
static ScanResult probe(uint8_t pin, SensorType t) { ScanResult r; r.pin = pin; r.type = t; memset(r.rom, 0, 8); return r; }
static ScanResult bus(SensorType t, uint8_t sda, uint8_t scl) { ScanResult r = probe(PIN_UNUSED, t); r.sda = sda; r.scl = scl; return r; }
static std::string doc(const std::vector<ScanResult>& v) { std::string s; scanResultsJson(v, s); return s; }
int main() {
    ScanResult ds = probe(2, TYPE_DS18B20);
    const unsigned char rom[8] = {0x28, 0xFF, 0x64, 0x1E, 0x80, 0x16, 0x03, 0x4A};
    memcpy(ds.rom, rom, 8);
    ScanResult dh = probe(3, TYPE_DHT22);
    ScanResult bmp = bus(TYPE_BMP280, 4, 5), bme = bus(TYPE_BME280, 8, 9);
    printf("none\t%s\nbmp\t%s\nbme_elsewhere\t%s\nmixed\t%s\n",
           doc({}).c_str(), doc({bmp}).c_str(), doc({bme}).c_str(), doc({ds, dh, bmp}).c_str());
    return 0;
}
"""


def firmware_scans():
    """{case: the firmware's JSON text}, from ScanResultJson.h compiled for the host, or None when g++ is absent."""
    gxx = shutil.which("g++") or shutil.which("c++")
    if not gxx:
        raise SystemExit("SKIP: g++ is not installed")
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        cpp, exe = Path(tmp) / "scan.cpp", Path(tmp) / "scan"
        cpp.write_text(SCAN_CPP, encoding="utf-8")
        r = subprocess.run([gxx, "-std=gnu++17", "-Wall", "-Wextra", "-I", str(REPO / "src"), "-I", str(REPO / "test" / "native_stubs"),
                            str(cpp), "-o", str(exe)], capture_output=True, text=True, timeout=120)
        if r.returncode != 0:
            raise RuntimeError("the firmware's scan JSON does not build: " + r.stderr[-1500:])
        out = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    return dict(line.split("\t", 1) for line in out.stdout.splitlines() if line)


SCAN_HARNESS = r"""
const CODE = __CODE__, DOCS = __DOCS__, PACKS = __PACKS__;
(async function () {
    const out = {};
    for (const [name, doc] of Object.entries(DOCS)) for (const [lang, dict] of Object.entries(PACKS)) {
        const els = {};
        const SE = id => els[id] || (els[id] = { disabled: false, textContent: '' });
        const window = { t: (k, d) => dict[k] || d };
        const sensAction = async op => op === 'sensor_scan' ? { ok: true, status: 202, body: { started: true } }
                                                            : { ok: true, status: 200, body: doc };
        const sensScan = new Function('SE', 'window', 'sensAction', 'setTimeout', CODE + '\nreturn sensScan;')(SE, window, sensAction, fn => { fn(); return 0; });
        await sensScan();
        out[name + '|' + lang] = { text: SE('sens_scan_out').textContent, enabled: !SE('sens_scan').disabled };
    }
    console.log(JSON.stringify(out));
})().catch(e => console.log(JSON.stringify({ error: String((e && e.stack) || e) })));
"""


def scan_code(src):
    """sensScan( ) of the sensor page, cut out by its name and the next function's."""
    cfg = block(src, "CFG_PAGE")
    a = cfg.find("async function sensScan() {")
    b = cfg.find("async function sensAdopt() {", a) if a >= 0 else -1
    return cfg[a:b] if a >= 0 and b > a else None


def scan_expected(doc, found_word, none_word):
    """The line the page must show for this firmware answer, from the JSON alone."""
    found = doc["found"]
    if not found:
        return none_word
    parts = []
    for f in found:
        if "sda" in f:
            parts.append(f"GP{f['sda']}/GP{f['scl']} ({f['name']}, I²C)")
        else:
            parts.append(f"GP{f['pin']}" + (f" ({f['rom']})" if f["rom"] else ""))
    return f"{len(found)} {found_word}: " + ", ".join(parts)


def scan_findings(code, docs, ns, dicts):
    """What is wrong with the line sensScan( ) builds from the firmware's JSON: [] when nothing."""
    found = []
    packs = {"English": {}, **dicts}
    for label, js in (("as written", code), ("minified", ns["_minify_web_block"](code, "js"))):
        try:
            res = run_node(SCAN_HARNESS.replace("__CODE__", json.dumps(js)).replace("__DOCS__", json.dumps({k: json.loads(v) for k, v in docs.items()}))
                           .replace("__PACKS__", json.dumps(packs)))
        except RuntimeError as e:
            found.append(f"{label}: {e}")
            continue
        if "error" in res:
            found.append(f"{label}: sensScan( ) threw {res['error'][:300]}")
            continue
        for key, got in sorted(res.items()):
            name, lang = key.split("|")
            d = packs[lang]
            want = scan_expected(json.loads(docs[name]), d.get("sens_scan_found", "found"), d.get("sens_scan_none", "No probes answered."))
            if got["text"] != want:
                found.append(f"{label}: {lang}, {name}: the page shows {got['text']!r}, the firmware's JSON says {want!r}")
            if "GP255" in got["text"]:
                found.append(f"{label}: {lang}, {name}: the page shows GP255, a pin the firmware does not have")
            if not got["enabled"]:
                found.append(f"{label}: {lang}, {name}: the Scan button stays disabled")
    return sorted(set(found))


def test_probe_scan(src, ns, dicts):
    code = scan_code(src)
    if not check(code is not None, "sensScan( ) is not in the sensor page of WebUI.h (or sensAdopt( ) no longer follows it). "
                                   "If they moved or were renamed, move this test with them."):
        return
    try:
        docs = firmware_scans()
    except RuntimeError as e:
        fails.append(f"probe scan: {e}")
        return
    bmp = json.loads(docs["bmp"])["found"]
    check(len(bmp) == 1 and bmp[0].get("sda") == 4 and bmp[0].get("scl") == 5 and bmp[0].get("name") == "BMP280",
          f"probe scan: the firmware's JSON for a BMP280 on 4/5 is {docs['bmp']!r}: the test below judges the page against it")
    for f in scan_findings(code, docs, ns, dicts):
        fails.append(f"probe scan: {f}")


SCAN_MUTANTS = [
    ("an I2C result printed from pin, as before the fix",
     r"f\.sda !== undefined \?", "false ?", {"bmp", "bme_elsewhere", "mixed"}),
    ("the pins copied into the page",
     r"'GP' \+ f\.sda \+ '/GP' \+ f\.scl", "'GP4/GP5'", {"bme_elsewhere"}),
    ("the chip name dropped from an I2C line",
     r" \(' \+ f\.name \+ ', I²C\)'", " (I²C)'", {"bmp", "bme_elsewhere", "mixed"}),
    ("the ROM dropped from a 1-Wire line",
     r"\(f\.rom \? ' \(' \+ f\.rom \+ '\)' : ''\)", "''", {"mixed"}),
]


def test_scan_mutants(src, ns, dicts):
    code = scan_code(src)
    if code is None:
        return
    docs = firmware_scans()
    check(scan_findings(code, docs, ns, dicts) == [], "scan mutants: the code they start from is not clean")
    for name, pat, rep, want in SCAN_MUTANTS:
        mutated, n = re.subn(pat, lambda m, r=rep: r, code)
        if not check(n == 1, f"scan mutant {name!r}: its pattern matches {n} times in the code, not once "
                             f"(the mutant no longer describes the code: update it)"):
            continue
        got = {m.group(1) for f in scan_findings(mutated, docs, ns, dicts) for m in [re.search(r", (\w+): the page shows", f)] if m}
        check(got == want, f"scan mutant {name!r}: the page is wrong for {sorted(got)}, wanted {sorted(want)}")


# ── what the next checks share: a page function beside lang.js's own code ──

# What node does not have, for a page function that runs together with lang.js's
# own t( ), escHtml( ) and applyLang( ) and the line that asks /api/lang for the
# dictionary: the page's storage, elements that keep their attributes and their
# text (the scenario says which ones the page has, by id and as [data-i18n]),
# and the dictionary request, answered when the scenario says so. LINK_HARNESS
# and SYNC_HARNESS above are older and keep their own copies of these lines.
LANG_ENV = r"""
var __store = {};
Object.defineProperty(globalThis, 'localStorage', { configurable: true, writable: true, value: {
    getItem: function (k) { return Object.prototype.hasOwnProperty.call(__store, k) ? __store[k] : null; },
    setItem: function (k, v) { __store[k] = String(v); } } });
class __El {
    constructor(tag, attrs, html) { this.tagName = tag; this._a = attrs || {}; this._h = html || ''; }
    get innerHTML() { return this._h; }  set innerHTML(v) { this._h = String(v); }
    get textContent() { return this._h; }  set textContent(v) { this._h = String(v); }
    getAttribute(n) { return Object.prototype.hasOwnProperty.call(this._a, n) ? this._a[n] : null; }
    setAttribute(n, v) { this._a[n] = String(v); }
    removeAttribute(n) { delete this._a[n]; }
    hasAttribute(n) { return Object.prototype.hasOwnProperty.call(this._a, n); }
}
var __byId = function () { return null; }, __i18n = function () { return []; };
var __query = function (s) { throw new Error('the harness has no answer for the selector ' + s); };
Object.defineProperty(globalThis, 'document', { configurable: true, writable: true, value: {
    getElementById: function (id) { return __byId(id); },
    querySelectorAll: function (s) { return s === '[data-i18n]' ? __i18n() : s === '.lang-select' ? [] : __query(s); },
    createElement: function () { var s = ''; return {
        set textContent(v) { s = String(v); },
        get innerHTML() { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); } }; } } });
var window = globalThis;
var dict = { pt: {} };
var __pack = null, __arrive = null;
globalThis.fetch = function () { return new Promise(function (res) { __arrive = function () { res({ json: function () { return __pack; } }); }; }); };
__LANG_JS__
function __startLang() {
__FETCH_LINE__
}
var __tick = function () { return new Promise(function (r) { setTimeout(r, 0); }); };
var __unesc = function (s) { return s.replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&'); };
"""

# The three orders the device can answer in, for every check that runs LANG_ENV:
# the dictionary before the page's own request, after it, and a first visit (no
# language stored yet, so the dictionary's arrival stores the pack's).
ORDERS = {"dict-first": "dictionary first", "late": "dictionary late", "first-visit": "first visit"}


def lang_env(parts):
    """LANG_ENV with lang.js's own lines in it (the four lang_js_parts( ) found)."""
    return LANG_ENV.replace("__LANG_JS__", "\n".join(parts[:3])).replace("__FETCH_LINE__", parts[3])


def merged(rows):
    """Findings as lines: {(label, text): {context}} to 'as written, minified: text (contexts)', the two runs on one line when they agree."""
    by_text = {}
    for (label, text), ctx in rows.items():
        by_text.setdefault((text, frozenset(ctx)), []).append(label)
    return sorted(f"{', '.join(sorted(labels, key=['as written', 'minified'].index))}: {text} ({', '.join(sorted(ctx))})"
                  for (text, ctx), labels in by_text.items())


# ── the Origem selector of the history page ────────────────────────────────

def selector_code(src):
    """_updateSensorMselLabel( ) of the history page, cut out by its name and the next function's."""
    hist = block(src, "HIST_PAGE")
    a = hist.find("function _updateSensorMselLabel() {")
    b = hist.find("function _toggleSensorMenu(", a) if a >= 0 else -1
    return hist[a:b] if a >= 0 and b > a else None


def selector_markup(src):
    """The button of the selector as the page ships it: [{id, attrs, text}] for each <span> in it that has an id."""
    m = re.search(r'<button\b[^>]*\bid="sensorMselBtn"[^>]*>(.*?)</button>', block(src, "HIST_PAGE"), re.S)
    if not m:
        return None
    out = []
    for s in re.finditer(r"<span([^>]*)>([^<]*)</span>", m.group(1)):
        attrs = dict(re.findall(r'\b([a-z][a-z0-9-]*)="([^"]*)"', s.group(1)))
        if "id" in attrs:
            out.append({"id": attrs.pop("id"), "attrs": attrs, "text": s.group(2)})
    return out


# The selector's button, built from the page's own markup, and the menu of two
# sensors that populateSensorMsel( ) fills (a checkbox in a <label> whose last
# <span> is the "ID — name"). What the selector reads is the count's text plus
# the label's text, the two <span>s the button holds; a page with no count
# element (the code before the fix) reads the label alone. A scenario is the
# order the device can answer in, the selection the list opens with (one, two or
# no sensor), and then what the user does: the language switched to English and
# back, and the selection moved to each of the other two states and back to the
# first, the language switched at each.
SELECTOR_HARNESS = r"""
var __spec = __SPEC__;
var NAMES = ['LIB_SENS — GELADEIRA', 'STH0003 — SALA'];
var __els = {}, __list = [], __menu = [], _selectedSensors = [];
__byId = function (id) { return __els[id] || null; };
__i18n = function () { return __list.filter(function (e) { return e.hasAttribute('data-i18n'); }); };
__query = function (s) {
    if (s === '#sensorMselMenu input[type=checkbox]:checked') return __menu.filter(function (c) { return c.checked; });
    throw new Error('the harness has no answer for the selector ' + s);
};
__CODE__
function __page() {
    __store = {}; dict.pt = {}; __els = {}; __list = []; __menu = []; _selectedSensors = [];
    __spec.forEach(function (s) { var e = new __El('SPAN', JSON.parse(JSON.stringify(s.attrs)), s.text); __els[s.id] = e; __list.push(e); });
    NAMES.forEach(function (n, i) {
        var txt = new __El('SPAN', {}, n);
        __menu.push({ checked: false, value: String(10 + i), parentNode: { querySelector: function (q) { return q === 'span:last-child' ? txt : null; } } });
    });
}
function __choose(sel) {
    var n = sel === 'one' ? 1 : sel === 'two' ? 2 : 0;
    _selectedSensors = [];
    __menu.forEach(function (b, i) { b.checked = i < n; if (b.checked) _selectedSensors.push(parseInt(b.value, 10)); });
}
function __read() { var c = __els['sensorMselN'], l = __els['sensorMselLbl']; return (c ? c.textContent : '') + (l ? l.textContent : ''); }
var WALK = { one: ['two', 'none'], two: ['one', 'none'], none: ['one', 'two'] };
async function __scenario(order, first, pack) {
    __page(); __pack = pack;
    if (order !== 'first-visit') localStorage.setItem('simut_lang', 'pt');
    __startLang();                                        /* lang.js asks for the dictionary as it loads */
    var seen = [];
    var note = function (when, sel) { seen.push([when, sel, __read()]); };
    var show = function (sel, when) { __choose(sel); _updateSensorMselLabel(); if (when) note(when, sel); };
    var lang = function (l) { localStorage.setItem('simut_lang', l); applyLang(); };
    if (order === 'dict-first') { __arrive(); await __tick(); show(first, 'painted'); }
    else { show(first); __arrive(); await __tick(); note('arrived', first); }
    lang('en'); note('switched to English', first);
    lang('pt'); note('switched back', first);
    [WALK[first][0], first, WALK[first][1], first].forEach(function (sel) {
        show(sel, 'painted'); lang('en'); note('switched to English', sel); lang('pt'); note('switched back', sel);
    });
    return seen;
}
(async function () {
    var out = {}, packs = __PACKS__;
    for (var lg of Object.keys(packs))
        for (var order of ['dict-first', 'late', 'first-visit'])
            for (var first of ['one', 'two', 'none'])
                out[lg + '|' + order + '|' + first] = await __scenario(order, first, packs[lg]);
    console.log(JSON.stringify(out));
})().catch(function (e) { console.log(JSON.stringify({ error: String((e && e.stack) || e) })); });
"""

SELECTOR_NAME = "LIB_SENS — GELADEIRA"      # the first sensor of the harness's menu, as the page writes it
SELECTION = {"one": "one sensor", "two": "two sensors", "none": "no sensor"}
MOMENTS = "painted|arrived|switched to English|switched back"


def selector_findings(code, markup, parts, ns, dicts):
    """What is wrong with the selector's label in this _updateSensorMselLabel( ) and this markup: [] when nothing."""
    found, rows = [], {}
    for lang, d in dicts.items():
        for key, en in (("hist_none_sel", "None"), ("hist_n_sel", "selected")):
            if not d.get(key, "").strip() or d[key] == en:
                found.append(f"{lang}: @WEBDICT has {key} = {d.get(key)!r}, which is not a translation of {en!r}")
    for label, js in (("as written", code), ("minified", ns["_minify_web_block"](code, "js"))):
        prog = (lang_env(parts) + SELECTOR_HARNESS.replace("__CODE__", js).replace("__SPEC__", json.dumps(markup))
                .replace("__PACKS__", json.dumps(dicts)))
        try:
            res = run_node(prog)
        except RuntimeError as e:
            found.append(f"{label}: {e}")
            continue
        if "error" in res:
            found.append(f"{label}: the scenarios threw {res['error'][:300]}")
            continue
        for key, seen in sorted(res.items()):
            lang, order, _ = key.split("|")
            d = dicts[lang]
            for when, sel, got in seen:
                none_w, n_w = ("None", "selected") if when == "switched to English" else (d.get("hist_none_sel", ""), d.get("hist_n_sel", ""))
                want = {"one": SELECTOR_NAME, "two": f"2 {n_w}", "none": none_w}[sel]
                if got != want:
                    rows.setdefault((label, f"{lang}, {SELECTION[sel]}: the selector reads {got!r} {when}, wanted {want!r}"), set()).add(ORDERS[order])
    return sorted(set(found)) + merged(rows)


def test_selector(src, ns, dicts):
    parts, missing = lang_js_parts(src)
    code, markup = selector_code(src), selector_markup(src)
    if not check(not missing, f"lang.js lacks {missing} as one line each; this test runs lang.js's own"):
        return
    if not check(code is not None, "_updateSensorMselLabel( ) is not in the history page of WebUI.h (or _toggleSensorMenu( ) "
                                   "no longer follows it). If they moved or were renamed, move this test with them."):
        return
    if not check(markup is not None and any(s["id"] == "sensorMselLbl" for s in markup),
                 "the history page has no <span id=\"sensorMselLbl\"> in the button #sensorMselBtn. If it moved, move this test with it."):
        return
    for f in selector_findings(code, markup, parts, ns, dicts):
        fails.append(f"origem selector: {f}")


def selector_cells(findings):
    """{(selection, moment)} the findings are about, for the mutants."""
    return {m.groups() for f in findings
            for m in [re.search(rf", ({'|'.join(SELECTION.values())}): the selector reads .+? ({MOMENTS}), wanted", f)] if m}


# Each mutant takes one piece of the fix out. What it must give is the set of
# (selection, moment) at which the selector reads wrong, whatever the pack or
# the order: a label that cannot be relabelled misses `arrived` and both
# switches; one with no English of its own gives the translated word for the
# English; a count inside the label's text loses the number the first time
# applyLang( ) writes the word; a count that is not emptied stays in front of
# a name or of "None"; a word painted from the English literal misses only
# `painted`.
SELECTOR_MUTANTS = [
    ("the label keeps its data-i18n for one sensor (the defect as shipped: the markup's hist_loading comes back)",
     r"lbl\.removeAttribute\('data-i18n'\); lbl\.removeAttribute\('data-en'\);\s*", "",
     {("one sensor", "arrived"), ("one sensor", "switched to English"), ("one sensor", "switched back")}),
    ("no data-en on the word: the first applyLang( ) takes the translated text, or the old one, for the English",
     r"lbl\.setAttribute\('data-en', n === 0 \? 'None' : 'selected'\);\s*", "",
     {("two sensors", "switched to English"), ("no sensor", "switched to English")}),
    ("the count back inside the label's text: applyLang( ) drops the number",
     r"cnt\.textContent = n === 0 \? '' : n \+ ' ';\s*lbl\.textContent = (n === 0 \? window\.t\('hist_none_sel', 'None'\) : window\.t\('hist_n_sel', 'selected'\));",
     r"cnt.textContent = ''; lbl.textContent = (n === 0 ? '' : n + ' ') + (\1);",
     {("two sensors", "arrived"), ("two sensors", "switched to English"), ("two sensors", "switched back")}),
    ("the count not emptied when the label shows a sensor's name",
     r"cnt\.textContent = ''; lbl\.textContent = lblTxt\.textContent;", "lbl.textContent = lblTxt.textContent;",
     {("one sensor", "painted"), ("one sensor", "switched to English"), ("one sensor", "switched back")}),
    ("the count not emptied when the label shows None",
     r"cnt\.textContent = n === 0 \? '' : n \+ ' ';", "cnt.textContent = n + ' ';",
     {("no sensor", "painted"), ("no sensor", "arrived"), ("no sensor", "switched to English"), ("no sensor", "switched back")}),
    ("the word painted from the English literal, not from t( )",
     r"n === 0 \? window\.t\('hist_none_sel', 'None'\) : window\.t\('hist_n_sel', 'selected'\);", "n === 0 ? 'None' : 'selected';",
     {("no sensor", "painted"), ("two sensors", "painted")}),
]


def test_selector_mutants(src, ns, dicts):
    parts, missing = lang_js_parts(src)
    code, markup = selector_code(src), selector_markup(src)
    if missing or code is None or markup is None:
        return
    check(selector_findings(code, markup, parts, ns, dicts) == [], "selector mutants: the code they start from is not clean")
    for name, pat, rep, want in SELECTOR_MUTANTS:
        mutated, n = re.subn(pat, rep, code)
        if not check(n == 1, f"selector mutant {name!r}: its pattern matches {n} times in the code, not once "
                             f"(the mutant no longer describes the code: update it)"):
            continue
        got = selector_cells(selector_findings(mutated, markup, parts, ns, dicts))
        check(got == want, f"selector mutant {name!r}: the selector reads wrong at {sorted(got)}, wanted {sorted(want)}")
    # the markup half of the fix: a button with no element for the count, and the function leaves the label as the markup has it
    # (the guard it shares with the label's), so the selector reads the loading word at every moment
    got = selector_findings(code, [s for s in markup if s["id"] != "sensorMselN"], parts, ns, dicts)
    check(got != [] and all(re.search(r"reads '(Carregando|Cargando|Loading)\.\.\.'", f) for f in got),
          f"selector mutant 'no #sensorMselN in the markup': {got[:2]}")


# ── the Max and Min badges of the history chart ────────────────────────────

def badge_code(src):
    """renderExtremes( ) of the history page, cut out by its name and the line that closes it."""
    hist = block(src, "HIST_PAGE")
    a = hist.find("function renderExtremes(json) {")
    m = re.search(r"function renderExtremes\(json\) \{.*?\n        \}", hist[a:], re.S) if a >= 0 else None
    return hist[a:a + m.end()] if m else None


# What the page hands renderExtremes( ): the extremes it measured over the range
# ({temp, hum, press}, each with the key of its name, the way the H5G reader
# builds them), with a fourth channel the page has no label for, so the name's
# key must be built from the channel's key; and the minT/maxT form an older
# firmware answered with. The values are what the badge writes after the name.
BADGE_INPUTS = {
    "extremes": {"extremes": {"temp": {"min": 27.1, "max": 27.7, "unit": "°C", "label": "ch_temp"},
                              "hum": {"min": 41.0, "max": 55.5, "unit": "%", "label": "ch_hum"},
                              "press": {"min": 1009.4, "max": 1013.2, "unit": "hPa", "label": "ch_press"},
                              "lux": {"min": 3, "max": 640, "unit": "lx"}}},
    "legacy": {"minT": 27.1, "maxT": 27.7, "minH": 41.0, "maxH": 55.5, "minP": 1009.4, "maxP": 1013.2},
}
LEGACY_UNITS = (("temp", "T", "°C"), ("hum", "H", "%"), ("press", "P", "hPa"))
# The English of a channel's name, the word the pack translates: the key, capitalized.
BADGE_ENGLISH = {"temp": "Temp", "hum": "Hum", "press": "Press", "lux": "Lux"}
BADGE_MOMENTS = "painted|before the dictionary|arrived|switched to English|switched back"


# The box of the badges: setting its innerHTML parses the markup into what a
# reader sees. Each .stat-badge keeps its text as parts, plain text or an element
# that carries data-i18n (applyLang( ) rewrites those, and their text is their
# innerHTML); an element with no data-i18n is just markup. A badge reads as the
# browser would lay it out: its parts joined, the spaces collapsed.
BADGE_HARNESS = r"""
var __grid = null;
function __Grid() { this.badges = []; this.items = []; }
Object.defineProperty(__Grid.prototype, 'innerHTML', { get: function () { return ''; }, set: function (h) {
    this.badges = []; this.items = [];
    var cur = null, open = null, re = /<(\/?)([a-z0-9]+)([^>]*)>|([^<]+)/gi, m;
    while ((m = re.exec(h))) {
        if (m[4] !== undefined) { if (open) open._h += m[4]; else if (cur) cur.push({ text: m[4] }); continue; }
        if (m[1]) { open = null; continue; }
        var attrs = {};
        for (var a of m[3].matchAll(/\b([a-z][a-z0-9-]*)="([^"]*)"/gi)) attrs[a[1]] = __unesc(a[2]);
        if (/(^| )stat-badge( |$)/.test(attrs['class'] || '')) { cur = []; this.badges.push(cur); }
        if (attrs['data-i18n'] !== undefined) { open = new __El(m[2].toUpperCase(), attrs, ''); this.items.push(open); if (cur) cur.push({ el: open }); }
    }
} });
__byId = function (id) { return id === 'statsGrid' ? __grid : null; };
__i18n = function () { return __grid.items.filter(function (e) { return e.hasAttribute('data-i18n'); }); };
__CODE__
var INPUTS = __INPUTS__;
var __text = function (b) { return b.map(function (p) { return __unesc(p.el ? p.el._h : p.text); }).join('').replace(/\s+/g, ' ').trim(); };
var __read = function () { return __grid.badges.map(__text); };
async function __scenario(order, input, pack) {
    __store = {}; dict.pt = {}; __grid = new __Grid(); __pack = pack;
    if (order !== 'first-visit') localStorage.setItem('simut_lang', 'pt');
    __startLang();                                        /* lang.js asks for the dictionary as it loads */
    var seen = [];
    var note = function (when) { seen.push([when, __read()]); };
    var lang = function (l) { localStorage.setItem('simut_lang', l); applyLang(); };
    if (order === 'dict-first') { __arrive(); await __tick(); renderExtremes(INPUTS[input]); note('painted'); }
    else { renderExtremes(INPUTS[input]); note('before the dictionary'); __arrive(); await __tick(); note('arrived'); }
    lang('en'); note('switched to English');
    lang('pt'); note('switched back');
    return seen;
}
(async function () {
    var out = {}, packs = __PACKS__;
    for (var lg of Object.keys(packs))
        for (var order of ['dict-first', 'late', 'first-visit'])
            for (var input of Object.keys(INPUTS))
                out[lg + '|' + order + '|' + input] = await __scenario(order, input, packs[lg]);
    console.log(JSON.stringify(out));
})().catch(function (e) { console.log(JSON.stringify({ error: String((e && e.stack) || e) })); });
"""


def badge_expected(input_name, d):
    """[(kind, channel, Max/Min word, name, value)] the badges must read for this input, in the pack d's words (the English when d is None)."""
    spec = BADGE_INPUTS[input_name]
    if "extremes" in spec:
        channels = [(k, e) for k, e in spec["extremes"].items()]
    else:
        channels = [(k, {"min": spec[f"min{c}"], "max": spec[f"max{c}"], "unit": u}) for k, c, u in LEGACY_UNITS]
    out = []
    for key, e in channels:
        name = BADGE_ENGLISH[key] if d is None else d.get(f"ch_{key}", "")
        for kind, field in (("Max", "max"), ("Min", "min")):
            word = kind if d is None else d.get(f"hist_{field}", "")
            out.append((kind, key, word, name, f"{e[field]:.1f} {e['unit']}"))
    return out


def badge_findings(code, parts, ns, dicts):
    """What is wrong with the Max and Min badges in this renderExtremes( ): [] when nothing."""
    found, rows = [], {}
    for lang, d in dicts.items():
        for key, en in (("hist_max", "Max"), ("hist_min", "Min")):
            if not d.get(key, "").strip() or d[key] == en:
                found.append(f"{lang}: @WEBDICT has {key} = {d.get(key)!r}, which is not a translation of {en!r}")
        for k in BADGE_ENGLISH:
            if not d.get(f"ch_{k}", "").strip():
                found.append(f"{lang}: @WEBDICT has no 'ch_{k}' (the badge asks for the name of the channel)")
    for label, js in (("as written", code), ("minified", ns["_minify_web_block"](code, "js"))):
        prog = lang_env(parts) + (BADGE_HARNESS.replace("__CODE__", js).replace("__INPUTS__", json.dumps(BADGE_INPUTS))
                                  .replace("__PACKS__", json.dumps(dicts)))
        try:
            res = run_node(prog)
        except RuntimeError as e:
            found.append(f"{label}: {e}")
            continue
        if "error" in res:
            found.append(f"{label}: the scenarios threw {res['error'][:300]}")
            continue
        for key, seen in sorted(res.items()):
            lang, order, inp = key.split("|")
            d = dicts[lang]
            for when, got in seen:
                want = badge_expected(inp, None if when in ("before the dictionary", "switched to English") else d)
                if len(got) != len(want):
                    rows.setdefault((label, f"{lang}: {len(got)} badges {when}, wanted {len(want)}"), set()).add(ORDERS[order])
                    continue
                for (kind, chan, w, n, v), text in zip(want, got):
                    m = re.fullmatch(r"(\S+) (\S+): (.*)", text)
                    if not m:
                        t = f"{lang}: the {kind} badge of {chan} reads {text!r} {when}, which is not '<word> <name>: <value>'"
                    elif m.group(1) != w:
                        t = f"{lang}: the {kind} word reads {m.group(1)!r} {when}, wanted {w!r}"
                    elif m.group(2) != n:
                        t = f"{lang}: the name of {chan} reads {m.group(2)!r} {when}, wanted {n!r}"
                    elif m.group(3) != v:
                        t = f"{lang}: the {kind} value of {chan} reads {m.group(3)!r} {when}, wanted {v!r}"
                    else:
                        continue
                    rows.setdefault((label, t), set()).add(ORDERS[order])
    return sorted(set(found)) + merged(rows)


def test_badges(src, ns, dicts):
    parts, missing = lang_js_parts(src)
    code = badge_code(src)
    if not check(not missing, f"lang.js lacks {missing} as one line each; this test runs lang.js's own"):
        return
    if not check(code is not None, "renderExtremes( ) is not in the history page of WebUI.h. "
                                   "If it moved or was renamed, move this test with it."):
        return
    for f in badge_findings(code, parts, ns, dicts):
        fails.append(f"max/min badges: {f}")


def badge_moments(findings):
    """{moment} the findings are about, for the mutants."""
    return {m.group(1) for f in findings for m in [re.search(rf" ({BADGE_MOMENTS}), wanted", f)] if m}


# Each mutant takes one piece of the fix out. What it must give is the set of
# moments at which a badge reads wrong, whatever the pack or the input: a word
# typed into the page misses every moment in the pack's language; badges that
# carry no key cannot be relabelled, so they miss the dictionary that arrives
# late and the switch away from the language they were painted in; with no
# data-en the first applyLang( ) takes the translated text for the English; an
# English that is the upper-cased key reads 'TEMP' until a dictionary relabels
# it and again at the switch to English. The last column is how many times the
# pattern must match (the Max word, the Min word and the channel's name).
BADGE_MUTANTS = [
    ("the Max word typed into the page again",
     r"<span data-i18n=\"hist_max\" data-en=\"Max\">' \+ esc\(window\.t\('hist_max', 'Max'\)\) \+ '</span>", "Max", 1,
     {"painted", "arrived", "switched back"}),
    ("the Min word typed into the page again",
     r"<span data-i18n=\"hist_min\" data-en=\"Min\">' \+ esc\(window\.t\('hist_min', 'Min'\)\) \+ '</span>", "Min", 1,
     {"painted", "arrived", "switched back"}),
    ("no data-i18n on the words: applyLang( ) cannot come back to them",
     r"data-i18n=\"(?:hist_max|hist_min|' \+ esc\(lk\) \+ ')\" ", "", 3,
     {"arrived", "switched to English", "switched back"}),
    ("no data-en on the words: the first applyLang( ) takes the translated text for the English",
     r" data-en=\"(?:Max|Min|' \+ esc\(en\) \+ ')\"", "", 3,
     {"switched to English"}),
    ("the English of a channel's name is its upper-cased key, as the fallback was",
     r"key\.charAt\(0\)\.toUpperCase\(\) \+ key\.slice\(1\)", "key.toUpperCase()", 1,
     {"before the dictionary", "switched to English"}),
]


def test_badge_mutants(src, ns, dicts):
    parts, missing = lang_js_parts(src)
    code = badge_code(src)
    if missing or code is None:
        return
    check(badge_findings(code, parts, ns, dicts) == [], "badge mutants: the code they start from is not clean")
    for name, pat, rep, times, want in BADGE_MUTANTS:
        mutated, n = re.subn(pat, lambda m, r=rep: r, code)
        if not check(n == times, f"badge mutant {name!r}: its pattern matches {n} times in the code, not {times} "
                                 f"(the mutant no longer describes the code: update it)"):
            continue
        got = badge_moments(badge_findings(mutated, parts, ns, dicts))
        check(got == want, f"badge mutant {name!r}: a badge reads wrong at {sorted(got)}, wanted {sorted(want)}")


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
    tests = [("test_export_box", lambda: test_export_box(src, ns, dicts)),
             ("test_link_state", lambda: test_link_state(src, ns, dicts)),
             ("test_melody_names", lambda: test_melody_names(src, ns, dicts)),
             ("test_probe_scan", lambda: test_probe_scan(src, ns, dicts)),
             ("test_selector", lambda: test_selector(src, ns, dicts)),
             ("test_badges", lambda: test_badges(src, ns, dicts))]
    if args.mutations:
        tests += [("test_export_mutants", lambda: test_export_mutants(src, ns, dicts)),
                  ("test_link_mutants", lambda: test_link_mutants(src, ns, dicts)),
                  ("test_melody_mutants", lambda: test_melody_mutants(src, ns, dicts)),
                  ("test_scan_mutants", lambda: test_scan_mutants(src, ns, dicts)),
                  ("test_selector_mutants", lambda: test_selector_mutants(src, ns, dicts)),
                  ("test_badge_mutants", lambda: test_badge_mutants(src, ns, dicts))]
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
