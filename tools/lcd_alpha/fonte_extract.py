#!/usr/bin/env python3
"""fonte_extract.py -- reads the X.Org misc-fixed 5x7 bitmap font (public domain) from the two PCF files of the xfonts-base package and writes or checks
fonte_x11.py, the glyph table of the alpha's live LCD view (codes 0x20 to 0x7F, 5 x 8 dots).

The maintainer decided on 2026-11-04 that the table shipped in the firmware images is an openly licensed font and not the HD44780 ROM's
(rom_hd44780.py stays in the repository for the manual's figures only). The glyphs are not typed. The tool reads the two files with its own PCF reader,
written from the format's documentation (https://fontforge.org/docs/techref/pcf-format.html, read on 2026-11-04), and it reads them twice, by readers that
share no code:

  A  fonte_extract.py's own reader: properties, accelerators (the font's ascent and descent), metrics, bitmaps in any padding, scan unit, byte order and
     bit order, and the BDF encodings (the arrows are at U+2190 and U+2192, past the 256 codes Pillow's own PCF reader covers). A glyph goes into the 5 x 8
     cell where its metrics put it; row 7 is the cursor row and stays 0.
  B  FreeType, through Pillow's ImageFont, drawing the same decompressed bytes: the 5 x 8 window of the origin and the baseline of every code.

They must agree on all 96 codes. The two files must also give the same glyph for the 95 codes 0x20 to 0x7E, and every glyph used must lie inside the font's
box (6 rows above the baseline, 1 below, 5 dots wide), as the files' own metrics say.

Three codes take another character, the one the HD44780 shows there: 0x5C the yen sign (U+00A5, from the Latin-1 file), 0x7E the right arrow (U+2192) and
0x7F the left arrow (U+2190), both from the Unicode file. Every other code takes the font's glyph of the same code point.

  fonte_extract.py [--font-dir DIR] [--module FILE] [--write FILE | --check] [--dump]

  --font-dir  where the two files are. Default: /usr/share/fonts/X11/misc, where xfonts-base installs them.
  --write     writes the module to FILE when both reads agree and every check holds.
  --check     generates the module text from the fonts and compares it, byte for byte, with --module (default: fonte_x11.py beside this file). Where
              NEITHER font file is installed (CI's runner has no xfonts-base) it says "skipped: the font files are not here" and exits 0: nothing was
              compared, and it says so.
  --dump      prints the 96 glyphs as pictures.

The run refuses (exit 2) a font file that is missing, whose COPYRIGHT property is not the public-domain sentence "Public domain font.  Share and enjoy.",
or that is not the pinned file (the sizes and sha256 of the .gz and of the decompressed PCF below, measured on 2026-11-04); that the package xfonts-base is
not at the version recorded (--write only; --check notes it, the sha256 of the files being the identity); and a glyph that is missing or does not fit.
Exit 0 clean; 1 findings (the reads disagree, or the module differs from the fonts). The fonts are downloaded data: run this with python3 -I. Needs
Pillow, and dpkg-query for the package's version.
"""
import argparse
import gzip
import hashlib
import os
import subprocess
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
MODULE = os.path.join(HERE, 'fonte_x11.py')

# The source. The run refuses any other file: the checks below were written for these bytes, measured on 2026-11-04 (dpkg-query -W xfonts-base; sha256sum
# of the .gz; zcat | sha256sum for the PCF).
PACKAGE = 'xfonts-base'
PACKAGE_VERSION = '1:1.0.5+nmu1build1'
COPYRIGHT = 'Public domain font.  Share and enjoy.'
READ_ON = '2026-11-04'
FONT_DIR = '/usr/share/fonts/X11/misc'
LATIN1 = {'name': '5x7-ISO8859-1.pcf.gz', 'gz_bytes': 4105, 'pcf_bytes': 14236,
          'gz_sha256': '6d76523f18b61d0f4ed37621050462015295ddc3c859771a213e693e039d8b9f',
          'pcf_sha256': 'cf398fec37bb73b20c84cc71e26023b5959a5566109cbb2eead73c359d9ce0fa'}
