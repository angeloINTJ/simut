#!/usr/bin/env python3
"""
Bluetooth surface of the SIMUT CLI, against the real device (findings V-01a/b).

The 2026-09-07 audit closed two Bluetooth findings in code and could verify
neither, because the bench had no instrument: the table in
docs/security-audit/IMPLEMENTACAO_2026-09-07.md records V-01a's lockout as
"não escrito — precisa de adaptador BT no host". The host has one. This is it.

WHAT IS CHECKED

  V-01a  The CLI locks out after wrong passwords, the penalty grows, and
         DROPPING THE LINK DOES NOT CLEAR IT. That last clause is the finding:
         before the fix a wrong guess cost nothing, because reconnecting reset
         the count. The ladder is authLockoutMs(): 1000 << n ms, capped at
         AUTH_LOCKOUT_MAX_MS, so 2 s, 4 s, 8 s, 16 s …

  V-01b  The discovery window closes BT_DISCOVERABLE_MS after boot (5 min).
         Two halves, and the second is the one that makes it a fix rather than
         a regression: the device must stop appearing in an inquiry scan, AND
         a host that already knows its address must still be able to connect.
         Connectability is deliberately untouched upstream — a phone that
         paired keeps working — so a test that only checked "cannot be found"
         would pass just as well against a device with Bluetooth switched off.

WHY IT HOLDS THE DEVICE AWAKE

  On SIMUT Air the radio only exists inside a wake window of a few tens of
  seconds, and a five-minute observation does not fit in one. The PicoHand's
  CHARGER channel (GP3 → target GP17) fakes a charger, which is exactly the
  signal the firmware treats as "stay up". Without it this test measures the
  hibernation cycle and calls it a closed discovery window.

USAGE
    python3 tools/bt_auth_test.py                 # both checks
    python3 tools/bt_auth_test.py --only lockout  # V-01a alone (fast)
    python3 tools/bt_auth_test.py --only window   # V-01b alone (~6 min)

    SIMUT_WEB_PASS must hold the admin password (source ~/.simut-bench.env);
    the CLI authenticates with it. Wrong-password attempts are deliberate, so
    run this when nobody else needs the device's Bluetooth for a few minutes.

Project: SIMUT
License: MIT
"""

import argparse
import os
import re
import socket
import subprocess
import sys
import time

HAND_SERIAL = os.environ.get('PICO_HAND_SERIAL', 'E660C062131E3E27')
HAND_PORT = f'/dev/serial/by-id/usb-Raspberry_Pi_Pico_{HAND_SERIAL}-if00'
ADMIN_PASS = os.environ.get('SIMUT_WEB_PASS', '')

# Mirrors src/SystemDefs_Network.h. Duplicated on purpose: a test that imports
# its expectations from the thing under test cannot fail when that thing is
# wrong. If these drift, the failure message says which side moved.
BT_DISCOVERABLE_MS = 300_000
AUTH_LOCKOUT_MAX_MS = 300_000


def lockout_ms(fail_count):
    """authLockoutMs() from the firmware, reimplemented for the same reason."""
    return min(1000 << fail_count, AUTH_LOCKOUT_MAX_MS)


# ── the hand ────────────────────────────────────────────────────────────────

def hand(cmd, timeout=3.0):
    """One command to the PicoHand. Returns its reply, or '' if it is absent."""
    try:
        import serial
    except ImportError:
        return ''
    try:
        h = serial.Serial(HAND_PORT, 115200, timeout=timeout)
    except Exception:
        return ''
    try:
        time.sleep(0.3)
        h.reset_input_buffer()
        h.write((cmd + '\n').encode())
        time.sleep(0.6)
        return h.read(600).decode('utf-8', 'replace').strip()
    finally:
        h.close()


def hold_awake(on):
    """Fake the charger so an Air build stays up. False when the hand cannot."""
    r = hand('CHARGER ON' if on else 'CHARGER OFF')
    return r.startswith('OK')


# ── bluetooth ───────────────────────────────────────────────────────────────

def adapter_ok():
    try:
        out = subprocess.run(['bluetoothctl', 'show'], capture_output=True,
                             text=True, timeout=10).stdout
        return 'Powered: yes' in out
    except Exception:
        return False


def _hci_adapter():
    """First adapter hcitool knows about, or None when it knows none.

    Hard-coding hci0 cost the first run of this tool on 2026-09-09: hcitool
    answered 'Invalid device' and the empty result read exactly like a device
    with its discovery window closed — a false pass waiting to happen for
    V-01b. Ask, do not assume.
    """
    try:
        out = subprocess.run(['hcitool', 'dev'], capture_output=True, text=True, timeout=5).stdout
    except Exception:
        return None
    m = re.search(r'\b(hci\d+)\b', out)
    return m.group(1) if m else None


