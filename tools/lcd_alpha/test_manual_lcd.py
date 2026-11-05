#!/usr/bin/env python3
"""test_manual_lcd.py -- holds manual_lcd.py --check to its word: it finds what it says it finds, and nothing else (2026-11-04).

--check is only worth running if it can fail. Each control below puts ONE fault into a scratch copy of the figures, of the chapters or of AlphaFrame.h
(never into the tree) and requires exactly the findings that fault is about; the first one requires none, on the figures and chapters as committed.

  clean            the committed figures and chapters: no finding (and the command line exits 0); the 17 glasses drawn at 639 x 137, the diagram at its
                   geometria( ), 639 x 271
  one pixel        one pixel of one figure changed, and one of the diagram: exactly 1 finding each, the file put back: 0
  deleted          a figure deleted, and the diagram: exactly 1 each (missing)
  unreadable       a figure that is not a PNG: exactly 1
  size             a figure saved at half size, one saved as a palette PNG, the diagram saved at a glass's size: exactly 1 each
  orphan           a 12-x.png, a 04-lcd-x.png and a 06-lcd-x.png no row names: exactly 1 each; a 12-*.png that a block of another kind declares (a scratch
                   block of tipo="diagrama") is not an orphan: 0
  block gone       a figure block removed from a chapter: exactly 1; a block of tipo="lcd" no row names: exactly 1; a block with another arquivo: exactly 1;
                   the diagram's block with tipo="lcd": exactly 1
  caption          a captura= edited: exactly 1
  version          a "SIMUT 2.7.1" in chapter 12: exactly 1 (it is the figures' version that moves a release, and the chapter has to follow)
  the code         " Conectado!" without its bang in a copy of AlphaFrame.h: exactly the two figures that draw it differ (12-conectado.png, 12-obtendo-ip.png);
                   the degree sign a column left: the four temperatures; the three-bar glyph with one dot more in a copy of BigFont_HD44780.h: exactly the
                   diagram (the glasses show only the five bars and the X)
  history          WIFI_V271 equals BF_WIFI0 to BF_WIFI5 of the tag v2.7.1, and not those of v2.7.2 (the control that the comparison can fail); skipped, and said
                   so, when the clone has no such tag (CI's shallow checkout)
  exit codes       the command line: 0 clean, 1 with a finding, 2 when g++ cannot be found (PATH without it); --list prints 18 rows and needs neither g++ nor Pillow

  python3 tools/lcd_alpha/test_manual_lcd.py     exit 0 when every control holds, 1 when one does not, 2 when it cannot draw
"""
import contextlib
import io
import os
import pathlib
import re
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
PULADOS = []


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


