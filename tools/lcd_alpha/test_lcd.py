#!/usr/bin/env python3
"""test_lcd.py -- holds lcd.py's drawing of the glass to the dot rule, the geometry and the CGRAM (2026-11-04).

The checks read pixels of drawn frames against what the rule says they must be, and the mutants show the checks can tell the rule
from a wrong one: each replaces one of lcd.py's small functions (the bit order of a row, the gap between two dots, where a cell starts,
where a code's glyph comes from) by a wrong version, reruns every check, and must make at least one fail. The CGRAM check reads BF_LT
from the firmware's own src/display/BigFont_HD44780.h, so the convention is tied to what the alpha loads and not to this file.

  python3 tools/lcd_alpha/test_lcd.py     exit 0 when every check passes and every mutant is caught, 1 otherwise
"""
import copy
import os
import re
import subprocess
import sys
import tempfile
import types

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
sys.path.insert(0, AQUI)
import lcd  # noqa: E402

A = (0b01110, 0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b00000)   # the 'A' of a small test font
FONTE = {0x20: (0,) * 8, 0x41: A}
VAZIA = [bytes(8)] * 8


def bf_lt():
    """BF_LT as the firmware declares it, parsed from src/display/BigFont_HD44780.h."""
    t = open(os.path.join(RAIZ, 'src', 'display', 'BigFont_HD44780.h'), encoding='utf-8').read()
    m = re.search(r'BF_LT\[8\]\s*=\s*\{([^}]*)\}', t)
    return bytes(int(v, 0) for v in m.group(1).replace(' ', '').split(','))


def linha(codigos):
    return bytes(codigos) + b' ' * (16 - len(codigos))


