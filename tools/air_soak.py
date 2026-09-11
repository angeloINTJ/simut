#!/usr/bin/env python3
"""Hands-off soak of the SIMUT Air hibernation cycle. PASSIVE by construction.

Never writes a byte to the device. Two instruments, both read-only:
  * USB presence of the target's CDC node (os.path.exists on the by-id path):
    every appear/vanish is a wake/sleep edge. This is the measurement that
    settled the read-path question (3/3 wakes on time) — it cannot be fooled.
  * The console, read-only, while the node exists: [AIR] phase/alarm lines
    (wakeSec, wip, OVERRUN), boot lines, and anything FATAL/WATCHDOG/panic.
    Opening the port is not activity to the firmware (only commands are), and
    a missed line here is the known alarm-line race, never a firmware verdict.

Writes:
  <log>.events   one line per edge / notable console line, timestamped
  <log>.summary  a rolling summary every SUMMARY_S, and a final JSON block

Usage: air_soak.py <hours> <log-prefix>
"""
import os, sys, time, json, re
try:
    import serial
except ImportError:
    serial = None
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from air_test_suite import Collector   # counts what the device uploads (HTTP)
COLLECTOR_PORT = 8010

TARGET = os.environ.get('SIMUT_TARGET_SERIAL', 'E6642815E34C1824')
NODE = f'/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_{TARGET}-if00'
ALARM_RE = re.compile(r'\[AIR\] alarm: (\d+):(\d+):(\d+) wakeSec=(\d+).*?wip=(\d+)(?P<over> OVERRUN)?')
PHASE_RE = re.compile(r'\[AIR\] phase=([A-Z]+)')
BAD_RE = re.compile(r'FATAL|WATCHDOG|panic|PANIC|HardFault|assert', re.I)
SUMMARY_S = 600          # rolling summary cadence
NOMINAL_PERIOD = 60.0    # hist=60 s on this bench
MISSED_GRACE = 90.0      # a sleep longer than nominal+grace is a missed/late wake

