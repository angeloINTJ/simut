#!/usr/bin/env python3
"""Bit-exact parity gate between the firmware V5 codec and the reference.

Generates a corpus of pseudo-random series, runs both implementations over it
and requires them to agree on every byte of every chunk and on every decoded
value. This is the WP2 gate: the firmware and `tools/history_v5.py` are two
independent implementations of one document, and only a diff proves it.

Run: python3 tools/check_history_v5_parity.py [--cases 20000] [--seed 1]
"""

from __future__ import annotations

import argparse
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import history_v5 as h5  # noqa: E402

KINDS = [h5.H5_KIND_TEMP_C, h5.H5_KIND_HUM_PCT, h5.H5_KIND_PRESS_HPA,
         h5.H5_KIND_CO2_PPM, h5.H5_KIND_VOC_IDX, h5.H5_KIND_GENERIC]

MODES = ('ramp', 'step', 'noise', 'flat', 'nan', 'sparse')


def make_case(rnd):
    """One series, shaped like something a real device could produce."""
    n = rnd.randint(1, h5.H5_MAX_CHANNELS)
    nominal = rnd.choice((30, 60, 60, 60, 120, 300))
    count = rnd.randint(1, h5.H5_BLOCK_MAX_RECORDS)
    mode = rnd.choice(MODES)
    schema = [h5.ChannelDesc(rnd.randint(0, 63), rnd.choice(KINDS),
                             rnd.randint(-3, 1), 0) for _ in range(n)]

    t = rnd.randint(1_600_000_000, 1_900_000_000)
    cur = [rnd.randint(-32767, 32767) for _ in range(n)]
    records = [(t, list(cur))]
    for _ in range(1, count):
        if mode == 'ramp':
            cur = [max(-32767, min(32767, v + rnd.randint(-3, 3))) for v in cur]
        elif mode == 'step':
            cur = [max(-32767, min(32767, v + rnd.choice((0, 0, 0, 0, 900, -900))))
                   for v in cur]
        elif mode == 'noise':
            cur = [rnd.randint(-32767, 32767) for _ in cur]
        elif mode == 'flat':
            pass
        elif mode == 'nan':
            cur = [h5.H5_NAN if rnd.random() < 0.3
                   else (rnd.randint(-32767, 32767) if v == h5.H5_NAN
                         else max(-32767, min(32767, v + rnd.randint(-40, 40))))
                   for v in cur]
        else:  # sparse: mostly unchanged, occasional wide jump
            cur = [v if rnd.random() < 0.9
                   else max(-32767, min(32767, v + rnd.randint(-20000, 20000)))
                   for v in cur]
        step = rnd.choice((nominal, nominal, nominal, nominal - 1, nominal + 1,
                           nominal + 3, 2 * nominal, 3600, 20000))
        if t + step - records[0][0] > 0xFFFF:
            break                      # the encoder would refuse it too
        t += step
        records.append((t, list(cur)))
    return schema, nominal, records


def pack_corpus(cases):
    out = bytearray(struct.pack('<I', len(cases)))
    for schema, nominal, records in cases:
        n = len(schema)
        out += struct.pack('<BHB', n, nominal, len(records))
        for d in schema:
            out += struct.pack('<BBbB', d.id, d.kind, d.scale_exp, d.flags)
        for epoch, values in records:
            out += struct.pack('<I', epoch)
            out += struct.pack(f'<{n}h', *values)
    return bytes(out)


def unpack_results(blob, cases):
    pos = 0
    (n_cases,) = struct.unpack_from('<I', blob, pos)
    pos += 4
    if n_cases != len(cases):
        raise SystemExit(f'harness returned {n_cases} cases, expected {len(cases)}')
    out = []
    for schema, _nominal, _records in cases:
        n = len(schema)
        (clen,) = struct.unpack_from('<I', blob, pos)
        pos += 4
        chunk = blob[pos:pos + clen]
        pos += clen
        (dcount,) = struct.unpack_from('<B', blob, pos)
        pos += 1
        decoded = []
        for _ in range(dcount):
            (epoch,) = struct.unpack_from('<I', blob, pos)
            pos += 4
            vals = list(struct.unpack_from(f'<{n}h', blob, pos))
            pos += 2 * n
            decoded.append((epoch, vals))
        out.append((chunk, decoded))
    if pos != len(blob):
        raise SystemExit(f'harness output has {len(blob) - pos} trailing bytes')
    return out


