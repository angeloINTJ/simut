#!/usr/bin/env python3
"""What the Files page says when the device refuses an upload.

The device refuses an upload it will not take with a 4xx, not a 5xx:
WebManager::handleUploadComplete answers 400 {"error":"Invalid upload"} for a
file name, a folder or a size it refuses, and 403 "Forbidden" in plain text to
a session without the upload permission. fetchSafe( ) throws only for a status
of 500 or more (503 excepted), so those answers came back to doUpload( ) as
ordinary responses, doUpload( ) never read `ok`, and the page refreshed the
listing and showed the green "Upload complete." for a file that never arrived.
Found on 2026-10-21 by running the page's code under node: a 400 came out as
a success, a 500 as an error.

This test takes doUpload( ) out of WebUI.h, with the page's own fetchSafe( ),
and runs both under node, as written and as the build's own minifier leaves
them, against a fake fetch that answers like the device:

  * 200 {"status":"ok"}: "Upload complete." in green and the listing refreshed;
  * a 4xx, or the 503 that fetchSafe( ) also lets through (a refusal): a red
    message with the status, the device's reason and what to check; the listing
    is not refreshed and the upload is not sent again;
  * a 500 or a network failure: "Upload error", as before (fetchSafe( ) throws).

It then runs the refusal with each language pack's @WEBDICT answering
window.t( ), and reads off the sources what node cannot see: that the firmware
answers what the fake fetch answers, that showToast( ) writes the device's
words as text and not as HTML, and that both packs carry the key.

Run: python3 tools/test_webui_upload_result.py [--mutations]
  --mutations   break doUpload( ) and fetchSafe( ) on purpose, one change at a
                time, and expect every change to fail the test.
Exit status is 0 on pass, 1 on failure.
"""

import functools
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WEBUI = REPO / "WebUI.h"
GEN = REPO / "tools" / "build_webui_gz.py"
ROUTE = REPO / "src" / "WebManager_Files.cpp"
PACKS = [REPO / "data" / "lang" / "language_pt-BR.lng", REPO / "data" / "lang" / "language_es-ES.lng"]
KEY = "fil_up_refused"
FIX = "Check the file name, the folder, the free space and your permission to upload"


def case(label, *, status=200, body="", net=False, kind="err", nav=False, has=(), no=(), calls=None):
    """One answer of the device and what the page must do about it: the toast's
    colour (`kind`), whether the listing is refreshed (`nav`), the texts the
    toast must and must not carry, and how many requests went out."""
    return {"label": label, "status": status, "body": body, "net": net, "type": kind,
            "nav": nav, "has": list(has), "no": list(no), "calls": calls}


NOT_A_SUCCESS = ["Upload complete", "undefined", "{s}", "[object Object]", "Upload error"]

CASES = [
    # The controls: what the page did right all along. A harness that fails
    # these says nothing about the refusals.
    case("200 stored", status=200, body='{"status":"ok"}', kind="ok", nav=True,
         has=["Upload complete."], no=["refused", "Upload error"], calls=1),
    case("200 whose body is not JSON", status=200, body="", kind="ok", nav=True,
         has=["Upload complete."], no=["refused", "Upload error"], calls=1),
    case("500 (fetchSafe throws), as before", status=500, body='{"error":"Failed"}',
         has=["Upload error"], no=["refused", "Upload complete"]),
    case("network failure, as before", net=True,
         has=["Upload error"], no=["refused", "Upload complete"]),
    # The refusals. The 400 and the 403 are the two handleUploadComplete( )
    # sends (the wiring check holds the fixtures to the firmware); the 409 and
    # the 413 are not sent by this route today and stand for "any other status
    # that is not a 2xx".
    case("400 Invalid upload (handleUploadComplete)", status=400, body='{"error":"Invalid upload"}',
         has=["Upload refused (HTTP 400: Invalid upload).", FIX], no=NOT_A_SUCCESS, calls=1),
    case("403 Forbidden in plain text (handleUploadComplete)", status=403, body="Forbidden",
         has=["Upload refused (HTTP 403).", FIX], no=NOT_A_SUCCESS, calls=1),
    case("409 with a reason", status=409, body='{"error":"File exists"}',
         has=["Upload refused (HTTP 409: File exists)."], no=NOT_A_SUCCESS, calls=1),
    case("413 with a reason", status=413, body='{"error":"Payload too large"}',
         has=["Upload refused (HTTP 413: Payload too large)."], no=NOT_A_SUCCESS, calls=1),
    case("413 with no body", status=413, body="",
         has=["Upload refused (HTTP 413)."], no=NOT_A_SUCCESS, calls=1),
    case("503 System Busy (fetchSafe does not throw for it)", status=503, body='{"error":"System Busy"}',
         has=["Upload refused (HTTP 503: System Busy)."], no=NOT_A_SUCCESS, calls=1),
    case("400 whose error is not text", status=400, body='{"error":{"code":7}}',
         has=["Upload refused (HTTP 400)."], no=NOT_A_SUCCESS, calls=1),
    case("400 whose body is JSON null", status=400, body="null",
         has=["Upload refused (HTTP 400)."], no=NOT_A_SUCCESS, calls=1),
]

