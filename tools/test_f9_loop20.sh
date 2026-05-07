#!/usr/bin/env bash
# ============================================================================
# test_f9_loop20.sh — 20 ciclos consecutivos de OTA F9 pra estatística
#
# Cada iteração:
#   1. Roda test_f9_snapshot.sh
#   2. Captura PASS/FAIL count
#   3. Se device travou (test FAIL irrecuperável), tenta recovery via
#      mão BOOTSEL + picotool load + WiFi reconfig
#   4. Próxima iteração
#
# Resultado: relatório final com taxa de sucesso, tempo médio, etc.
# ============================================================================
set -uo pipefail
cd /home/angelo/Documentos/SIMUT/

TS=$(date +%Y%m%d-%H%M%S)
LOOP_LOG=docs/test_reports/f9_loop20_${TS}.log
mkdir -p docs/test_reports
PYBIN=./.venv/bin/python3
HAND_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00
SIMUT_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00

PASS=0
FAIL=0
RECOVERY=0
TOTAL_TIME=0

log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOOP_LOG"; }

log "=== 20 ciclos OTA F9 — start $TS ==="

for iter in $(seq 1 20); do
    log ""
    log "================ ITERAÇÃO $iter / 20 ================"
    iter_start=$(date +%s)

    # Verifica se device está OK antes do test
    if ! curl -fsS --max-time 5 "http://192.168.3.195/api/login_init" >/dev/null 2>&1; then
        log "  Device offline — recovery via mão BOOTSEL + picotool load"
        $PYBIN -u -c "
import serial, time
s = serial.Serial('$HAND_PORT', 115200, timeout=3); time.sleep(0.5); s.reset_input_buffer()
s.write(b'BOOTSEL\n'); time.sleep(0.7); s.read(64); s.close()
"
        sleep 2
        if ls /dev/disk/by-label/RPI-RP2 >/dev/null 2>&1; then
            picotool load -x .pio/build/pico_w_release/firmware.uf2 2>&1 | tail -1 | tee -a "$LOOP_LOG"
            sleep 30
            # Reconfig WiFi
            $PYBIN -u -c "
import serial, time
s = serial.Serial('$SIMUT_PORT', 115200, timeout=2); time.sleep(2); s.reset_input_buffer()
for c in [b'conf system ssid ProcrastinationPLUS\r\n', b'conf system pass A\$AGzD3XeY7xSrwAg5JF\r\n', b'write memory\r\n']:
    s.write(c); time.sleep(2); s.read(2048)
s.close()
"
            $PYBIN -u -c "
import serial, time
s = serial.Serial('$HAND_PORT', 115200, timeout=2); time.sleep(0.3); s.reset_input_buffer()
s.write(b'RESET\n'); time.sleep(0.5); s.read(64); s.close()
"
            sleep 60
            RECOVERY=$((RECOVERY+1))
        fi
    fi

    # Roda o test
    if bash tools/test_f9_snapshot.sh 2>&1 | tee -a "$LOOP_LOG" | tail -2 | grep -q "FAIL: 0"; then
        PASS=$((PASS+1))
        log "  iter $iter: PASS"
    else
        FAIL=$((FAIL+1))
        log "  iter $iter: FAIL"
    fi

    iter_dt=$(($(date +%s) - iter_start))
    TOTAL_TIME=$((TOTAL_TIME+iter_dt))
    log "  iter $iter duração: ${iter_dt}s"
done

log ""
log "============== RELATÓRIO FINAL =============="
log "Total iterações: 20"
log "PASS:    $PASS / 20 ($((PASS*5))%)"
log "FAIL:    $FAIL / 20"
log "Recovery: $RECOVERY (vezes que precisei flashar baseline antes)"
log "Tempo total: ${TOTAL_TIME}s ($((TOTAL_TIME/60)) min)"
log "Tempo médio por iter: $((TOTAL_TIME/20))s"
log "============================================"
log "Log: $LOOP_LOG"
