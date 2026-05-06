#!/usr/bin/env bash
# ============================================================================
# test_f9_snapshot.sh — teste end-to-end da Fase 9 OTA (config snapshot).
#
# Objetivo: verificar que após um OTA apply, `system.bin` é restaurado a
# partir do snapshot na metadata partition — admin/users/WiFi/templates
# preservados sem precisar restore manual de .bkp.
#
# Pré-condição: device em $DEVICE_IP rodando v3.43.14 (ou superior) com
# WiFi associado, e USB serial conectado em $PORT.
# ============================================================================
set -uo pipefail

cd /home/angelo/Documentos/SIMUT/

PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
DEVICE_IP="${DEVICE_IP:-192.168.3.195}"
F9_PASS='F9Test@2026'
TS=$(date +%Y%m%d-%H%M%S)
LOG=docs/test_reports/f9_snapshot_${TS}.log
mkdir -p docs/test_reports
PYBIN=./.venv/bin/python3
FW=.pio/build/pico_w_release/firmware.bin
SERIAL_CAP=/tmp/f9_serial_${TS}.log

PASS_COUNT=0
FAIL_COUNT=0

log() { echo "[$(date +%T)] $*" | tee -a "$LOG"; }
ok()  { echo "[$(date +%T)] PASS: $*" | tee -a "$LOG"; PASS_COUNT=$((PASS_COUNT+1)); }
fail(){ echo "[$(date +%T)] FAIL: $*" | tee -a "$LOG"; FAIL_COUNT=$((FAIL_COUNT+1)); }
die() { echo "[$(date +%T)] ABORT: $*" | tee -a "$LOG"; cleanup; exit 1; }

cleanup() {
    if [ -n "${SERIAL_PID:-}" ]; then
        kill -TERM "$SERIAL_PID" 2>/dev/null || true
        wait "$SERIAL_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ----------------------------------------------------------------------------
# Step 0: Sanity — device alive
# ----------------------------------------------------------------------------
log "=== F9 snapshot test — start $TS ==="
log "Device IP: $DEVICE_IP   Port: $PORT   Firmware: $FW"

if ! curl -fsS --max-time 5 "http://$DEVICE_IP/api/login_init" >/dev/null 2>&1; then
    die "Device não respondeu /api/login_init — está vivo em $DEVICE_IP?"
fi
log "Pre-flight: /api/login_init OK"

# ----------------------------------------------------------------------------
# Step 1: Reset admin via Serial → captura OTP
# ----------------------------------------------------------------------------
log "=== Step 1: Reset admin via Serial CLI ==="

OTP_OUT=$($PYBIN -c "
import serial, time, re
s = serial.Serial('$PORT', 115200, timeout=2)
time.sleep(1); s.reset_input_buffer()
s.write(b'\r\nconf system admin reset confirm\r\nwrite memory\r\n'); time.sleep(4)
out = s.read(8192).decode(errors='replace')
s.close()
print(out)
" 2>&1)

OTP=$(echo "$OTP_OUT" | grep -A1 'Senha admin\|Senha ADMIN' | grep -oE '^[[:space:]]+[A-Z0-9]{6,16}[[:space:]]*$' | tr -d ' \r\n\t' | head -1)

if [ -z "$OTP" ]; then
    log "OTP capture output:"
    echo "$OTP_OUT" | tail -30 | tee -a "$LOG"
    die "Não consegui capturar OTP do Serial após admin reset"
fi
log "OTP capturado: [$OTP] (len=${#OTP})"
sleep 2

# ----------------------------------------------------------------------------
# Step 2: chpass via web → senha conhecida
# ----------------------------------------------------------------------------
log "=== Step 2: chpass OTP → '$F9_PASS' ==="

export F9_OTP="$OTP" F9_NEWPASS="$F9_PASS" F9_BASE="http://$DEVICE_IP"
$PYBIN <<'PYEOF' | tee -a "$LOG"
import os, requests, hashlib, sys
s = requests.Session()
base = os.environ['F9_BASE']
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login_chpass',
    data={'user': 'admin', 'oldpass': sha(os.environ['F9_OTP']),
          'newpass': sha(os.environ['F9_NEWPASS']), 'nonce': nonce}, timeout=10)
print(f'chpass HTTP {r.status_code}: {r.text[:200]}')
sys.exit(0 if r.status_code == 200 else 1)
PYEOF
[ ${PIPESTATUS[0]} -eq 0 ] || die "chpass falhou"
sleep 1

# ----------------------------------------------------------------------------
# Step 3: Login + baseline /api/config
# ----------------------------------------------------------------------------
log "=== Step 3: Login + baseline /api/config ==="

BASELINE_FILE=/tmp/f9_baseline_${TS}.json
export F9_BASELINE_FILE="$BASELINE_FILE"
$PYBIN <<'PYEOF' | tee -a "$LOG"
import os, requests, hashlib, json
s = requests.Session()
base = os.environ['F9_BASE']
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login',
    data={'user': 'admin', 'pass': sha(os.environ['F9_NEWPASS']), 'nonce': nonce}, timeout=10)
