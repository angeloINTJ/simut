#!/usr/bin/env python3
"""
Reset the bench device's admin password and write it into ~/.simut-bench.env.

WHY THIS EXISTS
---------------
The bench credentials moved out of the repository and into the environment
(`17b2a39`), which was right, and left a gap: when the device's password and
the file disagree, every web-dependent test SKIPS rather than fails, and a run
comes back looking mostly green while three tests never happened. That is what
happened on 2026-09-08 — `air_test_suite.py` logged in, got
`401 {"ok":false,"err":2,"lockSec":2}`, and skipped T06, T06b and T07.

`system admin reset confirm` on the emergency console is the documented way
back into a device whose password nobody has. It prints a one-time password
and nothing else knows it, so capturing it and storing it is the whole job.

WHAT IT DOES NOT DO
-------------------
It does not print the password. It writes it to ~/.simut-bench.env with mode
600 and reports only that it changed — a password echoed into a terminal ends
up in scrollback, in a transcript, and eventually somewhere it should not be.

IT ALSO FINISHES THE JOB
------------------------
`system admin reset` arms mustChangePassword, and a device in that state
refuses every configuration write. Until 2026-09-08 it refused them SILENTLY —
the socket closed with no response — so a bench recovered this way looked
healthy and answered nothing to a save, with no clue anywhere. The firmware now
answers 409, and this tool completes the forced change through
/api/force_chpass so the bench comes back usable rather than merely reachable.

Over plain HTTP the change API takes the sha256 of the password, not the
password (finding A-5: no plaintext on a cleartext link), hashed the way the
login page does it — latin-1, not UTF-8.

ON SIMUT AIR
------------
Every wake is a boot, so the console is only there for a few tens of seconds
at a time. This waits for a window rather than assuming one. It also verifies
the new password against the web API AFTER a reboot, not just in the session
that printed it — that distinction is finding F27, where the reset only ever
touched RAM and the password expired with the wake that announced it.

USAGE
    python3 tools/rig_reset_admin.py [--host 192.168.3.24] [--no-verify]

Project: SIMUT
License: MIT
"""

import argparse
import os
import re
import stat
import sys
import time

ENV_PATH = os.path.expanduser('~/.simut-bench.env')
TARGET_SERIAL = os.environ.get('SIMUT_TARGET_SERIAL', 'E6642815E34C1824')
PORT = f'/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_{TARGET_SERIAL}-if00'

# The generator's alphabet, from StorageManager::generateInitialAdminPassword:
# capitals and digits with the ambiguous ones removed, eight of them, printed
# alone on its own line.
OTP_RE = re.compile(r'^\s*([A-Z2-9]{8})\s*$', re.M)


