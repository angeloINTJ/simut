#!/usr/bin/env python3
"""test_keypad_answer.py -- tests tools/keypad_answer.py, the one reader of GET /api/keypad for the two bench tools that drive the ORIGINAL panel
(tools/panel_users_hw_test.py and tools/panel_fulltable_test.py).

Run: python3 tools/test_keypad_answer.py        (-v: one line per case and per control)

WHY (S12, F4 of v2.13.0's Known list, option (c), 2026-10-31). On an image built with the Angulo the route sends `faces`, `up`, `kb` and
`policy` plus `"targets":"/api/ui"`, and no `grid` and no `pop`: the original panel's geometry cannot describe the Angulo's keys, and a
client that taps by it misses them. The two tools above tap the original panel by that geometry, so they have to refuse an Angulo answer
with a message that says where to tap instead (the targets of /api/ui, which tools/tft_ui/roteiro_ui.py reads), and not run on and miss.
An answer without a `grid` is also what a device older than v25 sends, and the tools have always fallen back to the v24 card layout for
it: that stays, and it is not an Angulo answer.

WHAT IT PINS. Four cases, each an answer as the firmware writes it (the key order is the order of the format strings in
WebManager::handleApiKeypad( ), src/WebManager_History.cpp; the numbers are PinKb::GRIDS, GROUP_GRID, POP_* and LEN_MAX of src/PinKeypad.h):
  original   a six-card answer (digits, two glyphs a card): the grid, the policy, no pop;
  angulo     the same faces with `targets`: refused, and the message names /api/ui and tools/tft_ui/roteiro_ui.py;
  pre-v25    only `up` and `faces`: the v24 grid, the 'cards' keyboard, no pop, no policy;
  groups     the alphanumeric ordered keyboard: nine group keys, its grid, `pop` as a dict, and kb 'groups'.
Two more checks hold the wiring: each tool reads the route through keypad_answer.parse( ) and no longer takes a key of the answer
(faces, up, kb, grid, pop, policy) off the JSON by hand, which is how a tool would bypass the refusal.

THE NEGATIVE CONTROLS. A check only counts if it bites: the module is run again with one line of it taken out, and exactly the cases that
line serves have to fail. The refusal taken out fails the angulo case and no other; the v24 fallback taken out fails pre-v25; `pop` taken
out fails groups; and `grid` ignored (the fallback for every answer) fails original and groups. A mutation that does not apply to the
module's text (its line moved) is an error of this test, not a pass.

Project: SIMUT
License: MIT
"""
import ast
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
MODULE = os.path.join(TOOLS, "keypad_answer.py")

try:
    import keypad_answer
except ImportError as e:
    print(f"test_keypad_answer: FAILED, tools/keypad_answer.py cannot be imported ({e})")
    sys.exit(1)

# ── the answers, as the firmware writes them ──────────────────────────────────────────────────────────────────
# Digits, KB_SET2 (two glyphs a card, six cards, two decoys), policy 4..12 digits: GRIDS[ALPHA_DIGITS][KB_SET2] = { 6, 2, 3, 2, 6, 72, 100, 44, 104, 50 }.
ORIGINAL = '{"faces":["12","3!","45","67","8@","90"],"up":true,"kb":"cards","grid":[6,2,3,2,6,72,100,44,104,50],"policy":[4,12,2,0]}'
# The same faces on the Angulo's image: option (c), no grid and no pop, and the route the geometry is in.
ANGULO = '{"faces":["12","3!","45","67","8@","90"],"up":true,"kb":"cards","policy":[4,12,2,0],"targets":"/api/ui"}'
# A device older than v25 (the geometry went on the wire in v25): the four v24 cards and nothing else.
PRE_V25 = '{"up":true,"faces":["123","45!","678","90@"]}'
# ALPHA_ALNUM with KB_PLAIN: GROUP_GRID = { 9, 1, 3, 3, 6, 72, 100, 36, 104, 40 }, POP_KEY_W 56, POP_KEY_H 52, POP_GAP 6, POP_ONE_Y 104, POP_ROW0_Y 76, POP_ROW1_Y 132,
# LEN_MAX[KB_PLAIN] 16. The digits group is ten characters wide.
GROUPS = ('{"faces":["0123456789","ABC","DEF","GHI","JKL","MNO","PQRS","TUV","WXYZ"],"up":true,"kb":"groups",'
          '"grid":[9,1,3,3,6,72,100,36,104,40],"policy":[4,16,1,1],"pop":[56,52,6,104,76,132]}')


