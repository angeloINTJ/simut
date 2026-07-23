#!/usr/bin/env python3
"""
Sensor soak — protocol item #4 of SIMUT-Plano-Estabilidade-Concorrencia.md.

Watches the triple sensor read (DS18B20 + DHT22 + BMP280) over a long run and
fails the validation if the BMP280 ever silently drops to PIO/GPIO bit-bang,
which is cause C3: ~1.6 ms of interrupts-off per I2C transaction on Core 0,
i.e. the sensor problem and the Wi-Fi problem being the same problem.

The firmware makes that regression loud (SensorManager.cpp logs a WARN
containing "bit-bang"); this just watches for it, alongside sensor error
counters, heap drift, reboots, and — when running a build that has the T0.1
probe — the real IRQ-off window.

Samples are appended to JSONL as they are taken, so the run is inspectable
while in flight and survives interruption. Safe to stop and restart: pass the
same --out and it appends.

Usage:
    python3 tools/sensor_soak.py --hours 24 [--interval 300] [--out DIR]

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
PROMPTS = ('SIMUT#', 'SIMUT>', 'SIMUT(config)')
BOOT_MARKERS = ('[BOOT]', 'WATCHDOG_REBOOT', 'C0=[')


class Soak:
    def __init__(self, hours, interval, outdir):
        self.deadline = time.time() + hours * 3600
        self.interval = interval
        self.outdir = outdir
        self.ser = None
        self.port = None
        self.t0 = time.time()

        self.samples_path = os.path.join(outdir, 'samples.jsonl')
        self.events_path = os.path.join(outdir, 'events.log')

        self.reboots = 0
        self.reconnects = 0
        self.bitbang_hits = 0
        self.core1_dead = 0
        self.last_uptime = None
        self.n = 0

    def event(self, msg):
        line = f'{time.strftime("%Y-%m-%d %H:%M:%S")} {msg}'
        print(line, flush=True)
        with open(self.events_path, 'a', encoding='utf-8') as fh:
            fh.write(line + '\n')

    # ---------- transport ----------

    def connect(self, attempts=60):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        for _ in range(attempts):
            for port in sorted(glob.glob('/dev/ttyACM*')):
                try:
                    ser = serial.Serial(port, BAUD, timeout=1)
                    ser.dtr = True
                    time.sleep(1.2)
                    ser.reset_input_buffer()
                    ser.write(b'\r\n')
                    if self._wait_prompt(ser, 5):
                        self.ser = ser
                        self.port = port
                        ser.write(b'enable\r\n')
                        time.sleep(0.6)
                        ser.read(8192)
                        return True
                    ser.close()
                except Exception:
                    pass
            time.sleep(2)
        return False

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
                if any(p in buf.decode('utf-8', errors='replace') for p in PROMPTS):
                    return True
            else:
                time.sleep(0.02)
        return False

    def cmd(self, text, timeout=10.0):
        for attempt in (1, 2):
            try:
                self.ser.write((text + '\r\n').encode())
            except Exception:
                if attempt == 2:
                    return None
                self.reconnects += 1
                self.event(f'RECONNECT (write failed on {text!r})')
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
                    if any(decoded.rstrip().endswith(p) for p in PROMPTS):
                        return decoded
                else:
                    time.sleep(0.02)
            if buf:
                return buf.decode('utf-8', errors='replace')
            if attempt == 1:
                self.reconnects += 1
                self.event(f'RECONNECT (silent port on {text!r})')
                if not self.connect():
                    return None
        return None

    # ---------- sampling ----------

    @staticmethod
    def _uptime_seconds(text):
        m = re.search(r'Uptime:\s*(\d+):(\d+):(\d+)', text)
        if not m:
            return None
        h, mi, s = (int(g) for g in m.groups())
        return h * 3600 + mi * 60 + s

    def sample(self):
        self.n += 1
        s = {'n': self.n, 'wall_s': round(time.time() - self.t0, 1)}

        metrics = self.cmd('show metrics', timeout=12) or ''
        s['uptime_s'] = self._uptime_seconds(metrics)
        for key, pat in (
            ('heap', r'Heap:\s*(\d+)'),
            ('heap_min', r'Heap:\s*\d+\s*B\s*\(min:\s*(\d+)'),
            ('largest', r'(?:Maior bloco|Largest block):\s*(\d+)'),
            ('sensor_ok', r'(?:Leituras OK|Reads OK):\s*(\d+)'),
            ('sensor_err', r'(?:Leituras erro|Read errors):\s*(\d+)'),
            ('flash_ops', r'Flash ops:\s*(\d+)'),
            ('flash_max_ms', r'(?:Pior op|Worst op):\s*(\d+)'),
            # Present only on builds carrying the T0.1 probe.
            ('irqoff_max_us', r'IRQ-off max:\s*(\d+)'),
            ('irqoff_avg_us', r'IRQ-off max:\s*\d+\s*us\s*\|\s*(?:media|avg):\s*(\d+)'),
            ('irqoff_erase', r'IRQ-off erase:\s*(\d+)'),
            ('irqoff_prog', r'erase:\s*\d+\s*\|\s*prog:\s*(\d+)'),
            ('irqoff_over1ms', r'>1ms:\s*(\d+)'),
        ):
            m = re.search(pat, metrics)
            if m:
                s[key] = int(m.group(1))

        sensors = self.cmd('show sensors', timeout=12) or ''
        s['slots_ds18'] = sensors.count('DS18B20')
        s['slots_dht22'] = sensors.count('DHT22')
        s['slots_bmp'] = sensors.count('BMP280') + sensors.count('BME280')

        log = self.cmd('show system log', timeout=15) or ''
        bb = len(re.findall(r'bit-bang', log, re.IGNORECASE))
        c1 = len(re.findall(r'Core 1 dead', log, re.IGNORECASE))
        s['log_bitbang'] = bb
        s['log_core1_dead'] = c1
        if bb > self.bitbang_hits:
            self.event(f'*** BIT-BANG FALLBACK DETECTED (log hits {bb}) — validation #4 FAILS')
            self.bitbang_hits = bb
        if c1 > self.core1_dead:
            self.event(f'*** CORE 1 DEAD event (log hits {c1})')
            self.core1_dead = c1

        for marker in BOOT_MARKERS:
            if marker in metrics or marker in log:
                self.event(f'BOOT MARKER seen: {marker}')
                break

        up = s.get('uptime_s')
        if up is not None and self.last_uptime is not None and up < self.last_uptime:
            self.reboots += 1
            s['reboot_detected'] = True
            self.event(f'*** REBOOT (uptime {self.last_uptime}s -> {up}s), total {self.reboots}')
        if up is not None:
            self.last_uptime = up

        with open(self.samples_path, 'a', encoding='utf-8') as fh:
            fh.write(json.dumps(s) + '\n')
        return s

    def run(self):
        self.event(f'soak start: until {time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(self.deadline))}')
        if not self.connect():
            self.event('FATAL: no device responded')
            return 1
        self.event(f'connected on {self.port}')

        while time.time() < self.deadline:
            s = self.sample()
            left = (self.deadline - time.time()) / 3600
            self.event(
                f'#{s["n"]} up={s.get("uptime_s")}s heap={s.get("heap")} '
                f'ok={s.get("sensor_ok")} err={s.get("sensor_err")} '
                f'slots(ds/dht/bmp)={s.get("slots_ds18")}/{s.get("slots_dht22")}/{s.get("slots_bmp")} '
                f'irqoff_max_us={s.get("irqoff_max_us")} '
                f'reboots={self.reboots} bitbang={self.bitbang_hits} '
                f'left={left:.1f}h')
            # Sleep in slices so a stop lands promptly.
            end = min(time.time() + self.interval, self.deadline)
            while time.time() < end:
                time.sleep(min(5, end - time.time()))

        verdict = (self.bitbang_hits == 0 and self.reboots == 0 and self.core1_dead == 0)
        self.event(f'soak done: samples={self.n} reboots={self.reboots} '
                   f'bitbang={self.bitbang_hits} core1_dead={self.core1_dead} '
                   f'reconnects={self.reconnects} VERDICT={"PASS" if verdict else "REVIEW"}')
        try:
            self.ser.close()
        except Exception:
            pass
        return 0 if verdict else 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hours', type=float, default=24.0)
    ap.add_argument('--interval', type=int, default=300, help='seconds between samples')
    ap.add_argument('--out', default='docs/test_reports/pio_soak')
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    return Soak(args.hours, args.interval, args.out).run()


sys.exit(main())
