#!/usr/bin/env python3
"""wire_bench.py — a bancada do SIMUT Wire: a imagem pico_wire inteira, no alvo.

O wire_enc_bench.py prova o modulo ENC28J60 e a biblioteca sem o SIMUT; este prova o
firmware. Cada etapa diz o que conferiu, e o relatorio (JSON) e a saida da serial vao
para --out, com data.

  boot   a imagem sobe nesta placa, sem rede. A PicoHand reinicia o alvo (hand RESET);
         a serial le o boot; o console de emergencia diz a versao (`show system info`) e
         o estado da rede (`show net status`), e o log (`show system log`) nao pode ter
         falha de PIO ("pio0 full", "pio1 full", "PIO init failed", "PioSpace"). Com
         --admin-reset, `system admin reset confirm` gera a senha do admin, que vai para
         ~/.simut-bench.env (o arquivo antigo fica em .bak, a senha nunca na tela): o
         console desta imagem nao configura sensor, e o DHT22 do GP10 so se configura
         pela web (commit_all). Sem slot ativo o DHT22 nao pede PIO: a checagem de PIO
         desta etapa so cobre o buzzer.

As etapas de rede (o link e o DHCP, o /api/status com `link`, o DHT22 do GP10, o IP
fixo pelo console, o cabo puxado, a OTA pelo cabo, o TCP em massa, a suite web e o
soak) ainda nao estao aqui: o driver do ENC28J60 chegou em 2026-10-12, e elas sao o
proximo passo deste roteiro.

A placa e achada so pelo numero de serie (--target-serial ou SIMUT_TARGET_SERIAL; a mao,
por PICO_HAND_SERIAL), nunca por modelo: a imagem do Pico comum enumera como "Pico".

  python3 tools/wire_bench.py --target-serial <serial> --hand-serial <serial> --stages boot
"""
import argparse
import datetime
import glob
import json
import os
import re
import shutil
import stat
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "scratchpad", "wire_" + datetime.date.today().strftime("%Y%m%d"))
ENV_PATH = os.path.expanduser("~/.simut-bench.env")

# Placas que nunca sao o alvo nem a mao (AGENTS.md secao 1).
NOT_TARGETS = {"E660C062131E3E27": "a mao antiga, que segue no USB noutro projeto"}

PROMPT_RE = re.compile(r"SIMUT[#>] $")
# A senha que o firmware gera: oito de "ABCDEFGHJKLMNPQRSTUVWXYZ23456789", sozinha na linha
# (StorageManager::generateInitialAdminPassword; o mesmo padrao do rig_reset_admin.py).
OTP_RE = re.compile(r"^\s*([A-Z2-9]{8})\s*$", re.M)
PIO_FAIL_RE = re.compile(r"pio0 full|pio1 full|PIO init failed|PioSpace", re.I)
STAGES = ("boot",)


def port_of(serial, role):
    """O /dev/serial/by-id de quem tem esse numero de serie, e so dele."""
    serial = serial.upper()
    if serial in NOT_TARGETS:
        sys.exit(f"wire_bench: {serial} e {NOT_TARGETS[serial]}: nao e {role}")
    ports = sorted(glob.glob(f"/dev/serial/by-id/*_{serial}-if00"))
    if len(ports) > 1:
        sys.exit(f"wire_bench: {len(ports)} portas com o serial {serial}")
    return ports[0] if ports else None


