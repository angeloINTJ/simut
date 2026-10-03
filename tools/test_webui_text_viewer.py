#!/usr/bin/env python3
"""The text viewer of the web UI, run under node.

The viewer (LANG_JS in WebUI.h, drawn by the file manager and by the two live
previews of the telemetry page) shows a text organized for reading:
JSON indented two spaces, with every object and array that spans lines
foldable; the sections of a language or theme pack (`@SECTION` and
`[section]` headers) foldable; each line of an NDJSON file laid out as a JSON
block; and line numbers on everything that is not one JSON document. Nothing
is rewritten: a token keeps its spelling (`1.0` stays `1.0`, `\\u00e9` stays an
escape), only the whitespace between tokens changes, and the Original view
shows the bytes as they are.

This test takes the pure part of the viewer, everything between the
`tv: the pure part` comment and the `tv: end of the pure part` comment, out of
WebUI.h, the source the build compresses, and runs the cases below against it
twice under node: as written, and as the build's own minifier leaves it. The
minifier has deleted shipped code before and reported success
(tools/test_webui_minify.py tells that story), so passing as written is not
enough. When src/WebUI_GZ.h exists (any `pio run` writes it), the cases also
run against the copy the device serves, cut out of the compressed page.

Run: python3 tools/test_webui_text_viewer.py
Exit status is 0 on pass, 1 on failure.
"""

import functools
import gzip
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WEBUI = REPO / "WebUI.h"
GZ_HEADER = REPO / "src" / "WebUI_GZ.h"
GEN = REPO / "tools" / "build_webui_gz.py"
START = "/* tv: the pure part"
END = "/* tv: end of the pure part */"

