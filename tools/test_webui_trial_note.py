#!/usr/bin/env python3
"""What the dashboard and the Files page say about the update a Pico 2 W runs.

The Pico 2 W's update goes into the slot the board did not boot from and boots
on trial (docs/analysis/OTA_AB_RP2350.md, step 5). /api/status says so in three
fields only that chip sends: `slot`, `trial` (1 until the update has run
healthy for a minute and kept itself) and `reverted` (the version of an update
the boot ROM went back from). Until this test no page read them, and the Files
page warned that an update reformats the file system, which on this chip it
does not.

  * window.fwSlotNote( ), in lang.js between the `fw: slot note` and `fw: end
    of slot note` comments, turns `sys` into the note: none on a Pico W, which
    sends none of the three, or when nothing is pending; the trial while
    `trial` is 1; the way back while `reverted` names a version. It runs under node, as written and
    as the build's minifier leaves it;
  * all of it sits in `@IF slot` blocks. Every RP2040 image cuts them
    (`web_omit` of [chip.rp2040] in tools/features.toml) and keeps none of it;
    the RP2350's keeps all of it: the note in lang.js, the dashboard showing it
    on every poll, and the Files page's own warning;
  * the keys it needs are in both language packs, and the way back keeps its
    `{v}` there;
  * every RP2040 profile omits `slot`, and the RP2350's does not.

Run: python3 tools/test_webui_trial_note.py
Exit status is 0 on pass, 1 on failure.
"""

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
START = "/* fw: slot note"
END = "/* fw: end of slot note */"
KEYS = ("fw_trial", "fw_reverted", "fil_fw_warn_slot")
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
    return cond


# [sys, texts the note must contain, texts it must not]. Empty `must` and the
# empty string in mustNot below mean "no note at all".
CASES = [
    [None, [], ["*"]],
    [{"ver": "2.10.0", "env": "release"}, [], ["*"]],                        # a Pico W: no slot
    [{"slot": "A", "trial": 0, "reverted": ""}, [], ["*"]],
    [{"slot": "-", "trial": 0, "reverted": ""}, [], ["*"]],                   # no partition table
    [{"slot": "B", "trial": 1, "reverted": ""},
     ["on trial", "healthy for a minute", "previous version"], ["undefined", "613"]],
    [{"slot": "A", "trial": 0, "reverted": "2.10.9"},
     ["v2.10.9", "went back", "613"], ["undefined", "on trial", "{v}"]],
    [{"slot": "B", "trial": 1, "reverted": "2.10.9"},
     ["on trial", "v2.10.9"], ["undefined"]],
]

# The note assigns to window as it loads, so window comes first.
PRELUDE = "const window = { t: (k, d) => d };\n"
HARNESS = r"""
const cases = __CASES__;
const fails = [];
for (const [sys, must, mustNot] of cases) {
    let m;
    try { m = window.fwSlotNote(sys); }
    catch (e) { fails.push(JSON.stringify(sys) + ': threw ' + e); continue; }
    if (typeof m !== 'string') { fails.push(JSON.stringify(sys) + ': not a string: ' + m); continue; }
    if (mustNot.includes('*') && m !== '') fails.push(JSON.stringify(sys) + ': wanted no note, got ' + JSON.stringify(m));
    for (const s of must) if (!m.includes(s)) fails.push(JSON.stringify(sys) + ': missing ' + JSON.stringify(s) + ' in ' + JSON.stringify(m));
    for (const s of mustNot) if (s !== '*' && m.includes(s)) fails.push(JSON.stringify(sys) + ': has ' + JSON.stringify(s) + ' in ' + JSON.stringify(m));
}
console.log(JSON.stringify(fails));
"""


def generator() -> dict:
    """build_webui_gz.py's functions, loaded without running its generate()."""
    ns = {"__name__": "build_webui_gz"}
    exec(compile(GEN.read_text(encoding="utf-8").replace("\ngenerate()\n", "\n"),
                 str(GEN), "exec"), ns)
    return ns


def blocks(ns, omit):
    """The page blocks of WebUI.h as an image with these web features omitted gets them."""
    ns["WEB_OMIT"] = set(omit)
    cut = ns["_strip_web_features"](ns["_strip_disabled_langs"](WEBUI.read_text(encoding="utf-8")))
    return {n: c for n, c in ns["_PROGMEM_RE"].findall(cut)}


