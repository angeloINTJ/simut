#!/usr/bin/env python3
"""Phase B2 — drain the WHOLE archive, past the firmware's 30-day floor.

`tel reset` zeroes the cursor, and collectBatch then refuses to look further
back than `lastRecorded − 30 days`:

    if (lastCursor == 0) {
        uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp( );
        if (lastRecorded > 86400UL * 30) lastCursor = lastRecorded - 86400UL * 30;
    }

so anything older is unreachable through telemetry no matter how long it runs.
The floor is a policy in that one branch, not a storage limit — and this proves
it. The cursor lives in a 4-byte file, `/config/t_cursor.bin`; seeding it with
HIST_EPOCH_MIN instead of zero skips the fallback entirely and the device
happily streams the entire archive.

Run order matters: `tel reset` must come first (it clears the RAM cache AND
deletes the file), and the seeded file has to land before the next read.
"""
import json
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402

SEED_EPOCH = 1600000001   # just past HIST_EPOCH_MIN (1.6e9)


def seed_cursor(value):
    """Upload a 4-byte little-endian cursor. /api/upload ignores `dir`, so the
    destination path has to travel in the filename."""
    w = C.web_session()
    blob = struct.pack('<I', value)
    files = {'file': ('/config/t_cursor.bin', blob, 'application/octet-stream')}
    r = w.post('/api/upload', files=files)
    return r.status_code, r.text[:200]


def main():
    seconds_max = int(sys.argv[1]) if len(sys.argv) > 1 else 4200
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_drain_full.log'))
    time.sleep(1)

    srv = Server('http', 'drain_full', C.OUT, port=C.PORT_HTTP, mode='ok')
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=50,
               interval=1000, mode='json', path='/ingest')
    C.tel_reset(t)
    time.sleep(2)
    code, body = seed_cursor(SEED_EPOCH)
    C.log(f'seed /config/t_cursor.bin -> HTTP {code} {body}')
    time.sleep(2)
    # Verify the seed landed where it was aimed. A silently-misplaced upload
    # would leave the 30-day fallback in charge and this run would quietly
    # re-measure the previous phase while claiming to have beaten the cap.
    w = C.web_session()
    try:
        listing = w.get('/api/ls?dir=/config').json().get('entries', [])
    except Exception as e:
        listing = [{'err': str(e)}]
    seed_ok = any(e.get('n') == 't_cursor.bin' and e.get('s') == 4
                  for e in listing)
    C.log(f'/config listing: {listing} seed_ok={seed_ok}')
    C.tel_sync(t, wait=40)

    t0 = time.time()
    last_rec, last_change = 0, time.time()
    timeline = []
    boots0 = t.port_drops
    while time.time() - t0 < seconds_max:
        st = srv.stats()
        n = st.get('records') or 0
        s = C.status()
        timeline.append({
            't': round(time.time() - t0, 1), 'records': n,
            'requests': st.get('requests'),
            'pending': s.get('sys', {}).get('pending'),
            'heap': s.get('sys', {}).get('heap_f'),
            'lb': s.get('metr', {}).get('lb'),
            'tf': s.get('metr', {}).get('tf'), 'tl': s.get('metr', {}).get('tl'),
            'uptime': s.get('sys', {}).get('uptime'),
        })
        if n != last_rec:
            last_rec, last_change = n, time.time()
        elif time.time() - last_change > 120:
            C.log(f'drained: quiet for 120s at {n} records')
            break
        if len(timeline) % 20 == 0:
            C.log(f'  t={timeline[-1]["t"]}s records={n} '
                  f'pending={timeline[-1]["pending"]} heap={timeline[-1]["heap"]}')
        time.sleep(3)

    st = srv.stats()
    recs = srv.records()
    srv.stop()
    t.close()

    epochs = sorted({r['ts'] for r in recs if isinstance(r.get('ts'), int)})
    with_press = [r for r in recs if any(k.startswith('p') for k in r)]
    out = {
        'seed_epoch': SEED_EPOCH,
        'seed_upload_http': code,
        'seed_present_in_fs': seed_ok,
        'wall_s': round(time.time() - t0, 1),
        'requests': st.get('requests'), 'records': st.get('records'),
        'unique_epochs': len(epochs), 'duplicates': len(recs) - len(epochs),
        'bytes_in': st.get('bytes_in'),
        'epoch_min': epochs[0] if epochs else None,
        'epoch_max': epochs[-1] if epochs else None,
        'records_with_pressure': len(with_press),
        'first_pressure_record': with_press[0] if with_press else None,
        'usb_drops': t.port_drops - boots0,
        'timeline': timeline,
    }
    if out['wall_s']:
        out['records_per_s'] = round((out['records'] or 0) / out['wall_s'], 2)
    C.save('phase_drain_full.json', out)

    # Compare against the ground truth the 30-day drain already collected.
    prev = os.path.join(C.OUT, 'phase_drain.json')
    if os.path.exists(prev):
        with open(prev) as fh:
            p = json.load(fh)
        disk = p['ground_truth']
        out['vs_disk'] = {
            'disk_unique_epochs': disk['unique_epochs'],
            'full_drain_unique': len(epochs),
            'pct': round(100.0 * len(epochs) / max(1, disk['unique_epochs']), 2),
            'thirty_day_drain_unique': p['telemetry']['unique_epochs'],
            'thirty_day_pct': p['coverage']['pct'],
        }
        C.save('phase_drain_full.json', out)
        C.log(json.dumps(out['vs_disk'], indent=1))
    C.log(json.dumps({k: out[k] for k in
                      ('wall_s', 'requests', 'records', 'unique_epochs',
                       'epoch_min', 'epoch_max', 'records_per_s', 'usb_drops')}))


if __name__ == '__main__':
    main()
