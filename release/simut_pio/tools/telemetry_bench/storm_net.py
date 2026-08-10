#!/usr/bin/env python3
"""storm_net.py — the combined network storm.

The pieces to break the device three ways already existed and had each been
run on its own: telemetry fault injection (phase_survive.py), a concurrent web
hammer (scratchpad/storm502.py), and the sensors, which never stop. What no
run ever did was overlap them, and the overlap is where the interesting
failures live — a flash write racing Core 1's heartbeat needs both a storm of
writes and something holding Core 0.

So: one sink that misbehaves in a named way, a web hammer that never lets up,
and the sensors underneath, all at once. Survival is the measurement, and it
is not "did it answer" — it is uptime never going backwards, `metr.fx` staying
0, PBUF failures staying 0, the telemetry cursor advancing only on success,
and the device still being on the network when the host checks from outside.

Ownership, because two owners of one serial port is a lost run: `Target` owns
/dev/ttyACM*, full stop. The web threads speak HTTP only. Everything that
needs the CLI goes through `serial_cmd()`, which holds a lock — `Target.send`
collects its reply by watching the shared line buffer, so two callers at once
would each read the other's output.

Usage:
  python3 storm_net.py --minutes 90 --faults all --web-load on --out results/netstorm
"""
import argparse
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import requests  # noqa: E402
from bench import Target, Server, kill_stale, wait_web, HOST_IP  # noqa: E402

DEV = '192.168.3.24'
PORT_HTTP, PORT_HTTPS = 18080, 18443

# bench.Server launches the sink with cwd=tools/telemetry_bench, but the bench
# certificate pair lives in the scratchpad copy. Relative paths would leave
# every TLS window with a sink that dies on load_cert_chain — resolve once,
# absolutely, and let it fail loudly here instead.
CERT_DIR = os.environ.get(
    'SIMUT_BENCH_CERTS',
    '/home/angelo/Documentos/simut/scratchpad/telbench/certs')
CERT = os.path.join(CERT_DIR, 'cert.pem')
KEY = os.path.join(CERT_DIR, 'key.pem')

# The device is quicker to answer than to be believed: /api/status walks the
# heap 16 times (sampleLargestBlock) and takes one live RSSI ioctl per call,
# so polling it fast is itself a load. 3 s is the floor the plan settled on.
STATUS_PERIOD = 3.0
SERIAL_PERIOD = 30.0
PING_PERIOD = 5.0

stop_all = threading.Event()
serial_lock = threading.Lock()
sess_lock = threading.Lock()

TOK = {'v': None}
samples = []          # /api/status rows
serial_samples = []   # show metrics / show net status rows
host_samples = []     # ping + arp rows
events = []           # anything worth a line in the report
samples_lock = threading.Lock()

WEB = {'downloads': 0, 'dl_http_fail': 0, 'dl_json_fail': 0,
       'status_polls': 0, 'status_fail': 0, 'uploads': 0, 'upload_fail': 0}


def log(msg):
    print(f'[{time.strftime("%H:%M:%S")}] {msg}', flush=True)


def note(kind, msg):
    with samples_lock:
        events.append({'t': time.time(), 'kind': kind, 'msg': msg})
    log(f'  !! {kind}: {msg}')


# ---------------------------------------------------------------------------
# session
# ---------------------------------------------------------------------------

def sha256_frontend(password):
    return hashlib.sha256(password.encode('latin-1')).hexdigest()


def login(user, password):
    """Nonce-bound login, serialized: the device binds the nonce per client IP,
    so two logins racing each other both lose."""
    s = requests.Session()
    r = s.get(f'http://{DEV}/api/login_init', timeout=10)
    nonce = r.json().get('nonce', '')
    s.post(f'http://{DEV}/api/login',
           data={'user': user, 'pass': sha256_frontend(password),
                 'nonce': nonce},
           headers={'Content-Type': 'application/x-www-form-urlencoded'},
           timeout=10, allow_redirects=False)
    tok = s.cookies.get('SIMUTSESS')
    if not tok:
        raise RuntimeError('login produced no session cookie')
    return tok


def refresh_session(tag, user, password):
    with sess_lock:
        for attempt in range(6):
            try:
                TOK['v'] = login(user, password)
                note('session', f'{tag}: renewed')
                return True
            except Exception as e:
                time.sleep(2 + attempt)
        note('session', f'{tag}: gave up re-logging in')
        return False


