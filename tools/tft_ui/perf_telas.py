#!/usr/bin/env python3
"""Times the compose of the Ângulo's screens on the host: the ruler for the speed-ups of DisplayManager::uaPintar( ).

Builds tools/tft_ui/host/host_perf_main.cpp for each group of screens (base, painel, menus, ajustes, identidade, graficos) with the kit, the
screens, the samples and the firmware's own strip class (src/ui/angulo/Faixa.h), WITHOUT UA_HOST, as the firmware is, at -O2 with no
auto-vectorisation (the panel's core has no vector unit); then runs each build alone, one after the other, so that no two disturb each other.
Each sample is composed in six 320 x 40 strips the way uaPintar( ) does (clear to the theme's background, the screen's draw clipped to the
strip, the black margin, the strip's hash), `reps` frames per batch, the best of three batches; the table is microseconds per frame, x86.
They show the ratio between two builds, not the panel's time. Every frame is also stitched from its strips and compared with the same
sample drawn whole by Adafruit GFX's own path: the last column is the number of pixels in which they differ, and it must be 0.

Usage: python3 tools/tft_ui/perf_telas.py [reps] [--groups base,painel,...] [--only part-of-a-sample's-name] [--top N] [--out file]
                                          [--corrupt]   (the control: flips one pixel per frame; each must then count 1)
Exit 0 when every frame's strips equal the frame drawn whole, 1 when one differs, 2 when a group cannot be built or run.
"""
import argparse
import concurrent.futures
import contextlib
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import host_telas  # noqa: E402

GRUPOS = ('base', 'painel', 'menus', 'ajustes', 'identidade', 'graficos')
OPT = ('-O2', '-fno-tree-vectorize', '-fno-tree-slp-vectorize')


def construir(grupo, tmp):
    return host_telas.build(grupo, pathlib.Path(tmp), opt=OPT, host=False, main='host_perf_main.cpp')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('reps', nargs='?', type=int, default=20)
    ap.add_argument('--groups', default=','.join(GRUPOS))
    ap.add_argument('--only', default=None)
    ap.add_argument('--top', type=int, default=8)
    ap.add_argument('--out', default=None, help='keep every line the programs printed')
    ap.add_argument('--corrupt', action='store_true')
    ap.add_argument('--extra', default='', help='more arguments for the program, e.g. --direto')
    ap.add_argument('--keep', default=None, help='build into this folder and keep the programs (folder/<group>/telas_host)')
    ap.add_argument('--use', default=None, help='run the programs kept in this folder, build nothing')
    a = ap.parse_args()
    grupos = a.groups.split(',')
    pasta = tempfile.TemporaryDirectory() if not (a.keep or a.use) else contextlib.nullcontext(a.keep or a.use)
    with pasta as raiz:
        dirs = {g: pathlib.Path(raiz) / g for g in grupos}
        if a.use:
            exes = {g: dirs[g] / 'telas_host' for g in grupos}
        else:
            for d in dirs.values():
                d.mkdir(parents=True, exist_ok=True)
            with concurrent.futures.ThreadPoolExecutor(max_workers=len(grupos)) as ex:
                exes = dict(zip(grupos, ex.map(lambda g: construir(g, dirs[g]), grupos)))
        linhas, totais = [], {}
        for g in grupos:
            if not exes[g]:
                print(f'{g}: cannot be built', file=sys.stderr)
                return 2
            cmd = [str(exes[g]), str(a.reps)] + (['--only', a.only] if a.only else []) + (['--corrupt'] if a.corrupt else []) + a.extra.split()
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode:
                print(f'{g}: exit {r.returncode}\n{r.stderr}', file=sys.stderr)
                return 2
            for l in r.stdout.splitlines():
                p = l.split()
                if p and p[0] == 'S':
                    linhas.append((g, p[1], p[2], *map(float, p[3:7]), int(p[7])))
                elif p and p[0] == 'TOTAL':
                    totais[g] = (int(p[1]), *map(float, p[2:6]), int(p[6]), int(p[7]), int(p[8]), int(p[9]), int(p[10]))
    print(f'{"group":11} {"frames":>6} {"us/frame":>10} {"clear":>9} {"draw":>9} {"hash":>9} {"differing px":>13}   (microseconds per frame summed over the frames, host x86)')
    soma = [0, 0.0, 0.0, 0.0, 0.0, 0]
    for g in grupos:
        t = totais[g]
        print(f'{g:11} {t[0]:6d} {t[1]:10.0f} {t[2]:9.0f} {t[3]:9.0f} {t[4]:9.0f} {t[5]:13d}   record: {t[6]} calls, {t[7]} B of text and calls, {t[9]} of 3200 B of the list, {t[8]} frames over')
        soma = [x + y for x, y in zip(soma[:6], t[:6])]
    print(f'{"all":11} {soma[0]:6d} {soma[1]:10.0f} {soma[2]:9.0f} {soma[3]:9.0f} {soma[4]:9.0f} {soma[5]:13d}')
    print(f'slowest {a.top} frames:')
    for g, nome, tema, us, limpa, desenha, h, dif in sorted(linhas, key=lambda l: -l[3])[:a.top]:
        print(f'  {g}/{nome} {tema}: {us:.0f} us (clear {limpa:.0f}, draw {desenha:.0f}, hash {h:.0f})')
    if a.out:
        pathlib.Path(a.out).write_text(''.join(f'{g} {n} {t} {u:.1f} {c:.1f} {d:.1f} {h:.1f} {x}\n' for g, n, t, u, c, d, h, x in linhas))
    esperado = len(linhas) if a.corrupt else 0
    return 0 if soma[5] == esperado else 1


if __name__ == '__main__':
    sys.exit(main())
