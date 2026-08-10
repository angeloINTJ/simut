#!/usr/bin/env python3
r"""Watch the V5 history cadence over serial, using only the release CLI.

Why this exists: soak_a6.py polls /api/status, which is behind auth, and
soak24.py drives the 55-command CLI. Neither can watch a `pico_w_release`
device whose web credentials are unknown — and release is the profile that
ships, so it is the one worth watching. This asks the device for
`show system log`, which is one of the nine commands the reduced CLI keeps.

What it answers, which is what the per-record .wip snapshot needs proving:

  cadence      one STO_H5_WIP per APP_HISTORY_SAVED. Under the ten-minute
               timer this was one per ten, and the nine records in between
               lived only in RAM.
  seal         the hourly STO_H5_SEALED with ctx=60, and the next block
               restarting its snapshots at ctx=1. This is the branch the
               per-record change restructured, so a clean crossing is the
               point of the run.
  reboot       uptime going backwards. A reboot with no [FTL] is a power
               loss, not a watchdog — the target lives on the PC's USB.
  loss         a snapshot ctx that skips a value, or an adopted .wip whose
               ctx is below the block count that preceded it.

Usage:  python3 tools/history_watch.py <minutes> [out.ndjson]
"""
import glob
import json
import re
import sys
import time

import serial

BYID = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00'
EV = re.compile(
    r'^(\d+)\s+up(\S+)\s+C0 \[(\w+)\]\[(\w+)\s*\] code=(\d+)(?: ctx=(-?\d+))?')


def port():
    m = glob.glob(BYID)
    if not m:
        return None
    return m[0]


def up_seconds(u):
    g = re.match(r'(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?$', u)
    if not g:
        return None
    return (int(g.group(1) or 0) * 3600 + int(g.group(2) or 0) * 60
            + int(g.group(3) or 0))


def read_log(timeout=12):
    """One `show system log`, parsed into events. None if the port is gone."""
    p = port()
    if p is None:
        return None
    try:
        with serial.Serial(p, 115200, timeout=0.4) as s:
            s.dtr = True
            time.sleep(0.3)
            s.reset_input_buffer()
            s.write(b'show system log\r\n')
            s.flush()
            buf, end = [], time.time() + timeout
            while time.time() < end:
                chunk = s.read(4096)
                if chunk:
                    buf.append(chunk.decode('utf-8', 'replace'))
                    if 'SYSTEM LOG END' in buf[-1]:
                        break
    except Exception:
        return None
    out = []
    for line in ''.join(buf).splitlines():
        m = EV.match(line.strip())
        if m:
            out.append({
                'epoch': int(m.group(1)), 'up': up_seconds(m.group(2)),
                'lvl': m.group(3), 'mod': m.group(4).strip(),
                'code': int(m.group(5)),
                'ctx': int(m.group(6)) if m.group(6) is not None else None,
            })
    return out


REC, SEAL, WIP = 510, 566, 567


def last_boot(ev):
    """Events since the most recent boot.

    The log survives reboots and reflashes, so it still holds the previous
    image's entries — under the ten-minute timer those look exactly like the
    defect, because they are it. Analysing the whole file reports the same
    stale findings forever and buries a real one; findings have to be about
    the image running now.
    """
    start = 0
    for i in range(1, len(ev)):
        a, b = ev[i - 1]['up'], ev[i]['up']
        if a is not None and b is not None and b < a:
            start = i
    return ev[start:]


