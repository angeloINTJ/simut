#!/usr/bin/env python3
"""test_manual_lcd.py -- holds manual_lcd.py --check to its word: it finds what it says it finds, and nothing else (2026-11-04).

--check is only worth running if it can fail. Each control below puts ONE fault into a scratch copy of the figures, of the chapters or of AlphaFrame.h
(never into the tree) and requires exactly the findings that fault is about; the first one requires none, on the figures and chapters as committed.

  clean            the committed figures and chapters: no finding (and the command line exits 0)
  one pixel        one pixel of one figure changed: exactly 1 finding, the file put back: 0
  deleted          a figure deleted: exactly 1 (missing)
  unreadable       a figure that is not a PNG: exactly 1
  size             a figure saved at half size, and one saved as a palette PNG: exactly 1 each
  orphan           a 12-x.png, a 04-lcd-x.png and a 06-lcd-x.png no row names: exactly 1 each; the diagram 12-wifi-niveis.png (declared by a block of another
                   kind) is not an orphan: 0
  block gone       a figure block removed from a chapter: exactly 1; a block of tipo="lcd" no row names: exactly 1; a block with another arquivo: exactly 1
  caption          a captura= edited: exactly 1
  version          a "SIMUT 2.7.1" in chapter 12: exactly 1 (it is the figures' version that moves a release, and the chapter has to follow)
  the code         " Conectado!" without its bang in a copy of AlphaFrame.h: exactly the two figures that draw it differ (12-conectado.png, 12-obtendo-ip.png)
  exit codes       the command line: 0 clean, 1 with a finding, 2 when g++ cannot be found (PATH without it); --list prints 17 rows and needs neither g++ nor Pillow

  python3 tools/lcd_alpha/test_manual_lcd.py     exit 0 when every control holds, 1 when one does not, 2 when it cannot draw
"""
import io
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import host_lcd        # noqa: E402
import manual_lcd as m  # noqa: E402
from PIL import Image  # noqa: E402

CONTA = [0]
FALHAS = []


def confere(cond, msg, ok=None):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    elif ok:
        print('ok  ', ok)
    return bool(cond)


def achados(imagens, capitulos, desenhadas, sem):
    return m.conferir_imagens(imagens, desenhadas, sem, capitulos) + m.conferir_capitulos(capitulos)


def cli(*args, ambiente=None):
    return subprocess.run([sys.executable, '-B', str(HERE / 'manual_lcd.py'), *args], capture_output=True, text=True, env=ambiente or os.environ.copy())


def trocar(caminho, antigo, novo, vezes=1):
    t = caminho.read_text(encoding='utf-8')
    assert t.count(antigo) == vezes, f'{caminho.name}: {antigo!r} stands {t.count(antigo)} times, not {vezes}'
    caminho.write_text(t.replace(antigo, novo), encoding='utf-8')


