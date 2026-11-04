#!/usr/bin/env python3
"""lcd.py -- draws a 16x2 HD44780 character LCD from its two DDRAM rows and its eight CGRAM slots (2026-11-04).

The SIMUT alpha's LCD has no pixels to read back. What the glass shows is the DDRAM its driver wrote at the last blit( ) (two rows of
16 codes: src/display/HD44780_16x2.h) and the custom characters in CGRAM (codes 0..7: src/display/BigFont_HD44780.h). This draws that,
so a frame the alpha composes becomes an image: the manual's LCD figures, drawn from the alpha's code on the host, and a capture of a
board later.

Glyphs: codes 0..15 come from `cgram` (8..15 mirror 0..7, as on the controller: Table 4 of the datasheet lists CGRAM (1)..(8) twice in
its column 0000; the alpha's driver drops 8..31, so only 0..7 reach its DDRAM), every other code from `fonte` (by default the ROM A00
table, rom_hd44780.py, read from the HD44780U datasheet). A glyph is 8 rows, element 0 on top, and in each row bit 4 is the leftmost dot and bit 0 the rightmost: the
firmware's own CGRAM convention (BF_LT = { 0b00111, ... } has its top-left dot off). Bits above 4 are not dots and are ignored, as the
controller ignores them.

The look is a blue 1602 module with white dots, the unlit dots faintly visible, as on the glass (the maintainer's choice, 2026-11-04).
Every colour and size is in Estilo, and the image size is a formula of it: with the defaults a cell is 34 x 55 px (5 dots of 6 px and
4 gaps of 1 px across, 8 dots and 7 gaps down), and 16 cells with 15 gaps of 5 px plus 10 px margins give 639 x 137, about the 640 px
width of the panel's figures. Integer geometry only: no resize, no antialiasing, nothing from the system's fonts.

  python3 tools/lcd_alpha/lcd.py --quadro QUADRO.json --out SAIDA.png [--fonte FONTE.json]

QUADRO.json: {"linhas": ["<32 hex digits>", "<32 hex digits>"], "cgram": ["<16 hex digits>" x 8]}. FONTE.json, when given in place of
ROM A00: {"<code as a decimal string>": [8 ints], ...}. Exit 0 drawn, 2 when an input is missing, malformed or names a byte with no glyph.
"""
import argparse
import json
import os
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from typing import Tuple

from PIL import Image, ImageDraw

sys.dont_write_bytecode = True   # python3 -I ignores PYTHONDONTWRITEBYTECODE; no __pycache__ beside the tools
LINHAS = 2
COLUNAS = 16
PONTOS_X = 5
PONTOS_Y = 8


@dataclass(frozen=True)
class Estilo:
    fundo: Tuple[int, int, int] = (0x1A, 0x4F, 0xC8)      # the glass between the dots and around the cells
    aceso: Tuple[int, int, int] = (0xEE, 0xF4, 0xFF)      # a lit dot
    apagado: Tuple[int, int, int] = (0x24, 0x5C, 0xD6)    # an unlit dot, faintly visible as on the glass
    ponto: int = 6        # side of a dot, in pixels (square dots)
    vao_ponto: int = 1    # gap between two dots of one cell
    vao_char: int = 5     # gap between two cells of a row
    vao_linha: int = 7    # gap between the two rows
    margem: int = 10      # border around everything


def celula(estilo):
    """(width, height) of one cell in pixels."""
    return (PONTOS_X * estilo.ponto + (PONTOS_X - 1) * estilo.vao_ponto,
            PONTOS_Y * estilo.ponto + (PONTOS_Y - 1) * estilo.vao_ponto)


def tamanho(estilo=Estilo()):
    """(width, height) of the image: a formula of the style."""
    cw, ch = celula(estilo)
    return (2 * estilo.margem + COLUNAS * cw + (COLUNAS - 1) * estilo.vao_char,
            2 * estilo.margem + LINHAS * ch + (LINHAS - 1) * estilo.vao_linha)


def _aceso(valor, j):
    """Whether dot column j (0 = left) of a glyph row is lit: bit 4 is the leftmost dot."""
    return bool(valor & (0x10 >> j))


def _origem_celula(estilo, linha, coluna):
    """Top-left pixel of the cell at (row, column)."""
    cw, ch = celula(estilo)
    return (estilo.margem + coluna * (cw + estilo.vao_char), estilo.margem + linha * (ch + estilo.vao_linha))


def _origem_ponto(estilo, x0, y0, i, j):
    """Top-left pixel of dot (i, j): row i (0 = top), column j (0 = left) of the cell at (x0, y0)."""
    passo = estilo.ponto + estilo.vao_ponto
    return (x0 + j * passo, y0 + i * passo)