def analyse(ev):
    """Findings only — an empty list is the whole point of the run."""
    f = []
    ev = last_boot(ev)
    recs = [e for e in ev if e['code'] == REC and e['up'] is not None]
    for a, b in zip(recs, recs[1:]):
        if b['up'] is not None and a['up'] is not None and b['up'] < a['up']:
            continue                     # reboot, not a gap in sampling
        if b['epoch'] > 1_600_000_000 and b['epoch'] - a['epoch'] > 90:
            f.append(f"unsampled minute: +{b['epoch'] - a['epoch']}s at "
                     f"epoch {a['epoch']} (up{a['up']}s)")

    # Snapshot ctx must climb by one per record and restart after a seal.
    #
    # recoverWipV5( ) logs the boot adoption under the same STO_H5_WIP code,
    # carrying the count it adopted — so the first snapshot of a boot may be
    # that, and the fresh block legitimately restarting at 1 after it is not a
    # skip. Chaining from it reported correct behaviour as data loss.
    seen_record = False
    prev = None
    for e in ev:
        if e['code'] == REC:
            seen_record = True
        if e['code'] == WIP and not seen_record:
            continue                     # boot adoption, not this block

        if e['code'] == SEAL:
            # A seal below 60 is PARTIAL and legitimate: `reload confirm`, a
            # day rollover and a sensor-set change all close a block early.
            # The ctx is reported as an event, not judged as a finding.
            prev = None
        elif e['code'] == WIP and e['ctx'] is not None and e['ctx'] >= 0:
            if prev is not None and e['ctx'] not in (prev, prev + 1):
                f.append(f"snapshot ctx jumped {prev} -> {e['ctx']} at "
                         f"up{e['up']}s (a skip means records never reached flash)")
            prev = e['ctx']
        elif e['code'] == WIP and e['ctx'] == -1:
            f.append(f"wip_discarded at up{e['up']}s — a block was thrown away")
    return f


def main():
    minutes = float(sys.argv[1]) if len(sys.argv) > 1 else 70
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    end = time.time() + minutes * 60
    seen_seal, last_up, reboots, samples = 0, None, 0, 0

    print(f"[watch] {minutes:.0f} min on {port()}", flush=True)
    while time.time() < end:
        ev = read_log()
        if ev is None:
            print(f"[{time.strftime('%H:%M:%S')}] port absent (reboot in "
                  f"progress?)", flush=True)
            time.sleep(20)
            continue
        samples += 1
        # Scoped to the current boot, like the findings are. Counting the whole
        # log blends the previous image's ten-minute cadence into the ratio and
        # produces a headline number that describes neither firmware.
        cur = last_boot(ev)
        recs = [e for e in cur if e['code'] == REC]
        seals = [e for e in cur if e['code'] == SEAL]
        # Snapshots of this boot's blocks. Emission order per record is WIP
        # then REC, so exactly two WIPs can precede the first record: the boot
        # adoption and that record's own snapshot. Two means the first is the
        # adoption and is not a snapshot of anything this boot recorded; one
        # means there was no .wip to adopt. Dropping everything before the
        # first record instead threw away a real snapshot and read 1.03.
        first_rec = next((i for i, e in enumerate(cur) if e['code'] == REC),
                         len(cur))
        head = [e for e in cur[:first_rec] if e['code'] == WIP]
        wips = [e for e in cur if e['code'] == WIP]
        if len(head) >= 2:
            wips = [e for e in wips if e is not head[0]]
        up = max((e['up'] for e in cur if e['up'] is not None), default=None)
        if last_up is not None and up is not None and up < last_up:
            reboots += 1
            seen_seal = 0            # seals are boot-scoped now; so is the mark
            print(f"[{time.strftime('%H:%M:%S')}] REBOOT (up {last_up}s -> "
                  f"{up}s)", flush=True)
        last_up = up
        if len(seals) > seen_seal:
            for s in seals[seen_seal:]:
                print(f"[{time.strftime('%H:%M:%S')}] SEAL ctx={s['ctx']} at "
                      f"up{s['up']}s", flush=True)
            seen_seal = len(seals)

        findings = analyse(ev)
        ratio = (len(recs) / len(wips)) if wips else None
        line = {'wall': round(time.time()), 'up': up, 'records': len(recs),
                'snapshots': len(wips), 'seals': len(seals),
                'rec_per_snapshot': round(ratio, 2) if ratio else None,
                'reboots': reboots, 'findings': findings}
        print(f"[{time.strftime('%H:%M:%S')}] up={up}s rec={len(recs)} "
              f"wip={len(wips)} seal={len(seals)} "
              f"ratio={line['rec_per_snapshot']} findings={len(findings)}",
              flush=True)
        for x in findings:
            print(f"    ANOMALY {x}", flush=True)
        if out_path:
            with open(out_path, 'a') as fh:
                fh.write(json.dumps(line) + '\n')
        time.sleep(150)

    print(f"[watch] done: {samples} samples, {reboots} reboots, "
          f"{seen_seal} seals", flush=True)


if __name__ == '__main__':
    main()
