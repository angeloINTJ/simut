#!/usr/bin/env python3
"""
SIMUT v21 — Validação visual da correção do painel superior fixado (pin).

Cenário do bug reportado pelo usuário:
  "Quando o sensor que está em alarme fica no slot superior (painel TOP
   fixado), ao entrar em alarme, o slot inferior (painel BOTTOM) repete
   esse slot."

Correção: em checkAlarmConditions, a regra "o sensor em alarme aparece no
painel inferior mesmo sem estar selecionado" NÃO se aplica quando esse
sensor JÁ está fixado no painel superior — o painel inferior mantém a
seleção atual. A rotação de alarmes também nunca seleciona o slot fixado
no topo para o painel inferior.

Fluxo no hardware (Pico W via USB serial + screenshots via web):
  1. Seleciona o slot 0 (botão S0) com 'touch sim 34 215'.
  2. Fixa o painel superior no slot 0 com 'touch sim 160 70'.
  3. Dispara o alarme de limite do slot 0 ('sensor 0 tmax -100').
  4. Screenshots: TOP pisca vermelho; BOTTOM NÃO pisca (sem duplicação).
  5. Restaura os limites originais do slot 0.

Uso:
  python3 tools/validate_top_pin_alarm.py
"""
import glob
import hashlib
import io
import re
import sys
import time

import requests
import serial
from PIL import Image

BAUD = 115200
HOST = "192.168.3.24"          # device (bench)
WEB_USER = "alarmtest"
WEB_PASS = "Alarm!Test2026"

# Layout do dashboard (DisplayManager_Dashboard.cpp)
TOP_Y0, TOP_Y1 = 35, 110
BOT_Y0, BOT_Y1 = 115, 190
X0, X1 = 4, 316

def sha256_frontend(pw):
    return hashlib.sha256(pw.encode("latin-1")).hexdigest()

def red_px(img, y0, y1):
    """Conta pixels vermelho-escuro (flash de alarme C_ALARM_BG 180,30,30)."""
    n = 0
    px = img.load()
    for y in range(y0, y1):
        for x in range(X0, X1):
            r, g, b = px[x, y][:3]
            if r > 120 and g < 80 and b < 80:
                n += 1
    return n

class Dev:
    def __init__(self):
        self.ser = None
        self._connect()
    def _connect(self):
        for _ in range(30):
            for port in sorted(glob.glob("/dev/ttyACM*")):
                try:
                    self.ser = serial.Serial(port, BAUD, timeout=5)
                    self.ser.dtr = True
                    time.sleep(1)
                    self.ser.reset_input_buffer()
                    if self._wait_prompt(6):
                        print(f"  [CONNECT] {port} OK")
                        return True
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
            time.sleep(2)
        return False
    def _wait_prompt(self, timeout=6):
        try:
            self.ser.write(b"\r\n")
        except Exception:
            return False
        buf = b""
        dl = time.time() + timeout
        while time.time() < dl:
            c = self.ser.read(1)
            if c:
                buf += c
                if b"SIMUT" in buf:
                    return True
        return False
    def cmd(self, text, wait=2.0):
        for _ in (1, 2):
            try:
                self.ser.write((text + "\r\n").encode())
                time.sleep(wait)
                return self.ser.read(8192).decode("utf-8", "replace")
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

class Web:
    def __init__(self):
        self.base = f"http://{HOST}"
        self.s = requests.Session()
    def login(self):
        nonce = self.s.get(self.base + "/api/login_init", timeout=15).json().get("nonce", "")
        r = self.s.post(self.base + "/api/login", data={
            "user": WEB_USER,
            "pass": sha256_frontend(WEB_PASS),
            "nonce": nonce,
        }, timeout=15)
        return "SIMUTSESS" in self.s.cookies.get_dict() and r.status_code in (200, 302)
    def screenshot(self):
        r = self.s.get(self.base + "/api/screenshot", timeout=30)
        img = Image.open(io.BytesIO(r.content)).convert("RGB")
        return img

def main():
    print("== Validação: painel superior fixado NÃO duplica no alarme ==")
    dev = Dev()
    if not dev.ser:
        print("  [FAIL] sem serial do device")
        return 1

    r = dev.cmd("enable", 1.5)
    r = dev.cmd("show sensors", 2.5)
    print("  --- show sensors ---")
    print("\n".join(l for l in r.splitlines() if l.strip())[:1500])

    # confirma slot 0 ativo (GELADEIRA GPIO0) e guarda os limites atuais
    m = re.search(r"Slot\s+(\d+)\].*?GPIO=(\d+)", r)
    if not m or int(m.group(1)) != 0 or int(m.group(2)) != 0:
        print("  [FAIL] slot 0 não está no GPIO 0 — confira a bancada")
        return 1
    lim = re.search(r"\[T:\s*([-\d.]+)\s*\.\.\s*([-\d.]+)\]", r)
    tmin0 = float(lim.group(1)) if lim else 1.9
    tmax0 = float(lim.group(2)) if lim else 24.0
    print(f"  [OK] slot 0 ativo, limites originais {tmin0}..{tmax0}")

    web = Web()
    if not web.login():
        print("  [FAIL] login web falhou")
        return 1
    print("  [OK] login web")

    # 1) seleciona o slot 0 (S0 no botão inferior)
    dev.cmd("touch sim 34 215", 2.0)
    time.sleep(1.5)
    # 2) fixa o painel superior no slot 0
    dev.cmd("touch sim 160 70", 2.0)
    time.sleep(1.5)

    shot = web.screenshot()
    shot.save("/tmp/step1_pinned.png")
    print(f"  [baseline] top red={red_px(shot, TOP_Y0, TOP_Y1)} bottom red={red_px(shot, BOT_Y0, BOT_Y1)}")

    # 3) dispara alarme de limite no slot 0
    dev.cmd("sensor 0 tmax -100", 1.5)
    print("  [..] aguardando debounce do alarme (16s)")
    time.sleep(16)

    # 4) screenshots em rajada para pegar a fase vermelha do flash
    top_hit = 0
    bot_hit = 0
    for i in range(6):
        img = web.screenshot()
        tr = red_px(img, TOP_Y0, TOP_Y1)
        br = red_px(img, BOT_Y0, BOT_Y1)
        if tr > 2000:
            top_hit += 1
        if br > 2000:
            bot_hit += 1
        print(f"  [shot{i}] top red={tr} bottom red={br}")
        img.save(f"/tmp/step2_alarm_{i}.png")
        time.sleep(0.4)

    print()
    if top_hit == 0:
        print("  [FAIL] TOP nunca piscou vermelho — alarme não disparou?")
    elif bot_hit == 0:
        print("  [PASS] TOP pisca, BOTTOM NÃO pisca — sem duplicação no painel inferior")
    else:
        print("  [FAIL] BOTTOM piscou junto — duplicação do slot superior no inferior")
        return 1

    # 5) restaura limites originais (RAM-only; nada é gravado)
    dev.cmd(f"sensor 0 tmin {tmin0}", 1.5)
    dev.cmd(f"sensor 0 tmax {tmax0}", 1.5)
    dev.cmd("alarm flush", 1.5)
    time.sleep(12)
    img = web.screenshot()
    img.save("/tmp/step3_cleared.png")
    print(f"  [final] top red={red_px(img, TOP_Y0, TOP_Y1)} bottom red={red_px(img, BOT_Y0, BOT_Y1)}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
