#!/usr/bin/env python3
r"""§5.7 Interações de recurso (§4.4 do plano) — gate de heap da telemetria
(PLANO-VALIDACAO-v2.3.2-stable.md, linhas "novo").

Os três cenários da §4.4, um por execução (cada um custa 1 commit+reboot):

  tls     Web HTTPS + telemetria TLS:
          com a sessão web TLS viva o heap livre fica ~15 KB (< 24 KB) —
          o pré-voo TLS (floor 24.576) deve ABORTAR o envio (backoff com log,
          0 reboot) e RETOMAR no ciclo seguinte ao fechar a sessão.
  plain   Web HTTPS + telemetria claro:
          floor 14.336 — deve ENVIAR mesmo com heap < 24 KB (envio com a
          sessão viva), 0 reboot.
  closed  Sessão TLS fechada (controle do cenário tls, já embutido): heap
          sobe e o próximo ciclo envia.

Evidências:
  - sink local (tools/telemetry_bench/server_http.py) com --records NDJSON:
    cada registro recebido é 1 linha; contamos o delta por janela.
  - /api/status (heap_f, heap_lb, uptime, pending) amostrado a cada 2 s
    pela MESMA sessão requests (keep-alive = a sessão TLS viva sob teste).
  - /api/logs (anel de log do dispositivo): procura [WRN][TEL] novo durante
    o cenário tls (backoff com log).
  - uptime monotônico = 0 reboot.

PRÉ-REQUISITO: imagem release com HTTPS ligado (par EC em /config) — o pool
TLS de ~21,5 KB só existe aí; sem ele o heap nunca desce de 24 KB e o
cenário tls não tem o que medir.

Uso:  python3 tools/heap_gate_interaction.py --scenario tls|plain \
          [--host 192.168.3.24] [--sink-port 18885]
"""
import argparse
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scratchpad'))
from dev import Web, wait_web  # noqa: E402

try:
    from rig_secrets import PASS
except ImportError:
    PASS = os.environ.get('SIMUT_PASS', '')

FLOOR = {1: 24576, 0: 14336}   # t_transport -> floor do pré-voo
RESERVE = {1: 32768, 0: 12288}  # t_transport -> reserva de dimensionamento


def count_records(path):
    if not os.path.exists(path):
        return 0
    n = 0
    for line in open(path, encoding='utf-8', errors='replace'):
        if line.strip():
            n += 1
    return n


def fetch_logs(w):
    try:
        r = w.get('/api/logs')
        if r.status_code == 200:
            return r.json()
    except Exception:
        pass
    return []


