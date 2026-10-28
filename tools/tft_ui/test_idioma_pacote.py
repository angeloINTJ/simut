#!/usr/bin/env python3
"""test_idioma_pacote.py -- proves the Angulo's Idioma screen lists the pack an English boot found in /lang, and marks the saved language (S10c, sync 6, C3: #294 on the Angulo).

#294 (2026-10-24) made the panel's Idioma screen list a pack it has only FOUND in /lang when the unit booted in English: a pack loads only at a boot in another language, so before it a unit
that booted in English listed English alone and nothing on a product image could write the language back. The original's draw and touch took the change (isLangListed( ), langRowsListed( )); the
Angulo's two sites in src/ui/angulo/DisplayManager_UiA_Menus.cpp kept reading _activeLangLoaded, and no frame shows it (tools/tft_ui/amostras build the screen state directly). So the firmware's own
text is run: nomeDoPacote( ), the braced `case MODE_SETTINGS_LANG: {` of uaMontarMenus( ), the statements uaToqueMenus( ) declares before its switch, and the UNBRACED `case MODE_SETTINGS_LANG:` of
uaToqueMenus( ) are cut out of the live file at every run, verbatim and without comments, by brace matching (as test_temas.py does for the Tema screen), together with the statement of the core's
showSettingsLang( ) that sets the row the screen opens on, and tools/tft_ui/host/test_idioma_pacote_main.cpp compiles them against a stand-in DisplayManager, the firmware's own display/LangChoice.h and the
kit's own telas/MenusConfig.cpp (alvosIdioma( ) says how many rows the glass lists) and runs five cases: (1) an English boot with a pack listed, (2) no pack, (3) a pack loaded and in use, (4) the saved
language reopened after an English boot (D5: the mark is the saved row, _previewLangIdx, as the original marks it; the screen opens through the core's showSettingsLang( ), which sets it with
langRowAtOpen( )), (5) Back.

The touch site is not braced, so it has its own cutter, which refuses to run when it cannot find the site exactly once, finds no break, finds another label before it, or finds a block without
actApplyLang( ) or without Back's arm: it never cuts a smaller block (its controls are below). Then the instrument is bitten: one program per mutant of the live file (the draw, the touch, or the mark
turned back to what they read before #294) must exit 1.

Usage: python3 tools/tft_ui/test_idioma_pacote.py   exit 0 when all hold, 1 when not, 2 when it cannot build.
"""
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)

KIT = ROOT / 'src' / 'ui' / 'angulo'
MENUS_GLUE = KIT / 'DisplayManager_UiA_Menus.cpp'
MAIN = HERE / 'host' / 'test_idioma_pacote_main.cpp'
I18N = ROOT / 'src' / 'DisplayManager_i18n.cpp'
EVENTS = ROOT / 'src' / 'AppManager_Events.cpp'
RECORDS = ROOT / 'src' / 'SystemDefs_Records.h'

CASOS = []


def caso(nome, problemas):
    ok = problemas is True or (not isinstance(problemas, bool) and not problemas)
    CASOS.append((nome, ok))
    print(('[ok  ] ' if ok else '[FAIL] ') + nome)
    if not ok and not isinstance(problemas, bool):
        for p in problemas[:6]:
            print('       ' + str(p))


