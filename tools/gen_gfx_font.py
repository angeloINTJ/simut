#!/usr/bin/env python3
"""Porte fiel do fontconvert.c (Adafruit GFX) para Python/freetype-py.

Mesmos parâmetros do original: DPI 141, FT_LOAD_TARGET_MONO +
FT_RENDER_MODE_MONO, bits MSB-first contínuos, yOffset = 1 - bitmap_top,
yAdvance = face.size.height >> 6. Gera cobertura Latin-1 (0x20..0xFF)
para acentos pt-BR/es-ES e o ° tipográfico.

Uso: gen_gfx_font.py <ttf> <size> <first> <last> <StructName> <out.h> [--require CHARS] [--y-advance N]

--require CHARS (em qualquer posição da linha) lista os caracteres que o TTF precisa ter. Sem o
glifo, load_char desenha o .notdef e não avisa; com a opção, se faltar algum o script escreve em
stderr `missing glyphs: U+XXXX ...` (todos, na ordem de CHARS), sai com 1 e não grava o arquivo.

--y-advance N (em qualquer posição da linha) grava N como yAdvance do GFXfont no lugar de
face.size.height >> 6; N é um inteiro decimal de 1 a 255 (o campo é um uint8_t) e nada mais no arquivo
muda. Sem a opção a saída é a de sempre. Os cabeçalhos do painel original têm yAdvance 22 (9 pt), 29
(12 pt) e 56 (24 pt): o Adafruit_GFX soma o yAdvance a cada nova linha e a cada quebra automática, e
o leiaute do painel foi desenhado com esses valores. A Liberation Sans Bold mede 20, 27 e 54 nos
mesmos tamanhos (FreeType 2.13.2, 2026-11-09), então a face trocada passa o valor do cabeçalho que
substitui.

As três faces do painel original (src/ui/original/SansBold*.h), da Liberation Sans Bold 2.1.5, não têm receita em texto: tools/fonts/gen_original_faces.py DIR
as regera com este script e o subset_font.py, do TTF que tools/fonts/fetch_liberation.py confere contra o pino, e `gen_original_faces.py --check DIR` as compara
byte a byte com os cabeçalhos do repositório. A tabela delas (tamanho, faixa, yAdvance e glifos mantidos) mora lá, uma vez só. O interpretador precisa de
freetype-py==2.5.1 (FreeType 2.13.2 em 2026-11-09): os bits vêm da rasterização do FreeType, e outra versão pode mudá-los sem que nada no repositório mude.
Ao trocar de TTF, tools/compare_gfx_fonts.py ANTIGO.h NOVO.h lista o que muda entre os dois cabeçalhos.
"""
import sys
import freetype

DPI = 141
USO = ('Uso: gen_gfx_font.py <ttf> <size> <first> <last> <StructName> <out.h> '
       '[--require CHARS] [--y-advance N]')
OPCOES = ('--require', '--y-advance')

def usage():
    print(USO, file=sys.stderr)
    sys.exit(2)

def parse_args(argv):
    """Os seis argumentos posicionais, o CHARS de --require e o N de --y-advance, que podem estar em qualquer posição.

    --require=CHARS, --require sem a lista, --require repetido e posicional a mais ou a menos
    saem em usage(): uma conferência que a linha de comando pula em silêncio não confere nada.
    O mesmo vale para --y-advance, e um N que não seja inteiro decimal de 1 a 255 também: um valor
    fora do uint8_t do GFXfont seria gravado e quebraria só na compilação.
    """
    pos, found = [], {}
    it = iter(argv)
    for arg in it:
        if arg not in OPCOES:
            pos.append(arg)
            continue
        value = next(it, None)
        if value is None or arg in found:
            usage()
        found[arg] = value
    if len(pos) != 6:
        usage()
    y_advance = found.get('--y-advance')
    if y_advance is not None:
        if not (y_advance.isascii() and y_advance.isdecimal() and 1 <= int(y_advance) <= 255):
            usage()
        y_advance = int(y_advance)
    return pos, found.get('--require'), y_advance

