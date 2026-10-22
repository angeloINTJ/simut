#!/usr/bin/env python3
"""test_temas.py -- proves the Angulo's mode and accent on the glass (S10c, A'2) on the host.

In the Angulo a theme is the mode (light, dark, automatic) plus one accent of the palette; cfg.themeIndex holds both
(ua::escolhaDoIndice / ua::indiceDaEscolha, src/ui/angulo/Angulo.cpp). Builds tools/tft_ui/host/test_temas_main.cpp with the kit
and Adafruit GFX 1.12.6 (as test_cantos.py does) and runs it: the decode over all 256 int8_t values, ua::tema( ) and ua::desab( )
against the generated tables (17 roles x every entry x 2 modes), and the frame identity the glue hashes (two frames that differ
only in the accent hash differently and ask for a full repaint). Then the glue is linked (S10c, A'2b: tools/tft_ui/host/test_temas_glue_main.cpp cuts fnv( ), DisplayManager::uaTema( ) and the two runs of statements that hash
the view model and the identity out of the glue, verbatim, compiles them against a stand-in for the few members they touch and runs them over every index, preview
and hour: the review of A'2 found three mutants of the glue that every text check passed), and the glue and the sources are read as text: the glue builds
the identity only through ua::identidadeDoQuadro( ), from the theme uaTema( ) gives, and ACENTOS_N lives only in the two
encoding functions.

Then the instrument is bitten: the same program built with one mutant of the kit each (the decode by a modulo instead of the range
check, desab( ) and tema( ) stuck on entry 0, the identity or the full-repaint trigger without the accent, the table without its
accent field) must exit 1, and the text checks must report a glue that drops the theme or the trigger, and so must the glue link for every mutant of GLUE_MUTANTES (the accent, the mode, the hour,
the preview or part of the hash dropped).

Usage: python3 tools/tft_ui/test_temas.py   exit 0 when all hold, 1 when not, 2 when it cannot build.
"""
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import unicodedata

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)

KIT = ROOT / 'src' / 'ui' / 'angulo'
GLUE = KIT / 'DisplayManager_UiA.cpp'
MENUS_GLUE = KIT / 'DisplayManager_UiA_Menus.cpp'
MENUS_H = KIT / 'telas' / 'MenusConfig.h'
MENUS_CPP = KIT / 'telas' / 'MenusConfig.cpp'
TEMAS_CPP = KIT / 'Temas.cpp'
ANGULO = KIT / 'Angulo.cpp'
GLUE_MAIN = HERE / 'host' / 'test_temas_glue_main.cpp'

CASOS = []


def caso(nome, problemas):
    if isinstance(problemas, bool):
        problemas = [] if problemas else ['false']
    CASOS.append((nome, not problemas))
    print(('ok   ' if not problemas else 'FAIL ') + nome + ('' if not problemas else ': ' + '; '.join(problemas[:4])), flush=True)


# Each mutant: (name, text it replaces in Angulo.cpp, what replaces it, the check that must notice). Every `old` is the kit's own line.
MUTANTES = [
    ('decode by modulo instead of the range check',
     '  if ( t < 0 || t >= 3 * entrega::ACENTOS_N ) return Escolha{ MODO_CLARO, 0 };\n',
     '  t = (int8_t)( t % ( 3 * entrega::ACENTOS_N ) );\n'),
    ('desab( ) takes every twin from entry 0',
     '[t.entrada < kAcentos ? t.entrada : 0];', '[0];'),
    ('tema( ) gives entry 0 for every accent',
     'return kPaleta.t[escuro ? 1 : 0][acento < kAcentos ? acento : 0];', 'return kPaleta.t[escuro ? 1 : 0][0 * acento];'),
    ('the table never records its accent',
     'e.perigoSuave, e.perigoTinta, escuro, entrada };', 'e.perigoSuave, e.perigoTinta, escuro, 0 * entrada };'),
    ('the identity leaves the accent out of the bytes',
     '(uint8_t)t.escuro, t.entrada, ox, oy };', '(uint8_t)t.escuro, 0, ox, oy };'),
    ('the full-repaint trigger leaves the accent out',
     ' || acento != antes.acento;', ';'),
]


