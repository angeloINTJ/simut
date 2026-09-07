#!/usr/bin/env python3
"""
SIMUT Air — hardware-in-the-loop test suite (serial CLI + web + PicoHand).

Drives the real headless build through a hibernation cycle and checks what the
code review of 2026-09-06 (docs/analysis/SIMUT_AIR_PLANO_FIX.md) said must hold:

  * the device comes back from SLEEP on its own, cycle after cycle (F01);
  * a wake that has nothing to send still goes back to sleep (F02);
  * the awake window is bounded by the work, not by the telemetry cadence (F05);
  * the M0 idle timer is reset by web activity, not only by the CLI (F21);
  * offline wakes are stamped with the real elapsed time (F04);
  * `air idle` rejects what the uint16 field cannot hold (F09).

Three instruments, all optional except the target's USB CDC:

  target   Pico W running pico_w_air, found by USB serial number (udev by-id).
           The suite talks to its emergency console (prompt "SIMUT> ").
  web      the device's HTTP API — login exactly like the browser (nonce +
           sha256 latin-1), `/api/status`, `/api/config`, `/api/commit_all`,
           `/download`.
  hand     the PicoHand fixture (plain Pico): PING/RESET/BOOTSEL/VERIFY, used
           for clean state between tests and to recover a target that does
           not come back from a wake. If the hand firmware has the optional
           PROBE channel (see the plan, §3) the suite reads GP16 edges from it;
           otherwise timing comes from USB enumeration timestamps.

Known defects are marked `xfail=` on the test: a failing xfail test counts as
XFAIL (expected), a passing one as XPASS (remove the mark, the bug is fixed).

Usage:
    python3 tools/air_test_suite.py --list
    python3 tools/air_test_suite.py --selftest            # no hardware
    python3 tools/air_test_suite.py [--host IP] [--only T05,T06] [--cycles 3]
                                    [--hist-interval 1] [--long] [--baseline]
                                    [--report out.json] [--collector-port 8010]
    python3 tools/air_test_suite.py --flash .pio/build/pico_w_air/firmware.uf2

Environment:
    SIMUT_WEB_USER / SIMUT_WEB_PASS   web credentials (web tests are skipped without)
    SIMUT_HOST                        device IP (else read from `show net status`)
    SIMUT_TARGET_SERIAL               USB serial of the target (default E6642815E34C1824)
    PICO_HAND_SERIAL                  USB serial of the hand   (default E660C062131E3E27)
    SIMUT_COLLECTOR_IP                IP of this host as seen by the device

Exit code: 0 no FAIL (XFAIL allowed), 1 failures, 2 could not set up.

Project: SIMUT
License: MIT
"""

import argparse
import glob
import hashlib
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

try:
    import requests
    import serial  # pyserial
except ImportError:  # --list / --selftest must work without the deps
    requests = None
    serial = None

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

BAUD = 115200
PROMPT_RE = re.compile(r'SIMUT(?:\([a-z0-9-]+\))?\s*[#>]\s*$')
AIR_STATUS_RE = re.compile(
    r'Air:\s*phase=(?P<phase>\d+)\s+wake=(?P<wake>\d+)s\s+hist=(?P<hist>\d+)s'
    r'\s+backoff=(?P<backoff>\d+)s\s+idle=(?P<idle>\d+)s(?:\s+pin=(?P<pin>\S+))?'
    # F25: armed says a reset would bring the cycle back, dirty how close the
    # crash-loop guard is to holding the device in M0. Optional so the suite
    # still parses a firmware from before they existed.
    r'(?:\s+armed=(?P<armed>\d+))?(?:\s+dirty=(?P<dirty>\d+))?'
    # Two schedules: tel=<wakes since send>/<wakes between sends>, and whether
    # THIS wake raised the radio at all.
    r'(?:\s+tel=(?P<telnow>\d+)/(?P<televery>\d+))?(?:\s+radio=(?P<radio>\d+))?')
PHASE_RE = re.compile(r'\[AIR\] phase=(?P<name>[A-Z]+) @(?P<ms>\d+)')
ALARM_RE = re.compile(r'\[AIR\] alarm: (?P<h>\d+):(?P<m>\d+):(?P<s>\d+) wakeSec=(?P<sec>\d+)')
VFY_RE = re.compile(r'VFY BOOTSEL=(?P<b>\S+) RESET=(?P<r>\S+) HB=(?P<hb>\d+)us')
EDGE_RE = re.compile(r'EDGE\s+(?P<n>\d+)\s+(?P<lvl>[HL])\s+(?P<us>\d+)')

TARGET_SERIAL = os.environ.get('SIMUT_TARGET_SERIAL', 'E6642815E34C1824')
HAND_SERIAL = os.environ.get('PICO_HAND_SERIAL', 'E660C062131E3E27')
BY_ID = '/dev/serial/by-id'

# /api/commit_all field names (section "sys"). Confirm against /api/config on
# the device before trusting a new firmware: a renamed key is rejected in the
# `rejected` list of the reply, which the suite prints.
TEL_FIELDS = {
    'server': 't_srv', 'port': 't_port', 'path': 't_path', 'tls': 't_sec',
    'interval_ms': 't_int', 'batch': 't_bat',
}
HIST_FIELD = 'h_int'

# Mirrors AIR_RESUME_GRACE_SEC in src/simut_config.h: the M0 window before a
# cycle that a reset interrupted resumes itself (plan F25). Only used to size a
# test's patience, so drift here costs a wrong timeout, not a wrong verdict.
AIR_RESUME_GRACE_SEC = 10


# --------------------------------------------------------------------------
# result bookkeeping
# --------------------------------------------------------------------------

class Results:
    def __init__(self):
        self.rows = []      # (id, name, outcome, detail, seconds)

    def add(self, tid, name, outcome, detail='', seconds=0.0):
        self.rows.append((tid, name, outcome, detail, seconds))
        line = f'  [{outcome:5}] {tid} {name}'
        if detail:
            line += f' — {detail}'
        print(line, flush=True)

    def count(self, outcome):
        return sum(1 for r in self.rows if r[2] == outcome)

    def to_json(self):
        return [dict(id=r[0], name=r[1], outcome=r[2], detail=r[3], seconds=round(r[4], 1))
                for r in self.rows]


class TestFail(Exception):
    pass


class TestSkip(Exception):
    pass


# --------------------------------------------------------------------------
# USB presence — the Air detaches from USB when it sleeps, by design
# --------------------------------------------------------------------------

def by_id_path(serial_no, pico_w):
    model = 'Pico_W' if pico_w else 'Pico'
    return f'{BY_ID}/usb-Raspberry_Pi_{model}_{serial_no}-if00'


class UsbWatcher:
    """Timestamps of the target appearing/disappearing on the USB bus."""

    def __init__(self, path):
        self.path = path

    def present(self):
        return os.path.exists(self.path)

    def wait(self, want_present, timeout, poll=0.25):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.present() == want_present:
                return time.time()
            time.sleep(poll)
        return None


# --------------------------------------------------------------------------
# PicoHand
# --------------------------------------------------------------------------

