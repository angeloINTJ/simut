#!/usr/bin/env python3
"""
run_telemetry_perf.py — teste de desempenho/stress de telemetria HTTP+HTTPS.

Pipeline (totalmente automatizado):
  1. Backup canonical do LFS atual (.bkp pra restaurar no fim).
  2. Para cada protocolo (HTTP:9080, HTTPS:9443):
     a. Configura tel via CLI serial: server, port, crypto, batch=250,
        interval=300ms, mode=json
     b. write memory + reload confirm
     c. Aguarda boot + login
     d. Gera N dias de history sintético via generate_history_v2.py
     e. Upload pra /history/ via /api/upload
     f. Reset cursor de telemetria via CLI 'tel reset'
     g. Monitora /api/status por DURATION_SEC, capturando métricas a cada 2s
     h. Salva CSV + métricas summary
  3. Restaura canonical via /api/restore?op=apply
  4. Gera relatório comparativo HTTP×HTTPS (markdown + HTML)

Uso:
    F9_PASS=<senha> python3 tools/stress_test/run_telemetry_perf.py
"""

import os
import sys
import time
import json
import hashlib
import struct
import subprocess
import urllib.parse
import urllib.request
import urllib.error
from datetime import datetime, timedelta

# ===== Config =====
SIMUT_IP        = os.getenv('SIMUT_IP', '192.168.3.195')
SIMUT_PORT      = os.getenv('SIMUT_PORT', '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00')
USER_PWD        = os.getenv('F9_PASS', '^çarrando.1a')
TEL_SERVER      = os.getenv('TEL_SERVER', '192.168.3.206')
TEL_BATCH       = int(os.getenv('TEL_BATCH', '250'))
TEL_INTERVAL_MS = int(os.getenv('TEL_INTERVAL_MS', '300'))
DAYS            = int(os.getenv('DAYS', '15'))     # ~330 KB de history sintético
DURATION_SEC    = int(os.getenv('DURATION_SEC', '90'))  # tempo de monitoramento por protocolo
SAMPLE_INTERVAL = int(os.getenv('SAMPLE_INTERVAL', '2'))

TS = datetime.now().strftime('%Y%m%d-%H%M%S')
REPORT_DIR = f'docs/test_reports/telemetry_perf_{TS}'
os.makedirs(REPORT_DIR, exist_ok=True)

PROTOCOLS = [
    ('HTTP',  9080, 'off'),
    ('HTTPS', 9443, 'on'),
]

# ===== HTTP API =====
sha256 = lambda s: hashlib.sha256(s.encode('utf-8')).hexdigest()