def run_node(label, code):
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node is not installed")
    js = PRELUDE + code + "\n" + HARNESS.replace("__CASES__", json.dumps(CASES))
    r = subprocess.run([node, "-e", js], capture_output=True, text=True, timeout=60)
    if not check(r.returncode == 0, f"{label}: node rejected it: {r.stderr[-1500:]}"):
        return
    for f in json.loads(r.stdout.strip().splitlines()[-1]):
        fails.append(f"{label}: {f}")


def test_the_note(ns):
    src = WEBUI.read_text(encoding="utf-8")
    a, b = src.find(START), src.find(END)
    if not check(a >= 0 and b > a, "window.fwSlotNote( ) and its markers are not in WebUI.h. If it "
                                   "moved or was renamed, move this test with it."):
        return
    code = src[a:b]
    run_node("as written", code)
    run_node("minified", ns["_minify_web_block"](code, "js"))


def test_the_cut(ns):
    pico_w = blocks(ns, {"slot"})
    pico2 = blocks(ns, set())
    names = ("LANG_JS", "DASH_PAGE", "FILE_PAGE")
    for n in names:
        check(n in pico_w and n in pico2, f"{n} is not a page block of WebUI.h")
    marks = ("fwSlotNote", "fwSlotShow", "fil_fw_warn_slot", "fw_trial", "fw_reverted")
    for n in names:
        left = [m for m in marks if m in pico_w.get(n, "")]
        check(not left, f"{n}, cut for a Pico W image, still carries {left}")
    check("window.fwSlotNote = function" in pico2.get("LANG_JS", "") and
          "window.fwSlotShow = function" in pico2.get("LANG_JS", ""),
          "lang.js of the Pico 2 W image does not define fwSlotNote and fwSlotShow")
    check(re.search(r"fetchLoop[\s\S]*window\.fwSlotShow\(d\)", pico2.get("DASH_PAGE", "")) is not None,
          "the Pico 2 W dashboard does not show the note on its status poll")
    files = pico2.get("FILE_PAGE", "")
    check("window.fwSlotShow(" in files, "the Pico 2 W Files page does not show the note")
    m = re.search(r"fmFirmware = function[\s\S]*?window\.t\('fil_fw_warn_slot', '([^']*)'", files)
    if check(m is not None, "the Pico 2 W Files page does not replace the Pico W's warning"):
        warn = m.group(1)
        check("REFORMAT" not in warn.upper() and "trial" in warn and "stay as they are" in warn,
              f"the Pico 2 W warning does not say what its update does: {warn!r}")


def test_the_packs():
    for p in PACKS:
        s = p.read_text(encoding="utf-8")
        m = re.search(r"^@WEBDICT\n(\{.*\})$", s, re.M)
        if not check(m is not None, f"{p.name}: no @WEBDICT line"):
            continue
        d = json.loads(m.group(1))
        missing = [k for k in KEYS if not d.get(k, "").strip()]
        check(not missing, f"{p.name}: @WEBDICT lacks {missing}: those lines stay in English")
        check("{v}" in d.get("fw_reverted", "{v}"), f"{p.name}: fw_reverted lost its {{v}}, the version")


def test_the_profiles():
    sys.path.insert(0, str(REPO / "tools"))
    import gen_features as gf
    M = gf.load_manifest()
    for name in M["profiles"]:
        prof = gf.resolve_profile(name, M["profiles"])
        omit = [w.strip() for w in (gf.compose(prof, M)["web_omit"] or "").split(",")]
        chip = prof.get("chip", "rp2040")
        if chip == "rp2040":
            check("slot" in omit, f"{name} (RP2040) keeps the slot blocks: custom_web_omit {omit}")
        else:
            check("slot" not in omit, f"{name} ({chip}) cuts the slot blocks: custom_web_omit {omit}")
    ini = (REPO / "tools" / "generated" / "profiles.ini").read_text(encoding="utf-8")
    for env, body in re.findall(r"^\[env:(\w+)\]\n(.*?)(?=^\[|\Z)", ini, re.S | re.M):
        line = re.search(r"^custom_web_omit = (.*)$", body, re.M)
        omit = [w.strip() for w in line.group(1).split(",")] if line else []
        rp2350 = "board = rpipico2w" in body
        check(("slot" in omit) != rp2350, f"profiles.ini [env:{env}] custom_web_omit {omit}")


def main():
    ns = generator()
    check("slot" in ns["WEB_FEATURES"], "build_webui_gz.py does not know the web feature 'slot'")
    for name, t in (("test_the_note", lambda: test_the_note(ns)), ("test_the_cut", lambda: test_the_cut(ns)),
                    ("test_the_packs", test_the_packs), ("test_the_profiles", test_the_profiles)):
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
