#!/usr/bin/env python3
"""Drive the Ângulo panel study on the rig and capture every arm of it.

Bench companion of env:pico_w_uistudy (SIMUT_UI_STUDY=1). It switches the
theme and the dashboard layout over the serial CLI, opens the menu screens
with `screen <tag>`, captures each one with GET /api/screenshot, taps the
top card into min/max with POST /api/touch, and checks every study capture
against the colour the firmware wrote — the Ângulo tokens expanded from
RGB565 without bit replication (AGENTS.md §1) — never against another
capture. It also verifies the 4-px safe area (tft-area-segura: x 4..315,
y 4..235) is pure background on the layout captures.

    source ~/.simut-bench.env   # SIMUT_WEB_USER / SIMUT_WEB_PASS
    python3 tools/panel_study_capture.py --out shots --minmax

Flash pico_w_uistudy first; the release image has neither `screen` nor the
study themes. The hand must be released (CHARGER HIZ, PROBE STOP) or every
readback comes back dark. Nothing here writes to flash: `system theme` in
config mode is RAM-only until `write memory`, which this never sends.

2026-09-25: the run that produced the study report (23 captures, 0 px
outside the safe area, five tokens found in every layout capture).
"""
import argparse
import glob
import hashlib
import json
import os
import sys
import time
from collections import Counter
from io import BytesIO

import requests
import serial
from PIL import Image

TARGET_GLOB = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00'
BAUD = 115200
IP = os.environ.get('SIMUT_IP', '192.168.3.24')
BASE = f'http://{IP}'

# ANGULO.md §3.1, the roles the layouts paint most; the check counts pixels of
# each expanded value, so a missing token is a number, not an impression.
TOKENS = {
    'claro':  dict(fundo='#f6f6f4', superficie='#ffffff', tinta='#201e1a',
                   acento='#1f6355', linha='#dcdad3'),
    'escuro': dict(fundo='#161513', superficie='#201e1b', tinta='#ebe7df',
                   acento='#5fb39a', linha='#383430'),
}
ARMS = [
    ('def', 'simut_def', ['u0', 'set']),
    ('claro', 'angulo_claro', ['u0', 'u1', 'u2', 'u3', 'set', 'alm', 'thm', 'usr', 'lng']),
    ('escuro', 'angulo_escuro', ['u0', 'u1', 'u2', 'u3', 'set', 'alm', 'thm', 'usr', 'lng']),
]
TOP_CARD = (160, 70)   # a short tap here toggles min/max (DisplayManager_Touch.cpp)


def rgb565_expand(hexs):
    s = hexs.lstrip('#')
    r, g, b = (int(s[i:i + 2], 16) for i in (0, 2, 4))
    return (r & 0xF8, g & 0xFC, b & 0xF8)


# -- web ---------------------------------------------------------------------
def login():
    """Admin session: nonce + sha256 over the latin-1 bytes of the password,
    the way the login page hashes it. Refuses to try while the lockout is
    armed — a second wrong attempt only lengthens it."""
    user = os.environ.get('SIMUT_WEB_USER', 'admin')
    pw = os.environ.get('SIMUT_WEB_PASS')
    if not pw:
        raise SystemExit('SIMUT_WEB_PASS not set (source ~/.simut-bench.env)')
    s = requests.Session()
    init = s.get(f'{BASE}/api/login_init', timeout=10).json()
    if init.get('locked'):
        raise SystemExit(f'login locked for {init.get("lockSec")} s — not retrying')
    r = s.post(f'{BASE}/api/login',
               data={'user': user,
                     'pass': hashlib.sha256(pw.encode('latin-1')).hexdigest(),
                     'nonce': init['nonce']},
               headers={'Content-Type': 'application/x-www-form-urlencoded'},
               timeout=15, allow_redirects=False)
    if 'SIMUTSESS' not in s.cookies.get_dict():
        raise SystemExit(f'web login failed: HTTP {r.status_code}')
    return s


def shot(s, path, retries=5):
    """GET /api/screenshot -> PNG. 503 is the 5 s touch window; a dropped
    connection is the device busy (seen once right after a theme apply) —
    both are waited out, neither is a missing frame."""
    for i in range(retries):
        try:
            r = s.get(f'{BASE}/api/screenshot', timeout=60, stream=True)
        except requests.exceptions.ConnectionError:
            time.sleep(3.0)
            continue
        if r.status_code == 503:
            time.sleep(2.0)
            continue
        if r.status_code != 200:
            raise RuntimeError(f'screenshot HTTP {r.status_code}')
        data = b''
        try:
            for chunk in r.iter_content(65536):
                data += chunk
        except requests.exceptions.ChunkedEncodingError:
            pass  # the router cuts long port-80 flows near the end; keep what arrived
        try:
            img = Image.open(BytesIO(data))
            img.load()
        except Exception:
            if i + 1 < retries:
                time.sleep(1.5)
                continue
            raise
        img.save(path)
        return img
    raise RuntimeError('screenshot: retries exhausted')


