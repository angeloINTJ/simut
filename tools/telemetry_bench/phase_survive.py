#!/usr/bin/env python3
"""Phase C — survival against broken servers.

Each fault gets the same three-act structure, because "the device did not
reboot" is only half the question. The other half is whether it silently threw
data away.

  act 1  good sink, `tel reset` + `tel sync` → note the last epoch accepted (E1)
  act 2  swap in the fault, run it for the full window, watch for death
  act 3  good sink again, `tel sync` → note the first epoch accepted (E2)

If E2 is more than one sampling interval past E1, the device advanced its cursor
over records no server ever acknowledged: data loss. That check is the point of
acts 1 and 3 — without them a fault that quietly eats history looks identical to
one the device shrugged off.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402

HIST_INTERVAL_S = 60   # h_int = 1 minute


def good_server(tag, port, tls):
    return Server('http', tag, C.OUT, port=port, mode='ok', tls=tls,
                  cert='certs/cert.pem', key='certs/key.pem')


def act_baseline(t, tag, port, tls, batch=10):
    """Drain a couple of batches through a healthy sink and report where the
    cursor got to."""
    kill_stale()
    srv = good_server(f'{tag}_pre', port, tls)
    C.cfg_http(t, C.HOST_IP, port, crypto=tls, batch=batch, interval=1000,
               mode='json', path='/ingest')
    C.tel_reset(t)
    C.tel_sync(t)
    time.sleep(12)
    st = srv.stats()
    srv.stop()
    return {
        'requests': st.get('requests'), 'records': st.get('records'),
        'epoch_min': st.get('epoch_min'), 'epoch_max': st.get('epoch_max'),
    }


def act_recover(t, tag, port, tls, batch=10, settle=25):
    kill_stale()
    srv = good_server(f'{tag}_post', port, tls)
    C.tel_sync(t)
    time.sleep(settle)
    st = srv.stats()
    srv.stop()
    return {
        'requests': st.get('requests'), 'records': st.get('records'),
        'epoch_min': st.get('epoch_min'), 'epoch_max': st.get('epoch_max'),
    }


def run_fault(t, name, port, tls, seconds, server_kw=None, no_server=False,
              batch=10, host=None):
    C.log(f'=== fault: {name} ({seconds}s)')
    pre = act_baseline(t, name, port, tls, batch=batch)
    C.log(f'  baseline: {pre}')

    kill_stale()
    srv = None
    if not no_server:
        kw = dict(port=port, tls=tls, cert='certs/cert.pem', key='certs/key.pem')
        kw.update(server_kw or {})
        srv = Server('http', f'{name}_fault', C.OUT, **kw)

    if host:
        # Faults that live in the address itself: a name that does not resolve,
        # and an address that swallows the SYN instead of refusing it. Neither
        # can be produced by a listening socket.
        C.cfg_http(t, host, port, crypto=tls, batch=batch, interval=1000,
                   mode='json', path='/ingest')

    # Clear the backoff so the window is spent attacking the fault rather than
    # waiting out a timer inherited from the baseline act.
    C.tel_sync(t, wait=30)

    boots0, fatal0 = t.port_drops, len(t.fatal_lines)
    samples = []
    t0 = time.time()
    web_fail_streak = 0
    worst_streak = 0
    while time.time() - t0 < seconds:
        s = C.status()
        s['_t'] = round(time.time() - t0, 1)
        samples.append(s)
        if '_err' in s or '_http' in s:
            web_fail_streak += 1
            worst_streak = max(worst_streak, web_fail_streak)
        else:
            web_fail_streak = 0
        time.sleep(2)

    st = srv.stats() if srv else {}
    if srv:
        srv.stop()

    up = [s.get('sys', {}).get('uptime') for s in samples if s.get('sys')]
    heap = [s.get('sys', {}).get('heap_f') for s in samples if s.get('sys')]
    lb = [s.get('metr', {}).get('lb') for s in samples if s.get('metr')]
    tf = [s.get('metr', {}).get('tf') for s in samples if s.get('metr')]
    ts_ = [s.get('metr', {}).get('ts') for s in samples if s.get('metr')]
    resets = sum(1 for a, b in zip(up, up[1:])
                 if a is not None and b is not None and b < a)

    if host:
        C.cfg_http(t, C.HOST_IP, port, crypto=tls, batch=batch, interval=1000,
                   mode='json', path='/ingest')
    post = act_recover(t, name, port, tls, batch=batch)
    C.log(f'  recovery: {post}')

    gap = None
    lost = None
    if pre.get('epoch_max') and post.get('epoch_min'):
        gap = post['epoch_min'] - pre['epoch_max']
        lost = max(0, (gap // HIST_INTERVAL_S) - 1)

    row = {
        'fault': name, 'seconds': seconds, 'tls': tls,
        'server_kw': server_kw, 'no_server': no_server,
        'pre': pre, 'post': post,
        'cursor_gap_s': gap,
        'records_skipped': lost,
        'uptime_resets': resets,
        'usb_drops': t.port_drops - boots0,
        'fatal_lines': len(t.fatal_lines) - fatal0,
        'fatal_text': [l for _, l in t.fatal_lines[fatal0:]][:6],
        'web_unreachable_polls': sum(1 for s in samples if '_err' in s or '_http' in s),
        'web_worst_streak_polls': worst_streak,
        'heap_min': min([h for h in heap if h], default=None),
        'heap_max': max([h for h in heap if h], default=None),
        'largest_min': min([x for x in lb if x], default=None),
        'dev_failed_delta': (tf[-1] - tf[0]) if len(tf) > 1 and None not in (tf[0], tf[-1]) else None,
        'dev_sent_delta': (ts_[-1] - ts_[0]) if len(ts_) > 1 and None not in (ts_[0], ts_[-1]) else None,
        'srv_conns': st.get('conns'), 'srv_requests': st.get('requests'),
        'srv_records': st.get('records'),
        'srv_tls_ok': st.get('tls_ok'), 'srv_tls_fail': st.get('tls_failures'),
        'uptime_first': up[0] if up else None,
        'uptime_last': up[-1] if up else None,
    }
    verdict = []
    if row['usb_drops'] or resets:
        verdict.append('REBOOT')
    if row['fatal_lines']:
        verdict.append('FATAL')
    if lost:
        verdict.append(f'DATA-LOSS({lost} rec)')
    if worst_streak >= 3:
        verdict.append(f'WEB-STALL({worst_streak * 2}s)')
    if not post.get('records'):
        verdict.append('NO-RECOVERY')
    row['verdict'] = ' '.join(verdict) if verdict else 'SURVIVED'
    C.log(f'  -> {row["verdict"]} '
          f'(reboots={row["usb_drops"]}/{resets} devFail+{row["dev_failed_delta"]} '
          f'heapMin={row["heap_min"]} srvConns={row["srv_conns"]})')
    return row


HTTP_FAULTS = [
    ('refused',      dict(no_server=True)),
    ('blackhole',    dict(server_kw={'mode': 'blackhole', 'delay': 120})),
    ('slow20',       dict(server_kw={'mode': 'slow', 'delay': 20})),
    ('half',         dict(server_kw={'mode': 'half'})),
    ('rst',          dict(server_kw={'mode': 'rst'})),
    ('rst_mid',      dict(server_kw={'mode': 'rst_mid'})),
    ('garbage',      dict(server_kw={'mode': 'garbage'})),
    ('huge1mb',      dict(server_kw={'mode': 'huge', 'huge_bytes': 1048576})),
    ('drip',         dict(server_kw={'mode': 'drip', 'drip_ms': 400})),
    ('error500',     dict(server_kw={'mode': 'error500'})),
    ('error401',     dict(server_kw={'mode': 'error401'})),
    ('close_early',  dict(server_kw={'mode': 'close_early'})),
    # RFC 5737 TEST-NET-1: routed nowhere, so the SYN is swallowed rather than
    # refused — the connect() blocks instead of failing fast.
    ('syn_blackhole', dict(no_server=True, host='192.0.2.1')),
    ('dns_fail',      dict(no_server=True, host='nao-existe.invalid')),
]

TLS_FAULTS = [
    ('tls_blackhole', dict(server_kw={'mode': 'ok', 'tls_fault': 'blackhole', 'delay': 120})),
    ('tls_garbage',   dict(server_kw={'mode': 'ok', 'tls_fault': 'garbage'})),
    ('tls_rst',       dict(server_kw={'mode': 'ok', 'tls_fault': 'rst'})),
    ('tls_slow20',    dict(server_kw={'mode': 'ok', 'tls_fault': 'slow', 'delay': 20})),
    ('tls_refused',   dict(no_server=True)),
    ('tls_error500',  dict(server_kw={'mode': 'error500'})),
    ('tls_blackhole_http', dict(server_kw={'mode': 'blackhole', 'delay': 120})),
]


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 120
    which = sys.argv[2] if len(sys.argv) > 2 else 'all'
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_survive.log'))
    time.sleep(1)
    rows = []
    outname = f'phase_survive_{which}.json'

    if which in ('all', 'http'):
        for name, kw in HTTP_FAULTS:
            rows.append(run_fault(t, name, C.PORT_HTTP, False, seconds, **kw))
            C.save(outname, {'rows': rows})

    if which in ('all', 'tls'):
        for name, kw in TLS_FAULTS:
            rows.append(run_fault(t, name, C.PORT_HTTPS, True, seconds, **kw))
            C.save(outname, {'rows': rows})

    C.save(outname, {'rows': rows})
    t.close()
    for r in rows:
        C.log(f'{r["fault"]:22s} {r["verdict"]}')


if __name__ == '__main__':
    main()
