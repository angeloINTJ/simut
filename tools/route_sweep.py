#!/usr/bin/env python3
"""route_sweep.py — every read-only GET route once, kept byte for byte, to compare two images.

Usage:
    SIMUT_HOST=192.168.3.24:8080 SIMUT_WEB_USER=... SIMUT_WEB_PASS=... \\
        python3 tools/route_sweep.py <out.json>
    python3 tools/route_sweep.py --compare <a.json> <b.json>

Logs in once and asks for each route in ROUTES on one keep-alive socket, as a
browser does, through chunked_probe.py's raw client: the status, the headers,
the framing (chunked or Content-Length), every chunk's size, the body's length
and sha256, and whether the body parses as JSON. Bodies are never printed:
/api/network carries the SSID.

--compare says, route by route, what differs between two runs. A route whose
content is live (/api/status, /metrics, /api/config...) differs in its body
between any two reads; sweep the same image twice to see which those are. The
rest must match byte for byte. Override 2n (2026-10-14) was proven this way:
the same status, headers and chunk count on all 34 routes, and the same body
on every route that does not change between two reads.

Left out, on purpose: /logout (ends the session), /api/wifi/scan (scans with
the radio), /api/backup (the configuration, secrets included), /api/keypad and
/api/login_init (each starts a PIN or login state), the screenshot routes
(they pause the panel), and the routes that need parameters (/download,
/api/history_multi, /api/export/history.bin).
"""
import hashlib
import json
import os
import sys

ROUTES = [
    "/", "/config", "/telemetry", "/network", "/users", "/files", "/alarms", "/license",
    "/history", "/lang.js", "/style.css", "/favicon.ico", "/apple-touch-icon.png",
    "/login", "/force_chpass",
    "/api/status", "/metrics", "/api/perms", "/api/network", "/api/config", "/api/users",
    "/api/alarms", "/api/lang", "/api/logcodes", "/api/sensors", "/api/calib",
    "/api/history_days", "/api/sec_status", "/api/ls", "/api/logs", "/api/themes",
    "/api/history/open", "/api/export/logs.bin",
    "/no-such-route-sweep",
]


def sweep(out):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from air_test_suite import Web
    from chunked_probe import Conn, read_one, Hole, Cut, Closed
    host_port = os.environ.get("SIMUT_HOST", "192.168.3.24:8080")
    host, _, port = host_port.partition(":")
    w = Web(host_port)
    w.login(os.environ["SIMUT_WEB_USER"], os.environ["SIMUT_WEB_PASS"])
    cookie = "; ".join(f"{k.name}={k.value}" for k in w.s.cookies)
    c = Conn(host, int(port or 80))
    res = {}
    for r in ROUTES:
        req = (f"GET {r} HTTP/1.1\r\nHost: {host_port}\r\nUser-Agent: simut-route-sweep\r\n"
               f"Accept: */*\r\nAccept-Encoding: gzip\r\nCookie: {cookie}\r\n"
               f"Connection: keep-alive\r\n\r\n").encode()
        rec = {}
        for attempt in (0, 1):
            try:
                if c.s is None:
                    c.open()
                code, sizes, body = read_one(c, req)
                head = c.raw.split(b"\r\n\r\n", 1)[0].decode(errors="replace").split("\r\n")
                rec = {"status": code, "headers": head[1:], "chunks": sizes, "len": len(body),
                       "sha256": hashlib.sha256(body).hexdigest()}
                try:
                    json.loads(body)
                    rec["json"] = True
                except ValueError:
                    rec["json"] = False
                break
            except Closed:
                c.close()
                if attempt == 0:
                    continue          # the keep-alive ended between requests: once more
                rec = {"end": "closed"}
            except (Hole, Cut) as e:
                rec = {"end": type(e).__name__.lower(), "why": str(e)[:200]}
                c.close()
            except OSError as e:
                rec = {"end": "error", "why": f"{type(e).__name__}: {e}"}
                c.close()
            break
        res[r] = rec
        print(f"{r:28s} {rec.get('status', rec.get('end'))} len={rec.get('len')} "
              f"chunks={len(rec.get('chunks') or [])} json={rec.get('json')}", flush=True)
    c.close()
    with open(out, "w") as f:
        json.dump(res, f, indent=1)


def compare(a_path, b_path):
    a, b = json.load(open(a_path)), json.load(open(b_path))
    for r in ROUTES:
        x, y = a.get(r, {}), b.get(r, {})
        notes = []
        for k in ("status", "end", "json"):
            if x.get(k) != y.get(k):
                notes.append(f"{k} {x.get(k)}->{y.get(k)}")
        if x.get("headers") != y.get("headers"):
            hx, hy = set(x.get("headers") or []), set(y.get("headers") or [])
            notes.append(f"headers -{sorted(hx - hy)} +{sorted(hy - hx)}")
        if len(x.get("chunks") or []) != len(y.get("chunks") or []):
            notes.append(f"chunk count {len(x.get('chunks') or [])}->{len(y.get('chunks') or [])}")
        same = x.get("sha256") == y.get("sha256")
        if not same:
            notes.append(f"body {x.get('len')}->{y.get('len')} B")
        print(f"{r:28s} {'same body' if same else 'body differs'}; "
              f"{'; '.join(notes) or 'nothing else differs'}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] == "--compare":
        compare(sys.argv[2], sys.argv[3])
    else:
        sweep(sys.argv[1])
