"""Upload all /data files to SIMUT LFS via /api/upload."""
import urllib.request, urllib.parse, json, hashlib, os, sys, time

SIMUT_IP = '192.168.3.195'
F9_PASS = 'F9Test@2026'

# Login
nonce = json.loads(urllib.request.urlopen(f'http://{SIMUT_IP}/api/login_init', timeout=5).read())['nonce']
hp = hashlib.sha256(F9_PASS.encode()).hexdigest()
data = urllib.parse.urlencode({'user':'admin','pass':hp,'nonce':nonce}).encode()
r = urllib.request.urlopen(f'http://{SIMUT_IP}/api/login', data=data, timeout=10)
cookie = r.headers.get('Set-Cookie','').split(';')[0]
print('logged in')

def mkdir(path):
    """Create LFS dir."""
    data = urllib.parse.urlencode({'path': path}).encode()
    req = urllib.request.Request(f'http://{SIMUT_IP}/api/mkdir', data=data, method='POST')
    req.add_header('Cookie', cookie)
    req.add_header('Content-Type', 'application/x-www-form-urlencoded')
    try:
        r = urllib.request.urlopen(req, timeout=10)
        return r.status
    except Exception as e:
        return str(e)[:80]

def upload(local, target_path):
    """Upload local file to LFS target_path (full path)."""
    name = os.path.basename(target_path)
    parent = os.path.dirname(target_path) or '/'
    with open(local, 'rb') as f:
        body = f.read()
    boundary = '----WebKitFormBoundaryUP'
    parts = [
        f'--{boundary}\r\n'.encode(),
        f'Content-Disposition: form-data; name="path"\r\n\r\n{parent}\r\n'.encode(),
        f'--{boundary}\r\n'.encode(),
        f'Content-Disposition: form-data; name="file"; filename="{name}"\r\n'.encode(),
        b'Content-Type: application/octet-stream\r\n\r\n',
        body,
        f'\r\n--{boundary}--\r\n'.encode(),
    ]
    payload = b''.join(parts)
    req = urllib.request.Request(f'http://{SIMUT_IP}/api/upload', data=payload, method='POST')
    req.add_header('Cookie', cookie)
    req.add_header('Content-Type', f'multipart/form-data; boundary={boundary}')
    req.add_header('Content-Length', str(len(payload)))
    for attempt in range(3):
        try:
            r = urllib.request.urlopen(req, timeout=60)
            return r.status, r.read().decode()[:100]
        except Exception as e:
            if attempt < 2:
                time.sleep(2)
                continue
            return 0, str(e)[:100]

# Walk data/ and upload everything
SKIP_DIRS = []  # could skip 'config' to preserve credentials
TARGET_ROOT = ''  # LFS root

uploaded = 0
failed = 0
total_bytes = 0

for root, dirs, files in os.walk('data'):
    rel = os.path.relpath(root, 'data')
    lfs_dir = '/' if rel == '.' else f'/{rel}'
    
    # Create dir if needed (mkdir is idempotent in firmware)
    if rel != '.':
        s = mkdir(lfs_dir)
        # ignore mkdir errors — file upload will create dir if needed
    
    for f in files:
        local = os.path.join(root, f)
        target = f'{lfs_dir.rstrip("/")}/{f}' if lfs_dir != '/' else f'/{f}'
        sz = os.path.getsize(local)
        status, resp = upload(local, target)
        if status == 200:
            uploaded += 1
            total_bytes += sz
            print(f'  ✓ {target} ({sz} bytes)')
        else:
            failed += 1
            print(f'  ✗ {target} ({sz} bytes) FAILED: {status} {resp}')

print(f'\nUploaded: {uploaded} files, {total_bytes} bytes total. Failed: {failed}')
