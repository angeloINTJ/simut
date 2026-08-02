#!/usr/bin/env python3
"""Phase B — drain as much history as the telemetry path will give up.

Two independent readings of the same history, so the drain can be judged rather
than just described:

  telemetry   `tel reset` then let the device push until it goes quiet
  ground truth download every .h5 over /download and decode it with the
              reference codec in tools/history_v5.py

The difference between the two epoch sets is the answer to "todo o histórico
possível" — including the part of the archive the telemetry path structurally
cannot reach.
"""
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402

REPO = '/home/angelo/Documentos/simut'
sys.path.insert(0, os.path.join(REPO, 'tools'))

RAW = os.path.join(C.OUT, 'history_raw')
os.makedirs(RAW, exist_ok=True)


def drain(t, seconds_max=2400, quiet_s=90, batch=50, interval=1000):
    kill_stale()
    srv = Server('http', 'drain', C.OUT, port=C.PORT_HTTP, mode='ok')
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=batch,
               interval=interval, mode='json', path='/ingest')
    C.tel_reset(t)
    C.tel_sync(t, wait=30)

    t0 = time.time()
    last_rec = 0
    last_change = time.time()
    timeline = []
    boots0 = t.port_drops
    while time.time() - t0 < seconds_max:
        st = srv.stats()
        n = st.get('records') or 0
        s = C.status()
        timeline.append({
            't': round(time.time() - t0, 1),
            'records': n,
            'requests': st.get('requests'),
            'pending': s.get('sys', {}).get('pending'),
            'heap': s.get('sys', {}).get('heap_f'),
            'lb': s.get('metr', {}).get('lb'),
            'tf': s.get('metr', {}).get('tf'),
            'tl': s.get('metr', {}).get('tl'),
            'uptime': s.get('sys', {}).get('uptime'),
        })
        if n != last_rec:
            last_rec = n
            last_change = time.time()
        elif time.time() - last_change > quiet_s:
            C.log(f'drained: no new record for {quiet_s}s at {n} records')
            break
        if len(timeline) % 10 == 0:
            C.log(f'  t={timeline[-1]["t"]}s records={n} pending={timeline[-1]["pending"]}')
        time.sleep(3)

    st = srv.stats()
    recs = srv.records()
    srv.stop()
    epochs = sorted({r['ts'] for r in recs if isinstance(r.get('ts'), int)})
    return {
        'wall_s': round(time.time() - t0, 1),
        'requests': st.get('requests'),
        'records': st.get('records'),
        'records_written': len(recs),
        'unique_epochs': len(epochs),
        'bytes_in': st.get('bytes_in'),
        'epoch_min': epochs[0] if epochs else None,
        'epoch_max': epochs[-1] if epochs else None,
        'duplicates': len(recs) - len(epochs),
        'usb_drops': t.port_drops - boots0,
        'srv_ms_p50': st.get('server_ms_p50'),
        'srv_ms_max': st.get('server_ms_max'),
        'timeline': timeline,
        'batch': batch, 'interval': interval,
    }, epochs


def download_all():
    """Pull every history file over /download and decode with the reference codec."""
    import history_v5 as h5
    w = C.web_session()
    entries = w.get('/api/ls?dir=/history').json()['entries']
    files = [e for e in entries if e['n'].endswith('.h5')]
    got, failed = [], []
    for e in sorted(files, key=lambda x: x['n']):
        dst = os.path.join(RAW, e['n'])
        if os.path.exists(dst) and os.path.getsize(dst) == e['s']:
            got.append((e['n'], e['s']))
            continue
        try:
            r = w.get('/download?file=/history/' + e['n'])
            if r.status_code == 200 and len(r.content) == e['s']:
                with open(dst, 'wb') as fh:
                    fh.write(r.content)
                got.append((e['n'], e['s']))
            else:
                failed.append((e['n'], r.status_code, len(r.content), e['s']))
        except Exception as ex:
            failed.append((e['n'], type(ex).__name__, str(ex)[:60], e['s']))
        time.sleep(0.15)

    all_epochs = set()
    per_file = []
    decode_errors = []
    for name, size in got:
        try:
            blob = open(os.path.join(RAW, name), 'rb').read()
            eps = [ts for _sch, ts, _vals in
                   h5.read_series(blob, nominal_interval_s=60)]
            all_epochs.update(eps)
            per_file.append({'file': name, 'bytes': size, 'records': len(eps),
                             'first': min(eps) if eps else None,
                             'last': max(eps) if eps else None})
        except Exception as ex:
            decode_errors.append({'file': name, 'err': f'{type(ex).__name__}: {ex}'})
    return {
        'files_listed': len(files), 'files_downloaded': len(got),
        'download_failures': failed,
        'total_bytes': sum(s for _, s in got),
        'total_records': sum(p['records'] for p in per_file),
        'unique_epochs': len(all_epochs),
        'epoch_min': min(all_epochs) if all_epochs else None,
        'epoch_max': max(all_epochs) if all_epochs else None,
        'decode_errors': decode_errors,
        'per_file': per_file,
    }, all_epochs


def main():
    seconds_max = int(sys.argv[1]) if len(sys.argv) > 1 else 2400
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_drain.log'))
    time.sleep(1)

    C.log('=== draining via telemetry ===')
    d, tel_epochs = drain(t, seconds_max=seconds_max)
    C.log(json.dumps({k: d[k] for k in
                      ('wall_s', 'requests', 'records', 'unique_epochs',
                       'epoch_min', 'epoch_max', 'duplicates', 'usb_drops')}))
    t.close()

    C.log('=== downloading ground truth ===')
    g, disk_epochs = download_all()
    C.log(json.dumps({k: g[k] for k in
                      ('files_listed', 'files_downloaded', 'total_records',
                       'unique_epochs', 'epoch_min', 'epoch_max')}))

    missing = sorted(disk_epochs - set(tel_epochs))
    extra = sorted(set(tel_epochs) - disk_epochs)
    out = {
        'telemetry': d,
        'ground_truth': g,
        'coverage': {
            'on_disk': len(disk_epochs),
            'via_telemetry': len(tel_epochs),
            'pct': round(100.0 * len(tel_epochs) / max(1, len(disk_epochs)), 2),
            'missing_count': len(missing),
            'missing_first': missing[0] if missing else None,
            'missing_last': missing[-1] if missing else None,
            'extra_count': len(extra),
            'extra_sample': extra[:10],
        },
    }
    C.save('phase_drain.json', out)
    C.log(json.dumps(out['coverage'], indent=1))


if __name__ == '__main__':
    main()