UNICODE = {'name': '5x7.pcf.gz', 'gz_bytes': 27614, 'pcf_bytes': 239800,
           'gz_sha256': 'c017815a95d9aa4b43766b4619b89517f1bc9a0cd74a1acc5b9d4165cdecd3d4',
           'pcf_sha256': '0086c36e87169a2312033e9d9e9888301c2bab3c3b357b1279871792ddc3dba3'}
METHOD = ('two reads compared on all 96 codes: fonte_extract.py\'s own PCF reader, and FreeType through Pillow drawing the same bytes; '
          'the two files compared on 0x20 to 0x7E (fonte_extract.py)')

CELL_W = 5                      # the HD44780 cell is 5 dots wide
BOX_ASCENT, BOX_DESCENT = 6, 1  # the font's box, as its accelerator table says: 7 rows, which leave row 7 of the 5 x 8 cell for the cursor
# The codes where the HD44780's ROM shows another character: code -> (code point in the font, which file, name, what the font has at that code).
SUBSTITUTIONS = {0x5C: (0xA5, LATIN1, 'yen sign', 'the font\'s own 0x5C is the backslash'),
                 0x7E: (0x2192, UNICODE, 'right arrow', 'the font\'s own 0x7E is the tilde'),
                 0x7F: (0x2190, UNICODE, 'left arrow', 'the font has no glyph at 0x7F')}

# -- the PCF format -------------------------------------------------------------------------------------------------------------------------------
# Written from the FontForge documentation of the format (https://fontforge.org/docs/techref/pcf-format.html, read on 2026-11-04) and nothing else.

PCF_PROPERTIES, PCF_ACCELERATORS, PCF_METRICS, PCF_BITMAPS, PCF_INK_METRICS, PCF_BDF_ENCODINGS, PCF_SWIDTHS, PCF_GLYPH_NAMES, PCF_BDF_ACCELERATORS = \
    (1 << n for n in range(9))
PAD_MASK, BYTE_MASK, BIT_MASK, UNIT_MASK = 3, 1 << 2, 1 << 3, 3 << 4
COMPRESSED_METRICS = ACCEL_W_INKBOUNDS = 1 << 8


class Unreadable(Exception):
    """The file is not what the reader expects. A refusal (exit 2), never a guess."""


def _int(data, pos, size, order, signed=False):
    if pos < 0 or pos + size > len(data):
        raise Unreadable('the file ends inside a table (offset %d)' % pos)
    return int.from_bytes(data[pos:pos + size], order, signed=signed)


def _order(fmt):
    return 'big' if fmt & BYTE_MASK else 'little'


def read_toc(data):
    """-> {table type: (format, offset, size)}"""
    if data[:4] != b'\x01fcp':
        raise Unreadable('not a PCF file: the first four bytes are %r' % data[:4])
    tables = {}
    for k in range(_int(data, 4, 4, 'little')):
        typ, fmt, size, off = (_int(data, 8 + 16 * k + 4 * m, 4, 'little') for m in range(4))
        if typ in tables:
            raise Unreadable('table type %d twice' % typ)
        # The declared extent is NOT required to lie inside the file: both real files declare their last table (the BDF accelerators) 100 bytes
        # long, 72 of content and 28 of zero padding, and end 28 bytes short of it (measured on 2026-11-04). What each reader needs is checked where
        # it reads: _int( ) refuses a read past the end of the file, and the bitmap data is checked against the file and the table.
        if _int(data, off, 4, 'little') != fmt:
            raise Unreadable('table type %d: the format in the contents is not the one in the index' % typ)
        tables[typ] = (fmt, off, size)
    return tables


def read_properties(data, fmt, off, size):
    order = _order(fmt)
    n = _int(data, off + 4, 4, order)
    pos, raw = off + 8, []
    for _ in range(n):
        raw.append((_int(data, pos, 4, order), _int(data, pos + 4, 1, order), _int(data, pos + 5, 4, order, signed=True)))
        pos += 9
    pos += -(pos - off) % 4
    ssize = _int(data, pos, 4, order)
    strings = data[pos + 4:pos + 4 + ssize]
    if len(strings) != ssize:
        raise Unreadable('the properties\' string table is cut short')

    def text(at):
        end = strings.find(b'\0', at)
        if at < 0 or end < 0:
            raise Unreadable('a property string that never ends')
        return strings[at:end].decode('latin-1')

    return {text(name): (text(value) if is_string else value) for name, is_string, value in raw}


