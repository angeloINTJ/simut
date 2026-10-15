#!/usr/bin/env python3
"""Draws one group of the functional build's screens on the host and checks them.

Compiles the Ângulo kit (src/ui/angulo/Angulo.cpp), the shared screen code
(src/ui/angulo/telas/Telas.cpp and Base.cpp), the group's screens (src/ui/angulo/telas/
<Grupo>*.cpp) and its samples (tools/tft_ui/amostras/<grupo>.cpp) with Adafruit
GFX 1.12.6, draws every sample in both themes, and fails when a sample:

  - asks the kit for a glyph or an icon it does not have;
  - draws text outside the panel's safe area (x 4..315, y 4..235: the display
    alignment moves the image up to 4 px, DisplayManager.h);
  - draws two strings whose ink boxes overlap;
  - draws a text across the edge of one of its touch targets;
  - draws a text cut with "…" that tools/tft_ui/cortes_aceitos.txt does not
    accept for that sample (fixed copy must fit, and so must data of an
    ordinary length; a text the screen's own copy ends with "…", such as
    "Lendo o histórico…", is not a cut), or the list accepts a cut that no
    longer happens;
  - lists a touch target outside the panel.

It warns (without failing) on a target smaller than 24 px either way and on
two targets that overlap. With --out it keeps a PNG per frame, an overlay of
the targets per sample and a contact sheet of the group (_folha.png).

Usage: python3 tools/tft_ui/host_telas.py <grupo> [--out <folder>]
Exit 0 clean, 1 when a check fails, 2 when it cannot build or run.
"""
import argparse
import codecs
import fnmatch
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
SHIM = HERE / 'host' / 'shim'
SAFE = (4, 316, 4, 236)          # x0, x1 (exclusive), y0, y1 (exclusive)
MIN_ALVO = 24
RETIC = '…'
ACEITOS = HERE / 'cortes_aceitos.txt'

sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (same folder; the GFX lookup is shared)