def _glifo(codigo, cgram, fonte, linha, coluna):
    """The 8 rows of the glyph of a code: CGRAM for 0..15 (8..15 mirror 0..7), the font for the rest; an error that names the byte and
    where it is."""
    g = cgram[codigo & 7] if codigo < 16 else fonte.get(codigo)
    if g is None:
        raise ValueError(f'no glyph for byte 0x{codigo:02x} at row {linha}, column {coluna}')
    if len(g) != PONTOS_Y or not all(isinstance(v, int) for v in g):
        raise ValueError(f'the glyph of byte 0x{codigo:02x} at row {linha}, column {coluna} is not 8 integer rows')
    return g


def _valida(linhas, cgram, fonte):
    if not isinstance(linhas, tuple) or len(linhas) != LINHAS or not all(isinstance(l, (bytes, bytearray)) and len(l) == COLUNAS for l in linhas):
        raise ValueError('linhas must be a tuple of two rows of exactly 16 bytes')
    if not isinstance(cgram, Sequence) or len(cgram) != 8 or not all(isinstance(s, (bytes, bytearray)) and len(s) == 8 for s in cgram):
        raise ValueError('cgram must be a sequence of exactly 8 slots of exactly 8 bytes')
    if not isinstance(fonte, Mapping):
        raise ValueError('fonte must be a mapping from a code to its 8 rows')


def celula_em(d, estilo, x0, y0, glifo, apagados=True):
    """Draws one 5 x 8 glyph (8 rows, bit 4 the leftmost dot) as a cell of square dots with its top-left pixel at (x0, y0), on the ImageDraw `d`: a lit
    dot in estilo.aceso, an unlit one in estilo.apagado, or nothing for it when `apagados` is False (a label drawn in the font's dots, not a cell of the
    glass). The geometry of a dot is estilo's (ponto, vao_ponto); its colours too. The manual's diagram of the signal icon draws its cells and its labels
    with it, at sizes of its own."""
    for i in range(PONTOS_Y):
        for j in range(PONTOS_X):
            aceso = _aceso(glifo[i], j)
            if aceso or apagados:
                x, y = _origem_ponto(estilo, x0, y0, i, j)
                d.rectangle((x, y, x + estilo.ponto - 1, y + estilo.ponto - 1), fill=estilo.aceso if aceso else estilo.apagado)   # inclusive corners


def desenhar(linhas, cgram, fonte=None, estilo=Estilo()):
    """The image (RGB) of the glass: `linhas` two rows of 16 codes, `cgram` the 8 custom characters, `fonte` the ROM's glyphs
    (default ROM A00). Raises ValueError for a malformed input or a code with no glyph. Reads its inputs and never changes them."""
    if fonte is None:
        from rom_hd44780 import ROM_A00   # beside this file
        fonte = ROM_A00
    _valida(linhas, cgram, fonte)
    img = Image.new('RGB', tamanho(estilo), estilo.fundo)
    d = ImageDraw.Draw(img)
    for linha in range(LINHAS):
        for coluna in range(COLUNAS):
            g = _glifo(linhas[linha][coluna], cgram, fonte, linha, coluna)
            celula_em(d, estilo, *_origem_celula(estilo, linha, coluna), g)
    return img


def _ler_quadro(caminho):
    with open(caminho, encoding='utf-8') as f:
        q = json.load(f)
    return tuple(bytes.fromhex(h) for h in q['linhas']), [bytes.fromhex(h) for h in q['cgram']]


def _ler_fonte(caminho):
    with open(caminho, encoding='utf-8') as f:
        j = json.load(f)
    if not isinstance(j, dict):
        raise ValueError(f'{caminho}: a font is a JSON object of "<code>": [8 ints], not {type(j).__name__}')
    return {int(k): tuple(v) for k, v in j.items()}


def main(argv=None):
    ap = argparse.ArgumentParser(description='Draws a 16x2 HD44780 LCD from its two DDRAM rows and its CGRAM.')
    ap.add_argument('--quadro', required=True, help='the frame: {"linhas": [32 hex digits x 2], "cgram": [16 hex digits x 8]}')
    ap.add_argument('--out', required=True, help='the PNG to write')
    ap.add_argument('--fonte', help='a font in place of ROM A00: {"<code>": [8 ints], ...}')
    a = ap.parse_args(argv)
    try:
        linhas, cgram = _ler_quadro(a.quadro)
        fonte = _ler_fonte(a.fonte) if a.fonte else None
        desenhar(linhas, cgram, fonte).save(a.out)
    except (OSError, ValueError, KeyError, TypeError) as e:   # json.JSONDecodeError is a ValueError
        print(f'lcd.py: {e}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.exit(main())
