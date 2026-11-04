#!/usr/bin/env python3
"""keypad_answer.py -- the one reader of GET /api/keypad for the two bench tools that drive the ORIGINAL panel
(tools/panel_users_hw_test.py and tools/panel_fulltable_test.py). Tested by tools/test_keypad_answer.py.

The route (WebManager::handleApiKeypad( ), src/WebManager_History.cpp) answers in one of two shapes, chosen by the image's UI driver:

  the original panel     {"faces":[...],"up":true,"kb":"cards","grid":[keys,slots,cols,rows,x,y,w,h,pitchX,pitchY],"policy":[min,max,keypad,alphabet]}
                         and, for the groups keyboard only, ,"pop":[keyW,keyH,gap,rowY(one row),rowY(row 0),rowY(row 1)]
  the Angulo             {"faces":[...],"up":true,"kb":"cards","policy":[min,max,keypad,alphabet],"targets":"/api/ui"}
                         (an image built with ui = "angulo": pico2_w_release and pico_wiretft_angulo)

The Angulo's answer has no grid and no pop because the original panel's cannot describe its keys: with four cards the original's are 152 x 44
at (6, 72) with a pitch of 156 and 50, the Angulo's 146 x 44 at (12, 96) with a pitch of 150 and 48 (src/PinKeypad.h GRIDS against
src/ui/angulo/telas/IdentidadePin.cpp, kGrades), and the Angulo's popup keys are 48 px wide and 52 tall on one row, 36 on two, where the
original's are 56 x 52. A client that taps by the original's numbers misses the Angulo's keys. `faces`, `up`, `kb` and `policy` are the
core's own state and mean the same on both: the Angulo's key k is PinKb's key k. What differs is where to tap, and the Angulo says it:
`targets` names the route that lists the touch targets of the frame on the glass (tools/tft_ui/roteiro_ui.py taps by it).

parse( answer ) takes the decoded JSON and returns a Keypad. An answer with `targets` is refused with AnguloAnswer: the two tools above tap the
original panel by `grid` and `pop`, and an Angulo has neither, so the run would fall back to the v24 layout below and tap in the wrong places
with nothing failing in a way that pointed here. The refusal comes from `targets` and not from a missing `grid`: an answer without a grid is
also what a device older than v25 sends (the geometry went on the wire in v25), and for that one the tools have always used the v24 cards.
"""
from collections import namedtuple

GRID_FIELDS = ('keys', 'slots', 'cols', 'rows', 'x', 'y', 'w', 'h', 'px', 'py')   # v25: grid = keys, slots, cols, rows, x, y, w, h, pitchX, pitchY
POP_FIELDS = ('w', 'h', 'gap', 'one_y', 'row0_y', 'row1_y')                        # pop = keyW, keyH, gap, rowY(1 row), rowY(row 0), rowY(row 1)
POLICY_FIELDS = ('min', 'max', 'keypad', 'alphabet')                               # policy = minLen, maxLen, keypad, alphabet

# PIN keypad (src/PinKeypad.h): four cards of three slots, the ten digits and
# two decoy symbols dealt over them at random. The script cannot guess where a
# digit is -- `show display keypad` is what tells it. One tap per digit.
# v25: the card layout is a POLICY setting (PinKeypad.h::GRIDS) -- four cards of
# three glyphs is only one of five, and ALPHA_ALNUM puts twelve or eighteen on
# the glass. These are the v24 numbers and they survive as the fallback for a
# device that predates the geometry in /api/keypad; Rig.keypad_faces( ) reads
# the live grid and Rig.pin( ) taps what it says. Hardcoding them was the trap
# this file was about to walk into: the cards would have moved out from under
# every tap with nothing failing in a way that pointed here.
# (Moved here from tools/panel_users_hw_test.py, which imports it again, so that the parser and
# the tools have one copy.)
PIN_GRID_FALLBACK = dict(keys=4, slots=3, cols=2, rows=2,
                         x=6, y=72, w=152, h=44, px=156, py=50)

Keypad = namedtuple('Keypad', 'faces up kb grid pop policy')
Keypad.__doc__ = """One answer of the route, read.
faces   one string per key, POSITIONAL (the ordered numeric pad has two empty ones; never filter them)
up      the keypad is on the glass
kb      'cards' (dealt, one tap a card), 'num' (ordered, one tap a key) or 'groups' (ordered, two taps: the group, then the popup's key)
grid    dict keys, slots, cols, rows, x, y, w, h, px, py: the answer's, or a copy of PIN_GRID_FALLBACK when it has none (a device older than v25)
pop     dict w, h, gap, one_y, row0_y, row1_y; None unless the answer carries one (the groups keyboard)
policy  dict min, max, keypad, alphabet; None when the answer has none"""


class AnguloAnswer(RuntimeError):
    """The answer is the Angulo's: it has no geometry for a tool that taps the original panel. Not a KeypadGone: reopening the keypad and
    reading again gives the same answer, so a caller must stop and not retry."""


def _named(fields, values):
    """dict(zip(fields, values)) when `values` is a list of exactly len(fields) items, else None: an answer cut short or from another
    firmware is read as absent and not half-read (the shape check the tools had inline)."""
    if isinstance(values, list) and len(values) == len(fields):
        return dict(zip(fields, values))
    return None


def _refusal(route):
    return (f'this device draws the Ângulo panel: its GET /api/keypad sends the faces and the PIN policy, and "targets": "{route}" instead of '
            f'the grid and the popup geometry, which describe the original panel and would miss the Ângulo\'s keys. This tool taps the '
            f'original panel by that geometry and cannot drive the Ângulo. Run it against a device with the original panel, or tap by the '
            f'targets of {route} as tools/tft_ui/roteiro_ui.py does.')


def parse(answer):
    """The decoded JSON of GET /api/keypad as a Keypad, or AnguloAnswer when it is the Ângulo's."""
    if 'targets' in answer:
        route = answer['targets']
        raise AnguloAnswer(_refusal(route if isinstance(route, str) and route else '/api/ui'))
    grid = _named(GRID_FIELDS, answer.get('grid'))
    if grid is None:
        grid = dict(PIN_GRID_FALLBACK)
    pop = _named(POP_FIELDS, answer.get('pop'))
    policy = _named(POLICY_FIELDS, answer.get('policy'))
    return Keypad(faces=list(answer.get('faces') or []), up=bool(answer.get('up')),
                  kb=answer.get('kb') or 'cards', grid=grid, pop=pop, policy=policy)
