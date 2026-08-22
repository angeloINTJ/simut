#!/usr/bin/env python3
"""verify_mq_rst.py — mq_rst isolado: perda real ou ruído do heurístico?

Janelas: saudável(20s) → broker RST(45s) → saudável(40s). Compara as épocas
que os brokers RECEBERAM com o cursor do dispositivo (tel dump) antes/depois.
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


def broker(name, mode, **kw):
    return Server('mqtt', name, C.OUT, port=C.PORT_MQTT, mode=mode, **kw)


def rec_epochs(path):
    out = set()
    try:
        for line in open(path):
            line = line.strip()
            if not line:
                continue
            try:
                d = json.loads(line)
            except Exception:
                continue
            for r in d if isinstance(d, list) else [d]:
                if isinstance(r, dict) and isinstance(r.get('ts'), int):
                    out.add(r['ts'])
    except FileNotFoundError:
        pass
    return out


def main():
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_verify_rst.log'))
    time.sleep(1)
    C.cfg_http(t, C.HOST_IP, C.PORT_MQTT, crypto=False, batch=10,
               interval=1000, mode='json', path='/ingest')
    # mqtt transport
    web = C.web_session()
    C.commit(web, {'t_transport': '1', 't_sec': '0', 't_srv': C.HOST_IP,
                   't_port': str(C.PORT_MQTT), 't_int': '1000', 't_bat': '10',
                   't_mode': '0', 'm_topic': 'simut/data', 'm_qos': '0',
                   'm_retain': '0', 'm_ka': '30'})
    C.web_session(force=True)
    time.sleep(6)

    C.tel_reset(t)
    C.tel_sync(t, wait=20)

    s1 = broker('vrfy_pre', 'ok')
    print('--- janela saudável 25s ---', flush=True)
    time.sleep(25)
    pre = rec_epochs(s1.records_path)
    s1.stop()
    print(f'saudável: {len(pre)} épocas recebidas', flush=True)

    print('--- janela RST 45s ---', flush=True)
    s2 = broker('vrfy_fault', 'drop_on_publish')
    time.sleep(45)
    s2.stop()

    print('--- janela recuperação 40s ---', flush=True)
    s3 = broker('vrfy_post', 'ok')
    time.sleep(40)
    post = rec_epochs(s3.records_path)
    s3.stop()
    print(f'recuperação: {len(post)} épocas recebidas', flush=True)

    received = pre | post
    # cursor: tel dump — épocas ainda pendentes + as enviadas derivam do gap
    dump = t.send('enable', wait=1.2) + t.send('tel dump', wait=4.0)
    t.close()
    print('--- tel dump (últimos 600 chars) ---', flush=True)
    print(dump[-600:])
    print(f'épocas únicas recebidas pelos brokers: {len(received)}', flush=True)


if __name__ == '__main__':
    main()
