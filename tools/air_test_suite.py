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
           otherwise timing comes from USB enumeration timestamps. The optional
           CHARGER channel (hand GP3 into target GP17) fakes the charger so T14
           can check that a plugged-in device stops hibernating.

Known defects are marked `xfail=` on the test: a failing xfail test counts as
XFAIL (expected), a passing one as XPASS (remove the mark, the bug is fixed).

Tests that judge history read the SEALED day file plus /api/history/open. The
day file alone is the past with a hole at the near end — everything since the
last seal is still in the RAM encoder — and the hole is exactly where a test
that has just waited looks for its answer (2026-09-09).

Usage:
    python3 tools/air_test_suite.py --list
    python3 tools/air_test_suite.py --selftest            # no hardware
    python3 tools/air_test_suite.py [--host IP] [--only T05,T06] [--cycles 3]
                                    [--hist-interval 1] [--long] [--baseline]
                                    [--report out.json] [--collector-port 8010]
                                    [--t11-window 420]
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
# The last thing setup( ) prints. A cold boot keeps streaming log lines for
# ~15 s after the port enumerates, and a command written into that stream gets
# its reply buried: waiting for this marker is cheaper and surer than guessing
# a settle time. Both spellings, because the AIR marker only exists on Air.
BOOT_DONE_RE = re.compile(r'\[AIR\] boot: done|System ready')
AIR_STATUS_RE = re.compile(
    r'Air:\s*phase=(?P<phase>\d+)\s+wake=(?P<wake>\d+)s\s+hist=(?P<hist>\d+)s'
    r'\s+backoff=(?P<backoff>\d+)s\s+idle=(?P<idle>\d+)s(?:\s+pin=(?P<pin>\S+))?'
    # F25: armed says a reset would bring the cycle back, dirty how close the
    # crash-loop guard is to holding the device in M0. Optional so the suite
    # still parses a firmware from before they existed.
    r'(?:\s+armed=(?P<armed>\d+))?(?:\s+dirty=(?P<dirty>\d+))?'
    # The telemetry trigger: tel=<records pending>/<minimum batch>, skip=<reading
    # wakes still to be served after a failed send>, and whether THIS wake
    # raised the radio at all.
    r'(?:\s+tel=(?P<telnow>\d+)/(?P<televery>\d+))?(?:\s+skip=(?P<skip>\d+))?'
    r'(?:\s+radio=(?P<radio>\d+))?(?:\s+chg=(?P<chg>\d+))?'
    # Automatic cadence: the batch size the AIMD controller settled on and the
    # last full send cycle in ms. Also optional — older builds have neither.
    r'(?:\s+bat=(?P<bat>\d+))?(?:\s+cyc=(?P<cyc>\d+)ms)?'
    # History snapshots written to flash since boot. On an Air every wake is a
    # boot, so this reads as "per wake" — the only window the firmware has into
    # how much it wears the flash.
    r'(?:\s+wip=(?P<wip>\d+))?')