class Client:
    """Fixed cookie header rather than a requests jar.

    Error responses from the device carry `Set-Cookie: SIMUTSESS=0`; a jar
    shared between threads takes that as gospel and empties itself, so the
    hammer starts reporting auth failures that are its own doing."""
    base = f'http://{DEV}'

    def _h(self, kw):
        h = kw.pop('headers', {})
        h['Cookie'] = f"SIMUTSESS={TOK['v']}"
        return h

    def get(self, path, **kw):
        kw.setdefault('timeout', 30)
        return requests.get(self.base + path, headers=self._h(kw), **kw)

    def post(self, path, **kw):
        kw.setdefault('timeout', 30)
        return requests.post(self.base + path, headers=self._h(kw), **kw)


# ---------------------------------------------------------------------------
# serial (Target is the only owner)
# ---------------------------------------------------------------------------

def serial_cmd(t, cmd, wait=2.5):
    with serial_lock:
        return t.send(cmd, wait=wait)


METRIC_PATS = {
    'uptime_s': (r'Uptime:\s*(\d+):(\d+):(\d+)', 'hms'),
    'heap': (r'Heap:\s*(\d+)\s*B', 'int'),
    'largest': (r'Maior bloco:\s*(\d+)\s*B', 'int'),
    'flash_ops': (r'Flash ops:\s*(\d+)', 'int'),
    'core1_exposed': (r'Core1 exposto:\s*(\d+)\s*ops', 'int'),
    'core1_hb_ms': (r'Heartbeat:\s*(\d+)\s*ms', 'int'),
    'core1_running': (r'Rodando:\s*(\d+)', 'int'),
    # The plan's Core-1 assertion: health kills must stay 0. `show metrics`
    # prints it as "Kills lockout: N | saude: M | quiet: K".
    'core1_kills_lockout': (r'Kills lockout:\s*(\d+)', 'int'),
    'core1_kills_health': (r'Kills lockout:\s*\d+\s*\|\s*saude:\s*(\d+)', 'int'),
    'core1_kills_quiet': (r'saude:\s*\d+\s*\|\s*quiet:\s*(\d+)', 'int'),
    'lockout_stuck': (r'Lockout travado:\s*(\d+)', 'int'),
    'irqoff_max_us': (r'IRQ-off max:\s*(\d+)\s*us', 'int'),
}

PBUF_PAT = re.compile(
    r'PBUF pool:\s*(\d+) em uso / pico (\d+) / (\d+) total, (\d+) falhas')
RSSI_PAT = re.compile(r'RSSI:\s*(-?\d+)\s*dBm')


def parse_metrics(txt):
    d = {}
    for key, (pat, kind) in METRIC_PATS.items():
        m = re.search(pat, txt)
        if not m:
            d[key] = None
        elif kind == 'hms':
            d[key] = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
        else:
            d[key] = int(m.group(1))
    return d


def serial_sampler(t):
    """Two commands the web API cannot answer: the PBUF pool (D14) and the
    Core-1 heartbeat. Both cost real time on the device, so they run on a slow
    cadence and never inside a tight loop."""
    while not stop_all.is_set():
        try:
            mtxt = serial_cmd(t, 'show metrics', wait=4.0)
            ntxt = serial_cmd(t, 'show net status', wait=3.0)
            row = parse_metrics(mtxt)
            m = PBUF_PAT.search(ntxt)
            if m:
                row['pbuf_used'] = int(m.group(1))
                row['pbuf_peak'] = int(m.group(2))
                row['pbuf_total'] = int(m.group(3))
                row['pbuf_fail'] = int(m.group(4))
            r = RSSI_PAT.search(ntxt)
            row['rssi'] = int(r.group(1)) if r else None
            row['t'] = time.time()
            row['fault'] = CURRENT['fault']
            with samples_lock:
                serial_samples.append(row)
            if row.get('pbuf_fail'):
                note('PBUF', f"pool failures = {row['pbuf_fail']} (D14)")
            if row.get('core1_kills_health'):
                note('CORE1', f"health kills = {row['core1_kills_health']}")
        except Exception as e:
            note('serial', f'{type(e).__name__}: {e}')
        stop_all.wait(SERIAL_PERIOD)


# ---------------------------------------------------------------------------
# host-side liveness — the device's own report cannot catch D15
# ---------------------------------------------------------------------------

