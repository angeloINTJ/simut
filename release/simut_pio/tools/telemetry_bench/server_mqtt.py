#!/usr/bin/env python3
"""Instrumented MQTT 3.1.1 broker with fault injection (plain and TLS).

Written from the wire format rather than wrapping mosquitto, because the point
is to misbehave on purpose: refuse the CONNECT, answer half a CONNACK, drop the
socket exactly when the first PUBLISH lands. A real broker has no switch for
any of that.

Speaks enough of the protocol for the device's PubSubClient: CONNECT/CONNACK,
PUBLISH (QoS 0 and 1), PUBACK, SUBSCRIBE/SUBACK, PINGREQ/PINGRESP, DISCONNECT.
Every published message is timestamped and written to --records as NDJSON.

Modes
-----
ok                  full broker.
rst                 RST at accept, before CONNECT.
no_connack          read CONNECT, answer nothing, hold the socket.
slow_connack        answer CONNACK after --delay seconds.
half_connack        send 2 of the 4 CONNACK bytes and stall.
connack_refuse      CONNACK rc=5 (not authorized).
connack_badproto    CONNACK rc=1 (unacceptable protocol version).
connack_badid       CONNACK rc=2 (identifier rejected).
connack_unavail     CONNACK rc=3 (server unavailable).
drop_after_connack  CONNACK ok, then close immediately.
drop_on_publish     CONNACK ok, close when the first PUBLISH arrives.
rst_on_publish      CONNACK ok, RST when the first PUBLISH arrives.
garbage             send junk instead of CONNACK.
no_pingresp         full broker except PINGREQ is ignored (keepalive death).
"""
import argparse
import json
import os
import socket
import ssl
import struct
import threading
import time

LOCK = threading.Lock()

CONNECT, CONNACK, PUBLISH, PUBACK = 1, 2, 3, 4
SUBSCRIBE, SUBACK, PINGREQ, PINGRESP, DISCONNECT = 8, 9, 12, 13, 14


class Stats:
    def __init__(self, path, records_path):
        self.path = path
        self.started = time.time()
        self.conns = 0
        self.connects = 0
        self.connacks = 0
        self.publishes = 0
        self.bytes_in = 0
        self.records = 0
        self.pings = 0
        self.tls_ok = 0
        self.tls_failures = 0
        self.errors = []
        self.msgs = []           # per-publish: {t, topic, bytes, n, first_epoch, last_epoch}
        self.epochs_seen = set()
        self.client_ids = []
        self.qos_seen = {}
        self.retain_seen = {}
        self.will_topics = []
        self.status_msgs = []
        self.connect_frames = []   # full CONNECT fields, for config-fidelity checks
        self.connect_intervals = []
        self._last_connect = None
        self._rf = open(records_path, 'a') if records_path else None

    def note_connect(self, cid):
        with LOCK:
            self.connects += 1
            if cid not in self.client_ids:
                self.client_ids.append(cid)
            now = time.time()
            if self._last_connect is not None:
                self.connect_intervals.append(round(now - self._last_connect, 2))
            self._last_connect = now

    def note_publish(self, topic, payload, qos=0, retain=False, dup=False):
        recs = []
        txt = payload.decode('utf-8', 'replace')
        try:
            obj = json.loads(txt)
            recs = obj if isinstance(obj, list) else [obj]
        except Exception:
            depth, start = 0, None
            for i, c in enumerate(txt):
                if c == '{':
                    if depth == 0:
                        start = i
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0 and start is not None:
                        try:
                            recs.append(json.loads(txt[start:i + 1]))
                        except Exception:
                            pass
                        start = None
        recs = [r for r in recs if isinstance(r, dict)]
        data = [r for r in recs if 'ts' in r]
        epochs = [r['ts'] for r in data if isinstance(r.get('ts'), int)]
        with LOCK:
            self.publishes += 1
            self.bytes_in += len(payload)
            self.records += len(data)
            self.msgs.append({
                't': round(time.time() - self.started, 3),
                'topic': topic,
                'bytes': len(payload),
                'n': len(data),
                'qos': qos, 'retain': retain, 'dup': dup,
                'first_epoch': min(epochs) if epochs else None,
                'last_epoch': max(epochs) if epochs else None,
            })
            self.qos_seen[qos] = self.qos_seen.get(qos, 0) + 1
            self.retain_seen[bool(retain)] = self.retain_seen.get(bool(retain), 0) + 1
            for e in epochs:
                self.epochs_seen.add(e)
            if self._rf:
                for r in data:
                    self._rf.write(json.dumps(r, separators=(',', ':')) + '\n')
                self._rf.flush()

    def dump(self):
        with LOCK:
            d = {
                'started': self.started,
                'elapsed_s': round(time.time() - self.started, 1),
                'conns': self.conns,
                'connects': self.connects,
                'connacks': self.connacks,
                'publishes': self.publishes,
                'records': self.records,
                'bytes_in': self.bytes_in,
                'pings': self.pings,
                'unique_epochs': len(self.epochs_seen),
                'epoch_min': min(self.epochs_seen) if self.epochs_seen else None,
                'epoch_max': max(self.epochs_seen) if self.epochs_seen else None,
                'tls_ok': self.tls_ok,
                'tls_failures': self.tls_failures,
                'client_ids': self.client_ids[:10],
                'qos_seen': self.qos_seen,
                'retain_seen': {str(k): v for k, v in self.retain_seen.items()},
                'will_topics': self.will_topics[:5],
                'connect_frames': self.connect_frames[:5],
                'status_msgs': self.status_msgs[:10],
                'connect_intervals': self.connect_intervals[-60:],
                'errors': self.errors[-40:],
                'msgs': self.msgs[-400:],
            }
        tmp = self.path + '.tmp'
        with open(tmp, 'w') as fh:
            json.dump(d, fh, indent=1)
        os.replace(tmp, self.path)
        return d


