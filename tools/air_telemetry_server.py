#!/usr/bin/env python3
"""
SIMUT Air telemetry test server.

Listens on a TCP port and logs every HTTP request (method, path, headers, body)
to stdout and to a JSONL log file. Responds 200 so the device considers each
telemetry send successful.

Usage:
    python3 tools/air_telemetry_server.py [--port 8080] [--log /tmp/telemetry.jsonl]
"""

import argparse
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def _log(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length) if length else b""
        entry = {
            "ts": time.time(),
            "iso": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "method": self.command,
            "path": self.path,
            "headers": dict(self.headers),
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
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--log", default="/tmp/simut_telemetry.jsonl")
    args = ap.parse_args()

    logfile = open(args.log, "a") if args.log else None
    srv = HTTPServer(("0.0.0.0", args.port), Handler)
    srv.logfile = logfile
    print(f"[telemetry-server] listening on 0.0.0.0:{args.port} -> {args.log}", flush=True)
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
