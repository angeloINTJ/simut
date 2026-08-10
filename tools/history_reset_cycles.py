#!/usr/bin/env python3
r"""Abrupt-reset cycles against the V5 history, driven by the PicoHand.

What this is, and what it is NOT. The hand pulls the target's RUN pin (GP0), so
the reset is a hardware one: it does not pass through safeReboot( ), so neither
the seal nor the pre-reboot hook runs, and unlike a 1200 bps touch it neither
perturbs the USB CDC nor lands the target in BOOTSEL. That makes it the closest
available stand-in for a power cut — but it is a stand-in. **This does not
satisfy A6/A4's power-loss criterion**, which needs the USB supply actually
removed, because the target is powered from the PC's USB and the hand only
drives RESET.

Each cycle measures the two things that can lose a measurement, which are
independent and were fixed separately:

  storage   records held in the open block vs records the next boot adopts from
            /history/.wip. Equal means the abrupt reset cost nothing.
  sampling  seconds between the last record before the reset and the first one
            after. Over 120 s means a record slot went unsampled — the block
            can be perfect and this still fail, which is exactly how the
            first-record-of-a-boot delay hid behind a working snapshot.

The reset instant is walked across the minute on purpose: a fixed phase would
only ever probe one point of the interval, and the deferral window is phase
dependent.

Usage:  python3 tools/history_reset_cycles.py [cycles] [out.json]
"""
import glob
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import history_watch as hw                                     # noqa: E402

HAND_SH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'PicoHand', 'pico_hand.sh')
REC, SEAL, WIP = hw.REC, hw.SEAL, hw.WIP


def hand(cmd):
    """One hand command through the wrapper, which owns port detection."""
    r = subprocess.run(['bash', '-c',
                        f'source {HAND_SH} >/dev/null 2>&1; '
                        f'hand_init >/dev/null 2>&1; hand {cmd}'],
                       capture_output=True, text=True, timeout=30)
    return r.stdout.strip()


def target_present():
    return bool(glob.glob(hw.BYID))


def wait_port(present, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        if target_present() == present:
            return True
        time.sleep(0.5)
    return False


def block_state():
    """(records in the open block, last record epoch) for the current boot."""
    ev = hw.read_log(timeout=14)
    if ev is None:
        return None
    cur = hw.last_boot(ev)
    recs = [e for e in cur if e['code'] == REC and e['epoch'] > 1_600_000_000]
    # The block count is the newest snapshot ctx; a seal after it means the
    # block closed and a fresh one started, so anything before is stale.
    count = None
    for e in cur:
        if e['code'] == SEAL:
            count = 0
        elif e['code'] == WIP and e['ctx'] is not None and e['ctx'] >= 0:
            count = e['ctx']
    return {'count': count,
            'last_epoch': recs[-1]['epoch'] if recs else None,
            'up': max((e['up'] for e in cur if e['up'] is not None),
                      default=None)}


def boot_state():
    """(adopted count, first record epoch) of the boot that just happened."""
    ev = hw.read_log(timeout=14)
    if ev is None:
        return None
    cur = hw.last_boot(ev)
    first_rec = next((i for i, e in enumerate(cur) if e['code'] == REC),
                     len(cur))
    head = [e for e in cur[:first_rec] if e['code'] == WIP]
    recs = [e for e in cur if e['code'] == REC and e['epoch'] > 1_600_000_000]
    return {'adopted': head[0]['ctx'] if len(head) >= 2 else (
                head[0]['ctx'] if len(head) == 1 and not recs else None),
            'first_epoch': recs[0]['epoch'] if recs else None,
            'first_up': recs[0]['up'] if recs else None}


def main():
    cycles = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    out_path = sys.argv[2] if len(sys.argv) > 2 else None

    print(f"hand: {hand('PING')}   {hand('STATUS')}", flush=True)
    if not target_present():
        raise SystemExit('target absent')

    results = []
    for i in range(1, cycles + 1):
        want = 1 + (i % 3)              # 1..3 records in the block
        phase = (i % 4) * 15            # 0/15/30/45 s past the last record
        print(f"\n=== cycle {i}/{cycles}: reset with {want} record(s), "
              f"{phase}s into the minute", flush=True)

        # Wait for the block to hold `want` records.
        deadline = time.time() + 300
        st = None
        while time.time() < deadline:
            st = block_state()
            if st and st['count'] is not None and st['count'] >= want:
                break
            time.sleep(10)
        if not st or st['count'] is None:
            print("   could not read block state; skipping", flush=True)
            continue
        if phase:
            time.sleep(phase)
            st = block_state() or st

        print(f"   before: block={st['count']} rec last_epoch={st['last_epoch']}"
              f" up={st['up']}s", flush=True)

        r = hand('RESET')
        print(f"   hand RESET -> {r}", flush=True)
        if not wait_port(False, timeout=20):
            print("   WARN target port never vanished — reset may not have "
                  "landed", flush=True)
        wait_port(True, timeout=90)
        time.sleep(45)                  # let the first record of the boot land

        bt = boot_state()
        if not bt:
            print("   could not read boot state", flush=True)
            continue
        gap = (bt['first_epoch'] - st['last_epoch']
               if bt['first_epoch'] and st['last_epoch'] else None)
        lost_block = (st['count'] - bt['adopted']
                      if bt['adopted'] is not None else None)
        dropped = (max(0, round(gap / 60) - 1) if gap else None)
        ok = (lost_block == 0) and (dropped == 0)

        row = {'cycle': i, 'block_before': st['count'],
               'adopted': bt['adopted'], 'records_lost': lost_block,
               'gap_s': gap, 'first_up': bt['first_up'],
               'slots_unsampled': dropped, 'pass': ok}
        results.append(row)
        print(f"   after : adopted={bt['adopted']} first_up={bt['first_up']}s "
              f"gap={gap}s", flush=True)
        print(f"   -> block lost {lost_block} | unsampled slots {dropped} | "
              f"{'PASS' if ok else 'FAIL'}", flush=True)
        if out_path:
            with open(out_path, 'w') as fh:
                json.dump(results, fh, indent=1)

    p = sum(1 for r in results if r['pass'])
    print(f"\n==== {p}/{len(results)} cycles clean", flush=True)
    bad = [r for r in results if not r['pass']]
    for r in bad:
        print(f"   FAIL cycle {r['cycle']}: {r}", flush=True)
    return 0 if results and not bad else 1


if __name__ == '__main__':
    sys.exit(main())
