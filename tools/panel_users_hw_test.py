#!/usr/bin/env python3
"""v24 — identity at the panel, exercised on the rig and photographed.

Drives the panel with `touch sim` over the serial CLI, captures every screen
with GET /api/screenshot, listens for the alarm line on a local collector and
reads the binary log back — so that each claim in the report (who did what,
what the server received, what the log kept) is a file in --out, not a
sentence. Flash `pico_w_test` first: `touch sim` and `screen` live in the full
CLI only.

    source ~/.simut-bench.env
    python3 tools/alarm_collector.py --port 18081 --log /tmp/alarms.jsonl &
    python3 tools/panel_users_hw_test.py --out /tmp/panel_shots --collector-log /tmp/alarms.jsonl [--step ...]

Steps (default: all, in this order): prep, joao, admin, maria, web, cleanup.
`prep` points the alarm line at this host's collector and creates the test
accounts over the CLI; `cleanup` deletes them and restores the telemetry
fields it changed. Between the two the device's config is saved by the panel
actions themselves, which is why cleanup restores and saves too.

Coordinates are the firmware's (DisplayManager_Users.cpp, PinPad namespace
and the 4-row list geometry every menu shares) — none is guessed.
"""
import argparse
import glob
import http.server
import json
import os
import re
import socket
import sys
import threading
import time
from io import BytesIO

import requests
import serial
from PIL import Image

TARGET_GLOB = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00'
BAUD = 115200
COLLECTOR_PORT = int(os.environ.get('SIMUT_ALARM_COLLECTOR_PORT', '18081'))
COLLECTOR_LOG = os.environ.get('SIMUT_ALARM_COLLECTOR_LOG', '/tmp/simut_alarm_collector.jsonl')

# ── geometry, read out of the firmware ─────────────────────────────────────
ROW_Y = [57, 95, 133, 171]                 # 4-row lists: 40 + i*38, centred
FOOT = {'up': (39, 215), 'down': (105, 215), 'exit': (170, 215), 'enter': (250, 215)}
ALARMS_EXIT = (228, 215)                   # the alarms list's wide BACK
CFG_BTN = (286, 215)                       # dashboard footer, 5th slot
# PIN keypad (src/PinKeypad.h): four cards of three slots, the ten digits and
# two decoy symbols dealt over them at random. The script cannot guess where a
# digit is — `show display keypad` is what tells it. One tap per digit.
# v25: the card layout is a POLICY setting (PinKeypad.h::GRIDS) — four cards of
# three glyphs is only one of five, and ALPHA_ALNUM puts twelve or eighteen on
# the glass. These are the v24 numbers and they survive as the fallback for a
# device that predates the geometry in /api/keypad; Rig.keypad_faces( ) reads
# the live grid and Rig.pin( ) taps what it says. Hardcoding them was the trap
# this file was about to walk into: the cards would have moved out from under
# every tap with nothing failing in a way that pointed here.
PIN_GRID_FALLBACK = dict(keys=4, slots=3, cols=2, rows=2,
                         x=6, y=72, w=152, h=44, px=156, py=50)
PIN_BACK = (70, 215)                       # backspace, FOOT_BACK_X + W/2
PIN_CANCEL = (178, 215)                    # TR_BACK, FOOT_EXIT_X + W/2
PIN_OK = (268, 215)                        # TR_ENTER, FOOT_OK_X + W/2
# The ordered pad, for SETTING a PIN (PinKb::NUM_*): fixed positions, so
# pin_exact( ) needs no reading of the deal.
MSG_OK = (160, 205)
MAINT_ROW = [77, 125]                      # bars at 60 and 108, 34 high
MAINT_DEC, MAINT_INC, MAINT_MID = 30, 280, 160
KB_COL = [43, 121, 199, 277]               # groups: 6 + c*78, centred
KB_ROW = [93, 151]                         # 66 + r*58, centred
KB_OK = (277, 47)
KB_GROUPS = ["abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"]
LP_KEY_W, LP_GAP, LP_ROW0_Y, LP_KEY_H = 68, 8, 72, 56
CONFIRM_CANCEL, CONFIRM_DELETE = (85, 215), (235, 215)


def host_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('192.168.3.24', 80))
        return s.getsockname()[0]
    finally:
        s.close()


