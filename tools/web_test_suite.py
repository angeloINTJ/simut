#!/usr/bin/env python3
"""
End-to-end test suite for the SIMUT web interface.

Exercises the real device over HTTP the way a browser does: fetches the login
nonce, hashes the password client-side exactly as the page's JS does, posts the
form, carries the session cookie, and then walks every route.

Three things it checks that a plain "does it return 200" suite would not:

  * **Served JavaScript actually parses.** Each page is fetched, gunzipped, its
    <script> blocks extracted and run through `node --check`. This is the class
    of bug that shipped /history with 13 KB of JS eaten by the minifier, and it
    is invisible to a status-code check.
  * **Authorization is enforced, not just advertised.** Every protected route is
    requested with no session at all, and again with a session whose permissions
    are deliberately insufficient. A route that answers 200 to either is a hole.
  * **Chunked replies arrive whole.** Several endpoints stream through safeSend
    and abort mid-body when the handler is starved, leaving JSON that cannot
    parse. Each JSON endpoint is parsed, not merely counted.

Credentials: by default the suite creates a temporary user over the serial CLI
and deletes it afterwards. That user gets DASHBOARD|HISTORY|CALIB — enough to
prove the session works and to make the permission-boundary tests meaningful,
but not enough for admin routes, which are then reported as skipped. Export
SIMUT_WEB_USER / SIMUT_WEB_PASS to run the full sweep as an existing account.

Usage:
    python3 tools/web_test_suite.py [--host IP] [--keep-user] [--verbose]

Exit code: 0 all passed, 1 failures, 2 could not set up.

Project: SIMUT
License: MIT
"""

import argparse
import glob
import gzip
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import time

import requests
import serial

BAUD = 115200
PROMPT = re.compile(r'SIMUT(?:\([a-z0-9-]+\))?\s*[#>]\s*$')

TEST_USER = 'wtest'
TEST_PASS = 'Wt3st!suite#2026'

# route, method, permission needed by a plain logged-in DASHBOARD|HISTORY|CALIB
# user. 'open' = no session required.
PAGES = [
    ('/',          'dash'),
    ('/history',   'hist'),
    ('/alarms',    'admin'),
    ('/config',    'admin'),
    ('/network',   'admin'),
    ('/users',     'admin'),
    ('/files',     'admin'),
    ('/license',   'dash'),
    ('/login',     'open'),
]

JSON_APIS = [
    ('/api/status',        'dash'),
    ('/api/perms',         'dash'),
    ('/api/themes',        'dash'),
    ('/api/history_days',  'hist'),
    ('/api/calib',         'calib'),
    ('/api/config',        'admin'),
    ('/api/users',         'admin'),
    ('/api/network',       'admin'),
    ('/api/alarms',        'admin'),
    ('/api/sec_status',    'admin'),
]

# Not JSON: raw CompactLogRecord stream, 12 bytes per entry, sent as
# application/octet-stream because translating server-side cost ~10x the bytes.
BINARY_APIS = [
    ('/api/logs', 'admin', 12),
]

STATIC = ['/lang.js', '/style.css', '/favicon.ico']


class Result:
    def __init__(self):
        self.rows = []

    def add(self, group, name, ok, detail='', skipped=False):
        self.rows.append((group, name, ok, detail, skipped))
        mark = 'SKIP' if skipped else ('PASS' if ok else 'FAIL')
        line = f'  [{mark}] {name}'
        if detail:
            line += f' — {detail}'
        print(line, flush=True)

    @property
    def failed(self):
        return [r for r in self.rows if not r[2] and not r[4]]

    @property
    def passed(self):
        return [r for r in self.rows if r[2] and not r[4]]

    @property
    def skipped(self):
        return [r for r in self.rows if r[4]]


# --------------------------------------------------------------------------
# serial side: find the device, create/remove the throwaway user
# --------------------------------------------------------------------------

def find_target():
    """The SIMUT board is a Pico W; a PicoHand fixture is a plain Pico."""
    for port in sorted(glob.glob('/dev/ttyACM*')):
        try:
            out = subprocess.check_output(
                ['udevadm', 'info', '-q', 'property', '-n', port], text=True)
        except Exception:
            continue
        if 'ID_MODEL=Pico_W' in out:
            return port
    return None


