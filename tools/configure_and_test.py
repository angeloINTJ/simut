#!/usr/bin/env python3
"""Configure WiFi and test Pico stability after lockout fixes."""
import serial, time, socket, re, sys

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=5)
ser.dtr = True
time.sleep(2)
ser.reset_input_buffer()
time.sleep(1)
ser.read(4096)  # flush boot

def cmd(text, wait=2.0):
    ser.write((text + '\r\n').encode())
    time.sleep(wait)
    return ser.read(8192).decode('utf-8', errors='replace')

# Read boot
print('=== BOOT ===')
r = cmd('show system info')
for l in r.split('\n'):
    if 'Firmware' in l or 'Serial' in l:
        print(f'  {l.strip()}')

print('\n=== SENSORES (factory defaults) ===')
r = cmd('show sensors')
for l in r.split('\n'):
    if 'Slot' in l or 'DS18' in l or 'DHT22' in l or 'GPIO' in l:
        print(f'  {l.strip()}')

print('\n=== CONFIGURANDO WIFI ===')
cmd('enable')
# Factory reset to clear any old config first
r = cmd('system factory confirm', wait=8)
print(f'  Factory reset: {r[:80].strip()}')

# Wait for reboot
ser.close()
print('  Aguardando reboot...')
for i in range(30):
    time.sleep(1)
    try:
        ser = serial.Serial('/dev/ttyACM0', 115200, timeout=5)
        ser.dtr = True; time.sleep(1)
        ser.reset_input_buffer()
        d = ser.read(4096)
        if 'SIMUT' in d.decode():
            print(f'  Reboot apos {i+1}s')
            break
    except: continue
else:
    print('  Pico nao respondeu')
    sys.exit(1)

def cmd(text, wait=2.0):
    ser.write((text + '\r\n').encode())
    time.sleep(wait)
    return ser.read(8192).decode('utf-8', errors='replace')

# Now configure WiFi from scratch
cmd('enable')
cmd('configure terminal')

r = cmd('wifi ssid ProcrastinationPLUS')
if 'SIMUT' not in r:
    print(f'  wifi ssid: FALHOU - {r[:60]}')
else:
    print(f'  wifi ssid: OK')

r = cmd('wifi pass A$AGzD3XeY7xSrwAg5JF')
if 'SIMUT' not in r:
    print(f'  wifi pass: FALHOU - {r[:60]}')
else:
    print(f'  wifi pass: OK')

cmd('end')
r = cmd('write memory', 5)
print(f'  write memory: {r[:60].strip()}')

# Wait for IP
print('\n=== WiFi CONNECTING ===')
for i in range(45):
    r = cmd('show net status', 1)
    m = re.search(r'\b(\d+\.\d+\.\d+\.\d+)\b', r)
    if m:
        ip = m.group(1)
        print(f'  IP: {ip}')
        break
    if (i+1) % 15 == 0:
        print(f'  ...{i+1}s')
else:
    print(f'  WiFi timeout. Status: {r[:150]}')

# API tests
if ip:
    print('\n=== API TEST ===')
    for path, name in [('/', 'Home'), ('/api/sensors', 'Sensors'),
                       ('/api/system', 'System'),
                       ('/api/history_multi?range=1', 'History V4')]:
        try:
            s = socket.socket(); s.settimeout(8)
            s.connect((ip, 80))
            s.send(f'GET {path} HTTP/1.0\r\nHost: {ip}\r\n\r\n'.encode())
            r2 = b''
            while True:
                d = s.recv(16384)
                if not d: break
                r2 += d
            s.close()
            ok = b'200' in r2[:50]
            print(f'  {name:12s} {path:35s} {"OK" if ok else "FAIL"} ({len(r2)}B)')
        except Exception as e:
            print(f'  {name}: ERROR {e}')

# 10s stability test
print('\n=== STABILITY (debug 10s) ===')
cmd('enable')
cmd('debug on')
time.sleep(10)
data = ser.read(16384).decode('utf-8', errors='replace')
cmd('debug off')
lines = [l.strip() for l in data.split('\n') if l.strip() and 'SIMUT' not in l]
errs = [l for l in lines if any(w in l.upper() for w in ['ERROR', 'PANIC', 'FAULT'])]
locks = [l for l in lines if 'Lockout' in l]
print(f'  Logs: {len(lines)} linhas')
if locks:
    print(f'  >>> Lockout messages: {len(locks)} <<<')
    for l in locks[:3]:
        print(f'    {l[:100]}')
else:
    print(f'  >>> ZERO lockout messages <<<')
if errs:
    print(f'  Errors: {len(errs)}')
print(f'  ZERO erros' if not errs else f'  {len(errs)} erros')

ser.close()
print('\n=== TESTE CONCLUIDO ===')
