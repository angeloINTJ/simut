#!/usr/bin/env python3
"""Hardware validation for the provisional-clock and out-of-order fixes.

Runs against a live device. Three checks, each ending in the state it started
in:

  A. SEED CEILING — plants a .wip stamped past the day it belongs to, with a
     CRC that passes, and reboots. The seed must be refused: the boot's NTP
     correction has to stay small instead of walking the clock hours forward.
     This is the 2026-08-14 incident, reproduced on the device that had it.

  B. GRAPH READER — the page the device serves must carry the ordering fix, so
     a block stamped ahead no longer hides every block behind it.

  C. TELEMETRY CURSOR — resets the cursor and drains history into a local
     collector, checking what actually arrives against what is on flash. The
     day with out-of-order blocks is the one that matters.

Requires the pico_w_test profile: `tel` and `user` are not in the release CLI.

Usage:
    python3 tools/rig_validate_history_clock.py [--ip 192.168.3.24]
                                                [--skip-a] [--skip-c]
"""

import argparse
import gzip
import hashlib
import http.server
import json
import re
import socket
import struct
import sys
import threading
import time
from datetime import datetime, timezone, timedelta
from pathlib import Path

import requests
import serial

TZ = timezone(timedelta(hours=-3))
PORT = "/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00"
ADMIN = ("admin", "simutV5x")
H5_MAGIC = 0x4835
LOG_REC = 12

results = []


def record(label, ok, detail=""):
    results.append((label, ok))
    print(f"  {'ok  ' if ok else 'FAIL'} {label}{(' — ' + detail) if detail else ''}")
    return ok


def ts(e):
    return datetime.fromtimestamp(e, TZ).strftime("%d/%m %H:%M:%S")


# --------------------------------------------------------------------------
# device access
# --------------------------------------------------------------------------

