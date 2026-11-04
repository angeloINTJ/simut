#!/usr/bin/env python3
"""test_zona_minmax.py -- the original dashboard's min/max graph button: the rectangle it is painted on and the zones that open its graph, on the host (2026-11-04).

The defect (found by reading the code on 2026-09-30, commit 8d7b31d4 of fix/small-bugs; the touch code has since moved to src/ui/original/UiTouchChain.h): in min/max, each
dashboard card paints a graph button at CARD_X + minMaxBtnX( CARD_W ) = 4 + 241 = 245, MINMAX_BTN_W = 58 columns wide (x 245..302), by the same code for both cards
(drawSlotPanel( ) -> sensorRenderMinMax( ) -> drawMinMaxGraphBtn( ), at CARD_W and blitted at CARD_X; only cardY differs). The upper card's touch zone had been brought to those
numbers; the lower card's still read `x > 266`, so a tap on the left third of its button (x 245..266) did not open the graph: it fell through to the short-tap path and turned
min/max OFF. This holds it:

  1. The painted rectangle, from the numbers the drivers draw with, and the zones, from the real chain: SensorDrawing.h's MINMAX_BTN_* and minMaxBtnX( ), DisplayManager.h's CARD_X
     and CARD_W, and the dashboard branch of the original's touch chain (src/ui/original/UiTouchChain.h) are cut out of the sources verbatim, without comments, and compiled with a
     stand-in whose core actions only record that they ran and with which `onGraphBtn` (host/test_zona_minmax_main.cpp). For each card, every column 0..319 inside the card's
     rows: the card's action runs exactly once, and `onGraphBtn` is true exactly on the painted columns. The card's first and last row reach its action; the rows beside them do not.
  2. T1, the text that makes the lower card's button the upper card's: drawSlotPanel( ) has one sensorRenderMinMax( ) call, which takes CARD_W and is blitted at CARD_X, and
     cardY is the only thing that tells the cards apart; every drawMinMaxGraphBtn( ) call in src/ draws at minMaxBtnX( cardW ) with the MINMAX_BTN_* sizes. If a card ever paints its
     button elsewhere, this fails, and the oracle above is no longer the lower card's.
  3. The mutants (the cut chain edited one statement at a time), each of which must fail the run: M1 the lower zone back to `x > 266` (the defect), M2 the upper zone to `x > 266`,
     M3 the lower zone's far edge inclusive, M4 its near edge exclusive, M5 the end of both zones one column short, M6 the lower zone's test dropped (every column opens the graph).
     A mutant that does not compile, or an anchor not found exactly once, fails this script: a mutant must be killed by a check and not by the compiler.

While the defect stands the base run is red and the mutants are not run (they are judged against a base that is green).

Usage: python3 tools/tft_ui/test_zona_minmax.py   exit 0 when all hold, 1 when not, 2 when it cannot build (no g++).
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / 'src'
MAIN = HERE / 'host' / 'test_zona_minmax_main.cpp'

F_DESENHO = SRC / 'sensors' / 'SensorDrawing.h'
F_CARTAO = SRC / 'DisplayManager.h'
F_CADEIA = SRC / 'ui' / 'original' / 'UiTouchChain.h'
F_PAINEL = SRC / 'ui' / 'original' / 'DisplayManager_Dashboard.cpp'

FALHAS = []
CONTA = [0]


def confere(cond, msg):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    return bool(cond)


# ---- cutting the sources (comments removed, newlines kept; strings and chars left alone) ----
def apaga(t):
    saida, i, n = [], 0, len(t)
    while i < n:
        c, d = t[i], (t[i + 1] if i + 1 < n else '')
        if c == '/' and d == '*':
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            saida.append(re.sub(r'[^\n]', ' ', t[i:j]))
            i = j
        elif c == '/' and d == '/':
            j = t.find('\n', i)
            j = n if j < 0 else j
            saida.append(' ' * (j - i))
            i = j
        elif c == '"' or (c == "'" and not (i and t[i - 1].isalnum() and d.isalnum())):
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            saida.append(t[i:j + 1])
            i = j + 1
        else:
            saida.append(c)
            i += 1
    return ''.join(saida)


def casa_chaves(t, abre):
    prof, i, n = 0, abre, len(t)
    while i < n:
        c = t[i]
        if c == '"' or (c == "'" and not (i and t[i - 1].isalnum() and i + 1 < n and t[i + 1].isalnum())):
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            i = j + 1
            continue
        if c == '{':
            prof += 1
        elif c == '}':
            prof -= 1
            if prof == 0:
                return i
        i += 1
    return -1


def le(caminho):
    return apaga(caminho.read_text(encoding='utf-8'))


def uma(padrao, texto, onde, flags=re.M):
    m = list(re.finditer(padrao, texto, flags))
    if len(m) != 1:
        print(f'cannot cut: {padrao!r} found {len(m)} times in {onde}')
        sys.exit(1)
    return m[0]


def pecas():
    """The three pieces, as text: the painted button, the card rectangle, the chain's dashboard branch."""
    d = le(F_DESENHO)
    consts = [uma(r'^constexpr int16_t MINMAX_BTN_%s\s*=[^;]+;' % n, d, F_DESENHO.name).group(0) for n in ('ANCHOR', 'W', 'Y', 'H')]
    f = uma(r'^constexpr int16_t minMaxBtnX\(int16_t cardW\) \{', d, F_DESENHO.name)
    fim = casa_chaves(d, f.end() - 1)
    desenho = '\n'.join(consts) + '\n' + d[f.start():fim + 1] + '\n'
    c = le(F_CARTAO)
    cartao = '\n'.join(uma(r'^\s*static constexpr int16_t %s\s*=[^;]+;' % n, c, F_CARTAO.name).group(0).strip() for n in ('CARD_X', 'CARD_W')) + '\n'
    t = le(F_CADEIA)
    b = uma(r'if \s*\(_uiMode == MODE_DASHBOARD\) \{', t, F_CADEIA.name)
    fim = casa_chaves(t, b.end() - 1)
    toque = t[b.start():fim + 1] + '\n'
    if re.search(r'^\s*#', toque, re.M):
        print('cannot cut: the dashboard branch holds a preprocessor line; the host would take one side of it without saying so')
        sys.exit(1)
    return desenho, cartao, toque


