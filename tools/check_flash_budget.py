#!/usr/bin/env python3
"""Fails the build when a firmware image grows past its budget in tools/flash_budget.json.

WHY THIS EXISTS
---------------
The linker already refuses an image that does not fit. What it cannot do is
make growth visible while there is still room, and that is where this project
gets hurt: the SIMUT Air image has the least headroom of the five that link,
CI compiled only pico_w_release until 2026-09-08, and a change that ate several
kilobytes of the Air slot would have gone in green. This gate turns "it still
fits" into "it still fits, and here is what it cost".

WHAT IT MEASURES
----------------
The number PlatformIO prints, parsed out of a captured build log:

    Flash: [==========]  97.6% (used 1019276 bytes from 1044480 bytes)

Deliberately NOT a size recomputed here from the ELF. The measurement can be
reproduced from sections (.boot2 + .text + .rodata + .data comes out to exactly
the same figure today), but a gate that reimplements the toolchain's arithmetic
is a gate that can drift away from what the developer sees in their terminal
and then argue with them about it. Parsing the real line means the gate and the
build can never disagree; if the format ever changes, this fails loudly with
"no Flash: line" rather than quietly measuring the wrong thing.

USAGE
-----
    pio run -e pico_w_air 2>&1 | tee build.log
    python3 tools/check_flash_budget.py pico_w_air build.log

    # or read the log on stdin
    pio run -e pico_w_air 2>&1 | python3 tools/check_flash_budget.py pico_w_air -

Exit status is 0 when the image is within budget, 1 when it is over or when the
log carries no size line to check.

Project: SIMUT
License: MIT
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGET_FILE = os.path.join(ROOT, "tools", "flash_budget.json")

# The whole contract with PlatformIO, in one place.
FLASH_RE = re.compile(r"used\s+(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")


def fail(msg):
    print(f"[flash-budget] FAIL {msg}")
    sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().split("USAGE\n-----\n", 1)[1])
        sys.exit(2)

    env, log_path = sys.argv[1], sys.argv[2]

    with open(BUDGET_FILE, encoding="utf-8") as fh:
        cfg = json.load(fh)

    text = sys.stdin.read() if log_path == "-" else \
        open(log_path, encoding="utf-8", errors="replace").read()

    # An environment with no budget is not silently waved through: it is either
    # known-unbudgeted (and named as such, with a reason) or the budget file has
    # fallen behind the environment list in platformio.ini.
    if env not in cfg["envs"]:
        why = cfg.get("unbudgeted", {}).get(env)
        if why:
            print(f"[flash-budget] SKIP {env}: no budget on purpose — {why}")
            return
        fail(f"{env} has no budget in tools/flash_budget.json. "
             f"Add one (measure with `pio run -e {env}`) or record it under "
             f"\"unbudgeted\" with the reason.")

    # The last match wins: a build log can carry sizes from more than one pass.
    matches = FLASH_RE.findall(text)
    if not matches:
        fail(f"{env}: no 'used N bytes from M bytes' line in {log_path}. "
             f"Either the build did not get as far as linking, or PlatformIO "
             f"changed the line this gate reads.")

    used, ceiling = (int(x) for x in matches[-1])
    budget = cfg["envs"][env]["budget"]
    declared_ceiling = cfg.get("ceiling")

    # The ceiling moving is worth a word even when the image fits: it means the
    # partition layout changed under us, and every budget below was set against
    # the old one.
    if declared_ceiling and ceiling != declared_ceiling:
        print(f"[flash-budget] NOTE {env}: slot is {ceiling} B, but "
              f"flash_budget.json records a ceiling of {declared_ceiling} B — "
              f"the layout moved, so re-measure the budgets.")

    delta = used - budget
    pct = 100.0 * used / ceiling

    if delta > 0:
        fail(f"{env}: {used} B used, budget is {budget} B — over by {delta} B "
             f"({pct:.1f}% of the {ceiling} B slot; {ceiling - used} B of real "
             f"headroom left).\n"
             f"           If the growth is intended, raise the budget in "
             f"tools/flash_budget.json in this same change and say why in the "
             f"commit message.")

    print(f"[flash-budget] OK {env}: {used} B used, {-delta} B under budget "
          f"({pct:.1f}% of slot, {ceiling - used} B to the ceiling)")


if __name__ == "__main__":
    main()