def read_exact(sock, n):
    buf = b''
    while len(buf) < n:
        c = sock.recv(n - len(buf))
        if not c:
            return None
        buf += c
    return buf


def read_packet(sock):
    """Return (ptype, flags, payload) or None on clean EOF."""
    b0 = read_exact(sock, 1)
    if b0 is None:
        return None
    ptype = b0[0] >> 4
    flags = b0[0] & 0x0F
    mult, length = 1, 0
    while True:
        b = read_exact(sock, 1)
        if b is None:
            return None
        length += (b[0] & 0x7F) * mult
        if not (b[0] & 0x80):
            break
        mult *= 128
        if mult > 128 ** 4:
            raise ValueError('malformed remaining length')
    body = read_exact(sock, length) if length else b''
    if body is None:
        return None
    return ptype, flags, body


def enc_len(n):
    out = b''
    while True:
        d = n % 128
        n //= 128
        if n:
            d |= 0x80
        out += bytes([d])
        if not n:
            return out


def parse_connect(body):
    """Return dict with protocol name/level, client id, will, user, keepalive."""
    i = 0
    nlen = struct.unpack('>H', body[i:i + 2])[0]
    i += 2
    name = body[i:i + nlen].decode('latin-1')
    i += nlen
    level = body[i]
    i += 1
    cflags = body[i]
    i += 1
    keepalive = struct.unpack('>H', body[i:i + 2])[0]
    i += 2

    def rd_str():
        nonlocal i
        ln = struct.unpack('>H', body[i:i + 2])[0]
        i += 2
        s = body[i:i + ln]
        i += ln
        return s

    cid = rd_str().decode('utf-8', 'replace')
    will_topic = will_msg = None
    if cflags & 0x04:
        will_topic = rd_str().decode('utf-8', 'replace')
        will_msg = rd_str().decode('utf-8', 'replace')
    user = passwd = None
    if cflags & 0x80:
        user = rd_str().decode('utf-8', 'replace')
    if cflags & 0x40:
        passwd = rd_str().decode('utf-8', 'replace')
    return {
        'proto': name, 'level': level, 'clientId': cid, 'keepalive': keepalive,
        'clean': bool(cflags & 0x02), 'willTopic': will_topic,
        'willMsg': will_msg, 'willRetain': bool(cflags & 0x20),
        'willQos': (cflags >> 3) & 0x03, 'user': user, 'pass': passwd,
    }


def parse_publish(flags, body):
    qos = (flags >> 1) & 0x03
    tlen = struct.unpack('>H', body[0:2])[0]
    topic = body[2:2 + tlen].decode('utf-8', 'replace')
    i = 2 + tlen
    pid = None
    if qos > 0:
        pid = struct.unpack('>H', body[i:i + 2])[0]
        i += 2
    return topic, qos, pid, body[i:]


