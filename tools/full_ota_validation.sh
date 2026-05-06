#!/usr/bin/env bash
# ============================================================================
# full_ota_validation.sh — pipeline de validação completa pós-power-cycle
#
# Roda automaticamente quando device for replugado:
#   1. Detecta BOOTSEL ou app mode
#   2. Flash v3.43.11 (build atual em .pio/build/)
#   3. Aguarda CLI ready
#   4. Configura WiFi via CLI
#   5. Aguarda WiFi associate
#   6. Roda 3 ciclos de OTA apply (cada com captura de OTP do Serial)
#   7. Roda test_device_full.py para suite completa
#   8. Gera relatório consolidado em docs/test_reports/
#
# Use após replug físico do USB:
#   ./tools/full_ota_validation.sh
# ============================================================================
set -uo pipefail

cd /home/angelo/Documentos/SIMUT/

PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
WIFI_SSID="${WIFI_SSID:-ProcrastinationPLUS}"
WIFI_PASS="${WIFI_PASS:-A\$AGzD3XeY7xSrwAg5JF}"
DEVICE_IP="192.168.3.195"
TS=$(date +%Y%m%d-%H%M%S)
LOG=docs/test_reports/full_validation_${TS}.log
mkdir -p docs/test_reports
touch "$LOG"

log() { echo "[$(date +%T)] $*" | tee -a "$LOG"; }
err() { echo "[$(date +%T)] ERROR: $*" | tee -a "$LOG"; exit 1; }

# Step 1: detect device state
log "=== Step 1: Device state detection ==="
usb=$(lsusb | grep -oE "2e8a:[0-9a-f]+" | head -1)
log "USB id: $usb"

if [ "$usb" = "2e8a:f00a" ]; then
    log "Device em app mode. Tentando 1200bps trick..."
    timeout 5 ./.venv/bin/python3 -c "
import serial,time
try:
    s=serial.Serial('$PORT',1200,timeout=1); time.sleep(0.5); s.close()
except: pass
" 2>&1 | head -3
    sleep 5
    usb=$(lsusb | grep -oE "2e8a:[0-9a-f]+" | head -1)
    log "USB id após 1200bps: $usb"
fi

if [ "$usb" != "2e8a:0003" ]; then
    err "Device não está em BOOTSEL (atual: $usb). Power cycle físico necessário."
fi

# Step 2: Flash
log "=== Step 2: Flash v3.43.11 ==="
if ! [ -f .pio/build/pico_w_release/firmware.uf2 ]; then
    err "firmware.uf2 não encontrado. Run: pio run"
fi
picotool load -f -x .pio/build/pico_w_release/firmware.uf2 2>&1 | tail -3 | tee -a "$LOG"
sleep 5

