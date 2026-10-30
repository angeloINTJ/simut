#!/usr/bin/env python3
"""test_env_ui.py — the gates that hold an Ângulo image follow the profile's `ui`, not the ending of the environment's name (S10c C3, 2026-10-30).

Until the Pico 2 W's release became the Ângulo image, "this image carries the Ângulo" was spelled in the CI as the name's ending: `if: endsWith(matrix.env, '_angulo')` on the
link gate (tools/check_ui_link.py). After the switch pico2_w_release is an Ângulo image under a name that ends in nothing of the kind, and pico2_w_original is an original one;
the suffix would have skipped the first. The gate is keyed by the profile now: tools/env_ui.py prints an environment's `ui` from tools/features.toml, and the link-gate step of the
workflow runs when it says "angulo". The static-RAM gate (tools/check_ui_ram.py, a post-build script) never read a name: it reads the image (the Ângulo's objects) and looks the
budget up by $PIOENV in tools/ui_ram_budget.json, so that file has an entry for every profile with ui = "angulo" and for no other; this test holds that list to the manifest, both
ways (tools/test_check_ui_ram.py has the gate's own cases).

Controls (a check is proven by its mutants): an Ângulo profile under any other name is still "angulo"; an original profile named like an Ângulo one is "original"; each edit of the
workflow that brings the suffix back, points the gate at the wrong answer, asks in the form that skips silently, deletes the question or adds an environment with no profile gives
exactly one finding; a budget list with an entry removed, one added or one of an original image gives exactly one.

    python3 tools/test_env_ui.py        # exit 1 on any divergence

@project SIMUT
@license MIT License
"""
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402
import env_ui  # noqa: E402

M = gf.load_manifest()
failures = []


def check(cond, msg):
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)


WORKFLOW = os.path.join(ROOT, ".github", "workflows", "build.yml")
TEXT = open(WORKFLOW, encoding="utf-8").read()
WANT_IF = "if: steps.ui.outputs.ui == 'angulo'"
ASK_LINES = ('ui=$(python3 tools/env_ui.py ${{ matrix.env }})', 'echo "ui=$ui" >> "$GITHUB_OUTPUT"')


def job_text(text, job):
    m = re.search(r"(?ms)^  %s:\n(.*?)(?=^  [A-Za-z0-9_-]+:\n|\Z)" % re.escape(job), text)
    return m.group(1) if m else ""


def workflow_findings(text, profiles):
    """What is wrong with how the firmware job of build.yml holds the Ângulo images (a list of sentences; empty when nothing is)."""
    out = []
    job = job_text(text, "firmware")
    steps = re.split(r"(?m)^      - (?=\S)", job)[1:]
    link = [i for i, s in enumerate(steps) if "check_ui_link.py .pio/build/" in s]
    ask = [i for i, s in enumerate(steps) if re.search(r"(?m)^        id: ui$", s)]
    if len(link) != 1:
        out.append(f"the firmware job has {len(link)} link-gate steps, not 1")
    else:
        ifs = [l.strip() for l in re.findall(r"(?m)^        if: .*$", steps[link[0]])]
        if ifs != [WANT_IF]:
            out.append(f"the link gate must run on `{WANT_IF}` and on nothing else; it has {ifs}")
    if len(ask) != 1:
        out.append(f"the firmware job has {len(ask)} steps with `id: ui`, not 1")
    else:
        lines = [l.strip() for l in steps[ask[0]].splitlines()]
        if not all(l in lines for l in ASK_LINES):
            out.append("the step `ui` must run tools/env_ui.py in an assignment of its own and then write the output (a failing command inside `echo \"...$(...)\"` would skip the gate silently)")
        if link and ask[0] > link[0]:
            out.append("the step `ui` runs after the link gate that reads its answer")
    m = re.search(r"(?m)^        env:\n((?:          (?:#.*|- .*)\n)+)", job)
    envs = [l.strip()[2:].strip() for l in (m.group(1).splitlines() if m else []) if l.strip().startswith("- ")]
    nope = [e for e in envs if e not in profiles]
    if not envs:
        out.append("the firmware matrix lists no environment")
    if nope:
        out.append(f"matrix environments that are no profile of the manifest (env_ui.py would fail on them): {nope}")
    return out


def ram_budget_findings(profiles, entries):
    """tools/ui_ram_budget.json's environments against the profiles that carry the Ângulo: (an Ângulo profile with no entry | an entry of no Ângulo profile) per sentence."""
    angulo = {n for n in profiles if env_ui.ui_of(n, {"profiles": profiles}) == "angulo"}
    return ([f"{n}: a profile with ui = angulo and no entry in tools/ui_ram_budget.json" for n in sorted(angulo - set(entries))]
            + [f"{n}: an entry in tools/ui_ram_budget.json that is not a profile with ui = angulo" for n in sorted(set(entries) - angulo)])


# ---- the answers, from the real manifest ----------------------------------------------------------------------------------------------------------------------------
want = {"pico2_w_release": "angulo", "pico_wiretft_angulo": "angulo", "pico2_w_original": "original", "pico_w_release": "original", "pico_wiretft": "original",
        "pico_w_alpha": "original", "pico_w_air": "original", "pico_wire": "original", "pico_w_test": "original", "pico_w_test_https": "original", "pico_w_asserts": "original"}
for env, ui in want.items():
    check(env_ui.ui_of(env, M) == ui, f"{env} builds the {ui} look")
