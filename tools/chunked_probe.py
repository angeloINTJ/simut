#!/usr/bin/env python3
"""chunked_probe.py — read /api/status in a loop and say how each reply ended (#189).

Usage:
    SIMUT_HOST=192.168.3.24:8080 SIMUT_WEB_USER=... SIMUT_WEB_PASS=... \\
        python3 tools/chunked_probe.py <out dir> [reads=2000] [gap s=0.2]

A raw HTTP/1.1 client on one keep-alive socket, as a browser or requests'
Session uses it, with its own chunked parser, so a broken reply is kept BYTE
FOR BYTE. Read-only: logs in once and sends GET /api/status only.

Each reply ends one of these ways:
  ok      the terminating chunk arrived and the JSON parses.
  hole    the framing broke mid-stream: bytes are missing from the middle of
          the reply (a size line, a chunk's data or its CRLF), and the bytes
          after the hole arrive as if nothing were missing. This is #189:
          release 2.11.1, 24 of 2,000 on 2026-10-14.
  cut     the device closed the connection mid-reply, every chunk read so far
          well formed: a reply ended short and honestly. Override 2m turns a
          hole into this.
  closed  the device closed the connection before sending a byte of the
          reply (the keep-alive ended between requests); retried once on a
          fresh connection, and counted.
  json    the framing is whole and the JSON does not parse.
  timeout no byte for 10 s.
  error   the socket failed otherwise: a connection refused or no route to
          the host, as while the device reboots.

The device's own counters ride along on every good reply (metr.cgd, cgg, cgx
and, from override 2m on, cgw): the summary gives how far each moved, so a
cut can be matched to the short write the device counted for it. The raw
bytes of every reply that is not ok go to <out>/fail_<n>.bin, with a line in
fail_<n>.txt; the first three good ones to good_<n>.bin. The summary, and a
row per read, go to <out>/summary.json.
"""
import json
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from air_test_suite import Web  # noqa: E402


class Hole(Exception):
    """The framing broke: bytes missing from the middle of the reply."""


class Cut(Exception):
    """The connection ended mid-reply, the framing whole up to that point."""


class Closed(Exception):
    """The connection ended before the reply's first byte."""


class Conn:
    def __init__(self, host, port):
        self.host, self.port = host, port
        self.s = None
        self.buf = b""
        self.raw = b""
        self.opens = 0

    def open(self):
        self.close()
        self.s = socket.create_connection((self.host, self.port), timeout=10)
        self.opens += 1
        self.buf = b""

    def close(self):
        if self.s:
            try:
                self.s.close()
            except OSError:
                pass
        self.s = None

    def fill(self, started):
        try:
            d = self.s.recv(4096)
        except ConnectionResetError:
            d = b""           # a reset ends the stream as a FIN does
        if not d:
            raise (Cut if started else Closed)("the device closed the connection")
        self.buf += d
        self.raw += d

    def line(self, started=True):
        while b"\r\n" not in self.buf:
            self.fill(started or bool(self.raw))
        ln, self.buf = self.buf.split(b"\r\n", 1)
        return ln

    def take(self, n):
        while len(self.buf) < n:
            self.fill(True)
        d, self.buf = self.buf[:n], self.buf[n:]
        return d


