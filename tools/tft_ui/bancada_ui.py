#!/usr/bin/env python3
"""The bench's hands on the functional study build (pico_wiretft_ui_a).

As a command:
  bancada_ui.py --host ip:port estado                 the frame on the glass (GET /api/ui)
  bancada_ui.py --host ip:port captura <arq.png>      GET /api/screenshot as PNG
  bancada_ui.py --host ip:port tocar <x> <y> [ms]     POST /api/touch
  bancada_ui.py --host ip:port alvo <tipo> [indice]   touch the centre of that target of the frame
  bancada_ui.py --host ip:port tema <0|1|2>           POST /api/save_sys theme= (light, dark, automatic)

As a module, Bancada wraps the same calls for a test script. Credentials come
from ~/.simut-bench.env (SIMUT_WEB_USER, SIMUT_WEB_PASS), never printed.
"""
import argparse
import io
import os
import pathlib
import re
import shlex
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from air_test_suite import TestFail, Web  # noqa: E402  (the bench's own web client)

W, H = 320, 240
BMP_SIZE = 54 + W * H * 3


def creds():
    env = {}
    for line in open(os.path.expanduser('~/.simut-bench.env'), encoding='utf-8'):
        m = re.match(r'\s*export\s+(SIMUT_WEB_\w+)=(.*)$', line)
        if m:
            v = shlex.split(m.group(2), comments=True)
            env[m.group(1)] = v[0] if v else ''
    return env['SIMUT_WEB_USER'], env['SIMUT_WEB_PASS']


class Bancada:
    def __init__(self, host):
        self.web = Web(host)
        self.web.login(*creds())

    def estado(self, timeout=10.0):
        """GET /api/ui, waiting out the 503 of a board with no frame yet."""
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            r = self.web.get('/api/ui')
            if r.status_code == 200:
                return r.json()
            time.sleep(0.3)
        raise TestFail(f'/api/ui não respondeu 200 em {timeout:.0f} s')

    def esperar(self, cond, timeout=10.0, oque='a tela'):
        """GET /api/ui until cond(estado) holds and the frame on the glass is the current mode's."""
        t0 = time.monotonic()
        ultimo = None
        while time.monotonic() - t0 < timeout:
            st = self.estado()
            ultimo = st
            if st['modo'] == st['modo_atual'] and cond(st):
                return st
            time.sleep(0.15)
        raise TestFail(f'{oque}: não chegou em {timeout:.0f} s (último estado: modo {ultimo and ultimo["modo"]})')

    def captura(self, timeout=60.0):
        """GET /api/screenshot as a PIL image, waiting out 503 (a touch in the last 5 s) and 409."""
        from PIL import Image
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            r = self.web.get('/api/screenshot')
            if r.status_code == 200 and r.content[:2] == b'BM' and len(r.content) == BMP_SIZE:
                return Image.open(io.BytesIO(r.content)).convert('RGB')
            if r.status_code in (503, 409):
                time.sleep(float(r.headers.get('Retry-After', '1')) if r.status_code == 503 else 1.0)
                continue
            if r.status_code == 200:
                time.sleep(1.0)
                continue
            raise TestFail(f'/api/screenshot HTTP {r.status_code}: {r.text[:120]}')
        raise TestFail(f'o /api/screenshot não respondeu 200 em {timeout:.0f} s')

    def tocar(self, x, y, ms=100):
        r = self.web.post('/api/touch', data={'x': str(int(x)), 'y': str(int(y)), 'ms': str(int(ms))})
        if r.status_code != 200:
            raise TestFail(f'/api/touch HTTP {r.status_code}: {r.text[:120]}')
        # The injected press lasts `ms`; the panel samples every 15 ms or so.
        time.sleep(ms / 1000.0 + 0.12)

    def alvos(self, st, tipo, indice=None):
        return [a for a in st['alvos'] if a[4] == tipo and (indice is None or a[5] == indice)]

    def tocar_alvo(self, tipo, indice=None, ms=100, st=None):
        st = st or self.estado()
        hit = self.alvos(st, tipo, indice)
        if not hit:
            raise TestFail(f'modo {st["modo"]}: não há alvo {tipo}/{indice} (alvos: {st["alvos"]})')
        x, y, w, h = hit[0][:4]
        self.tocar(x + w // 2, y + h // 2, ms)
        return hit[0]

    def tema(self, n):
        r = self.web.post('/api/save_sys', data={'theme': str(int(n))})
        if r.status_code != 200:
            raise TestFail(f'/api/save_sys theme={n}: HTTP {r.status_code}: {r.text[:120]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--host', default=os.environ.get('SIMUT_HOST'))
    ap.add_argument('cmd')
    ap.add_argument('args', nargs='*')
    a = ap.parse_args()
    if not a.host:
        raise SystemExit('passe --host ip:porta (ou defina SIMUT_HOST)')
    b = Bancada(a.host)
    if a.cmd == 'estado':
        st = b.estado()
        print({k: v for k, v in st.items() if k != 'alvos'})
        for al in st['alvos']:
            print('  alvo', al)
    elif a.cmd == 'captura':
        b.captura().save(a.args[0])
        print('salvo', a.args[0])
    elif a.cmd == 'tocar':
        b.tocar(*map(int, a.args))
    elif a.cmd == 'alvo':
        print(b.tocar_alvo(*map(int, a.args)))
    elif a.cmd == 'tema':
        b.tema(int(a.args[0]))
    else:
        raise SystemExit(f'comando desconhecido: {a.cmd}')


if __name__ == '__main__':
    sys.exit(main())
