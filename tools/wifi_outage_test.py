#!/usr/bin/env python3
"""
The access point that vanishes — the 2026-09-08 field failure, on the bench.

The host's USB Wi-Fi adapter is the access point (NetworkManager hotspot,
SIMUT-BENCH, 10.42.0.1/24). The device is pointed at it, then the hotspot is
taken down and brought back, and the only question is the one the field asked:
does the device come back on its own, without a power cycle?

Three phases, each with its own expectation:

  1. short outage  (60 s)   the fast rungs: reconnect within ~2 min of the AP
                             returning
  2. long outage   (9 min)  exhausts the 5 fast cycles and enters DORMANCY
                             (10 min waits) — the state the old firmware went
                             terminal in. Reconnect within one dormant wait of
                             the AP returning, i.e. ≤ 11 min. Dormancy is not
                             assumed from the clock: NET_DORMANT_MODE (526) has
                             to be in the device log inside the outage, or the
                             phase says so. The first run used 4 min and never
                             got there — the observed backoff between blind
                             attempts was 42, 62, 102 s, so five cycles take
                             over five minutes.
  3. hidden SSID + outage   the AP is hidden, so the scan never lists it, and
                             then it goes away and comes back: the reconnect
                             has to go through the blind path — SYS_WIFI_CONNECT
                             ctx=1 in the log, then an address. (A BOOT joins a
                             hidden network directly with begin( ) and proves
                             nothing about the reconnect path; the first run
                             measured that and called it the blind path.)

Observed from BOTH sides, because either alone can lie: the host pings the
device on the hotspot subnet, and the console's `show net status` says what
the state machine thinks. Nothing is written to the device between the outage
and the reconnect except that read.

The device runs the Air build. Every wake there is a boot, which would make
"reconnect without a reboot" meaningless — so it is held in M0 for the whole
run: `air idle` raised to an hour and the hand's CHARGER line held, both put
back at the end.

RESTORE. The device's real Wi-Fi password is overwritten by `system pass`
and cannot be read back from anything (the firmware does not expose it and
/download refuses /config). It must come from SIMUT_WIFI_PASS in the
environment, and the tool refuses to start without it. QUOTE IT in the env
file: `export SIMUT_WIFI_PASS='...'`. On 2026-09-09 an unquoted value with a
`$` in it lost two characters to shell expansion, the device was handed an
18-character password for a 20-character network, and the bench was off the
air twice before anyone measured the LENGTH instead of trusting the value.

RESULTS, 2026-09-09, v2.4.1-beta, run 3 (the first two were the instrument
learning: a /24 sweep that stopped at .39 while the console said .235, a
4-minute outage that never reached dormancy, and a "hidden" phase that
measured a boot):

    short   reconnected 45 s after the AP returned   (8 s in run 2)
    long    NET_DORMANT_MODE logged at ~20:52:40 after five blind attempts
            (backoff 42 → 62 → 102 → 181 s); AP back 20:54:50; reconnected
            21:03:13 — 499 s later, when the 10-minute dormant wait expired.
            The state the old firmware never left.
    hidden  reconnected 13 s after the hidden AP returned, two blind joins
            (ctx=1) and zero scan hits in the log

USAGE
    source ~/.simut-bench.env      # SIMUT_WEB_USER/PASS, SIMUT_WIFI_PASS
    SIMUT_HOTSPOT_PASS=... python3 tools/wifi_outage_test.py [--short 60] [--long 540] [--skip-hidden]

    The hotspot connection `simut-bench` (SSID SIMUT-BENCH, WPA2) must exist in
    NetworkManager: nmcli dev wifi hotspot ifname <iface> con-name simut-bench
    ssid SIMUT-BENCH band bg password <SIMUT_HOTSPOT_PASS>; the interface is
    SIMUT_WIFI_IFACE (default wlx001a3fa1e19e).

Project: SIMUT
License: MIT
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from air_test_suite import Suite, parse_air_status  # noqa: E402

W = os.environ.get('SIMUT_WIFI_IFACE', 'wlx001a3fa1e19e')
HOT_SSID, HOT_CON = 'SIMUT-BENCH', 'simut-bench'
REAL_SSID = os.environ.get('SIMUT_REAL_SSID', '')
REAL_PASS = os.environ.get('SIMUT_WIFI_PASS', '')
HOT_PASS = os.environ.get('SIMUT_HOTSPOT_PASS', '')
DEV_IP_RE = re.compile(r'(10\.42\.0\.\d+)')


def sh(*a, t=40):
    r = subprocess.run(a, capture_output=True, text=True, timeout=t)
    return r.returncode, (r.stdout + r.stderr).strip()


def log(msg):
    print(f'{time.strftime("%H:%M:%S")}  {msg}', flush=True)


def hotspot(up, hidden=False):
    if up:
        sh('nmcli', 'con', 'modify', HOT_CON, '802-11-wireless.hidden', 'yes' if hidden else 'no')
        rc, out = sh('nmcli', 'con', 'up', HOT_CON, t=60)
        log(f'hotspot UP (hidden={hidden}) rc={rc}')
    else:
        rc, out = sh('nmcli', 'con', 'down', HOT_CON, t=60)
        log(f'hotspot DOWN rc={rc}')
    return rc == 0


def leases():
    """Clients the hotspot's dnsmasq has handed an address to."""
    found = set()
    for path in ('/var/lib/NetworkManager/dnsmasq-%s.leases' % W,):
        try:
            for line in open(path):
                m = DEV_IP_RE.search(line)
                if m:
                    found.add(m.group(1))
        except OSError:
            pass
    return found