def read_metrics(data, fmt, off, size):
    """-> [(left side bearing, right side bearing, width, ascent, descent)] for every glyph, in glyph order."""
    order = _order(fmt)
    out = []
    if fmt & COMPRESSED_METRICS:
        n = _int(data, off + 4, 2, order, signed=True)
        for k in range(n):
            pos = off + 6 + 5 * k
            if pos + 5 > len(data):
                raise Unreadable('the metrics run past the end of the file')
            out.append(tuple(b - 0x80 for b in data[pos:pos + 5]))
    else:
        n = _int(data, off + 4, 4, order)
        for k in range(n):
            pos = off + 8 + 12 * k
            out.append(tuple(_int(data, pos + 2 * m, 2, order, signed=True) for m in range(5)))
    return out


def read_accelerators(data, fmt, off, size):
    """-> the font-wide facts: ascent, descent, constant_width, terminal, and the two bounding metrics. The metrics inside are always the plain ones (12
    bytes), whatever the format word says: its 0x100 bit means "with ink bounds" here, and "compressed" in the metrics table."""
    order = _order(fmt)
    flags = data[off + 4:off + 12]
    if len(flags) != 8 or size < 48:
        raise Unreadable('the accelerator table is cut short')

    def metric(pos):
        return tuple(_int(data, pos + 2 * m, 2, order, signed=True) for m in range(5))

    return {'terminal': flags[2], 'constant_width': flags[3],
            'ascent': _int(data, off + 12, 4, order, signed=True), 'descent': _int(data, off + 16, 4, order, signed=True),
            'minbounds': metric(off + 24), 'maxbounds': metric(off + 36)}


def unpack_row(row, width, unit, order, msb_first):
    """The dots of one stored row, leftmost first: the row is cut in scan units of `unit` bytes, each read as one integer in the table's byte
    order, and the leftmost dot of a unit is its most significant bit when the bit order is MSB first and its least significant when not."""
    bits = []
    for u in range(0, len(row), unit):
        value = int.from_bytes(row[u:u + unit], order)
        n = 8 * unit
        bits += [(value >> (n - 1 - i)) & 1 if msb_first else (value >> i) & 1 for i in range(n)]
    if any(bits[width:]):
        raise Unreadable('ink in the padding of a row: the byte or bit order is misread')
    return bits[:width]


def read_bitmaps(data, fmt, off, size, metrics):
    """-> a list, one entry for each glyph, of its rows (top first), each a list of 0 and 1 of the glyph's width (right - left side bearing)."""
    order = _order(fmt)
    n = _int(data, off + 4, 4, order)
    if n != len(metrics):
        raise Unreadable('%d bitmaps for %d metrics' % (n, len(metrics)))
    offsets = [_int(data, off + 8 + 4 * k, 4, order) for k in range(n)]
    sizes_at = off + 8 + 4 * n
    total = _int(data, sizes_at + 4 * (fmt & PAD_MASK), 4, order)
    base = sizes_at + 16
    if base + total > off + size or base + total > len(data):
        raise Unreadable('the bitmap data runs past its table or past the end of the file')
    pad, unit = 1 << (fmt & PAD_MASK), 1 << ((fmt & UNIT_MASK) >> 4)
    if unit == 8:
        raise Unreadable('a scan unit of 8 bytes: the format\'s documentation lists 1, 2 and 4')
    if unit > pad:
        raise Unreadable('a scan unit of %d bytes in rows padded to %d: not supported' % (unit, pad))
    out = []
    for (lsb, rsb, _width, asc, desc), at in zip(metrics, offsets):
        w, h = rsb - lsb, asc + desc
        if w < 0 or h < 0:
            raise Unreadable('a glyph of %d x %d dots' % (w, h))
        stride = (w + 8 * pad - 1) // (8 * pad) * pad
        if at + h * stride > total:
            raise Unreadable('a glyph\'s bitmap runs past the bitmap data')
        out.append([unpack_row(data[base + at + r * stride:base + at + (r + 1) * stride], w, unit, order, bool(fmt & BIT_MASK)) for r in range(h)])
    return out


