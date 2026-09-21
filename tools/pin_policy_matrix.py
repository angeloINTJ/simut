#!/usr/bin/env python3
"""What each knob of the v25 PIN policy buys, and what it costs.

Three knobs, three axes, and they do not move together. This prints the whole
matrix so a policy can be chosen from numbers instead of from taste.

    alphabet C   digits (10) or 0-9A-Z (36)   -- security, free
    keypad   S   glyphs per card, 1..3        -- security AGAINST guessing,
                                                 paid for in shoulder-surfing
    length   n   minimum characters           -- security, paid in taps

The three quantities that follow from them:

    guess    (S/C)^n per attempt      -- a blind entry proving one account
    watcher  S^n candidates           -- what someone who saw ONE entry has left.
                                         At S=1 that is 1: the ordered keyboard
                                         prints the character on the key, so it
                                         hides nothing from anyone who can read
                                         the glass. It is the honest reading of
                                         a keypad that never pretended
                                         otherwise, not a regression.
    CPU      S + S^2 + ... + S^n      -- SHA-256 nodes, 36.6 us each on the
                                         RP2040, and the reason maxLenFor( )
                                         exists (PinKeypad.h)

Note the second and third columns are the SAME number in different clothes:
the set that hides a character from a watcher is the set a search has to walk.
That is why moving the keypad is zero-sum, and why the alphabet — which does
not appear in either — is the only knob that is not a trade.

Calibration: 8 taps of 3 glyphs is a 9,840-node tree measured at ~360 ms of
blocked Core 0 on the rig, 2026-09-19 (449 ms worst HTTP response against
91 ms idle). Everything here scales from that one measurement.

Usage:
    python3 tools/pin_policy_matrix.py            # the matrix
    python3 tools/pin_policy_matrix.py --v24      # what v25 changed, by number

Project: SIMUT
License: MIT
"""
import argparse
from fractions import Fraction

US_PER_NODE = 360_000 / 9840          # measured, see the header
MAX_USERS = 32                        # SystemDefs_Limits.h
TRIES = 6                             # PinKb::SLOT_FAIL_MAX

ALPHABETS = {0: ('0-9', 10), 1: ('0-9A-Z', 36)}
KEYPADS = {1: ('ordered', 16), 2: ('scrambled-2', 12), 3: ('scrambled-3', 8)}
# Every pair has a layout since 2026-09-20. One glyph per key is not dealt at
# all: it is the ORDERED keyboard, the numeric pad for digits and a two-tap
# group keyboard for the 36 — which is what made the second pair possible,
# since it is no longer 36 keys of one character at 19 px.
UNSUPPORTED = set()


def nodes(s, n):
    return n if s == 1 else (s * (s ** n - 1)) // (s - 1)


def fmt_one_in(p):
    """`p` is a Fraction, so 1/p is exact. As a float, (1/10)**16 printed as
    "1 in 9,999,999,999,999,992" — a tool that cannot say 10^16 is a tool
    nobody should quote."""
    return f"1 in {round(1 / p):,}" if p > 0 else "-"


def matrix():
    print("=" * 100)
    print("v25 PIN policy — what each combination is worth")
    print("=" * 100)
    print(f"{'alphabet':>9} {'keypad':>13} {'n':>3} | {'blind guess':>22} "
          f"{'in 6 tries':>11} | {'watcher':>9} {'6 tries':>8} | {'CPU':>10} {'taps':>5}")
    print("-" * 100)
    for a, (aname, C) in ALPHABETS.items():
        for kb, (kname, cap) in KEYPADS.items():
            if (a, kb) in UNSUPPORTED:
                print(f"{aname:>9} {kname:>13}  -  | {'no layout: 36 keys of one character is not a finger target':<55}")
                continue
            for n in sorted({4, 6, 8, cap}):
                if n > cap:
                    continue
                taps = n * (2 if (kb == 1 and a == 1) else 1)
                per = Fraction(kb, C) ** n
                six = 1 - (1 - per) ** TRIES
                cand = kb ** n                    # what one observation leaves
                wsix = min(1.0, TRIES / cand)
                ms = nodes(kb, n) * US_PER_NODE / 1000
                mark = " <-- v24" if (a, kb, n) == (0, 3, 4) else ""
                print(f"{aname:>9} {kname:>13} {n:>3} | {fmt_one_in(per):>22} "
                      f"{float(six)*100:>10.3f}% | {cand:>9,} {wsix*100:>7.1f}% | "
                      f"{ms:>8.0f} ms {taps:>5}{mark}")
        print()
    print("blind guess  P(one entry proves the chosen account); in 6 tries = before the")
    print("             account is locked out (PinKb::SLOT_FAIL_MAX)")
    print("watcher      candidates left to someone who saw ONE full entry, and their")
    print("             odds inside the same 6 tries")
    print("CPU          Core 0 blocked while the tap tree resolves — the web server")
    print("             stops behind it, so this is also worst-case HTTP latency")
    print("taps         screen taps to enter the PIN: one per character, except on")
    print("             the ordered alphanumeric keyboard, which costs two (group,")
    print("             then character)")


