#!/usr/bin/env python3
"""Instrumented HTTP/HTTPS telemetry sink with fault injection.

One process, one port, one mode. The device points its telemetry at it and the
server records every byte, the wall-clock of every request, and every record it
could parse out of the payload. Modes other than `ok` are deliberate failures —
each one models something a real server does when it goes wrong, so the device's
survival can be measured against a named fault instead of "the network was bad".

Metrics land in a JSON file (--stats) that the orchestrator reads; the raw
records land in an NDJSON file (--records) so the drained history can be
compared against what is actually on the device.

Modes
-----
ok              200 with the same body shape as the user's real server.
error500        valid HTTP 500 — device must NOT advance its cursor.
error401        valid HTTP 401.
blackhole       accept the socket, read the request, answer nothing, hold open.
slow N          accept, then answer only after --delay seconds.
half            send a partial status line and close.
rst             accept then RST (SO_LINGER 0) before reading.
rst_mid         read the request, send half the headers, then RST.
garbage         answer with non-HTTP bytes.
huge            answer 200 with a --huge-bytes body.
drip            answer one byte every --drip-ms ms (slowloris, server side).
close_early     read only the headers, close before the body arrives.
never_read      accept, finish the TLS handshake, then never read the request.
tls_bigrecord   answer in one oversized TLS record (--tls only).

never_read is the seam the framework patches did not close. The other faults
break the *response*, which the device reads under a deadline; this one breaks
the *request*, which the device writes. HTTPClient's sendAll loops up to 5 s
with no watchdog feed, and DNS (4 s) plus connect (4 s) sit in front of it with
no feed either, so the three chained cross the 8388 ms hardware watchdog.

Refusing to call recv( ) is not enough on its own: Linux sizes the receive
buffer in the hundreds of KB, so the whole request lands in the kernel and the
device's write returns immediately having blocked on nothing. The window has to
be small enough to actually close, hence --rcvbuf on the listening socket
(accepted sockets inherit it). Set it once there rather than on each accepted
socket: after the handshake bytes are already in flight, shrinking is too late.
"""
import argparse
import json
import os
import re
import socket
import ssl
import struct
import sys
import threading
import time

STATS_LOCK = threading.Lock()


class Stats:
    def __init__(self, path, records_path):
        self.path = path
        self.records_path = records_path
        self.started = time.time()
        self.conns = 0
        self.requests = 0
        self.bytes_in = 0
        self.records = 0
        self.batches = []          # per-request: {t, bytes, n, ms, first_epoch, last_epoch}
        self.tls_failures = 0
        self.tls_ok = 0
        self.errors = []
        self.raw_bodies = []
        self.epochs_seen = set()
        self._rf = open(records_path, 'a') if records_path else None

    def add_batch(self, entry, recs):
        with STATS_LOCK:
            self.requests += 1
            self.batches.append(entry)
            self.records += len(recs)
            if self._rf:
                for r in recs:
                    self._rf.write(json.dumps(r, separators=(',', ':')) + '\n')
                self._rf.flush()
            for r in recs:
                ts = r.get('ts')
                if ts is not None:
                    self.epochs_seen.add(ts)

    def dump(self):
        with STATS_LOCK:
            lat = [b['ms'] for b in self.batches if b.get('ms') is not None]
            lat_sorted = sorted(lat)

            def pct(p):
                if not lat_sorted:
                    return None
                k = min(len(lat_sorted) - 1, int(round((p / 100.0) * (len(lat_sorted) - 1))))
                return lat_sorted[k]

            d = {
                'started': self.started,
                'now': time.time(),
                'elapsed_s': round(time.time() - self.started, 1),
                'conns': self.conns,
                'requests': self.requests,
                'bytes_in': self.bytes_in,
                'records': self.records,
                'unique_epochs': len(self.epochs_seen),
                'epoch_min': min(self.epochs_seen) if self.epochs_seen else None,
                'epoch_max': max(self.epochs_seen) if self.epochs_seen else None,
                'tls_ok': self.tls_ok,
                'tls_failures': self.tls_failures,
                'errors': self.errors[-40:],
                'raw_bodies': self.raw_bodies,
                'server_ms_min': min(lat) if lat else None,
                'server_ms_p50': pct(50),
                'server_ms_p90': pct(90),
                'server_ms_max': max(lat) if lat else None,
                'batches': self.batches[-400:],
            }
            tmp = self.path + '.tmp'
            with open(tmp, 'w') as fh:
                json.dump(d, fh, indent=1)
            os.replace(tmp, self.path)
            return d