def main():
    try:
        desenhadas, sem = m.desenhar_figuras()
    except host_lcd.Falta as e:
        print(f'test_manual_lcd: {e}', file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix='manual_lcd_ctl_') as tmp:
        tmp = pathlib.Path(tmp)
        img, cap = tmp / 'img', tmp / 'cap'
        shutil.copytree(m.IMAGENS, img, ignore=lambda d, nomes: [n for n in nomes if not n.endswith('.png')])
        shutil.copytree(m.CAPITULOS, cap)

        def agora():
            return achados(img, cap, desenhadas, sem)

        # clean: the committed state, and the command line
        a = agora()
        confere(not sem and not a, f'the committed figures and chapters give {len(a)} finding(s): {a[:3]}', f'clean: 17 figures drawn again, 0 findings')
        r = cli('--check')
        confere(r.returncode == 0 and r.stdout.rstrip().endswith('0 finding(s)'), f'the command line on the committed tree exited {r.returncode}: {r.stdout.strip()[-200:]} {r.stderr.strip()[-200:]}', 'command line: exit 0 on the committed tree')

        # one pixel of one figure
        alvo = img / '12-temperatura.png'
        original = alvo.read_bytes()
        im = Image.open(alvo).convert('RGB')
        x, y = 100, 50
        im.putpixel((x, y), (255, 0, 0) if im.getpixel((x, y)) != (255, 0, 0) else (0, 255, 0))
        im.save(alvo)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-temperatura.png: 1 of ') and f'x={x} y={y}' in a[0], f'one pixel changed gave {a}', 'one pixel changed: exactly 1 finding, naming the pixel')
        r = cli('--check', '--images', str(img), '--capitulos', str(cap))
        confere(r.returncode == 1 and r.stdout.count('differ from the sample') == 1, f'command line on a changed pixel exited {r.returncode}', 'command line: exit 1 with one finding')
        alvo.write_bytes(original)
        confere(not agora(), 'the figure put back is not clean', 'the figure put back: 0 findings')

        # deleted, unreadable, size, mode
        alvo = img / '12-offline.png'
        original = alvo.read_bytes()
        alvo.unlink()
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-offline.png: missing'), f'a deleted figure gave {a}', 'a figure deleted: exactly 1 finding')
        alvo.write_bytes(b'not a png at all')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-offline.png: not a readable PNG'), f'a figure that is not a PNG gave {a}', 'a figure that is not a PNG: exactly 1 finding')
        Image.open(io.BytesIO(original)).convert('RGB').resize((320, 68)).save(alvo)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-offline.png: 320x68 RGB'), f'a figure at half size gave {a}', 'a figure at half size: exactly 1 finding')
        Image.open(io.BytesIO(original)).convert('P').save(alvo)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-offline.png: 639x137 P'), f'a palette PNG gave {a}', 'a palette PNG: exactly 1 finding')
        alvo.write_bytes(original)
        confere(not agora(), 'the figure put back is not clean (after the size controls)')

        # orphans
        for nome in ('12-x.png', '04-lcd-x.png', '06-lcd-x.png'):
            (img / nome).write_bytes(original)
            a = agora()
            confere(len(a) == 1 and a[0].startswith(f'{nome}: no row of FIGURAS'), f'an orphan {nome} gave {a}', f'an orphan {nome}: exactly 1 finding')
            (img / nome).unlink()
        (img / '12-wifi-niveis.png').write_bytes(original)
        a = agora()
        confere(not a, f'the diagram 12-wifi-niveis.png (a block of tipo="diagrama") gave {a}', 'the diagram 12-wifi-niveis.png is not an orphan: 0 findings')
        (img / '12-wifi-niveis.png').unlink()
        (img / '11a-x.png').write_bytes(original)
        confere(not agora(), 'a file of the panel chapter (11a-x.png) is not this tool\'s and must not be a finding', 'a 11a-*.png is not this tool\'s: 0 findings')
        (img / '11a-x.png').unlink()

        # the chapters
        cap12 = cap / 'cap12-lcd-alpha.md'
        original12 = cap12.read_text(encoding='utf-8')
        linhas = original12.split('\n')
        i = next(k for k, l in enumerate(linhas) if l.startswith('::: {.figura #fig-12-umidade '))
        sem_bloco = linhas[:i] + linhas[i + 3:]    # the block is three lines: the opening, the caption, the closing
        cap12.write_text('\n'.join(sem_bloco), encoding='utf-8')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('fig-12-umidade: the figure block is gone'), f'a block removed gave {a}', 'a figure block removed: exactly 1 finding')
        cap12.write_text(original12, encoding='utf-8')
        trocar(cap12, 'arquivo="12-umidade.png"', 'arquivo="12-umidade-2.png"')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('fig-12-umidade: the block says'), f'a block with another arquivo gave {a}', 'a block with another arquivo: exactly 1 finding')
        cap12.write_text(original12, encoding='utf-8')
        cap12.write_text(original12 + '\n::: {.figura #fig-12-zzz tipo="lcd" arquivo="12-zzz.png" captura="x"}\nUm quadro novo.\n:::\n', encoding='utf-8')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('fig-12-zzz: a block of tipo="lcd"'), f'a lcd block no row names gave {a}', 'a lcd block no row names: exactly 1 finding')
        cap12.write_text(original12, encoding='utf-8')
        trocar(cap12, 'Sem WiFi e Offline"', 'Sem WiFi e Offline."')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('fig-12-offline: the captura= of the block'), f'a captura= edited gave {a}', 'a captura= edited: exactly 1 finding')
        cap12.write_text(original12, encoding='utf-8')
        trocar(cap12, f'| Abertura | `SIMUT {m.VERSAO}` |', '| Abertura | `SIMUT 2.7.1` |')
        a = agora()
        confere(len(a) == 1 and 'says SIMUT 2.7.1' in a[0], f'a stale version in chapter 12 gave {a}', 'a stale "SIMUT 2.7.1" in chapter 12: exactly 1 finding')
        cap12.write_text(original12, encoding='utf-8')
        confere(not agora(), 'the chapter put back is not clean')

        # the code: " Conectado!" without its bang
        mut = host_lcd.mutante_em(tmp / 'mut', 'lcd.print(" Conectado!     ");', 'lcd.print(" Conectado      ");', 2)
        des2, sem2 = m.desenhar_figuras(mut)
        a = m.conferir_imagens(img, des2, sem2, cap)
        nomes = sorted(x.split(':')[0] for x in a)
        confere(nomes == ['12-conectado.png', '12-obtendo-ip.png'] and all('differ from the sample' in x for x in a),
                f'" Conectado!" without its bang gave {a}', '" Conectado!" without its bang in AlphaFrame.h: exactly the figures 12-conectado.png and 12-obtendo-ip.png differ')
        mut2 = host_lcd.mutante_em(tmp / 'mut2', "lcd.setCursor(14, 0); lcd.write('o');", "lcd.setCursor(13, 0); lcd.write('o');", 1)
        des3, sem3 = m.desenhar_figuras(mut2)
        nomes = sorted(x.split(':')[0] for x in m.conferir_imagens(img, des3, sem3, cap))
        confere(nomes == ['12-negativa.png', '12-pendentes-k.png', '12-pendentes.png', '12-temperatura.png'],
                f'the degree sign a column left gave {nomes}', 'the degree sign a column to the left: exactly the four temperatures differ')

        # the command line without a compiler, and the list
        sem_gxx = tmp / 'sem_gxx'
        sem_gxx.mkdir()
        ambiente = dict(os.environ, PATH=str(sem_gxx))
        r = cli('--check', ambiente=ambiente)
        confere(r.returncode == 2 and 'g++ not found' in r.stderr, f'without g++ the command line exited {r.returncode}: {r.stderr.strip()[-200:]}', 'command line: exit 2 and "g++ not found" when there is no compiler')
        r = cli('--list')
        linhas = r.stdout.splitlines()
        confere(r.returncode == 0 and len(linhas) == 17 and all(l.count('\t') == 2 for l in linhas), f'--list: exit {r.returncode}, {len(linhas)} rows', '--list: 17 rows of file, sample and captura=')
    print(f'test_manual_lcd: {CONTA[0]} controls, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
