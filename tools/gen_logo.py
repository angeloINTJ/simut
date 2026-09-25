#!/usr/bin/env python3
"""Generate the SIMUT brand assets in the Ângulo standard (ANGULO.md).

ANGULO.md §6: "the brand is the name set in the display face, weight 600,
tracked -0.02em". So the name is the brand, and the mark is the one letter of it
that still reads at 16 px: the S of the same face, in acento-tinta on a disc of
acento -- the pair the standard gives a primary button, whose contrast its token
build already checks. The disc is raio-total, which §3.4 reserves for pills and
avatars; a site icon is the product's avatar.

No colour is typed in this file. Every value is read from docs/assets/angulo.css,
which is a byte-for-byte copy of simut-rx/web/angulo.css, the file the Ângulo
token build generates. Change a colour there, recopy, and run this again.

The letterforms are the display face the docs site and the manual already ship
(docs/assets/fonts/angulo-display-600.woff2, Bricolage Grotesque SemiBold, OFL
1.1), converted to outlines so no file here needs a font installed. The face has
one kerning pair inside "SIMUT" -- U-T, +1/1000 em, measured 2026-09-24 -- which
is under a pixel at every size drawn below, so kerning is not applied.

Outputs, all committed:

  docs/images/logo-mark.svg           the disc alone (claro)
  docs/images/logo-wordmark.svg       disc + name, claro   -- README, light mode
  docs/images/logo-wordmark-dark.svg  disc + name, escuro  -- README, dark mode
  docs/images/logo-name.svg           the name alone, currentColor
  docs/images/powered-by-simut.svg        badge, 40 px tall
  docs/images/powered-by-simut-large.svg  badge, 72 px tall
  docs/images/social-preview.png      1280x640, the og:image of the site
  docs/_includes/marca.html           disc + name inline, coloured by CSS classes
  data/favicon.ico                    16 + 32 px; the firmware serves this file

data/favicon.ico is compiled into the firmware image by
tools/build_favicon_header.py, byte for byte: a byte here is a byte of flash.
It is assembled by hand below, because Pillow's ICO writer re-encodes the frames
and undoes the palette reduction (the recipe that took it from 11,047 B to 835 B
on 2026-08-19).

This is a design-time tool, not part of the firmware build: nothing in
platformio.ini calls it. Run it from the repository root:

    pip install fonttools brotli pillow     # not project dependencies
    python3 tools/gen_logo.py

Project: SIMUT
License: MIT
"""
import io
import os
import re
import struct

from fontTools.pens.basePen import BasePen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKENS = os.path.join(ROOT, "docs", "assets", "angulo.css")
FACE = os.path.join(ROOT, "docs", "assets", "fonts", "angulo-display-600.woff2")
# The texto family is "the font of the device" (§3.2), which a PNG does not
# have. Liberation Sans stands in: metric-compatible with Arial, the last
# family in the texto stack, and present on the machine that renders this.
TEXTO = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
IMAGES = os.path.join(ROOT, "docs", "images")

NAME = "SIMUT"
TRACK = -0.02          # em, §6
S_IN_DISC = 0.60       # S height / disc diameter; 9.6 px of S in a 16 px icon


# ---- tokens -----------------------------------------------------------------
def read_tokens():
    """{'claro': {...}, 'escuro': {...}} straight out of the generated CSS."""
    css = open(TOKENS, encoding="utf-8").read()
    blocks = {
        "claro": re.search(r':root,\s*\[data-theme="claro"\]\s*\{(.*?)\}', css, re.S),
        "escuro": re.search(r'\[data-theme="escuro"\]\s*\{(.*?)\}', css, re.S),
    }
    out = {}
    for theme, m in blocks.items():
        if not m:
            raise SystemExit(f"{TOKENS}: no {theme} block -- is it still the generated file?")
        out[theme] = dict(re.findall(r"--([a-z0-9-]+):\s*(#[0-9a-fA-F]{6})\s*;", m.group(1)))
    return out