def roda(desenho, cartao, toque, pasta, nome):
    """Writes the pieces, builds, runs. Returns (built, exit code, output)."""
    d = pathlib.Path(pasta) / nome
    d.mkdir(parents=True)
    (d / 'pecas_desenho.inc').write_text(desenho, encoding='utf-8')
    (d / 'pecas_cartao.inc').write_text(cartao, encoding='utf-8')
    (d / 'pecas_toque.inc').write_text(toque, encoding='utf-8')
    exe = d / 'zona'
    b = subprocess.run(['g++', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-I', str(d), str(MAIN), '-o', str(exe)], capture_output=True, text=True)
    if b.returncode != 0:
        return False, b.returncode, b.stdout + b.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    return True, r.returncode, r.stdout + r.stderr


# ---- the mutants: (name, [(anchor, replacement)]) on the cut chain; each anchor must be found once ----
ZONA = 'x >= MM_BTN_X0 && x < MM_BTN_X1'
CIMA = f'actTopCardPress( {ZONA} );'
BAIXO = f'actBottomCardTap( {ZONA} );'
FIM = 'constexpr int16_t MM_BTN_X1 = MM_BTN_X0 + MINMAX_BTN_W;'
MUTANTES = [
    ('M1 the lower zone back to `x > 266` (the defect)', [(BAIXO, 'actBottomCardTap( x > 266 );')]),
    ('M2 the upper zone to `x > 266`', [(CIMA, 'actTopCardPress( x > 266 );')]),
    ('M3 the lower zone\'s far edge inclusive', [(BAIXO, 'actBottomCardTap( x >= MM_BTN_X0 && x <= MM_BTN_X1 );')]),
    ('M4 the lower zone\'s near edge exclusive', [(BAIXO, 'actBottomCardTap( x > MM_BTN_X0 && x < MM_BTN_X1 );')]),
    ('M5 the end of both zones one column short', [(FIM, 'constexpr int16_t MM_BTN_X1 = MM_BTN_X0 + MINMAX_BTN_W - 1;')]),
    ('M6 the lower zone\'s test dropped', [(BAIXO, 'actBottomCardTap( true );')]),
]


def desenho_compartilhado():
    """T1: both cards paint the button by the same code, at the same x."""
    p = le(F_PAINEL)
    uma(r'int16_t cardY = \(&panel == &_topPanel\) \? 35 : 115;', p, F_PAINEL.name)
    m = uma(r'sensorRenderMinMax\(_driver\.canvas, type,[^;]*?isValid, CARD_W, isRedPhase,', p, F_PAINEL.name, re.S)
    resto = p[m.end():]
    confere(re.search(r'blitCanvas\(_driver\.canvas, CARD_X, cardY \+ 20, CARD_W, 43\);', resto) is not None,
            'T1 the min/max strip of drawSlotPanel( ) is not blitted at CARD_X with CARD_W')
    chamadas = []
    for caminho in sorted(SRC.rglob('*')):
        if caminho.suffix not in ('.h', '.cpp') or not caminho.is_file():
            continue
        t = le(caminho)
        for mm in re.finditer(r'drawMinMaxGraphBtn\(([^;]*?)\);', t, re.S):
            chamadas.append((caminho.relative_to(SRC).as_posix(), re.sub(r'\s+', ' ', mm.group(1)).strip()))
    # one definition (its parameter list ends in `uint16_t fg`) and the callers
    chamadores = [(a, b) for a, b in chamadas if not b.startswith('GFXcanvas16*')]
    confere(len(chamadores) == 3, f'T1 drawMinMaxGraphBtn( ) is called {len(chamadores)} times in src/, not 3 (the DS18B20/BMP shared strip, DHT22, BME280)')
    for a, b in chamadores:
        confere(b.startswith('cv, BTN_X, MINMAX_BTN_Y, MINMAX_BTN_W, MINMAX_BTN_H,'), f'T1 {a} paints the graph button elsewhere than at BTN_X with the MINMAX_BTN_* sizes: {b}')
    for nome in ('sensors/SensorDrawing.h', 'sensors/DHT22Driver.h', 'sensors/BME280Driver.h'):
        t = le(SRC / nome)
        confere(len(re.findall(r'const int BTN_X = minMaxBtnX\(cardW\);', t)) == 1, f'T1 {nome} does not take BTN_X from minMaxBtnX( cardW ) exactly once')


def main():
    if shutil.which('g++') is None:
        print('no g++: cannot build the host check')
        return 2
    desenho_compartilhado()
    desenho, cartao, toque = pecas()
    with tempfile.TemporaryDirectory() as tmp:
        ok, rc, out = roda(desenho, cartao, toque, tmp, 'base')
        print(out.rstrip())
        if not ok:
            print('the base did not build')
            return 2
        confere(rc == 0, 'the zones do not match the painted button (see the lines above)')
        if rc == 0:
            for nome, trocas in MUTANTES:
                t = toque
                for ancora, novo in trocas:
                    if t.count(ancora) != 1:
                        confere(False, f'{nome}: the anchor {ancora!r} is found {t.count(ancora)} times, not once')
                        t = None
                        break
                    t = t.replace(ancora, novo)
                if t is None:
                    continue
                construiu, rcm, saida = roda(desenho, cartao, t, tmp, 'm' + nome.split()[0])
                if not construiu:
                    confere(False, f'{nome}: the mutant does not compile:\n{saida}')
                else:
                    confere(rcm != 0, f'{nome}: the mutant passes (the check does not bite)')
                    if rcm != 0:
                        print(f'killed  {nome}')
    print(f'{CONTA[0]} checks, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
