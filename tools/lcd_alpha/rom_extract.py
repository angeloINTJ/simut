#!/usr/bin/env python3
"""rom_extract.py -- reads Table 4 (ROM code A00) of the Hitachi HD44780U datasheet and writes or checks rom_hd44780.py.

The glyphs are not typed. Page 17 of the datasheet is a vector drawing, not a picture: every lit dot is a filled 2.51 pt square in the page's
content stream (2298 of them, counted on 2026-11-04), so the table can be read from the drawing itself. It is read twice, by two instruments that
share the page and the cell geometry and nothing else:

  A  the vector read: each filled square of the content stream goes into its cell (column = the code's high nibble, row = its low nibble) and
     onto the cell's lattice, 5 dot columns by 10 dot rows at a 3.0 pt pitch. The datasheet draws every cell as a 5 x 10 lattice; lattice rows
     8 and 9 only carry dots in the 5 x 10 patterns of columns 0xE and 0xF.
  B  the raster read: the page rendered by pdftoppm at 600 dpi, and each of the 50 lattice positions of every cell sampled by threshold.

They must agree on every position of every cell of columns 1 to 15 (column 0 is CG RAM, which the page fills with the labels (1) to (8)), not only
on the 96 codes the module keeps, and no ink of the render may lie outside A's squares in the cells' dot area: a dot A does not know cannot hide
there. The instrument is also proved on the datasheet's own worked example (Table 2, page 15, written there as text: the character code 0x62, a
'b', as five-bit rows, O4 the leftmost dot and O0 the LSB at the right): both reads must give that glyph for the table's code 0x62.

  rom_extract.py PDF --render-dir DIR [--write FILE | --check] [--dump] [--controls]

  --render-dir  where the 600 dpi PNG goes. Never the PDF's own folder: the download stays untouched. Default: a temporary folder.
  --write       writes the module to FILE when both reads agree and every check holds.
  --check       generates the module text from the reads and compares it, byte for byte, with tools/lcd_alpha/rom_hd44780.py.
  --dump        prints the 96 glyphs as pictures.
  --controls    puts one fault at a time into each instrument (a dot dropped from A, a dot added to A, a dot painted out of the render, a stray mark
                in the render, the worked example read mirrored and upside down) and requires the checks to report exactly that fault.

Exit 0 clean; 1 findings (the reads disagree, the render holds ink that A does not explain, or the module differs from the PDF); 2 when it cannot
read (a tool is missing, the PDF is not the pinned file, or the page draws something the reader does not know). The PDF is downloaded data: run
this with python3 -I. Needs pdftoppm and pdftotext (poppler) and Pillow.
"""
import argparse
import collections
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

try:
    from PIL import Image, ImageDraw, ImageStat
except ImportError:
    Image = None

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
MODULE = os.path.join(HERE, 'rom_hd44780.py')

# The document. The run refuses any other file: the object numbers and the geometry below hold for this one only.
DOCUMENT = "Hitachi HD44780U (LCD-II), Dot Matrix Liquid Crystal Display Controller/Driver, ADE-207-272(Z), Rev. 0.0, '99.9"
TABLE = 'Table 4, Correspondence between Character Codes and Character Patterns (ROM Code: A00), page 17'
URL = 'https://www.sparkfun.com/datasheets/LCD/HD44780.pdf'
PDF_SHA256 = '4324d67ec14fc8e7ba8cf7db7256749c9b80b94b3f8bedfa614a0d2839c7dea9'
PDF_BYTES = 329795
READ_ON = '2026-11-04'
METHOD = 'two reads compared on all 256 cells: the vector squares of page 17, and a 600 dpi render sampled on the 5 x 10 lattice (rom_extract.py)'
TABLE_PAGE, EXAMPLE_PAGE = 17, 15
# Page 17 of this file is object 76, whose /Contents (object 78) is [424 0 R 80 0 R] (mutool show, 2026-11-04).
CONTENT_OBJS = (424, 80)
DOTS = 2298
# What the page's two content streams paint besides the dots, as (width, height, painting operator, the path has more than the rectangle), counted on
# 2026-11-04: the table's outer box (stroked), the 16 white bars under the rows (filled and stroked), and, plainly filled, the page header's two rules
# (434 x 1 pt) and the bars of the HITACHI logo. Anything else is a drawing this reader has not seen; and the plainly filled ones must lie outside the
# table's body (they are page furniture), which run( ) checks.
EXPECTED_CENSUS = {((429.91, 506.61), 'S', True): 1, ((337.72, 6.25), 'B', False): 16,
                   ((434.0, 1.0), 'f', False): 2, ((2.04, 8.19), 'f', False): 7, ((4.3, 1.3), 'f', False): 2,
                   ((8.45, 1.37), 'f', False): 1, ((4.0, 1.28), 'f', False): 1}