class SimutAPI:
    def __init__(self, ip):
        self.ip = ip
        self.cookie = None

    def _url(self, path):
        return f'http://{self.ip}{path}'

    def _req(self, path, method='GET', data=None, files=None, timeout=30):
        import requests
        kw = {'timeout': timeout}
        if self.cookie:
            kw['cookies'] = {'SIMUTSESS': self.cookie}
        if data:
            kw['data'] = data
        if files:
            kw['files'] = files
        if method == 'GET':
            return requests.get(self._url(path), **kw)
        return requests.post(self._url(path), **kw)

    def login(self, pwd):
        import requests
        s = requests.Session()
        nonce = s.get(self._url('/api/login_init'), timeout=5).json()['nonce']
        r = s.post(self._url('/api/login'),
                   data={'user':'admin','pass':sha256(pwd),'nonce':nonce}, timeout=10)
        if r.status_code != 200:
            return False
        for c in s.cookies:
            if c.name == 'SIMUTSESS':
                self.cookie = c.value
                return True
        return False

    def status(self):
        try:
            r = self._req('/api/status', timeout=10)
            if r.status_code != 200: return None
            return r.json()
        except Exception:
            return None

    def info(self):
        try:
            r = self._req('/api/info', timeout=10)
            if r.status_code != 200: return None
            # /api/info returns 2 JSONs concatenated — parse first only
            t = r.text
            depth = 0; end = 0
            for i, ch in enumerate(t):
                if ch == '{': depth += 1
                elif ch == '}':
                    depth -= 1
                    if depth == 0:
                        end = i+1; break
            return json.loads(t[:end])
        except Exception:
            return None

    def upload(self, local_path, remote_dir):
        import requests
        name = os.path.basename(local_path)
        url = self._url(f'/api/upload?uploadDir={urllib.parse.quote(remote_dir)}')
        with open(local_path, 'rb') as f:
            r = self._req(f'/api/upload?uploadDir={urllib.parse.quote(remote_dir)}',
                          method='POST',
                          files={'file': (name, f, 'application/octet-stream')},
                          timeout=60)
        return r.status_code == 200

    def backup(self, out_path):
        import requests
        kw = {'timeout': 240, 'stream': True}
        if self.cookie: kw['cookies'] = {'SIMUTSESS': self.cookie}
        r = requests.get(self._url('/api/backup'), **kw)
        if r.status_code != 200: return False
        total = 0
        with open(out_path, 'wb') as f:
            for chunk in r.iter_content(8192):
                f.write(chunk); total += len(chunk)
        return total

    def restore(self, bkp_path):
        import requests
        with open(bkp_path,'rb') as f: data = f.read()
        kw = {'timeout': 90}
        if self.cookie: kw['cookies'] = {'SIMUTSESS': self.cookie}
        # Validate first
        files = {'file': ('canonical.bkp', data, 'application/octet-stream')}
        r = requests.post(self._url('/api/restore?op=validate'), files=files, **kw)
        if r.status_code != 200:
            return False, f'validate {r.status_code}'
        # Apply (tolera ConnReset por reboot)
        try:
            files = {'file': ('canonical.bkp', data, 'application/octet-stream')}
            r = requests.post(self._url('/api/restore?op=apply'), files=files, **kw)
            return True, f'apply {r.status_code}'
        except (requests.exceptions.ConnectionError, requests.exceptions.ChunkedEncodingError):
            return True, 'apply ConnReset (auto-reboot)'

# ===== Serial CLI =====
def kill_serial_holders():
    try:
        out = subprocess.check_output(['lsof', SIMUT_PORT], stderr=subprocess.DEVNULL).decode()
        for line in out.splitlines()[1:]:
            pid = line.split()[1]
            subprocess.call(['kill', '-9', pid])
        time.sleep(1)
    except subprocess.CalledProcessError:
        pass

def cli(cmd, wait_s=1.5, debug=False, tolerant=False):
    """tolerant=True: comandos que rebooam (reload confirm) — ignore exceções."""
    import serial
    kill_serial_holders()
    p = serial.Serial(SIMUT_PORT, 115200, timeout=0.5, write_timeout=4)
    time.sleep(0.3)
    try: p.reset_input_buffer()
    except Exception:
        if tolerant: pass
        else: raise
    p.write(cmd.encode() + b'\r\n')
    time.sleep(wait_s)
    try:
        out = p.read(8192).decode('utf-8', 'replace')
    except Exception:
        out = ''
        if not tolerant: raise
    try: p.close()
    except Exception: pass
    if debug: print(f'  cli({cmd!r}) → {out.strip()[-100:]!r}')
    return out

def cli_seq(cmds, wait_s=1.5):
    """Send multiple commands in a single session (avoids reopening serial each time)."""
    import serial
    kill_serial_holders()
    p = serial.Serial(SIMUT_PORT, 115200, timeout=0.5, write_timeout=4)
    time.sleep(0.3); p.reset_input_buffer()
    out = ''
    for c in cmds:
        p.write(c.encode() + b'\r\n')
        time.sleep(wait_s)
        try: out += p.read(4096).decode('utf-8', 'replace')
        except Exception: pass
    p.close()
    return out

def wait_http(timeout=180):
    import requests
    start = time.time()
    while time.time() - start < timeout:
        try:
            r = requests.get(f'http://{SIMUT_IP}/api/login_init', timeout=3)
            if r.status_code == 200: return True
        except Exception: pass
        time.sleep(3)
    return False

