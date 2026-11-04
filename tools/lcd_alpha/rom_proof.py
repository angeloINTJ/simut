#!/usr/bin/env python3
"""rom_proof.py -- the proof sheet of the ROM A00 table: rom_hd44780.py drawn in the datasheet's grid, beside the same region of the datasheet's page.

Left, the table as transcribed, drawn at the datasheet's own geometry (columns = the code's high nibble 0010 to 0111, rows = its low nibble xxxx0000
to xxxx1111, each dot a 2.51 pt square on the 3.0 pt lattice, the code in grey under each glyph). Right, the same region cropped from the rendered
page 17 of the PDF and brought to the same scale, so that a reader can run an eye down both and see a dot that differs. With --overlay a second
picture puts the page in light grey and outlines every dot of the table in red: a dot of the page with no outline is missing from the table, an
outline over blank paper is a dot the page does not have.

  rom_proof.py --page-png PAGE.png --out SHEET.png [--page-dpi 600] [--scale 4] [--overlay OVERLAY.png]

PAGE.png is page 17 rendered by pdftoppm (rom_extract.py --render-dir leaves one, page17-17.png). The PNG comes from downloaded data: run this with
python3 -I. Needs Pillow.
"""
import argparse
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)      # explicit: python3 -I leaves the script's own folder off the path
import rom_extract as rx      # noqa: E402  (the page geometry lives there, once)
import rom_hd44780 as rom     # noqa: E402

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    Image = None

LAB_L = 81.477 * rx.GRID_SX                  # the outer box's left edge: the column of row labels starts here
TOP = (202.98 + 506.609) * rx.GRID_SY        # the outer box's top edge, over the header row
GL, GR = rx.XL + 2 * rx.CW, rx.XL + 8 * rx.CW  # nibble columns 2 to 7 (0x20 to 0x7F)
LAB_W = rx.XL - LAB_L


def font(px):
    try:
        return ImageFont.load_default(size=px)
    except TypeError:         # Pillow before 10.1: the bitmap font has no size
        return ImageFont.load_default()


def centred(d, cx, cy, text, f, fill):
    bb = d.textbbox((0, 0), text, font=f)
    d.text((cx - (bb[2] - bb[0]) / 2 - bb[0], cy - (bb[3] - bb[1]) / 2 - bb[1]), text, font=f, fill=fill)


def to_px(S):
    """Page points to the panel's pixels, for a point inside the dot columns (the label column is drawn at the left of them)."""
    return (lambda x: (LAB_W + (x - GL)) * S), (lambda y: (TOP - y) * S)


def draw_mine(S):
    W, H = int(round((LAB_W + GR - GL) * S)), int(round((TOP - rx.YB) * S))
    im = Image.new('RGB', (W, H), 'white')
    d = ImageDraw.Draw(im)
    X, Y = to_px(S)
    lw = max(1, int(round(0.6 * S)))          # the datasheet's lines are 0.49 to 0.78 pt
    d.rectangle([0, 0, W - 1, H - 1], outline='black', width=lw)
    for r in range(17):
        d.line([0, Y(rx.YT - r * rx.CH), W, Y(rx.YT - r * rx.CH)], fill='black', width=lw)
    d.line([0, 0, W, 0], fill='black', width=lw)
    d.line([LAB_W * S, 0, LAB_W * S, H], fill='black', width=lw)
    for n in range(2, 8):
        d.line([X(rx.XL + n * rx.CW), 0, X(rx.XL + n * rx.CW), H], fill='black', width=lw)
        centred(d, X(rx.XL + (n + 0.5) * rx.CW), Y(rx.YT + 8.0), format(n, '04b'), font(int(7.8 * S)), 'black')
    for r in range(16):
        centred(d, LAB_W * S / 2, Y(rx.YT - (r + 0.5) * rx.CH), 'xxxx' + format(r, '04b'), font(int(7.2 * S)), 'black')
    for code, rows in sorted(rom.ROM_A00.items()):
        n, r = code >> 4, code & 15
        for i, v in enumerate(rows):
            for j in range(5):
                if v & (16 >> j):
                    x, yb = rx.lattice_box(n, r, i, j)
                    d.rectangle([X(x), Y(yb + rx.DOT), X(x + rx.DOT) - 1, Y(yb) - 1], fill='black')
        centred(d, X(rx.XL + (n + 0.5) * rx.CW), Y(rx.YT - (r + 1) * rx.CH + 3.0), '%02X' % code, font(int(4.6 * S)), (150, 150, 150))
    return im


