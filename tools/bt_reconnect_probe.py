#!/usr/bin/env python3
"""Item 4 probe: does a reconnecting BT client inherit the authenticated session?

BluetoothManager has no onDisconnect, so `_authenticated` is only cleared on
boot, idle-timeout, or a rejected password — never when the SPP link drops. If
SPP hands a second connection to the same manager while `_authenticated` is
still true, the new client is privileged without a password. This measures it
on the real device, reusing bt_auth_test.py's helpers.

    authenticate (correct password) -> confirm granted -> drop link ->
    reconnect -> WITHOUT a password, ask `air status` and read what comes back.
      * a password prompt  -> session NOT inherited (no bypass)  -> PASS
      * air status output  -> session INHERITED (bypass)         -> FAIL (item 4)
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bt_auth_test as bt

PROMPTS = ('Admin password', 'Senha do admin')
GRANTED = ('Access granted', 'Acesso concedido', 'SIMUT IoT CLI')


def main():
    pw = os.environ.get('SIMUT_WEB_PASS')
    if not pw:
        print('SIMUT_WEB_PASS not set'); return 2
    if not bt.adapter_ok():
        print('no BT adapter'); return 2

    held = bt.hold_awake(True)
    print(f'  charger hold: {"on" if held else "unavailable"}')
    time.sleep(3)
    try:
        addr, name = bt.find_device()
        if not addr and bt.hand('PING').startswith('PONG'):
            print('  no scan hit — RESET to open a fresh window (charger holds it up)')
            bt.hand('RESET'); time.sleep(25)
            addr, name = bt.find_device()
        if not addr:
            print('  device not discoverable — cannot probe'); return 3
        print(f'  device: {addr} ({name})')

        # 1) authenticate on a first link
        s = bt.rfcomm_connect(addr, retry_s=5.0)
        if not s:
            print('  could not connect'); return 3
        banner = bt.read_for(s, 3)
        if not any(p in banner for p in PROMPTS):
            s.sendall(b'\r\n'); banner += bt.read_for(s, 2)
        s.sendall((pw + '\r\n').encode())
        granted = bt.read_for(s, 4)
        auth_ok = any(g in granted for g in GRANTED)
        print(f'  first link authenticated: {auth_ok}  ({granted.strip()[:60]!r})')
        if not auth_ok:
            print('  could not authenticate — probe inconclusive'); s.close(); return 3

        # 2) drop the link
        s.close()
        time.sleep(2)

        # 3) reconnect and, WITHOUT a password, ask for privileged output
        s2 = bt.rfcomm_connect(addr, retry_s=6.0)
        if not s2:
            print('  reconnect refused — probe inconclusive'); return 3
        hello = bt.read_for(s2, 3)                 # banner on reconnect
        s2.sendall(b'air status\r\n')
        resp = bt.read_for(s2, 4)
        s2.close()
        blob = (hello + resp)
        prompted = any(p in blob for p in PROMPTS)
        privileged = ('phase=' in blob or 'idle=' in blob or 'wake=' in blob
                      or 'AIR' in resp)
        print(f'  reconnect banner+resp: {blob.strip()[:120]!r}')
        print()
        if prompted and not privileged:
            print('VERDICT: PASS — reconnect asks for the password again; the session '
                  'is NOT inherited. No bypass on this build.')
            return 0
        if privileged:
            print('VERDICT: FAIL (item 4 CONFIRMED) — reconnect ran `air status` with no '
                  'password. The authenticated session is inherited across the SPP drop.')
            return 1
        print(f'VERDICT: INCONCLUSIVE — neither a prompt nor privileged output. '
              f'raw={blob.strip()[:100]!r}')
        return 3
    finally:
        if held:
            bt.hold_awake(False)


if __name__ == '__main__':
    sys.exit(main())