class Hand:
    """One open descriptor per exchange, DEBUG echo lines skipped (manual §7)."""

    def __init__(self, path=None):
        self.path = path or by_id_path(HAND_SERIAL, pico_w=False)
        self.available = os.path.exists(self.path) and serial is not None
        self._probe = None

    def cmd(self, text, timeout=2.0, multiline=False):
        if not self.available:
            raise TestSkip('PicoHand not present')
        try:
            return self._cmd(text, timeout, multiline)
        except serial.SerialException as exc:
            # Errno 16: something else holds the port — an IDE serial monitor is
            # the usual culprit (the manual's §7.3). That is a bench condition,
            # not a hand failure, so say which and skip rather than crash.
            raise TestSkip(f'PicoHand port busy or unreadable ({exc.__class__.__name__}: '
                           f'{exc}) — close any serial monitor on it')

    def _cmd(self, text, timeout, multiline):
        with serial.Serial(self.path, BAUD, timeout=0.2) as s:
            time.sleep(0.05)
            s.reset_input_buffer()
            s.write((text + '\n').encode())
            lines, deadline = [], time.time() + timeout
            while time.time() < deadline:
                raw = s.readline()
                if not raw:
                    continue
                line = raw.decode('utf-8', 'replace').strip()
                if not line or line.startswith('[DBG'):
                    continue
                lines.append(line)
                if not multiline or line.startswith('DONE') or line.startswith('ERR'):
                    break
            return lines if multiline else (lines[0] if lines else '')

    def ping(self):
        try:
            return self.cmd('PING') == 'PONG'
        except TestSkip:
            return False

    def reset(self):
        return self.cmd('RESET', timeout=4).startswith('OK')

    def bootsel(self):
        return self.cmd('BOOTSEL', timeout=6).startswith('OK')

    def release_all(self):
        if self.available:
            try:
                self.cmd('RELEASE BOOTSEL')
                self.cmd('RELEASE RESET')
            except Exception:
                pass

    def verify(self):
        return parse_vfy(self.cmd('VERIFY', timeout=3))

    def probe_supported(self):
        """Optional PROBE extension (plan §3). ERR/timeout = not there."""
        if self._probe is None:
            try:
                r = self.cmd('PROBE STATUS')
            except TestSkip:
                r = ''
            self._probe = r.startswith('PROBE')
        return self._probe

    def probe_start(self):
        return self.cmd('PROBE START').startswith('OK')

    def probe_read(self):
        return parse_edges(self.cmd('PROBE READ', timeout=3, multiline=True))


def parse_vfy(line):
    m = VFY_RE.search(line or '')
    if not m:
        return None
    return {'bootsel': m.group('b'), 'reset': m.group('r'), 'hb_us': int(m.group('hb'))}


def parse_edges(lines):
    """[(n, 'H'|'L', t_us), ...] from PROBE READ output."""
    out = []
    for line in lines or []:
        m = EDGE_RE.match(line)
        if m:
            out.append((int(m.group('n')), m.group('lvl'), int(m.group('us'))))
    return out


def probe_windows(edges):
    """Turn probe edges into (label, seconds) windows.

    The probe line is HIGH while the target is awake, so an interval that
    *starts* with a falling edge is a sleep and one that starts with a rising
    edge is an awake window. Timestamps are the hand's micros(), which wraps
    about every 71 minutes — differences are taken modulo 2**32 so a
    measurement that straddles the wrap still reads correctly.
    """
    out = []
    for (_i1, l1, u1), (_i2, _l2, u2) in zip(edges, edges[1:]):
        out.append(('asleep' if l1 == 'L' else 'awake',
                    ((u2 - u1) % (1 << 32)) / 1e6))
    return out


# --------------------------------------------------------------------------
# target serial (emergency console)
# --------------------------------------------------------------------------

class Target:
    def __init__(self, path=None):
        self.path = path or by_id_path(TARGET_SERIAL, pico_w=True)
        self.usb = UsbWatcher(self.path)
        self.ser = None

    def open(self, timeout=30):
        if serial is None:
            raise TestSkip('pyserial missing')
        self.close()
        if self.usb.wait(True, timeout) is None:
            raise TestFail(f'target absent from USB for {timeout}s')
        # the CDC node needs a moment after enumeration
        for _ in range(20):
            try:
                self.ser = serial.Serial(self.path, BAUD, timeout=0.2)
                self.ser.dtr = True
                return
            except Exception:
                time.sleep(0.25)
        raise TestFail('could not open target serial port')

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def read_until(self, pattern, timeout, collect=None):
        """Read lines until `pattern` (compiled regex) matches; returns the match."""
        buf, deadline = '', time.time() + timeout
        while time.time() < deadline:
            try:
                raw = self.ser.readline()
            except Exception:
                return None      # port vanished: the device went to sleep
            if not raw:
                continue
            line = raw.decode('utf-8', 'replace')
            buf += line
            if collect is not None:
                collect.append(line.rstrip())
            m = pattern.search(line)
            if m:
                return m
        return None

    def cmd(self, text, timeout=6.0):
        """Send one line, return the transcript up to the next prompt."""
        if not self.ser:
            self.open()
        try:
            self.ser.reset_input_buffer()
            self.ser.write((text + '\r\n').encode())
        except Exception as exc:
            raise TestFail(f'serial write failed: {exc}')
        buf, deadline = b'', time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = self.ser.read(256)
            except Exception:
                break
            if chunk:
                buf += chunk
                if PROMPT_RE.search(buf.decode('utf-8', 'replace').rstrip()):
                    break
        return buf.decode('utf-8', 'replace')

    def air_status(self, retry_s=0):
        """Ask the device where it is.

        `retry_s` is for callers that catch a device at the start of an M1 wake:
        the port enumerates about ten seconds before the console answers, so the
        first reply is boot chatter and parsing it fails on a device that is
        perfectly healthy. Polling until it parses is the same trick ensure_m0
        uses, and for the same reason.
        """
        deadline = time.time() + retry_s
        while True:
            out = self.cmd('air status')
            st = parse_air_status(out)
            if st:
                return st
            if time.time() >= deadline:
                raise TestFail(f'air status unparsable: {out.strip()[-120:]!r}')
            time.sleep(1.0)

    def ip(self):
        m = re.search(r'IP:\s*(\d+\.\d+\.\d+\.\d+)', self.cmd('show net status', 8))
        return m.group(1) if m else None

    def ssid(self):
        """The configured SSID, from `show system info`.

        Not from `show net status`, which does not print one, and the value sits
        on the line AFTER the label — the emergency console wraps its fields.
        """
        out = self.cmd('show system info', 10)
        lines = [l.strip() for l in out.splitlines()]
        for i, l in enumerate(lines):
            if 'SSID' in l:
                tail = l.split(':', 1)[1].strip() if ':' in l else ''
                if tail:
                    return tail
                if i + 1 < len(lines) and lines[i + 1]:
                    return lines[i + 1]
        return None


def parse_air_status(text):
    m = AIR_STATUS_RE.search(text or '')
    if not m:
        return None
    # Optional groups are None on a firmware that predates the field, and int(None)
    # would turn "this build is older" into an instrument crash.
    d = {k: (int(v) if v is not None else None)
         for k, v in m.groupdict().items() if k != 'pin'}
    d['pin'] = m.group('pin')          # "16", "off" or None (firmware without F14)
    return d


# --------------------------------------------------------------------------
# web
# --------------------------------------------------------------------------

def sha256_frontend(password):
    """The login page hashes UTF-16 code units as bytes: latin-1, not UTF-8."""
    return hashlib.sha256(password.encode('latin-1')).hexdigest()


