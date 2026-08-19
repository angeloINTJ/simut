#!/usr/bin/env python3
"""Generate the SIMUT logo (mark + wordmark) as self-contained SVG.

The mark is not invented: it is the icon that already ships as data/favicon.ico,
re-drawn as vector. Geometry and colours were measured off the 48px frame of the
original icon (centre 23.5,23.5; ring at r=21.5; S box 22x30 px; #00DCFF on
#0A172F with a #7ADFFF ring) and scaled to a 128-unit viewBox.

The letterforms are DejaVu Sans Bold outlines, converted to paths so the file
needs no font installed. DejaVu won on measured overlap against the original
icon's S -- 91.2% IoU, versus 85.3% for the runner-up -- and its licence
(Bitstream Vera derived) permits redistribution of derived work.

This is a design-time tool, not part of the firmware build: nothing in
platformio.ini calls it, and the generated SVGs are committed. Run it only to
regenerate them.

    pip install fonttools          # not a project dependency
    python3 tools/gen_logo.py      # writes into the current directory

Project: SIMUT
License: MIT
"""
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.boundsPen import BoundsPen

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
NAVY, RING, CYAN, EDGE = '#0A172F', '#7ADFFF', '#00DCFF', '#060F22'
WORD = '#1a73e8'   # holds >=4:1 on both GitHub themes; see the note in the wordmark below

_f = TTFont(FONT)
_gs = _f.getGlyphSet()
_upm = _f['head'].unitsPerEm
_cmap = _f.getBestCmap()

def glyph(ch):
    g = _cmap[ord(ch)]
    bp = BoundsPen(_gs); _gs[g].draw(bp)
    pen = SVGPathPen(_gs); _gs[g].draw(pen)
    return pen.getCommands(), bp.bounds, _gs[g].width

def fitted_S(box_w, box_h, cx, cy):
    """Place the S outline centred on (cx,cy) inside a box_w x box_h box."""
    d, (x0, y0, x1, y1), _ = glyph('S')
    sx, sy = box_w / (x1 - x0), box_h / (y1 - y0)
    tx = cx - box_w / 2 - x0 * sx
    ty = cy + box_h / 2 + y0 * sy
    return f'<g transform="translate({tx:.3f} {ty:.3f}) scale({sx:.6f} {-sy:.6f})"><path d="{d}"/></g>'

def mark_group(cx, cy, scale):
    """Disc + ring + S, all measured from the shipped icon and scaled."""
    return (f'<circle cx="{cx}" cy="{cy}" r="{23.2*scale:.3f}" fill="{EDGE}"/>'
            f'<circle cx="{cx}" cy="{cy}" r="{21.5*scale:.3f}" fill="{NAVY}" '
            f'stroke="{RING}" stroke-width="{1.3*scale:.3f}"/>'
            f'<g fill="{CYAN}">{fitted_S(22*scale, 30*scale, cx, cy)}</g>')

# ---- mark, 128x128 -------------------------------------------------------
S = 128 / 48
open('logo-mark.svg', 'w').write(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" width="128" height="128" '
    'role="img" aria-label="SIMUT">'
    '<title>SIMUT</title>' + mark_group(64, 64, S) + '</svg>')

# ---- wordmark ------------------------------------------------------------
# Text as outlines, laid out on the font's own advance widths so the spacing is
# the typeface's, not guesswork.
CAP = _f['OS/2'].sCapHeight if hasattr(_f['OS/2'], 'sCapHeight') else 1493
TEXT, TSIZE = 'SIMUT', 44.0          # cap height in viewBox units
k = TSIZE / CAP
pen_x, parts = 0.0, []
for ch in TEXT:
    d, _, adv = glyph(ch)
    parts.append(f'<g transform="translate({pen_x:.3f} 0) scale({k:.6f} {-k:.6f})"><path d="{d}"/></g>')
    pen_x += adv * k
text_w = pen_x
MH = 96.0                     # mark height in the wordmark
ms  = MH / 48
GAP = 22.0
W = MH + GAP + text_w + 8
H = MH + 8
baseline = H / 2 + TSIZE / 2

open('logo-wordmark.svg', 'w').write(
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.1f} {H:.1f}" '
    f'width="{W:.0f}" height="{H:.0f}" role="img" aria-label="SIMUT">'
    '<title>SIMUT</title>'
    # The wordmark text sits directly on the page, so it has to hold contrast on
    # BOTH GitHub themes. A prefers-color-scheme switch was rejected: inside an
    # <img>, that media query follows the reader's OS theme, not the theme they
    # picked on GitHub, so the two disagree and the text can land white-on-white.
    # #1a73e8 needs no switch -- measured 4.51:1 on #ffffff and 4.20:1 on
    # #0d1117 -- and it is the blue the project's badge already uses.
    + mark_group(4 + MH/2, H/2, ms)
    + f'<g fill="{WORD}" transform="translate({4+MH+GAP:.3f} {baseline:.3f})">{"".join(parts)}</g>'
    + '</svg>')

for f in ('logo-mark.svg', 'logo-wordmark.svg'):
    import os
    print(f'{f:<22} {os.path.getsize(f):>6} B')
