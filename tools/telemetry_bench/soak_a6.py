#!/usr/bin/env python3
"""Acceptance A6 — long soak on the image that actually ships.

A6 asks for "heartbeat/WDT without regression" over 72 h. soak24.py samples the
serial CLI, which means it can only run against pico_w_test — and certifying a
profile nobody installs is not certifying anything. This one reads /api/status,
which the release image serves, and which carries the Core-1 lifecycle since
the fields were added for this run.

What counts as a finding, and why each one:

  reboot        uptime went backwards. The only unambiguous failure. A reboot
                with no [FTL] in the boot log is a power loss, not a watchdog —
                the target lives on the PC's USB, so the two are distinguished
                by the log, never by the counter.
  core1 death   c1kl/c1kh/c1kq rising, or c1n above its starting value. This is
                the R1 class. It is silent from every other angle.
  frozen beat   c1a above the threshold. Tens of ms is healthy; seconds means
                Core 1 is frozen, killed or parked.
  exposure      fx above 0. Any value names a flash write missing its
                Core1FlashPause. It has been 0 all campaign; a first non-zero
                is a regression, not noise.
  heap floor    heap_lb (largest contiguous block) is what BearSSL needs, not
                total free. Fragmentation shows up here first.

Sampling is every 5 min. Anything anomalous is written to the log with an
ANOMALY prefix so grep is a valid triage. The run is resumable and additive:
it appends to the ndjson, so an interrupted soak keeps its hours.

Stop early:  kill $(pgrep -f "telemetry_bench/soak_a6")  — the summary is
rebuilt from the ndjson. Do NOT use `pkill -f soak_a6.py`: the pattern also
matches the shell that launched it, so the whole command line dies mid-way and
whatever was meant to run after the kill silently never does.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', '..', 'scratchpad'))
from dev import Web  # noqa: E402

try:
    from rig_secrets import PASS
except ImportError:
    PASS = os.environ.get('SIMUT_PASS', '')

PERIOD_S = 300
BEAT_FROZEN_MS = 2000
OUT_DIR = os.environ.get('SOAK_OUT', 'scratchpad/telbench/results/soak_a6')
OUT = os.path.join(OUT_DIR, 'soak_a6.ndjson')

METR = ('lb', 'lbm', 'hm', 'wf', 'mq', 'ts', 'tf', 'tr', 'tl',
        'so', 'se', 'cs', 'fo', 'fom', 'f50', 'fx',
        'c1a', 'c1n', 'c1kl', 'c1kh', 'c1kq', 'c1s', 'cgd', 'cgg', 'cgx')
SYS = ('uptime', 'heap_f', 'heap_lb', 'fs_u', 'pending', 'rssi', 'ntp')


def session():
    w = Web(timeout=20)
    ok, info = w.login('admin', PASS)
    if not ok:
        raise RuntimeError(f'login failed: {info}')
    return w


class SessionExpired(Exception):
    """The device answered; this end was not authorised to read it."""


def sample(w):
    r = w.get('/api/status')
    j = json.loads(r.text)
    # A device that answers {"error": "Forbidden"} or {"error": "Too Fast"} is
    # a healthy device refusing THIS request. Letting that land in the same
    # bucket as "no answer at all" would put an instrument problem in the
    # findings column, which is the one mistake this soak exists to not make.
    if 'sys' not in j:
        raise SessionExpired(str(j.get('error', j))[:80])
    d = {'wall': time.time()}
    d.update({k: j['sys'].get(k) for k in SYS})
    d.update({k: j['metr'].get(k) for k in METR})
    d['sensors_reading'] = sum(1 for s in j.get('sensors', [])
                               if s.get('val') not in (None, 'null'))
    return d


def judge(prev, cur, base):
    """Returns a list of anomaly strings. Empty means the sample is clean."""
    out = []
    if prev and cur['uptime'] < prev['uptime']:
        out.append(f'REBOOT uptime {prev["uptime"]} -> {cur["uptime"]}')
    for k, label in (('c1kl', 'lockout'), ('c1kh', 'health'), ('c1kq', 'quiet')):
        if base.get(k) is not None and cur.get(k, 0) > base[k]:
            out.append(f'CORE1_KILL_{label} {base[k]} -> {cur[k]}')
    if base.get('c1n') is not None and cur.get('c1n', 0) > base['c1n']:
        out.append(f'CORE1_RELAUNCH {base["c1n"]} -> {cur["c1n"]}')
    if cur.get('c1a') is not None and cur['c1a'] > BEAT_FROZEN_MS:
        out.append(f'CORE1_BEAT_FROZEN {cur["c1a"]} ms')
    if cur.get('fx', 0):
        out.append(f'FLASH_EXPOSED fx={cur["fx"]}')
    if cur.get('c1s', 0):
        out.append(f'LOCKOUT_STUCK c1s={cur["c1s"]}')
    return out


def summarise(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    if not rows:
        return 'no samples'
    span_h = (rows[-1]['wall'] - rows[0]['wall']) / 3600.0
    # Rows without 'uptime' are gaps — a read that never landed, or a session
    # this end had to renew. They are counted, never silently dropped, and
    # never compared: an earlier version indexed them blind and crashed with
    # KeyError, which meant the summary died exactly when there was finally
    # something in it to read.
    good = [r for r in rows if 'uptime' in r]
    gaps = len(rows) - len(good)
    if not good:
        return f'{len(rows)} samples, none of them readable ({gaps} gaps)'
    reboots = sum(1 for i in range(1, len(good))
                  if good[i]['uptime'] < good[i - 1]['uptime'])
    beats = [r['c1a'] for r in good if r.get('c1a') is not None]
    lbs = [r['heap_lb'] for r in good if r.get('heap_lb') is not None]
    last = good[-1]
    return '\n'.join([
        f'samples      {len(good)} over {span_h:.1f} h'
        + (f'   ({gaps} unreadable — see gaps below)' if gaps else ''),
        f'reboots      {reboots}',
        f'core1 kills  lockout={last.get("c1kl")} health={last.get("c1kh")} '
        f'quiet={last.get("c1kq")} launches={last.get("c1n")} stuck={last.get("c1s")}',
        f'beat age     max {max(beats) if beats else "?"} ms, '
        f'last {last.get("c1a")} ms',
        f'fx           {last.get("fx")}',
        f'heap largest min {min(lbs) if lbs else "?"} B, last {last.get("heap_lb")} B',
        f'wifi reconn  {last.get("wf")}   tel sent/failed {last.get("ts")}/{last.get("tf")}',
        f'uptime       {last.get("uptime")} ms',
    ] + ([''] + ['gaps:'] + [
        f'  {time.strftime("%d/%m %H:%M", time.localtime(r["wall"]))}  '
        f'{"session renewed" if r.get("session_expired") else "no answer"}: '
        f'{str(r.get("read_error", ""))[:70]}'
        for r in rows if 'uptime' not in r
    ] if gaps else []))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    if '--summary' in sys.argv:
        print(summarise(OUT))
        return 0

    w = session()
    base = sample(w)
    print(f'baseline: uptime={base["uptime"]} c1a={base["c1a"]} c1n={base["c1n"]} '
          f'fx={base["fx"]} heap_lb={base["heap_lb"]}')
    print(f'logging to {OUT}, every {PERIOD_S} s — stop with kill $(pgrep -f "telemetry_bench/soak_a6")')

    prev = None
    n = 0
    while True:
        try:
            cur = sample(w)
        except SessionExpired as exc:
            # The device answered and refused us. Renew and retry immediately —
            # waiting a whole period would turn a 2-second instrument problem
            # into a 5-minute hole in the record.
            try:
                w = session()
                cur = sample(w)
            except Exception as exc2:
                cur = {'wall': time.time(), 'session_expired': True,
                       'read_error': f'{exc} -> renew failed: {exc2}'[:200]}
                with open(OUT, 'a') as fh:
                    fh.write(json.dumps(cur) + '\n')
                print(f'[{time.strftime("%H:%M:%S")}] INSTRUMENT session renew '
                      f'failed: {exc2}'[:160], flush=True)
                time.sleep(PERIOD_S)
                continue
            print(f'[{time.strftime("%H:%M:%S")}] instrument: session renewed '
                  f'({exc})', flush=True)
        except Exception as exc:
            # No answer at all. This one IS device data — it may be rebooting.
            cur = {'wall': time.time(), 'read_error': str(exc)[:200]}
            try:
                w = session()
            except Exception:
                pass
            with open(OUT, 'a') as fh:
                fh.write(json.dumps(cur) + '\n')
            print(f'[{time.strftime("%H:%M:%S")}] ANOMALY no answer: {exc}'[:160],
                  flush=True)
            time.sleep(PERIOD_S)
            continue

        flags = judge(prev, cur, base)
        cur['anomalies'] = flags
        with open(OUT, 'a') as fh:
            fh.write(json.dumps(cur) + '\n')
        n += 1
        stamp = time.strftime('%H:%M:%S')
        if flags:
            print(f'[{stamp}] ANOMALY ' + ' | '.join(flags), flush=True)
        elif n % 12 == 1:   # one line per hour when nothing is wrong
            print(f'[{stamp}] up={cur["uptime"] // 1000}s beat={cur["c1a"]}ms '
                  f'lb={cur["heap_lb"]} fx={cur["fx"]} wf={cur["wf"]} '
                  f'sens={cur["sensors_reading"]}', flush=True)
        prev = cur
        time.sleep(PERIOD_S)


if __name__ == '__main__':
    sys.exit(main())