PHASE_RE = re.compile(r'\[AIR\] phase=(?P<name>[A-Z]+) @(?P<ms>\d+)')
ALARM_RE = re.compile(r'\[AIR\] alarm: (?P<h>\d+):(?P<m>\d+):(?P<s>\d+) wakeSec=(?P<sec>\d+)')
# The same line also carries the flash cost of the wake. Matched separately so
# a firmware without the counter still satisfies ALARM_RE.
ALARM_WIP_RE = re.compile(r'\[AIR\] alarm: .*\bwip=(?P<wip>\d+)')
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
    'min_batch': 't_int', 'batch': 't_bat',
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
        self._charger = None

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

    def charger_supported(self):
        """Optional CHARGER extension (GP3 → target GP17). ERR/timeout = not there."""
        if self._charger is None:
            try:
                r = self.cmd('CHARGER STATUS')
            except TestSkip:
                r = ''
            self._charger = r.startswith('CHARGER')
        return self._charger

    def charger(self, on):
        """Drive the target's charger sense line. Returns True on OK."""
        return self.cmd('CHARGER ON' if on else 'CHARGER OFF').startswith('OK')


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

    def alive(self):
        """True when self.ser still points at a device that is there.

        A handle outliving the device it was opened on is the NORMAL case on
        this bench, not an error path: every hibernation detaches the USB
        device and every wake enumerates a new one, so a handle taken before a
        sleep raises EIO on the next write. `if not self.ser` only ever caught
        "never opened" — it says nothing about whether the fd still resolves.

        This went unnoticed while a wake was 25 s long, because the bench
        rarely crossed a sleep between opening and writing. On 2026-09-08 the
        wake dropped to 9 s and four tests of a full run failed as
        `serial write failed: (5, 'Input/output error')`, which reads exactly
        like a device that crashed and was not one — the passive `--watch` in
        the same hour showed nine clean transitions.
        """
        if not self.ser:
            return False
        try:
            self.ser.in_waiting          # cheap ioctl; raises once the node is gone
            return True
        except Exception:
            return False

    def reopen(self, timeout=30):
        """Guarantee a usable handle, replacing a stale one."""
        if self.alive():
            return
        self.close()
        if not self.usb.present() and self.usb.wait(True, timeout) is None:
            raise TestFail(f'target absent from USB for {timeout}s — cannot reopen')
        self.open(timeout=timeout)

    def _drain_until_quiet(self, quiet_s=0.25, cap_s=4.0):
        """Swallow whatever the device is still saying, then hand back a clean line.

        reset_input_buffer( ) drops what has ARRIVED; it does nothing about the
        bytes still in flight. Right after a boot that is most of the banner,
        and cmd( ) returns on the first prompt it sees — which is the banner's
        own prompt, not the reply to the line just written. The caller then gets
        the banner as the answer: measured on 2026-09-08 as
        `air status unparsable: "-beta\n Digite \'help\'..."`, on a device that
        was answering perfectly well.

        Waiting for a quiet line before writing makes the next prompt
        unambiguously ours. The cap keeps a chatty device (debug on) from
        blocking here forever.
        """
        self.ser.reset_input_buffer()
        deadline = time.time() + cap_s
        last_byte = time.time()
        while time.time() < deadline:
            n = self.ser.in_waiting
            if n:
                self.ser.read(n)
                last_byte = time.time()
            elif time.time() - last_byte >= quiet_s:
                return
            else:
                time.sleep(0.02)

    def read_until(self, pattern, timeout, collect=None):
        """Read until `pattern` (compiled regex) matches; return the match, or None.

        Reads in chunks, not by line. The device's last line before sleep — the
        `[AIR] alarm` line — is printed right before the D+ pull-up is released,
        and the disconnect event races that already-delivered line to the
        reading process: measured 2026-09-10, a 10 ms readline reader caught it
        on one wake and missed it on the next, same firmware, which already
        flushes and waits 100 ms. readline() also throws its own partial buffer
        away when it raises. Chunked reads take whatever the OS delivered right
        up to the disconnect, and on the exception we drain the buffer once more
        before giving up. The alarm line is bench-only diagnostics, so closing
        this race belongs in the reader, not in a firmware delay that would cost
        battery on every wake.
        """
        acc, deadline, gone = b'', time.time() + timeout, False
        while time.time() < deadline and not gone:
            try:
                chunk = self.ser.read(256)
            except Exception:
                gone = True                 # port vanished — drain what we have
                chunk = b''
            if chunk:
                acc += chunk
            elif not gone:
                continue                    # read timeout, nothing yet
            while b'\n' in acc:
                raw, acc = acc.split(b'\n', 1)
                line = raw.decode('utf-8', 'replace')
                if collect is not None:
                    collect.append(line.rstrip())
                m = pattern.search(line)
                if m:
                    return m
        # A disconnect can cut the final line before its newline; scan it too.
        if acc:
            line = acc.decode('utf-8', 'replace')
            if collect is not None:
                collect.append(line.rstrip())
            m = pattern.search(line)
            if m:
                return m
        return None

    def cmd(self, text, timeout=6.0):
        """Send one line, return the transcript up to the next prompt."""
        if not self.alive():
            self.reopen()
        try:
            self._drain_until_quiet()
            self.ser.write((text + '\r\n').encode())
        except Exception:
            # The device slept between the check above and this write, or the
            # node was replaced under us. One reopen, one retry: past that the
            # target really is gone and the caller should hear about it.
            try:
                self.reopen()
                self._drain_until_quiet()
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

    def air_status(self, retry_s=12):
        """Ask the device where it is.

        The port enumerates about ten seconds before the console answers, so a
        single ask right after any boot parses boot chatter and fails on a
        device that is perfectly healthy. The default retry covers that lag for
        every caller; `retry_s` goes higher for callers that catch a device at
        the very start of an M1 wake, where the whole window is mostly boot.
        Polling until it parses is the same trick ensure_m0 uses, and for the
        same reason.

        Measured on 2026-09-07: with no default, one reset in T02 left T03 and
        T04 reading boot chatter and reporting their own xfail defects for a
        reason that had nothing to do with them.
        """
        deadline = time.time() + retry_s
        while True:
            out = self.cmd('air status', timeout=9.0)
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
        self._cred = None       # set by login(), so a dead session can be revived
        self._relogin = False   # re-entry guard: login() itself must not recurse

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
                return self._authed(self.s.get, path, **kw)
            except Exception as exc:      # connection reset, chunked truncation
                last = exc
                time.sleep(1.5 * (attempt + 1))
        raise TestFail(f'GET {path} failed after 3 tries: {type(last).__name__}: {last}')

    def post(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self._authed(self.s.post, path, **kw)

    def _authed(self, fn, path, **kw):
        """One request, logging back in if the session died under us.

        The device keeps a single session and loses it on every reboot, and a
        test that resets the target (T02, T12) or reboots it through
        /api/commit_all kills the login for everything that follows. A stale
        cookie answers **403, not 401**, so the whole rest of a run reads as a
        permission bug against a perfectly healthy device — measured on
        2026-09-07, where one hand RESET in T02 turned T05, T06, T06b and T07
        into "/api/config HTTP 403".

        Re-login is safe to do blind: it is a GET plus a POST to /api/login,
        neither of which changes device state. Retrying the original request is
        safe even when it is a POST — the rule above says POSTs are never
        retried because /api/commit_all reboots, but a 403 means the handler
        was refused before it ran, so there is nothing to do twice. Only a
        403 is retried; any other status is returned untouched.
        """
        r = fn(self.base + path, timeout=self.timeout, **kw)
        if r.status_code != 403 or not self._cred or self._relogin:
            return r
        try:
            self.login(*self._cred)
        except TestFail:
            return r        # really unauthorized, or the device is not up: report the 403
        return fn(self.base + path, timeout=self.timeout, **kw)

    def login(self, user, password):
        self._relogin = True
        try:
            r = self.get('/api/login_init')
            if r.status_code != 200:
                raise TestFail(f'login_init HTTP {r.status_code}')
            nonce = r.json().get('nonce', '')
            r = self.post('/api/login', data={'user': user, 'pass': sha256_frontend(password),
                                              'nonce': nonce})
            if 'SIMUTSESS' not in self.s.cookies.get_dict():
                raise TestFail(f'login failed HTTP {r.status_code}: {r.text[:80]}')
        finally:
            self._relogin = False
        self._cred = (user, password)

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

    def open_block(self):
        """The block still open in RAM, as a V5 stream. b'' when there is none.

        The day file only ever holds SEALED blocks. Whatever the device has
        measured since the last seal lives in the encoder (mirrored to
        /history/.wip as a crash bound) and appears in <day>.h5 no earlier than
        the seal that files it. Reading only the day file therefore reads the
        past with a hole at the near end, and the hole is exactly the part a
        test just spent its time producing: on 2026-09-09 a T11 re-run after a
        deliberate 12-minute quiet window came back with byte-identical numbers
        to the run before it, because all fifteen records that window produced
        were in the open block and none of them were in the file.

        204 is the normal answer in the minute after a seal, not an error.
        """
        r = self.get('/api/history/open')
        if r.status_code == 204:
            return b''
        if r.status_code != 200:
            raise TestFail(f'/api/history/open HTTP {r.status_code}')
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


def records_outside_awake(epochs, spans, skew=0.0, grace=0.0):
    """Records timestamped while the device was not observably awake.

    `epochs` come from the device's clock, `spans` are (t_on, t_off) pairs on
    the host's, and `skew` is device minus host. This is the 2026-09-06 failure
    stated as something measurable: a device that is asleep is not measuring,
    so a record bearing a mid-sleep timestamp was filed at a time it was not
    taken. Counting records or checking their spacing cannot see it — a burst
    backdated at exactly the nominal interval has the right count and the right
    gaps — which is why the awake windows have to come from somewhere other
    than the history file.

    `grace` widens both edges: USB enumerates a moment after a boot that has
    already begun sampling, and the port goes away a moment before the device
    really is down.
    """
    out = []
    for e in epochs:
        h = e - skew
        if not any(a - grace <= h <= b + grace for a, b in spans):
            out.append(e)
    return out


def awake_margins(epochs, spans, skew=0.0):
    """Seconds each record sits outside the nearest awake span (0 when inside).

    The number that tells whether `grace` is honest. Set it from what this
    reports on a healthy device — the lag between a boot that has already begun
    sampling and the USB node appearing — rather than from a fraction of the
    interval, which on a 60 s cycle would widen every span until they meet and
    the check could never fire.
    """
    if not spans:
        return []
    out = []
    for e in epochs:
        h = e - skew
        out.append(min(0.0 if a <= h <= b else min(abs(h - a), abs(h - b))
                       for a, b in spans))
    return out


def interior_gaps_ok(epochs, expected_s, tol_s):
    """True when EVERY gap between the given records is within expected±tol.

    Replaces an earlier spacing_ok(epochs, ..., last_n) that judged the LAST
    n gaps of whatever it was handed. That is the wrong end for F04: by the
    time T08 can read the history the device is back online and writing at an
    undisturbed cadence, so "the last three gaps" were three gaps the test did
    not produce, and they passed every time. The caller now selects the records
    it caused and every gap between them is judged — no end to pick, and
    nothing to get backwards.
    """
    if len(epochs) < 2:
        return False, 'not enough records', []
    gaps = [b - a for a, b in zip(epochs, epochs[1:])]
    bad = [g for g in gaps if abs(g - expected_s) > tol_s]
    return (not bad), f'gaps={gaps}', gaps


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
            ('T04', 'air_idle_bounds', self.t04_air_idle_bounds, None, {'target'}),
            ('T05', 'hibernate_cycles', self.t05_hibernate_cycles, None, {'target'}),
            ('T06', 'telemetry_drain', self.t06_telemetry_drain, None, {'target', 'web'}),
            ('T06b', 'telemetry_off_sleeps', self.t06b_telemetry_off_sleeps, None, {'target', 'web'}),
            ('T07', 'web_activity_resets_idle', self.t07_web_activity_resets_idle, None, {'target', 'web'}),
            # xfail F04 removed 2026-09-09: it does not reproduce, measured twice
            # on the records this test actually produces (see the docstring for
            # why the earlier verdicts were not about those records).
            ('T08', 'offline_timestamps', self.t08_offline_timestamps, None, {'target', 'web'}),
            ('T09', 'probe_cycle', self.t09_probe_cycle, None, {'target', 'hand'}),
            ('T10', 'm1_services_off', self.t10_m1_services_off, None, {'target', 'web'}),
            ('T11', 'history_integrity', self.t11_history_integrity, None, {'target', 'web'}),
            ('T12', 'cycle_survives_reset', self.t12_cycle_survives_reset, None, {'target', 'hand'}),
            ('T13', 'two_schedules', self.t13_two_schedules, None, {'target'}),
            ('T14', 'charger_holds_awake', self.t14_charger_holds_awake, None, {'target', 'hand'}),
            ('T15', 'wip_writes_per_cycle', self.t15_wip_writes_per_cycle, None, {'target'}),
            ('T16', 'wake_writes_no_preamble', self.t16_wake_writes_no_preamble, None, {'target'}),
            ('T17', 'admin_reset_persists', self.t17_admin_reset_persists, None, {'target', 'web'}),
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
            # Retried, because one attempt is not a verdict about the web. The
            # device can go back to sleep between ensure_m0( ) and this login,
            # and a single 'Connection reset by peer' then silently downgrades
            # the whole run: the tests do not fail, they SKIP, and the summary
            # comes back green with the web-dependent half never executed.
            # Measured 2026-09-09, when it cost T08 a 20-minute bench window.
            last = None
            for attempt in range(4):
                try:
                    self.web = Web(self.host)
                    self.web.login(user, pw)
                    last = None
                    break
                except Exception as exc:
                    last = exc
                    self.web = None
                    if attempt < 3:
                        # Bring it back to M0 so the next try meets a wake
                        # rather than the same sleep. Guarded: failing to reach
                        # M0 here means the web tests skip, which is what was
                        # about to happen anyway — it must not take the run's
                        # target-only tests down with it.
                        try:
                            self.ensure_m0()
                        except Exception:
                            pass
            if last is not None:
                print(f'  web unavailable after 4 tries ({last}) — '
                      f'web-dependent tests will be skipped')
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

        Chasing the window is a race, and on 2026-09-08 the wake went from 25 s
        to 9 s and the race started losing: opening the port and reading one
        `air status` no longer fits inside a window, so seven tests of a full
        run died as `serial write failed: Input/output error` — the port going
        away mid-command, which reads like a broken device and is not one.

        The hand is the way in that does not race. RESET drives RUN, which is a
        CLEAN boot, and since 2026-09-06 a clean boot means "somebody is
        standing there" and keeps the whole `air idle` in M0. So: chase the
        window for a while, and if that does not land, reset and take the long
        window. Only if there is no hand does this stay a pure race.
        """
        deadline = time.time() + timeout
        chase_until = time.time() + min(timeout, 110)
        reset_used = False
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
                # The docstring's own rule, which the code did not follow: the
                # port going away mid-command is the device sleeping, not a
                # failure. It raises out of cmd(), and unhandled it aborted the
                # whole run with "setup failed: serial write failed" — measured
                # on 2026-09-07 against a device that was merely cycling.
                try:
                    out = self.target.cmd('air status', 6)
                except TestFail as exc:
                    last = str(exc)
                    self.target.close()
                    break                       # slept mid-command: wait for the next wake
                st = parse_air_status(out)
                if st:
                    break
                last = out.strip()[-80:]
                if not self.target.usb.present():
                    break                       # slept again: fall out and retry
            if not st:
                continue
            try:
                if st['phase'] != 0:
                    self.target.cmd('air stop', 5)
                    st = parse_air_status(self.target.cmd('air status', 6))
            except TestFail as exc:
                last = str(exc)
                self.target.close()
                continue
            if st and st['phase'] == 0:
                return st
            last = f'phase={st["phase"] if st else "?"} after air stop'
            if (not reset_used and time.time() > chase_until
                    and self.hand.available and self.hand.ping()):
                # Stop racing. A clean boot hands back a 300 s window instead of
                # a 7 s one, and every test after this one gets to run.
                print('    ensure_m0: window too short to catch — hand RESET for a clean boot')
                reset_used = True
                self.target.close()
                self.hand.reset()
                if self.target.usb.wait(True, 40) is None:
                    last = 'no USB enumeration 40 s after the recovery reset'
                else:
                    # Do not start talking into the boot: wait for setup( ) to
                    # say it is done. A cold boot brings up WiFi and NTP and
                    # prints through all of it.
                    try:
                        self.target.open(timeout=30)
                        self.target.read_until(BOOT_DONE_RE, 40)
                    except TestFail:
                        pass
                    time.sleep(1.5)
        raise TestFail(f'could not reach M0 within {timeout}s (last: {last!r})')

    def need_web(self):
        if not self.web:
            raise TestSkip('web not available (SIMUT_WEB_USER/PASS, host)')

    def full_history(self, expected):
        """Every record the device holds for today, sealed AND still open.

        The day file is the sealed past; the encoder holds everything since the
        last seal. A reader that takes only the file is blind at the near end,
        which is precisely where a test that just waited looks for its answer.
        Returns (epochs, n_sealed, n_open).
        """
        day = time.strftime('%Y%m%d')
        sealed = h5_epochs(self.web.download(f'/history/{day}.h5'), expected)
        raw = self.web.open_block()
        opened = h5_epochs(raw, expected) if raw else []
        # A seal can land between the two GETs, so the open block may repeat
        # what the file already has. Splice on the timeline rather than
        # concatenate: a duplicated record would read as a 0 s gap and be
        # reported as a firmware fault that is really a race in this reader.
        cut = sealed[-1] if sealed else None
        merged = sealed + [e for e in opened if cut is None or e > cut]
        return merged, len(sealed), len(opened)

    def clock_skew(self):
        """device epoch − host epoch, in seconds.

        Record timestamps come from the device's clock and USB presence is
        stamped by the host's. Comparing them without this compares two clocks
        that agree only by luck; the round trip that measures it is worth well
        under a second against a 60 s interval.
        """
        t0 = time.time()
        dev = int(self.web.status()['sys']['time'])
        return dev - (t0 + time.time()) / 2.0

    def watch_awake(self, seconds, poll=0.5):
        """USB presence through a window, as [(t_on, t_off), ...] host clock.

        Nothing is written to the port and the port is never opened: every CLI
        command calls airMarkActivity( ) and rearms the idle timer, so asking
        the device whether it is cycling is what stops it from cycling. That is
        the T12 lesson, and it applies with more force here because this window
        is minutes long. os.path.exists on the by-id node is the whole
        instrument.

        Returns (spans, head_cut, tail_cut). The two flags say whether the
        first and last spans were already open when the watch began or ended:
        those wakes are only partly observed, so a record belonging to one of
        them may sit outside the watch entirely, and judging them would fail a
        healthy device for the test's own timing.
        """
        spans = []
        t0 = time.time()
        was = self.target.usb.present()
        head_cut = was
        start = t0 if was else None
        while time.time() - t0 < seconds:
            time.sleep(poll)
            now = self.target.usb.present()
            if now and not was:
                start = time.time()
            elif was and not now:
                spans.append((start if start is not None else t0, time.time()))
                start = None
            was = now
        if was:
            spans.append((start if start is not None else t0, time.time()))
        return spans, head_cut, was

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
        # A device left cycling by the previous test has no web server for most
        # of every minute, so both the snapshot and the commit would hit a
        # closed port. Measured on 2026-09-07: T06 failed as "No route to host"
        # on /api/commit_all right after T05 handed the bench back mid-cycle.
        # restore_config already did this; the write path needs it just as much.
        self.ensure_m0()
        if self.web.wait_up(120) is None:
            raise TestFail('web did not come up before commit_all')
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

    def point_telemetry_here(self, min_batch, batch=50):
        self.need_web()
        me = host_ip_toward(self.host)
        f = {TEL_FIELDS['server']: me, TEL_FIELDS['port']: self.args.collector_port,
             TEL_FIELDS['path']: '/telemetry', TEL_FIELDS['tls']: 0,
             TEL_FIELDS['min_batch']: min_batch, TEL_FIELDS['batch']: batch}
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
        # Not `if not self.target.ser`: see Target.alive( ). This write is the
        # one that must not be lost — everything the test measures comes after
        # it — so the handle is verified rather than assumed.
        self.target.reopen()
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
        for line in lines:                       # last one wins: it is this cycle's
            mw = ALARM_WIP_RE.search(line)
            if mw:
                row['wip'] = int(mw.group('wip'))
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
            # Everything this used to do by hand — open the port, write
            # `air stop`, read the status back — is what ensure_m0( ) does, and
            # ensure_m0( ) also knows the two things this did not: that the
            # console is still printing the boot when the port enumerates, and
            # that a 9 s wake may close before any of it lands, in which case a
            # hand RESET is the way in. Reusing it removed three separate
            # failure shapes this branch produced on 2026-09-08
            # (`air status unparsable`, then `cannot reopen`).
            #
            # The timing this test reports does NOT come from here: sleep_s and
            # wake_sec are measured from USB enumeration, above. This is only
            # about handing the bench back in M0.
            st = self.ensure_m0()
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
            # USB enumerates about ten seconds before the console answers, so
            # asking once catches boot chatter and fails a healthy device. Poll
            # until it parses — the same trick ensure_m0 uses, for the same
            # reason. Measured on 2026-09-07: this failed as "unparsable right
            # after reset" against firmware that was simply still booting.
            try:
                st0 = self.target.air_status(retry_s=40)
            except TestFail:
                raise TestFail('air status still unparsable 40 s after reset — the console never '
                               'came up, which is a boot failure, not a slow boot')
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
        """`air idle` must refuse what its uint16 field cannot hold (F09).

        ⚠️ An out-of-range value is not merely stored wrong: 65536 truncates to
        **zero**, and an idle timeout of zero makes the device hibernate on the
        very next loop pass. Measured on 2026-09-07: this test used to leave the
        bench cycling, and T07 and T11 after it reported "target absent from
        USB" and "no route to host" against a device that was simply asleep.

        So every probe restores a safe value immediately, before any assertion
        can leave the reactor running.
        """
        before = self.target.air_status()['idle']
        seen, slept = {}, []

        def idle_set(v, safe):
            """Try a value; report whether it was accepted and what stuck.

            `safe` says the value cannot disable the timer, so the readback is
            worth taking. For the out-of-range probes it is not: 65536 stores 0
            and the device is already on its way to sleep by the time the reply
            comes back, so asking anything else just loses the port.
            """
            out = self.target.cmd(f'air idle {v}', 4)
            accepted = 'set' in out.lower() and 'error' not in out.lower() and '<' not in out
            back = None
            if accepted and safe:
                back = self.target.air_status()['idle']
            if accepted:
                self.target.cmd(f'air idle {before}', 4)
            return accepted, back

        def probe(v, safe):
            try:
                return idle_set(v, safe)
            except TestFail:
                # The port vanished: the value disabled the idle timer and the
                # device hibernated mid-command. That IS the answer — recover
                # the bench and record it.
                slept.append(v)
                self.ensure_m0()
                self.target.cmd(f'air idle {before}', 4)
                return True, 0

        try:
            for v, safe in ((9, True), (10, True), (65535, True), (65536, False), (86400, False)):
                seen[v] = probe(v, safe)
            if seen[9][0]:
                raise TestFail('air idle 9 accepted (min is 10)')
            if not seen[10][0]:
                raise TestFail('air idle 10 rejected')
            if not seen[65535][0]:
                raise TestFail('air idle 65535 rejected')
            if seen[65535][1] != 65535:
                raise TestFail(f'idle readback {seen[65535][1]} after setting 65535')
            if seen[65536][0]:
                raise TestFail(
                    'air idle 65536 accepted — it truncates to 0 and an idle timeout of zero '
                    'hibernates the device on the next loop pass' +
                    (' (observed: it went to sleep mid-command)' if 65536 in slept else '') +
                    ' (F09)')
            if seen[86400][0]:
                raise TestFail(f'air idle 86400 accepted (stored as 20864, F09)')
        finally:
            self.ensure_m0()
            self.target.cmd(f'air idle {before}', 4)
        return f'bounds 10..65535 enforced, readback exact; idle back to {before}s'

    def t05_hibernate_cycles(self):
        if self.web and self.args.hist_interval:
            self.point_telemetry_here(min_batch=1)
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
        """A wake that is due must actually drain, and fit inside the interval.

        The trigger is a COUNT since config v22: with a minimum batch of 1 the
        very first wake carrying a reading is due, so this also covers what F05
        was about — the first send of a boot used to wait a whole interval,
        which no wake ever lasted, so the radio came up and sent nothing."""
        self.need_web()
        self.point_telemetry_here(min_batch=1)
        t0 = time.time()
        row = self.hibernate_and_observe(stop_on_wake=False)
        aw = self.watch_awake_window(self.args.flush_cap + 60)
        self.ensure_m0()
        got = self.collector.records_since(t0)
        if got < 1:
            raise TestFail('collector received nothing during the wake')
        # The budget is derived from the reading interval, so the wake has to
        # leave room for the sleep that follows it.
        hist_s = (self.args.hist_interval or 1) * 60
        if aw is None or aw >= hist_s:
            raise TestFail(f'awake window {aw}s does not fit a {hist_s}s reading interval; records={got}')
        return f'records={got} awake_s={aw} sleep_s={row["sleep_s"]}'

    def t06b_telemetry_off_sleeps(self):
        self.need_web()
        f = {TEL_FIELDS['min_batch']: 0}
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
        """Offline wakes must be stamped with the real elapsed time (F04).

        WHICH RECORDS THIS JUDGES, AND WHY IT IS SPELLED OUT (2026-09-09)
        ----------------------------------------------------------------
        It used to download <today>.h5 and take the last `wakes` gaps in it.
        That reads only SEALED blocks, and whether the offline wakes are in one
        by the time this looks depends on something the test does not control:
        the NTP correction on the way back seals the block before shifting, so
        the records land in the file — but only if a correction actually
        happened. With no correction the block stays open, the file still ends
        at whatever was sealed BEFORE the test ran, and "the last three gaps"
        are three gaps this test did not produce. A verdict that is right only
        when an unrelated event fires is worse than one that is plainly wrong,
        because it passes often enough to be believed.

        So: mark the newest record before going offline, read sealed AND open
        afterwards (see full_history), take the FIRST `wakes` records past the
        mark — those are the offline ones — and judge every gap between them.
        Fewer records than wakes is reported as such, never quietly padded out
        with older ones. The gap from the mark to the first offline record
        spans the reboot that broke the SSID, so it is printed and not judged.

        The mark survives the clock correction: a resumed block keeps the
        previous session's stamps and shiftHistoryTimeV5 seals rather than
        rewrites them (_h5AdoptedT0), and the provisional clock only ever runs
        forward, so nothing this test produced can land before the mark.
        """
        if not self.args.long:
            raise TestSkip('long test — pass --long')
        self.need_web()
        good = self.target.ssid()
        if not good:
            raise TestFail('could not read the current SSID from show net status')
        hist_min = 2
        self.commit_and_reboot({HIST_FIELD: hist_min})
        expected = hist_min * 60
        # Four wakes, not three: only the gaps BETWEEN offline records carry
        # the F04 signal, and the first gap after the mark spans the reboot
        # that broke the SSID. Three wakes leave two judged gaps; four leaves
        # three, which is the weight this test already had before the boundary
        # gap was excluded from the judgement.
        wakes = 4
        before, _, _ = self.full_history(expected)
        t_mark = before[-1] if before else 0
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
        after, n_sealed, n_open = self.full_history(expected)
        fresh = [e for e in after if e > t_mark]
        where = f'{n_sealed} sealed + {n_open} open, {len(fresh)} newer than the mark'
        if len(fresh) < wakes:
            raise TestFail(
                f'{wakes} offline wakes produced only {len(fresh)} record(s) after the '
                f'mark — not enough to judge the spacing, and a wake that files '
                f'nothing is itself the loss F04 is about ({where})')

        # The FIRST records after the mark are the offline ones; the last ones
        # were written after the SSID came back, at a cadence nothing was
        # interfering with.
        #
        # Offline, exactly one record per wake: the wake interval is the
        # history interval, and the after-boot rule (_histFirstDone) is gated
        # on the RAW clock, which no offline boot ever sets.
        offline = fresh[:wakes]
        ok, gap_detail, gaps = interior_gaps_ok(offline, expected, 25)
        boundary = offline[0] - t_mark
        detail = (f'offline {gap_detail} (expected {expected}s), boundary gap over '
                  f'the reboot={boundary}s, not judged; {where}')
        if not ok:
            bad = [g for g in gaps if abs(g - expected) > 25]
            raise TestFail(f'offline wakes not spaced by the real sleep (F04): '
                           f'{len(bad)} of {len(gaps)} off by more than 25s; {detail}')
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
        """A wake must not start listeners nobody can reach.

        Closed since 2026-09-07: the web server, the Bluetooth CLI, the mDNS
        announcement and the dashboard cache all moved to M0. Port 80 is the
        one an outsider can check, so it stands for the rest — it is also the
        one that used to be open, which is how this was reported."""
        if not self.host:
            raise TestSkip('no host IP')
        state = {}

        def on_wake(row):
            time.sleep(8)            # give the M1 boot time to (not) bind the web port
            state['web80'] = tcp_open(self.host, 80, 2.0)

        row = self.hibernate_and_observe(stop_on_wake=True, on_wake=on_wake)
        if state.get('web80'):
            raise TestFail('port 80 accepts connections during an M1 wake — a listener no one can reach')
        return f'port 80 closed in M1; sleep_s={row["sleep_s"]}'

    def t11_history_integrity(self):
        """The history must say WHEN each sample was taken.

        Two independent failures live here, both measured on 2026-09-06:
          * records that go backwards in time (the file is not monotonic);
          * bursts spaced at a fixed nominal interval covering stretches the
            device spent asleep, so measurements are filed at times they were
            not taken.

        WHY THIS IS NOT THE DAY FILE ANY MORE (2026-09-09)
        -------------------------------------------------
        The first version downloaded <today>.h5, classified every gap in it
        against h_int and demanded that 90% land within ±25%. Two things made
        that unable to reach a verdict about the firmware at all.

        It read the sealed past only. Whatever the device has measured since
        the last seal lives in the RAM encoder, and appears in the day file no
        earlier than the seal that files it. Re-running the test after a
        deliberate 12-minute quiet window returned byte-identical numbers to
        the run before it — 17 records either way — while /api/history/open
        held the fifteen records that window had just produced. The instrument
        could not see the thing it was waiting for.

        And it judged a whole day of somebody else's work. Every reboot the
        suite causes, and every test that moves h_int (T08 sets 2 min), leaves
        gaps in that file which are correct behaviour and which the test then
        counted against the firmware. Worse, the firmware deliberately files a
        record right after a boot without waiting out the interval
        (_histFirstDone, AppManager_Loop.cpp) — added because a reboot
        otherwise cost a whole unmeasured minute — and on Air EVERY wake is a
        boot, so short gaps are structural, not faults.

        So the test now makes its own window and judges only that. It arms the
        cycle, then does not touch the device for several intervals while
        watching USB presence, which says when the device was actually awake
        without talking to it. Then it reads back the complete history and asks
        four things, none of which needs a tolerance invented for the occasion:

          1. the whole history is monotonic — corrupt is corrupt, whoever
             caused it, so this one is judged over everything on the device;
          2. every record in the window carries a timestamp from a moment the
             device was observably awake. This IS the second failure above,
             stated as something measurable: a record filed mid-sleep is a
             measurement that was not taken;
          3. one record per wake. On Air the wake interval IS the history
             interval (AppManager_Air.cpp: "Wake interval = history save
             interval"), so waking without filing is a lost measurement and
             filing without waking is an invented one;
          4. the gaps inside the window match h_int.
        """
        self.need_web()
        # The history lives behind the web server, and the web server belongs to
        # M0 — a device left cycling by the test before this one answers
        # "connection refused" on port 80, which reads like a broken endpoint
        # and is a device that is simply asleep. Measured on 2026-09-08, when
        # the 9 s wake started leaving T10 short of its own M0 handoff.
        self.ensure_m0()
        if self.web.wait_up(120) is None:
            raise TestSkip('web did not come up in M0 — nothing to read the history through')
        cfg = self.web.config()
        hint_min = int(cfg.get('h_int') or 0)
        if hint_min <= 0:
            raise TestSkip('h_int not readable from /api/config')
        expected = hint_min * 60

        eps0, n_sealed, n_open = self.full_history(expected)
        if len(eps0) < 6:
            raise TestSkip(f'only {len(eps0)} records on the device — not enough to judge')

        # ── 1. monotonic, over everything the device holds ──────────────────
        back, _, _, _ = gap_report(eps0, expected)
        if back:
            raise TestFail(f'history is not monotonic — {len(back)} backwards gap(s) '
                           f'(e.g. {back[0]}s) across {len(eps0)} records '
                           f'({n_sealed} sealed + {n_open} open)')

        # ── 2. a window this test owns ──────────────────────────────────────
        # The charger line suppresses hibernation by design (T14). Left on by
        # an earlier test or a stray tool, it would turn this into a
        # measurement of a device that never sleeps.
        if self.hand.available and self.hand.charger_supported():
            self.hand.charger(False)
        skew = self.clock_skew()

        # Prove the cycle is running before starting to measure it: a window
        # spent watching a device that never armed would report "no records"
        # and blame the history for it.
        row = self.hibernate_and_observe(stop_on_wake=False)
        if not row.get('woke'):
            raise TestFail('the cycle did not come back — nothing to measure')

        window = max(4 * expected, int(self.args.t11_window))
        wall0 = time.time()
        spans, head_cut, tail_cut = self.watch_awake(window)
        elapsed = time.time() - wall0

        # Judge whole wakes only. The wake in progress when the watch opened
        # may have filed its record before wall0 — during the cycle armed just
        # above, which this test did not watch — and the wake still running
        # when it closed may file after the last poll. Counting either against
        # the firmware would be marking the test's own timing as a fault.
        full = spans[1 if head_cut else 0: len(spans) - (1 if tail_cut else 0)]

        self.ensure_m0()
        if self.web.wait_up(180) is None:
            raise TestSkip('web did not come back after the window')
        eps1, n_sealed1, n_open1 = self.full_history(expected)

        if not spans:
            raise TestFail(f'the device never appeared on USB in {elapsed:.0f}s — the cycle '
                           f'stopped, so the history proves nothing '
                           f'({len(eps1)} records on the device)')
        if len(full) < 3:
            raise TestSkip(f'only {len(full)} complete wake(s) observed in {elapsed:.0f}s '
                           f'({len(spans)} spans, head_cut={head_cut} tail_cut={tail_cut}) — '
                           f'too few to judge')

        t_lo, t_hi = full[0][0], full[-1][1]
        win = [e for e in eps1 if t_lo <= e - skew <= t_hi]
        summary = (f'{len(win)} records across {len(full)} complete wakes in a '
                   f'{elapsed:.0f}s window, interval={expected}s, skew={skew:+.1f}s '
                   f'({n_sealed1} sealed + {n_open1} open on the device)')
        if not win:
            raise TestFail(f'{len(full)} complete wake(s) and not one record filed; {summary}')

        # ── 3. every record inside an observed awake window ─────────────────
        # Grace on both edges, because USB enumerates a moment after the boot
        # that has already begun sampling, and the port goes away a moment
        # before the device is really down.
        #
        # A quarter of the interval, capped at 10 s. Half an interval was the
        # first choice and it is vacuous here: the device is awake ~9 s of
        # every 60, so widening each span by 30 s on both sides makes the spans
        # meet and no timestamp can ever fall outside one. The margins below
        # say what the lag really is, so the cap can be tightened on evidence
        # instead of moved on taste.
        grace = min(expected / 4.0, 10.0)
        margins = awake_margins(win, full, skew)
        worst = max(margins) if margins else 0.0
        summary += f', worst margin {worst:.1f}s (grace {grace:.0f}s)'
        stray = records_outside_awake(win, full, skew, grace=grace)
        if stray:
            when = ', '.join(time.strftime('%H:%M:%S', time.localtime(e)) for e in stray[:4])
            raise TestFail(f'{len(stray)} record(s) timestamped while the device was '
                           f'asleep ({when}) — filed at times they were not taken; {summary}')

        # ── 4. one record per wake ──────────────────────────────────────────
        # ±1 because the firmware files a record right after a boot without
        # waiting out the interval (_histFirstDone), so a wake can legitimately
        # carry two when the previous one ran long.
        if abs(len(win) - len(full)) > 1:
            raise TestFail(f'{len(win)} records for {len(full)} complete wakes — '
                           f'{"records without a wake to take them" if len(win) > len(full) else "wakes that filed nothing"}'
                           f'; {summary}')

        # ── 5. cadence inside the window ────────────────────────────────────
        wback, ok, short, long_ = gap_report(win, expected)
        gaps = len(win) - 1
        detail = f'on-time={len(ok)} short={len(short)} long={len(long_)}'
        # Check 1 judged the history as it stood BEFORE the window, so a
        # backwards gap written during the window would pass it untouched.
        if wback:
            raise TestFail(f'records written during the window go backwards — '
                           f'{len(wback)} backwards gap(s) (e.g. {wback[0]}s); {summary}')
        if gaps and len(ok) < 0.9 * gaps:
            raise TestFail(f'gaps in an undisturbed window do not match h_int: {detail} '
                           f'(longest={max(long_) if long_ else 0}s, '
                           f'shortest={min(short) if short else 0}s); {summary}')
        return f'{summary}; {detail}'

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
        # A hand RESET drives RUN, which is a CLEAN boot — and since 2026-09-06
        # a clean boot is read as "a person is standing there" and gets the full
        # configured idle timeout, not the short grace (only a watchdog keeps
        # the grace). So the wait to measure is `air idle`, and on a bench idle
        # of minutes that would be minutes of dead time: shorten it for the run.
        # An earlier version of this test budgeted the grace and had been failing
        # against healthy firmware ever since that change landed.
        before = self.target.air_status()['idle']
        idle = 30
        self.target.cmd(f'air idle {idle}', 4)
        try:
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

            # Idle window + the rest of a wake. Generous cap, because the point
            # is "does it return at all", not how fast.
            budget = idle + row['awake_before_sleep_s'] + self.args.wake_grace + 30
            t_gone = self.target.usb.wait(False, budget)
            if t_gone is None:
                raise TestFail(
                    f'still awake {budget:.0f}s after a reset with idle={idle}s — the cycle did '
                    f'not resume (F25). Check `air status` for armed=1; armed=0 means air.bin '
                    f'never recorded the intent')
        finally:
            # Verdict is in; hand the bench back in a known state. Leaving the
            # device cycling makes the NEXT test start against a target that is
            # asleep more often than not, with no web server to talk to.
            self.ensure_m0()
            self.target.cmd(f'air idle {before}', 4)
        return (f'reset -> boot {t_boot - t_reset:.1f}s -> asleep again '
                f'{t_gone - t_boot:.1f}s later with idle={idle}s, no command sent')

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
            raise TestSkip(f'minimum batch is {every} record(s), so a wake is due as soon as it '
                           f'reads — there is no radio-off wake to observe. Set t_int above the '
                           f'number of readings one wake produces to exercise this')

        # Enter the cycle ONCE. Every wake after this one happens on its own, so
        # driving each with `air hibernate` would be fighting the device: sent
        # mid-wake the command adds nothing and the alarm line lands outside the
        # window the helper is watching, which is exactly how an earlier version
        # of this test failed with "serial vanished before the alarm line".
        row = self.hibernate_and_observe(stop_on_wake=False)

        # How many wakes until the radio is due, when the queue starts empty.
        #
        # One wake produces one reading, so reaching a minimum batch of N takes
        # N wakes, and the radio is due on the Nth. It used to take N+2: the
        # decision is taken at BOOT, before this wake's own reading exists, and
        # the count did not include it — measured on 2026-09-07 with min=5,
        # tel= read 0,0,1,2,3,4,5 across seven wakes with the radio up on the
        # seventh. airTelemetryDue( ) now counts the reading this wake is about
        # to take, so t_int means exactly what it says. One wake of margin is
        # kept because the queue is rarely empty at the first wake observed.
        budget = every + 1
        radio_by_wake, awake_by_wake, pending_by_wake = [], [], []
        for _ in range(budget + 2):        # two spare wakes of margin
            t_up = self.target.usb.wait(True, row['wake_sec'] + self.args.wake_grace)
            if t_up is None:
                raise TestFail('no wake while measuring the schedule')
            self.target.open(30)
            # The wake has just enumerated; the console needs a few more seconds.
            st = self.target.air_status(retry_s=25)
            radio_by_wake.append(st.get('radio'))
            pending_by_wake.append(st.get('telnow'))
            self.target.close()
            t_down = self.target.usb.wait(False, 240)
            if t_down is None:
                raise TestFail('device stayed awake instead of going back to sleep')
            # Enumeration lags the boot by about a second, so this reads slightly
            # short — fine, because the verdict is a comparison between wakes
            # measured the same way.
            awake_by_wake.append(t_down - t_up)
            # Both verdicts are in as soon as there is one of each kind; the
            # remaining wakes would only cost a minute each.
            if any(radio_by_wake) and not all(radio_by_wake):
                break

        self.ensure_m0()
        if None in radio_by_wake:
            raise TestSkip('firmware without the radio= field')
        ups = sum(1 for r in radio_by_wake if r)
        if ups == 0:
            raise TestFail(f'radio never came up in {len(radio_by_wake)} wakes — telemetry '
                           f'would never leave the device: radio {radio_by_wake}, pending '
                           f'{pending_by_wake} against a minimum batch of {every}. A pending '
                           f'count that is not climbing is the trigger being broken; one that '
                           f'climbs and never fires is the comparison')
        if ups == len(radio_by_wake):
            raise TestFail(f'radio came up on EVERY wake — the schedule is not being '
                           f'applied: {radio_by_wake}')

        quiet = [a for a, r in zip(awake_by_wake, radio_by_wake) if not r]
        loud = [a for a, r in zip(awake_by_wake, radio_by_wake) if r]
        detail = (f'minimum batch {every}; pending {pending_by_wake}; radio {radio_by_wake}; '
                  f'awake quiet={[round(a, 1) for a in quiet]}s '
                  f'loud={[round(a, 1) for a in loud]}s')
        # A reading-only wake that is not shorter means the network was started
        # anyway somewhere, which is the failure worth catching.
        if quiet and loud and min(loud) <= max(quiet):
            raise TestFail(f'reading-only wakes are not cheaper than telemetry wakes — '
                           f'something still brings the radio up. {detail}')
        return detail

    def t14_charger_holds_awake(self):
        """On the charger the device must not hibernate; off it, it must.

        Both halves are needed. A device that never sleeps passes the first
        half for the wrong reason, so the same run has to show it sleeping
        once the line drops — that is the control.

        The stimulus is the PicoHand driving GP3 into the target's GP17
        (manual §12). Without that wire the line floats and the target's
        pull-down reads "battery" forever, which would make the first half
        fail for a bench reason; so a hand without the CHARGER channel skips
        rather than fails.

        Hands-off like T12: every CLI command rearms the idle timer, so USB
        enumeration is the only honest answer to "is it still awake".

        ⚠️ A browser sitting on the device's dashboard invalidates this test
        completely. Web hits rearm the idle timer too (that is F21 working), so
        both halves report "awake" and the comparison discriminates nothing. On
        2026-09-07 that cost half an hour and looked exactly like a firmware
        bug. `ss -tn | grep <device ip>` names the culprit in one command; the
        clean window is taking the device off the network from its own console.
        """
        if not (self.hand.available and self.hand.ping()):
            raise TestSkip('needs the PicoHand to drive the charger line')
        if not self.hand.charger_supported():
            raise TestSkip('PicoHand without the CHARGER channel — reflash it (manual §12)')

        self.ensure_m0()
        st = self.target.air_status()
        if st.get('chg') is None:
            raise TestSkip('firmware without the chg= field — older than charger sense')

        # The idle timeout is what the charger suppresses, so shorten it: the
        # bench default is minutes and the verdict needs two of them.
        before = st['idle']
        idle, slept_after = 30, None
        self.target.cmd(f'air idle {idle}', 4)
        watch = idle + 45
        try:
            # Half 1: line HIGH, the idle timer must never fire.
            self.hand.charger(True)
            st = self.target.air_status(retry_s=15)
            if not st.get('chg'):
                raise TestFail('CHARGER ON but the device still reads chg=0 — check the '
                               'GP3-to-GP17 wire and the common ground')
            self.target.close()          # no serial traffic: it would rearm the timer
            if self.target.usb.wait(False, watch) is not None:
                raise TestFail(f'device hibernated within {watch}s while charging with '
                               f'idle={idle}s — the charger must suppress the idle timeout '
                               f'entirely')

            # Half 2 (control): drop the line and the SAME device must now sleep.
            self.target.open(30)
            self.hand.charger(False)
            st = self.target.air_status(retry_s=15)
            if st.get('chg'):
                raise TestFail('CHARGER OFF but the device still reads chg=1 — something other '
                               'than the hand is holding the line high')
            self.target.close()
            t0 = time.time()
            t_gone = self.target.usb.wait(False, watch)
            if t_gone is None:
                raise TestFail(f'still awake {watch}s after the charger was removed — the first '
                               f'half proves nothing, because this device never sleeps. Most '
                               f'likely a browser is polling the dashboard: check with '
                               f'`ss -tn | grep {self.host or "<device ip>"}`')
            slept_after = t_gone - t0
        finally:
            self.hand.charger(False)
            self.ensure_m0()
            self.target.cmd(f'air idle {before}', 4)
        return f'awake through {watch}s on charger; slept {slept_after:.0f}s after removal'

    def t15_wip_writes_per_cycle(self):
        """One cycle must snapshot the open history block ONCE.

        The .wip is rewritten WHOLE every time, and on a device reading once a
        minute that rewrite is the dominant flash cost there is. Three callers
        used to ask for it unconditionally — the pre-reboot hook,
        airStartHibernate( ) and the DECIDE phase — right after the record
        write had already done it. Measured on 2026-09-07: 3 to 4 whole-block
        writes per M0 -> M1 cycle, now 1.

        The count is read from the `[AIR] alarm:` line rather than `air status`
        because the console answers EARLY in a wake: three wakes polled through
        their whole window reported wip=0 from inside SAMPLE, before the record
        was even written. The alarm line is the one thing every wake prints
        last.
        """
        st = self.ensure_m0()
        if st.get('wip') is None:
            raise TestSkip('firmware without the wip= counter')
        before = self.target.air_status()['wip']
        time.sleep(5)
        settled = self.target.air_status()['wip']
        row = self.hibernate_and_observe(stop_on_wake=True)
        after = row.get('wip')
        if after is None:
            raise TestSkip('alarm line without wip= — firmware older than the counter')
        n = after - settled
        # Zero would mean the snapshot stopped happening, which loses the open
        # block on the next power cut. That failure looks identical to the fix
        # working if only the upper bound is checked.
        if n < 1:
            raise TestFail(f'no snapshot written in a whole cycle (wip {settled} -> {after}) — '
                           f'the open block would be lost on a power cut')
        # Two is the ceiling, not one, and the second write is legitimate: since
        # the boot resumes the open block (F23) rather than sealing it, the
        # snapshot on flash carries the provenance of the session that wrote it.
        # A wake stamps its record BEFORE it reaches NTP, so the block on flash
        # says "provisional"; when the clock is then confirmed, the next
        # unconditional flush rewrites it to say "synced" — and that flag is
        # what the next boot's seed gate reads. Suppressing it would leave flash
        # claiming a provenance it does not have.
        #
        # It costs one extra write per resumed block on the wakes that reach
        # NTP, which on a device with a minimum batch above one is a minority
        # of them. Measured 2026-09-07: 3 and 4 writes per cycle before any of
        # this, 1 with the redundant callers gone, 2 once the boot started
        # resuming. Three or more means an unconditional caller is rewriting
        # bytes that have not changed at all.
        if n > 2:
            raise TestFail(f'{n} whole-block snapshots in one cycle (wip {settled} -> {after}) — '
                           f'at most one write plus one provenance upgrade is expected; a caller '
                           f'is rewriting what is already on flash')
        return (f'{n} snapshot(s) per cycle (wip {before} -> {settled} -> {after})'
                + ('; the second is the clock-provenance upgrade' if n == 2 else ''))

    # The fixed sequence setup( ) emits on its way up, in the order the rig
    # printed it. Measured 2026-09-07 over 108 boots: eight codes, 68.2% of the
    # whole forensic window, identical every time.
    BOOT_PREAMBLE_CODES = (524, 441, 590, 407, 549, 540, 567, 404)

    def count_preamble_records(self):
        out = self.target.cmd('show system log', 60)
        n = 0
        for line in out.splitlines():
            m = re.search(r'code=(\d+)', line)
            if m and int(m.group(1)) in self.BOOT_PREAMBLE_CODES:
                n += 1
        return n

    def t16_wake_writes_no_preamble(self):
        """A wake is a boot that already happened — the log must not say so again.

        Every M1 wake runs the whole setup( ), so the eight init records above
        were rewritten once a minute and left about 79 minutes of forensic
        window. A boot that came out of hibernation now skips them; a boot that
        did not — a power interruption, a reset — still writes all eight,
        because there the burst is the record of what happened.

        ONE is the expected result, not zero, and the one is deliberate:
        STO_H5_WIP is emitted a second time by the cycle's own snapshot after
        setup( ) ends, and lands as the history family's first transition. That
        record carries the block's count in ctx and is the cycle proving it did
        the work it woke up for. Zero would also pass — the ceiling is what this
        test is for — and T15 is what guards against the snapshot disappearing.

        `air stop` does not re-run setup( ), so the M0 the device is left in
        contributes nothing to the count.
        """
        self.ensure_m0()
        before = self.count_preamble_records()
        self.hibernate_and_observe(stop_on_wake=True)
        after = self.count_preamble_records()
        delta = after - before
        if delta < 0:
            # /api/logs and `show system log` stitch the rotated file with the
            # current one, so a rotation inside the window drops ~800 records at
            # once and the delta goes deeply negative. That is not a verdict.
            raise TestSkip(f'the log rotated during the cycle ({before} -> {after}); '
                           f'only deltas mean anything here, so this run has none')
        if delta > 1:
            raise TestFail(f'{delta} preamble records written by a single wake '
                           f'({before} -> {after}) — the wake is rewriting the boot '
                           f'sequence it inherited; expected at most the cycle\'s own '
                           f'history snapshot')
        return (f'{delta} preamble record(s) for a whole wake, was 8'
                + ('; the one is the cycle\'s history snapshot' if delta == 1 else ''))

    def t17_admin_reset_persists(self):
        """A password reset announced on the console must survive the next boot.

        `system admin reset confirm` is the documented recovery for a web nobody
        can log into, and the 2026-09-07 audit made it USB-only for that reason
        (V-01a, part B). What it did was print a one-time password and rewrite
        the hash in RAM — and on the emergency console that was the whole story:
        `changed = true` there only prints "applies to this session", and nobody
        called saveConfiguration( ). `system ssid` and `system pass`, two cases
        up the same switch, had always saved for themselves.

        On SIMUT Air every wake is a boot, so the printed password expired about
        a minute after it was read and the one recovery for a locked-out web
        recovered nothing. Measured on the rig 2026-09-08, both directions:
        login with the printed password succeeded inside the boot that printed
        it and answered 401 err=2 after `reload confirm`.

        The rotation is put back before returning, so a failure leaves the bench
        usable either way: a reset that did NOT persist has already restored the
        old password by failing, and one that did is changed back over the web.
        """
        self.need_web()
        user = os.environ['SIMUT_WEB_USER']
        known = os.environ['SIMUT_WEB_PASS']
        out = self.target.cmd('system admin reset confirm', 12)
        # The alphabet is deliberately O/0/I/1-free (generateInitialAdminPassword),
        # so the password is the only all-caps-and-digits 8-run on its own line.
        m = re.search(r'^\s*([A-Z2-9]{8})\s*$', out, re.M)
        if not m:
            raise TestFail(f'no one-time password on the console: {out.strip()[:160]!r}')
        otp = m.group(1)

        self.target.close()
        try:
            self.target.cmd('reload confirm', 6)
        except TestFail:
            pass                      # the port dropping IS the reboot
        self.target.close()
        time.sleep(3)
        self.target.usb.wait(False, 20)
        self.target.open(timeout=120)
        self.ensure_m0()
        if self.web.wait_up(120) is None:
            raise TestFail('web did not come back after the reboot')

        fresh = Web(self.host)
        try:
            fresh.login(user, otp)
        except TestFail as exc:
            # The old password answering again is the proof, not a side effect:
            # confirm it so a genuinely dead login is not read as this defect.
            back = 'no'
            try:
                Web(self.host).login(user, known)
                back = 'yes'
            except TestFail:
                pass
            if back == 'yes':
                self.web.login(user, known)
            raise TestFail(f'the reset did not survive the reboot ({exc}); '
                           f'previous password works again: {back}. The console '
                           f'printed a credential the device forgot on the next '
                           f'boot — on Air that is one wake.')

        # It persisted. Put the bench password back over the web, which is the
        # path that has always saved.
        r = fresh.get('/api/login_init')
        nonce = r.json().get('nonce', '') if r.status_code == 200 else ''
        r = fresh.post('/api/login_chpass',
                       data={'user': user, 'oldpass': sha256_frontend(otp),
                             'newpass': sha256_frontend(known), 'nonce': nonce})
        if r.status_code != 200:
            raise TestFail(f'reset persisted (good) but the bench password could NOT be '
                           f'put back: login_chpass HTTP {r.status_code} {r.text[:80]}. '
                           f'The device is on the one-time password {otp} — set it back '
                           f'by hand before the next run.')
        self.web.login(user, known)
        return f'the console reset survived a reboot; bench password restored'

    # ---- runner -----------------------------------------------------------

    def run(self):
        selected = self.selected()
        # Every test that talks to the device assumes M0: a live console, a web
        # server, and no wake window closing under it. A run that starts against
        # a cycling target instead fails its early tests with "serial write
        # failed" and "no route to host", which say nothing about the code under
        # test. Measured on 2026-09-07, twice. Establishing the precondition is
        # not reconfiguration — nothing here changes what the device is set to.
        if any('target' in needs for *_, needs in selected):
            try:
                self.ensure_m0()
            except TestFail as exc:
                print(f'  could not reach M0 before starting: {exc}', flush=True)
        for tid, name, fn, xfail, _needs in selected:
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
            # Put the device's configuration back before the NEXT test reads it.
            # Restoring only at teardown means a test that reconfigures hands
            # its settings to everything after it: measured on 2026-09-07, T06
            # left the minimum batch at 1 and T13 skipped itself with "a wake is
            # due as soon as it reads" — describing T06's configuration, not the
            # bench's. Cheap, because only a test that actually committed
            # something has anything to undo.
            if self.saved:
                try:
                    self.restore_config()
                except Exception as exc:
                    print(f'  could not restore config after {tid}: {exc}', flush=True)


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

    # The oldest line the parser must still read: every field added since is
    # optional and comes back None, so this case is also the regression guard
    # for adding another one — compare the fields present, not the whole dict,
    # or the check becomes a list of names nobody remembers to extend.
    st = parse_air_status('Air: phase=0 wake=300s hist=300s backoff=0s idle=300s')
    check('air status parse', st is not None and
          {k: v for k, v in st.items() if v is not None} ==
          {'phase': 0, 'wake': 300, 'hist': 300, 'backoff': 0, 'idle': 300})
    st2 = parse_air_status('Air: phase=2 wake=900s hist=60s backoff=900s idle=60s pin=16')
    check('air status with pin', st2 and st2['pin'] == '16' and st2['wake'] == max(st2['hist'], st2['backoff']))
    # A line from the current firmware, verbatim off the bench on 2026-09-07.
    st3 = parse_air_status(
        'Air: phase=0 wake=60s hist=60s backoff=0s idle=300s armed=0 dirty=0 '
        'tel=9/5 skip=0 radio=1 chg=0 bat=100 cyc=3871ms')
    check('air status full line', st3 is not None and st3['chg'] == 0 and st3['bat'] == 100
          and st3['cyc'] == 3871 and st3['telnow'] == 9 and st3['televery'] == 5)
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
    ok1, _, _ = interior_gaps_ok([0, 120, 241, 358], 120, 25)
    # The F04 signature: every offline wake advances the history ~80 s where
    # the real sleep was 120 s.
    ok2, _, _ = interior_gaps_ok([0, 80, 160, 240], 120, 25)
    check('interior gaps: clean passes, F04 compression fails', ok1 and not ok2)
    # The regression guard for the bug this replaced: a run whose EARLY gaps
    # are wrong and whose LAST ones are clean — offline records followed by
    # the online ones written after the SSID came back. spacing_ok judged the
    # tail and passed this; every gap is judged now, so it fails.
    mixed = [0, 80, 160, 240, 360, 480, 600]
    ok3, _, _ = interior_gaps_ok(mixed, 120, 25)
    check('a bad head behind a clean tail is not excused', not ok3)
    # the real 2026-09-06 shape: three 60 s records, one backwards, one huge jump
    back, on_time, short, long_ = gap_report([0, 60, 120, 103, 1920], 120)
    check('gap_report finds the backwards gap', len(back) == 1 and back[0] == -17)
    check('gap_report classifies short/long', len(short) == 2 and len(long_) == 1 and not on_time)
    b2, ok2b, s2, l2 = gap_report([0, 120, 240, 360], 120)
    check('gap_report clean file', not b2 and len(ok2b) == 3 and not s2 and not l2)

    # The check T11 leans on, and the one that has to be shown to fail: a
    # burst backdated across a sleep has a plausible count and textbook gaps,
    # so nothing in gap_report can see it. Awake windows can.
    #
    # A device cycling every 60 s, awake ~15 s of each: five wakes, five
    # records, each filed while the port was there.
    spans5 = [(1000.0 + 60 * i, 1015.0 + 60 * i) for i in range(5)]
    honest = [1005 + 60 * i for i in range(5)]
    check('awake windows accept honest records',
          not records_outside_awake(honest, spans5, skew=0.0, grace=30.0))
    # Now the failure itself: the device sleeps five minutes, wakes twice in
    # all, and files six records spaced at exactly the nominal 60 s so that
    # they cover the sleep. Six records for two wakes, and every gap textbook.
    slept = [(1000.0, 1015.0), (1300.0, 1315.0)]
    faked = [1005, 1065, 1125, 1185, 1245, 1305]
    bf, ok_f, sf, lf = gap_report(faked, 60)
    check('gap spacing cannot see a burst at the nominal interval',
          not bf and not sf and not lf and len(ok_f) == len(faked) - 1)
    # The awake windows can: four of the six were filed mid-sleep.
    check('awake windows catch a burst at the nominal interval',
          len(records_outside_awake(faked, slept, skew=0.0, grace=5.0)) == 4)
    # And the same data survives the half-interval grace T11 actually uses,
    # so the grace is not what makes the check pass.
    check('the grace T11 uses does not hide the burst',
          len(records_outside_awake(faked, slept, skew=0.0, grace=30.0)) == 4)
    # Skew is applied, not ignored: a device 40 s ahead of the host still
    # lands inside the same windows once the offset is taken out.
    # The grace is a measurement, not a preference, so the thing that measures
    # it is checked too: zero for a record inside a span, and the real distance
    # for one outside.
    check('awake margins are zero inside a span',
          awake_margins(honest, spans5) == [0.0] * len(honest))
    check('awake margins measure the distance outside',
          awake_margins([1035], spans5) == [20.0])
    check('awake windows apply the clock skew',
          not records_outside_awake([e + 40 for e in honest], spans5, skew=40.0, grace=30.0)
          and len(records_outside_awake([e + 40 for e in honest], spans5,
                                        skew=0.0, grace=5.0)) == 5)
    # A neutral vector: this only checks that the frontend hashes latin-1 bytes
    # the way the device does, so any ASCII string proves it. It used to be the
    # rig's real password, which is how a live credential ended up in a tracked
    # file that no pattern in the secret gate was looking at (V-02, 2026-09-07).
    check('sha256 latin-1', sha256_frontend('Str1ngDeTeste') == hashlib.sha256(b'Str1ngDeTeste').hexdigest())
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
    ap.add_argument('--t11-window', type=int, default=420,
                    help='seconds T11 leaves the device alone before judging (default 420)')
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
