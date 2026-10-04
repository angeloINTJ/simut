#!/usr/bin/env python3
"""What the Files page says when the device does not commit a staged image.

A stage the device refuses comes back in one of two shapes:

  * refused by validation: `v` says why (7 another model, 8 unsigned, 9 a
    signature that does not match, 10 a retired key, 11 below the security
    level, 12 the bench key), and the page explains each code;
  * refused before any validation: no `v`, only `error`, in the device's own
    words. An RP2350 that boots from no slot of a partition table answers
    every stage this way (501; docs/analysis/OTA_AB_RP2350.md, step 4), as
    every RP2350 image did from step 2 until step 4. Before this test the page
    printed "validation v=undefined" for it.

The message is built by `fwStageMessage( )` in FILES_PAGE, between the
`fw: stage message` and `fw: end of stage message` comments. This test takes
it out of WebUI.h and runs it under node, as written and as the build's own
minifier leaves it, with window.t answering the English default. It also
checks that doFirmware( ) uses it, and that both language packs carry the key
the new shape needs.

Run: python3 tools/test_webui_firmware_refusal.py
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
PACKS = [REPO / "data" / "lang" / "language_pt-BR.lng", REPO / "data" / "lang" / "language_es-ES.lng"]
START = "/* fw: stage message"
END = "/* fw: end of stage message */"

UNAVAILABLE = "This board boots from no slot of a partition table. Install the factory image over USB."

# [HTTP status, reply, texts the message must contain, texts it must not]
CASES = [
    [422, {"st": 5, "v": 8, "committed": 0, "env": "release"},
     ["Upload failed (validation v=8). Cancelled.", "is not signed"], ["undefined"]],
    [422, {"st": 5, "v": 7, "committed": 0, "env": "air"},
     ["validation v=7", "another model (env air)."], ["undefined"]],
    [422, {"st": 5, "v": 9, "committed": 0, "env": "release"},
     ["validation v=9", "signature does not match"], []],
    [422, {"st": 5, "v": 11, "committed": 0, "env": "release"},
     ["validation v=11", "below the security level"], []],
    [422, {"st": 5, "v": 12, "committed": 0, "env": "release"},
     ["validation v=12", "bench key"], []],
    # A valid image whose metadata write failed: v 0, not committed. No reason is
    # known for it, and the page says only the code, as before.
    [200, {"st": 5, "v": 0, "committed": 0, "env": "release"},
     ["Upload failed (validation v=0). Cancelled."], ["undefined"]],
    # An RP2350 with no slot to write, refusing before any validation.
    [501, {"st": 0, "committed": 0, "error": UNAVAILABLE},
     ["Upload refused (HTTP 501). " + UNAVAILABLE], ["undefined", "validation v="]],
    # An error without text still names the status, and nothing reads "undefined".
    [501, {"st": 0, "committed": 0},
     ["Upload refused (HTTP 501)."], ["undefined", "validation v="]],
]

HARNESS = r"""
const window = { t: (k, d) => d };
const cases = __CASES__;
const fails = [];
for (const [status, reply, must, mustNot] of cases) {
    let m;
    try { m = fwStageMessage(status, reply); }
    catch (e) { fails.push(JSON.stringify(reply) + ': threw ' + e); continue; }
    if (typeof m !== 'string') { fails.push(JSON.stringify(reply) + ': not a string: ' + m); continue; }
    for (const s of must) if (!m.includes(s)) fails.push(JSON.stringify(reply) + ': missing ' + JSON.stringify(s) + ' in ' + JSON.stringify(m));
    for (const s of mustNot) if (m.includes(s)) fails.push(JSON.stringify(reply) + ': has ' + JSON.stringify(s) + ' in ' + JSON.stringify(m));
}
console.log(JSON.stringify(fails));
"""


def extract(src: str) -> str:
    a = src.find(START)
    b = src.find(END, a + 1)
    if a < 0 or b < 0:
        raise SystemExit("FAIL: fwStageMessage( ) and its markers are not in WebUI.h. If it moved or "
                         "was renamed, move this test with it — do not delete it to make it pass.")
    return src[a:b]


@functools.lru_cache(maxsize=None)
def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(GEN.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(GEN), "exec"), ns)
    return ns


def run(label: str, code: str) -> bool:
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed")
    js = code + "\n" + HARNESS.replace("__CASES__", json.dumps(CASES))
    r = subprocess.run([node, "-e", js], capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        print(f"  FAIL {label}: node rejected it\n{r.stderr[-2000:]}")
        return False
    fails = json.loads(r.stdout.strip().splitlines()[-1])
    for f in fails:
        print(f"  FAIL {label}: {f}")
    if not fails:
        print(f"  ok   {label}: {len(CASES)} replies")
    return not fails


def wiring(src: str) -> list:
    """What node cannot see, read off the source."""
    fails = []
    m = re.search(r"async function doFirmware\(.*?\n        }\n", src, re.S)
    if not m:
        return ["doFirmware( ) not found in WebUI.h"]
    body = m.group(0)
    if "fwStageMessage(r2.status, v)" not in body:
        fails.append("doFirmware( ) does not build the stage refusal with fwStageMessage(r2.status, v)")
    if "validation v=" in body:
        fails.append("doFirmware( ) still builds a 'validation v=' message of its own")
    for pack in PACKS:
        text = pack.read_text(encoding="utf-8")
        if '"fil_fw_stage_refused":' not in text:
            fails.append(f"{pack.name}: @WEBDICT has no fil_fw_stage_refused")
    return fails


def main() -> int:
    src = WEBUI.read_text(encoding="utf-8", errors="replace")
    raw = extract(src)
    fails = wiring(src)
    for f in fails:
        print(f"  FAIL wiring: {f}")
    if not fails:
        print("  ok   wiring: doFirmware( ) uses fwStageMessage( ), and both packs carry the key")
    ok = not fails
    ok = run("as written", raw) and ok
    ok = run("minified by build_webui_gz.py", generator()["_minify_js"](raw)) and ok
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
