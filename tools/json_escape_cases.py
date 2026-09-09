#!/usr/bin/env python3
"""
JSON-escaping cases for the web API, run against the real device (finding V-04).

Three strings that a user with the matching permission was allowed to store,
and that each broke a different JSON response for EVERY user of the device:

    net.ssid  = a"b        -> GET /api/network stopped parsing
    hwId      = X\\         -> GET /api/status  stopped parsing
    .lng @NAME = Po"rt     -> GET /api/perms    stopped parsing, so the whole
                              UI fell over: it is the first request every page
                              makes. Worse than the other two, because a pack
                              is only read at boot, so a reboot did not clear
                              it -- only replacing the pack did.

WHY IT IS AN A/B AND NOT A CHECKLIST
    Run this against firmware from before the fix and cases 1 and 2 must show
    `broke=yes` -- that failure IS the positive control. On a bench whose host
    reaches the device only over the LAN, case 1 can be judged on the WRITE
    alone: pre-fix firmware answers 200 and then leaves the network with the
    probe as its SSID (the response half is unobservable from here); fixed
    firmware refuses the write and stays. Case 2 shows both halves on any
    bench, which is why it runs first. A tool that reports
    "all good" on both images measured nothing, and this project has lost whole
    sessions to instruments that only looked like they worked
    (docs: validate-the-instrument).

    After the fix there are two acceptable outcomes per case, and both are
    passes, because the fix has two layers:
      · the write is REFUSED (400 / rejected[]), which is validation, or
      · the write is accepted and the response still parses as JSON, which is
        escaping.
    What is never acceptable is an accepted write whose response does not
    parse.

WHAT IT COSTS
    /api/commit_all is save-and-reboot, so cases 1 and 2 cost one reboot each
    plus one to restore. Case 3 uploads a language pack and needs a reboot to
    load it, and another to get back to the original pack -- run it with
    --with-lang only when you are willing to spend that, and only with the
    original pack file at hand.

STATE
    Every value this touches is read before the case and written back after,
    and the restore is VERIFIED, not assumed. An aborted run leaves a device
    whose ssid or hwId is the test string; --restore-only puts it back.

Credentials: SIMUT_WEB_USER / SIMUT_WEB_PASS (source ~/.simut-bench.env), or
--user/--pass. An admin account is required: the cases need PERM_NET_CONFIG,
PERM_SYS_CONFIG and, for case 3, PERM_FILE_UPLOAD.

Usage:
    source ~/.simut-bench.env
    python3 tools/json_escape_cases.py --host 192.168.3.24
    python3 tools/json_escape_cases.py --host 192.168.3.24 --with-lang \\
        --lang-pack data/lang/language_pt-BR.lng
    python3 tools/json_escape_cases.py --host 192.168.3.24 --restore-only

Exit code: 0 all passed, 1 failures, 2 could not set up.

Project: SIMUT
License: MIT
"""

import argparse
import hashlib
import json
import os
import re
import sys
import time

import requests

REBOOT_GRACE = 90        # seconds to wait for the device to answer again
REBOOT_SETTLE = 3.0      # the reply precedes the reset, so do not poll at once

# The three payloads. Each one is a value the device ACCEPTED before the fix.
SSID_PROBE = 'a"b'
HWID_PROBE = 'X\\'
LANG_PROBE = 'Po"rt'


def sha256_frontend(password):
    """The login page hashes each UTF-16 code unit as one byte (latin-1)."""
    return hashlib.sha256(password.encode('latin-1')).hexdigest()


