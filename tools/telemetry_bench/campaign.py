#!/usr/bin/env python3
"""Telemetry test campaign driver.

Phases
------
  perf     latency/throughput for HTTP, HTTPS, MQTT, MQTTS + batch sweep
  drain    point telemetry at a sink, reset the cursor, drain the history
  survive  cycle each fault mode past the device and watch it live or die

Everything is measured from three independent places so no single instrument
can lie: the server (what actually arrived), /api/status (what the device
thinks), and the serial log (whether it rebooted).
"""
import argparse
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench import Target, Web, Server, kill_stale, wait_web, HOST_IP  # noqa: E402

import requests  # noqa: E402

DEV = '192.168.3.24'
WEB_USER, WEB_PASS = 'telb', 'Bench2026x'
PORT_HTTP, PORT_HTTPS, PORT_MQTT, PORT_MQTTS = 18080, 18443, 11883, 18883

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'results')
os.makedirs(OUT, exist_ok=True)


def log(msg):
    print(f'[{time.strftime("%H:%M:%S")}] {msg}', flush=True)


# ---------------------------------------------------------------------------
# device control
# ---------------------------------------------------------------------------

_SESS = {'web': None}


def web_session(force=False):
    """One logged-in session, reused. /api/status is behind auth, so an
    unauthenticated poll returns Forbidden and reads as 'device fine' — the
    exact instrument failure this campaign is meant to avoid."""
    if _SESS['web'] is None or force:
        w = Web(DEV)
        ok, why = w.login(WEB_USER, WEB_PASS)
        if not ok:
            return None
        _SESS['web'] = w
    return _SESS['web']


def status(dev=DEV, timeout=6):
    w = web_session()
    if w is None:
        return {'_err': 'noauth'}
    try:
        r = w.get('/api/status')
        if r.status_code == 200:
            return r.json()
        if r.status_code in (401, 403, 302):
            # Session died with the device: re-login once, then report.
            w = web_session(force=True)
            if w is None:
                return {'_err': 'noauth'}
            r = w.get('/api/status')
            if r.status_code == 200:
                return r.json()
        return {'_http': r.status_code}
    except Exception as e:
        return {'_err': type(e).__name__}


def cfg_http(t, server, port, crypto, batch=None, interval=None, mode=None,
             path=None):
    """Set the HTTP-side telemetry knobs over the serial CLI (no reboot)."""
    cmds = ['enable', 'configure terminal',
            f'tel server {server}', f'tel port {port}',
            f'tel crypto {"on" if crypto else "off"}']
    if path is not None:
        cmds.append(f'tel path {path}')
    if batch is not None:
        cmds.append(f'tel batch {batch}')
    if interval is not None:
        cmds.append(f'tel interval {interval}')
    if mode is not None:
        cmds.append(f'tel mode {mode}')
    cmds += ['end', 'write memory']
    out = []
    for c in cmds:
        out.append(t.send(c, wait=3.0 if c == 'write memory' else 1.6))
    return out


def tel_reset(t):
    t.send('enable', wait=1.2)
    return t.send('tel reset', wait=4.0)


def tel_sync(t, wait=25.0):
    """Force one send. forceSync() resets the backoff before doing anything,
    which is what makes a measurement window start at t=0 instead of somewhere
    inside an escalated retry timer."""
    t.send('enable', wait=1.2)
    return t.send('tel sync', wait=wait)


def commit(web, fields, expect_reboot=True):
    """Push config through /api/commit_all; the device reboots on success."""
    r = web.commit(fields)
    ok = r.status_code in (200, 302)
    log(f'commit_all -> HTTP {r.status_code} {r.text[:120]}')
    if ok and expect_reboot:
        time.sleep(4)
        back = wait_web(DEV, timeout=120)
        log(f'device web back after {back}s')
        return ok, back
    return ok, None


def sample_loop(seconds, period=2.0, servers=(), tag=''):
    """Poll /api/status while a run is in flight."""
    samples = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        s = status()
        s['t'] = round(time.time() - t0, 1)
        samples.append(s)
        time.sleep(period)
    return samples


def summarize_samples(samples):
    up = [s.get('sys', {}).get('uptime') for s in samples if 'sys' in s]
    heap = [s.get('sys', {}).get('heap_f') for s in samples if 'sys' in s]
    lb = [s.get('sys', {}).get('heap_lb') for s in samples if 'sys' in s]
    pend = [s.get('sys', {}).get('pending') for s in samples if 'sys' in s]
    resets = 0
    for a, b in zip(up, up[1:]):
        if a is not None and b is not None and b < a:
            resets += 1
    unreachable = sum(1 for s in samples if '_err' in s or '_http' in s)
    return {
        'n': len(samples),
        'uptime_resets': resets,
        'unreachable': unreachable,
        'heap_min': min([h for h in heap if h], default=None),
        'heap_max': max([h for h in heap if h], default=None),
        'largest_min': min([x for x in lb if x], default=None),
        'largest_max': max([x for x in lb if x], default=None),
        'pending_first': pend[0] if pend else None,
        'pending_last': pend[-1] if pend else None,
        'uptime_first': up[0] if up else None,
        'uptime_last': up[-1] if up else None,
    }


def save(name, obj):
    p = os.path.join(OUT, name)
    with open(p, 'w') as fh:
        json.dump(obj, fh, indent=1, default=str)
    log(f'saved {p}')
    return p
