#!/usr/bin/env python3
r"""§5.18 Interoperabilidade — Syslog RFC 5424 ao vivo (bônus).

Os golden vectors já estão verdes nativamente (7 testes em test_validators).
Este script confirma no ferro que o forwarder emite linhas RFC 5424 válidas
para um coletor UDP real: PRI VERSION TIMESTAMP HOST APP PROCID MSGID + msg,
com timestamp NILVALUE "-" antes do sync de relógio e campos saneados.

Protocolo: commit slog_en=1 + slog_srv=<host> + slog_port=<port> (1 reboot),
coleta N datagramas, valida cada linha, restaura a config original (2º
reboot) e verifica.

Uso:  python3 tools/syslog_live_check.py [--host 192.168.3.24]
          [--scheme http|https] [--port 5514] [--count 10]
"""
import argparse
import json
import re
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scratchpad'))
from dev import Web, wait_web  # noqa: E402

try:
    from rig_secrets import PASS
except ImportError:
    PASS = os.environ.get('SIMUT_PASS', '')

RFC5424 = re.compile(
    r'^<(\d{1,3})>(\d) (\S+) (\S+) (\S+) (\S+) (\S+)(?: (.*))?$')
# estrutura: <PRI>VERSION SP TIMESTAMP SP HOST SP APP SP PROCID SP MSGID [SP SD]


def validate(line):
    m = RFC5424.match(line)
    if not m:
        return f'não casa com o esqueleto RFC 5424: {line[:100]!r}'
    pri, ver, ts, host, app, procid, msgid = m.groups()[:7]
    errs = []
    if not (0 <= int(pri) <= 191):
        errs.append(f'PRI fora de faixa: {pri}')
    if ver != '1':
        errs.append(f'VERSION {ver!r} != 1')
    if not (re.match(r'^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}', ts) or ts == '-'):
        errs.append(f'TIMESTAMP inválido: {ts!r}')
    if not host or ' ' in host:
        errs.append(f'HOST inválido: {host!r}')
    if not app:
        errs.append('APP vazio')
    if ' ' in procid or ' ' in msgid:
        errs.append(f'PROCID/MSGID com espaço: {procid!r}/{msgid!r}')
    return '; '.join(errs) if errs else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--scheme', default='http', choices=['http', 'https'])
    ap.add_argument('--port', type=int, default=5514)
    ap.add_argument('--count', type=int, default=10)
    ap.add_argument('--out', default='scratchpad/syslog_live_v232.log')
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', args.port))
    sock.settimeout(2.0)

    w = Web(host=args.host, timeout=20, scheme=args.scheme)
    ok, info = w.login('admin', PASS)
    if not ok:
        print(f'FATAL: login falhou: {info}')
        return 2

    def commit(fields):
        r = w.commit(fields)
        print(f'    commit_all -> HTTP {r.status_code}')
        if r.status_code not in (200, 302):
            raise RuntimeError(f'commit_all HTTP {r.status_code}')
        time.sleep(4)
        if wait_web(args.host, timeout=150, scheme=args.scheme) is None:
            raise RuntimeError('dispositivo não voltou após commit')
        w.s.cookies.clear()
        ok2, info2 = w.login('admin', PASS)
        if not ok2:
            raise RuntimeError(f'relogin falhou: {info2}')

    orig_cfg = w.config()
    touched = ('slog_en', 'slog_srv', 'slog_port', 'slog_lvl')
    restore = {k: orig_cfg[k] for k in touched if k in orig_cfg}

    received = []
    fails = []
    try:
        print(f'[1] commit slog_en=1 -> {args.port}/udp …')
        commit({'slog_en': '1', 'slog_srv': '192.168.3.31',
                'slog_port': str(args.port), 'slog_lvl': '1'})
        print(f'    coletor ouvindo na {args.port}/udp …')
        deadline = time.time() + 90
        while len(received) < args.count and time.time() < deadline:
            try:
                data, _ = sock.recvfrom(2048)
                line = data.decode('utf-8', errors='replace').rstrip('\n')
                received.append(line)
                print(f'    [{len(received)}] {line[:110]}')
            except socket.timeout:
                continue
        if len(received) < args.count:
            fails.append(f'só {len(received)}/{args.count} datagramas em 90 s')
        for line in received:
            err = validate(line)
            if err:
                fails.append(err)
        nilvals = [l for l in received if '- 192.' in l or '- simut' in l]
        print(f'    válidas: {len(received) - len(fails)}/{len(received)}')

        print('[2] restauração da config original …')
        commit(restore)
        after = w.config()
        drift = [k for k in touched
                 if k in orig_cfg and k in after and str(orig_cfg[k]) != str(after[k])]
        if drift:
            fails.append(f'config não restaurada: {drift}')

        Path(args.out).write_text('\n'.join(received) + '\n')
        print(f'\nsyslog_live_check: {"FAIL" if fails else "PASS"}')
        for f in fails:
            print('  FAIL: ' + f)
        return 1 if fails else 0
    finally:
        sock.close()


if __name__ == '__main__':
    raise SystemExit(main())
