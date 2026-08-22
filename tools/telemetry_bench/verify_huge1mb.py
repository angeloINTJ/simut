#!/usr/bin/env python3
"""verify_huge1mb.py — reproduz o fault huge1mb isolado, 2 corridas.

Mede o cursor (tel dump) antes/depois e o que o servidor de fato recebeu.
Se o gap de épocas for consistente entre corridas → perda real (DEFEITOS);
se variar/impossível → ruído de relógio documentado na campanha 08-02.
"""
import json
import os
import sys
import time

sys.path.insert(0, '/home/angelo/Documentos/simut/tools/telemetry_bench')
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402

os.environ.setdefault('SIMUT_WEB_USER', 'admin')
os.environ.setdefault('SIMUT_WEB_PASS', 'simutV5x')

SECONDS = 90


def cursor_epochs(t):
    out = t.send('enable', wait=1.2) + t.send('tel dump', wait=3.0)
    lines = [l for l in out.splitlines() if ':' in l]
    return out[-400:]


def run_one(t, n):
    kill_stale()
    srv = Server('http', f'verify_huge_{n}', C.OUT, port=C.PORT_HTTP,
                 mode='huge', huge_bytes=1 << 20)
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=10,
               interval=1000, mode='json', path='/ingest')
    C.tel_reset(t)
    C.tel_sync(t)
    time.sleep(2)
    dump0 = cursor_epochs(t)
    t0 = time.time()
    sends0 = C.status()['sys'].get('t_sent', None)
    while time.time() - t0 < SECONDS:
        time.sleep(10)
        st = C.status()
        sysc = st.get('sys', {})
        print(f'  [{n}] t={int(time.time()-t0):3d}s sent={sysc.get("t_sent")} '
              f'fail={sysc.get("t_fail")} pending={sysc.get("pending")} '
              f'uptime={sysc.get("uptime")}')
    dump1 = cursor_epochs(t)
    srv.stop()
    stats = json.load(open(srv.stats_path)) if os.path.exists(srv.stats_path) else {}
    print(f'  [{n}] srv: requests={stats.get("requests")} records={stats.get("records")} '
          f'conns={stats.get("conns")}')
    print(f'  [{n}] cursor antes: {dump0.strip().splitlines()[-2:] if dump0.strip() else "?"}')
    print(f'  [{n}] cursor depois: {dump1.strip().splitlines()[-2:] if dump1.strip() else "?"}')
    return stats


def main():
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_verify_huge.log'))
    time.sleep(1)
    for n in (1, 2):
        print(f'=== corrida {n} ===')
        run_one(t, n)
        time.sleep(15)
    t.close()
    print('done')


if __name__ == '__main__':
    main()