# ===== Telemetry config phase =====
def configure_telemetry(proto, port, crypto):
    """Set telemetry params via CLI + reload."""
    print(f'\n[CONFIG] {proto} port={port} crypto={crypto} batch={TEL_BATCH} interval={TEL_INTERVAL_MS}ms')
    cmds = [
        '',
        f'conf tel server {TEL_SERVER}',
        f'conf tel port {port}',
        f'conf tel path /api/v1/data',
        f'conf tel batch {TEL_BATCH}',
        f'conf tel interval {TEL_INTERVAL_MS}',
        f'conf tel crypto {crypto}',
        f'conf tel mode json',
        'write memory',
    ]
    out = cli_seq(cmds, wait_s=1.5)
    if 'OK' not in out:
        print(f'  ⚠️ unexpected CLI output (last 200 chars): ...{out[-200:]}')
    print('  → reload confirm')
    cli('reload confirm', wait_s=2.0, tolerant=True)
    print('  → aguardando boot HTTP up...')
    if not wait_http(180):
        print('  ❌ HTTP não voltou em 180s')
        return False
    print(f'  ✓ HTTP up')
    return True

# ===== History generation =====
def gen_history(days, out_dir):
    """Reuses tools/stress_test/generate_history_v2.py."""
    print(f'\n[GEN] {days} dias de history sintético → {out_dir}')
    os.makedirs(out_dir, exist_ok=True)
    rc = subprocess.call([
        'python3', 'tools/stress_test/generate_history_v2.py',
        '--days', str(days),
        '--records-per-day', '1440',
        '--output-dir', out_dir,
        '--quiet',
    ])
    if rc != 0:
        print(f'  ❌ generate_history_v2 rc={rc}')
        return None
    files = sorted(f for f in os.listdir(out_dir) if f.endswith('.bin'))
    total = sum(os.path.getsize(f'{out_dir}/{f}') for f in files)
    print(f'  ✓ {len(files)} arquivos, {total} B ({total//1024} KB)')
    return files

# ===== Upload + monitor =====
def cleanup_history(api):
    """Apaga /history/*.bin pra LFS não estourar entre fases. Retorna count."""
    print('\n[CLEANUP] limpando /history/* pra liberar LFS')
    import requests
    kw = {'timeout': 30}
    if api.cookie: kw['cookies'] = {'SIMUTSESS': api.cookie}
    try:
        r = requests.get(api._url('/api/ls?dir=/history/'), **kw)
        j = r.json()
    except Exception as e:
        print(f'  ⚠️ ls falhou: {e}')
        return 0
    n = 0
    for ent in j.get('entries', []):
        if ent.get('t') == 'f' and ent.get('n','').endswith('.bin'):
            path = f'/history/{ent["n"]}'
            try:
                rd = requests.post(api._url(f'/api/delete?file={path}'), **kw)
                if rd.status_code == 200: n += 1
            except Exception: pass
    print(f'  ✓ {n} arquivos removidos')
    time.sleep(2)
    return n

def upload_history(api, src_dir, files):
    print(f'\n[UPLOAD] {len(files)} arquivos → /history/')
    ok = fail = 0
    t0 = time.time()
    for f in files:
        if api.upload(f'{src_dir}/{f}', '/history'): ok += 1
        else: fail += 1
    print(f'  ✓ {ok} OK / {fail} FAIL em {time.time()-t0:.1f}s')
    return ok, fail

def reset_tel_cursor():
    print('\n[RESET] tel reset')
    out = cli('tel reset', wait_s=2.5)
    if 'reset' in out.lower() or 'cursor' in out.lower():
        print('  ✓ cursor resetado')
    else:
        print(f'  ⚠️ resposta: {out.strip()[-100:]}')

HEAP_ABORT_THRESHOLD = 15000  # abort se heap_f < 15 KB (zona de risco OOM)
HEAP_WARN_THRESHOLD  = 25000  # warn

