#!/usr/bin/env python3
"""
SIMUT v21 — Suíte de testes em hardware da 2ª linha de telemetria (alarmes).

Valida, no ferro (Pico W pela USB serial), o fluxo completo da linha de
alarmes sobre HTTP (e, com --tls, sobre HTTPS herdando a config de
criptografia da linha convencional):

  1. Config CLI: alarm set on/mode/qmax/path; tel server/port/path.
  2. Borda de limite: aperta tmax num slot ativo com sensor real →
     registro com {ts, id-com-prefixo, valor} chega ao coletor local.
  3. Borda de erro: define um DS18B20 fantasma → 3 erros consecutivos →
     registro com "err" chega ao coletor.
  4. Fila RAM + confirmação: o coletor responde 2xx e a fila do device
     esvazia (alarm show → "fila 0/...") — apagado conforme recebimento.
  5. Métricas: show metrics tem Enfileirados == Confirmados e 0 descartados.
  6. Modos: custom (template editável) e CSV produzem os payloads esperados.
  7. --tls: tel crypto on → o mesmo fluxo trafega cifrado (coletor TLS com
     cert autoassinado; o device usa setInsecure — cifrado, não autenticado).

Uso:
  SIMUT_WIFI_SSID=... SIMUT_WIFI_PASS=... python3 tools/alarm_hw_test.py [--tls]

Requisitos: pyserial; firmware pico_w_test flashado no device; WiFi no host
na MESMA rede do device (o device conecta no SSID e alcança o host pelo IP
que este script descobre via rota default).
"""
import argparse
import atexit
import glob
import hashlib
import http.server
import json
import os
import re
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

import requests
import serial

BAUD = 115200
WIFI_SSID = os.environ.get("SIMUT_WIFI_SSID", "")
WIFI_PASS = os.environ.get("SIMUT_WIFI_PASS", "")
# vazio = o device já tem WiFi persistido (migração v20→v21 preserva) — a
# suíte apenas aponta o servidor/coletor.

COLLECTOR_PORT = int(os.environ.get("SIMUT_ALARM_COLLECTOR_PORT", "18080"))


def host_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


# ── coletor ─────────────────────────────────────────────────────────────────
class CollectorState:
    def __init__(self):
        self.lock = threading.Lock()
        self.alarm_batches = []   # list[list[dict]]
        self.conventional = []
        self.tls_hits = 0

    def record(self, path, body):
        with self.lock:
            if path.endswith("/alarm"):
                # JSON (modo 0) vira lista; custom/CSV ficam como texto cru,
                # com o header no campo _header e o corpo inteiro em _raw.
                try:
                    parsed = json.loads(body)
                except Exception:
                    parsed = [{"_raw": body,
                               "_header": body.splitlines()[0] if body else ""}]
                self.alarm_batches.append(parsed if isinstance(parsed, list)
                                          else [parsed])
            else:
                self.conventional.append(body)

STATE = CollectorState()

def make_handler(tls: bool):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def do_POST(self):
            n = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(n).decode("utf-8", "replace") if n else ""
            STATE.record(self.path, body)
            if tls:
                STATE.tls_hits += 1
            # 2xx = confirmação de recebimento — o device esvazia a fila aqui
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"ok":true}')

        def do_GET(self):
            if self.path == "/stats":
                with STATE.lock:
                    out = {
                        "alarm_batches": STATE.alarm_batches,
                        "alarm_records": sum(len(b) for b in STATE.alarm_batches),
                        "tls_hits": STATE.tls_hits,
                    }
                data = json.dumps(out).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            else:
                self.send_response(404)
                self.end_headers()
    return Handler


class Collector:
    def __init__(self, tls=False):
        self.tls = tls
        self.httpd = None
        self.thread = None

    def start(self):
        if self.tls:
            td = tempfile.mkdtemp(prefix="alarmtls")
            key = os.path.join(td, "k.pem")
            crt = os.path.join(td, "c.pem")
            subprocess.run(
                ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                 "-keyout", key, "-out", crt, "-days", "2",
                 "-subj", "/CN=simut-alarm-test"],
                check=True, capture_output=True,
            )
            self.httpd = http.server.ThreadingHTTPServer(
                ("0.0.0.0", COLLECTOR_PORT), make_handler(True))
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(crt, key)
            self.httpd.socket = ctx.wrap_socket(self.httpd.socket, server_side=True)
        else:
            self.httpd = http.server.ThreadingHTTPServer(
                ("0.0.0.0", COLLECTOR_PORT), make_handler(False))
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def stop(self):
        if self.httpd:
            self.httpd.shutdown()
            self.httpd.server_close()