# ── the alarm-line collector ───────────────────────────────────────────────
class Collector:
    """Reads the JSON lines tools/alarm_collector.py appends. The collector is
    a process of its own so it is listening while the device boots and
    between runs: an in-script listener answered the main line but the alarm
    line's first attempt still failed and its retry landed after the wait."""
    def __init__(self, log_path):
        self.path = log_path
        self.records = []
        self._pos = 0
        self.raw = []
        self._pull()

    def _pull(self):
        if not os.path.exists(self.path):
            return
        with open(self.path) as f:
            f.seek(self._pos)
            chunk = f.read()
            self._pos = f.tell()
        for line in chunk.splitlines():
            if True:
                try:
                    e = json.loads(line)
                except Exception:
                    continue
                self.raw.append((e.get('path'), e.get('body', '')[:200]))
                if not str(e.get('path', '')).endswith('/alarm'):
                    continue
                try:
                    data = json.loads(e['body'])
                    items = data if isinstance(data, list) else data.get('alarms', [])
                    for it in items:
                        it['_rx'] = e['ts']
                        self.records.append(it)
                except Exception:
                    self.records.append({'_raw': e.get('body'), '_rx': e.get('ts')})

    def wait_for(self, pred, timeout=25, nudge=None):
        """The alarm line backs off for 5 s after a tap and retries on its own
        interval after a failure, so a record follows the action by 5-20 s."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            self._pull()
            for r in self.records:
                if pred(r):
                    return r
            time.sleep(0.5)
        return None

    def since(self, t0):
        self._pull()
        return [r for r in self.records if r.get('_rx', 0) >= t0]


# ── the rig: serial CLI + throwaway web admin ──────────────────────────────
class KeypadGone(RuntimeError):
    """`show display keypad` found no keypad. On a device whose log has grown,
    a CLI read can take long enough for the 30 s idle guard to send the panel
    home mid-entry — the caller reopens it and types again."""


class Rig:
    def __init__(self, out):
        self.out = out
        port = next(iter(glob.glob(TARGET_GLOB)), None)
        if not port:
            raise SystemExit('target Pico W not found under /dev/serial/by-id')
        self.ser = serial.Serial(port, BAUD, timeout=0.3)
        self.ser.dtr = True
        time.sleep(0.4)
        self.ser.reset_input_buffer()
        self.cmd('enable')
        self.kb = 'cards'
        self.pop = {'w': 56, 'h': 52, 'gap': 6, 'one_y': 104, 'row0_y': 76, 'row1_y': 132}
        self.ip = self._ip()
        if not self.ip:
            raise SystemExit('device reports no IP')
        self.session = self._login()
        self.shots = []

    def cmd(self, text, quiet_for=0.5, timeout=6.0):
        """⚠️ A device REBOOT reaches this as OSError/SerialException errno 5
        on the USB CDC port, from write( ) or read( ). Letting it escape kills
        the run with a stack trace that says nothing about the device — on
        2026-09-20 a watchdog stall (SYS_BOOT ctx=209, C0=[CLI]) ended a
        full-table run that way, and the reboot had to be reconstructed from
        the uptime afterwards. So it is caught, NAMED, and the port is
        reopened. It is not swallowed: reboot_count goes up and the panel
        session is gone, so whatever the caller was doing is already invalid
        and the run must say so."""
        try:
            self.quiet()
            self.ser.write((text + '\r\n').encode())
            self.ser.flush()
            buf, deadline, last = b'', time.time() + timeout, time.time()
            while time.time() < deadline:
                chunk = self.ser.read(2048)
                if chunk:
                    buf += chunk
                    last = time.time()
                elif buf and time.time() - last >= quiet_for:
                    break
            return buf.decode('utf-8', 'replace')
        except (OSError, serial.SerialException) as e:
            # The port went away. That is USUALLY a reboot, but not always: on
            # 2026-09-20 one of these came back with uptime 00:21:49, i.e. the
            # CDC link hiccuped and the device never reset. So do not ASSERT a
            # reboot — print the uptime and let it say.
            self.serial_drops = getattr(self, 'serial_drops', 0) + 1
            print(f'  [serial] {e} — port lost (#{self.serial_drops}); reopening')
            time.sleep(12.0)
            if not self.reconnect():
                raise
            self._lost_mode = True          # see cfg( )
            up = re.search(r'Uptime:\s*(\S+)',
                           self.cmd('show metrics', quiet_for=1.0, timeout=15))
            up = up.group(1) if up else '?'
            print(f'  [serial] back, uptime {up} — any panel session and CLI mode are gone')
            return ''

    def cfg(self, *cmds):
        """⚠️ A dropped port mid-batch leaves the CLI back at EXEC, and the
        commands after it then run OUTSIDE `configure terminal` and are
        refused — silently, because nobody reads the replies. That is exactly
        how the 2026-09-20 cleanup restored `tel server/port/path` and then
        failed to restore `alarm set path` and `qmax`, leaving the rig pointed
        half at the bench. Re-enter config mode whenever cmd( ) reconnected."""
        self._lost_mode = False
        self.cmd('configure terminal')
        outs = []
        for c in cmds:
            if self._lost_mode:
                self._lost_mode = False
                self.cmd('enable')
                self.cmd('configure terminal')
            outs.append(self.cmd(c))
        self.cmd('end')
        return outs

    def _ip(self):
        m = re.search(r'IP:\s*(\d+\.\d+\.\d+\.\d+)', self.cmd('show net status'))
        return m.group(1) if m else None

    def _login(self):
        """Mint this run's own web session account.

        The password is drawn fresh every run and never written down. It used
        to be the literal `Mapper26x`, in this file, in the repository — so
        every bench run put a FULL-ADMIN account on the device whose password
        anyone with the repo already had. That is only survivable while the
        cleanup always runs, and on 20/09 it did not: a `user del smap` was
        removed from a suite's cleanup on the reasoning that smap "predates the
        test", and the account sat there with admin bits until it was noticed.
        A random password makes a leftover far less useful; deleting it in
        cleanup is still the actual fix, and both suites do.

        The account name stays fixed so a leftover is recognisable and any
        cleanup can name it."""
        import hashlib
        import secrets
        user = 'smap'
        pw = 'B' + secrets.token_urlsafe(12).replace('-', 'x').replace('_', 'y') + '2z'
        self._web_pw = pw
        self.cfg(f'user del {user}', f'user add {user} {pw}', f'user perm {user} admin')
        s = requests.Session()
        nonce = s.get(f'http://{self.ip}/api/login_init', timeout=10).json()['nonce']
        s.post(f'http://{self.ip}/api/login',
               data={'user': user, 'pass': hashlib.sha256(pw.encode('latin-1')).hexdigest(), 'nonce': nonce},
               headers={'Content-Type': 'application/x-www-form-urlencoded'}, timeout=15, allow_redirects=False)
        if 'SIMUTSESS' not in s.cookies.get_dict():
            raise SystemExit('web login failed')
        return s

    def relogin(self):
        self.reconnect()
        self.session = self._login()

    def get(self, path, **kw):
        """GET with one reconnect. The device drops the socket now and then on
        a long pass (the session also expires after 15 min idle), and a raw
        requests ConnectionError ends the run with a stack trace three screens
        long instead of a result. One relogin and one retry; a second failure
        is real and propagates."""
        timeout = kw.pop('timeout', 20)
        try:
            r = self.session.get(f'http://{self.ip}{path}', timeout=timeout, **kw)
        except requests.exceptions.RequestException:
            time.sleep(2)
            self.relogin()
            return self.session.get(f'http://{self.ip}{path}', timeout=timeout, **kw)
        if r.status_code == 401:
            self.relogin()
            return self.session.get(f'http://{self.ip}{path}', timeout=timeout, **kw)
        return r

    def get_json(self, path, **kw):
        r = self.get(path, **kw)
        if r.status_code != 200:
            raise RuntimeError(f'GET {path}: HTTP {r.status_code} {r.text[:80]}')
        return r.json()

    def commit(self, payload):
        return self.session.post(f'http://{self.ip}/api/commit_all',
                                 data={'_payload': json.dumps(payload)},
                                 headers={'Content-Type': 'application/x-www-form-urlencoded'}, timeout=30)

    # -- driving --
    def goto(self, tag, settle=1.2):
        out = self.cmd(f'screen {tag}')
        time.sleep(settle)
        return '?screen' not in out

    def tap(self, x, y, settle=1.0):
        """POST /api/touch, not `touch sim`: a tap arms the 5 s touch-priority
        window, during which the CLI queues at most two commands and drops
        the rest — a PIN typed over the CLI lost its digits. The web route
        has no queue, and the capture is let through the window it opens."""
        # 20 s and one retry, not 10 s and none. A tap is answered after Core 0
        # has taken it, and Core 0 is also what writes flash: with the table
        # full this timed out on 2026-09-20 and the run died three screens
        # later, on a keypad that had simply never been opened. A LOST tap is
        # worse than a slow one — it desynchronises every step after it — so
        # the retry is here and the failure still prints.
        for attempt in (1, 2):
            try:
                r = self.session.post(f'http://{self.ip}/api/touch',
                                      data={'x': x, 'y': y}, timeout=20)
                if r.status_code != 200:
                    print(f'  [tap] {x},{y}: HTTP {r.status_code} {r.text[:60]}')
                break
            except Exception as e:
                print(f'  [tap] {x},{y}: {e}{" — retry" if attempt == 1 else " — GAVE UP"}')
                if attempt == 2:
                    break
                time.sleep(2.0)
        self._last_tap = time.time()
        time.sleep(settle)

    def quiet(self, secs=6.0):
        """Wait until the touch-priority window after the last tap is over:
        the CLI executes again, pending log records reach the file, and the
        alarm line is allowed to send."""
        left = getattr(self, '_last_tap', 0) + secs - time.time()
        if left > 0:
            time.sleep(left)

    def taps(self, seq, settle=1.0):
        for x, y in seq:
            self.tap(x, y, settle)

    def activate_row(self, r, settle=1.2):
        """Lists open on row 0: r presses of DOWN select row r (the page
        follows), and a tap on a SELECTED row activates it."""
        for _ in range(r):
            self.tap(*FOOT['down'], 0.5)
        self.tap(160, ROW_Y[r % 4], settle)

    def nudge(self):
        """A tap on the title bar: ignored by every list, resets the 30 s idle."""
        self.cmd('touch sim 300 20', quiet_for=0.2, timeout=3)

    def reboot(self, settle=25.0):
        """`reload confirm`, then wait for the console and the web back.

        The panel's failure ladder lives in RAM and the top rung is cleared
        ONLY by a boot: PinKb::PANEL_FAIL_CEILING failures set
        _permanentLockout (DisplayManager_Users.cpp:190) and every visit to
        Settings then repaints "Tentativas Excedidas" instead of opening the
        picker. One full suite spends ~3 deliberate wrong PINs, so it never
        reached 20 — but partial re-runs accumulate, and on 2026-09-20 they
        did: seven checks failed with no log records at all, because every
        tap after the first was landing on the lockout screen. A run that
        starts from an unknown rung is not a repeatable test."""
        try:
            self.cmd('reload confirm', quiet_for=0.5, timeout=8)
        except Exception:
            pass          # the USB CDC port goes away mid-read: that IS the reboot
        time.sleep(settle)
        if not self.reconnect():
            raise RuntimeError('device did not come back after reload')
        for _ in range(30):
            try:
                if self.get('/api/config', timeout=10).status_code == 200:
                    return True
            except Exception:
                pass
            time.sleep(2)
        raise RuntimeError('web server did not come back after reload')

    def reconnect(self):
        try:
            self.ser.close()
        except Exception:
            pass
        for _ in range(30):
            port = next(iter(glob.glob(TARGET_GLOB)), None)
            if port:
                try:
                    self.ser = serial.Serial(port, BAUD, timeout=0.3)
                    self.ser.dtr = True
                    time.sleep(0.5)
                    self.ser.reset_input_buffer()
                    self.cmd('enable')
                    if self._ip():
                        return True
                except Exception:
                    pass
            time.sleep(2)
        return False

    def keypad_faces(self):
        """The cards, in deal order (`slots` glyphs each, decoys included).
        Also refreshes self.grid and self.policy from the same answer.
        ⚠️ Since the deal is rolled after EVERY tap, this has to be read again
        before each one.

        Over HTTP (GET /api/keypad), because the serial route was the thing
        that broke the full-table test: `show display keypad` costs the 5 s
        touch-priority window plus the CLI round trip, ~7 s per digit on a
        device with 32 accounts, and an eight-tap entry then runs past the
        panel's 30 s idle guard, which sends the screen home mid-PIN. Measured
        19/09 on the rig, one 4-digit entry: serial 27.9 s, HTTP 1.7 s.
        `keypad_faces_cli()` keeps the old path for the emergency console."""
        try:
            j = self.get_json('/api/keypad', timeout=10)
        except Exception as e:
            raise KeypadGone(f'/api/keypad failed: {e}')
        # NOT filtered for emptiness: a face is POSITIONAL, and the ordered
        # numeric pad has two blank cells (1..9, then 0 alone). Dropping them
        # used to be harmless because a dealt card is never empty; with the
        # ordered keyboards it would shift every key after the ninth.
        faces = list(j.get('faces') or [])
        # Which keyboard is on the glass: "cards" (one tap per card, re-dealt
        # after every tap), "num" (ordered, one tap per key) or "groups"
        # (ordered, TWO taps — the group then the character in its popup).
        self.kb = j.get('kb') or 'cards'
        pop = j.get('pop')
        if isinstance(pop, list) and len(pop) == 6:
            self.pop = dict(zip(('w', 'h', 'gap', 'one_y', 'row0_y', 'row1_y'), pop))
        # v25: grid = keys, slots, cols, rows, x, y, w, h, pitchX, pitchY
        g = j.get('grid')
        if isinstance(g, list) and len(g) == 10:
            self.grid = dict(zip(('keys', 'slots', 'cols', 'rows',
                                  'x', 'y', 'w', 'h', 'px', 'py'), g))
        else:
            self.grid = dict(PIN_GRID_FALLBACK)
        # policy = minLen, maxLen, keypad, alphabet
        pol = j.get('policy')
        if isinstance(pol, list) and len(pol) == 4:
            self.policy = dict(zip(('min', 'max', 'keypad', 'alphabet'), pol))
        if not j.get('up') or len(faces) != self.grid['keys']:
            raise KeypadGone(f'keypad not on screen '
                             f'(got {len(faces)} faces, grid says {self.grid["keys"]})')
        return faces

    def tap_key(self, k, settle=0.35):
        """Centre of key `k` of whatever layout is up."""
        g = self.grid
        self.tap(g['x'] + (k % g['cols']) * g['px'] + g['w'] // 2,
                 g['y'] + (k // g['cols']) * g['py'] + g['h'] // 2, settle)

    def tap_popup(self, i, n, settle=0.5):
        """Second tap of the alphanumeric keyboard: character `i` of a group
        of `n`. Up to five keys per row, two rows at most, every row centred —
        the same rule PinKeypad.h draws with, and the y values come from the
        device (`pop`) because the bench must not carry geometry of its own."""
        p = self.pop
        first = (n + 1) // 2 if n > 5 else n
        if i < first:
            m, row, idx = first, 0, i
        else:
            m, row, idx = n - first, 1, i - first
        y = p['one_y'] if n <= 5 else (p['row0_y'] if row == 0 else p['row1_y'])
        x0 = (320 - (m * p['w'] + (m - 1) * p['gap'])) // 2
        self.tap(x0 + idx * (p['w'] + p['gap']) + p['w'] // 2, y + p['h'] // 2, settle)

    def keypad_faces_cli(self):
        """The same cards over the serial CLI. Slow (see keypad_faces),
        kept for images without the web server up."""
        out = self.cmd('show display keypad', quiet_for=0.8, timeout=12)
        faces = {}
        for line in out.splitlines():
            # `SIMUT# 0: 521` — the echo of the prompt shares the line with
            # the first face, so the match is not anchored at the start.
            m = re.search(r'(?:^|#\s)(\d+):\s(\S+)\s*$', line)
            if m:
                faces[int(m.group(1))] = m.group(2)
        # However many the policy deals: 4, 6, 12 or 18 (PinKb::GRIDS).
        if not faces or sorted(faces) != list(range(len(faces))):
            raise KeypadGone(f'keypad not on screen (got {len(faces)} faces)')
        return [faces[i] for i in range(len(faces))]

    def _card_of(self, faces, ch):
        # len(faces), NOT 4. Four cards is the digits+3-glyph layout alone
        # (PinKb::GRIDS: 10 digits + 2 decoys over 4 cards); the other layouts
        # deal 6, 12 or 18. On 2026-09-20 the rig was left on 0-9A-Z with two
        # glyphs per card — 18 cards — and this looked at the first four, then
        # reported "'5' is on no card" while printing a list whose last card
        # was '56'. A bench that only works under one policy cannot test a
        # configurable one.
        k = next((i for i in range(len(faces)) if ch in faces[i]), None)
        if k is None:
            raise SystemExit(f'{ch!r} is on no card: {faces}')
        return k, faces[k].index(ch)

    def fresh_faces(self, previous, tries=25):
        """The deal AFTER the tap that has just been sent.

        ⚠️ This is the race the HTTP reader created. Core 1 re-deals when it
        consumes the tap, and /api/keypad answers in 0.01 s — faster than
        Core 1 gets to it. The serial reader took 1.2 s and hid the problem
        by accident; with the fast one, a read 0.6 s after the tap could
        still return the deal that tap was aimed at, and the next tap then
        hit the wrong card. It does not fail loudly: the entry is simply a
        different PIN, which the device rightly refuses.

        It cost a whole 25-account pass before it was spotted. The first-try
        rate fell from 23/25 to 15/25 while the AMBIGUITY events fell from 4
        to 1 — retries going up while the thing that causes retries went down
        is the shape of a broken instrument, not of a worse device.

        Waiting for the faces to differ is the signal, because the re-deal IS
        the acknowledgement. A fresh Fisher-Yates over twelve slots repeating
        all four faces exactly is too unlikely to plan around."""
        deadline = time.time() + 6.0
        for _ in range(tries):
            f = self.keypad_faces()
            if f != previous:
                return f
            if time.time() > deadline:
                raise SystemExit('keypad did not re-deal within 6 s: Core 1 stuck?')
            time.sleep(0.15)
        return self.keypad_faces()

    def reopen_picker(self):
        """Back to MODE_AUTH_USER from wherever we are.

        v25 flow detail the hard way: a REFUSED PIN leaves the KEYPAD on the
        glass, and SAIR from the keypad goes to the dashboard, not back to the
        picker (DisplayManager::pinCancel, PIN_FOR_AUTH -> forceDashboard).
        So a second account needs dashboard -> CFG, not another pick_user( )
        on top of the keypad — doing that fed the picker's taps to the keypad
        as card taps and the run reported a login as slot 0."""
        self.goto('dash')
        self.tap(*CFG_BTN, 1.2)

    def pick_user(self, who):
        """v25 — MODE_AUTH_USER: choose the account, THEN prove it.

        The list holds every active account that has a PIN, in slot order, so
        the caller knows the index without reading the glass. The right arrow
        moves the SELECTION and the page follows it, so walking `idx` steps
        lands on the right row of the right page; tapping a row that is
        already selected is what confirms it.

        Why this screen exists: identifying BY the PIN made one entry a search
        over the whole table, and with 32 accounts and four digits a blind
        entry landed on somebody 20.2% of the time — measured on this rig on
        19/09 as its mirror image, 15% of honest logins refused because two
        accounts fell inside the same tap sequence."""
        # The arrow steps one row while the list fits two pages and one PAGE
        # (4 rows) beyond that — DisplayManager_Users.cpp, same rule for both
        # lists. So: walk the arrow to the right PAGE, then tap the row. The
        # count decides the step, and it is asked for rather than assumed,
        # because guessing it wrong lands the tap on the wrong account and
        # the run then fails somewhere else entirely.
        listed = [u for u in self.users() if u.get('pin')]
        if isinstance(who, str):
            idx = next((i for i, u in enumerate(listed) if u['name'] == who), None)
            if idx is None:
                raise SystemExit(f"pick_user: '{who}' nao esta no picker "
                                 f"(lista: {[u['name'] for u in listed]})")
        else:
            idx = who
        # The arrow steps one ROW while the list fits two pages and one PAGE
        # (4 rows) beyond that — DisplayManager_Users.cpp, same rule for both
        # lists. So the arrow gets us to the right page and the row tap picks
        # inside it.
        step = 4 if len(listed) > 8 else 1
        sel = 0
        for _ in range(idx // step if step == 4 else idx):
            self.tap(*FOOT['down'], 0.35)
            sel += step
        # A tap on a row that is NOT the selected one only MOVES the selection;
        # confirming takes a second tap on the same row (drawUserList/
        # handleTouchPanelV24). With step=4 the selection lands on the first
        # row of the page, so any other row needs both taps — one tap left the
        # picker on screen and the keypad never opened, which is what
        # "keypad not on screen (got 0 faces)" was on 2026-09-20.
        row = idx % 4
        if sel != idx:
            self.tap(260, ROW_Y[row], 0.7)
        self.tap(260, ROW_Y[row], 1.2)

    def pin(self, digits, ok=True, faces=None):
        """Prove the PIN, on whichever of the three keyboards is up. The
        device reports which in /api/keypad's `kb`, and they do not cost the
        same number of taps:

            cards   one tap per CHARACTER, anywhere on the card holding it —
                    the device never learns which of the card's glyphs was
                    meant. RE-DEALT after every tap, so the cards are read
                    again before each one and the read WAITS for the new deal
                    (fresh_faces). `faces` seeds only the first read.
            num     one tap per character on the ordered numeric pad.
            groups  TWO taps: the group key, then the character in its popup.

        Only `cards` re-deals. On an ordered keyboard fresh_faces would wait
        for a change that never comes.

        v25: the account is chosen on MODE_AUTH_USER BEFORE this runs, so the
        sequence is resolved against ONE digest instead of the whole table —
        see Rig.pick_user( )."""
        for attempt in (1, 2, 3):
            try:
                f = faces if (faces is not None and attempt == 1) else self.keypad_faces()
                for i, ch in enumerate(digits):
                    # Only a DEALT keypad re-deals. An ordered one is the same
                    # keyboard on every tap, so waiting for it to change would
                    # wait for ever.
                    if i and self.kb == 'cards':
                        f = self.fresh_faces(f)
                    k, sl = self._card_of(f, ch)
                    self.tap_key(k)
                    if self.kb == 'groups':
                        self.tap_popup(sl, len(f[k]))
                break
            except KeypadGone:
                if attempt == 3:
                    raise
                # the idle guard took the screen: open it again and start over
                self.goto('dash')
                self.tap(*CFG_BTN, 1.2)
        if ok:
            self.tap(*PIN_OK, 1.6)

    def pin_exact(self, digits, ok=True):
        """Set a PIN: the screen is the ORDERED keyboard, not the dealt cards —
        choosing a PIN is not the problem scrambling solves. The positions are
        fixed but they are still READ, from /api/keypad like everything else:
        this used to carry its own NUM_COL/NUM_ROW copy of the numeric pad,
        which is one more thing to keep in step with the firmware, and it could
        not type a letter at all once the alphanumeric alphabet allowed one."""
        f = self.keypad_faces()
        for ch in digits:
            k, sl = self._card_of(f, ch)
            self.tap_key(k, 0.5)
            if self.kb == 'groups':
                self.tap_popup(sl, len(f[k]))
        if ok:
            self.tap(*PIN_OK, 1.6)

    def kb_type(self, text):
        """Type lower-case letters on the group keyboard: group, then popup key."""
        for ch in text:
            g = next(i for i, grp in enumerate(KB_GROUPS) if ch in grp)
            self.tap(KB_COL[g % 4], KB_ROW[g // 4], 0.8)
            n = len(KB_GROUPS[g])
            idx = KB_GROUPS[g].index(ch)
            x0 = (320 - (n * LP_KEY_W + (n - 1) * LP_GAP)) // 2
            self.tap(x0 + idx * (LP_KEY_W + LP_GAP) + LP_KEY_W // 2, LP_ROW0_Y + LP_KEY_H // 2, 0.8)

    def shot(self, name, retries=3):
        """A capture is ~1.7 s, but a retry is not: the timeout alone is 45 s
        and the panel's idle guard is 30 s. A capture that had to retry
        therefore lands the panel on the DASHBOARD, and every tap after it
        goes somewhere else — on 2026-09-20 one broken transfer
        (IncompleteRead on /api/screenshot) silently cost the four actions
        that followed it, and the run read them as missing log records. So
        the idle timer is reset whenever a shot took long enough to matter,
        whether or not it finally succeeded."""
        path = os.path.join(self.out, name + '.png')
        t0 = time.time()
        out = None
        for _ in range(retries):
            try:
                time.sleep(0.8)
                r = self.get('/api/screenshot', timeout=45)
                if r.status_code == 200 and len(r.content) > 200_000:
                    Image.open(BytesIO(r.content)).convert('RGB').save(path)
                    self.shots.append(name)
                    print(f'  [shot] {name}')
                    out = path
                    break
            except Exception as e:
                print(f'  [shot] {name}: {e}')
            time.sleep(2.0)
        if out is None:
            print(f'  [shot] {name}: FAILED')
        # 8 s, not 30: the guard counts from the last TAP, and the taps around
        # a shot have their own settle times on top of this.
        if time.time() - t0 > 8.0:
            self.nudge()
        return out

    def log_records(self, codes=None):
        """The binary log over HTTP: /api/logs streams 12-byte records
        (CompactLogRecord: epoch u32, uptimeLo u16, code u16, ctx i16, flags
        u8, uptimeHi u8). Measured on the rig 19/09 with 1189 records in the
        log: 0.15 s against 1.76 s for the same query over the serial console,
        which has to wait out the 5 s touch-priority window and then print the
        ring as text at 115200 baud.

        ⚠️ It RAISES on a refusal and never returns an empty list to mean
        one. The first version answered `[]` for any non-200, and /api/logs
        refuses two reads inside 200 ms with 429 and any read inside the
        touch window with 503 — so a perfectly healthy device that had just
        been tapped reported "no such record", which reads as "the action did
        not happen". It cost a verification pass before it was caught by
        comparing against the CLI: HTTP said 0 records of code 308, the CLI
        said 88, and the log itself had 88. Same family as the sealed-block
        blindness in the history reader: an instrument that answers "nothing"
        for "I could not look" will end an investigation early."""
        import struct
        want = set(codes) if codes else None
        buf = None
        for attempt in range(6):
            r = self.get('/api/logs', timeout=40)
            if r.status_code == 200:
                buf = r.content
                break
            if r.status_code in (429, 503):
                time.sleep(0.4 * (attempt + 1))
                continue
            raise RuntimeError(f'GET /api/logs: HTTP {r.status_code} {r.text[:80]}')
        if buf is None:
            raise RuntimeError('GET /api/logs: still 429/503 after 6 tries')
        if len(buf) % 12:
            raise RuntimeError(f'/api/logs returned {len(buf)} bytes, not a multiple of 12')
        out = []
        for off in range(0, len(buf) - 11, 12):
            epoch, uplo, code, ctx, flags, uphi = struct.unpack('<IHHhBB', buf[off:off + 12])
            if want is None or code in want:
                out.append({'epoch': epoch, 'code': code, 'ctx': ctx,
                            'level': (flags >> 5) & 7, 'core': (flags >> 4) & 1,
                            'up': uplo | (uphi << 16)})
        return out

    def wait_for_log(self, code, ctx, since=0, timeout=25.0):
        """Wait for a record with this code and ctx to appear at or after
        index `since`, and return the whole window. Returns (fresh, found).

        ⚠️ A single read right after the action is a RACE, and it is the one
        the HTTP reader introduced. `show system log` prints the RAM ring, so
        the console saw a record the instant it was written; /api/logs streams
        the FILES, and the write lands a few seconds later. The read that used
        to be safe is not, and it fails intermittently — in one 5-account pass
        the limits and block records were missed, in the next it was the
        maintenance one. Measured 19/09: the record was on the wire 6.5 s
        after the save, while the test read at ~2 s.

        Same shape as the sealed-history-block trap: the fast reader is blind
        at the most recent end, which is exactly the end a test asks about."""
        deadline = time.time() + timeout
        fresh = []
        while True:
            recs = self.log_records()
            fresh = recs[since:] if len(recs) >= since else recs
            if any(r['code'] == code and r['ctx'] == ctx for r in fresh):
                return fresh, True
            if time.time() > deadline:
                return fresh, False
            time.sleep(1.0)

    def log_lines(self, codes):
        """Lines of `show system log` whose code is one of `codes` (the CLI
        prints the binary log as `code=NNN ctx=N`). Prefer log_records( )."""
        out = self.cmd('show system log', quiet_for=1.2, timeout=20)
        want = {f'code={c} ' for c in codes}
        return [ln.strip() for ln in out.splitlines() if any(w in ln for w in want)]

    def log_count(self, code):
        return len(self.log_records([code]))

    def log_ctx(self, code):
        """ctx values of every record with this code, oldest first."""
        return [r['ctx'] for r in self.log_records([code])]

    def log_ctx_cli(self, code):
        """The same, read over the serial console. Kept for the emergency
        image, which has no web server."""
        return [int(m.group(1)) for ln in self.log_lines([code]) for m in [re.search(r'ctx=(-?\d+)', ln)] if m]

    def users(self):
        return self.get('/api/users').json()


def check(results, name, cond, detail=''):
    results.append({'check': name, 'ok': bool(cond), 'detail': detail})
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {detail}")
    return bool(cond)


def find_perm_free_slot_row(users_list, name):
    """Row index (0-based) in the panel's Users list: ascending slot, and
    WITHOUT the admin — slot 0 is not listed there (nothing on that screen
    applies to it), so every row is one lower than the API's order."""
    ordered = sorted((u for u in users_list if u['id'] != 0), key=lambda u: u['id'])
    for i, u in enumerate(ordered):
        if u['name'] == name:
            return i
    return -1


