#!/usr/bin/env python3
"""
Boolean round-trip cases for POST /api/commit_all, run against the real device.

Every boolean the config API accepts is written here in each of the spellings a
real client uses, and then read back from the endpoint that publishes it. The
bug this exists for: four `sys` fields and two `net` fields read their booleans
as `getNum(k) != "0"`, so every spelling that was not the literal `0` meant
TRUE — including the `false` that this device's own GET /api/config emits. Fetch
the config, flip one field, post it back, and `t_sec`, `log`, `m_retain` and
`ntp_enabled` all came back ON, with no boolean spelling able to turn any of
them off.

That makes the natural API workflow — GET, edit, POST — the failing case, which
is why the browser never saw it: the page's forms emit 1/0.

WHY IT IS AN A/B AND NOT A CHECKLIST
    Run this against firmware that still carries the bug and the `literal`
    cases must FAIL; that failure is the positive control. A suite that passes
    on both images is measuring nothing, and this project has burned whole
    sessions on instruments that only looked like they worked. The `numeric`
    cases are the A-against-A control in the other direction: they exercise the
    same path with the spelling the page uses and must pass on BOTH images —
    if they ever fail, the fix broke the browser, not the parser.

WHAT IT COSTS
    /api/commit_all is save-and-reboot, so every case costs one reboot and one
    fresh login. The cases are batched by spelling (all four sys booleans in one
    payload) to keep that count at seven.

WHAT IT DELIBERATELY DOES NOT TEST ON HARDWARE
    `use_dhcp: false`. Committing it moves the device onto its static address,
    and getting that wrong on a remote bench loses the bench. It is covered by
    the native tests (`pio test -e native`), where it costs nothing to be wrong.

STATE
    The current value of every field it touches is read before the first case
    and written back at the end using the NUMERIC spelling, which both images
    understand — so an aborted run against buggy firmware still restores. The
    restore is verified and reported; it is not assumed.

Credentials: SIMUT_WEB_USER / SIMUT_WEB_PASS, or --user/--pass. An admin
account is required (PERM_SYS_CONFIG); the throwaway user web_test_suite.py
creates does not carry it.

Usage:
    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... \\
        python3 tools/commit_bool_cases.py --host 192.168.3.24 [--json out.json]

Exit code: 0 all passed, 1 failures, 2 could not set up.

Project: SIMUT
License: MIT
"""

import argparse
import hashlib
import json
import os
import sys
import time

import requests

# The four sys booleans that read through the same field reader, and the one
# net boolean that is safe to flip on a bench reached over the network.
SYS_FLAGS = ['log', 't_sec', 'm_retain', 'ntp_enabled']
NET_FLAGS = ['dns_auto']