print(f'login HTTP {r.status_code}')
assert r.status_code == 200, r.text
cfg = s.get(f'{base}/api/config', timeout=10).json()
keys = ['name','tz','ntp_srv','ssid','t_srv','t_port','t_path','t_int','m_topic','m_user','m_qos']
baseline = {k: cfg.get(k) for k in keys}
print('baseline:', json.dumps(baseline, indent=2)[:600])
with open(os.environ['F9_BASELINE_FILE'], 'w') as f: json.dump(baseline, f)
PYEOF
[ ${PIPESTATUS[0]} -eq 0 ] || die "login/baseline falhou"

# ----------------------------------------------------------------------------
# Step 4: Inicia captura Serial em background
# ----------------------------------------------------------------------------
log "=== Step 4: Iniciando captura Serial → $SERIAL_CAP ==="
$PYBIN -u -c "
import serial, time, sys
s = serial.Serial('$PORT', 115200, timeout=0.5)
deadline = time.time() + 240
while time.time() < deadline:
    try:
        chunk = s.read(2048)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.flush()
    except Exception as e:
        time.sleep(0.5)
" > "$SERIAL_CAP" 2>&1 &
SERIAL_PID=$!
log "Serial capture PID: $SERIAL_PID"
sleep 1

# ----------------------------------------------------------------------------
# Step 5: Trigger OTA apply
# ----------------------------------------------------------------------------
log "=== Step 5: Trigger OTA stage+commit+apply ==="

./tools/ota_apply.py \
    --ip "$DEVICE_IP" \
    --user admin --pass "$F9_PASS" \
    --firmware "$FW" \
    --no-restore 2>&1 | tee -a "$LOG"

apply_rc=${PIPESTATUS[0]}
log "ota_apply.py exit code: $apply_rc"

# ----------------------------------------------------------------------------
# Step 6: Aguarda device voltar e verifica login com a MESMA senha
# ----------------------------------------------------------------------------
log "=== Step 6: Aguardando reboot + login com senha PRESERVADA ==="
sleep 60