def read_encodings(data, fmt, off, size):
    """-> {code: glyph index}; the code of a two-byte font is byte 1 * 256 + byte 2 (U+2190 is 0x21, 0x90)."""
    order = _order(fmt)
    min2, max2, min1, max1, _default = (_int(data, off + 4 + 2 * k, 2, order, signed=True) for k in range(5))
    width = max2 - min2 + 1
    out = {}
    for b1 in range(min1, max1 + 1):
        for b2 in range(min2, max2 + 1):
            g = _int(data, off + 14 + 2 * ((b1 - min1) * width + (b2 - min2)), 2, order)
            if g != 0xFFFF:
                out[b1 * 256 + b2] = g
    return out


class Font:
    """A PCF file read: .props, .metrics, .bitmaps, .encoding, .accel (the BDF accelerators when the file has them, as the format says)."""

    def __init__(self, data):
        toc = read_toc(data)
        for need in (PCF_PROPERTIES, PCF_METRICS, PCF_BITMAPS, PCF_BDF_ENCODINGS):
            if need not in toc:
                raise Unreadable('the file has no table of type %d' % need)
        accel = PCF_BDF_ACCELERATORS if PCF_BDF_ACCELERATORS in toc else PCF_ACCELERATORS
        if accel not in toc:
            raise Unreadable('the file has no accelerator table')
        self.props = read_properties(data, *toc[PCF_PROPERTIES])
        self.metrics = read_metrics(data, *toc[PCF_METRICS])
        self.bitmaps = read_bitmaps(data, *toc[PCF_BITMAPS], self.metrics)
        self.encoding = read_encodings(data, *toc[PCF_BDF_ENCODINGS])
        self.accel = read_accelerators(data, *toc[accel])
        self.formats = {t: toc[t][0] for t in toc}


# -- the glyphs -----------------------------------------------------------------------------------------------------------------------------------

def cell(font, code, label):
    """Read A. The 5 x 8 cell of a code point: 8 ints, element 0 the top row, bit 4 the leftmost dot. The glyph is put where its own metrics put it: its
    top row is `font ascent - glyph ascent` rows below the top of the box, and its dot c of a row at column `left side bearing + c` of the cell. Nothing is
    centred. Raises Unreadable when the font's box is not 6 rows above the baseline and 1 below, when the glyph is missing, does not advance by the cell's
    5 dots, does not lie inside the box, or has a dot outside the cell: a dot cut off would be a glyph drawn wrong."""
    ascent, descent = font.accel['ascent'], font.accel['descent']
    if (ascent, descent) != (BOX_ASCENT, BOX_DESCENT):
        raise Unreadable('%s: the font box is %d rows above the baseline and %d below, not %d and %d' % (label, ascent, descent, BOX_ASCENT, BOX_DESCENT))
    for key, want in (('FONT_ASCENT', ascent), ('FONT_DESCENT', descent)):
        if key in font.props and font.props[key] != want:
            raise Unreadable('%s: %s is %s but the accelerator table says %d' % (label, key, font.props[key], want))
    i = font.encoding.get(code)
    if i is None:
        raise Unreadable('%s has no glyph for U+%04X' % (label, code))
    lsb, _rsb, advance, g_ascent, _g_descent = font.metrics[i]
    rows = font.bitmaps[i]
    if advance != CELL_W:
        raise Unreadable('%s, U+%04X: it advances %d dots, not the cell\'s %d' % (label, code, advance, CELL_W))
    top = ascent - g_ascent
    if top < 0 or top + len(rows) > ascent + descent:
        raise Unreadable('%s, U+%04X: the glyph does not lie inside the font box' % (label, code))
    out = [0] * 8
    for r, bits in enumerate(rows):
        for c, bit in enumerate(bits):
            if bit:
                x = lsb + c
                if not 0 <= x < CELL_W:
                    raise Unreadable('%s, U+%04X: a dot at column %d, outside the %d-dot cell' % (label, code, x, CELL_W))
                out[top + r] |= 0x10 >> x
    return tuple(out)