def host_liveness():
    """A device that has silently fallen off the network still answers its own
    health question with "fine" — it has no way to know. Ask from outside."""
    while not stop_all.is_set():
        row = {'t': time.time(), 'fault': CURRENT['fault']}
        try:
            p = subprocess.run(['ping', '-c', '1', '-W', '2', DEV],
                               capture_output=True, timeout=6)
            row['ping'] = (p.returncode == 0)
        except Exception:
            row['ping'] = False
        try:
            n = subprocess.run(['ip', 'neigh', 'show', DEV],
                               capture_output=True, text=True, timeout=4)
            row['arp'] = n.stdout.strip()
            row['arp_incomplete'] = 'INCOMPLETE' in n.stdout
        except Exception:
            row['arp'] = ''
            row['arp_incomplete'] = False
        with samples_lock:
            host_samples.append(row)
        if not row['ping']:
            note('HOST', 'ping lost')
        if row['arp_incomplete']:
            note('HOST', 'ARP INCOMPLETE (D15)')
        stop_all.wait(PING_PERIOD)


# ---------------------------------------------------------------------------
# assertions from /api/status
# ---------------------------------------------------------------------------

CURRENT = {'fault': 'idle'}


def status_sampler(user, password):
    last_up = 0
    while not stop_all.is_set():
        row = {'t': time.time(), 'fault': CURRENT['fault']}
        try:
            c = Client()
            r = c.get('/api/status', timeout=15)
            if r.status_code != 200:
                row['http'] = r.status_code
                WEB['status_fail'] += 1
                if r.status_code in (401, 403, 302):
                    refresh_session('status', user, password)
            else:
                st = r.json()
                row.update({
                    'uptime': st['sys']['uptime'],
                    'heap_f': st['sys']['heap_f'],
                    'pending': st['sys']['pending'],
                    'fs_u': st['sys']['fs_u'],
                })
                row.update({f'm_{k}': v for k, v in st['metr'].items()})
                WEB['status_polls'] += 1
                up = st['sys']['uptime']
                if up < last_up:
                    note('REBOOT', f'uptime {last_up} -> {up}')
                last_up = up
                if st['metr'].get('fx'):
                    note('fx', f"flash exposed = {st['metr']['fx']} "
                               f"(a write without Core1FlashPause)")
                if st['metr'].get('cgg'):
                    note('cgg', f"SendGuard latch = {st['metr']['cgg']}")
        except Exception as e:
            row['err'] = type(e).__name__
            WEB['status_fail'] += 1
        with samples_lock:
            samples.append(row)
        stop_all.wait(STATUS_PERIOD)


# ---------------------------------------------------------------------------
# web hammer (HTTP only — the serial belongs to Target)
# ---------------------------------------------------------------------------

def downloads(user, password):
    """History downloads on rotating ranges, JSON-checked.

    A truncated tail is not a death: the handler has a 15 s deadline that cuts
    long streams, which shows up as invalid JSON at the end. Counted apart from
    an HTTP failure for exactly that reason."""
    c = Client()
    DAY = 86400
    now = int(time.time())
    jobs = [('1', ''), ('3', '')]
    for k in range(4):
        jobs.append(('6', f'&from={now - (k + 1) * 7 * DAY}&to={now - k * DAY * 7}'))
    modes = ['decode', 'envelope']
    i = 0
    while not stop_all.is_set():
        rng, win = jobs[i % len(jobs)]
        mode = modes[i % len(modes)]
        i += 1
        try:
            r = c.get(f'/api/history_multi?range={rng}&mode={mode}{win}',
                      timeout=60)
            if r.status_code != 200:
                WEB['dl_http_fail'] += 1
                if r.status_code in (401, 403, 302):
                    refresh_session('downloads', user, password)
            else:
                WEB['downloads'] += 1
                try:
                    json.loads(r.text)
                except Exception:
                    WEB['dl_json_fail'] += 1
        except Exception:
            WEB['dl_http_fail'] += 1
            time.sleep(1.5)
        stop_all.wait(0.4)