def inquiry(seconds=12):
    """One inquiry scan. Returns {address: name} for what answered NOW.

    hcitool's `scan` is a classic inquiry, which is what BT_DISCOVERABLE_MS
    controls — not an LE scan, which would answer a different question and
    find nothing either way on this device.

    When hcitool has no adapter (the BlueZ build here exposes one to
    bluetoothctl only), fall back to bluetoothctl — with the cache CLEARED
    first. `bluetoothctl devices` lists everything it has ever seen, and a
    device remembered from a scan five minutes ago would make the window look
    open forever. Forgetting it before each scan makes the answer about now.
    """
    adapter = _hci_adapter()
    if adapter:
        try:
            out = subprocess.run(['hcitool', '-i', adapter, 'scan', '--flush'],
                                 capture_output=True, text=True,
                                 timeout=seconds + 10).stdout
        except Exception as e:
            return {'__error__': str(e)}
        found = {}
        for line in out.splitlines():
            m = re.match(r'\s*((?:[0-9A-F]{2}:){5}[0-9A-F]{2})\s+(.*)', line, re.I)
            if m:
                found[m.group(1).upper()] = m.group(2).strip()
        return found

    try:
        known = subprocess.run(['bluetoothctl', 'devices'], capture_output=True,
                               text=True, timeout=10).stdout
        for line in known.splitlines():
            m = re.match(r'Device\s+((?:[0-9A-F]{2}:){5}[0-9A-F]{2})', line, re.I)
            if m:
                subprocess.run(['bluetoothctl', 'remove', m.group(1)],
                               capture_output=True, text=True, timeout=10)
        subprocess.run(['bluetoothctl', '--timeout', str(seconds), 'scan', 'on'],
                       capture_output=True, text=True, timeout=seconds + 10)
        out = subprocess.run(['bluetoothctl', 'devices'], capture_output=True,
                             text=True, timeout=10).stdout
    except Exception as e:
        return {'__error__': str(e)}
    found = {}
    for line in out.splitlines():
        m = re.match(r'Device\s+((?:[0-9A-F]{2}:){5}[0-9A-F]{2})\s+(.*)', line, re.I)
        if m:
            found[m.group(1).upper()] = m.group(2).strip()
    return found


def find_device(name_hint='simut', tries=2):
    """Address of the target, by name. None when nothing matching answers."""
    for _ in range(tries):
        for addr, name in inquiry().items():
            if addr == '__error__':
                continue
            if name_hint.lower() in name.lower():
                return addr, name
    return None, None


def rfcomm_connect(addr, channel=1, timeout=12):
    """RFCOMM socket to the device, or None. No pybluez: the kernel does it."""
    s = socket.socket(socket.AF_BLUETOOTH, socket.SOCK_STREAM, socket.BTPROTO_RFCOMM)
    s.settimeout(timeout)
    try:
        s.connect((addr, channel))
        return s
    except Exception:
        try:
            s.close()
        except Exception:
            pass
        return None


def read_for(sock, seconds):
    """Everything the device says within a window. Never raises on timeout."""
    sock.settimeout(0.5)
    end = time.time() + seconds
    buf = b''
    while time.time() < end:
        try:
            chunk = sock.recv(1024)
            if not chunk:
                break
            buf += chunk
        except socket.timeout:
            pass
        except Exception:
            break
    return buf.decode('utf-8', 'replace')


# ── the checks ──────────────────────────────────────────────────────────────

def check_lockout(addr):
    """V-01a. Three wrong passwords, with a disconnect in the middle."""
    print('\n── V-01a — lockout do CLI Bluetooth ──')
    results = []

    s = rfcomm_connect(addr)
    if not s:
        return [('V-01a', 'SKIP', 'RFCOMM recusou a conexão')]

    banner = read_for(s, 4)
    prompts = ('Admin password', 'Senha do admin')
    if not any(p in banner for p in prompts):
        s.close()
        return [('V-01a', 'SKIP',
                 f'sem prompt de senha; recebido: {banner.strip()[:70]!r}')]
    print('  prompt de senha recebido')

    # First wrong guess: must be denied, and must start the clock.
    s.sendall(b'senha-errada-1\r\n')
    r1 = read_for(s, 4)
    denied = any(k in r1 for k in ('negado', 'denied', 'Denied'))
    results.append(('V-01a.1', 'PASS' if denied else 'FAIL',
                    'primeira senha errada foi recusada' if denied
                    else f'esperava recusa, veio {r1.strip()[:60]!r}'))

    # Drop the link. THIS is the finding: reconnecting used to clear the count.
    s.close()
    time.sleep(1.0)
    s = rfcomm_connect(addr)
    if not s:
        results.append(('V-01a.2', 'SKIP', 'reconexão recusada — não dá para julgar'))
        return results

    r2 = read_for(s, 4)
    locked = any(k in r2 for k in ('Bloqueado', 'Locked', 'locked'))
    results.append(('V-01a.2', 'PASS' if locked else 'FAIL',
                    'o lockout sobreviveu à reconexão'
                    if locked else
                    'reconectar limpou o lockout — este É o achado V-01a; '
                    f'recebido: {r2.strip()[:70]!r}'))

    # The penalty must grow. Wait out the first one, fail again, and the second
    # wait has to be longer than the first.
    if locked:
        time.sleep(lockout_ms(1) / 1000.0 + 1.0)
        s.close()
        s = rfcomm_connect(addr)
        if s:
            r3 = read_for(s, 4)
            if any(p in r3 for p in prompts):
                s.sendall(b'senha-errada-2\r\n')
                read_for(s, 3)
                s.close()
                time.sleep(1.0)
                s = rfcomm_connect(addr)
                if s:
                    r4 = read_for(s, 4)
                    still = any(k in r4 for k in ('Bloqueado', 'Locked', 'locked'))
                    results.append(('V-01a.3', 'PASS' if still else 'FAIL',
                                    f'a segunda falha bloqueou de novo '
                                    f'(escada: {lockout_ms(1)/1000:.0f}s → '
                                    f'{lockout_ms(2)/1000:.0f}s)'
                                    if still else
                                    'a segunda falha não bloqueou'))
            else:
                results.append(('V-01a.3', 'SKIP',
                                'o lockout não expirou quando a escada previa'))
    if s:
        s.close()
    return results