def ping(ip):
    rc, _ = sh('ping', '-c', '1', '-W', '2', '-I', W, ip, t=6)
    return rc == 0


KNOWN_IP = {'ip': None}   # what the console last said; dnsmasq usually re-issues it


def neighbours():
    """Hotspot clients the kernel has seen: the ARP/neighbour table, not a guess."""
    rc, out = sh('ip', '-4', 'neigh', 'show', 'dev', W)
    return set(DEV_IP_RE.findall(out))


def find_device(hint=None):
    """IP of the SIMUT on the hotspot subnet.

    The first run of this test pinged 10.42.0.2-39 and declared "did not
    join" while the console was saying IP: 10.42.0.235 — dnsmasq hands out
    the whole /24. So: the address the console reported, then whatever the
    neighbour table has seen, then the lease file; a blind sweep only as the
    last resort, and over the whole range.
    """
    cands = []
    if hint: cands.append(hint)
    if KNOWN_IP['ip']: cands.append(KNOWN_IP['ip'])
    cands += sorted(neighbours()) + sorted(leases())
    seen = set()
    for ip in cands:
        if ip in seen or ip == '10.42.0.1': continue
        seen.add(ip)
        if ping(ip):
            KNOWN_IP['ip'] = ip
            return ip
    return None


def sweep():
    for n in range(2, 255):
        ip = f'10.42.0.{n}'
        rc, _ = sh('ping', '-c', '1', '-W', '1', '-I', W, ip, t=4)
        if rc == 0:
            KNOWN_IP['ip'] = ip
            return ip
    return None


def wait_online(deadline_s, poll=5, console=None):
    """Seconds until the device answers a ping through the hotspot, or None.

    `console` is a callable returning the device's own view (show net status);
    the IP it reports is tried first, so the host never has to guess.
    """
    t0 = time.time()
    swept = False
    while time.time() - t0 < deadline_s:
        hint = None
        if console:
            m = DEV_IP_RE.search(console() or '')
            if m: hint = m.group(1)
        ip = find_device(hint)
        if not ip and not swept and time.time() - t0 > 60:
            ip = sweep(); swept = True
        if ip:
            return round(time.time() - t0, 1), ip
        time.sleep(poll)
    return None, None


def fmt_secs(secs):
    return 'já respondia quando a contagem começou' if secs is not None and secs < 1 else f'{secs:.0f}s'