def upload_churn(user, password, blob_kb):
    """Flash-write pressure concurrent with streaming — the shape that puts a
    write next to Core 1's heartbeat."""
    c = Client()
    blob = os.urandom(blob_kb * 1024)
    while not stop_all.is_set():
        try:
            c.post('/api/upload',
                   files={'file': ('/storm.bin', io.BytesIO(blob),
                                   'application/octet-stream')},
                   timeout=60)
            WEB['uploads'] += 1
            stop_all.wait(1.0)
            c.post('/api/delete', data={'file': '/storm.bin'},
                   headers={'Content-Type': 'application/x-www-form-urlencoded'},
                   timeout=30)
        except Exception:
            WEB['upload_fail'] += 1
        stop_all.wait(2.0)


# ---------------------------------------------------------------------------
# device telemetry config
# ---------------------------------------------------------------------------

def read_tel_cfg():
    """What the device actually has, not what it was told."""
    try:
        r = Client().get('/api/config', timeout=15)
        if r.status_code != 200:
            return None
        c = r.json()
        return {'srv': c.get('t_srv'), 'port': c.get('t_port'),
                'sec': bool(c.get('t_sec')), 'bat': c.get('t_bat')}
    except Exception:
        return None


def cfg_tel(t, server, port, crypto, batch=10, interval=1000, mode='json',
            path='/ingest', tries=4):
    """Point telemetry at the sink over the CLI, and CONFIRM it took.

    Sending and moving on is not enough. A reboot landing inside this sequence
    swallows the rest of it — `write memory` never runs, the device keeps the
    previous transport, and every window afterwards measures a device aimed at
    a port nobody is listening on. That happened once for real, at the
    HTTP-to-TLS handover, and it silently voided the whole TLS half of a run:
    the sink logged `listening on 0.0.0.0:18443` while the device still had
    port 18080, crypto off. Nothing in the verdicts said so — the windows just
    reported failures, which is what a broken server is supposed to look like.

    So: send, read the config back over HTTP, and retry the whole sequence
    until it matches. Verified once here beats a whole phase of plausible
    nonsense."""
    cmds = ['enable', 'configure terminal',
            f'tel server {server}', f'tel port {port}',
            f'tel crypto {"on" if crypto else "off"}',
            f'tel path {path}', f'tel batch {batch}',
            f'tel interval {interval}', f'tel mode {mode}',
            'end', 'write memory']
    want = {'srv': server, 'port': int(port), 'sec': bool(crypto),
            'bat': int(batch)}

    for attempt in range(tries):
        for cmd in cmds:
            serial_cmd(t, cmd, wait=3.5 if cmd == 'write memory' else 1.8)
        time.sleep(1.0)
        got = read_tel_cfg()
        if got is None:
            # No answer usually means it is rebooting under us — exactly the
            # case that used to slip through.
            note('cfg', f'sem resposta apos configurar (tentativa {attempt + 1})')
            wait_web(DEV, timeout=120)
            continue
        if got == want:
            if attempt:
                note('cfg', f'confirmada na tentativa {attempt + 1}: {got}')
            return True
        note('cfg', f'nao pegou (tentativa {attempt + 1}): quer {want}, tem {got}')
        time.sleep(2)

    note('cfg', f'DESISTIU de aplicar {want} — janelas seguintes sao suspeitas')
    return False


def tel_reset_sync(t):
    """Reset the cursor and force one send.

    forceSync clears the backoff first, which is what makes a window measure
    the transport instead of a retry timer inherited from the window before."""
    serial_cmd(t, 'enable', wait=1.2)
    serial_cmd(t, 'tel reset', wait=4.0)
    serial_cmd(t, 'tel sync', wait=6.0)


# ---------------------------------------------------------------------------
# one fault window
# ---------------------------------------------------------------------------

