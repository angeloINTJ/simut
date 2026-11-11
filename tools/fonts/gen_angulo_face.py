#!/usr/bin/env python3
"""gen_angulo_face.py — uma face de texto do driver Ângulo (src/ui/angulo/kit/fontes/*.h) a partir de um TTF.

SANS9, SANSB9, MONO9 e MONO12 saem deste script, dos TTF da Liberation 2.1.5 que tools/fonts/liberation.json
fixa; a receita (o TTF, o corpo, o yAdvance e a linha de proveniência de cada face) é tools/fonts/gen_angulo_faces.py,
que o roda uma vez por face. O formato é o do gerador da entrega do designer (gerar_fontes_gfx.js, fora do git),
que rasteriza com o Skia (@napi-rs/canvas) e corta o alfa em 50 %. Este script faz o mesmo com o FreeType
(freetype-py), sem node nem Skia. O que o Skia faz, e o que aqui se repete (medido em 2026-11-11,
@napi-rs/canvas 1.0.11 contra o FreeType 2.13.2 do .venv-tools):

  1. Hinting leve: FT_LOAD_TARGET_LIGHT, que no FreeType 2.13.2 é o autohinter só vertical, também nos TTF
     que trazem instruções (as Liberation trazem: maxSizeOfInstructions 516 em LiberationSans-Regular e 785 em
     LiberationMono-Bold). Com e sem FT_LOAD_FORCE_AUTOHINT as quatro faces saem byte a byte iguais (medido em
     2026-11-11); o script o pede mesmo assim, para que uma versão do FreeType que escolha o interpretador
     TrueType no modo leve não mude a face. O autohinter encaixa a caixa alta e a altura de x na grade: sem
     hinting, a altura medida da caixa alta, do x ou do dígito muda em alguma das quatro faces (o mutante
     "no hinting" de test_gen_angulo_face.py derruba o controle kcapx).
  2. O alfa de um glifo branco passa pela tabela de gama do Skia, a curva sRGB: a cobertura 0x98 vira 0xCB, e
     0x0C vira 0x3D. O limiar de alfa >= 128 equivale a cobertura >= 55 do FreeType, não a 128; com 128 a tinta
     encolheria e a caixa de cada glifo junto.
  3. O avanço é o linear (sem hinting), arredondado.
  4. A caixa do glifo é a da tinta (os pixels acima do limiar); os bits saem MSB primeiro, cada glifo alinhado
     ao byte, 12 bytes por linha, e o glifo vazio (o espaço) tem largura e altura 0.

O que sai daqui, face a face, é o cabeçalho versionado byte a byte: test_gen_angulo_face.py compara os sha256,
e gen_angulo_faces.py --check faz o mesmo no CI.

Uso: gen_angulo_face.py <ttf> <pt> <SIMBOLO> <saida.h> [--face-label TEXTO] [--y-advance N] [--provenance TEXTO]
                        [--extra ARQUIVO.json]

--face-label TEXTO  o nome da face na primeira linha do comentário (o padrão é o nome do TTF sem a extensão).
--y-advance N       grava N (1 a 255) como yAdvance do GFXfont no lugar de round(pt * dpi / 72 * 1,25), a regra
                    da entrega (22 aos 9 pt, 29 aos 12 pt); nada mais no arquivo muda.
--provenance TEXTO  grava `// TEXTO` como a PRIMEIRA linha (o TTF, a versão, o sha256) e troca o comentário que
                    cita o gerador da entrega por um que cita este script e a tabela EXTRA. TEXTO é uma linha de
                    ASCII imprimível sem barra invertida. Sem a opção o cabeçalho é o da entrega, letra por
                    letra; o controle provenance do teste confere as duas formas.
--extra ARQUIVO     a tabela EXTRA (códigos 127..176 -> caractere); o padrão é angulo_extra.json, ao lado.

As opções podem estar em qualquer posição; sem valor, repetidas, na forma --opcao=valor ou com valor fora do
intervalo o script sai 2 e não grava o arquivo. Um glifo que o TTF não tem (o FreeType desenharia o .notdef sem
avisar) ou uma opção recusada sai 1, escreve `missing glyphs: U+XXXX ...` ou a recusa em stderr e também não grava.
Duas execuções dão bytes iguais; FREETYPE_PROPERTIES é ignorada (ver abaixo).

A receita das quatro faces é tools/fonts/gen_angulo_faces.py; uma face à mão, como ela a gera:
  gen_angulo_face.py LiberationSans-Regular.ttf 9 SANS9 SANS9.h --y-advance 22 --provenance "<TTF, versão, sha256, licença>"
"""
import json
import math
import os
import re
import sys

sys.dont_write_bytecode = True  # não deixa o .pyc do subset_font em tools/
# FREETYPE_PROPERTIES muda o que o autohinter desenha (autofitter:no-stem-darkening=0 escurece as hastes: na linha do
# meio do 'H' do SANS9, a borda da haste direita passa de 0x4C a 0x6C, medido em 2026-11-11 na LiberationSans-Regular),
# e a face não pode depender do ambiente de quem a gera.
os.environ.pop('FREETYPE_PROPERTIES', None)
import freetype  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from subset_font import comment_line  # noqa: E402  (a mesma recusa de linha ruim do --provenance dele)

