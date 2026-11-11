#!/usr/bin/env python3
"""gen_angulo_meta.py [--check] [--fontes DIR] [--extra FILE] [--icones FILE] [--out FILE] -- writes src/ui/angulo/AnguloMeta.h from the
tree and, with --check, says what in the committed file is not what the tree generates.

Why this exists. The header came from tools/tft_ui/gerar_meta.js, which runs only with the TFT plan's delivery and a renderer
environment, neither in the git tree (the rule for a generated file is that it is never edited by hand, and a generator that cannot
be run is a rule that cannot be kept). Since 2026-11-11 each of its three tables has its source in the tree:

  kExtra     the code point of GFX code 0x7F onwards: tools/fonts/angulo_extra.json, the table the four text faces are made from.
  kCapX      cap height and x height of the nine faces of ua::Fonte, in that order: the height of the glyph 0x48 ('H') and of the
             glyph 0x78 ('x') in the face's own header (src/ui/angulo/kit/fontes/*.h), read with parse_font_header of
             tools/subset_font.py, the reader that test_gen_angulo_face.py's kcapx control uses too. Six faces are measured so:
             SANS9, SANSB9, MONO9, MONO12, DISP9 and DISP12. DISP18, DISP24 and DISP32 carry the codes 43..57 only (the digits, the
             sign and the point), so there is no 'H' and no 'x' to measure: their heights are the table SO_DIGITOS, with its origin.
  IconeNome  the enum: the names of the registry in src/ui/angulo/AnguloIcones.h, which gerar_meta.js still writes, sorted as it
             sorted them (the names with their hyphens, by code unit: CHEVRON_UP before CHEVRONS_LEFT), then NENHUM.

Checked on 2026-11-11 against the header of commit 2b21be6c: with the inputs of that tree the text is the committed one but for line 1,
which names this script. The comments inside the text (fontes.json, "the delivery's own engine") are the ones gerar_meta.js wrote and
stay, so that moving the generator changes one line of the header and no table.

--check   writes nothing: regenerates in memory and compares with --out, byte for byte. Exit 0 if equal. If not, exit 1 and stdout
          says what differs by UNIT: one line for each entry of kExtra (by GFX code), each line of kCapX (by face) and each name of
          the enum, and the count above them. A difference in the text around the tables (line 1, the opening comment, the braces)
          is one finding of its own, `text line N`, and only when no unit differs: the counts in the opening comment follow from the
          same inputs as the names, so a registry with one name fewer is one finding, not two. An --out that is not there is the
          finding `file`.
--fontes DIR, --extra FILE, --icones FILE, --out FILE
          where the faces, the EXTRA table and the registry are read, and where the header is written or compared; the defaults are
          the tree's. test_gen_angulo_meta.py points them at copies so that it never touches a tracked file.
Exit: 0 if it wrote (or, with --check, if equal); 1 only with --check, if the file differs; 2 if it could not look (the command line,
an input that is not there or not what it should be, an --out it cannot read or write), the reason in stderr. 1 means it looked and
the bytes are other; 2, that it did not get to look: whoever calls it must not take one for the other. Standard library only.
"""
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True   # no tools/__pycache__ for subset_font
HERE = Path(__file__).resolve().parent
TOOLS = HERE.parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
from subset_font import parse_font_header  # noqa: E402

UA = ROOT / 'src' / 'ui' / 'angulo'
FONTES_PADRAO = UA / 'kit' / 'fontes'
EXTRA_PADRAO = TOOLS / 'fonts' / 'angulo_extra.json'
ICONES_PADRAO = UA / 'AnguloIcones.h'
SAIDA_PADRAO = UA / 'AnguloMeta.h'
USO = 'Usage: gen_angulo_meta.py [--check] [--fontes DIR] [--extra FILE] [--icones FILE] [--out FILE]'
OPCOES = ('--fontes', '--extra', '--icones', '--out')

