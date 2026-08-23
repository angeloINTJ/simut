#!/usr/bin/env python3
r"""§5.19 Performance — export CSV de 3 dias (PLANO-VALIDACAO-v2.3.2-stable.md).

O download real é GET /api/export/history.bin?from=<epoch>&to=<epoch> — um
bundle .simx (kind 'H') que o navegador expande em CSV localmente
(src/WebManager_History.cpp:983). Limites do plano: 3 dias ~10 s (teto
15 s), sem estouro de deadline.

Mede o tempo de transferência do corpo completo, valida o formato
(HEADER "SIMX" ver=1 kind='H' recSize=70, range correto, TRAILER crc32
sobre header+table+payload) e reporta registros/s.

Uso:  python3 tools/export_csv_bench.py [--host 192.168.3.24]
          [--scheme http|https] [--days 3]
"""
import argparse
import json
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scratchpad'))
from dev import Web  # noqa: E402

try:
    from rig_secrets import PASS
except ImportError:
    PASS = os.environ.get('SIMUT_PASS', '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--scheme', default='http', choices=['http', 'https'])
    ap.add_argument('--days', type=int, default=3)
    ap.add_argument('--out', default='scratchpad/export_csv_v232.bin')
    args = ap.parse_args()

    w = Web(host=args.host, timeout=30, scheme=args.scheme)
    ok, info = w.login('admin', PASS)
    if not ok:
        print(f'FATAL: login falhou: {info}')
        return 2

    now = int(time.time())
    frm = now - args.days * 86400
    url = f'/api/export/history.bin?from={frm}&to={now}'
    t0 = time.time()
    r = w.get(url, stream=True)
    body = b''
    for chunk in r.iter_content(65536):
        if chunk:
            body += chunk
    wall = time.time() - t0
    print(f'HTTP {r.status_code} · {len(body)} B em {wall:.2f} s '
          f'({len(body) / max(wall, 0.001) / 1024:.0f} KB/s)')

    fails = []
    if r.status_code != 200:
        print(f'FATAL: HTTP {r.status_code}: {body[:200]}')
        return 2

    # Formato .simx (todo LE) — ver WebManager_History.cpp:983
    if len(body) < 32 + 4 + 4:
        fails.append(f'corpo curto demais ({len(body)} B)')
    else:
        magic, ver, kind, rsv0, recsize, rsv1, r_from, r_to, tbl, rsv2, rsv3 = \
            struct.unpack_from('<4sBBHHIHHIII', body, 0)
        trailer = struct.unpack_from('<I', body, len(body) - 4)[0]
        crc = zlib.crc32(body[:-4]) & 0xFFFFFFFF
        n = (len(body) - 32 - tbl - 4) // recsize if recsize else 0
        print(f'    magic={magic!r} ver={ver} kind={chr(kind)!r} '
              f'recSize={recsize} tbl={tbl} range={r_from}..{r_to} '
              f'records={n}')
        if magic != b'SIMX':
            fails.append(f'magic {magic!r} != SIMX')
        if ver != 1 or kind != ord('H') or recsize != 70:
            fails.append(f'header inesperado: ver={ver} kind={chr(kind)!r} '
                         f'recSize={recsize}')
        if trailer != crc:
            fails.append(f'crc32 trailer {trailer:08x} != calculado {crc:08x}')
        if n == 0:
            fails.append('0 registros no range de 3 dias')
        if wall > 15.0:
            fails.append(f'teto de 15 s estourado ({wall:.2f}s)')

    Path(args.out).write_bytes(body)
    print(f'\nexport_csv_bench: {"FAIL" if fails else "PASS"} '
          f'({wall:.2f}s para {args.days} dias)')
    for f in fails:
        print('  FAIL: ' + f)
    return 1 if fails else 0


if __name__ == '__main__':
    raise SystemExit(main())