def freetype_cells(pcf, points):
    """Read B. The same decompressed bytes, drawn by FreeType through Pillow: the 5 x 8 window under the origin and the baseline of each code point
    (rows from the box's top, six above the baseline, to one row under the font's descent). -> {code point: 8 ints}; None for a code point that
    puts ink outside the window."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        raise Unreadable('Pillow is not installed: FreeType cannot be the second reader')
    ox, oy = 12, 20
    window = {(ox + c, oy - BOX_ASCENT + r) for c in range(CELL_W) for r in range(8)}
    out = {}
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, 'font.pcf')
        with open(path, 'wb') as f:
            f.write(pcf)
        try:
            ft = ImageFont.truetype(path, BOX_ASCENT + BOX_DESCENT)
        except OSError as e:
            raise Unreadable('FreeType cannot open the font: %s' % e)
        for p in points:
            img = Image.new('L', (40, 40), 0)
            ImageDraw.Draw(img).text((ox, oy), chr(p), font=ft, fill=255, anchor='ls')
            lit = {(x, y) for y in range(40) for x in range(40) if img.getpixel((x, y)) > 127}
            if lit - window:
                out[p] = None
                continue
            out[p] = tuple(sum(0x10 >> c for c in range(CELL_W) if (ox + c, oy - BOX_ASCENT + r) in lit) for r in range(8))
    return out


def compare_reads(a, b):
    """a and b are {code point: 8 rows, or None} from read A and read B. -> the code points where they differ, sorted."""
    return sorted(p for p in a if a[p] != b.get(p))


def table_digest(table):
    text = ''.join('%02X:%s\n' % (c, ','.join('%02X' % v for v in table[c])) for c in sorted(table))
    return hashlib.sha256(text.encode('ascii')).hexdigest()


def build_table(latin1, unicode_):
    """-> (table, findings): code -> 8 rows for 0x20 to 0x7F, and the codes where the two files disagree."""
    files = {id(LATIN1): (latin1, 'the Latin-1 file'), id(UNICODE): (unicode_, 'the Unicode file')}
    table, findings = {}, []
    for code in range(0x20, 0x80):
        point, spec = (SUBSTITUTIONS[code][:2]) if code in SUBSTITUTIONS else (code, LATIN1)
        font, label = files[id(spec)]
        table[code] = cell(font, point, label)
    for code in range(0x20, 0x7F):
        a, b = cell(latin1, code, 'the Latin-1 file'), cell(unicode_, code, 'the Unicode file')
        if a != b:
            findings.append('the two files give a different glyph for 0x%02X' % code)
    return table, findings


# -- the module -----------------------------------------------------------------------------------------------------------------------------------

def label_of(code):
    return '%s (U+%04X, from the %s file)' % (SUBSTITUTIONS[code][2], SUBSTITUTIONS[code][0], 'Latin-1' if SUBSTITUTIONS[code][1] is LATIN1 else 'Unicode') \
        if code in SUBSTITUTIONS else repr(chr(code))


def module_source(table):
    reach = [c for c in sorted(table) if any(v & 1 for v in table[c])]        # the glyphs with a dot in column 4, the cell's last
    letters = [c for c in reach if chr(c).isalnum()]
    shown = ' '.join(chr(c) for c in reach)
    from_unicode = [c for c in sorted(SUBSTITUTIONS) if SUBSTITUTIONS[c][1] is UNICODE]
    out = ['"""The glyph table of the alpha\'s live LCD view: codes 0x20 to 0x7F, 5 x 8 dots, from the X.Org misc-fixed 5x7 font (public domain).',
           'GENERATED by tools/lcd_alpha/fonte_extract.py: never edit by hand.',
           '',
           'Package     %s %s (dpkg-query -W), read on %s' % (PACKAGE, PACKAGE_VERSION, READ_ON),
           'Files       /usr/share/fonts/X11/misc/%s: %d bytes, sha256 %s;' % (LATIN1['name'], LATIN1['gz_bytes'], LATIN1['gz_sha256']),
           '            decompressed %d bytes, sha256 %s.' % (LATIN1['pcf_bytes'], LATIN1['pcf_sha256']),
           '            The font in ISO 8859-1: %d of the 96 codes (%d ordinary ones and the yen sign).' % (96 - len(from_unicode), 95 - len(SUBSTITUTIONS) + 1),
           '            /usr/share/fonts/X11/misc/%s: %d bytes, sha256 %s;' % (UNICODE['name'], UNICODE['gz_bytes'], UNICODE['gz_sha256']),
           '            decompressed %d bytes, sha256 %s.' % (UNICODE['pcf_bytes'], UNICODE['pcf_sha256']),
           '            The font in ISO 10646-1: the %d arrows (U+2190 and U+2192 are past the 256 codes of the first file).' % len(from_unicode),
           'Read on     ' + READ_ON,
           'Licence     Public domain. Both files carry the property COPYRIGHT "%s" (fonte_extract.py refuses a file whose property is' % COPYRIGHT,
           '            anything else), and the package\'s copyright file, /usr/share/doc/xfonts-base/copyright, says the same for font-misc-misc. The',
           '            maintainer decided on 2026-11-04 that the table shipped in the firmware images is an openly licensed font and not the HD44780\'s ROM',
           '            (rom_hd44780.py stays for the manual\'s figures only). The shapes here are the font\'s; only which character sits at three',
           '            codes follows the HD44780\'s character set (Substituted, below).',
           'Method      Not typed. The glyphs are read twice, by readers that share no code, and the reads must agree on all 96 codes. Read A:',
           '            fonte_extract.py\'s own PCF reader, written from the format\'s documentation, each glyph put in the 5 x 8 cell by its own metrics. Read B:',
           '            the same decompressed bytes drawn by FreeType through Pillow, the 5 x 8 window under each code\'s origin and baseline. The two files',
           '            give the same glyph for the 95 codes 0x20 to 0x7E, and every glyph used lies inside the font\'s box and advances by the cell\'s 5 dots.',
           '            The golden glyphs of test_fonte.py were typed by hand from the font, before this file existed.',
           '',
           'Substituted Three codes take another character, the one the HD44780\'s ROM shows there (the glass is drawn as that module shows it), so the',
           '            font\'s glyph of the same code point is NOT used there:']
    for code in sorted(SUBSTITUTIONS):
        point, spec, name, why = SUBSTITUTIONS[code]
        out.append('            0x%02X  U+%04X %s, from the %s file (%s)' % (code, point, name.upper(), 'Latin-1' if spec is LATIN1 else 'Unicode', why))
    out += ['',
            'Shape       FONTE_5X7[code] is 8 integers, one per dot row of the 5 x 8 cell. Element 0 is the TOP row; in each integer bit 4 (value 16) is the',
            '            LEFTMOST dot and bit 0 the rightmost, a lit dot being a 1: the convention of lcd.py, of rom_hd44780.py and of the firmware\'s CGRAM',
            '            bitmaps (src/display/BigFont_HD44780.h).',
            '',
            'Box         The font\'s box is %d rows, %d above the baseline and %d below, read from the accelerator table of both files, and %d dots wide. Its rows' % (
                BOX_ASCENT + BOX_DESCENT, BOX_ASCENT, BOX_DESCENT, CELL_W),
            '            are rows 0 to %d of the cell and row 7 is 0 in all 96 glyphs: it is the cursor row of the 5 x 8 cell. A glyph sits where the font puts it,' % (
                BOX_ASCENT + BOX_DESCENT - 1),
            '            from the cell\'s left edge, and is not centred. ' + (
                'The letters and digits stop at column 3 (column 4 is the font\'s spacing); %d symbols reach column 4: %s' % (len(reach), shown) if not letters
                else '%d glyphs reach column 4, the last: %s' % (len(reach), shown)),
            '',
            'Regenerate  python3 -I tools/lcd_alpha/fonte_extract.py --write tools/lcd_alpha/fonte_x11.py     (needs xfonts-base and Pillow)',
            'Check       the same with --check in place of --write exits 0 when this file is what the font files say. Where the font files are not',
            '            installed (CI\'s runner) it says "skipped: the font files are not here" and exits 0, comparing nothing. tools/lcd_alpha/test_fonte.py',
            '            holds the shape, the golden, the reader and the mutants, and runs without the fonts.',
            '"""',
            'PACKAGE = %r' % PACKAGE,
            'PACKAGE_VERSION = %r' % PACKAGE_VERSION,
            'COPYRIGHT = %r' % COPYRIGHT,
            'LATIN1_FILE = %r' % (FONT_DIR + '/' + LATIN1['name']),
            'LATIN1_GZ_SHA256 = %r' % LATIN1['gz_sha256'],
            'LATIN1_PCF_SHA256 = %r' % LATIN1['pcf_sha256'],
            'UNICODE_FILE = %r' % (FONT_DIR + '/' + UNICODE['name']),
            'UNICODE_GZ_SHA256 = %r' % UNICODE['gz_sha256'],
            'UNICODE_PCF_SHA256 = %r' % UNICODE['pcf_sha256'],
            'READ_ON = %r' % READ_ON,
            'METHOD = %r' % METHOD,
            '# sha256 of the table as text, one line per code: "%02X:" then the 8 rows as %02X separated by commas. test_fonte.py recomputes it.',
            'TABLE_SHA256 = %r' % table_digest(table),
            '',
            'FONTE_5X7 = {']
    for code in sorted(table):
        out.append('    0x%02X: (%s),  # %s' % (code, ', '.join('0b%s' % format(v, '05b') for v in table[code]), label_of(code)))
    out.append('}')
    return '\n'.join(out) + '\n'