def wait_for_port(timeout=420):
    """Air sleeps; wait for a wake window rather than assume one."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(PORT):
            time.sleep(1.2)          # let the CDC finish enumerating
            return True
        time.sleep(0.25)
    return False


def console_reset(timeout=25):
    """Run the reset on the emergency console. Returns the new password."""
    import serial
    if not wait_for_port():
        raise SystemExit('alvo não apareceu no USB dentro do prazo')
    last = None
    for attempt in range(4):
        try:
            s = serial.Serial(PORT, 115200, timeout=2)
        except Exception as e:            # ModemManager or a monitor holding it
            last = e
            time.sleep(2.0)
            continue
        try:
            time.sleep(0.6)
            s.reset_input_buffer()
            s.write(b'\r\n')
            time.sleep(0.6)
            s.read(8000)
            s.write(b'system admin reset confirm\r\n')
            time.sleep(6.0)
            out = s.read(20000).decode('utf-8', 'replace')
        finally:
            s.close()
        m = OTP_RE.search(out)
        if m:
            return m.group(1)
        last = ('o console respondeu, mas sem senha no formato esperado: '
                + ' | '.join(l.strip() for l in out.splitlines() if l.strip())[:200])
        time.sleep(2.0)
    raise SystemExit(f'não consegui capturar a senha nova: {last}')


def write_env(password, user='admin'):
    """Rewrite the env file, preserving anything else it holds."""
    lines, seen_user, seen_pass = [], False, False
    if os.path.exists(ENV_PATH):
        with open(ENV_PATH, encoding='utf-8') as fh:
            for line in fh:
                if re.match(r'\s*export\s+SIMUT_WEB_PASS=', line):
                    lines.append(f'export SIMUT_WEB_PASS={password}\n')
                    seen_pass = True
                elif re.match(r'\s*export\s+SIMUT_WEB_USER=', line):
                    lines.append(f'export SIMUT_WEB_USER={user}\n')
                    seen_user = True
                else:
                    lines.append(line)
    if not seen_user:
        lines.append(f'export SIMUT_WEB_USER={user}\n')
    if not seen_pass:
        lines.append(f'export SIMUT_WEB_PASS={password}\n')

    with open(ENV_PATH, 'w', encoding='utf-8') as fh:
        fh.writelines(lines)
    os.chmod(ENV_PATH, stat.S_IRUSR | stat.S_IWUSR)   # 600, as the audit asked


def verify_over_web(host, user, password, tries=6):
    """Log in the way the browser does, AFTER the reboot the reset causes.

    Verifying in the session that printed the password would pass against
    finding F27, where the reset only ever reached RAM.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from air_test_suite import Web              # reuse the real login
    except Exception as e:
        return None, f'não consegui reutilizar o login da suíte: {e}'
    for attempt in range(tries):
        try:
            w = Web(host)
            w.login(user, password)
            return True, 'login aceito depois do reboot'
        except Exception as e:
            last = str(e)[:120]
            time.sleep(20)                            # Air: espera outro wake
    return False, last


def clear_forced_change(host, user, otp):
    """Finish the forced change, so the bench is usable and not merely reachable.

    A device fresh out of `system admin reset` refuses every configuration
    write until this is done. Returns (ok, new_password, detail).
    """
    import secrets
    import string
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from air_test_suite import Web, sha256_frontend
    except Exception as e:
        return False, otp, f'não consegui reutilizar o cliente da suíte: {e}'

    alphabet = string.ascii_uppercase + string.digits
    while True:
        new = ''.join(secrets.choice(alphabet) for _ in range(12))
        if any(c.isalpha() for c in new) and any(c.isdigit() for c in new):
            break

    try:
        w = Web(host)
        w.login(user, otp)
        # Plain HTTP takes the digest, not the password (A-5).
        digest = sha256_frontend(new)
        r = w.s.post(f'http://{host}/api/force_chpass',
                     data={'p1': digest, 'p2': digest}, timeout=20)
        if r.status_code != 200:
            return False, otp, f'HTTP {r.status_code}: {r.text[:80]}'
        time.sleep(2)
        Web(host).login(user, new)          # provado, não presumido
        return True, new, 'concluída e relogin aceito'
    except Exception as e:
        return False, otp, f'{type(e).__name__}: {str(e)[:90]}'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default=os.environ.get('SIMUT_HOST', '192.168.3.24'))
    ap.add_argument('--user', default='admin')
    ap.add_argument('--no-verify', action='store_true')
    args = ap.parse_args()

    print('  esperando uma janela de console...')
    pw = console_reset()
    print(f'  senha nova capturada ({len(pw)} caracteres) — não será exibida')

    write_env(pw, args.user)
    print(f'  gravada em {ENV_PATH} (modo 600)')

    if args.no_verify:
        return 0
    print('  verificando pela web depois do reboot (F27)...')
    ok, detail = verify_over_web(args.host, args.user, pw)
    if ok:
        ok2, pw2, detail2 = clear_forced_change(args.host, args.user, pw)
        print(f'  troca forçada: {detail2}')
        if ok2:
            write_env(pw2, args.user)
            print('  senha definitiva gravada no env (não exibida)')
    if ok is None:
        print(f'  AVISO: {detail}')
        return 0
    print(f'  {"OK" if ok else "FALHOU"}: {detail}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