# Page 17's geometry, in PDF points (MediaBox 595 x 842, origin at the bottom left), measured on the content stream on 2026-11-04. The grid lines are
# drawn inside a cm matrix (x 0.9994, y 1.0006) and the dots outside it, so the lines' numbers are scaled here and the dots' are not.
PAGE_H = 842.0
GRID_SX, GRID_SY = 0.9994, 1.0006
XL, XR = 125.584 * GRID_SX, 511.391 * GRID_SX     # the 16 nibble columns span XL to XR
YT, YB = 693.319 * GRID_SY, 202.98 * GRID_SY      # the 16 rows span YT (under the header row) to YB
CW, CH = (XR - XL) / 16, (YT - YB) / 16
DOT, PITCH = 2.51, 3.0                            # a dot's side, and the pitch of the lattice
DOT_X0 = 4.55   # dot column 0's left edge, from its cell's left edge: 4.18 to 4.94 over the 12 populated columns
DOT_Y0 = 0.60   # dot row 0's top edge, below its cell's top edge: 0.54 to 0.66 over the 16 rows
DPI = 600

GLYPH_NAMES = {0x5C: 'yen sign (a backslash in ASCII)', 0x7E: 'right arrow', 0x7F: 'left arrow'}


class Unreadable(Exception):
    """The page is not what the reader expects. A refusal (exit 2), never an empty table."""


# -- the PDF: page 17's content stream ------------------------------------------------------------------------------------------------------------

def object_stream(pdf, num):
    m = re.search(rb'[\r\n]%d 0 obj[\r\n]' % num, pdf)
    if not m:
        raise Unreadable('object %d not found' % num)
    s = pdf.find(b'stream', m.end())
    if s < 0 or b'FlateDecode' not in pdf[m.end():s] or b'endobj' in pdf[m.end():s]:
        raise Unreadable('object %d is not a Flate stream' % num)
    i = s + 6
    if pdf[i:i + 2] == b'\r\n':
        i += 2
    elif pdf[i:i + 1] in (b'\r', b'\n'):
        i += 1
    d = zlib.decompressobj()
    data = d.decompress(pdf[i:])
    if not d.eof:
        raise Unreadable('object %d: the Flate stream does not end' % num)
    return data


_WS = b' \t\r\n\x0c\x00'
_SEP = _WS + b'()<>[]{}/%'


def tokens(data):
    """The numbers ('n', float) and operators ('o', str) of a content stream; strings, names and array brackets are skipped."""
    i, n = 0, len(data)
    while i < n:
        c = data[i:i + 1]
        if c in _WS:
            i += 1
        elif c == b'(':
            depth, i = 1, i + 1
            while depth:
                if i >= n:
                    raise Unreadable('a string never ends in the content stream')
                ch = data[i:i + 1]
                if ch == b'\\':
                    i += 1
                elif ch == b'(':
                    depth += 1
                elif ch == b')':
                    depth -= 1
                i += 1
        elif c == b'/':
            i += 1
            while i < n and data[i:i + 1] not in _SEP:
                i += 1
        elif c in b'[]':
            i += 1
        elif c == b'%':
            while i < n and data[i:i + 1] not in b'\r\n':
                i += 1
        elif c in b'<>{}':
            raise Unreadable('unexpected %r in the content stream' % c)
        else:
            j = i
            while j < n and data[j:j + 1] not in _SEP:
                j += 1
            word = data[i:j].decode('latin-1')
            i = j
            try:
                yield 'n', float(word)
            except ValueError:
                yield 'o', word


