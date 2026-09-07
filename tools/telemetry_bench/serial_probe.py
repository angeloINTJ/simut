#!/usr/bin/env python3
"""One measured window with the serial console captured alongside.

What it is for: any question of the form "did the firmware say something while
that happened?" — a cursor reset, an FTL, a heap warning — answered with the
device's own lines stamped against the window, next to the server's count.

How it earned its keep (2026-09-07): the first capacity matrix looked like
22–35% of every HTTP window was re-sent, restarting from the oldest record.
Three runs of this probe, with the firmware's cursor-reset paths instrumented,
showed no reset firing and 0% re-sends inside the window. The restart was the
harness's own `tel_reset`, landing on a device that had already resumed its
drain after the configure reboot — and a server that had been counting since
before the reboot. The accounting in phase_cadence.window( ) now cuts the
window out of the server's per-request log by wall clock; this stays as the
tool that settles the next such question.

Usage: SIMUT_WEB_USER=... SIMUT_WEB_PASS=... python3 serial_probe.py [--batch 100] [--seconds 45] [--tls] [--tag NAME]
"""
import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C                      # noqa: E402
import phase_cadence as P                 # noqa: E402
import serial                             # noqa: E402

KEEP = ('cursor', '[STO]', '[TEL]', 'FTL', 'WRN', 'heap', 'reset', 'Boot', 'boot', '[BOOT]', '[SYS]', 'WATCHDOG', 'panic', 'PANIC')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--batch', type=int, default=100)
    ap.add_argument('--seconds', type=int, default=45)
    ap.add_argument('--tls', action='store_true')
    ap.add_argument('--tag', default='serialprobe', help='server result file name (each run keeps its own)')
    ap.add_argument('--delay', type=float, default=0.0,
                    help='server answers after this many seconds (mode slow); 0 = at once')
    args = ap.parse_args()

    dev = P.Dev()
    dev.ensure_m0()
    dev.login()
    port = P.PORT_HTTPS if args.tls else P.PORT_HTTP
    srv = P.start_server(args.tag, args.tls, port, mode=('slow' if args.delay > 0 else 'ok'),
                         delay=(args.delay if args.delay > 0 else None))
    lines, stop = [], threading.Event()

    def reader():
        path = dev.target.path
        while not stop.is_set():
            if not os.path.exists(path):
                time.sleep(0.05)
                continue
            try:
                with serial.Serial(path, P.A.BAUD, timeout=0.2) as s:
                    while not stop.is_set():
                        raw = s.readline()
                        if raw:
                            ln = raw.decode('utf-8', 'replace').rstrip()
                            if any(k in ln for k in KEEP):
                                lines.append((time.time(), ln))
            except Exception:
                time.sleep(0.2)

    try:
        dev.configure(args.tls, args.batch, interval_ms=1, port=port)
        dev.tel_reset()
        th = threading.Thread(target=reader, daemon=True)
        th.start()
        t0 = time.time()
        dev.tel_sync()
        time.sleep(3)
        row, _ = P.window(dev, srv, args.seconds, f'{args.tag}_b{args.batch}')
        stop.set()
        time.sleep(0.5)
        print('--- serial (t desde o tel_sync) ---')
        for t, ln in lines:
            print('%7.2fs  %s' % (t - t0, ln))
        print('--- janela ---')
        print(json.dumps({k: row.get(k) for k in (
            'active_s', 'drained_at_s', 'srv_requests', 'srv_records', 'records_per_s',
            'pre_window_records', 'dev_failed', 'uptime_resets')}))
    finally:
        srv.stop()


if __name__ == '__main__':
    main()