def first_difference(a, b):
    la, lb = a.splitlines(), b.splitlines()
    for n in range(max(len(la), len(lb))):
        if n >= len(la) or n >= len(lb) or la[n] != lb[n]:
            return n + 1
    return 0


# -- the run --------------------------------------------------------------------------------------------------------------------------------------

def package_checks(paths, strict):
    """Who installed the files, and at which version. -> notes (printed). A different version raises only when `strict` (--write): the header would
    record a version the machine no longer has; the sha256 of the files is what identifies them."""
    def dpkg(*args):
        try:
            r = subprocess.run(['dpkg-query'] + list(args), capture_output=True, text=True)
        except OSError as e:
            raise Unreadable('dpkg-query cannot be run: %s' % e)
        return r.returncode, r.stdout.strip()

    notes = []
    rc, version = dpkg('-W', '-f=${Version}', PACKAGE)
    if rc != 0 or not version:
        raise Unreadable('dpkg-query does not know the package %s' % PACKAGE)
    for p in paths:
        rc, owner = dpkg('-S', p)
        if rc != 0 or not owner.startswith(PACKAGE + ':'):
            raise Unreadable('%s does not belong to the package %s (dpkg-query -S: %r)' % (p, PACKAGE, owner))
    if version != PACKAGE_VERSION:
        if strict:
            raise Unreadable('the installed %s is %s, the tool records %s: set PACKAGE_VERSION (and READ_ON) after checking the files\' sha256' % (
                PACKAGE, version, PACKAGE_VERSION))
        notes.append('note: the installed %s is %s, the module records %s; the files are the pinned ones, byte for byte' % (PACKAGE, version, PACKAGE_VERSION))
    return version, notes


