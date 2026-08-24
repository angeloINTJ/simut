#!/usr/bin/env python3
r"""§5.14 Display/TFT — área segura (PLANO-VALIDACAO-v2.3.2-stable.md).

Analisa as capturas de framebuffer produzidas por tools/screen_mapper.py
(pixels do painel via GET /api/screenshot):

  Limite do plano: conteúdo em x 4..315, y 4..235; borda 100% fundo;
  19 capturas.

Regras por captura:
  - borda = os 4 px externos (x<4, x>315, y<4, y>235, painel 320×240);
  - fundo = cor modal da borda (o UI desenha sobre o fundo do tema);
  - qualquer pixel da borda ≠ fundo  → violação (borda não é 100% fundo);
  - qualquer pixel ≠ fundo fora de x∈[4,315], y∈[4,235] → violação
    (conteúdo fora da área segura) — coberto pela regra anterior, mas
    reportado separado por clareza.

Uso:  python3 tools/screen_safe_area.py --dir DIR [--out out.json]
"""
import argparse
import json
from collections import Counter
from pathlib import Path

from PIL import Image


def analyze(path):
    img = Image.open(path).convert('RGB')
    w, h = img.size
    px = img.load()
    border = []
    for x in range(w):
        for y in list(range(0, 4)) + list(range(h - 4, h)):
            border.append(px[x, y])
    for y in range(4, h - 4):
        for x in list(range(0, 4)) + list(range(w - 4, w)):
            border.append(px[x, y])
    bg = Counter(border).most_common(1)[0][0]

    border_viol = sum(1 for p in border if p != bg)
    outside = 0
    inside = 0
    for y in range(4, h - 4):
        for x in range(4, w - 4):
            if px[x, y] != bg:
                inside += 1
    # conteúdo fora da área segura: pixels ≠ fundo na zona x>315|y>235 —
    # é exatamente a borda já contada; aqui só contamos os do anel interno
    # (impossível por construção) e reportamos a cobertura.
    content = {
        'file': str(path),
        'size': [w, h],
        'bg': list(bg),
        'border_pixels': len(border),
        'border_violations': border_viol,
        'content_pixels_in_safe_area': inside,
        'ok': border_viol == 0,
    }
    return content


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', required=True)
    ap.add_argument('--out', default=None)
    args = ap.parse_args()
    files = sorted(Path(args.dir).glob('*.png'))
    if not files:
        print(f'FATAL: nenhum .png em {args.dir}')
        return 2
    results = [analyze(f) for f in files]
    ok = sum(1 for r in results if r['ok'])
    for r in results:
        print(f"  [{'PASS' if r['ok'] else 'FAIL'}] {r['file']} "
              f"bg={r['bg']} viol={r['border_violations']} "
              f"conteudo={r['content_pixels_in_safe_area']}px")
    print(f'\nscreen_safe_area: {ok}/{len(results)} capturas com borda 100% fundo '
          f'({len(results)} capturas analisadas)')
    if args.out:
        Path(args.out).write_text(json.dumps(results, indent=2))
        print(f'saída: {args.out}')
    return 0 if ok == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
