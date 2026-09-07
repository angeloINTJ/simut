#!/usr/bin/env python3
"""Put a directory of synthetic days on the device, and check them first.

The days themselves come from `tools/gen_synth_history.py`, which draws varied
plausible shapes from a real device's schema — do not generate them here, and
above all do not synthesise V5 blocks by hand: the schema, the anchor layout
and the CRC are firmware-side details, and a file this bench got subtly wrong
is dropped by the reader without a word. What was missing was the other half:
getting those days onto the device, and being able to say a file is sound
before and after the trip.

    python3 ../gen_synth_history.py --schema-from 20260906.h5 --out /tmp/synth \
        --end-date 2026-09-06 --days 28
    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... python3 synth_upload.py upload --dir /tmp/synth
    # after the measurement
    python3 cadence_cleanup.py --backup <real .h5 dir> --synth /tmp/synth ...

`fetch` pulls a real day off the device (the schema source, and a sample to
check against); `verify` recomputes every DATA CRC in a file.
"""

import argparse
import glob
import os
import struct
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'tools'))
import air_test_suite as A  # noqa: E402

DEV = os.environ.get('SIMUT_DEV', '192.168.3.24')
H5_MAGIC = 0x4835  # 'H5' little-endian, as HistoryV5.h spells it
DAY = 86400


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def chunks(blob):
    """Walk the file: (offset, length, type, nCh) per chunk, SCHEMA and DATA."""
    off = 0
    while off + 8 <= len(blob):
        magic, ver, typ, flags, a, b, rsv = struct.unpack_from('<HBBBBBB', blob, off)
        if magic != H5_MAGIC:
            return
        if typ == 1:                       # SCHEMA: preamble(8) + 4 per channel + crc(2)
            ln = 8 + 4 * a + 2
        elif typ == 2:                     # DATA: preamble(8) | t0(4) | len(2) | crc(2) | anchors | payload
            plen = struct.unpack_from('<H', blob, off + 12)[0]
            ln = 16 + 6 * b + plen
        else:
            return
        if off + ln > len(blob):
            return
        yield off, ln, typ, b
        off += ln


def verify(blob):
    """Recompute every DATA CRC — a file that fails this would be dropped silently."""
    n_data, bad, t0s = 0, 0, []
    for off, ln, typ, _n in chunks(blob):
        if typ != 2:
            continue
        n_data += 1
        c = blob[off:off + ln]
        want = struct.unpack_from('<H', c, 14)[0]
        got = crc16(bytes(c[:14]))
        got = crc16(bytes(c[16:]), got)
        bad += (want != got)
        t0s.append(struct.unpack_from('<I', c, 8)[0])
    return n_data, bad, (min(t0s) if t0s else None), (max(t0s) if t0s else None)


def web():
    w = A.Web(DEV)
    if w.wait_up(150) is None:
        raise SystemExit('web is down — bring the device to M0 first (air stop over serial)')
    w.login(os.environ['SIMUT_WEB_USER'], os.environ['SIMUT_WEB_PASS'])
    return w


def cmd_fetch(args):
    w = web()
    ents = w.get('/api/ls?dir=/history').json().get('entries', [])
    days = sorted(e['n'] for e in ents if e['n'].endswith('.h5'))
    if not days:
        raise SystemExit('no .h5 on the device')
    name = args.day or max(days, key=lambda n: next(e['s'] for e in ents if e['n'] == n))
    blob = w.get('/api/download?file=/history/' + name).content
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, name)
    open(path, 'wb').write(blob)
    n, bad, lo, hi = verify(blob)
    print(f'{path}: {len(blob)} B, {n} data blocks, {bad} bad CRC')
    return path


def cmd_verify(args):
    blob = open(args.file, 'rb').read()
    n, bad, lo, hi = verify(blob)
    span = (f'{time.strftime("%d/%m %H:%M", time.localtime(lo))} → '
            f'{time.strftime("%d/%m %H:%M", time.localtime(hi))}') if lo else 'no data blocks'
    print(f'{os.path.basename(args.file)}: {len(blob)} B, {n} data blocks, {bad} bad CRC, {span}')
    if bad or not n:
        raise SystemExit(1)


def cmd_upload(args):
    w = web()
    files = sorted(glob.glob(os.path.join(args.dir, '*.h5')))
    ok = 0
    for p in files:
        n = os.path.basename(p)
        r = w.post('/api/upload', files={'file': ('history/' + n, open(p, 'rb').read(),
                                                  'application/octet-stream')})
        ok += (r.status_code == 200)
        if r.status_code != 200:
            print(f'{n}: HTTP {r.status_code} {r.text[:60]}')
        time.sleep(0.8)
    ents = w.get('/api/ls?dir=/history').json().get('entries', [])
    print(f'uploaded {ok}/{len(files)}; history now {len(ents)} files, '
          f'{sum(e["s"] for e in ents)} B')


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    f = sub.add_parser('fetch', help='download one real day off the device')
    f.add_argument('--day', default='', help='file name, e.g. 20260906.h5 (default: the biggest)')
    f.add_argument('--out', default='.', help='directory to write it to')
    v = sub.add_parser('verify', help='recompute every DATA CRC in a file')
    v.add_argument('--file', required=True)
    u = sub.add_parser('upload', help='upload every .h5 in a directory')
    u.add_argument('--dir', required=True)
    args = ap.parse_args()
    {'fetch': cmd_fetch, 'verify': cmd_verify, 'upload': cmd_upload}[args.cmd](args)


if __name__ == '__main__':
    main()
