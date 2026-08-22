#!/usr/bin/env python3
"""repro_slowloris.py — §5.9: GET lento dropado sem reboot (parse 3.000 ms).

Abre conexões TCP e pinga cabeçalhos HTTP lentos (1 linha a cada 900 ms, abaixo
do timeout de inatividade por byte, acima do orçamento total do parse). O
servidor deve derrubar cada conexão em ~3 s (prazo global do parse) e NUNCA
rebootar. Várias conexões em paralelo para medir o pior caso.

Uso: python3 repro_slowloris.py [--host IP] [--conns 6] [--rounds 3]
"""
import argparse
import hashlib
import socket
import sys
import time

import requests

HOST = '192.168.3.24'
PORT = 80


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


def one_loris(conn_id, results):
    s = socket.socket()
    s.settimeout(1.0)
    try:
        s.connect((HOST, PORT))
    except OSError as e:
        results.append((conn_id, 'connect_fail', str(e)))
        return
    t0 = time.time()
    try:
        s.sendall(b'GET / HTTP/1.1\r\nHost: x\r\n')
        while True:
            try:
                s.sendall(b'X-Dribble: a\r\n')
            except OSError:
                break  # servidor fechou
            time.sleep(0.9)
    finally:
        dt = time.time() - t0
        s.close()
    results.append((conn_id, 'closed_after_s', round(dt, 2)))


def main():
    global HOST
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default=HOST)
    ap.add_argument('--conns', type=int, default=6)
    ap.add_argument('--rounds', type=int, default=3)
    args = ap.parse_args()
    HOST = args.host

    u0 = uptime_ms()
    for rnd in range(args.rounds):
        import threading
        results = []
        threads = [threading.Thread(target=one_loris, args=(i, results))
                   for i in range(args.conns)]
        t0 = time.time()
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=12)
        wall = time.time() - t0
        closes = [r for r in results if r[1] == 'closed_after_s']
        times = [r[2] for r in closes]
        print(f'round {rnd + 1}: {len(closes)}/{args.conns} conexões derrubadas '
              f'em {min(times):.2f}–{max(times):.2f} s (janela {wall:.1f} s)')
        for r in results:
            if r[1] != 'closed_after_s':
                print(f'  ! {r}')
        time.sleep(2)
    u1 = uptime_ms()
    rebooted = u1 < u0
    print(f'uptime: {u0} -> {u1} ms; reboot={"SIM" if rebooted else "NÃO"}')
    sys.exit(1 if rebooted else 0)


if __name__ == '__main__':
    main()
