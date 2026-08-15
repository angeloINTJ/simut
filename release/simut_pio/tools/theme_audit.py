#!/usr/bin/env python3
"""Coherence audit for SIMUT themes.

Parses Themes.cpp (macro-aware), converts every color through the real
RGB565 round-trip (what the panel shows), and checks WCAG-style contrast
on the exact fg/bg pairs the render code draws. Pairs and minimums are
derived from the audited draw calls, not from generic guidelines:

  small text (9/12pt)  min 4.5   values/large (24pt) min 3.0
  muted-but-readable   min 2.5   (textSub as secondary label)
  disabled/axis        min 1.6   (textOff: deliberately faint)
  decorative border    min 1.3   (panel borders, bar tracks)

Usage: theme_audit.py [--json out.json]
"""
import re, sys, json

from pathlib import Path as _P
PATH = str(_P(__file__).resolve().parents[1] / "src" / "Themes.cpp")

FIELDS = ["bgMain","cardBg","textMain","textSub","textOff","accent","accentHigh","barBg",
          "tempHot","tempWarm","tempOk","tempCold","humidity",
          "btnText","titleText","sensorName","btnTextActive",
          "alarmBg","alarmText","alarmTextDim","alarmBorder","cautionBg","selBg","stampText"]

def rgb565_roundtrip(r, g, b):
    """RGB888 -> RGB565 -> RGB888 (as the panel displays it)."""
    c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    r5, g6, b5 = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))

def luminance(rgb):
    def ch(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = rgb
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)

def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    if la < lb: la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)

src = open(PATH).read()

# Expand the two state macros
m = re.search(r"#define THM_STATE_DARK \\\n(.*)", src)
DARK = m.group(1).strip()
m = re.search(r"#define THM_STATE_LIGHT \\\n(.*)", src)
LIGHT = m.group(1).strip()

themes = []
# Join palette blocks: name line + color lines until `},`
blocks = re.findall(r'\{\s*"([a-z0-9_]+)",\s*"([^"]+)",\s*(.*?)\}\s*,', src, re.S)
for tid, tname, body in blocks:
    body = body.replace("THM_STATE_DARK", DARK).replace("THM_STATE_LIGHT", LIGHT)
    cols = re.findall(r"RGB565\((\d+),\s*(\d+),\s*(\d+)\)", body)
    if len(cols) != len(FIELDS):
        print(f"!! {tid}: {len(cols)} colors (expected {len(FIELDS)})")
        continue
    pal = {f: rgb565_roundtrip(*map(int, c)) for f, c in zip(FIELDS, cols)}
    themes.append((tid, tname, pal))

# (fg, bg, min_ratio, where-it-draws)
PAIRS = [
    ("textMain",     "bgMain",     4.5, "labels/messages on screen bg"),
    ("textMain",     "cardBg",     4.5, "button text (uiButton secondary), values"),
    ("titleText",    "cardBg",     4.5, "title bar text (uiTitleBar)"),
    ("sensorName",   "cardBg",     4.5, "sensor name on slot panel"),
    ("btnText",      "cardBg",     4.5, "S0..S9/CFG button labels"),
    ("btnTextActive","accentHigh", 4.5, "selected slot button label"),
    ("bgMain",       "accent",     3.0, "PRIMARY button: bgMain text on accent fill"),
    ("textSub",      "cardBg",     2.5, "secondary text on cards"),
    ("textSub",      "bgMain",     2.5, "secondary text on screen bg"),
    ("tempOk",       "cardBg",     3.0, "value 24pt on panel"),
    ("tempHot",      "cardBg",     3.0, "hot value / error label on panel"),
    ("tempCold",     "cardBg",     3.0, "cold value on panel"),
    ("tempWarm",     "cardBg",     3.0, "warm value on panel"),
    ("humidity",     "cardBg",     3.0, "humidity value on panel"),
    ("tempOk",       "bgMain",     3.0, "graph line/wifi bars on bg"),
    ("textOff",      "bgMain",     1.6, "disabled text / graph axis"),
    ("stampText",    "bgMain",     3.0, "graph detail date/time stamps (9pt)"),
    ("accentHigh",   "bgMain",     1.6, "panel border on bg"),
    ("accent",       "bgMain",     1.6, "accent tab on title bar edge"),
    ("alarmText",    "alarmBg",    4.5, "alarm header/button text"),
    ("alarmTextDim", "alarmBg",    3.0, "alarm secondary text"),
    ("alarmText",    "cautionBg",  4.5, "silence button text"),
    ("alarmText",    "selBg",      4.5, "selection-mode panel text"),
    ("alarmTextDim", "selBg",      3.0, "selection-mode secondary text"),
    ("alarmBorder",  "bgMain",     1.6, "alarm border on bg"),
    ("alarmBg",      "cardBg",     1.3, "alarm flash phase vs card bg (must read as a change)"),
]

report = {}
fails = 0
for tid, tname, pal in themes:
    tfails = []
    for fg, bg, minr, where in PAIRS:
        r = contrast(pal[fg], pal[bg])
        if r < minr:
            tfails.append((fg, bg, r, minr, where))
            fails += 1
    report[tid] = {"name": tname, "fails": tfails, "pal": pal}
    if tfails:
        print(f"\n── {tid} ({tname})")
        for fg, bg, r, minr, where in tfails:
            print(f"   {fg:14s} on {bg:11s} {r:4.2f} < {minr:.1f}  [{where}]  fg={pal[fg]} bg={pal[bg]}")

print(f"\n{len(themes)} themes, {fails} failing pairs, "
      f"{sum(1 for t in report.values() if t['fails'])} themes with failures")

if "--json" in sys.argv:
    out = sys.argv[sys.argv.index("--json") + 1]
    json.dump({k: {"name": v["name"],
                   "fails": [list(f) for f in v["fails"]],
                   "pal": {f: list(c) for f, c in v["pal"].items()}}
               for k, v in report.items()}, open(out, "w"), indent=1)
    print(f"json -> {out}")
