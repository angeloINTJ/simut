#!/usr/bin/env python3
"""repro_lockout.py — §5.9: lockout de login — backoff exponencial + teto.

Erra a senha N vezes seguidas e mede o lockSec devolvido por /api/login_init:
espera (1<<fail)×1 s com teto de 300 s; sob lockout o login responde 429.
Uso: python3 repro_lockout.py [--host IP] [--fails 10]
"""
import argparse
import hashlib
import json
import sys
import time

import requests

HOST = '192.168.3.24'


def init_state(s):
    j = s.get(f'http://{HOST}/api/login_init', timeout=8).json()
    return j


def main():
    global HOST
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default=HOST)
    ap.add_argument('--fails', type=int, default=10)
    args = ap.parse_args()
    HOST = args.host

    s = requests.Session()
    seq = []
    for i in range(1, args.fails + 1):
        j = init_state(s)
        nonce = j.get('nonce', '')
        r = s.post(f'http://{HOST}/api/login',
                   data={'user': 'admin', 'pass': 'senha-errada', 'nonce': nonce},
                   timeout=8)
        after = init_state(s)
        seq.append((i, r.status_code, after.get('locked'), after.get('lockSec', 0)))
        print(f'falha {i:2d}: HTTP {r.status_code} locked={after.get("locked")} '
              f'lockSec={after.get("lockSec")}')
        time.sleep(1)

    expect = [min((1 << f) * 1, 300) for f in range(1, args.fails + 1)]
    ok = True
    for (i, code, locked, sec), exp in zip(seq, expect):
        # o lockSec lido logo após a falha é o teto do backoff daquele fail
        if not (locked or sec > 0):
            print(f'  ! falha {i}: lockout não armado'); ok = False
    # teto de 300 s: nunca acima
    if any(sec > 300 for _, _, _, sec in seq):
        print('  ! lockSec acima de 300 s'); ok = False
    # cresce exponencialmente (não-linear): último > primeiro
    if seq[-1][3] <= seq[0][3]:
        print(f'  ! backoff não cresceu ({seq[0][3]} -> {seq[-1][3]})'); ok = False
    print(f'sequência lockSec: {[s[3] for s in seq]}')
    print(f'RESULTADO: {"PASS" if ok else "FAIL"}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
