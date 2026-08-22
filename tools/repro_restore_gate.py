#!/usr/bin/env python3
"""repro_restore_gate.py — §5.9/§5.21: restore sem auth → 100 recusas, 0 arquivo.

POST /api/restore?op=apply SEM sessão, 100 vezes, com payload .bkp forjado:
espera 401/403 em todas, nenhum arquivo gravado e nenhum reboot.
Uso: python3 repro_restore_gate.py [--host IP]
"""
import argparse
import hashlib
import sys

import requests

HOST = '192.168.3.24'


def main():
    global HOST
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default=HOST)
    args = ap.parse_args()
    HOST = args.host

    s = requests.Session()
    refused = 0
    other = []
    for i in range(100):
        r = s.post(f'http://{HOST}/api/restore?op=apply',
                   files={'restore': ('forged.bkp', b'BKP1' + b'\x00' * 512)},
                   timeout=15)
        if r.status_code in (401, 403):
            refused += 1
        else:
            other.append((i, r.status_code, r.text[:60]))
    print(f'recusadas: {refused}/100')
    if other:
        for o in other[:10]:
            print(f'  ! {o}')
    ok = refused == 100
    print(f'RESULTADO: {"PASS" if ok else "FAIL"}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