DPI = 141
ALFA_MIN = 128
PRIMEIRO, ULTIMO = 32, 176
LINHA = 12
FLAGS = freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_FORCE_AUTOHINT | freetype.FT_LOAD_NO_BITMAP
EXTRA_PADRAO = os.path.join(HERE, 'angulo_extra.json')
USO = ('Uso: gen_angulo_face.py <ttf> <pt> <SIMBOLO> <saida.h> '
       '[--face-label TEXTO] [--y-advance N] [--provenance TEXTO] [--extra ARQUIVO.json]')
OPCOES = ('--face-label', '--y-advance', '--provenance', '--extra')


class Ausentes(Exception):
    """O TTF não tem glifo para estes caracteres (`chars`, na ordem da tabela)."""

    def __init__(self, chars):
        super().__init__(chars)
        self.chars = chars


def _alfa_do_skia(cobertura):
    """O alfa que o Skia grava para texto branco, dada a cobertura de 8 bits do FreeType: o SkMaskGamma de
    luminância 255 com gama sRGB (o contraste não entra: ele vai a 0 quando o texto é branco)."""
    x = cobertura / 255.0
    y = x * 12.92 if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
    return int(255.0 * y + 0.5)


_ALFA = [_alfa_do_skia(i) for i in range(256)]


def carregar_extra(caminho):
    """[(codigo, caractere)] da tabela EXTRA; ValueError se o arquivo não é essa tabela."""
    try:
        with open(caminho, encoding='utf-8') as f:
            entradas = json.load(f)['entries']
        pares = [(e['code'], e['char']) for e in entradas]
        bom = all(isinstance(e['code'], int) and isinstance(e['char'], str) and len(e['char']) == 1
                  and e['unicode'] == 'U+%04X' % ord(e['char']) for e in entradas)
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise ValueError('%s: not the EXTRA table (%s: %s)' % (caminho, type(e).__name__, e))
    if not bom or [c for c, _ in pares] != list(range(127, ULTIMO + 1)) or len({ch for _, ch in pares}) != len(pares):
        raise ValueError('%s: the EXTRA table holds the codes 127..%d in order, one distinct character each, '
                         'with its unicode' % (caminho, ULTIMO))
    return pares


def rasterizar(face, ch, limiar):
    """O glifo de `ch`: caixa da tinta, deslocamentos, avanço e os bits (0/1, linha a linha) da caixa."""
    face.load_char(ord(ch), FLAGS)
    g = face.glyph
    avanco = math.floor(g.linearHoriAdvance / 65536.0 + 0.5)
    g.render(freetype.FT_RENDER_MODE_NORMAL)
    bmp = g.bitmap
    buf = list(bmp.buffer)  # bmp.buffer monta a lista inteira a cada acesso
    tinta = {(x, y) for y in range(bmp.rows) for x in range(bmp.width) if _ALFA[buf[y * bmp.pitch + x]] >= limiar}
    if not tinta:
        return {'w': 0, 'h': 0, 'xa': avanco, 'xo': 0, 'yo': 0, 'bits': []}
    x0, x1 = min(x for x, _ in tinta), max(x for x, _ in tinta)
    y0, y1 = min(y for _, y in tinta), max(y for _, y in tinta)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    bits = [1 if (x0 + i, y0 + j) in tinta else 0 for j in range(h) for i in range(w)]
    return {'w': w, 'h': h, 'xa': avanco, 'xo': g.bitmap_left + x0, 'yo': y0 - g.bitmap_top, 'bits': bits}


def empacotar(bits):
    """Os bits em bytes, MSB primeiro, completando o último byte com zeros."""
    saida = []
    for i in range(0, len(bits), 8):
        b = 0
        for k in range(8):
            b = (b << 1) | (bits[i + k] if i + k < len(bits) else 0)
        saida.append(b)
    return saida


