#!/usr/bin/env python3
"""Enumerate /history by date instead of trusting /api/ls.

`handleApiLs` breaks out of its enumeration loop on `isHandlerOvertime()` (a
6 s budget) and then closes the JSON as if it had finished. Two listings of the
same directory came back with 84 entries each and *different* contents;
`20260523.h5` and `20260524.h5` both download fine (200, different md5) and no
single listing showed both. A client cannot tell a truncated listing from a
complete one, so any inventory built on /api/ls is an undercount of unknown
size.

Asking for each date directly removes the listing from the loop entirely: a
404 means the day is genuinely absent, a 200 means it is there whatever the
listing said.
"""
import datetime as dt
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, '/home/angelo/Documentos/simut/tools')
import campaign as C  # noqa: E402
import history_v5 as h5  # noqa: E402

RAW = os.path.join(C.OUT, 'history_full')
os.makedirs(RAW, exist_ok=True)


def fetch(w, name, tries=3):
    for i in range(tries):
        try:
            r = w.get('/download?file=/history/' + name)
        except Exception:
            time.sleep(1.5)
            continue
        if r.status_code == 200:
            return r.content
        if r.status_code == 404:
            return None
        # 403 = session gone, 503 = HeavyTaskGuard busy: both are retryable
        w = C.web_session(force=True)
        time.sleep(1.5)
    return False   # distinct from None: "could not decide"


def main():
    start = dt.date(2026, 4, 1)
    end = dt.date.today()
    w = C.web_session(force=True)

    present, absent, unknown = [], [], []
    d = start
    while d <= end:
        name = d.strftime('%Y%m%d') + '.h5'
        dst = os.path.join(RAW, name)
        if os.path.exists(dst) and os.path.getsize(dst) > 0:
            present.append((name, os.path.getsize(dst)))
            d += dt.timedelta(days=1)
            continue
        blob = fetch(w, name)
        if blob is None:
            absent.append(name)
        elif blob is False:
            unknown.append(name)
        else:
            with open(dst, 'wb') as fh:
                fh.write(blob)
            present.append((name, len(blob)))
        d += dt.timedelta(days=1)
        time.sleep(0.2)

    epochs = set()
    per_file, errors = [], []
    for name, size in present:
        try:
            blob = open(os.path.join(RAW, name), 'rb').read()
            eps = [ts for _s, ts, _v in h5.read_series(blob, 60)]
            epochs.update(eps)
            per_file.append({'file': name, 'bytes': size, 'records': len(eps),
                             'first': min(eps) if eps else None,
                             'last': max(eps) if eps else None,
                             'md5': hashlib.md5(blob).hexdigest()[:12]})
        except Exception as ex:
            errors.append({'file': name, 'err': f'{type(ex).__name__}: {ex}'})

    # What did /api/ls claim, for the record?
    try:
        listed = [e['n'] for e in w.get('/api/ls?dir=/history').json()['entries']]
    except Exception:
        listed = []

    out = {
        'scanned_days': (end - start).days + 1,
        'files_present': len(present),
        'files_absent': len(absent),
        'files_unknown': unknown,
        'total_bytes': sum(s for _, s in present),
        'total_records': sum(p['records'] for p in per_file),
        'unique_epochs': len(epochs),
        'epoch_min': min(epochs) if epochs else None,
        'epoch_max': max(epochs) if epochs else None,
        'decode_errors': errors,
        'api_ls_reported': len(listed),
        'api_ls_missed': sorted(set(n for n, _ in present) - set(listed)),
        'api_ls_extra': sorted(set(listed) - set(n for n, _ in present)),
        'absent_days': absent,
        'per_file': per_file,
    }
    C.save('history_inventory.json', out)
    C.log(json.dumps({k: out[k] for k in
                      ('files_present', 'files_absent', 'total_bytes',
                       'total_records', 'unique_epochs', 'api_ls_reported',
                       'api_ls_missed', 'files_unknown')}, indent=1))

    # Re-score both drains against the true inventory.
    for src, key in (('phase_drain.json', 'telemetry'),
                     ('phase_drain_full.json', None)):
        p = os.path.join(C.OUT, src)
        if not os.path.exists(p):
            continue
        with open(p) as fh:
            j = json.load(fh)
        n = (j[key] if key else j).get('unique_epochs')
        C.log(f'{src}: {n} epochs = '
              f'{round(100.0 * (n or 0) / max(1, len(epochs)), 2)}% of the true '
              f'{len(epochs)} on flash')


if __name__ == '__main__':
    main()