def restore_telemetry(rig, b):
    """Put the real alarm line back. ⚠️ `tel …` and `alarm set …` are
    CONFIGURATION commands; running them outside `configure terminal` answers
    OK and changes nothing, which is how a restore silently half-applies."""
    rig.cfg(f"tel server {b['t_srv']}", f"tel port {b['t_port']}",
            f"tel path {b['t_path']}", f"tel crypto {'on' if b.get('t_sec') else 'off'}",
            f"alarm set {'on' if b.get('a_en') else 'off'}",
            f"alarm set path {b['a_path']}", f"alarm set qmax {b.get('a_qmax') or 32}")
    rig.cmd('write memory')


# ── steps ──────────────────────────────────────────────────────────────────
def step_prep(rig, col, results, state):
    """⚠️ prep points the telemetry at the bench collector and only `cleanup`
    puts the device's own server back, from the backup prep took. Running prep
    WITHOUT cleanup therefore leaves the rig talking to the collector — and the
    next run's prep then backs THAT up as if it were the real setting, which is
    how a real server address gets lost (2026-09-19). Always run cleanup."""
    print('== prep ==')
    # First, from a known rung: see Rig.reboot( ).
    rig.reboot()
    cfgj = rig.get('/api/config').json()
    backup = {k: cfgj.get(k) for k in ('t_srv', 't_port', 't_path', 't_sec', 'a_en', 'a_mode', 'a_qmax', 'a_path')}
    # The guard panel_fulltable_test.py has had since 2026-09-19, and which
    # this file was missing: a prep that runs while the device is ALREADY
    # pointed here backs the bench collector up as if it were the real
    # setting, and the next cleanup then "restores" it. On 2026-09-20 five
    # partial runs did exactly that and the real address (192.168.3.206:8080
    # /telemetry) survived only in the FIRST run's state.json. Stop before
    # writing, not after.
    if backup.get('t_srv') == host_ip():
        # A previous run pointed it here and never ran cleanup — which is what
        # `--step prep --step joao` does every time. The saved backup is the
        # real address, so PUT IT BACK and carry on instead of refusing: it is
        # the bench's own recording, not a guess. Only refuse when there is
        # nothing to restore from.
        saved = (state.get('tel_backup') or {}) if isinstance(state.get('tel_backup'), dict) else {}
        if saved.get('t_srv') and saved['t_srv'] != host_ip():
            print(f"  [prep] device still pointed at this host; restoring "
                  f"{saved['t_srv']}:{saved['t_port']} from the saved backup")
            restore_telemetry(rig, saved)
            backup = dict(saved)
        else:
            raise SystemExit('the device is already pointed at this host and no saved '
                             'backup has the real address: restore the telemetry target '
                             'by hand first (the oldest state.json of the day has it). '
                             'A run without `--step cleanup` leaves it like this.')
    state['tel_backup'] = backup
    # Persisted BEFORE the first write to the device, not after the step. main( )
    # dumps the state when a step RETURNS, so a prep that died between pointing
    # the telemetry here and returning took the only copy of the real address
    # with it — which is how this run lost it on 2026-09-20, minutes after the
    # guard above was added to stop exactly that.
    if state.get('_statepath'):
        json.dump(state, open(state['_statepath'], 'w'), indent=1)
    state['a_line_before'] = cfgj.get('a_line')
    check(results, 'alarm line template is the v24 default after migration',
          isinstance(cfgj.get('a_line'), str) and '{user}' in cfgj['a_line'] and '{until}' in cfgj['a_line'],
          cfgj.get('a_line'))
    # The migration record is written once, on the first boot after it, and the
    # binary log is a ring: on a rig that has booted a few hundred times since,
    # it has aged out. Assert what is still true — the device is RUNNING v24 —
    # and report the record when it is still there.
    ctxs = rig.log_ctx(25)
    v24 = all('pin' in u for u in rig.users())
    check(results, 'the device is running the v24 schema (/api/users carries "pin")',
          v24, f'SYS_STORAGE_MIGRATED ctx in the log window: {ctxs[-4:] or "aged out"}')
    us = rig.users()
    state['users_before'] = us
    check(results, 'accounts survived the migration', any(u['name'] == 'admin' for u in us), json.dumps(us))
    # "and nobody else" held only on the first boot after the migration; this
    # rig has carried demo accounts with PINs for days.
    check(results, 'admin holds a PIN', any(u['name'] == 'admin' and u['pin'] for u in us),
          json.dumps([(u['name'], u['pin']) for u in us]))
    ip = host_ip()
    rig.cfg(f'tel server {ip}', f'tel port {COLLECTOR_PORT}', 'tel path /tel', 'tel crypto off',
            'alarm set on', 'alarm set mode json', 'alarm set qmax 32', 'alarm set path /tel/alarm',
            'user del pjoao', 'user add pjoao Joao2026x', 'user perm pjoao 0x1C00', 'user pin pjoao 5678',
            'user del pmaria', 'user add pmaria Maria2026x', 'user perm pmaria 0x1000', 'user pin pmaria 8765')
    dup = rig.cfg('user pin pmaria 5678')[0]
    check(results, 'CLI refuses a duplicate PIN and names the owner', 'pjoao' in dup, dup.strip()[:80])
    rig.cmd('write memory')
    time.sleep(1.0)
    us = rig.users()
    state['users_prep'] = us
    check(results, '/api/users shows the PIN flag of both test accounts',
          all(u['pin'] for u in us if u['name'] in ('pjoao', 'pmaria')) and any(u['name'] == 'pjoao' for u in us), json.dumps(us))
    cfgj = rig.get('/api/config').json()
    check(results, 'alarm line points at the collector', cfgj.get('t_srv') == ip and int(cfgj.get('t_port', 0)) == COLLECTOR_PORT, f"{cfgj.get('t_srv')}:{cfgj.get('t_port')}")
    al = rig.get('/api/alarms').json()
    sensors = al.get('sensors', al) if isinstance(al, dict) else al
    # The list IS the panel's list, so element k is row k: /api/alarms emits the
    # CONFIGURED sensors in index order (WebManager_Api.cpp:368) and so does
    # _activeSensorsMap (DisplayManager_Settings.cpp:86). Do NOT filter on
    # "active" here — in this JSON that key is alarmsActive, the alarm ENABLE
    # bit (WebManager_Api.cpp:393), not "the sensor exists". On 2026-09-20 the
    # filter left only sensor 4 (the one rig sensor with alarms on) while the
    # run tapped row 0, which is sensor 0, and the firmware's correct ctx=500
    # was read as a row-vs-index defect.
    state['slot_row'] = 0
    state['slot'] = int(sensors[state['slot_row']]['idx']) if sensors else 0
    state['alarms_before'] = al
    print(f"  sensor slot under test: {state['slot']}")


