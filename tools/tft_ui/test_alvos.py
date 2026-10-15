#!/usr/bin/env python3
"""test_alvos.py — proves ua::alvoEm( ) and ua::alvoDoToque( ), the touch hit test of the Angulo screens, and the
control recorder (UA_CONTROLE( )) that host_telas.py trusts, on the host.

Builds tools/tft_ui/host/test_alvos_main.cpp with src/ui/angulo/telas/Telas.cpp (the real one), the
kit (Angulo.cpp) and Adafruit GFX 1.12.6 (the tree's .pio/libdeps, as tools/tft_ui/host_ui.py finds
it) and runs its cases: a touch inside a box (the last listed wins where boxes overlap), in a dead band
between two boxes (the nearer wins, whichever is listed first), just outside an edge (12 px answers,
13 px does not), at a corner (the slop is a distance, not a margin per axis), a tie (the first listed),
an empty list, a target left out of the list, extreme coordinates, a box with no pixel, a control drawn
disabled (an inert target, type 0: a touch on it, or in the gap beside it, is its own, and the enabled
neighbour answers it only when the inert one is left out of the list), what the glue is handed
(alvoDoToque( ): an inert answer is nobody) and the chart's band.

Then the instrument is bitten: a copy of Telas.cpp with one change at a time is built with the same
main, and every one of them must fail at least one case. The ten changes: (1) the slop compared with
< instead of <=; (2) per axis (the larger offset) instead of Euclidean; (3) the distance not checked,
each axis within the slop being enough; (4) the nearest replaced by the first within the slop; (5) the
slop ignored; (6) a tie to the last listed; (7) the overlap to the first listed; (8) the far edge one
pixel short; (9) a box with no pixel allowed to be the nearest; (10) alvoDoToque( ) handing the glue
the inert target. A mutant that passes, or a line to change that is not found exactly once, fails
this script.

The recorder is proven the same way. host/test_controles_main.cpp draws each control function of the kit
(botao, chip, interruptor, segmentado, tecla), botaoIcone, the pager with its dimmed arrows and the chart's
footer, each enabled and disabled, and asserts the records one by one; host_telas.py holds every sample to what
the recorder says it drew, so a recorder that skipped a state would pass them all. A copy of Angulo.cpp with
one change to the recorder at a time (it skips the controls drawn disabled, or enabled; a control drawn over
another of the same box is added, not replaced; one kit function records itself enabled whatever it was told,
or not at all; a note outlives the control it was for) is built with the same main, and every one must fail at
least one case.

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
L_VAZIA = '    if ( a.w <= 0 || a.h <= 0 ) continue;'
L_INERTE = '  return a && a->tipo == ALVO_INERTE ? nullptr : a;'

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
    ('a box with no pixel can be the nearest',
     [(L_VAZIA, '    if ( false ) continue;')]),
    ('the glue is handed the inert target',
     [(L_INERTE, '  return a;')]),
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


# The recorder's mutants: Angulo.cpp, one change at a time, each found exactly once.
L_C_IGUAL = '    if ( s_controles[i].x == x && s_controles[i].y == y && s_controles[i].w == w && s_controles[i].h == h ) c = &s_controles[i];'
L_C_NOVO = '  ControleDesenhado *c = nullptr;'
L_C_NOTA = '  copiarNome( c->excluido, (int)sizeof c->excluido, s_naoControle );\n  s_naoControle[0] = 0;'
L_C_SOB = '  copiarNome( c->sob, (int)sizeof c->sob, s_sobAlvo );\n  s_sobAlvo[0] = 0;'
L_C_LIMPA = 'void hostLimparControles( ) { s_nControles = 0; s_naoControle[0] = 0; s_sobAlvo[0] = 0; }'
MUTANTES_CONTROLES = [
    ('UA_CONTROLE skips the controls drawn disabled', [(L_C_NOVO, '  if ( !habilitado ) return;\n' + L_C_NOVO)]),
    ('UA_CONTROLE skips the controls drawn enabled', [(L_C_NOVO, '  if ( habilitado ) return;\n' + L_C_NOVO)]),
    ('a control drawn over another of the same box is added, not replaced', [(L_C_IGUAL, '    if ( false ) c = &s_controles[i];')]),
    ('botao records itself enabled whatever it was told',
     [('  UA_CONTROLE( x, y, w, h, !des, "botao" );', '  UA_CONTROLE( x, y, w, h, true, "botao" );')]),
    ('tecla records itself enabled whatever its state', [('o.estado != TeclaEstado::DESAB, "tecla"', 'true, "tecla"')]),
    ('segmentado records its segments enabled whatever it was told',
     [('UA_CONTROLE( sx, y, sw, h, !des, "segmento" );', 'UA_CONTROLE( sx, y, sw, h, true, "segmento" );')]),
    ('interruptor records itself enabled whatever it was told',
     [('UA_CONTROLE( x, y, 40, 24, !des, "interruptor" );', 'UA_CONTROLE( x, y, 40, 24, true, "interruptor" );')]),
    ('chip does not record itself', [('  UA_CONTROLE( x, y, w, h, true, "chip" );\n', '')]),
    ('the note "not a control" outlives its control', [(L_C_NOTA, L_C_NOTA.replace('\n  s_naoControle[0] = 0;', ''))]),
    ('the note "dimmed on purpose" outlives its control', [(L_C_SOB, L_C_SOB.replace('\n  s_sobAlvo[0] = 0;', ''))]),
    ('clearing the records keeps the note "not a control" that was waiting',
     [(L_C_LIMPA, L_C_LIMPA.replace(' s_naoControle[0] = 0;', ''))]),
]
TELAS_DO_TESTE = ['GraficosComum.cpp', 'GraficosGrafico.cpp', 'AjustesComum.cpp']


def controles_base(tmp, comum):
    """What every kit variant of the recorder's test shares: its main, Telas.cpp and the screens' files it draws."""
    objs, flags = comum
    partes = [compilar(tmp, HERE / 'host' / 'test_controles_main.cpp', flags, 'ctl_main'),
              compilar(tmp, TELAS / 'Telas.cpp', flags, 'ctl_telas')]
    partes += [compilar(tmp, TELAS / f, flags, 'ctl_' + f[:-4]) for f in TELAS_DO_TESTE]
    return partes if all(partes) else None


def controles_variante(tmp, nome, angulo_cpp, comum, partes):
    """Builds the recorder's test with the kit given (the real one, or a mutated copy); returns the run."""
    objs, flags = comum
    obj = compilar(tmp, angulo_cpp, flags, nome)
    if not obj:
        return None
    exe = tmp / nome
    r = subprocess.run(['g++', objs[0], obj, *partes, '-o', str(exe)], capture_output=True, text=True)
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
        # The control recorder: its own cases, then the kit with one change to the recorder at a time.
        partes = controles_base(tmp, comum)
        if not partes:
            return 2
        real_c = controles_variante(tmp, 'ctl_real', ANGULO / 'Angulo.cpp', comum, partes)
        if not real_c:
            return 2
        print(real_c.stdout, end='')
        ultima = real_c.stdout.strip().splitlines()[-1] if real_c.stdout.strip() else ''
        m = re.fullmatch(r'(OK|FAIL): (\d+) case\(s\), (\d+) failure\(s\)', ultima)
        oks = sum(l.startswith('ok   ') for l in real_c.stdout.splitlines())
        if not m or int(m.group(2)) == 0 or oks != int(m.group(2)) - int(m.group(3)):
            print('FAIL: the recorder test did not print a summary that agrees with its cases: the harness is broken')
            return 2
        if real_c.returncode:
            return 1
        fonte_k = (ANGULO / 'Angulo.cpp').read_text(encoding='utf-8')
        for i, (nome, trocas) in enumerate(MUTANTES_CONTROLES):
            texto = fonte_k
            for antigo, novo in trocas:
                if texto.count(antigo) != 1:
                    print(f'cannot build recorder mutant "{nome}": "{antigo.strip()}" is in Angulo.cpp {texto.count(antigo)} times, '
                          'not once (the recorder changed: update MUTANTES_CONTROLES)', file=sys.stderr)
                    return 2
                texto = texto.replace(antigo, novo)
            copia = tmp / f'fonte_ctl{i}'
            copia.mkdir()
            (copia / 'Angulo.cpp').write_text(texto, encoding='utf-8')
            r = controles_variante(tmp, f'ctl_mut{i}', copia / 'Angulo.cpp', comum, partes)
            if not r:
                print(f'cannot build recorder mutant "{nome}"', file=sys.stderr)
                return 2
            falhou = sorted({l.split(':')[0][5:] for l in r.stdout.splitlines() if l.startswith('FAIL ')})
            mordeu = r.returncode == 1 and bool(falhou)
            print(f'recorder mutant {i + 1}: {nome}: exit {r.returncode}, '
                  + (f'fails {len(falhou)} case(s): {"; ".join(falhou)}' if mordeu else 'PASSES'))
            if not mordeu:
                sobrou.append(nome)
    if sobrou:
        print(f'FAIL: {len(sobrou)} mutant(s) passed every case: the test does not bite')
        return 1
    print(f'all {len(MUTANTES)} hit-test mutants and all {len(MUTANTES_CONTROLES)} recorder mutants fail at least one case')
    return 0


if __name__ == '__main__':
    sys.exit(main())