def read_one(c, req):
    """One GET on the open socket: (status, chunk sizes, body)."""
    c.raw = b""
    try:
        c.s.sendall(req)
    except (BrokenPipeError, ConnectionResetError) as e:
        raise Closed(f"the request could not be sent: {e}")
    status = c.line(started=False)
    hdr = {}
    while True:
        ln = c.line()
        if not ln:
            break
        k, _, v = ln.decode(errors="replace").partition(":")
        hdr[k.strip().lower()] = v.strip()
    code = int(status.split()[1]) if len(status.split()) > 1 else 0
    sizes, body = [], b""
    if hdr.get("transfer-encoding", "").lower() == "chunked":
        while True:
            ln = c.line()
            try:
                n = int(ln.split(b";")[0].strip(), 16)
            except ValueError:
                raise Hole(f"a chunk-size line was expected, got {ln[:80]!r} "
                           f"after {len(body)} body bytes, chunks {sizes}")
            sizes.append(n)
            if n == 0:
                while c.line():
                    pass
                break
            body += c.take(n)
            crlf = c.take(2)
            if crlf != b"\r\n":
                raise Hole(f"CRLF expected after a {n}-byte chunk, got {crlf!r}; chunks {sizes}")
    elif "content-length" in hdr:
        body = c.take(int(hdr["content-length"]))
    return code, sizes, body


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    reads = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    gap = float(sys.argv[3]) if len(sys.argv) > 3 else 0.2
    host_port = os.environ.get("SIMUT_HOST", "192.168.3.24:8080")
    host, _, port = host_port.partition(":")
    port = int(port or 80)

    w = Web(host_port)
    w.login(os.environ["SIMUT_WEB_USER"], os.environ["SIMUT_WEB_PASS"])
    cookie = "; ".join(f"{k.name}={k.value}" for k in w.s.cookies)
    req = (f"GET /api/status HTTP/1.1\r\nHost: {host_port}\r\nUser-Agent: simut-chunked-probe\r\n"
           f"Accept: */*\r\nCookie: {cookie}\r\nConnection: keep-alive\r\n\r\n").encode()

    c = Conn(host, port)
    c.open()
    rows, kinds, fails, goods = [], {}, 0, 0
    first_metr, last_metr, last_up, reboots = None, None, None, 0
    t0 = time.time()

    def keep(kind, i, why):
        nonlocal fails
        kinds[kind] = kinds.get(kind, 0) + 1
        with open(os.path.join(out, f"fail_{fails}.bin"), "wb") as f:
            f.write(c.raw)
        with open(os.path.join(out, f"fail_{fails}.txt"), "w") as f:
            f.write(f"read {i}: {kind}: {why}\n")
        fails += 1

    for i in range(reads):
        t = time.time()
        rec = {"i": i, "t": round(t - t0, 3)}
        for attempt in (0, 1):
            try:
                if c.s is None:
                    c.open()
                code, sizes, body = read_one(c, req)
                rec.update(code=code, chunks=len(sizes), bytes=len(body))
                try:
                    st = json.loads(body)
                except ValueError as e:
                    rec["end"] = "json"
                    keep("json", i, e)
                    break
                rec["end"] = "ok"
                metr = st.get("metr", {})
                last_metr = {k: metr.get(k) for k in ("cgd", "cgg", "cgx", "cgw")}
                if first_metr is None:
                    first_metr = dict(last_metr)
                up = st.get("sys", {}).get("uptime")
                if last_up is not None and up is not None and up < last_up:
                    reboots += 1
                    rec["reboot"] = True
                last_up = up
                if goods < 3:
                    with open(os.path.join(out, f"good_{goods}.bin"), "wb") as f:
                        f.write(c.raw)
                    goods += 1
                break
            except Closed as e:
                c.close()
                if attempt == 0:
                    kinds["closed"] = kinds.get("closed", 0) + 1
                    continue          # the keep-alive ended between requests: once more
                rec["end"] = "closed"
                keep("closed-twice", i, e)
            except (Hole, Cut) as e:
                kind = "hole" if isinstance(e, Hole) else "cut"
                try:                   # what came right after, for the picture
                    c.s.settimeout(0.5)
                    while True:
                        d = c.s.recv(4096)
                        if not d:
                            break
                        c.raw += d
                except OSError:
                    pass
                rec["end"] = kind
                keep(kind, i, e)
                c.close()
            except socket.timeout as e:
                rec["end"] = "timeout"
                keep("timeout", i, f"{type(e).__name__}: {e}")
                c.close()
            except OSError as e:
                rec["end"] = "error"
                keep("error", i, f"{type(e).__name__}: {e}")
                c.close()
            break
        rec["s"] = round(time.time() - t, 3)
        rows.append(rec)
        if rec.get("end") != "ok":
            print(f"{time.strftime('%H:%M:%S')} read {i}: {rec.get('end')}", flush=True)
        time.sleep(gap)
    c.close()

    secs = sorted(r["s"] for r in rows if r.get("end") == "ok")
    # A counter this image does not have (cgw before override 2m) is reported
    # as absent, not as a 0 that would read as "nothing happened".
    moved = ({k: (None if last_metr.get(k) is None else last_metr[k] - (first_metr.get(k) or 0))
              for k in last_metr} if first_metr and last_metr else {})
    summary = {
        "reads": reads, "ends": kinds, "reboots": reboots, "connections": c.opens,
        "not_ok_pct": round(100.0 * sum(v for k, v in kinds.items() if k != "closed") / reads, 3),
        "seconds": round(time.time() - t0, 1),
        "ok_median_s": secs[len(secs) // 2] if secs else None,
        "ok_slowest_s": secs[-5:] if secs else None,
        "device_counters_moved": moved,
    }
    with open(os.path.join(out, "summary.json"), "w") as f:
        json.dump({"summary": summary, "rows": rows}, f, indent=0)
    print("SUMMARY", json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