def login_panel(rig, pin, shot_prefix, results, expect_ok=True, user='admin'):
    """v25: Settings now opens the account picker, not the keypad. `user` is
    the account NAME (see Rig.pick_user) — never an index, because a rig
    carries accounts this script did not create."""
    rig.goto('dash')
    rig.tap(*CFG_BTN, 1.2)
    rig.shot(f'{shot_prefix}-picker')
    rig.pick_user(user)
    rig.shot(f'{shot_prefix}-keypad')
    rig.pin(pin)
    time.sleep(0.8)
    return True


def step_joao(rig, col, results, state):
    """Every action first, the collector last: the device defers telemetry
    while the panel is in use and retries the alarm line every 15 s, so a
    record follows its action by up to ~30 s — and a flow that waits that
    long between taps hits the panel's 30 s idle guard and lands on the
    dashboard. The log is read as it goes (it is written at once)."""
    print('== pjoao: alarm operator (limits, block, maintenance) ==')
    slot = state['slot']
    t_run = time.time()
    rig.goto('dash')
    rig.tap(*CFG_BTN, 1.2)
    rig.shot('01-pin-keypad')
    n_fail, n_ok = rig.log_count(309), rig.log_count(308)
    rig.pick_user('admin'); rig.pin('0000')
    rig.shot('02-pin-invalid')
    rig.reopen_picker()                      # a refused PIN stays on the keypad
    rig.pick_user('pjoao'); rig.pin('5678')
    rig.shot('03-menu-operator')
    # The slot is READ, not assumed: a rig carries accounts this script did
    # not create, so pjoao is not slot 4 just because it would be on a fresh
    # table. Assuming it cost a whole run on 2026-09-20.
    pj_slot = next(u['id'] for u in rig.users() if u['name'] == 'pjoao')
    okctx = rig.log_ctx(308)
    check(results, 'wrong PIN logged as SEC_PIN_FAIL (309), right one as SEC_PIN_OK (308) with ctx = user slot',
          rig.log_count(309) == n_fail + 1 and len(okctx) > n_ok and okctx[-1] == pj_slot,
          f'308 ctx now {okctx[-1:]}, pjoao slot {pj_slot}')
    n_saved, n_blk, n_unblk, n_mon, n_moff = (rig.log_count(c) for c in (442, 456, 457, 454, 455))
    # Alarms is the first visible item for this account
    rig.tap(*FOOT['enter'], 1.2)
    rig.shot('04-alarms-list')
    rig.tap(160, ROW_Y[state['slot_row']], 1.2)   # the selected row opens the sensor menu
    rig.shot('05-sensor-menu')
    # limits: row 0 selected already -> tap again opens the editor
    rig.tap(160, ROW_Y[0], 1.2)
    rig.shot('06-limit-editor')
    rig.tap(280, ROW_Y[0], 0.8)                  # + on the focused (first) bar
    rig.tap(280, ROW_Y[0], 0.8)
    rig.tap(*FOOT['enter'], 1.5)                 # SAVE
    rig.shot('07-sensor-menu-after-save')
    sctx = rig.log_ctx(442)
    # 400 was pjoao's slot (4) x100 on a fresh table, written as a literal. The
    # slot is read now, like everywhere else in this file.
    check(results, 'APP_UI_ALARM_SAVED (442) carries ctx=user*100+slot',
          len(sctx) > n_saved and sctx[-1] == pj_slot * 100 + slot,
          f'ctx={sctx[-1:]} (pjoao slot {pj_slot} x100 + sensor {slot})')
    # block / unblock: row 1 (the menu re-opens on row 0 after every action)
    rig.activate_row(1, 1.5)
    rig.shot('08-sensor-menu-toggled')
    rig.activate_row(1, 1.5)
    rig.shot('08b-sensor-menu-toggled-back')
    check(results, 'APP_UI_ALARM_BLOCKED (456) and UNBLOCKED (457) in the log, one each',
          rig.log_count(456) == n_blk + 1 and rig.log_count(457) == n_unblk + 1, f'ctx={rig.log_ctx(456)[-1:]}')
    # maintenance: row 2 -> entry screen, 0h05
    rig.activate_row(2, 1.5)
    rig.shot('09-maint-entry')
    rig.tap(MAINT_DEC, MAINT_ROW[0], 0.8)        # hours 1 -> 0
    rig.tap(MAINT_MID, MAINT_ROW[1], 0.8)        # focus minutes
    rig.tap(MAINT_INC, MAINT_ROW[1], 0.8)        # +5
    rig.shot('10-maint-0h05')
    rig.tap(*FOOT['enter'], 1.8)                 # START
    rig.shot('11-sensor-menu-in-maint')
    rig.activate_row(2)                          # open the window screen
    rig.shot('12-maint-remaining')
    rig.tap(*FOOT['enter'], 1.8)                 # END
    rig.shot('12b-sensor-menu-maint-ended')
    check(results, 'APP_UI_MAINT_ON (454) and _OFF (455) in the log',
          rig.log_count(454) == n_mon + 1 and rig.log_count(455) == n_moff + 1, f'ctx={rig.log_ctx(454)[-1:]}')
    rig.tap(*FOOT['exit'], 1.0)                  # back to the alarms list
    rig.tap(*ALARMS_EXIT, 1.0)                   # back to the menu
    rig.shot('13-menu-operator-no-users')
    rig.tap(*FOOT['exit'], 1.0)                  # dashboard
    # now the alarm line: everything above, signed, in order
    fresh = lambda r: r.get('_rx', 0) >= t_run
    rec = col.wait_for(lambda r: fresh(r) and r.get('maint') == 'maint_off' and r.get('user') == 'pjoao', 60)
    recs = [r for r in col.since(t_run) if r.get('user') == 'pjoao']
    kinds = [r.get('alarm') or r.get('maint') for r in recs]
    lim = next((r for r in recs if r.get('alarm') == 'alarm_lim'), None)
    mon = next((r for r in recs if r.get('maint') == 'maint_on'), None)
    check(results, 'alarm_lim record with lo/hi and user=pjoao reached the collector',
          lim is not None and 'lo' in lim and 'hi' in lim, json.dumps(lim) if lim else 'none')
    check(results, 'alarm_off and alarm_on records, both signed pjoao', 'alarm_off' in kinds and 'alarm_on' in kinds, str(kinds))
    check(results, 'maint_on record with until and user=pjoao', mon is not None and 'until' in mon, json.dumps(mon) if mon else 'none')
    if mon:
        planned = int(mon['until']) - int(mon['ts'])
        check(results, 'maint_on until = ts + 5 min (minute resolution)', 240 <= planned <= 360, f'until-ts={planned}s')
    check(results, 'maint_off record after END', rec is not None, json.dumps(rec) if rec else 'none')
    state['joao_records'] = recs