# The glue's own statements about the theme (S10c, A'2b): each entry is a change to DisplayManager_UiA.cpp that the glue link must refuse. (name, the glue's text, what replaces it,
# how many times the text occurs.) The first three are the mutants the review of A'2 found passing every check of A'2; the others were found by mutating the same statements.
GLUE_MUTANTES = [
    ('the accent never reaches the glass: uaTema( ) asks ua::tema( ) for entry 0', 'return ua::tema( escuro, e.acento );', 'return ua::tema( escuro, 0 );', 1),
    ('the dark mode inverted in uaTema( )', 'bool escuro = e.modo == ua::MODO_ESCURO;', 'bool escuro = e.modo != ua::MODO_ESCURO;', 1),
    ('both hashes cut the identity after 2 bytes, so the accent leaves them', 'h = fnv( &id, sizeof id, h );', 'h = fnv( &id, 2, h );', 2),
    ('the update screen hash cuts the identity after 2 bytes', '(uint8_t)MODE_OTA_UPDATE, tema, 0, 0 );\n  h = fnv( &id, sizeof id, h );',
     '(uint8_t)MODE_OTA_UPDATE, tema, 0, 0 );\n  h = fnv( &id, 2, h );', 1),
    ('the paint hash cuts the identity after 2 bytes', '(uint8_t)_uiMode, tema, ox, oy );\n  h = fnv( &id, sizeof id, h );',
     '(uint8_t)_uiMode, tema, ox, oy );\n  h = fnv( &id, 2, h );', 1),
    ('the identity restarts the hash and drops the view model', 'h = fnv( &id, sizeof id, h );', 'h = fnv( &id, sizeof id );', 2),
    ('the view model leaves the hash', 'uint32_t h = fnv( q.vm, q.tam );', 'uint32_t h = fnv( q.vm, 0 );', 2),
    ('the paint identity swaps ox and oy', 'tema, ox, oy );', 'tema, oy, ox );', 1),
    ('the paint identity leaves the screen out', '(uint8_t)_uiMode, tema, ox, oy )', '0, tema, ox, oy )', 1),
    ('the automatic mode inverted', 'if ( e.modo == ua::MODO_AUTOMATICO )', 'if ( e.modo != ua::MODO_AUTOMATICO )', 1),
    ('dark from 20:00 instead of 19:00', '( h >= 19 || h < 7 )', '( h >= 20 || h < 7 )', 1),
    ('dark only until 5:59 instead of 6:59', '( h >= 19 || h < 7 )', '( h >= 19 || h < 6 )', 1),
    ('dark only where both bounds hold', '( h >= 19 || h < 7 )', '( h >= 19 && h < 7 )', 1),
    ('no clock reads as night', 'uaRelogio( hora, h, provisoria ) && (', 'uaRelogio( hora, h, provisoria ) || (', 1),
    ('the preview of index 0 is ignored', '_uaPrevia >= 0 )', '_uaPrevia > 0 )', 1),
    ('every frame takes the preview, not the Tema screen alone', '( modo == MODE_SETTINGS_THEMES && _uaPrevia >= 0 )', '( (void)modo, _uaPrevia >= 0 )', 1),
    ('the saved theme is ignored', '_uaPrevia : _sysConfigPtr ? _sysConfigPtr->themeIndex : 0 )', '_uaPrevia : 0 )', 1),
]


def compila(src, obj, inc, estrito):
    r = subprocess.run(['g++', *inc, *(['-Wall', '-Wextra', '-Werror'] if estrito else ['-w']), '-c', str(src), '-o', str(obj)],
                       capture_output=True, text=True)
    return r.returncode, r.stderr