def want(label, got, expected):
    if got != expected:
        raise AssertionError(f"{label}: got {got!r}, wanted {expected!r}")


def case_original(m):
    k = m.parse(json.loads(ORIGINAL))
    want("faces", k.faces, ["12", "3!", "45", "67", "8@", "90"])
    want("up", k.up, True)
    want("kb", k.kb, "cards")
    want("grid", k.grid, dict(keys=6, slots=2, cols=3, rows=2, x=6, y=72, w=100, h=44, px=104, py=50))
    want("pop", k.pop, None)
    want("policy", k.policy, dict(min=4, max=12, keypad=2, alphabet=0))


def case_angulo(m):
    try:
        m.parse(json.loads(ANGULO))
    except m.AnguloAnswer as e:
        msg = str(e)
        for needle in ("/api/ui", "tools/tft_ui/roteiro_ui.py"):
            if needle not in msg:
                raise AssertionError(f"the refusal does not name {needle}: {msg!r}")
        return
    raise AssertionError("an answer with `targets` was accepted")


def case_pre_v25(m):
    k = m.parse(json.loads(PRE_V25))
    want("faces", k.faces, ["123", "45!", "678", "90@"])
    want("up", k.up, True)
    want("kb", k.kb, "cards")
    want("grid", k.grid, dict(keys=4, slots=3, cols=2, rows=2, x=6, y=72, w=152, h=44, px=156, py=50))
    if k.grid is m.PIN_GRID_FALLBACK:
        raise AssertionError("the fallback was handed out itself, and a caller that edits its grid would edit the next answer's")
    want("pop", k.pop, None)
    want("policy", k.policy, None)


def case_groups(m):
    k = m.parse(json.loads(GROUPS))
    want("faces", len(k.faces), 9)
    want("kb", k.kb, "groups")
    want("grid", k.grid, dict(keys=9, slots=1, cols=3, rows=3, x=6, y=72, w=100, h=36, px=104, py=40))
    want("pop", k.pop, dict(w=56, h=52, gap=6, one_y=104, row0_y=76, row1_y=132))
    want("policy", k.policy, dict(min=4, max=16, keypad=1, alphabet=1))


CASES = (("original", case_original), ("angulo", case_angulo), ("pre-v25", case_pre_v25), ("groups", case_groups))


def run_cases(m):
    """The names of the cases that failed on module `m`, with the reason of each."""
    failed = {}
    for name, fn in CASES:
        try:
            fn(m)
        except Exception as e:  # a case that raises anything else is a failed case too
            failed[name] = f"{type(e).__name__}: {e}"
    return failed


# ── the negative controls: the module with one line taken out ─────────────────────────────────────────────────
MUTANTS = (
    ("the refusal taken out", "if 'targets' in answer:", "if False:", {"angulo"}),
    ("the v24 fallback taken out", "grid = dict(PIN_GRID_FALLBACK)", "grid = None", {"pre-v25"}),
    ("pop taken out", "pop = _named(POP_FIELDS, answer.get('pop'))", "pop = None", {"groups"}),
    ("grid ignored (the fallback for every answer)", "grid = _named(GRID_FIELDS, answer.get('grid'))", "grid = None", {"original", "groups"}),
)


def mutant(src, old, new):
    n = src.count(old)
    if n != 1:
        raise RuntimeError(f"the mutation does not apply: {old!r} is in tools/keypad_answer.py {n} times, not once")
    m = types.ModuleType("keypad_answer_mutant")
    m.__file__ = MODULE
    exec(compile(src.replace(old, new), MODULE + " (mutant)", "exec"), m.__dict__)
    return m


# ── the wiring: each tool reads the route through the parser, and only through it ─────────────────────────────
ANSWER_KEYS = {"faces", "up", "kb", "grid", "pop", "policy"}