def step_admin(rig, col, results, state):
    print('== admin: inherited PIN, forced change, users ==')
    rig.goto('dash')
    rig.tap(*CFG_BTN, 1.2)
    n_ok, n_302, n_445 = rig.log_count(308), rig.log_count(302), rig.log_count(445)
    rig.pick_user('admin'); rig.pin('1234')      # admin, the factory PIN if the rig never changed it
    rig.shot('20-admin-after-1234')
    accepted = rig.log_count(308) > n_ok
    admin_pin = '1234'
    if accepted:
        forced = rig.log_count(302) > n_302      # "Default PIN detected; forcing change."
        check(results, 'admin identified with the inherited factory PIN', True, f'forced change={forced}')
        if forced:
            rig.pin_exact('2468'); rig.shot('21-admin-new-pin-confirm'); rig.pin_exact('2468')
            rig.shot('22-admin-pin-saved')
            rig.tap(*MSG_OK, 1.2)
            admin_pin = '2468'
            check(results, 'forced change: new PIN typed twice, saved (APP_UI_PIN_CHANGED 445)',
                  rig.log_count(445) > n_445, '')
    else:
        rig.tap(*PIN_CANCEL, 1.0)
        rig.cfg('user pin admin 2468'); rig.cmd('write memory')
        rig.goto('dash'); rig.tap(*CFG_BTN, 1.2); rig.pick_user('admin'); rig.pin('2468')
        admin_pin = '2468'
        check(results, 'admin PIN was not the factory one; set over the CLI and accepted',
              rig.log_count(308) > n_ok, '')
    state['admin_pin'] = admin_pin
    rig.shot('23-menu-admin-p1')
    for _ in range(4):
        rig.tap(*FOOT['down'], 0.6)
    rig.shot('24-menu-admin-p2')
    for _ in range(5):
        rig.tap(*FOOT['down'], 0.6)
    rig.shot('25-menu-admin-p3')                 # row 9 = Users
    rig.tap(*FOOT['enter'], 1.5)
    rig.shot('26-users-list')
    # NEW -> name keyboard -> bits -> PIN
    rig.tap(*FOOT['enter'], 1.2)
    rig.shot('27-new-user-keyboard')
    rig.kb_type('ana')
    rig.shot('28-new-user-name-typed')
    rig.tap(*KB_OK, 1.2)
    rig.shot('29-new-user-bits')
    rig.tap(160, ROW_Y[0], 0.8)                  # row 0 is selected: the tap toggles limits ON
    rig.tap(*FOOT['down'], 0.5); rig.tap(*FOOT['down'], 0.5)
    rig.tap(160, ROW_Y[2], 0.8)                  # maintenance ON
    rig.shot('30-new-user-bits-set')
    rig.tap(*FOOT['enter'], 1.2)                 # CONTINUE -> PIN
    rig.shot('31-new-user-pin')
    rig.pin_exact('1111'); rig.pin_exact('1111')
    rig.shot('32-new-user-saved')
    rig.tap(*MSG_OK, 1.5)
    rig.shot('33-users-list-with-ana')
    us = rig.users()
    ana = next((u for u in us if u['name'] == 'ana'), None)
    check(results, 'ana created from the panel with limits+maintenance bits and a PIN',
          ana is not None and ana['perms'] == 0x1400 and ana['pin'] is True, json.dumps(ana))
    check(results, 'APP_UI_USER_ADDED (450) in the log', rig.log_count(450) >= 1, f'ctx={rig.log_ctx(450)[-1:]}')
    # duplicate PIN refused on the panel: pjoao's PIN set to pmaria's
    row = find_perm_free_slot_row(us, 'pjoao')
    rig.activate_row(row)
    rig.shot('34-user-edit-pjoao')
    rig.activate_row(3)                          # Set PIN
    rig.pin_exact('8765'); rig.pin_exact('8765')
    rig.shot('35-pin-in-use')
    rig.tap(*MSG_OK, 1.2)                        # back to the editor
    rig.tap(*FOOT['exit'], 1.0)                  # back to the list
    us = rig.users()
    check(results, 'pjoao kept its PIN (duplicate refused)', any(u['name'] == 'pjoao' and u['pin'] for u in us), '')
    # delete ana: editor row 4 (second page), then confirm
    row = find_perm_free_slot_row(us, 'ana')
    rig.activate_row(row)
    rig.activate_row(4)
    rig.shot('36-delete-confirm')
    rig.tap(*CONFIRM_DELETE, 1.5)
    rig.shot('37-user-deleted')
    rig.tap(*MSG_OK, 1.2)
    us = rig.users()
    check(results, 'ana deleted from the panel', not any(u['name'] == 'ana' for u in us), json.dumps([u['name'] for u in us]))
    check(results, 'APP_UI_USER_DELETED (451) in the log', rig.log_count(451) >= 1, f'ctx={rig.log_ctx(451)[-1:]}')
    rig.tap(*FOOT['exit'], 1.0)
    rig.tap(*FOOT['exit'], 1.0)


