#!/usr/bin/env python3
"""Bench library for the telemetry test campaign.

One process owns the serial port for a whole run, so reboot detection and CLI
driving cannot fight each other for /dev/ttyACM*. Everything the tests need —
serial, web, servers, metrics sampling — lives here so the test scripts stay
about the experiment.

Reboot detection is deliberately belt-and-braces: the USB CDC drops when the
device resets, but a fast reset can be missed by a reader that happens to be
between reads, so uptime going backwards in `show metrics` is checked too.
"""
import glob
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time

import serial
import requests
import hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET_GLOB = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00'
HAND_GLOB = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_[0-9A-Z]*-if00'
HOST_IP = '192.168.3.31'


def target_port():
    m = glob.glob(TARGET_GLOB)
    return os.path.realpath(m[0]) if m else None


def hand_port():
    for p in glob.glob(HAND_GLOB):
        if '_W_' in p:
            continue
        return os.path.realpath(p)
    return None


# ---------------------------------------------------------------------------
# serial
# ---------------------------------------------------------------------------

class Target:
    """Owns the target's USB CDC for the lifetime of a run.

    A background thread drains the port continuously into a timestamped log and
    watches for the markers that mean the device died: the port vanishing
    (reset re-enumerates USB), the boot banner, and the fatal-log line the
    firmware prints after a watchdog reboot.
    """

    BOOT_MARKERS = (
        re.compile(r'\[BOOT'),
        re.compile(r'SIMUT v?\d'),
        re.compile(r'Iniciando|Booting|=== SIMUT'),
    )
    FATAL = re.compile(r'\[FTL\]|SOFT PANIC|HW WATCHDOG|PANIC')

    def __init__(self, logpath, echo=False):
        self.logpath = logpath
        self.echo = echo
        self.lf = open(logpath, 'a', buffering=1)
        self.ser = None
        self.buf = ''
        self.lock = threading.Lock()
        self.stop = False
        self.port_drops = 0          # USB re-enumerations = hard reboots
        self.fatal_lines = []
        self.boot_lines = []
        self.all_lines = []
        self.t0 = time.time()
        self._open()
        self.rx = threading.Thread(target=self._reader, daemon=True)
        self.rx.start()

    # -- plumbing ----------------------------------------------------------
    def _open(self):
        p = target_port()
        if not p:
            return False
        try:
            s = serial.Serial(p, 115200, timeout=0.15)
            s.dtr = True
            time.sleep(0.25)
            self.ser = s
            self._log(f'--- serial open {p}')
            return True
        except Exception as e:
            self._log(f'--- serial open failed: {e}')
            return False

    def _log(self, line):
        stamp = f'{time.time() - self.t0:9.2f} '
        self.lf.write(stamp + line + '\n')
        if self.echo:
            print(stamp + line, flush=True)

    def _feed(self, text):
        with self.lock:
            self.buf += text
            while '\n' in self.buf:
                line, _, self.buf = self.buf.partition('\n')
                line = line.rstrip('\r')
                if not line:
                    continue
                self.all_lines.append((time.time() - self.t0, line))
                self._log(line)
                if self.FATAL.search(line):
                    self.fatal_lines.append((time.time() - self.t0, line))
                for m in self.BOOT_MARKERS:
                    if m.search(line):
                        self.boot_lines.append((time.time() - self.t0, line))
                        break

    def _reader(self):
        while not self.stop:
            if self.ser is None:
                if not self._open():
                    time.sleep(0.4)
                    continue
            try:
                data = self.ser.read(4096)
                if data:
                    self._feed(data.decode('utf-8', 'replace'))
            except Exception as e:
                # Port vanished: the device re-enumerated, i.e. it reset.
                self._log(f'--- serial dropped ({e})')
                self.port_drops += 1
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
                time.sleep(0.8)

    # -- commands ----------------------------------------------------------
    def send(self, cmd, wait=2.5, quiet=0.45):
        """Send one CLI line and collect the reply until the prompt goes quiet."""
        if self.ser is None:
            self._open()
        if self.ser is None:
            return ''
        with self.lock:
            mark = len(self.all_lines)
        try:
            self.ser.write((cmd + '\r\n').encode())
            self.ser.flush()
        except Exception as e:
            self._log(f'--- write failed: {e}')
            return ''
        deadline = time.time() + wait
        last_len = mark
        quiet_at = None
        while time.time() < deadline:
            time.sleep(0.08)
            with self.lock:
                n = len(self.all_lines)
            if n != last_len:
                last_len = n
                quiet_at = time.time() + quiet
                deadline = max(deadline, time.time() + 0.5)
            elif quiet_at and time.time() > quiet_at:
                break
        with self.lock:
            return '\n'.join(l for _, l in self.all_lines[mark:])

    def cmds(self, *cmds, wait=2.5):
        return [self.send(c, wait=wait) for c in cmds]

    # -- state -------------------------------------------------------------
    def metrics(self, wait=3.5):
        txt = self.send('show metrics', wait=wait)
        d = {'raw': txt}
        pats = {
            'uptime': r'Uptime:\s*(\d+):(\d+):(\d+)',
            'heap': r'Heap:\s*(\d+)\s*B\s*\(min:\s*(\d+)',
            'largest': r'Maior bloco:\s*(\d+)\s*B\s*\(min:\s*(\d+)',
            'wifi_conns': r'WiFi conns:\s*(\d+)',
            'mqtt_conns': r'MQTT conns:\s*(\d+)',
            'tel_sent': r'Enviadas:\s*(\d+)',
            'tel_failed': r'Falhas:\s*(\d+)',
            'tel_retries': r'Retries:\s*(\d+)',
            'tel_bytes': r'Bytes:\s*(\d+)',
            'tel_lat': r'Ult\. lat:\s*(\d+)',
            'reads_ok': r'Leituras OK:\s*(\d+)',
            'reads_err': r'Leituras erro:\s*(\d+)',
            'flash_ops': r'Flash ops:\s*(\d+)',
            'core1_exposed': r'Core1 exposto:\s*(\d+)',
            'core1_hb': r'Heartbeat:\s*(\d+)\s*ms',
            'core1_running': r'Rodando:\s*(\d+)',
        }
        for k, p in pats.items():
            m = re.search(p, txt)
            if not m:
                d[k] = None
                continue
            if k == 'uptime':
                d[k] = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3))
            elif k in ('heap', 'largest'):
                d[k] = int(m.group(1))
                d[k + '_min'] = int(m.group(2))
            else:
                d[k] = int(m.group(1))
        return d

    def close(self):
        self.stop = True
        time.sleep(0.3)
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.lf.close()


