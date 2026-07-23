#!/usr/bin/env python3
"""
Save-storm validation — protocol item #2 of SIMUT-Plano-Estabilidade-Concorrencia.md.

Alternates a real `write memory` with simulated touch events to hammer the
quiet-mode request/release path (T1.1) and the Core-1 no-heap regime (T1.2).
The failure modes this is built to catch:

  * Core 1 killed while holding a lock -> APP_CORE1_DEAD in the log, or a
    watchdog reboot whose autopsy banner names the owning module.
  * malloc lock stuck -> heap/largest-block collapse, or a hang that shows up
    as a dead serial port followed by a boot banner.
  * quiet-mode refcount leak (R5) -> display never returns; logged by the
    15 s watchdog added in T1.5.

THE CONFIG MUST CHANGE EVERY CYCLE. `saveConfiguration( )` CRC-checks the
struct and returns early on a no-op save — before the WdtWindow, before
BigSaveGuard, before quiet mode is ever requested. It still increments
`configSaves`, so a storm of identical saves reports 500/500 while exercising
none of what this test exists to exercise. Each cycle therefore flips the
device name between two values so the CRC always differs, and the run asserts
afterwards that flash operations actually grew.

Samples are appended to JSONL as they are taken, so an interrupted run still
yields data.

Usage:
    python3 tools/save_storm.py [--cycles N] [--sample-every N] [--out DIR]

Project: SIMUT
License: MIT
"""

import argparse
import glob
import json
import os
import re
import sys
import time

import serial

BAUD = 115200
# Matches SIMUT>, SIMUT#, SIMUT(config)#, SIMUT(config-sensor)# at end of output.
PROMPT_RE = re.compile(r'SIMUT(?:\([a-z0-9-]+\))?\s*[#>]\s*$')

# Corners and centre of the 320x240 panel, so touches walk different hit-boxes.
TOUCH_POINTS = [(160, 120), (20, 20), (300, 20), (300, 220), (20, 220), (160, 40)]

BOOT_MARKERS = ('[BOOT]', 'WATCHDOG_REBOOT', 'C0=[')

# Restored on exit so the storm does not leave the rig renamed.
BASE_NAME = 'simut'