def tel_warns(logs, seen_before):
    """Entradas novas de log com [WRN][TEL] (texto serializado)."""
    hits = []
    for e in logs[seen_before:]:
        s = json.dumps(e, ensure_ascii=False)
        if 'TEL' in s and ('WRN' in s or 'code=3' in s or 'code":3' in s):
            hits.append(s[:140])
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scenario', required=True, choices=['tls', 'plain'])
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--sink-port', type=int, default=18885)
    ap.add_argument('--out', default='scratchpad/heap_gate_v232')
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    records = out / f'records_{args.scenario}.ndjson'

    transport = 1 if args.scenario == 'tls' else 0
    w = Web(host=args.host, timeout=20, scheme='https')
    ok, info = w.login('admin', PASS)
    if not ok:
        print(f'FATAL: login HTTPS falhou ({info}) — a release precisa estar '
              'com HTTPS ligado para este teste')
        return 2

    def commit(fields):
        r = w.commit(fields)
        print(f'    commit_all -> HTTP {r.status_code}')
        if r.status_code not in (200, 302):
            raise RuntimeError(f'commit_all HTTP {r.status_code}: {r.text[:120]}')
        time.sleep(4)
        if wait_web(args.host, timeout=150, scheme='https') is None:
            raise RuntimeError('dispositivo não voltou após commit')
        w.s.cookies.clear()
        ok2, info2 = w.login('admin', PASS)
        if not ok2:
            raise RuntimeError(f'relogin falhou: {info2}')

    orig_cfg = w.config()
    touched = ('t_srv', 't_port', 't_int', 't_bat', 't_mode', 't_transport',
               't_sec', 't_path')
    restore = {k: orig_cfg[k] for k in touched if k in orig_cfg}
    (out / f'config_before_{args.scenario}.json').write_text(
        json.dumps(orig_cfg, indent=2))

    if os.path.exists(records):
        os.remove(records)
    sink = subprocess.Popen(
        [sys.executable, 'tools/telemetry_bench/server_http.py',
         '--port', str(args.sink_port), '--mode', 'ok',
         '--stats', str(out / f'sink_stats_{args.scenario}.json'),
         '--records', str(records)]
        + (['--tls'] if transport else []),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)

    stop = threading.Event()
    samples = []

    def hold_session():
        """Mantém a sessão TLS web viva (keep-alive) amostrando /api/status."""
        while not stop.is_set():
            try:
                r = w.get('/api/status')
                if r.status_code == 200:
                    j = r.json()
                    samples.append({
                        't': time.time(),
                        'uptime': j.get('uptime'),
                        'heap_f': j.get('heap_f'),
                        'heap_lb': j.get('heap_lb'),
                        'pending': j.get('pending'),
                    })
            except Exception as e:
                samples.append({'t': time.time(), 'err': str(e)[:80]})
            stop.wait(2.0)

    fails = []

    try:
        fields = {
            't_srv': '192.168.3.31',
            't_port': str(args.sink_port),
            't_int': '2000',
            't_bat': '10',
            't_mode': '0',
            't_transport': str(transport),
            't_sec': '0',
        }
        print(f'[1] commit telemetria {"TLS" if transport else "clara"} '
              f'-> sink :{args.sink_port} …')
        commit(fields)
        logs_before = len(fetch_logs(w))

        th = threading.Thread(target=hold_session, daemon=True)
        th.start()
        time.sleep(30)  # ≥ 15 ciclos de telemetria com a sessão viva

        live = [s for s in samples if 'heap_f' in s and isinstance(s.get('heap_f'), int)]
        heaps = [s['heap_f'] for s in live]
        uptimes = [s['uptime'] for s in live if s.get('uptime')]
        recs_during = count_records(records)
        print(f'    sessão viva 30 s: amostras={len(live)} '
              f'heap_f min/max={min(heaps) if heaps else "-"}/'
              f'{max(heaps) if heaps else "-"} '
              f'records no sink={recs_during}')

        if heaps and min(heaps) >= FLOOR[transport]:
            fails.append(f'sem pressão de heap: heap_f nunca < {FLOOR[transport]} '
                         f'(min {min(heaps)}) — pool TLS não ativo?')
        if len(uptimes) >= 2 and any(uptimes[i] < uptimes[i - 1] for i in range(1, len(uptimes))):
            fails.append('reboot detectado (uptime retrocedeu)')

        logs_now = fetch_logs(w)
        warns = tel_warns(logs_now, logs_before)
        print(f'    log [WRN][TEL] novos: {len(warns)}')
        for x in warns[:3]:
            print('      ' + x)

        if transport == 1:
            # TLS: o pré-voo deve segurar o envio (0 registros no sink)…
            if recs_during > 0:
                fails.append(f'telemetria TLS enviou {recs_during} registros com '
                             'a sessão web viva (esperado 0 — backoff)')
            # …e retomar no ciclo seguinte após fechar a sessão.
            stop.set()
            th.join(timeout=10)
            t_close = time.time()
            time.sleep(12)
            recs_after = count_records(records)
            if recs_after <= recs_during:
                fails.append(f'sem retomada após fechar a sessão '
                             f'({recs_during} -> {recs_after})')
            else:
                # quando chegou o 1º registro pós-fechamento
                first_after = None
                with open(records, encoding='utf-8') as fh:
                    lines = fh.readlines()
                if len(lines) > recs_during:
                    try:
                        first_after = json.loads(lines[recs_during]).get('t')
                    except Exception:
                        pass
                resume_s = round((first_after or t_close + 12) - t_close, 1)
                print(f'    retomada: +{recs_after - recs_during} registros, '
                      f'1º ~{resume_s}s após fechar')
        else:
            # claro: deve enviar mesmo com heap < 24 KB
            if recs_during == 0:
                fails.append('telemetria clara NÃO enviou com a sessão viva '
                             '(esperado envio com heap < 24 KB)')
            low_heaps = [h for h in heaps if h < 24576]
            if not low_heaps:
                fails.append('heap nunca ficou < 24 KB durante o envio — '
                             'o cenário não exercitou o gate')
            stop.set()
            th.join(timeout=10)

        print('[2] restauração da config original …')
        commit(restore)
        after = w.config()
        drift = [k for k in touched
                 if k in orig_cfg and k in after and str(orig_cfg[k]) != str(after[k])]
        if drift:
            fails.append(f'config não restaurada: {drift}')

        print(f'\nheap_gate_interaction [{args.scenario}]: '
              f'{"FAIL" if fails else "PASS"}')
        for f in fails:
            print('  FAIL: ' + f)
        return 1 if fails else 0
    finally:
        stop.set()
        sink.terminate()
        try:
            sink.wait(timeout=5)
        except subprocess.TimeoutExpired:
            sink.kill()


if __name__ == '__main__':
    raise SystemExit(main())
