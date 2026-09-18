#!/usr/bin/env python3
"""install_tls_cert.py — install the HTTPS pair on a SIMUT already in service.

POST /api/tls takes the two PEM blocks concatenated, in either order, and
refuses the pair unless it parses AND the key belongs to the certificate. This
script is the operator-facing half: it logs in, sends the pair, prints what the
device said, and — with --reboot — restarts it so the next boot picks it up.

    python3 tools/install_tls_cert.py --host 192.168.1.50 \\
        --cert web_cert.pem --key web_key.pem --reboot

Credentials come from SIMUT_WEB_USER / SIMUT_WEB_PASS, or --user/--password.
The account must be a full admin: the same gate /api/ota/apply carries, because
a certificate decides who the browser trusts from the next boot onwards.

Generating a self-signed pair the device can serve (P-256; it also accepts RSA,
and a chain from a real CA):

    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \\
        -keyout web_key.pem -out web_cert.pem -days 3650 -nodes \\
        -subj "/CN=simut" -addext "subjectAltName=IP:192.168.1.50"

The SAN matters if you want a browser to stop complaining once the certificate
is trusted; without it every client treats the certificate as belonging to no
name. The device does not check the SAN — it cannot know which address it will
be reached at — so this script does not either.

WHY THIS ROUTE EXISTS: uploads into /config are refused (they are the
credential store), so between 2026-08-29 and this tool the only way to install
the pair was to reflash the filesystem, which erases it. See issue #133 and
docs/AUTHORIZATION.md.
"""
import argparse
import hashlib
import os
import sys

import requests


def sha256_frontend(password):
    """The login page hashes before sending; the device never sees the plain
    password. latin-1, matching WebUI.h — a non-ASCII password hashed as UTF-8
    here would simply never match."""
    return hashlib.sha256(password.encode('latin-1')).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', required=True, help='device address, e.g. 192.168.1.50')
    ap.add_argument('--cert', required=True, help='certificate PEM (chain allowed)')
    ap.add_argument('--key', required=True, help='private key PEM, not passphrase-encrypted')
    ap.add_argument('--user', default=os.environ.get('SIMUT_WEB_USER'))
    ap.add_argument('--password', default=os.environ.get('SIMUT_WEB_PASS'))
    ap.add_argument('--scheme', default='http', choices=('http', 'https'),
                    help='how to reach the device NOW (default http; use https to replace an expiring pair)')
    ap.add_argument('--insecure', action='store_true',
                    help='with --scheme https, do not verify the certificate being replaced')
    ap.add_argument('--reboot', action='store_true',
                    help='restart the device afterwards so the pair takes effect')
    args = ap.parse_args()

    if not args.user or not args.password:
        print('FATAL: set SIMUT_WEB_USER/SIMUT_WEB_PASS or pass --user/--password', file=sys.stderr)
        return 2

    try:
        cert = open(args.cert, 'r', encoding='utf-8').read()
        key = open(args.key, 'r', encoding='utf-8').read()
    except OSError as e:
        print(f'FATAL: {e}', file=sys.stderr)
        return 2

    # Read locally what the device would otherwise refuse after a round trip:
    # an encrypted key is the one mistake worth catching before the network.
    if 'ENCRYPTED PRIVATE KEY' in key:
        print('FATAL: the key is passphrase-encrypted. Decrypt it first:', file=sys.stderr)
        print('  openssl pkey -in %s -out plain_key.pem' % args.key, file=sys.stderr)
        return 2
    body = cert.rstrip() + '\n' + key.rstrip() + '\n'
    if len(body) > 8192:
        print(f'FATAL: {len(body)} B of PEM; the device accepts 8192 B', file=sys.stderr)
        return 2

    base = f'{args.scheme}://{args.host}'
    verify = not (args.scheme == 'https' and args.insecure)
    s = requests.Session()
    # Two steps, like the login page: /api/login_init hands out a nonce and
    # /api/login takes it back with the hash. Posting straight to /login is a
    # 404 — there is no such route, which is how this script was written the
    # first time and what the bench caught.
    r = s.get(f'{base}/api/login_init', timeout=20, verify=verify)
    if r.status_code != 200:
        print(f'FATAL: login_init answered HTTP {r.status_code}', file=sys.stderr)
        return 1
    nonce = r.json().get('nonce', '')
    r = s.post(f'{base}/api/login',
               data={'user': args.user, 'pass': sha256_frontend(args.password), 'nonce': nonce},
               timeout=20, allow_redirects=False, verify=verify)
    if r.status_code not in (200, 302) or 'SIMUTSESS' not in s.cookies.get_dict():
        print(f'FATAL: login answered HTTP {r.status_code}', file=sys.stderr)
        return 1
    print(f'[1/3] autenticado como {args.user}')

    r = s.post(f'{base}/api/tls', data=body.encode('utf-8'),
               headers={'Content-Type': 'application/x-pem-file'},
               timeout=30, allow_redirects=False, verify=verify)
    print(f'[2/3] POST /api/tls -> HTTP {r.status_code}: {r.text.strip()[:300]}')
    if r.status_code != 200:
        return 1

    if not args.reboot:
        print('[3/3] instalado. O par vale a partir do PROXIMO boot — reinicie quando puder')
        print('      (ou rode de novo com --reboot).')
        return 0

    r = s.post(f'{base}/api/action?op=reboot', timeout=20, allow_redirects=False, verify=verify)
    print(f'[3/3] reboot -> HTTP {r.status_code}. Depois do boot o dispositivo atende em '
          f'https://{args.host}')
    return 0 if r.status_code in (200, 202, 204) else 1


if __name__ == '__main__':
    sys.exit(main())