def build_harness(workdir):
    src = os.path.join(HERE, 'h5_parity', 'h5_parity.cpp')
    core = os.path.join(HERE, '..', 'src', 'HistoryV5.cpp')
    exe = os.path.join(workdir, 'h5_parity')
    cmd = ['g++', '-std=gnu++17', '-O2', '-Wall', '-Wextra',
           '-I', os.path.join(HERE, '..', 'src'),
           '-I', os.path.join(HERE, '..', 'test', 'native_stubs'),
           src, core, '-o', exe]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout)
        print(p.stderr, file=sys.stderr)
        raise SystemExit('could not build the parity harness')
    if p.stderr.strip():
        print('build warnings:', file=sys.stderr)
        print(p.stderr, file=sys.stderr)
    return exe


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cases', type=int, default=20000)
    ap.add_argument('--seed', type=int, default=20260731)
    args = ap.parse_args()

    # The phone converter ships its own copy of the reference — Pydroid gives
    # it no way to import from anywhere else. A copy left behind would turn
    # V4 archives into whatever the format USED to be, silently, on the one
    # tool that runs with nothing to compare against. So the parity gate also
    # refuses to pass while the two files differ.
    bundled = os.path.join(HERE, 'conversor_v4_v5', 'history_v5.py')
    if os.path.exists(bundled):
        with open(os.path.join(HERE, 'history_v5.py'), 'rb') as fh:
            canon = fh.read()
        with open(bundled, 'rb') as fh:
            copy = fh.read()
        if copy != canon:
            print('tools/conversor_v4_v5/history_v5.py differs from '
                  'tools/history_v5.py — refresh the copy (and the .zip).')
            print('PARITY FAIL')
            return 1

    rnd = random.Random(args.seed)
    cases = [make_case(rnd) for _ in range(args.cases)]

    with tempfile.TemporaryDirectory() as tmp:
        exe = build_harness(tmp)
        corpus = os.path.join(tmp, 'corpus.bin')
        result = os.path.join(tmp, 'result.bin')
        with open(corpus, 'wb') as fh:
            fh.write(pack_corpus(cases))
        p = subprocess.run([exe, corpus, result], capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stderr, file=sys.stderr)
            raise SystemExit(f'harness exited {p.returncode}')
        results = unpack_results(open(result, 'rb').read(), cases)

    bad_bytes = bad_decode = bad_cross = 0
    total_records = 0
    for i, ((schema, nominal, records), (fw_chunk, fw_decoded)) in enumerate(
            zip(cases, results)):
        total_records += len(records)

        # 1. Same bytes out of both encoders.
        enc = h5.BlockEncoder(schema, nominal)
        enc.reset(records[0][0], records[0][1])
        for epoch, values in records[1:]:
            enc.add(epoch, values)
        ref_chunk = enc.seal(0)
        if ref_chunk != fw_chunk:
            bad_bytes += 1
            if bad_bytes <= 3:
                print(f'case {i}: chunk differs '
                      f'(ref {len(ref_chunk)} B, fw {len(fw_chunk)} B, '
                      f'nCh={len(schema)} count={len(records)})')
                for k in range(min(len(ref_chunk), len(fw_chunk))):
                    if ref_chunk[k] != fw_chunk[k]:
                        print(f'  first difference at byte {k}: '
                              f'ref 0x{ref_chunk[k]:02X} fw 0x{fw_chunk[k]:02X}')
                        break
            continue

        # 2. The firmware decoder reproduces the input exactly.
        if fw_decoded != records:
            bad_decode += 1
            if bad_decode <= 3:
                print(f'case {i}: firmware decode differs from the input')

        # 3. The reference decoder agrees with the firmware's own bytes.
        if h5.decode_block(fw_chunk, schema, nominal) != records:
            bad_cross += 1
            if bad_cross <= 3:
                print(f'case {i}: reference decode of the firmware chunk differs')

    print(f'{args.cases} cases, {total_records} records')
    print(f'  encoder bytes differ: {bad_bytes}')
    print(f'  firmware decode differs: {bad_decode}')
    print(f'  reference decode of firmware bytes differs: {bad_cross}')
    ok = not (bad_bytes or bad_decode or bad_cross)
    print('PARITY', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