def apaga_comentarios(t):
    """`t` with every comment turned into blanks, the newlines kept, so that offsets and lines stay: a string or a character literal is skipped, not read as a comment."""
    saida, i, n = [], 0, len(t)
    while i < n:
        c = t[i]
        if t.startswith('//', i):
            j = t.find('\n', i)
            j = n if j < 0 else j
            saida.append(' ' * (j - i))
            i = j
        elif t.startswith('/*', i):
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            saida.append(''.join(ch if ch == '\n' else ' ' for ch in t[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            saida.append(t[i:j + 1])
            i = j + 1
        else:
            saida.append(c)
            i += 1
    return ''.join(saida)


def fim_do_bloco(t, abre):
    """The index just after the `}` that closes the `{` at t[abre], -1 when none does. `t` has no comments."""
    d, i, n = 0, abre, len(t)
    while i < n:
        c = t[i]
        if c in '"\'':
            i += 1
            while i < n and t[i] != c:
                i += 2 if t[i] == '\\' else 1
        elif c == '{':
            d += 1
        elif c == '}':
            d -= 1
            if d == 0:
                return i + 1
        i += 1
    return -1


def definicao(limpo, cabeca):
    """(start, end) of the function definition whose head matches the regex `cabeca` (written to begin at a line's start), from the start of its line to its closing brace; None when the text holds it zero
    times or several, or only as a declaration. `limpo` has no comments."""
    achados = list(re.finditer(cabeca, limpo, re.M))
    if len(achados) != 1:
        return None
    m = achados[0]
    i, d = m.end() - 1, 0
    while i < len(limpo):
        d += (limpo[i] == '(') - (limpo[i] == ')')
        i += 1
        if d == 0:
            break
    k = limpo.find('{', i)
    if k < 0 or ';' in limpo[i:k]:
        return None
    f = fim_do_bloco(limpo, k)
    return (m.start(), f) if f > 0 else None


def intervalos_do_caso(texto, rotulo):
    """(start, end) of each braced body that begins with `rotulo`: from the label to the closing brace."""
    out, i = [], 0
    while True:
        i = texto.find(rotulo, i)
        if i < 0:
            return out
        k = texto.index('{', i)
        j = fim_do_bloco(texto, k)
        if j < 0:
            return out
        out.append((i, j))
        i = j


DIRETIVA = re.compile(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b[^\n]*', re.M)


def pilha_em(limpo, pos):
    """The conditionals open at `pos`, outermost first."""
    pilha = []
    for m in DIRETIVA.finditer(limpo):
        if m.start() >= pos:
            break
        k = m.group(1)
        if k in ('if', 'ifdef', 'ifndef'):
            pilha.append(m.group(0).strip())
        elif k == 'endif':
            if pilha:
                pilha.pop()
        elif pilha:
            pilha[-1] += ' | ' + m.group(0).strip()
    return pilha


def corta_toque_idioma(corpo):
    """(text, None) of the UNBRACED `case MODE_SETTINGS_LANG:` of the text of uaToqueMenus( ) (no comments) whose arm opens with `if ( voltar )`, from its label to its `break;`; (None, why) when the site is not there exactly once, has no break at
    the depth of the case, meets another label first, goes on after the break, or is a smaller block than the case (no actApplyLang( ), no Back arm)."""
    todos = list(re.finditer(r'case\s+MODE_SETTINGS_LANG\s*:(?!\s*\{)', corpo))
    # uaToqueMenus( ) has two unbraced labels of the mode (the first switch lists the modes this function answers); the site is the one whose arm opens with Back
    achados = [m for m in todos if corpo[m.end():].lstrip().startswith('if ( voltar )')]
    if len(achados) != 1:
        return None, f'`case MODE_SETTINGS_LANG:` (unbraced, opening with `if ( voltar )`) was found {len(achados)} times in uaToqueMenus( ) ({len(todos)} unbraced labels of the mode), expected once'
    ini, i, d, fim = achados[0].start(), achados[0].end(), 0, -1
    while i < len(corpo):
        c = corpo[i]
        if c in '"\'':
            i += 1
            while i < len(corpo) and corpo[i] != c:
                i += 2 if corpo[i] == '\\' else 1
        elif c == '{':
            d += 1
        elif c == '}':
            d -= 1
            if d < 0:
                return None, 'the switch closed before the case found a break'
        elif d == 0 and not (corpo[i - 1].isalnum() or corpo[i - 1] == '_'):
            if re.match(r'break\s*;', corpo[i:]):
                fim = corpo.index(';', i) + 1
                break
            if re.match(r'(case\b|default\s*:)', corpo[i:]):
                return None, 'another label comes before the case found a break'
        i += 1
    if fim < 0:
        return None, 'the case has no break'
    bloco = corpo[ini:fim]
    for preciso in ('actApplyLang', 'showSettingsMain', 'acceptTouch( ZONA_VOLTAR )', 'acceptTouch( ZONA_LINHA )', 'if ( voltar )'):
        if preciso not in bloco:
            return None, f'the block cut is smaller than the case: it holds no `{preciso}`'
    resto = corpo[fim:].lstrip()
    if not (resto.startswith('case ') or resto.startswith('default') or resto.startswith('}')):
        return None, 'the case goes on after its break'
    return bloco, None


TOQUE_PROLOGO = r'^[ \t]*const bool voltar\b[^;]*;\s*const int i\b[^;]*;'   # the statements before the switch of uaToqueMenus( ) that the case reads (voltar, linha, i)


def recorta(menus, i18n):
    """The pieces the host program compiles: ({file name: text}, [problem]). Verbatim and without comments; a piece that holds a preprocessor line, or that a conditional the function is not under encloses, is refused."""
    limpo = apaga_comentarios(menus)
    pecas, falta, pos = {}, [], []
    d = definicao(limpo, r'^(?!\s)[^\n;{}()]*\bnomeDoPacote\s*\(')
    if d is None:
        falta.append('nome_do_pacote.inc: nomeDoPacote( ) was not found exactly once, as a definition, in DisplayManager_UiA_Menus.cpp')
    else:
        pecas['nome_do_pacote.inc'] = limpo[d[0]:d[1]] + '\n'
        pos.append(('nomeDoPacote( )', d[0], d[1], d[0]))
    m = definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::uaMontarMenus\s*\(')
    if m is None:
        falta.append('montar_idioma.inc: DisplayManager::uaMontarMenus( ) was not found exactly once')
    else:
        corpo = limpo[m[0]:m[1]]
        cs = intervalos_do_caso(corpo, 'case MODE_SETTINGS_LANG: {')
        if len(cs) != 1:
            falta.append(f'montar_idioma.inc: uaMontarMenus( ) has {len(cs)} bodies of `case MODE_SETTINGS_LANG: {{`, expected 1')
        else:
            pecas['montar_idioma.inc'] = corpo[cs[0][0]:cs[0][1]] + '\n'
            pos.append(('uaMontarMenus( ), case MODE_SETTINGS_LANG', m[0] + cs[0][0], m[0] + cs[0][1], m[0]))
    f = definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::uaToqueMenus\s*\(')
    if f is None:
        falta.append('toque_idioma.inc: DisplayManager::uaToqueMenus( ) was not found exactly once, as a definition')
    else:
        corpo = limpo[f[0]:f[1]]
        pre = list(re.finditer(TOQUE_PROLOGO, corpo, re.M))
        if len(pre) != 1:
            falta.append(f'toque_prologo.inc: the statements `const bool voltar = ...;` and `const int i = ...;` of uaToqueMenus( ) were found {len(pre)} times, expected once')
        else:
            pecas['toque_prologo.inc'] = pre[0].group(0).strip() + '\n'
        bloco, porque = corta_toque_idioma(corpo)
        if bloco is None:
            falta.append('toque_idioma.inc: ' + porque)
        else:
            pecas['toque_idioma.inc'] = bloco + '\n'
            a = corpo.index(bloco)
            pos.append(('uaToqueMenus( ), case MODE_SETTINGS_LANG', f[0] + a, f[0] + a + len(bloco), f[0]))
    limpo_i = apaga_comentarios(i18n)
    s = definicao(limpo_i, r'^[^\n;{}()]*\bDisplayManager::showSettingsLang\s*\(')
    if s is None:
        falta.append('mostrar_idioma.inc: DisplayManager::showSettingsLang( ) was not found exactly once in DisplayManager_i18n.cpp')
    else:
        corpo_s = limpo_i[s[0]:s[1]]
        achados = list(re.finditer(r'_previewLangIdx\s*=\s*langRowAtOpen\([^;]*;', corpo_s))
        if len(achados) != 1 or not re.search(r'_uiMode\s*=\s*MODE_SETTINGS_LANG\s*;', corpo_s):
            falta.append('mostrar_idioma.inc: showSettingsLang( ) does not set _uiMode = MODE_SETTINGS_LANG and _previewLangIdx = langRowAtOpen( ... ) exactly once')
        else:
            pecas['mostrar_idioma.inc'] = achados[0].group(0) + '\n'
    pilhas = {pilha_em(limpo, c) and tuple(pilha_em(limpo, c)) or () for _, _, _, c in pos}
    for rotulo, a, b, c in pos:
        dentro = [x.group(0).strip() for x in DIRETIVA.finditer(limpo, a, b)]
        if dentro:
            falta.append(f'{rotulo} holds a preprocessor line ({dentro[0]}): the host program compiles the text between its braces whichever way the preprocessor goes on the firmware')
    if len(pilhas) > 1:
        falta.append(f'the three pieces of the menus glue are not under the same conditionals: {sorted(pilhas)}')
    return pecas, falta


def fontes_amarradas(events, records):
    """[problem]: the two facts the stand-in assumes and the cut text does not carry: Core 0 opens the screen through showSettingsLang( ) with the saved language, and the language ids are 0, 1 and 2."""
    p = []
    ev = apaga_comentarios(events)
    if len(re.findall(r'MENU_LANG\s*\)\s*\{\s*_displayMgr->showSettingsLang\(\s*_storageMgr->getConfig\(\s*\)\.displayLang\s*\)\s*;', ev)) != 1:
        p.append('AppManager_Events.cpp: the MENU_LANG arm does not call _displayMgr->showSettingsLang( _storageMgr->getConfig( ).displayLang ) exactly once: the Idioma screen may no longer open through the core')
    rc = apaga_comentarios(records)
    m = re.search(r'enum\b[^{;]*\{([^{}]*\bLANG_COUNT\b[^{}]*)\}', rc)
    if not m:
        p.append('SystemDefs_Records.h: the enum of the languages (with LANG_COUNT) was not found')
    else:
        valor, vals = -1, {}
        for item in m.group(1).split(','):
            item = item.strip()
            if not item:
                continue
            nome, _, v = item.partition('=')
            valor = int(v.strip(), 0) if v.strip() else valor + 1
            vals[nome.strip()] = valor
        if (vals.get('LANG_EN'), vals.get('LANG_PT'), vals.get('LANG_COUNT')) != (0, 1, 2):
            p.append(f'SystemDefs_Records.h: LANG_EN, LANG_PT and LANG_COUNT are {vals}, the host program assumes 0, 1 and 2')
    return p


def compila(src, obj, inc, estrito):
    r = subprocess.run(['g++', *inc, *(['-Wall', '-Wextra', '-Werror'] if estrito else ['-w']), '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
    return r.returncode, r.stderr


def constroi(tmp, nome, menus, i18n, libs, inc):
    """The program with the pieces cut out of `menus`. Returns (executable, '') or (None, why)."""
    pecas, falta = recorta(menus, i18n)
    if falta:
        return None, 'cut: ' + '; '.join(falta)
    d = tmp / nome
    d.mkdir()
    for arq, texto in pecas.items():
        (d / arq).write_text(texto, encoding='utf-8')
    rc, err = compila(MAIN, d / 'main.o', inc + [f'-I{d}'], False)
    if rc:
        return None, err
    exe = d / 'idioma'
    r = subprocess.run(['g++', *[str(x) for x in libs], str(d / 'main.o'), '-o', str(exe)], capture_output=True, text=True)
    return (exe, '') if r.returncode == 0 else (None, r.stderr)


def corre(exe):
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    return r.returncode, r.stdout


# The mutants: the sites of the fixed text turned back to what they read before #294 (the first two: _activeLangLoaded; the third: the mark from _currentLangIdx, which an English boot leaves at English).
MUTANTES = [
    ('the draw lists by _activeLangLoaded again', 'if ( isLangListed( ) ) nomeDoPacote(', 'if ( _activeLangLoaded ) nomeDoPacote('),
    ('the touch bounds the row by _activeLangLoaded again', 'i < langRowsListed( isLangLoaded( ), isLangListed( ) )', 'i < ( _activeLangLoaded ? LANG_COUNT : 1 )'),
    ('the mark is _currentLangIdx again (D5)', '_previewLangIdx == LANG_EN ? ID_INGLES : ID_PACOTE', '_currentLangIdx == LANG_EN ? ID_INGLES : ID_PACOTE'),
]


def main():
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found (run pio run -e pico_wiretft_angulo once)', file=sys.stderr)
        return 2
    menus = MENUS_GLUE.read_text(encoding='utf-8')
    i18n = I18N.read_text(encoding='utf-8')
    caso('the stand-in\'s two assumptions hold in the source (Core 0 opens the screen through showSettingsLang( ); the language ids are 0, 1, 2)', fontes_amarradas(EVENTS.read_text(encoding='utf-8'), RECORDS.read_text(encoding='utf-8')))
    pecas, falta = recorta(menus, i18n)
    caso('the five pieces are cut out of the live files (the unbraced touch case by its own cutter)', falta or (['expected five pieces, got ' + str(sorted(pecas))] if len(pecas) != 5 else []))
    # the touch cutter refuses what it cannot cut whole
    limpo = apaga_comentarios(menus)
    f = definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::uaToqueMenus\s*\(')
    corpo = limpo[f[0]:f[1]] if f else ''
    bloco, _ = corta_toque_idioma(corpo)
    controles = []
    if bloco:
        controles = [('no site', corpo.replace(bloco, bloco.replace('case MODE_SETTINGS_LANG:', 'case MODE_SETTINGS_LANG_X:', 1), 1), 'found 0 times'),
                     ('the site twice', corpo + '\n' + bloco, 'found 2 times'),
                     ('a smaller block (no actApplyLang)', corpo.replace('actApplyLang( i );', ';', 1), 'no `actApplyLang`'),
                     ('no break', corpo.replace(bloco, bloco.rstrip().removesuffix('break;') + ' ', 1), None),
                     ('the case goes on after its break', corpo.replace(bloco, bloco + ' i++; ', 1), 'goes on after')]
    mal = []
    for nome, texto, palavra in controles:
        b, porque = corta_toque_idioma(texto)
        if b is not None or (palavra and palavra not in porque):
            mal.append(f'{nome}: the cutter returned {"a block" if b is not None else repr(porque)}')
    caso('control: the touch cutter refuses a text without the site, with it twice, with a smaller block, without its break, or with the case going on', mal or (['the cutter has nothing to cut'] if not bloco else []))
    if falta:
        print('\n'.join(falta), file=sys.stderr)
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{HERE / "host" / "shim"}', f'-I{gfx}', f'-I{KIT}', f'-I{ROOT / "src" / "display"}']
    with tempfile.TemporaryDirectory(prefix='idioma_host_') as t:
        tmp = pathlib.Path(t)
        objs = {}
        for nome, src, estrito in (('gfx', gfx / 'Adafruit_GFX.cpp', False), ('textos', KIT / 'Textos.cpp', True), ('angulo', KIT / 'Angulo.cpp', True),
                                   ('menusconfig', KIT / 'telas' / 'MenusConfig.cpp', True), ('menuslista', KIT / 'telas' / 'MenusLista.cpp', True), ('telas', KIT / 'telas' / 'Telas.cpp', True)):
            objs[nome] = tmp / f'{nome}.o'
            rc, err = compila(src, objs[nome], inc + ([f'-I{KIT / "telas"}'] if 'telas' in str(src) else []), estrito)
            if rc:
                print(err, file=sys.stderr)
                return 2
        r = subprocess.run(['ld', '-r', str(objs['gfx']), str(objs['textos']), '-o', str(tmp / 'Biblioteca.o')], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr, file=sys.stderr)
            return 2
        libs = [tmp / 'Biblioteca.o', objs['angulo'], objs['menusconfig'], objs['menuslista'], objs['telas']]
        jobs = [('real', menus)]
        ausentes = []
        for nome, velho, novo in MUTANTES:
            if menus.count(velho) != 1:
                ausentes.append(nome)
                jobs.append((None, None))
            else:
                jobs.append((f'm{len(jobs)}', menus.replace(velho, novo)))
        with concurrent.futures.ThreadPoolExecutor(4) as ex:
            feitos = list(ex.map(lambda j: constroi(tmp, j[0], j[1], i18n, libs, inc) if j[0] else (None, 'no mutant'), jobs))
        exe, err = feitos[0]
        if exe is None:
            print(err, file=sys.stderr)
            return 2
        rc, saida = corre(exe)
        for l in saida.splitlines():
            if l.startswith('FAIL'):
                print('       ' + l)
        rodou = {}
        for l in saida.splitlines():
            m = re.match(r'CASE (\d) (.*): (\d+) checks, (\d+) failed', l)
            if m:
                rodou[int(m.group(1))] = (m.group(2), int(m.group(3)), int(m.group(4)))
        for n in range(1, 6):
            nome, total, falhas = rodou.get(n, ('?', 0, 1))
            caso(f'case {n}, {nome}: {total} checks run on the live text, {falhas} failed', falhas == 0 and total > 0)
        resumo = saida.strip().splitlines()[-1] if saida.strip() else 'no output'
        caso(f'host program, the firmware\'s own pieces ({resumo})', rc == 0 and resumo.endswith(' 0 failed'))
        for (nome, velho, novo), (exe_m, err_m) in zip(MUTANTES, feitos[1:]):
            if nome in ausentes:
                caso(f'control: {nome} must fail the program', [f'the mutant\'s line is not in DisplayManager_UiA_Menus.cpp exactly once: {velho!r}'])
            elif exe_m is None:
                caso(f'control: {nome} must fail the program', [f'the mutant does not build: {err_m.strip()[:300]}'])
            else:
                rc_m, saida_m = corre(exe_m)
                caso(f'control: {nome} must fail the program', rc_m == 1 and 'FAIL' in saida_m)
    falhou = [n for n, ok in CASOS if not ok]
    print(f'\n{len(falhou)} of {len(CASOS)} checks failed' if falhou else f'\nall {len(CASOS)} checks passed')
    return 1 if falhou else 0


if __name__ == '__main__':
    sys.exit(main())
