#!/usr/bin/env python3
"""test_fonte.py -- proves the shape of tools/lcd_alpha/fonte_x11.py, the glyph table of the alpha's live LCD view (codes 0x20 to 0x7F, 5 x 8 dots, from the
X.Org misc-fixed 5x7 font, public domain), that fonte_extract.py reads the font files right, and that the checks bite (2026-11-04).

What is held, on the real module (standard library and Pillow only; no font file is needed, so CI runs all of it):
  * count      exactly the 96 codes 0x20 to 0x7F;
  * rows       each glyph is 8 rows, and range: each row an int from 0 to 31 (five dots; a bool is refused);
  * row7       row 7 is 0 in every glyph: it is the cursor row of the 5 x 8 cell, and the font's box is 7 rows;
  * header     the module names its source: the package and its version, the COPYRIGHT sentence of the font ("Public domain font.  Share and enjoy.", which
               must be exactly that), the path, the gz's sha256 and the sha256 of the decompressed PCF of both files, the date read, the METHOD, the table's own
               digest, and the three substitutions (0x5C, 0x7E, 0x7F) with their code points; the docstring carries them; and the header equals what
               fonte_extract.py pins, so the tool and the module cannot be edited apart;
  * pin        the sha256 of the table as text equals TABLE_SHA256. A change detector and nothing more: it says the table is the one that
               fonte_extract.py --check proved equal to the font files on 2026-11-04, so a hand edit or a regeneration from other files shows;
  * golden     14 glyphs, TYPED BY HAND from the font on 2026-11-04, BEFORE fonte_x11.py existed (the first run of this file, with no module, was at
               21:34:33; git: the module is committed after this file and after fonte_extract.py's reader): A 0 g _ and the three substitutions (the yen sign at 0x5C, the right arrow at 0x7E, the left arrow
               at 0x7F), and the space ! @ W j { |. The sources were three readers that agree: the reader of fonte_extract.py (its dump), FreeType
               through Pillow, and the hex rows of the font's own source, the upstream 5x7.bdf (v1.38). The font's 'g' really is .###. #..#. .##.. #.... .###.;
  * substitution  0x5C is not the font's backslash and 0x7E is not its tilde (the HD44780's ROM shows a yen sign and a right arrow there);
  * blank, distinct, stem  the space is blank and the other 95 are not; no two glyphs are equal; the capitals B D E F H K L P R have a stem down the left side,
               bit 4 set in rows 0 to 5 (the font's capitals are 6 rows tall): the orientation anchor for the 9 glyphs the golden does not hold.
  * drawing    lcd.desenhar( ) draws the 96 codes, in three frames, with this font: no error, the lit pixels are 36 for each lit dot, and the dots of
               A g and the three substitutions are where the golden says.

And on the reader (the extractor is judged here, not on a promise):
  * synthetic PCF files, written by the writer below from the format's documentation, in every layout the format allows (row padding 1, 2, 4, 8 bytes;
    scan unit 1, 2, 4 bytes; both byte orders and both bit orders; compressed and plain metrics; one- and two-byte encodings): fonte_extract.py's reader
    gives back the glyphs written, and FreeType (Pillow), a reader that shares no code with it, draws the same dots. The control is a file whose
    format word lies about the bit order or the byte order: the reader must NOT give the glyphs back. The real files use one layout only (the format
    0x0E: rows padded to 4 bytes, MSB first, scan unit 1); the other layouts are proved here and nowhere else;
  * the placement of a glyph in the 5 x 8 cell, with glyphs that do not fill the font's box (the real ones all do) and the refusals;
  * the command line, on synthetic font folders: --check with the font files not there says "skipped: the font files are not here" and exits 0, --write
    there exits 2, one file of the two missing exits 2, a COPYRIGHT that is not the public-domain sentence exits 2 and names it, a file that is not the pinned
    one exits 2;
  * and, where the fonts are installed (xfonts-base; skipped, and said, where they are not, as on CI's runner): --check on the real files exits 0 and says
    "byte for byte", the same with one row of the module changed exits 1, and --write gives the committed module back byte for byte.

The mutants: every check is run again on a copy of the table (or header) with one fault put in, and must report exactly the findings expected: a row shifted,
every glyph a row down (the descent row on the cursor row), a dot shifted right (centred), the descent row lost, a substitution missing, the bit order reversed,
a glyph changed, two glyphs swapped, a glyph of 7 rows, a row of 32, a dot on row 7, a code missing or extra, a header field missing, malformed, or edited apart
from the tool, the licence sentence altered, a stale pin. The expected sets say which check owns which fault: the pin alone sees a bit flipped in a letter
nothing else covers.

  python3 tools/lcd_alpha/test_fonte.py      exit 0 when every check passes and every mutant is caught, 1 otherwise
"""
import copy
import gzip
import hashlib
import itertools
import os
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)      # explicit: python3 -I leaves the script's own folder off the path
import fonte_extract as fx    # noqa: E402

try:
    import fonte_x11 as fonte     # noqa: E402
    NO_MODULE = None
except ImportError as e:
    fonte, NO_MODULE = None, e

CODES = range(0x20, 0x80)
HEADER_KEYS = ('PACKAGE', 'PACKAGE_VERSION', 'COPYRIGHT', 'LATIN1_FILE', 'LATIN1_GZ_SHA256', 'LATIN1_PCF_SHA256', 'UNICODE_FILE', 'UNICODE_GZ_SHA256',
               'UNICODE_PCF_SHA256', 'READ_ON', 'METHOD', 'TABLE_SHA256')
