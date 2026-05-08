#!/usr/bin/env bash
# capture_tft_screenshots.sh — automação de captura de telas TFT via touch sim
#
# Pré-requisitos:
#   - alpha14 ou superior flashado (CLI 'touch sim X Y' disponível)
#   - Device online com WiFi configurada (HTTP 200 em /api/login_init)
#   - Admin password definida (variável F9_PASS abaixo)
#
# Uso: bash tools/manual_capture/capture_tft_screenshots.sh
# Output: docs/screenshots/tft_*.bmp

set -uo pipefail
cd "$(dirname "$0")/../.."

SIMUT_IP=${SIMUT_IP:-192.168.3.195}
SIMUT_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
F9_PASS="${F9_PASS:-F9Test@2026}"
PYBIN=./.venv/bin/python3
OUT_DIR=docs/screenshots
mkdir -p "$OUT_DIR"

log() { echo "[$(date +%H:%M:%S)] $*"; }

inject_touch() {
    local x=$1 y=$2
    timeout 5 $PYBIN -u <<PYEOF 2>/dev/null
import serial, time
s = serial.Serial('$SIMUT_PORT', 115200, timeout=2); s.dsrdtr=False; s.rtscts=False
time.sleep(0.3); s.reset_input_buffer()
s.write(b'touch sim $x $y\r\n')
time.sleep(0.8)
print(s.read(256).decode(errors='replace').strip())
s.close()
PYEOF
}

# Login + cookie
NONCE=$(curl -s --max-time 5 "http://$SIMUT_IP/api/login_init" | $PYBIN -c "import json,sys; print(json.load(sys.stdin)['nonce'])")
HASH=$(echo -n "$F9_PASS" | sha256sum | head -c 64)
curl -s -c /tmp/simut.cookie -X POST -d "user=admin&pass=$HASH&nonce=$NONCE" "http://$SIMUT_IP/api/login" >/dev/null

# Helper: captura screenshot atual + salva
capture() {
    local name=$1
    log "Capturing: $name"
    curl -s -b /tmp/simut.cookie -o "$OUT_DIR/tft_$name.bmp" "http://$SIMUT_IP/api/screenshot"
    if [ -s "$OUT_DIR/tft_$name.bmp" ]; then
        log "  saved $OUT_DIR/tft_$name.bmp ($(stat -c%s "$OUT_DIR/tft_$name.bmp") bytes)"
    else
        log "  ERROR: empty file"
    fi
}

# 1. Dashboard atual (sem injetar toque)
capture "dashboard"
sleep 1

# 2. Tap botão Settings (canto inferior esquerdo)
inject_touch 50 220
sleep 1.5
capture "settings_main"
sleep 1

# 3. Voltar pra dashboard via toque "voltar" (canto superior direito tipicamente)
inject_touch 280 20
sleep 1.5

# 4. Tap botão Graph (centro inferior)
inject_touch 160 220
sleep 1.5
capture "graph"
sleep 1

# 5. Voltar
inject_touch 280 20
sleep 1.5

# 6. Tap botão Alarms (canto inferior direito)
inject_touch 270 220
sleep 1.5
capture "alarms_action"
sleep 1

# 7. Voltar
inject_touch 280 20
sleep 1.5

# 8. Tap em slot 0 (panel ambient ou primeiro sensor)
inject_touch 80 80
sleep 1.5
capture "slot_detail"
sleep 1

log "Done. Screenshots em $OUT_DIR/"
ls -la "$OUT_DIR/"
