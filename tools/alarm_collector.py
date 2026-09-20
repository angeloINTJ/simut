#!/usr/bin/env python3
"""A collector for the alarm line, as a process of its own: every POST body is
appended to --log as one JSON line {path, ts, body}, and answered 200 so the
device acks the batch. Kept out of the test script so that it is up while the
device boots and between runs — a record posted while nobody listens is a
retry the device pays for and the bench never sees."""
import argparse, http.server, json, time
ap = argparse.ArgumentParser(); ap.add_argument('--port', type=int, default=18081); ap.add_argument('--log', required=True)
a = ap.parse_args()
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *x): pass
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(n).decode('utf-8', 'replace')
        with open(a.log, 'a') as f:
            f.write(json.dumps({'path': self.path, 'ts': time.time(), 'body': body}) + '\n')
        self.send_response(200); self.send_header('Content-Type', 'application/json'); self.end_headers()
        self.wfile.write(b'{"ok":true}')
    def do_GET(self):
        self.send_response(200); self.end_headers(); self.wfile.write(b'ok')
http.server.ThreadingHTTPServer(('0.0.0.0', a.port), H).serve_forever()