def run_window(t, name, seconds, port, tls, server_kw=None, no_server=False,
               host=None, batch=None, outdir=''):
    log(f'=== {name} ({seconds}s, {"tls" if tls else "http"})')
    kill_stale()
    srv = None
    if not no_server:
        kw = dict(port=port, tls=tls, cert=CERT, key=KEY)
        kw.update(server_kw or {})
        srv = Server('http', f'w_{name}', outdir, **kw)

    # never_read only bites if the device's payload is bigger than the window
    # the sink leaves open (~4 KB after the kernel's floor). A 10-record batch
    # is ~500 B: it fits, the write returns, and the fault measures nothing.
    if host or batch:
        cfg_tel(t, host or HOST_IP, port, tls, batch=batch or 10)

    CURRENT['fault'] = name
    tel_reset_sync(t)

    with samples_lock:
        s0, ser0, h0 = len(samples), len(serial_samples), len(host_samples)
    drops0, fatal0 = t.port_drops, len(t.fatal_lines)
    web0 = dict(WEB)
    t0 = time.time()
    stop_all.wait(seconds)
    dur = time.time() - t0

    st = srv.stats() if srv else {}
    if srv:
        srv.stop()
    if host or batch:
        cfg_tel(t, HOST_IP, port, tls)

    with samples_lock:
        win = samples[s0:]
        wser = serial_samples[ser0:]
        whost = host_samples[h0:]

    ups = [r['uptime'] for r in win if 'uptime' in r]
    resets = sum(1 for a, b in zip(ups, ups[1:]) if b < a)
    fx = [r.get('m_fx') for r in win if r.get('m_fx') is not None]
    lbm = [r.get('m_lbm') for r in win if r.get('m_lbm') is not None]
    ts_ = [r.get('m_ts') for r in win if r.get('m_ts') is not None]
    tf_ = [r.get('m_tf') for r in win if r.get('m_tf') is not None]
    heap = [r.get('heap_f') for r in win if r.get('heap_f') is not None]
    pbf = [r.get('pbuf_fail') for r in wser if r.get('pbuf_fail') is not None]
    kills = [r.get('core1_kills_health') for r in wser
             if r.get('core1_kills_health') is not None]
    hb = [r.get('core1_hb_ms') for r in wser if r.get('core1_hb_ms') is not None]

    row = {
        'fault': name, 'seconds': round(dur, 1), 'tls': tls,
        'server_kw': server_kw, 'no_server': no_server, 'host': host,
        'uptime_resets': resets,
        'usb_drops': t.port_drops - drops0,
        'fatal_lines': len(t.fatal_lines) - fatal0,
        'fatal_text': [l for _, l in t.fatal_lines[fatal0:]][:8],
        'fx_max': max(fx) if fx else None,
        'lbm_min': min(lbm) if lbm else None,
        'heap_min': min(heap) if heap else None,
        'ts_delta': (ts_[-1] - ts_[0]) if len(ts_) > 1 else None,
        'tf_delta': (tf_[-1] - tf_[0]) if len(tf_) > 1 else None,
        'pbuf_fail_max': max(pbf) if pbf else None,
        'core1_kills_health': max(kills) if kills else None,
        'core1_hb_max_ms': max(hb) if hb else None,
        'web_unreachable': sum(1 for r in win if 'err' in r or 'http' in r),
        'ping_lost': sum(1 for r in whost if not r.get('ping')),
        'arp_incomplete': sum(1 for r in whost if r.get('arp_incomplete')),
        'dl_ok': WEB['downloads'] - web0['downloads'],
        'dl_http_fail': WEB['dl_http_fail'] - web0['dl_http_fail'],
        'dl_json_fail': WEB['dl_json_fail'] - web0['dl_json_fail'],
        'uploads': WEB['uploads'] - web0['uploads'],
        'srv_conns': st.get('conns'), 'srv_requests': st.get('requests'),
        'srv_records': st.get('records'),
        'srv_tls_ok': st.get('tls_ok'), 'srv_tls_fail': st.get('tls_failures'),
        'srv_epoch_min': st.get('epoch_min'), 'srv_epoch_max': st.get('epoch_max'),
    }

    verdict = []
    if row['usb_drops'] or resets:
        verdict.append('REBOOT')
    if row['fx_max']:
        verdict.append(f"FX={row['fx_max']}")
    if row['pbuf_fail_max']:
        verdict.append(f"PBUF={row['pbuf_fail_max']}")
    if row['core1_kills_health']:
        verdict.append(f"CORE1KILL={row['core1_kills_health']}")
    if row['ping_lost']:
        verdict.append(f"PING-LOST={row['ping_lost']}")
    if row['arp_incomplete']:
        verdict.append('ARP-INCOMPLETE')
    row['verdict'] = ' '.join(verdict) if verdict else 'SURVIVED'
    log(f"  -> {row['verdict']} (reboots={row['usb_drops']}/{resets} "
        f"ts+{row['ts_delta']} tf+{row['tf_delta']} heapMin={row['heap_min']} "
        f"srvConns={row['srv_conns']} dl={row['dl_ok']})")
    return row


# ---------------------------------------------------------------------------
# fault matrix
# ---------------------------------------------------------------------------

