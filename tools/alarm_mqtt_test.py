#!/usr/bin/env python3
"""
SIMUT v21 — Teste em hardware da 2ª linha de telemetria sobre MQTT com
confirmação de recebimento por aplicação (ACK).

Fluxo:
  1. Broker local (docker eclipse-mosquitto ou mosquitto do host; senão,
     aponte SIMUT_MQTT_BROKER para um broker existente).
  2. Config via CLI (wifi, tel server/port = broker, alarm set on/json/qmax).
  3. Transporte MQTT via web: /api/commit_all (a CLI não expõe transporte) —
     conta throwaway criada pelo CLI, mesmo padrão do web_test_suite.py.
  4. Borda de limite (tmax apertado) → payload no tópico simut/data/alarm.
  5. O script publica {"seq":[...]} em simut/data/alarm/ack.
  6. A fila do device esvazia (alarm show → fila 0) — apagado conforme
     confirmação.

Uso:
  SIMUT_WIFI_SSID=... SIMUT_WIFI_PASS=... python3 tools/alarm_mqtt_test.py
  ... --tel-off     linha convencional desligada (tel interval 0): o ACK só é
                    lido se o loop( ) do MQTT roda mesmo assim (achado 28 do
                    manual v2.7.1; até 2026-10-01 a fila nunca esvaziava)
  ... --burst N     N idas e voltas do limite SEM confirmar, com qmax 32: o lote
                    passa de 2048 B, o buffer de fábrica do PubSubClient (achado
                    29; até 2026-10-01 o publish falhava calado para sempre).
                    Depois confirma tudo e exige fila 0. O template ganha 120 B
                    de enchimento (~191 B por registro): 11 registros passam de
                    2048 B, e N=12 sobra (nem toda borda vira registro).
  ... --refused-edge  #161: qmax 1 e nada confirmado, então a 2ª borda do mesmo
                    canal é recusada (fila cheia, descartados +1). Depois
                    confirma tudo e exige que essa borda, ainda ativa, chegue
                    com um seq novo. Até 2026-10-01 ela ficava latchada como
                    anunciada e só voltava com um reinício.

Requisitos: pyserial, paho-mqtt, requests, docker (ou broker em SIMUT_MQTT_BROKER).
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

import requests
import serial

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("instale paho-mqtt: pip install paho-mqtt")

BAUD = 115200
WIFI_SSID = os.environ.get("SIMUT_WIFI_SSID", "")
WIFI_PASS = os.environ.get("SIMUT_WIFI_PASS", "")
# vazio = device já tem WiFi persistido (migração v20→v21 preserva).

ALARM_LINE = ('{"ts":{TS},"id":"{ID}","val":{val},"alarm":{alarm},'
              '"err":{err},"seq":{seq}}')
# --burst: the same line plus 120 bytes, so that a dozen round trips pass the
# 2048 B buffer. Measured on the rig 2026-10-01: 16 round trips queued only 18
# new records (edges this close together are not all recorded), and 23 records
# of the plain line came to 1625 B — never past the size under test.
ALARM_LINE_BURST = ALARM_LINE[:-1] + ',"pad":"' + "x" * 120 + '"}'

TEST_USER = "alarmtest"
TEST_PASS = "Alarm!Test2026"
BROKER_PORT = int(os.environ.get("SIMUT_MQTT_BROKER_PORT", "1883"))
BROKER_HOST = os.environ.get("SIMUT_MQTT_BROKER", "")

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


def host_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def start_broker():
    """Broker local; retorna (host, porta) ou None. docker > mosquitto > SIMUT_MQTT_BROKER."""
    if BROKER_HOST:
        return BROKER_HOST, BROKER_PORT
    if shutil.which("docker"):
        r = subprocess.run(
            ["docker", "run", "-d", "--rm", "--name", "simut-alarm-mqtt",
             "-p", f"{BROKER_PORT}:1883", "eclipse-mosquitto:2"],
            capture_output=True, text=True)
        if r.returncode == 0:
            print("  [BROKER] eclipse-mosquitto via docker")
            return host_ip(), BROKER_PORT
        print(f"  [BROKER] docker falhou: {r.stderr[:120]}")
    if shutil.which("mosquitto"):
        subprocess.Popen(["mosquitto", "-p", str(BROKER_PORT)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)
        print("  [BROKER] mosquitto local")
        return host_ip(), BROKER_PORT
    return None, None


def stop_broker():
    if shutil.which("docker"):
        subprocess.run(["docker", "rm", "-f", "simut-alarm-mqtt"],
                       capture_output=True)


class Dev:
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
                    if self._prompt(5):
                        print(f"  [CONNECT] {port} OK")
                        return True
                    self.ser.close()
                    self.ser = None
                except Exception:
                    self.ser = None
            time.sleep(1)
        return False

    def _prompt(self, timeout=8):
        try:
            self.ser.write(b"\r\n")
            self.ser.flush()
        except Exception:
            return False
        buf = b""
        t0 = time.time()
        while time.time() - t0 < timeout:
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

    def wait_online(self, cap=60):
        """Espera o device voltar com IP (pós-reboot do commit_all)."""
        deadline = time.time() + cap
        while time.time() < deadline:
            if not self.ser or not self.ser.is_open:
                self._connect()
            r = self.cmd("show net status", 2)
            m = re.search(r"\d+\.\d+\.\d+\.\d+", r)
            if m:
                return m.group(0)
            time.sleep(3)
        return None


def temp_slots(dev):
    """[slot, alarmes ligados, tmin, tmax] de cada slot ativo que mede temperatura."""
    r = dev.cmd("show sensors", 3)
    slots, cur = [], None
    for line in r.split("\n"):
        # [Slot 04] GPIO=4,SDA/5 | BMP280   | T+P   | SALA 2
        m = re.search(r"\[Slot\s+(\d+)\]\s+GPIO=\S+\s*\|\s*\S+\s*\|\s*(\S+)", line)
        if m:
            cur = [int(m.group(1)), False, None, None] if "T" in m.group(2).split("+") else None
            if cur:
                slots.append(cur)
            continue
        if cur and re.search(r"ALARMES:\s*LIGADO|ALARMS:\s*ON", line):
            cur[1] = True
            lim = re.search(r"\[T:\s*([-\d.]+)\s*\.\.\s*([-\d.]+)\]", line)
            if lim:
                cur[2], cur[3] = float(lim.group(1)), float(lim.group(2))
    return slots


def pick_alarm_slot(dev):
    """Slot que vai gerar a borda; retorna (slot, tmin, tmax, ligado_pelo_teste).

    A CLI endereça o SLOT (`sensor <slot> tmax ...`), não o GPIO: até 01/10/2026
    isto devolvia o GPIO do cabeçalho, o que só acertava onde o slot N mora no
    GPIO N. E exigia um slot com alarmes já ligados: a config da bancada tem
    todos desligados, nenhuma borda saía e o teste falhava em qualquer imagem.
    Agora um slot desligado é ligado para o teste (só na RAM) e desligado no fim."""
    slots = temp_slots(dev)
    on = [s for s in slots if s[1] and s[2] is not None]
    if on:
        return on[0][0], on[0][2], on[0][3], False
    if not slots:
        return None, None, None, False
    slot = slots[0][0]
    dev.cmd(f"sensor {slot} alarm on")
    s = next((x for x in temp_slots(dev) if x[0] == slot), None)
    return slot, (s[2] if s else None), (s[3] if s else None), True


def clear_limit_edge(dev, slot):
    # The band opened on both sides: a slot just switched on may sit below its
    # stored minimum, and then raising tmax would not clear anything.
    dev.cmd(f"sensor {slot} tmin -100")
    dev.cmd(f"sensor {slot} tmax 100")
    time.sleep(12)


def main():
    ap = argparse.ArgumentParser(description="SIMUT: linha de alarmes por MQTT com ACK")
    ap.add_argument("--tel-off", action="store_true",
                    help="tel interval 0: a linha convencional desligada")
    ap.add_argument("--burst", type=int, default=0, metavar="N",
                    help="N idas e voltas do limite sem confirmar (qmax 32)")
    ap.add_argument("--refused-edge", action="store_true",
                    help="#161: borda recusada pela fila cheia (qmax 1) tem de chegar depois do ACK")
    args = ap.parse_args()

    broker_host, broker_port = start_broker()
    if not broker_host:
        sys.exit("  [FATAL] sem broker: instale mosquitto/docker ou defina SIMUT_MQTT_BROKER")

    dev = Dev()
    if dev.ser is None:
        stop_broker()
        sys.exit("  [FATAL] device Pico W não encontrado em /dev/ttyACM*")

    # ── config CLI ──
    print("\n[01] Config CLI + conta throwaway")
    dev.cmd("enable")
    dev.cmd("configure terminal")
    if WIFI_SSID:
        dev.cmd("wifi ssid " + WIFI_SSID)
        dev.cmd("wifi pass " + WIFI_PASS)
    dev.cmd(f"tel server {broker_host}")
    dev.cmd(f"tel port {broker_port}")
    dev.cmd("tel crypto off")
    dev.cmd("alarm set on")
    dev.cmd("alarm set mode json")
    dev.cmd("alarm set qmax 1" if args.refused_edge
            else "alarm set qmax 32" if args.burst else "alarm set qmax 16")
    if args.tel_off:
        dev.cmd("tel interval 0")
    dev.cmd(f"user del {TEST_USER}")
    dev.cmd(f"user add {TEST_USER} {TEST_PASS}")
    dev.cmd(f"user perm {TEST_USER} admin")
    dev.cmd("end")
    dev.cmd("write memory")

    ip = dev.wait_online()
    check("device online", ip is not None, str(ip))
    if not ip:
        stop_broker()
        sys.exit(1)

    # ── transporte MQTT via commit_all ──
    print("\n[02] Transporte MQTT via /api/commit_all (reboot)")
    web = requests.Session()
    r = web.get(f"http://{ip}/api/login_init", timeout=15)
    nonce = r.json().get("nonce", "")
    r = web.post(f"http://{ip}/api/login", data={
        "user": TEST_USER,
        "pass": hashlib.sha256(TEST_PASS.encode("latin-1")).hexdigest(),
        "nonce": nonce,
    }, headers={"Content-Type": "application/x-www-form-urlencoded"}, timeout=15)
    check("login web OK", "SIMUTSESS" in web.cookies.get_dict(), str(r.status_code))

    # The line template goes by the web, not by `alarm set line`: the CLI's
    # value field is 64 bytes (CliDemand::strVal2), and this template is 75.
    # Until 2026-10-01 it was sent by the CLI and arrived cut after "err":{err},
    # — no {seq}, so nothing could be acknowledged, and a trailing comma that
    # made every batch invalid JSON. Measured on the rig on main 2721eb2:
    # [{"ts":1790859522,"id":"tSTM0009","val":24.75,"alarm":"alarm",]
    payload = {"sys": {"t_transport": 1, "m_topic": "simut/data",
                       "m_cid": "simut-alarm-hw", "a_mode": 0,
                       "a_line": ALARM_LINE_BURST if args.burst else ALARM_LINE}}
    r = web.post(f"http://{ip}/api/commit_all",
                 data={"_payload": json.dumps(payload)}, timeout=30)
    check("commit_all aceito", r.status_code == 200, f"HTTP {r.status_code} {r.text[:120]}")

    ip2 = dev.wait_online(cap=120)
    check("device voltou pós-reboot", ip2 is not None)

    # ── broker: assina e ack ──
    print("\n[03] Broker: assinar simut/data/alarm e publicar ACK")
    got = {"payloads": [], "acked": 0, "hold": bool(args.burst or args.refused_edge)}

    def on_connect(client, userdata, flags, reason_code, properties=None):
        client.subscribe("simut/data/alarm")

    def on_message(client, userdata, msg):
        try:
            body = msg.payload.decode("utf-8", "replace")
            got["payloads"].append(body)
            recs = json.loads(body) if body.startswith("[") else []
            seqs = [r["seq"] for r in recs if isinstance(r, dict) and "seq" in r]
            if seqs and got["hold"]:
                print(f"  [HOLD] {len(seqs)} registros, {len(body)} B — sem confirmar")
            elif seqs:
                client.publish("simut/data/alarm/ack", json.dumps({"seq": seqs}))
                got["acked"] += len(seqs)
                print(f"  [ACK] confirmados {seqs}")
        except Exception as e:
            print(f"  [WARN] on_message: {e} body={body[:160]!r}")

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    cli.on_connect = on_connect
    cli.on_message = on_message
    cli.connect(broker_host, broker_port, 30)
    cli.loop_start()
    time.sleep(1)

    # ── borda de limite ──
    print("\n[04] Borda de limite → payload no tópico /alarm")
    slot, tmin0, tmax0, switched_on = pick_alarm_slot(dev)
    check("slot com temperatura", slot is not None, str(slot))
    if slot is not None:
        clear_limit_edge(dev, slot)
        before = len(got["payloads"])
        dev.cmd(f"sensor {slot} tmax -100")
        deadline = time.time() + 120
        while time.time() < deadline and len(got["payloads"]) == before:
            time.sleep(2)
        check("payload MQTT chegou", len(got["payloads"]) > before,
              str(got["payloads"][-1:])[:200])
        dev.cmd(f"sensor {slot} tmax 100")

    # ── rajada: o lote passa do buffer de fábrica ──
    if args.burst and slot is not None:
        print(f"\n[04b] Rajada de {args.burst} idas e voltas sem confirmar")
        for _ in range(args.burst):
            dev.cmd(f"sensor {slot} tmax -100", 6)
            dev.cmd(f"sensor {slot} tmax 100", 6)
        r = dev.cmd("alarm show", 2)
        m = re.search(r"(?:fila|queue)\s+(\d+)/(\d+)", r)
        print(f"  fila depois da rajada: {m.group(0) if m else r[:80]!r}")
        got["hold"] = False                     # daqui em diante, confirma
        deadline = time.time() + 90
        while time.time() < deadline and not any(len(p) > 2048 for p in got["payloads"]):
            time.sleep(2)
        big = max((len(p) for p in got["payloads"]), default=0)
        check("lote maior que 2048 B publicado", big > 2048, f"maior={big} B")

    # ── #161: a borda que a fila cheia recusou chega quando há espaço ──
    if args.refused_edge and slot is not None:
        print("\n[04c] Borda recusada pela fila cheia, e o que acontece com ela depois do ACK (#161)")

        def queue_state():
            r = dev.cmd("alarm show", 2)
            m = re.search(r"(?:fila|queue)\s+(\d+)/(\d+)\s*\|\s*(?:descartados|dropped)\s+(\d+)", r)
            return tuple(int(x) for x in m.groups()) if m else None

        def seen_seqs():
            out = set()
            for body in list(got["payloads"]):
                try:
                    recs = json.loads(body) if body.startswith("[") else []
                except ValueError:
                    continue
                out |= {r["seq"] for r in recs if isinstance(r, dict) and "seq" in r}
            return out

        time.sleep(12)                        # [04] put the limit back: the edge ends
        st = queue_state()
        check("fila cheia e nada confirmado", st is not None and st[0] == st[1] == 1, str(st))
        before_seqs, dropped0 = seen_seqs(), (st[2] if st else 0)
        dev.cmd(f"sensor {slot} tmax -100")   # the same channel trips again: a new edge
        time.sleep(20)                        # two passes of debounce, and a margin
        st = queue_state()
        check("a borda nova foi recusada (descartados +1)",
              st is not None and st[2] == dropped0 + 1, f"{st}, antes {dropped0}")
        got["hold"] = False                   # the server comes back and confirms
        deadline = time.time() + 120
        fresh = set()
        while time.time() < deadline and not fresh:
            fresh = seen_seqs() - before_seqs
            time.sleep(2)
        check("a borda recusada, ainda ativa, chegou depois do ACK", bool(fresh),
              f"seqs vistos {sorted(seen_seqs())}, antes {sorted(before_seqs)}")
        dev.cmd(f"sensor {slot} tmax 100")

    if slot is not None:
        # Alarms off BEFORE the stored limits come back: on the bench the
        # "fridge" probe sits at room temperature, and its own band put it back
        # in alarm — one more record the run had not asked for.
        if switched_on:
            dev.cmd(f"sensor {slot} alarm off")
        if tmin0 is not None:
            dev.cmd(f"sensor {slot} tmin {tmin0}")
            dev.cmd(f"sensor {slot} tmax {tmax0}")

    # ── confirmação esvazia a fila ──
    print("\n[05] Fila esvazia após o ACK")
    deadline = time.time() + 60
    size, cap = None, None
    while time.time() < deadline:
        r = dev.cmd("alarm show", 2)
        m = re.search(r"(?:fila|queue)\s+(\d+)/(\d+)", r)
        if m:
            size, cap = int(m.group(1)), int(m.group(2))
            if size == 0:
                break
        time.sleep(4)
    check("fila 0 após ACK", size == 0, f"fila={size}/{cap}")
    check("ACKs publicados", got["acked"] > 0, str(got["acked"]))

    cli.loop_stop()
    stop_broker()

    print(f"\n== RESULTADO: {PASSED} passed, {FAILED} failed ==")
    sys.exit(0 if FAILED == 0 else 1)


if __name__ == "__main__":
    main()
