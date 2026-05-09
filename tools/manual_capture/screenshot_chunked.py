#!/usr/bin/env python3
"""screenshot_chunked.py — captura TFT screenshot via /api/screenshot_chunk com CRC.

Endpoint (alpha16+): GET /api/screenshot_chunk?n=N
Retorna 12 bytes header + 15360 bytes payload (último chunk pode ser menor).
Header (big-endian): [chunk_idx u32 | payload_size u32 | crc32 u32].
CRC32 EDB88320 (gzip-compatible).

Total: 15 chunks (240/16 rows). Cliente verifica CRC + retry no chunk
individual em case de mismatch. Reassembla BMP 320×240 24-bit.

Uso:
    F9_PASS=<senha> python3 screenshot_chunked.py <output.bmp> [retries=3]
"""
import os, sys, time, json, hashlib, urllib.request, urllib.parse, struct
import zlib  # padrão da python pra CRC32 EDB88320

SIMUT_IP = os.getenv('SIMUT_IP', '192.168.3.195')
F9_PASS = os.getenv('F9_PASS', 'F9Test@2026')
W = 320
H = 240
ROWS_PER_CHUNK = 16
TOTAL_CHUNKS = (H + ROWS_PER_CHUNK - 1) // ROWS_PER_CHUNK  # 15
CHUNK_BYTES = ROWS_PER_CHUNK * W * 3  # 15360

def login():
    nonce = json.loads(urllib.request.urlopen(f'http://{SIMUT_IP}/api/login_init', timeout=5).read())['nonce']
    hp = hashlib.sha256(F9_PASS.encode()).hexdigest()
    data = urllib.parse.urlencode({'user':'admin','pass':hp,'nonce':nonce}).encode()
    r = urllib.request.urlopen(f'http://{SIMUT_IP}/api/login', data=data, timeout=10)
    return r.headers.get('Set-Cookie','').split(';')[0]

def fetch_chunk(cookie, n, max_retries=3):
    """Get chunk N with CRC verification + retry."""
    for attempt in range(max_retries):
        try:
            req = urllib.request.Request(f'http://{SIMUT_IP}/api/screenshot_chunk?n={n}')
            req.add_header('Cookie', cookie)
            r = urllib.request.urlopen(req, timeout=20)
            blob = r.read()
            if len(blob) < 12:
                raise ValueError(f'short response: {len(blob)} bytes')
            chunk_idx, payload_sz, crc_want = struct.unpack('>III', blob[:12])
            payload = blob[12:12+payload_sz]
            if len(payload) != payload_sz:
                raise ValueError(f'payload truncated: got {len(payload)}, expected {payload_sz}')
            if chunk_idx != n:
                raise ValueError(f'wrong chunk: got {chunk_idx}, expected {n}')
            crc_got = zlib.crc32(payload) & 0xFFFFFFFF
            if crc_got != crc_want:
                raise ValueError(f'CRC mismatch: got 0x{crc_got:08X}, expected 0x{crc_want:08X}')
            return payload, payload_sz
        except Exception as e:
            print(f'  chunk {n} attempt {attempt+1}/{max_retries} failed: {e}', file=sys.stderr)
            if attempt == max_retries - 1:
                raise
            time.sleep(0.5)
    raise RuntimeError(f'chunk {n} failed after {max_retries} attempts')

def main():
    if len(sys.argv) < 2:
        print('Usage: F9_PASS=<senha> screenshot_chunked.py <output.bmp> [retries]', file=sys.stderr)
        sys.exit(1)
    out_path = sys.argv[1]
    retries = int(sys.argv[2]) if len(sys.argv) > 2 else 3

    cookie = login()
    print(f'logged in (cookie: {cookie[:30]})')

    # Fetch all chunks
    chunks = []
    for n in range(TOTAL_CHUNKS):
        t0 = time.time()
        payload, sz = fetch_chunk(cookie, n, retries)
        dt = time.time() - t0
        chunks.append(payload)
        print(f'  chunk {n}: {sz} bytes OK ({dt:.2f}s)')

    # Reassemble BMP
    img_bytes = b''.join(chunks)
    expected_img = H * W * 3
    if len(img_bytes) != expected_img:
        print(f'WARN: image bytes mismatch: got {len(img_bytes)}, expected {expected_img}', file=sys.stderr)

    img_size = len(img_bytes)
    file_size = 54 + img_size
    bmp_header = bytes([
        ord('B'), ord('M'),
        file_size & 0xFF, (file_size>>8)&0xFF, (file_size>>16)&0xFF, (file_size>>24)&0xFF,
        0,0,0,0, 54,0,0,0, 40,0,0,0,
        W & 0xFF, (W>>8)&0xFF, 0,0,
        H & 0xFF, (H>>8)&0xFF, 0,0,
        1,0, 24,0, 0,0,0,0,
        img_size & 0xFF, (img_size>>8)&0xFF, (img_size>>16)&0xFF, (img_size>>24)&0xFF,
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    ])

    with open(out_path, 'wb') as f:
        f.write(bmp_header)
        f.write(img_bytes)

    print(f'\n✓ BMP saved: {out_path} ({file_size} bytes total)')
    print(f'  All {TOTAL_CHUNKS} chunks verified via CRC32')

if __name__ == '__main__':
    main()
