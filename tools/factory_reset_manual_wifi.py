#!/usr/bin/env python3
r"""§5.6 E2E — Config de fábrica → WiFi com reconfiguração manual
(PLANO-VALIDACAO-v2.3.2-stable.md, item configure_and_test adaptado).

O `configure_and_test.py` original hardcoda /dev/ttyACM0 e exige
SIMUT_WIFI_SSID/PASS no ambiente. Decisão do operador (2026-08-22): as
credenciais do WiFi não entram no host — o operador reconecta o Pico
**manualmente no console** quando o script pausar. Este wrapper roda sobre
o alvo real (/dev/ttyACM1) e cobre o critério do plano: reset de fábrica,
reconfiguração, reload **sem reboot inesperado** (0 reboot não provocado).

Protocolo:
  0. Backup .bkp da config atual (GET /api/backup) — higiene da bancada.
  1. `system factory confirm` no CLI (destrutivo por design) + aguarda reboot.
     Captura a SENHA ADMIN inicial (SEC-003) impressa na serial — o factory
     gera senha ALEATÓRIA (não há default fixo).
  2. PAUSA: o operador configura o WiFi manualmente (configure terminal +
     wifi … + end + write memory). O script espera ENTER.
  3. Verifica rede (IP obtido), uptime sem reboot inesperado, login web com
     a senha OTP capturada e /api/status.
  4. Restaura o .bkp (POST /api/restore?op=apply) e verifica login com a
     credencial original da bancada + config íntegra.

AVISO: apaga TODO o /config (cert TLS, usuários, sensores, telemetria) —
a restauração do .bkp no fim devolve tudo. Rodar com o operador presente.

Uso:  python3 tools/factory_reset_manual_wifi.py [--port /dev/ttyACM1]
"""
import argparse
import re
import sys
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scratchpad'))
from dev import Web  # noqa: E402

try:
    from rig_secrets import PASS, USER
except ImportError:
    PASS = os.environ.get('SIMUT_PASS', 'admin')
    USER = os.environ.get('SIMUT_USER', 'admin')

PROMPTS = ('SIMUT#', 'SIMUT>', 'SIMUT(config)')
OTP_RE = re.compile(r'Senha ADMIN inicial:\s*(\S+)')


class Cli:
    def __init__(self, port, baud=115200):
        self.ser = serial.Serial(port, baud, timeout=0.5)
        self.ser.dtr = True
        time.sleep(1.0)
        self.ser.reset_input_buffer()

    def read(self, timeout=10):
        buf = b''
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.ser.read(512)
            if chunk:
                buf += chunk
                if any(buf.decode(errors='replace').rstrip().endswith(p)
                       for p in PROMPTS):
                    return buf.decode(errors='replace')
            else:
                time.sleep(0.05)
        return buf.decode(errors='replace')

    def cmd(self, text, timeout=15):
        self.ser.write((text + '\r\n').encode())
        return self.read(timeout)

    def close(self):
        self.ser.close()


def parse_uptime(text):
    m = re.search(r'Uptime:\s*(\d+):(\d+):(\d+)', text)
    if not m:
        return None
    h, mi, s = (int(g) for g in m.groups())
    return h * 3600 + mi * 60 + s