# ── device ──────────────────────────────────────────────────────────────────
# ── config do aparelho: fotografar antes, devolver depois ───────────────────
#
# Esta suíte reescreve a telemetria e a linha de alarmes do aparelho — servidor,
# porta, path, modo, qmax e os dois templates — e até 21/09/2026 não devolvia
# nada. Quem rodasse ela numa bancada apontada para o coletor de verdade
# (192.168.3.206:8080 /telemetry) ficava com o aparelho falando com a bancada,
# em CSV, com os templates do teste. Ninguém percebe até faltar dado do outro
# lado.
#
# A devolução vai pela WEB, não pela CLI, e por um motivo medido: o
# `alarm set line` passa pelo `strVal2[64]` do CommandParser e **corta em 63
# caracteres em silêncio** (medido em 21/09: 70 entram, 63 ficam). O template
# real deste aparelho tem 141. Só o commit_all carrega o valor inteiro.

# t_key fica de fora de propósito: o /api/config devolve a chave MASCARADA
# ("Bobi***"), e mandar a máscara de volta gravaria a máscara.
CFG_FIELDS = ("t_srv", "t_port", "t_path", "t_int", "t_bat", "t_mode", "t_sec",
              "a_en", "a_mode", "a_qmax", "a_path", "a_glob", "a_line", "a_sep",
              "t_glob", "t_line")


def _web_session(host):
    user = os.environ.get("SIMUT_WEB_USER", "admin")
    pw = os.environ["SIMUT_WEB_PASS"]
    s = requests.Session()
    n = s.get(f"http://{host}/api/login_init", timeout=10).json().get("nonce", "")
    s.post(f"http://{host}/api/login",
           data={"user": user,
                 "pass": hashlib.sha256(pw.encode("latin-1")).hexdigest(),
                 "nonce": n},
           headers={"Content-Type": "application/x-www-form-urlencoded"},
           timeout=15, allow_redirects=False)
    if "SIMUTSESS" not in s.cookies.get_dict():
        raise RuntimeError("login web recusado")
    return s


def config_snapshot(host):
    """Devolve o /api/config do aparelho, ou None se não der para fotografar."""
    if not os.environ.get("SIMUT_WEB_PASS"):
        print("  [WARN] sem SIMUT_WEB_PASS: a config do aparelho NAO sera "
              "restaurada no fim — ela ficara apontada para esta bancada")
        return None
    try:
        snap = _web_session(host).get(f"http://{host}/api/config", timeout=20).json()
        print(f"  [CFG] snapshot da config tirado ({len(snap)} campos)")
        return snap
    except Exception as e:
        print(f"  [WARN] snapshot da config falhou ({e}) — nada sera restaurado")
        return None


def config_restore(host, snap):
    """Põe a config de volta e CONFERE lendo o que o aparelho ficou."""
    if not snap:
        return
    payload = {k: snap.get(k) for k in CFG_FIELDS if k in snap}
    for k in ("a_en", "t_sec"):
        if k in payload:
            payload[k] = 1 if payload[k] else 0
    try:
        s = _web_session(host)
        r = s.post(f"http://{host}/api/commit_all",
                   data={"_payload": json.dumps({"sys": payload})}, timeout=40)
        print(f"  [CFG] restauracao: HTTP {r.status_code} {r.text[:80]}")
        time.sleep(4)
        for _ in range(20):
            try:
                back = _web_session(host).get(f"http://{host}/api/config",
                                              timeout=20).json()
                break
            except Exception:
                time.sleep(3)
        else:
            print("  [CFG] FALHOU: aparelho nao respondeu depois do commit")
            return
        drift = {k: (payload[k], back.get(k)) for k in payload
                 if back.get(k) != payload[k]}
        if drift:
            print(f"  [CFG] NAO VOLTOU: {drift}")
        else:
            print(f"  [CFG] config restaurada e conferida ({len(payload)} campos)")
    except Exception as e:
        print(f"  [CFG] restauracao falhou: {e}")