def constroi(tmp, nome, texto_angulo, gfx_obj, main_obj, inc):
    """The program with Angulo.cpp as given (None = the file itself). Returns the executable, or None and the compiler's words."""
    d = tmp / nome
    d.mkdir()
    fonte = ANGULO
    if texto_angulo is not None:
        fonte = d / 'Angulo.cpp'
        fonte.write_text(texto_angulo, encoding='utf-8')
    rc, err = compila(fonte, d / 'Angulo.o', inc, True)
    if rc:
        return None, err
    exe = d / 'temas'
    r = subprocess.run(['g++', str(gfx_obj), str(d / 'Angulo.o'), str(main_obj), '-o', str(exe)], capture_output=True, text=True)
    return (exe, '') if r.returncode == 0 else (None, r.stderr)


def corre(exe):
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    return r.returncode, r.stdout


def recorta_glue(glue):
    """The pieces of the glue that the host link compiles, verbatim (S10c, A'2b): {file name: text}, and what could not be found."""
    pecas, falta = {}, []
    for arq, padrao in (('fnv.inc', r'^uint32_t fnv\(.*?^\}\n'),
                        ('uatema.inc', r'^const ua::Tema &DisplayManager::uaTema\( UiMode modo \) \{.*?^\}\n')):
        m = re.search(padrao, glue, re.S | re.M)
        if m:
            pecas[arq] = m.group(0)
        else:
            falta.append(f'{arq}: the function was not found in DisplayManager_UiA.cpp')
    inicios = [m.start() for m in re.finditer(r'^  uint32_t h = fnv\(.*\n', glue, re.M)]
    if len(inicios) != 2:
        falta.append(f'hash_*.inc: {len(inicios)} runs of statements start with `uint32_t h = fnv(`, expected 2 (the Angulo frame and the update screen)')
    else:
        for arq, ini, fim in (('hash_pintar.inc', inicios[0], '  static ua::IdentidadeDoQuadro s_id = {};'),
                              ('hash_ota.inc', inicios[1], '  if ( _otaRedrawAll ) _uaTudo = true;')):
            f = glue.find(fim, ini)
            if f < 0:
                falta.append(f'{arq}: the line that ends the run, `{fim.strip()}`, was not found after it')
            else:
                pecas[arq] = glue[ini:f]
    return pecas, falta


def constroi_glue(tmp, nome, glue, angulo_o, gfx_obj, inc):
    """The host link of the glue's pieces (S10c, A'2b): cuts them out of `glue`, compiles test_temas_glue_main.cpp around them and links it with the kit's object.
    Returns (executable, '') or (None, why)."""
    pecas, falta = recorta_glue(glue)
    if falta:
        return None, '; '.join(falta)
    d = tmp / nome
    d.mkdir()
    for arq, texto in pecas.items():
        (d / arq).write_text(texto, encoding='utf-8')
    rc, err = compila(GLUE_MAIN, d / 'glue_main.o', inc + [f'-I{d}'], True)
    if rc:
        return None, err
    exe = d / 'glue'
    r = subprocess.run(['g++', str(gfx_obj), str(angulo_o), str(d / 'glue_main.o'), '-o', str(exe)], capture_output=True, text=True)
    return (exe, '') if r.returncode == 0 else (None, r.stderr)


def problemas_do_glue(glue):
    """The glue's side of the hash: [problem]."""
    p = []
    if 'extra[' in glue:
        p.append('an extra[ ] array is left in the glue: the frame identity must come from ua::identidadeDoQuadro( )')
    chamadas = re.findall(r'ua::identidadeDoQuadro\( \(uint8_t\)(\w+), (\w+), ', glue)
    if sorted(chamadas) != [('MODE_OTA_UPDATE', 'tema'), ('_uiMode', 'tema')]:
        p.append(f'ua::identidadeDoQuadro( ) must be called twice, for _uiMode and for MODE_OTA_UPDATE, with the theme uaTema( ) gave: {chamadas}')
    if len(re.findall(r'\.repintaTudo\( s_id \)', glue)) != 1 or len(re.findall(r'\bs_id = id;', glue)) != 1:
        p.append('the full-repaint trigger must be id.repintaTudo( s_id ) and s_id = id; must follow a painted frame')
    if 'ua::escolhaDoIndice(' not in glue or re.search(r'escolha == [12]\b', glue):
        p.append('uaTema( ) must decode themeIndex with ua::escolhaDoIndice( ), not compare it with 1 and 2')
    return p


