#!/usr/bin/env python3
"""GET /api/wifi/scan on the bench, in STA mode and from inside the setup AP.

WHY THIS EXISTS
---------------
AP mode is the case the feature was built for and the only one that cannot be
tested from the LAN: the device stops being on the network, so the host has to
join the device's own access point with a second radio, the way a phone does.
Everything below was learned by getting it wrong first.

THE TRAPS, in the order they bite
---------------------------------
1. `nmcli device wifi connect` reads a CACHED scan list. An access point that
   came up three seconds ago is "network not found" — rescan, wait ~8 s, and
   only then connect. This cost two runs.

2. The softAP hands out NO DHCP lease. `nmcli ... connect` associates fine and
   then fails with "IP configuration could not be reserved". Use a profile
   with a static 192.168.4.2/24 and ipv4.never-default=yes, or the host also
   loses its default route.

3. `user perm <u> dashboard` is REFUSED — the roles are admin|operator|viewer|
   none or 0xMASK — and a refused perm change leaves the account with the bits
   it already had. On 2026-09-20 that made a full admin look like a
   dashboard-only account getting 200 from a PERM_NET_CONFIG route, i.e. an
   authorization hole that was not there. Assert on GET /api/perms, never on
   the CLI's reply.

4. `reload confirm` takes the USB CDC port down. Catch SerialException around
   every read that follows a reboot, or the run ends in a stack trace that
   says nothing about the device.

5. A .lng pack is read only at boot, its destination comes from the multipart
   FILENAME (a `dir` form field is ignored, and the file lands in /), and
   POST /api/delete takes `file`, not `path`.

USAGE
    python3 tools/wifi_scan_hw_test.py                  # STA only
    python3 tools/wifi_scan_hw_test.py --ap             # STA, then AP mode
    python3 tools/wifi_scan_hw_test.py --ap --if wlx…   # pick the second radio

The AP PSK is derived from the board id and printed by the `ap` command; this
script reads it off the console and never prints it.

@project SIMUT
@license MIT License
"""
import argparse
import glob
import hashlib
import re
import secrets
import subprocess
import sys
import time

import requests
import serial

TARGET_GLOB = '/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00'
BAUD = 115200
AP_IP = '192.168.4.1'
AP_HOST_IP = '192.168.4.2/24'
PROFILE = 'simut-ap-probe'
USER = 'swscan'          # fixed name so a leftover is recognisable
RATE_GAP = 1.5           # the device answers "Too Fast" below this


class Rig:
    def __init__(self):
        ports = glob.glob(TARGET_GLOB)
        if not ports:
            raise SystemExit('target Pico W not found under /dev/serial/by-id')
        self.ser = serial.Serial(ports[0], BAUD, timeout=0.3)
        self.pw = None

    def cli(self, *cmds, wait=1.6):
        """Trap 4: a reboot reaches this as SerialException, not as a reply."""
        try:
            self.ser.write(b'\r\n'); time.sleep(0.3); self.ser.read(60000)
            out = ''
            for c in cmds:
                self.ser.write(c.encode() + b'\r\n')
                time.sleep(wait)
                out += self.ser.read(60000).decode('utf-8', 'replace')
            return out
        except (OSError, serial.SerialException) as e:
            print(f'  [serial] {e} — port lost; reopening')
            time.sleep(14)
            ports = glob.glob(TARGET_GLOB)
            if ports:
                self.ser = serial.Serial(ports[0], BAUD, timeout=0.3)
            return ''

    def ip(self):
        m = re.search(r'IP:\s*(\d+\.\d+\.\d+\.\d+)', self.cli('show net status', wait=2.5))
        return m.group(1) if m else None

    def mint(self, host):
        """A throwaway admin account, password drawn fresh and never written down."""
        self.pw = 'B' + secrets.token_urlsafe(12).replace('-', 'x').replace('_', 'y') + '2z'
        self.cli('enable', 'configure terminal', f'user del {USER}',
                 f'user add {USER} {self.pw}', f'user perm {USER} admin', 'end')
        return self.login(host)

    def login(self, host):
        s = requests.Session()
        nonce = s.get(f'http://{host}/api/login_init', timeout=10).json()['nonce']
        s.post(f'http://{host}/api/login',
               data={'user': USER,
                     'pass': hashlib.sha256(self.pw.encode('latin-1')).hexdigest(),
                     'nonce': nonce},
               headers={'Content-Type': 'application/x-www-form-urlencoded'},
               timeout=15, allow_redirects=False)
        if 'SIMUTSESS' not in s.cookies.get_dict():
            raise SystemExit(f'web login failed at {host}')
        return s

    def drop(self):
        self.cli('enable', 'configure terminal', f'user del {USER}', 'end')


def nm(*a):
    return subprocess.run(['nmcli', *a], capture_output=True, text=True)


def sweep(s, host, tag, out):
    """One scan, polled the way the page polls it."""
    t0, fails, d = time.time(), 0, None
    r = s.get(f'http://{host}/api/wifi/scan?again=1', timeout=20)
    if r.status_code != 200:
        out.fail(f'{tag}: first call answered HTTP {r.status_code} — {r.text[:80]}')
        return []
    for _ in range(25):
        time.sleep(0.9)
        try:
            d = s.get(f'http://{host}/api/wifi/scan', timeout=20).json()
        except Exception as e:                       # noqa: BLE001 — see trap 2
            fails += 1
            continue
        if not d.get('scanning'):
            break
    nets = (d or {}).get('nets', [])
    took = time.time() - t0
    print(f'  [{tag}] {took:5.2f}s | {fails} poll(s) lost | {len(nets)} networks')
    out.check(f'{tag}: the scan landed', bool(nets) or (d or {}).get('error') is None)
    if nets:
        print(f'  [{tag}] signals {[n["rssi"] for n in nets]}')
        out.check(f'{tag}: sorted by signal',
                  all(nets[i]['rssi'] >= nets[i + 1]['rssi'] for i in range(len(nets) - 1)))
        out.check(f'{tag}: no hidden SSID offered', all(n['ssid'] for n in nets))
        out.check(f'{tag}: no SSID listed twice',
                  len({n['ssid'] for n in nets}) == len(nets))
    return nets


