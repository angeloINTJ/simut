#!/usr/bin/env python3
"""Recover the stuck Air bench AFTER the self-wake observation window, and collect
the forensics that survive a reset. Order matters: read before restoring.

  1. hand RESET (cold boot -> M0)            -- destroys RAM/RTC state, keeps flash log
  2. read the device log tail                -- what the last wake before the stall logged
  3. end-of-soak snapshot (M0)               -- records/pending/fs/log vs soak_baseline.json
  4. restore telemetry target -> .206:8080   -- the soak pointed it at the local collector
  5. print the deltas

Usage: air_soak_recover.py <baseline.json> <out.json>
"""
import os, sys, json, time, subprocess, requests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from air_test_suite import Web

SP = os.path.dirname(os.path.abspath(__file__))
HOST = '192.168.3.24'

def hand(cmd):
    return subprocess.run(['bash', '-c',
        f'source /home/angelo/Documentos/simut/tools/PicoHand/pico_hand.sh; hand_init >/dev/null 2>&1; hand {cmd}'],
        capture_output=True, text=True, timeout=15).stdout.strip()

def wait_web(timeout=120):
    for i in range(timeout):
        try:
            if requests.get(f'http://{HOST}/api/login_init', timeout=3).status_code < 500: return i
        except Exception: pass
        time.sleep(1)
    return None

def main():
    base = json.load(open(sys.argv[1])); out = sys.argv[2]
    pw = os.environ['SIMUT_WEB_PASS']
    print('1) hand RESET ->', hand('RELEASE RESET') and hand('RESET'))
    up = wait_web()
    print(f'   web up @{up}s' if up is not None else '   web did NOT come up — device may be truly dead (BOOTSEL/reflash next)')
    if up is None: return 2

    w = Web(HOST, timeout=20); w.login('admin', pw)
    # 2) log tail: /api/logs is 12 B/record binary; the CLI 'show system log' is readable. Use the CLI.
    try:
        r = w.s.get(w.base + '/api/logs', timeout=30)
        nlog = len(r.content)//12 if r.status_code == 200 else None
    except Exception: nlog = None
    print(f'2) log entries now: {nlog}  (baseline {base.get("log_entries")}; delta = {None if nlog is None else nlog-base.get("log_entries",0)})')
    print('   readable tail (CLI show system log, last 25 lines):')
    try:
        from air_test_suite import Target
        t = Target(); t.open(30); txt = t.cmd('show system log', 10); t.close()
        for line in [l for l in txt.splitlines() if l.strip()][-25:]: print('     ', line[:140])
    except Exception as e:
        print('      (CLI read failed:', type(e).__name__, str(e)[:80], ')')

    # 3) snapshot
    snap = json.loads(subprocess.run([sys.executable, f'{SP}/air_soak_snapshot.py', out],
                                     capture_output=True, text=True, timeout=120).stdout.strip().splitlines()[-1])
    print('3) snapshot:', json.dumps({k: snap.get(k) for k in ('records_today','records_open','pending','fs_u','heap_f','log_entries')}))
    d = lambda k: (snap.get(k) or 0) - (base.get(k) or 0)
    print(f'   Δ records_today={d("records_today"):+d}  Δ pending={d("pending"):+d}  Δ fs_u={d("fs_u"):+d}  Δ log={d("log_entries"):+d}')

    # 4) restore telemetry target
    r = w.commit_sys({'t_srv': '192.168.3.206', 't_port': 8080})
    print('4) telemetry restored to .206:8080 ->', getattr(r, 'status_code', r))
    up2 = wait_web()
    if up2 is not None:
        w2 = Web(HOST, timeout=20); w2.login('admin', pw)
        cfg = w2.get('/api/config').json()
        print('   confirmed:', {k: cfg.get(k) for k in ('t_srv', 't_port')})
    return 0

if __name__ == '__main__':
    sys.exit(main())
