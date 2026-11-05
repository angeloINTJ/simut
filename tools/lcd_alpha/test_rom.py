#!/usr/bin/env python3
"""test_rom.py -- proves the shape of tools/lcd_alpha/rom_hd44780.py, the HD44780U ROM A00 table (codes 0x20 to 0x7F), and that the checks bite.

What is held, on the real module:
  * count     exactly the 96 codes 0x20 to 0x7F;
  * rows      each glyph is 8 rows, and range: each row an int from 0 to 31 (five dots; a bool is refused);
  * row7      row 7 is 0 in every glyph: Table 4 of the datasheet draws no dot on the cursor row for any of these codes (rom_extract.py reads that
              from the page; the module's header says so);
  * header    the module names its source: DOCUMENT (ADE-207-272), TABLE (Table 4, ROM Code: A00, page 17), URL, the PDF's sha256 and size, the date
              it was read, the METHOD, and the table's own digest; the docstring carries the URL, the sha256, the date and a Method line;
  * pin       the sha256 of the table as text equals TABLE_SHA256 in the header. This is a change detector and nothing more: it says the table is
              the one that rom_extract.py --check proved equal to the PDF on 2026-11-04, so a hand edit, or a regeneration from another file, shows;
              it does not say the table is right. What says that is the extractor (two reads of the page) and the golden table below;
  * typography facts about letter shapes that hold in any sans-serif face and are visible on the datasheet's page, and so do not come from the
              table: the space is blank and the other 95 glyphs are not (blank); no two glyphs are equal (distinct); the capitals A H I M O T U V
              W X Y are their own mirror image (symmetric); the capitals B D E F H K L P R have a stem down the left side, bit 4 set in rows 0 to
              6 (stem). The last one is the orientation anchor: it is what tells bit 4 = leftmost from the reverse.

  * golden    every glyph equals rom_a00_golden.txt: the reviewer's own reading of page 17, typed by eye from a 600 dpi render on 2026-11-04 at
              20:41:55Z BEFORE the reviewer opened this module (the file as delivered, sha256
              961e08fe17175a0b8df5dc7cb6044e3cf6346a9c44576e2927dea14b5d1018a7; 96 of 96 codes equal when it was compared). It is a second reading of
              the same page by another hand, so a regeneration from a wrong source with a consistent pin, which the pin cannot see, fails here.

The controls: every check is run again on a copy of the table (or header) with one fault put in, and must report exactly the findings expected: a bit
flipped, the bit order reversed in every row, two glyphs swapped, a glyph of 7 rows, a row of 32, a dot on row 7, a code missing, a code too many, a
header field missing or malformed or not in the docstring, a stale pin. The pin alone sees a flipped bit in a letter nothing else covers, a reversal
sees the stems too, and a swap of two letters nothing covers is the pin alone: the expected sets below say which check owns which fault.

Host only, standard library only. Prints one line per check and exits 0 when all hold, 1 when any fails.
"""
import copy
import hashlib
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)      # explicit: python3 -I leaves the script's own folder off the path
import rom_hd44780 as rom     # noqa: E402

CODES = range(0x20, 0x80)
HEADER_KEYS = ('DOCUMENT', 'TABLE', 'URL', 'PDF_SHA256', 'PDF_BYTES', 'READ_ON', 'METHOD', 'TABLE_SHA256')
HEADER_FORMAT = {
    'DOCUMENT': r'.*ADE-207-272.*',
    'TABLE': r'Table 4, .*\(ROM Code: A00\), page 17',
    'URL': r'https://\S+\.pdf',
    'PDF_SHA256': r'[0-9a-f]{64}',
    'READ_ON': r'\d{4}-\d{2}-\d{2}',
    'TABLE_SHA256': r'[0-9a-f]{64}',
}
SYMMETRIC = 'AHIMOTUVWXY'      # capitals that are their own mirror image
STEM = 'BDEFHKLPR'             # capitals with a stem down the left side (H has one on the right too)


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
        elif key in HEADER_FORMAT and not re.fullmatch(HEADER_FORMAT[key], str(header[key])):
            out.append(('header', key))
        elif key == 'PDF_BYTES' and not (type(header[key]) is int and header[key] > 0):
            out.append(('header', key))
        if key in ('URL', 'PDF_SHA256', 'READ_ON') and header.get(key) not in (None, '') and str(header[key]) not in doc:
            out.append(('header', 'doc ' + key))
    if 'Method' not in doc:
        out.append(('header', 'doc Method'))
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
    for ch in SYMMETRIC:
        if ord(ch) in good and any(mirror(v) != v for v in good[ord(ch)]):
            out.append(('symmetric', '0x%02X' % ord(ch)))
    for ch in STEM:
        if ord(ch) in good and not all(good[ord(ch)][i] & 16 for i in range(7)):
            out.append(('stem', '0x%02X' % ord(ch)))
    return out


# -- the faults ---------------------------------------------------------------------------------------------------------------------------------------

def flip(code, row, bit):
    def f(t, h, d):
        t[code] = tuple(v ^ (1 << bit) if i == row else v for i, v in enumerate(t[code]))
    return f


def reverse_bits(t, h, d):
    for c in t:
        t[c] = tuple(mirror(v) for v in t[c])


def swap(a, b):
    def f(t, h, d):
        t[a], t[b] = t[b], t[a]
    return f


def short_glyph(t, h, d):
    t[0x41] = t[0x41][:7]