def gerar(ttf, pt, simbolo, rotulo=None, y_advance=None, provenance=None, extra=None, dpi=DPI, limiar=ALFA_MIN):
    """(texto do cabeçalho, info) da face; ValueError ou Ausentes se não dá, antes de qualquer arquivo.

    info: cap, xh e digito (altura da tinta do 'H', do 'x' e do '0', o que AnguloMeta.h guarda em kCapX),
    y_advance, bitmap_bytes e glifos."""
    if not re.fullmatch(r'[A-Za-z_]\w*', simbolo):
        raise ValueError('the symbol must be a C identifier: %r' % simbolo)
    if not 1 <= pt <= 255 or (y_advance is not None and not 1 <= y_advance <= 255):
        raise ValueError('the size and the yAdvance go from 1 to 255')
    primeira = None if provenance is None else comment_line(provenance)
    if rotulo is None:
        rotulo = os.path.splitext(os.path.basename(ttf))[0]
    if not re.fullmatch(r'[ -~]+', rotulo) or '\\' in rotulo:
        raise ValueError('--face-label must be one line of printable ASCII without a backslash')
    pares = [(c, chr(c)) for c in range(PRIMEIRO, 127)] + carregar_extra(EXTRA_PADRAO if extra is None else extra)
    try:
        face = freetype.Face(ttf)
    except Exception as e:  # FT_Exception ou OSError: o TTF não abre
        raise ValueError('%s: %s' % (ttf, e))
    faltam = [ch for ch in dict.fromkeys(ch for _, ch in pares) if face.get_char_index(ord(ch)) == 0]
    if faltam:
        raise Ausentes(faltam)
    face.set_char_size(pt << 6, 0, dpi, 0)

    bitmaps, linhas_glifos, por = [], [], {}
    for code, ch in pares:
        g = rasterizar(face, ch, limiar)
        por[ch] = g
        linhas_glifos.append("  { %d, %d, %d, %d, %d, %d },  // 0x%02X '%s'"
                             % (len(bitmaps), g['w'], g['h'], g['xa'], g['xo'], g['yo'], code, ch))
        bitmaps.extend(empacotar(g['bits']))
    cap, xh, dig = (por[c]['h'] for c in 'Hx0')
    padrao = math.floor(pt * dpi / 72 * 1.25 + 0.5)
    y_adv = padrao if y_advance is None else y_advance

    if provenance is None:
        cab = ['// %s: %s %d pt a %d dpi, limiar de 50 %% — gerada pelo mesmo' % (simbolo, rotulo, pt, dpi),
               '// rasterizador das telas de referencia (gerar_fontes_gfx.js). Nao editar a mao.']
        onde = 'ver fontes.json'
    else:
        cab = [primeira,
               '// %s: %s %d pt a %d dpi, limiar de 50 %% — gerada por' % (simbolo, rotulo, pt, dpi),
               '// tools/fonts/gen_angulo_face.py, no FreeType e com o limiar do rasterizador das telas de '
               'referencia. Nao editar a mao.']
        onde = 'ver tools/fonts/angulo_extra.json'
    cab += ['// Caixa alta %d px, altura de x %d px, digito %d px.' % (cap, xh, dig),
            '// Codigos: %d..%d (0x7F em diante = tabela EXTRA, %s).' % (PRIMEIRO, ULTIMO, onde)]
    linhas_bitmap = ['  ' + ', '.join('0x%02X' % b for b in bitmaps[i:i + LINHA]) + ','
                     for i in range(0, len(bitmaps), LINHA)]
    texto = '\n'.join(cab + [
        '#pragma once',
        '#include <Adafruit_GFX.h>',
        '',
        'const uint8_t %sBitmaps[] PROGMEM = {' % simbolo,
        *linhas_bitmap,
        '};',
        '',
        'const GFXglyph %sGlyphs[] PROGMEM = {' % simbolo,
        *linhas_glifos,
        '};',
        '',
        'const GFXfont %s PROGMEM = { (uint8_t *)%sBitmaps, (GFXglyph *)%sGlyphs, %d, %d, %d };'
        % (simbolo, simbolo, simbolo, PRIMEIRO, ULTIMO, y_adv),
        ''])
    return texto, {'cap': cap, 'xh': xh, 'digito': dig, 'y_advance': y_adv,
                   'bitmap_bytes': len(bitmaps), 'glifos': len(pares)}


def usage():
    print(USO, file=sys.stderr)
    sys.exit(2)


def parse_args(argv):
    """Os quatro posicionais e as opções (cada uma com um valor, em qualquer posição); o que sobra sai em usage()."""
    pos, achadas = [], {}
    it = iter(argv)
    for arg in it:
        if arg not in OPCOES:
            pos.append(arg)
            continue
        valor = next(it, None)
        if valor is None or arg in achadas:
            usage()
        achadas[arg] = valor
    if len(pos) != 4 or not (pos[1].isascii() and pos[1].isdecimal() and 1 <= int(pos[1]) <= 255):
        usage()
    y = achadas.get('--y-advance')
    if y is not None and not (y.isascii() and y.isdecimal() and 1 <= int(y) <= 255):
        usage()
    return pos, achadas, None if y is None else int(y)


def main(argv):
    pos, opcoes, y_advance = parse_args(argv)
    ttf, pt, simbolo, saida = pos[0], int(pos[1]), pos[2], pos[3]
    try:
        texto, info = gerar(ttf, pt, simbolo, rotulo=opcoes.get('--face-label'), y_advance=y_advance,
                            provenance=opcoes.get('--provenance'), extra=opcoes.get('--extra'))
    except Ausentes as e:
        print('missing glyphs: ' + ' '.join('U+%04X' % ord(ch) for ch in e.chars), file=sys.stderr)
        return 1
    except ValueError as e:
        print('gen_angulo_face: %s' % e, file=sys.stderr)
        return 1
    with open(saida, 'wb') as f:
        f.write(texto.encode('utf-8'))
    print('%s: %d glyphs, bitmaps %d B, yAdvance %d; caixa alta %d px, altura de x %d px, digito %d px'
          % (saida, info['glifos'], info['bitmap_bytes'], info['y_advance'], info['cap'], info['xh'], info['digito']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
