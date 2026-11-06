#!/usr/bin/env python3
"""test_manual_diagramas.py -- holds tools/manual_diagramas.py --check, and the SVG in line of tools/build_manual.py, to their word (2026-11-06).

--check is only worth running if it can fail. Each control below puts ONE fault into a scratch copy of the SVG files, of the chapters or of the source files the
diagrams read their numbers from (never into the tree) and requires exactly the findings that fault is about; the first one requires none, on the files as committed.

  clean            the committed SVG files and chapters: no finding, the command line exits 0; every SVG is well-formed XML, every class it uses has a rule in the
                   manual's CSS, no id lacks the slug, no literal colour, every coordinate on the 4 px grid; the datasheet's pin table (PINAGEM) says what chapter 2's table says
  one character    one character of one SVG changed: exactly 1 finding, naming the file; the file put back: 0 (two files)
  deleted, extra   an SVG deleted: exactly 1; an SVG no row names: exactly 1; a PNG in the folder: 0 (the diagrams not drawn yet keep their .png)
  chapters         a captura= edited, a block removed, a block with another tipo, a block with another arquivo: exactly 1 each; a block of tipo="diagrama" with an
                   .svg no row names: exactly 1; one with a .png no row names: 0
  colour, id       a literal colour injected into an SVG on disk: 2 findings (the file no longer matches what the code draws, and the colour); an id without the slug:
                   2 the same way; the code made to draw them and the files written from it, so that the file matches: exactly 1 each, which is the rule alone
  the source       in a scratch copy of the files the diagrams read, a number changed: exactly the diagrams that draw it change, and no other (the table SOURCE_CASES
                   below says which); an override in the profile follows; two functions on one pin, a pin inside the sensor slots, a premise the figures depend on
                   (the alpha's parallel mode, the charger's polarity) gone: exit 2 naming the file
  pattern gone     every pattern the tool looked for, mangled in a scratch copy, exits 2 with the file and the pattern in the message (one control per pattern)
  determinism      two draws, and two --write into two folders, give the same bytes
  build_manual     the SVG goes into the page in line: inside <div class="moldura moldura-diagrama">, with aria-label, without the XML prologue; two SVG with one id (or
                   one id that a heading has) make its --check complain, two with different ids do not (run with pandoc through the whole build when pandoc is there)
  command line     0 clean, 1 with a finding, 2 when it cannot draw (an empty source folder); --list prints one row per diagram; --write writes the files and --check
                   passes on them; --preview writes <slug>.claro.html and <slug>.escuro.html with the manual's CSS and the figure's frame

  python3 tools/test_manual_diagramas.py     exit 0 when every control holds, 1 when one does not, 2 when it cannot draw
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
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import build_manual as bm         # noqa: E402
import manual_diagramas as m      # noqa: E402

CONTA = [0]
FALHAS = []
PULADOS = []

CONFIG = 'src/simut_config.h'
PERFIS = 'tools/generated/profiles.ini'
BME_DRIVER = 'src/sensors/BME280Driver.h'
BME_SENSOR = 'src/sensors/BME280SensorDriver.h'
AIR_CONFIG = 'src/air/AirConfig.h'


def confere(cond, msg, ok=None):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    elif ok:
        print('ok  ', ok)
    return bool(cond)


def cli(*args, ambiente=None):
    return subprocess.run([sys.executable, '-B', str(HERE / 'manual_diagramas.py'), *args], capture_output=True, text=True, env=ambiente or os.environ.copy())


def em_processo(*args):
    """(exit code, stdout, stderr) of m.main( ) run in this process: the loop over every pattern would spawn a hundred interpreters otherwise."""
    saida, erro = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(saida), contextlib.redirect_stderr(erro):
        rc = m.main(list(args))
    return rc, saida.getvalue(), erro.getvalue()


def trocar(caminho, antigo, novo, vezes=1):
    t = caminho.read_text(encoding='utf-8')
    assert vezes is None or t.count(antigo) == vezes, f'{caminho.name}: {antigo!r} stands {t.count(antigo)} times, not {vezes}'
    assert antigo in t, f'{caminho.name}: {antigo!r} is not there'
    caminho.write_text(t.replace(antigo, novo), encoding='utf-8')


def copiar_fonte(fonte, destino):
    """The files the tool read (Fonte.lidos), in a scratch tree with the same relative paths."""
    for rel in fonte.lidos:
        alvo = destino / rel
        alvo.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fonte.raiz / rel, alvo)


def desenhar_de(raiz):
    """({file: svg}, None) drawn from the source tree at `raiz`, or (None, the exception) when the tool cannot draw."""
    try:
        return m.desenhar_todas(m.Fonte(raiz)), None
    except (m.Falta, ValueError) as e:
        return None, e


def mudados(a, b):
    return sorted(k for k in a if a[k] != b.get(k))


def achados(imagens, capitulos, desenhadas):
    return m.conferir_imagens(imagens, desenhadas) + m.conferir_capitulos(capitulos)


def tabela_do_capitulo(capitulo):
    """{gpio number: physical pin} and the power and ground pins as chapter 2's table of the map and of the power points state them, read from the prose."""
    texto = capitulo.read_text(encoding='utf-8')
    pinos = {}
    for linha in texto.split('\n'):
        c = [x.strip() for x in linha.strip().strip('|').split('|')]
        if len(c) >= 2 and re.fullmatch(r'GP\d+', c[0]) and re.fullmatch(r'\d+', c[1]):
            pinos[c[0]] = int(c[1])
        elif len(c) >= 2 and c[0] == 'GP0 a GP15':
            ordem = []
            for pedaco in c[1].split(','):
                pedaco = pedaco.strip()
                if '–' in pedaco:
                    a, b = pedaco.split('–')
                    ordem += list(range(int(a), int(b) + 1))
                else:
                    ordem.append(int(pedaco))
            pinos.update({f'GP{i}': p for i, p in enumerate(ordem)})
    gnd = re.search(r'\| GND \| ([0-9, ]+) \|', texto)
    return pinos, [int(x) for x in gnd.group(1).split(',')] if gnd else []


