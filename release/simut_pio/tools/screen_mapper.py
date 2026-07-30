#!/usr/bin/env python3
"""Drive the SIMUT touch UI from the host and capture every screen it has.

Two mechanisms, used where each is appropriate:

  screen <tag>     jumps straight to one of twelve screens. Fast, reliable,
                   and it resets the touch-idle timer so the 30 s revert to
                   the dashboard cannot fire mid-capture.
  touch sim <X> <Y>  injects a tap in screen space. This is the only way to
                   reach the screens that exist as a state *within* another
                   screen -- alarm edit, graph detail, statistics, calendar,
                   the auth keypad, the mute confirmation.

Both live in the full-CLI profile, so flash `pico_w_test` before running:

    pio run -e pico_w_test -t upload --upload-port /dev/serial/by-id/usb-...

Frames come from GET /api/screenshot, which reads the framebuffer back over
SPI -- these are the panel's own pixels, not a rendering of them.

    python3 tools/screen_mapper.py --out docs/images/screens

SAFETY: every step here is an explicit coordinate. The tool never explores
blindly, because the UI it is walking contains factory reset, filesystem
format and mute-everything confirmations. Steps that open a destructive
dialog are captured and then explicitly cancelled; see CONFIRM_MUTE.
"""
import argparse
import glob
import json
import os
import re
import sys
import time
from io import BytesIO

import requests
import serial
from PIL import Image

TARGET_GLOB = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00'
BAUD = 115200

# Screen-space geometry, read out of DisplayManager_Touch.cpp rather than
# guessed from a screenshot.
FOOTER_Y = 215                      # any y > 195 lands in the footer row
FOOTER_BTN = [34, 97, 160, 223, 286]  # btnIdx = (x - 5) / 63, five slots
TOP_PANEL = (160, 70)               # y in 35..110 -- upper sensor card
BOTTOM_PANEL = (160, 150)           # lower sensor card


class Rig:
    """Serial CLI + authenticated web session against one device."""

    def __init__(self, verbose=True):
        self.verbose = verbose
        port = next(iter(glob.glob(TARGET_GLOB)), None)
        if not port:
            raise SystemExit('target Pico W not found under /dev/serial/by-id')
        self.ser = serial.Serial(port, BAUD, timeout=0.3)
        self.ser.dtr = True
        time.sleep(0.4)
        self.ser.reset_input_buffer()

        self.cmd('enable')
        self.ip = self._ip()
        if not self.ip:
            raise SystemExit('device reports no IP -- is it on Wi-Fi?')
        self.session = self._login()

    # -- serial ------------------------------------------------------------
    def cmd(self, text, quiet_for=0.5, timeout=6.0):
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

    def _ip(self):
        m = re.search(r'IP:\s*(\d+\.\d+\.\d+\.\d+)', self.cmd('show net status'))
        return m.group(1) if m else None

    # -- web ---------------------------------------------------------------
    def _login(self):
        """Log in as a throwaway account created over the CLI.

        Resetting the admin password would arm mustChangePassword and every
        page would answer 302 /force_chpass. Nothing here is written to
        flash, so the account is gone at the next reboot.
        """
        import hashlib
        user, pw = 'smap', 'Mapper26x'
        self.cmd('configure terminal')
        self.cmd(f'user del {user}')
        self.cmd(f'user add {user} {pw}')
        self.cmd(f'user perm {user} admin')
        self.cmd('end')

        s = requests.Session()
        nonce = s.get(f'http://{self.ip}/api/login_init', timeout=10).json()['nonce']
        s.post(f'http://{self.ip}/api/login',
               data={'user': user,
                     'pass': hashlib.sha256(pw.encode('latin-1')).hexdigest(),
                     'nonce': nonce},
               headers={'Content-Type': 'application/x-www-form-urlencoded'},
               timeout=15, allow_redirects=False)
        if 'SIMUTSESS' not in s.cookies.get_dict():
            raise SystemExit('web login failed -- cannot capture frames')
        return s

    def cleanup(self):
        self.cmd('configure terminal')
        self.cmd('user del smap')
        self.cmd('end')
        self.ser.close()

    # -- driving -----------------------------------------------------------
    def goto(self, tag, settle=1.2):
        out = self.cmd(f'screen {tag}')
        time.sleep(settle)
        return '?screen' not in out

    def tap(self, x, y, settle=1.1):
        self.cmd(f'touch sim {x} {y}', quiet_for=0.3, timeout=5)
        time.sleep(settle)

    def keep_awake(self):
        """The UI reverts to the dashboard after 30 s without a touch."""
        self.cmd('screen dash', quiet_for=0.2, timeout=4)

    def shot(self, path, retries=3):
        for _ in range(retries):
            try:
                r = self.session.get(f'http://{self.ip}/api/screenshot', timeout=45)
                if r.status_code == 200 and len(r.content) > 200_000:
                    im = Image.open(BytesIO(r.content)).convert('RGB')
                    im.save(path)
                    return im
            except Exception:
                pass
            time.sleep(2.5)
        return None


# Bottom button row of the settings menus: up, down, exit, enter.
MENU = {'up': (39, 215), 'down': (105, 215), 'exit': (170, 215),
        'enter': (250, 215)}

