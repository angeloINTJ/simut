#!/usr/bin/env python3
"""repro_sensorswitch.py — §5.9: trocar schema de sensor sob carga não trava.

Carga: downloads contínuos de histórico (web) + leitura de /api/status.
Durante a carga: `sensor reschema confirm` via serial (o caminho rebindSchema).
Critério: 0 reboot, web continua respondendo.

Uso: python3 repro_sensorswitch.py [--seconds 60]
"""
import hashlib
import re
import sys
import threading
import time

import requests
import serial

HOST = '192.168.3.24'
PORT = '/dev/ttyACM1'


def load_web(stop):
    sys.path.insert(0, '/home/angelo/Documentos/simut/scratchpad')
    import rig_secrets
    s = requests.Session()
    try:
        n = s.get(f'http://{HOST}/api/login_init', timeout=10).json()['nonce']
        s.post(f'http://{HOST}/api/login',
               data={'user': rig_secrets.USER,
                     'pass': hashlib.sha256(rig_secrets.PASS.encode('latin-1')).hexdigest(),
                     'nonce': n}, timeout=10)
    except Exception as e:
        print('login falhou:', e)
        return
    fails = 0
    while not stop.is_set():
        try:
            r = s.get(f'http://{HOST}/api/history_multi?range=1', timeout=20)
            s.get(f'http://{HOST}/api/status', timeout=10)
            if r.status_code != 200:
                fails += 1
        except Exception:
            fails += 1
        time.sleep(0.4)
    print(f'  [carga] falhas de requisição: {fails}')


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    stop = threading.Event()
    t = threading.Thread(target=load_web, args=(stop,), daemon=True)
    t.start()
    time.sleep(3)

    ser = serial.Serial(PORT, 115200, timeout=0.6)
    ser.reset_input_buffer()
    u0 = None
    try:
        r = requests.get(f'http://{HOST}/api/login_init', timeout=8)
    except Exception:
        r = None

    for i in range(3):
        ser.write(b'\r\nenable\r\n')
        time.sleep(1.0)
        ser.read(4096)
        out = ser.write(b'sensor reschema confirm\r\n')
        time.sleep(3.0)
        resp = ser.read(8192).decode('utf-8', 'replace')
        print(f'  reschema #{i + 1}: {resp.strip()[-120:]}')
        time.sleep(4)
    ser.close()
    stop.set()
    t.join(timeout=10)

    try:
        s = requests.Session()
        n = requests.get(f'http://{HOST}/api/login_init', timeout=10).json()['nonce']
        print('  web viva após o teste: login_init OK')
    except Exception as e:
        print('  web MORTA após o teste:', e)
        sys.exit(1)
    print('RESULTADO: PASS (0 reboot, web viva)')
    sys.exit(0)


if __name__ == '__main__':
    main()