def main():
    try:
        fonte = m.Fonte(m.ROOT)
        desenhadas = m.desenhar_todas(fonte)
    except (m.Falta, ValueError) as e:
        print(f'test_manual_diagramas: {e}', file=sys.stderr)
        return 2
    nomes = [f.arquivo for f in m.FIGURAS]

    def precisa(*arquivos):
        """True when every diagram a control is about has its row. A control about a diagram the table no longer draws is a FAIL, not a skip: a skipped control would
        let the removal of a row pass for a clean tree (the controls were skipped, and said so, only while the table grew a few diagrams at a time, 2026-11-06)."""
        faltam = [a for a in arquivos if a not in nomes]
        confere(not faltam, f'a control is about {", ".join(faltam)}, which has no row of FIGURAS')
        return not faltam

    with tempfile.TemporaryDirectory(prefix='manual_diagramas_ctl_') as tmp:
        tmp = pathlib.Path(tmp)
        img, cap = tmp / 'img', tmp / 'cap'
        shutil.copytree(m.IMAGENS, img, ignore=lambda d, ns: [n for n in ns if not n.endswith('.svg')])
        shutil.copytree(m.CAPITULOS, cap)

        def agora():
            return achados(img, cap, desenhadas)

        # ---- clean: the committed state --------------------------------------------------------------------------------------------------------------------
        confere(m.tabela_ok() is None, f'the table itself: {m.tabela_ok()}', 'the table of FIGURAS holds: no file or block twice, no captura= that breaks the attribute')
        confere(all(re.fullmatch(r'\d\d-[a-z0-9]+(-[a-z0-9]+)*\.svg', a) for a in nomes) and len(set(nomes)) == len(nomes), f'the files FIGURAS names: {nomes}',
                'every file is NN-slug.svg, once')
        a = agora()
        confere(not a, f'the committed SVG files and chapters give {len(a)} finding(s): {a[:3]}', f'clean: {len(m.FIGURAS)} diagrams drawn again, 0 findings')
        confere(set(desenhadas) == set(nomes) and all((m.IMAGENS / n).is_file() for n in nomes), 'a diagram has no committed file', 'every row has its committed file')
        r = cli('--check')
        confere(r.returncode == 0 and r.stdout.rstrip().endswith('0 finding(s)'), f'the command line on the committed tree exited {r.returncode}: {r.stdout.strip()[-200:]} {r.stderr.strip()[-200:]}',
                'command line: exit 0 on the committed tree')

        # every SVG, as drawn: well-formed, one root, only the manual's classes, on the grid
        regras = set(re.findall(r'\.(dg[\w-]*)', bm.CSS))
        for f in m.FIGURAS:
            svg = desenhadas[f.arquivo]
            slug = m.slug_de(f.arquivo)
            try:
                raiz = ET.fromstring(svg)
            except ET.ParseError as e:
                confere(False, f'{f.arquivo} is not well-formed XML: {e}')
                continue
            altura = raiz.get('viewBox', '').split()[-1:]
            confere(raiz.tag == '{http://www.w3.org/2000/svg}svg' and raiz.get('role') == 'img' and raiz.get('viewBox') == f'0 0 {m.LARGURA} {altura[0] if altura else "?"}'
                    and svg.endswith('</svg>\n') and '\r' not in svg and not svg.startswith('<?xml'),
                    f'{f.arquivo}: root {raiz.tag} role={raiz.get("role")} viewBox={raiz.get("viewBox")}', f'{f.arquivo}: well-formed, role="img", viewBox 0 0 {m.LARGURA} {altura[0] if altura else "?"}, LF and a final newline')
            classes = {c for e in raiz.iter() for c in e.get('class', '').split()}
            sem_regra = sorted(c for c in classes if c not in regras)
            confere(not sem_regra and all(c.startswith('dg') for c in classes), f'{f.arquivo}: classes with no rule in the manual CSS: {sem_regra}',
                    f'{f.arquivo}: every class it uses has a rule in the manual CSS ({len(classes)} classes)')
            confere(not m.cores_literais(svg) and not m.ids_sem_prefixo(svg, slug), f'{f.arquivo}: colours {m.cores_literais(svg)}, ids {m.ids_sem_prefixo(svg, slug)}',
                    f'{f.arquivo}: no literal colour, every id starts with {slug}-')
            fora = []
            for e in raiz.iter():
                tag = e.tag.split('}')[-1]
                if tag == 'rect':
                    d = 1 if 'dg-comp' in e.get('class', '') else .5      # a box is drawn inside its coordinates: half its line's width in, all round
                    vals = [float(e.get('x')) - d, float(e.get('y')) - d, float(e.get('width')) + 2 * d, float(e.get('height')) + 2 * d]
                elif tag in ('polyline', 'polygon'):
                    vals = [float(v) for par in e.get('points').split() for v in par.split(',')]
                elif tag == 'circle':
                    vals = [float(e.get('cx')), float(e.get('cy'))]
                elif tag == 'text':
                    vals = [float(e.get('x')), float(e.get('y'))]
                else:
                    continue
                fora += [(tag, v) for v in vals if v % 4]
            confere(not fora, f'{f.arquivo}: coordinates off the 4 px grid: {fora[:5]}', f'{f.arquivo}: every coordinate and size on the 4 px grid')

        # the datasheet's table, kept in the tool, against chapter 2's own
        pinos, gnd = tabela_do_capitulo(m.CAPITULOS / 'cap02-hardware.md')
        errados = {g: (p, m.PINAGEM.get(p)) for g, p in pinos.items() if m.PINAGEM.get(p) != g}
        confere(len(pinos) == 26 and not errados, f'PINAGEM against chapter 2: {len(pinos)} GPIOs read, wrong: {errados}',
                'PINAGEM (the Pico W datasheet) names every pin of chapter 2\'s table as the table does: 26 GPIOs, from GP0 at pin 1 to GP28 at pin 34')
        confere(sorted(gnd) == sorted(p for p, nome in m.PINAGEM.items() if nome == 'GND') and m.PINAGEM[36] == '3V3' and m.PINAGEM[40] == 'VBUS',
                f'PINAGEM ground pins {sorted(p for p, nome in m.PINAGEM.items() if nome == "GND")}, chapter 2 says {gnd}',
                'PINAGEM: the same seven GND pins as chapter 2, 3V3 at 36, VBUS at 40')

        # ---- the builder's guards --------------------------------------------------------------------------------------------------------------------------
        s = m.Svg('99-teste', 100)
        s.texto(16, 28, 'um texto')
        for descr, fn in (('a text over another', lambda: s.texto(20, 28, 'outro texto em cima')),
                          ('a text past the canvas', lambda: s.texto(m.LARGURA - 8, 60, 'passa do quadro')),
                          ('a box off the 4 px grid', lambda: s.caixa(10, 10, 40, 40)),
                          ('a word wider than its line', lambda: m.quebrar('uma_palavra_enorme_sem_espacos_nenhuns', 40))):
            try:
                fn()
                confere(False, f'the builder let through {descr}')
            except ValueError:
                confere(True, None, f'the builder refuses {descr}')
        linhas = m.quebrar('um dois tres quatro cinco', 80)
        confere(len(linhas) >= 2 and ' '.join(linhas) == 'um dois tres quatro cinco' and all(m.largura_texto(l) <= 80 for l in linhas),
                f'quebrar gave {linhas}', f'quebrar breaks a line that does not fit at a space and keeps every line inside its width: {linhas}')
        confere(m.n(24.0) == '24' and m.n(24.5) == '24.5' and m.n(4) == '4', f'n( ) wrote {m.n(24.0)} {m.n(24.5)} {m.n(4)}', 'numbers are written without ".0"')

        # ---- one character, deleted, extra ----------------------------------------------------------------------------------------------------------------
        for nome in (nomes[0], nomes[-1]):
            alvo = img / nome
            original = alvo.read_bytes()
            t = original.decode('utf-8')
            i = t.index('class="dg-') + len('class="dg')       # the hyphen of the first class: a hyphen becomes an underscore
            alvo.write_text(t[:i] + '_' + t[i + 1:], encoding='utf-8')
            a = agora()
            confere(len(a) == 1 and a[0].startswith(f'{nome}: differs from what the code draws'), f'one character changed in {nome} gave {a}', f'one character changed in {nome}: exactly 1 finding, naming the file')
            alvo.write_bytes(original)
            confere(not agora(), f'{nome} put back is not clean', f'{nome} put back: 0 findings')
        alvo = img / nomes[0]
        original = alvo.read_bytes()
        alvo.unlink()
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{nomes[0]}: missing'), f'{nomes[0]} deleted gave {a}', 'an SVG deleted: exactly 1 finding (missing)')
        alvo.write_bytes(original)
        (img / '99-x.svg').write_bytes(original)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('99-x.svg: no row of FIGURAS'), f'an SVG no row names gave {a}', 'an SVG no row names: exactly 1 finding')
        (img / '99-x.svg').unlink()
        (img / '12-wifi-niveis.png').write_bytes(b'not a diagram of this tool')
        (img / '99-x.png').write_bytes(b'x')
        confere(not agora(), 'a PNG in the folder is a finding', 'a PNG in the folder (12-wifi-niveis.png, a diagram not drawn yet): 0 findings')
        (img / '12-wifi-niveis.png').unlink()
        (img / '99-x.png').unlink()

        # ---- the chapters ----------------------------------------------------------------------------------------------------------------------------------
        primeira = m.FIGURAS[0]
        capitulo = cap / primeira.capitulo
        original_cap = capitulo.read_text(encoding='utf-8')
        linhas = original_cap.split('\n')
        i = next(k for k, l in enumerate(linhas) if l.startswith(f'::: {{.figura #{primeira.bloco} '))
        capitulo.write_text('\n'.join(linhas[:i] + linhas[i + 3:]), encoding='utf-8')     # a block is three lines: the opening, the caption, the closing
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{primeira.bloco}: the figure block is gone'), f'a block removed gave {a}', 'a figure block removed: exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        trocar(capitulo, f'arquivo="{primeira.arquivo}"', f'arquivo="{m.slug_de(primeira.arquivo)}.png"')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{primeira.bloco}: the block says'), f'a block with another arquivo gave {a}', 'a block with another arquivo (the .png it had): exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        trocar(capitulo, f'#{primeira.bloco} tipo="diagrama"', f'#{primeira.bloco} tipo="tft"')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f"{primeira.bloco}: the block says tipo='tft'"), f'a block with another tipo gave {a}', 'a block with another tipo: exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        b = m.ler_blocos(cap)[primeira.bloco]
        trocar(capitulo, f'captura="{b.captura}"', f'captura="{b.captura}."')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{primeira.bloco}: the captura= of the block'), f'a captura= edited gave {a}', 'a captura= edited: exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        capitulo.write_text(original_cap + '\n::: {.figura #fig-02-zzz tipo="diagrama" arquivo="02-zzz.svg" captura="x"}\nUm diagrama a mais.\n:::\n', encoding='utf-8')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('fig-02-zzz: a block of tipo="diagrama" with the SVG 02-zzz.svg'), f'a diagram block with an SVG no row names gave {a}',
                'a tipo="diagrama" block with an .svg no row names: exactly 1 finding')
        capitulo.write_text(original_cap + '\n::: {.figura #fig-02-zzz tipo="diagrama" arquivo="02-zzz.png" captura="x"}\nUm diagrama a mais.\n:::\n', encoding='utf-8')
        confere(not agora(), 'a diagram block with a .png no row names is a finding', 'a tipo="diagrama" block with a .png no row names (not drawn yet): 0 findings')
        capitulo.write_text(original_cap, encoding='utf-8')
        confere(not agora(), 'the chapter put back is not clean')

        # ---- colour and id: on disk, and the rule alone ----------------------------------------------------------------------------------------------------
        alvo = img / nomes[0]
        original = alvo.read_bytes()
        t = original.decode('utf-8')
        for descr, antigo, novo in (('a hex colour', '<rect ', '<rect fill="#ff0000" '), ('a colour name', '<rect ', '<rect stroke="red" '),
                                    ('an rgb( ) in a style', '<rect ', '<rect style="fill:rgb(255,0,0)" ')):
            alvo.write_text(t.replace(antigo, novo, 1), encoding='utf-8')
            a = agora()
            confere(len(a) == 2 and a[0].startswith(f'{nomes[0]}: differs') and 'literal colour' in a[1] and a[1].startswith(f'{nomes[0]}:'),
                    f'{descr} injected on disk gave {a}', f'{descr} injected on disk: 2 findings, that the file differs from the code and the literal colour')
        alvo.write_text(t.replace('<rect ', '<rect id="sem-prefixo" ', 1), encoding='utf-8')
        a = agora()
        confere(len(a) == 2 and a[0].startswith(f'{nomes[0]}: differs') and 'does not start with' in a[1], f'an id without the slug injected on disk gave {a}',
                'an id without the slug injected on disk: 2 findings, that the file differs and the id')
        alvo.write_bytes(original)
        confere(not agora(), 'the file put back is not clean')
        # the code itself drawing them: the files are written from it, so the file matches the drawing and only the rule is left
        desenho = getattr(m, m.FIGURAS[0].desenho)
        for descr, antigo, novo, pedaco in (('a literal colour', '<rect ', '<rect fill="#ff0000" ', 'literal colour'),
                                            ('an id without the slug', '<rect ', '<rect id="sem-prefixo" ', 'does not start with')):
            setattr(m, m.FIGURAS[0].desenho, lambda num, slug, antigo=antigo, novo=novo: desenho(num, slug).replace(antigo, novo, 1))
            try:
                mutadas = m.desenhar_todas(m.Fonte(m.ROOT))
            finally:
                setattr(m, m.FIGURAS[0].desenho, desenho)
            escritos = tmp / 'escritos'
            shutil.rmtree(escritos, ignore_errors=True)
            m.gravar(escritos, mutadas)
            a = m.conferir_imagens(escritos, mutadas)
            confere(len(a) == 1 and pedaco in a[0] and a[0].startswith(f'{nomes[0]}:'), f'{descr} drawn by the code gave {a}', f'{descr} drawn by the code and written: exactly 1 finding, the rule alone')

        # ---- determinism -----------------------------------------------------------------------------------------------------------------------------------
        outra = m.desenhar_todas(m.Fonte(m.ROOT))
        confere(outra == desenhadas, 'two draws differ: ' + ', '.join(mudados(desenhadas, outra)), 'two draws in a row give the same text')
        for pasta in ('a', 'b'):
            rc, so, se = em_processo('--write', '--images', str(tmp / pasta))
            confere(rc == 0, f'--write into {pasta} exited {rc}: {se.strip()[-200:]}')
        iguais = all((tmp / 'a' / n).read_bytes() == (tmp / 'b' / n).read_bytes() == (m.IMAGENS / n).read_bytes() for n in nomes) and sorted(p.name for p in (tmp / 'a').iterdir()) == sorted(nomes)
        confere(iguais, 'two --write runs, or a run and the committed files, differ', 'two --write runs write the same bytes as the committed files, and only the table\'s files')

        # ---- the source: a number changes exactly the diagrams that draw it ------------------------------------------------------------------------------
        def casos_da_fonte():
            """(description, [(file, old, new, how many times)], the diagrams that change, or the text the exit-2 message has to carry)."""
            mapas = ('02-pinos-release.svg', '02-pinos-alpha.svg', '02-pinos-air.svg')
            return [
                ('TFT_CS 28 -> 21, the release\'s chip select', [(CONFIG, '#define TFT_CS 28', '#define TFT_CS 21', 1)], {'02-pinos-release.svg'}),
                ('HD44780_D7 21 -> 28, the alpha\'s last data line', [(CONFIG, '#define HD44780_D7 21', '#define HD44780_D7 28', 1)], {'02-pinos-alpha.svg'}),
                ('AIR_CHARGER_PIN 17 -> 21', [(CONFIG, '#define AIR_CHARGER_PIN 17', '#define AIR_CHARGER_PIN 21', 1)], {'02-pinos-air.svg', '02-air-energia.svg'}),
                ('AIR_SENSOR_POWER_PIN 16 -> 21', [(CONFIG, '#define AIR_SENSOR_POWER_PIN 16', '#define AIR_SENSOR_POWER_PIN 21', 1)], {'02-pinos-air.svg', '02-air-energia.svg'}),
                ('MAX_SENSORS 16 -> 11 (the LAN build\'s slots): the three maps and the two figures whose note names the range, not the BME280\'s',
                 [(CONFIG, '#define MAX_SENSORS 16 ', '#define MAX_SENSORS 11 ', 1)], set(mapas) | {'02-ds18b20.svg', '02-dht22.svg'}),
                ('SPI0\'s MISO named GP21 in the TFT section', [(CONFIG, 'MOSI=GP19, MISO=GP16, SCK=GP18', 'MOSI=GP19, MISO=GP21, SCK=GP18', 1)], {'02-pinos-release.svg'}),
                ('BUZZER_PIN 22 -> 21 with HD44780_D7 21 -> 28 (a buzzer on a pin both images leave free)',
                 [(CONFIG, '#define BUZZER_PIN 22', '#define BUZZER_PIN 21', 1), (CONFIG, '#define HD44780_D7 21', '#define HD44780_D7 28', 1)],
                 {'02-pinos-release.svg', '02-pinos-alpha.svg'}),
                ('the radio\'s fourth GPIO 29 -> 30 in AirConfig.h', [(AIR_CONFIG, 'case 25: case 29: return false;', 'case 25: case 30: return false;', 1)], set(mapas)),
                ('the BME280 probe pair (4, 5) -> (6, 7)', [(BME_SENSOR, 'probe(4, 5, BME280_ADDR_PRIMARY)', 'probe(6, 7, BME280_ADDR_PRIMARY)', 1)], {'02-bmx280.svg'}),
                ('the BME280 addresses 0x76/0x77 -> 0x75/0x78 in the driver, code and comments', [(BME_DRIVER, '0x76', '0x75', None), (BME_DRIVER, '0x77', '0x78', None)], {'02-bmx280.svg'}),
            ]

        def aplica(raiz, edicoes):
            for rel, antigo, novo, vezes in edicoes:
                trocar(raiz / rel, antigo, novo, vezes)

        for k, (descr, edicoes, esperado) in enumerate(casos_da_fonte()):
            if not precisa(*sorted(esperado)):
                continue
            raiz = tmp / f'fonte_{k}'
            copiar_fonte(fonte, raiz)
            aplica(raiz, edicoes)
            novas, falta = desenhar_de(raiz)
            confere(falta is None and mudados(desenhadas, novas) == sorted(esperado), f'{descr}: changed {mudados(desenhadas, novas) if novas else falta}, expected {sorted(esperado)}',
                    f'{descr}: exactly {", ".join(sorted(esperado))} change')
        if precisa('02-pinos-release.svg'):
            raiz = tmp / 'fonte_cli'
            copiar_fonte(fonte, raiz)
            aplica(raiz, [(CONFIG, '#define TFT_CS 28', '#define TFT_CS 21', 1)])
            r = cli('--check', '--fonte', str(raiz), '--images', str(img), '--capitulos', str(cap))
            confere(r.returncode == 1 and r.stdout.count('differs from what the code draws') == 1 and r.stdout.startswith('02-pinos-release.svg:'),
                    f'--check on a source whose pin moved exited {r.returncode}: {r.stdout.strip()[-300:]}', 'command line: the source says the TFT\'s CS moved, the committed SVG is stale: exit 1, exactly the release\'s map')
            # the profile's -D flag wins over simut_config.h's default, as the compiler does
            raiz = tmp / 'fonte_perfil'
            copiar_fonte(fonte, raiz)
            perfis = (raiz / PERFIS)
            t = perfis.read_text(encoding='utf-8')
            i = t.index('[env:pico_w_release]')
            j = t.index('    -DNDEBUG', i)
            perfis.write_text(t[:j] + '    -DTFT_CS=21\n' + t[j:], encoding='utf-8')
            novas, falta = desenhar_de(raiz)
            confere(falta is None and mudados(desenhadas, novas) == ['02-pinos-release.svg'], f'-DTFT_CS=21 in the release profile: {mudados(desenhadas, novas) if novas else falta}',
                    'a -DTFT_CS=21 in [env:pico_w_release] moves the release\'s CS in the map, and only there')

        # what has to refuse to draw (exit 2): two functions on one pin, a pin among the slots, a premise the figures depend on
        recusas = [
            ('TOUCH_CS 17 -> 28, the TFT\'s CS pin', [(CONFIG, '#define TOUCH_CS 17', '#define TOUCH_CS 28', 1)], 'GP28', '02-pinos-release.svg'),
            ('TFT_DC 27 -> 5, a sensor slot', [(CONFIG, '#define TFT_DC 27', '#define TFT_DC 5', 1)], 'GP5', '02-pinos-release.svg'),
            ('the alpha loses -DHD44780_MODE_PARALLEL=1 (it would be the I2C backpack on GP26/GP27)', [(PERFIS, '    -DHD44780_MODE_PARALLEL=1\n    -DSIMUT_CLI_FULL=0\n    -DSIMUT_MDNS=0\n    -DSIMUT_BLUETOOTH=1',
                                                                                                         '    -DSIMUT_CLI_FULL=0\n    -DSIMUT_MDNS=0\n    -DSIMUT_BLUETOOTH=1', 1)], 'HD44780_MODE_PARALLEL', '02-pinos-alpha.svg'),
            ('AIR_CHARGER_ACTIVE_HIGH 1 -> 0 (the figure draws a divider off 5 V)', [(CONFIG, '#define AIR_CHARGER_ACTIVE_HIGH 1', '#define AIR_CHARGER_ACTIVE_HIGH 0', 1)], 'AIR_CHARGER_ACTIVE_HIGH', '02-air-energia.svg'),
            ('the Air no longer builds with -DSIMUT_AIR=1', [(PERFIS, '    -DSIMUT_AIR=1\n', '', 1)], 'SIMUT_AIR', '02-pinos-air.svg'),
        ]
        for k, (descr, edicoes, agulha, figura) in enumerate(recusas):
            if not precisa(figura):
                continue
            raiz = tmp / f'recusa_{k}'
            copiar_fonte(fonte, raiz)
            aplica(raiz, edicoes)
            novas, falta = desenhar_de(raiz)
            confere(falta is not None and agulha in str(falta), f'{descr}: {falta if falta else "drew all the same"}', f'{descr}: refuses to draw, and says {agulha}')
            rc, so, se = em_processo('--write', '--images', str(tmp / 'recusado'), '--fonte', str(raiz))
            confere(rc == 2 and agulha in se and not (tmp / 'recusado').exists(), f'--write on that source exited {rc}: {se.strip()[-200:]}', f'  and --write on it exits 2 and writes nothing')

        # ---- a pattern the tool looks for, gone: exit 2, naming the file and the pattern ---------------------------------------------------------------------
        for k, (rel, padrao) in enumerate(fonte.padroes):
            raiz = tmp / f'padrao_{k}'
            copiar_fonte(fonte, raiz)
            t = (raiz / rel).read_text(encoding='utf-8')
            estragado = re.sub(padrao, lambda mm: '§' + mm.group(0)[1:], t, flags=m.FLAGS_PADRAO)
            if estragado == t:
                confere(False, f'{rel}: the pattern {padrao} matches nothing in the file the tool read it from')
                continue
            (raiz / rel).write_text(estragado, encoding='utf-8')
            rc, so, se = em_processo('--check', '--fonte', str(raiz), '--images', str(img), '--capitulos', str(cap))
            confere(rc == 2 and rel in se and padrao in se, f'{rel}, pattern {padrao} mangled: exit {rc}, message {se.strip()[-240:]}', f'{rel}: the pattern {padrao[:60]} mangled: exit 2 naming the file and the pattern')
        confere(len(fonte.padroes) >= len(m.FIGURAS), f'the tool recorded only {len(fonte.padroes)} patterns for {len(m.FIGURAS)} diagrams', f'{len(fonte.padroes)} patterns recorded, each held to the control above')
        sem_arquivo = tmp / 'fonte_sem_arquivo'
        copiar_fonte(fonte, sem_arquivo)
        (sem_arquivo / CONFIG).unlink()
        rc, so, se = em_processo('--check', '--fonte', str(sem_arquivo), '--images', str(img), '--capitulos', str(cap))
        confere(rc == 2 and CONFIG in se, f'{CONFIG} missing: exit {rc}, {se.strip()[-200:]}', f'{CONFIG} deleted: exit 2 naming it')

        # ---- build_manual.py: the SVG in the page -----------------------------------------------------------------------------------------------------------
        figs = tmp / 'figs'
        figs.mkdir()
        svg_a = (m.IMAGENS / nomes[0]).read_text(encoding='utf-8')
        (figs / 'a.svg').write_text('<?xml version="1.0" encoding="UTF-8"?>\n' + svg_a, encoding='utf-8')
        (figs / 'b.svg').write_text(svg_a.replace('<svg ', '<svg id="outro-b" ', 1), encoding='utf-8')
        (figs / 'c.svg').write_text(svg_a.replace('<svg ', '<svg id="outro-b" ', 1), encoding='utf-8')
        svg, ids = bm.inline_svg(str(figs / 'a.svg'), 'Uma legenda com "aspas" & <tags>')
        confere(svg.startswith('<svg ') and '<?xml' not in svg and 'aria-label="Uma legenda com &quot;aspas&quot; &amp; &lt;tags&gt;"' in svg and 'role="img"' in svg and svg.rstrip().endswith('</svg>'),
                f'inline_svg( ) gave {svg[:200]}', 'inline_svg( ): the XML prologue is gone, the root is <svg>, aria-label carries the caption escaped, role="img" is there')
        confere(ids == re.findall(r'\sid="([^"]+)"', svg), f'inline_svg( ) ids {ids}', 'inline_svg( ) names the ids the SVG holds')
        (figs / 'd.svg').write_text(svg_a.replace('<svg ', '<svg id="outro-d" ', 1), encoding='utf-8')

        def pagina(*nomes_svg):
            """(the page's HTML, the figures) of render_figures( ) over pandoc-shaped blocks for the files named, read from the scratch folder."""
            antes = bm.IMG
            bm.IMG = str(figs)
            try:
                frag = '<p>Texto.</p>\n' + '\n'.join(
                    f'<div id="fig-99-{x}" class="figura" data-tipo="diagrama" data-arquivo="{x}.svg" data-captura="como">\n<p>Legenda <code>{x}</code>.</p>\n</div>' for x in nomes_svg)
                figuras = []
                return bm.render_figures(frag, '99', 'Teste', figuras), figuras
            finally:
                bm.IMG = antes

        html_fig, figuras = pagina('a', 'b', 'c')
        um = re.search(r'<figure id="fig-99-a" class="fig fig-diagrama"><div class="moldura moldura-diagrama"><svg [^>]*aria-label="Legenda a\."[^>]*>.*?</svg></div><figcaption>', html_fig, re.S)
        confere(um is not None and '<img' not in html_fig and '<?xml' not in html_fig and [f['svg_ids'] for f in figuras] == [[], ['outro-b'], ['outro-b']],
                f'render_figures( ) gave {html_fig[:300]}', 'render_figures( ): the SVG sits in <div class="moldura moldura-diagrama"> inside the <figure>, with aria-label, no <img>, no prologue')
        erros = bm.svg_id_errors(figuras, html_fig)
        confere(len(erros) == 1 and 'outro-b' in erros[0], f'two SVG with one id gave {erros}', 'svg_id_errors( ): two SVG with the same id: exactly 1 error, naming the id')
        html_ok, figuras_ok = pagina('a', 'b', 'd')
        confere(bm.svg_id_errors(figuras_ok, html_ok) == [], f'two SVG with different ids gave {bm.svg_id_errors(figuras_ok, html_ok)}', 'svg_id_errors( ): two SVG with different ids: no error')
        erros = bm.svg_id_errors(figuras_ok, html_ok + '<h2 id="outro-d">Um título</h2>')
        confere(len(erros) == 1 and 'outro-d' in erros[0], f'an id the page already has gave {erros}', 'svg_id_errors( ): an SVG id that a heading of the page also has: exactly 1 error')
        html_png = bm.render_figures('<div id="fig-99-x" class="figura" data-tipo="tft" data-arquivo="nao-existe.png" data-captura="como">\n<p>x</p>\n</div>', '99', 'Teste', [])
        confere('pendente' in html_png and '<svg' not in html_png, 'a missing file did not stay a pending box', 'a figure whose file is missing is still the pending box (the .png path is untouched)')

        if shutil.which('pandoc'):
            raiz = tmp / 'arvore'
            for rel in ('tools/build_manual.py', 'tools/gen_logcodes.py', 'tools/logcodes.tsv', 'src/SystemDefs_Limits.h'):
                (raiz / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(m.ROOT / rel, raiz / rel)
            shutil.copytree(m.CAPITULOS, raiz / 'docs' / '_manual')
            shutil.copytree(m.IMAGENS, raiz / 'docs' / 'images' / 'manual', ignore=lambda d, ns: [n for n in ns if n.endswith('.png')])

            def build(*args):
                return subprocess.run([sys.executable, '-B', str(raiz / 'tools' / 'build_manual.py'), *args], capture_output=True, text=True, cwd=raiz)
            r = build('--check')
            confere(r.returncode == 0 and r.stdout.strip() == '', f'build_manual.py --check on the committed chapters and SVG exited {r.returncode}: {r.stdout.strip()[-300:]}',
                    'build_manual.py --check on a copy with the SVG: exit 0, nothing to say')
            r = build()
            pagina = (raiz / 'docs' / 'MANUAL.pt-BR.html').read_text(encoding='utf-8') if r.returncode == 0 else ''
            quadro = re.search(rf'<figure id="{re.escape(m.FIGURAS[0].bloco)}" class="fig fig-diagrama"><div class="moldura moldura-diagrama"><svg [^>]*aria-label="[^"]+"', pagina)
            confere(quadro is not None and '<?xml' not in pagina and f'data:image/svg+xml' not in pagina, f'the page built from the copy: {r.stderr.strip()[-200:]} {pagina[:100]}',
                    'build_manual.py on a copy: the page has the <svg> in the <figure> with aria-label, no prologue, no data: URI')
            # a second figure with the same SVG, in the copy's chapter 2, and the same id put into both
            pasta_img = raiz / 'docs' / 'images' / 'manual'
            shutil.copyfile(pasta_img / nomes[0], pasta_img / '99-copia.svg')
            with open(raiz / 'docs' / '_manual' / 'cap02-hardware.md', 'a', encoding='utf-8') as fh:
                fh.write('\n::: {.figura #fig-99-copia tipo="diagrama" arquivo="99-copia.svg" captura="uma cópia"}\nUma cópia do primeiro diagrama.\n:::\n')
            for nome in (nomes[0], '99-copia.svg'):
                trocar(pasta_img / nome, '<svg ', '<svg id="repetido" ', 1)
            r = build('--check')
            confere(r.returncode == 1 and r.stdout.count('repetido') == 1, f'two SVG with one id: build_manual.py --check exited {r.returncode}: {r.stdout.strip()[-300:]}',
                    'build_manual.py --check: two SVG with one id: exit 1, exactly one line about it')
        else:
            PULADOS.append('build_manual.py through pandoc')
            print('skip build_manual.py run through pandoc: pandoc is not installed (the controls above read render_figures( ) and svg_id_errors( ) directly)')

        # ---- the command line ------------------------------------------------------------------------------------------------------------------------------
        r = cli('--list')
        linhas = r.stdout.splitlines()
        confere(r.returncode == 0 and len(linhas) == len(m.FIGURAS) and all(l.count('\t') == 3 for l in linhas) and [l.split('\t')[0] for l in linhas] == nomes,
                f'--list: exit {r.returncode}, {len(linhas)} rows', f'--list: {len(linhas)} rows of file, chapter, function and captura=')
        r = cli('--check', '--fonte', str(tmp / 'vazia'))
        confere(r.returncode == 2 and CONFIG in r.stderr, f'an empty source folder: exit {r.returncode}: {r.stderr.strip()[-200:]}', 'command line: exit 2 when the source cannot be read')
        alvo = img / nomes[0]
        original = alvo.read_bytes()
        alvo.unlink()
        r = cli('--check', '--images', str(img), '--capitulos', str(cap))
        confere(r.returncode == 1 and r.stdout.count('missing') == 1, f'a deleted SVG: exit {r.returncode}: {r.stdout.strip()[-200:]}', 'command line: exit 1 with one finding')
        alvo.write_bytes(original)
        escritos = tmp / 'escritos2'
        r = cli('--write', '--images', str(escritos))
        r2 = cli('--check', '--images', str(escritos))
        confere(r.returncode == 0 and r2.returncode == 0, f'--write exited {r.returncode}, --check on what it wrote exited {r2.returncode}: {r2.stdout.strip()[-200:]}', 'command line: --write, then --check on the files it wrote: exit 0')
        previa = tmp / 'previa'
        r = cli('--preview', str(previa))
        esperados = sorted(f'{m.slug_de(n)}.{t}.html' for n in nomes for t in ('claro', 'escuro'))
        confere(r.returncode == 0 and sorted(p.name for p in previa.iterdir()) == esperados, f'--preview exited {r.returncode} and wrote {sorted(p.name for p in previa.iterdir()) if previa.is_dir() else None}',
                f'--preview: {len(esperados)} pages, {{slug}}.claro.html and {{slug}}.escuro.html for each diagram')
        boa = []
        for f in m.FIGURAS:
            for tema in ('claro', 'escuro'):
                p = (previa / f'{m.slug_de(f.arquivo)}.{tema}.html').read_text(encoding='utf-8')
                for cond, o_que in ((f'<html lang="pt-BR" data-theme="{tema}">' in p, 'the fixed theme'), ('.dg-caixa{' in p or '.dg-caixa,' in p, 'the manual\'s CSS'),
                                    ('<div class="moldura moldura-diagrama"><svg ' in p, 'the frame'), ('<figcaption><b>Figura 1.</b> ' in p, 'the caption'),
                                    (p.count('<svg ') == 1 and '__FONT__' not in p, 'one SVG')):
                    if not cond:
                        boa.append(f'{f.arquivo} {tema}: lacks {o_que}')
        confere(not boa, f'a preview page lacks: {boa[:3]}', '--preview pages: data-theme fixed, the manual\'s CSS, the same frame, the caption, one SVG')
    print(f'test_manual_diagramas: {CONTA[0]} controls, {len(FALHAS)} failed' + (f', {len(PULADOS)} skipped ({"; ".join(PULADOS)})' if PULADOS else ''))
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
