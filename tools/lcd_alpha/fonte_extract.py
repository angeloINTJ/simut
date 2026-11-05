#!/usr/bin/env python3
"""fonte_extract.py -- PHASE 1 (the reader only; the generator comes next)."""
import gzip
import hashlib
import sys

sys.dont_write_bytecode = True

# The source. The run refuses any other file: the checks below were written for these bytes, which were measured on 2026-11-04.
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
    """-> the font-wide facts: ascent, descent, constant_width, terminal, and the two bounding metrics."""
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


def load(path):
    with open(path, 'rb') as f:
        gz = f.read()
    return gz, gzip.decompress(gz)


if __name__ == '__main__':
    for path in sys.argv[1:]:
        gz, raw = load(path)
        font = Font(raw)
        print(path, hashlib.sha256(gz).hexdigest(), len(raw), hashlib.sha256(raw).hexdigest())
        print('  formats', {t: hex(f) for t, f in font.formats.items()})
        print('  accel', font.accel)
        print('  props', {k: v for k, v in font.props.items() if k in ('FONT', 'COPYRIGHT', 'FONT_ASCENT', 'FONT_DESCENT', 'PIXEL_SIZE', 'CHARSET_REGISTRY', 'CHARSET_ENCODING')})
        print('  glyphs', len(font.metrics), 'encoded', len(font.encoding), 'range', hex(min(font.encoding)), hex(max(font.encoding)))