def serve(conn, args, stats):
    mode = args.mode
    with LOCK:
        stats.conns += 1
    try:
        if mode == 'rst':
            conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                            struct.pack('ii', 1, 0))
            conn.close()
            return

        conn.settimeout(args.read_timeout)
        pkt = read_packet(conn)
        if pkt is None:
            return
        ptype, flags, body = pkt
        if ptype != CONNECT:
            with LOCK:
                stats.errors.append(f'first packet type {ptype}, expected CONNECT')
            return
        info = parse_connect(body)
        stats.note_connect(info['clientId'])
        with LOCK:
            if info.get('willTopic') and info['willTopic'] not in stats.will_topics:
                stats.will_topics.append(info['willTopic'])
            if len(stats.connect_frames) < 5:
                stats.connect_frames.append(dict(info))
        with LOCK:
            stats.errors.append(
                'CONNECT ' + json.dumps({k: info[k] for k in
                                         ('proto', 'level', 'clientId', 'keepalive',
                                          'user', 'willTopic')}))

        if mode == 'no_connack':
            time.sleep(args.delay)
            return
        if mode == 'garbage':
            conn.sendall(os.urandom(64))
            return
        if mode == 'half_connack':
            conn.sendall(b'\x20\x02')
            time.sleep(args.delay)
            return
        if mode == 'slow_connack':
            time.sleep(args.delay)

        rc = {'connack_refuse': 5, 'connack_badproto': 1,
              'connack_badid': 2, 'connack_unavail': 3}.get(mode, 0)
        conn.sendall(bytes([CONNACK << 4, 2, 0, rc]))
        with LOCK:
            stats.connacks += 1
        if rc != 0:
            return
        if mode == 'drop_after_connack':
            conn.close()
            return

        # Steady state.
        deadline_ka = info['keepalive'] * 2 if info['keepalive'] else 0
        while True:
            conn.settimeout(args.read_timeout)
            pkt = read_packet(conn)
            if pkt is None:
                return
            ptype, flags, body = pkt
            if ptype == PUBLISH:
                topic, qos, pid, payload = parse_publish(flags, body)
                if mode == 'drop_on_publish':
                    with LOCK:
                        stats.errors.append(f'dropping on PUBLISH to {topic}')
                    conn.close()
                    return
                if mode == 'rst_on_publish':
                    with LOCK:
                        stats.errors.append(f'RST on PUBLISH to {topic}')
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                    struct.pack('ii', 1, 0))
                    conn.close()
                    return
                retain = bool(flags & 0x01)
                dup = bool(flags & 0x08)
                stats.note_publish(topic, payload, qos=qos, retain=retain, dup=dup)
                if topic.endswith('/status'):
                    with LOCK:
                        stats.status_msgs.append(
                            {'topic': topic, 'retain': retain,
                             'payload': payload.decode('utf-8', 'replace')[:200]})
                if qos == 1 and pid is not None:
                    conn.sendall(bytes([PUBACK << 4, 2]) + struct.pack('>H', pid))
            elif ptype == PINGREQ:
                with LOCK:
                    stats.pings += 1
                if mode != 'no_pingresp':
                    conn.sendall(bytes([PINGRESP << 4, 0]))
            elif ptype == SUBSCRIBE:
                pid = struct.unpack('>H', body[0:2])[0]
                # One granted QoS byte per requested filter.
                i, granted = 2, b''
                while i < len(body):
                    ln = struct.unpack('>H', body[i:i + 2])[0]
                    i += 2 + ln
                    granted += bytes([body[i]])
                    i += 1
                conn.sendall(bytes([SUBACK << 4]) + enc_len(2 + len(granted)) +
                             struct.pack('>H', pid) + granted)
            elif ptype == DISCONNECT:
                return
    except Exception as e:
        with LOCK:
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
    ap.add_argument('--read-timeout', type=float, default=120.0)
    ap.add_argument('--stats', default='mqtt_stats.json')
    ap.add_argument('--records', default='')
    ap.add_argument('--tls-fault', default='',
                    choices=['', 'blackhole', 'garbage', 'rst', 'slow'])
    args = ap.parse_args()

    stats = Stats(args.stats, args.records)

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.bind, args.port))
    srv.listen(16)

    ctx = None
    if args.tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.cert, args.key)
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
    print(f'[{"mqtts" if args.tls else "mqtt"}] listening on {args.bind}:{args.port} '
          f'mode={args.mode} tls_fault={args.tls_fault or "-"}', flush=True)

    while True:
        try:
            conn, _ = srv.accept()
        except OSError:
            break
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def wrapped(conn=conn):
            if args.tls:
                f = args.tls_fault
                if f == 'rst':
                    with LOCK:
                        stats.conns += 1
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                    struct.pack('ii', 1, 0))
                    conn.close()
                    return
                if f == 'blackhole':
                    with LOCK:
                        stats.conns += 1
                    time.sleep(args.delay)
                    conn.close()
                    return
                if f == 'garbage':
                    with LOCK:
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
                    with LOCK:
                        stats.tls_ok += 1
                except Exception as e:
                    with LOCK:
                        stats.tls_failures += 1
                        stats.errors.append(f'TLS: {type(e).__name__}: {e}')
                    try:
                        conn.close()
                    except Exception:
                        pass
                    return
                serve(tconn, args, stats)
            else:
                serve(conn, args, stats)

        threading.Thread(target=wrapped, daemon=True).start()


if __name__ == '__main__':
    main()