def monitor_telemetry(api, duration_s, sample_int, csv_path):
    print(f'\n[MONITOR] {duration_s}s amostrando a cada {sample_int}s')
    print(f'  Safety: abort se heap_f < {HEAP_ABORT_THRESHOLD} B')
    samples = []
    s0 = api.status()
    if not s0:
        print('  ❌ status inicial falhou'); return None
    sent0 = s0.get('metr',{}).get('ts', 0)
    bytes0 = s0.get('metr',{}).get('tb', 0)
    heap0 = s0.get('sys',{}).get('heap_f', 0)
    print(f'  inicial: sent={sent0} bytes={bytes0} heap={heap0}B')

    if heap0 < HEAP_ABORT_THRESHOLD:
        print(f'  ⛔ heap inicial {heap0} < {HEAP_ABORT_THRESHOLD} — abort safety')
        return {'samples':[], 'aborted':True, 'reason':'heap_initial_low',
                'sent_delta':0, 'bytes_delta':0, 'fail_total':0, 'pending_final':0,
                'heap_final':heap0, 'duration':0}

    start = time.time()
    last_sent = sent0; last_bytes = bytes0; last_ts = start
    aborted = False; abort_reason = None
    with open(csv_path, 'w') as f:
        f.write('elapsed_s,sent,fail,bytes,pending,heap_free,heap_min,heap_lb,rate_msg_s,rate_kbps,last_lat_ms\n')
        while time.time() - start < duration_s:
            time.sleep(sample_int)
            now = time.time()
            elapsed = now - start
            st = api.status()
            if not st:
                f.write(f'{elapsed:.1f},,,,,,,,,,\n'); continue
            m = st.get('metr', {})
            s = st.get('sys', {})
            sent = m.get('ts', 0)
            fail = m.get('tf', 0)
            bytes_ = m.get('tb', 0)
            pending = s.get('pending', 0)
            heap_f = s.get('heap_f', 0)
            heap_m = s.get('heap_min', 0)
            heap_lb = s.get('heap_lb', 0)
            last_lat = m.get('tl', 0)
            d_sent = sent - last_sent
            d_bytes = bytes_ - last_bytes
            dt = now - last_ts
            rate_msg = d_sent / dt if dt > 0 else 0
            rate_kbps = (d_bytes * 8 / 1000) / dt if dt > 0 else 0
            f.write(f'{elapsed:.1f},{sent},{fail},{bytes_},{pending},{heap_f},{heap_m},{heap_lb},{rate_msg:.2f},{rate_kbps:.2f},{last_lat}\n')
            f.flush()
            warn_flag = ' ⚠️' if heap_f < HEAP_WARN_THRESHOLD else ''
            print(f'    {elapsed:5.1f}s | sent={sent} fail={fail} pend={pending} heap={heap_f}B rate={rate_msg:.1f}msg/s {rate_kbps:.1f}kbps lat={last_lat}ms{warn_flag}')
            samples.append({
                'elapsed': elapsed, 'sent': sent, 'fail': fail, 'bytes': bytes_,
                'pending': pending, 'heap_f': heap_f, 'rate_msg': rate_msg,
                'rate_kbps': rate_kbps, 'lat_ms': last_lat,
            })
            # SAFETY CHECK
            if heap_f < HEAP_ABORT_THRESHOLD and heap_f > 0:
                aborted = True
                abort_reason = f'heap_f={heap_f} < {HEAP_ABORT_THRESHOLD}'
                print(f'  ⛔ ABORT: {abort_reason}')
                break
            last_sent = sent; last_bytes = bytes_; last_ts = now

    sf = api.status()
    final = {
        'sent_delta': (sf.get('metr',{}).get('ts',0) - sent0) if sf else 0,
        'bytes_delta': (sf.get('metr',{}).get('tb',0) - bytes0) if sf else 0,
        'fail_total': sf.get('metr',{}).get('tf',0) if sf else 0,
        'pending_final': sf.get('sys',{}).get('pending',0) if sf else 0,
        'heap_final': sf.get('sys',{}).get('heap_f',0) if sf else 0,
        'heap_initial': heap0,
        'duration': time.time()-start,
        'aborted': aborted,
        'abort_reason': abort_reason,
        'samples': samples,
    }
    status_tag = '⛔ ABORTED' if aborted else '✓ FINAL'
    print(f'  {status_tag}: Δsent={final["sent_delta"]} Δbytes={final["bytes_delta"]} fail={final["fail_total"]} pending={final["pending_final"]} heap_end={final["heap_final"]}B')
    return final

