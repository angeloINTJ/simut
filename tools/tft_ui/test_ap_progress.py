#!/usr/bin/env python3
"""test_ap_progress.py -- the original panel's boot screen while the setup-AP gesture is held: one clear, then only the bar (2026-11-04).

The defect (the small-bugs triage of 2026-11-04, candidate 5; found by reading the code): while the finger holds the panel at boot, the boot
loop sets the hold's percentage every 50 ms (AppManager_Boot.cpp, setApProgress( pct )), and the original's boot renderer
(src/ui/original/DisplayManager_Render.cpp) decides whether to clear the whole screen with `enterAp = fullRedraw || prev < 0`. But
`fullRedraw` is true whenever the percentage changed, which it does at every step, so the 320x240 screen was cleared and the caption
stamped again at every step of the hold: the flicker the comment beside it says is gone. This holds the renderer to that comment:

  1. The two expressions, `fullRedraw` and `enterAp`, are cut out of the boot branch of DisplayManager_Render.cpp verbatim and evaluated
     here over the states the boot loop produces: the boot terminal on screen (percentage -1), then a hold from 0 to 100 % in steps of 5.
     Over the whole hold the screen is cleared exactly once, on entering it.
  2. A language that arrives in the middle of the hold clears once more (the caption is translated), and a boot whose first frame is
     already the hold (the previous frame was not the boot screen) clears on that frame, whatever percentage the other screen's state
     carried (today -2 or -1, so the previous-percentage term alone would also clear; the rule must not hang on that).
  3. A cancelled hold (the percentage back to -1, then the boot terminal again) is a full redraw: the terminal is painted again over the
     hold's frame. The fix must keep `fullRedraw` true there.
  4. The mutants, each of which must fail 1-3: M1 `enterAp` back to `fullRedraw || prev < 0` (the defect), M2 without `langJustChanged`,
     M3 without the not-booting term, M4 `fullRedraw` without the percentage term (a cancel would leave the hold's frame on screen).

While the defect stands the base run is red and the mutants are not run.

Usage: python3 tools/tft_ui/test_ap_progress.py   exit 0 when all hold, 1 when not, 2 when an expression is not found exactly once.
"""
import pathlib
import re
import sys
from types import SimpleNamespace

ROOT = pathlib.Path(__file__).resolve().parents[2]
RENDER = ROOT / 'src' / 'ui' / 'original' / 'DisplayManager_Render.cpp'

FULL_RE = re.compile(r'bool\s+fullRedraw\s*=\s*(.*?);', re.S)
ENTER_RE = re.compile(r'const\s+bool\s+enterAp\s*=\s*(.*?);', re.S)


def cut(text, rx, name):
    found = rx.findall(text)
    if len(found) != 1:
        print(f'ERROR: {name} found {len(found)} times in {RENDER.relative_to(ROOT)} (expected exactly once)')
        sys.exit(2)
    return ' '.join(found[0].split())


def to_python(expr):
    """The C++ boolean expression as Python: the operators these two lines use, nothing more (an unknown token fails the eval)."""
    py = expr.replace('||', ' or ').replace('&&', ' and ')
    py = re.sub(r'!(?!=)', ' not ', py)
    py = re.sub(r'\bfalse\b', 'False', py)
    py = re.sub(r'\btrue\b', 'True', py)
    return py


def frame(full_expr, enter_expr, prev, cur, lang=False):
    """One render of the boot branch: (fullRedraw, enterAp or None when the hold's branch is not taken)."""
    ns = {'_lastRenderedState': prev, 'state': cur, 'langJustChanged': lang}
    full_redraw = eval(to_python(full_expr), {}, ns)
    if cur.apProgressPct < 0:
        return full_redraw, None
    ns['fullRedraw'] = full_redraw
    return full_redraw, eval(to_python(enter_expr), {}, ns)


def st(booting, pct):
    return SimpleNamespace(isBooting=booting, apProgressPct=pct)


def checks(full_expr, enter_expr):
    """The failures of 1-3, as text (empty when all hold)."""
    bad = []
    # 1. the boot terminal, then a hold from 0 to 100 % in steps of 5: one clear, on entering.
    prev, clears = st(True, -1), 0
    for pct in range(0, 101, 5):
        cur = st(True, pct)
        _, enter = frame(full_expr, enter_expr, prev, cur)
        clears += 1 if enter else 0
        if pct == 0 and not enter:
            bad.append('1: entering the hold (0 %) did not clear the screen')
        prev = cur
    if clears != 1:
        bad.append(f'1: a hold of 21 steps cleared the screen {clears} times (expected 1)')
    # 2a. a language arriving in the middle of the hold clears once more.
    _, enter = frame(full_expr, enter_expr, st(True, 45), st(True, 50), lang=True)
    if not enter:
        bad.append('2: a language arriving at 50 % did not clear (the caption stays in the old language)')
    # 2b. a first frame that is already the hold (the previous frame was not the boot screen) clears, whatever percentage that
    #     other screen's state carried: the constructor leaves -2 and the end of the hold window -1, so today the previous
    #     percentage alone would say "entering"; the rule is that coming from another screen always clears, and a change to
    #     how that field is kept must not quietly turn it off.
    for prev_pct in (-2, -1, 0, 50):
        _, enter = frame(full_expr, enter_expr, st(False, prev_pct), st(True, 0))
        if not enter:
            bad.append(f'2: a hold on the first boot frame (previous frame another screen, its percentage {prev_pct}) did not clear')
    # 3. a cancelled hold: the percentage back to -1 is a full redraw of the boot terminal.
    full, _ = frame(full_expr, enter_expr, st(True, 60), st(True, -1))
    if not full:
        bad.append('3: a cancelled hold (60 % -> -1) is not a full redraw: the hold frame would stay under the terminal')
    return bad


def main():
    text = RENDER.read_text(encoding='utf-8')
    full_expr, enter_expr = cut(text, FULL_RE, 'fullRedraw'), cut(text, ENTER_RE, 'enterAp')
    print(f'fullRedraw = {full_expr}')
    print(f'enterAp    = {enter_expr}')
    bad = checks(full_expr, enter_expr)
    for b in bad:
        print(f'FAIL {b}')
    if bad:
        print(f'test_ap_progress: {len(bad)} failure(s); the mutants are not run against a red base')
        return 1
    print('PASS 1-3: one clear over the hold, again on a language and on a first frame, a full redraw on a cancel')
    mutants = {
        'M1 enterAp = fullRedraw || prev < 0 (the defect)': (full_expr, 'fullRedraw || (_lastRenderedState.apProgressPct < 0)'),
        'M2 without langJustChanged': (full_expr, re.sub(r'\|\|\s*langJustChanged', '', enter_expr)),
        'M3 without the not-booting term': (full_expr, re.sub(r'!_lastRenderedState\.isBooting\s*\|\|', '', enter_expr)),
        'M4 fullRedraw without the percentage term': (re.sub(r'\|\|\s*\(_lastRenderedState\.apProgressPct != state\.apProgressPct\)', '',
                                                               full_expr), enter_expr),
    }
    survived = 0
    for name, (f, e) in mutants.items():
        if (f, e) == (full_expr, enter_expr):
            print(f'FAIL {name}: the mutation did not change the cut text (its anchor moved)')
            survived += 1
            continue
        killed = bool(checks(f, e))
        print(f'{"PASS" if killed else "FAIL"} {name}: {"killed" if killed else "SURVIVED"}')
        survived += 0 if killed else 1
    if survived:
        print(f'test_ap_progress: {survived} mutant(s) not killed')
        return 1
    print(f'test_ap_progress: all hold, {len(mutants)} mutants killed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