def main():
    hours = float(sys.argv[1]); prefix = sys.argv[2]
    ev = open(prefix + '.events', 'a', buffering=1)
    sm = open(prefix + '.summary', 'a', buffering=1)
    t0 = time.time(); end = t0 + hours * 3600
    coll = Collector(COLLECTOR_PORT); coll.start()
    def ts(): return f'{time.time()-t0:9.1f}s'
    def log(line): ev.write(f'{ts()} {line}\n')

    wakes = []            # dicts: t_on, t_off, awake_s, sleep_before_s, wakeSec, wip, overrun, phases, alarm_seen, bad
    cur = None
    last_off = None
    ser = None
    acc = b''
    bad_lines = []
    next_summary = t0 + SUMMARY_S
    present_prev = os.path.exists(NODE)
    if present_prev:
        cur = {'t_on': time.time(), 'phases': [], 'alarm_seen': False, 'bad': 0, 'partial_start': True}
        log('START: node already PRESENT (partial first wake)')
    else:
        log('START: node absent (device asleep) — clean first edge expected')
    log(f'collector listening on :{COLLECTOR_PORT}')

    def open_port():
        nonlocal ser, acc
        if serial is None: return
        try:
            ser = serial.Serial(NODE, 115200, timeout=0.05); acc = b''
        except Exception:
            ser = None

    def close_port():
        nonlocal ser
        try:
            if ser: ser.close()
        except Exception: pass
        ser = None

    def handle_line(s):
        nonlocal cur
        if cur is None: return
        m = PHASE_RE.search(s)
        if m: cur['phases'].append(m.group(1))
        m = ALARM_RE.search(s)
        if m:
            cur['alarm_seen'] = True
            cur['wakeSec'] = int(m.group(4)); cur['wip'] = int(m.group(5))
            cur['overrun'] = bool(m.group('over'))
            log(f'ALARM wakeSec={cur["wakeSec"]} wip={cur["wip"]}{" OVERRUN" if cur["overrun"] else ""}')
        if BAD_RE.search(s):
            cur['bad'] += 1; bad_lines.append((ts(), s.strip()[:160]))
            log(f'BAD: {s.strip()[:160]}')
        if '[AIR] boot: done' in s: log('boot: done')

    def write_summary(final=False):
        n = len(wakes)
        sleeps = [w['sleep_before_s'] for w in wakes if w.get('sleep_before_s') is not None]
        awakes = [w['awake_s'] for w in wakes if w.get('awake_s') is not None]
        late = [s for s in sleeps if s > NOMINAL_PERIOD + MISSED_GRACE]
        no_alarm = sum(1 for w in wakes if not w.get('alarm_seen'))
        over = sum(1 for w in wakes if w.get('overrun'))
        wips = [w['wip'] for w in wakes if 'wip' in w]
        s = {'elapsed_h': round((time.time()-t0)/3600, 2), 'wakes': n,
             'expected_wakes': int((time.time()-t0)/NOMINAL_PERIOD),
             'sleep_s_min': round(min(sleeps),1) if sleeps else None,
             'sleep_s_mean': round(sum(sleeps)/len(sleeps),1) if sleeps else None,
             'sleep_s_max': round(max(sleeps),1) if sleeps else None,
             'awake_s_mean': round(sum(awakes)/len(awakes),1) if awakes else None,
             'late_or_missed_wakes': len(late), 'no_alarm_line_seen': no_alarm,
             'overruns': over, 'wip_max': max(wips) if wips else None,
             'bad_console_lines': len(bad_lines),
             'tel_posts': len(coll.posts), 'tel_records_delivered': coll.records_since(t0),
             'final': final}
        sm.write(('FINAL ' if final else '') + json.dumps(s) + '\n')
        return s

    while time.time() < end:
        present = os.path.exists(NODE)
        now = time.time()
        if present and not present_prev:
            # wake edge
            sleep_before = (now - last_off) if last_off else None
            cur = {'t_on': now, 'sleep_before_s': round(sleep_before,1) if sleep_before else None,
                   'phases': [], 'alarm_seen': False, 'bad': 0}
            log(f'WAKE (slept {cur["sleep_before_s"]}s)' if sleep_before else 'WAKE (first)')
            if sleep_before and sleep_before > NOMINAL_PERIOD + MISSED_GRACE:
                log(f'LATE/MISSED WAKE: slept {sleep_before:.1f}s (> {NOMINAL_PERIOD+MISSED_GRACE}s)')
            time.sleep(0.4); open_port()
        elif not present and present_prev:
            # sleep edge
            close_port()
            if cur is not None:
                cur['t_off'] = now; cur['awake_s'] = round(now - cur['t_on'], 1)
                log(f'SLEEP (awake {cur["awake_s"]}s, phases={"/".join(cur["phases"]) or "-"}'
                    f'{"" if cur["alarm_seen"] else ", no-alarm-line"})')
                wakes.append(cur); cur = None
            last_off = now
        present_prev = present

        # read the console while present
        if present and ser is not None:
            try:
                chunk = ser.read(512)
                if chunk:
                    acc += chunk
                    while b'\n' in acc:
                        raw, acc = acc.split(b'\n', 1)
                        handle_line(raw.decode('utf-8', 'replace'))
            except Exception:
                close_port()   # vanished mid-read; the presence poll will confirm
        elif present and ser is None and serial is not None:
            open_port()

        if now >= next_summary:
            write_summary(); next_summary = now + SUMMARY_S
        time.sleep(0.05 if present else 0.25)

    close_port()
    final = write_summary(final=True)
    try: coll.stop()
    except Exception: pass
    log('END')
    print(json.dumps(final))
    if bad_lines:
        print('BAD console lines:'); [print(' ', t, l) for t, l in bad_lines[:40]]

if __name__ == '__main__':
    main()