REBOOT_GRACE = 90        # seconds to wait for the device to answer again
REBOOT_SETTLE = 3.0      # seconds before the first poll — the reply precedes the reset


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
        """Fresh cookie jar every time: the device keeps ONE session slot, so a
        stale cookie from before a reboot is not merely useless, it is the
        session the next login would have to evict.

        Retried, because /api/login_init starts answering before the boot is
        finished: the first run of this tool took a RST on the POST that
        followed a 200 on the GET, and read a device that was merely still
        coming up as a device that had failed. Waiting on the wrong signal is
        an instrument bug, not a device bug.
        """
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
                    last = f'sem cookie de sessao (HTTP {r.status_code})'
            except requests.RequestException as e:
                last = type(e).__name__
            time.sleep(2.0 + attempt)
        return False, last

    def flags(self, tries=3):
        """Every boolean this tool touches, as the device reports it."""
        last = None
        for attempt in range(tries):
            try:
                out = {}
                cfg = self.get('/api/config').json()
                for k in SYS_FLAGS:
                    out[k] = cfg.get(k)
                net = self.get('/api/network').json()
                for k in NET_FLAGS:
                    out[k] = net.get(k)
                return out
            except (requests.RequestException, ValueError) as e:
                last = e
                time.sleep(1.5 + attempt)
        raise last

    def commit(self, payload):
        """POST /api/commit_all and wait out the reboot it schedules."""
        body = payload if isinstance(payload, str) else json.dumps(payload)
        try:
            r = self.post('/api/commit_all', data={'_payload': body},
                          headers={'Content-Type': 'application/x-www-form-urlencoded'})
        except requests.RequestException as e:
            return None, f'POST falhou: {e}'
        if r.status_code != 200:
            return None, f'HTTP {r.status_code}: {r.text[:120]}'
        try:
            return r.json(), ''
        except ValueError:
            return None, f'resposta nao e JSON: {r.text[:120]}'

    def wait_reboot(self):
        started = time.time()
        time.sleep(REBOOT_SETTLE)
        while time.time() - started < REBOOT_GRACE:
            try:
                probe = requests.get(f'{self.base}/api/login_init', timeout=4)
                if probe.status_code == 200 and 'nonce' in probe.text:
                    time.sleep(2.0)   # answering is not the same as ready
                    return True, round(time.time() - started, 1)
            except requests.RequestException:
                pass
            time.sleep(1.0)
        return False, REBOOT_GRACE


class Result:
    def __init__(self):
        self.rows = []

    def add(self, case, name, ok, detail=''):
        self.rows.append({'case': case, 'name': name, 'ok': bool(ok), 'detail': detail})
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f' — {detail}' if detail else ''),
              flush=True)

    @property
    def failed(self):
        return [r for r in self.rows if not r['ok']]


def sys_payload(spelling, value, spaced=False):
    """One payload carrying all four sys booleans in the given spelling.

    `spaced` writes the shape json.dumps produces by default — a space after
    every colon. That is not cosmetic: the `net` copy of the field reader never
    learned to skip it, and read `{"use_dhcp": 0}` as the token " 0".
    """
    lit = {'literal': ('true' if value else 'false'),
           'numeric': ('1' if value else '0')}[spelling]
    sep = ': ' if spaced else ':'
    fields = ','.join(f'"{k}"{sep}{lit}' for k in SYS_FLAGS)
    return '{"sys"' + sep + '{' + fields + '}}'


def check_flags(dev, res, case, expected, keys):
    try:
        got = dev.flags()
    except Exception as e:
        res.add(case, f'{case}: leitura de volta', False, str(e)[:120])
        return
    for k in keys:
        res.add(case, f'{case}: {k} = {str(expected).lower()}', got.get(k) == expected,
                f'device diz {got.get(k)!r}')