def step_maria(rig, col, results, state):
    print('== pmaria: maintenance only ==')
    rig.goto('dash')
    rig.tap(*CFG_BTN, 1.2)
    rig.pick_user('pmaria'); rig.pin('8765')
    rig.shot('40-menu-maint-only')
    rig.tap(*FOOT['enter'], 1.2)
    rig.tap(160, ROW_Y[state['slot_row']], 1.2)
    rig.shot('41-sensor-menu-maint-only')
    rig.tap(160, ROW_Y[0], 1.0)                  # limits: dimmed, refused locally
    rig.shot('42-limits-refused')
    rig.tap(*FOOT['exit'], 1.0); rig.tap(*ALARMS_EXIT, 1.0); rig.tap(*FOOT['exit'], 1.0)


def step_web(rig, col, results, state):
    print('== web: commit_all users with a PIN ==')
    rig.quiet(7)                                  # commit_all answers 503 inside the touch window
    r = rig.commit({'users': {'actions': [{'type': 'add', 'name': 'web1', 'perms': 4096, 'pin': '2222'}]}})
    check(results, 'commit_all users add with pin answers 200', r.status_code == 200, r.text[:160])
    # users changes reboot the device
    time.sleep(28)
    for _ in range(20):
        try:
            rig.relogin(); break
        except Exception:
            time.sleep(3)
    us = rig.users()
    w = next((u for u in us if u['name'] == 'web1'), None)
    check(results, 'web1 exists with pin=true after the reboot', w is not None and w['pin'] is True, json.dumps(w))
    rig.quiet(7)
    r = rig.commit({'users': {'actions': [{'type': 'pin', 'id': 0, 'pin': '12'}]}})
    check(results, 'a malformed PIN is rejected by field, not by request', r.status_code in (200, 400) and 'users.pin' in r.text, r.text[:160])
    n_ok = rig.log_count(308)
    rig.goto('dash'); rig.tap(*CFG_BTN, 1.2); rig.pick_user('web1'); rig.pin('2222')
    rig.shot('50-web1-logged-in')
    # v25: the ctx has to be web1's OWN slot. Before the account picker this
    # check only asked whether SOMEBODY got in, which is exactly the property
    # that made a blind entry worth 20% with a full table.
    ok_ctx = rig.log_ctx(308)[-1:]
    check(results, 'the web-created account identifies at the panel by its PIN (308 with ITS slot)',
          rig.log_count(308) > n_ok and ok_ctx == [w['id']],
          f'ctx={ok_ctx}, web1 slot={w["id"] if w else None}')
    rig.tap(*FOOT['exit'], 1.0)


