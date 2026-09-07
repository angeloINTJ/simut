#!/usr/bin/env python3
"""Does the radio stay off until there is enough to say?

Config v22 made the telemetry trigger a COUNT: the device transmits once
`t_int` records are waiting, in batches of at most `t_bat`, and `t_int` = 0
disables telemetry. On the Air build that is the whole battery argument — a
wake that has nothing to send must never power the CYW43 — so the measurement
is not throughput but which wakes raised the radio.

The device says it on every boot:

    [AIR] wake: radio=off (pending=2 min=5 skip=0)

so this camps on the serial port across the sleeps (the port disappears with
the USB pull-up and comes back on the next wake), collects those lines, and
checks them against what the collector received.

    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... python3 phase_minbatch.py \
        --min-batch 5 --minutes 8
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


def reader(dev, lines, stop):
    """Collect [AIR] wake/alarm lines across sleeps, reopening as the port returns."""
    while not stop.is_set():
        path = dev.target.path
        if not path or not os.path.exists(path):
            time.sleep(0.2)
            continue
        try:
            with serial.Serial(path, P.A.BAUD, timeout=0.3) as s:
                while not stop.is_set():
                    raw = s.readline()
                    if not raw:
                        continue
                    ln = raw.decode('utf-8', 'replace').rstrip()
                    if '[AIR]' in ln or '[TEL]' in ln:
                        lines.append((time.time(), ln))
        except Exception:
            time.sleep(0.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--min-batch', type=int, default=5, help='t_int: records that trigger a send')
    ap.add_argument('--max-batch', type=int, default=100, help='t_bat: records per upload')
    ap.add_argument('--minutes', type=float, default=8.0, help='how long to watch the cycle')
    ap.add_argument('--tag', default='minbatch')
    args = ap.parse_args()

    dev = P.Dev()
    dev.ensure_m0()
    dev.login()
    srv = P.start_server(args.tag, False, P.PORT_HTTP)
    out = {'started': time.time(), 'min_batch': args.min_batch, 'rows': []}
    try:
        dev.configure(False, args.max_batch, min_batch=args.min_batch, port=P.PORT_HTTP)
        # Empty the queue first: with a backlog every wake would be due, and the
        # question here is what happens once the device is caught up.
        P.log('draining the backlog before the measurement')
        t0 = time.time()
        while time.time() - t0 < 180:
            if (dev.status().get('sys', {}).get('pending') or 0) <= args.min_batch:
                break
            time.sleep(3)
        pend = dev.status().get('sys', {}).get('pending')
        P.log(f'pending before the cycle: {pend}')

        lines, stop = [], threading.Event()
        th = threading.Thread(target=reader, args=(dev, lines, stop), daemon=True)
        th.start()
        srv_before = len(srv.records())
        dev.target.open(30)
        dev.target.ser.write(b'air hibernate\r\n')
        dev.target.close()
        time.sleep(args.minutes * 60)
        stop.set()
        time.sleep(0.5)

        wakes = [(t, ln) for t, ln in lines if '[AIR] wake:' in ln]
        on = [ln for _t, ln in wakes if 'radio=on' in ln]
        off = [ln for _t, ln in wakes if 'radio=off' in ln]
        recs = len(srv.records()) - srv_before
        print('--- wakes ---')
        for t, ln in wakes:
            print('%6.1fs  %s' % (t - out['started'], ln))
        out['rows'] = [{'t': round(t - out['started'], 1), 'line': ln} for t, ln in wakes]
        out.update(wakes_total=len(wakes), radio_on=len(on), radio_off=len(off),
                   records=recs, pending_before=pend)
        print(json.dumps({k: out[k] for k in ('wakes_total', 'radio_on', 'radio_off',
                                              'records', 'pending_before')}))
        C.save(f'phase_minbatch_{args.tag}.json', out)
    finally:
        srv.stop()
        try:
            dev.ensure_m0()
        except Exception as exc:
            P.log(f'post-run ensure_m0: {exc}')


if __name__ == '__main__':
    main()