check(set(want) == set(M["profiles"]), "the table above names every profile of the manifest (a new profile is decided here)")
ini = open(os.path.join(ROOT, "tools", "generated", "profiles.ini"), encoding="utf-8").read()
check(set(re.findall(r"(?m)^\[env:([^\]]+)\]", ini)) == set(M["profiles"]), "every profile has its environment in tools/generated/profiles.ini, and no other has one")

# ---- the controls: another name, same look (the suffix is not the key) -----------------------------------------------------------------------------------------------
extra = ('\n[profiles.pico2_w_zz]\nextends = "pico2_w_release"\nui = "angulo"\npublish = false\nota_trust_bench = false\n'
         '\n[profiles.x_angulo]\nextends = "pico2_w_original"\nui = "original"\npublish = false\nota_trust_bench = false\n')
with tempfile.TemporaryDirectory() as tmp:
    mp = os.path.join(tmp, "features.toml")
    open(mp, "w", encoding="utf-8").write(open(gf.MANIFEST, encoding="utf-8").read() + extra)

    def cli(*args):
        p = subprocess.run([sys.executable, "-B", os.path.join(HERE, "env_ui.py"), *args], capture_output=True, text=True)
        return p.returncode, p.stdout.strip(), p.stderr.strip()

    check(cli("--manifest", mp, "pico2_w_zz") == (0, "angulo", ""), "control: an Ângulo profile under another name (pico2_w_zz) is still `angulo`, by the command line")
    check(cli("--manifest", mp, "x_angulo") == (0, "original", ""), "control: an original profile named like an Ângulo one (x_angulo) is `original`, by the command line")
    check(cli("pico2_w_zz")[0] == 2, "... and the real manifest does not know pico2_w_zz (exit 2)")
    rc, out, err = cli("pico2_w_release")
    check((rc, out) == (0, "angulo"), "the command line on the real manifest: pico2_w_release -> angulo")
    rc, out, err = cli("pico2_w_original")
    check((rc, out) == (0, "original"), "the command line on the real manifest: pico2_w_original -> original")
    rc, out, err = cli("pico2_w_nope")
    check(rc == 2 and out == "" and "pico2_w_nope" in err and "tools/features.toml" in err, "an unknown environment: exit 2, nothing on stdout, the message names it and the manifest")
    check(cli()[0] == 2, "no environment: exit 2")

# ---- the workflow ---------------------------------------------------------------------------------------------------------------------------------------------------
real = workflow_findings(TEXT, M["profiles"])
check(real == [], "the real build.yml holds the Ângulo images by the profile's ui" + ("" if not real else f": {real}"))
check("endsWith(matrix.env" not in TEXT, "no step of build.yml is keyed by the ending of the environment's name")


def mutant(name, old, new):
    assert TEXT.count(old) == 1, (name, TEXT.count(old))
    found = workflow_findings(TEXT.replace(old, new), M["profiles"])
    check(len(found) == 1, f"mutant `{name}` gives exactly one finding" + ("" if len(found) == 1 else f" (it gave {found})"))


mutant("the suffix is back", WANT_IF, "if: endsWith(matrix.env, '_angulo')")
mutant("the gate waits for the wrong answer", WANT_IF, "if: steps.ui.outputs.ui == 'original'")
mutant("the question is asked inside an echo (a failure would pass silently)", "          ui=$(python3 tools/env_ui.py ${{ matrix.env }})\n          echo \"ui=$ui\" >> \"$GITHUB_OUTPUT\"\n",
       "          echo \"ui=$(python3 tools/env_ui.py ${{ matrix.env }})\" >> \"$GITHUB_OUTPUT\"\n")
mutant("the question has no id", "        id: ui\n", "")
mutant("an environment with no profile joins the matrix", "          - pico2_w_original\n", "          - pico2_w_original\n          - pico2_w_zz\n")

# ---- the static-RAM budget's list -------------------------------------------------------------------------------------------------------------------------------------
budget = json.load(open(os.path.join(HERE, "ui_ram_budget.json"), encoding="utf-8"))["envs"]
real = ram_budget_findings(M["profiles"], budget)
check(real == [], "tools/ui_ram_budget.json has an entry for every profile with ui = angulo and for no other" + ("" if not real else f": {real}"))
check(sorted(budget) == ["pico2_w_release", "pico_wiretft_angulo"], "... the two Ângulo images today")
for name, mutated in (("an entry removed", {k: v for k, v in budget.items() if k != "pico2_w_release"}),
                      ("an entry of an original image added", {**budget, "pico2_w_original": {"budget": 1}}),
                      ("an entry of no profile added", {**budget, "pico2_w_zz": {"budget": 1}})):
    found = ram_budget_findings(M["profiles"], mutated)
    check(len(found) == 1, f"mutant `{name}` gives exactly one finding" + ("" if len(found) == 1 else f" (it gave {found})"))
# the same list, with an Ângulo profile under another name and no entry: found, whatever the name
M2 = dict(M["profiles"]); M2["pico2_w_zz"] = {"extends": "pico2_w_release", "ui": "angulo"}
found = ram_budget_findings(M2, budget)
check(len(found) == 1 and found[0].startswith("pico2_w_zz:"), "control: an Ângulo profile under another name with no entry is found by its name, not by an ending")

print(f"\n{len(failures)} failed" if failures else "\nall passed")
sys.exit(1 if failures else 0)