HTTP_FAULTS = [
    ('ok',            dict(server_kw={'mode': 'ok'})),
    ('error500',      dict(server_kw={'mode': 'error500'})),
    ('error401',      dict(server_kw={'mode': 'error401'})),
    ('refused',       dict(no_server=True)),
    ('blackhole',     dict(server_kw={'mode': 'blackhole', 'delay': 120})),
    ('slow20',        dict(server_kw={'mode': 'slow', 'delay': 20})),
    ('half',          dict(server_kw={'mode': 'half'})),
    ('rst',           dict(server_kw={'mode': 'rst'})),
    ('rst_mid',       dict(server_kw={'mode': 'rst_mid'})),
    ('garbage',       dict(server_kw={'mode': 'garbage'})),
    ('huge1mb',       dict(server_kw={'mode': 'huge', 'huge_bytes': 1048576})),
    ('drip',          dict(server_kw={'mode': 'drip', 'drip_ms': 400})),
    ('close_early',   dict(server_kw={'mode': 'close_early'})),
    # The device's own send blocking against a window that never opens.
    #
    # batch is capped at 50 by the firmware (AppManager_Commands.cpp: "Batch
    # fora de range (1-50)"), and 50 records is only ~2,3 KB. The device will
    # not block until it has more in flight than TCP_SND_BUF (8*1460) plus the
    # peer's window (~4 KB after the kernel floor) — about 15,8 KB. So this
    # window as configured here does NOT engage the seam; it only shows the
    # device surviving a sink that ignores it. Making it bite needs a padded
    # t_line (512 B max, web-only setting) — see the dedicated retest.
    ('never_read',    dict(server_kw={'mode': 'never_read', 'delay': 120,
                                      'rcvbuf': 2048}, batch=50)),
    ('syn_blackhole', dict(no_server=True, host='192.0.2.1')),
    ('dns_fail',      dict(no_server=True, host='nao-existe.invalid')),
]

TLS_FAULTS = [
    ('tls_ok',          dict(server_kw={'mode': 'ok'})),
    ('tls_error500',    dict(server_kw={'mode': 'error500'})),
    ('tls_blackhole',   dict(server_kw={'mode': 'ok', 'tls_fault': 'blackhole',
                                        'delay': 120})),
    ('tls_garbage',     dict(server_kw={'mode': 'ok', 'tls_fault': 'garbage'})),
    ('tls_rst',         dict(server_kw={'mode': 'ok', 'tls_fault': 'rst'})),
    ('tls_slow20',      dict(server_kw={'mode': 'ok', 'tls_fault': 'slow',
                                        'delay': 20})),
    ('tls_refused',     dict(no_server=True)),
    ('tls_never_read',  dict(server_kw={'mode': 'never_read', 'delay': 120,
                                        'rcvbuf': 2048}, batch=50)),
    ('tls_bigrecord',   dict(server_kw={'mode': 'tls_bigrecord',
                                        'big_record_bytes': 16384})),
    ('tls_blackhole_http', dict(server_kw={'mode': 'blackhole', 'delay': 120})),
]