# With a pack's @WEBDICT as window.t( ): translated, and the status and the
# reason still in it.
IN_THE_PACK = ["{s}", "Upload refused", "Check the file name", "Upload complete", "undefined"]
PACK_CASES = [
    case("400 in the pack", status=400, body='{"error":"Invalid upload"}',
         has=["(HTTP 400: Invalid upload)"], no=IN_THE_PACK, calls=1),
    case("403 in the pack", status=403, body="Forbidden",
         has=["(HTTP 403)"], no=IN_THE_PACK, calls=1),
]

# [what is broken, the text in the code (exactly once), what replaces it]
MUTATIONS = [
    ("the ok check dropped", "if (!r.ok) {", "if (false) {"),
    ("a 4xx back to success (only a 5xx is a refusal)", "if (!r.ok) {", "if (!r.ok && r.status >= 500) {"),
    ("only the 400 is a refusal", "if (!r.ok) {", "if (r.status === 400) {"),
    ("the 503 back to success", "if (!r.ok) {", "if (!r.ok && r.status !== 503) {"),
    ("the listing refreshed on a refusal", "                    return;\n",
     "                    fmNavigate(currentDir);\n                    return;\n"),
    ("no return after the refusal (falls into the success)", "                    return;\n", ""),
    ("the refusal in green", "'err');\n                    return;", "'ok');\n                    return;"),
    ("the status dropped from the message", "() => r.status + (why", "() => (why"),
    ("the device's reason dropped", "(why ? ': ' + why : '')", "''"),
    ("the body read without a guard (a plain-text 403 throws)",
     "try { const j = await r.json(); if (j && typeof j.error === 'string') why = j.error; } catch(e2) {}",
     "const j = await r.json(); if (j && typeof j.error === 'string') why = j.error;"),
    ("the reason taken without checking it is text", "if (j && typeof j.error === 'string') why = j.error;",
     "why = j.error;"),
    ("the {s} left in the message", ".replace('{s}', () => r.status + (why ? ': ' + why : ''))", ""),
    ("the message no longer asks the pack", "'fil_up_refused'", "'fil_up_unused'"),
    ("the success loses the refresh", "fmNavigate(currentDir); showToast(window.t('fil_uploaded'",
     "showToast(window.t('fil_uploaded'"),
    ("the success text changed", "'Upload complete.'", "'Done.'"),
    ("the catch says nothing", "catch(e) { showToast(window.t('fil_up_err','Upload error'), 'err'); }", "catch(e) { }"),
    ("the catch in green", "'Upload error'), 'err'); }", "'Upload error'), 'ok'); }"),
    ("the catch refreshes the listing", "catch(e) { showToast(", "catch(e) { fmNavigate(currentDir); showToast("),
    ("fetchSafe throws for every non-ok (the refusal is retried and reads as an error)",
     "!resp.ok && resp.status >= 500 && resp.status !== 503", "!resp.ok"),
]