def cli(port, commands, settle=0.4):
    """Run CLI commands, return the whole transcript."""
    ser = serial.Serial(port, BAUD, timeout=1)
    ser.dtr = True
    time.sleep(1.4)
    ser.reset_input_buffer()
    transcript = ''
    for c in ['', 'end', 'enable'] + commands:
        ser.write((c + '\r\n').encode())
        buf = b''
        deadline = time.time() + 12
        while time.time() < deadline:
            chunk = ser.read(512)
            if chunk:
                buf += chunk
                if PROMPT.search(buf.decode('utf-8', 'replace').rstrip()):
                    break
            else:
                time.sleep(0.02)
        transcript += buf.decode('utf-8', 'replace')
        time.sleep(settle)
    ser.close()
    return transcript


def device_ip(port):
    out = cli(port, ['show net status'])
    m = re.search(r'IP:\s*(\d+\.\d+\.\d+\.\d+)', out)
    return m.group(1) if m else None


def create_test_user(port):
    """Create the throwaway user, deleting any leftover first.

    Deleting first is not tidiness. The device config lives in LittleFS and
    survives a firmware flash, so a `wtest` left behind by an earlier run
    persists — and `user add` then answers "user already exists". Accepting
    that as success silently reuses an account whose stored hash may predate
    whatever is being tested, which is exactly how this suite once reported a
    working login as broken.
    """
    cli(port, ['configure terminal', f'user del {TEST_USER}',
               'end', 'write memory'], settle=0.6)
    out = cli(port, ['configure terminal',
                     f'user add {TEST_USER} {TEST_PASS}',
                     'end', 'write memory'], settle=0.8)
    return 'usuario criado' in out.lower() or 'user created' in out.lower()


def grant_admin(port):
    """Promote the throwaway user to full admin via `user perm`.

    Without this the suite can only reach dashboard/history/calibration routes,
    because `user add` hardcodes those permissions. The alternative — resetting
    the real admin password to borrow the account — changes the operator's
    credentials and arms mustChangePassword, which redirects the session to
    /force_chpass and breaks the authenticated tests anyway.
    """
    out = cli(port, ['configure terminal',
                     f'user perm {TEST_USER} admin',
                     'end', 'write memory'], settle=0.8)
    return 'permiss' in out.lower()


def revoke_admin(port):
    cli(port, ['configure terminal', f'user perm {TEST_USER} operator',
               'end', 'write memory'], settle=0.6)


def delete_test_user(port):
    cli(port, ['configure terminal', f'user del {TEST_USER}',
               'end', 'write memory'], settle=0.8)


# --------------------------------------------------------------------------
# http side
# --------------------------------------------------------------------------

def sha256_frontend(password):
    """Replicate the login page's sha256().

    Its inner loop reads charCodeAt(i) and bails on anything above 0xFF, so it
    hashes each UTF-16 code unit as one byte — latin-1, not UTF-8. Encoding any
    other way silently produces a different digest for non-ASCII passwords.
    """
    return hashlib.sha256(password.encode('latin-1')).hexdigest()