class Storm:
    def __init__(self, cycles, sample_every, outdir):
        self.cycles = cycles
        self.sample_every = sample_every
        self.outdir = outdir
        self.ser = None
        self.port = None

        self.samples_path = os.path.join(outdir, 'samples.jsonl')
        self.raw_path = os.path.join(outdir, 'serial_raw.log')
        self.raw = open(self.raw_path, 'a', encoding='utf-8', errors='replace')

        self.saves_ok = 0
        self.saves_fail = 0
        self.touches = 0
        self.reboots = 0
        self.reconnects = 0
        self.boot_events = []
        self.last_uptime = None
        self.first_sample = None
        self.last_sample = None

    # ---------- transport ----------

    def connect(self, attempts=30):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        for _ in range(attempts):
            # The port can flip ACM0<->ACM1 across a reset; always rescan.
            for port in sorted(glob.glob('/dev/ttyACM*')):
                try:
                    ser = serial.Serial(port, BAUD, timeout=1)
                    ser.dtr = True  # critical for the earlephilhower core
                    time.sleep(1.2)
                    ser.reset_input_buffer()
                    ser.write(b'\r\n')
                    if self._wait_prompt(ser, 5):
                        self.ser = ser
                        self.port = port
                        # Land in a known mode: privileged, not config.
                        for setup in (b'end\r\n', b'enable\r\n'):
                            ser.write(setup)
                            time.sleep(0.5)
                            self._drain(ser)
                        return True
                    ser.close()
                except Exception:
                    pass
            time.sleep(1)
        return False

    def _drain(self, ser):
        try:
            data = ser.read(8192)
            if data:
                self._record(data)
        except Exception:
            pass

    def _record(self, data):
        text = data.decode('utf-8', errors='replace')
        self.raw.write(text)
        self.raw.flush()
        for marker in BOOT_MARKERS:
            if marker in text:
                line = next((l.strip() for l in text.split('\n') if marker in l), marker)
                self.boot_events.append(line[:200])
                break
        return text

    def _wait_prompt(self, ser, timeout):
        buf = b''
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = ser.read(256)
            except Exception:
                return False
            if chunk:
                buf += chunk
                if PROMPT_RE.search(buf.decode('utf-8', errors='replace').rstrip()):
                    self._record(buf)
                    return True
            else:
                time.sleep(0.02)
        if buf:
            self._record(buf)
        return False

    def cmd(self, text, timeout=8.0):
        """Send a command and read until the prompt returns. Reconnects once."""
        for attempt in (1, 2):
            try:
                self.ser.write((text + '\r\n').encode())
            except Exception:
                if attempt == 2:
                    return None
                self.reconnects += 1
                if not self.connect():
                    return None
                continue

            buf = b''
            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    chunk = self.ser.read(512)
                except Exception:
                    break
                if chunk:
                    buf += chunk
                    decoded = buf.decode('utf-8', errors='replace')
                    if PROMPT_RE.search(decoded.rstrip()):
                        return self._record(buf)
                else:
                    time.sleep(0.02)
            if buf:
                return self._record(buf)
            # No bytes at all: the port is probably a corpse -> reconnect.
            if attempt == 1:
                self.reconnects += 1
                if not self.connect():
                    return None
        return None

    # ---------- metrics ----------

    @staticmethod
    def _uptime_seconds(text):
        m = re.search(r'Uptime:\s*(\d+):(\d+):(\d+)', text)
        if not m:
            return None
        h, mi, s = (int(g) for g in m.groups())
        return h * 3600 + mi * 60 + s

    def sample(self, cycle):
        text = self.cmd('show metrics', timeout=12)
        if not text:
            return None
        s = {'cycle': cycle, 'wall': round(time.time() - self.t0, 1)}
        s['uptime_s'] = self._uptime_seconds(text)

        for key, pat in (
            ('heap', r'Heap:\s*(\d+)'),
            ('heap_min', r'Heap:\s*\d+\s*B\s*\(min:\s*(\d+)'),
            ('largest', r'(?:Maior bloco|Largest block):\s*(\d+)'),
            ('largest_min', r'(?:Maior bloco|Largest block):\s*\d+\s*B\s*\(min:\s*(\d+)'),
            ('flash_ops', r'Flash ops:\s*(\d+)'),
            ('flash_avg_ms', r'Flash ops:\s*\d+\s*\((?:media|avg)\s*(\d+)'),
            ('flash_max_ms', r'(?:Pior op|Worst op):\s*(\d+)'),
            ('flash_over50', r'>50ms:\s*(\d+)'),
            ('config_saves', r'(?:Config saves|Saves):\s*(\d+)'),
            ('sensor_ok', r'(?:Leituras OK|Reads OK):\s*(\d+)'),
            ('sensor_err', r'(?:Leituras erro|Read errors):\s*(\d+)'),
            # Present only on builds carrying the T0.1 probe.
            ('irqoff_max_us', r'IRQ-off max:\s*(\d+)'),
            ('irqoff_avg_us', r'IRQ-off max:\s*\d+\s*us\s*\|\s*(?:media|avg):\s*(\d+)'),
            ('irqoff_erase', r'IRQ-off erase:\s*(\d+)'),
            ('irqoff_over1ms', r'>1ms:\s*(\d+)'),
        ):
            m = re.search(pat, text)
            if m:
                s[key] = int(m.group(1))

        # Uptime going backwards is the unambiguous reboot signal.
        up = s.get('uptime_s')
        if up is not None and self.last_uptime is not None and up < self.last_uptime:
            self.reboots += 1
            s['reboot_detected'] = True
        if up is not None:
            self.last_uptime = up

        with open(self.samples_path, 'a', encoding='utf-8') as fh:
            fh.write(json.dumps(s) + '\n')
        if self.first_sample is None:
            self.first_sample = s
        self.last_sample = s
        return s

    # ---------- run ----------

    def run(self):
        self.t0 = time.time()
        print('[storm] connecting...')
        if not self.connect():
            print('[storm] FATAL: no device responded')
            return 1
        print(f'[storm] connected on {self.port}')

        base = self.sample(0)
        print(f'[storm] baseline: {base}')

        for cycle in range(1, self.cycles + 1):
            # Mutate the config so the CRC differs and the save is real.
            self.cmd('configure terminal', timeout=6)
            self.cmd(f'system name {BASE_NAME}-{cycle % 2}', timeout=6)
            self.cmd('end', timeout=6)

            out = self.cmd('write memory', timeout=15)
            if out is None:
                self.saves_fail += 1
            elif re.search(r'OK|salv|saved|sucesso|success', out, re.IGNORECASE):
                self.saves_ok += 1
            else:
                self.saves_fail += 1

            # Touch between saves: this is the window where a Core-1 reset
            # used to catch the event queue's spinlock.
            for i in range(2):
                x, y = TOUCH_POINTS[(cycle + i) % len(TOUCH_POINTS)]
                if self.cmd(f'touch sim {x} {y}', timeout=5) is not None:
                    self.touches += 1

            if cycle % self.sample_every == 0 or cycle == self.cycles:
                s = self.sample(cycle)
                elapsed = time.time() - self.t0
                rate = cycle / elapsed if elapsed else 0
                eta = (self.cycles - cycle) / rate if rate else 0
                fo = s.get('flash_ops') if s else None
                print(f'[storm] {cycle}/{self.cycles} '
                      f'saves_ok={self.saves_ok} fail={self.saves_fail} '
                      f'touch={self.touches} reboots={self.reboots} '
                      f'reconn={self.reconnects} '
                      f'heap={s.get("heap") if s else "?"} '
                      f'flash_ops={fo} '
                      f'eta={eta/60:.1f}min', flush=True)

        return self.finish()

    def finish(self):
        # Leave the rig as we found it.
        self.cmd('configure terminal', timeout=6)
        self.cmd(f'system name {BASE_NAME}', timeout=6)
        self.cmd('end', timeout=6)
        self.cmd('write memory', timeout=15)

        log = self.cmd('show system log', timeout=20) or ''
        core1_dead = len(re.findall(r'Core 1 dead', log, re.IGNORECASE))
        lockout = len(re.findall(r'lockout', log, re.IGNORECASE))

        first_ops = (self.first_sample or {}).get('flash_ops')
        last_ops = (self.last_sample or {}).get('flash_ops')
        flash_delta = (last_ops - first_ops) if (first_ops is not None
                                                and last_ops is not None) else None

        summary = {
            'cycles': self.cycles,
            'saves_ok': self.saves_ok,
            'saves_fail': self.saves_fail,
            'touches': self.touches,
            'reboots': self.reboots,
            'reconnects': self.reconnects,
            'boot_events': self.boot_events,
            'core1_dead_log_hits': core1_dead,
            'lockout_log_hits': lockout,
            'flash_ops_delta': flash_delta,
            'duration_s': round(time.time() - self.t0, 1),
            'first_sample': self.first_sample,
            'last_sample': self.last_sample,
        }

        # A storm whose saves never reached flash proved nothing: the CRC
        # no-op path returns before quiet mode is even requested.
        saves_were_real = flash_delta is not None and flash_delta >= self.cycles * 0.5
        summary['saves_were_real'] = saves_were_real

        with open(os.path.join(self.outdir, 'summary.json'), 'w', encoding='utf-8') as fh:
            json.dump(summary, fh, indent=2)

        print('\n===== SAVE-STORM SUMMARY =====')
        for k, v in summary.items():
            if k not in ('first_sample', 'last_sample', 'boot_events'):
                print(f'  {k}: {v}')
        print(f'  boot_events: {len(self.boot_events)}')
        for e in self.boot_events[:10]:
            print(f'    {e}')
        print(f'  first: {self.first_sample}')
        print(f'  last:  {self.last_sample}')
        print(f'  artifacts: {self.outdir}')

        verdict_ok = (self.reboots == 0 and self.saves_fail == 0
                      and core1_dead == 0 and not self.boot_events
                      and saves_were_real)
        if not saves_were_real:
            print('\n  !! saves did not reach flash — the CRC no-op path was hit.')
            print('     This run does NOT validate T1.1/T1.2.')
        print(f'\n  VERDICT: {"PASS" if verdict_ok else "REVIEW"}')

        try:
            self.ser.close()
        except Exception:
            pass
        self.raw.close()
        return 0 if verdict_ok else 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cycles', type=int, default=500)
    ap.add_argument('--sample-every', type=int, default=25)
    ap.add_argument('--out', default='docs/test_reports/save_storm')
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    return Storm(args.cycles, args.sample_every, args.out).run()


sys.exit(main())