def parse_ip(text):
    m = re.search(r'(\d{1,3}(?:\.\d{1,3}){3})', text)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port',
                    default='/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00',
                    help='serial do alvo (by-id do Pico W; pode re-enumerar '
                         'entre ttyACM1/ttyACM2 após power-cycle)')
    ap.add_argument('--host', default='192.168.3.24')
    ap.add_argument('--out', default='scratchpad/factory_test_v232')
    args = ap.parse_args()
    if '*' in args.port:
        import glob
        hits = sorted(glob.glob(args.port))
        if not hits:
            print(f'FATAL: nenhum serial em {args.port}')
            return 2
        args.port = hits[0]
        print(f'  serial do alvo: {args.port}')
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    fails = []

    # 0. Backup da config atual
    print('=== BACKUP ANTES DO RESET ===')
    w = Web(host=args.host, timeout=20, scheme='http')
    ok, info = w.login(USER, PASS)
    if not ok:
        print(f'  FATAL: login falhou ({info})')
        return 2
    r = w.get('/api/backup')
    if r.status_code != 200:
        print(f'  FATAL: /api/backup HTTP {r.status_code}')
        return 2
    bkp = out / 'backup_pre_factory.bkp'
    bkp.write_bytes(r.content)
    print(f'  {len(r.content)} B salvo em {bkp}')

    cli = Cli(args.port)
    print('\n=== BOOT / ESTADO INICIAL ===')
    info = cli.cmd('show system info')
    print('\n'.join('  ' + l for l in info.splitlines() if l.strip()))

    # 1. Factory reset (destrutivo) + captura da OTP
    print('\n=== FACTORY RESET (destrutivo) ===')
    r = cli.cmd('system factory confirm', timeout=30)
    print('  ' + r.strip()[-160:])
    cli.close()

    print('  Aguardando reboot e capturando a senha inicial (SEC-003)…')
    otp = None
    boot_buf = ''
    for _ in range(60):
        time.sleep(1)
        try:
            cli = Cli(args.port)
            boot_buf += cli.read(timeout=8)
            m = OTP_RE.search(boot_buf)
            if m:
                otp = m.group(1)
            if 'SIMUT' in boot_buf:
                break
            cli.close()
        except Exception:
            pass
    if not otp:
        fails.append('senha ADMIN inicial não capturada na serial')
        print('  !! não capturei a senha — o operador terá que informá-la')
        otp = input('  Cole a "Senha ADMIN inicial" do console (ou ENTER p/ pular): ').strip()
    else:
        print(f'  Senha ADMIN inicial capturada ({len(otp)} chars)')

    # 2. PAUSA — operador configura o WiFi manualmente
    print('\n=== OPERADOR: configure o WiFi manualmente no console ===')
    print('    (configure terminal + comando de WiFi + end + write memory)')
    input('    Quando terminar, volte aqui e digite ENTER: ')

    # 3. Rede + estabilidade
    print('\n=== REDE E ESTABILIDADE ===')
    net = cli.cmd('show net status', timeout=20)
    print('\n'.join('  ' + l for l in net.splitlines() if l.strip()))
    ip = parse_ip(net)
    if not ip:
        fails.append('sem IP na rede após a configuração manual')
    up1 = parse_uptime(cli.cmd('show metrics', timeout=15) or '')
    time.sleep(3)
    up2 = parse_uptime(cli.cmd('show metrics', timeout=15) or '')
    if up1 is not None and up2 is not None and up2 < up1:
        fails.append(f'reboot inesperado durante a verificação ({up1}s -> {up2}s)')
    else:
        print(f'  uptime {up1}s -> {up2}s: sem reboot inesperado')

    # 4. Login web com a OTP + sanidade
    print(f'\n=== SANIDADE WEB (OTP) ===')
    host = ip or args.host
    wotp = Web(host=host, timeout=10, scheme='http')
    ok_otp, info_otp = wotp.login('admin', otp) if otp else (False, 'sem OTP')
    if not ok_otp:
        fails.append(f'login web com a OTP falhou: {info_otp}')
    else:
        print(f'  login OTP ok ({info_otp})')
        r = wotp.get('/api/status')
        print(f'  /api/status HTTP {r.status_code}')

    # 5. Restauração do .bkp (higiene da bancada)
    print('\n=== RESTAURAÇÃO DO BACKUP ===')
    if ok_otp:
        with open(bkp, 'rb') as fh:
            r = wotp.post('/api/restore?op=apply',
                          files={'restore': (bkp.name, fh.read())},
                          timeout=60)
        print(f'  restore -> HTTP {r.status_code}: {r.text[:120]}')
        time.sleep(6)
        w2 = Web(host=host, timeout=15, scheme='http')
        ok2, info2 = w2.login(USER, PASS)
        if not ok2:
            fails.append(f'login com a credencial original após restore: {info2}')
        else:
            print(f'  login original ok ({info2})')
            rr = w2.get('/api/status')
            print(f'  /api/status HTTP {rr.status_code}')
            if rr.status_code != 200:
                fails.append('/api/status != 200 pós-restore')
    else:
        fails.append('restore não executado (sem login OTP)')

    print(f'\nfactory_reset_manual_wifi: {"FAIL" if fails else "PASS"} '
          f'({len(fails)} falha(s))')
    for f in fails:
        print('  FAIL: ' + f)
    return 1 if fails else 0


if __name__ == '__main__':
    raise SystemExit(main())