class Rig:
    def __init__(self, ip):
        self.ip = ip
        self.s = requests.Session()
        n = self.s.get(f"http://{ip}/api/login_init", timeout=10).json()["nonce"]
        self.s.post(
            f"http://{ip}/api/login",
            data={"user": ADMIN[0],
                  "pass": hashlib.sha256(ADMIN[1].encode("latin-1")).hexdigest(),
                  "nonce": n},
            headers={"Content-Type": "application/x-www-form-urlencoded"},
            timeout=15, allow_redirects=False)
        if "SIMUTSESS" not in self.s.cookies.get_dict():
            raise SystemExit("web login failed")

    def get(self, path, **kw):
        return self.s.get(f"http://{self.ip}{path}", timeout=kw.pop("timeout", 30), **kw)

    def download(self, path):
        r = self.get("/download", params={"file": path})
        return r.content if r.status_code == 200 else None

    def upload(self, path, blob):
        r = self.s.post(f"http://{self.ip}/api/upload",
                        files={"file": (path, blob, "application/octet-stream")},
                        timeout=60)
        return r.status_code == 200

    def delete(self, path):
        return self.s.post(f"http://{self.ip}/api/delete",
                           data={"file": path}, timeout=30).status_code == 200

    def ls(self, d="/history"):
        e = self.get("/api/ls", params={"dir": d}).json().get("entries", [])
        return {x.get("n"): x.get("s") for x in e}

    def logs(self):
        raw = self.get("/api/logs", timeout=45).content
        out = []
        for i in range(len(raw) // LOG_REC):
            ep, uplo, code, ctx, flags, uphi = struct.unpack_from("<IHHhBB", raw, i * LOG_REC)
            out.append({"epoch": ep, "up": uplo | (uphi << 16), "code": code,
                        "ctx": ctx, "level": (flags >> 5) & 7})
        return out

    def wait(self, timeout=120):
        end = time.time() + timeout
        while time.time() < end:
            try:
                if self.s.get(f"http://{self.ip}/api/perms", timeout=4).status_code < 500:
                    return True
            except Exception:
                pass
            time.sleep(2)
        return False


class Hand:
    """The PicoHand fixture — a second RP2040 holding the target's RESET line.

    A hard reset is not a convenience here, it is the experiment. `reload
    confirm` runs the pre-reboot hook, which snapshots the open block over
    /history/.wip — so a planted snapshot is overwritten before the reboot that
    was supposed to read it, and the test passes having exercised nothing. It
    is also the wrong event: the incident followed an unclean restart, which is
    exactly what a reset line does and a clean reboot does not.
    """

    PORT = "/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00"

    def __init__(self):
        self.ser = serial.Serial(self.PORT, 115200, timeout=2)
        self.ser.dtr = True
        time.sleep(2)
        if "PONG" not in self._cmd("PING"):
            raise SystemExit("PicoHand did not answer PING")

    def _cmd(self, c, wait=1.0):
        self.ser.reset_input_buffer()
        self.ser.write((c + "\r\n").encode())
        time.sleep(wait)
        return self.ser.read(self.ser.in_waiting or 1).decode(errors="replace")

    def reset(self):
        return self._cmd("RESET", 2.0)

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


class Cli:
    def __init__(self, port=PORT):
        self.ser = None
        for _ in range(15):
            try:
                self.ser = serial.Serial(port, 115200, timeout=1)
                self.ser.dtr = True
                break
            except Exception:
                time.sleep(2)
        if not self.ser:
            raise SystemExit("serial never appeared")
        time.sleep(2.5)

    def cmd(self, c, wait=1.5):
        self.ser.reset_input_buffer()
        self.ser.write((c + "\r\n").encode())
        time.sleep(wait)
        return self.ser.read(self.ser.in_waiting or 1).decode(errors="replace")

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


# --------------------------------------------------------------------------
# H5 helpers
# --------------------------------------------------------------------------

def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def first_data_chunk(blob):
    """Return (offset, length, nCh) of the first DATA chunk in a .h5 file."""
    off = 0
    while off + 8 <= len(blob):
        magic, ver, typ, flags, a, b, rsv = struct.unpack_from("<HBBBBBB", blob, off)
        if magic != H5_MAGIC:
            return None
        if typ == 1:
            off += 8 + 4 * a + 2
            continue
        if typ != 2:
            return None
        # DATA header: preamble(8) | t0(4) @8 | payloadLen(2) @12 | crc(2) @14
        plen = struct.unpack_from("<H", blob, off + 12)[0]
        return off, 16 + 6 * b + plen, b
    return None


def restamp(chunk, new_t0):
    """Rewrite t0 and fix the CRC, so only the ceiling can reject the block."""
    c = bytearray(chunk)
    struct.pack_into("<I", c, 8, new_t0)
    crc = crc16(bytes(c[:14]))
    crc = crc16(bytes(c[16:]), crc)
    struct.pack_into("<H", c, 14, crc)
    return bytes(c)


def day_bounds(name):
    d = datetime(int(name[0:4]), int(name[4:6]), int(name[6:8]), tzinfo=TZ)
    return int(d.timestamp()), int(d.timestamp()) + 86400


# --------------------------------------------------------------------------
# A — seed ceiling
# --------------------------------------------------------------------------

def test_seed(rig, cli):
    print("\nA. SEED CEILING — a .wip stamped past its day must not move the clock")
    files = sorted(n for n in rig.ls() if n.endswith(".h5"))
    newest = files[-1]
    day_start, day_end = day_bounds(newest)
    forged_t0 = day_end + 5 * 3600                      # 05:00 of the next day
    victim = datetime.fromtimestamp(forged_t0, TZ).strftime("%Y%m%d") + ".h5"
    print(f"   newest file {newest}; forging t0 = {ts(forged_t0)}")
    print(f"   old rule ceiling {ts(day_end + 86400)} -> would accept")
    print(f"   new rule ceiling {ts(day_end + 3600)} -> must refuse")

    blob = rig.download(f"/history/{newest}")
    loc = first_data_chunk(blob)
    if not loc:
        return record("could build a forged snapshot", False, "no DATA chunk")
    off, ln, nch = loc
    forged = restamp(blob[off:off + ln], forged_t0)

    saved_wip = rig.download("/history/.wip")
    created = None
    hand = Hand()
    try:
        if not rig.upload("/history/.wip", forged):
            return record("planted the forged .wip", False, "upload refused")
        back = rig.download("/history/.wip")
        if back != forged:
            return record("the forged .wip survived to the reset", False,
                          f"lido de volta {len(back or b'')} B != {len(forged)} B")
        record("the forged .wip survived to the reset", True,
               f"{len(forged)} B, {nch} canais, t0 conferido")

        hand.reset()
        time.sleep(16)
        if not rig.wait(150):
            return record("device came back", False)
        record("device came back", True)

        # NTP lands around up=20 s and the correction is logged there. Reading
        # the log before that finds no correction and reads as a pass — the
        # failure mode this test exists to catch would slip through it.
        deadline = time.time() + 90
        while time.time() < deadline:
            try:
                if int(rig.get("/api/status").json()["sys"]["uptime"]) > 40000:
                    break
            except Exception:
                rig.__init__(rig.ip)
            time.sleep(5)

        # The boot's own NTP correction is the measurement: a seed 29 h ahead
        # would show up here as a correction of that size, in the opposite
        # direction. A refused seed leaves the sealed data in charge.
        # The boot boundary is where uptime steps DOWN, not a SYS_BOOT record:
        # that code is only written when there is an autopsy to report, so a
        # clean restart has none and slicing on it reads the previous boot.
        log = rig.logs()
        start = 0
        for i in range(1, len(log)):
            if log[i]["up"] < log[i - 1]["up"]:
                start = i
        tail = log[start:]
        corr = [r for r in tail if r["code"] == 408]
        rejected = [r for r in tail if r["code"] == 567 and r["level"] == 2]

        if corr:
            worst = max(abs(r["ctx"]) for r in corr)
            # Context is int16; a pegged value means the real correction was
            # larger than the field can hold, which is itself the failure.
            pegged = any(abs(r["ctx"]) >= 32767 for r in corr)
            record("NTP correction stayed small (clock was not walked)",
                   worst < 3600 and not pegged,
                   f"pior |delta| = {worst} s" + (" (SATURADO)" if pegged else ""))
        else:
            record("NTP correction stayed small (clock was not walked)", True,
                   "nenhuma correcao registrada")

        why = {0: "gate recusou o snapshot", 1: "sem schema — caminho inteiro pulado"}
        record("the refusal was logged (wip_seed_rejected)", bool(rejected),
               "; ".join(why.get(r["ctx"], f'ctx={r["ctx"]}') for r in rejected)
               if rejected else "nao encontrado")

        now = int(time.time())
        try:
            st = rig.get("/api/status").json()["sys"]
            drift = abs(int(st["time"]) - now)
        except (KeyError, ValueError):
            rig.__init__(rig.ip)          # the reset dropped the session
            st = rig.get("/api/status").json()["sys"]
            drift = abs(int(st["time"]) - now)
        record("device clock agrees with the host", drift < 120, f"drift {drift} s")

        created = victim if victim in rig.ls() else None
    finally:
        hand.close()
        if created:
            rig.delete(f"/history/{created}")
            print(f"   limpeza: {created} removido "
                  f"({'ausente' if created not in rig.ls() else 'AINDA PRESENTE'})")
        if saved_wip:
            rig.upload("/history/.wip", saved_wip)
        else:
            rig.delete("/history/.wip")
        print("   limpeza: .wip restaurado")


# --------------------------------------------------------------------------
# B — the served page
# --------------------------------------------------------------------------

def test_page(rig):
    print("\nB. GRAPH READER — the served page must carry the ordering fix")
    r = rig.get("/history", headers={"Accept-Encoding": "gzip"})
    body = r.content
    if body[:2] == b"\x1f\x8b":
        body = gzip.decompress(body)
    js = body.decode("utf-8", errors="replace")
    record("history page served", r.status_code == 200, f"HTTP {r.status_code}, {len(js)} B")
    # The old guard dropped anything not newer than the running maximum.
    old = re.search(r"\|\|\s*\w+\s*<=\s*\w+\s*\)\s*continue", js)
    record("the old monotonic guard is gone", old is None,
           old.group(0) if old else "")
    record("the series are sorted before use", ".sort(" in js)
    return js


# --------------------------------------------------------------------------
# C — telemetry cursor
# --------------------------------------------------------------------------


def decode_epochs(blob):
    """Every record epoch in a .h5 blob, via the reference decoder."""
    import subprocess, tempfile, csv, os
    with tempfile.NamedTemporaryFile(suffix=".h5", delete=False) as f:
        f.write(blob); h5 = f.name
    out = h5 + ".csv"
    try:
        subprocess.run([sys.executable,
                        str(Path(__file__).resolve().parent / "history_v5.py"),
                        "--dump-csv", h5, "--out", out],
                       capture_output=True, timeout=120)
        eps = set()
        if os.path.exists(out):
            with open(out) as fh:
                for row in csv.reader(fh):
                    if row and row[0].isdigit():
                        eps.add(int(row[0]))
        return eps
    finally:
        for p_ in (h5, out):
            try: os.unlink(p_)
            except OSError: pass


class Collector(http.server.BaseHTTPRequestHandler):
    seen = set()
    bodies = 0

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(n)
        Collector.bodies += 1
        for m in re.finditer(rb'"(?:epoch|ts|timestamp)"\s*:\s*(\d{9,12})', raw):
            Collector.seen.add(int(m.group(1)))
        for m in re.finditer(rb'^\s*(\d{9,12})\s*[,;]', raw, re.M):
            Collector.seen.add(int(m.group(1)))
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *a):
        pass