GATILHO = '  if ( id.repintaTudo( s_id ) ) _uaTudo = true;\n'
PULO = '  if ( !_uaTudo && s_temQuadro && h == s_hash && s_modo == _uiMode ) return;\n'


def problemas_do_glue_exato(glue):
    """The statements of the glue that no run reaches, held to their exact text (S10c, A'2b): the mode each call of uaTema( ) takes, and the full-repaint trigger, its place and
    the record of the frame it compares with, the update screen's test of the hash, and the alignment the identity hashes."""
    p = []
    g = sem_comentarios(glue)
    chamadas = re.findall(r'const ua::Tema &tema = uaTema\( (\w+) \);', g)
    if chamadas != ['modo', '_uiMode', 'MODE_OTA_UPDATE']:
        p.append(f'the three frames must take their theme with uaTema( modo ) (uaPintar( )), uaTema( _uiMode ) (the paint identity) and uaTema( MODE_OTA_UPDATE ) (the update screen), in that order: {chamadas}')
    if len(re.findall(r'(?<![:\w])uaTema\( [^)]', g)) != 3:
        p.append('uaTema( ) is called from somewhere other than those three declarations')
    if len(re.findall(r'^  if \( id\.repintaTudo\( s_id \) \) _uaTudo = true;$', g, re.M)) != 1:
        p.append('the trigger must be the statement `if ( id.repintaTudo( s_id ) ) _uaTudo = true;`, once')
    i, j = g.find('id.repintaTudo( s_id )'), g.find('if ( !_uaTudo && s_temQuadro && h == s_hash && s_modo == _uiMode ) return;')
    if not 0 <= i < j:
        p.append('the full-repaint trigger must come before the line that skips an unchanged frame')
    if g.count('s_id = id;') != 1 or not re.search(r'if \( uaPintar\( q, _uiMode, h \) \) \{[^{}]*\bs_id = id;\s*\}', g):
        p.append('s_id = id; must be written once, as the last statement of the branch that follows a painted frame')
    if g.count('if ( _uaTudo || !s_temQuadro || h != s_hash || s_modo != MODE_OTA_UPDATE ) {') != 1:
        p.append('the update screen must repaint on a changed hash, `if ( _uaTudo || !s_temQuadro || h != s_hash || s_modo != MODE_OTA_UPDATE ) {`, written once: the hash is its only trigger for the accent')
    if g.count('const int8_t ox = _driver.tft->getOffsetX( ), oy = _driver.tft->getOffsetY( );') != 1:
        p.append('the alignment the identity hashes must be read as `const int8_t ox = _driver.tft->getOffsetX( ), oy = _driver.tft->getOffsetY( );`, once')
    return p


def acentos_n_fora(angulo, outros):
    """ACENTOS_N must appear only inside escolhaDoIndice and indiceDaEscolha, in static_assert statements (S10c, A'3: the catalogue and the Tema screen tie their sizes to the
    palette's) and in the generated header that defines it."""
    p = []
    dentro = 0
    for nome in ('Escolha escolhaDoIndice', 'int8_t indiceDaEscolha'):
        m = re.search(r'^' + re.escape(nome) + r'\(.*?\n^\}', angulo, re.S | re.M)
        if not m:
            p.append(f'{nome}( ) not found in Angulo.cpp')
            continue
        dentro += m.group(0).count('ACENTOS_N')
        angulo = angulo.replace(m.group(0), '')
    if dentro < 2:
        p.append(f'ACENTOS_N is read {dentro} time(s) inside the two encoding functions')
    for nome, texto in [('Angulo.cpp', angulo)] + outros:
        if 'ACENTOS_N' in re.sub(r'static_assert\s*\([^;]*;', '', texto):
            p.append(f'ACENTOS_N appears in {nome} outside the two encoding functions')
    return p


