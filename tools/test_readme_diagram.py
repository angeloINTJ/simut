#!/usr/bin/env python3
"""test_readme_diagram.py -- holds tools/readme_diagram.py --check to its word (2026-11-07, issue #58).

--check is only worth running if it can fail. Each control below puts ONE fault into a scratch copy of the six SVG files, of the three READMEs or of the source files the
figure reads its facts from (never into the tree) and requires exactly the findings that fault is about; the first one requires none, on the files as committed.

  clean            the committed tree: --check exits 0 with 0 findings (in this process and as a command); exactly six docs/images/architecture*.svg, each well-formed XML, 832 px
                   wide, with a <title> and a <style>, every colour in it a value of its own theme's token block (claro for the light files, escuro for the dark ones), none in an
                   attribute, no var( ) left and a rule for every class an element uses; the three READMEs show their two files through <picture>, with an alt text and no box
                   drawing. That checker is itself held by nine faults put into a copy of one file (the width, a colour that is no token, the other theme's ink, a fill=, a style=,
                   a class with no rule, a var( ), an open tag, no <title>): exactly one fault each
  one character    one character of each of the six files changed (the first, the last and four between): exactly 1 finding, naming the file and the line and column of the
                   character; the file put back: 0; a file that is not UTF-8: exactly 1
  deleted          each of the six files deleted: exactly 1 finding, "missing"
  READMEs          in a scratch copy of the three: the <picture> removed, the dark <source> (and, for README.md, the light one, the <img> or both sources) pointing at a file of
                   another language or theme, an empty or blank alt, no alt, the <img> narrower than the drawing, a box-drawing character in the section (at its start, in its
                   alt, at its end; the two edges of the
                   Unicode block): exactly 1 finding, naming the README (with the line, for the character); the same character in the section before, in the one after and
                   at the end of the file: 0 (the READMEs carry a project tree drawn in box characters); a section renamed, a README gone: exactly 1
  the source       in a scratch copy of the files the figure read, after one draw: the sensor slots 16 -> 12, the panel's ring 16 -> 32 and the touch's window 5 -> 8 s
                   change all six drawings and the figure says the new number; a light token colour changes the three light files and not the dark ones (and the other way round); a header FooManager with
                   no role, a role whose header is gone, a header without its class, the Core 0 header gone, and one manager's @brief reworded (all of them): exit 2 naming the
                   header, and --write writes nothing
  pattern gone     every pattern the tool looked for, mangled in a scratch copy, exits 2 with the file and the pattern in the message (one control per pattern; the first
                   character of the match is mangled, and the last when another pattern reads the same text and is told first); every file it read, deleted, exits 2 naming it
  determinism      two draws give the same bytes; two --write in two processes with different hash seeds write the same files, and the committed ones
  command line     0 clean, 1 with a finding, 2 when it cannot draw; --write then --check on what it wrote; --preview writes a page for each language with the light file and the
                   dark one, each on its theme's background
  line breaker     quebrar_runs( ) keeps a run in the code's font whole (loop( ) is not cut at its inner space), moves the text glued to code with no space (the ( before
                   TouchPriority and the ). after it) to the next line with it, keeps a no-break space's two words together, and refuses a word wider than the line; the real notes
                   at 62 widths lose no character, overflow no line and start none with a ); the six files, as drawn, have loop( ) whole and TouchPriority between its parenthesis
                   and its full stop
  the tree         the files the controls copy (the six drawings, the three READMEs, the tokens) are the same bytes at the end as at the start

  python3 tools/test_readme_diagram.py     exit 0 when every control holds, 1 when one does not, 2 when it cannot draw
"""
import contextlib
import hashlib
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import manual_diagramas as md     # noqa: E402
import readme_diagram as rd       # noqa: E402

CONTA = [0]
FALHAS = []

CONFIG = 'src/simut_config.h'
DISPLAY_H = 'src/DisplayManager.h'
APP_H = 'src/AppManager.h'
FOLHA_ANGULO = 'docs/assets/angulo.css'
IMAGENS = ROOT / 'docs' / 'images'
LARGURA = 832                    # the drawing's width: GitHub's README column is 838 px wide (2026-11-07), and the READMEs' <img> says 832 too
SVG = '{http://www.w3.org/2000/svg}'
CAIXA = re.compile('[─-╿]')      # the Unicode block of box drawing: the sketch the figure replaced was made of it

# What the READMEs, the folder and the tool agree on, spelled out here and not taken from the tool: a control that holds the tool to the names it gave itself holds nothing.
ARQUIVOS = {'en': ('architecture.svg', 'architecture-dark.svg'), 'pt-BR': ('architecture.pt-BR.svg', 'architecture-dark.pt-BR.svg'),
            'es-ES': ('architecture.es-ES.svg', 'architecture-dark.es-ES.svg')}      # (light, dark)
LEITURAS = {'en': ('README.md', 'Architecture'), 'pt-BR': ('README.pt-BR.md', 'Arquitetura'), 'es-ES': ('README.es-ES.md', 'Arquitectura')}
SEIS = [nome for par in ARQUIVOS.values() for nome in par]
CLAROS = [nome for nome in SEIS if '-dark' not in nome]
ESCUROS = [nome for nome in SEIS if '-dark' in nome]


def confere(cond, msg, ok=None):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    elif ok:
        print('ok  ', ok)
    return bool(cond)


def resumo(antes, texto):
    """The line that closes a loop of controls, printed only when none of them failed (the loop's controls print nothing when they hold)."""
    if len(FALHAS) == antes:
        print('ok  ', texto)


def cli(*args, ambiente=None):
    assert '--write' not in args or '--images' in args, 'a --write without --images would write into the tree'
    return subprocess.run([sys.executable, '-B', str(HERE / 'readme_diagram.py'), *args], capture_output=True, text=True, env=ambiente or os.environ.copy())


def em_processo(*args):
    """(exit code, stdout, stderr) of rd.main( ) run in this process: the loops below would spawn hundreds of interpreters otherwise."""
    assert '--write' not in args or '--images' in args, 'a --write without --images would write into the tree'
    saida, erro = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(saida), contextlib.redirect_stderr(erro):
        try:
            rc = rd.main(list(args))
        except SystemExit as e:          # argparse refusing an argument
            rc = e.code
    return rc, saida.getvalue(), erro.getvalue()