class Device:
    def __init__(self):
        self.ser = None
        self._connect()

    def _connect(self):
        for _ in range(20):
            for port in sorted(glob.glob("/dev/ttyACM*")):
                try:
                    self.ser = serial.Serial(port, BAUD, timeout=5)
                    self.ser.dtr = True
                    time.sleep(1)
                    self.ser.reset_input_buffer()
                    if self._wait_prompt(5):
                        print(f"  [CONNECT] {port} OK")
                        return True
                    self.ser.close()
                    self.ser = None
                except Exception:
                    self.ser = None
            time.sleep(1)
        return False

    def _wait_prompt(self, timeout=8):
        try:
            self.ser.write(b"\r\n")
            self.ser.flush()
        except Exception:
            return False
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            c = self.ser.read(1)
            if c:
                buf += c
                if b"SIMUT>" in buf or b"SIMUT#" in buf or b"SIMUT(config)" in buf:
                    return True
        return False

    def cmd(self, text, wait=2.0):
        for _ in (1, 2):
            try:
                self.ser.write((text + "\r\n").encode())
                time.sleep(wait)
                data = self.ser.read(8192)
                return data.decode("utf-8", errors="replace")
            except Exception:
                if not self._connect():
                    return ""
                try:
                    self.ser.write(b"enable\r\n")
                    time.sleep(1.0)
                    self.ser.read(4096)
                except Exception:
                    return ""
        return ""


PASSED = 0
FAILED = 0

def check(name, cond, extra=""):
    global PASSED, FAILED
    if cond:
        PASSED += 1
        print(f"  [PASS] {name}")
    else:
        FAILED += 1
        print(f"  [FAIL] {name} {extra}")


def clear_limit_edge(dev, gpio):
    """Garante borda fresca: coloca o valor em faixa e espera 2 ciclos de
    checkAlarmConditions (~12s) para os bits de trip/cand zerarem."""
    dev.cmd(f"sensor {gpio} tmax 100")
    time.sleep(12)


def wait_for(pred, timeout, step=2.0, what="condição"):
    deadline = time.time() + timeout
    while time.time() < deadline:
        v = pred()
        if v:
            return v
        time.sleep(step)
    print(f"  [WARN] timeout esperando {what}")
    return None


def first_active_sensor(dev: Device):
    """Primeiro slot ATIVO com alarmes LIGADOS; retorna (gpio, tmin, tmax)."""
    r = dev.cmd("show sensors", 3)
    cur = None
    for line in r.split("\n"):
        m = re.search(r"Slot\s+(\d+)\].*?GPIO=(\d+)", line)
        if m:
            cur = int(m.group(2))
            continue
        if cur is None:
            continue
        mm = re.search(r"ALARMES:\s*LIGADO|ALARMS:\s*ON", line)
        if mm:
            lim = re.search(r"\[T:\s*([-\d.]+)\s*\.\.\s*([-\d.]+)\]", line)
            if lim:
                return cur, float(lim.group(1)), float(lim.group(2))
    # fallback: primeiro sensor ativo (mesmo sem alarmes — o teste liga)
    r2 = dev.cmd("show sensors", 3)
    m = re.search(r"GPIO=(\d+)\s*\|\s*(DS18B20|DHT22|BME280|BMP280)", r2)
    if m:
        return int(m.group(1)), None, None
    return None, None, None


def parse_alarm_show(dev: Device):
    r = dev.cmd("alarm show", 2)
    q = re.search(r"(?:fila|queue)\s+(\d+)/(\d+)", r)
    return (int(q.group(1)), int(q.group(2))) if q else (None, None)


def parse_metrics(dev: Device):
    r = dev.cmd("show metrics", 2)
    keys = {
        "Enfileirados": ["Enfileirados", "Queued"],
        "Enviados": ["Enviados", "Sent"],
        "Confirmados": ["Confirmados", "Acked"],
        "Descartados": ["Descartados", "Dropped"],
        "Registros 'err'": ["Registros 'err'", "'err' records"],
    }
    out = {}
    for canon, names in keys.items():
        out[canon] = -1
        for name in names:
            m = re.search(re.escape(name) + r":\s+(\d+)", r)
            if m:
                out[canon] = int(m.group(1))
                break
    return out