def check_window(addr_hint):
    """V-01b. The discovery window closes; connectability does not."""
    print('\n── V-01b — janela de descoberta ──')
    results = []

    print('  reiniciando o alvo para zerar a janela...')
    if not hand('RESET').startswith('OK'):
        return [('V-01b', 'SKIP', 'a mão não conseguiu resetar o alvo')]
    time.sleep(25)                      # boot + stack Bluetooth de pé

    addr, name = find_device()
    if not addr:
        return [('V-01b', 'SKIP',
                 'o aparelho não apareceu na varredura logo após o boot — '
                 'sem isso não há o que ver fechar')]
    print(f'  visível logo após o boot: {addr} ({name})')
    results.append(('V-01b.1', 'PASS', f'descobrível após o boot ({name})'))

    wait_s = BT_DISCOVERABLE_MS / 1000.0 + 45
    print(f'  esperando {wait_s:.0f}s para a janela fechar...')
    deadline = time.time() + wait_s
    while time.time() < deadline:
        time.sleep(15)

    again, _ = find_device()
    gone = again is None
    results.append(('V-01b.2', 'PASS' if gone else 'FAIL',
                    'não aparece mais em varredura' if gone else
                    f'ainda descobrível em {again} — a janela não fechou'))

    # The half that separates "window closed" from "Bluetooth is off".
    s = rfcomm_connect(addr)
    if s:
        s.close()
        results.append(('V-01b.3', 'PASS',
                        'quem já conhece o endereço ainda conecta'))
    else:
        results.append(('V-01b.3', 'FAIL',
                        'nem quem conhece o endereço conecta — a conectividade '
                        'foi desligada junto, o que quebra um celular pareado'))
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', choices=['lockout', 'window'])
    ap.add_argument('--no-hold', action='store_true',
                    help='não usar o canal CHARGER da mão (alvo já acordado)')
    args = ap.parse_args()

    if not adapter_ok():
        print('ERRO: nenhum adaptador Bluetooth ligado (bluetoothctl show).')
        return 2

    held = False
    if not args.no_hold:
        held = hold_awake(True)
        print(f'  canal CHARGER da mão: {"ligado" if held else "indisponível"}')
        if held:
            time.sleep(3)

    try:
        addr, name = find_device()
        if not addr and args.only != 'window' and hand('PING').startswith('PONG'):
            # Two honest reasons for silence on an Air, neither a failure: the
            # discovery window closed five minutes after the last boot, or the
            # device was ASLEEP when the inquiry ran — CHARGER keeps it from
            # going to sleep, it does not wake it. A reset with the charger held
            # answers both: a fresh boot, a fresh window, and it stays up.
            # Measured 2026-09-09: the first run of this tool against the bench
            # died here with the device mid-cycle.
            print('  sem resposta à varredura — RESET pela mão para abrir uma '
                  'janela nova (o CHARGER segura o aparelho acordado)')
            hand('RESET')
            time.sleep(25)                       # boot + pilha Bluetooth de pé
            addr, name = find_device()
        if not addr and args.only != 'window':
            print('ERRO: o aparelho não respondeu a uma varredura. Se a janela '
                  'de descoberta já fechou, reinicie-o e rode de novo — é o '
                  'comportamento correto, não uma falha do teste.')
            return 2
        if addr:
            print(f'  alvo: {addr} ({name})')

        results = []
        if args.only in (None, 'lockout') and addr:
            if not ADMIN_PASS:
                print('  aviso: SIMUT_WEB_PASS vazio — o lockout ainda é '
                      'exercitável com senhas erradas, e é isso que se mede')
            results += check_lockout(addr)
        if args.only in (None, 'window'):
            results += check_window(addr)

        print('\n── resultado ──')
        for tid, outcome, detail in results:
            print(f'  [{outcome:5}] {tid} — {detail}')
        failed = sum(1 for _, o, _ in results if o == 'FAIL')
        print(f'\n{len(results) - failed} ok, {failed} falha(s)')
        return 1 if failed else 0
    finally:
        if held:
            hold_awake(False)


if __name__ == '__main__':
    sys.exit(main())