login_attempts=0
login_ok=0
while [ $login_attempts -lt 30 ]; do
    login_attempts=$((login_attempts+1))
    rc=$($PYBIN -c "
import os, requests, hashlib
try:
    s = requests.Session(); base = os.environ['F9_BASE']
    nonce = s.get(f'{base}/api/login_init', timeout=3).json()['nonce']
    r = s.post(f'{base}/api/login',
        data={'user': 'admin',
              'pass': hashlib.sha256(os.environ['F9_NEWPASS'].encode()).hexdigest(),
              'nonce': nonce}, timeout=5)
    print(r.status_code)
except Exception as e:
    print('ERR')
" 2>&1)
    if [ "$rc" = "200" ]; then
        login_ok=1
        break
    fi
    log "  tentativa $login_attempts: rc=$rc"
    sleep 5
done

if [ $login_ok -eq 1 ]; then
    ok "Login pós-OTA com senha PRESERVADA F9_PASS funcionou (snapshot restaurou users)"
else
    fail "Login pós-OTA com senha preservada FALHOU em 30 tentativas — snapshot não restaurou users"
fi

# ----------------------------------------------------------------------------
# Step 7: Compara /api/config pós-apply com baseline
# ----------------------------------------------------------------------------
if [ $login_ok -eq 1 ]; then
    log "=== Step 7: Compare /api/config pós-apply ==="
    $PYBIN <<'PYEOF' | tee -a "$LOG"
import os, requests, hashlib, json, sys
s = requests.Session(); base = os.environ['F9_BASE']
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login',
    data={'user': 'admin', 'pass': sha(os.environ['F9_NEWPASS']), 'nonce': nonce}, timeout=10)
assert r.status_code == 200, r.text
cfg = s.get(f'{base}/api/config', timeout=10).json()
keys = ['name','tz','ntp_srv','ssid','t_srv','t_port','t_path','t_int','m_topic','m_user','m_qos']
post = {k: cfg.get(k) for k in keys}
with open(os.environ['F9_BASELINE_FILE']) as f: base_cfg = json.load(f)
diffs = []
for k in keys:
    if base_cfg.get(k) != post.get(k):
        diffs.append(f'{k}: {base_cfg.get(k)!r} -> {post.get(k)!r}')
if diffs:
    print('DIFFS DETECTADAS:')
    for d in diffs: print('  ', d)
    sys.exit(2)
else:
    print('OK: todos os 11 campos criticos preservados')
    sys.exit(0)
PYEOF
    case ${PIPESTATUS[0]} in
        0) ok "Todos os 11 campos críticos de /api/config preservados pós-OTA" ;;
        *) fail "Campos do /api/config divergem do baseline (ver acima)" ;;
    esac
fi

# ----------------------------------------------------------------------------
# Step 8: Verifica log Serial pós-apply
# ----------------------------------------------------------------------------
log "=== Step 8: Verifica log Serial pós-apply ==="
sleep 3
kill -TERM "$SERIAL_PID" 2>/dev/null || true
wait "$SERIAL_PID" 2>/dev/null || true
SERIAL_PID=""

if [ -s "$SERIAL_CAP" ]; then
    log "Serial capture: $(wc -c < "$SERIAL_CAP") bytes"
    if grep -q "OTA post-apply detected" "$SERIAL_CAP"; then
        ok "Boot log contém '[BOOT] OTA post-apply detected'"
    else
        fail "Boot log NÃO contém '[BOOT] OTA post-apply detected'"
    fi
    if grep -q "snapshot=present" "$SERIAL_CAP"; then
        ok "Boot log indica 'snapshot=present' — restore foi acionado"
    else
        fail "Boot log NÃO indica 'snapshot=present'"
    fi
    if grep -q "Senha ADMIN inicial" "$SERIAL_CAP"; then
        fail "Boot regenerou senha admin (factory mode) — snapshot NÃO funcionou"
    else
        ok "Boot NÃO regenerou senha admin (factory mode evitado)"
    fi
    log "Trecho do boot pós-apply:"
    grep -E "OTA post-apply|snapshot|Senha ADMIN|IP obtido|AP detect" "$SERIAL_CAP" | tee -a "$LOG"
else
    fail "Serial capture vazio — não foi possível verificar boot log"
fi

# ----------------------------------------------------------------------------
# Resumo
# ----------------------------------------------------------------------------
log "=== RESUMO ==="
log "PASS: $PASS_COUNT   FAIL: $FAIL_COUNT"
log "Log: $LOG"
log "Baseline: $BASELINE_FILE"
log "Serial capture: $SERIAL_CAP"
[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
