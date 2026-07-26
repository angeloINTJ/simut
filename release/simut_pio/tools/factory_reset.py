#!/usr/bin/env python3
"""Factory reset Pico and configure WiFi."""
import serial, time, socket, re, sys

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=5)
ser.dtr = True
time.sleep(1.5)
ser.reset_input_buffer()
time.sleep(1)
ser.read(4096)  # flush boot

def cmd(text, wait=2.0):
    ser.write((text + '\r\n').encode())
    time.sleep(wait)
    return ser.read(8192).decode('utf-8', errors='replace')

print('=== FACTORY RESET ===')
# Enable first
r = cmd('enable')
print(f'Enable: {"OK" if "#" in r else "FAIL"}')

# Factory reset with confirm
r = cmd('conf system factory confirm', wait=5)
print(f'Factory: {r[:150]}')

# Wait for reboot
print('Waiting for reboot...')
ser.close()

for i in range(30):
    time.sleep(1)
    try:
        ser = serial.Serial('/dev/ttyACM0', 115200, timeout=5)
        ser.dtr = True
        time.sleep(1)
        ser.reset_input_buffer()
        time.sleep(0.5)
        d = ser.read(4096)
        text = d.decode('utf-8', errors='replace')
        if 'SIMUT' in text or 'Firmware' in text:
            print(f'Pico rebooted after {i+1}s')
            # Show boot message
            for l in text.split('\n'):
                if 'Firmware' in l or 'Serial' in l or 'SIMUT' in l:
                    print(f'  {l.strip()}')
            break
    except:
        continue
else:
    print('Pico did not come back')
    sys.exit(1)

# System info
def cmd(text, wait=2.0):
    ser.write((text + '\r\n').encode())
    time.sleep(wait)
    return ser.read(8192).decode('utf-8', errors='replace')

print('\n=== SYSTEM INFO ===')
r = cmd('show system info')
for l in r.split('\n'):
    if 'Firmware' in l or 'Serial' in l or 'WiFi' in l or 'SSID' in l:
        print(f'  {l.strip()}')

print('\n=== SENSORS ===')
r = cmd('show sensors')
for l in r.split('\n'):
    if 'Slot' in l or 'GPIO' in l or 'DS18' in l or 'DHT22' in l:
        print(f'  {l.strip()}')

print('\n=== GPIO ===')
r = cmd('show gpio')
for l in r.split('\n'):
    if 'GPIO' in l and ('Slot' in l or 'FREE' in l):
        print(f'  {l.strip()}')

print('\n=== STORAGE ===')
r = cmd('show storage stats')
for l in r.split('\n'):
    if 'Total' in l or 'Used' in l or 'Free' in l:
        print(f'  {l.strip()}')

ser.close()
print('\n=== Factory reset concluido ===')
