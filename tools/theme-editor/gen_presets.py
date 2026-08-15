#!/usr/bin/env python3
"""Regenerates presets.js from src/Themes.cpp — the single source of truth.

Parses the palette table (THM_STATE_* macro-aware), prepends the diagnostic
theme (24 unique colors used by the editor's screenshot region-mapper), and
rewrites presets.js in place. Run after ANY palette or field change:

    python3 tools/theme-editor/gen_presets.py

The field list here must match ThemePalette (Themes.h) and FIELDS (app.js).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT / "src" / "Themes.cpp"
OUT = Path(__file__).resolve().parent / "presets.js"

FIELDS = ["bgMain","cardBg","textMain","textSub","textOff","accent","accentHigh","barBg",
          "tempHot","tempWarm","tempOk","tempCold","humidity",
          "btnText","titleText","sensorName","btnTextActive",
          "alarmBg","alarmText","alarmTextDim","alarmBorder","cautionBg","selBg","stampText"]

# Order swapped vs Themes.cpp historical (btnTextActive after sensorName here,
# matching the struct); presets consumers key by name, so order only matters
# for the parse below.

# Diagnostic theme: one UNIQUE color per field (post-RGB565 round-trip) so the
# editor can map screenshot pixels back to fields. State colors only show up
# in captures taken during an alarm/selection/graph-detail screen.
DIAG = {
    "bgMain": (0,0,0), "cardBg": (32,32,32), "textMain": (255,255,255),
    "textSub": (192,192,192), "textOff": (96,96,96), "accent": (0,255,255),
    "accentHigh": (255,255,0), "barBg": (48,48,48), "tempHot": (255,0,0),
    "tempWarm": (255,136,0), "tempOk": (0,255,0), "tempCold": (0,0,255),
    "humidity": (255,0,255), "btnText": (0,255,128), "titleText": (255,128,255),
    "sensorName": (128,255,0), "btnTextActive": (80,0,0),
    "alarmBg": (128,0,64), "alarmText": (0,128,255), "alarmTextDim": (128,128,0),
    "alarmBorder": (255,64,0), "cautionBg": (0,64,128), "selBg": (64,0,128),
    "stampText": (192,255,192),
}

src = CPP.read_text()

macros = {}
for name in ("THM_STATE_DARK", "THM_STATE_LIGHT"):
    m = re.search(r"#define " + name + r" \\\n(.*)", src)
    macros[name] = m.group(1).strip()

themes = []
for m in re.finditer(r'\{\s*"([a-z0-9_]+)",\s*"([^"]+)",\s*(.*?)\}\s*,', src, re.S):
    tid, tname, body = m.groups()
    for k, v in macros.items():
        body = body.replace(k, v)
    cols = re.findall(r"RGB565\((\d+),\s*(\d+),\s*(\d+)\)", body)
    if len(cols) != len(FIELDS):
        raise SystemExit(f"{tid}: {len(cols)} colors, expected {len(FIELDS)} — "
                         "Themes.cpp field set changed? Update FIELDS here + app.js.")
    themes.append((tid, tname, {f: tuple(map(int, c)) for f, c in zip(FIELDS, cols)}))

def js_colors(pal):
    return "{" + ",".join(f"{f}:[{r},{g},{b}]" for f, (r, g, b) in pal.items()) + "}"

lines = [
    f"// Auto-gerado de src/Themes.cpp por gen_presets.py ({len(FIELDS)} cores). NAO editar na mao.",
    "",
    "window.PRESET_THEMES = [",
    f'  {{ id: "diagnostic", name: "\\ud83d\\udd2c Diagnostic ({len(FIELDS)} cores \\u00fanicas)", '
    f"colors: {js_colors(DIAG)} }},",
]
for tid, tname, pal in themes:
    safe = tname.replace("\\", "\\\\").replace('"', '\\"')
    lines.append(f'  {{ id: "{tid}", name: "{safe}", colors: {js_colors(pal)} }},')
lines += ["];", ""]

OUT.write_text("\n".join(lines))
print(f"presets.js: {len(themes)} temas + diagnostic, {len(FIELDS)} cores cada")