class Web:
    def __init__(self, host, timeout=15):
        self.base = f'http://{host}'
        self.s = requests.Session()
        self.timeout = timeout

    def get(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.get(self.base + path, timeout=self.timeout, **kw)

    def post(self, path, **kw):
        kw.setdefault('allow_redirects', False)
        return self.s.post(self.base + path, timeout=self.timeout, **kw)

    def logout(self):
        try:
            self.get('/logout')
        except Exception:
            pass
        self.s.cookies.clear()

    def wait_lockout(self, cap=30):
        """Sit out the brute-force lockout.

        A failed attempt arms a short lockout, so a suite that tests a wrong
        password and then the right one locks itself out and misreads working
        auth as broken. login_init reports the remaining seconds.
        """
        waited = 0
        while waited < cap:
            try:
                j = self.get('/api/login_init').json()
            except Exception:
                return waited
            sec = int(j.get('lockSec') or 0)
            if not j.get('locked') and sec <= 0:
                return waited
            nap = min(sec + 1, cap - waited)
            time.sleep(nap)
            waited += nap
        return waited

    def login(self, user, password):
        r = self.get('/api/login_init')
        if r.status_code != 200:
            return False, f'login_init HTTP {r.status_code}'
        try:
            nonce = r.json().get('nonce', '')
        except Exception as e:
            return False, f'login_init not JSON: {e}'

        r = self.post('/api/login', data={
            'user': user,
            'pass': sha256_frontend(password),
            'nonce': nonce,
        }, headers={'Content-Type': 'application/x-www-form-urlencoded'})

        if r.status_code not in (200, 302):
            return False, f'login HTTP {r.status_code}: {r.text[:120]}'
        if 'SIMUTSESS' not in self.s.cookies.get_dict():
            return False, f'no session cookie (HTTP {r.status_code})'
        return True, self.s.cookies.get_dict()['SIMUTSESS'][:8] + '…'


def extract_scripts(html):
    return [s for s in re.findall(r'<script[^>]*>(.*?)</script>', html, re.S)
            if s.strip()]


def node_check(js):
    with tempfile.NamedTemporaryFile('w', suffix='.js', delete=False) as fh:
        fh.write(js)
        path = fh.name
    try:
        p = subprocess.run(['node', '--check', path],
                           capture_output=True, text=True, timeout=30)
        return p.returncode == 0, (p.stderr.strip().splitlines() or [''])[0][:160]
    finally:
        os.unlink(path)


# --------------------------------------------------------------------------
# test groups
# --------------------------------------------------------------------------

def t_reachability(web, res):
    print('\n[1] Alcance e conteudo estatico')
    for path in STATIC:
        try:
            r = web.get(path)
            # 204 is a legitimate answer for an asset the device declines to
            # serve — the favicon route does exactly that. Only a body that
            # claims 200 and arrives empty is a real fault.
            ok = (r.status_code == 200 and len(r.content) > 0) or r.status_code == 204
            res.add('static', f'GET {path}', ok,
                    f'HTTP {r.status_code}, {len(r.content)} B')
        except Exception as e:
            res.add('static', f'GET {path}', False, str(e)[:100])

    try:
        r = web.get('/lang.js')
        enc = r.headers.get('Content-Encoding', '')
        res.add('static', '/lang.js servido gzipado', enc == 'gzip',
                f"Content-Encoding={enc or 'ausente'}")
    except Exception as e:
        res.add('static', '/lang.js servido gzipado', False, str(e)[:100])


def t_unauthenticated(web, res):
    print('\n[2] Sem sessao: paginas redirecionam, APIs negam')
    web.logout()
    for path, need in PAGES:
        if need == 'open':
            continue
        try:
            r = web.get(path)
            ok = r.status_code in (302, 401, 403)
            res.add('authz', f'{path} bloqueado sem sessao', ok, f'HTTP {r.status_code}')
        except Exception as e:
            res.add('authz', f'{path} bloqueado sem sessao', False, str(e)[:100])

    for path, need in JSON_APIS + [(p, n) for p, n, _ in BINARY_APIS]:
        try:
            r = web.get(path)
            ok = r.status_code in (401, 403)
            res.add('authz', f'{path} negado sem sessao', ok, f'HTTP {r.status_code}')
        except Exception as e:
            res.add('authz', f'{path} negado sem sessao', False, str(e)[:100])


def t_login(web, res, user, password):
    print('\n[3] Fluxo de login')
    web.logout()

    try:
        r = web.get('/api/login_init')
        j = r.json()
        res.add('login', 'login_init entrega nonce', bool(j.get('nonce')),
                f"nonce={str(j.get('nonce'))[:12]}…")
    except Exception as e:
        res.add('login', 'login_init entrega nonce', False, str(e)[:100])

    # Wrong password must not open a session.
    web.wait_lockout()
    ok, detail = web.login(user, password + 'X')
    res.add('login', 'senha errada e rejeitada', not ok,
            'sessao aberta indevidamente!' if ok else detail)
    web.logout()

    # That failure arms the lockout — proof the brute-force guard works, and a
    # trap for the next step if we do not sit it out.
    waited = web.wait_lockout()
    res.add('login', 'lockout de forca bruta e aplicado', waited > 0,
            f'esperou {waited}s' if waited else 'nenhum lockout apos senha errada')

    ok, detail = web.login(user, password)
    res.add('login', 'senha correta abre sessao', ok, detail)
    return ok


def t_pages_authenticated(web, res, is_admin):
    print('\n[4] Paginas com sessao — status, gzip e sintaxe do JS servido')
    for path, need in PAGES:
        if need == 'admin' and not is_admin:
            res.add('pages', f'{path}', True, 'requer admin', skipped=True)
            continue
        try:
            r = web.get(path)
            if r.status_code != 200:
                res.add('pages', f'{path} carrega', False, f'HTTP {r.status_code}')
                continue
            html = r.text
            res.add('pages', f'{path} carrega', len(html) > 200,
                    f'HTTP 200, {len(html)} B')

            scripts = extract_scripts(html)
            if not scripts:
                continue
            biggest = max(scripts, key=len)
            ok, err = node_check(biggest)
            res.add('pages', f'{path} JS servido parseia', ok,
                    f'{len(biggest)} B' + ('' if ok else f' — {err}'))
        except Exception as e:
            res.add('pages', f'{path} carrega', False, str(e)[:100])


def t_apis_authenticated(web, res, is_admin):
    print('\n[5] APIs com sessao — JSON completo e parseavel')
    for path, need in JSON_APIS:
        if need == 'admin' and not is_admin:
            continue
        try:
            r = web.get(path)
            if r.status_code != 200:
                res.add('api', f'{path} responde 200', False, f'HTTP {r.status_code}')
                continue
            try:
                data = r.json()
            except Exception as e:
                # The failure mode that broke /config: body cut mid-stream.
                res.add('api', f'{path} JSON integro', False,
                        f'{len(r.content)} B, parse falhou: {str(e)[:70]}')
                continue
            res.add('api', f'{path} JSON integro', True,
                    f'{len(r.content)} B, {type(data).__name__}')
        except Exception as e:
            res.add('api', f'{path} responde 200', False, str(e)[:100])


def t_binary_apis(web, res, is_admin):
    for path, need, unit in BINARY_APIS:
        if need == 'admin' and not is_admin:
            continue
        try:
            r = web.get(path)
            if r.status_code != 200:
                res.add('api', f'{path} responde 200', False, f'HTTP {r.status_code}')
                continue
            n = len(r.content)
            ctype = r.headers.get('Content-Type', '')
            # A stream cut mid-record leaves a length that is not a whole
            # number of entries — the binary equivalent of unparseable JSON.
            ok = n > 0 and n % unit == 0
            res.add('api', f'{path} stream binario integro', ok,
                    f'{n} B = {n // unit} registros de {unit} B, {ctype}'
                    if ok else f'{n} B nao e multiplo de {unit}')
        except Exception as e:
            res.add('api', f'{path} responde 200', False, str(e)[:100])


def t_permission_boundary(web, res, is_admin):
    print('\n[6] Limite de permissao com sessao valida')
    if is_admin:
        res.add('authz', 'rotas admin negadas a usuario comum', True,
                'sessao e admin — precisa do usuario de teste', skipped=True)
        return
    for path, need in JSON_APIS + [(p, n) for p, n, _ in BINARY_APIS]:
        if need != 'admin':
            continue
        try:
            r = web.get(path)
            ok = r.status_code in (401, 403)
            res.add('authz', f'{path} negado a usuario sem permissao', ok,
                    f'HTTP {r.status_code}')
        except Exception as e:
            res.add('authz', f'{path} negado a usuario sem permissao', False, str(e)[:100])


def t_config_regression(web, res, is_admin):
    print('\n[7] Regressao especifica da pagina /config')
    if not is_admin:
        res.add('regress', '/config traz o tratamento de falha', True,
                'requer admin', skipped=True)
        return
    try:
        html = web.get('/config').text
        for token, label in (('loadConfig', 'loader presente'),
                             ('applyConfig', 'preenchimento separado do transporte'),
                             ('cfg_load_err', 'aviso visivel de falha'),
                             ('setFormEnabled', 'form desabilitado em falha')):
            res.add('regress', f'/config: {label}', token in html,
                    f"'{token}' {'encontrado' if token in html else 'AUSENTE'}")
    except Exception as e:
        res.add('regress', '/config traz o tratamento de falha', False, str(e)[:100])


def t_logout(web, res):
    print('\n[8] Logout invalida a sessao')
    try:
        web.get('/logout')
        r = web.get('/')
        ok = r.status_code in (302, 401, 403)
        res.add('login', 'sessao invalidada apos logout', ok, f'HTTP {r.status_code}')
    except Exception as e:
        res.add('login', 'sessao invalidada apos logout', False, str(e)[:100])


def t_calib_shape(web, res):
    """GET /api/calib: every channel entry must carry the point-editor fields.

    raw feeds the capture button, min/max echo the bounds the POST enforces,
    pts is the stored curve (<=5 [raw,ref] pairs). A missing field here is the
    page silently losing a feature, not a cosmetic problem."""
    print('\n[5b] /api/calib — forma dos canais (raw/min/max/pts)')
    try:
        r = web.get('/api/calib')
        if r.status_code != 200:
            res.add('calib', 'GET /api/calib responde 200', False, f'HTTP {r.status_code}')
            return
        j = r.json()
        sensors = j.get('sensors', [])
        if not sensors:
            res.add('calib', 'channels[] com raw/min/max/pts', True,
                    'sem sensores ativos', skipped=True)
            return
        ok_all, detail, n_ch = True, '', 0
        for s in sensors:
            for ch in s.get('channels', []):
                n_ch += 1
                missing = [k for k in ('key', 'read', 'raw', 'offset', 'min', 'max', 'mode', 'pts')
                           if k not in ch]
                if missing:
                    ok_all = False
                    detail = f"slot {s.get('slot')} {ch.get('key')}: faltam {missing}"
                    break
                pts = ch['pts']
                if (not isinstance(pts, list) or len(pts) > 5
                        or any(not (isinstance(p, list) and len(p) == 2) for p in pts)):
                    ok_all = False
                    detail = f"slot {s.get('slot')} {ch.get('key')}: pts invalido: {pts!r}"
                    break
                if not (isinstance(ch['min'], (int, float)) and isinstance(ch['max'], (int, float))
                        and ch['min'] < ch['max']):
                    ok_all = False
                    detail = f"slot {s.get('slot')} {ch.get('key')}: faixa {ch['min']}..{ch['max']}"
                    break
            if not ok_all:
                break
        res.add('calib', 'channels[] com raw/min/max/pts', ok_all,
                detail or f'{n_ch} canais verificados')
    except Exception as e:
        res.add('calib', 'GET /api/calib forma', False, str(e)[:100])


def t_calib_rw(web, res):
    """Opt-in (--calib-rw): mutating round-trip on POST /api/calib.

    Writes flash, bumps the calibration version and briefly applies a +0.5
    correction to one live channel, then clears it. Only channels with NO
    correction at all are eligible: a restore is not guaranteed to run (a
    Save & Restart from another browser mid-run reboots the device and kills
    the session — it happened), so the test must never touch a channel that
    carries real user calibration."""
    print('\n[5c] /api/calib — escrita opt-in (--calib-rw)')

    def post(payload):
        return web.post('/api/calib', json=payload)

    try:
        j = web.get('/api/calib').json()
    except Exception as e:
        res.add('calibrw', 'GET inicial', False, str(e)[:100])
        return
    if j.get('ntp') is False:
        res.add('calibrw', 'round-trip de pontos', True,
                'NTP fora do ar — POST responderia 503', skipped=True)
        return

    target = None
    for s in j.get('sensors', []):
        for ch in s.get('channels', []):
            untouched = (not ch.get('pts')) and abs(ch.get('offset') or 0.0) < 0.005
            if ch.get('raw') is not None and untouched and ch['raw'] + 1.0 < ch['max']:
                target = (s, ch)
                break
        if target:
            break
    if not target:
        res.add('calibrw', 'round-trip de pontos', True,
                'nenhum canal sem correcao com leitura ao vivo', skipped=True)
        return

    s, ch = target
    slot, key, raw, prev_pts = s['slot'], ch['key'], ch['raw'], ch['pts']
    print(f"  alvo: slot {slot} {key} (raw {raw}, pts previos {prev_pts})")

    # 1. Set a one-point +0.5 correction anchored at the current raw.
    r = post({'sensors': [{'slot': slot, 'cal': {key: [[raw, round(raw + 0.5, 2)]]}}]})
    okd = {}
    try:
        okd = r.json()
    except Exception:
        pass
    res.add('calibrw', 'POST 1 ponto aceita', r.status_code == 200 and okd.get('ok') is True,
            f"HTTP {r.status_code} {str(okd)[:60]}")
    if r.status_code != 200:
        return
    v1 = okd.get('version', 0)

    # 2. An immediate second POST must trip the 5 s rate limit.
    r = post({'sensors': [{'slot': slot, 'cal': {key: [[raw, round(raw + 0.5, 2)]]}}]})
    res.add('calibrw', 'POST imediato leva 429', r.status_code == 429, f'HTTP {r.status_code}')

    time.sleep(6)

    # 3. Round-trip: the stored pts and the applied correction come back.
    try:
        j = web.get('/api/calib').json()
        ch2 = next(c for sn in j['sensors'] if sn['slot'] == slot
                   for c in sn['channels'] if c['key'] == key)
        pts_ok = (len(ch2['pts']) == 1 and abs(ch2['pts'][0][0] - raw) < 0.02
                  and abs(ch2['pts'][0][1] - (raw + 0.5)) < 0.02)
        res.add('calibrw', 'pts persistiu no GET', pts_ok, f"pts={ch2['pts']}")
        applied = (ch2['read'] is not None and ch2['raw'] is not None
                   and abs((ch2['read'] - ch2['raw']) - 0.5) < 0.02)
        res.add('calibrw', 'correcao aplicada (read = raw + 0.5)', applied,
                f"raw={ch2['raw']} read={ch2['read']} offset={ch2['offset']}")
        res.add('calibrw', 'versao avancou', isinstance(v1, int) and v1 > 0
                and j.get('calibVersion', 0) >= v1, f"v={j.get('calibVersion')}")
    except Exception as e:
        res.add('calibrw', 'round-trip no GET', False, str(e)[:100])

    time.sleep(6)

    # 4. Six points: one beyond the model, and the file must not change.
    six = [[round(raw + k * 0.05, 2), round(raw + k * 0.05, 2)] for k in range(6)]
    r = post({'sensors': [{'slot': slot, 'cal': {key: six}}]})
    res.add('calibrw', 'POST 6 pontos leva 400', r.status_code == 400,
            f'HTTP {r.status_code} {r.text[:60]}')

    time.sleep(6)

    # 5. Duplicate raws collapse at two decimals and must be refused.
    r = post({'sensors': [{'slot': slot, 'cal': {key: [[raw, raw + 0.1], [raw, raw + 0.2]]}}]})
    res.add('calibrw', 'POST bruto duplicado leva 400', r.status_code == 400,
            f'HTTP {r.status_code} {r.text[:60]}')

    time.sleep(6)

    # 6. Restore what was there — an empty list IS the restore when the
    #    channel had no correction before.
    r = post({'sensors': [{'slot': slot, 'cal': {key: prev_pts}}]})
    restored = r.status_code == 200
    if restored:
        time.sleep(6)
        try:
            j = web.get('/api/calib').json()
            ch3 = next(c for sn in j['sensors'] if sn['slot'] == slot
                       for c in sn['channels'] if c['key'] == key)
            restored = (len(ch3['pts']) == len(prev_pts)
                        and all(abs(a[0] - b[0]) < 0.02 and abs(a[1] - b[1]) < 0.02
                                for a, b in zip(ch3['pts'], prev_pts)))
        except Exception:
            restored = False
    res.add('calibrw', 'estado anterior restaurado', restored,
            f'pts de volta a {prev_pts}')


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', help='IP do dispositivo (default: perguntar pela serial)')
    ap.add_argument('--keep-user', action='store_true',
                    help='nao remover o usuario de teste ao final')
    ap.add_argument('--calib-rw', action='store_true',
                    help='roda o round-trip MUTANTE de /api/calib (escreve flash, '
                         'aplica e desfaz uma correcao de +0.5 num canal ao vivo)')
    args = ap.parse_args()

    env_user = os.environ.get('SIMUT_WEB_USER')
    env_pass = os.environ.get('SIMUT_WEB_PASS')
    created = False
    port = None

    host = args.host
    if not host or not env_user:
        port = find_target()
        if not port:
            print('FATAL: alvo (Pico W) nao encontrado na serial')
            return 2
        print(f'[setup] serial do alvo: {port}')

    if not host:
        host = device_ip(port)
        if not host:
            print('FATAL: nao consegui ler o IP pela serial')
            return 2
    print(f'[setup] host: {host}')

    if env_user and env_pass:
        user, password, is_admin = env_user, env_pass, True
        print(f'[setup] usando credenciais do ambiente: {user} (assumindo admin)')
    else:
        user, password, is_admin = TEST_USER, TEST_PASS, False
        print(f'[setup] criando usuario temporario "{user}" pela CLI serial')
        if not create_test_user(port):
            print('FATAL: nao consegui criar o usuario de teste')
            return 2
        created = True
        print('[setup] usuario criado (perms: dashboard+historico+calibracao)')

    res = Result()
    web = Web(host)

    try:
        t_reachability(web, res)
        t_unauthenticated(web, res)
        if t_login(web, res, user, password):
            t_pages_authenticated(web, res, is_admin)
            t_apis_authenticated(web, res, is_admin)
            # The test user carries CALIB, so both run under-privileged too.
            t_calib_shape(web, res)
            if args.calib_rw:
                t_calib_rw(web, res)
            t_binary_apis(web, res, is_admin)
            # Run this while the session is still under-privileged: it is the
            # only moment the refusals can be proven rather than assumed.
            t_permission_boundary(web, res, is_admin)

            if not is_admin and created:
                print('\n[setup] promovendo o usuario a admin para o sweep completo')
                if grant_admin(port):
                    # Session permissions are captured at login, so the old
                    # cookie still carries the old rights — log in again.
                    web.logout()
                    web.wait_lockout()
                    ok, detail = web.login(user, password)
                    res.add('login', 'reautentica como admin', ok, detail)
                    if ok:
                        is_admin = True
                        t_pages_authenticated(web, res, True)
                        t_apis_authenticated(web, res, True)
                        t_binary_apis(web, res, True)
                        t_config_regression(web, res, True)
                else:
                    res.add('login', 'promocao a admin via CLI', False,
                            "'user perm' nao confirmou")
            else:
                t_config_regression(web, res, is_admin)

            t_logout(web, res)
        else:
            print('\n!! login falhou — testes autenticados nao rodaram')
    finally:
        if created and not args.keep_user:
            print(f'\n[cleanup] removendo usuario "{user}"')
            try:
                delete_test_user(port)
            except Exception as e:
                print(f'[cleanup] AVISO: falha ao remover: {e}')

    print('\n' + '=' * 62)
    print(f'  passaram: {len(res.passed)}   falharam: {len(res.failed)}   '
          f'pulados: {len(res.skipped)}')
    if res.failed:
        print('\n  FALHAS:')
        for _, name, _, detail, _ in res.failed:
            print(f'    - {name}: {detail}')
    if not is_admin:
        print('\n  Rotas admin ficaram de fora. Para o sweep completo:')
        print('    SIMUT_WEB_USER=admin SIMUT_WEB_PASS=... python3 tools/web_test_suite.py')
    print('=' * 62)
    return 1 if res.failed else 0


sys.exit(main())