# The faces of ua::Fonte (Angulo.h), in its order; MARCA reuses DISP24's row and has none of its own. The 7 pt faces left this list
# on 2026-10-15 (the panel's smallest type is 9 pt).
FONTES = ('SANS9', 'SANSB9', 'MONO9', 'MONO12', 'DISP9', 'DISP12', 'DISP18', 'DISP24', 'DISP32')
CAIXA_ALTA, ALTURA_X = 0x48, 0x78   # the glyphs whose height is the cap height and the x height
PRIMEIRO_EXTRA = 0x7F               # the GFX code of the first entry of the EXTRA table

# The faces with the codes 43..57 only carry neither 'H' nor 'x': there is nothing to measure. Their cap and x heights are the ones
# fontes.json of the TFT plan's delivery (outside the tree) gave gerar_meta.js, which wrote them to AnguloMeta.h; these are the
# values that header held at commit 2b21be6c, and they equal line 3 of each header ("Caixa alta N px, altura de x M px"), which the
# delivery's own generator wrote (test_gen_angulo_meta.py holds the two together). A face whose header has both glyphs is measured,
# whether or not it is listed here.
SO_DIGITOS = {'DISP18': (23, 19), 'DISP24': (31, 24), 'DISP32': (42, 33)}

LINHA1 = "// GENERATED by tools/tft_ui/gen_angulo_meta.py from the tree's faces, EXTRA table and icon registry. Do not edit."
# `{ IconeNome::NAME, 20, ::entrega::ICONE_NAME_20_bits },` (delivered) or `{ IconeNome::NAME, 20, EXTRA_NAME_20_bits },` (extra)
LINHA_REGISTRO = re.compile(r'^  \{ IconeNome::([A-Z0-9_]+), (\d+), (::entrega::)?\w+_bits \},$')


class Refused(Exception):
    """It did not get to look (command line, an input, the output): exit 2."""


def parse_args(argv):
    """(check, {option: value}): --check at most once, each option once and with a value; anything else is the Usage line.

    A positional, an unknown option and the form --option=value are refused: an option that the command line skips in silence
    checks nothing.
    """
    check, found = False, {}
    it = iter(argv)
    for arg in it:
        if arg == '--check':
            if check:
                raise Refused(USO)
            check = True
        elif arg in OPCOES:
            value = next(it, None)
            if value is None or arg in found:
                raise Refused(USO)
            found[arg] = value
        else:
            raise Refused(USO)
    return check, found