# The cases, as data: inputs travel to node through JSON, so no string here
# has to survive two levels of escaping. A row is [depth, text, fold end,
# count] and, in the line view, [depth, text, fold end, count, line number];
# a fold end of -1 means the row does not fold.
CASES = {
    "nested": {
        "name": "a.json",
        "text": '{"a":1,"b":{"c":[1,2,{"d":null}],"e":"x"},"f":[]}',
        "j": 1, "bad": False,
        "rows": [
            [0, "{", 13, 3], [1, '"a": 1,', -1, 0], [1, '"b": {', 11, 2],
            [2, '"c": [', 9, 3], [3, "1,", -1, 0], [3, "2,", -1, 0],
            [3, "{", 8, 1], [4, '"d": null', -1, 0], [3, "}", -1, 0],
            [2, "],", -1, 0], [2, '"e": "x"', -1, 0], [1, "},", -1, 0],
            [1, '"f": []', -1, 0], [0, "}", -1, 0],
        ],
        # Opened down to depth 2: the blocks at depth 2 and below start closed.
        "initial": [[0, "{", 1], [1, '"a": 1,', 0], [2, '"b": {', 1],
                    [3, '"c": […],', 2, 3], [10, '"e": "x"', 0], [11, "},", 0],
                    [12, '"f": []', 0], [13, "}", 0]],
        # Collapse all keeps the outermost JSON block open: one line saying
        # "{…}" would be the least useful thing the button could show.
        "all": [[0, "{", 1], [1, '"a": 1,', 0], [2, '"b": {…},', 2, 2],
                [12, '"f": []', 0], [13, "}", 0]],
    },
    "empties": {
        "name": "e.json",
        "text": '{ "a" : { } , "b":[ ] , "c":[{}] }',
        "j": 1, "bad": False,
        "rows": [
            [0, "{", 6, 3], [1, '"a": {},', -1, 0], [1, '"b": [],', -1, 0],
            [1, '"c": [', 5, 1], [2, "{}", -1, 0], [1, "]", -1, 0], [0, "}", -1, 0],
        ],
    },
    "bare empties": {"name": "o.json", "text": "{}", "j": 1, "bad": False,
                     "rows": [[0, "{}", -1, 0]]},
    "strings keep their spelling": {
        "name": "s.json",
        "text": '{"s":"a{b}[c],:\\"q\\" \\\\","t":"\\u00e9","n":1.0e+2}',
        "j": 1, "bad": False,
        "rows": [
            [0, "{", 4, 3], [1, '"s": "a{b}[c],:\\"q\\" \\\\",', -1, 0],
            [1, '"t": "\\u00e9",', -1, 0], [1, '"n": 1.0e+2', -1, 0], [0, "}", -1, 0],
        ],
    },
    "truncated": {
        "name": "t.json",
        "text": '{"a":[1,2',
        "j": 1, "bad": True,
        # Each open block folds to the end of what arrived.
        "rows": [[0, "{", 3, 1], [1, '"a": [', 3, 2], [2, "1,", -1, 0], [2, "2", -1, 0]],
        "all": [[0, "{", 1], [1, '"a": [ …', 2, 2]],
    },
    "string cut short": {
        "name": "u.json", "text": '{"a":"xy', "j": 1, "bad": True,
        "rows": [[0, "{", 1, 1], [1, '"a": "xy', -1, 0]],
    },
    "wrong closer": {
        "name": "w.json", "text": '{"a":1]', "j": 1, "bad": True,
        "rows": [[0, "{", 2, 1], [1, '"a": 1', -1, 0], [0, "]", -1, 0]],
    },
    "one closer too many": {
        "name": "x.json", "text": "[1]]", "j": 1, "bad": True,
        "rows": [[0, "[", 2, 1], [1, "1", -1, 0], [0, "]", -1, 0], [0, "]", -1, 0]],
    },
    "missing colon": {
        "name": "y.json", "text": '{"a" 1 2}', "j": 1, "bad": True,
        "rows": [[0, "{", 4, 1], [1, '"a"', -1, 0], [1, "1", -1, 0], [1, "2", -1, 0],
                 [0, "}", -1, 0]],
    },
    "ndjson": {
        "name": "n.json",
        "text": '{"a":1}\n{"b":[2,3]}\n',
        "j": 0, "bad": False,
        "rows": [
            [0, "{", 2, 1, 1], [1, '"a": 1', -1, 0, 0], [0, "}", -1, 0, 0],
            [0, "{", 8, 1, 2], [1, '"b": [', 7, 2, 0], [2, "2,", -1, 0, 0],
            [2, "3", -1, 0, 0], [1, "]", -1, 0, 0], [0, "}", -1, 0, 0],
        ],
    },
    "language pack": {
        "name": "language_pt-BR.lng",
        "text": '# c\n@NAME Portugues\n@DICT\na\nb\n@WEBDICT\n{"k":"v","w":"x"}\n',
        "j": 0, "bad": False,
        "rows": [
            [0, "# c", -1, 0, 1], [0, "@NAME Portugues", -1, 0, 2],
            [0, "@DICT", 4, 2, 3], [1, "a", -1, 0, 4], [1, "b", -1, 0, 5],
            [0, "@WEBDICT", 9, 1, 6], [1, "{", 9, 2, 7], [2, '"k": "v",', -1, 0, 0],
            [2, '"w": "x"', -1, 0, 0], [1, "}", -1, 0, 0],
        ],
        "all": [[0, "# c", 0], [1, "@NAME Portugues", 0], [2, "@DICT …", 2, 2],
                [5, "@WEBDICT …", 2, 1]],
    },
    "blank lines do not make a section": {
        "name": "b.thm",
        "text": "@A\n\n@B\nx\n\ny\n",
        "j": 0, "bad": False,
        # @A holds only a blank line, so it has nothing to fold; @B counts the
        # two lines with text and still folds the blank one between them.
        "rows": [[0, "@A", -1, 0, 1], [1, "", -1, 0, 2], [0, "@B", 5, 2, 3],
                 [1, "x", -1, 0, 4], [1, "", -1, 0, 5], [1, "y", -1, 0, 6]],
    },
    "ini sections and a JSON array line": {
        "name": "c.cfg",
        "text": "[a]\nx=1\n[b]\n[1,2]",
        "j": 0, "bad": False,
        "rows": [
            [0, "[a]", 1, 1, 1], [1, "x=1", -1, 0, 2], [0, "[b]", 6, 1, 3],
            [1, "[", 6, 2, 4], [2, "1,", -1, 0, 0], [2, "2", -1, 0, 0], [1, "]", -1, 0, 0],
        ],
    },
    "plain text": {
        "name": "README.txt", "text": "one\r\ntwo\n\nfour", "j": 0, "bad": False,
        "rows": [[0, "one", -1, 0, 1], [0, "two", -1, 0, 2], [0, "", -1, 0, 3],
                 [0, "four", -1, 0, 4]],
    },
    "JSON in a .txt is still JSON": {
        "name": "p.txt", "text": ' {"a":1} ', "j": 1, "bad": False,
        "rows": [[0, "{", 2, 1], [1, '"a": 1', -1, 0], [0, "}", -1, 0]],
    },
    "a broken JSON in a .txt is text": {
        "name": "q.txt", "text": '{"a":', "j": 0, "bad": False,
        "rows": [[0, '{"a":', -1, 0, 1]],
    },
    "a JSON scalar is text": {
        "name": "r.json", "text": "42", "j": 0, "bad": False,
        "rows": [[0, "42", -1, 0, 1]],
    },
    "empty file": {"name": "z.txt", "text": "", "j": 0, "bad": False, "rows": []},
    # A header is @ and a capital, as the packs write them; a mention or a
    # number after the @ is a line like any other.
    "an @ line that is not a header": {
        "name": "notes.txt", "text": "@CODE\nx\n@mention\n@1\ny", "j": 0, "bad": False,
        "rows": [[0, "@CODE", 4, 4, 1], [1, "x", -1, 0, 2], [1, "@mention", -1, 0, 3],
                 [1, "@1", -1, 0, 4], [1, "y", -1, 0, 5]],
    },
}

