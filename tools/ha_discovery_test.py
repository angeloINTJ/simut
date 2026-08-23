#!/usr/bin/env python3
r"""§5.18 Interoperabilidade — Home Assistant MQTT Discovery ao vivo
(PLANO-VALIDACAO-v2.3.2-stable.md).

Limite do plano: discovery cria entidades (1 por medição: DS18B20→1,
DHT22→2, BMP280→2 — com a bancada atual de 5 sensores = 8 entidades);
desmarcar remove a config retida (0 config retida após off).

Protocolo:
  1. Salva o /api/config atual (e o restaura no fim, com verificação).
  2. Sobe tools/telemetry_bench/server_mqtt.py em --port livre (broker real,
     com stats + records NDJSON).
  3. commit_all: m_had=1 + broker local (custa 1 reboot; HA publica no
     connect pós-reboot).
  4. Espera os topics `homeassistant/sensor/<node>/<obj>/config` (retained);
     valida JSON, contagem == entidades esperadas da tabela viva de sensores.
  5. commit_all: m_had=0 (2º reboot) → asserta que o último publish retained
     de cada topic de config é a limpeza (payload vazio) — nada retido.
  6. Restaura a config original (3º reboot) e verifica.

Uso:  python3 tools/ha_discovery_test.py [--host 192.168.3.24] [--scheme https]
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scratchpad'))
from dev import Web, wait_web  # noqa: E402

try:
    from rig_secrets import PASS
except ImportError:
    PASS = os.environ.get('SIMUT_PASS', '')

BROKER_PORT = 18884
HOME = 'homeassistant'
SLOT_CH = {  # tipo -> nº de entidades (medições) que publica
    'DS18B20': 1, 'DHT22': 2, 'BMP280': 2, 'BME280': 2,
}


def expected_entities(sensors):
    """Da lista viva de sensores (/api/sensors), conta as entidades esperadas."""
    n = 0
    for s in sensors:
        t = str(s.get('type', '')).upper()
        n += SLOT_CH.get(t, 1)
    return n


def parse_records(path):
    recs = []
    if not os.path.exists(path):
        return recs
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.strip()
        if not line:
            continue
        try:
            recs.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--scheme', default='http', choices=['http', 'https'])
    ap.add_argument('--out', default='scratchpad/ha_discovery_v232')
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    stats_path = out / 'broker_stats.json'
    records_path = out / 'broker_records.ndjson'

    w = Web(host=args.host, timeout=20, scheme=args.scheme)
    ok, info = w.login('admin', PASS)
    if not ok:
        print(f'FATAL: login falhou: {info}')
        return 2

    def commit(fields):
        r = w.commit(fields)
        print(f'    commit_all -> HTTP {r.status_code} {r.text[:100]}')
        if r.status_code not in (200, 302):
            raise RuntimeError(f'commit_all HTTP {r.status_code}')
        time.sleep(4)
        back = wait_web(args.host, timeout=150, scheme=args.scheme)
        if back is None:
            raise RuntimeError('dispositivo não voltou após commit')
        w.s.cookies.clear()
        ok2, info2 = w.login('admin', PASS)
        if not ok2:
            raise RuntimeError(f'relogin falhou: {info2}')
        print(f'    web de volta em {back}s, sessão nova ok')

    # 1. Config atual
    orig_cfg = w.config()
    if '_http' in orig_cfg:
        print(f'FATAL: /api/config {orig_cfg["_http"]}: {orig_cfg["_body"][:120]}')
        return 2
    (out / 'config_before.json').write_text(json.dumps(orig_cfg, indent=2))

    # Campos que este teste mexe (restaurados no fim)
    touched = ('t_srv', 't_port', 't_int', 't_bat', 't_mode', 't_transport',
               't_sec', 't_path', 'm_topic', 'm_cid', 'm_user', 'm_qos',
               'm_retain', 'm_ka', 'm_had')

    # 2. Broker
    if os.path.exists(stats_path):
        os.remove(stats_path)
    if os.path.exists(records_path):
        os.remove(records_path)
    proc = subprocess.Popen(
        [sys.executable, 'tools/telemetry_bench/server_mqtt.py',
         '--port', str(BROKER_PORT), '--stats', str(stats_path),
         '--records', str(records_path)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    try:
        # 3. Liga HA + broker local
        fields = {
            't_srv': '192.168.3.31',  # host da bancada (broker local)
            't_port': str(BROKER_PORT),
            't_int': '2000',
            't_bat': '3',
            't_mode': '0',
            't_transport': '0',
            't_sec': '0',
            'm_topic': 'simut/data',
            'm_cid': 'simut-ha-test',
            'm_user': '',
            'm_qos': '0',
            'm_retain': '1',
            'm_ka': '30',
            'm_had': '1',
        }
        print(f'[1] commit m_had=1 (broker {BROKER_PORT}) …')
        commit(fields)

        # Tabela viva de sensores pós-reboot
        r = w.get('/api/sensors')
        sensors = r.json() if r.status_code == 200 else []
        expect = expected_entities(sensors)
        print(f'    sensores vivos: {len(sensors)} -> {expect} entidades esperadas')

        # 4. Espera as configs de discovery
        deadline = time.time() + 90
        config_topics = {}
        while time.time() < deadline:
            for rec in parse_records(records_path):
                t = rec.get('topic', '')
                if t.startswith(HOME + '/') and t.endswith('/config'):
                    config_topics[t] = rec
            if len(config_topics) >= expect:
                break
            time.sleep(2)

        print(f'    topics de config: {len(config_topics)} (esperado {expect})')
        fails = []
        if len(config_topics) != expect:
            fails.append(f'contagem de entidades {len(config_topics)} != {expect}')
        for t, rec in config_topics.items():
            try:
                j = json.loads(rec.get('payload', ''))
            except json.JSONDecodeError:
                fails.append(f'{t}: payload não é JSON')
                continue
            need = ('name', 'state_topic', 'unique_id', 'device_class')
            for k in need:
                if k not in j:
                    fails.append(f'{t}: sem {k!r}')
            if not rec.get('retain'):
                fails.append(f'{t}: publish sem retain')
            print(f'    [OK] {t} retain={rec.get("retain")} '
                  f'keys={len(j) if isinstance(j, dict) else "?"}')

        # 5. Desliga HA -> limpeza retained
        print('[2] commit m_had=0 …')
        fields['m_had'] = '0'
        commit(fields)
        deadline = time.time() + 90
        clears = {}
        while time.time() < deadline:
            for rec in parse_records(records_path):
                t = rec.get('topic', '')
                if t in config_topics and rec.get('retain') and rec.get('payload', '') == '':
                    clears[t] = rec
            if len(clears) >= len(config_topics):
                break
            time.sleep(2)
        print(f'    limpezas retained observadas: {len(clears)}/{len(config_topics)}')
        if len(clears) < len(config_topics):
            missing = set(config_topics) - set(clears)
            fails.append(f'config retida após off: {sorted(missing)}')

        # 6. Restaura a config original
        print('[3] restauração da config original …')
        restore = {k: orig_cfg[k] for k in touched if k in orig_cfg}
        commit(restore)
        after = w.config()
        drift = [k for k in touched
                 if k in orig_cfg and k in after and str(orig_cfg[k]) != str(after[k])]
        if drift:
            fails.append(f'config não restaurada: {drift}')
        else:
            print('    restauração verificada (campos tocados idênticos)')

        print(f'\nha_discovery_test: {"FAIL" if fails else "PASS"} '
              f'({expect} entidades, {len(clears)}/{len(config_topics)} limpezas)')
        for f in fails:
            print('  FAIL: ' + f)
        return 1 if fails else 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == '__main__':
    raise SystemExit(main())
