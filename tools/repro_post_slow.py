#!/usr/bin/env python3
"""repro_post_slow.py — §5.9: corpo lento (POST) dropado sem reboot (15.000 ms).

POST com Content-Length grande e corpo pingado devagar para /api/upload e
/api/restore: o servidor deve cortar em ~15 s (orçamento do corpo) e continuar
vivo. Uso: python3 repro_post_slow.py [--host IP] [--rounds 2]
"""
import argparse
import hashlib
import socket
import sys
import time

import requests

HOST = '192.168.3.24'
PORT = 80
TARGETS = ['/api/upload', '/api/restore']


def uptime_ms():
    sys.path.insert(0, '/home/angelo/Documentos/simut/scratchpad')
    import rig_secrets
    s = requests.Session()
    n = s.get(f'http://{HOST}/api/login_init', timeout=8).json()['nonce']
    s.post(f'http://{HOST}/api/login',
           data={'user': rig_secrets.USER,
                 'pass': hashlib.sha256(rig_secrets.PASS.encode('latin-1')).hexdigest(),
                 'nonce': n}, timeout=8)
    return s.get(f'http://{HOST}/api/status', timeout=8).json()['sys']['uptime']


def slow_post(target):
    s = socket.socket()
    s.settimeout(1.0)
    s.connect((HOST, PORT))
    t0 = time.time()
    head = (f'POST {target} HTTP/1.1\r\nHost: x\r\n'
            'Content-Type: application/octet-stream\r\n'
            'Content-Length: 1000000\r\n\r\n').encode()
    s.sendall(head)
    try:
        for _ in range(60):
            s.sendall(b'x' * 1000)
            time.sleep(0.5)
    except OSError:
        pass
    dt = time.time() - t0
    s.close()
    return round(dt, 2)


def main():
    global HOST
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default=HOST)
    ap.add_argument('--rounds', type=int, default=2)
    args = ap.parse_args()
    HOST = args.host

    u0 = uptime_ms()
    for rnd in range(args.rounds):
        for t in TARGETS:
            dt = slow_post(t)
            print(f'round {rnd + 1} {t}: conexão derrubada em {dt} s')
        time.sleep(2)
    u1 = uptime_ms()
    rebooted = u1 < u0
    print(f'uptime: {u0} -> {u1} ms; reboot={"SIM" if rebooted else "NÃO"}')
    sys.exit(1 if rebooted else 0)


if __name__ == '__main__':
    main()