T = read_tokens()


# ---- the face -----------------------------------------------------------------
_face = TTFont(FACE)
_gs = _face.getGlyphSet()
_cmap = _face.getBestCmap()
UPM = _face["head"].unitsPerEm
CAP = _face["OS/2"].sCapHeight


def _gname(ch):
    return _cmap[ord(ch)]


def ink(ch):
    bp = BoundsPen(_gs)
    _gs[_gname(ch)].draw(bp)
    return bp.bounds


def advance(ch):
    return _gs[_gname(ch)].width


def _num(v):
    r = round(v)
    return str(int(r)) if abs(v - r) < 1e-6 or abs(v) >= 100 else f"{v:.1f}".rstrip("0").rstrip(".")


def path(ch, dx, dy, s):
    """SVG path of one glyph, flipped to y-down: x' = dx + s*x, y' = dy - s*y."""
    pen = SVGPathPen(_gs, ntos=_num)
    _gs[_gname(ch)].draw(TransformPen(pen, (s, 0, 0, -s, dx, dy)))
    return pen.getCommands()


class _Flatten(BasePen):
    """Contours as point lists, curves cut into short straight pieces."""

    def __init__(self, gs, steps=12):
        super().__init__(gs)
        self.contours, self._cur, self.steps = [], [], steps

    def _moveTo(self, p):
        self._cur = [p]

    def _lineTo(self, p):
        self._cur.append(p)

    def _qCurveToOne(self, p1, p2):
        p0 = self._getCurrentPoint()
        for i in range(1, self.steps + 1):
            t = i / self.steps
            a, b, c = (1 - t) ** 2, 2 * (1 - t) * t, t * t
            self._cur.append((a * p0[0] + b * p1[0] + c * p2[0], a * p0[1] + b * p1[1] + c * p2[1]))

    def _curveToOne(self, p1, p2, p3):
        p0 = self._getCurrentPoint()
        for i in range(1, self.steps + 1):
            t = i / self.steps
            a, b, c, d = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
            self._cur.append((a * p0[0] + b * p1[0] + c * p2[0] + d * p3[0],
                              a * p0[1] + b * p1[1] + c * p2[1] + d * p3[1]))

    def _closePath(self):
        if self._cur:
            self.contours.append(self._cur)
        self._cur = []

    _endPath = _closePath


def polygons(ch, dx, dy, s):
    """The glyph as polygons in raster space (y-down), for Pillow.

    Pillow fills each polygon on its own, so a counter would come out solid.
    Every letter drawn here is a single contour -- asserted, so a change of
    face or of name fails here instead of rendering a filled-in letter."""
    pen = _Flatten(_gs)
    _gs[_gname(ch)].draw(TransformPen(pen, (s, 0, 0, -s, dx, dy)))
    assert len(pen.contours) == 1, f"{ch!r} has {len(pen.contours)} contours; counters need even-odd filling"
    return pen.contours


# ---- geometry, in font units (y-down), shared by every output -------------------
# The lockup: a disc 1000 units across, centred on the name's cap height, then
# a gap of 0.4 cap heights to the S's ink, then the name tracked -0.02 em.
DISC = 1000
GAP = round(0.4 * CAP)
MID = DISC / 2                       # disc centre, both axes
BASE = MID + CAP / 2                 # baseline that centres the caps on the disc

_s_ink = ink("S")
S_SCALE = S_IN_DISC * DISC / (_s_ink[3] - _s_ink[1])


def mark_s(cx, cy, diameter):
    """Path of the disc's S, ink-centred on (cx, cy), for a disc of `diameter`."""
    s = S_SCALE * diameter / DISC
    icx, icy = (_s_ink[0] + _s_ink[2]) / 2, (_s_ink[1] + _s_ink[3]) / 2
    return path("S", cx - icx * s, cy + icy * s, s)