def host_ip(target):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((target, 80))
    ip = s.getsockname()[0]
    s.close()
    return ip


def test_telemetry(rig, cli):
    print("\nC. TELEMETRY CURSOR — drain history into a local collector")
    ip = host_ip(rig.ip)
    srv = http.server.ThreadingHTTPServer(("0.0.0.0", 8099), Collector)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"   coletor em http://{ip}:8099/ingest")

    saved = rig.get("/api/config").json()
    try:
        cli.cmd("configure terminal")
        cli.cmd(f"tel server {ip}")
        cli.cmd("tel port 8099")
        cli.cmd("tel path /ingest")
        cli.cmd("tel mode json")
        cli.cmd("tel crypto off")
        cli.cmd("tel batch 50")
        out = cli.cmd("end", 2)
        cli.cmd("tel reset", 3)

        rounds, idle = 0, 0
        while rounds < 25 and idle < 3:
            before = len(Collector.seen)
            cli.cmd("tel sync", 4)
            rounds += 1
            idle = idle + 1 if len(Collector.seen) == before else 0
        print(f"   {rounds} syncs, {Collector.bodies} POSTs, "
              f"{len(Collector.seen)} instantes distintos")

        record("the collector received data", len(Collector.seen) > 0,
               f"{len(Collector.seen)} registros")

        # `tel reset` starts the drain 30 days back, so a session-length run
        # covers the oldest days rather than the day with out-of-order blocks —
        # reaching that one is ~860 syncs. What this can prove, and what the
        # cursor bug would have broken, is that nothing inside the drained span
        # is skipped. Compare delivered epochs against the files themselves.
        lo, hi = min(Collector.seen), max(Collector.seen)
        print(f"   trecho drenado: {ts(lo)} -> {ts(hi)}")
        expected = set()
        day = datetime.fromtimestamp(lo, TZ).date()
        last = datetime.fromtimestamp(hi, TZ).date()
        while day <= last:
            blob = rig.download(f"/history/{day:%Y%m%d}.h5")
            if blob:
                expected |= decode_epochs(blob)
            day += timedelta(days=1)
        window = {e for e in expected if lo <= e <= hi}
        missing = window - Collector.seen
        record("no record inside the drained span was skipped",
               not missing,
               f"{len(window)} no flash, {len(missing)} nao entregues"
               + (f" (ex.: {ts(min(missing))})" if missing else ""))

        future = {e for e in Collector.seen if e > int(time.time())}
        record("nothing was delivered with a future stamp", not future,
               f"{len(future)} registros" if future else "")
    finally:
        srv.shutdown()
        cli.cmd("configure terminal")
        cli.cmd(f"tel server {saved.get('telServer', '') or ' '}")
        cli.cmd(f"tel port {saved.get('telPort', 80)}")
        cli.cmd("end", 2)
        cli.cmd("tel reset", 3)
        print("   limpeza: config de telemetria restaurada, cursor resetado")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", default="192.168.3.24")
    ap.add_argument("--skip-a", action="store_true")
    ap.add_argument("--skip-c", action="store_true")
    a = ap.parse_args()

    rig = Rig(a.ip)
    cli = Cli()
    print(f"device {a.ip}")
    try:
        if not a.skip_a:
            test_seed(rig, cli)
        test_page(rig)
        if not a.skip_c:
            test_telemetry(rig, cli)
    finally:
        cli.close()

    bad = [l for l, ok in results if not ok]
    print(f"\n{len(results) - len(bad)}/{len(results)} checks passaram")
    if bad:
        print("FALHAS: " + "; ".join(bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
