#!/usr/bin/env python3
"""mini_mqtt_broker.py — broker MQTT cru que imprime payloads (investigação)."""
import socket
import struct
import sys
import threading
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 11883
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('0.0.0.0', PORT))
srv.listen(4)
print(f'mini-broker na {PORT}', flush=True)
counts = {'pub': 0, 'bytes': 0}
t0 = time.time()


def handle(c):
    try:
        while True:
            hdr = c.recv(1)
            if not hdr:
                break
            ptype = hdr[0] >> 4
            mult = 1
            rl = 0
            while True:
                x = c.recv(1)
                if not x:
                    return
                rl += (x[0] & 0x7F) * mult
                mult *= 128
                if not (x[0] & 0x80):
                    break
            body = b''
            while len(body) < rl:
                chunk = c.recv(rl - len(body))
                if not chunk:
                    return
                body += chunk
            if ptype == 1:
                c.sendall(b'\x20\x02\x00\x00')
                print(f'[{time.time()-t0:6.1f}s] CONNECT -> CONNACK', flush=True)
            elif ptype == 3:
                counts['pub'] += 1
                counts['bytes'] += rl
                tlen = struct.unpack('>H', body[:2])[0]
                topic = body[2:2 + tlen].decode('latin-1')
                payload = body[2 + tlen:]
                if counts['pub'] <= 10:
                    print(f'[{time.time()-t0:6.1f}s] PUBLISH #{counts["pub"]} '
                          f'topic={topic} len={len(payload)}', flush=True)
                    print('    payload:', payload[:400], flush=True)
                if counts['pub'] % 50 == 0:
                    print(f'[{time.time()-t0:6.1f}s] ... {counts["pub"]} publishes, '
                          f'{counts["bytes"]} B', flush=True)
                    print(f'    payload #50k:', payload[:400], flush=True)
            elif ptype == 12:
                c.sendall(b'\xd0\x00')
    except OSError as e:
        print(f'[{time.time()-t0:6.1f}s] conn ended: {e}', flush=True)


while True:
    c, a = srv.accept()
    print(f'[{time.time()-t0:6.1f}s] accept {a}', flush=True)
    threading.Thread(target=handle, args=(c,), daemon=True).start()