def step_identity(rig, col, results, state):
    """v25 — a assinatura de um registro pendente sobrevive ao apagamento.

    Até a v24 o registro de alarme guardava o SLOT de quem agiu e o nome era
    resolvido ao montar o payload, minutos depois. A fila espera o servidor
    confirmar e o slot é reutilizável: apagar a conta nesse intervalo fazia um
    registro de AUDITORIA sair assinado por quem tomasse o slot em seguida.

    Este passo provoca exatamente isso — age, apaga, põe outra pessoa no MESMO
    slot, e só então deixa a linha drenar — e exige que o que chega ao coletor
    ainda nomeie quem agiu. É a única prova que distingue o nome congelado no
    push de um nome lido tarde demais."""
    print('== identity: a assinatura sobrevive ao apagamento da conta ==')
    rig.cfg('user del zeca', 'user add zeca Zeca2026xx',
            'user perm zeca 0x1C00', 'user pin zeca 4747')
    rig.cmd('write memory')
    time.sleep(1.0)
    slot = next((u['id'] for u in rig.users() if u['name'] == 'zeca'), None)
    check(results, 'conta de teste criada com PIN', slot is not None, f'slot {slot}')
    if slot is None:
        return
    col._pull()
    n0 = len(col.records)

    rig.goto('dash'); rig.tap(*CFG_BTN, 1.5)
    rig.pick_user('zeca'); rig.pin('4747')
    time.sleep(1.2)
    rig.tap(*FOOT['enter'], 1.2)            # Alarmes
    rig.tap(160, ROW_Y[0], 1.2)             # primeiro sensor
    rig.activate_row(1, 1.8)                # bloquear
    time.sleep(1.0)
    rig.activate_row(1, 1.8)                # desbloquear
    time.sleep(1.0)
    rig.goto('dash')

    # apaga e poe OUTRA pessoa no mesmo slot, antes de a linha drenar
    rig.cfg('user del zeca', 'user del outro', 'user add outro Outro2026xx',
            'user perm outro 0x1C00')
    rig.cmd('write memory')
    novo = next((u['id'] for u in rig.users() if u['name'] == 'outro'), None)
    check(results, 'o slot da conta apagada foi reocupado (e a prova vale)',
          novo == slot, f'zeca era {slot}, outro ficou {novo}')

    rig.cmd('tel sync', quiet_for=0.8, timeout=20)
    time.sleep(12)
    col._pull()
    sig = [r.get('user') for r in col.records[n0:] if r.get('user')]
    check(results, 'o registro pendente continua assinado por quem AGIU',
          'zeca' in sig, f'assinaturas: {sig}')
    check(results, 'e NUNCA por quem tomou o slot depois',
          'outro' not in sig, f'assinaturas: {sig}')
    # a conta apagada nao deixa resto: sem PIN, sem nome, sem bits
    gone = [u for u in rig.users() if u['name'] == 'zeca']
    check(results, 'a conta apagada nao aparece mais em /api/users', not gone, f'{gone}')
    fresh = next((u for u in rig.users() if u['name'] == 'outro'), None)
    check(results, 'a conta que tomou o slot nao herdou o PIN do anterior',
          fresh is not None and fresh.get('pin') is False, json.dumps(fresh))
    rig.cfg('user del outro'); rig.cmd('write memory')


