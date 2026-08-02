#!/usr/bin/env python3
"""Phase A — telemetry throughput and latency, per transport and batch size.

Each run starts from the same place: `tel reset` puts the cursor back to the
firmware's 30-day floor, so every run has the same large backlog to chew on and
the numbers compare. Without that the first run drains the queue and every run
after it measures an idle device.

Three instruments per run:
  server     what actually arrived (requests, records, bytes, wall clock)
  /api/status what the device believes (telSent/telFailed/latency/heap)
  serial     whether it rebooted
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale, wait_web  # noqa: E402


def run_window(t, srv, seconds, label, period=2.0):
    """Sample the device while telemetry runs, then reduce to one row."""
    s0 = C.status()
    m0 = s0.get('metr', {})
    boots0 = t.port_drops
    fatal0 = len(t.fatal_lines)
    samples = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        s = C.status()
        s['_t'] = round(time.time() - t0, 1)
        samples.append(s)
        time.sleep(period)
    s1 = C.status()
    m1 = s1.get('metr', {})
    st = srv.stats() if srv else {}

    lat = [s.get('metr', {}).get('tl') for s in samples if s.get('metr')]
    lat = [x for x in lat if isinstance(x, int) and x > 0]
    heap = [s.get('sys', {}).get('heap_f') for s in samples if s.get('sys')]
    lb = [s.get('metr', {}).get('lb') for s in samples if s.get('metr')]
    up = [s.get('sys', {}).get('uptime') for s in samples if s.get('sys')]
    resets = sum(1 for a, b in zip(up, up[1:])
                 if a is not None and b is not None and b < a)

    def d(k):
        a, b = m0.get(k), m1.get(k)
        return (b - a) if isinstance(a, int) and isinstance(b, int) else None

    lat_sorted = sorted(set(lat))
    row = {
        'label': label,
        'seconds': seconds,
        'dev_sent': d('ts'), 'dev_failed': d('tf'), 'dev_retries': d('tr'),
        'dev_bytes': d('tb'),
        'dev_lat_samples': sorted(lat),
        'dev_lat_min': min(lat) if lat else None,
        'dev_lat_med': lat_sorted[len(lat_sorted) // 2] if lat_sorted else None,
        'dev_lat_max': max(lat) if lat else None,
        'heap_min': min([h for h in heap if h], default=None),
        'heap_max': max([h for h in heap if h], default=None),
        'largest_min': min([x for x in lb if x], default=None),
        'largest_max': max([x for x in lb if x], default=None),
        'pending_start': s0.get('sys', {}).get('pending'),
        'pending_end': s1.get('sys', {}).get('pending'),
        'uptime_resets': resets,
        'usb_drops': t.port_drops - boots0,
        'fatal_lines': len(t.fatal_lines) - fatal0,
        'unreachable_polls': sum(1 for s in samples if '_err' in s or '_http' in s),
        'srv_requests': st.get('requests') or st.get('publishes'),
        'srv_records': st.get('records'),
        'srv_bytes': st.get('bytes_in'),
        'srv_conns': st.get('conns'),
        'srv_connects': st.get('connects'),
        'srv_tls_ok': st.get('tls_ok'),
        'srv_tls_fail': st.get('tls_failures'),
        'srv_ms_p50': st.get('server_ms_p50'),
        'srv_epoch_min': st.get('epoch_min'),
        'srv_epoch_max': st.get('epoch_max'),
    }
    if row['srv_records'] is not None:
        row['records_per_s'] = round(row['srv_records'] / seconds, 2)
        row['bytes_per_s'] = round((row['srv_bytes'] or 0) / seconds, 1)
    if row['srv_requests']:
        row['s_between_sends'] = round(seconds / row['srv_requests'], 2)
        row['records_per_send'] = round((row['srv_records'] or 0) / row['srv_requests'], 1)
    return row, samples


def http_matrix(t, tls, port, batches, seconds, tag):
    rows = []
    for b in batches:
        label = f'{"https" if tls else "http"}_batch{b}'
        C.log(f'--- {label}')
        kill_stale()
        srv = Server('http', f'{tag}_{label}', C.OUT, port=port, mode='ok',
                     tls=tls, cert='certs/cert.pem', key='certs/key.pem')
        C.cfg_http(t, C.HOST_IP, port, crypto=tls, batch=b, interval=1000,
                   mode='json', path='/ingest')
        C.tel_reset(t)
        # `tel sync` calls resetBackoff() before it does anything else, so it
        # is the only way to clear an escalated backoff without a reboot.
        # Skipping it measures the backoff timer, not the transport.
        C.tel_sync(t)
        time.sleep(3)
        row, samples = run_window(t, srv, seconds, label)
        row['batch'] = b
        row['tls'] = tls
        rows.append(row)
        C.log(json.dumps({k: row[k] for k in
                          ('label', 'srv_requests', 'srv_records', 'records_per_s',
                           'dev_lat_med', 'dev_lat_max', 'dev_failed', 'heap_min',
                           'largest_min', 'usb_drops')}))
        srv.stop()
    return rows


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_perf.log'))
    time.sleep(1)
    out = {'started': time.time(), 'seconds_per_run': seconds, 'rows': []}

    C.log('=== HTTP plain ===')
    out['rows'] += http_matrix(t, False, C.PORT_HTTP, [1, 10, 25, 50], seconds, 'perf')
    C.save('phase_perf_http.json', out)

    C.log('=== HTTPS ===')
    out['rows'] += http_matrix(t, True, C.PORT_HTTPS, [1, 10, 25, 50], seconds, 'perf')
    C.save('phase_perf_http.json', out)

    t.close()
    C.log('done')


if __name__ == '__main__':
    main()