def shown(path):
    """The path as the messages say it: relative to the repository when it is inside."""
    try:
        return Path(path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def read_text(path, what):
    try:
        return Path(path).read_bytes().decode('utf-8')
    except FileNotFoundError:
        raise Refused(f'{shown(path)}: not there ({what})')
    except UnicodeDecodeError as e:
        raise Refused(f'{shown(path)}: not UTF-8 ({what}): {e}')
    except OSError as e:
        raise Refused(f'{shown(path)}: {e.strerror or e} ({what})')


def read_extra(path):
    """[(code, char)] of the EXTRA table: the codes 0x7F, 0x80, ... in order, one character each, with the unicode it names."""
    text = read_text(path, 'the EXTRA table')
    try:
        table = [(e['code'], e['char'], e['unicode']) for e in json.loads(text)['entries']]
    except (ValueError, KeyError, TypeError) as e:
        raise Refused(f'{shown(path)}: not the EXTRA table ({type(e).__name__}: {e})')
    well = bool(table) and all(isinstance(code, int) and isinstance(ch, str) and len(ch) == 1 and uni == 'U+%04X' % ord(ch)
                               for code, ch, uni in table)
    if not well or [code for code, _, _ in table] != list(range(PRIMEIRO_EXTRA, PRIMEIRO_EXTRA + len(table))):
        raise Refused(f'{shown(path)}: not the EXTRA table: it holds the codes {PRIMEIRO_EXTRA}.. in order, one character each, '
                      f'with the unicode that character has')
    return [(code, ch) for code, ch, _ in table]


def read_heights(fontes):
    """[(face, cap, x, origin)] for the faces of ua::Fonte: measured from the header when it carries 'H' and 'x', else the table."""
    heights = []
    for face in FONTES:
        path = Path(fontes) / f'{face}.h'
        text = read_text(path, 'a face')
        try:
            _, glyphs, _, _, _, first, last, _ = parse_font_header(text)
        except ValueError as e:
            raise Refused(f'{shown(path)}: not a GFXfont header ({e})')
        pair = tuple(glyphs[code - first]['height'] if first <= code <= last and code - first < len(glyphs) else 0
                     for code in (CAIXA_ALTA, ALTURA_X))
        if all(pair):
            heights.append((face, *pair, f'measured from {face}.h'))
        elif face in SO_DIGITOS:
            heights.append((face, *SO_DIGITOS[face], f"the table SO_DIGITOS (codes {first}..{last}: {face}.h has no 'H' and no 'x')"))
        else:
            raise Refused(f"{shown(path)}: no 'H' and no 'x' to measure (codes {first}..{last}), and {face} is not in SO_DIGITOS")
    return heights


def read_registry(path):
    """(names, delivered, extra): the icon names of the registry, sorted as gerar_meta.js sorts them, and the rows of each kind."""
    names, delivered, extra = set(), 0, 0
    for number, line in enumerate(read_text(path, 'the icon registry').split('\n'), 1):
        if 'IconeNome::' not in line:
            continue
        m = LINHA_REGISTRO.match(line)
        if not m:
            raise Refused(f'{shown(path)}:{number}: not a row of the registry: {line!r}')
        names.add(m.group(1))
        if m.group(3):
            delivered += 1
        else:
            extra += 1
    if not names:
        raise Refused(f'{shown(path)}: no row of the registry (`{{ IconeNome::NAME, SIZE, ..._bits }},`)')
    # gerar_meta.js sorted the names with hyphens ('chevron-down'), and '-' comes before every letter: CHEVRON_UP, CHEVRONS_LEFT.
    return sorted(names, key=lambda n: n.lower().replace('_', '-')), delivered, extra


def render(extra, heights, names, delivered, extras):
    """The text of AnguloMeta.h. Lines 2 on are gerar_meta.js's template, word for word."""
    lines = [
        LINHA1,
        '//',
        '// The EXTRA table and the cap/x heights the text engine needs, and the icon',
        f"// registry: the {delivered} icons of icones.h plus {extras} the firmware's real screens",
        "// need, rasterized by the delivery's own engine.",
        '#pragma once',
        '#include <stdint.h>',
        '',
        'namespace ua {',
        'namespace meta {',
        '',
        '// Unicode code point of GFX codes 0x7F.. (fontes.json "extra"), in code order.',
        f'static const uint32_t kExtra[{len(extra)}] = {{',
        *[f'  0x{ord(ch):04x},  // 0x{code:X} {ch}' for code, ch in extra],
        '};',
        '',
        "// Cap height and x height (ink box of 'H' and 'x'; fontes.json for the digit",
        '// fonts), in the order of ua::Fonte.',
        f'static const uint8_t kCapX[{len(heights)}][2] = {{',
        *[f'  {{ {cap}, {x} }},  // {face}' for face, cap, x, _ in heights],
        '};',
        '',
        '}  // namespace meta',
        '',
        'enum class IconeNome : uint8_t {',
        *[f'  {name},' for name in names],
        '  NENHUM',
        '};',
        '',
        '}  // namespace ua',
        '',
        '',
    ]
    return '\n'.join(lines)


def units(text):
    """The three tables of an AnguloMeta.h: ({code: row}, {face: row}, [name]), each row as the file says it."""
    extra, capx, names, block = {}, {}, [], None
    for line in text.split('\n'):
        if line.startswith('static const uint32_t kExtra['):
            block = 'extra'
        elif line.startswith('static const uint8_t kCapX['):
            block = 'capx'
        elif line.startswith('enum class IconeNome'):
            block = 'enum'
        elif line.startswith('};'):
            block = None
        elif block == 'extra':
            m = re.match(r'^  (0x[0-9a-f]+,  // (0x[0-9A-F]+) .*)$', line)
            if m:
                extra[m.group(2)] = m.group(1)
        elif block == 'capx':
            m = re.match(r'^  (\{ \d+, \d+ \}),  // (\w+)$', line)
            if m:
                capx[m.group(2)] = m.group(1)
        elif block == 'enum':
            m = re.match(r'^  (\w+),$', line)
            if m:
                names.append(m.group(1))
    return extra, capx, names


def differences(want, have):
    """The findings, as (label, detail), between the text the tree generates (`want`) and the file's (`have`): [] if equal."""
    if want == have:
        return []
    out = []
    w_extra, w_capx, w_names = units(want)
    h_extra, h_capx, h_names = units(have)
    for code in sorted(set(w_extra) | set(h_extra), key=lambda c: int(c, 16)):
        if w_extra.get(code) != h_extra.get(code):
            out.append((f'kExtra {code}', f'the file has {quote(h_extra.get(code))}, the tree gives {quote(w_extra.get(code))}'))
    for face in [f for f in FONTES if f in w_capx or f in h_capx] + sorted((set(w_capx) | set(h_capx)) - set(FONTES)):
        if w_capx.get(face) != h_capx.get(face):
            out.append((f'kCapX {face}', f'the file has {quote(h_capx.get(face))}, the tree gives {quote(w_capx.get(face))}'))
    for name in sorted(set(w_names) ^ set(h_names), key=lambda n: n.lower().replace('_', '-')):
        out.append((f'IconeNome {name}', "the registry names it, the file's enum does not" if name in w_names
                    else "the file's enum has it, the registry does not"))
    if not out:   # no unit differs: what differs is the text around them, or the order of the names
        a, b = want.split('\n'), have.split('\n')
        at = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
        out.append((f'text line {at + 1}', f'the file has {quote(b[at] if at < len(b) else None)}, the tree gives '
                                           f'{quote(a[at] if at < len(a) else None)}'))
    return out


def quote(row):
    return 'nothing' if row is None else f'`{row}`'


def main(argv):
    try:
        check, opts = parse_args(argv)
        fontes = opts.get('--fontes', FONTES_PADRAO)
        out = Path(opts.get('--out', SAIDA_PADRAO))
        extra = read_extra(opts.get('--extra', EXTRA_PADRAO))
        heights = read_heights(fontes)
        names, delivered, extras = read_registry(opts.get('--icones', ICONES_PADRAO))
        text = render(extra, heights, names, delivered, extras)
        summary = f'{len(extra)} EXTRA codes, {len(heights)} faces, {len(names)} icon names'
        if check:
            try:
                have = out.read_bytes()
            except FileNotFoundError:
                print(f'gen_angulo_meta: {shown(out)} is not what the tree generates: 1 finding')
                print(f'  file: {shown(out)} is not there')
                return 1
            except OSError as e:
                raise Refused(f'{shown(out)}: {e.strerror or e} (the file to compare with)')
            found = differences(text, have.decode('utf-8', errors='replace'))
            if not found and text.encode('utf-8') != have:   # the same lines in other bytes (a line ending, an invalid sequence)
                found = [('text line 1', 'the bytes differ though the lines read the same (a line ending or a character set)')]
            if not found:
                print(f'gen_angulo_meta: {shown(out)} is what the tree generates ({summary})')
                return 0
            print(f"gen_angulo_meta: {shown(out)} is not what the tree generates: {len(found)} finding{'s' if len(found) != 1 else ''}")
            for label, detail in found:
                print(f'  {label}: {detail}')
            print('Run python3 tools/tft_ui/gen_angulo_meta.py to write it again from the tree.')
            return 1
        try:
            out.write_bytes(text.encode('utf-8'))
        except OSError as e:
            raise Refused(f'{shown(out)}: could not write: {e.strerror or e}')
        print(f'gen_angulo_meta: wrote {shown(out)} from the tree: {summary} ({delivered} delivered and {extras} extra registry rows)')
        for face, cap, x, origin in heights:
            print(f'  kCapX {face} {{ {cap}, {x} }}: {origin}')
        return 0
    except Refused as e:
        text = str(e)
        print(text if text == USO else f'gen_angulo_meta: {text}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