def main():
    global PASSED, FAILED
    ap = argparse.ArgumentParser()
    ap.add_argument("--tls", action="store_true", help="segunda passada com tel crypto on (HTTPS)")
    args = ap.parse_args()

    print("== SIMUT v21 — suíte de hardware da linha de alarmes ==")
    print(f"   coletor em {host_ip()}:{COLLECTOR_PORT} (TLS={args.tls})")

    dev = Device()
    if dev.ser is None:
        sys.exit("  [FATAL] device Pico W não encontrado em /dev/ttyACM*")

    # atexit e não try/finally de propósito: o corpo desta suíte chama
    # sys.exit( ) em vários pontos, e um finally só cobriria o bloco em que
    # estivesse. Assim a devolução acontece por qualquer saída, inclusive a que
    # aborta no meio — que é exatamente quando a config fica torta.
    dev_host = os.environ.get("SIMUT_HOST", "192.168.3.24")
    snap = config_snapshot(dev_host)
    atexit.register(config_restore, dev_host, snap)

    col = Collector(tls=args.tls)
    col.start()
    time.sleep(0.3)

    # Modos CLI: 'enable' → SIMUT# (priv); 'configure terminal' → config;
    # sensores = priv (#); tel/alarm set = config; show/alarm show = exec.

    # ── 1. config CLI ──
    print("\n[01] Config da telemetria + linha de alarmes")
    dev.cmd("enable")
    dev.cmd("configure terminal")
    if WIFI_SSID:
        dev.cmd("wifi ssid " + WIFI_SSID)
        dev.cmd("wifi pass " + WIFI_PASS)
    dev.cmd(f"tel server {host_ip()}")
    dev.cmd(f"tel port {COLLECTOR_PORT}")
    dev.cmd("tel path /api.php")
    dev.cmd("tel interval 60000")
    dev.cmd("tel crypto " + ("on" if args.tls else "off"))
    dev.cmd("alarm set on")
    dev.cmd("alarm set mode json")
    dev.cmd("alarm set qmax 16")
    dev.cmd("alarm set path /api.php/alarm")
    # templates default (token único, sem espaços — limitação do CLI tokenizado)
    dev.cmd('alarm set line {"ts":{TS},"id":"{ID}","val":{val},"alarm":{alarm},"err":{err},"seq":{seq}}')
    dev.cmd('alarm set glob {"dev":"{DEV}","mac":"{MAC}","alarms":[{DATA}]}')
    dev.cmd("exit")
    time.sleep(2)
    r = dev.cmd("alarm show", 2)
    check("alarm show ligado", "LIGADO" in r or "ON" in r, r[:120])

    # ── 2. borda de limite num sensor real (priv mode) ──
    print("\n[02] Borda de limite (tmax apertado) → registro com valor")
    gpio, tmin0, tmax0 = first_active_sensor(dev)
    check("sensor real encontrado em GPIO", gpio is not None, str(gpio))
    if gpio is not None:
        clear_limit_edge(dev, gpio)
        before = sum(len(b) for b in STATE.alarm_batches)
        dev.cmd(f"sensor {gpio} tmax -100")
        got = wait_for(
            lambda: sum(len(b) for b in STATE.alarm_batches) > before,
            timeout=90, what="registro de limite no coletor",
        )
        check("payload de alarme (limite) chegou", got is not None)
        if got:
            recs = [r for b in STATE.alarm_batches for r in b]
            rec = recs[-1]
            ok_id = bool(re.fullmatch(r"[tup][A-Za-z0-9]{1,20}", rec.get("id", "")))
            ok_ts = isinstance(rec.get("ts"), int) and rec["ts"] > 1600000000
            ok_seq = isinstance(rec.get("seq"), int) and rec["seq"] >= 1
            check("registro tem id com prefixo da grandeza", ok_id, str(rec.get("id")))
            check("registro tem timestamp e seq", ok_ts and ok_seq, str(rec))
        # restaura o limite original (RAM-only; nada é gravado)
        if tmin0 is not None:
            dev.cmd(f"sensor {gpio} tmin {tmin0}")
            dev.cmd(f"sensor {gpio} tmax {tmax0}")
        else:
            dev.cmd(f"sensor {gpio} tmax 100")

    # ── 3. borda de erro (priv: define + alarm on; persiste e reinicia) ──
    print("\n[03] Borda de erro (sensor fantasma) → registro 'err'")
    err_gpio = 14 if gpio != 14 else 13
    dev.cmd(f'sensor define {err_gpio} 0000000000000000 GHOST "Ghost" ds18b20')
    dev.cmd(f"sensor {err_gpio} alarm on")
    dev.cmd("write memory")
    dev.cmd("reload confirm")
    time.sleep(3)
    dev.ser.close()
    dev.ser = None
    ok = False
    for _ in range(20):
        if dev._connect():
            ok = True
            break
        time.sleep(3)
    check("device voltou do reboot", ok)
    got = wait_for(
        lambda: any(r.get("err") == "err" for b in STATE.alarm_batches for r in b),
        timeout=300, what="registro 'err' no coletor (histerese DS18 + boot)",
    )
    check("registro 'err' chegou (sensor em falha)", got is not None)
    dev.cmd(f"sensor remove {err_gpio} confirm")
    dev.cmd("write memory")

    # ── 4. fila esvazia com a confirmação ──
    print("\n[04] Fila RAM esvazia conforme confirmação de recebimento")
    size, cap = parse_alarm_show(dev)
    ok_drain = size is not None and cap is not None and size == 0
    if not ok_drain:
        time.sleep(20)
        size, cap = parse_alarm_show(dev)
        ok_drain = size is not None and size == 0
    check("alarm show → fila 0 (tudo confirmado)", ok_drain,
          f"fila={size}/{cap}")

    # ── 5. métricas ──
    print("\n[05] Métricas coerentes")
    m = parse_metrics(dev)
    print(f"   métricas: {m}")
    check("Enfileirados >= 1", m.get("Enfileirados", -1) >= 1)
    check("Confirmados == Enfileirados", m.get("Confirmados") == m.get("Enfileirados"))
    check("Descartados == 0", m.get("Descartados") == 0)
    if gpio is not None:
        check("Registros 'err' >= 1", m.get("Registros 'err'", -1) >= 1)

    # ── 6. payload custom editável ──
    print("\n[06] Payload custom (template editável)")
    dev.cmd("enable")
    dev.cmd("configure terminal")
    dev.cmd("alarm set line {CH};{SLOT};{VAL};{ERR};{SEQ}")
    dev.cmd("alarm set mode custom")
    dev.cmd("alarm set glob {DATA}")
    dev.cmd("exit")
    if gpio is not None:
        clear_limit_edge(dev, gpio)
        before = sum(len(b) for b in STATE.alarm_batches)
        dev.cmd(f"sensor {gpio} tmax -100")
        got = wait_for(
            lambda: any(
                re.search(r"[tup];\d+;[-\d.]+;;\d+", r.get("_raw", ""))
                for b in STATE.alarm_batches for r in b),
            timeout=90, what="linha custom no coletor",
        )
        check("linha custom chegou no formato editado", got is not None)
        if tmax0 is not None:
            dev.cmd(f"sensor {gpio} tmin {tmin0}")
            dev.cmd(f"sensor {gpio} tmax {tmax0}")
        else:
            dev.cmd(f"sensor {gpio} tmax 100")

    # ── 7. CSV ──
    print("\n[07] Modo CSV")
    dev.cmd("enable")
    dev.cmd("configure terminal")
    dev.cmd("alarm set mode csv")
    dev.cmd("exit")
    if gpio is not None:
        clear_limit_edge(dev, gpio)
        before = sum(len(b) for b in STATE.alarm_batches)
        dev.cmd(f"sensor {gpio} tmax -100")
        got = wait_for(
            lambda: any("seq;ts;id;v" in r.get("_header", "")
                        for b in STATE.alarm_batches for r in b
                        if isinstance(r, dict)),
            timeout=90, what="payload CSV no coletor",
        )
        check("payload CSV (header seq;ts;id;v) chegou", got is not None)
        if tmax0 is not None:
            dev.cmd(f"sensor {gpio} tmin {tmin0}")
            dev.cmd(f"sensor {gpio} tmax {tmax0}")
        else:
            dev.cmd(f"sensor {gpio} tmax 100")

    # ── 8. TLS herdado ──
    if args.tls:
        print("\n[08] Criptografia herdada (HTTPS)")
        check("coletor atendeu via TLS", STATE.tls_hits > 0, f"hits={STATE.tls_hits}")

    col.stop()

    print(f"\n== RESULTADO: {PASSED} passed, {FAILED} failed ==")
    sys.exit(0 if FAILED == 0 else 1)

if __name__ == "__main__":
    main()