def load_font(path, spec):
    """-> (gz bytes, PCF bytes, Font) of a pinned file. The COPYRIGHT is checked BEFORE the pin: a file under another licence is refused for that, not
    for being another file."""
    with open(path, 'rb') as f:
        gz = f.read()
    try:
        raw = gzip.decompress(gz)
    except (OSError, EOFError, zlib.error) as e:
        raise Unreadable('%s is not a gzip file: %s' % (path, e))
    try:
        font = Font(raw)
    except Unreadable as e:
        raise Unreadable('%s: %s' % (spec['name'], e))
    if font.props.get('COPYRIGHT') != COPYRIGHT:
        raise Unreadable('%s: the COPYRIGHT property is %r, not the public-domain sentence %r' % (spec['name'], font.props.get('COPYRIGHT'), COPYRIGHT))
    for what, data, size_key, sha_key in (('the .gz', gz, 'gz_bytes', 'gz_sha256'), ('the decompressed PCF', raw, 'pcf_bytes', 'pcf_sha256')):
        if len(data) != spec[size_key] or hashlib.sha256(data).hexdigest() != spec[sha_key]:
            raise Unreadable('%s (%s) is not the pinned file (%d bytes, sha256 %s; this one is %d bytes, sha256 %s)' % (
                path, what, spec[size_key], spec[sha_key], len(data), hashlib.sha256(data).hexdigest()))
    return gz, raw, font