def vector_dots(content):
    """Read A. -> (dots, census, furniture): the lower-left corner (x, y) of every filled 2.51 pt square; a count of every other painted rectangle;
    and the (x, y, w, h) of the other rectangles that are only filled."""
    dots, census, furniture = [], collections.Counter(), []
    nums, pend, extra = [], None, False
    ident, stack = True, []
    for kind, v in tokens(content):
        if kind == 'n':
            nums.append(v)
            continue
        if v == 'q':
            stack.append(ident)
        elif v == 'Q':
            ident = stack.pop()
        elif v == 'cm':
            ident = ident and nums[-6:] == [1, 0, 0, 1, 0, 0]
        elif v == 're':
            if pend is not None or len(nums) < 4:
                raise Unreadable('a rectangle that is never painted')
            pend, extra = tuple(nums[-4:]), False
        elif v in ('m', 'l', 'c', 'v', 'y', 'h'):
            extra = extra or pend is not None
        elif v in ('f', 'F', 'f*', 'S', 's', 'B', 'b', 'B*', 'b*', 'n'):
            if pend is not None:
                x, y, w, h = pend
                size = (round(abs(w), 2), round(abs(h), 2))
                if v in ('f', 'F') and not extra and size == (DOT, DOT):
                    if not ident:
                        raise Unreadable('a dot drawn under a transform')
                    dots.append((x, y if h > 0 else y + h))   # a negative height means y is the top edge
                else:
                    census[(size, v, extra)] += 1
                    if v in ('f', 'F') and not extra:
                        furniture.append((min(x, x + w), min(y, y + h), abs(w), abs(h)))
            pend, extra = None, False
        nums = []
    return dots, census, furniture


def lattice_box(n, r, i, j):
    """The square of lattice position (dot row i, dot column j) of cell (n, r): (x, y) of its lower-left corner, in points."""
    return XL + n * CW + DOT_X0 + PITCH * j, YT - r * CH - DOT_Y0 - PITCH * i - DOT


