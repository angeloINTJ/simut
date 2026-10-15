#!/usr/bin/env python3
"""test_alvos.py — proves ua::alvoEm( ), the touch hit test of the Angulo screens, on the host.

Builds tools/tft_ui/host/test_alvos_main.cpp with src/ui/angulo/telas/Telas.cpp (the real one), the
kit (Angulo.cpp) and Adafruit GFX 1.12.6 (the tree's .pio/libdeps, as tools/tft_ui/host_ui.py finds
it) and runs its cases: a touch inside a box (the last listed wins where boxes overlap), in a dead band
between two boxes (the nearer wins, whichever is listed first), just outside an edge (12 px answers,
13 px does not), at a corner (the slop is a distance, not a margin per axis), a tie (the first listed),
an empty list, a target left out of the list, and extreme coordinates.

Then the instrument is bitten: a copy of Telas.cpp with one change at a time to the hit test (the slop
compared with < instead of <=, per axis instead of Euclidean, the nearest replaced by the first within
the slop, the slop ignored, and three more: a tie to the last, the overlap to the first, the far edge
one pixel short) is built with the same main, and every one of them must fail at least one case. A
mutant that passes, or a line to change that is not found exactly once, fails this script.

Usage: python3 tools/tft_ui/test_alvos.py   exit 0 when the cases pass and every mutant fails,
1 when not, 2 when it cannot build.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ANGULO = ROOT / 'src' / 'ui' / 'angulo'
TELAS = ANGULO / 'telas'
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)

# The lines of the hit test that a mutant changes, as they are in Telas.cpp; each must be found once.
L_LOOP = '  for ( int i = n - 1; i >= 0; i-- ) {'
L_FAR = 'p - ( a + w - 1 )'
L_AXIS = '    if ( dx > FOLGA_TOQUE || dy > FOLGA_TOQUE ) continue;'
L_DIST = '    const int32_t d2 = dx * dx + dy * dy;'
L_EUCL = '    if ( d2 > FOLGA_TOQUE * FOLGA_TOQUE ) continue;'
L_NEAR = '    if ( !perto || d2 < melhor ) { perto = &a; melhor = d2; }'

MUTANTES = [
    ('the slop compared with < instead of <=',
     [(L_EUCL, '    if ( d2 >= FOLGA_TOQUE * FOLGA_TOQUE ) continue;')]),
    ('per axis (the larger offset) instead of Euclidean',
     [(L_DIST, '    const int32_t d2 = dx > dy ? dx : dy;'), (L_EUCL, '    if ( d2 > FOLGA_TOQUE ) continue;')]),
    ('each axis within the slop is enough (the distance is not checked)',
     [(L_EUCL, '    if ( false ) continue;')]),
    ('the nearest replaced by the first within the slop',
     [(L_NEAR, '    if ( !perto ) { perto = &a; melhor = d2; (void)melhor; }')]),
    ('the slop ignored',
     [(L_AXIS, '    if ( false ) continue;'), (L_EUCL, '    if ( false ) continue;')]),
    ('a tie goes to the last listed',
     [(L_NEAR, '    if ( !perto || d2 <= melhor ) { perto = &a; melhor = d2; }')]),
    ('inside two boxes, the first listed answers, not the last',
     [(L_LOOP, '  for ( int i = 0; i < n; i++ ) {')]),
    ('the far edge measured one pixel short',
     [(L_FAR, 'p - ( a + w )')]),
]


def compilar(tmp, fonte, flags, alvo):
    obj = tmp / (alvo + '.o')
    r = subprocess.run(['g++', *flags, '-c', str(fonte), '-o', str(obj)], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr, file=sys.stderr)
        return None
    return str(obj)


def base(tmp):
    """The objects every variant shares: Adafruit GFX and the kit. Returns (objects, flags) or None."""
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found (run pio run -e pico_wiretft_angulo once)', file=sys.stderr)
        return None
    flags = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{HERE / "host" / "shim"}', f'-I{gfx}',
             f'-I{ANGULO}', f'-I{TELAS}']
    strict = ['-Wall', '-Wextra', '-Werror']
    objs = [compilar(tmp, gfx / 'Adafruit_GFX.cpp', [*flags, '-w'], 'gfx'),
            compilar(tmp, ANGULO / 'Angulo.cpp', [*flags, *strict], 'angulo'),
            compilar(tmp, HERE / 'host' / 'test_alvos_main.cpp', [*flags, *strict], 'main')]
    return (objs, flags + strict) if all(objs) else None


def variante(tmp, nome, telas_cpp, comum):
    """Builds Telas.cpp (the file given, which may be a mutated copy) with the shared objects; returns the run."""
    objs, flags = comum
    obj = compilar(tmp, telas_cpp, flags, nome)
    if not obj:
        return None
    exe = tmp / nome
    r = subprocess.run(['g++', *objs, obj, '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr, file=sys.stderr)
        return None
    return subprocess.run([str(exe)], capture_output=True, text=True)


def main():
    fonte = (TELAS / 'Telas.cpp').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='alvos_') as d:
        tmp = pathlib.Path(d)
        comum = base(tmp)
        if not comum:
            return 2
        real = variante(tmp, 'real', TELAS / 'Telas.cpp', comum)
        if not real:
            return 2
        print(real.stdout, end='')
        ultima = real.stdout.strip().splitlines()[-1] if real.stdout.strip() else ''
        m = re.fullmatch(r'(OK|FAIL): (\d+) case\(s\), (\d+) failure\(s\)', ultima)
        # The instrument first: the summary must agree with the lines above it, or the run proves nothing.
        oks = sum(l.startswith('ok   ') for l in real.stdout.splitlines())
        if not m or int(m.group(2)) == 0 or oks != int(m.group(2)) - int(m.group(3)):
            print('FAIL: the main did not print a summary that agrees with its cases: the harness is broken')
            return 2
        if real.returncode:
            return 1
        sobrou = []
        for i, (nome, trocas) in enumerate(MUTANTES):
            texto = fonte
            for antigo, novo in trocas:
                if texto.count(antigo) != 1:
                    print(f'cannot build mutant "{nome}": "{antigo.strip()}" is in Telas.cpp {texto.count(antigo)} times, '
                          'not once (the hit test changed: update MUTANTES)', file=sys.stderr)
                    return 2
                texto = texto.replace(antigo, novo)
            copia = tmp / f'fonte_mut{i}'
            copia.mkdir()
            (copia / 'Telas.cpp').write_text(texto, encoding='utf-8')
            r = variante(tmp, f'mut{i}', copia / 'Telas.cpp', comum)
            if not r:
                print(f'cannot build mutant "{nome}"', file=sys.stderr)
                return 2
            falhou = sorted({l.split(':')[0][5:] for l in r.stdout.splitlines() if l.startswith('FAIL ') and '(' in l})
            mordeu = r.returncode == 1 and bool(falhou)
            print(f'mutant {i + 1}: {nome}: exit {r.returncode}, '
                  + (f'fails {len(falhou)} case(s): {"; ".join(falhou)}' if mordeu else 'PASSES'))
            if not mordeu:
                sobrou.append(nome)
    if sobrou:
        print(f'FAIL: {len(sobrou)} mutant(s) passed every case: the test does not bite')
        return 1
    print(f'all {len(MUTANTES)} mutants fail at least one case')
    return 0


if __name__ == '__main__':
    sys.exit(main())