class Web:
    def __init__(self, host, scheme='http', timeout=15):
        if requests is None:
            raise TestSkip('requests missing')
        self.base = f'{scheme}://{host}'
        self.s = requests.Session()
        self.timeout = timeout

    def get(self, path, **kw):
        """GET with retries.

        The device drops the odd request while it settles after a boot or a
        port change: /api/config in particular answers with a truncated chunked
        body (ChunkedEncodingError) and works on the next try. Retrying is safe
        here because every GET in this suite is a read. POSTs are NOT retried —
        /api/commit_all saves and reboots, so a blind retry would reboot twice.
        """
        kw.setdefault('allow_redirects', False)
        last = None
        for attempt in range(3):
            try:
                return self.s.get(self.base + path, timeout=self.timeout, **kw)
            except Exception as exc:      # connection reset, chunked truncation
                last = exc
                time.sleep(1.5 * (attempt + 1))
        raise TestFail(f'GET {path} failed after 3 tries: {type(last).__name__}: {last}')

    def post(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.post(self.base + path, timeout=self.timeout, **kw)

    def login(self, user, password):
        r = self.get('/api/login_init')
        if r.status_code != 200:
            raise TestFail(f'login_init HTTP {r.status_code}')
        nonce = r.json().get('nonce', '')
        r = self.post('/api/login', data={'user': user, 'pass': sha256_frontend(password),
                                          'nonce': nonce})
        if 'SIMUTSESS' not in self.s.cookies.get_dict():
            raise TestFail(f'login failed HTTP {r.status_code}: {r.text[:80]}')

    def status(self):
        r = self.get('/api/status')
        if r.status_code != 200:
            raise TestFail(f'/api/status HTTP {r.status_code}')
        j = r.json()
        if 'sys' not in j:
            raise TestFail('/api/status without sys — session lost')
        return j

    def config(self):
        r = self.get('/api/config')
        if r.status_code != 200:
            raise TestFail(f'/api/config HTTP {r.status_code}')
        return r.json()

    def commit_sys(self, fields):
        """POST /api/commit_all {"sys":{...}} — the device saves AND reboots."""
        payload = json.dumps({'sys': fields}, separators=(',', ':'))
        r = self.post('/api/commit_all', data={'_payload': payload})
        if r.status_code not in (200, 202):
            raise TestFail(f'commit_all HTTP {r.status_code}: {r.text[:120]}')
        try:
            rejected = r.json().get('rejected', [])
        except Exception:
            rejected = []
        if rejected:
            raise TestFail(f'commit_all rejected fields: {rejected}')
        return r

    def download(self, path):
        r = self.get('/download', params={'file': path})
        if r.status_code != 200:
            raise TestFail(f'download {path} HTTP {r.status_code}')
        return r.content

    def wait_up(self, timeout=120):
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                if self.get('/api/login_init').status_code < 500:
                    return time.time() - t0
            except Exception:
                pass
            time.sleep(1)
        return None


def tcp_open(host, port, timeout=2.0):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def host_ip_toward(target_ip):
    if os.environ.get('SIMUT_COLLECTOR_IP'):
        return os.environ['SIMUT_COLLECTOR_IP']
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((target_ip, 9))
        return s.getsockname()[0]
    finally:
        s.close()


# --------------------------------------------------------------------------
# telemetry collector (counts what the device uploads)
# --------------------------------------------------------------------------

class Collector:
    def __init__(self, port):
        self.port = port
        self.posts = []        # (t, path, n_records)
        self.lock = threading.Lock()
        outer = self

        class H(BaseHTTPRequestHandler):
            def do_POST(self):
                n = int(self.headers.get('Content-Length', 0) or 0)
                body = self.rfile.read(n) if n else b''
                with outer.lock:
                    outer.posts.append((time.time(), self.path, count_records(body)))
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', '2')
                self.end_headers()
                self.wfile.write(b'{}')

            def log_message(self, *a):
                pass

        self.srv = HTTPServer(('0.0.0.0', port), H)
        self.thread = threading.Thread(target=self.srv.serve_forever, daemon=True)

    def start(self):
        self.thread.start()

    def stop(self):
        self.srv.shutdown()

    def records_since(self, t):
        with self.lock:
            return sum(n for (ts, _, n) in self.posts if ts >= t)

    def posts_since(self, t):
        with self.lock:
            return [p for p in self.posts if p[0] >= t]


def count_records(body):
    """JSON array/object with a list, or CSV lines — best effort record count."""
    if not body:
        return 0
    try:
        j = json.loads(body.decode('utf-8', 'replace'))
        if isinstance(j, list):
            return len(j)
        if isinstance(j, dict):
            for v in j.values():
                if isinstance(v, list):
                    return len(v)
            return 1
    except Exception:
        pass
    return max(1, body.count(b'\n'))


# --------------------------------------------------------------------------
# history helpers (V5 decoder from tools/history_v5.py)
# --------------------------------------------------------------------------

def h5_epochs(blob, nominal_s):
    """Decode record timestamps from a V5 day file.

    `nominal_s` MUST be the device's own history interval in seconds
    (h5NominalSeconds(h_int)). V5 encodes each record as a deviation from the
    nominal step, so decoding with the wrong nominal silently rewrites every
    interior timestamp: reading a 120 s file with the 60 s default compresses
    each block and manufactures both "bursts" and backwards gaps that are not in
    the data. That mistake cost a wrong finding on 2026-09-06 — the parameter is
    required here so it cannot be defaulted away again.
    """
    import history_v5  # noqa: E402  (tools/ is on sys.path)
    return [epoch for (_schema, epoch, _values)
            in history_v5.read_series(blob, nominal_interval_s=nominal_s)]


def h5_block_anchors(blob):
    """(t0, count) of every DATA block — absolute, so immune to the nominal.

    This is the reading to trust when the question is "when did the device wake":
    each block header carries its own t0, decoded from the file rather than
    reconstructed from an interval.
    """
    import history_v5
    return [(e.header.t0, e.header.count) for e in history_v5.scan(blob)
            if e.kind == 'data']


def gap_report(epochs, expected_s, tol_frac=0.25):
    """Classify consecutive record gaps against the configured interval.

    Returns (backwards, on_time, short, long) counts. `backwards` is the one
    that means the file is corrupt rather than merely late: a history file whose
    records go back in time cannot be read by anything that assumes order, and
    the V5 scanner has to give up its fast path when it sees it.
    """
    gaps = [b - a for a, b in zip(epochs, epochs[1:])]
    lo, hi = expected_s * (1 - tol_frac), expected_s * (1 + tol_frac)
    backwards = [g for g in gaps if g < 0]
    on_time = [g for g in gaps if lo <= g <= hi]
    short = [g for g in gaps if 0 <= g < lo]
    long_ = [g for g in gaps if g > hi]
    return backwards, on_time, short, long_


def spacing_ok(epochs, expected_s, tol_s, last_n):
    """True when the last `last_n` gaps are within expected±tol."""
    if len(epochs) < last_n + 1:
        return False, 'not enough records'
    gaps = [b - a for a, b in zip(epochs[-last_n - 1:-1], epochs[-last_n:])]
    bad = [g for g in gaps if abs(g - expected_s) > tol_s]
    return (not bad), f'gaps={gaps}'


# --------------------------------------------------------------------------
# the suite
# --------------------------------------------------------------------------

class Suite:
    def __init__(self, args):
        self.args = args
        self.res = Results()
        self.hand = Hand()
        self.target = Target()
        self.web = None
        self.collector = None
        self.host = args.host or os.environ.get('SIMUT_HOST')
        self.saved = {}          # config to restore at the end
        self.cycle_data = []     # per-cycle timing rows
        # (id, name, fn, xfail, needs) — `needs` keeps setup() from demanding a
        # target for tests that do not use one. Without it a missing target
        # blocked even the hand health check, which is exactly the instrument
        # you reach for when the target is missing.
        self.tests = [
            ('T01', 'hand_health', self.t01_hand_health, None, {'hand'}),
            ('T02', 'target_boot_m0', self.t02_target_boot_m0, None, {'target'}),
            ('T03', 'air_status_fields', self.t03_air_status_fields, 'F14', {'target'}),
            ('T04', 'air_idle_bounds', self.t04_air_idle_bounds, 'F09', {'target'}),
            ('T05', 'hibernate_cycles', self.t05_hibernate_cycles, 'F01', {'target'}),
            ('T06', 'telemetry_drain', self.t06_telemetry_drain, 'F05', {'target', 'web'}),
            ('T06b', 'telemetry_off_sleeps', self.t06b_telemetry_off_sleeps, 'F02', {'target', 'web'}),
            ('T07', 'web_activity_resets_idle', self.t07_web_activity_resets_idle, 'F21', {'target', 'web'}),
            ('T08', 'offline_timestamps', self.t08_offline_timestamps, 'F04', {'target', 'web'}),
            ('T09', 'probe_cycle', self.t09_probe_cycle, None, {'target', 'hand'}),
            ('T10', 'm1_services_off', self.t10_m1_services_off, 'F13', {'target', 'web'}),
            ('T11', 'history_integrity', self.t11_history_integrity, 'F23', {'target', 'web'}),
            ('T12', 'cycle_survives_reset', self.t12_cycle_survives_reset, None, {'target', 'hand'}),
            ('T13', 'two_schedules', self.t13_two_schedules, None, {'target'}),
        ]

    def selected(self):
        only = set(self.args.only.split(',')) if self.args.only else None
        return [t for t in self.tests if not only or t[0] in only]

    def needs(self, what):
        return any(what in t[4] for t in self.selected())

    # ---- infrastructure -------------------------------------------------

    def setup(self):
        if not self.needs('target'):
            print('  no selected test needs the target — skipping target/web setup')
            return
        if not self.target.usb.present():
            # a sleeping Air is absent by design: wait one history interval
            print('  target absent from USB — waiting up to 6 min for a wake window')
            if self.target.usb.wait(True, 360) is None:
                if self.hand.available and self.hand.ping():
                    print('  still absent: hand RESET (note: RESET during SLEEP boots M1)')
                    self.hand.reset()
                    if self.target.usb.wait(True, 30) is None:
                        sys.exit('target does not enumerate even after RESET — power-cycle it (see plan F01)')
                else:
                    sys.exit('target absent and no PicoHand to recover it')
        self.target.open()
        self.ensure_m0()
        if not self.host:
            self.host = self.target.ip()
        user, pw = os.environ.get('SIMUT_WEB_USER'), os.environ.get('SIMUT_WEB_PASS')
        if self.needs('web') and self.host and user and pw and requests is not None:
            try:
                self.web = Web(self.host)
                self.web.login(user, pw)
            except Exception as exc:
                print(f'  web unavailable ({exc}) — web-dependent tests will be skipped')
                self.web = None
        if self.web:
            self.collector = Collector(self.args.collector_port)
            self.collector.start()

    def teardown(self):
        try:
            self.restore_config()
        except Exception as exc:
            print(f'  restore failed: {exc}')
        if self.collector:
            self.collector.stop()
        self.hand.release_all()
        self.target.close()

    def ensure_m0(self, timeout=420):
        """Get the target into M0 with a live prompt, cancelling M1 if needed.

        Two things make this harder than one command. An M1 wake spends most of
        its short window booting, so the port can be open while the console is
        still silent — asking once and believing the answer reported "not in M0"
        for a device that was merely still booting. And if the window closes
        first, the only option is to wait for the next wake and try again.

        So: poll `air status` until it actually parses, then cancel M1 if that
        is where it is, and re-check. Losing the port mid-way is not an error,
        it is the device going back to sleep — wait for the next one.
        """
        deadline = time.time() + timeout
        last = ''
        while time.time() < deadline:
            if not self.target.usb.present():
                if self.target.usb.wait(True, max(5, deadline - time.time())) is None:
                    break
            try:
                self.target.open(timeout=20)
            except TestFail as exc:
                last = str(exc)
                continue
            st = None
            settle = time.time() + 45          # a wake boot takes ~25 s
            while time.time() < settle and time.time() < deadline:
                out = self.target.cmd('air status', 6)
                st = parse_air_status(out)
                if st:
                    break
                last = out.strip()[-80:]
                if not self.target.usb.present():
                    break                       # slept again: fall out and retry
            if not st:
                continue
            if st['phase'] != 0:
                self.target.cmd('air stop', 5)
                st = parse_air_status(self.target.cmd('air status', 6))
            if st and st['phase'] == 0:
                return st
            last = f'phase={st["phase"] if st else "?"} after air stop'
        raise TestFail(f'could not reach M0 within {timeout}s (last: {last!r})')

    def need_web(self):
        if not self.web:
            raise TestSkip('web not available (SIMUT_WEB_USER/PASS, host)')

    def snapshot_config(self):
        if self.web and not self.saved:
            cfg = self.web.config()
            keep = {}
            for k in list(TEL_FIELDS.values()) + [HIST_FIELD]:
                if k in cfg:
                    keep[k] = cfg[k]
            self.saved = keep

    def commit_and_reboot(self, fields):
        """commit_all reboots the device: wait for USB + prompt + web."""
        self.need_web()
        self.snapshot_config()
        self.target.close()
        self.web.commit_sys(fields)
        time.sleep(3)
        self.target.usb.wait(False, 20)
        self.target.open(timeout=120)
        self.ensure_m0()
        if self.web.wait_up(120) is None:
            raise TestFail('web did not come back after commit_all')
        self.web.login(os.environ['SIMUT_WEB_USER'], os.environ['SIMUT_WEB_PASS'])

    def restore_config(self):
        if self.saved and self.web:
            print('  restoring telemetry/history config …')
            try:
                # Same reason as T13: a device left in the cycle has no web most
                # of the time, so the restore has to bring it to M0 first or it
                # fails and silently leaves the bench misconfigured. And the
                # session may have died while the device was asleep — a stale
                # cookie answers 403, not 401, which reads like a permission bug
                # rather than an expired login.
                self.ensure_m0()
                self.web.wait_up(120)
                self.web.login(os.environ['SIMUT_WEB_USER'], os.environ['SIMUT_WEB_PASS'])
                self.commit_and_reboot(self.saved)
            finally:
                self.saved = {}

    def point_telemetry_here(self, interval_ms, batch=50):
        self.need_web()
        me = host_ip_toward(self.host)
        f = {TEL_FIELDS['server']: me, TEL_FIELDS['port']: self.args.collector_port,
             TEL_FIELDS['path']: '/telemetry', TEL_FIELDS['tls']: 0,
             TEL_FIELDS['interval_ms']: interval_ms, TEL_FIELDS['batch']: batch}
        if self.args.hist_interval:
            f[HIST_FIELD] = self.args.hist_interval
        self.commit_and_reboot(f)

    def hibernate_and_observe(self, stop_on_wake=True, on_wake=None):
        """`air hibernate` → watch phases → sleep → wake. Returns timing dict."""
        row = {}
        lines = []
        t_cmd = time.time()
        # Do NOT use cmd() here. It reads for a couple of seconds and throws the
        # transcript away, and a device whose sensors are already stable, WiFi up
        # and queue empty goes from `air hibernate` to `[AIR] alarm:` in well
        # under a second — the line lands inside that discarded window and the
        # next read only sees the port vanish. Measured on the bench 2026-09-06:
        # the first live run of this suite failed as "serial vanished before the
        # alarm line" for exactly this reason. Write, then read one stream.
        if not self.target.ser:
            self.target.open()
        self.target.ser.reset_input_buffer()
        self.target.ser.write(b'air hibernate\r\n')
        m = self.target.read_until(ALARM_RE, timeout=self.args.flush_cap, collect=lines)
        phases = {pm.group('name'): int(pm.group('ms')) for pm in map(PHASE_RE.search, lines) if pm}
        row['phases_ms'] = phases
        if m is None:
            if self.target.usb.present():
                raise TestFail(f'no [AIR] alarm line within {self.args.flush_cap}s — device stuck awake '
                               f'(phases seen: {sorted(phases)})')
            raise TestFail('serial vanished before the alarm line was seen — the device slept, but the '
                           'line was missed (check the read path, not the firmware)')
        row['wake_sec'] = int(m.group('sec'))
        row['awake_before_sleep_s'] = round(time.time() - t_cmd, 1)
        self.target.close()
        t_absent = self.target.usb.wait(False, 60)
        if t_absent is None:
            raise TestFail('USB did not detach after the alarm line (sleep entry failed)')
        row['t_absent'] = t_absent
        t_present = self.target.usb.wait(True, row['wake_sec'] + self.args.wake_grace)
        if t_present is None:
            row['woke'] = False
            self.recover_missing_wake(row)
            raise TestFail(f"no wake after wakeSec={row['wake_sec']}s + {self.args.wake_grace}s grace "
                           f"(F01 suspect); recovery={row.get('recovery')}")
        row['woke'] = True
        row['sleep_s'] = round(t_present - t_absent, 1)
        row['period_error_s'] = round(row['sleep_s'] - row['wake_sec'], 1)
        if on_wake:
            on_wake(row)
        if stop_on_wake:
            self.target.open(timeout=15)
            self.target.cmd('air stop', 3)
            st = self.target.air_status()
            row['stopped'] = st['phase'] == 0
        return row

    def recover_missing_wake(self, row):
        """Wake did not happen: get the bench back with the hand.

        RESET drives RUN — a global chip reset that restores the ROSC and the
        default clocks — so it recovers the target even when the sleep path is
        broken. That is why it is recovery only, never evidence: the verdict on
        F01 is whether the target re-enumerated ON ITS OWN, which the caller
        already decided before calling this.
        """
        if not (self.hand.available and self.hand.ping()):
            row['recovery'] = 'no hand available — power-cycle the target by hand'
            return
        self.hand.reset()
        if self.target.usb.wait(True, 30):
            row['recovery'] = 'hand RESET recovered the bench (proves nothing about the wake path)'
            try:
                self.target.open(15)
                self.ensure_m0()
            except Exception:
                pass
            return
        row['recovery'] = 'RESET did not enumerate: target unpowered or unplugged, not hung'

    def watch_awake_window(self, timeout):
        """After a wake (M1), measure how long the device stays enumerated."""
        t0 = time.time()
        t_absent = self.target.usb.wait(False, timeout)
        return round(t_absent - t0, 1) if t_absent else None

    # ---- tests ------------------------------------------------------------

    def t01_hand_health(self):
        if not self.hand.available:
            raise TestSkip('PicoHand not on the bus')
        if not self.hand.ping():
            raise TestFail('PING without PONG')
        st = self.hand.cmd('STATUS')
        if 'RELEASED' not in st:
            raise TestFail(f'lines not released: {st}')
        v = self.hand.verify()
        if not v or v['reset'] != 'OK':
            raise TestFail(f'VERIFY RESET not OK: {v}')
        return f"hb={v['hb_us']}us probe={'yes' if self.hand.probe_supported() else 'no'}"

    def t02_target_boot_m0(self):
        """Cold boot through the hand, and MEASURE what a physical reset does
        to the hibernation marker.

        src/LogManager.cpp:605 states the scratch registers are zeroed by
        "power cycle / physical reset", and the Air marker lives in scratch[0],
        so a RUN-pin reset should land in M0. That is documentation, not a
        measurement — so read `air status` before sending any `air stop` and
        report the mode the device actually came up in.
        """
        reset_mode = 'not tested (no hand)'
        if self.hand.available and self.hand.ping():
            self.target.close()
            self.hand.reset()
            if self.target.usb.wait(True, 40) is None:
                raise TestFail('no USB enumeration 40 s after hand RESET — target unpowered?')
            self.target.open(30)
            st0 = parse_air_status(self.target.cmd('air status'))
            if st0 is None:
                raise TestFail('air status unparsable right after reset')
            reset_mode = 'M0 (scratch cleared, as documented)' if st0['phase'] == 0 \
                else f'M1 phase={st0["phase"]} (scratch[0] SURVIVED a physical reset — fix the docs)'
        self.target.open(30)
        self.ensure_m0()
        info = self.target.cmd('show system info', 8)
        m = re.search(r'(\d+\.\d+\.\d+[-\w]*)', info)
        if 'Firmware' not in info and 'SIMUT' not in info:
            raise TestFail('show system info did not answer')
        st = self.target.air_status()
        if st['phase'] != 0:
            raise TestFail(f'phase={st["phase"]} in M0')
        return (f'fw={m.group(1) if m else "?"} idle={st["idle"]}s hist={st["hist"]}s; '
                f'after physical reset: {reset_mode}')

    def t03_air_status_fields(self):
        st = self.target.air_status()
        if st['wake'] != max(st['hist'], st['backoff']):
            raise TestFail(f'wake={st["wake"]} != max(hist,backoff)={max(st["hist"], st["backoff"])}')
        if st['hist'] <= 0 or st['idle'] <= 0:
            raise TestFail(f'zero fields: {st}')
        if st.get('pin') is None:
            raise TestFail('no pin= field (F14: sensor power pin not reported)')
        return str(st)

    def t04_air_idle_bounds(self):
        before = self.target.air_status()['idle']
        try:
            def idle_set(v):
                out = self.target.cmd(f'air idle {v}', 4)
                return 'set' in out.lower() and 'error' not in out.lower() and '<' not in out
            if idle_set(9):
                raise TestFail('air idle 9 accepted (min is 10)')
            if not idle_set(10):
                raise TestFail('air idle 10 rejected')
            if not idle_set(65535):
                raise TestFail('air idle 65535 rejected')
            if idle_set(65536):
                raise TestFail('air idle 65536 accepted (uint16 overflow, F09)')
            if idle_set(86400):
                raise TestFail('air idle 86400 accepted (stored as 20864, F09)')
            st = self.target.air_status()
            if st['idle'] != 65535:
                raise TestFail(f'idle readback {st["idle"]} after 65535')
        finally:
            self.target.cmd(f'air idle {before}', 4)
        return 'bounds 10..65535 enforced'

    def t05_hibernate_cycles(self):
        if self.web and self.args.hist_interval:
            self.point_telemetry_here(interval_ms=1000)
        rows = []
        n = self.args.cycles
        for i in range(n):
            last = (i == n - 1)
            row = self.hibernate_and_observe(stop_on_wake=last)
            rows.append(row)
            if not last:
                # let M1 run its wake untouched and measure the awake window
                aw = self.watch_awake_window(self.args.flush_cap + 60)
                row['awake_s'] = aw
                if aw is None:
                    self.ensure_m0()
                    raise TestFail(f'cycle {i + 1}: M1 wake stayed awake > {self.args.flush_cap + 60}s')
                t_present = self.target.usb.wait(True, row['wake_sec'] + self.args.wake_grace)
                if t_present is None:
                    self.recover_missing_wake(row)
                    raise TestFail(f'cycle {i + 1}: no wake after the M1 window (F01 suspect); '
                                   f"recovery={row.get('recovery')}")
            self.cycle_data.append(row)
        sl = [r['sleep_s'] for r in rows]
        aw = [r.get('awake_s') for r in rows if r.get('awake_s') is not None]
        detail = f'cycles={n} sleep_s={sl} awake_s={aw} period_err={[r["period_error_s"] for r in rows]}'
        if any(abs(r['period_error_s']) > self.args.period_tol for r in rows):
            raise TestFail('period error beyond tolerance: ' + detail)
        return detail

    def t06_telemetry_drain(self):
        self.need_web()
        # 60 s cadence: with F05 the first batch waits a whole interval after boot
        self.point_telemetry_here(interval_ms=60000)
        t0 = time.time()
        row = self.hibernate_and_observe(stop_on_wake=False)
        aw = self.watch_awake_window(self.args.flush_cap + 60)
        self.ensure_m0()
        got = self.collector.records_since(t0)
        if got < 1:
            raise TestFail('collector received nothing during the wake')
        if aw is None or aw > 30:
            raise TestFail(f'awake window {aw}s with t_int=60s: drain is cadence-bound (F05); records={got}')
        return f'records={got} awake_s={aw} sleep_s={row["sleep_s"]}'

    def t06b_telemetry_off_sleeps(self):
        self.need_web()
        f = {TEL_FIELDS['interval_ms']: 0}
        if self.args.hist_interval:
            f[HIST_FIELD] = self.args.hist_interval
        self.commit_and_reboot(f)
        lines = []
        self.target.cmd('air hibernate', 2)
        m = self.target.read_until(ALARM_RE, timeout=90, collect=lines)
        if m is None and self.target.usb.present():
            self.ensure_m0()
            raise TestFail('telemetry off + WiFi up: stuck in FLUSH, never slept (F02)')
        if m is None:
            raise TestFail('serial vanished without an alarm line')
        self.target.close()
        self.target.usb.wait(False, 30)
        if self.target.usb.wait(True, int(m.group('sec')) + self.args.wake_grace) is None:
            raise TestFail('no wake after the telemetry-off cycle')
        self.ensure_m0()
        return f"slept after {m.group('sec')}s alarm with telemetry disabled"

    def t07_web_activity_resets_idle(self):
        self.need_web()
        before = self.target.air_status()['idle']
        self.target.cmd('air idle 60', 4)
        try:
            self.target.close()          # no serial traffic: only web activity
            t0 = time.time()
            while time.time() - t0 < 100:
                self.web.status()
                time.sleep(15)
            if not self.target.usb.present():
                # it hibernated under the web operator: wait the wake and stop
                self.target.usb.wait(True, 400)
                self.ensure_m0()
                raise TestFail('device hibernated while the web was in use (F21)')
            self.target.open()
            st = self.target.air_status()
            if st['phase'] != 0:
                raise TestFail(f'phase={st["phase"]} while web active')
        finally:
            self.target.open()
            self.target.cmd(f'air idle {before}', 4)
        return 'web activity kept M0 for 100 s with idle=60 s'

    def t08_offline_timestamps(self):
        if not self.args.long:
            raise TestSkip('long test — pass --long')
        self.need_web()
        good = self.target.ssid()
        if not good:
            raise TestFail('could not read the current SSID from show net status')
        hist_min = 2
        self.commit_and_reboot({HIST_FIELD: hist_min})
        wakes = 3
        try:
            self.target.cmd(f'system ssid {good}_nope', 4)
            self.target.close()
            self.target.cmd('reload confirm', 2)
            self.target.usb.wait(False, 20)
            self.target.open(120)
            self.ensure_m0()
            for i in range(wakes):
                self.hibernate_and_observe(stop_on_wake=(i == wakes - 1))
                if i < wakes - 1:
                    self.watch_awake_window(120)
                    self.target.usb.wait(True, hist_min * 60 + self.args.wake_grace)
        finally:
            self.ensure_m0()
            self.target.cmd(f'system ssid {good}', 4)
            self.target.close()
            self.target.cmd('reload confirm', 2)
            self.target.usb.wait(False, 20)
            self.target.open(120)
            self.ensure_m0()
            if self.web.wait_up(120) is None:
                raise TestFail('web did not come back after restoring the SSID')
            self.web.login(os.environ['SIMUT_WEB_USER'], os.environ['SIMUT_WEB_PASS'])
        day = time.strftime('%Y%m%d')
        blob = self.web.download(f'/history/{day}.h5')
        epochs = h5_epochs(blob, hist_min * 60)
        ok, detail = spacing_ok(epochs, hist_min * 60, 25, wakes)
        if not ok:
            raise TestFail(f'offline wakes not spaced by the real sleep (F04): {detail}')
        return detail

    def t09_probe_cycle(self):
        """Time the cycle through the PicoHand probe instead of USB.

        The probe watches the power-gating line (HIGH awake, LOW asleep) at
        10 kHz, which beats USB enumeration by a wide margin: enumeration lags
        the boot by about a second and that second lands straight in the
        measured sleep. It also detects the glitch F07 fixed, because a
        gpio_init on every pump shows up as a pair of edges microseconds apart.
        """
        if not (self.hand.available and self.hand.probe_supported()):
            raise TestSkip('PicoHand without the PROBE channel')
        if not self.hand.probe_start():
            raise TestFail('PROBE START refused')
        row = self.hibernate_and_observe(stop_on_wake=False)
        if self.target.usb.wait(True, row['wake_sec'] + self.args.wake_grace) is None:
            raise TestFail('no wake while the probe was armed')
        self.ensure_m0()
        edges = self.hand.probe_read()
        if len(edges) < 2:
            raise TestFail(f'probe saw {len(edges)} edge(s) — is GP16 wired to the hand? {edges}')
        wins = probe_windows(edges)

        glitches = [(a, b) for (a, b) in zip(edges, edges[1:])
                    if ((b[2] - a[2]) % (1 << 32)) < 5000]
        if glitches:
            raise TestFail(f'{len(glitches)} edge pair(s) less than 5 ms apart — the line is '
                           f'glitching (F07): {glitches[:2]}')

        asleep = [s for kind, s in wins if kind == 'asleep']
        if not asleep:
            raise TestFail(f'no sleep window in the capture: {wins}')
        # The first sleep is the one this test asked for, so it is the one whose
        # requested length we know from the alarm line.
        want, got = row['wake_sec'], asleep[0]
        if abs(got - want) > max(2.0, want * 0.02):
            raise TestFail(f'slept {got:.3f} s against an alarm of {want} s; windows={wins}')
        return (f'{len(edges)} edges; ' +
                ', '.join(f'{k}={s:.3f}s' for k, s in wins))

    def t10_m1_services_off(self):
        if not self.host:
            raise TestSkip('no host IP')
        state = {}

        def on_wake(row):
            time.sleep(8)            # give the M1 boot time to (not) bind the web port
            state['web80'] = tcp_open(self.host, 80, 2.0)

        row = self.hibernate_and_observe(stop_on_wake=True, on_wake=on_wake)
        if state.get('web80'):
            raise TestFail('port 80 accepts connections during an M1 wake (F13: D5 not implemented)')
        return f'port 80 closed in M1; sleep_s={row["sleep_s"]}'

    def t11_history_integrity(self):
        """The history must say WHEN each sample was taken.

        Two independent failures live here, both measured on 2026-09-06:
          * records that go backwards in time (the file is not monotonic);
          * bursts spaced at a fixed nominal interval covering stretches the
            device spent asleep, so measurements are filed at times they were
            not taken.
        Downloading the day file and decoding it is the only view that shows
        this: /api/status and the log both look healthy while it happens.
        """
        self.need_web()
        cfg = self.web.config()
        hint_min = int(cfg.get('h_int') or 0)
        if hint_min <= 0:
            raise TestSkip('h_int not readable from /api/config')
        expected = hint_min * 60
        day = time.strftime('%Y%m%d')
        blob = self.web.download(f'/history/{day}.h5')
        eps = h5_epochs(blob, expected)
        if len(eps) < 6:
            raise TestSkip(f'only {len(eps)} records in {day}.h5 — not enough to judge')
        back, ok, short, long_ = gap_report(eps, expected)
        # Block anchors are absolute, so they date the wakes even when the
        # interior reconstruction is off: report them alongside the records.
        anchors = [t0 for t0, _n in h5_block_anchors(blob)]
        wake_gaps = [b - a for a, b in zip(anchors, anchors[1:])]
        late = [g for g in wake_gaps if g > expected * 2]
        summary = (f'{len(eps)} records, interval={expected}s: on-time={len(ok)} '
                   f'short={len(short)} long={len(long_)} backwards={len(back)}; '
                   f'{len(anchors)} blocks, {len(late)} wake gaps > {2 * expected}s')
        if back:
            raise TestFail(f'history is not monotonic — {len(back)} backwards gap(s) '
                           f'(e.g. {back[0]}s); {summary}')
        if len(ok) < 0.9 * (len(eps) - 1):
            raise TestFail(f'most gaps do not match the configured interval; {summary} '
                           f'(longest={max(long_) if long_ else 0}s)')
        return summary

    def t12_cycle_survives_reset(self):
        """A reset in the middle of the cycle must not leave the device awake.

        The hibernation marker is cleared on every boot on purpose, so that a
        device which dies inside the cycle stays reachable. The cost, measured
        on 2026-09-06, was that ANY reset dropped the device into M0 with the
        radio on, and only the idle timeout (300 s) could bring it back — which
        on a bench whose fault repeated every 54 to 107 s never happened. That
        is plan F25, and the fix is the armed flag in air.bin plus a short
        resume grace.

        The measurement has to be hands-off: every CLI command calls
        airMarkActivity( ) and rearms the idle timer, so asking the device
        whether it went back to sleep is exactly what stops it from going. USB
        enumeration answers instead — absent means asleep.
        """
        if not (self.hand.available and self.hand.ping()):
            raise TestSkip('needs the PicoHand to reset the target')
        # Arm the cycle and let it prove it is cycling.
        row = self.hibernate_and_observe(stop_on_wake=False)
        if self.target.usb.wait(True, row['wake_sec'] + self.args.wake_grace) is None:
            raise TestFail('no wake — the cycle was not running, so the reset proves nothing')

        # Reset mid-wake, then do not touch the port again.
        self.target.close()
        self.hand.reset()
        t_reset = time.time()
        if self.target.usb.wait(True, 40) is None:
            raise TestFail('no USB enumeration 40 s after the reset — target unpowered?')
        t_boot = time.time()

        # It must now go back to sleep on its own: grace + the rest of a wake.
        # Generous cap, because the point is "does it return at all", not how fast.
        budget = AIR_RESUME_GRACE_SEC + row['awake_before_sleep_s'] + self.args.wake_grace + 30
        t_gone = self.target.usb.wait(False, budget)
        if t_gone is None:
            raise TestFail(
                f'still awake {budget:.0f}s after a reset — the cycle did not resume (F25). '
                f'Check `air status` for armed=1; armed=0 means air.bin never recorded the intent')
        # Verdict is in; hand the bench back in a known state. Leaving the device
        # cycling makes the NEXT test start against a target that is asleep more
        # often than not, with no web server to talk to.
        self.target.open(60)
        self.ensure_m0()
        return (f'reset -> boot {t_boot - t_reset:.1f}s -> asleep again '
                f'{t_gone - t_boot:.1f}s later, with no command sent')

    def t13_two_schedules(self):
        """Readings on every wake, telemetry only on every Nth — and the radio
        only on those.

        The saving this feature exists for is not the transmission, it is the
        CYW43 never being powered on the wakes in between. So the verdict is not
        "did it send", it is "was the radio down on some wakes and up on others,
        and were the quiet ones cheaper".

        Deliberately does NOT reconfigure the device. Two earlier versions of
        this test set the telemetry interval through /api/commit_all and both
        failed the same way: commit_all reboots, the reboot lands the device
        back in the cycle, and in the cycle the web server only exists on
        telemetry wakes — so the restore could not reach it and left the bench
        misconfigured. The measurement needs no configuration of its own; it
        needs a bench that already has one.
        """
        st = self.target.air_status()
        every = st.get('televery')
        if every is None:
            raise TestSkip('firmware without the tel= field — older than the two schedules')
        if every <= 1:
            raise TestSkip(f'telemetry interval is at or below the reading interval '
                           f'(tel every {every} wake), so every wake sends and there is no '
                           f'schedule to observe — set t_int > h_int to exercise this')

        # Enter the cycle ONCE. Every wake after this one happens on its own, so
        # driving each with `air hibernate` would be fighting the device: sent
        # mid-wake the command adds nothing and the alarm line lands outside the
        # window the helper is watching, which is exactly how an earlier version
        # of this test failed with "serial vanished before the alarm line".
        row = self.hibernate_and_observe(stop_on_wake=False)

        radio_by_wake, awake_by_wake = [], []
        for _ in range(every + 1):
            t_up = self.target.usb.wait(True, row['wake_sec'] + self.args.wake_grace)
            if t_up is None:
                raise TestFail('no wake while measuring the schedule')
            self.target.open(30)
            # The wake has just enumerated; the console needs a few more seconds.
            st = self.target.air_status(retry_s=25)
            radio_by_wake.append(st.get('radio'))
            self.target.close()
            t_down = self.target.usb.wait(False, 240)
            if t_down is None:
                raise TestFail('device stayed awake instead of going back to sleep')
            # Enumeration lags the boot by about a second, so this reads slightly
            # short — fine, because the verdict is a comparison between wakes
            # measured the same way.
            awake_by_wake.append(t_down - t_up)

        self.ensure_m0()
        if None in radio_by_wake:
            raise TestSkip('firmware without the radio= field')
        ups = sum(1 for r in radio_by_wake if r)
        if ups == 0:
            raise TestFail(f'radio never came up in {len(radio_by_wake)} wakes — telemetry '
                           f'would never leave the device: {radio_by_wake}')
        if ups == len(radio_by_wake):
            raise TestFail(f'radio came up on EVERY wake — the schedule is not being '
                           f'applied: {radio_by_wake}')

        quiet = [a for a, r in zip(awake_by_wake, radio_by_wake) if not r]
        loud = [a for a, r in zip(awake_by_wake, radio_by_wake) if r]
        detail = (f'every {every} wakes; radio {radio_by_wake}; awake '
                  f'quiet={[round(a, 1) for a in quiet]}s loud={[round(a, 1) for a in loud]}s')
        # A reading-only wake that is not shorter means the network was started
        # anyway somewhere, which is the failure worth catching.
        if quiet and loud and min(loud) <= max(quiet):
            raise TestFail(f'reading-only wakes are not cheaper than telemetry wakes — '
                           f'something still brings the radio up. {detail}')
        return detail

    # ---- runner -----------------------------------------------------------

    def run(self):
        for tid, name, fn, xfail, _needs in self.selected():
            t0 = time.time()
            try:
                detail = fn() or ''
                outcome = 'XPASS' if xfail else 'PASS'
                if xfail:
                    detail = f'(marked xfail {xfail} — remove the mark) ' + detail
            except TestSkip as exc:
                outcome, detail = 'SKIP', str(exc)
            except TestFail as exc:
                outcome, detail = ('XFAIL' if xfail else 'FAIL'), f'{"[" + xfail + "] " if xfail else ""}{exc}'
            except KeyboardInterrupt:
                raise
            except Exception as exc:  # instrument error, not a verdict
                outcome, detail = 'FAIL', f'error: {type(exc).__name__}: {exc}'
            self.res.add(tid, name, outcome, detail, time.time() - t0)


# --------------------------------------------------------------------------
# flashing with PicoHand recovery (AGENTS.md recipe)
# --------------------------------------------------------------------------

def watch_cycle(seconds, quiet=False):
    """Passive measurement: log every USB appear/disappear of the target.

    Touches nothing — no serial, no reset, no config — so it measures the cycle
    the device actually runs, including the wake period, without the observer
    changing it. This is the instrument to trust when the question is "how long
    does it really sleep": every CLI command resets the M0 idle timer, and
    opening the port at the wrong moment perturbs the very window being timed.

    Prints one line per transition and a summary of awake/asleep durations.
    """
    usb = UsbWatcher(by_id_path(TARGET_SERIAL, pico_w=True))
    t0 = time.time()
    state = usb.present()
    print(f'[watch] start {time.strftime("%H:%M:%S")} target={"PRESENT" if state else "absent"} '
          f'for {seconds}s', flush=True)
    marks = [(t0, state)]
    while time.time() - t0 < seconds:
        time.sleep(0.25)
        now = usb.present()
        if now != state:
            t = time.time()
            prev = marks[-1][0]
            print(f'[watch] {time.strftime("%H:%M:%S")} +{t - t0:7.1f}s '
                  f'{"WAKE (enumerated)" if now else "SLEEP (detached)"} '
                  f'after {t - prev:.1f}s {"asleep" if now else "awake"}', flush=True)
            marks.append((t, now))
            state = now
    awake, asleep = [], []
    for (ta, sa), (tb, _sb) in zip(marks, marks[1:]):
        (awake if sa else asleep).append(tb - ta)
    print(f'[watch] done: {len(marks) - 1} transitions')
    if awake:
        print(f'[watch] awake  n={len(awake)} durations={[round(x, 1) for x in awake]}')
    if asleep:
        print(f'[watch] asleep n={len(asleep)} durations={[round(x, 1) for x in asleep]}')
    if not awake and not asleep:
        print('[watch] no transition seen — the device never changed state in this window')
    return 0


def bootsel_touch(timeout=15):
    """Put the target in BOOTSEL by opening its port at 1200 baud with DTR low.

    The classic Arduino auto-reset, and on this bench the only picotool-free way
    in. picotool's own force (-f) is not usable here: with two RP2040s on the bus
    it takes the first it finds — the PicoHand, whose sketch has no reset
    interface ("Unable to locate reset interface on the device") — and naming
    the target with --ser does not help either, because after the reboot the
    board enumerates in BOOTSEL under a different serial and the filter then
    matches nothing.
    """
    port = by_id_path(TARGET_SERIAL, pico_w=True)
    if not os.path.exists(port):
        return False
    try:
        s = serial.Serial(port, 1200)
        s.dtr = False
        time.sleep(0.3)
        s.close()
    except Exception:
        pass          # the port disappearing mid-touch is the expected outcome
    t0 = time.time()
    while time.time() - t0 < timeout:
        out = subprocess.run(['lsusb'], capture_output=True, text=True).stdout
        if '2e8a:0003' in out:      # RP2 Boot
            return True
        time.sleep(0.5)
    return False


def flash(uf2):
    """Flash the target, in the order that costs least.

    1. The 1200 bps touch on the target's own port, then picotool. Works
       whenever the target still answers USB, and cannot pick the wrong board.
    2. The PicoHand, for a target too wedged to answer USB at all.

    Order matters because the hand is not always usable — an IDE serial monitor
    holding its port is enough to take it away, and that must not block a flash
    the first path can do on its own.
    """
    if not os.path.exists(uf2):
        sys.exit(f'no such file: {uf2}')
    if bootsel_touch() and subprocess.call(['picotool', 'load', '-x', uf2]) == 0:
        print('[flash] 1200 bps touch + picotool load ok', flush=True)
        return 0
    print('[flash] the touch path did not work — forcing BOOTSEL through the PicoHand', flush=True)
    hand = Hand()
    if not (hand.available and hand.ping()):
        sys.exit('[flash] the hand is unavailable too (port busy? close any serial monitor) '
                 '— nothing left to try automatically')
    if not hand.bootsel():
        sys.exit('[flash] hand BOOTSEL failed (see the PicoHand manual §7.1 on the wiring)')
    time.sleep(2)
    rc = subprocess.call(['picotool', 'load', '-x', uf2])
    hand.release_all()
    return rc


# --------------------------------------------------------------------------
# selftest (no hardware): parsers and helpers
# --------------------------------------------------------------------------

def selftest():
    ok = True

    def check(name, cond):
        nonlocal ok
        print(f'  [{"PASS" if cond else "FAIL"}] {name}')
        ok = ok and cond

    st = parse_air_status('Air: phase=0 wake=300s hist=300s backoff=0s idle=300s')
    check('air status parse', st == {'phase': 0, 'wake': 300, 'hist': 300, 'backoff': 0,
                                     'idle': 300, 'pin': None})
    st2 = parse_air_status('Air: phase=2 wake=900s hist=60s backoff=900s idle=60s pin=16')
    check('air status with pin', st2 and st2['pin'] == '16' and st2['wake'] == max(st2['hist'], st2['backoff']))
    m = ALARM_RE.search('[AIR] alarm: 00:05:00 wakeSec=300\r\n')
    check('alarm line parse', m is not None and int(m.group('sec')) == 300)
    pm = PHASE_RE.search('[AIR] phase=SAMPLE @1234')
    check('phase line parse', pm is not None and pm.group('name') == 'SAMPLE')
    v = parse_vfy('VFY BOOTSEL=OK RESET=OK HB=2us E:BOOTSEL=HIGH E:RESET=HIGH A:BOOTSEL=HIGH A:RESET=HIGH')
    check('VFY parse', v == {'bootsel': 'OK', 'reset': 'OK', 'hb_us': 2})
    e = parse_edges(['OK PROBE READ', 'EDGE 0 H 1000', 'EDGE 1 L 5000', 'DONE PROBE'])
    check('PROBE edges parse', e == [(0, 'H', 1000), (1, 'L', 5000)])
    # the real 2026-09-06 capture: sleep, wake, sleep again
    w = probe_windows([(0, 'L', 287679546), (1, 'H', 408394395),
                       (2, 'L', 437849200), (3, 'H', 527262405)])
    check('probe windows', [k for k, _ in w] == ['asleep', 'awake', 'asleep']
          and abs(w[0][1] - 120.715) < 0.01 and abs(w[1][1] - 29.455) < 0.01)
    wrapped = probe_windows([(0, 'L', (1 << 32) - 1_000_000), (1, 'H', 1_000_000)])
    check('probe windows survive the micros() wrap', abs(wrapped[0][1] - 2.0) < 0.001)
    check('count_records json list', count_records(b'[{"a":1},{"a":2}]') == 2)
    check('count_records json dict', count_records(b'{"records":[1,2,3]}') == 3)
    check('count_records csv', count_records(b'a,b\n1,2\n3,4\n') == 3)
    ok1, _ = spacing_ok([0, 120, 241, 358], 120, 25, 3)
    ok2, _ = spacing_ok([0, 80, 160, 240], 120, 25, 3)
    check('spacing ok / compressed detected', ok1 and not ok2)
    # the real 2026-09-06 shape: three 60 s records, one backwards, one huge jump
    back, on_time, short, long_ = gap_report([0, 60, 120, 103, 1920], 120)
    check('gap_report finds the backwards gap', len(back) == 1 and back[0] == -17)
    check('gap_report classifies short/long', len(short) == 2 and len(long_) == 1 and not on_time)
    b2, ok2b, s2, l2 = gap_report([0, 120, 240, 360], 120)
    check('gap_report clean file', not b2 and len(ok2b) == 3 and not s2 and not l2)
    check('sha256 latin-1', sha256_frontend('simutV5x') == hashlib.sha256(b'simutV5x').hexdigest())
    check('by-id paths', by_id_path('X', True).endswith('Pico_W_X-if00') and by_id_path('Y', False).endswith('Pico_Y-if00'))
    try:
        import history_v5  # noqa: F401
        check('history_v5 importable', True)
    except Exception as exc:
        check(f'history_v5 importable ({exc})', False)
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', help='device IP (default: SIMUT_HOST or show net status)')
    ap.add_argument('--only', help='comma-separated test ids, e.g. T05,T06')
    ap.add_argument('--list', action='store_true', help='list tests and exit')
    ap.add_argument('--selftest', action='store_true', help='parser/helper checks, no hardware')
    ap.add_argument('--flash', metavar='UF2', help='flash the target (picotool, PicoHand fallback) and exit')
    ap.add_argument('--watch', type=int, metavar='SECONDS',
                    help='passive: log USB wake/sleep transitions for N seconds and exit (touches nothing)')
    ap.add_argument('--cycles', type=int, default=3, help='hibernate/wake cycles in T05 (default 3)')
    ap.add_argument('--hist-interval', type=int, default=1, help='h_int (min) set for the run; 0 = leave as is')
    ap.add_argument('--flush-cap', type=int, default=180, help='seconds to wait for the alarm line (awake cap)')
    ap.add_argument('--wake-grace', type=int, default=120, help='seconds beyond wakeSec before declaring no wake')
    ap.add_argument('--period-tol', type=float, default=45.0, help='tolerated |sleep_s - wakeSec| (s)')
    ap.add_argument('--collector-port', type=int, default=8010)
    ap.add_argument('--long', action='store_true', help='include long tests (T08)')
    ap.add_argument('--baseline', action='store_true', help='exit 0 even with FAIL (record the state)')
    ap.add_argument('--report', help='write a JSON report here')
    args = ap.parse_args()

    suite = Suite(args)
    if args.list:
        for tid, name, _fn, xfail, needs in suite.tests:
            print(f'{tid:5} {name:28} needs={",".join(sorted(needs)):12} '
                  f'{"xfail " + xfail if xfail else ""}')
        return 0
    if args.selftest:
        return selftest()
    if args.watch:
        return watch_cycle(args.watch)
    if args.flash:
        return flash(args.flash)
    if serial is None or requests is None:
        sys.exit('pyserial and requests are required (pip install pyserial requests)')

    print(f'SIMUT Air suite — target {TARGET_SERIAL}, hand {HAND_SERIAL}')
    try:
        suite.setup()
    except SystemExit:
        raise
    except Exception as exc:
        print(f'setup failed: {exc}')
        return 2
    try:
        suite.run()
    finally:
        suite.teardown()

    r = suite.res
    print(f'\n{r.count("PASS")} passed, {r.count("FAIL")} failed, {r.count("XFAIL")} xfail, '
          f'{r.count("XPASS")} xpass, {r.count("SKIP")} skipped')
    if args.report:
        with open(args.report, 'w', encoding='utf-8') as f:
            json.dump({'when': time.strftime('%Y-%m-%dT%H:%M:%S'), 'args': vars(args),
                       'results': r.to_json(), 'cycles': suite.cycle_data}, f, indent=1)
        print(f'report: {args.report}')
    if r.count('FAIL') and not args.baseline:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