def run(args):
    paths = {spec['name']: os.path.join(args.font_dir, spec['name']) for spec in (LATIN1, UNICODE)}
    missing = [p for p in paths.values() if not os.path.exists(p)]
    if len(missing) == 2 and args.check:
        print('fonte_extract: skipped: the font files are not here (%s); fonte_x11.py was NOT compared with the fonts' % ', '.join(missing))
        return 0
    if missing:
        raise Unreadable('the font file%s %s %s not here' % ('s' if len(missing) > 1 else '', ', '.join(missing), 'are' if len(missing) > 1 else 'is'))
    notes = []
    loaded = {}
    for spec in (LATIN1, UNICODE):
        loaded[spec['name']] = load_font(paths[spec['name']], spec)
    if os.path.realpath(args.font_dir) == os.path.realpath(FONT_DIR):
        notes += package_checks(list(paths.values()), strict=bool(args.write))[1]
    else:
        notes.append('note: --font-dir is not %s: the package is not checked' % FONT_DIR)
    for n in notes:
        print(n)
    latin1, unicode_ = loaded[LATIN1['name']][2], loaded[UNICODE['name']][2]
    print('fonts: box %d + %d rows, %d glyphs in the Latin-1 file, %d in the Unicode file; COPYRIGHT "%s" in both; the files are the pinned ones' % (
        BOX_ASCENT, BOX_DESCENT, len(latin1.metrics), len(unicode_.metrics), COPYRIGHT))
    findings = []
    table, differ = build_table(latin1, unicode_)
    findings += differ
    print('A  own reader: 96 codes; the two files give the same glyph for the 95 codes 0x20 to 0x7E: %s' % ('no' if differ else 'yes'))
    plan = {LATIN1['name']: {}, UNICODE['name']: {}}        # file -> {code point: the code it fills}
    for code in range(0x20, 0x80):
        point, spec = (SUBSTITUTIONS[code][:2]) if code in SUBSTITUTIONS else (code, LATIN1)
        plan[spec['name']][point] = code
    disagree = []
    for spec in (LATIN1, UNICODE):
        points = plan[spec['name']]
        second = freetype_cells(loaded[spec['name']][1], sorted(points))
        for p in compare_reads({p: table[c] for p, c in points.items()}, second):
            disagree.append(points[p])
            findings.append('A and B disagree on 0x%02X: %s against %s' % (points[p], table[points[p]], second[p]))
    print('B  FreeType read: A vs B: %d codes differ of 96' % len(disagree))
    if args.dump:
        for base in range(0x20, 0x80, 8):
            print('  '.join('0x%02X ' % c for c in range(base, base + 8)))
            for row in range(8):
                print('  '.join(''.join('#' if table[c][row] & (16 >> j) else '.' for j in range(5)) + ' ' for c in range(base, base + 8)))
            print()
    src = module_source(table)
    if args.check:
        with open(args.module, encoding='utf-8') as f:
            have = f.read()
        if have != src:
            findings.append('%s differs from the fonts, first at line %d' % (args.module, first_difference(have, src)))
        else:
            print('%s is what the fonts say (byte for byte)' % args.module)
    if findings:
        print('fonte_extract: %d findings' % len(findings))
        for f in findings:
            print('  ' + f)
        return 1
    if args.write:
        with open(args.write, 'w', encoding='utf-8', newline='\n') as f:
            f.write(src)
        print('wrote %s (table sha256 %s)' % (args.write, table_digest(table)))
    print('fonte_extract: clean')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description='Reads the X.Org misc-fixed 5x7 font (PCF) from xfonts-base; writes or checks fonte_x11.py')
    ap.add_argument('--font-dir', default=FONT_DIR, help='where 5x7-ISO8859-1.pcf.gz and 5x7.pcf.gz are (default %(default)s)')
    ap.add_argument('--module', default=MODULE, help='the module --check compares with (default: fonte_x11.py beside this file)')
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument('--write', metavar='FILE', help='write the module to FILE')
    mode.add_argument('--check', action='store_true', help='compare the module with the fonts; skipped, and said so, where the fonts are not installed')
    ap.add_argument('--dump', action='store_true', help='print the 96 glyphs as pictures')
    args = ap.parse_args(argv)
    try:
        return run(args)
    except Unreadable as e:
        print('fonte_extract: cannot read: %s' % e, file=sys.stderr)
        return 2
    except (OSError, subprocess.CalledProcessError) as e:
        print('fonte_extract: cannot run: %s' % e, file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