HEADER_FORMAT = {
    'PACKAGE': r'xfonts-base',
    'PACKAGE_VERSION': r'\d+:\d[\w.+~-]*',
    'COPYRIGHT': r'Public domain font\.  Share and enjoy\.',
    'LATIN1_FILE': r'/usr/share/fonts/X11/misc/5x7-ISO8859-1\.pcf\.gz',
    'LATIN1_GZ_SHA256': r'[0-9a-f]{64}',
    'LATIN1_PCF_SHA256': r'[0-9a-f]{64}',
    'UNICODE_FILE': r'/usr/share/fonts/X11/misc/5x7\.pcf\.gz',
    'UNICODE_GZ_SHA256': r'[0-9a-f]{64}',
    'UNICODE_PCF_SHA256': r'[0-9a-f]{64}',
    'READ_ON': r'\d{4}-\d{2}-\d{2}',
    'TABLE_SHA256': r'[0-9a-f]{64}',
}
IN_DOC = ('PACKAGE_VERSION', 'COPYRIGHT', 'LATIN1_GZ_SHA256', 'LATIN1_PCF_SHA256', 'UNICODE_GZ_SHA256', 'UNICODE_PCF_SHA256', 'READ_ON')
# What the header must equal in the tool that wrote it: the key, and where the tool keeps the value.
SAME_IN_TOOL = (('PACKAGE', lambda: fx.PACKAGE), ('PACKAGE_VERSION', lambda: fx.PACKAGE_VERSION), ('COPYRIGHT', lambda: fx.COPYRIGHT),
                ('READ_ON', lambda: fx.READ_ON), ('LATIN1_GZ_SHA256', lambda: fx.LATIN1['gz_sha256']), ('LATIN1_PCF_SHA256', lambda: fx.LATIN1['pcf_sha256']),
                ('UNICODE_GZ_SHA256', lambda: fx.UNICODE['gz_sha256']), ('UNICODE_PCF_SHA256', lambda: fx.UNICODE['pcf_sha256']))
SUBSTITUTED = {0x5C: 'U+00A5', 0x7E: 'U+2192', 0x7F: 'U+2190'}
STEM = 'BDEFHKLPR'        # capitals with a stem down the left side, in rows 0 to 5


def glyph(*rows):
    """8 rows of 5 characters, '#' a lit dot and the FIRST character the leftmost dot (bit 4), as 8 ints."""
    return tuple(int(r.replace('#', '1').replace('.', '0'), 2) for r in rows)


# The golden: the font's own glyphs as drawn, typed by hand (see the docstring). Row 7 is the cursor row: blank.
GOLDEN = {
    0x20: glyph('.....', '.....', '.....', '.....', '.....', '.....', '.....', '.....'),
    0x21: glyph('..#..', '..#..', '..#..', '..#..', '.....', '..#..', '.....', '.....'),
    0x30: glyph('..#..', '.#.#.', '.#.#.', '.#.#.', '.#.#.', '..#..', '.....', '.....'),
    0x40: glyph('.##..', '#..#.', '#.##.', '#.##.', '#....', '.##..', '.....', '.....'),
    0x41: glyph('.##..', '#..#.', '#..#.', '####.', '#..#.', '#..#.', '.....', '.....'),
    0x57: glyph('#..#.', '#..#.', '#..#.', '####.', '####.', '#..#.', '.....', '.....'),
    0x5C: glyph('.#.#.', '.#.#.', '..#..', '.###.', '..#..', '..#..', '.....', '.....'),      # the yen sign, U+00A5
    0x5F: glyph('.....', '.....', '.....', '.....', '.....', '####.', '.....', '.....'),
    0x67: glyph('.....', '.....', '.###.', '#..#.', '.##..', '#....', '.###.', '.....'),      # the descent row is used
    0x6A: glyph('...#.', '.....', '...#.', '...#.', '...#.', '.#.#.', '..#..', '.....'),
    0x7B: glyph('...#.', '..#..', '.##..', '..#..', '..#..', '...#.', '.....', '.....'),
    0x7C: glyph('..#..', '..#..', '..#..', '..#..', '..#..', '..#..', '.....', '.....'),
    0x7E: glyph('.....', '.....', '..#..', '####.', '..#..', '.....', '.....', '.....'),      # the right arrow, U+2192
    0x7F: glyph('.....', '.....', '.#...', '####.', '.#...', '.....', '.....', '.....'),      # the left arrow, U+2190
}
# What the font draws at the code points the HD44780's ROM shows something else at: not to be found at 0x5C and 0x7E.
BACKSLASH = glyph('.....', '#....', '.#...', '..#..', '...#.', '.....', '.....', '.....')
TILDE = glyph('.#.#.', '#.#..', '.....', '.....', '.....', '.....', '.....', '.....')


def mirror(v):
    return int(format(v, '05b')[::-1], 2)


def well_formed(rows):
    return isinstance(rows, (tuple, list)) and len(rows) == 8 and all(type(v) is int and 0 <= v <= 31 for v in rows)


def digest(table):
    text = ''.join('%02X:%s\n' % (c, ','.join('%02X' % v for v in table[c])) for c in sorted(table))
    return hashlib.sha256(text.encode('ascii')).hexdigest()