# ---------------------------------------------------------------------------
# web
# ---------------------------------------------------------------------------

def sha256_frontend(password):
    """The login page hashes each UTF-16 code unit as one byte — latin-1."""
    return hashlib.sha256(password.encode('latin-1')).hexdigest()


class Web:
    def __init__(self, host, timeout=20):
        self.base = f'http://{host}'
        self.s = requests.Session()
        self.timeout = timeout

    def get(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.get(self.base + path, timeout=self.timeout, **kw)

    def post(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.post(self.base + path, timeout=self.timeout, **kw)

    def login(self, user, password):
        r = self.get('/api/login_init')
        if r.status_code != 200:
            return False, f'login_init HTTP {r.status_code}'
        nonce = r.json().get('nonce', '')
        r = self.post('/api/login', data={
            'user': user, 'pass': sha256_frontend(password), 'nonce': nonce,
        }, headers={'Content-Type': 'application/x-www-form-urlencoded'})
        if 'SIMUTSESS' not in self.s.cookies.get_dict():
            return False, f'no session cookie (HTTP {r.status_code}) {r.text[:120]}'
        return True, 'ok'

    def commit(self, sys_fields):
        """POST /api/commit_all — applies config and reboots the device."""
        payload = json.dumps({'sys': sys_fields}, separators=(',', ':'))
        return self.post('/api/commit_all', data={'_payload': payload},
                         headers={'Content-Type': 'application/x-www-form-urlencoded'})

    def config(self):
        r = self.get('/api/config')
        return r.json() if r.status_code == 200 else {'_http': r.status_code,
                                                      '_body': r.text[:200]}


def wait_web(host, timeout=90, path='/api/login_init'):
    """Block until the web server answers again (post-reboot)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            r = requests.get(f'http://{host}{path}', timeout=4)
            if r.status_code < 500:
                return round(time.time() - t0, 1)
        except Exception:
            pass
        time.sleep(1)
    return None


# ---------------------------------------------------------------------------
# servers
# ---------------------------------------------------------------------------

class Server:
    """A test server subprocess with its stats file."""

    def __init__(self, kind, name, outdir, **kw):
        self.kind = kind          # 'http' | 'mqtt'
        self.name = name
        self.outdir = outdir
        self.stats_path = os.path.join(outdir, f'{name}.stats.json')
        self.records_path = os.path.join(outdir, f'{name}.records.ndjson')
        self.log_path = os.path.join(outdir, f'{name}.server.log')
        for p in (self.stats_path, self.records_path):
            if os.path.exists(p):
                os.remove(p)
        script = 'server_http.py' if kind == 'http' else 'server_mqtt.py'
        cmd = [sys.executable, os.path.join(HERE, script),
               '--stats', self.stats_path, '--records', self.records_path]
        for k, v in kw.items():
            flag = '--' + k.replace('_', '-')
            if v is True:
                cmd.append(flag)
            elif v is False or v is None:
                continue
            else:
                cmd += [flag, str(v)]
        self.cmd = cmd
        self.lf = open(self.log_path, 'w')
        self.proc = subprocess.Popen(cmd, cwd=HERE, stdout=self.lf,
                                     stderr=subprocess.STDOUT,
                                     preexec_fn=os.setsid)
        time.sleep(0.7)

    def alive(self):
        return self.proc.poll() is None

    def stats(self):
        try:
            with open(self.stats_path) as fh:
                return json.load(fh)
        except Exception:
            return {}

    def records(self):
        out = []
        try:
            with open(self.records_path) as fh:
                for line in fh:
                    line = line.strip()
                    if line:
                        try:
                            out.append(json.loads(line))
                        except Exception:
                            pass
        except Exception:
            pass
        return out

    def stop(self):
        try:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            self.proc.wait(timeout=5)
        except Exception:
            try:
                os.killpg(os.getpgid(self.proc.pid), signal.SIGKILL)
            except Exception:
                pass
        try:
            self.lf.close()
        except Exception:
            pass


def kill_stale():
    subprocess.run(['pkill', '-f', 'server_http.py'], capture_output=True)
    subprocess.run(['pkill', '-f', 'server_mqtt.py'], capture_output=True)
    time.sleep(0.4)