def wiring(tool):
    """(calls to keypad_answer.parse( ), the answer's keys taken off a JSON by hand) in a tool's source."""
    with open(os.path.join(TOOLS, tool), encoding="utf-8") as f:
        tree = ast.parse(f.read(), tool)
    parses, by_hand = 0, []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Attribute):
            continue
        if node.func.attr == "parse" and isinstance(node.func.value, ast.Name) and node.func.value.id == "keypad_answer":
            parses += 1
        if (node.func.attr == "get" and node.args and isinstance(node.args[0], ast.Constant)
                and node.args[0].value in ANSWER_KEYS):
            by_hand.append(f"line {node.lineno}: .get({node.args[0].value!r})")
    return parses, by_hand


def refusals_delete_account(tool="panel_users_hw_test.py"):
    """Every handler of AnguloAnswer in the tool deletes the run's full-admin account `smap` before it raises. The
    web login makes it in Rig.__init__( ), and nothing in this tool deletes it once a SystemExit is under way: the
    start-of-run refusal comes before main( )'s try, and that try's finally only writes the results and closes the
    port (`user del smap` is step_cleanup( ), a step, skipped once an earlier one raises). The review of F4 found a
    refused run leaving the account on the device (2026-10-31). The tool imports serial and requests, so it is read
    from the source, as the wiring is. Returns (handlers found, handlers that raise without the delete)."""
    with open(os.path.join(TOOLS, tool), encoding="utf-8") as f:
        tree = ast.parse(f.read(), tool)
    found, bare = 0, []
    for h in ast.walk(tree):
        if not (isinstance(h, ast.ExceptHandler) and isinstance(h.type, ast.Attribute)
                and h.type.attr == "AnguloAnswer"):
            continue
        found += 1
        deleted = False
        for st in h.body:
            if isinstance(st, ast.Raise):
                break
            for c in ast.walk(st):
                if (isinstance(c, ast.Call) and isinstance(c.func, ast.Attribute) and c.func.attr == "cfg"
                        and any(isinstance(a, ast.Constant) and a.value == "user del smap" for a in c.args)):
                    deleted = True
        if not deleted:
            bare.append(f"line {h.lineno}")
    return found, bare


def main():
    fails = []
    verbose = "-v" in sys.argv[1:]

    failed = run_cases(keypad_answer)
    for name, why in failed.items():
        fails.append(f"case {name}: {why}")
    if verbose:
        for name, _ in CASES:
            print(f"  case {name:9s} {'FAIL ' + failed[name] if name in failed else 'ok'}")

    with open(MODULE, encoding="utf-8") as f:
        src = f.read()
    bitten = 0
    for label, old, new, expected in MUTANTS:
        try:
            got = set(run_cases(mutant(src, old, new)))
        except RuntimeError as e:
            fails.append(f"control «{label}»: {e}")
            continue
        if verbose:
            print(f"  control «{label}»: fails {sorted(got)}, the line serves {sorted(expected)}")
        if got == expected:
            bitten += 1
        else:
            fails.append(f"control «{label}»: the cases that failed are {sorted(got)}, the line serves {sorted(expected)}")

    wired = 0
    for tool in ("panel_users_hw_test.py", "panel_fulltable_test.py"):
        parses, by_hand = wiring(tool)
        if parses < 1:
            fails.append(f"{tool} does not read the route through keypad_answer.parse( )")
        elif by_hand:
            fails.append(f"{tool} still takes the answer's keys by hand ({'; '.join(by_hand)}), which bypasses the refusal")
        else:
            wired += 1
    found, bare = refusals_delete_account()
    if found < 2:
        fails.append(f"panel_users_hw_test.py: {found} handler(s) of AnguloAnswer, where the start-of-run check and the "
                     f"keypad read make 2")
    elif bare:
        fails.append("panel_users_hw_test.py: a handler of AnguloAnswer stops without deleting the account _login( ) "
                     f"made (`self.cfg('user del smap')` before the SystemExit): {', '.join(bare)}")

    if fails:
        print("test_keypad_answer: FAILED")
        for f in fails:
            print("  -", f)
        return 1
    print(f"test_keypad_answer: {len(CASES)} cases pass on tools/keypad_answer.py; {bitten} of {len(MUTANTS)} controls bite "
          f"(the line taken out fails exactly the cases it serves); {wired} of 2 tools read the route through the parser; "
          f"each refusal deletes the run's web account before it stops.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