def wait_port(serial, role, timeout):
    """Espera a porta aparecer (o alvo reenumera depois de um reset)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        p = port_of(serial, role)
        if p:
            time.sleep(1.0)   # o CDC termina de enumerar
            return p
        time.sleep(0.2)
    return None


class Hand:
    """A PicoHand: um comando por linha, a resposta na linha seguinte."""

    def __init__(self, serial):
        self.serial = serial

    def cmd(self, text, timeout=3.0):
        import serial as pyserial
        port = port_of(self.serial, "a mao")
        if not port:
            return ""
        with pyserial.Serial(port, 115200, timeout=timeout) as h:
            time.sleep(0.3)
            h.reset_input_buffer()
            h.write((text + "\n").encode())
            time.sleep(0.6)
            return h.read(600).decode("utf-8", "replace").strip()


class Console:
    """O console USB do SIMUT. Le ate o prompt (SIMUT# ou SIMUT>) ou ate o prazo."""

    def __init__(self, port, log):
        import serial as pyserial
        self.s = pyserial.Serial(port, 115200, timeout=0.2)
        self.log = log

    def read_for(self, seconds, until=None):
        out, deadline = "", time.time() + seconds
        while time.time() < deadline:
            chunk = self.s.read(4096).decode("utf-8", "replace")
            if chunk:
                out += chunk
                self.log.write(chunk)
                if until and until.search(out):
                    break
        self.log.flush()
        return out

    def cmd(self, text, seconds=6.0):
        self.s.reset_input_buffer()
        self.log.write(f"\n>>> {text}\n")
        self.s.write((text + "\r\n").encode())
        return self.read_for(seconds, PROMPT_RE)

    def close(self):
        self.s.close()


def write_env(password, user="admin"):
    """Grava a senha em ~/.simut-bench.env (modo 600), guardando o arquivo anterior em .bak.
    As outras linhas ficam; a senha nunca e impressa."""
    lines, seen = [], {"USER": False, "PASS": False}
    if os.path.exists(ENV_PATH):
        shutil.copy2(ENV_PATH, ENV_PATH + ".bak")
        with open(ENV_PATH, encoding="utf-8") as fh:
            for line in fh:
                m = re.match(r"\s*export\s+SIMUT_WEB_(USER|PASS)=", line)
                if m:
                    seen[m.group(1)] = True
                    line = f"export SIMUT_WEB_{m.group(1)}={user if m.group(1) == 'USER' else password}\n"
                lines.append(line)
    for key, value in (("USER", user), ("PASS", password)):
        if not seen[key]:
            lines.append(f"export SIMUT_WEB_{key}={value}\n")
    with open(ENV_PATH, "w", encoding="utf-8") as fh:
        fh.writelines(lines)
    os.chmod(ENV_PATH, stat.S_IRUSR | stat.S_IWUSR)


def stage_boot(args, report, log):
    r = report.setdefault("boot", {"checks": {}, "manual": {}})
    ck = r["checks"]
    before = port_of(args.target_serial, "o alvo")
    ck["alvo no USB antes do reset"] = bool(before)
    if not before:
        return
    before_node = os.path.realpath(before)
    hand = Hand(args.hand_serial) if args.hand_serial else None
    if hand:
        reply = hand.cmd("RESET")
        ck["a mao confirmou o RESET"] = reply.startswith("OK")
        if not ck["a mao confirmou o RESET"]:
            print(f"  a mao respondeu {reply!r}")
    else:
        print("  sem --hand-serial: reinicie o alvo (RESET ou religue o USB) nos proximos 20 s")
    # O reset so aconteceu se o alvo sumiu do USB (ou voltou noutro no); so entao a porta
    # nova e a do boot novo, e nao o no antigo que ainda nao saiu de /dev.
    deadline = time.time() + (5 if hand else 20)
    while time.time() < deadline:
        p = port_of(args.target_serial, "o alvo")
        if not p or os.path.realpath(p) != before_node:
            break
        time.sleep(0.05)
    else:
        ck["o alvo saiu do USB (o reset aconteceu)"] = False
        return
    ck["o alvo saiu do USB (o reset aconteceu)"] = True
    port = wait_port(args.target_serial, "o alvo", 30)
    ck["o alvo voltou ao USB"] = bool(port)
    if not port:
        return
    con = Console(port, log)
    try:
        # A serial so recebe o que o firmware imprime com a porta aberta, e a porta abre
        # depois de o USB enumerar: o que veio antes se perde. A prova de PIO e o log
        # gravado, abaixo; isto aqui so registra o que se viu.
        boot = con.read_for(args.boot_seconds)
        r["boot_lines"] = len(boot.splitlines())
        r["boot_pio_failures"] = sorted({ln.strip() for ln in boot.splitlines() if PIO_FAIL_RE.search(ln)})
        con.cmd("")
        info = con.cmd("show system info")
        m = re.search(r"Firmware:\s*(\S+)", info)
        r["firmware"] = m.group(1) if m else None
        ck["show system info diz a versao"] = bool(m)
        net = con.cmd("show net status")
        r["net_status"] = " | ".join(ln.strip() for ln in net.splitlines() if ln.strip() and not PROMPT_RE.search(ln))[:400]
        logtxt = con.cmd("show system log", seconds=10.0)
        fails = sorted({ln.strip() for ln in logtxt.splitlines() if PIO_FAIL_RE.search(ln)})
        r["pio_failures"] = fails
        ck["log sem falha de PIO"] = not fails
        if args.admin_reset:
            out = con.cmd("system admin reset confirm", seconds=8.0)
            m = OTP_RE.search(out)
            ck["system admin reset deu uma senha"] = bool(m)
            if m:
                write_env(m.group(1))
                r["admin"] = f"senha nova em {ENV_PATH} (o anterior em .bak); troca forcada no primeiro login"
    finally:
        con.close()
    r["manual"]["LCD mostra o SIMUT"] = "pendente: confira na placa"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--target-serial", default=os.environ.get("SIMUT_TARGET_SERIAL"))
    ap.add_argument("--hand-serial", default=os.environ.get("PICO_HAND_SERIAL"))
    ap.add_argument("--stages", default=",".join(STAGES), help=f"separadas por virgula: {', '.join(STAGES)}")
    ap.add_argument("--boot-seconds", type=float, default=20.0, help="quanto da serial ler depois do reset")
    ap.add_argument("--admin-reset", action="store_true",
                    help="gera a senha do admin pelo console e a grava em ~/.simut-bench.env")
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()
    if not args.target_serial:
        sys.exit("wire_bench: diga qual placa e o alvo: --target-serial (ou SIMUT_TARGET_SERIAL)")
    if args.hand_serial and args.hand_serial.upper() == args.target_serial.upper():
        sys.exit("wire_bench: o alvo e a mao tem o mesmo serial")
    stages = [s.strip() for s in args.stages.split(",") if s.strip()]
    unknown = [s for s in stages if s not in STAGES]
    if unknown:
        sys.exit(f"wire_bench: etapa desconhecida: {', '.join(unknown)} (ha: {', '.join(STAGES)})")
    try:
        import serial  # noqa: F401
    except ImportError:
        sys.exit("wire_bench: falta o pyserial (pip install pyserial)")

    os.makedirs(args.out, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    logp = os.path.join(args.out, f"wire_bench_{stamp}.serial.txt")
    report = {"when": stamp, "target": args.target_serial, "hand": args.hand_serial, "stages": stages}
    with open(logp, "w", encoding="utf-8") as log:
        for st in stages:
            print(f"== {st}")
            globals()[f"stage_{st}"](args, report, log)
    failed = []
    for st in stages:
        for name, ok in report.get(st, {}).get("checks", {}).items():
            print(f"  {'ok   ' if ok else 'FALHA'} {st}: {name}")
            if not ok:
                failed.append(f"{st}: {name}")
        for name, state in report.get(st, {}).get("manual", {}).items():
            print(f"  manual {st}: {name} ({state})")
    report["failed"] = failed
    rep = os.path.join(args.out, f"wire_bench_{stamp}.json")
    with open(rep, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=2, ensure_ascii=False)
    print(f"wire_bench: {len(failed)} falha(s); relatorio {rep}, serial {logp}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