def findings(table, header, doc):
    """-> a list of (kind, detail). Empty when everything holds."""
    out = []
    codes = set(table)
    for c in sorted(set(CODES) - codes):
        out.append(('count', 'missing 0x%02X' % c))
    for c in sorted(codes - set(CODES)):
        out.append(('count', 'extra 0x%02X' % c))
    for c in sorted(codes & set(CODES)):
        rows = table[c]
        if not (isinstance(rows, (tuple, list)) and len(rows) == 8):
            out.append(('rows', '0x%02X' % c))
        elif not all(type(v) is int and 0 <= v <= 31 for v in rows):
            out.append(('range', '0x%02X' % c))
        elif rows[7] != 0:
            out.append(('row7', '0x%02X' % c))
    for key in HEADER_KEYS:
        if header.get(key) in (None, ''):
            out.append(('header', key))
        elif not re.fullmatch(HEADER_FORMAT.get(key, r'.+'), str(header[key])):
            out.append(('header', key))
        else:
            if key in IN_DOC and str(header[key]) not in doc:
                out.append(('header', 'doc ' + key))
    for key, tool in SAME_IN_TOOL:
        if header.get(key) not in (None, '') and header[key] != tool():
            out.append(('header', 'tool ' + key))
    for what in ['Method'] + ['0x%02X' % c for c in SUBSTITUTED] + list(SUBSTITUTED.values()):
        if what not in doc:
            out.append(('header', 'doc ' + what))
    if header.get('TABLE_SHA256') not in (None, '') and header['TABLE_SHA256'] != digest(table):
        out.append(('pin', ''))
    good = {c: tuple(table[c]) for c in sorted(codes & set(CODES)) if well_formed(table[c])}
    for c, rows in good.items():
        if (c == 0x20) == any(rows):
            out.append(('blank', '0x%02X' % c))
    seen = {}
    for c, rows in sorted(good.items()):
        if rows in seen:
            out.append(('distinct', '0x%02X = 0x%02X' % (seen[rows], c)))
        else:
            seen[rows] = c
    for ch in STEM:
        if ord(ch) in good and not all(good[ord(ch)][i] & 16 for i in range(6)):
            out.append(('stem', '0x%02X' % ord(ch)))
    for c, want in sorted(GOLDEN.items()):
        if c in good and good[c] != want:
            out.append(('golden', '0x%02X' % c))
    for c, original in ((0x5C, BACKSLASH), (0x7E, TILDE)):
        if c in good and good[c] == original:
            out.append(('substitution', '0x%02X' % c))
    return out


# -- the faults ---------------------------------------------------------------------------------------------------------------------------------------

def flip(code, row, bit):
    def f(t, h, d):
        t[code] = tuple(v ^ (1 << bit) if i == row else v for i, v in enumerate(t[code]))
    return f


def put(code, rows):
    def f(t, h, d):
        t[code] = rows
    return f


def every(fn):
    def f(t, h, d):
        for c in t:
            t[c] = fn(t[c])
    return f


def swap(a, b):
    def f(t, h, d):
        t[a], t[b] = t[b], t[a]
    return f


def drop_code(t, h, d):
    del t[0x41]


def extra_code(t, h, d):
    t[0x80] = (0,) * 8


def drop_header(key):
    def f(t, h, d):
        del h[key]
    return f


def set_header(key, value):
    def f(t, h, d):
        h[key] = value
    return f


def set_header_and_doc(key, value):
    def f(t, h, d):
        d[0] = d[0].replace(str(h[key]), str(value))
        h[key] = value
    return f


def strip_from_doc(key):
    def f(t, h, d):
        d[0] = d[0].replace(str(h[key]), '')
    return f


def down(rows):
    return (0,) + tuple(rows[:7])


def right(rows):
    return tuple(v >> 1 for v in rows)


def mirrored(rows):
    return tuple(mirror(v) for v in rows)


def no_descent(rows):
    return tuple(rows[:6]) + (0, 0)


def lit(codes):
    return {c for c in codes if any(GOLDEN[c])}


def stems():
    return {('stem', '0x%02X' % ord(ch)) for ch in STEM}


def golden_changed_by(fn):
    return {('golden', '0x%02X' % c) for c, g in GOLDEN.items() if fn(g) != g}


# Each mutant: what is put in, the function, the findings expected (exactly these), or a function of the clean table that gives them.
MUTANTS = [
    ("the A one row down: nothing but the golden and the pin see it", put(0x41, down(GOLDEN[0x41])), {('golden', '0x41'), ('pin', '')}),
    ("every glyph one row down (the descent row onto the cursor row): the golden, the cursor row, the stems and the pin", every(down),
     lambda t: {('pin', '')} | golden_changed_by(down) | stems() | {('row7', '0x%02X' % c) for c in t if t[c][6]}),
    ("the A one dot to the right (centred): the golden and the pin", put(0x41, right(GOLDEN[0x41])), {('golden', '0x41'), ('pin', '')}),
    ("every glyph one dot to the right: the golden, the stems and the pin", every(right), lambda t: {('pin', '')} | golden_changed_by(right) | stems()),
    ("the descent row lost in every glyph (the box read as 6 rows): the golden of the g and the j, and the pin", every(no_descent),
     lambda t: {('pin', '')} | golden_changed_by(no_descent)),
    ("0x5C left as the font's backslash (the yen sign missing)", put(0x5C, BACKSLASH), {('golden', '0x5C'), ('substitution', '0x5C'), ('pin', '')}),
    ("0x7E left as the font's tilde (the right arrow missing)", put(0x7E, TILDE), {('golden', '0x7E'), ('substitution', '0x7E'), ('pin', '')}),
    ("0x7F left blank (the font has no glyph there; the left arrow missing): it is the space twice", put(0x7F, (0,) * 8),
     {('golden', '0x7F'), ('blank', '0x7F'), ('distinct', '0x20 = 0x7F'), ('pin', '')}),
    ("the bit order reversed in every row: the golden, the stems and the pin", every(mirrored),
     lambda t: {('pin', '')} | golden_changed_by(mirrored) | stems()),
    ("one bit flipped in the S (row 3, bit 2): nothing but the pin sees it", flip(ord('S'), 3, 2), {('pin', '')}),
    ("one bit flipped in the g (row 2, bit 3): the golden and the pin", flip(0x67, 2, 3), {('golden', '0x67'), ('pin', '')}),
    ("the S and the 5 swapped: nothing but the pin sees it", swap(ord('S'), ord('5')), {('pin', '')}),
    ("a glyph of 7 rows", lambda t, h, d: t.__setitem__(0x41, tuple(t[0x41][:7])), {('rows', '0x41'), ('pin', '')}),
    ("a row of 32", lambda t, h, d: t.__setitem__(0x41, (32,) + tuple(t[0x41][1:])), {('range', '0x41'), ('pin', '')}),
    ("a dot on row 7 of the A", lambda t, h, d: t.__setitem__(0x41, tuple(t[0x41][:7]) + (0b00100,)), {('row7', '0x41'), ('golden', '0x41'), ('pin', '')}),
    ("the code 0x41 missing", drop_code, {('count', 'missing 0x41'), ('pin', '')}),
    ("a code 0x80 too many", extra_code, {('count', 'extra 0x80'), ('pin', '')}),
    ("the package's version missing from the header", drop_header('PACKAGE_VERSION'), {('header', 'PACKAGE_VERSION')}),
    ("the Unicode file's sha256 malformed", set_header('UNICODE_GZ_SHA256', 'not-a-hash'),
     {('header', 'UNICODE_GZ_SHA256'), ('header', 'tool UNICODE_GZ_SHA256')}),
    ("the Latin-1 file's sha256 edited in the header and the docstring, the tool not", set_header_and_doc('LATIN1_GZ_SHA256', '0' * 64),
     {('header', 'tool LATIN1_GZ_SHA256')}),
    ("the licence sentence altered ('enjoy!'), in the header and the docstring", set_header_and_doc('COPYRIGHT', 'Public domain font.  Share and enjoy!'),
     {('header', 'COPYRIGHT'), ('header', 'tool COPYRIGHT')}),
    ("the date missing from the docstring", strip_from_doc('READ_ON'), {('header', 'doc READ_ON')}),
    ("a stale pin", set_header('TABLE_SHA256', '0' * 64), {('pin', '')}),
]


