#!/usr/bin/env python3
"""Put the bench back the way it was after phase_cadence.py.

The cadence phase fills /history with synthetic days so there is a backlog to
measure against. This removes every synthetic file, re-uploads the real files
that were backed up before the fill, restores the telemetry/history config the
device had, and resets the cursor so the real records are pending again — the
state the device was found in.

Usage:
    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... python3 cadence_cleanup.py \
        --backup DIR --synth DIR [--config '{"t_srv":...}']
"""
import argparse
import glob
import json
import os
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import air_test_suite as A  # noqa: E402

DEV = os.environ.get('SIMUT_DEV', '192.168.3.24')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--backup', required=True, help='dir with the real .h5 files saved before the fill')
    ap.add_argument('--synth', required=True, help='dir with the synthetic .h5 files that were uploaded')
    ap.add_argument('--config', default='', help='JSON of the config to restore (commit_all sys fields)')
    args = ap.parse_args()

    w = A.Web(DEV)
    if w.wait_up(150) is None:
        raise SystemExit('web is down — bring the device to M0 first (air stop over serial)')
    w.login(os.environ['SIMUT_WEB_USER'], os.environ['SIMUT_WEB_PASS'])

    real = {os.path.basename(p) for p in glob.glob(os.path.join(args.backup, '*.h5'))}
    synth = sorted(os.path.basename(p) for p in glob.glob(os.path.join(args.synth, '*.h5')))

    # 1. delete every synthetic file that is not also a real one (those get re-uploaded)
    removed = 0
    for name in synth:
        r = w.post('/api/delete', data={'file': '/history/' + name})
        if r.status_code == 200:
            removed += 1
        else:
            print(f'delete {name}: HTTP {r.status_code} {r.text[:60]}')
        time.sleep(0.8)
    print(f'synthetic files removed: {removed}/{len(synth)}')

    # 2. re-upload the real files (overwrites any synthetic file with the same name)
    for name in sorted(real):
        blob = open(os.path.join(args.backup, name), 'rb').read()
        r = w.post('/api/upload', files={'file': ('history/' + name, blob, 'application/octet-stream')})
        print(f'restore {name}: HTTP {r.status_code}')
        time.sleep(1.0)

    ents = w.get('/api/ls?dir=/history').json().get('entries', [])
    print('history now:', [(e['n'], e['s']) for e in ents])

    # 3. cursor back to "everything real is pending", which is how the device was found
    r = w.post('/api/action', data={'op': 'tel_reset'})
    print('tel_reset:', r.status_code)

    # 4. config
    if args.config:
        fields = json.loads(args.config)
        print('restoring config:', fields)
        w.commit_sys(fields)
        print('commit sent — the device reboots')


if __name__ == '__main__':
    main()
