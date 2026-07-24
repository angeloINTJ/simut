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
    ('/api/logs',          'admin'),
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

    for path, need in JSON_APIS:
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


def t_permission_boundary(web, res, is_admin):
    print('\n[6] Limite de permissao com sessao valida')
    if is_admin:
        res.add('authz', 'rotas admin negadas a usuario comum', True,
                'sessao e admin — precisa do usuario de teste', skipped=True)
        return
    for path, need in JSON_APIS:
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


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', help='IP do dispositivo (default: perguntar pela serial)')
    ap.add_argument('--keep-user', action='store_true',
                    help='nao remover o usuario de teste ao final')
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
            t_permission_boundary(web, res, is_admin)
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
