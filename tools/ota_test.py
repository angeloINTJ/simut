#!/usr/bin/env python3
"""Drive one OTA update on the bench: login -> stage -> apply.

Reuses air_test_suite.Web for the login (login_init nonce + sha256_frontend,
latin-1). Does NOT verify the new version — the device reboots on apply and the
caller reads /api/status back after, which is the only proof of an installed
update (OTA_USAGE.md: "never infer success from timing or HTTP codes alone").

The device installs only signed images (docs/analysis/OTA_ASSINADA.md). An
unsigned .bin is signed here with the BENCH key before it goes up
(~/.simut-ota/signer-bench.{p8,cert}, or SIMUT_OTA_BENCH_KEY / _CERT): a bench
image accepts it, a production image answers v=12. A .bin that is already
signed goes up as it is. --as-is sends the file untouched, signed or not: the
refusal cases on the rig.

Usage:
    SIMUT_WEB_PASS=... python3 ota_test.py <host[:port]> <image.bin> [--as-is]
"""
import io
import os
import sys
import time

# tools/ is where air_test_suite lives
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from air_test_suite import Web  # noqa: E402
import ota_sign  # noqa: E402

# The stage refusals, by v= (src/ota/validation.h).
WHY = {4: 'too small', 5: 'too large for the slot', 6: 'not an RP2040 image (boot2 CRC)',
       7: 'another variant', 8: 'not signed', 9: 'signature does not verify',
       10: 'signer revoked', 11: 'security_version below the installed one',
       12: 'signer scope not accepted (bench key on a production image?)'}


def image_to_send(binpath, as_is):
    data = open(binpath, 'rb').read()
    if as_is or data[-len(ota_sign.MAGIC):] == ota_sign.MAGIC:
        return data, 'as given' if as_is else 'already signed'
    home = os.path.expanduser('~/.simut-ota')
    key = os.environ.get('SIMUT_OTA_BENCH_KEY', os.path.join(home, 'signer-bench.p8'))
    cert = os.environ.get('SIMUT_OTA_BENCH_CERT', os.path.join(home, 'signer-bench.cert'))
    blob, info = ota_sign.sign_image(data, ota_sign.load_key(key), open(cert, 'rb').read(),
                                     ota_sign.known_roots())
    return blob, f"signed here with the bench key, serial {info['serial']}"


def main():
    args = [a for a in sys.argv[1:] if a != '--as-is']
    as_is = len(args) != len(sys.argv) - 1
    host, binpath = args[0], args[1]
    pw = os.environ.get('SIMUT_WEB_PASS')
    user = os.environ.get('SIMUT_WEB_USER', 'admin')
    if not pw:
        print('SIMUT_WEB_PASS not set'); return 2
    try:
        blob, how = image_to_send(binpath, as_is)
    except ota_sign.SignRefused as e:
        print(f'  {binpath}: not signed: {e}'); return 1
    print(f'  image: {binpath} ({len(blob)} B, {how})  ->  {host}')

    w = Web(host, timeout=30)
    w.login(user, pw)
    print(f'  logged in ({w.base})')

    # STAGE — multipart, ~30 s while the device writes flash. commit=1.
    t0 = time.time()
    r = w.s.post(w.base + '/api/restore?op=stage&commit=1',
                 files={'file': ('firmware.bin', io.BytesIO(blob), 'application/octet-stream')},
                 timeout=240, allow_redirects=False)
    dt = time.time() - t0
    body = r.text[:300]
    print(f'  STAGE: HTTP {r.status_code} in {dt:.1f}s  body={body}')
    committed = False
    try:
        j = r.json()
        committed = (j.get('committed') in (1, True)) and (j.get('v') in (0, '0'))
    except Exception:
        committed = ('"committed":1' in body) and ('"v":0' in body)
    if not committed:
        try:
            v = int(r.json().get('v'))
            print(f'  refused: v={v} ({WHY.get(v, "?")})')
        except Exception:
            pass
        print('  STAGE did not report v:0 committed:1 — NOT applying (an aborted '
              'stage can corrupt the FS; the device keeps running the old image).')
        return 1
    print('  STAGE ok: v:0 committed:1')

    # APPLY — 202 then reboot; 503 "Display in use" = retry. The response often
    # never completes because the device reboots mid-reply: a connection error
    # here is the expected shape of success, not a failure.
    for attempt in range(4):
        try:
            r = w.s.post(w.base + '/api/ota/apply', timeout=15, allow_redirects=False)
            print(f'  APPLY: HTTP {r.status_code} body={r.text[:120]}')
            if r.status_code == 202:
                print('  APPLY accepted (202) — device rebooting into the new image')
                return 0
            if r.status_code == 503:
                print('  503 (display/busy) — retrying'); time.sleep(4); continue
            print(f'  APPLY unexpected status {r.status_code}'); return 1
        except Exception as e:
            print(f'  APPLY connection ended ({type(e).__name__}) — expected if the '
                  'device rebooted; verify the version to confirm')
            return 0
    print('  APPLY never accepted after retries'); return 1


if __name__ == '__main__':
    sys.exit(main())
