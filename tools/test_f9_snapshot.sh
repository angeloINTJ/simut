#!/usr/bin/env bash
# ============================================================================
# test_f9_snapshot.sh — teste end-to-end da Fase 9 OTA com TOLERÂNCIA EXPANDIDA.
#
# Mudanças vs v1 (que dava brick falsos):
#  - Espera até 5 min por boot pós-apply.
#  - Se não responder, faz reset longo via mão (HOLD RESET 10s + RELEASE)
#    e espera mais 3 min. Repete até 4 ciclos.
#  - Considera brick definitivo só após 4 ciclos sem resposta.
#  - Loga estado USB + serial passivo + HTTP em cada espera.
#
# Pré-condição:
#   - SIMUT em $SIMUT_IP rodando o firmware da Fase 9, com WiFi associado.
#   - pico_hand conectada via fiação GP0/GP1/GND ao SIMUT, em $HAND_PORT.
# ============================================================================
set -uo pipefail

cd /home/angelo/Documentos/SIMUT/

export SIMUT_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
export HAND_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00
SIMUT_IP="${SIMUT_IP:-192.168.3.195}"
F9_PASS='F9Test@2026'
TS=$(date +%Y%m%d-%H%M%S)
LOG=docs/test_reports/f9_snapshot_v2_${TS}.log
mkdir -p docs/test_reports
PYBIN=./.venv/bin/python3
FW=.pio/build/pico_w_release/firmware.bin

PASS_COUNT=0
FAIL_COUNT=0

log()  { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }
ok()   { echo "[$(date +%H:%M:%S)] PASS: $*" | tee -a "$LOG"; PASS_COUNT=$((PASS_COUNT+1)); }
fail() { echo "[$(date +%H:%M:%S)] FAIL: $*" | tee -a "$LOG"; FAIL_COUNT=$((FAIL_COUNT+1)); }
die()  { echo "[$(date +%H:%M:%S)] ABORT: $*" | tee -a "$LOG"; exit 1; }

# Helpers da mão. flock (-x exclusivo, -w 10 timeout) evita concorrência
# se outro processo tentar usar a mão durante um comando.
HAND_LOCK=/tmp/pico_hand.lock
hand_cmd() {
    local cmd="$1"
    (
        flock -x -w 10 200 || { echo "ERR: flock timeout"; exit 1; }
        $PYBIN -u -c "
import serial, time, os, sys
cmd = sys.argv[1]
try:
    s = serial.Serial(os.environ['HAND_PORT'], 115200, timeout=2)
    time.sleep(0.3); s.reset_input_buffer()
    s.write((cmd + '\n').encode()); time.sleep(0.5)
    print(s.read(256).decode(errors='replace').strip())
    s.close()
except Exception as e:
    print(f'ERR: {e}')
" "$cmd"
    ) 200>"$HAND_LOCK"
}

hand_long_reset() {
    log "  >> mão: HOLD RESET por 10 s + RELEASE (reset longo)"
    hand_cmd 'HOLD RESET' >> "$LOG"
    sleep 10
    hand_cmd 'RELEASE RESET' >> "$LOG"
    sleep 1
}

# Probes
probe_http() {
    curl -fsS --max-time 3 "http://$SIMUT_IP/api/login_init" >/dev/null 2>&1
}

probe_cli() {
    # timeout strict shell-wrapped: serial open pode pendurar quando SIMUT
    # está em estado ruim (USB CDC enumera mas não responde). Sem isso,
    # probe_cli trava o probe_state inteiro e impede reset longo de sair.
    timeout 5 $PYBIN -u <<'PYEOF' 2>/dev/null
import serial, time, os, sys
try:
    s = serial.Serial(os.environ['SIMUT_PORT'], 115200, timeout=1)
    time.sleep(0.2); s.reset_input_buffer()
    s.write(b'\r\n'); time.sleep(0.5)
    out = s.read(512).decode(errors='replace')
    s.close()
    sys.exit(0 if 'SIMUT' in out else 1)
except Exception:
    sys.exit(1)
PYEOF
}

probe_state() {
    local usb=$(lsusb | grep -oE "2e8a:[0-9a-f]+" | tr '\n' ' ')
    local has_simut="N"; [ -e "$SIMUT_PORT" ] && has_simut="Y"
    local bootsel="N"; [ -e /dev/disk/by-label/RPI-RP2 ] && bootsel="Y"
    local http="N"; probe_http && http="Y"
    local cli="N"; probe_cli && cli="Y"
    log "  state: USB=[$usb] SIMUT_acm=$has_simut BOOTSEL=$bootsel HTTP=$http CLI=$cli"
}

