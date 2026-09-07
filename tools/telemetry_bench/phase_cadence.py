#!/usr/bin/env python3
"""Phase E — what the cadence and the batch size actually cost, on the Air build.

Three questions, each answered by a matrix of measured windows:

  capacity   how many records per second each transport delivers per batch
             size when the server answers at once (delay 0). This is the
             ceiling the cadence can aim at, and it says where the per-request
             overhead (connect, TLS handshake, headers) stops mattering.
  latency    what a slow server does to the same device: delays of 0.5 to 6 s
             injected server-side, batch fixed. The device's socket timeout is
             4 s (NET_SOCKET_TIMEOUT_MS), so the sweep crosses the cliff on
             purpose — the point is to see the failure/backoff path, not to
             avoid it.
  wake       one M1 wake per configuration, timed by the PicoHand probe: how
             long the device stays awake with the radio on to send what it has.
             That is the energy proxy this bench can measure without a meter:
             seconds of radio per record delivered.

Configuration goes through /api/commit_all — the Air image has no `tel` CLI
(SIMUT_CLI_FULL=0), and commit_all reboots, so every (transport, batch) pair
costs a reboot. The server-side delay does not, so the latency sweep reuses one
device configuration.

Usage:
    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... python3 phase_cadence.py [capacity|latency|wake|all]
      [--seconds 60] [--batches 10,25,50,100,250] [--delays 0.5,1,2,3.5,6]

Writes results/phase_cadence.json and one server stats/records/log triple per
window, like the other phases.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))

import campaign as C                      # noqa: E402
from bench import Server, kill_stale      # noqa: E402
import air_test_suite as A                # noqa: E402

HOST = os.environ.get('SIMUT_HOST_IP', C.HOST_IP)
DEV = os.environ.get('SIMUT_DEV', '192.168.3.24')
PORT_HTTP, PORT_HTTPS = 18080, 18443


def log(msg):
    C.log(msg)


# ---------------------------------------------------------------------------
# device control — all over the web, because the Air CLI has no `tel`
# ---------------------------------------------------------------------------

class Dev:
    def __init__(self):
        self.web = A.Web(DEV)
        self.user = os.environ['SIMUT_WEB_USER']
        self.pw = os.environ['SIMUT_WEB_PASS']
        self.target = A.Target()
        self.saved = None

    def login(self):
        if self.web.wait_up(150) is None:
            raise RuntimeError('web did not come up')
        for attempt in range(3):
            self.web.login(self.user, self.pw)
            try:
                self.web.status()      # proves the cookie is honoured, not just set
                return
            except Exception:
                time.sleep(2.0 + attempt * 2)
        raise RuntimeError('login did not produce a usable session')

    def status(self):
        try:
            return self.web.status()
        except Exception:
            self.login()
            return self.web.status()

    def snapshot(self):
        cfg = self.web.config()
        keep = ('t_srv', 't_port', 't_path', 't_transport', 't_sec', 't_int', 't_bat',
                't_mode', 'h_int')
        self.saved = {k: cfg[k] for k in keep if k in cfg}
        log('config snapshot: ' + json.dumps(self.saved))
        return self.saved

    def ensure_m0(self):
        """Serial `air stop` until phase=0 — the cycle would hide the web."""
        deadline = time.time() + 300
        while time.time() < deadline:
            if not self.target.usb.present():
                self.target.usb.wait(True, 240)
            try:
                self.target.open(20)
                st = self.target.air_status(retry_s=25)
                if st['phase'] != 0 or st.get('armed'):
                    self.target.cmd('air stop', 3)
                    st = self.target.air_status(retry_s=10)
                if st['phase'] == 0:
                    self.target.close()
                    return st
            except Exception as exc:
                log(f'ensure_m0 retry: {exc}')
                time.sleep(2)
        raise RuntimeError('could not bring the device to M0')

    def configure(self, tls, batch, interval_ms=1000, port=None, path='/ingest'):
        """commit_all reboots; come back in M0 with a fresh login."""
        fields = {'t_srv': HOST, 't_port': port or (PORT_HTTPS if tls else PORT_HTTP),
                  't_path': path, 't_transport': 0, 't_sec': bool(tls),
                  't_int': int(interval_ms), 't_bat': int(batch), 't_mode': 0}
        log('commit ' + json.dumps(fields))
        self.target.close()
        try:
            self.web.commit_sys(fields)
        except A.TestFail as exc:
            # A session that answers 403 is one the device no longer knows —
            # seen right after a flash, when the login raced the boot. One fresh
            # login and one retry; a second failure is a real one.
            if '403' not in str(exc):
                raise
            log('commit_all 403 — re-login and retry once')
            self.login()
            self.web.commit_sys(fields)
        time.sleep(4)
        self.target.usb.wait(False, 25)
        self.target.usb.wait(True, 150)
        time.sleep(12)
        self.ensure_m0()
        self.login()
        got = self.web.config()
        bad = {k: (got.get(k), v) for k, v in fields.items() if k in got and got.get(k) != v}
        if bad:
            raise RuntimeError(f'config did not apply: {bad}')

    def tel_reset(self):
        r = self.web.post('/api/action', data={'op': 'tel_reset'})
        if r.status_code != 200:
            raise RuntimeError(f'tel_reset HTTP {r.status_code}')

    def tel_sync(self):
        """Clears an escalated backoff (forceSync resets it first) and fires one send."""
        r = self.web.post('/api/action', data={'op': 'tel_sync'})
        return r.status_code

    def restore(self):
        if not self.saved:
            return
        log('restoring config: ' + json.dumps(self.saved))
        self.target.close()
        try:
            self.web.commit_sys(self.saved)
        except Exception as exc:
            log(f'restore commit failed: {exc}')
            return
        time.sleep(4)
        self.target.usb.wait(False, 25)
        self.target.usb.wait(True, 150)
        time.sleep(12)
        try:
            self.ensure_m0()
            self.login()
        except Exception as exc:
            log(f'post-restore: {exc}')


# ---------------------------------------------------------------------------
# one measured window
# ---------------------------------------------------------------------------

def window(dev, srv, seconds, label, period=1.0):
    """One measured window: device counters at both ends, server requests in between.

    The server's cumulative counters are NOT the window's numbers. The server is
    started before the device is configured, and a configured device reboots
    and resumes the drain from its persisted cursor while the harness is still
    logging in; then `tel_reset` restarts the drain from the oldest record. The
    first matrix (2026-09-07) counted all of that as "the window" and read the
    restart as 22–35% duplicates. What belongs to the window is cut out of the
    server's per-request log by wall clock; the rest is reported as
    `pre_window_records` so the split is visible."""
    s0 = dev.status()
    m0 = s0.get('metr', {})
    samples = []
    wall0 = time.time()
    while time.time() - wall0 < seconds:
        try:
            s = dev.status()
        except Exception as exc:
            s = {'_err': type(exc).__name__}
        s['_t'] = round(time.time() - wall0, 1)
        samples.append(s)
        time.sleep(period)
    wall1 = time.time()
    s1 = dev.status()
    m1 = s1.get('metr', {})

    # the server dumps once a second; take a dump written after the window closed
    st = {}
    if srv:
        for _ in range(10):
            st = srv.stats()
            if (st.get('now') or 0) >= wall1:
                break
            time.sleep(0.5)
    started = st.get('started') or 0
    reqs = [(started + r[0], r[1] or 0, r[2] or 0, r[3]) for r in (st.get('req_log') or [])]
    inw = [r for r in reqs if wall0 <= r[0] <= wall1]
    n_req = len(inw)
    n_rec = sum(r[1] for r in inw)
    n_bytes = sum(r[2] for r in inw)
    ms = sorted(r[3] for r in inw if isinstance(r[3], int))
    pre = sum(r[1] for r in reqs if r[0] < wall0)

    # A window can outlast the backlog: at ~1000 rec/s the ~35k records of the
    # 30-day floor are gone in half a minute. The transport is rated over the
    # time it was busy: from the window start to its last request, when that
    # last request came well before the end. (`pending` in /api/status is
    # recounted once a minute, so it cannot mark the moment.)
    last_t = max((r[0] for r in inw), default=wall0)
    drained = n_req > 0 and (wall1 - last_t) > 3 * period
    active = (last_t - wall0) if drained else (wall1 - wall0)
    active = max(active, 0.5)

    lat = [s.get('metr', {}).get('tl') for s in samples if s.get('metr')]
    lat = [x for x in lat if isinstance(x, int) and x > 0]
    heap = [s.get('sys', {}).get('heap_f') for s in samples if s.get('sys')]
    lb = [s.get('sys', {}).get('heap_lb') for s in samples if s.get('sys')]
    up = [s.get('sys', {}).get('uptime') for s in samples if s.get('sys')]
    resets = sum(1 for a, b in zip(up, up[1:]) if a is not None and b is not None and b < a)

    def d(k):
        a, b = m0.get(k), m1.get(k)
        return (b - a) if isinstance(a, int) and isinstance(b, int) else None

    def pct(p):
        return ms[min(len(ms) - 1, int(round(p / 100.0 * (len(ms) - 1))))] if ms else None

    ls = sorted(lat)
    row = {
        'label': label, 'seconds': seconds,
        'active_s': round(active, 1),
        'drained_at_s': round(last_t - wall0, 1) if drained else None,
        'dev_sent': d('ts'), 'dev_failed': d('tf'), 'dev_retries': d('tr'), 'dev_bytes': d('tb'),
        'dev_lat_min': ls[0] if ls else None,
        'dev_lat_med': ls[len(ls) // 2] if ls else None,
        'dev_lat_max': ls[-1] if ls else None,
        'heap_min': min([h for h in heap if h], default=None),
        'largest_min': min([x for x in lb if x], default=None),
        'pending_start': s0.get('sys', {}).get('pending'),
        'pending_end': s1.get('sys', {}).get('pending'),
        'uptime_resets': resets,
        'unreachable_polls': sum(1 for s in samples if '_err' in s),
        'srv_requests': n_req,
        'srv_records': n_rec,
        'srv_bytes': n_bytes,
        'pre_window_records': pre,
        'srv_ms_p50': pct(50),
        'srv_ms_p90': pct(90),
        'srv_tls_ok': st.get('tls_ok'),
        'srv_tls_fail': st.get('tls_failures'),
        'srv_keepalive_reuses': st.get('keepalive_reuses'),
    }
    row['records_per_s'] = round(n_rec / active, 2)
    row['bytes_per_s'] = round(n_bytes / active, 1)
    if n_req:
        row['s_between_sends'] = round(active / n_req, 3)
        row['records_per_send'] = round(n_rec / n_req, 1)
    return row, samples


def out_name():
    """Result file for this run: phase_cadence.json, or phase_cadence_<tag>.json."""
    tag = getattr(C, 'OUT_TAG', '')
    return 'phase_cadence%s.json' % (('_' + tag) if tag else '')


SERVER_KEEPALIVE = False   # set by --server-keepalive: ok mode answers keep-alive and serves more


def start_server(name, tls, port, mode='ok', delay=None):
    kill_stale()
    kw = dict(port=port, mode=mode, tls=tls)
    if SERVER_KEEPALIVE and mode == 'ok':
        kw['keepalive'] = True
    if tls:
        kw.update(cert='certs/cert.pem', key='certs/key.pem')
    if delay is not None:
        kw['delay'] = delay
    srv = Server('http', name, C.OUT, **kw)
    time.sleep(1.0)
    if not srv.alive():
        raise RuntimeError(f'server {name} died at start — see {srv.log_path}')
    return srv


# ---------------------------------------------------------------------------
# matrices
# ---------------------------------------------------------------------------

def phase_capacity(dev, batches, seconds, out, interval_ms=1, which=('http', 'https')):
    for tls in [w == 'https' for w in which]:
        port = PORT_HTTPS if tls else PORT_HTTP
        for b in batches:
            label = f'{"https" if tls else "http"}_b{b}_d0_i{interval_ms}'
            log(f'--- {label}')
            srv = start_server('cad_' + label + (('_' + C.OUT_TAG) if getattr(C, 'OUT_TAG', '') else ''), tls, port)
            try:
                dev.configure(tls, b, interval_ms=interval_ms, port=port)
                dev.tel_reset()
                dev.tel_sync()
                time.sleep(3)
                row, _ = window(dev, srv, seconds, label)
                row.update(tls=tls, batch=b, delay=0.0, interval_ms=interval_ms, phase='capacity',
                           server_keepalive=SERVER_KEEPALIVE, tag=getattr(C, 'OUT_TAG', ''))
                out['rows'].append(row)
                log(json.dumps({k: row.get(k) for k in (
                    'label', 'srv_requests', 'srv_records', 'records_per_s', 's_between_sends',
                    'dev_lat_med', 'dev_lat_max', 'dev_failed', 'heap_min', 'largest_min',
                    'uptime_resets')}))
                C.save(out_name(), out)
            finally:
                srv.stop()


def phase_latency(dev, batch, delays, seconds, out, tls=False, interval_ms=1):
    port = PORT_HTTPS if tls else PORT_HTTP
    dev.configure(tls, batch, interval_ms=interval_ms, port=port)
    for delay in delays:
        label = f'{"https" if tls else "http"}_b{batch}_d{delay}'
        log(f'--- {label}')
        srv = start_server('cad_' + label, tls, port, mode=('ok' if delay == 0 else 'slow'),
                           delay=(None if delay == 0 else delay))
        try:
            dev.tel_reset()
            dev.tel_sync()
            time.sleep(3)
            row, _ = window(dev, srv, seconds, label)
            row.update(tls=tls, batch=batch, delay=delay, interval_ms=interval_ms, phase='latency')
            out['rows'].append(row)
            log(json.dumps({k: row.get(k) for k in (
                'label', 'srv_requests', 'srv_records', 'records_per_s', 's_between_sends',
                'dev_lat_med', 'dev_lat_max', 'dev_failed', 'dev_retries', 'uptime_resets')}))
            C.save(out_name(), out)
        finally:
            srv.stop()


def phase_wake(dev, configs, out, reset=True):
    """One M1 wake each, timed by the probe: awake seconds per record delivered.

    reset=False leaves the cursor where the last drain put it, so the wake has
    only what accumulated since — the steady-state case, where the wake should
    send one small batch and go back to sleep instead of running to the cap."""
    hand = A.Hand()
    if not (hand.available and hand.ping() and hand.probe_supported()):
        log('wake phase needs the PicoHand probe — skipped')
        return
    for tls, batch, interval_ms in configs:
        port = PORT_HTTPS if tls else PORT_HTTP
        label = f'wake_{"https" if tls else "http"}_b{batch}_i{interval_ms}' + ('' if reset else '_noreset')
        log(f'--- {label}')
        srv = start_server('cad_' + label + (('_' + C.OUT_TAG) if getattr(C, 'OUT_TAG', '') else ''), tls, port)
        try:
            dev.configure(tls, batch, interval_ms=interval_ms, port=port)
            if reset:
                dev.tel_reset()
            s0 = dev.status()
            hand.probe_start()
            dev.target.open(30)
            # Only what lands after this instant is the wake's: the device in M0
            # goes on draining between tel_reset and the hibernate command.
            wall_hib = time.time()
            dev.target.ser.write(b'air hibernate\r\n')
            dev.target.close()
            # sleep, then one wake; the wake sends, then sleeps again
            dev.target.usb.wait(False, 120)
            dev.target.usb.wait(True, 240)
            dev.target.usb.wait(False, 240)
            time.sleep(2)
            edges = hand.probe_read()
            wins = A.probe_windows(edges)
            awake = [sec for lbl, sec in wins if lbl == 'awake']
            st = srv.stats()
            started = st.get('started') or 0
            after = [r for r in (st.get('req_log') or []) if started + r[0] >= wall_hib]
            recs = sum((r[1] or 0) for r in after)
            row = {'label': label, 'phase': 'wake', 'tls': tls, 'batch': batch,
                   'interval_ms': interval_ms, 'awake_windows_s': [round(a, 2) for a in awake],
                   'srv_records': recs, 'srv_requests': len(after),
                   'pending_before': s0.get('sys', {}).get('pending')}
            if awake and recs:
                row['awake_s_per_1000_records'] = round(awake[-1] / recs * 1000, 2)
            out['rows'].append(row)
            log(json.dumps(row))
            C.save(out_name(), out)
            dev.ensure_m0()
            dev.login()
        finally:
            srv.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('what', nargs='?', default='all', choices=['capacity', 'latency', 'wake', 'all'])
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--batches', default='10,25,50,100,250')
    ap.add_argument('--delays', default='0.5,1,2,3.5,6')
    ap.add_argument('--lat-batch', type=int, default=50)
    ap.add_argument('--no-restore', action='store_true')
    ap.add_argument('--interval', type=int, default=1,
                    help='t_int in ms for capacity/latency; 1 = back-to-back, the floor is then 1.5x latency')
    ap.add_argument('--tls-only', action='store_true')
    ap.add_argument('--server-keepalive', action='store_true',
                    help='bench server keeps the connection open between requests (TLS session A/B)')
    ap.add_argument('--tag', default='', help='suffix for the server result files, e.g. ka')
    ap.add_argument('--http-only', action='store_true')
    ap.add_argument('--wake-no-reset', action='store_true',
                    help='wake phase: do not tel_reset before the wake (steady-state backlog)')
    ap.add_argument('--wake-configs', default='',
                    help='wake phase only: transport:batch:t_int_ms list, e.g. http:100:60000,https:100:1')
    args = ap.parse_args()
    batches = [int(x) for x in args.batches.split(',')]
    delays = [float(x) for x in args.delays.split(',')]

    global SERVER_KEEPALIVE
    SERVER_KEEPALIVE = bool(args.server_keepalive)
    if args.tag:
        C.OUT_TAG = args.tag
    dev = Dev()
    dev.ensure_m0()
    dev.login()
    dev.snapshot()
    out = {'started': time.time(), 'host': HOST, 'seconds': args.seconds, 'rows': []}
    try:
        if args.what in ('capacity', 'all'):
            phase_capacity(dev, batches, args.seconds, out, interval_ms=args.interval,
                           which=('https',) if args.tls_only else (('http',) if args.http_only else ('http', 'https')))
        if args.what in ('latency', 'all'):
            phase_latency(dev, args.lat_batch, delays, args.seconds, out, interval_ms=args.interval)
        if args.what in ('wake', 'all'):
            # The reading interval on the bench is 1 min, and the radio only comes
            # up on a wake where telemetry is due (t_int <= reading interval, or
            # every ceil(t_int / reading) wakes). Every config here is due on the
            # first wake; t_int is then the cadence INSIDE the wake — 60 s means
            # one batch per wake, which is the behaviour the plan replaces.
            cfgs = [(False, 100, 60000), (False, 100, 1000), (False, 100, 1), (True, 100, 1)]
            if args.wake_configs:
                cfgs = []
                for item in args.wake_configs.split(','):
                    tr, b, ms = item.split(':')
                    cfgs.append((tr.strip().lower() == 'https', int(b), int(ms)))
            phase_wake(dev, cfgs, out, reset=not args.wake_no_reset)
    finally:
        C.save(out_name(), out)
        if not args.no_restore:
            dev.restore()
        kill_stale()
    log('done')


if __name__ == '__main__':
    main()