# The stubs the page's code needs, ahead of it: its first statement already
# reads `window`.
HEAD = r"""
const realSetTimeout = setTimeout;
// fetchSafe( ) waits 1 s and then 2 s between attempts and arms an abort timer
// for the upload's own 60 s: the waits run at once and the abort timer never fires.
globalThis.setTimeout = (fn, ms) => (ms > 8000 ? 0 : realSetTimeout(fn, 0));
globalThis.clearTimeout = () => {};
globalThis.window = globalThis;
const PACK = __PACK__;
window.t = (k, d) => (PACK && Object.prototype.hasOwnProperty.call(PACK, k)) ? PACK[k] : d;
globalThis.document = { getElementById: () => ({}) };
globalThis.FormData = function () {};
globalThis.currentDir = '/data';
const toasts = [], navs = [];
globalThis.fmNavigate = (d) => { navs.push(d); };
globalThis.showToast = (m, type) => { toasts.push([m, type]); };
let calls = 0, scenario = null;
globalThis.fetch = async () => { calls++; return scenario(); };
const mkResp = (status, body) => ({ ok: status >= 200 && status < 300, status,
    json: async () => JSON.parse(body), text: async () => body });
"""

TAIL = r"""
const cases = __CASES__;
(async () => {
    const out = [];
    for (const c of cases) {
        toasts.length = 0; navs.length = 0; calls = 0;
        scenario = c.net ? (async () => { throw new TypeError('Failed to fetch'); })
                         : (async () => mkResp(c.status, c.body));
        const fails = [];
        try { await doUpload(); } catch (e) { fails.push('doUpload( ) threw ' + e); }
        if (toasts.length !== 1) fails.push(toasts.length + ' toasts: ' + JSON.stringify(toasts));
        else {
            const [m, type] = toasts[0];
            if (type !== c.type) fails.push('toast is ' + type + ', wanted ' + c.type + ': ' + JSON.stringify(m));
            for (const s of c.has) if (!m.includes(s)) fails.push('missing ' + JSON.stringify(s) + ' in ' + JSON.stringify(m));
            for (const s of c.no) if (m.includes(s)) fails.push('has ' + JSON.stringify(s) + ' in ' + JSON.stringify(m));
        }
        if (navs.length !== (c.nav ? 1 : 0)) fails.push('listing refreshed ' + navs.length + ' time(s), wanted ' + (c.nav ? 1 : 0));
        if (c.calls !== null && calls !== c.calls) fails.push(calls + ' request(s), wanted ' + c.calls);
        out.push({ label: c.label, fails });
    }
    console.log(JSON.stringify(out));
})();
"""


def extract(src: str) -> str:
    """The page's fetchSafe( ) and doUpload( ), as they stand in WebUI.h."""
    d = re.search(r"^[ \t]*async function doUpload\(\) \{.*?\n        \}\n", src, re.S | re.M)
    f = re.search(r"^[ \t]*window\.fetchSafe = function\(url, options\) \{.*\};[ \t]*$", src, re.M)
    if not d or not f:
        raise SystemExit("FAIL: doUpload( ) or window.fetchSafe( ) is not in WebUI.h where this test looks for it. "
                         "If it moved or was renamed, move this test with it — do not delete it to make it pass.")
    return f.group(0).strip() + "\n" + d.group(0)


@functools.lru_cache(maxsize=None)
def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(GEN.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(GEN), "exec"), ns)
    return ns


def webdict(pack: Path) -> dict:
    """The pack's @WEBDICT: the JSON line that follows the marker."""
    lines = pack.read_text(encoding="utf-8").splitlines()
    try:
        i = next(n for n, line in enumerate(lines) if line.startswith("@WEBDICT"))
        return json.loads(next(line for line in lines[i + 1:] if line.lstrip().startswith("{")))
    except (StopIteration, ValueError) as e:
        raise SystemExit(f"FAIL: {pack.name} has no readable @WEBDICT ({e})")