# Step 3: Wait CLI
log "=== Step 3: Wait CLI alive ==="
for i in $(seq 1 30); do
    sleep 5
    out=$(timeout 5 ./.venv/bin/python3 -c "
import serial,time
try:
    s=serial.Serial('$PORT',115200,timeout=2)
    time.sleep(2); s.read(8192)
    s.write(b'\r\nshow system info\r\n'); time.sleep(2)
    print(s.read(4096).decode(errors='replace'))
    s.close()
except Exception as e:
    print(f'err: {e}')
" 2>&1)
    if echo "$out" | grep -q "Firmware:"; then
        ver=$(echo "$out" | grep -oE "v3\\.[0-9]+\\.[0-9]+" | head -1)
        log "CLI alive em $((i*5))s — firmware=$ver"
        break
    fi
    log "  attempt $i: CLI silent"
done

if ! echo "$out" | grep -q "Firmware:"; then
    err "CLI não respondeu em 150s"
fi

# Step 4: Configure WiFi
log "=== Step 4: Configure WiFi ==="
./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(1); s.read(8192)
for c in [b'conf system ssid $WIFI_SSID\r\n',
          b'conf system pass $WIFI_PASS\r\n',
          b'conf ip dhcp\r\n',
          b'write memory\r\n']:
    s.write(c); time.sleep(2); s.read(4096)
s.write(b'reload confirm\r\n'); time.sleep(1)
s.close()
print('reload sent')
" 2>&1 | tee -a "$LOG"

sleep 10

# Step 5: Wait WiFi up
log "=== Step 5: Wait WiFi associate ==="
for i in $(seq 1 20); do
    sleep 5
    out=$(timeout 5 ./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(1); s.read(8192)
s.write(b'\r\nshow net status\r\n'); time.sleep(2)
print(s.read(4096).decode(errors='replace'))
s.close()
" 2>&1)
    if echo "$out" | grep -qE "IP: *[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"; then
        log "WiFi UP em $((i*5+10))s"
        break
    fi
done

if ! echo "$out" | grep -qE "IP: *[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"; then
    err "WiFi não associou em 100s"
fi

# Step 6: Reset admin password (factory mode generated OTP)
log "=== Step 6: Get admin OTP ==="
./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(1); s.read(8192)
s.write(b'\r\nconf system admin reset confirm\r\nwrite memory\r\n'); time.sleep(3)
out = s.read(4096).decode(errors='replace')
s.close()
print(out)
" 2>&1 | tee -a "$LOG" | grep -A2 "Senha admin"

OTP=$(grep -A1 "Senha admin" "$LOG" | tail -1 | grep -oE "[A-Z0-9]{6,12}" | head -1)
if [ -z "$OTP" ]; then
    err "Não consegui extrair OTP do output"
fi
log "OTP capturado: $OTP"

# Step 7: Run OTA cycles
log "=== Step 7: 3 OTA apply cycles ==="
PASS=$OTP
for cycle in 1 2 3; do
    log "----- Cycle $cycle -----"
    NEW_PASS="OtaTest${cycle}@2026"
    log "Cycle $cycle: chpass $PASS → $NEW_PASS"

    cycle_start=$SECONDS
    ./tools/ota_apply.py \
        --ip $DEVICE_IP \
        --user admin --pass "$PASS" --new-pass "$NEW_PASS" \
        --firmware .pio/build/pico_w_release/firmware.bin \
        --no-restore 2>&1 | tee -a "$LOG"
    cycle_dt=$((SECONDS - cycle_start))
    log "Cycle $cycle completed in ${cycle_dt}s"

    # Capture next OTP from serial
    log "Cycle $cycle: capturing OTP from serial..."
    sleep 5
    OTP=$(timeout 30 ./.venv/bin/python3 -c "
import serial,time,sys,re
s=serial.Serial('$PORT',115200,timeout=0.5)
end=time.time()+25
buf=b''
while time.time()<end:
    n=s.read(512)
    if n: buf+=n
    if b'Senha ADMIN inicial' in buf:
        time.sleep(0.5); buf+=s.read(1024)
        break
s.close()
m=re.search(rb'Senha ADMIN inicial:\s*([A-Z0-9]+)', buf)
print(m.group(1).decode() if m else 'NONE')
" 2>&1 | tail -1)

    if [ -z "$OTP" ] || [ "$OTP" = "NONE" ]; then
        log "WARNING: não consegui OTP do ciclo $cycle. Tentando reload + nova captura..."
        sleep 15
        OTP=$(timeout 30 ./.venv/bin/python3 -c "
import serial,time,re
s=serial.Serial('$PORT',115200,timeout=0.5)
time.sleep(1); s.read(8192)
s.write(b'\r\nshow system info\r\n'); time.sleep(2)
buf=s.read(4096)
s.close()
print(buf.decode(errors='replace'))
" 2>&1)
        log "After cycle $cycle device state:"
        echo "$OTP" | tee -a "$LOG"
        break
    fi
    log "Next OTP: $OTP"
    PASS=$OTP
done

# Step 8: Run full test suite
log "=== Step 8: Run test_device_full.py ==="
# Use last known credentials (which should still work)
./tools/test_device_full.py \
    --ip $DEVICE_IP --user admin --pass "$PASS" \
    --report-dir docs/test_reports 2>&1 | tee -a "$LOG" || true

log "=== Validation complete. Log: $LOG ==="
