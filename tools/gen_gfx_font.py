#!/usr/bin/env python3
"""Porte fiel do fontconvert.c (Adafruit GFX) para Python/freetype-py.

Mesmos parâmetros do original: DPI 141, FT_LOAD_TARGET_MONO +
FT_RENDER_MODE_MONO, bits MSB-first contínuos, yOffset = 1 - bitmap_top,
yAdvance = face.size.height >> 6. Gera cobertura Latin-1 (0x20..0xFF)
para acentos pt-BR/es-ES e o ° tipográfico.

Uso: gen_gfx_font.py <ttf> <size> <first> <last> <StructName> <out.h>

Receita das fontes Latin-1 do TFT (FreeSansBold9pt8b_latin1.h / 12pt):
  1. TTF: https://ftp.gnu.org/gnu/freefont/freefont-ttf-20120503.zip
  2. pip install freetype-py (venv)
  3. gen_gfx_font.py FreeSansBold.ttf 9 0x20 0xFF FreeSansBold9pt8b out9.h
     (idem 12pt; conferir yAdvance legado: 9pt=22, 12pt=29)
  4. tools/subset_font.py out9.h src/FreeSansBold9pt8b_latin1.h "<ASCII+acentos pt/es>"
"""
import sys
import freetype

DPI = 141

def main():
    ttf, size, first, last, name, out = (
        sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 0), int(sys.argv[4], 0),
        sys.argv[5], sys.argv[6])

    face = freetype.Face(ttf)
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

    y_advance = face.size.height >> 6

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