def run(code: str, cases: list, pack=None):
    """The cases' results, one {label, fails} each; None when node rejects the code."""
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed")
    js = (HEAD.replace("__PACK__", json.dumps(pack)) + "\n" + code + "\n"
          + TAIL.replace("__CASES__", json.dumps(cases)))
    r = subprocess.run([node, "-e", js], capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        print(f"  node rejected the code:\n{r.stderr[-1500:]}")
        return None
    return json.loads(r.stdout.strip().splitlines()[-1])


def show(label: str, results) -> bool:
    if results is None:
        return False
    ok = True
    for r in results:
        if r["fails"]:
            ok = False
            for f in r["fails"]:
                print(f"  FAIL {label} / {r['label']}: {f}")
        else:
            print(f"  ok   {label} / {r['label']}")
    return ok


def wiring(src: str) -> list:
    """What node cannot see, read off the sources."""
    fails = []
    cpp = ROUTE.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"void WebManager::handleUploadComplete\( \) \{.*?\n\}\n", cpp, re.S)
    if not m:
        fails.append("handleUploadComplete( ) is not in src/WebManager_Files.cpp: the 400 and 403 "
                     "fixtures are no longer held to the firmware")
    else:
        for want in ('send(400, "application/json", "{\\"error\\":\\"Invalid upload\\"}")',
                     'send(403, "text/plain", "Forbidden")'):
            if want not in m.group(0):
                fails.append(f"handleUploadComplete( ) no longer sends {want}: change the fixtures with it")
    t = re.search(r"^[ \t]*window\.showToast = function\(.*$", src, re.M)
    if not t:
        fails.append("window.showToast( ) is not in WebUI.h")
    elif ".textContent = msg" not in t.group(0) or "innerHTML" in t.group(0):
        fails.append("showToast( ) does not write the message as text: the device's reason would be read as HTML")
    for pack in PACKS:
        v = webdict(pack).get(KEY)
        if v is None:
            fails.append(f"{pack.name}: @WEBDICT has no {KEY}")
        elif "{s}" not in v:
            fails.append(f"{pack.name}: {KEY} has no {{s}} for the status")
    return fails


def mutations(raw: str) -> int:
    pack = webdict(PACKS[0])

    def passes(code):
        a, b = run(code, CASES), run(code, PACK_CASES, pack)
        if a is None or b is None:
            return None
        return all(not r["fails"] for r in a + b)

    if passes(raw) is not True:
        print("  FAIL control: the code as it stands does not pass, so no mutant can be judged")
        return 1
    print("  ok   control: the code as it stands passes")
    caught = 0
    for label, old, new in MUTATIONS:
        n = raw.count(old)
        if n != 1:
            print(f"  BAD    {label}: the text to break is in the code {n} times, not once")
            continue
        res = passes(raw.replace(old, new))
        if res is None:
            print(f"  BAD    {label}: node rejected the mutant")
        elif res:
            print(f"  MISSED {label}")
        else:
            caught += 1
            print(f"  caught {label}")
    print(f"{caught} of {len(MUTATIONS)} mutations caught")
    return 0 if caught == len(MUTATIONS) else 1


def main() -> int:
    src = WEBUI.read_text(encoding="utf-8", errors="replace")
    raw = extract(src)
    if "--mutations" in sys.argv[1:]:
        return mutations(raw)
    fails = wiring(src)
    for f in fails:
        print(f"  FAIL wiring: {f}")
    if not fails:
        print(f"  ok   wiring: the firmware sends what the fake fetch sends, showToast( ) writes text, both packs carry {KEY}")
    ok = not fails
    ok = show("as written", run(raw, CASES)) and ok
    ok = show("minified by build_webui_gz.py", run(generator()["_minify_js"](raw), CASES)) and ok
    for pack in PACKS:
        ok = show(pack.name, run(raw, PACK_CASES, webdict(pack))) and ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
