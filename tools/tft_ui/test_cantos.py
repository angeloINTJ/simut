#!/usr/bin/env python3
"""test_cantos.py — proves the smoothed corners and circles of the Angulo kit's Pintor on the host.

Builds tools/tft_ui/host/test_cantos_main.cpp with src/ui/angulo/Angulo.cpp and Adafruit GFX
1.12.6 (the tree's .pio/libdeps, as tools/tft_ui/host_ui.py finds it) and runs it: a button, a
card and a pill, filled and outlined, and three circles, each drawn black on white, checked for
what the smoothing promises and what it must not change. Nothing outside the shape's box; the
straight edges and the inside as before; tones between background and colour in the corner (the
smoothing); four mirror-image corners; coverage that never drops along an edge toward its middle.
Then ua::margemPreta( ): the four bands of the margin are black and no pixel inside the safe area is touched.

Then the instrument is bitten: the same program built with -DCANTOS_SEM_LEITOR draws the shapes
with Adafruit GFX itself, its all-or-nothing arcs, what the Pintor drew before and the 45-degree
look the maintainer pointed at on the panel (2026-10-15). Every "tones in between" check must
fail there, and the control must exit 1.

Usage: python3 tools/tft_ui/test_cantos.py   exit 0 when both hold, 1 when not, 2 when it cannot build.
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)


def build(tmp, extra):
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found (run pio run -e pico_wiretft_angulo once)', file=sys.stderr)
        return None
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{HERE / "host" / "shim"}', f'-I{gfx}',
           f'-I{ROOT / "src" / "ui" / "angulo"}', *extra]
    strict = ['-Wall', '-Wextra', '-Werror']
    units = [(gfx / 'Adafruit_GFX.cpp', ['-w']),
             (ROOT / 'src' / 'ui' / 'angulo' / 'Angulo.cpp', strict),
             (ROOT / 'src' / 'ui' / 'angulo' / 'Textos.cpp', strict),   # the kit's UA_TXT( ): ua::txt( ) (S10c, B3)
             (HERE / 'host' / 'test_cantos_main.cpp', strict)]
    objs = []
    for src, flags in units:
        obj = tmp / (src.stem + '.o')
        r = subprocess.run(['g++', *inc, *flags, '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr, file=sys.stderr)
            return None
        objs.append(str(obj))
    exe = tmp / 'cantos'
    r = subprocess.run(['g++', *objs, '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr, file=sys.stderr)
        return None
    return exe


def main():
    with tempfile.TemporaryDirectory(prefix='cantos_') as d:
        tmp = pathlib.Path(d)
        (tmp / 'real').mkdir()
        (tmp / 'ctl').mkdir()
        exe = build(tmp / 'real', [])
        ctl = build(tmp / 'ctl', ['-DCANTOS_SEM_LEITOR'])
        if not exe or not ctl:
            return 2
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        c = subprocess.run([str(ctl)], capture_output=True, text=True)
    print(r.stdout, end='')
    tons = [l for l in c.stdout.splitlines() if 'tones in between' in l]
    mordeu = c.returncode == 1 and tons and all(l.startswith('FAIL') for l in tons)
    print(f'\ncontrol (Adafruit GFX arcs): exit {c.returncode}, '
          f'{sum(l.startswith("FAIL") for l in tons)} of {len(tons)} "tones in between" checks fail')
    if not mordeu:
        print('FAIL: the control did not fail every "tones in between" check: the test does not bite')
        print(c.stdout)
        return 1
    return r.returncode


if __name__ == '__main__':
    sys.exit(main())