def crops(page, k, S):
    """The datasheet's region at S pixels per point: (the whole panel, the dot columns alone)."""
    resample = getattr(Image, 'Resampling', Image).LANCZOS
    y0, y1 = int(round((rx.PAGE_H - TOP) * k)), int(round((rx.PAGE_H - rx.YB) * k))
    H, wa, wb = int(round((TOP - rx.YB) * S)), int(round(LAB_W * S)), int(round((GR - GL) * S))
    a = page.crop((int(round(LAB_L * k)), y0, int(round(rx.XL * k)), y1)).resize((wa, H), resample)
    b = page.crop((int(round(GL * k)), y0, int(round(GR * k)), y1)).resize((wb, H), resample)
    whole = Image.new('L', (wa + wb, H), 255)
    whole.paste(a, (0, 0))
    whole.paste(b, (wa, 0))
    return whole, b


def overlay(strip, S):
    """The page's dot columns in light grey, every dot of the table outlined in red."""
    lut = [int(150 + v * 105 / 255) for v in range(256)]
    im = strip.point(lut).convert('RGB')
    d = ImageDraw.Draw(im)
    wa = int(round(LAB_W * S))
    X, Y = to_px(S)
    for code, rows in sorted(rom.ROM_A00.items()):
        for i, v in enumerate(rows):
            for j in range(5):
                if v & (16 >> j):
                    x, yb = rx.lattice_box(code >> 4, code & 15, i, j)
                    cx, cy = x + rx.DOT / 2, yb + rx.DOT / 2
                    d.rectangle([X(cx - 1.45) - wa, Y(cy + 1.45), X(cx + 1.45) - wa, Y(cy - 1.45)], outline=(200, 0, 0), width=1)
    return im


def main(argv=None):
    ap = argparse.ArgumentParser(description='Proof sheet of the ROM A00 table beside the datasheet page')
    ap.add_argument('--page-png', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--page-dpi', type=float, default=600)
    ap.add_argument('--scale', type=float, default=4, help='pixels per point of the sheet (4 = 288 dpi)')
    ap.add_argument('--overlay')
    args = ap.parse_args(argv)
    if Image is None:
        print('rom_proof: Pillow is not installed', file=sys.stderr)
        return 2
    S, k = args.scale, args.page_dpi / 72.0
    page = Image.open(args.page_png).convert('L')
    mine = draw_mine(S)
    whole, strip = crops(page, k, S)
    pad, band = 20, int(24 * S / 4)
    sheet = Image.new('RGB', (2 * mine.size[0] + 3 * pad, mine.size[1] + band + 2 * pad), 'white')
    d = ImageDraw.Draw(sheet)
    f = font(int(5.0 * S))
    d.text((pad, pad // 2), 'transcription: rom_hd44780.py (%s)' % rom.READ_ON, font=f, fill='black')
    d.text((2 * pad + mine.size[0], pad // 2), 'datasheet: HD44780.pdf page 17, sha256 %s...' % rom.PDF_SHA256[:12], font=f, fill='black')
    sheet.paste(mine, (pad, band + pad))
    sheet.paste(whole.convert('RGB'), (2 * pad + mine.size[0], band + pad))
    sheet.save(args.out, optimize=True)
    print('wrote %s (%d x %d)' % (args.out, sheet.size[0], sheet.size[1]))
    if args.overlay:
        ov = overlay(strip, S)
        ov.save(args.overlay, optimize=True)
        print('wrote %s (%d x %d)' % (args.overlay, ov.size[0], ov.size[1]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