def name_layout():
    """[(char, pen_x)] with -0.02 em tracking, and the ink width of the whole."""
    pen, out = 0.0, []
    for i, ch in enumerate(NAME):
        out.append((ch, pen))
        pen += advance(ch) + (TRACK * UPM if i < len(NAME) - 1 else 0)
    first, last = ink(NAME[0]), ink(NAME[-1])
    return out, first[0], out[-1][1] + last[2]      # layout, ink left, ink right


LAYOUT, INK_L, INK_R = name_layout()
NAME_W = INK_R - INK_L
LOCK_W = DISC + GAP + NAME_W


def name_paths(x0, base, s=1.0):
    """The name, ink starting at x0, on baseline `base`, scaled by s."""
    return "".join(path(ch, x0 + (px - INK_L) * s, base, s) for ch, px in LAYOUT)


# ---- SVG outputs ------------------------------------------------------------------
PAD = 12   # units around the lockup, so the disc's antialiasing is not clipped


def svg(w, h, body, label="SIMUT", size=None):
    ws, hs = size if size else (w, h)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {_num(w)} {_num(h)}" '
            f'width="{_num(ws)}" height="{_num(hs)}" role="img" aria-label="{label}">'
            f"<title>{label}</title>{body}</svg>\n")


def write(rel, data, mode="w"):
    full = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, mode, **({"encoding": "utf-8"} if mode == "w" else {})) as f:
        f.write(data)
    print(f"{rel:<44} {os.path.getsize(full):>7} B")


def lockup(theme):
    c = T[theme]
    w, h = LOCK_W + 2 * PAD, DISC + 2 * PAD
    body = (f'<circle cx="{_num(PAD + MID)}" cy="{_num(PAD + MID)}" r="{_num(MID)}" fill="{c["acento"]}"/>'
            f'<path fill="{c["acento-tinta"]}" d="{mark_s(PAD + MID, PAD + MID, DISC)}"/>'
            f'<path fill="{c["tinta"]}" d="{name_paths(PAD + DISC + GAP, PAD + BASE)}"/>')
    return svg(w, h, body, size=(w * 64 / h, 64))


def mark_svg():
    c = T["claro"]
    body = (f'<circle cx="{_num(MID)}" cy="{_num(MID)}" r="{_num(MID)}" fill="{c["acento"]}"/>'
            f'<path fill="{c["acento-tinta"]}" d="{mark_s(MID, MID, DISC)}"/>')
    return svg(DISC, DISC, body, size=(128, 128))


def name_svg():
    # currentColor: whoever inlines it decides the colour (the web UI's login
    # page uses it this way, over its own --tinta).
    top = BASE - _s_ink[3]
    h = (_s_ink[3] - _s_ink[1])
    body = f'<path fill="currentColor" d="{name_paths(0, BASE - top)}"/>'
    return svg(NAME_W, h, body, size=(NAME_W * 24 / h, 24))


def marca_include():
    """Disc + name inline for the docs site; the classes take their colours from
    docs/assets/site.css, so the lockup follows the page's theme."""
    w, h = LOCK_W + 2 * PAD, DISC + 2 * PAD
    return (f'<svg class="marca-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {_num(w)} {_num(h)}" '
            f'aria-hidden="true" focusable="false">'
            f'<circle class="marca-disco" cx="{_num(PAD + MID)}" cy="{_num(PAD + MID)}" r="{_num(MID)}"/>'
            f'<path class="marca-s" d="{mark_s(PAD + MID, PAD + MID, DISC)}"/>'
            f'<path class="marca-nome" d="{name_paths(PAD + DISC + GAP, PAD + BASE)}"/></svg>')


def text_width(s, px):
    """Width of s in the texto stand-in: the room a <text> is given, pinned with
    textLength so a wider or narrower system face moves spacing, not layout."""
    return ImageFont.truetype(TEXTO, px).getlength(s)


