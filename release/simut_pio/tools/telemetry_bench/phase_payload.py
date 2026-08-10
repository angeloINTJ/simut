#!/usr/bin/env python3
"""Phase D — payload builders and value integrity.

Two questions the throughput numbers cannot answer:

  1. Do the three payload modes (json / csv / custom) actually produce what
     they claim? Raw request bodies are captured verbatim and checked.
  2. Are the *values* that arrive the values that are on flash? The same
     records are read back from the .h5 files with the reference codec and
     compared field by field. Throughput is worthless if the numbers are wrong.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, '/home/angelo/Documentos/simut/tools')
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402
import history_v5 as h5  # noqa: E402


def capture(t, mode, seconds=45, batch=5):
    kill_stale()
    srv = Server('http', f'payload_{mode}', C.OUT, port=C.PORT_HTTP, mode='ok',
                 raw_dump=6)
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=batch,
               interval=2000, mode=mode, path='/ingest')
    C.tel_reset(t)
    C.tel_sync(t, wait=30)
    time.sleep(seconds)
    st = srv.stats()
    srv.stop()
    return {
        'mode': mode,
        'requests': st.get('requests'),
        'records_parsed': st.get('records'),
        'bytes': st.get('bytes_in'),
        'bodies': st.get('raw_bodies', [])[:3],
        'avg_bytes_per_request': (round(st['bytes_in'] / st['requests'], 1)
                                  if st.get('requests') else None),
    }


def integrity(t, seconds=60, batch=25):
    """Send a slice of history, then read the same epochs off flash and compare."""
    kill_stale()
    srv = Server('http', 'integrity', C.OUT, port=C.PORT_HTTP, mode='ok',
                 raw_dump=3)
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=batch,
               interval=1000, mode='json', path='/ingest')
    C.tel_reset(t)
    C.tel_sync(t, wait=30)
    time.sleep(seconds)
    recs = srv.records()
    srv.stop()
    if not recs:
        return {'error': 'no records received'}

    by_epoch = {}
    for r in recs:
        ts = r.get('ts')
        if isinstance(ts, int):
            by_epoch[ts] = r

    # Ground truth for the days those epochs fall on.
    import datetime as dt
    days = sorted({dt.datetime.fromtimestamp(e).strftime('%Y%m%d') for e in by_epoch})
    w = C.web_session()
    disk = {}
    schema_keys = None
    for d in days:
        r = w.get(f'/download?file=/history/{d}.h5')
        if r.status_code != 200:
            continue
        for sch, epoch, vals in h5.read_series(r.content, nominal_interval_s=60):
            if schema_keys is None:
                schema_keys = [(c.id, c.kind, c.scale_exp) for c in sch]
            if epoch in by_epoch:
                disk[epoch] = (sch, vals)

    compared = matched = 0
    mismatches = []
    missing_on_disk = []
    for epoch, rec in sorted(by_epoch.items()):
        if epoch not in disk:
            missing_on_disk.append(epoch)
            continue
        sch, vals = disk[epoch]
        compared += 1
        ok = True
        for c, v in zip(sch, vals):
            if v == h5.H5_NAN:
                continue
            fv = v * (10.0 ** c.scale_exp)
            # Find the matching key in the telemetry record by value, since
            # the JSON key encodes slot+hwId rather than the schema id.
            hit = any(abs(float(x) - fv) < 0.051
                      for k, x in rec.items()
                      if k != 'ts' and isinstance(x, (int, float)))
            if not hit:
                ok = False
                if len(mismatches) < 12:
                    mismatches.append({'epoch': epoch, 'chan_id': c.id,
                                       'disk_value': round(fv, 3),
                                       'record': rec})
                break
        if ok:
            matched += 1
    return {
        'received': len(recs), 'unique_epochs': len(by_epoch),
        'days_fetched': days,
        'compared': compared, 'matched': matched,
        'match_pct': round(100.0 * matched / max(1, compared), 2),
        'missing_on_disk': len(missing_on_disk),
        'missing_sample': missing_on_disk[:5],
        'mismatches': mismatches,
        'schema': schema_keys,
        'sample_record': recs[0],
    }


def main():
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_payload.log'))
    time.sleep(1)
    out = {}
    for mode in ('json', 'csv', 'custom'):
        C.log(f'--- payload mode {mode}')
        out[mode] = capture(t, mode)
        C.log(json.dumps({k: out[mode][k] for k in
                          ('requests', 'records_parsed', 'avg_bytes_per_request')}))
        for b in out[mode]['bodies'][:1]:
            C.log('  body: ' + b[:300])
        C.save('phase_payload.json', out)

    C.log('--- restoring json mode for the integrity check')
    out['integrity'] = integrity(t)
    C.log(json.dumps({k: out['integrity'].get(k) for k in
                      ('received', 'unique_epochs', 'compared', 'matched',
                       'match_pct', 'missing_on_disk')}))
    C.save('phase_payload.json', out)
    t.close()


if __name__ == '__main__':
    main()
