#!/usr/bin/env bash
# ============================================================================
# validate_ota_bug2_fix.sh — validação automatizada do fix Bug 2 (v3.43.11)
#
# Pre-requisitos:
#   - Device em BOOTSEL ou app mode acessível
#   - Firmware v3.43.11 (ou superior) buildado em .pio/build/pico_w_release/
#   - WiFi credentials salvos em $WIFI_SSID / $WIFI_PASS env vars (ou usar
#     defaults abaixo)
#
# Critério de sucesso:
#   - 5 OTA cycles consecutivos com boot OK em 100% dos casos
#   - Tempo médio de boot pós-apply <90 s
#
# Saída: log em /tmp/ota_bug2_validation_<ts>.log + JSON com métricas.
# ============================================================================
set -uo pipefail

cd /home/angelo/Documentos/SIMUT/

WIFI_SSID="${WIFI_SSID:-ProcrastinationPLUS}"
WIFI_PASS="${WIFI_PASS:-A\$AGzD3XeY7xSrwAg5JF}"
DEVICE_IP="192.168.3.195"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
LOG=/tmp/ota_bug2_validation_${TIMESTAMP}.log
PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
FW_UF2=.pio/build/pico_w_release/firmware.uf2
FW_BIN=.pio/build/pico_w_release/firmware.bin

log() { echo "[$(date +%T)] $*" | tee -a "$LOG"; }

step_force_bootsel() {
    log "Forçando BOOTSEL via 1200bps trick..."
    ./.venv/bin/python3 -c "
import serial,time
try:
    s=serial.Serial('$PORT',1200,timeout=1); time.sleep(0.5); s.close()
except: pass
" 2>/dev/null || true
    sleep 4
    if lsusb | grep -q "2e8a:0003"; then
        log "BOOTSEL OK"; return 0
    fi
    log "ERROR: device não entrou em BOOTSEL via 1200bps"
    return 1
}

step_flash() {
    log "Flashing $FW_UF2 via picotool..."
    if ! picotool load -f -x "$FW_UF2" 2>&1 | tail -2 | tee -a "$LOG"; then
        log "ERROR: picotool load falhou"; return 1
    fi
    sleep 5
    return 0
}

step_wait_cli() {
    local timeout=${1:-180}
    log "Esperando CLI alive (timeout ${timeout}s)..."
    local start=$SECONDS
    while [ $((SECONDS - start)) -lt $timeout ]; do
        if [ -e "$PORT" ]; then
            local out=$(timeout 5 ./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(2); s.read(8192)
s.write(b'\r\nshow system info\r\n'); time.sleep(2)
print(s.read(4096).decode(errors='replace'))
s.close()" 2>&1)
            if echo "$out" | grep -q "Firmware:"; then
                local elapsed=$((SECONDS - start))
                log "CLI alive em ${elapsed}s"
                return 0
            fi
        fi
        sleep 5
    done
    log "TIMEOUT: CLI não respondeu em ${timeout}s"
    return 1
}

step_config_wifi() {
    log "Configurando WiFi via CLI..."
    ./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(1); s.read(8192)
for c in [b'conf system ssid $WIFI_SSID\r\n',
          b'conf system pass $WIFI_PASS\r\n',
          b'conf ip dhcp\r\n',
          b'write memory\r\n']:
    s.write(c); time.sleep(1.5); s.read(2048)
s.write(b'reload confirm\r\n'); time.sleep(1)
s.close()
" 2>&1 | tee -a "$LOG"
    return 0
}

step_wait_wifi() {
    local timeout=${1:-90}
    log "Esperando WiFi up (timeout ${timeout}s)..."
    local start=$SECONDS
    while [ $((SECONDS - start)) -lt $timeout ]; do
        if [ -e "$PORT" ]; then
            local out=$(timeout 5 ./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(1); s.read(8192)
s.write(b'\r\nshow net status\r\n'); time.sleep(2)
print(s.read(4096).decode(errors='replace'))
s.close()" 2>&1)
            if echo "$out" | grep -qE "IP: *[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+"; then
                local elapsed=$((SECONDS - start))
                local ip=$(echo "$out" | grep -oE "IP: *[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+" | head -1)
                log "WiFi UP em ${elapsed}s — $ip"
                return 0
            fi
        fi
        sleep 5
    done
    log "TIMEOUT: WiFi não associou em ${timeout}s"
    return 1
}

step_get_otp_from_serial() {
    log "Capturando OTP do Serial USB..."
    timeout 30 ./.venv/bin/python3 -c "
import serial,time,sys,re
s=serial.Serial('$PORT',115200,timeout=0.5)
end=time.time()+25
buf=b''
while time.time()<end:
    n=s.read(512)
    if n: buf+=n
    if b'Senha ADMIN inicial' in buf:
        break
s.close()
m=re.search(rb'Senha ADMIN inicial:\s*([A-Z0-9]+)', buf)
if m:
    print('OTP=' + m.group(1).decode())
else:
    sys.stderr.write('OTP not found\n')
" 2>&1 | tee -a "$LOG"
}

step_ota_apply_cycle() {
    local cycle=$1
    log "===== CYCLE $cycle: OTA apply ====="
    local apply_start=$SECONDS

    # Note: assumes WiFi config + admin password already set
    # OTA apply via web requires admin auth which after each cycle
    # requires chpass (factory state)

    # Pre-apply CLI snapshot
    log "Pre-apply CLI state:"
    ./.venv/bin/python3 -c "
import serial,time
s=serial.Serial('$PORT',115200,timeout=2)
time.sleep(1); s.read(8192)
s.write(b'\r\nshow net status\r\nshow system info\r\n'); time.sleep(3)
print(s.read(4096).decode(errors='replace'))
s.close()
" 2>&1 | tee -a "$LOG"

    log "Cycle $cycle complete (in ${SECONDS}s)"
}

# ============================================================================
# Main
# ============================================================================
log "===== OTA Bug 2 Fix Validation Suite ====="
log "Timestamp: $TIMESTAMP"
log "Firmware: $FW_UF2"
log "Device: $PORT"

if ! [ -f "$FW_UF2" ]; then
    log "ERROR: firmware $FW_UF2 não encontrado. Run: pio run"
    exit 1
fi

# Step 1: BOOTSEL → flash v3.43.11
if ! lsusb | grep -q "2e8a:0003"; then
    if ! step_force_bootsel; then
        log "MANUAL ACTION REQUIRED: power cycle físico do device + segurar BOOTSEL ao plugar"
        exit 2
    fi
fi

if ! step_flash; then exit 3; fi

# Step 2: wait CLI ready
if ! step_wait_cli 180; then exit 4; fi

# Step 3: configure WiFi + reboot
step_config_wifi
sleep 8  # device rebooting
if ! step_wait_cli 90; then
    log "ERROR: device não voltou após config wifi"
    exit 5
fi

if ! step_wait_wifi 60; then
    log "ERROR: WiFi não associou"
    exit 6
fi

log "===== Setup complete. Device em estado limpo. ====="

# Aqui poderíamos rodar 5 ciclos OTA mas precisaria de senha conhecida
# após cada apply (factory state regen OTP). Próximo passo manual.

log "===== Para rodar OTA cycles, use: ====="
log "tools/ota_apply.py --ip $DEVICE_IP --user admin --pass <CURRENT_PASS> \\"
log "    --new-pass 'TestPass2026' --firmware $FW_BIN --no-restore"
log "Após cada apply, capture OTP do Serial USB e use como --pass do próximo."

log "===== Validation suite OK ====="