def wifi_da_tag(tag):
    """(BF_WIFI0 to BF_WIFI5 of src/display/BigFont_HD44780.h at `tag`, None) from git, or (None, why) when the clone does not have it."""
    r = subprocess.run(['git', '-C', str(ROOT), 'show', f'{tag}:src/display/BigFont_HD44780.h'], capture_output=True, text=True)
    if r.returncode:
        return None, (r.stderr.strip() or f'git exited {r.returncode}')[:120]
    bm = {mm.group(1): tuple(int(v, 0) for v in mm.group(2).replace(' ', '').split(','))
          for mm in re.finditer(r'const uint8_t (BF_WIFI[0-5])\[8\]\s*=\s*\{([^}]*)\}', r.stdout)}
    if len(bm) != 6:
        return None, f'{tag} has {len(bm)} of the six BF_WIFI bitmaps'
    return tuple(bm[f'BF_WIFI{k}'] for k in range(6)), None


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
        confere(not sem and not a, f'the committed figures and chapters give {len(a)} finding(s): {a[:3]}', f'clean: {len(m.FIGURAS)} figures drawn again, 0 findings')
        g = m.geometria()
        tamanhos = {f.arquivo: desenhadas[f.arquivo].size for f in m.FIGURAS}
        confere(tamanhos.pop('12-wifi-niveis.png') == (g['largura'], g['altura']) == (639, 271) and set(tamanhos.values()) == {(639, 137)} and len(tamanhos) == 17,
                f'the sizes drawn: {tamanhos}, the diagram\'s geometria( ) {g["largura"]} x {g["altura"]}',
                'the 17 glasses drawn at 639 x 137, the diagram at 639 x 271 as geometria( ) and its captura= say')
        r = cli('--check')
        confere(r.returncode == 0 and r.stdout.rstrip().endswith('0 finding(s)'), f'the command line on the committed tree exited {r.returncode}: {r.stdout.strip()[-200:]} {r.stderr.strip()[-200:]}', 'command line: exit 0 on the committed tree')
        # The firmware at the figures' version: no note. The tree's own SIMUT_VERSION moves at every release while the figures are pinned on purpose, so
        # this control sets the version the command reads and does not read the tree's (it was red at the first release bump, v2.14.0, 2026-11-05).
        antes = m.versao_do_firmware
        m.versao_do_firmware = lambda: m.VERSAO
        saida = io.StringIO()
        try:
            with contextlib.redirect_stdout(saida):
                rc = m.main(['--check'])
        finally:
            m.versao_do_firmware = antes
        confere(rc == 0 and 'note:' not in saida.getvalue(), f'with the firmware at {m.VERSAO} the command line printed a note: {saida.getvalue().strip()[-200:]}',
                f'the firmware is SIMUT {m.VERSAO}, the figures show it: no note')
        # A firmware ahead of the figures: a note in the log and still exit 0 (the figures are pinned on purpose; review of 2026-11-04).
        antes = m.versao_do_firmware
        m.versao_do_firmware = lambda: '9.9.9'
        saida = io.StringIO()
        try:
            with contextlib.redirect_stdout(saida):
                rc = m.main(['--check'])
        finally:
            m.versao_do_firmware = antes
        confere(rc == 0 and f'the figures show SIMUT {m.VERSAO} and the firmware is 9.9.9' in saida.getvalue(),
                f'a firmware at 9.9.9 gave exit {rc}: {saida.getvalue().strip()[-200:]}', 'a firmware ahead of the figures: the note, and exit 0')

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
        diagrama = img / '12-wifi-niveis.png'
        original_d = diagrama.read_bytes()
        im = Image.open(diagrama).convert('RGB')
        x, y = 200, 100
        im.putpixel((x, y), (255, 0, 0) if im.getpixel((x, y)) != (255, 0, 0) else (0, 255, 0))
        im.save(diagrama)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-wifi-niveis.png: 1 of 173169 pixels differ') and f'x={x} y={y}' in a[0], f'one pixel of the diagram changed gave {a}',
                'one pixel of the diagram changed: exactly 1 finding, naming the pixel')
        diagrama.unlink()
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-wifi-niveis.png: missing'), f'the diagram deleted gave {a}', 'the diagram deleted: exactly 1 finding')
        Image.open(io.BytesIO(original_d)).convert('RGB').crop((0, 0, 639, 137)).save(diagrama)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('12-wifi-niveis.png: 639x137 RGB, not 639x271 RGB'), f'the diagram at a glass\'s size gave {a}',
                'the diagram saved at a glass\'s size: exactly 1 finding')
        diagrama.write_bytes(original_d)
        confere(not agora(), 'the diagram put back is not clean', 'the diagram put back: 0 findings')

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
        cap12 = cap / 'cap12-lcd-alpha.md'
        original12 = cap12.read_text(encoding='utf-8')
        cap12.write_text(original12 + '\n::: {.figura #fig-12-yyy tipo="diagrama" arquivo="12-yyy.png" captura="x"}\nOutro diagrama.\n:::\n', encoding='utf-8')
        (img / '12-yyy.png').write_bytes(original)
        a = agora()
        confere(not a, f'a 12-yyy.png declared by a block of tipo="diagrama" gave {a}', 'a 12-*.png declared by a block of another kind is not an orphan: 0 findings')
        (img / '12-yyy.png').unlink()
        cap12.write_text(original12, encoding='utf-8')
        (img / '11a-x.png').write_bytes(original)
        confere(not agora(), 'a file of the panel chapter (11a-x.png) is not this tool\'s and must not be a finding', 'a 11a-*.png is not this tool\'s: 0 findings')
        (img / '11a-x.png').unlink()

        # the chapters
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
        trocar(cap12, 'tipo="diagrama" arquivo="12-wifi-niveis.png"', 'tipo="lcd" arquivo="12-wifi-niveis.png"')
        a = agora()
        confere(len(a) == 1 and a[0].startswith("fig-12-wifi-niveis: the block says tipo='lcd'"), f'the diagram\'s block with tipo="lcd" gave {a}',
                'the diagram\'s block with tipo="lcd": exactly 1 finding')
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
        mut3 = host_lcd.mutante_em(tmp / 'mut3', 'BF_WIFI3[8] = { 0b00000, 0b00000, 0b00000, 0b00100,', 'BF_WIFI3[8] = { 0b00000, 0b00000, 0b00000, 0b00110,', 1,
                                   host_lcd.BIGFONT)
        des4, sem4 = m.desenhar_figuras(mut3)
        a = m.conferir_imagens(img, des4, sem4, cap)
        confere([x.split(':')[0] for x in a] == ['12-wifi-niveis.png'] and 'differ from the sample icones' in a[0],
                f'the three-bar glyph with one dot more gave {a}', 'the three-bar glyph with one dot more in BigFont_HD44780.h: exactly the diagram differs')

        # the history: v2.7.1's drawings, kept in manual_lcd.py
        antigos, porque = wifi_da_tag('v2.7.1')
        if antigos is None:
            PULADOS.append('v2.7.1')
            print(f'skip WIFI_V271 against the tag v2.7.1: the clone does not have it ({porque}); a shallow checkout, as CI\'s, has no tags')
        else:
            confere(antigos == m.WIFI_V271, f'WIFI_V271 is not BF_WIFI0..5 of v2.7.1: {antigos}', 'WIFI_V271 equals BF_WIFI0 to BF_WIFI5 of the tag v2.7.1')
            seguintes, porque = wifi_da_tag('v2.7.2')
            confere(seguintes is not None and seguintes != m.WIFI_V271, f'the control against v2.7.2 did not differ ({porque})',
                    'control: v2.7.2\'s bitmaps (the staircase) differ from WIFI_V271, so the comparison can fail')

        # the command line without a compiler, and the list
        sem_gxx = tmp / 'sem_gxx'
        sem_gxx.mkdir()
        ambiente = dict(os.environ, PATH=str(sem_gxx))
        r = cli('--check', ambiente=ambiente)
        confere(r.returncode == 2 and 'g++ not found' in r.stderr, f'without g++ the command line exited {r.returncode}: {r.stderr.strip()[-200:]}', 'command line: exit 2 and "g++ not found" when there is no compiler')
        r = cli('--list')
        linhas = r.stdout.splitlines()
        confere(r.returncode == 0 and len(linhas) == 18 and all(l.count('\t') == 2 for l in linhas), f'--list: exit {r.returncode}, {len(linhas)} rows', '--list: 18 rows of file, sample and captura=')
    print(f'test_manual_lcd: {CONTA[0]} controls, {len(FALHAS)} failed' + (f', {len(PULADOS)} skipped ({", ".join(PULADOS)})' if PULADOS else ''))
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
