#!/usr/bin/env python3
"""
SIMUT Air telemetry test server.

Listens on a TCP port and logs every HTTP request (method, path, headers, body)
to stdout and to a JSONL log file. Responds 200 so the device considers each
telemetry send successful.

Binds to loopback by default (finding O-3, 2026-09-07): it answers 200 to
anything and writes every request to a file, so it is not something to leave
listening on every interface by accident. The device is on the LAN, so a real
bench run passes --bind 0.0.0.0 deliberately.

Authorization and X-Api-Key headers are redacted in the log. The point of the
log is the payload, and the credential in the header is the one thing in a
request that must not end up in a file that gets pasted into an issue.

Usage:
    python3 tools/air_telemetry_server.py [--bind 0.0.0.0] [--port 8080] \
        [--log /tmp/telemetry.jsonl]
"""

import argparse
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def _log(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length) if length else b""
        headers = dict(self.headers)
        for h in list(headers):
            if h.lower() in ("authorization", "x-api-key"):
                headers[h] = "<redacted>"
        entry = {
            "ts": time.time(),
            "iso": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "method": self.command,
            "path": self.path,
            "headers": headers,
            "body": body.decode("utf-8", "replace"),
        }
        line = json.dumps(entry)
        print(line, flush=True)
        if self.server.logfile:
            self.server.logfile.write(line + "\n")
            self.server.logfile.flush()

    def _respond(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"{}")

    def do_POST(self):
        self._log()
        self._respond()

    def do_GET(self):
        self._log()
        self._respond()

    def log_message(self, fmt, *args):  # silence default stderr logging
        pass


def main():
    ap = argparse.ArgumentParser(description="SIMUT Air telemetry test server")
    ap.add_argument("--bind", default="127.0.0.1",
                    help="interface to listen on (default loopback; a bench that "
                         "the device must reach passes 0.0.0.0)")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--log", default="/tmp/simut_telemetry.jsonl")
    args = ap.parse_args()

    logfile = None
    if args.log:
        # 0600 before anything is written: the file holds whole telemetry
        # payloads, and on a shared machine /tmp is world-readable.
        fd = os.open(args.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        os.fchmod(fd, 0o600)
        logfile = os.fdopen(fd, "a")
    srv = HTTPServer((args.bind, args.port), Handler)
    srv.logfile = logfile
    print(f"[telemetry-server] listening on {args.bind}:{args.port} -> {args.log}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
        if logfile:
            logfile.close()


if __name__ == "__main__":
    main()
