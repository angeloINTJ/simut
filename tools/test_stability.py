#!/usr/bin/env python3
"""Stability test - check if lockout and WDT fixes work."""
import serial, time, sys, re

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=5)
ser.dtr = True  # critical for earlephilhower core
time.sleep(2)
ser.reset_input_buffer()

def cmd(text, wait=2.0):
    ser.write((text + '\r\n').encode())
    time.sleep(wait)
    return ser.read(8192).decode('utf-8', errors='replace')

def read_all(timeout=3):
    buf = b''
    deadline = time.time() + timeout
    last = time.time()
    while time.time() < deadline:
        try:
            if ser.in_waiting:
                buf += ser.read(ser.in_waiting)
                last = time.time()
            elif time.time() - last > 0.3:
                break
            time.sleep(0.05)
        except:
            break
    return buf.decode('utf-8', errors='replace')

# 1. Read boot
time.sleep(1)
d = ser.read(4096).decode('utf-8', errors='replace')
print('=== BOOT LOG (first 500 chars) ===')
print(d[:500])

# 2. System info
print('\n=== SYSTEM INFO ===')
r = cmd('show system info')
for l in r.split('\n'):
    if 'Firmware' in l or 'Serial' in l:
        print(f'  {l.strip()}')

# 3. Sensors
print('\n=== SENSORS ===')
r = cmd('show sensors')
ds = r.count('DS18B20')
dht = r.count('DHT22')
bmp = r.count('BMP') + r.count('BME')
print(f'  DS18B20={ds} DHT22={dht} BMP280={bmp}')
for l in r.split('\n'):
    if 'Slot' in l or 'GPIO' in l or 'DS18' in l or 'DHT22' in l:
        print(f'  {l.strip()}')

# 4. GPIO
print('\n=== GPIO ===')
r = cmd('show gpio')
for l in r.split('\n'):
    if 'GPIO' in l and ('Slot' in l or 'FREE' in l):
        print(f'  {l.strip()}')

# 5. Storage
print('\n=== STORAGE ===')
r = cmd('show storage stats')
for l in r.split('\n'):
    if 'Total' in l or 'Used' in l or 'Free' in l:
        print(f'  {l.strip()}')

# 6. Enable + debug stream (15s) - CHECK FOR LOCKOUT MESSAGES
print('\n=== DEBUG STREAM (15s) ===')
cmd('enable', 0.5)
cmd('debug on', 0.5)
time.sleep(15)
data = read_all(3)
cmd('debug off', 0.5)

lines = [l for l in data.split('\n') if l.strip() and 'SIMUT' not in l]
lockout_msgs = [l for l in lines if 'Lockout' in l or 'lockout' in l.lower()]
errors = [l for l in lines if 'ERROR' in l or 'PANIC' in l or 'FAULT' in l]

print(f'  Total log lines: {len(lines)}')
if lockout_msgs:
    print(f'  >>> LOCKOUT MESSAGES: {len(lockout_msgs)} <<<')
    for l in lockout_msgs[:5]:
        print(f'    {l.strip()[:120]}')
else:
    print(f'  >>> ZERO LOCKOUT MESSAGES - FIX CONFIRMED <<<')
if errors:
    print(f'  Errors: {len(errors)}')
    for e in errors[:3]:
        print(f'    {e.strip()[:120]}')

# 7. WiFi config and connect
print('\n=== WIFI CONFIG ===')
cmd('enable', 0.5)
cmd('configure terminal', 0.5)
r = cmd('wifi ssid ProcrastinationPLUS')
print(f'  SSID: {"OK" if "SIMUT" in r else "FAIL"}')
r = cmd('wifi pass A$AGzD3XeY7xSrwAg5JF')
print(f'  PASS: {"OK" if "SIMUT" in r else "FAIL"}')
cmd('end', 0.5)
r = cmd('write memory', 5)
print(f'  Save: {"OK" if "OK" in r or "salva" in r.lower() else "CHECK"}')

print('\n=== WiFi CONNECTING ===')
for i in range(30):
    r = cmd('show net status', 1)
    m = re.search(r'\b(\d+\.\d+\.\d+\.\d+)\b', r)
    if m:
        ip = m.group(1)
        print(f'  IP: {ip} - WiFi OK!')
        break
    if (i+1) % 10 == 0:
        print(f'  ...{i+1}s')
else:
    print(f'  WiFi timeout')

# 8. Post-WiFi debug stream (10s) - check for lockout after flash writes
if ip:
    print('\n=== POST-WIFI DEBUG (10s) ===')
    cmd('enable', 0.5)
    cmd('debug on', 0.5)
    # Do a write memory to trigger flash operations
    cmd('configure terminal', 0.5)
    cmd('system name simut-test', 0.5)
    cmd('end', 0.5)
    cmd('write memory', 5)
    time.sleep(10)
    data2 = read_all(3)
    cmd('debug off', 0.5)
    lines2 = [l for l in data2.split('\n') if l.strip() and 'SIMUT' not in l]
    lockout2 = [l for l in lines2 if 'Lockout' in l]
    print(f'  Logs: {len(lines2)} lines, Lockout: {len(lockout2)}')
    if lockout2:
        print(f'  >>> LOCKOUT AFTER FLASH: {len(lockout2)} <<<')
    else:
        print(f'  >>> ZERO LOCKOUT AFTER FLASH <<<')

ser.close()
print('\n=== TESTE CONCLUIDO ===')