def v24_vs_v25():
    """The table-wide numbers: what identifying BY the PIN cost."""
    print("=" * 100)
    print(f"v24 -> v25, with the table full ({MAX_USERS} accounts, 4-digit PINs)")
    print("=" * 100)
    C, S, n = 10, 3, 4
    cand = S ** n
    N = 10 ** n
    # v24: an entry proved WHOEVER fell inside it, so an attacker fished the
    # whole table at once, and two accounts inside one entry refused both.
    import math
    def exactly_one(s, A, N):
        return math.comb(s, 1) * math.comb(N - s, A - 1) / math.comb(N, A)
    v24_attack = exactly_one(cand, MAX_USERS, N)
    v25_attack = (S / C) ** n
    # The legitimate user's own entry carries |S|-1 OTHER candidates, and |S|
    # is not 3^n: two of the twelve slots are decoys, so the card holding his
    # character has 3, 2 or 1 real ones (36/55, 18/55, 1/55 — size-biased,
    # because a fatter card is likelier to be the one holding it). Using 3^n
    # here overstates it by a third; the 3^n form is the CEILING, which is what
    # tools/panel_fulltable_test.py prints as "v24 would have seen".
    from itertools import product
    card = ((3, 36/55), (2, 18/55), (1, 1/55))
    v24_amb = 0.0
    for combo in product(card, repeat=n):
        size, pr = 1, 1.0
        for v, q in combo:
            size *= v; pr *= q
        v24_amb += pr * (1 - math.comb(N - 1 - (size - 1), MAX_USERS - 1)
                             / math.comb(N - 1, MAX_USERS - 1))
    rows = [
        ("blind entry proves SOMEBODY, per try", f"{v24_attack*100:.1f}%", f"{v25_attack*100:.2f}%",
         f"{v24_attack/v25_attack:.0f}x better"),
        ("...in 6 tries (before lockout)",
         f"{(1-(1-v24_attack)**TRIES)*100:.0f}%", f"{(1-(1-v25_attack)**TRIES)*100:.1f}%",
         f"{(1-(1-v24_attack)**TRIES)/(1-(1-v25_attack)**TRIES):.0f}x better"),
        ("honest login refused as ambiguous", f"{v24_amb*100:.1f}%", "0%",
         "measured 15% on the rig 19/09; gone by construction"),
        ("six wrong taps lock", "the PANEL, until reboot", "that ACCOUNT",
         "panel needs 20 (PANEL_FAIL_CEILING)"),
        ("attacker's budget across the table", f"6 tries total",
         f"6 per account, {TRIES*MAX_USERS} total",
         "which is why the panel ceiling stays"),
        ("longest PIN the keypad resolves", "8 (fixed)", "8 / 12 / 16 by keypad", "CPU-derived"),
        ("alphabet", "10 digits", "10 or 36",
         f"36 costs a guess {(36/3)**4/(10/3)**4:.0f}x more at n=4, and the CPU nothing"),
    ]
    print(f"{'':<38} {'v24':>26} {'v25':>26}")
    print("-" * 100)
    for label, a, b, note in rows:
        print(f"{label:<38} {a:>26} {b:>26}   {note}")


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--v24', action='store_true', help='what v25 changed, by number')
    args = ap.parse_args()
    if args.v24:
        v24_vs_v25()
    else:
        matrix()