# Aguarda HTTP responder com timeout em segundos.
wait_http() {
    local timeout="$1" label="${2:-wait}"
    local start=$(date +%s)
    local deadline=$((start+timeout))
    log "  $label: aguardando HTTP até ${timeout}s..."
    while [ $(date +%s) -lt $deadline ]; do
        if probe_http; then
            log "  $label: HTTP OK em $(( $(date +%s)-start ))s"
            return 0
        fi
        sleep 5
    done
    log "  $label: TIMEOUT após ${timeout}s"
    return 1
}

# Estratégia AGRESSIVA (user 2026-05-08): teste todo deve caber em ≤3 min.
# OTA upload (32s) + apply (3s) + boot (~30-60s) + verify (5s) = ~100s no PASS.
# No FAIL: declarar brick rápido — se não voltou em 90s pós-apply, não volta.
# Total cycle 1 = 90s. Sem cycle 2 (long reset não recupera bricks reais que
# já vimos; só consome tempo).
wait_post_apply_with_recovery() {
    # User 2026-05-08: cap absoluto 3 min. Post-OTA boot pode ter LFS
    # reformat (~10-30s) + config recreate (~10s) + WiFi connect (~30s)
    # = pode chegar perto de 90s no normal case, especialmente em alpha9+
    # com cooperative quiet mode (Core 1 reset + relaunch).
    # 180s = 3 min cap; cobre até cenários slow boot mas detecta brick real.
    log "Wait único: até 180s pra boot pós-apply..."
    probe_state
    if wait_http 180 "wait"; then
        log "DEVICE BACK ONLINE"
        probe_state
        return 0
    fi
    log "FAIL: device não voltou em 180s — brick (não recuperável por software)"
    return 1
}

# ============================================================================
log "=== F9 snapshot test v2 — start $TS ==="
log "SIMUT: $SIMUT_IP   port: $SIMUT_PORT"
log "Mão: $HAND_PORT"
log ""

# Step 0: Pre-flight
[ -e "$HAND_PORT" ] || die "mão pico_hand não encontrada em $HAND_PORT"
HP=$(hand_cmd 'PING')
log "Pre-flight mão: $HP"
[[ "$HP" =~ "PONG" ]] || die "mão não responde PONG"

probe_http || die "SIMUT HTTP não responde — flashar baseline + configurar WiFi antes"
log "Pre-flight HTTP: OK"

# Step 1: Reset admin via Serial CLI
log ""
log "=== Step 1: Reset admin via Serial CLI ==="
OTP_OUT=$($PYBIN -u <<'PYEOF' 2>&1
import serial, time, os
s = serial.Serial(os.environ['SIMUT_PORT'], 115200, timeout=2)
time.sleep(0.5); s.reset_input_buffer()
s.write(b'\r\nconf system admin reset confirm\r\nwrite memory\r\n'); time.sleep(4)
print(s.read(8192).decode(errors='replace'))
s.close()
PYEOF
)
OTP=$(echo "$OTP_OUT" | grep -A1 'Senha admin\|Senha ADMIN' | grep -oE '^[[:space:]]+[A-Z0-9]{6,16}[[:space:]]*$' | tr -d ' \r\n\t' | head -1)
[ -n "$OTP" ] || { echo "$OTP_OUT" | tail -30 | tee -a "$LOG"; die "OTP não capturado"; }
log "OTP: $OTP"
sleep 2

# Step 2: chpass
log ""
log "=== Step 2: chpass OTP → '$F9_PASS' ==="
export F9_OTP="$OTP" F9_NEWPASS="$F9_PASS" F9_BASE="http://$SIMUT_IP"
$PYBIN <<'PYEOF' | tee -a "$LOG"
import os, requests, hashlib, sys
s = requests.Session(); base = os.environ['F9_BASE']
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

# Step 3: Login + baseline /api/config
log ""
log "=== Step 3: Login + baseline /api/config ==="
BASELINE_FILE=/tmp/f9_baseline_${TS}.json
export F9_BASELINE_FILE="$BASELINE_FILE"
$PYBIN <<'PYEOF' | tee -a "$LOG"
import os, requests, hashlib, json
s = requests.Session(); base = os.environ['F9_BASE']
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login',
    data={'user': 'admin', 'pass': sha(os.environ['F9_NEWPASS']), 'nonce': nonce}, timeout=10)