def badge(large):
    """"Powered by SIMUT": a secondary button's surface and border (§4.1), the
    disc on the left. Drawn on its own surface because it lands on pages whose
    background nobody here chooses."""
    c = T["claro"]
    tx = "font-family=\"system-ui, -apple-system, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif\""
    if not large:
        H, D, pad, gap = 40, 24, 12, 8
        cap = 11                                   # name cap height, px
        s = cap / CAP
        pw = round(text_width("Powered by", 13))
        x_txt = pad + D + gap
        x_name = x_txt + pw + 4
        W = x_name + NAME_W * s + pad
        base = 25
        body = (f'<rect x=".5" y=".5" width="{_num(W - 1)}" height="{H - 1}" rx="6" '
                f'fill="{c["superficie"]}" stroke="{c["linha-forte"]}"/>'
                f'<circle cx="{pad + D / 2}" cy="{H / 2}" r="{D / 2}" fill="{c["acento"]}"/>'
                f'<path fill="{c["acento-tinta"]}" d="{mark_s(pad + D / 2, H / 2, D)}"/>'
                f'<text x="{x_txt}" y="{base}" {tx} font-size="13" fill="{c["tinta-2"]}" '
                f'textLength="{pw}" lengthAdjust="spacing">Powered by</text>'
                f'<path fill="{c["tinta"]}" d="{name_paths(x_name, base, s)}"/>')
        return svg(W, H, body, label="Powered by SIMUT")
    H, D, pad, gap = 72, 40, 16, 12
    cap = 17
    s = cap / CAP
    sub = "Monitoring firmware for the Raspberry Pi Pico W"
    pw, sw = round(text_width("Powered by", 13)), round(text_width(sub, 13))
    x_txt = pad + D + gap
    x_name = x_txt + pw + 6
    W = max(x_name + NAME_W * s, x_txt + sw) + pad
    base1, base2 = 35, 55
    body = (f'<rect x=".5" y=".5" width="{_num(W - 1)}" height="{H - 1}" rx="12" '
            f'fill="{c["superficie"]}" stroke="{c["linha-forte"]}"/>'
            f'<circle cx="{pad + D / 2}" cy="{H / 2}" r="{D / 2}" fill="{c["acento"]}"/>'
            f'<path fill="{c["acento-tinta"]}" d="{mark_s(pad + D / 2, H / 2, D)}"/>'
            f'<text x="{x_txt}" y="{base1}" {tx} font-size="13" fill="{c["tinta-2"]}" '
            f'textLength="{pw}" lengthAdjust="spacing">Powered by</text>'
            f'<path fill="{c["tinta"]}" d="{name_paths(x_name, base1, s)}"/>'
            f'<text x="{x_txt}" y="{base2}" {tx} font-size="13" fill="{c["tinta-2"]}" '
            f'textLength="{sw}" lengthAdjust="spacing">{sub}</text>')
    return svg(W, H, body, label="Powered by SIMUT")


# ---- raster outputs -------------------------------------------------------------------
def rgb(hexv):
    return tuple(int(hexv[i:i + 2], 16) for i in (1, 3, 5))


# The S is optically sized for the 16 px frame. At the vector's 0.60 the middle
# stroke blurs into the counters there; 0.60/0.64/0.68/0.72 were compared at
# real size on a light and a dark tab bar (2026-09-24) and 0.68 is the largest
# that still clears the rim. From 32 px up the vector's proportion reads fine.
S_AT = {16: 0.68}


def draw_mark(size, ss=16):
    """The disc at `size` px, drawn 16x larger and reduced with a box filter,
    which averages exactly the area each final pixel covers."""
    c = T["claro"]
    big = size * ss
    im = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((0, 0, big - 1, big - 1), fill=rgb(c["acento"]) + (255,))
    s = S_AT.get(size, S_IN_DISC) * DISC / (_s_ink[3] - _s_ink[1]) * big / DISC
    icx, icy = (_s_ink[0] + _s_ink[2]) / 2, (_s_ink[1] + _s_ink[3]) / 2
    for poly in polygons("S", big / 2 - icx * s, big / 2 + icy * s, s):
        d.polygon(poly, fill=rgb(c["acento-tinta"]) + (255,))
    return im.resize((size, size), Image.BOX)


