#!/usr/bin/env python3
"""Holds the Ângulo kit to the TFT plan's references, on the host.

Compiles src/ui/angulo/Angulo.cpp and the ports of the plan's family A
screens (tools/tft_ui/plano) with Adafruit GFX 1.12.6, draws every ported
screen in both themes into the library's own GFXcanvas16 and compares each
frame, as RGB565, with the plan's reference frame regenerated glyph by glyph
(gerar_firmware.js --esperado <dir>, of the synthetic-capture study: tools/tft_synth/ on
the branch study/tft-angulo-ui-a, which this tree does not carry).

Usage: python3 tools/tft_ui/host_ui.py <reference frames folder> [--out <folder>]
Exit 0 when every ported frame is equal, 1 when one differs, 2 when it cannot run.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
SHIM = HERE / 'host' / 'shim'


def find_gfx():
    """Adafruit GFX 1.12.6, the version the kit was drawn against, as PlatformIO fetched it into this
    tree: run `pio run -e pico_wiretft_angulo` (or any TFT environment) once. Only this tree's
    .pio/libdeps is looked at; an earlier version also looked three directories above it, which
    resolved to a library folder only in the author's scratch layout."""
    for d in sorted((ROOT / '.pio' / 'libdeps').glob('*/Adafruit GFX Library')):
        props = d / 'library.properties'
        if props.is_file() and re.search(r'^version=1\.12\.6$', props.read_text(), re.M):
            return d
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('esperado')
    ap.add_argument('--out', help='keep the frames and a diff PNG per differing frame here')
    args = ap.parse_args()
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found', file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix='ui_host_') as tmp:
        tmp = pathlib.Path(tmp)
        inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', f'-I{SHIM}', f'-I{gfx}',
               f'-I{ROOT / "src" / "ui" / "angulo"}', f'-I{HERE / "plano"}']
        units = [(gfx / 'Adafruit_GFX.cpp', ['-w']),
                 (ROOT / 'src' / 'ui' / 'angulo' / 'Angulo.cpp', ['-Wall', '-Wextra', '-Werror']),
                 *[(f, ['-Wall', '-Wextra', '-Werror']) for f in sorted((HERE / 'plano').glob('*.cpp'))],
                 (HERE / 'host' / 'host_main.cpp', ['-Wall', '-Wextra', '-Werror'])]
        objs = []
        for src, flags in units:
            obj = tmp / (src.stem + '.o')
            r = subprocess.run(['g++', *inc, *flags, '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
            if r.returncode:
                print(r.stderr, file=sys.stderr)
                return 2
            objs.append(str(obj))
        exe = tmp / 'ui_host'
        r = subprocess.run(['g++', *objs, '-o', str(exe)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr, file=sys.stderr)
            return 2
        frames = pathlib.Path(args.out) if args.out else tmp / 'frames'
        frames.mkdir(parents=True, exist_ok=True)
        r = subprocess.run([str(exe), str(frames)], capture_output=True, text=True)
        print(r.stdout.strip())
        if r.returncode:
            print(r.stderr.strip(), file=sys.stderr)
            return 1
        bad = n = 0
        for f in sorted(frames.glob('*.565')):
            n += 1
            got = f.read_bytes()
            want = (pathlib.Path(args.esperado) / f.name).read_bytes()
            diff = [k // 2 for k in range(0, len(got), 2) if got[k:k + 2] != want[k:k + 2]]
            if diff:
                bad += 1
                xs = [d % 320 for d in diff]
                ys = [d // 320 for d in diff]
                print(f'DIFFERS  {f.stem}: {len(diff)} px in x {min(xs)}..{max(xs)}, y {min(ys)}..{max(ys)}')
                if args.out:
                    from PIL import Image
                    def img(b):
                        px = [b[k] | (b[k + 1] << 8) for k in range(0, len(b), 2)]
                        return Image.frombytes('RGB', (320, 240), bytes(v for c in px for v in (
                            ((c >> 11) << 3) | (c >> 13), (((c >> 5) & 63) << 2) | (((c >> 5) & 63) >> 4), ((c & 31) << 3) | ((c & 31) >> 2))))
                    sheet = Image.new('RGB', (968, 240), (90, 90, 96))
                    sheet.paste(img(want), (0, 0)); sheet.paste(img(got), (324, 0))
                    m = Image.new('RGB', (320, 240), (0, 0, 0))
                    for d in diff: m.putpixel((d % 320, d // 320), (255, 0, 255))
                    sheet.paste(m, (648, 0)); sheet.save(frames / f'{f.stem}.diff.png')
        print(f'{n - bad} of {n} frames equal to the plan')
        return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