def centro(e, linha_, coluna, i, j):
    x0, y0 = lcd._origem_celula(e, linha_, coluna)
    x, y = lcd._origem_ponto(e, x0, y0, i, j)
    return (x + e.ponto // 2, y + e.ponto // 2)


def verificacoes():
    """Runs every check against the module as it is now; returns the list of failures (empty = all hold)."""
    falhas = []

    def confere(cond, msg):
        if not cond:
            falhas.append(msg)

    e = lcd.Estilo()
    # The fixed points are written out, not computed by lcd.py's own functions: a check that asked the module where a dot is would move
    # with a mutant of that function and catch nothing.
    confere(lcd.tamanho(e) == (639, 137), f'tamanho( ) of the defaults is {lcd.tamanho(e)}, not (639, 137)')
    a = lcd.desenhar((linha([0x41]), linha([])), VAZIA, FONTE, e)
    confere(a.size == (639, 137), f'the image is {a.size}')
    lit = dict((c, n) for n, c in a.getcolors(1 << 16)).get(e.aceso, 0)   # getcolors( ): getdata( ) leaves Pillow in 14
    confere(lit == sum(bin(v).count('1') for v in A) * 36, f"lit pixels of 'A' = {lit}, not 15 dots x 36")
    # Cell (0,0) starts at (10,10); a dot is 6 px with a 1 px gap, so dot (i,j) spans x 10+7j .. 15+7j, y 10+7i .. 15+7i.
    confere(a.getpixel((12, 12)) == e.apagado, "dot (0,0) of 'A' (row 0b01110: leftmost off) is not unlit")
    confere(a.getpixel((19, 12)) == e.aceso, "dot (0,1) of 'A' is not lit")
    confere(a.getpixel((40, 12)) == e.apagado, "dot (0,4) of 'A' is not unlit")
    confere(a.getpixel((12, 33)) == e.aceso, "dot (3,0) of 'A' is not lit")
    confere(all(a.getpixel((16, y)) == e.fundo for y in range(10, 65)), 'the gap between dot columns 0 and 1 is not the glass')
    confere(all(a.getpixel((x, y)) == e.fundo for x in range(44, 49) for y in range(10, 65)), 'the gap between cells 0 and 1 is not the glass')
    confere(all(a.getpixel((x, 5)) == e.fundo for x in range(639)), 'the top margin is not the glass')
    confere(a.getpixel((51, 12)) == e.apagado, 'cell (0,1) does not start at x 49')
    # CGRAM: code 0 at row 1, column 15 with slot 0 = BF_LT (top row 0b00111): its top-left dot off, its top-right dot on.
    # Cell (1,15) starts at x 10 + 15*(34+5) = 595, y 10 + 55 + 7 = 72.
    cg = [bf_lt()] + [bytes(8)] * 7
    b = lcd.desenhar((linha([]), bytes(15) .replace(b'\x00', b' ') + b'\x00'), cg, FONTE, e)
    confere(b.getpixel((597, 74)) == e.apagado, 'CGRAM slot 0 (BF_LT): dot (0,0) is not unlit')
    confere(b.getpixel((625, 74)) == e.aceso, 'CGRAM slot 0 (BF_LT): dot (0,4) is not lit')
    confere(b.getpixel((597, 88)) == e.aceso, 'CGRAM slot 0 (BF_LT): dot (2,0) is not lit')
    # A code with no glyph: the message names the byte, the row and the column.
    # Each refusal is its own check: another exception is that check's failure, and the checks after it still run.
    try:
        lcd.desenhar((bytes([0x10]) + b' ' * 15, linha([])), VAZIA, FONTE, e)
        confere(False, 'byte 0x10 drew instead of failing')
    except ValueError as x:
        confere(str(x) == 'no glyph for byte 0x10 at row 0, column 0', f'byte 0x10: {x}')
    except Exception as x:
        confere(False, f'byte 0x10 raised {type(x).__name__} ({x}), not ValueError')
    # A glyph that is not 8 rows is refused, not drawn short.
    try:
        lcd.desenhar((linha([0x41]), linha([])), VAZIA, {0x20: (0,) * 8, 0x41: A[:7]}, e)
        confere(False, 'a 7-row glyph drew')
    except ValueError:
        pass
    except Exception as x:
        confere(False, f'a 7-row glyph raised {type(x).__name__} ({x}), not ValueError')
    # Inputs unchanged; any mapping is a font; the style is a real dataclass whose sizes move the image.
    li, cgc, fo = (linha([0x41]), linha([0])), [bytearray(bf_lt())] + [bytearray(8)] * 7, dict(FONTE)
    antes = copy.deepcopy((li, cgc, fo))
    lcd.desenhar(li, cgc, types.MappingProxyType(fo), e)
    confere((li, cgc, fo) == antes, 'an input changed')
    confere(lcd.tamanho(lcd.Estilo(ponto=3, margem=4)) == (2 * 4 + 16 * 19 + 15 * 5, 2 * 4 + 2 * 31 + 7), 'Estilo(ponto=3, margem=4) does not move the size')
    return falhas


def cli():
    """The command line: exit 0 with a PNG of the right size, exit 2 on a byte with no glyph."""
    falhas = []
    with tempfile.TemporaryDirectory() as d:
        import json
        q = os.path.join(d, 'q.json')
        f = os.path.join(d, 'f.json')
        out = os.path.join(d, 'o.png')
        json.dump({'linhas': [linha([0x41]).hex(), linha([]).hex()], 'cgram': [bytes(8).hex()] * 8}, open(q, 'w'))
        json.dump({str(k): list(v) for k, v in FONTE.items()}, open(f, 'w'))
        r = subprocess.run([sys.executable, os.path.join(AQUI, 'lcd.py'), '--quadro', q, '--fonte', f, '--out', out], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(out):
            falhas.append(f'CLI: exit {r.returncode} {r.stderr.strip()}')
        else:
            from PIL import Image
            if Image.open(out).size != (639, 137):
                falhas.append('CLI: the PNG is not 639 x 137')
        json.dump({'linhas': [(bytes([0x10]) + b' ' * 15).hex(), linha([]).hex()], 'cgram': [bytes(8).hex()] * 8}, open(q, 'w'))
        r = subprocess.run([sys.executable, os.path.join(AQUI, 'lcd.py'), '--quadro', q, '--fonte', f, '--out', out], capture_output=True, text=True)
        if r.returncode != 2 or '0x10' not in r.stderr:
            falhas.append(f'CLI on byte 0x10: exit {r.returncode}, stderr {r.stderr.strip()!r}')
    return falhas


MUTANTES = {
    'the bit order reversed (bit 0 leftmost)': ('_aceso', lambda valor, j: bool(valor & (1 << j))),
    'the gap between dots one pixel smaller': ('_origem_ponto', lambda e, x0, y0, i, j: (x0 + j * (e.ponto + e.vao_ponto - 1), y0 + i * (e.ponto + e.vao_ponto - 1))),
    'the cells without the gap between them': ('_origem_celula', lambda e, l, c: (e.margem + c * lcd.celula(e)[0], e.margem + l * (lcd.celula(e)[1] + e.vao_linha))),
    'codes 0..7 read from the font, not the CGRAM': ('_glifo', lambda cod, cg, fo, l, c: fo.get(cod) or (0,) * 8),
}


def main():
    falhas = verificacoes() + cli()
    for f in falhas:
        print('FAIL', f)
    print(f'checks: {"all hold" if not falhas else str(len(falhas)) + " failed"}')
    escaparam = []
    for nome, (fn, errada) in MUTANTES.items():
        certa = getattr(lcd, fn)
        setattr(lcd, fn, errada)
        try:
            pegos = verificacoes()
        except Exception as x:   # a mutant that crashes the drawing is caught too, but say so
            pegos = [f'raised {type(x).__name__}: {x}']
        finally:
            setattr(lcd, fn, certa)
        print(f'mutant {nome}: {"caught by " + str(len(pegos)) + " check(s), first: " + pegos[0] if pegos else "NOT CAUGHT"}')
        if not pegos:
            escaparam.append(nome)
    return 1 if falhas or escaparam else 0


if __name__ == '__main__':
    sys.exit(main())