RESUMO = re.compile(r'readme_diagram: (\d+) files drawn again from the sources and compared as text, the READMEs read, (\d+) finding\(s\)')


def achados(img, leit, fonte=None):
    """The finding lines of `--check` over the scratch images and READMEs (and, when given, the scratch sources). The line that closes the output counts the findings and the exit
    code follows them: when either disagrees with the lines printed, or the command wrote to stderr, a line saying so is added, so a control that wants exactly k findings sees k + 1."""
    rc, so, se = em_processo('--check', '--images', str(img), '--readmes', str(leit), *(['--fonte', str(fonte)] if fonte else []))
    linhas = so.splitlines()
    fim = linhas.pop() if linhas else ''
    fecho = RESUMO.fullmatch(fim)
    if fecho is None or int(fecho.group(1)) != len(SEIS) or int(fecho.group(2)) != len(linhas) or rc != (1 if linhas else 0) or se:
        linhas.append(f'!! the command closed with {fim!r} and exited {rc} for {len(linhas)} finding line(s), stderr {se.strip()[:120]!r}')
    return linhas


def trocar(caminho, antigo, novo, vezes=1):
    t = caminho.read_text(encoding='utf-8')
    assert t.count(antigo) == vezes, f'{caminho.name}: {antigo!r} stands {t.count(antigo)} times, not {vezes}'
    caminho.write_text(t.replace(antigo, novo), encoding='utf-8')


def copiar_fonte(fonte, destino):
    """The files the tool read (Fonte.lidos) and the folders it listed (Fonte.listados), in a scratch tree with the same relative paths."""
    for rel in fonte.lidos:
        alvo = destino / rel
        alvo.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fonte.raiz / rel, alvo)
    for rel, glob in fonte.listados:
        (destino / rel).mkdir(parents=True, exist_ok=True)
        for p in (fonte.raiz / rel).glob(glob):
            shutil.copyfile(p, destino / rel / p.name)


def desenhar_de(raiz):
    """({file: svg}, None) drawn from the source tree at `raiz`, or (None, the exception) when the tool cannot draw."""
    try:
        return rd.desenhar_todas(md.Fonte(raiz)), None
    except (md.Falta, ValueError) as e:
        return None, e


def mudados(a, b):
    return sorted(k for k in a if a[k] != b.get(k))


def limites_da_secao(texto, titulo):
    """(start, end) of the body of the README's `## titulo`, found with plain string searches and not with rd.secao( ): a control must not trust the reader it holds."""
    cabeca = f'\n## {titulo}\n'
    ini = texto.index(cabeca) + len(cabeca)
    fim = texto.find('\n## ', ini)
    return ini, len(texto) if fim < 0 else fim + 1


def texto_visivel(svg):
    """What a reader reads in a drawing: the text of each <text> in the order they are drawn, a space between lines (so a number a line break separates from its noun is still read)."""
    return ' '.join(''.join(e.itertext()) for e in ET.fromstring(svg).iter(SVG + 'text'))


def falhas_do_svg(texto, permitidas):
    """The faults of one drawing as an <img> shows it: [] when it is well-formed XML, LARGURA wide, with a <title> and a <style>, no colour but the values of its theme's token block
    (`permitidas`), none outside the <style>, no var( ) left in it (an <img> sees no property of the page) and a rule in the <style> for every class an element of the body uses."""
    try:
        raiz = ET.fromstring(texto)
    except ET.ParseError as e:
        return [f'not well-formed XML ({e})']
    falhas = []
    if raiz.tag != SVG + 'svg' or raiz.get('width') != str(LARGURA) or not (raiz.get('viewBox') or '').startswith(f'0 0 {LARGURA} '):
        falhas.append(f'the root is {raiz.tag} with width={raiz.get("width")} and viewBox={raiz.get("viewBox")}, not an <svg> {LARGURA} wide')
    titulo, estilo = raiz.find(SVG + 'title'), raiz.find(SVG + 'style')
    if titulo is None or not (titulo.text or '').strip():
        falhas.append('no <title>')
    regras = (estilo.text or '') if estilo is not None else ''
    if not regras.strip():
        falhas.append('no <style>')
    atributos = [v for e in raiz.iter() for v in e.attrib.values()]
    for cor in re.findall(r'(?<!url\()#[0-9a-fA-F]{3,8}\b', '\n'.join([regras] + atributos)):
        if cor not in permitidas:
            falhas.append(f'the colour {cor} is no value of the theme\'s tokens')
    if re.search(r'\b(?:rgb|rgba|hsl|hsla)\(', regras + '\n'.join(atributos)):
        falhas.append('a colour written as a function')
    if 'var(' in texto:
        falhas.append('a var( ) left in the file')
    for e in raiz.iter():
        if e.tag == SVG + 'style':
            continue
        etiqueta = e.tag.rsplit('}', 1)[-1]
        for nome in ('fill', 'stroke', 'style', 'color', 'stop-color', 'flood-color', 'lighting-color'):
            valor = e.get(nome)
            if valor is None:
                continue
            neutro = valor in ('none', 'currentColor', 'inherit') or valor.startswith('url(#')      # a value that is no colour
            if nome == 'style' or not neutro:
                falhas.append(f'<{etiqueta} {nome}="{valor}">: a colour outside the <style>')
    com_regra = set(re.findall(r'\.(dg-[\w-]+)', regras))
    for c in sorted({c for e in raiz.iter() if e is not raiz for c in e.get('class', '').split()} - com_regra):
        falhas.append(f'the class {c} has no rule in the <style>')
    return falhas


def nomeia(mensagem, rel, padrao):
    """True when the message of a draw that stopped names the file and says that this very pattern was not found: a pattern that is a part of another one's text must not pass
    on the other's message (`the pattern X was not found` of Fonte.achar( ), `no longer matches the pattern X, and ...` of Fonte.exigir( ))."""
    return rel in mensagem and re.search('the pattern ' + re.escape(padrao) + '(?: was not found|, and )', mensagem) is not None