VIEWABLE = [
    ["a.json", 10, True], ["A.JSON", 10, True], ["lang/language_pt-BR.lng", 37332, True],
    ["t.thm", 500, True], ["calib.csv", 100, True], ["README.txt", 1377, True],
    ["a.json", 65536, True], ["a.json", 65537, False],
    ["x.h5", 10, False], ["x.bin", 10, False], ["backup.bkp", 10, False],
    ["noext", 10, False], ["files.html.gz", 10, False],
]

IS_TEXT = [
    ["plain text\n", True], ["", True], ["tab\tand\r\nlines\fesc\u001b", True],
    ["abc\u0000" + "x" * 300, True], ["\u0000\u0001\u0002abc", False],
    ["\u0000" * 64, False],
    # The threshold is 1 in 100: at it, still text; past it, not.
    ["x" * 99 + "\u0000", True], ["x" * 98 + "\u0000" * 2, False],
    ["x" * 95 + "\u0000" * 5, False],
    # The vertical tab is not one of the five a text may carry.
    ["x" * 98 + "\u000b" * 2, False],
]

HARNESS = r"""
const C = __CASES__, V = __VIEWABLE__, T = __IS_TEXT__;
const out = [];
const eq = (label, got, want) => {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  if (g !== w) out.push(label + '\n      want ' + w + '\n      got  ' + g);
};
const rowsOf = m => m.rows.map(r => m.j ? [r.d, r.t, r.f, r.n] : [r.d, r.t, r.f, r.n, r.ln]);
const visOf = (m, c) => tvVis(m, c).map(v => v.k === 2 ? [v.i, v.t, v.k, v.n] : [v.i, v.t, v.k]);
for (const name in C) {
  const k = C[name];
  let m;
  try { m = tvParse(k.name, k.text); } catch (e) { out.push(name + ': threw ' + e.message); continue; }
  eq(name + ': json view', m.j, k.j);
  eq(name + ': bad', m.bad, k.bad);
  eq(name + ': rows', rowsOf(m), k.rows);
  if (k.initial) eq(name + ': opened down to depth 2', visOf(m, tvInit(m)), k.initial);
  if (k.all) eq(name + ': collapse all', visOf(m, tvAll(m)), k.all);
  eq(name + ': expand all shows every row', tvVis(m, {}).length, m.rows.length);
}
for (const [n, s, want] of V) eq('viewable ' + n + ' ' + s, tvViewable(n, s), want);
for (const [s, want] of T) eq('is text ' + JSON.stringify(s.slice(0, 12)), tvIsText(s), want);

/* The layout keeps the content. Random JSON from a fixed seed, with strings
   that carry brackets, quotes, escapes and commas: the rows joined back must
   parse to the same value, and every fold must end on the bracket that
   closes it, at its own depth. */
let seed = 20261003;
const rnd = n => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
const STR = ['', 'a', '{', ']', ',', ':', '\\', '"', 'x y', 'é', '[1,2]', '{"k":1}'];
const gen = d => {
  const r = rnd(d > 5 ? 4 : 7);
  if (r === 0) return rnd(1000) - 500;
  if (r === 1) return STR[rnd(STR.length)] + STR[rnd(STR.length)];
  if (r === 2) return [true, false, null][rnd(3)];
  if (r === 3) return rnd(100) / 8;
  if (r === 4) { const a = []; for (let i = rnd(4); i > 0; i--) a.push(gen(d + 1)); return a; }
  const o = {}; for (let i = rnd(4); i > 0; i--) o['k' + i + STR[rnd(STR.length)]] = gen(d + 1); return o;
};
for (let n = 0; n < 300; n++) {
  const v = { root: gen(0) }, s = JSON.stringify(v, null, n % 3 ? 0 : 1);
  const m = tvParse('f.json', s), R = m.rows;
  if (m.j !== 1 || m.bad) { out.push('random ' + n + ': not taken as valid JSON'); continue; }
  let back;
  try { back = JSON.parse(R.map(r => r.t).join('')); } catch (e) { out.push('random ' + n + ': rows do not parse back'); continue; }
  eq('random ' + n + ': same value', back, v);
  R.forEach((r, i) => {
    if (r.f < 0) return;
    const open = r.t[r.t.length - 1], close = R[r.f].t[0];
    if (!(r.f > i && R[r.f].d === r.d && ((open === '{' && close === '}') || (open === '[' && close === ']'))))
      out.push('random ' + n + ': row ' + i + ' folds to ' + r.f + ' (' + JSON.stringify(R[r.f].t) + ')');
  });
}

/* Size and depth: a 48 KiB pack is the largest text the device writes, and a
   deep nesting must not cost a stack frame per level. */
let big = '[' + Array.from({ length: 5000 }, (_, i) => i).join(',') + ']';
let t0 = Date.now(), mb = tvParse('big.json', big);
eq('5000 elements: rows', mb.rows.length, 5002);
if (Date.now() - t0 > 1000) out.push('5000 elements took ' + (Date.now() - t0) + ' ms');
let deep = '['.repeat(3000) + ']'.repeat(3000), md = tvParse('deep.json', deep);
eq('3000 levels: bad', md.bad, false);
eq('3000 levels: innermost empty array on one row', md.rows[2999].t, '[]');
eq('3000 levels: rows', md.rows.length, 5999);
console.log(JSON.stringify(out));
"""