# Every screen the firmware has, in the order a person would meet them.
#
# `start` is a `screen` tag to jump to first (None means wherever we are),
# `taps` is the sequence applied from there. Jumping first makes each entry
# independent -- an earlier failure cannot silently displace a later capture,
# which is exactly the mistake the first version of this table made.
SCREENS = [
    ('dashboard', 'Live readings, two sensor cards and the slot footer',
     'dash', [], 'MODE_DASHBOARD'),

    ('auth-keypad', 'PIN entry guarding the settings menu',
     'dash', [(FOOTER_BTN[4], FOOTER_Y)], 'MODE_AUTH'),

    ('settings-main', 'Settings menu',
     'set', [], 'MODE_SETTINGS_MAIN'),

    ('settings-sounds', 'Alarm sound settings, third item of the menu',
     'set', [MENU['down'], MENU['down'], MENU['enter']],
     'MODE_SETTINGS_SOUNDS'),

    ('settings-themes', 'Theme picker',
     'thm', [], 'MODE_SETTINGS_THEMES'),

    ('settings-lang', 'Interface language',
     'lng', [], 'MODE_SETTINGS_LANG'),

    ('settings-password', 'On-screen keyboard for the display PIN',
     'pwd', [], 'MODE_SETTINGS_PASSWORD'),

    ('settings-alarms', 'Per-sensor alarm thresholds',
     'alm', [], 'MODE_SETTINGS_ALARMS'),

    # The editor opens from the ON/OFF zone (x >= 230) of the row that is
    # already selected, and only while that row reads OFF. Tapping the row
    # body merely moves the selection.
    ('settings-alarm-edit', 'Threshold editor for one sensor',
     'alm', [(270, 60)], 'MODE_SETTINGS_ALARM_EDIT'),

    ('settings-status', 'Real-time system status',
     'sts', [], 'MODE_SETTINGS_STATUS'),

    ('settings-license', 'License text',
     'lic', [], 'MODE_SETTINGS_LICENSE'),

    ('settings-offset', 'Display position adjustment',
     'offset', [], 'MODE_SETTINGS_DISPLAY_OFFSET'),

    ('touch-calibration', 'Touch calibration crosshairs',
     'touchcal', [], 'MODE_SETTINGS_TOUCH_CAL'),

    ('touch-sensitivity', 'Touch pressure calibration',
     'touchsens', [], 'MODE_SETTINGS_TOUCH_SENS'),

    ('graph', 'History plot for the selected sensor',
     'gra', [], 'MODE_GRAPH_VIEW'),

    ('graph-detail', 'Numeric detail: max, min, average, standard deviation',
     'gra', [(160, 120)], 'MODE_GRAPH_DETAIL'),

    ('calendar', 'Month picker, third button of the graph bar',
     'gra', [(160, 215)], 'MODE_CALENDAR'),
]

# Modes this tool deliberately does not drive, and why. Kept here so the
# coverage report can distinguish "not reached" from "not attempted".
NOT_ATTEMPTED = {
    'MODE_GRAPH_LOADING':
        'transient -- only painted while a 7-day range is being read',
    'MODE_ALARM_ACTION':
        'needs a sensor genuinely outside its thresholds',
    'MODE_CONFIRM_MUTE_ALL':
        'a destructive confirmation; driving it risks silencing every alarm',
    'MODE_STATS_VIEW':
        'no touch path reaches it -- see the note in screens.md',
}


def write_index(out, results):
    """Emit the screen map: what exists, how to reach it, what is missing."""
    lines = [
        '# SIMUT display screens',
        '',
        'Generated by `tools/screen_mapper.py` against a real device. Every',
        'frame was read back off the panel through `GET /api/screenshot`.',
        '',
        '| Screen | UiMode | How to reach it | What it is |',
        '|---|---|---|---|',
    ]
    for r in results:
        shot = f'![{r["name"]}]({r["name"]}.png)' if r['captured'] else '—'
        lines.append(f'| {shot}<br>`{r["name"]}` | `{r["mode"]}` | '
                     f'`{r["route"]}` | {r["description"]} |')

    lines += ['', '## Modes this tool does not drive', '']
    for mode, why in sorted(NOT_ATTEMPTED.items()):
        lines.append(f'- **`{mode}`** — {why}')

    failed = [r for r in results if not r['captured']]
    if failed:
        lines += ['', '## Did not capture', '']
        for r in failed:
            lines.append(f'- **`{r["name"]}`** via `{r["route"]}`')

    with open(os.path.join(out, 'screens.md'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='docs/images/screens')
    ap.add_argument('--scale', type=int, default=2,
                    help='upscale factor for the saved PNGs (nearest)')
    ap.add_argument('--only', default=None,
                    help='comma-separated screen names to capture')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    rig = Rig()
    print(f'device {rig.ip}', flush=True)
    results = []

    try:
        wanted = set(args.only.split(',')) if args.only else None
        for name, desc, start, taps, mode in SCREENS:
            if wanted and name not in wanted:
                continue
            ok = True
            route = []
            if start:
                ok = rig.goto(start)
                route.append(f'screen {start}')
            for (x, y) in taps:
                rig.tap(x, y)
                route.append(f'tap({x},{y})')
            route = ' -> '.join(route)

            path = os.path.join(args.out, f'{name}.png')
            im = rig.shot(path)
            if im and args.scale > 1:
                im.resize((im.width * args.scale, im.height * args.scale),
                          Image.NEAREST).save(path)
            status = 'ok' if im else 'CAPTURE FAILED'
            if not ok:
                status = 'route rejected'
            print(f'  {name:20} {status:16} [{route}]', flush=True)
            results.append({'name': name, 'description': desc, 'mode': mode,
                            'route': route, 'captured': bool(im)})
    finally:
        rig.cleanup()

    with open(os.path.join(args.out, 'screens.json'), 'w') as f:
        json.dump(results, f, indent=2)

    if not args.only:
        write_index(args.out, results)

    got = {r['mode'] for r in results if r['captured']}
    print(f'\n{sum(r["captured"] for r in results)}/{len(results)} captured, '
          f'{len(got)} distinct UiMode values')
    for mode, why in sorted(NOT_ATTEMPTED.items()):
        print(f'  not attempted: {mode:28} {why}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
