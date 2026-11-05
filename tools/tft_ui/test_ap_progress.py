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
  4. The hold's block itself (`if (state.apProgressPct >= 0) { ... }`, cut by its braces with the comments taken out): the screen is
     cleared exactly once in it, by the first statement under `if (enterAp) {`; the bar is painted after that block closes, so on every
     frame; the frame is recorded (`_lastRenderedState = state;`) after that block and before the `return;`; and the block ends in
     `return;`, so a hold frame never falls through to the boot terminal's painting (whose `fullRedraw` is true at every step). The
     expressions alone said nothing of what they guard: until 2026-11-04 the guard could read `if (fullRedraw) {` or `if (true) {` and this
     script still passed (the review of the small-bugs branch); and without the record `_lastRenderedState` keeps the boot terminal's state
     (percentage -1), so `enterAp` is true at every step, the defect again with both expressions intact, and the script still passed
     (the third review, 2026-11-04).
  5. The mutants, each of which must fail 1-4: M1 `enterAp` back to `fullRedraw || prev < 0` (the defect), M2 without `langJustChanged`,
     M3 without the not-booting term, M4 `fullRedraw` without the percentage term (a cancel would leave the hold's frame on screen),
     M5 the clear guarded by `fullRedraw`, M6 by `true`, M7 a second clear before the bar, M8 the block's `return;` gone, M9 the bar
     never painted, M10 the frame never recorded (`_lastRenderedState = state;` gone).

What it cannot see, written here because a list of names would not close it: the clear is recognised by its spelling, `fastClearScreen(`,
counted once in the hold's block. Another call that clears the whole panel under another name, a `fillScreen( )` before the bar, is not
seen, and any drawing call that covers the screen is a clear: the bench's look at the hold (the caption steady, only the bar growing) is the
check for that. This script holds the two expressions and what they guard.

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
HOLD_RE = re.compile(r'if\s*\(\s*state\.apProgressPct\s*>=\s*0\s*\)\s*\{')
GUARD_RE = re.compile(r'if\s*\(\s*enterAp\s*\)\s*\{')
COMMENT_RE = re.compile(r'/\*.*?\*/|//[^\n]*', re.S)


def cut(text, rx, name):
    found = rx.findall(text)
    if len(found) != 1:
        print(f'ERROR: {name} found {len(found)} times in {RENDER.relative_to(ROOT)} (expected exactly once)')
        sys.exit(2)
    return ' '.join(found[0].split())


def braced(text, open_at):
    """The text between the brace at `open_at` and the brace that closes it (None when it never closes)."""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[open_at + 1:i]
    return None


def cut_hold(text):
    """The hold's block, comments taken out (the comment beside it quotes code), or exit 2 when it is not found exactly once."""
    code = COMMENT_RE.sub(' ', text)
    found = list(HOLD_RE.finditer(code))
    body = braced(code, found[0].end() - 1) if len(found) == 1 else None
    if body is None:
        print(f'ERROR: the hold block `if (state.apProgressPct >= 0) {{` found {len(found)} times in {RENDER.relative_to(ROOT)}'
              f' (expected exactly once, with its closing brace)')
        sys.exit(2)
    return body


def body_checks(body):
    """The failures of 4, as text."""
    bad = []
    clears = body.count('fastClearScreen(')
    if clears != 1:
        bad.append(f'4: the hold block clears the screen at {clears} places (expected 1, under `if (enterAp) {{`)')
    guard = GUARD_RE.search(body)
    inner = braced(body, guard.end() - 1) if guard else None
    if inner is None:
        bad.append('4: no `if (enterAp) {` block in the hold: nothing says the clear is only for entering the hold')
        return bad
    if not re.match(r'\s*fastClearScreen\(', inner):
        bad.append('4: the first statement under `if (enterAp) {` is not the clear')
    after = body[guard.end() + len(inner) + 1:]
    if 'fillRoundRect(' not in after:
        bad.append('4: the bar is not painted after the `if (enterAp)` block (it must grow on every frame)')
    ret = re.search(r'return\s*;\s*$', body)
    if not ret:
        bad.append('4: the hold block does not end in `return;`: a hold frame falls through to the boot terminal, cleared at every step')
    between = body[guard.end() + len(inner) + 1:ret.start() if ret else len(body)]
    if not re.search(r'\b_lastRenderedState\s*=\s*state\s*;', between):
        bad.append('4: the hold block does not record the frame (`_lastRenderedState = state;`) after the `if (enterAp)` block and before `return;`: '
                   'the previous state would stay the boot terminal\'s and `enterAp` would be true at every step')
    return bad


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


def checks(full_expr, enter_expr, body):
    """The failures of 1-4, as text (empty when all hold)."""
    bad = body_checks(body)
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
    body = cut_hold(text)
    print(f'fullRedraw = {full_expr}')
    print(f'enterAp    = {enter_expr}')
    bad = checks(full_expr, enter_expr, body)
    for b in bad:
        print(f'FAIL {b}')
    if bad:
        print(f'test_ap_progress: {len(bad)} failure(s); the mutants are not run against a red base')
        return 1
    print('PASS 1-3: one clear over the hold, again on a language and on a first frame, a full redraw on a cancel')
    print('PASS 4: the hold block clears once, under `if (enterAp) {`, paints the bar on every frame, records the frame and ends in `return;`')
    mutants = {
        'M1 enterAp = fullRedraw || prev < 0 (the defect)': (full_expr, 'fullRedraw || (_lastRenderedState.apProgressPct < 0)', body),
        'M2 without langJustChanged': (full_expr, re.sub(r'\|\|\s*langJustChanged', '', enter_expr), body),
        'M3 without the not-booting term': (full_expr, re.sub(r'!_lastRenderedState\.isBooting\s*\|\|', '', enter_expr), body),
        'M4 fullRedraw without the percentage term': (re.sub(r'\|\|\s*\(_lastRenderedState\.apProgressPct != state\.apProgressPct\)', '',
                                                               full_expr), enter_expr, body),
        'M5 the clear guarded by fullRedraw': (full_expr, enter_expr, GUARD_RE.sub('if (fullRedraw) {', body, count=1)),
        'M6 the clear guarded by true': (full_expr, enter_expr, GUARD_RE.sub('if (true) {', body, count=1)),
        'M7 a second clear before the bar': (full_expr, enter_expr, re.sub(r'(\bint\s+wBar\b)', r'fastClearScreen(C_BG_MAIN); \1', body,
                                                                               count=1)),
        'M8 the block without its return': (full_expr, enter_expr, re.sub(r'return\s*;(\s*)$', r'\1', body, count=1)),
        'M9 the bar never painted': (full_expr, enter_expr, re.sub(r'_driver\.tft->fillRoundRect\([^;]*\);', '', body, count=1)),
        'M10 the frame never recorded': (full_expr, enter_expr, re.sub(r'_lastRenderedState\s*=\s*state\s*;', '', body, count=1)),
    }
    survived = 0
    for name, (f, e, bd) in mutants.items():
        if (f, e, bd) == (full_expr, enter_expr, body):
            print(f'FAIL {name}: the mutation did not change the cut text (its anchor moved)')
            survived += 1
            continue
        killed = bool(checks(f, e, bd))
        print(f'{"PASS" if killed else "FAIL"} {name}: {"killed" if killed else "SURVIVED"}')
        survived += 0 if killed else 1
    if survived:
        print(f'test_ap_progress: {survived} mutant(s) not killed')
        return 1
    print(f'test_ap_progress: all hold, {len(mutants)} mutants killed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
