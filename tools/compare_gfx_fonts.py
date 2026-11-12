#!/usr/bin/env python3
"""compare_gfx_fonts.py — o que muda entre dois cabeçalhos GFXfont (Adafruit_GFX).

Para cada código presente nas duas faixas, imprime uma linha pelo glifo cujo xAdvance, yOffset,
width ou height difere (só os campos que diferem); depois o yAdvance, se difere, e sempre o
resumo. Os glifos se alinham pelo código, não pela posição; os bitmaps e o xOffset não entram.
Lê com parse_font_header, de subset_font.py, e não escreve arquivos.

Uso: python3 tools/compare_gfx_fonts.py A.h B.h
Saída: 0 se nada difere, 1 se algo difere (um glifo ou o yAdvance), 2 se não leu um dos
cabeçalhos (mensagem em stderr).
"""
import os
import sys

sys.dont_write_bytecode = True  # não escreve arquivos: nem o .pyc do subset_font
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from subset_font import parse_font_header  # noqa: E402

FIELDS = ("xAdvance", "yOffset", "width", "height")


def read_font(path):
    """(first, last, glyphs, line_h) do cabeçalho em `path`; OSError ou ValueError se não dá para ler."""
    with open(path, encoding="utf-8") as f:
        content = f.read()
    _, glyphs, _, _, _, first, last, line_h = parse_font_header(content)
    # glyphs é indexada por código menos first: sem um glifo por código, o alinhamento mentiria.
    if len(glyphs) != last - first + 1:
        raise ValueError(f"{len(glyphs)} glyphs in the table for the range "
                         f"U+{first:04X}..U+{last:04X}")
    return first, last, glyphs, line_h


def main(argv):
    if len(argv) != 3:
        print("Uso: compare_gfx_fonts.py A.h B.h", file=sys.stderr)
        return 2
    fonts = []
    for path in argv[1:]:
        try:
            fonts.append(read_font(path))
        except (OSError, ValueError) as e:
            why = (e.strerror if isinstance(e, OSError) else None) or e
            print(f"compare_gfx_fonts: {path}: {why}", file=sys.stderr)
    if len(fonts) != 2:
        return 2
    (first_a, last_a, glyphs_a, line_a), (first_b, last_b, glyphs_b, line_b) = fonts

    lo, hi = max(first_a, first_b), min(last_a, last_b)
    if lo > hi:
        # Sem nada a comparar, "0 glyph(s) differ" seria uma resposta falsa.
        print(f"compare_gfx_fonts: no code in common: U+{first_a:04X}..U+{last_a:04X} "
              f"and U+{first_b:04X}..U+{last_b:04X}", file=sys.stderr)
        return 2

    differ = 0
    for code in range(lo, hi + 1):
        a, b = glyphs_a[code - first_a], glyphs_b[code - first_b]
        changes = [f"{field} {a[field]} -> {b[field]}" for field in FIELDS if a[field] != b[field]]
        if changes:
            differ += 1
            # ascii(): a saída não depende da codificação do stdout (Latin-1 tem º, ª, ÿ...).
            print(f"U+{code:04X} {ascii(chr(code))}: {', '.join(changes)}")
    if line_a != line_b:
        print(f"yAdvance {line_a} -> {line_b}")
    print(f"{differ} glyph(s) differ, yAdvance {line_a} -> {line_b}")
    return 1 if differ or line_a != line_b else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