def run_case(dev, res, user, password, case, payload, expected, keys):
    print(f'\n[{case}] _payload={payload}', flush=True)
    body, err = dev.commit(payload)
    if body is None:
        res.add(case, f'{case}: commit aceito', False, err)
        return None
    rejected = body.get('rejected', [])
    res.add(case, f'{case}: commit aceito', True,
            f"rejected={rejected}" if rejected else 'sem campos rejeitados')
    ok, waited = dev.wait_reboot()
    if not ok:
        res.add(case, f'{case}: aparelho voltou', False, f'sem resposta em {waited}s')
        return body
    ok, err = dev.login(user, password)
    if not ok:
        res.add(case, f'{case}: sessao reaberta', False, err)
        return body
    check_flags(dev, res, case, expected, keys)
    return body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True, help='IP do dispositivo')
    ap.add_argument('--user', default=os.environ.get('SIMUT_WEB_USER'))
    ap.add_argument('--pass', dest='password', default=os.environ.get('SIMUT_WEB_PASS'))
    ap.add_argument('--json', help='grava o relatorio da corrida neste arquivo')
    ap.add_argument('--label', default='', help='rotulo da imagem sob teste (vai no JSON)')
    args = ap.parse_args()

    if not args.user or not args.password:
        print('FATAL: exporte SIMUT_WEB_USER/SIMUT_WEB_PASS ou use --user/--pass')
        return 2

    dev = Device(args.host)
    ok, err = dev.login(args.user, args.password)
    if not ok:
        print(f'FATAL: login falhou — {err}')
        return 2
    print(f'[setup] sessao aberta em {args.host} como {args.user}')

    try:
        original = dev.flags()
    except Exception as e:
        print(f'FATAL: nao consegui ler o estado inicial — {e}')
        return 2
    if any(v is None for v in original.values()):
        print(f'FATAL: /api/config ou /api/network nao trouxe todos os campos: {original}')
        return 2
    print(f'[setup] estado original: {original}')

    res = Result()

    # Every case below is inside the try so the restore is a `finally`: a run
    # that dies halfway leaves the bench with telemetry encryption off and
    # logging disabled, which is a worse outcome than the bug being measured.
    try:
        run_cases(dev, res, args)
    finally:
        print('\n[restauro] devolvendo o estado original com a grafia numerica')
        fields = ','.join(f'"{k}":{1 if original[k] else 0}' for k in SYS_FLAGS)
        net_fields = ','.join(f'"{k}":{1 if original[k] else 0}' for k in NET_FLAGS)
        run_case(dev, res, args.user, args.password, 'restauro',
                 '{"sys":{' + fields + '},"net":{' + net_fields + '}}', None, [])
        try:
            final = dev.flags()
        except Exception as e:
            final = {'erro': str(e)}
        restored = final == original
        res.add('restauro', 'estado original restaurado', restored,
                f'{final}' if not restored else f'{original}')

    print('\n' + '=' * 70)
    total = len(res.rows)
    print(f'{total - len(res.failed)}/{total} verificacoes passaram')
    for r in res.failed:
        print(f'  FAIL  {r["name"]} — {r["detail"]}')

    if args.json:
        with open(args.json, 'w', encoding='utf-8') as fh:
            json.dump({'host': args.host, 'label': args.label,
                       'original': original, 'final': final,
                       'rows': res.rows}, fh, indent=2, ensure_ascii=False)
        print(f'[relatorio] {args.json}')

    return 1 if res.failed else 0


def run_cases(dev, res, args):
    # ---- literal JSON booleans: the failing case, and the whole point -------
    run_case(dev, res, args.user, args.password, 'literal-false',
             sys_payload('literal', False), False, SYS_FLAGS)
    run_case(dev, res, args.user, args.password, 'literal-true',
             sys_payload('literal', True), True, SYS_FLAGS)

    # ---- json.dumps shape: literal booleans with a space after each colon ---
    run_case(dev, res, args.user, args.password, 'literal-false-espacado',
             sys_payload('literal', False, spaced=True), False, SYS_FLAGS)

    # ---- numeric: what the page sends. A/A control, must pass on both images
    run_case(dev, res, args.user, args.password, 'numerico-1',
             sys_payload('numeric', True), True, SYS_FLAGS)
    run_case(dev, res, args.user, args.password, 'numerico-0',
             sys_payload('numeric', False), False, SYS_FLAGS)

    # ---- unreadable value: keep the stored setting AND say so --------------
    print('\n[invalido] um valor que nao e booleano nao pode virar um palpite')
    before = dev.flags()
    body = run_case(dev, res, args.user, args.password, 'invalido',
                    '{"sys":{"log":2}}', before['log'], ['log'])
    if body is not None:
        res.add('invalido', 'invalido: campo aparece em "rejected"',
                'log' in body.get('rejected', []), f"rejected={body.get('rejected', [])}")

    # ---- net section: dns_auto only (see the module docstring) --------------
    run_case(dev, res, args.user, args.password, 'net-literal-false',
             '{"net":{"dns_auto":false}}', False, NET_FLAGS)


if __name__ == '__main__':
    sys.exit(main())