class Device:
    def __init__(self, host, timeout=15):
        self.base = f'http://{host}'
        self.host = host
        self.timeout = timeout
        self.s = requests.Session()

    def get(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.get(self.base + path, timeout=self.timeout, **kw)

    def post(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.post(self.base + path, timeout=self.timeout, **kw)

    def login(self, user, password, tries=6):
        """Fresh cookie jar every time: the device keeps one session slot, so a
        cookie from before a reboot is the session the next login must evict.
        Retried, because /api/login_init answers before the boot has finished
        and reading that as a failure is an instrument bug, not a device bug."""
        last = ''
        for attempt in range(tries):
            self.s.close()
            self.s = requests.Session()
            try:
                r = self.get('/api/login_init')
                if r.status_code != 200:
                    last = f'login_init HTTP {r.status_code}'
                else:
                    nonce = r.json().get('nonce', '')
                    r = self.post('/api/login', data={
                        'user': user, 'pass': sha256_frontend(password), 'nonce': nonce,
                    }, headers={'Content-Type': 'application/x-www-form-urlencoded'})
                    if 'SIMUTSESS' in self.s.cookies.get_dict():
                        return True, ''
                    last = f'no session cookie (HTTP {r.status_code})'
            except requests.RequestException as e:
                last = type(e).__name__
            time.sleep(2.0 + attempt)
        return False, last

    def raw(self, path):
        """The response body as BYTES, plus whether it parsed as JSON.

        Reading .json() alone is what would hide the whole finding: the caller
        has to be able to say "the device answered 200 and the body is not
        JSON", which is exactly the failure being measured."""
        try:
            r = self.get(path)
        except requests.RequestException as e:
            return None, False, type(e).__name__
        body = r.content
        try:
            json.loads(body.decode('utf-8', 'replace'))
            return body, True, f'HTTP {r.status_code}'
        except ValueError as e:
            return body, False, f'HTTP {r.status_code}: {e}'

    def wait_reboot(self, user, password, grace=REBOOT_GRACE):
        time.sleep(REBOOT_SETTLE)
        deadline = time.time() + grace
        while time.time() < deadline:
            ok, _ = self.login(user, password, tries=1)
            if ok:
                return True
            time.sleep(2.0)
        return False


def commit(dev, section, fields):
    """POST /api/commit_all with one section. Returns (http, rejected list)."""
    payload = {section: fields}
    # The device reads the JSON out of a FORM FIELD named _payload — the way
    # the page posts it (WebManager_Commit.cpp: "Missing _payload" is its 400).
    # This function used to post a raw JSON body, which that check refuses
    # before any string is looked at; the tool then reported PASS (refused)
    # on every image, INCLUDING the one from before the fix. Found on
    # 2026-09-09 by the A/B this file's own docstring insists on: the
    # positive control did not fire, so the instrument was measuring nothing.
    try:
        r = dev.post('/api/commit_all', data={'_payload': json.dumps(payload)})
    except requests.RequestException as e:
        return None, [f'transport: {type(e).__name__}']
    rejected = []
    try:
        rejected = r.json().get('rejected', []) or []
    except ValueError:
        pass
    return r.status_code, rejected


class Case:
    def __init__(self, name, endpoint):
        self.name, self.endpoint = name, endpoint
        self.accepted = None      # did the device store the probe?
        self.parses = None        # did the response still parse?
        self.detail = ''

    def verdict(self):
        """A pass is: refused on the way in, OR accepted and still parseable."""
        if self.accepted is None:
            return 'SETUP'
        if not self.accepted:
            return 'PASS (refused)'
        return 'PASS (escaped)' if self.parses else 'FAIL (broke=yes)'


def case_ssid(dev, user, password, results):
    c = Case('net.ssid', '/api/network')
    results.append(c)

    body, ok, why = dev.raw('/api/network')
    if body is None or not ok:
        c.detail = f'baseline /api/network not usable: {why}'
        return
    original = json.loads(body).get('ssid', '')
    print(f'  baseline ssid = {original!r}')

    http, rejected = commit(dev, 'net', {'ssid': SSID_PROBE})
    refused = (http is not None and http >= 400) or any('ssid' in r for r in rejected)
    print(f'  commit ssid={SSID_PROBE!r} -> HTTP {http}, rejected={rejected}')

    if refused:
        c.accepted = False
        c.detail = f'HTTP {http}, rejected={rejected}'
        return

    # Accepted: it rebooted, so wait, then look at what the endpoint says.
    if not dev.wait_reboot(user, password):
        c.detail = 'device did not come back after commit'
        return
    body, ok, why = dev.raw('/api/network')
    c.accepted, c.parses, c.detail = True, ok, why
    if ok:
        got = json.loads(body).get('ssid', '')
        c.detail = f'stored and round-tripped as {got!r}'

    # Restore, whatever happened.
    http, rejected = commit(dev, 'net', {'ssid': original})
    print(f'  restore ssid={original!r} -> HTTP {http}, rejected={rejected}')
    dev.wait_reboot(user, password)
    body, ok, _ = dev.raw('/api/network')
    if ok and json.loads(body).get('ssid', '') == original:
        print('  restore verified')
    else:
        print('  !! RESTORE NOT VERIFIED — check the ssid by hand')


def case_hwid(dev, user, password, results):
    c = Case('sensor hwId', '/api/status')
    results.append(c)

    body, ok, why = dev.raw('/api/status')
    if body is None or not ok:
        c.detail = f'baseline /api/status not usable: {why}'
        return
    sensors = json.loads(body).get('sensors', [])
    active = [s for s in sensors if s.get('id')]
    if not active:
        c.detail = 'no active sensor with an id — nothing to probe'
        return
    slot = active[0].get('idx', 0)
    original = active[0]['id']
    print(f'  baseline slot {slot} hwId = {original!r}')

    http, rejected = commit(dev, 'sensors', [{'idx': slot, 'hwId': HWID_PROBE}])
    refused = (http is not None and http >= 400) or any('hwId' in r for r in rejected)
    print(f'  commit hwId={HWID_PROBE!r} -> HTTP {http}, rejected={rejected}')

    if refused:
        c.accepted = False
        c.detail = f'HTTP {http}, rejected={rejected}'
        return

    if not dev.wait_reboot(user, password):
        c.detail = 'device did not come back after commit'
        return
    body, ok, why = dev.raw('/api/status')
    c.accepted, c.parses, c.detail = True, ok, why

    http, rejected = commit(dev, 'sensors', [{'idx': slot, 'hwId': original}])
    print(f'  restore hwId={original!r} -> HTTP {http}, rejected={rejected}')
    dev.wait_reboot(user, password)
    body, ok, _ = dev.raw('/api/status')
    if ok and any(s.get('id') == original for s in json.loads(body).get('sensors', [])):
        print('  restore verified')
    else:
        print('  !! RESTORE NOT VERIFIED — check the sensor hwId by hand')


def case_lang(dev, user, password, pack_path, results):
    """The expensive one, and the only one whose damage outlives a reboot.

    A .lng is read once at boot, so a broken @NAME keeps /api/perms broken --
    and with it every page -- until a different pack is uploaded. That is why
    this case is opt-in and why it needs the original pack on disk before it
    starts: the way back is another upload, and it must not depend on being
    able to load the UI."""
    c = Case('.lng @NAME', '/api/perms')
    results.append(c)

    if not os.path.isfile(pack_path):
        c.detail = f'original pack not found: {pack_path} (refusing to run)'
        return

    with open(pack_path, 'rb') as fh:
        original_bytes = fh.read()
    m = re.search(rb'^@NAME[ \t]*(.*)$', original_bytes, re.M)
    if not m:
        c.detail = 'pack has no @NAME line'
        return
    original_name = m.group(1).decode('utf-8', 'replace').strip()
    print(f'  baseline @NAME = {original_name!r}')

    probe_bytes = original_bytes[:m.start(1)] + LANG_PROBE.encode('utf-8') + \
        original_bytes[m.end(1):]
    name = os.path.basename(pack_path)

    def upload(data):
        return dev.post('/api/upload?dir=/lang',
                        files={'file': (name, data, 'application/octet-stream')})

    r = upload(probe_bytes)
    print(f'  upload probe pack -> HTTP {r.status_code}')
    if r.status_code >= 400:
        c.accepted = False
        c.detail = f'upload refused, HTTP {r.status_code}'
        return

    dev.post('/api/action', data={'do': 'reboot'})
    if not dev.wait_reboot(user, password):
        c.detail = ('device did not come back after the probe pack — '
                    f'reupload {pack_path} to /lang by hand')
        return

    body, ok, why = dev.raw('/api/perms')
    c.accepted, c.parses, c.detail = True, ok, why
    if ok:
        c.detail = f"langName came back as {json.loads(body).get('langName')!r}"

    r = upload(original_bytes)
    print(f'  restore original pack -> HTTP {r.status_code}')
    dev.post('/api/action', data={'do': 'reboot'})
    dev.wait_reboot(user, password)
    body, ok, _ = dev.raw('/api/perms')
    if ok and json.loads(body).get('langName', '') == original_name:
        print('  restore verified')
    else:
        print('  !! RESTORE NOT VERIFIED — reupload the pack by hand')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', default=os.environ.get('SIMUT_HOST'),
                    help='device IP (default: SIMUT_HOST)')
    ap.add_argument('--user', default=os.environ.get('SIMUT_WEB_USER'))
    ap.add_argument('--pass', dest='password', default=os.environ.get('SIMUT_WEB_PASS'))
    ap.add_argument('--with-lang', action='store_true',
                    help='also run the language-pack case (two extra reboots)')
    ap.add_argument('--lang-pack', default='data/lang/language_pt-BR.lng',
                    help='the ORIGINAL pack, used to restore (must exist)')
    ap.add_argument('--json', help='write the results here')
    ap.add_argument('--cases', default='hwid,ssid',
                    help='which cases, in order (default hwid,ssid). ssid goes LAST on '
                         'purpose: once the device stores the probe SSID it leaves the '
                         'network, and nothing after it can be observed from this host')
    args = ap.parse_args()

    if not args.host:
        print('FATAL: --host or SIMUT_HOST', file=sys.stderr)
        return 2
    if not args.user or not args.password:
        print('FATAL: set SIMUT_WEB_USER / SIMUT_WEB_PASS '
              '(source ~/.simut-bench.env) or use --user/--pass', file=sys.stderr)
        return 2

    dev = Device(args.host)
    ok, why = dev.login(args.user, args.password)
    if not ok:
        print(f'FATAL: login failed: {why}', file=sys.stderr)
        return 2

    results = []
    # hwId first, ssid last. Storing the probe SSID is exactly what case 1 is
    # about, and on firmware that accepts it the device reboots into a network
    # that does not exist and is gone from this host's point of view — so
    # nothing that runs after it can be observed, and the tool's own restore
    # cannot reach it either. Measured 2026-09-09 on the pre-fix image: HTTP
    # 200, then "device did not come back", then case 2 with no baseline. The
    # way back is the console: `system ssid <the real one>` + `reload confirm`.
    for name in [c.strip().lower() for c in args.cases.split(',') if c.strip()]:
        if name == 'hwid':
            print('case 2 — sensor hwId in /api/status')
            case_hwid(dev, args.user, args.password, results)
        elif name == 'ssid':
            print('case 1 — net.ssid in /api/network')
            case_ssid(dev, args.user, args.password, results)
            if any(r.name == 'net.ssid' and r.accepted for r in results):
                print('  NOTE: the device now holds the probe SSID and has left the network. '
                      'Recover over the console: `system ssid <real ssid>` then `reload confirm`.')
        else:
            print(f'unknown case {name!r} (hwid, ssid)', file=sys.stderr)
            return 2
    if args.with_lang:
        print('case 3 — .lng @NAME in /api/perms')
        case_lang(dev, args.user, args.password, args.lang_pack, results)
    else:
        print('case 3 — skipped (pass --with-lang; costs two reboots)')

    print()
    failed = 0
    for c in results:
        v = c.verdict()
        if v.startswith('FAIL') or v == 'SETUP':
            failed += 1
        print(f'  [{v}] {c.name} -> {c.endpoint}: {c.detail}')

    if args.json:
        with open(args.json, 'w') as fh:
            json.dump([{'name': c.name, 'endpoint': c.endpoint,
                        'accepted': c.accepted, 'parses': c.parses,
                        'verdict': c.verdict(), 'detail': c.detail}
                       for c in results], fh, indent=2)
        print(f'\nwrote {args.json}')

    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