def bin_dots(dots):
    """-> (cells, spread): {(nibble, row): {(i, j)}} for the dots, and the ranges of their distance from the lattice, in points."""
    cells, rx, ry = {}, [], []
    for x, yb in dots:
        n, r = int((x + DOT / 2 - XL) // CW), int((YT - (yb + DOT / 2)) // CH)
        if not (0 <= n < 16 and 0 <= r < 16):
            raise Unreadable('a dot outside the table at (%.2f, %.2f)' % (x, yb))
        fx = (x - (XL + n * CW) - DOT_X0) / PITCH
        fy = ((YT - r * CH) - DOT_Y0 - (yb + DOT)) / PITCH
        j, i = round(fx), round(fy)
        if not (0 <= j <= 4 and 0 <= i <= 9) or abs(fx - j) * PITCH > 0.75 or abs(fy - i) * PITCH > 0.5:
            raise Unreadable('a dot off the lattice at (%.2f, %.2f), cell 0x%X%X' % (x, yb, n, r))
        cell = cells.setdefault((n, r), set())
        if (i, j) in cell:
            raise Unreadable('two dots on one lattice position, cell 0x%X%X row %d column %d' % (n, r, i, j))
        cell.add((i, j))
        rx.append((fx - j) * PITCH)
        ry.append((fy - i) * PITCH)
    return cells, (min(rx), max(rx), min(ry), max(ry))


# -- the render: read B and the ink check ---------------------------------------------------------------------------------------------------------

def render_page(pdf_path, page, dpi, outdir):
    prefix = os.path.join(outdir, 'page%d' % page)
    for old in glob.glob(prefix + '-*.png'):
        os.remove(old)
    subprocess.run(['pdftoppm', '-f', str(page), '-l', str(page), '-r', str(dpi), '-gray', '-png', pdf_path, prefix],
                   check=True, capture_output=True)
    found = glob.glob(prefix + '-*.png')
    if len(found) != 1:
        raise Unreadable('pdftoppm wrote %d files for page %d' % (len(found), page))
    return found[0]


def raster_cells(ink, k):
    """Read B. ink is the render with ink = 255. -> (cells, least inked lit position, most inked unlit position), over nibble columns 1 to 15."""
    half = max(1, int(0.4 * k))      # a window of 0.8 pt, well inside a 2.51 pt dot and well clear of the next one
    cells, lit_min, unlit_max = {}, 1.0, 0.0
    for n in range(1, 16):
        for r in range(16):
            s = set()
            for i in range(10):
                for j in range(5):
                    x, yb = lattice_box(n, r, i, j)
                    cx, cy = int(round((x + DOT / 2) * k)), int(round((PAGE_H - (yb + DOT / 2)) * k))
                    f = ImageStat.Stat(ink.crop((cx - half, cy - half, cx + half + 1, cy + half + 1))).mean[0] / 255.0
                    if f >= 0.5:
                        s.add((i, j))
                        lit_min = min(lit_min, f)
                    else:
                        unlit_max = max(unlit_max, f)
            if s:
                cells[(n, r)] = s
    return cells, lit_min, unlit_max


def residual_ink(ink, dots, k):
    """Paints A's squares (0.3 pt wider) out of the render and counts the ink left in the dot areas (lattice rows 0 to 7) of columns 1 to 15."""
    work = ink.copy()
    d = ImageDraw.Draw(work)
    pad = 0.3
    for x, yb in dots:
        d.rectangle([(x - pad) * k, (PAGE_H - (yb + DOT + pad)) * k, (x + DOT + pad) * k, (PAGE_H - (yb - pad)) * k], fill=0)
    total, where = 0, []
    for n in range(1, 16):
        for r in range(16):
            x0, x1 = XL + n * CW + 2.0, XL + (n + 1) * CW - 2.0
            y0, y1 = YT - r * CH - 0.5, YT - r * CH - 23.9
            box = (int(round(x0 * k)), int(round((PAGE_H - y0) * k)), int(round(x1 * k)), int(round((PAGE_H - y1) * k)))
            c = work.crop(box).histogram()[255]
            if c:
                total += c
                where.append('0x%X%X: %d px' % (n, r, c))
    return total, where


# -- the instrument's proof: the datasheet's own worked example ----------------------------------------------------------------------------------

def worked_example(pdf_path):
    """Table 2 of page 15 is text: 16 lines of 4 line-position bits and 5 data bits (O4 .. O0); the 8th line also carries the 8-bit character code."""
    text = subprocess.run(['pdftotext', '-f', str(EXAMPLE_PAGE), '-l', str(EXAMPLE_PAGE), '-layout', pdf_path, '-'],
                          capture_output=True, text=True, check=True).stdout
    rows, code = [], None
    for line in text.splitlines():
        bits = []
        for w in line.split():
            if w not in ('0', '1'):
                break
            bits.append(w)
        if len(bits) == 17:
            code, bits = int(''.join(bits[:8]), 2), bits[8:]
        if len(bits) == 9:
            rows.append((int(''.join(bits[:4]), 2), int(''.join(bits[4:]), 2)))
    if code is None or [p for p, _ in rows] != list(range(16)) or any(d for p, d in rows if p >= 8):
        raise Unreadable('cannot read the worked example (Table 2) on page %d' % EXAMPLE_PAGE)
    return code, tuple(d for p, d in rows[:8])


# -- the table and the module ---------------------------------------------------------------------------------------------------------------------

def glyph_table(cells):
    """-> (table, below): code -> 8 five-bit rows (element 0 the top row, bit 4 the leftmost dot) for 0x20 to 0x7F, and the codes with dots below row 7."""
    table, below = {}, []
    for code in range(0x20, 0x80):
        s = cells.get((code >> 4, code & 15), set())
        if any(i >= 8 for i, _ in s):
            below.append(code)
        table[code] = tuple(sum(16 >> j for ii, j in s if ii == i) for i in range(8))
    return table, below


def table_digest(table):
    text = ''.join('%02X:%s\n' % (c, ','.join('%02X' % v for v in table[c])) for c in sorted(table))
    return hashlib.sha256(text.encode('ascii')).hexdigest()


def module_source(table, wide):
    def label(code):
        return GLYPH_NAMES.get(code) or repr(chr(code))

    out = ['"""The HD44780U character generator ROM A00, codes 0x20 to 0x7F, 5 x 8 dots. GENERATED by tools/lcd_alpha/rom_extract.py: never edit by hand.',
           '',
           'Document   ' + DOCUMENT,
           'Table      ' + TABLE,
           'URL        ' + URL,
           'PDF        %d bytes, sha256 %s' % (PDF_BYTES, PDF_SHA256),
           'Read on    ' + READ_ON,
           'Method     Not typed. Page 17 is a vector drawing, one filled 2.51 pt square for each lit dot (2298 of them). rom_extract.py read it twice, and',
           '           the two reads agree on every one of the 12000 lattice positions of the 240 cells of columns 1 to 15, not only on these 96 codes:',
           '           (A) the squares of the page\'s content stream, each put in its cell and on the cell\'s 5 x 10 lattice; (B) the page rendered at 600 dpi',
           '           by pdftoppm, each lattice position sampled by threshold. No ink of the render lies outside the squares of A in the cells\' dot',
           '           areas, and the datasheet\'s own worked example (Table 2, page 15: the code 0x62, a \'b\', written there as text) reads the same from',
           '           the table. Looked at beside the page on 2026-11-04 with tools/lcd_alpha/rom_proof.py. No data from any other source.',
           '',
           'Shape      ROM_A00[code] is 8 integers, one per dot row of the 5 x 8 cell. Element 0 is the TOP row; in each integer bit 4 (value 16) is the',
           '           LEFTMOST dot and bit 0 the rightmost, a lit dot being a 1: the convention of the firmware\'s CGRAM bitmaps (src/display/BigFont_HD44780.h:',
           '           BF_LT, whose top-left dot is off, starts 0b00111) and the datasheet\'s own (Table 2, page 15: O4 to O0, the LSB at the right).',
           '',
           'Row 7      is the cursor row of the 5 x 8 font (Table 2 labels that line Cursor position). Table 4 draws no dot on it for any of these 96 codes,',
           '           and none on lattice rows 8 and 9 either, so it is 0 here. The page draws dots on rows 7 to 9 only in %d cells of columns 0xE and 0xF' % len(wide),
           '           (%s), none of them in this table. Nothing here says what a display does with the cursor row when the cursor is on.' % ' '.join('0x%02X' % c for c in wide),
           '',
           'Regenerate  python3 -I tools/lcd_alpha/rom_extract.py HD44780.pdf --render-dir DIR --write tools/lcd_alpha/rom_hd44780.py',
           'Check       the same with --check in place of --write exits 0 when this file is what the PDF says. tools/lcd_alpha/test_rom.py holds the shape.',
           '"""',
           'DOCUMENT = %r' % DOCUMENT,
           'TABLE = %r' % TABLE,
           'URL = %r' % URL,
           'PDF_SHA256 = %r' % PDF_SHA256,
           'PDF_BYTES = %d' % PDF_BYTES,
           'READ_ON = %r' % READ_ON,
           'METHOD = %r' % METHOD,
           '# sha256 of the table as text, one line per code: "%02X:" then the 8 rows as %02X separated by commas. test_rom.py recomputes it.',
           'TABLE_SHA256 = %r' % table_digest(table),
           '',
           'ROM_A00 = {']
    for code in sorted(table):
        out.append('    0x%02X: (%s),  # %s' % (code, ', '.join('0b%s' % format(v, '05b') for v in table[code]), label(code)))
    out.append('}')
    return '\n'.join(out) + '\n'


def first_difference(a, b):
    la, lb = a.splitlines(), b.splitlines()
    for n in range(max(len(la), len(lb))):
        if n >= len(la) or n >= len(lb) or la[n] != lb[n]:
            return n + 1
    return 0


# -- the run --------------------------------------------------------------------------------------------------------------------------------------

def compare_reads(a, b):
    """The positions of nibble columns 1 to 15 on which the vector read a and the raster read b differ, one line each."""
    diffs = []
    for n in range(1, 16):
        for r in range(16):
            sa, sb = a.get((n, r), set()), b.get((n, r), set())
            for i, j in sorted(sa ^ sb):
                diffs.append('code 0x%X%X, dot row %d, column %d: %s only' % (n, r, i, j, 'the vector read' if (i, j) in sa else 'the raster read'))
    return diffs


def controls(cells_a, cells_b, dots, ink, k, glyph, example):
    """A fault put into one instrument at a time, and the checks must report exactly it. -> the controls that did not bite."""
    missed = []

    def expect(what, got, want):
        ok = got == want
        print('control: %s -> %s %s' % (what, got, 'ok' if ok else 'MISSED, wanted %s' % (want,)))
        if not ok:
            missed.append(what)

    def copy_of(cells):
        return {c: set(s) for c, s in cells.items()}

    dot = sorted(cells_a[(4, 1)])[0]          # a lit dot of the 0x41
    a1 = copy_of(cells_a)
    a1[(4, 1)].discard(dot)
    expect('a lit dot of 0x41 dropped from the vector read', compare_reads(a1, cells_b), ['code 0x41, dot row %d, column %d: the raster read only' % dot])
    a2 = copy_of(cells_a)
    a2[(2, 1)].add((0, 0))                    # the '!' has no dot there
    expect('a dot the page does not have added to 0x21 in the vector read', compare_reads(a2, cells_b),
           ['code 0x21, dot row 0, column 0: the vector read only'])
    x, yb = lattice_box(4, 1, *dot)
    ink3 = ink.copy()
    ImageDraw.Draw(ink3).rectangle([(x - 0.2) * k, (PAGE_H - (yb + DOT + 0.2)) * k, (x + DOT + 0.2) * k, (PAGE_H - (yb - 0.2)) * k], fill=0)
    expect('the same dot of 0x41 painted out of the render', compare_reads(cells_a, raster_cells(ink3, k)[0]),
           ['code 0x41, dot row %d, column %d: the vector read only' % dot])
    ink4 = ink.copy()                         # 0.6 pt of ink between four lattice positions of the blank 0x20: no sample window reaches it
    cx, cy = XL + 2 * CW + DOT_X0 + DOT / 2 + 4.5, YT - DOT_Y0 - DOT / 2 - 10.5
    ImageDraw.Draw(ink4).rectangle([(cx - 0.3) * k, (PAGE_H - (cy + 0.3)) * k, (cx + 0.3) * k, (PAGE_H - (cy - 0.3)) * k], fill=255)
    expect('a stray 0.6 pt mark on the blank 0x20 in the render: unexplained ink', residual_ink(ink4, dots, k)[0] > 0, True)
    expect('the worked example read with the dot columns right to left is not the table\'s glyph', tuple(int(format(v, '05b')[::-1], 2) for v in glyph) == example, False)
    expect('the worked example read with the rows bottom to top is not the table\'s glyph', glyph[::-1] == example, False)
    return missed


def run(args):
    for tool in ('pdftoppm', 'pdftotext'):
        if not shutil.which(tool):
            raise Unreadable('%s (poppler) is not installed' % tool)
    if Image is None:
        raise Unreadable('Pillow is not installed')
    with open(args.pdf, 'rb') as f:
        pdf = f.read()
    if len(pdf) != PDF_BYTES or hashlib.sha256(pdf).hexdigest() != PDF_SHA256:
        raise Unreadable('%s is not the pinned file (%d bytes, sha256 %s)' % (args.pdf, PDF_BYTES, PDF_SHA256))
    holder = None
    if args.render_dir:
        os.makedirs(args.render_dir, exist_ok=True)
        rdir = args.render_dir
    else:
        holder = tempfile.TemporaryDirectory()
        rdir = holder.name
    if os.path.realpath(rdir) == os.path.realpath(os.path.dirname(os.path.abspath(args.pdf))):
        raise Unreadable('--render-dir must not be the PDF\'s own folder')
    findings = []

    content = b'\n'.join(object_stream(pdf, o) for o in CONTENT_OBJS)
    if b'ROM Code: A00' not in content:
        raise Unreadable('page %d is not the A00 table' % TABLE_PAGE)
    dots, census, furniture = vector_dots(content)
    if len(dots) != DOTS or dict(census) != EXPECTED_CENSUS:
        raise Unreadable('the page draws something unexpected: %d dots, other rectangles %s' % (len(dots), dict(census)))
    inside = [f for f in furniture if f[0] < XR and f[0] + f[2] > XL and f[1] < YT and f[1] + f[3] > YB]
    if inside:
        raise Unreadable('a filled rectangle that is not a dot lies inside the table body: %s' % inside[:3])
    cells_a, spread = bin_dots(dots)
    if any(n == 0 for n, _ in cells_a):
        raise Unreadable('dots in the CG RAM column')
    print('A  vector read: %d dots in %d cells; their distance from the lattice, x %+.2f..%+.2f pt, y %+.2f..%+.2f pt' % ((len(dots), len(cells_a)) + spread))

    k = DPI / 72.0
    png = render_page(args.pdf, TABLE_PAGE, DPI, rdir)
    ink = Image.open(png).convert('L').point(lambda v: 255 if v < 128 else 0)
    cells_b, lit_min, unlit_max = raster_cells(ink, k)
    print('B  raster read: %s at %d dpi, %d x %d px; 12000 positions sampled; the least inked lit one %.0f %%, the most inked unlit one %.0f %%'
          % (os.path.basename(png), DPI, ink.size[0], ink.size[1], 100 * lit_min, 100 * unlit_max))
    diffs = compare_reads(cells_a, cells_b)
    print('A vs B: %d differences over 12000 positions' % len(diffs))
    findings += diffs
    left, where = residual_ink(ink, dots, k)
    print('ink in the render outside A\'s squares, in the dot areas: %d px%s' % (left, ' (' + ', '.join(where[:8]) + ')' if where else ''))
    if left:
        findings.append('the render holds %d px of ink that the vector squares do not explain (%s)' % (left, ', '.join(where[:8])))

    table, below = glyph_table(cells_a)
    if below:
        findings.append('dots below row 7 in %s: a 5 x 8 table cannot hold them' % ', '.join('0x%02X' % c for c in below))
    code, example = worked_example(args.pdf)
    same = (code in table and table[code] == example)
    print('worked example (page 15, code 0x%02X): %s -- the table\'s 0x%02X is %s' % (code, ' '.join(format(v, '05b') for v in example), code,
                                                                            ' '.join(format(v, '05b') for v in table.get(code, ()))))
    if not same:
        findings.append('the worked example of page %d is not the table\'s code 0x%02X' % (EXAMPLE_PAGE, code))
    row7 = [c for c in table if table[c][7]]
    wide = sorted((n << 4 | r) for (n, r), s in cells_a.items() if any(i >= 7 for i, _ in s))
    print('96 codes: %d non-blank; row 7 holds a dot in %d of them; cells that use lattice rows 7 to 9: %s'
          % (sum(1 for c in table if any(table[c])), len(row7), ' '.join('0x%02X' % c for c in wide)))

    if args.dump:
        for base in range(0x20, 0x80, 8):
            print('  '.join('0x%02X ' % c for c in range(base, base + 8)))
            for row in range(8):
                print('  '.join(''.join('#' if table[c][row] & (16 >> j) else '.' for j in range(5)) + ' ' for c in range(base, base + 8)))
            print()

    if args.controls:
        findings += ['control missed: ' + m for m in controls(cells_a, cells_b, dots, ink, k, table[code], example)]
    src = module_source(table, wide)
    if args.check:
        with open(MODULE, encoding='utf-8') as f:
            have = f.read()
        if have != src:
            findings.append('%s differs from the datasheet read, first at line %d' % (MODULE, first_difference(have, src)))
        else:
            print('%s is what the PDF says (byte for byte)' % MODULE)
    if findings:
        print('rom_extract: %d findings' % len(findings))
        for f in findings:
            print('  ' + f)
        return 1
    if args.write:
        with open(args.write, 'w', encoding='utf-8', newline='\n') as f:
            f.write(src)
        print('wrote %s (table sha256 %s)' % (args.write, table_digest(table)))
    print('rom_extract: clean')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description='Reads Table 4 (ROM A00) of the HD44780U datasheet; writes or checks rom_hd44780.py')
    ap.add_argument('pdf')
    ap.add_argument('--render-dir')
    ap.add_argument('--write', metavar='FILE')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--controls', action='store_true', help='also put one fault at a time into each instrument and require the checks to see it')
    args = ap.parse_args(argv)
    try:
        return run(args)
    except Unreadable as e:
        print('rom_extract: cannot read: %s' % e, file=sys.stderr)
        return 2
    except (OSError, subprocess.CalledProcessError) as e:
        print('rom_extract: cannot run: %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
