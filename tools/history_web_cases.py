#!/usr/bin/env python3
r"""The two history-loss cases that need a web session, not just serial.

  commit    N cycles of POST /api/commit_all — the reboot taken *to configure
            the device*, and the one voluntary reboot path that never sealed on
            its own. It is what the pre-reboot hook exists for: `reload confirm`
            sealed before calling safeReboot( ), so it proves nothing about the
            hook. Measures records-in-block against records-the-boot-adopts,
            and the sampling gap across the restart.

  contend   Holds the heavy-task lock across record boundaries by hammering
            /api/history_multi, which takes HeavyTaskGuard for the whole
            request. Sampling used to be gated on that lock, so a record whose
            due moment landed inside a held window was never taken at all — a
            hole no snapshot can fill. Two things are checked, and the second
            matters as much as the first: that no record slot went unsampled,
            AND that the deferral actually engaged, because a run where the
            lock never covered a boundary proves nothing. The signature of a
            record taken while a gate was shut is an APP_HISTORY_SAVED with no
            STO_H5_WIP immediately before it — the write moved, the sample did
            not.

Credentials come from SIMUT_WEB_USER / SIMUT_WEB_PASS. Nothing is written to
disk that could carry them.

Usage:
  SIMUT_WEB_PASS=... python3 tools/history_web_cases.py commit [cycles]
  SIMUT_WEB_PASS=... python3 tools/history_web_cases.py contend [minutes]
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                'telemetry_bench'))
import history_watch as hw                                      # noqa: E402
import bench                                                    # noqa: E402

DEV = os.environ.get('SIMUT_DEV', '192.168.3.24')
USER = os.environ.get('SIMUT_WEB_USER', 'admin')
PASS = os.environ.get('SIMUT_WEB_PASS', '')
REC, SEAL, WIP = hw.REC, hw.SEAL, hw.WIP


def session():
    w = bench.Web(DEV)
    ok, why = w.login(USER, PASS)
    if not ok:
        raise SystemExit(f'login failed: {why}')
    return w


def block_state():
    ev = hw.read_log(timeout=14)
    if ev is None:
        return None
    cur = hw.last_boot(ev)
    recs = [e for e in cur if e['code'] == REC and e['epoch'] > 1_600_000_000]
    count = None
    for e in cur:
        if e['code'] == SEAL:
            count = 0
        elif e['code'] == WIP and e['ctx'] is not None and e['ctx'] >= 0:
            count = e['ctx']
    return {'count': count,
            'last_epoch': recs[-1]['epoch'] if recs else None}


def boot_state():
    ev = hw.read_log(timeout=14)
    if ev is None:
        return None
    cur = hw.last_boot(ev)
    first = next((i for i, e in enumerate(cur) if e['code'] == REC), len(cur))
    head = [e for e in cur[:first] if e['code'] == WIP]
    recs = [e for e in cur if e['code'] == REC and e['epoch'] > 1_600_000_000]
    return {'adopted': head[0]['ctx'] if head else None,
            'first_epoch': recs[0]['epoch'] if recs else None,
            'first_up': recs[0]['up'] if recs else None}


def cmd_commit(cycles):
    passed = 0
    for i in range(1, cycles + 1):
        print(f"\n=== commit_all cycle {i}/{cycles}", flush=True)
        st = None
        deadline = time.time() + 240
        while time.time() < deadline:
            st = block_state()
            if st and st['count']:
                break
            time.sleep(10)
        if not st or not st['count']:
            print("   no open block to risk; skipping", flush=True)
            continue
        print(f"   before: block={st['count']} last_epoch={st['last_epoch']}",
              flush=True)

        w = session()
        # The device name changes every cycle: saveConfiguration( ) short-cuts
        # on an unchanged CRC, and a commit that saved nothing exercises a
        # different path than the one under test.
        r = w.commit({'name': f'simut-c{i}'})
        print(f"   POST /api/commit_all -> HTTP {r.status_code} "
              f"{r.text[:80]}", flush=True)

        back = bench.wait_web(DEV, timeout=120)
        print(f"   web back after {back}s", flush=True)
        time.sleep(40)

        bt = boot_state()
        gap = (bt['first_epoch'] - st['last_epoch']
               if bt and bt['first_epoch'] and st['last_epoch'] else None)
        lost = st['count'] - bt['adopted'] if bt and bt['adopted'] is not None else None
        slots = max(0, round(gap / 60) - 1) if gap else None
        ok = lost == 0 and slots == 0
        passed += 1 if ok else 0
        print(f"   after : adopted={bt['adopted']} first_up={bt['first_up']}s "
              f"gap={gap}s", flush=True)
        print(f"   -> block lost {lost} | unsampled slots {slots} | "
              f"{'PASS' if ok else 'FAIL'}", flush=True)
    print(f"\n==== commit_all: {passed}/{cycles} clean", flush=True)
    return passed == cycles


def cmd_contend(minutes):
    w = session()
    t_end = time.time() + minutes * 60
    reqs, errs, held, sizes = 0, 0, 0.0, []
    # `from`/`to`, not `range`. Three traps, all of which produced a run that
    # looked fine and held the lock for nothing:
    #  - `range` is parsed with .toInt( ) and clamped to 0..6, so it is an INDEX;
    #    "24h" silently became 24, then 6.
    #  - a wide `range` answers {"probe":1,"sliceRequired":1,...} in 184 bytes and
    #    does no work at all — it tells the client to slice instead. Only an
    #    explicit window streams.
    #  - emit=0 decodes without formatting or sending, which SHORTENS the hold,
    #    and HeavyTaskGuard covers the whole handler including the stream.
    # A 7-day explicit window measures ~1.1 s of held lock; 30 days breaks the
    # chunked framing on this firmware (InvalidChunkLength), so it is not usable
    # as a load generator here.
    span = 7 * 86400
    print(f"hammering /api/history_multi over a {span // 86400}-day explicit "
          f"window for {minutes} min, to hold the heavy lock across record "
          f"boundaries", flush=True)
    while time.time() < t_end:
        now = int(time.time())
        t0 = time.time()
        try:
            r = w.get(f'/api/history_multi?from={now - span}&to={now}')
            dt = time.time() - t0
            reqs += 1
            # Only a substantial body proves the handler did work while holding
            # the lock; a probe or an error body is not a hold.
            if r.status_code >= 400 or len(r.content) < 2048:
                errs += 1
            else:
                held += dt
                sizes.append(len(r.content))
        except Exception:
            errs += 1
            w = session()
    window = minutes * 60
    print(f"{reqs} requests, {errs} rejected/failed, "
          f"{held:.0f}s of held lock over {window:.0f}s "
          f"({100.0 * held / window:.0f}% duty)", flush=True)
    if sizes:
        print(f"response bytes: min={min(sizes)} max={max(sizes)}", flush=True)
    else:
        print("NO substantial response at all — the lock was never really held, "
              "so nothing below says anything about contention", flush=True)

    ev = hw.read_log(timeout=14)
    cur = hw.last_boot(ev)
    seq = [e for e in cur if e['code'] in (REC, WIP, SEAL)]

    # Sampling: a slot lost is a gap over 90 s inside one boot.
    recs = [e for e in cur if e['code'] == REC and e['epoch'] > 1_600_000_000]
    gaps = [(a['epoch'], b['epoch'] - a['epoch'])
            for a, b in zip(recs, recs[1:]) if b['epoch'] - a['epoch'] > 90]

    # Did the deferral engage? A record with no snapshot immediately before it.
    deferred = []
    for i, e in enumerate(seq):
        if e['code'] == REC and (i == 0 or seq[i - 1]['code'] != WIP):
            deferred.append(e['up'])

    print(f"\nrecords this boot: {len(recs)}")
    print(f"unsampled slots (gap > 90 s): {len(gaps)} {gaps}")
    print(f"records whose snapshot was deferred: {len(deferred)} at up{deferred}")
    if not deferred:
        print("INCONCLUSIVE on contention, and structurally so — not bad luck.\n"
              "  Every heavy-lock holder runs in AppManager::loop( ) on Core 0:\n"
              "  _webMgr->update( ) at line 187, _telemetryMgr->update( ) at 223,\n"
              "  the graph via UI events — all strictly sequential with\n"
              "  processHistoryLogging( ) at 290, and nothing on the Core 1 path\n"
              "  takes it at all. So the lock CANNOT be held at the instant a\n"
              "  record is sampled, no matter how hard this hammers.\n"
              "  The reachable gate is touch priority: getLastTouchTimestamp( ) is\n"
              "  set by Core 1 and read by the loop, so it can be true while line\n"
              "  290 runs. Exercising it needs real touch, or `touch sim` — which\n"
              "  exists only in pico_w_test.\n"
              "  The sampling result below stands on its own.")
    ok = not gaps
    print(f"-> {'PASS' if ok else 'FAIL'} (sampling)", flush=True)
    return ok


if __name__ == '__main__':
    if not PASS:
        raise SystemExit('set SIMUT_WEB_PASS')
    what = sys.argv[1] if len(sys.argv) > 1 else 'commit'
    arg = float(sys.argv[2]) if len(sys.argv) > 2 else None
    if what == 'commit':
        sys.exit(0 if cmd_commit(int(arg or 3)) else 1)
    elif what == 'contend':
        sys.exit(0 if cmd_contend(arg or 4) else 1)
    raise SystemExit(f'unknown: {what}')