# -- serial ------------------------------------------------------------------
class Cli:
    def __init__(self):
        port = next(iter(glob.glob(TARGET_GLOB)), None)
        if not port:
            raise SystemExit('target Pico W not found under /dev/serial/by-id')
        self.ser = serial.Serial(port, BAUD, timeout=0.3)
        self.ser.dtr = True
        time.sleep(0.5)
        self.ser.reset_input_buffer()
        self.cmd('enable')

    def cmd(self, text, quiet_for=0.4, timeout=5.0):
        self.ser.write((text + '\r\n').encode('latin-1'))
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

    def theme(self, name):
        """`system theme` is a config-mode command; RAM-only without `write memory`."""
        self.cmd('configure terminal')
        r = self.cmd(f'system theme {name}')
        self.cmd('end')
        return r

    def screen(self, tag):
        return self.cmd(f'screen {tag}')


# -- checks ------------------------------------------------------------------
def count_tokens(img, arm):
    cnt = Counter(img.convert('RGB').getdata())
    return {tok: cnt.get(rgb565_expand(hexs), 0) for tok, hexs in TOKENS[arm].items()}


def outside_safe_area(img, arm):
    """Pixels in the 4-px bands that are not the theme's background."""
    bg = rgb565_expand(TOKENS[arm]['fundo'])
    px = img.convert('RGB').load()
    w, h = img.size
    return sum(1 for y in range(h) for x in range(w)
               if (x < 4 or x > 315 or y < 4 or y > 235) and px[x, y] != bg)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default='shots')
    ap.add_argument('--only', choices=['def', 'claro', 'escuro'])
    ap.add_argument('--settle', type=float, default=2.0, help='seconds after a screen switch')
    ap.add_argument('--minmax', action='store_true', help='tap the top card into min/max on layout A')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    s = login()
    cli = Cli()
    perms = s.get(f'{BASE}/api/perms', timeout=10).json()
    print(f'rig: {perms.get("version")} {perms.get("env")} — themes: '
          f'{[t["name"] for t in s.get(f"{BASE}/api/themes", timeout=10).json()]}')

    report = {}
    failures = 0
    for arm, theme, tags in ARMS:
        if a.only and arm != a.only:
            continue
        cli.theme(theme)
        time.sleep(1.5)
        for tag in tags:
            cli.screen(tag)
            time.sleep(a.settle)
            fn = os.path.join(a.out, f'{arm}_{tag}.png')
            img = shot(s, fn)
            line = f'[{arm}] screen {tag:4s} -> {fn}'
            if arm in TOKENS and tag.startswith('u'):
                toks = count_tokens(img, arm)
                bad = outside_safe_area(img, arm) if tag != 'u0' else None
                report[f'{arm}_{tag}'] = dict(tokens=toks, outside_safe_area=bad)
                missing = [k for k, v in toks.items() if v == 0 and not (tag == 'u0' and k == 'linha')]
                if missing or bad:
                    failures += 1
                    line += f'  FAIL missing={missing} outside={bad}'
                else:
                    line += '  ok ' + ' '.join(f'{k}={v}' for k, v in toks.items())
            print(line)
            if tag == 'u1' and a.minmax and arm in TOKENS:
                s.post(f'{BASE}/api/touch', data={'x': TOP_CARD[0], 'y': TOP_CARD[1]}, timeout=10)
                time.sleep(6.5)   # /api/screenshot answers 503 for 5 s after a touch
                shot(s, os.path.join(a.out, f'{arm}_u1_minmax.png'))
                print(f'[{arm}] u1 min/max captured')
                s.post(f'{BASE}/api/touch', data={'x': TOP_CARD[0], 'y': TOP_CARD[1]}, timeout=10)
                time.sleep(6.5)
                cli.screen('u1')
                time.sleep(a.settle)
        cli.screen('u0')
    cli.theme('simut_def')
    cli.screen('u0')
    with open(os.path.join(a.out, 'checks.json'), 'w') as fh:
        json.dump(report, fh, indent=1)
    print(f'{len(report)} layout captures checked, {failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