def favicon():
    """16 and 32 px frames, the two sizes a tab asks for; anything larger scales
    from 32. Alpha under 32 is zeroed before the palette is cut, or near-clear
    edge pixels become opaque specks around the disc (seen at 16 px, 2026-08-19)."""
    frames = []
    for size in (16, 32):
        im = draw_mark(size)
        px = im.load()
        for y in range(size):
            for x in range(size):
                r, g, b, a = px[x, y]
                if a < 32:
                    px[x, y] = (0, 0, 0, 0)
        q = im.quantize(16, method=Image.Quantize.FASTOCTREE)
        buf = io.BytesIO()
        q.save(buf, "PNG", optimize=True)
        frames.append((size, buf.getvalue()))
    out = struct.pack("<HHH", 0, 1, len(frames))
    off = 6 + 16 * len(frames)
    for size, png in frames:
        out += struct.pack("<BBBBHHII", size, size, 0, 0, 1, 32, len(png), off)
        off += len(png)
    return out + b"".join(png for _, png in frames)


def social_preview():
    """1280x640 og:image: fundo, the lockup, one sentence of what it does, and
    the facts under a linha. Drawn at 2x and reduced."""
    c, k = T["claro"], 2
    W, H = 1280 * k, 640 * k
    im = Image.new("RGB", (W, H), rgb(c["fundo"]))
    d = ImageDraw.Draw(im)
    left = 96 * k
    # lockup: 176 px disc
    s = 176 * k / DISC
    top = 112 * k
    d.ellipse((left, top, left + DISC * s, top + DISC * s), fill=rgb(c["acento"]))
    ss = S_SCALE * s
    icx, icy = (_s_ink[0] + _s_ink[2]) / 2, (_s_ink[1] + _s_ink[3]) / 2
    cx, cy = left + MID * s, top + MID * s
    for poly in polygons("S", cx - icx * ss, cy + icy * ss, ss):
        d.polygon(poly, fill=rgb(c["acento-tinta"]))
    x0 = left + (DISC + GAP) * s
    for ch, px in LAYOUT:
        for poly in polygons(ch, x0 + (px - INK_L) * s, top + BASE * s, s):
            d.polygon(poly, fill=rgb(c["tinta"]))
    # one sentence, texto, tinta-2
    f = ImageFont.truetype(TEXTO, 40 * k)
    y = 352 * k
    for line in ("Offline-first temperature, humidity and pressure",
                 "monitoring for the Raspberry Pi Pico W."):
        d.text((left, y), line, font=f, fill=rgb(c["tinta-2"]))
        y += 56 * k
    # facts under a linha
    d.line((left, 500 * k, W - left, 500 * k), fill=rgb(c["linha"]), width=2 * k // 2)
    f2 = ImageFont.truetype(TEXTO, 26 * k)
    d.text((left, 528 * k), "Touch panel  ·  Web interface  ·  Telemetry  ·  Over-the-air updates  ·  Open source",
           font=f2, fill=rgb(c["tinta-2"]))
    im = im.resize((1280, 640), Image.LANCZOS)
    q = im.quantize(64, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    buf = io.BytesIO()
    q.save(buf, "PNG", optimize=True)
    return buf.getvalue()


if __name__ == "__main__":
    write("docs/images/logo-mark.svg", mark_svg())
    write("docs/images/logo-wordmark.svg", lockup("claro"))
    write("docs/images/logo-wordmark-dark.svg", lockup("escuro"))
    write("docs/images/logo-name.svg", name_svg())
    write("docs/images/powered-by-simut.svg", badge(False))
    write("docs/images/powered-by-simut-large.svg", badge(True))
    write("docs/_includes/marca.html", marca_include() + "\n")
    write("docs/images/social-preview.png", social_preview(), "wb")
    write("data/favicon.ico", favicon(), "wb")