def parse_records(body):
    """Pull {"ts":...} objects out of the JSON payload.

    Tolerant on purpose: the point is to count what arrived even when the
    device truncates, so a strict json.loads that throws would hide the very
    failure being measured.
    """
    recs = []
    try:
        obj = json.loads(body)
        if isinstance(obj, list):
            return [r for r in obj if isinstance(r, dict)]
        if isinstance(obj, dict):
            for k in ('data', 'records', 'points'):
                if isinstance(obj.get(k), list):
                    return [r for r in obj[k] if isinstance(r, dict)]
            return [obj]
    except Exception:
        pass
    # Fallback: brace-matched scan.
    depth = 0
    start = None
    for i, c in enumerate(body):
        if c == '{':
            if depth == 0:
                start = i
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0 and start is not None:
                try:
                    recs.append(json.loads(body[start:i + 1]))
                except Exception:
                    pass
                start = None
    return recs


def read_request(conn, stats, timeout):
    conn.settimeout(timeout)
    buf = b''
    # headers
    while b'\r\n\r\n' not in buf:
        chunk = conn.recv(4096)
        if not chunk:
            return None, buf
        buf += chunk
        if len(buf) > 1 << 20:
            break
    head, _, rest = buf.partition(b'\r\n\r\n')
    m = re.search(rb'Content-Length:\s*(\d+)', head, re.I)
    want = int(m.group(1)) if m else 0
    body = rest
    while len(body) < want:
        chunk = conn.recv(min(65536, want - len(body)))
        if not chunk:
            break
        body += chunk
    return head.decode('latin-1', 'replace'), body