SMOKE = [
    ('ok',          dict(server_kw={'mode': 'ok'})),
    ('huge1mb',     dict(server_kw={'mode': 'huge', 'huge_bytes': 1048576})),
    ('never_read',  dict(server_kw={'mode': 'never_read', 'delay': 120,
                                    'rcvbuf': 2048}, batch=50)),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--minutes', type=float, default=90)
    ap.add_argument('--window', type=int, default=0,
                    help='seconds per fault; 0 = split the budget evenly')
    ap.add_argument('--faults', default='all',
                    choices=['all', 'http', 'tls', 'smoke'])
    ap.add_argument('--only', default='',
                    help='comma-separated fault names, for re-running just the '
                         'windows that failed last time')
    ap.add_argument('--web-load', default='on', choices=['on', 'off'])
    ap.add_argument('--churn', default='on', choices=['on', 'off'])
    ap.add_argument('--churn-kb', type=int, default=48)
    ap.add_argument('--out', default='results/netstorm')
    ap.add_argument('--user', default=os.environ.get('SIMUT_USER', 'admin'))
    ap.add_argument('--password', default=os.environ.get('SIMUT_PASS', ''))
    args = ap.parse_args()

    if not args.password:
        sys.path.insert(0, '/home/angelo/Documentos/simut/scratchpad')
        from rig_secrets import USER, PASS
        args.user, args.password = USER, PASS

    outdir = args.out if os.path.isabs(args.out) else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), args.out)
    os.makedirs(outdir, exist_ok=True)

    for p in (CERT, KEY):
        if not os.path.exists(p):
            sys.exit(f'certificate missing: {p} (set SIMUT_BENCH_CERTS)')

    faults = {'all': HTTP_FAULTS + TLS_FAULTS, 'http': HTTP_FAULTS,
              'tls': TLS_FAULTS, 'smoke': SMOKE}[args.faults]
    if args.only:
        pick = {s.strip() for s in args.only.split(',') if s.strip()}
        known = {n for n, _ in HTTP_FAULTS + TLS_FAULTS}
        unknown = pick - known
        if unknown:
            sys.exit(f'unknown fault name(s): {sorted(unknown)}')
        faults = [f for f in HTTP_FAULTS + TLS_FAULTS if f[0] in pick]
    wanted = {name for name, _ in faults}
    budget = args.minutes * 60
    window = args.window or max(45, int(budget / max(1, len(faults))))
    log(f'{len(faults)} faults x {window}s (budget {budget:.0f}s)')

    # The device has to be up before the storm starts: setup() runs with no
    # watchdog, so a storm landing mid-boot can hang it with nothing to log.
    back = wait_web(DEV, timeout=120)
    log(f'device answering (after {back}s)')

    TOK['v'] = login(args.user, args.password)
    log('session up')

    kill_stale()
    t = Target(os.path.join(outdir, 'serial_storm.log'))
    time.sleep(1.5)

    threads = [threading.Thread(target=status_sampler,
                               args=(args.user, args.password), daemon=True),
               threading.Thread(target=serial_sampler, args=(t,), daemon=True),
               threading.Thread(target=host_liveness, daemon=True)]
    if args.web_load == 'on':
        threads.append(threading.Thread(target=downloads,
                                        args=(args.user, args.password),
                                        daemon=True))
        if args.churn == 'on':
            threads.append(threading.Thread(
                target=upload_churn,
                args=(args.user, args.password, args.churn_kb), daemon=True))
    for th in threads:
        th.start()
    log(f'{len(threads)} background threads up')

    rows = []
    t_start = time.time()
    try:
        # HTTP transport first, then TLS: one `write memory` per transport
        # instead of one per fault.
        for group, port, tls in ((HTTP_FAULTS, PORT_HTTP, False),
                                 (TLS_FAULTS, PORT_HTTPS, True)):
            todo = [f for f in group if f[0] in wanted]
            if not todo:
                continue
            cfg_tel(t, HOST_IP, port, tls)
            for name, kw in todo:
                if time.time() - t_start > budget:
                    log('budget spent — stopping the matrix here')
                    break
                rows.append(run_window(t, name, window, port, tls,
                                       outdir=outdir, **kw))
                with open(os.path.join(outdir, 'storm_rows.json'), 'w') as fh:
                    json.dump({'rows': rows}, fh, indent=1, default=str)
    except KeyboardInterrupt:
        log('interrupted')
    finally:
        CURRENT['fault'] = 'teardown'
        stop_all.set()
        time.sleep(2)
        with samples_lock:
            with open(os.path.join(outdir, 'samples.jsonl'), 'w') as fh:
                for r in samples:
                    fh.write(json.dumps(r, default=str) + '\n')
            with open(os.path.join(outdir, 'serial_samples.jsonl'), 'w') as fh:
                for r in serial_samples:
                    fh.write(json.dumps(r, default=str) + '\n')
            with open(os.path.join(outdir, 'host_samples.jsonl'), 'w') as fh:
                for r in host_samples:
                    fh.write(json.dumps(r, default=str) + '\n')
            with open(os.path.join(outdir, 'events.json'), 'w') as fh:
                json.dump(events, fh, indent=1, default=str)
        with open(os.path.join(outdir, 'storm_rows.json'), 'w') as fh:
            json.dump({'rows': rows, 'web': WEB}, fh, indent=1, default=str)
        t.close()

    log('')
    log(f'{"fault":22s} verdict')
    for r in rows:
        log(f'{r["fault"]:22s} {r["verdict"]}')
    log('')
    log(f'web: {WEB}')


if __name__ == '__main__':
    main()