def extract(src: str, where: str) -> str:
    a = src.find(START)
    b = src.find(END, a + 1)
    if a < 0 or b < 0:
        raise SystemExit(f"FAIL: the markers of the viewer's pure part are not in {where}. "
                         f"If the viewer moved or was renamed, move this test with it — "
                         f"do not delete it to make it pass.")
    return src[a:b]


@functools.lru_cache(maxsize=None)
def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(GEN.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(GEN), "exec"), ns)
    return ns


def minifier():
    return generator()["_minify_js"]


def served_copy():
    """The pure part as the device serves it: (array, code), or a reason why not."""
    if not GZ_HEADER.exists():
        return "no src/WebUI_GZ.h (a pio run writes it)"
    text = GZ_HEADER.read_text(encoding="utf-8", errors="replace")
    stamp = re.search(r"Source hash: ([0-9a-f]{64})", text)
    if not stamp or stamp.group(1) != hashlib.sha256(WEBUI.read_bytes()).hexdigest():
        return "src/WebUI_GZ.h was built from another WebUI.h (a pio run refreshes it)"
    # The page is an array in the header, or, in an image that keeps it on
    # LittleFS (custom_fs_pages), the file the same generation wrote there.
    pages = [(m.group(1), gzip.decompress(bytes(int(x, 16) for x in
                                                re.findall(r"0x[0-9a-fA-F]{2}", m.group(2)))))
             for m in re.finditer(r"(\w+_GZ)\[\]\s*(?:PROGMEM)?\s*=\s*\{(.*?)\};", text, re.S)]
    pages += [(f"data/web/{f.name}", gzip.decompress(f.read_bytes()))
              for f in sorted((REPO / "data" / "web").glob("*.html.gz"))]
    for name, page in pages:
        page = page.decode("utf-8", "replace")
        a = page.find("const TV_CAP")
        if a < 0:
            continue
        b = page.find("window.tvView", a)
        if b < 0:
            raise SystemExit(f"FAIL: {name} has the viewer but no window.tvView after "
                             f"its pure part; this test cuts the served copy there")
        # Back to the start of that line, so an "async" before it stays out.
        return name, page[a:page.rfind("\n", a, b) + 1]
    raise SystemExit("FAIL: neither src/WebUI_GZ.h nor data/web/ carries the viewer")