def main():
    args, require, y_advance_given = parse_args(sys.argv[1:])
    ttf, size, first, last, name, out = (
        args[0], int(args[1]), int(args[2], 0), int(args[3], 0), args[4], args[5])

    face = freetype.Face(ttf)
    # Antes de qualquer saída: o arquivo só nasce de um TTF que tem tudo o que se exigiu.
    missing = [ch for ch in dict.fromkeys(require or '') if face.get_char_index(ord(ch)) == 0]
    if missing:
        print('missing glyphs: ' + ' '.join(f'U+{ord(ch):04X}' for ch in missing), file=sys.stderr)
        sys.exit(1)
    face.set_char_size(size << 6, 0, DPI, 0)

    bitmap_bytes = []
    glyphs = []
    bit_acc = 0
    bit_n = 0
    offset = 0

    def put_bit(b):
        nonlocal bit_acc, bit_n, offset
        bit_acc = (bit_acc << 1) | (1 if b else 0)
        bit_n += 1
        if bit_n == 8:
            bitmap_bytes.append(bit_acc)
            bit_acc = 0
            bit_n = 0
            offset += 1

    def flush_bits():
        nonlocal bit_acc, bit_n, offset
        if bit_n:
            bitmap_bytes.append(bit_acc << (8 - bit_n))
            bit_acc = 0
            bit_n = 0
            offset += 1

    for code in range(first, last + 1):
        face.load_char(code, freetype.FT_LOAD_TARGET_MONO)
        face.glyph.render(freetype.FT_RENDER_MODE_MONO)
        g = face.glyph
        bmp = g.bitmap
        start = offset
        # bits MSB-first, linha a linha, SEM alinhamento por linha (igual ao C)
        for y in range(bmp.rows):
            for x in range(bmp.width):
                byte = bmp.buffer[y * bmp.pitch + (x >> 3)]
                put_bit(byte & (0x80 >> (x & 7)))
        flush_bits()
        glyphs.append({
            'code': code,
            'off': start,
            'w': bmp.width,
            'h': bmp.rows,
            'xa': g.advance.x >> 6,
            'xo': g.bitmap_left,
            'yo': 1 - g.bitmap_top,
        })

    y_advance = face.size.height >> 6 if y_advance_given is None else y_advance_given

    lines = []
    lines.append(f'const uint8_t {name}Bitmaps[] PROGMEM = {{')
    for i in range(0, len(bitmap_bytes), 12):
        chunk = ', '.join(f'0x{b:02X}' for b in bitmap_bytes[i:i+12])
        sep = ',' if i + 12 < len(bitmap_bytes) else ''
        lines.append(f'  {chunk}{sep}')
    lines.append('};\n')

    lines.append(f'const GFXglyph {name}Glyphs[] PROGMEM = {{')
    for gi, g in enumerate(glyphs):
        c = g['code']
        label = chr(c) if 0x20 <= c <= 0x7E else f'0x{c:02X}'
        if label == '\\':
            label = 'backslash'
        sep = ',' if gi < len(glyphs) - 1 else ''
        lines.append(
            f"  {{ {g['off']:5d}, {g['w']:3d}, {g['h']:3d}, {g['xa']:3d}, "
            f"{g['xo']:4d}, {g['yo']:4d} }}{sep} // 0x{c:02X} '{label}'")
    lines.append('};\n')

    lines.append(f'const GFXfont {name} PROGMEM = {{')
    lines.append(f'  (uint8_t  *){name}Bitmaps,')
    lines.append(f'  (GFXglyph *){name}Glyphs,')
    lines.append(f'  0x{first:02X}, 0x{last:02X}, {y_advance} }};\n')
    lines.append(f'// Approx. {len(bitmap_bytes) + len(glyphs)*7 + 7} bytes\n')

    with open(out, 'w') as f:
        f.write('\n'.join(lines))
    print(f'{out}: {len(glyphs)} glyphs, bitmaps {len(bitmap_bytes)} B, '
          f'total ~{len(bitmap_bytes) + len(glyphs)*7 + 7} B, yAdvance {y_advance}')

if __name__ == '__main__':
    main()
