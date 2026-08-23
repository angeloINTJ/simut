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

Requisitos: pyserial, paho-mqtt, requests, docker (ou broker em SIMUT_MQTT_BROKER).
"""
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


def first_active_sensor(dev):
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
        if re.search(r"ALARMES:\s*LIGADO|ALARMS:\s*ON", line):
            lim = re.search(r"\[T:\s*([-\d.]+)\s*\.\.\s*([-\d.]+)\]", line)
            if lim:
                return cur, float(lim.group(1)), float(lim.group(2))
    r2 = dev.cmd("show sensors", 3)
    m = re.search(r"GPIO=(\d+)\s*\|\s*(DS18B20|DHT22|BME280|BMP280)", r2)
    if m:
        return int(m.group(1)), None, None
    return None, None, None


def clear_limit_edge(dev, gpio):
    dev.cmd(f"sensor {gpio} tmax 100")
    time.sleep(12)


def main():
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
    dev.cmd("alarm set qmax 16")
    # templates default (token único, sem espaços — limitação do CLI tokenizado)
    dev.cmd('alarm set line {"ts":{TS},"id":"{ID}","val":{val},"err":"{err}","seq":{seq}}')
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

    payload = {"sys": {"t_transport": 1, "m_topic": "simut/data",
                       "m_cid": "simut-alarm-hw"}}
    r = web.post(f"http://{ip}/api/commit_all",
                 data={"_payload": json.dumps(payload)}, timeout=30)
    check("commit_all aceito", r.status_code == 200, f"HTTP {r.status_code} {r.text[:120]}")

    ip2 = dev.wait_online(cap=120)
    check("device voltou pós-reboot", ip2 is not None)

    # ── broker: assina e ack ──
    print("\n[03] Broker: assinar simut/data/alarm e publicar ACK")
    got = {"payloads": [], "acked": 0}

    def on_connect(client, userdata, flags, reason_code, properties=None):
        client.subscribe("simut/data/alarm")

    def on_message(client, userdata, msg):
        try:
            body = msg.payload.decode("utf-8", "replace")
            got["payloads"].append(body)
            recs = json.loads(body) if body.startswith("[") else []
            seqs = [r["seq"] for r in recs if isinstance(r, dict) and "seq" in r]
            if seqs:
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
    gpio, tmin0, tmax0 = first_active_sensor(dev)
    check("sensor real em GPIO", gpio is not None, str(gpio))
    if gpio is not None:
        clear_limit_edge(dev, gpio)
        before = len(got["payloads"])
        dev.cmd(f"sensor {gpio} tmax -100")
        deadline = time.time() + 120
        while time.time() < deadline and len(got["payloads"]) == before:
            cli.loop()
            time.sleep(2)
        check("payload MQTT chegou", len(got["payloads"]) > before,
              str(got["payloads"][-1:])[:200])
        if tmax0 is not None:
            dev.cmd(f"sensor {gpio} tmin {tmin0}")
            dev.cmd(f"sensor {gpio} tmax {tmax0}")
        else:
            dev.cmd(f"sensor {gpio} tmax 100")

    # ── confirmação esvazia a fila ──
    print("\n[05] Fila esvazia após o ACK")
    deadline = time.time() + 60
    size, cap = None, None
    while time.time() < deadline:
        cli.loop()
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