# ===== Report =====
def gen_report(report_dir, http_metrics, https_metrics, canonical_size):
    md = f"""# Telemetry Performance Test — Relatório

**Data:** {datetime.now().strftime('%Y-%m-%d %H:%M')}
**SIMUT IP:** {SIMUT_IP}
**Telemetry server alvo:** {TEL_SERVER}
**Configuração:** batch={TEL_BATCH}, interval={TEL_INTERVAL_MS}ms, mode=json
**History sintético:** {DAYS} dias (~{DAYS*22} KB upload pra `/history/`)
**Duração de monitoramento:** {DURATION_SEC}s por protocolo

---

## Resultados comparativos

| Métrica | HTTP (9080) | HTTPS (9443) |
|---|---|---|
| Mensagens enviadas | {http_metrics['sent_delta'] if http_metrics else 'N/A'} | {https_metrics['sent_delta'] if https_metrics else 'N/A'} |
| Bytes enviados | {http_metrics['bytes_delta'] if http_metrics else 'N/A'} | {https_metrics['bytes_delta'] if https_metrics else 'N/A'} |
| Falhas | {http_metrics['fail_total'] if http_metrics else 'N/A'} | {https_metrics['fail_total'] if https_metrics else 'N/A'} |
| Pending no fim | {http_metrics['pending_final'] if http_metrics else 'N/A'} | {https_metrics['pending_final'] if https_metrics else 'N/A'} |
| Heap livre no fim | {http_metrics['heap_final'] if http_metrics else 'N/A'} B | {https_metrics['heap_final'] if https_metrics else 'N/A'} B |

### Throughput (médio durante monitoramento)
"""
    for label, m in [('HTTP', http_metrics), ('HTTPS', https_metrics)]:
        if not m:
            md += f'\n**{label}:** sem dados\n'; continue
        rates_msg = [s['rate_msg'] for s in m['samples'] if s['rate_msg'] > 0]
        rates_kbps = [s['rate_kbps'] for s in m['samples'] if s['rate_kbps'] > 0]
        lats = [s['lat_ms'] for s in m['samples'] if s['lat_ms'] > 0]
        md += f"\n**{label}:**\n"
        if rates_msg:
            md += f"- Rate msg/s: avg={sum(rates_msg)/len(rates_msg):.2f}, max={max(rates_msg):.2f}, min={min(rates_msg):.2f}\n"
        if rates_kbps:
            md += f"- Rate kbps:  avg={sum(rates_kbps)/len(rates_kbps):.2f}, max={max(rates_kbps):.2f}\n"
        if lats:
            md += f"- Latência ms: avg={sum(lats)/len(lats):.0f}, max={max(lats)}, min={min(lats)}\n"
        else:
            md += '- Latência: sem amostras válidas (0 envios bem-sucedidos)\n'

    md += f"""

---

## Configuração testada

```
conf tel server {TEL_SERVER}
conf tel port <9080|9443>
conf tel path /api/v1/data
conf tel batch {TEL_BATCH}
conf tel interval {TEL_INTERVAL_MS}
conf tel crypto <off|on>
conf tel mode json
```

## Arquivos gerados

- `drain_http.csv` — amostras HTTP a cada {SAMPLE_INTERVAL}s
- `drain_https.csv` — amostras HTTPS a cada {SAMPLE_INTERVAL}s
- `report.md` (este arquivo)
- `report.html`

## Restauração

LFS canonical ({canonical_size} B) restaurado via `/api/restore?op=apply`. SIMUT
retornado ao estado pré-teste (config telemetria + history original).

## Notas

- Servidor de telemetria alvo (`{TEL_SERVER}`) pode estar offline/inacessível —
  nesse caso, "Falhas" será alto e "Mensagens enviadas" baixo. O teste então
  mede a resiliência do SIMUT a backoff/retry, não throughput real.
- Intervalo de 300ms é agressivo (2-10× mais rápido que produção típica).
  Stress no buffer + heap. Heap livre é o indicador-chave de saúde.
- Pending crescente = SIMUT acumulando registros que não consegue enviar.
  Esperado se servidor inacessível.
"""

    md_path = f'{report_dir}/report.md'
    with open(md_path, 'w') as f: f.write(md)

    # HTML via pandoc
    html_path = f'{report_dir}/report.html'
    rc = subprocess.call([
        'pandoc', md_path, '-o', html_path,
        '--standalone', '--toc',
        '--css=https://cdn.jsdelivr.net/npm/water.css@2/out/water.css',
        '--metadata', f'title=Telemetry Perf Report {TS}',
    ])
    print(f'\n[REPORT] {md_path}')
    if rc == 0: print(f'         {html_path}')
    return md_path, html_path