def build(grupo, tmp):
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found', file=sys.stderr)
        return None
    telas = ROOT / 'src' / 'ui' / 'angulo' / 'telas'
    prefixo = grupo[:1].upper() + grupo[1:]
    proprios = sorted(telas.glob(f'{prefixo}*.cpp'))
    amostras = HERE / 'amostras' / f'{grupo}.cpp'
    if not amostras.is_file():
        print(f'{amostras} not found', file=sys.stderr)
        return None
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{SHIM}', f'-I{gfx}',
           f'-I{ROOT / "src" / "ui" / "angulo"}', f'-I{telas}', f'-I{HERE / "amostras"}']
    strict = ['-Wall', '-Wextra', '-Werror']
    units = [(gfx / 'Adafruit_GFX.cpp', ['-w']),
             (ROOT / 'src' / 'ui' / 'angulo' / 'Angulo.cpp', strict),
             (telas / 'Telas.cpp', strict),
             # The result and confirmation screens every group may lean on.
             *([(telas / 'Base.cpp', strict)] if grupo != 'base' else []),
             *[(f, strict) for f in proprios],
             (amostras, strict),
             (HERE / 'host' / 'host_telas_main.cpp', strict)]
    objs = []
    for src, flags in units:
        obj = tmp / (src.parent.name + '_' + src.stem + '.o')
        r = subprocess.run(['g++', *inc, *flags, '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr, file=sys.stderr)
            return None
        objs.append(str(obj))
    exe = tmp / 'telas_host'
    r = subprocess.run(['g++', *objs, '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr, file=sys.stderr)
        return None
    return exe


def ler_565(path):
    from PIL import Image
    raw = path.read_bytes()
    img = Image.new('RGB', (320, 240))
    px = img.load()
    for i in range(320 * 240):
        v = raw[2 * i] | (raw[2 * i + 1] << 8)
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        px[i % 320, i // 320] = (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    return img


def sobrepoe(a, b):
    return min(a[1], b[1]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[2], b[2])


def literais():
    """The driver's string literals, decoded (escapes, and adjacent literals joined as the compiler
    joins them): a text drawn exactly as one of them carries its own "…" and is not a cut. The
    font tables are skipped: their comments quote the '"' glyph, which this scanner cannot tell
    from the start of a literal."""
    out = set()
    for f in sorted((ROOT / 'src' / 'ui' / 'angulo').rglob('*')):
        if f.suffix not in ('.cpp', '.h') or 'fontes' in f.parts:
            continue
        dados = f.read_bytes()
        corpo, fim = [], None
        for m in re.finditer(rb'"((?:[^"\\\n]|\\.)*)"', dados):
            if corpo and dados[fim:m.start()].strip():
                out.add(codecs.escape_decode(b''.join(corpo))[0].decode('utf-8', 'replace'))
                corpo = []
            corpo.append(m.group(1))
            out.add(codecs.escape_decode(m.group(1))[0].decode('utf-8', 'replace'))
            fim = m.end()
        if corpo:
            out.add(codecs.escape_decode(b''.join(corpo))[0].decode('utf-8', 'replace'))
    return out


def aceitos(grupo):
    """The cuts tools/tft_ui/cortes_aceitos.txt accepts in this group: (sample glob, start of the
    drawn text, line number). None when a line is malformed."""
    out = []
    for n, linha in enumerate(ACEITOS.read_text(encoding='utf-8').splitlines(), 1):
        if not linha.strip() or linha.startswith('#'):
            continue
        partes = [p.strip() for p in linha.split('|')]
        if len(partes) != 4 or not all(partes):
            print(f'{ACEITOS.name}:{n}: expected "<group> | <sample glob> | <start of the drawn text> | <why>"',
                  file=sys.stderr)
            return None
        if partes[0] == grupo:
            out.append((partes[1], partes[2], n))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('grupo')
    ap.add_argument('--out', help='keep the PNGs, the target overlays and the contact sheet here')
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='telas_host_') as tmp:
        tmp = pathlib.Path(tmp)
        exe = build(args.grupo, tmp)
        if not exe:
            return 2
        dados = tmp / 'dados'
        dados.mkdir()
        r = subprocess.run([str(exe), str(dados)], capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip(), file=sys.stderr)
            return 2
        erros, avisos = [], []
        regras = aceitos(args.grupo)
        if regras is None:
            return 2
        proprios, usadas, cortes = literais(), set(), 0
        nomes = sorted({p.name[:-len('.alvos')] for p in dados.glob('*.alvos')})
        for nome in nomes:
            alvos = []
            for linha in (dados / f'{nome}.alvos').read_text().splitlines():
                x, y, w, h, tipo, ind = map(int, linha.split()[:6])
                alvos.append((x, y, w, h, tipo, ind))
                if x < 0 or y < 0 or x + w > 320 or y + h > 240:
                    erros.append(f'{nome}: target {tipo}/{ind} at {x},{y} {w}x{h} leaves the panel')
                if w < MIN_ALVO or h < MIN_ALVO:
                    avisos.append(f'{nome}: target {tipo}/{ind} is {w}x{h} (< {MIN_ALVO} px)')
            for i in range(len(alvos)):
                for j in range(i + 1, len(alvos)):
                    a, b = alvos[i], alvos[j]
                    if sobrepoe((a[0], a[0] + a[2], a[1], a[1] + a[3]), (b[0], b[0] + b[2], b[1], b[1] + b[3])):
                        avisos.append(f'{nome}: targets {a[4]}/{a[5]} and {b[4]}/{b[5]} overlap')
            for tema in ('escuro', 'claro'):
                falta = int((dados / f'{nome}_{tema}.faltam').read_text().strip() or 0)
                if falta:
                    erros.append(f'{nome}_{tema}: {falta} glyphs or icons missing')
                textos = []
                for linha in (dados / f'{nome}_{tema}.textos').read_text().splitlines():
                    partes = linha.split(' ', 4)
                    x0, x1, y0, y1 = map(int, partes[:4])
                    t = partes[4] if len(partes) > 4 else ''
                    textos.append((x0, x1, y0, y1, t))
                    if x0 < SAFE[0] or x1 > SAFE[1] or y0 < SAFE[2] or y1 > SAFE[3]:
                        erros.append(f'{nome}_{tema}: "{t}" ink {x0}..{x1} x {y0}..{y1} leaves the safe area')
                for i in range(len(textos)):
                    for j in range(i + 1, len(textos)):
                        if textos[i] == textos[j]:
                            continue
                        if sobrepoe(textos[i], textos[j]):
                            erros.append(f'{nome}_{tema}: "{textos[i][4]}" overlaps "{textos[j][4]}"')
                # A text that crosses a target's edge has outgrown its button or row: the
                # 9 pt floor of 2026-10-15 cut "Gráfico" on both sides of a 56 px button,
                # and neither check above saw it (the text stayed on the glass and clear
                # of the other texts).
                for x0, x1, y0, y1, t in textos:
                    for ax, ay, aw, ah, tipo, ind in alvos:
                        caixa = (ax, ax + aw, ay, ay + ah)
                        dentro = x0 >= caixa[0] and x1 <= caixa[1] and y0 >= caixa[2] and y1 <= caixa[3]
                        if sobrepoe((x0, x1, y0, y1), caixa) and not dentro:
                            erros.append(f'{nome}_{tema}: "{t}" ink {x0}..{x1} x {y0}..{y1} crosses the edge of '
                                         f'target {tipo}/{ind} at {ax},{ay} {aw}x{ah}')
                # A text cut with "…" stays on the glass and inside its box, so no check above
                # sees it: at the 9 pt floor (2026-10-15) the PIN screen's hint and four error
                # messages lost their ends this way, and only an eye on the frames caught it.
                for x0, x1, y0, y1, t in textos:
                    if RETIC not in t or t in proprios:
                        continue
                    regra = next((r for r in regras if fnmatch.fnmatchcase(nome, r[0]) and t.startswith(r[1])), None)
                    if regra:
                        usadas.add(regra[2])
                        cortes += 1
                    else:
                        erros.append(f'{nome}_{tema}: "{t}" is cut with "…" ({ACEITOS.name} lists the cuts a '
                                     f'sample makes on purpose, with data too long to fit)')
        for glob, inicio, n in regras:
            if n not in usadas:
                erros.append(f'{ACEITOS.name}:{n}: accepts a cut of "{inicio}" in {glob} that no longer happens '
                             f'(remove the line)')
        for a in avisos:
            print('warning:', a)
        for e in erros:
            print('FAIL:', e)
        print(f'{len(nomes)} samples, {len(erros)} failures, {len(avisos)} warnings, {cortes} accepted cuts')
        if args.out:
            from PIL import Image, ImageDraw
            out = pathlib.Path(args.out)
            out.mkdir(parents=True, exist_ok=True)
            linhas = []
            for nome in nomes:
                quadros = []
                for tema in ('escuro', 'claro'):
                    img = ler_565(dados / f'{nome}_{tema}.565')
                    img.save(out / f'{nome}_{tema}.png')
                    quadros.append(img)
                ov = quadros[1].copy()
                d = ImageDraw.Draw(ov)
                for linha in (dados / f'{nome}.alvos').read_text().splitlines():
                    x, y, w, h, tipo, ind = map(int, linha.split()[:6])
                    d.rectangle([x, y, x + w - 1, y + h - 1], outline=(255, 0, 255))
                    d.text((x + 2, y + 1), f'{tipo}/{ind}', fill=(255, 0, 255))
                ov.save(out / f'{nome}_alvos.png')
                quadros.append(ov)
                linhas.append((nome, quadros))
            folha = Image.new('RGB', (3 * 330 + 10, len(linhas) * 262 + 10), (128, 128, 128))
            d = ImageDraw.Draw(folha)
            for k, (nome, quadros) in enumerate(linhas):
                y = 10 + k * 262
                d.text((10, y), nome, fill=(0, 0, 0))
                for c, q in enumerate(quadros):
                    folha.paste(q, (10 + c * 330, y + 12))
            folha.save(out / '_folha.png')
        return 1 if erros else 0


if __name__ == '__main__':
    sys.exit(main())