def step_cleanup(rig, col, results, state):
    print('== cleanup ==')
    b = state.get('tel_backup') or {}
    # pjoao (PIN 5678) and pmaria (8765) stay as demo accounts unless --purge:
    # the rig is left running this image for the maintainer to try the panel.
    cmds = ['user del ana', 'user del web1']
    if state.get('purge'):
        cmds += ['user del pjoao', 'user del pmaria']
    if b.get('t_srv') is not None:
        cmds += [f"tel server {b['t_srv']}", f"tel port {b['t_port']}", f"tel path {b['t_path']}",
                 f"tel crypto {'on' if b.get('t_sec') else 'off'}",
                 f"alarm set {'on' if b.get('a_en') else 'off'}"]
        if b.get('a_path'):
            cmds.append(f"alarm set path {b['a_path']}")
    rig.cfg(*cmds)
    rig.cmd('write memory')
    rig.cfg('user del smap')
    rig.goto('dash')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--step', action='append', default=None)
    ap.add_argument('--collector-log', default=COLLECTOR_LOG)
    ap.add_argument('--purge', action='store_true', help='cleanup also deletes the demo accounts')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    steps = a.step or ['prep', 'joao', 'admin', 'maria', 'web', 'identity', 'cleanup']
    col = Collector(a.collector_log)
    rig = Rig(a.out)
    results, state = [], {}

    sp = os.path.join(a.out, 'state.json')
    if os.path.exists(sp):
        state = json.load(open(sp))
    state['purge'] = a.purge
    state['_statepath'] = sp
    fns = {'prep': step_prep, 'joao': step_joao, 'admin': step_admin, 'maria': step_maria,
           'web': step_web, 'identity': step_identity, 'cleanup': step_cleanup}
    try:
        for s in steps:
            fns[s](rig, col, results, state)
            json.dump(state, open(sp, 'w'), indent=1)
    finally:
        json.dump({'results': results, 'records': col.records, 'raw': col.raw[-50:]},
                  open(os.path.join(a.out, f"results_{int(time.time())}.json"), 'w'), indent=1, default=str)
        rig.ser.close()
    ok = sum(1 for r in results if r['ok']); n = len(results)
    print(f'\n{ok}/{n} checks passed; {len(rig.shots)} screens captured; {len(col.records)} alarm records')


if __name__ == '__main__':
    main()