class Out:
    def __init__(self):
        self.ok = self.bad = 0

    def check(self, what, cond):
        print(f'    {"PASS" if cond else "FAIL"}  {what}')
        if cond:
            self.ok += 1
        else:
            self.bad += 1

    def fail(self, what):
        self.check(what, False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ap', action='store_true', help='also test from inside the setup AP')
    ap.add_argument('--if', dest='iface', default='wlx001a3fa1e19e',
                    help='the second radio that joins the setup AP')
    args = ap.parse_args()

    rig, out = Rig(), Out()
    sta_ip = rig.ip()
    if not sta_ip or sta_ip.startswith('('):
        raise SystemExit('the device has no STA address; start from a connected unit')
    print(f'device at {sta_ip}')

    try:
        s = rig.mint(sta_ip)
        print('== STA mode ==')
        sweep(s, sta_ip, 'sta', out)

        # Trap 3: the gate, asserted on /api/perms and not on the CLI's reply.
        time.sleep(RATE_GAP)
        pw = 'B' + secrets.token_urlsafe(12).replace('-', 'x').replace('_', 'y') + '2z'
        rig.cli('enable', 'configure terminal', 'user del swperm',
                f'user add swperm {pw}', 'end')
        g = requests.Session()
        nonce = g.get(f'http://{sta_ip}/api/login_init', timeout=10).json()['nonce']
        g.post(f'http://{sta_ip}/api/login',
               data={'user': 'swperm', 'pass': hashlib.sha256(pw.encode('latin-1')).hexdigest(),
                     'nonce': nonce},
               headers={'Content-Type': 'application/x-www-form-urlencoded'},
               timeout=15, allow_redirects=False)
        perms = g.get(f'http://{sta_ip}/api/perms', timeout=20).json().get('perms')
        out.check(f'the plain account really lacks PERM_NET_CONFIG (perms={perms})',
                  not (perms & 0x0010))
        time.sleep(RATE_GAP)
        out.check('it is refused with 403',
                  g.get(f'http://{sta_ip}/api/wifi/scan', timeout=20).status_code == 403)
        out.check('and no session at all is 401',
                  requests.get(f'http://{sta_ip}/api/wifi/scan', timeout=20).status_code == 401)
        rig.cli('enable', 'configure terminal', 'user del swperm', 'end')

        if args.ap:
            print('== AP mode ==')
            banner = rig.cli('enable', 'ap', wait=4.0)
            m = re.search(r'\[AP\] SSID:\s*(\S+)', banner)
            k = re.search(r'\[AP\] PSK\s*:\s*(\S+)', banner)
            if not (m and k):
                out.fail('the `ap` command did not announce an access point')
                raise SystemExit(1)
            ssid, psk = m.group(1), k.group(1)
            print(f'  access point {ssid} up, key read off the console (not printed)')

            nm('device', 'disconnect', args.iface)
            nm('connection', 'delete', PROFILE)
            # Trap 1: the cached list does not have an AP that came up seconds ago.
            for attempt in range(4):
                time.sleep(5)
                nm('device', 'wifi', 'rescan', 'ifname', args.iface)
                time.sleep(6)
                r = nm('-t', '-f', 'SSID', 'device', 'wifi', 'list', '--rescan', 'no',
                       'ifname', args.iface)
                if ssid in r.stdout:
                    break
            else:
                out.fail(f'{ssid} never appeared to {args.iface}')
                raise SystemExit(1)

            # Trap 2: static address, because the softAP leases nothing.
            nm('connection', 'add', 'type', 'wifi', 'ifname', args.iface,
               'con-name', PROFILE, 'ssid', ssid,
               'ipv4.method', 'manual', 'ipv4.addresses', AP_HOST_IP,
               'ipv4.never-default', 'yes', 'ipv6.method', 'disabled',
               'wifi-sec.key-mgmt', 'wpa-psk', 'wifi-sec.psk', psk)
            r = nm('connection', 'up', PROFILE)
            out.check('the host joined the setup AP', r.returncode == 0)
            if r.returncode:
                print('   ', (r.stdout or r.stderr).strip()[:200])
                raise SystemExit(1)
            time.sleep(4)

            s = rig.mint(AP_IP)
            first = sweep(s, AP_IP, 'ap', out)
            time.sleep(RATE_GAP)
            out.check('the access point survived being scanned from',
                      s.get(f'http://{AP_IP}/api/network', timeout=20).status_code == 200)
            time.sleep(RATE_GAP)
            second = sweep(s, AP_IP, 'ap-again', out)
            out.check('a second sweep in the same boot still works '
                      '(wifi_scan_state did not wedge)', bool(second) or not first)
    finally:
        print('-- cleanup --')
        nm('device', 'disconnect', args.iface)
        nm('connection', 'delete', PROFILE)
        if args.ap:
            rig.cli('enable', 'reload confirm', wait=2.0)   # AP mode -> back to STA
            time.sleep(20)
        rig.drop()
        rig.ser.close()

    print(f'\n{out.ok}/{out.ok + out.bad} checks passed')
    return 1 if out.bad else 0


if __name__ == '__main__':
    sys.exit(main())