def handle(conn, addr, args, stats):
    t0 = time.time()
    with STATS_LOCK:
        stats.conns += 1
    mode = args.mode
    try:
        if mode == 'rst':
            conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                            struct.pack('ii', 1, 0))
            conn.close()
            return

        if mode == 'never_read':
            # The handshake is finished (wrap_socket did read that much); the
            # request is not, and never will be. Hold the socket open without
            # draining it so the window closes under the device mid-write.
            # Nothing to parse, so there is no batch to record — the evidence
            # for this fault is on the device side (uptime, watchdog), not here.
            time.sleep(args.delay)
            return

        head, body = read_request(conn, stats, args.read_timeout)
        if head is None:
            return
        with STATS_LOCK:
            stats.bytes_in += len(body)

        text = body.decode('utf-8', 'replace') if body else ''
        if args.raw_dump:
            with STATS_LOCK:
                if len(stats.raw_bodies) < args.raw_dump:
                    stats.raw_bodies.append(text[:4096])
        recs = parse_records(text) if body else []
        epochs = [r.get('ts') for r in recs if isinstance(r.get('ts'), int)]
        entry = {
            't': round(t0 - stats.started, 3),
            'bytes': len(body),
            'n': len(recs),
            'first_epoch': min(epochs) if epochs else None,
            'last_epoch': max(epochs) if epochs else None,
            'ms': None,
        }

        if mode == 'ok':
            resp = json.dumps({'status': 'ok', 'msg': 'Salvo', 'pts': len(recs)}).encode()
            conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n'
                         b'Content-Length: ' + str(len(resp)).encode() +
                         b'\r\nConnection: close\r\n\r\n' + resp)
        elif mode == 'slow':
            time.sleep(args.delay)
            resp = json.dumps({'status': 'ok', 'pts': len(recs)}).encode()
            conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n'
                         b'Content-Length: ' + str(len(resp)).encode() +
                         b'\r\nConnection: close\r\n\r\n' + resp)
        elif mode == 'error500':
            resp = b'{"status":"error","msg":"internal"}'
            conn.sendall(b'HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n'
                         b'Content-Length: ' + str(len(resp)).encode() +
                         b'\r\nConnection: close\r\n\r\n' + resp)
        elif mode == 'error401':
            resp = b'{"error":"unauthorized"}'
            conn.sendall(b'HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n'
                         b'Content-Length: ' + str(len(resp)).encode() +
                         b'\r\nConnection: close\r\n\r\n' + resp)
        elif mode == 'blackhole':
            # Answer nothing. Hold the socket open past anything the device
            # could reasonably wait for, then drop it.
            time.sleep(args.delay)
        elif mode == 'half':
            conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Len')
            conn.close()
            return
        elif mode == 'rst_mid':
            conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n')
            time.sleep(0.05)
            conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                            struct.pack('ii', 1, 0))
            conn.close()
            return
        elif mode == 'garbage':
            conn.sendall(os.urandom(512))
            conn.close()
            return
        elif mode == 'huge':
            n = args.huge_bytes
            conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n'
                         b'Content-Length: ' + str(n).encode() + b'\r\n\r\n')
            blob = b'A' * 4096
            sent = 0
            while sent < n:
                k = min(len(blob), n - sent)
                conn.sendall(blob[:k])
                sent += k
        elif mode == 'drip':
            resp = (b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n'
                    b'Content-Length: 2\r\n\r\n{}')
            for b in resp:
                conn.sendall(bytes([b]))
                time.sleep(args.drip_ms / 1000.0)
        elif mode == 'close_early':
            conn.close()
            return
        elif mode == 'tls_bigrecord':
            # One sendall = one TLS record. The device asks BearSSL for a 4096 B
            # input buffer (setBufferSizes(4096,512)) but never negotiates
            # max_fragment_length (RFC 6066), so a server is within its rights
            # to send 16 KB records and the device has to cope. Failing to
            # decode is acceptable; hanging or rebooting is not.
            n = max(args.big_record_bytes, 512)
            body = b'B' * (n - 128)
            conn.sendall(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n'
                         b'Content-Length: ' + str(len(body)).encode() +
                         b'\r\nConnection: close\r\n\r\n' + body)
        else:
            raise SystemExit(f'unknown mode {mode}')

        entry['ms'] = int((time.time() - t0) * 1000)
        stats.add_batch(entry, recs)
    except Exception as e:
        with STATS_LOCK:
            stats.errors.append(f'{type(e).__name__}: {e}')
    finally:
        try:
            conn.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, required=True)
    ap.add_argument('--bind', default='0.0.0.0')
    ap.add_argument('--tls', action='store_true')
    ap.add_argument('--cert', default='certs/cert.pem')
    ap.add_argument('--key', default='certs/key.pem')
    ap.add_argument('--mode', default='ok')
    ap.add_argument('--delay', type=float, default=30.0)
    ap.add_argument('--drip-ms', type=float, default=500.0)
    ap.add_argument('--huge-bytes', type=int, default=1 << 20)
    ap.add_argument('--read-timeout', type=float, default=30.0)
    ap.add_argument('--big-record-bytes', type=int, default=16384,
                    help='tls_bigrecord: bytes written in a single sendall, '
                         'which OpenSSL emits as one TLS record')
    ap.add_argument('--rcvbuf', type=int, default=0,
                    help='SO_RCVBUF on the listening socket, inherited by every '
                         'accepted socket. Required by never_read: the kernel '
                         'default is large enough to swallow the whole request, '
                         'which makes a server that never reads look identical '
                         'to one that does.')
    ap.add_argument('--stats', default='stats.json')
    ap.add_argument('--records', default='')
    ap.add_argument('--raw-dump', type=int, default=0,
                    help='keep the first N request bodies verbatim, for '
                         'checking the payload builders (json/csv/custom)')
    # TLS fault modes act before any HTTP is spoken.
    ap.add_argument('--tls-fault', default='',
                    choices=['', 'blackhole', 'garbage', 'rst', 'slow'],
                    help='blackhole: accept and never handshake; garbage: junk '
                         'bytes instead of ServerHello; rst: RST at accept; '
                         'slow: sleep --delay then handshake')
    args = ap.parse_args()

    stats = Stats(args.stats, args.records)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if args.rcvbuf:
        # Before bind/listen, so accepted sockets inherit it. The kernel doubles
        # the value and enforces a floor (~2 KB), so the effective window is
        # larger than asked for — report both rather than assume.
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, args.rcvbuf)
        eff = srv.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
        print(f'[rcvbuf] asked {args.rcvbuf} B, kernel gave {eff} B', flush=True)
    srv.bind((args.bind, args.port))
    srv.listen(16)
    ctx = None
    if args.tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key)
        # BearSSL on the RP2040 speaks TLS 1.2 with ECDHE-RSA-AES-GCM. Pinning
        # to 1.2 keeps the negotiation identical to the user's real server.
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.maximum_version = ssl.TLSVersion.TLSv1_2

    def dumper():
        while True:
            time.sleep(2)
            try:
                stats.dump()
            except Exception:
                pass

    threading.Thread(target=dumper, daemon=True).start()

    label = ('https' if args.tls else 'http')
    print(f'[{label}] listening on {args.bind}:{args.port} mode={args.mode} '
          f'tls_fault={args.tls_fault or "-"}', flush=True)

    while True:
        try:
            conn, addr = srv.accept()
        except OSError:
            break
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def wrapped(conn=conn, addr=addr):
            if args.tls:
                f = args.tls_fault
                if f == 'rst':
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                    struct.pack('ii', 1, 0))
                    with STATS_LOCK:
                        stats.conns += 1
                    conn.close()
                    return
                if f == 'blackhole':
                    with STATS_LOCK:
                        stats.conns += 1
                    time.sleep(args.delay)
                    conn.close()
                    return
                if f == 'garbage':
                    with STATS_LOCK:
                        stats.conns += 1
                    try:
                        conn.recv(4096)
                        conn.sendall(os.urandom(2048))
                    except Exception:
                        pass
                    conn.close()
                    return
                if f == 'slow':
                    time.sleep(args.delay)
                try:
                    conn.settimeout(args.read_timeout)
                    tconn = ctx.wrap_socket(conn, server_side=True)
                    with STATS_LOCK:
                        stats.tls_ok += 1
                except Exception as e:
                    with STATS_LOCK:
                        stats.tls_failures += 1
                        stats.errors.append(f'TLS: {type(e).__name__}: {e}')
                    try:
                        conn.close()
                    except Exception:
                        pass
                    return
                handle(tconn, addr, args, stats)
            else:
                handle(conn, addr, args, stats)

        threading.Thread(target=wrapped, daemon=True).start()


if __name__ == '__main__':
    main()
