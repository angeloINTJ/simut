#!/usr/bin/env python3
"""Regression test for the graph reader's handling of out-of-order blocks.

The browser assembles each series by walking the .h5 files in name order and
the blocks inside them in file order. File order is WRITE order, not time
order: a boot whose clock was seeded wrong writes blocks stamped ahead of the
ones that follow them. On the night of 2026-08-14 one file ended up holding

    20:33 -> 02:15(+1d) -> 03:16(+1d) -> 02:16(+1d) -> 00:55(+1d) -> 23:29

and the guard in place at the time, `t <= last`, treated every block after the
high-water mark as a duplicate and dropped it — including the 31 records from
23:29 to 23:59 whose timestamps were correct all along. The hole the user saw
in the chart was therefore part mis-stamped data and part data the reader had
thrown away.

This test extracts the normalisation loop from WebUI.h — the shipped source,
not a copy of it — and runs it under node against the real timestamps decoded
from /history/20260814.h5, asserting that:

  · no in-window record is lost, whatever order it arrives in;
  · exact duplicates are still removed, which is the job the old guard did
    correctly (a seal landing mid-load puts one record in the file AND in the
    RAM tail);
  · the output is sorted, so the gap detection and "last value" downstream of
    it keep working.

Run: python3 tools/test_webui_graph_order.py
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
MARKER = "/* Normalizacao: ordena por instante"


def extract_normaliser() -> str:
    """Pull the normalisation loop out of WebUI.h by brace matching."""
    src = WEBUI.read_text(encoding="utf-8", errors="replace")
    at = src.find(MARKER)
    if at < 0:
        raise SystemExit(
            f"FAIL: marker not found in {WEBUI.name}. If the normalisation "
            f"block was renamed or removed, this test must be updated with it "
            f"— do not delete the test to make it pass."
        )
    # Start just past the comment, so the recsInWin reset that precedes the
    # loop is part of what gets exercised.
    comment_end = src.find("*/", at)
    if comment_end < 0:
        raise SystemExit("FAIL: normalisation comment is unterminated")
    start = comment_end + 2
    loop = src.find("for (const s of series.values())", start)
    if loop < 0:
        raise SystemExit("FAIL: normalisation loop not found after its comment")

    depth, i, opened = 0, loop, False
    while i < len(src):
        if src[i] == "{":
            depth += 1
            opened = True
        elif src[i] == "}":
            depth -= 1
            if opened and depth == 0:
                return src[start:i + 1]
        i += 1
    raise SystemExit("FAIL: unbalanced braces while extracting the loop")


def run_case(body: str, points):
    """Feed one series through the extracted loop; return the result."""
    # recsInWin is declared by the surrounding function in WebUI.h; the loop
    # resets and recomputes it, so the harness has to supply it.
    js = f"""
let recsInWin = -1;
const series = new Map();
series.set('t', {{ ts: {json.dumps([p[0] for p in points])},
                  vs: {json.dumps([p[1] for p in points])} }});
{body}
const s = series.get('t');
console.log(JSON.stringify({{ ts: s.ts, vs: s.vs, recs: recsInWin }}));
"""
    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        raise SystemExit("SKIP: node not installed; cannot exercise the reader")
    r = subprocess.run([node, "-e", js], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"FAIL: node rejected the extracted loop:\n{r.stderr}")
    return json.loads(r.stdout)


def old_guard(points):
    """What the reader did before the fix, for the contrast in the report."""
    out, last = [], float("-inf")
    for t, v in points:
        if t <= last:
            continue
        out.append((t, v))
        last = t
    return out


def check(label, condition, detail=""):
    print(f"  {'ok  ' if condition else 'FAIL'} {label}{detail}")
    return condition


def main() -> int:
    body = extract_normaliser()
    print(f"normalisation loop extracted from {WEBUI.name} "
          f"({len(body.splitlines())} lines)\n")
    ok = True

    # ---- 1. the real block layout of 2026-08-14 -------------------------
    # t0 and record count of every block in /history/20260814.h5, in file
    # order, as read back from the device on 2026-08-15.
    blocks = [
        (1786750398, 60),   # 20:33:18        last block with an honest clock
        (1786770946, 60),   # 02:15:46 (+1d)  stamped 4 h 43 min ahead
        (1786774568, 20),   # 03:16:08 (+1d)  high-water mark of the night
        (1786770962, 4),    # 02:16:02 (+1d)  out of order from here on
        (1786766135, 30),   # 00:55:35 (+1d)
        (1786760983, 31),   # 23:29:43        correct, and was being dropped
    ]
    points = []
    for t0, n in blocks:
        for i in range(n):
            points.append((t0 + i * 60, 20.0 + i * 0.01))

    got = run_case(body, points)
    dropped = old_guard(points)
    print(f"1) real file layout: {len(points)} records in {len(blocks)} blocks")
    print(f"   old guard kept {len(dropped)}, lost {len(points) - len(dropped)}")
    ok &= check("every record survives",
                len(got["ts"]) == len(points),
                f" ({len(got['ts'])}/{len(points)})")
    ok &= check("output is sorted",
                all(a < b for a, b in zip(got["ts"], got["ts"][1:])))
    ok &= check("the 23:29 block is present",
                1786760983 in got["ts"])
    ok &= check("the old guard really did lose data (test is meaningful)",
                len(dropped) < len(points),
                f" — {len(points) - len(dropped)} records")

    # ---- 2. duplicates still go, which is what the guard got right ------
    dup = [(1000, 1.0), (1060, 2.0), (1060, 2.0), (1120, 3.0), (1000, 1.0)]
    got = run_case(body, dup)
    print("\n2) duplicate instants (seal landing mid-load)")
    ok &= check("collapsed to the distinct instants",
                got["ts"] == [1000, 1060, 1120], f" -> {got['ts']}")
    ok &= check("the reported record count excludes them",
                got["recs"] == 3, f" -> {got['recs']} of {len(dup)} pushed")

    # ---- 3. degenerate shapes ------------------------------------------
    print("\n3) degenerate inputs")
    got = run_case(body, [])
    ok &= check("empty series survives", got["ts"] == [])
    got = run_case(body, [(5, 1.0)])
    ok &= check("single point survives", got["ts"] == [5])
    got = run_case(body, [(9, 9.0), (9, 9.0)])
    ok &= check("two identical points collapse to one", got["ts"] == [9])

    # ---- 4. values ride along with their timestamps ---------------------
    print("\n4) value/timestamp pairing")
    got = run_case(body, [(300, 30.0), (100, 10.0), (200, 20.0)])
    ok &= check("reordering keeps each value with its instant",
                got["ts"] == [100, 200, 300] and got["vs"] == [10.0, 20.0, 30.0],
                f" -> {got['ts']} / {got['vs']}")

    print("\nPASS" if ok else "\nFAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