def mudar_token(css, tema, nome, novo):
    """The tokens file with the colour `--nome` of the theme's block set to `novo`."""
    ini = css.index(f'[data-theme="{tema}"] {{')
    fim = css.index('}', ini)
    bloco, n = re.subn(rf'(--{nome}:\s*)#[0-9a-f]{{6}}', rf'\g<1>{novo}', css[ini:fim], count=1)
    assert n == 1, f'--{nome} is not in the {tema} block of {FOLHA_ANGULO}'
    return css[:ini] + bloco + css[fim:]


def impressao():
    """{file: sha256} of what the controls copy: the six drawings, the three READMEs and the tokens."""
    arquivos = [IMAGENS / n for n in SEIS] + [ROOT / nome for nome, _ in LEITURAS.values()] + [ROOT / FOLHA_ANGULO]
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in arquivos}


def main():
    try:
        fonte = md.Fonte(ROOT)
        desenhadas = rd.desenhar_todas(fonte)
        cores, _ = rd.tokens(md.Fonte(ROOT))
    except (md.Falta, ValueError) as e:
        print(f'test_readme_diagram: {e}', file=sys.stderr)
        return 2
    antes_da_arvore = impressao()

    with tempfile.TemporaryDirectory(prefix='readme_diagram_ctl_') as tmp:
        tmp = pathlib.Path(tmp)
        img, leit = tmp / 'img', tmp / 'readmes'
        img.mkdir()
        leit.mkdir()
        for nome in SEIS:
            shutil.copyfile(IMAGENS / nome, img / nome)
        for nome, _ in LEITURAS.values():
            shutil.copyfile(ROOT / nome, leit / nome)
        originais = {idioma: (leit / nome).read_bytes().decode('utf-8') for idioma, (nome, _) in LEITURAS.items()}

        def agora():
            return achados(img, leit)

        # ---- clean: the committed state --------------------------------------------------------------------------------------------------------------------
        em_disco = sorted(p.name for p in IMAGENS.glob('architecture*.svg'))
        confere(sorted(desenhadas) == sorted(SEIS) == em_disco, f'drawn {sorted(desenhadas)}, committed {em_disco}, expected {sorted(SEIS)}',
                'exactly six files, drawn and committed: architecture{,-dark}{,.pt-BR,.es-ES}.svg')
        confere(rd.README == {i: nome for i, (nome, _) in LEITURAS.items()} and rd.SECAO == {i: t for i, (_, t) in LEITURAS.items()}
                and {i: (rd.arquivo(i, 'claro'), rd.arquivo(i, 'escuro')) for i in rd.IDIOMAS} == ARQUIVOS,
                f'the tool names the READMEs {rd.README}, their sections {rd.SECAO} and the files {[rd.arquivo(i, t) for i in rd.IDIOMAS for t in rd.TEMAS]} otherwise than the READMEs do',
                'the tool names the three READMEs, their sections and the six files as the READMEs and the folder do')
        rc, so, se = em_processo('--check')
        confere(rc == 0 and so.rstrip().endswith('0 finding(s)') and not se, f'--check on the committed tree exited {rc}: {so.strip()[-200:]} {se.strip()[-200:]}', 'clean: --check on the committed tree exits 0, 0 findings')
        a = agora()
        confere(not a, f'the scratch copy of the committed files gives {len(a)} finding(s): {a[:3]}', 'clean: the scratch copies of the six files and the three READMEs give 0 findings, the base of every control below')
        r = cli('--check')
        confere(r.returncode == 0 and r.stdout.rstrip().endswith('0 finding(s)'), f'the command line on the committed tree exited {r.returncode}: {r.stdout.strip()[-200:]} {r.stderr.strip()[-200:]}',
                'command line: exit 0 on the committed tree')

        for nome in SEIS:
            tema = 'escuro' if '-dark' in nome else 'claro'
            texto = desenhadas[nome]
            falhas = falhas_do_svg(texto, set(cores[tema].values()))
            confere(not falhas and len(set(cores[tema].values()) & set(re.findall(r'#[0-9a-f]{6}', texto))) >= 3, f'{nome} ({tema}): {falhas[:3]}',
                    f'{nome}: well-formed, {LARGURA} px wide, titled, its colours are the {tema} tokens\' (and only there), every class has its rule')
        luz, escura = desenhadas['architecture.svg'], desenhadas['architecture-dark.svg']
        confere(set(re.findall(r'#[0-9a-f]{6}', luz)) != set(re.findall(r'#[0-9a-f]{6}', escura)) and not set(re.findall(r'#[0-9a-f]{6}', escura)) & {cores['claro']['tinta'], cores['claro']['superficie']},
                'the dark file carries the colours of the light one', 'the dark file has the escuro colours and none of the claro ink or surface')
        # the checker of those faults, held by a fault put into a copy of the light file
        tinta, claras = cores['claro']['tinta'], set(cores['claro'].values())
        for descr, mutada, agulha in (('a width of 800', luz.replace(f'width="{LARGURA}"', 'width="800"', 1), 'width=800'),
                                      ('a colour that is no token', luz.replace(tinta, '#123456', 1), '#123456'),
                                      ('the dark theme\'s ink in a light file', luz.replace(tinta, cores['escuro']['tinta'], 1), cores['escuro']['tinta']),
                                      ('a fill= with a colour name', luz.replace('<rect ', '<rect fill="red" ', 1), 'fill="red"'),
                                      ('a style= on an element', luz.replace('<rect ', '<rect style="opacity:.5" ', 1), 'style="opacity:.5"'),
                                      ('a class with no rule', luz.replace('<text class="dg-t', '<text class="dg-zzz', 1), 'dg-zzz'),
                                      ('a var( ) left in the style', luz.replace(f'fill:{tinta}', 'fill:var(--tinta)', 1), 'var('),
                                      ('an open tag', luz.replace('</svg>', '', 1), 'not well-formed'),
                                      ('no <title>', re.sub(r'<title>.*?</title>\n', '', luz, count=1), 'no <title>')):
            falhas = falhas_do_svg(mutada, claras)
            confere(mutada != luz and len(falhas) == 1 and agulha in falhas[0], f'{descr}: the checker gave {falhas}', f'the SVG checker finds {descr}: exactly 1 fault')
        for idioma, (nome, titulo) in LEITURAS.items():
            corpo = originais[idioma]
            ini, fim = limites_da_secao(corpo, titulo)
            corpo = corpo[ini:fim]
            claro, escuro = ARQUIVOS[idioma]
            confere(re.findall(r'docs/images/(architecture[\w.-]*\.svg)', corpo) == [escuro, claro, claro] and re.search(r'<picture>\s*<source media="\(prefers-color-scheme: dark\)"', corpo)
                    and re.search(r'<img [^>]*alt="[^"]+"', corpo) and not CAIXA.search(corpo), f'{nome}: the section "{titulo}" does not show {escuro}, {claro}, {claro} with an alt text and no box drawing',
                    f'{nome}: the section "## {titulo}" shows {escuro} (dark), {claro} (light) and {claro} through <picture>, with an alt text and no box-drawing character')

        # ---- one character, a file that is not text, deleted -----------------------------------------------------------------------------------------------
        for k, nome in enumerate(SEIS):
            alvo = img / nome
            original = alvo.read_bytes()
            t = original.decode('utf-8')
            i = (len(t) - 1) * k // (len(SEIS) - 1)               # the first character, the last (the final newline) and four between
            antes = t[:i].split('\n')
            alvo.write_bytes((t[:i] + '§' + t[i + 1:]).encode('utf-8'))
            a = agora()
            confere(len(a) == 1 and a[0].startswith(f'docs/images/{nome}: differs from what the code draws, first at line {len(antes)} column {len(antes[-1]) + 1} ') and '--write' in a[0],
                    f'one character changed in {nome} (offset {i}) gave {a}', f'one character changed in {nome} (line {len(antes)}, column {len(antes[-1]) + 1}): exactly 1 finding, naming the file and the place')
            alvo.write_bytes(original)
            confere(not agora(), f'{nome} put back is not clean', f'{nome} put back: 0 findings')
        alvo = img / SEIS[2]
        original = alvo.read_bytes()
        alvo.write_bytes(b'\xff\xfe<svg')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'docs/images/{SEIS[2]}: not UTF-8 text'), f'a file that is not UTF-8 gave {a}', 'a file that is not UTF-8 text: exactly 1 finding')
        alvo.write_bytes(original)
        for nome in SEIS:
            alvo = img / nome
            original = alvo.read_bytes()
            alvo.unlink()
            a = agora()
            confere(len(a) == 1 and a[0].startswith(f'docs/images/{nome}: missing'), f'{nome} deleted gave {a}', f'{nome} deleted: exactly 1 finding (missing)')
            alvo.write_bytes(original)
        confere(not agora(), 'the files put back are not clean', 'the six files put back: 0 findings')

        # ---- the READMEs -----------------------------------------------------------------------------------------------------------------------------------
        def com_readme(idioma, texto):
            """The findings of --check with README `idioma` replaced by `texto` in the scratch folder; the README is put back."""
            alvo = leit / LEITURAS[idioma][0]
            try:
                alvo.write_bytes(texto.encode('utf-8'))
                return agora()
            finally:
                alvo.write_bytes(originais[idioma].encode('utf-8'))

        def na_secao(idioma, fn):
            t = originais[idioma]
            ini, fim = limites_da_secao(t, LEITURAS[idioma][1])
            novo = t[:ini] + fn(t[ini:fim]) + t[fim:]
            assert novo != t, f'{LEITURAS[idioma][0]}: the edit changed nothing'
            return novo

        def a_linha_de(texto, marca, desde=0):
            return len(texto[:texto.index(marca, desde)].split('\n'))

        for idioma, (nome, titulo) in LEITURAS.items():
            claro, escuro = ARQUIVOS[idioma]
            outro_claro, outro_escuro = ARQUIVOS[list(ARQUIVOS)[(list(ARQUIVOS).index(idioma) + 1) % 3]]
            fonte_escura = f'<source media="(prefers-color-scheme: dark)" srcset="docs/images/{escuro}">'
            fonte_clara = f'<source media="(prefers-color-scheme: light)" srcset="docs/images/{claro}">'
            casos = [('the <picture> removed', na_secao(idioma, lambda c: re.sub(r'<picture>.*?</picture>\n', '', c, count=1, flags=re.S)), f'{nome}: the section "## {titulo}" has no <picture>'),
                     (f'the dark <source> on {outro_escuro}', na_secao(idioma, lambda c: c.replace(fonte_escura, fonte_escura.replace(escuro, outro_escuro), 1)), f'{nome}: the <picture> shows '),
                     ('an empty alt', na_secao(idioma, lambda c: re.sub(r'(<img [^>]*?alt=")[^"]*(")', r'\1\2', c, count=1)), f'{nome}: the <img> of the architecture has no alt text'),
                     ('a blank alt', na_secao(idioma, lambda c: re.sub(r'(<img [^>]*?alt=")[^"]*(")', r'\1   \2', c, count=1)), f'{nome}: the <img> of the architecture has no alt text'),
                     ('the alt attribute gone', na_secao(idioma, lambda c: re.sub(r' alt="[^"]*"', '', c, count=1)), f'{nome}: the section "## {titulo}" has no <picture>'),
                     ('the <img> 800 px wide', na_secao(idioma, lambda c: c.replace(f'width="{LARGURA}">', 'width="800">', 1)), f'{nome}: the <img> of the architecture is not width="{LARGURA}"'),
                     ('the <img> without a width', na_secao(idioma, lambda c: c.replace(f' width="{LARGURA}">', '>', 1)), f'{nome}: the <img> of the architecture is not width="{LARGURA}"'),
                     ('the section renamed', originais[idioma].replace(f'\n## {titulo}\n', f'\n## {titulo} notes\n', 1), f'{nome}: no section "## {titulo}"')]
            if idioma == 'en':
                casos += [(f'the light <source> on {outro_claro}', na_secao(idioma, lambda c: c.replace(fonte_clara, fonte_clara.replace(claro, outro_claro), 1)), f'{nome}: the <picture> shows '),
                          (f'the <img> on {escuro}', na_secao(idioma, lambda c: c.replace(f'<img src="docs/images/{claro}"', f'<img src="docs/images/{escuro}"', 1)), f'{nome}: the <picture> shows '),
                          ('each <source> on the other\'s file', na_secao(idioma, lambda c: c.replace(fonte_escura, '@@').replace(fonte_clara, fonte_clara.replace(claro, escuro)).replace('@@', fonte_escura.replace(escuro, claro))),
                           f'{nome}: the <picture> shows '),
                          ('the two <source> in the other order', na_secao(idioma, lambda c: c.replace(fonte_escura, '@@').replace(fonte_clara, fonte_escura).replace('@@', fonte_clara)),
                           f'{nome}: the section "## {titulo}" has no <picture>')]
            for descr, texto, comeco in casos:
                a = com_readme(idioma, texto)
                confere(len(a) == 1 and a[0].startswith(comeco), f'{nome}, {descr}: {a}', f'{nome}, {descr}: exactly 1 finding, naming the README')
            # a box-drawing character in the section: exactly 1 finding, with the line it stands on
            for descr, caixa, onde in (('at the start of the section', '┌', lambda c: ('┌─┐\n' + c)),
                                       ('in the alt text', '┌', lambda c: c.replace('alt="', 'alt="┌ ', 1)),
                                       ('at the end of the section', '└', lambda c: (c + '└─┘\n')),
                                       ('after the picture (U+2500, the first of the Unicode block)', '─', lambda c: c.replace('</picture>\n', '</picture>\n─\n', 1)),
                                       ('at the end of the section (U+257F, the last of the Unicode block)', '╿', lambda c: (c + '╿\n'))):
                texto = na_secao(idioma, onde)
                ini, _ = limites_da_secao(originais[idioma], titulo)
                linha = a_linha_de(texto, caixa, ini)
                a = com_readme(idioma, texto)
                confere(len(a) == 1 and re.fullmatch(rf'{re.escape(nome)}:{linha}: a box-drawing character \(.*\) in the section "## {titulo}".*', a[0]) is not None,
                        f'{nome}, a box-drawing character {descr} (line {linha}): {a}', f'{nome}, a box-drawing character {descr}: exactly 1 finding, naming the README and line {linha}')
            # the same character in another section: no finding (the READMEs have a project tree in box characters, and --check passes on them)
            t = originais[idioma]
            cab = t.index(f'\n## {titulo}\n') + 1
            ini, fim = limites_da_secao(t, titulo)
            depois = t.index('\n', fim) + 1
            for descr, texto in (('at the end of the section before', t[:cab] + '┌─┐\n\n' + t[cab:]), ('at the start of the section after', t[:depois] + '┌─┐\n' + t[depois:]),
                                 ('at the end of the file', t + '\n┌─┐\n')):
                a = com_readme(idioma, texto)
                confere(not a, f'{nome}, a box-drawing character {descr}: {a}', f'{nome}, a box-drawing character {descr}: 0 findings')
        alvo = leit / LEITURAS['en'][0]
        alvo.unlink()
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{LEITURAS["en"][0]}: cannot be read'), f'README.md gone gave {a}', 'README.md gone: exactly 1 finding')
        alvo.write_bytes(originais['en'].encode('utf-8'))
        confere(not agora(), 'the READMEs put back are not clean', 'the READMEs put back: 0 findings')

        # ---- the source: a number changes exactly the drawings that show it, a premise gone stops the draw ----------------------------------------------------
        def copia(nome):
            raiz = tmp / nome
            copiar_fonte(fonte, raiz)
            return raiz

        slots = int(re.search(r'^#define MAX_SENSORS (\d+)\b', (ROOT / CONFIG).read_text(encoding='utf-8'), re.M).group(1))
        anel = int(re.search(r'\bUI_EV_RING = (\d+);', (ROOT / DISPLAY_H).read_text(encoding='utf-8')).group(1))
        toque_ms = int(re.search(r'\bTOUCH_PRIORITY_MS = (\d+);', (ROOT / APP_H).read_text(encoding='utf-8')).group(1))
        toque_s = toque_ms // 1000
        leitura = texto_visivel(desenhadas['architecture.svg'])
        confere(f'{slots} slots, ' in leitura and f'a ring of {anel} ' in leitura and f'for {toque_s} s after a touch' in leitura,
                f'the committed English figure does not say {slots} slots, a ring of {anel} and for {toque_s} s after a touch: {leitura[:300]!r}',
                f'the committed figure says what the sources say: {slots} sensor slots (MAX_SENSORS), a ring of {anel} events (UI_EV_RING), {toque_s} s after a touch (TOUCH_PRIORITY_MS)')
        for k, (descr, rel, antigo, novo, velha, nova) in enumerate((
                (f'MAX_SENSORS {slots} -> {slots - 4}', CONFIG, f'#define MAX_SENSORS {slots}', f'#define MAX_SENSORS {slots - 4}', f'{slots} slots, ', f'{slots - 4} slots, '),
                (f'UI_EV_RING {anel} -> {anel * 2}', DISPLAY_H, f'UI_EV_RING = {anel}', f'UI_EV_RING = {anel * 2}', f'a ring of {anel} ', f'a ring of {anel * 2} '),
                (f'TOUCH_PRIORITY_MS {toque_ms} -> {toque_ms + 3000}', APP_H, f'TOUCH_PRIORITY_MS = {toque_ms};', f'TOUCH_PRIORITY_MS = {toque_ms + 3000};',
                 f'for {toque_s} s after', f'for {toque_s + 3} s after'))):
            raiz = copia(f'fonte_{k}')
            trocar(raiz / rel, antigo, novo)
            novas, falta = desenhar_de(raiz)
            confere(falta is None and mudados(desenhadas, novas) == sorted(SEIS), f'{descr}: changed {mudados(desenhadas, novas) if novas else falta}, expected the six',
                    f'{descr} in a scratch copy of {rel}: all six drawings change')
            dita = texto_visivel(novas['architecture.svg']) if novas else ''
            confere(nova in dita and velha not in dita, f'{descr}: the English figure reads {dita[:200]!r}', f'  and the English figure says {nova.strip(" ,")!r}, no longer {velha.strip(" ,")!r}')
            a = achados(img, leit, raiz)
            confere(sorted(l.split(': ')[0] for l in a) == sorted(f'docs/images/{n}' for n in SEIS) and all('differs from what the code draws' in l for l in a),
                    f'--check on that source gave {a}', '  and --check on it finds the six committed files stale: exactly 6 findings, one for each')
        # a token colour: the three files of its theme change, the three of the other theme do not
        for tema, mudam in (('claro', CLAROS), ('escuro', ESCUROS)):
            raiz = copia(f'fonte_{tema}')
            css = raiz / FOLHA_ANGULO
            css.write_text(mudar_token(css.read_text(encoding='utf-8'), tema, 'tinta-2', '#123456'), encoding='utf-8')
            novas, falta = desenhar_de(raiz)
            parados = [n for n in SEIS if n not in mudam]
            confere(falta is None and mudados(desenhadas, novas) == sorted(mudam) and all('#123456' in novas[n] for n in mudam) and not any('#123456' in novas[n] for n in parados),
                    f'--tinta-2 of the {tema} block: changed {mudados(desenhadas, novas) if novas else falta}, expected {sorted(mudam)}',
                    f'--tinta-2 of the {tema} block changed in the tokens: the 3 {tema} files change (and carry the new colour), the 3 others do not')
            a = achados(img, leit, raiz)
            confere(sorted(l.split(': ')[0] for l in a) == sorted(f'docs/images/{n}' for n in mudam), f'--check on that source gave {a}', f'  and --check on it finds exactly those 3 files stale')

        # what stops the draw (exit 2): a manager with no role, a role with no manager, a premise gone; --write on it writes nothing
        def novo_cabecalho(raiz):
            (raiz / 'src' / 'FooManager.h').write_text('class FooManager {\n};\n', encoding='utf-8')

        def cabecalho_sem_classe(raiz):
            (raiz / 'src' / 'FooManager.h').write_text('// a manager that declares nothing\n', encoding='utf-8')

        def sem_cabecalho(nome):
            return lambda raiz: (raiz / 'src' / f'{nome}.h').unlink()

        for k, (descr, edita, agulhas) in enumerate((('a new header src/FooManager.h declaring FooManager, which PAPEIS gives no role', novo_cabecalho, ('FooManager', 'PAPEIS')),
                                                     ('src/MetricsManager.h gone, and PAPEIS still gives it a role', sem_cabecalho('MetricsManager'), ('MetricsManager', 'PAPEIS')),
                                                     ('src/FooManager.h that does not declare FooManager', cabecalho_sem_classe, ('src/FooManager.h', 'FooManager')),
                                                     ('src/AppManager.h gone, the loop of Core 0', sem_cabecalho('AppManager'), ('src/AppManager.h',)))):
            raiz = copia(f'recusa_{k}')
            edita(raiz)
            rc, so, se = em_processo('--check', '--images', str(img), '--readmes', str(leit), '--fonte', str(raiz))
            confere(rc == 2 and not so and all(x in se for x in agulhas), f'{descr}: exit {rc}, stdout {so.strip()[-100:]!r}, stderr {se.strip()[-240:]!r}', f'{descr}: exit 2, and the message says {" and ".join(agulhas)}')
            destino = tmp / f'recusado_{k}'
            rc, so, se = em_processo('--write', '--images', str(destino), '--fonte', str(raiz))
            confere(rc == 2 and all(x in se for x in agulhas) and not destino.exists(), f'--write on that source exited {rc} and {"wrote" if destino.exists() else "wrote nothing"}: {se.strip()[-200:]}',
                    '  and --write on it exits 2 and writes nothing')
        # one manager's @brief reworded so that the role PAPEIS gives it no longer summarizes it: the draw stops at that header
        antes = len(FALHAS)
        for nome in rd.PAPEIS:
            raiz = copia(f'brief_{nome}')
            cab = raiz / 'src' / f'{nome}.h'
            t = cab.read_text(encoding='utf-8')
            m = re.search(rd.PAPEIS[nome][0], t, md.FLAGS_PADRAO)
            assert m is not None, f'src/{nome}.h: PAPEIS\'s pattern matches nothing in the committed header'
            reescrito = t[:m.start()] + re.sub(r'\w+\W*$', 'other', m.group(0)) + t[m.end():]
            cab.write_text(reescrito, encoding='utf-8')
            rc, so, se = em_processo('--check', '--images', str(img), '--readmes', str(leit), '--fonte', str(raiz))
            confere(re.search(rd.PAPEIS[nome][0], reescrito, md.FLAGS_PADRAO) is None and rc == 2 and f'src/{nome}.h' in se and (nome != 'SensorManager' or 'PAPEIS' in se),
                    f'the @brief of {nome} reworded: exit {rc}, {se.strip()[-240:]}')
        resumo(antes, f'the @brief of each of the {len(rd.PAPEIS)} managers PAPEIS gives a role to, reworded in a scratch copy: exit 2 naming its header')

        # ---- a pattern the tool looks for, gone; a file it reads, gone: exit 2 naming it --------------------------------------------------------------------
        confere(len(fonte.padroes) >= 40, f'the tool recorded only {len(fonte.padroes)} patterns', f'{len(fonte.padroes)} patterns recorded (at least 40), each held to the control below')
        antes, pelo_fim = len(FALHAS), 0
        estragos = (lambda m: '§' + m.group(0)[1:], lambda m: m.group(0)[:-1] + '§')     # the first character of each match, and the last when the first leaves two patterns to be told
        for rel, padrao in fonte.padroes:
            viu = []
            for k, estraga in enumerate(estragos):
                raiz = tmp / 'padrao'
                shutil.rmtree(raiz, ignore_errors=True)
                copiar_fonte(fonte, raiz)
                t = (raiz / rel).read_text(encoding='utf-8')
                estragado = re.sub(padrao, estraga, t, flags=md.FLAGS_PADRAO)
                if estragado == t:
                    viu.append('the pattern matches nothing in the file the tool read it from')
                    continue
                (raiz / rel).write_text(estragado, encoding='utf-8')
                rc, so, se = em_processo('--check', '--images', str(img), '--readmes', str(leit), '--fonte', str(raiz))
                if rc == 2 and nomeia(se, rel, padrao):
                    pelo_fim += k
                    break
                viu.append(f'{("first", "last")[k]} character mangled: exit {rc}, message {se.strip()[-200:]}')
            else:
                confere(False, f'{rel}, pattern {padrao}: {" / ".join(viu)}')
                continue
            confere(True, None)
        resumo(antes, f'{len(fonte.padroes)} patterns, each mangled in a scratch copy: exit 2 naming the file and the pattern ({len(fonte.padroes) - pelo_fim} with the first character of the match mangled, '
                      f'{pelo_fim} with the last, because another pattern reads the same text and is told first)')
        antes = len(FALHAS)
        for rel in fonte.lidos:
            raiz = tmp / 'arquivo'
            shutil.rmtree(raiz, ignore_errors=True)
            copiar_fonte(fonte, raiz)
            (raiz / rel).unlink()
            rc, so, se = em_processo('--check', '--images', str(img), '--readmes', str(leit), '--fonte', str(raiz))
            confere(rc == 2 and not so and (rel in se or pathlib.PurePosixPath(rel).stem in se), f'{rel} deleted: exit {rc}, stdout {so.strip()[-100:]!r}, stderr {se.strip()[-200:]!r}')
        resumo(antes, f'{len(fonte.lidos)} files the tool read, each deleted in a scratch copy: exit 2 naming it')
        shutil.rmtree(tmp / 'padrao', ignore_errors=True)
        shutil.rmtree(tmp / 'arquivo', ignore_errors=True)

        # ---- determinism -----------------------------------------------------------------------------------------------------------------------------------
        outra = rd.desenhar_todas(md.Fonte(ROOT))
        confere(outra == desenhadas, 'two draws differ: ' + ', '.join(mudados(desenhadas, outra)), 'two draws in a row give the same text')
        pastas = []
        for semente in ('1', '2'):
            pasta = tmp / f'escritos_{semente}'
            r = cli('--write', '--images', str(pasta), ambiente={**os.environ, 'PYTHONHASHSEED': semente})
            confere(r.returncode == 0 and sorted(p.name for p in pasta.iterdir()) == sorted(SEIS), f'--write into {pasta.name} exited {r.returncode}: {r.stderr.strip()[-200:]}')
            pastas.append(pasta)
        iguais = all((pastas[0] / n).is_file() and (pastas[1] / n).is_file() and (pastas[0] / n).read_bytes() == (pastas[1] / n).read_bytes() == (IMAGENS / n).read_bytes() for n in SEIS)
        confere(iguais, 'two --write runs, with the hash seeds 1 and 2, or a run and the committed files, differ',
                'two --write runs in two processes (hash seeds 1 and 2) write the same bytes as each other and as the committed files, and only the six files')

        # ---- the command line ------------------------------------------------------------------------------------------------------------------------------
        escritos = tmp / 'escritos'
        r = cli('--write', '--images', str(escritos))
        r2 = cli('--check', '--images', str(escritos))
        confere(r.returncode == 0 and r2.returncode == 0 and r2.stdout.rstrip().endswith('0 finding(s)'), f'--write exited {r.returncode}, --check on what it wrote exited {r2.returncode}: {r2.stdout.strip()[-200:]}',
                'command line: --write, then --check on the files it wrote: exit 0')
        alvo = escritos / SEIS[0]
        original = alvo.read_bytes()
        alvo.unlink()
        r = cli('--check', '--images', str(escritos))
        confere(r.returncode == 1 and r.stdout.count('missing') == 1 and r.stdout.count('\n') == 2, f'a deleted SVG: exit {r.returncode}: {r.stdout.strip()[-200:]}', 'command line: a file gone, exit 1 with exactly one finding')
        alvo.write_bytes(original)
        r = cli('--check', '--fonte', str(tmp / 'vazia'))
        confere(r.returncode == 2 and 'cannot be read' in r.stderr and not r.stdout, f'an empty source folder: exit {r.returncode}: {r.stdout.strip()[-100:]} {r.stderr.strip()[-200:]}', 'command line: exit 2 when the sources cannot be read')
        r = cli('--write', '--images', str(tmp / 'nada'), '--fonte', str(tmp / 'vazia'))
        confere(r.returncode == 2 and not (tmp / 'nada').exists(), f'--write on an empty source folder: exit {r.returncode}, wrote {(tmp / "nada").exists()}', 'command line: --write on sources that cannot be read exits 2 and writes nothing')
        previa = tmp / 'previa'
        r = cli('--preview', str(previa), '--images', str(escritos))
        paginas = {i: previa / f'architecture.{i}.html' for i in rd.IDIOMAS}
        confere(r.returncode == 0 and sorted(p.name for p in previa.iterdir()) == ['architecture.en.html', 'architecture.es-ES.html', 'architecture.pt-BR.html'],
                f'--preview exited {r.returncode} and wrote {sorted(p.name for p in previa.iterdir()) if previa.is_dir() else None}', '--preview: exits 0 and writes architecture.en.html, architecture.pt-BR.html and architecture.es-ES.html')
        for idioma, pagina in paginas.items():
            texto = pagina.read_text(encoding='utf-8') if pagina.is_file() else ''
            partes = texto.split('<img ')[1:]
            alvos = [pathlib.Path(urllib.request.url2pathname(urllib.parse.urlparse(re.search(r'src="([^"]+)"', p).group(1)).path)).resolve() for p in partes if re.search(r'src="([^"]+)"', p)]
            claro, escuro = ARQUIVOS[idioma]
            fundos = [re.findall(r'background:(#[0-9a-fA-F]{6})', texto.split('<img ')[k])[-1:] for k in range(len(partes))]
            confere(sorted(alvos) == sorted([(escritos / claro).resolve(), (escritos / escuro).resolve()]) and fundos == [[rd.FUNDO_GITHUB['claro']], [rd.FUNDO_GITHUB['escuro']]] and 'width:838px' in texto
                    and alvos == [(escritos / claro).resolve(), (escritos / escuro).resolve()],
                    f'{pagina.name}: <img> {alvos}, backgrounds {fundos}', f'{pagina.name}: two <img>, the light file ({claro}) on the light background and the dark file ({escuro}) on the dark one, in a 838 px column')

        # ---- the line breaker ------------------------------------------------------------------------------------------------------------------------------
        def planas(linhas):
            return [''.join(s for _, s in linha) for linha in linhas]

        confere(rd.runs_de('a `b c` d') == [('', 'a '), ('dg-m', 'b c'), ('', ' d')] and rd.runs_de('`x`') == [('dg-m', 'x')], f'runs_de gave {rd.runs_de("a `b c` d")}', 'runs_de( ) sets what is between backticks in the code\'s font, spaces kept')
        try:
            rd.runs_de('a `b')
            confere(False, 'runs_de( ) let an unbalanced backtick through')
        except ValueError:
            confere(True, None, 'runs_de( ) refuses an unbalanced backtick')
        # loop( ) is one word: a line that would take it up to the parenthesis ends before it
        largura = rd.largura_runs([('', 'It feeds the '), ('dg-m', 'loop(')])
        confere(largura < rd.largura_runs([('', 'It feeds the '), ('dg-m', 'loop( )')]), 'the scenario of loop( ) is not one in which a cut inside the code would fit', None)
        linhas = rd.quebrar_runs(rd.runs_de('It feeds the `loop( )` now'), largura)
        confere(planas(linhas) == ['It feeds the', 'loop( ) now'] and [r for linha in linhas for r in linha if r[0] == 'dg-m'] == [('dg-m', 'loop( )')] and all(rd.largura_runs(linha) <= largura for linha in linhas),
                f'quebrar_runs gave {linhas} at {largura} px', f'loop( ) is kept whole: {planas(linhas)} at {largura} px, where the line could have taken it up to "loop("')
        solta = rd.largura_runs([('', 'It feeds the loop(')])
        confere(planas(rd.quebrar_runs(rd.runs_de('It feeds the loop( ) now'), solta)) == ['It feeds the loop(', ') now'], f'the same words without backticks gave {planas(rd.quebrar_runs(rd.runs_de("It feeds the loop( ) now"), solta))}',
                '  and the same words without backticks are cut at the inner space: the backticks are what keep it whole')
        # a parenthesis and a full stop with no space between them and a code word go with it
        palavra = [('', '('), ('dg-m', 'TouchPriority'), ('', ').')]
        largura = rd.largura_runs(palavra) + 4
        confere(rd.largura_runs([('', 'holds them (')]) <= largura < rd.largura_runs([('', 'holds them ')] + palavra), 'the scenario of TouchPriority is not one in which the parenthesis would fit alone on the first line', None)
        linhas = rd.quebrar_runs(rd.runs_de('holds them (`TouchPriority`).'), largura)
        confere(linhas == [[('', 'holds them')], palavra], f'quebrar_runs gave {linhas} at {largura} px', f'the ( before TouchPriority and the ). after it go with it to the next line: {planas(linhas)} at {largura} px')
        # a no-break space keeps two words on one line
        largura = rd.largura_runs([('', 'web UI, REST')])
        com_nbsp = planas(rd.quebrar_runs(rd.runs_de('web UI, REST\u00a0API'), largura))
        com_espaco = planas(rd.quebrar_runs(rd.runs_de('web UI, REST API'), largura))
        confere(com_nbsp == ['web UI,', 'REST\u00a0API'] and com_espaco == ['web UI, REST', 'API'], f'at {largura} px: with U+00A0 {com_nbsp}, with a space {com_espaco}',
                f'REST\\u00a0API stays on one line ({com_nbsp[1]!r}), where "REST API" with a space is cut ({com_espaco}), at {largura} px')
        # a word wider than the line is an error, a glued word counts as one
        codigo = rd.largura_runs([('dg-m', 'TouchPriority')])
        for descr, texto, w in (('a word wider than its line', 'a `ReallyLongIdentifierName` b', 60), ('a glued word wider than its line, though the code alone fits', 'holds them (`TouchPriority`).', codigo + 4)):
            try:
                rd.quebrar_runs(rd.runs_de(texto), w)
                confere(False, f'quebrar_runs let {descr} through at {w} px')
            except ValueError as e:
                confere('is wider than the' in str(e) and f'{w} px' in str(e), f'{descr}: the error says {e}', f'quebrar_runs refuses {descr} ({w} px): ValueError')
        # the real notes, at many widths: nothing lost, nothing past the line, no line starting with a closing parenthesis, no code cut
        antes = len(FALHAS)
        notas = [(chave, idioma, rd.TEXTOS[chave][idioma].format(anel=anel, toque_s=toque_s)) for chave in ('core0_nota', 'core1_nota', 'eventos') for idioma in rd.IDIOMAS]
        for chave, idioma, texto in notas:
            ruins, quebradas = [], 0
            for w in range(120, 120 + 4 * 62, 4):
                try:
                    linhas = rd.quebrar_runs(rd.runs_de(texto), w)
                except ValueError:
                    continue
                quebradas += 1
                lidas = planas(linhas)
                codigos = sorted(s for linha in linhas for c, s in linha if c == 'dg-m')
                if (' '.join(lidas) != texto.replace('`', '') or any(rd.largura_runs(linha) > w for linha in linhas) or any(l.startswith(')') for l in lidas)
                        or codigos != sorted(s for c, s in rd.runs_de(texto) if c == 'dg-m')):
                    ruins.append(w)
            confere(quebradas >= 20 and not ruins, f'{chave} ({idioma}): broken wrongly at {ruins[:5]} px, and broken at {quebradas} widths')
        resumo(antes, f'the {len(notas)} real notes (Core 0, Core 1 and the events, three languages) broken at up to 62 widths: no character lost, no line past its width, none starting with ")", no code cut')
        for nome in SEIS:
            texto = desenhadas[nome]
            confere('<tspan class="dg-m">loop( )</tspan>' in texto and re.search(r'\(<tspan class="dg-m">TouchPriority</tspan>\)\.</text>', texto) is not None,
                    f'{nome}: loop( ) or TouchPriority is cut in the drawing', f'{nome}: loop( ) is one piece and TouchPriority stands between its parenthesis and its full stop in the same line')

        # ---- the tree --------------------------------------------------------------------------------------------------------------------------------------
        depois = impressao()
        confere(depois == antes_da_arvore, 'the controls changed ' + ', '.join(k for k in depois if depois[k] != antes_da_arvore.get(k)) + ' in the tree',
                'the six drawings, the three READMEs and the tokens are the same bytes after the controls as before: they wrote only into scratch copies')
    print(f'test_readme_diagram: {CONTA[0]} controls, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