def block(src: str, name: str) -> str:
    """One page or asset of WebUI.h: from its declaration to the end of its raw string."""
    a = src.find(f"static const char {name}[] PROGMEM")
    if a < 0:
        raise SystemExit(f"FAIL: {name} is not in WebUI.h")
    return src[a:src.find(')raw";', a)]


def wiring(src: str) -> list:
    """What node cannot see, read off the source: the views the pages draw.

    The engine lives once, in LANG_JS, so a page that grew its own copy would
    pay for it in every image and drift from the tested one. The telemetry
    previews sit inside a form, so a button without type="button" would submit
    it on every click of Organized or Collapse all."""
    fails = []
    lang, files, tel = block(src, "LANG_JS"), block(src, "FILE_PAGE"), block(src, "TEL_PAGE")
    if "window.tvView = function" not in lang or "function tvParse" not in lang:
        fails.append("LANG_JS does not define the engine and window.tvView")
    for name, page in (("FILE_PAGE", files), ("TEL_PAGE", tel)):
        if "function tvParse" in page or "function tvJson" in page:
            fails.append(f"{name} carries its own copy of the engine")
    if files.count("window.tvView(") != 1:
        fails.append("FILE_PAGE does not draw its viewer through window.tvView")
    if "window.tvView(" not in tel:
        fails.append("TEL_PAGE does not draw its previews through window.tvView")
    for pid in ("preview", "apreview"):
        for b in "ORECN":
            if f'id="{pid}{b}"' not in tel:
                fails.append(f"TEL_PAGE has no #{pid}{b}")
        for b in "OREC":
            m = re.search(rf'<button[^>]*id="{pid}{b}"[^>]*>', tel)
            if m and 'type="button"' not in m.group(0):
                fails.append(f"TEL_PAGE #{pid}{b} is a submit button inside the form")
        if not re.search(rf'<pre class="tv" id="{pid}"', tel):
            fails.append(f"TEL_PAGE #{pid} is not a tv pre")
    return fails


def node_fails(label: str, js: str) -> bool:
    """Run `js` under node; True when it ran and printed no failure."""
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed; the viewer cannot be exercised")
    r = subprocess.run([node, "-e", js], capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        print(f"  FAIL {label}: node rejected it\n{r.stderr[-2000:]}")
        return False
    fails = json.loads(r.stdout.strip().splitlines()[-1])
    for f in fails:
        print(f"  FAIL {label}: {f}")
    return not fails


def run(label: str, code: str) -> bool:
    js = code + "\n" + (HARNESS.replace("__CASES__", json.dumps(CASES))
                        .replace("__VIEWABLE__", json.dumps(VIEWABLE))
                        .replace("__IS_TEXT__", json.dumps(IS_TEXT)))
    ok = node_fails(label, js)
    if ok:
        print(f"  ok   {label}: {len(CASES)} files, {len(VIEWABLE)} names, "
              f"{len(IS_TEXT)} texts, 300 random documents, size and depth")
    return ok


def main() -> int:
    src = WEBUI.read_text(encoding="utf-8", errors="replace")
    raw = extract(src, WEBUI.name)
    fails = wiring(src)
    for f in fails:
        print(f"  FAIL wiring: {f}")
    if not fails:
        print("  ok   wiring: one engine in LANG_JS, drawn by the file viewer and both telemetry previews")
    ok = not fails
    ok = run("as written", raw) and ok
    ok = run("minified by build_webui_gz.py", minifier()(raw)) and ok
    served = served_copy()
    if isinstance(served, tuple):
        ok = run(f"served ({served[0]})", served[1]) and ok
    else:
        print(f"  --   served copy: {served}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