def device_log(s, since_epoch):
    """NET/Wi-Fi codes the device logged since `since_epoch`, via /api/logs.

    Needs the web, i.e. the device on a network the host can reach — so it is
    read AFTER a reconnect, retrospectively; the records carry their own
    epoch. 12-byte records, <IHHhBB: epoch, up_lo, code, ctx, flags, up_hi.
    """
    import struct, os
    from air_test_suite import Web
    ip = KNOWN_IP['ip']
    if not ip:
        return []
    try:
        w = Web(ip)
        if w.wait_up(60) is None:
            return []
        w.login(os.environ['SIMUT_WEB_USER'], os.environ['SIMUT_WEB_PASS'])
        raw = w.get('/api/logs').content
    except Exception as e:
        log(f'  (log do aparelho indisponível: {type(e).__name__})')
        return []
    out = []
    for i in range(len(raw) // 12):
        ep, uplo, code, ctx, flags, uphi = struct.unpack_from('<IHHhBB', raw, i * 12)
        if ep >= since_epoch and (code in (10, 12, 525, 526) or 500 <= code < 560):
            out.append((ep, code, ctx))
    return out


def net_status(s):
    try:
        out = s.target.cmd('show net status', 6)
        keep = [l.strip() for l in out.splitlines() if re.search(r'State|Estado|IP|RSSI|SSID|Wi', l)]
        return ' | '.join(keep)[:200]
    except Exception as e:
        return f'(console: {type(e).__name__})'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--short', type=int, default=60, help='short outage, seconds')
    ap.add_argument('--long', type=int, default=540, help='long outage, seconds (dormancy needs > 5 fast cycles)')
    ap.add_argument('--skip-hidden', action='store_true')
    args = ap.parse_args()

    if not REAL_PASS:
        print('FATAL: SIMUT_WIFI_PASS não está no ambiente — sem ela a senha real não pode ser devolvida ao aparelho.')
        return 2
    if not HOT_PASS:
        print('FATAL: SIMUT_HOTSPOT_PASS não está no ambiente — a senha do hotspot simut-bench.')
        return 2

    ns = argparse.Namespace(host='192.168.3.24', collector_port=8010, wake_grace=120, flush_cap=180, cycles=3,
                            hist_interval=0, period_tol=45.0, long=False, baseline=False, report=None,
                            t11_window=420, only='T04', list=False, selftest=False, flash=None, watch=None)
    s = Suite(ns)
    results = []
    idle_before = None
    try:
        # ── hold the device up: an Air wake is a boot, and this is about NOT rebooting ──
        st = s.ensure_m0()
        idle_before = st.get('idle')
        s.target.cmd('air idle 3600', 4)
        s.hand.charger(True)
        log(f'M0 held: air idle {idle_before} -> 3600, CHARGER on')

        # ── point the device at the hotspot ─────────────────────────────────
        hotspot(True)
        s.target.cmd(f'system ssid {HOT_SSID}', 4)
        s.target.cmd(f'system pass {HOT_PASS}', 4)
        s.target.close()
        s.target.cmd('reload confirm', 2)
        s.target.usb.wait(False, 20); s.target.usb.wait(True, 60)
        s.ensure_m0(); s.target.cmd('air idle 3600', 4)
        secs, ip = wait_online(180, console=lambda: net_status(s))
        log(f'join do hotspot: {(fmt_secs(secs) + ", ip " + ip) if ip else "NÃO ENTROU em 180 s"}')
        log('console: ' + net_status(s))
        if not ip:
            results.append(('join', 'FAIL', 'o aparelho não entrou no hotspot — nada a medir'))
            return 1
        results.append(('join', 'PASS', f'{fmt_secs(secs)} em {ip}'))
        t_run0 = int(time.time())

        # ── phase 1: short outage ───────────────────────────────────────────
        hotspot(False)
        log(f'apagão curto: {args.short}s')
        time.sleep(args.short)
        log('console durante o apagão: ' + net_status(s))
        hotspot(True)
        secs, ip2 = wait_online(180, console=lambda: net_status(s))
        log(f'volta após apagão curto: {"%.0fs" % secs if ip2 else "NÃO VOLTOU em 180 s"} | ' + net_status(s))
        results.append(('short', 'PASS' if ip2 else 'FAIL',
                        f'reconectou {secs:.0f}s depois de o AP voltar' if ip2 else 'não reconectou em 180 s'))

        # ── phase 2: long outage, into dormancy ─────────────────────────────
        hotspot(False)
        log(f'apagão longo: {args.long}s (5 ciclos rápidos + dormência)')
        t0 = time.time()
        while time.time() - t0 < args.long:
            time.sleep(60)
            log('  console: ' + net_status(s))
        t_out0, t_out1 = int(t0), int(time.time())
        hotspot(True)
        secs, ip3 = wait_online(11 * 60, poll=10, console=lambda: net_status(s))
        log(f'volta após apagão longo: {fmt_secs(secs) if ip3 else "NÃO VOLTOU em 11 min"} | ' + net_status(s))
        recs = device_log(s, t_out0)
        dormant = [r for r in recs if r[1] == 526 and t_out0 <= r[0] <= t_out1 + 5]
        blind = [r for r in recs if r[1] == 10 and r[2] == 1 and t_out0 <= r[0] <= t_out1 + 5]
        log(f'log do aparelho no apagão: {len(blind)} join(s) às cegas (ctx=1), '
            f'{len(dormant)} NET_DORMANT_MODE — ' + ', '.join(time.strftime("%H:%M:%S", time.localtime(r[0])) + f":{r[1]}/{r[2]}" for r in recs[:12]))
        if not ip3:
            results.append(('long', 'FAIL', 'não reconectou em 11 min — o estado terminal do firmware antigo'))
        elif dormant:
            results.append(('long', 'PASS', f'entrou em dormência ({len(dormant)}×) e reconectou {fmt_secs(secs)} depois de o AP voltar'))
        else:
            results.append(('long', 'PASS*', f'reconectou {fmt_secs(secs)} depois de o AP voltar, mas NÃO chegou à dormência '
                                              f'em {args.long}s ({len(blind)} tentativas às cegas) — a saída da dormência segue sem prova'))

        # ── phase 3: hidden SSID, from a boot ───────────────────────────────
        if not args.skip_hidden:
            # The AP goes hidden while the device is on it, then vanishes for
            # a minute. On the way back the scan lists nothing, so the only
            # route is the blind one. A boot would join a hidden AP directly.
            hotspot(False); hotspot(True, hidden=True)
            secs, ip4 = wait_online(180, console=lambda: net_status(s))
            log(f'AP oculto de pé: {fmt_secs(secs) if ip4 else "NÃO VOLTOU em 180 s"}')
            t_h0 = int(time.time()); hotspot(False); time.sleep(60)
            log('console durante o apagão (oculto): ' + net_status(s)); hotspot(True, hidden=True)
            secs, ip5 = wait_online(300, poll=10, console=lambda: net_status(s))
            t_h1 = int(time.time())
            recs = device_log(s, t_h0)
            blind_h = [r for r in recs if r[1] == 10 and r[2] == 1 and t_h0 <= r[0] <= t_h1]
            normal_h = [r for r in recs if r[1] == 10 and r[2] == 0 and t_h0 <= r[0] <= t_h1]
            log(f'volta com AP oculto: {fmt_secs(secs) if ip5 else "NÃO VOLTOU em 300 s"} | '
                f'log: {len(blind_h)} join(s) às cegas, {len(normal_h)} por varredura')
            if not ip5:
                results.append(('hidden', 'FAIL', 'não reconectou a uma rede oculta em 300 s'))
            elif blind_h:
                results.append(('hidden', 'PASS', f'reconectou {fmt_secs(secs)} depois de o AP oculto voltar, pelo caminho cego (ctx=1 ×{len(blind_h)})'))
            else:
                results.append(('hidden', 'PASS*', f'reconectou {fmt_secs(secs)}, mas o log não mostra o join às cegas ({len(normal_h)} por varredura) — conferir'))
        return 0 if all(r[1].startswith('PASS') for r in results) else 1
    finally:
        # ── put everything back, whatever happened ──────────────────────────
        log('restaurando: hotspot off, credenciais reais, air idle, CHARGER')
        hotspot(False)
        sh('nmcli', 'con', 'modify', HOT_CON, '802-11-wireless.hidden', 'no')
        try:
            s.ensure_m0()
            s.target.cmd(f'system ssid {REAL_SSID}', 4)
            s.target.cmd(f'system pass {REAL_PASS}', 4)
            if idle_before:
                s.target.cmd(f'air idle {idle_before}', 4)
            s.target.close(); s.target.cmd('reload confirm', 2)
            s.target.usb.wait(False, 20); s.target.usb.wait(True, 60)
            s.ensure_m0(); s.target.close()
        except Exception as e:
            log(f'RESTORE INCOMPLETO: {type(e).__name__}: {str(e)[:80]}')
        s.hand.charger(False)
        back = None
        for i in range(24):
            rc, _ = sh('ping', '-c', '1', '-W', '2', '192.168.3.24', t=6)
            if rc == 0: back = i * 5; break
            time.sleep(5)
        log(f'aparelho de volta na rede real: {"sim, em %ds" % back if back is not None else "NÃO em 120 s — ver console"}')
        print('\n── resultado ──')
        for name, verdict, detail in results:
            print(f'  [{verdict:4}] {name:6} — {detail}')


if __name__ == '__main__':
    sys.exit(main())
