#!/usr/bin/env bash
# validate_alpha14_full.sh — pipeline end-to-end de validação alpha14
# após device recovery (power-cycle físico ou similar).
#
# 10 etapas em sequência:
#   1. Pre-flight mão (PING/PONG)
#   2. Blink revival (confirma chip OK)
#   3. Flash alpha14 sobre blink
#   4. Capture boot 60s
#   5. WiFi config (HW reset via mão, evita safeReboot)
#   6. Verify HTTP 200
#   7. Test 'touch sim 50 220'
#   8. Capture TFT + browser screenshots
#   9. Loop20 OTA stats (comentado por padrão — leva 1h+)
#  10. Build MANUAL.pdf via pandoc
#
# Uso: F9_PASS='<senha>' bash tools/manual_capture/validate_alpha14_full.sh

set -uo pipefail
cd "$(dirname "$0")/../.."

SIMUT_IP=${SIMUT_IP:-192.168.3.195}
SIMUT_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
HAND_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00
F9_PASS=${F9_PASS:-F9Test@2026}
PYBIN=./.venv/bin/python3

log() { echo "[$(date +%H:%M:%S)] $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

# 1. Pre-flight
log "Step 1: Pre-flight check"
[ -e "$HAND_PORT" ] || fail "mão não detectada em $HAND_PORT"
$PYBIN -u -c "
import serial, time, sys
s = serial.Serial('$HAND_PORT', 115200, timeout=2); time.sleep(0.3); s.reset_input_buffer()
s.write(b'PING\n'); time.sleep(0.4)
r = s.read(64).decode(errors='replace').strip()
print('mão:', r)
sys.exit(0 if 'PONG' in r else 1)
" || fail "mão não responde PONG"

# 2. Blink revival
log "Step 2: Blink revival"
$PYBIN -u -c "
import serial, time
s = serial.Serial('$HAND_PORT', 115200, timeout=3); time.sleep(0.3); s.reset_input_buffer()
s.write(b'BOOTSEL\n'); time.sleep(3); s.close()
"
sleep 3
[ -e "/dev/disk/by-label/RPI-RP2" ] || fail "RPI-RP2 não detectado pós BOOTSEL"
picotool erase -a 2>&1 | tail -1
picotool load -x tools/test_firmwares/pico_blink_echo/build/pico_blink_echo.ino.uf2 2>&1 | tail -1
sleep 5
log "  verifying blink alive..."
timeout 4 $PYBIN -u -c "
import serial, time, sys
s = serial.Serial('$SIMUT_PORT', 115200, timeout=1); s.dsrdtr=False; s.rtscts=False
time.sleep(0.3)
out = s.read(512).decode(errors='replace')
if 'tick' not in out: sys.exit(1)
print('  blink OK')
s.close()
" || fail "blink revival não funcionou"

# 3. Flash alpha14
log "Step 3: Flash alpha14"
$PYBIN -u -c "
import serial, time
s = serial.Serial('$HAND_PORT', 115200, timeout=3); time.sleep(0.3); s.reset_input_buffer()
s.write(b'BOOTSEL\n'); time.sleep(3); s.close()
"
sleep 3
picotool load -x .pio/build/pico_w_release/firmware.uf2 2>&1 | tail -1

# 4. Capture boot
log "Step 4: Capturing boot 60s"
$PYBIN -u -c "
import serial, time, os, sys
PORT = '$SIMUT_PORT'
for i in range(40):
    if os.path.exists(PORT):
        try:
            s = serial.Serial(PORT, 115200, timeout=1); s.dsrdtr=False; s.rtscts=False
            break
        except: pass
    time.sleep(0.3)
deadline = time.time() + 60
while time.time() < deadline:
    chunk = s.read(2048)
    if chunk: sys.stdout.write(chunk.decode(errors='replace')); sys.stdout.flush()
print()
"

# 5. WiFi config + HW reset
log "Step 5: WiFi config + HW reset"
$PYBIN -u -c "
import serial, time
s = serial.Serial('$SIMUT_PORT', 115200, timeout=2); s.dsrdtr=False; s.rtscts=False
time.sleep(0.5); s.reset_input_buffer()
for c in [b'conf system ssid ProcrastinationPLUS\r\n',
          b'conf system pass A\$AGzD3XeY7xSrwAg5JF\r\n',
          b'write memory\r\n']:
    s.write(c); time.sleep(2)
s.close()
print('cfg saved')
"
$PYBIN -u -c "
import serial, time
s = serial.Serial('$HAND_PORT', 115200, timeout=3); time.sleep(0.3); s.reset_input_buffer()
s.write(b'RESET\n'); time.sleep(0.5); s.close()
"

# 6. Wait + verify HTTP
log "Step 6: Wait 60s + verify HTTP"
sleep 60
HTTP=$(curl -s -m 3 -o /dev/null -w "%{http_code}" "http://$SIMUT_IP/api/login_init")
[ "$HTTP" = "200" ] || fail "HTTP $HTTP — device não online"
log "  HTTP 200 ✓"

# 7. Test touch sim CLI
log "Step 7: Test 'touch sim 50 220'"
$PYBIN -u -c "
import serial, time, sys
s = serial.Serial('$SIMUT_PORT', 115200, timeout=2); s.dsrdtr=False; s.rtscts=False
time.sleep(0.3); s.reset_input_buffer()
s.write(b'touch sim 50 220\r\n'); time.sleep(1)
out = s.read(1024).decode(errors='replace')
print('  cli:', out[:200])
sys.exit(0 if 'injected' in out.lower() else 1)
" || log "  WARN: touch sim resposta não confirmada"

# 8. Capturar screenshots
log "Step 8: Capturing screenshots (TFT + browser)"
F9_PASS="$F9_PASS" bash tools/manual_capture/capture_tft_screenshots.sh || log "  WARN: TFT capture parcial"
F9_PASS="$F9_PASS" $PYBIN tools/manual_capture/capture_browser_screenshots.py 2>&1 || log "  WARN: browser capture falhou (selenium?)"

# 9. Loop20 stats (1h+)
log "Step 9: Loop20 OTA stats — pulando (1h+, descomente para rodar)"
# F9_PASS="$F9_PASS" bash tools/test_f9_loop20.sh

# 10. Build PDF
log "Step 10: Build MANUAL.pdf"
bash tools/manual_capture/build_manual_pdf.sh || log "  WARN: PDF build falhou (pandoc?)"

log "DONE — see docs/MANUAL.pdf, docs/screenshots/, docs/test_reports/"