# -- a PCF writer, for the reader's proof: written from the format's documentation, shares no code with fonte_extract.py's reader ------------------------

def pcf_bytes(glyphs, ascent, descent, copyright_text, pad=4, unit=1, msb_byte=True, msb_bit=True, compressed=True, two_byte=False,
              lie_bit=False, lie_byte=False):
    """A PCF file. glyphs: [(code, left side bearing, advance, ascent, [row strings of '#' and '.'])]; every row of a glyph has its width. With lie_bit or lie_byte
    the bitmaps are packed in the order given and the format word says the other one: the control that a reader obeys the word."""
    order = 'big' if msb_byte else 'little'
    code_of = {1: 0, 2: 1, 4: 2, 8: 3}
    fmt = code_of[pad] | (fx.BYTE_MASK if msb_byte != lie_byte else 0) | (fx.BIT_MASK if msb_bit != lie_bit else 0) | (code_of[unit] << 4)

    def i(v, n=4, signed=True):
        return v.to_bytes(n, order, signed=signed)

    def le(v):
        return v.to_bytes(4, 'little')

    def align(b):
        return b + bytes(-len(b) % 4)

    props = [('COPYRIGHT', copyright_text), ('PIXEL_SIZE', ascent + descent), ('CHARSET_REGISTRY', 'ISO10646' if two_byte else 'ISO8859'), ('CHARSET_ENCODING', '1')]
    strings, entries = b'', b''
    for name, val in props:
        at = len(strings)
        strings += name.encode() + b'\0'
        if isinstance(val, str):
            entries += i(at) + b'\x01' + i(len(strings))
            strings += val.encode('latin-1') + b'\0'
        else:
            entries += i(at) + b'\x00' + i(val)
    t_props = le(fmt) + i(len(props)) + entries
    t_props = align(t_props) + i(len(strings)) + strings
    t_props = align(t_props)

    def metric(g):
        _code, lsb, adv, asc, rows = g
        w = len(rows[0]) if rows else 0
        return lsb, lsb + w, adv, asc, len(rows) - asc

    ms = [metric(g) for g in glyphs]
    if compressed:
        t_met = le(fmt | fx.COMPRESSED_METRICS) + i(len(ms), 2) + b''.join(bytes(v + 0x80 for v in m) for m in ms)
    else:
        t_met = le(fmt) + i(len(ms)) + b''.join(b''.join(i(v, 2) for v in m) + i(0, 2, signed=False) for m in ms)
    t_met = align(t_met)

    def pack(rows):
        out = b''
        for r in rows:
            stride = (len(r) + 8 * pad - 1) // (8 * pad) * pad
            dots = [c == '#' for c in r] + [False] * (8 * stride - len(r))
            for u in range(0, stride, unit):
                value = 0
                for k in range(8 * unit):
                    if dots[8 * u + k]:
                        value |= 1 << (8 * unit - 1 - k) if msb_bit else 1 << k
                out += value.to_bytes(unit, order)
        return out

    blobs = [pack(g[4]) for g in glyphs]
    offsets, pos = [], 0
    for b in blobs:
        offsets.append(pos)
        pos += len(b)
    sizes = [sum(len(g[4]) * ((len(g[4][0]) + 8 * p - 1) // (8 * p) * p) for g in glyphs if g[4]) for p in (1, 2, 4, 8)]
    assert sizes[code_of[pad]] == pos
    t_bmp = align(le(fmt) + i(len(glyphs)) + b''.join(i(o) for o in offsets) + b''.join(i(s) for s in sizes) + b''.join(blobs))

    codes = [g[0] for g in glyphs]
    if two_byte:
        min1, max1, min2, max2 = min(c >> 8 for c in codes), max(c >> 8 for c in codes), 0, 255
    else:
        min1, max1, min2, max2 = 0, 0, min(codes), max(codes)
    width = max2 - min2 + 1
    index = [0xFFFF] * (width * (max1 - min1 + 1))
    for k, c in enumerate(codes):
        b1, b2 = (c >> 8, c & 255) if two_byte else (0, c)
        index[(b1 - min1) * width + (b2 - min2)] = k
    t_enc = align(le(fmt) + i(min2, 2) + i(max2, 2) + i(min1, 2) + i(max1, 2) + i(0, 2) + b''.join(i(v, 2, signed=False) for v in index))

    def bounds(fn):
        return b''.join(i(fn(m[k] for m in ms), 2) for k in range(5)) + i(0, 2, signed=False)

    t_acc = le(fmt | fx.ACCEL_W_INKBOUNDS) + bytes(8) + i(ascent) + i(descent) + i(0) + bounds(min) + bounds(max) + bounds(min) + bounds(max)
    # In ascending order of type, as in the real files: FreeType refuses an index in any other order (119 of the 120 orders of the 5 tables, measured
    # on 2026-11-04 with FreeType 2.14.2 on a real file's tables), though the format's documentation does not ask for it.
    tables = [(fx.PCF_PROPERTIES, fmt, t_props), (fx.PCF_METRICS, fmt | (fx.COMPRESSED_METRICS if compressed else 0), t_met), (fx.PCF_BITMAPS, fmt, t_bmp),
              (fx.PCF_BDF_ENCODINGS, fmt, t_enc), (fx.PCF_BDF_ACCELERATORS, fmt | fx.ACCEL_W_INKBOUNDS, t_acc)]
    head, body, at = b'\x01fcp' + le(len(tables)), b'', 8 + 16 * len(tables)
    for typ, f, blob in tables:
        head += le(typ) + le(f) + le(len(blob)) + le(at)
        body += blob
        at += len(blob)
    return head + body


# Glyphs that fill none of the box the way the real font's do: a bearing, a descent, a width of two and of three bytes, a negative bearing, an empty one.
SYNTH = [
    (0x41, 0, 6, 6, ['.###..', '#...#.', '#...#.', '#####.', '#...#.', '#...#.', '.....#']),
    (0x42, 1, 9, 5, ['#.#.#.#.#.#.#', '.#.#.#.#.#.#.', '###..###..###', '#..#..#..#..#', '#############']),
    (0x43, -1, 4, 3, ['#' * 20, '#' + '.' * 18 + '#', '.#.#.#.#.#.#.#.#.#.#']),
    (0x44, 0, 5, 0, []),
    (0xA5, 0, 5, 4, ['.#...', '####.', '.#...']),
]
SYNTH2 = SYNTH[:-1] + [(0x2190,) + SYNTH[-1][1:]]       # the two-byte layouts carry the glyph at U+2190, which a one-byte code cannot hold
SYNTH_FONT_BOX = (6, 1)


def layouts():
    for pad, unit in ((1, 1), (2, 1), (2, 2), (4, 1), (4, 2), (4, 4), (8, 1), (8, 2), (8, 4)):      # the scan unit of 8 is refused below
        for msb_byte, msb_bit, compressed, two_byte in itertools.product((True, False), repeat=4):
            yield dict(pad=pad, unit=unit, msb_byte=msb_byte, msb_bit=msb_bit, compressed=compressed, two_byte=two_byte)


def name_of(kw):
    return 'pad %d unit %d %sB %sb %s %s' % (kw['pad'], kw['unit'], 'MS' if kw['msb_byte'] else 'LS', 'MS' if kw['msb_bit'] else 'LS',
                                            'compressed' if kw['compressed'] else 'plain', 'two-byte' if kw['two_byte'] else 'one-byte')


def expected_dots(g):
    """The dots a glyph spec draws, as {(x, y)} from the glyph's origin and baseline, y down: what any reader must agree on."""
    _code, lsb, _adv, asc, rows = g
    return {(lsb + c, r - asc) for r, row in enumerate(rows) for c, ch in enumerate(row) if ch == '#'}


def reader_dots(font, g):
    idx = font.encoding[g[0]]
    lsb, _rsb, _adv, asc, _desc = font.metrics[idx]
    return {(lsb + c, r - asc) for r, row in enumerate(font.bitmaps[idx]) for c, bit in enumerate(row) if bit}


def freetype_dots(ft, g):
    from PIL import Image, ImageDraw
    img = Image.new('L', (80, 60), 0)
    ImageDraw.Draw(img).text((30, 30), chr(g[0]), font=ft, fill=255, anchor='ls')
    return {(x - 30, y - 30) for y in range(60) for x in range(80) if img.getpixel((x, y)) > 127}


def run_tool(*args):
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'fonte_extract.py')] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def write_fonts(folder, latin1=None, unicode_=None):
    for name, data in (('5x7-ISO8859-1.pcf.gz', latin1), ('5x7.pcf.gz', unicode_)):
        if data is not None:
            with open(os.path.join(folder, name), 'wb') as f:
                f.write(gzip.compress(data))


def main():
    fails = 0
    skipped = []

    def report(ok, text):
        nonlocal fails
        fails += 0 if ok else 1
        print('%s %s' % ('ok  ' if ok else 'FAIL', text))

    def skip(text):
        skipped.append(text)
        print('skip %s' % text)

    # -- the module -----------------------------------------------------------------------------------------------------------------------------------
    table = None
    if fonte is None:
        report(False, 'fonte_x11.py cannot be imported: %s' % NO_MODULE)
    else:
        table = dict(fonte.FONTE_5X7)
        header = {k: getattr(fonte, k, None) for k in HEADER_KEYS}
        doc = fonte.__doc__ or ''
        real = findings(table, header, doc)
        report(not real, 'the module: %d codes, 8 rows each, rows 0..31, row 7 blank, header, pin, golden, typography: %s'
               % (len(table), 'no findings' if not real else real))
        report(len(table) == 96 and min(table) == 0x20 and max(table) == 0x7F, 'the table holds 0x20..0x7F, 96 codes')
        report(sum(1 for c in table if table[c][7]) == 0, 'row 7 is blank in all 96 glyphs')
        empty = findings({}, header, doc)
        report(len([1 for k, _ in empty if k == 'count']) == 96, 'control: an empty table is reported, 96 codes missing, whatever else it says')
        for what, mutate, expected in MUTANTS:
            t, h, d = copy.deepcopy(table), dict(header), [doc]
            if callable(expected):
                expected = expected(table)
            mutate(t, h, d)
            got = findings(t, h, d[0])
            ok = set(got) == expected and len(got) == len(set(got))
            shown = ', '.join('%s%s' % (k, ' ' + dt if dt else '') for k, dt in sorted(expected))
            report(ok, 'mutant: %s -> %s' % (what, shown if len(shown) < 150 else '%d findings' % len(expected))
                   if ok else 'mutant: %s -> EXPECTED %s, GOT %s' % (what, sorted(expected), sorted(got)))
        report(set(GOLDEN) <= set(table) and len(GOLDEN) >= 6, 'golden: %d glyphs typed by hand, all codes of the table' % len(GOLDEN))

    # -- the drawing ----------------------------------------------------------------------------------------------------------------------------------
    if table is not None:
        try:
            import lcd
        except ImportError as e:
            report(False, 'lcd.py cannot be imported (Pillow?): %s' % e)
            lcd = None
        if lcd is not None:
            e = lcd.Estilo()
            blank = [bytes(8)] * 8
            drawn_ok, problems = 0, []
            for frame in range(3):
                rows = tuple(bytes(0x20 + 32 * frame + 16 * r + k for k in range(16)) for r in range(2))
                try:
                    img = lcd.desenhar(rows, blank, table, e)
                except ValueError as err:
                    problems.append('frame %d: %s' % (frame, err))
                    continue
                lit_px = dict((c, n) for n, c in img.getcolors(1 << 16)).get(e.aceso, 0)
                dots = sum(bin(v).count('1') for r in rows for code in r for v in table[code])
                if img.size != (639, 137) or lit_px != 36 * dots:
                    problems.append('frame %d: %s, %d lit pixels for %d dots' % (frame, img.size, lit_px, dots))
                else:
                    drawn_ok += 1
            report(drawn_ok == 3 and not problems, 'lcd.desenhar( ) draws the 96 codes in 3 frames, 639 x 137, 36 lit pixels for each lit dot %s' % problems)
            row = bytes([0x41, 0x67, 0x5C, 0x7E, 0x7F]) + b' ' * 11
            img = lcd.desenhar((row, b' ' * 16), blank, table, e)
            wrong = []
            for cell_no, code in enumerate(row[:5]):
                x0, y0 = 10 + 39 * cell_no, 10        # the cell's top-left pixel: 34 px wide, a gap of 5; a dot is 6 px with a gap of 1 (test_lcd.py)
                for i in range(8):
                    for j in range(5):
                        on = img.getpixel((x0 + 7 * j + 2, y0 + 7 * i + 2)) == e.aceso
                        if on != bool(GOLDEN[code][i] & (16 >> j)):
                            wrong.append('0x%02X dot (%d,%d)' % (code, i, j))
            report(not wrong, 'the drawn dots of A g and the three substitutions are the golden\'s, leftmost dot first %s' % wrong[:6])

    # -- the reader, on synthetic files ---------------------------------------------------------------------------------------------------------------
    n = ok_n = ft_n = ft_ok = 0
    bad, ft_refused, ft_bad = [], [], []
    try:
        from PIL import ImageFont
    except ImportError:
        ImageFont = None
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'synth.pcf')
        for kw in layouts():
            n += 1
            glyphs = SYNTH2 if kw['two_byte'] else SYNTH
            data = pcf_bytes(glyphs, *SYNTH_FONT_BOX, fx.COPYRIGHT, **kw)
            try:
                font = fx.Font(data)
                same = all(reader_dots(font, g) == expected_dots(g) for g in glyphs)
                same = same and font.accel['ascent'] == 6 and font.accel['descent'] == 1 and font.props['COPYRIGHT'] == fx.COPYRIGHT
                same = same and [font.metrics[font.encoding[g[0]]] for g in glyphs] == [(g[1], g[1] + (len(g[4][0]) if g[4] else 0), g[2], g[3], len(g[4]) - g[3])
                                                                                       for g in glyphs]
            except fx.Unreadable as err:
                same = False
                bad.append('%s: %s' % (name_of(kw), err))
            ok_n += 1 if same else 0
            if not same and len(bad) < 3:
                bad.append(name_of(kw))
            if ImageFont is not None:
                with open(path, 'wb') as f:
                    f.write(data)
                try:
                    ft = ImageFont.truetype(path, 7)
                except OSError:
                    ft_refused.append(name_of(kw))
                    continue
                ft_n += 1
                if all(freetype_dots(ft, g) == expected_dots(g) for g in glyphs):
                    ft_ok += 1
                elif len(ft_bad) < 3:
                    ft_bad.append(name_of(kw))
    report(ok_n == n and n == 144, 'the reader gives back the glyphs, the metrics, the font box and the COPYRIGHT of %d synthetic layouts (pad 1..8, unit 1..4, both orders, '
           'compressed or plain, one or two bytes) %s' % (n, bad[:3]))
    try:
        fx.Font(pcf_bytes(SYNTH, *SYNTH_FONT_BOX, fx.COPYRIGHT, pad=8, unit=8))
        refused = None
    except fx.Unreadable as err:
        refused = str(err)
    report(refused is not None and 'scan unit of 8' in refused, 'the reader refuses a scan unit of 8 bytes, which the format\'s documentation does not list (FreeType draws it wrong '
           'when the byte and bit orders differ: 8 of the 16 layouts with that unit, measured on 2026-11-04) -> %s' % refused)
    if ImageFont is None:
        report(False, 'Pillow is not installed: FreeType cannot be the second reader')
    else:
        report(ft_n >= 120 and ft_ok == ft_n, 'FreeType (Pillow), sharing no code with the reader, draws the same dots from %d of those layouts%s %s'
               % (ft_n, ' (it refused %d: %s)' % (len(ft_refused), ft_refused[0]) if ft_refused else '', ft_bad[:3]))
        report(ft_n + len(ft_refused) == 144, 'FreeType loaded or refused each of the 144 layouts, none went missing')
    # The control: a file whose format word lies must not be read as the glyphs written.
    lies = []
    for kw in layouts():
        if kw['unit'] != 1 or not kw['compressed'] or kw['two_byte']:
            continue
        for lie in ('lie_bit', 'lie_byte'):
            if lie == 'lie_byte' and kw['unit'] == 1:
                continue
            try:
                font = fx.Font(pcf_bytes(SYNTH, *SYNTH_FONT_BOX, fx.COPYRIGHT, **kw, **{lie: True}))
                if all(reader_dots(font, g) == expected_dots(g) for g in SYNTH):
                    lies.append('%s %s' % (name_of(kw), lie))
            except fx.Unreadable:
                pass
    report(not lies, 'control: a format word that lies about the bit order is never read as the glyphs written (%d layouts) %s' % (
        sum(1 for kw in layouts() if kw['unit'] == 1 and kw['compressed'] and not kw['two_byte']), lies[:3]))
    lies = []
    for kw in layouts():
        if kw['unit'] > 1 and kw['pad'] >= kw['unit'] and kw['compressed'] and not kw['two_byte']:
            try:
                font = fx.Font(pcf_bytes(SYNTH, *SYNTH_FONT_BOX, fx.COPYRIGHT, **kw, lie_byte=True))
                if all(reader_dots(font, g) == expected_dots(g) for g in SYNTH):
                    lies.append(name_of(kw))
            except fx.Unreadable:
                pass
    report(not lies, 'control: a format word that lies about the byte order, with a scan unit of 2 bytes or more, is never read as the glyphs written %s' % lies[:3])

    # -- the placement in the 5 x 8 cell, the refusals ------------------------------------------------------------------------------------------------
    cell_fn = getattr(fx, 'cell', None)

    def placement():
        small = [(0x41, 1, 5, 4, ['.#.', '###', '#.#', '#.#', '..#']),      # lsb 1, ascent 4, descent 1: rows 2..6 of the cell, columns 1..3
                 (0x42, 0, 5, 6, ['#....', '.....', '....#', '.....', '.....', '.....', '#...#'])]
        font = fx.Font(pcf_bytes(small, 6, 1, fx.COPYRIGHT))
        got = cell_fn(font, 0x41, 'test')
        want = glyph('.....', '.....', '..#..', '.###.', '.#.#.', '.#.#.', '...#.', '.....')
        report(got == want, 'a glyph that does not fill the box is placed by the font\'s metrics (lsb 1, ascent 4): rows 2..6, columns 1..3 %s' % (got,))
        got = cell_fn(font, 0x42, 'test')
        report(got == glyph('#....', '.....', '....#', '.....', '.....', '.....', '#...#', '.....'),
               'a glyph of the whole box keeps its corners: rows 0 and 6, columns 0 and 4 %s' % (got,))
        a_read = {p: cell_fn(font, p, 'test') for p in (0x41, 0x42)}
        try:
            b_read = fx.freetype_cells(pcf_bytes(small, 6, 1, fx.COPYRIGHT), [0x41, 0x42])
        except fx.Unreadable as err:
            b_read = {}
            report(False, 'read B (FreeType) cannot read the small test font: %s' % err)
        report(fx.compare_reads(a_read, b_read) == [] and b_read.get(0x41) == a_read[0x41], 'read B (FreeType) places the same two glyphs in the cell as read A')
        wrong_a = dict(a_read)
        wrong_a[0x42] = tuple(v ^ 0b00100 if i == 3 else v for i, v in enumerate(wrong_a[0x42]))
        report(fx.compare_reads(wrong_a, b_read) == [0x42], 'control: one dot changed in read A is reported by the comparison, for that code point and no other')
        report(fx.compare_reads(a_read, {**b_read, 0x41: None}) == [0x41], 'control: a code point that read B gives as ink outside the window is reported')

        def refuses(rows, lsb=0, adv=5, asc=6, box=(6, 1), code=0x41, ask=0x41):
            """-> the refusal's text, or None when the tool took the glyph; any other exception is not a refusal (the tool would die with a traceback)."""
            try:
                cell_fn(fx.Font(pcf_bytes([(code, lsb, adv, asc, rows)], *box, fx.COPYRIGHT)), ask, 'test')
            except fx.Unreadable as err:
                return str(err)
            except Exception as err:
                report(False, 'a glyph that should be refused ends in %s: %s' % (type(err).__name__, err))
            return None

        for what, args in (("a glyph taller than the box's ascent", dict(rows=['#'] * 8, asc=7)),
                           ("a glyph that goes below the box's descent", dict(rows=['#'] * 8, asc=6)),
                           ("a dot in column 5 (outside the cell)", dict(rows=['#....#'])),
                           ("a dot left of the cell (a negative bearing)", dict(rows=['##'], lsb=-1)),
                           ("an advance of 6, not the cell's 5", dict(rows=['#'], adv=6)),
                           ("a font box of 5 rows above and 2 below", dict(rows=['#'], asc=1, box=(5, 2))),
                           ("a code the font does not have", dict(rows=['#'], ask=0x42))):
            msg = refuses(**args)
            report(msg is not None, 'refusal (exit 2 in the tool): %s -> %s' % (what, msg))

    if cell_fn is None:
        report(False, 'fonte_extract.cell( ) does not exist yet')
    else:
        try:
            placement()
        except Exception as err:        # the tool under test dying is a failed check, not a crash of this test
            report(False, 'the placement checks stopped on %s: %s' % (type(err).__name__, err))

    # -- the generator's mapping, on synthetic fonts built from the table itself: it must give the table back ---------------------------------------------
    def generator():
        def rows7(rows):
            return [''.join('#' if v & (16 >> j) else '.' for j in range(5)) for v in rows[:7]]

        def ordinary(c):          # the font's own glyph at an ASCII code: the table's, except at the two codes where the table has what the HD44780 shows
            return {0x5C: BACKSLASH, 0x7E: TILDE}.get(c, table[c])

        l1 = [(c, 0, 5, 6, rows7(ordinary(c))) for c in range(0x20, 0x7F)] + [(0xA5, 0, 5, 6, rows7(table[0x5C]))]
        un = [(c, 0, 5, 6, rows7(ordinary(c))) for c in range(0x20, 0x7F)] + [(0x2190, 0, 5, 6, rows7(table[0x7F])), (0x2192, 0, 5, 6, rows7(table[0x7E]))]

        def font_of(specs, two_byte):
            return fx.Font(pcf_bytes(specs, 6, 1, fx.COPYRIGHT, two_byte=two_byte))

        t2, found = fx.build_table(font_of(l1, False), font_of(un, True))
        report(t2 == table and found == [], 'the generator, on two synthetic fonts built from the table (the font\'s backslash and tilde at 0x5C and 0x7E, the yen sign in the first, the '
               'arrows in the second), gives the table back: the 3 substitutions come from the right file and code point')
        bad_a = [(c, 0, 5, 6, ([('#' if r[0][0] == '.' else '.') + r[0][1:]] + r[1:]) if c == 0x41 else r) for c, _l, _a, _s, r in un]
        t3, found = fx.build_table(font_of(l1, False), font_of(bad_a, True))
        report(t3 == table and found == ['the two files give a different glyph for 0x41'],
               'control: a dot of the A changed in the second file only is reported, for 0x41 and no other code (%s)' % (found,))
        for what, specs_l1, specs_un, word in (('the arrow U+2190 missing from the Unicode file', l1, [g for g in un if g[0] != 0x2190], 'U+2190'),
                                               ('the yen sign U+00A5 missing from the Latin-1 file', [g for g in l1 if g[0] != 0xA5], un, 'U+00A5')):
            try:
                fx.build_table(font_of(specs_l1, False), font_of(specs_un, True))
                msg = None
            except fx.Unreadable as err:
                msg = str(err)
            report(msg is not None and word in msg, 'refusal (exit 2 in the tool): %s -> %s' % (what, msg))

    if table is not None and getattr(fx, 'build_table', None) is not None:
        try:
            generator()
        except Exception as err:
            report(False, 'the generator checks stopped on %s: %s' % (type(err).__name__, err))

    # -- the command line, on synthetic font folders ---------------------------------------------------------------------------------------------------
    if not os.path.exists(os.path.join(HERE, 'fonte_extract.py')):
        report(False, 'fonte_extract.py is missing')
    else:
        with tempfile.TemporaryDirectory() as tmp:
            nowhere = os.path.join(tmp, 'nofonts')
            os.mkdir(nowhere)
            out = os.path.join(tmp, 'out.py')
            rc, so, se = run_tool('--check', '--font-dir', nowhere)
            report(rc == 0 and 'skipped: the font files are not here' in so, 'CLI: --check with the font files not there says "skipped: the font files are not here" and exits 0 (exit %d)' % rc)
            rc, so, se = run_tool('--write', out, '--font-dir', nowhere)
            report(rc == 2 and not os.path.exists(out), 'CLI: --write with the font files not there exits 2 and writes nothing (exit %d)' % rc)
            good = pcf_bytes(SYNTH, *SYNTH_FONT_BOX, fx.COPYRIGHT)
            one = os.path.join(tmp, 'one')
            os.mkdir(one)
            write_fonts(one, latin1=good)
            rc, so, se = run_tool('--check', '--font-dir', one)
            report(rc == 2 and '5x7.pcf.gz' in se, 'CLI: --check with one of the two files missing exits 2 and names it (exit %d)' % rc)
            wrong = os.path.join(tmp, 'wrong')
            os.mkdir(wrong)
            bad_text = pcf_bytes(SYNTH, *SYNTH_FONT_BOX, 'Public domain font.  Share and enjoy!')
            write_fonts(wrong, latin1=bad_text, unicode_=bad_text)
            for flag in ('--check', '--write'):
                args = (flag, out) if flag == '--write' else (flag,)
                rc, so, se = run_tool(*args, '--font-dir', wrong)
                report(rc == 2 and 'COPYRIGHT' in se and not os.path.exists(out), 'CLI: %s with a COPYRIGHT that is not the public-domain sentence exits 2 and names it (exit %d)' % (flag, rc))
            other = os.path.join(tmp, 'other')
            os.mkdir(other)
            write_fonts(other, latin1=good, unicode_=good)
            for flag in ('--check', '--write'):
                args = (flag, out) if flag == '--write' else (flag,)
                rc, so, se = run_tool(*args, '--font-dir', other)
                report(rc == 2 and 'pinned' in se and not os.path.exists(out), 'CLI: %s with the right COPYRIGHT and files that are not the pinned ones exits 2 (exit %d)' % (flag, rc))

        # -- the real fonts, where they are installed ---------------------------------------------------------------------------------------------------
        have = all(os.path.exists(os.path.join(fx.FONT_DIR, d['name'])) for d in (fx.LATIN1, fx.UNICODE))
        if not have:
            skip('the real font files: the font files are not here (%s); only fonte_extract.py --check on them is skipped, the module is held above' % fx.FONT_DIR)
        elif fonte is None:
            report(False, 'the real font files are here but fonte_x11.py cannot be imported')
        else:
            rc, so, se = run_tool('--check')
            report(rc == 0 and 'byte for byte' in so, 'CLI, real fonts: --check exits 0 and says "byte for byte" (exit %d) %s' % (rc, (so + se).strip().splitlines()[-1:]))
            with tempfile.TemporaryDirectory() as tmp:
                module_path = os.path.join(HERE, 'fonte_x11.py')
                with open(module_path, encoding='utf-8') as f:
                    text = f.read()
                tampered = os.path.join(tmp, 'fonte_x11.py')
                row_a = ', '.join('0b%s' % format(v, '05b') for v in GOLDEN[0x41])
                with open(tampered, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text.replace(row_a, row_a.replace('0b01100', '0b00110', 1), 1))
                rc, so, se = run_tool('--check', '--module', tampered)
                report(text != open(tampered, encoding='utf-8').read() and rc == 1 and 'differs' in so,
                       'CLI, real fonts: control, --check with one row of the A changed in a copy of the module exits 1 (exit %d)' % rc)
                regen = os.path.join(tmp, 'regen.py')
                rc, so, se = run_tool('--write', regen)
                same = rc == 0 and os.path.exists(regen) and open(regen, 'rb').read() == open(module_path, 'rb').read()
                report(same, 'CLI, real fonts: --write gives the committed fonte_x11.py back, byte for byte (exit %d)' % rc)

    print('test_fonte: %s%s' % ('all hold' if not fails else '%d failed' % fails, ', %d skipped (%s)' % (len(skipped), '; '.join(skipped)) if skipped else ''))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