def sem_comentarios(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def pedaco(texto, ini, fim):
    i = texto.find(ini)
    j = texto.find(fim, i + len(ini)) if i >= 0 else -1
    return texto[i:j] if i >= 0 and j > i else None


def blocos_do_caso(texto, rotulo='case MODE_SETTINGS_THEMES: {'):
    """The braced bodies of `case MODE_SETTINGS_THEMES: {` in a file (the builder's and the touch's)."""
    out, i = [], 0
    while True:
        i = texto.find(rotulo, i)
        if i < 0:
            return out
        k, d = texto.index('{', i), 0
        for j in range(k, len(texto)):
            d += (texto[j] == '{') - (texto[j] == '}')
            if d == 0:
                break
        out.append(texto[i:j + 1])
        i = j


def problemas_de_literais(mh, mc, menus, ua):  # the 6 is not looked for in uaTema( ): its `char hora[6]` is the clock's text
    """S10c, A'3, brief restriction 3: the number of modes and the palette's size are written only in the encoding functions and in static_assert statements. The Tema
    screen, its touch and its builder, and uaTema( ) carry no bare 3 or 6 (comments, strings and static_asserts apart)."""
    p = []
    blocos = [('MenusConfig.h', pedaco(mh, 'enum EscolhaTema', '/* Idioma (MODE_SETTINGS_LANG)'), '36'), ('MenusConfig.cpp', pedaco(mc, '/* Tema:', '/* Idioma:'), '36'),
              ('DisplayManager_UiA.cpp, uaTema( )', pedaco(ua, 'const ua::Tema &DisplayManager::uaTema(', '/* "HH:MM"'), '3')]
    g = blocos_do_caso(menus)
    if len(g) != 2:
        p.append(f'DisplayManager_UiA_Menus.cpp has {len(g)} bodies of case MODE_SETTINGS_THEMES: {{, expected 2 (the builder and the touch)')
    blocos += [(f'DisplayManager_UiA_Menus.cpp, body {n + 1}', b, '36') for n, b in enumerate(g)]
    for nome, b, algarismos in blocos:
        if b is None:
            p.append(f'{nome}: the block was not found')
            continue
        b = re.sub(r'static_assert\s*\([^;]*;', '', sem_comentarios(b))
        b = re.sub(r'"(?:\\.|[^"\\])*"', '""', b)
        for m in re.finditer(r'(?<![\w.])[' + algarismos + r'](?![\w.])', b):
            p.append(f'{nome}: a bare {m.group(0)} in `{b[max(0, m.start() - 24):m.end() + 12].strip()}`')
    return p


def pedacos_do_catalogo(cat, mh):
    rows = re.findall(r'TEMA\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', cat)
    cores = re.findall(r'"([^"]*)"', pedaco(mh, 'TEMA_CORES[] =', '};') or '')
    modos = re.findall(r'"([^"]*)"', pedaco(mh, 'TEMA_MODOS[TM_N] =', '};') or '')
    return rows, cores, modos


def problemas_do_catalogo(cat, mh):
    """The 18 ids and names of Temas.cpp: each row at the index ua::escolhaDoIndice( ) gives it (mode t % 3, accent t / 3), the stock accent's three unchanged, an id at most
    15 B, a name at most 23 B of UTF-8, none repeated, and the static_assert that ties the count to the palette."""
    p = []
    rows, cores, modos = pedacos_do_catalogo(cat, mh)
    ids = ['claro', 'escuro', 'auto']
    if len(modos) != 3 or len(cores) < 2:
        return [f'the screen header gives {len(modos)} modes and {len(cores)} accents']
    if len(rows) != 3 * len(cores):
        p.append(f'{len(rows)} rows, expected {3 * len(cores)} (three modes by {len(cores)} accents)')
    for t, (i, n) in enumerate(rows):
        m, k = t % 3, t // 3
        if k >= len(cores):
            break
        chave = unicodedata.normalize('NFKD', cores[k]).encode('ascii', 'ignore').decode().lower()
        want = (ids[m], modos[m]) if k == 0 else (f'{ids[m]}-{chave}', f'{modos[m]}, {cores[k]}')
        if (i, n) != want:
            p.append(f'row {t} is ({i!r}, {n!r}), expected {want}')
        if len(i.encode('utf-8')) > 15 or not re.fullmatch(r'[a-z]+(-[a-z]+)?', i):
            p.append(f'row {t}: the id {i!r} is {len(i.encode("utf-8"))} B or not lower-case ASCII (at most 15 B)')
        if len(n.encode('utf-8')) > 23:
            p.append(f'row {t}: the name {n!r} is {len(n.encode("utf-8"))} B of UTF-8 (at most 23)')
    if len(set(i for i, _ in rows)) != len(rows) or len(set(n for _, n in rows)) != len(rows):
        p.append('an id or a name is repeated')
    if not re.search(r'static_assert\(\s*NUM_TEMAS\s*==\s*3\s*\*\s*entrega::ACENTOS_N', cat):
        p.append('Temas.cpp lacks static_assert( NUM_TEMAS == 3 * entrega::ACENTOS_N )')
    return p


def main():
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found (run pio run -e pico_wiretft_angulo once)', file=sys.stderr)
        return 2
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{HERE / "host" / "shim"}', f'-I{gfx}', f'-I{KIT}']
    with tempfile.TemporaryDirectory(prefix='temas_host_') as t:
        tmp = pathlib.Path(t)
        gfx_obj, main_obj = tmp / 'Adafruit_GFX.o', tmp / 'test_temas_main.o'
        for src, obj, estrito in ((gfx / 'Adafruit_GFX.cpp', gfx_obj, False), (HERE / 'host' / 'test_temas_main.cpp', main_obj, True)):
            rc, err = compila(src, obj, inc, estrito)
            if rc:
                print(err, file=sys.stderr)
                return 2
        texto = ANGULO.read_text(encoding='utf-8')
        for nome, velho, novo in MUTANTES:
            if texto.count(velho) != 1:
                print(f'the mutant "{nome}" does not find its line in Angulo.cpp exactly once', file=sys.stderr)
                return 2
        # the real program and one program per mutant, built side by side
        jobs = [('real', None)] + [(f'm{i}', texto.replace(v, n)) for i, (_, v, n) in enumerate(MUTANTES)]
        with concurrent.futures.ThreadPoolExecutor(4) as ex:
            feitos = list(ex.map(lambda j: constroi(tmp, j[0], j[1], gfx_obj, main_obj, inc), jobs))
        if feitos[0][0] is None:
            print(feitos[0][1], file=sys.stderr)
            return 2
        rc, saida = corre(feitos[0][0])
        falhas = [l for l in saida.splitlines() if l.startswith('FAIL')]
        resumo = saida.strip().splitlines()[-1] if saida.strip() else 'no output'
        caso(f'host program: mode, accent, tema( ), desab( ) and the frame identity against the generated header ({resumo})', not falhas and rc == 0 and resumo.endswith(' 0 failed'))
        for l in falhas[:6]:
            print('       ' + l)
        for l in saida.splitlines():
            if l.startswith('MEDIDA'):
                print('       ' + l)
        for (nome, _, _), (exe, err) in zip(MUTANTES, feitos[1:]):
            if exe is None:
                caso(f'control: {nome}', [f'the mutant does not build: {err.strip()[:200]}'])
                continue
            rc, saida = corre(exe)
            caso(f'control: {nome} must fail the program', rc == 1 and 'FAIL' in saida)

        # the glue's own statements, compiled against a stand-in and run (S10c, A'2b): the real text first, then one program per mutant of it
        glue0 = GLUE.read_text(encoding='utf-8')
        angulo_o = tmp / 'real' / 'Angulo.o'
        aplicaveis = [(i, m) for i, m in enumerate(GLUE_MUTANTES) if glue0.count(m[1]) == m[3]]
        jobs_g = [('glue_real', glue0)] + [(f'g{i}', glue0.replace(m[1], m[2])) for i, m in aplicaveis]
        with concurrent.futures.ThreadPoolExecutor(4) as ex:
            ligados = list(ex.map(lambda j: constroi_glue(tmp, j[0], j[1], angulo_o, gfx_obj, inc), jobs_g))
        exe, err = ligados[0]
        if exe is None:
            caso("glue link: the glue's own uaTema( ) and hashes build against the stand-in", [f'does not build: {err.strip()[:400]}'])
        else:
            rc, saida = corre(exe)
            falhas = [l for l in saida.splitlines() if l.startswith('FAIL')]
            resumo = saida.strip().splitlines()[-1] if saida.strip() else 'no output'
            caso(f"glue link: the glue's own uaTema( ) and the two hashes, run against the generated header ({resumo})", not falhas and rc == 0 and resumo.endswith(' 0 failed'))
            for l in falhas[:6]:
                print('       ' + l)
            for l in saida.splitlines():
                if l.startswith('MEDIDA'):
                    print('       ' + l)
        feitos_g = dict(zip([i for i, _ in aplicaveis], ligados[1:]))
        for i, (nome, velho, _, n) in enumerate(GLUE_MUTANTES):
            if i not in feitos_g:
                caso(f'control: {nome} must fail the glue link', [f'its text is not in DisplayManager_UiA.cpp {n} time(s)'])
                continue
            exe, err = feitos_g[i]
            if exe is None:
                caso(f'control: {nome} must fail the glue link', [f'the mutant does not build: {err.strip()[:200]}'])
                continue
            rc, saida = corre(exe)
            caso(f'control: {nome} must fail the glue link', rc == 1 and 'FAIL' in saida)

    glue = GLUE.read_text(encoding='utf-8')
    caso('glue: the frame identity comes from ua::identidadeDoQuadro( ) in both paints, uaTema( ) decodes with ua::escolhaDoIndice( )', problemas_do_glue(glue))
    caso("glue: the three calls of uaTema( ), the full-repaint trigger and the record of the frame are written exactly", problemas_do_glue_exato(glue))
    caso("control: the painted frame taking the theme of _uiMode instead of its own mode is reported",
         bool(problemas_do_glue_exato(glue.replace('const ua::Tema &tema = uaTema( modo );', 'const ua::Tema &tema = uaTema( _uiMode );'))))
    caso("control: the update screen taking the theme of _uiMode is reported",
         bool(problemas_do_glue_exato(glue.replace('const ua::Tema &tema = uaTema( MODE_OTA_UPDATE );', 'const ua::Tema &tema = uaTema( _uiMode );'))))
    caso("control: a trigger that clears the flag is reported",
         bool(problemas_do_glue_exato(glue.replace('.repintaTudo( s_id ) ) _uaTudo = true;', '.repintaTudo( s_id ) ) _uaTudo = false;'))))
    caso("control: a trigger after the line that skips an unchanged frame is reported", bool(problemas_do_glue_exato(glue.replace(GATILHO + PULO, PULO + GATILHO))))
    caso("control: the record of the frame hoisted before the paint is reported",
         bool(problemas_do_glue_exato(glue.replace('  if ( uaPintar( q, _uiMode, h ) ) {', '  s_id = id;\n  if ( uaPintar( q, _uiMode, h ) ) {'))))
    caso("control: an update screen that no longer compares the hash is reported",
         bool(problemas_do_glue_exato(glue.replace('|| h != s_hash || s_modo != MODE_OTA_UPDATE', '|| s_modo != MODE_OTA_UPDATE'))))
    caso("control: an alignment read from the wrong axis is reported",
         bool(problemas_do_glue_exato(glue.replace('oy = _driver.tft->getOffsetY( );', 'oy = _driver.tft->getOffsetX( );'))))
    caso('control: a glue that hashes a fixed theme instead of uaTema( )\'s is reported',
         problemas_do_glue(glue.replace('ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, tema,', 'ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, ua::temaClaro( ),')) != [] and
         bool(problemas_do_glue(glue.replace('ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, tema,', 'ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, ua::temaClaro( ),'))))
    caso('control: a glue without the full-repaint trigger is reported', bool(problemas_do_glue(glue.replace('.repintaTudo( s_id )', '.pronto( )'))))
    caso('control: a glue with its own extra[ ] array is reported', bool(problemas_do_glue(glue + '\nconst uint8_t extra[2] = { 0, 0 };\n')))
    outros = []
    for f in sorted(KIT.rglob('*')):
        if f.suffix in ('.cpp', '.h') and f.name not in ('Angulo.cpp', 'angulo_tokens.h'):
            outros.append((str(f.relative_to(ROOT)), f.read_text(encoding='utf-8', errors='replace')))
    caso('sources: ACENTOS_N appears only inside the two encoding functions', acentos_n_fora(texto, outros))
    caso('control: an ACENTOS_N in a screen file is reported', bool(acentos_n_fora(texto, outros + [('telas/X.cpp', 'int n = ACENTOS_N;')])))
    caso('control: an ACENTOS_N in tema( ) is reported', bool(acentos_n_fora(texto.replace('acento < kAcentos ? acento : 0', 'acento < ACENTOS_N ? acento : 0'), outros)))

    cat, mh, mc = TEMAS_CPP.read_text(encoding='utf-8'), MENUS_H.read_text(encoding='utf-8'), MENUS_CPP.read_text(encoding='utf-8')
    menus = MENUS_GLUE.read_text(encoding='utf-8')
    caso('catalogue: 18 ids and names, each at the index the decode gives it, ids within 15 B and names within 23 B', problemas_do_catalogo(cat, mh))
    r0 = re.findall(r'TEMA\(\s*"[^"]*"\s*,\s*"[^"]*"\s*\)', cat)
    caso('control: an id of 16 B is reported', bool(problemas_do_catalogo(cat.replace('"escuro-petroleo"', '"escuro-petroleos"'), mh)))
    caso('control: a name of 24 B is reported', bool(problemas_do_catalogo(cat.replace('"Automático, Petróleo"', '"Automático, Petróleos2"'), mh)))
    caso('control: two rows swapped are reported', bool(problemas_do_catalogo(cat.replace(r0[4], '@@').replace(r0[5], r0[4]).replace('@@', r0[5]), mh)))
    caso('control: a catalogue of 17 rows is reported', bool(problemas_do_catalogo(cat.replace(r0[17] + ',', '', 1), mh)))
    caso('control: a catalogue without its static_assert is reported', bool(problemas_do_catalogo(cat.replace('static_assert( NUM_TEMAS == 3 *', 'static_assert( NUM_TEMAS >= 3 *'), mh)))
    caso('sources: the Tema screen, its touch and uaTema( ) carry no bare 3 or 6 outside static_asserts', problemas_de_literais(mh, mc, menus, glue))
    caso('control: an `i < 3` in the touch is reported', bool(problemas_de_literais(mh, mc, menus.replace('else if ( a->tipo == TT_MODO ) {', 'else if ( a->tipo == TT_MODO && i < 3 ) {'), glue)))
    caso('control: a 6 in the screen is reported', bool(problemas_de_literais(mh, mc.replace('for ( int k = 0; k < TEMA_CORES_N; k++ ) desenharCor', 'for ( int k = 0; k < 6; k++ ) desenharCor'), menus, glue)))
    caso('control: a static_assert with the 3 and the 6 is not reported', not problemas_de_literais(mh, mc.replace('/* Idioma:', 'static_assert( 3 * 6 == 18, "x" );\n/* Idioma:', 1), menus, glue))

    falhas = [n for n, ok in CASOS if not ok]
    print(f'{len(CASOS)} cases, {len(falhas)} failed')
    return 1 if falhas else 0


if __name__ == '__main__':
    sys.exit(main())