def big_value(t, h, d):
    t[0x41] = (32,) + tuple(t[0x41][1:])


def row7_dot(t, h, d):
    t[0x41] = tuple(t[0x41][:7]) + (0b00100,)


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


def strip_from_doc(key):
    def f(t, h, d):
        d[0] = d[0].replace(str(h[key]), '')
    return f


CH_A, CH_B, CH_S, CH_5, CH_H = ord('A'), ord('B'), ord('S'), ord('5'), ord('H')
STEMS_LEFT_ONLY = {('stem', '0x%02X' % ord(ch)) for ch in STEM if ch != 'H'}   # reversed, H keeps the bit: its right stem is the old left one
MUTANTS = [
    # (what is put in, the function, the findings expected: exactly these)
    ("one bit flipped in the S (row 3, bit 2): nothing but the pin sees it", flip(CH_S, 3, 2), {('pin', '')}),
    ("one bit flipped in the H (row 1, bit 0): the pin and the mirror", flip(CH_H, 1, 0), {('pin', ''), ('symmetric', '0x48')}),
    ("the bit order reversed in every row: the pin and the left stems", reverse_bits, {('pin', '')} | STEMS_LEFT_ONLY),
    ("the S and the 5 swapped: nothing but the pin sees it", swap(CH_S, CH_5), {('pin', '')}),
    ("the A and the B swapped: the pin, the A's mirror, the B's stem", swap(CH_A, CH_B), {('pin', ''), ('symmetric', '0x41'), ('stem', '0x42')}),
    ("a glyph of 7 rows", short_glyph, {('rows', '0x41'), ('pin', '')}),
    ("a row of 32", big_value, {('range', '0x41'), ('pin', '')}),
    ("a dot on row 7 of the A", row7_dot, {('row7', '0x41'), ('pin', '')}),
    ("the code 0x41 missing", drop_code, {('count', 'missing 0x41'), ('pin', '')}),
    ("a code 0x80 too many", extra_code, {('count', 'extra 0x80'), ('pin', '')}),
    ("the URL missing from the header", drop_header('URL'), {('header', 'URL')}),
    ("the PDF's sha256 malformed", set_header('PDF_SHA256', 'abc'), {('header', 'PDF_SHA256'), ('header', 'doc PDF_SHA256')}),
    ("the date missing from the docstring", strip_from_doc('READ_ON'), {('header', 'doc READ_ON')}),
    ("a stale pin", set_header('TABLE_SHA256', '0' * 64), {('pin', '')}),
]


def golden():
    """rom_a00_golden.txt as {code: 8 rows}: a line is '0xNN' and 8 groups of 5 characters, '#' a lit dot, the first character the left dot."""
    out = {}
    for line in open(os.path.join(HERE, 'rom_a00_golden.txt'), encoding='utf-8'):
        f = line.split()
        if not f or f[0].startswith('#'):
            continue
        out[int(f[0], 16)] = tuple(int(g.replace('#', '1').replace('.', '0'), 2) for g in f[1:9])
    return out


def main():
    fails = 0

    def report(ok, text):
        nonlocal fails
        fails += 0 if ok else 1
        print('%s %s' % ('ok  ' if ok else 'FAIL', text))

    table = dict(rom.ROM_A00)
    header = {k: getattr(rom, k, None) for k in HEADER_KEYS}
    doc = rom.__doc__ or ''
    real = findings(table, header, doc)
    report(not real, 'the module: %d codes, 8 rows each, rows 0..31, row 7 blank, header, pin, typography: %s'
           % (len(table), 'no findings' if not real else real))
    report(len(table) == 96 and min(table) == 0x20 and max(table) == 0x7F, 'the table holds 0x20..0x7F, 96 codes')
    report(sum(1 for c in table if table[c][7]) == 0, 'row 7 is blank in all 96 glyphs, as the datasheet draws it')
    empty = findings({}, header, doc)
    report(len([1 for k, _ in empty if k == 'count']) == 96, 'control: an empty table is reported, 96 codes missing, whatever else it says')
    for what, mutate, expected in MUTANTS:
        t, h, d = copy.deepcopy(table), dict(header), [doc]
        mutate(t, h, d)
        got = findings(t, h, d[0])
        ok = set(got) == expected and len(got) == len(set(got))
        shown = ', '.join('%s%s' % (k, ' ' + dt if dt else '') for k, dt in sorted(expected))
        report(ok, 'mutant: %s -> %s' % (what, shown if ok else 'EXPECTED %s, GOT %s' % (sorted(expected), sorted(got))))
    gold = golden()
    report(sorted(gold) == list(range(0x20, 0x80)), 'golden: rom_a00_golden.txt holds the 96 codes 0x20..0x7F')
    diff = sorted(c for c in gold if tuple(table.get(c, ())) != gold[c])
    report(not diff, 'golden: every glyph equals the reviewer\'s independent reading of page 17%s' % ('' if not diff else ': differ ' + ', '.join('0x%02X' % c for c in diff)))
    t = copy.deepcopy(table)
    t[0x53] = tuple(v ^ (0x04 if i == 3 else 0) for i, v in enumerate(t[0x53]))   # the S's mutant above: only the pin sees it among the shape checks
    report(sorted(c for c in gold if tuple(t[c]) != gold[c]) == [0x53], 'mutant: one bit flipped in the S -> golden reports exactly 0x53')
    print('test_rom: %s' % ('all hold' if not fails else '%d failed' % fails))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
