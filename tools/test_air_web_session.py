#!/usr/bin/env python3
"""Tests the bench web client's re-login (tools/air_test_suite.py, Web._authed) against a local stand-in for the device.

Run: python3 tools/test_air_web_session.py   (CI runs it in the gates job)

The device keeps ONE web session per user: a login reuses the user's slot and gives it a new token
(WebManager::allocSessionSlot, src/WebManager_Auth.cpp), so a second client logging in as the same user (a walk run
as a subprocess, the maintainer's browser) kills the first one's cookie. The device answers that cookie in two ways:
- 401 {"error":"Unauthorized"} on the routes behind requirePerm( ) (/api/status, /api/users; WebManager_Auth.cpp,
  WebManager_Api.cpp);
- 403 on the file routes (/api/ls; WebManager_Files.cpp).
Both mean the request was refused before its handler ran, so the client logs back in and sends it once more. A 401
with any other body is a handler's own answer: a wrong password at /api/login or at a password change answers
{"ok":false,"err":1}. Retrying that would count a second failure toward the lockout, so it is returned untouched.

Found on 2026-10-24 on the bench (S10c B7): the client retried only on 403, so a run whose walks logged in as the same
admin read "Unauthorized" from a healthy device.

The stand-in answers like the device on five routes; nothing here touches a board or the network beyond 127.0.0.1.
"""
import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import air_test_suite as ats  # noqa: E402

FAILS, RAN = [], []
SESSION_GONE = '{"error":"Unauthorized"}'
WRONG_PASSWORD = '{"ok":false,"err":1}'


def check(name, cond, detail=""):
    RAN.append(name)
    print(f"[{'ok  ' if cond else 'FAIL'}] {name}" + (f" — {detail}" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


class Device:
    """The rule under test: one session per user, a login replaces the token."""
    def __init__(self):
        self.lock = threading.Lock()
        self.token, self.n_logins, self.n_chpass = None, 0, 0


DEV = Device()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):        # quiet
        pass

    def _cookie(self):
        for part in (self.headers.get('Cookie') or '').split(';'):
            k, _, v = part.strip().partition('=')
            if k == 'SIMUTSESS':
                return v
        return None

    def _send(self, code, body, ctype='application/json', cookie=None):
        data = body.encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        if cookie:
            self.send_header('Set-Cookie', f'SIMUTSESS={cookie}; Path=/')
        self.end_headers()
        self.wfile.write(data)

    def _valid(self):
        with DEV.lock:
            return DEV.token is not None and self._cookie() == DEV.token

    def do_GET(self):
        if self.path.startswith('/api/login_init'):
            return self._send(200, json.dumps({'nonce': 'n0'}))
        if self.path.startswith('/api/status'):
            return self._send(200, json.dumps({'sys': {'uptime': 1}})) if self._valid() else self._send(401, SESSION_GONE)
        if self.path.startswith('/api/ls'):
            return self._send(200, json.dumps({'entries': []})) if self._valid() else self._send(403, 'Forbidden', 'text/plain')
        return self._send(404, '{}')

    def do_POST(self):
        length = int(self.headers.get('Content-Length') or 0)
        self.rfile.read(length)
        if self.path.startswith('/api/login'):
            with DEV.lock:
                DEV.n_logins += 1
                DEV.token = f't{DEV.n_logins}'
                tok = DEV.token
            return self._send(200, '{"ok":true}', cookie=tok)
        if self.path.startswith('/api/chpass'):
            if not self._valid():
                return self._send(401, SESSION_GONE)
            with DEV.lock:
                DEV.n_chpass += 1
            return self._send(401, WRONG_PASSWORD)
        return self._send(404, '{}')


def main():
    if ats.requests is None:
        print('FAIL: the requests module is missing, so the client cannot be tested (refused, not passed)')
        return 2
    srv = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    host = f'127.0.0.1:{srv.server_address[1]}'
    try:
        a, b = ats.Web(host), ats.Web(host)
        a.login('admin', 'x')
        b.login('admin', 'x')                       # the walk, or the browser: a's cookie is gone now
        n0 = DEV.n_logins
        r = a.get('/api/status')
        check('a 401 {"error":"Unauthorized"} after another login of the same user: the client logs back in and gets 200',
              r.status_code == 200 and DEV.n_logins == n0 + 1, f'HTTP {r.status_code} {r.text[:60]!r}, logins {DEV.n_logins - n0}')
        b.login('admin', 'x')                       # stale again
        r = a.get('/api/ls')
        check('a 403 after another login (the file routes): the client logs back in and gets 200, as before',
              r.status_code == 200, f'HTTP {r.status_code} {r.text[:60]!r}')
        c0 = DEV.n_chpass
        r = a.post('/api/chpass', data={'old': 'wrong', 'new': 'y'})
        check('a 401 with a handler\'s own body (a wrong password) is returned untouched and sent once',
              r.status_code == 401 and r.text == WRONG_PASSWORD and DEV.n_chpass == c0 + 1,
              f'HTTP {r.status_code} {r.text[:60]!r}, the handler ran {DEV.n_chpass - c0} time(s)')
        fresh = ats.Web(host)
        n0 = DEV.n_logins
        r = fresh.get('/api/status')
        check('a client that never logged in gets the 401 untouched (nothing to log back in with)',
              r.status_code == 401 and DEV.n_logins == n0, f'HTTP {r.status_code}, logins {DEV.n_logins - n0}')
    finally:
        srv.shutdown()
    print(f'{len(RAN) - len(FAILS)} ok, {len(FAILS)} falha(s)')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