# ===== Main =====
def main():
    print(f'═══ Telemetry Perf Test — {TS} ═══')
    print(f'  SIMUT: {SIMUT_IP}')
    print(f'  Tel server: {TEL_SERVER}')
    print(f'  Batch: {TEL_BATCH}, interval: {TEL_INTERVAL_MS}ms')
    print(f'  Days history: {DAYS}, monitor: {DURATION_SEC}s/protocol')
    print(f'  Report dir: {REPORT_DIR}\n')

    api = SimutAPI(SIMUT_IP)
    print('[INIT] aguardando HTTP up (até 180s)...')
    if not wait_http(180):
        print('FATAL: device offline'); return 1
    if not api.login(USER_PWD):
        print('FATAL: login failed (rever senha)'); return 1

    # ─── Backup canonical ───
    canonical_path = f'{REPORT_DIR}/canonical.bkp'
    sz = api.backup(canonical_path)
    if not sz: print('FATAL: backup falhou'); return 1
    print(f'[BACKUP] canonical: {sz} B → {canonical_path}')

    # ─── Per-protocol stress ───
    metrics = {}
    for proto, port, crypto in PROTOCOLS:
        print(f'\n══ FASE: {proto} (port {port}, crypto={crypto}) ══')
        if not configure_telemetry(proto, port, crypto):
            metrics[proto] = None; continue
        # Re-login pós-reload
        if not api.login(USER_PWD):
            print(f'  ❌ login pós-reload falhou'); metrics[proto] = None; continue

        # Desabilita telemetria ANTES do upload — TLS handshake (~16 KB RAM)
        # + upload simultâneo estoura heap em HTTPS. Reabilita depois.
        print('  → desabilitando telemetria pre-upload (interval 0) — evita OOM TLS+upload')
        cli_seq(['conf tel interval 0','write memory'], wait_s=1.5)
        time.sleep(3)

        # Cleanup /history/ entre fases — sem isso, HTTP+HTTPS cumulativo
        # estoura LFS (15 dias × 22KB × 2 fases = 660KB sobre user data).
        cleanup_history(api)

        gen_dir = f'{REPORT_DIR}/gen_{proto.lower()}'
        files = gen_history(DAYS, gen_dir)
        if files: upload_history(api, gen_dir, files)

        # Reabilita telemetria com config alvo
        print(f'  → reabilitando telemetria (interval {TEL_INTERVAL_MS}ms)')
        cli_seq([f'conf tel interval {TEL_INTERVAL_MS}','write memory'], wait_s=1.5)
        time.sleep(2)

        reset_tel_cursor()
        time.sleep(2)

        csv_path = f'{REPORT_DIR}/drain_{proto.lower()}.csv'
        m = monitor_telemetry(api, DURATION_SEC, SAMPLE_INTERVAL, csv_path)
        metrics[proto] = m

        # Safety: pare imediatamente telemetria pra liberar heap antes da próxima fase
        print('  → desabilitando telemetria pós-fase (interval 0)')
        cli_seq(['conf tel interval 0','write memory'], wait_s=1.5)
        time.sleep(2)

        # Se abortou, restaurar canonical agora (não esperar fim)
        if m and m.get('aborted'):
            print(f'  ⛔ Fase {proto} abortou — interrompendo demais fases pra restaurar')
            break

    # ─── Restore canonical ───
    print(f'\n══ RESTORE canonical ══')
    if not api.login(USER_PWD): print('  re-login...')
    ok, msg = api.restore(canonical_path)
    print(f'  restore: {msg}')
    print('  aguardando boot pós-restore...')
    wait_http(120)
    print('  ✓ HTTP up')

    # ─── Reset telemetry config to sane defaults ───
    print('\n══ Reset config telemetria pra defaults ══')
    cli_seq([
        'conf tel interval 0',  # disable
        'write memory',
    ], wait_s=1.5)

    # ─── Report ───
    md, html = gen_report(REPORT_DIR, metrics.get('HTTP'), metrics.get('HTTPS'), sz)

    print(f'\n═══ DONE ═══')
    print(f'  Relatório: {md}')
    print(f'  HTML:      {html}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