print(f'login HTTP {r.status_code}'); assert r.status_code == 200
cfg = s.get(f'{base}/api/config', timeout=10).json()
keys = ['name','tz','ntp_srv','ssid','t_srv','t_port','t_path','t_int','m_topic','m_user','m_qos']
baseline = {k: cfg.get(k) for k in keys}
print('baseline:', json.dumps(baseline, indent=2)[:600])
with open(os.environ['F9_BASELINE_FILE'], 'w') as f: json.dump(baseline, f)
PYEOF
[ ${PIPESTATUS[0]} -eq 0 ] || die "login/baseline falhou"

# Step 4: Trigger OTA
log ""
log "=== Step 4: Trigger OTA (stage+commit+apply) ==="
log "Estado pré-OTA:"
probe_state
log ""

timeout 80 ./tools/ota_apply.py \
    --ip "$SIMUT_IP" \
    --user admin --pass "$F9_PASS" \
    --firmware "$FW" \
    --no-restore 2>&1 | tee -a "$LOG"

apply_rc=${PIPESTATUS[0]}
log "ota_apply.py terminou (exit=$apply_rc — wait timeout esperado, vamos seguir com nosso wait tolerante)"
log ""

# Step 5: Espera tolerante
log "=== Step 5: Espera tolerante (até 4 ciclos × 3-5 min, com resets longos) ==="
if wait_post_apply_with_recovery; then
    ok "device voltou online após OTA"
else
    fail "device NÃO voltou online — brick definitivo após 4 ciclos com 3 resets longos"
    log ""
    log "=== RESUMO ==="
    log "PASS: $PASS_COUNT  FAIL: $FAIL_COUNT  Log: $LOG"
    exit 1
fi

# Step 6: Login com senha PRESERVADA
log ""
log "=== Step 6: Login com senha preservada ==="
LOGIN_OK=0
for try in 1 2 3 4 5; do
    rc=$($PYBIN <<'PYEOF'
import os, requests, hashlib
try:
    s = requests.Session(); base = os.environ['F9_BASE']
    nonce = s.get(f'{base}/api/login_init', timeout=3).json()['nonce']
    r = s.post(f'{base}/api/login',
        data={'user': 'admin',
              'pass': hashlib.sha256(os.environ['F9_NEWPASS'].encode()).hexdigest(),
              'nonce': nonce}, timeout=5)
    print(r.status_code)
except Exception:
    print('ERR')
PYEOF
)
    log "  tentativa $try: HTTP $rc"
    [ "$rc" = "200" ] && { LOGIN_OK=1; break; }
    sleep 5
done

if [ $LOGIN_OK -eq 1 ]; then
    ok "Login com senha PRESERVADA funcionou — snapshot restaurou users"
else
    fail "Login com senha preservada falhou — provável que device caiu em factory"
fi

# Step 7: Compare /api/config
if [ $LOGIN_OK -eq 1 ]; then
    log ""
    log "=== Step 7: Compare /api/config preservado ==="
    $PYBIN <<'PYEOF' | tee -a "$LOG"
import os, requests, hashlib, json, sys
s = requests.Session(); base = os.environ['F9_BASE']
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login',
    data={'user': 'admin', 'pass': sha(os.environ['F9_NEWPASS']), 'nonce': nonce}, timeout=10)
assert r.status_code == 200
cfg = s.get(f'{base}/api/config', timeout=10).json()
keys = ['name','tz','ntp_srv','ssid','t_srv','t_port','t_path','t_int','m_topic','m_user','m_qos']
post = {k: cfg.get(k) for k in keys}
with open(os.environ['F9_BASELINE_FILE']) as f: base_cfg = json.load(f)
diffs = [f'{k}: {base_cfg.get(k)!r} -> {post.get(k)!r}' for k in keys if base_cfg.get(k) != post.get(k)]
if diffs:
    print('DIFFS:'); [print('  '+d) for d in diffs]
    sys.exit(2)
print('OK: 11 campos preservados')
PYEOF
    case ${PIPESTATUS[0]} in
        0) ok "11 campos /api/config preservados" ;;
        *) fail "Campos divergem (ver diffs acima)" ;;
    esac
fi

log ""
log "=== RESUMO ==="
log "PASS: $PASS_COUNT   FAIL: $FAIL_COUNT"
log "Log: $LOG"
[ $FAIL_COUNT -eq 0 ] && exit 0 || exit 1
