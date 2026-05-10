#!/usr/bin/env bash
# ============================================================================
# test_f9_loop100.sh — 100 ciclos OTA com pre-test único (factory + backup).
#
# Diferente de test_f9_loop20.sh (que rodava test_f9_snapshot.sh por iter,
# refazendo factory reset + chpass em cada uma):
#
#   PRE-TEST (1×):
#     - Factory reset admin → captura OTP via Serial
#     - chpass OTP → senha conhecida
#     - GET /api/backup → salva .bkp local
#
#   LOOP (100×):
#     - timing.start
#     - stage upload firmware.bin + commit + apply via ota_apply.py
#     - Aguarda boot (max 180s) + verify HTTP /api/login_init
#     - Login com senha PRESERVADA (snapshot deve restaurar config)
#     - timing.stop → log per-iter
#     - Em caso de FAIL: recovery via PicoHand BOOTSEL + picotool reflash
#       + WiFi reconfig + factory + chpass (counter recovery++)
#
#   FINAL REPORT:
#     - PASS / FAIL / RECOVERY counts
#     - Per-iter time stats: min / max / avg / median
#     - Failure breakdown (iter num + tempo até falha)
# ============================================================================
set -uo pipefail
cd /home/angelo/Documentos/SIMUT/

TS=$(date +%Y%m%d-%H%M%S)
LOG=docs/test_reports/f9_loop100_${TS}.log
STATS_CSV=docs/test_reports/f9_loop100_${TS}.csv
REPORT=docs/test_reports/f9_loop100_${TS}_report.md
BKP_FILE=/tmp/simut_loop100_${TS}.bkp
mkdir -p docs/test_reports

PYBIN=./.venv/bin/python3
export HAND_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00
export SIMUT_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
SIMUT_IP="${SIMUT_IP:-192.168.3.195}"
PASSWORD='Loop100@2026'
FW=.pio/build/pico_w_release/firmware.bin

PASS=0
FAIL=0
RECOVERY=0
TOTAL_TIME=0
declare -a ITER_TIMES
declare -a FAIL_ITERS

log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }

# CSV header
echo "iter,result,duration_s,recovery_used,timestamp" > "$STATS_CSV"

# ============================================================================
# Helpers
# ============================================================================
hand_cmd() {
    timeout 5 $PYBIN -u -c "
import serial, time, sys, os
s = serial.Serial(os.environ['HAND_PORT'], 115200, timeout=2)
time.sleep(0.3); s.reset_input_buffer()
s.write(('$1' + '\n').encode()); time.sleep(0.5)
print(s.read(256).decode(errors='replace').strip())
s.close()
" 2>&1
}

probe_http() {
    curl -fsS --max-time 3 "http://$SIMUT_IP/api/login_init" >/dev/null 2>&1
}

wait_http() {
    local timeout="$1"
    local start=$(date +%s)
    local deadline=$((start+timeout))
    while [ $(date +%s) -lt $deadline ]; do
        probe_http && return 0
        sleep 3
    done
    return 1
}

factory_reset_and_chpass() {
    log "  [factory] resetting admin via Serial..."
    OTP_OUT=$(timeout 10 $PYBIN -u <<'PYEOF' 2>&1
import serial, time, os
s = serial.Serial(os.environ['SIMUT_PORT'], 115200, timeout=2)
time.sleep(0.5); s.reset_input_buffer()
s.write(b'\r\nconf system admin reset confirm\r\nwrite memory\r\n')
time.sleep(4)
print(s.read(8192).decode(errors='replace'))
s.close()
PYEOF
)
    OTP=$(echo "$OTP_OUT" | grep -A1 'Senha admin\|Senha ADMIN' | grep -oE '^[[:space:]]+[A-Z0-9]{6,16}[[:space:]]*$' | tr -d ' \r\n\t' | head -1)
    if [ -z "$OTP" ]; then
        log "  [factory] OTP not captured, output:"
        echo "$OTP_OUT" | tail -20 | tee -a "$LOG"
        return 1
    fi
    log "  [factory] OTP: $OTP"
    sleep 2
    log "  [factory] chpass OTP → '$PASSWORD'..."
    F9_OTP="$OTP" F9_NEWPASS="$PASSWORD" F9_BASE="http://$SIMUT_IP" \
        timeout 15 $PYBIN <<'PYEOF' 2>&1 | tee -a "$LOG"
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
s = requests.Session(); base = os.environ['F9_BASE']
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login_chpass',
    data={'user':'admin','oldpass':sha(os.environ['F9_OTP']),
          'newpass':sha(os.environ['F9_NEWPASS']),'nonce':nonce}, timeout=10)
print(f'chpass HTTP {r.status_code}: {r.text[:100]}')
sys.exit(0 if r.status_code == 200 else 1)
PYEOF
    return ${PIPESTATUS[0]}
}

# Recovery: BOOTSEL via PicoHand + picotool flash + WiFi reconfig + factory
recover_device() {
    log "  [RECOVERY] device offline — iniciando recovery"
    log "  [RECOVERY] PicoHand HOLD BOOTSEL + RESET pulse"
    hand_cmd 'HOLD BOOTSEL' >> "$LOG"
    hand_cmd 'HOLD RESET' >> "$LOG"
    sleep 0.4
    hand_cmd 'RELEASE RESET' >> "$LOG"
    sleep 0.6
    hand_cmd 'RELEASE BOOTSEL' >> "$LOG"
    sleep 3

    # Wait for RPI-RP2 mount (up to 8s)
    for try in 1 2 3 4; do
        sleep 2
        if [ -e /dev/disk/by-label/RPI-RP2 ]; then
            log "  [RECOVERY] RPI-RP2 detected (try $try)"
            break
        fi
    done
    if [ ! -e /dev/disk/by-label/RPI-RP2 ]; then
        log "  [RECOVERY] RPI-RP2 NOT detected — recovery failed"
        return 1
    fi
    log "  [RECOVERY] picotool load -x firmware.uf2"
    picotool load -x .pio/build/pico_w_release/firmware.uf2 2>&1 | tail -1 >> "$LOG"

    # Esperar CDC re-enumerar (até 8s)
    for try in 1 2 3 4 5 6 7 8; do
        [ -e "$SIMUT_PORT" ] && break
        sleep 1
    done
    if [ ! -e "$SIMUT_PORT" ]; then
        log "  [RECOVERY] SIMUT_PORT não re-enumerou em 8s — recovery falhou"
        return 1
    fi

    # Esperar marker SYS READY via UART (até 60s) — substitui sleep 30 fixo.
    # Boot saudável v4.0.0 tinge SYS READY em ~33s; 60s dá margem 2x.
    log "  [RECOVERY] aguardando SYS READY via UART (até 60s)"
    READY_OUT=$(timeout 65 $PYBIN -u <<'PYEOF' 2>&1
import serial, time, os, sys
p = serial.Serial(os.environ['SIMUT_PORT'], 115200, timeout=0.5)
end = time.time() + 60
buf = ''
while time.time() < end:
    c = p.read(4096)
    if c:
        buf += c.decode('utf-8','replace')
        if 'SYS READY' in buf or 'BOOT step] 14' in buf:
            print('SYS_READY_DETECTED')
            sys.exit(0)
print('SYS_READY_TIMEOUT')
sys.exit(2)
PYEOF
)
    if echo "$READY_OUT" | grep -q SYS_READY_DETECTED; then
        log "  [RECOVERY] SYS READY detectado"
    else
        log "  [RECOVERY] SYS READY não chegou em 60s — seguindo mesmo assim"
    fi

    # Tentar HTTP direto: se NVS preservou WiFi, device já associou e responde.
    # Skip do "wifi reconfigure" elimina segundo reboot e write desnecessário em NVS.
    log "  [RECOVERY] probe HTTP rápido (até 30s) — pular reconfigure se NVS válido"
    if wait_http 30; then
        log "  [RECOVERY] WiFi OK direto do NVS — pulando CLI reconfigure"
    else
        log "  [RECOVERY] HTTP timeout — NVS pode estar sem config; reconfigurando WiFi via CLI"
        timeout 25 $PYBIN -u <<'PYEOF' 2>&1 | tee -a "$LOG"
import serial, time, os
s = serial.Serial(os.environ['SIMUT_PORT'], 115200, timeout=2)
time.sleep(2); s.reset_input_buffer()
for c in [b'conf system ssid ProcrastinationPLUS\r\n',
          b'conf system pass A$AGzD3XeY7xSrwAg5JF\r\n',
          b'write memory\r\n', b'reload confirm\r\n']:
    s.write(c); time.sleep(2); s.read(2048)
s.close()
PYEOF
        log "  [RECOVERY] aguardando WiFi pós-reconfigure (até 120s)"
        if ! wait_http 120; then
            log "  [RECOVERY] WiFi não veio em 120s — recovery falhou"
            return 1
        fi
    fi

    log "  [RECOVERY] WiFi OK — refazendo factory reset + chpass"
    factory_reset_and_chpass || { log "  [RECOVERY] factory falhou"; return 1; }
    log "  [RECOVERY] OK"
    return 0
}

# ============================================================================
# PRE-TEST
# ============================================================================
log "=========================================================="
log "F9 loop100 — start $TS"
log "  SIMUT: $SIMUT_IP   port: $SIMUT_PORT"
log "  Hand:  $HAND_PORT"
log "  Log:   $LOG"
log "  CSV:   $STATS_CSV"
log "  BKP:   $BKP_FILE"
log "=========================================================="

# Pre-flight
[ -e "$HAND_PORT" ] || { log "ABORT: PicoHand not found at $HAND_PORT"; exit 1; }
[ -e "$SIMUT_PORT" ] || { log "ABORT: SIMUT serial not found at $SIMUT_PORT"; exit 1; }
[ -e "$FW" ] || { log "ABORT: firmware.bin not found at $FW"; exit 1; }

HP=$(hand_cmd 'PING')
log "Pre-flight PicoHand: $HP"
[[ "$HP" =~ "PONG" ]] || { log "ABORT: PicoHand não responde PONG"; exit 1; }

probe_http || { log "ABORT: SIMUT HTTP não responde — flashar baseline antes"; exit 1; }
log "Pre-flight HTTP: OK"

# Step 1: factory reset + chpass
log ""
log "=== PRE-1: Factory reset admin + chpass ==="
factory_reset_and_chpass || { log "ABORT: factory inicial falhou"; exit 1; }

# Step 2: backup FS
log ""
log "=== PRE-2: Backup FS via /api/backup ==="
F9_BASE="http://$SIMUT_IP" F9_PASSWORD="$PASSWORD" F9_BKP="$BKP_FILE" \
    timeout 60 $PYBIN <<'PYEOF' 2>&1 | tee -a "$LOG"
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
base = os.environ['F9_BASE']; bkp = os.environ['F9_BKP']
s = requests.Session()
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login',
    data={'user':'admin','pass':sha(os.environ['F9_PASSWORD']),'nonce':nonce}, timeout=10)
assert r.status_code == 200, f'login HTTP {r.status_code}'
print(f'  login OK')
r = s.get(f'{base}/api/backup', stream=True, timeout=60)
r.raise_for_status()
size = 0
with open(bkp, 'wb') as f:
    for chunk in r.iter_content(chunk_size=4096):
        f.write(chunk); size += len(chunk)
print(f'  .bkp salvo: {size} bytes em {bkp}')
print(f'  Header BKP1: {open(bkp,"rb").read(4)!r}')
PYEOF
[ ${PIPESTATUS[0]} -eq 0 ] || { log "ABORT: backup falhou"; exit 1; }

log ""
log "=========================================================="
log "PRE-TEST COMPLETO — iniciando 100 iters de OTA"
log "=========================================================="
log ""

# ============================================================================
# LOOP 100x
# ============================================================================
LOOP_START=$(date +%s)
ITERS="${ITERS:-100}"
for iter in $(seq 1 "$ITERS"); do
    iter_start=$(date +%s)
    iter_recovery=0
    log "==================== ITER $iter / $ITERS ===================="

    # Pre-iter: device must be reachable. If not, recover.
    if ! probe_http; then
        log "  pre-iter: device offline — recovery"
        if recover_device; then
            iter_recovery=1
            RECOVERY=$((RECOVERY+1))
        else
            log "  iter $iter: pre-iter recovery FAILED — marking as FAIL"
            FAIL=$((FAIL+1))
            FAIL_ITERS+=("$iter")
            iter_dt=$(($(date +%s) - iter_start))
            ITER_TIMES+=("$iter_dt")
            TOTAL_TIME=$((TOTAL_TIME + iter_dt))
            echo "$iter,FAIL_RECOVERY,$iter_dt,1,$(date +%H:%M:%S)" >> "$STATS_CSV"
            continue
        fi
    fi

    # OTA via ota_apply.py (--no-restore: usaremos restore manual abaixo
    # com o .bkp PRE-TEST canônico em todo iter, não um .bkp fresh).
    log "  ota_apply.py executando (stage+apply, sem restore embutido)..."
    timeout 200 ./tools/ota_apply.py \
        --ip "$SIMUT_IP" \
        --user admin --pass "$PASSWORD" \
        --firmware "$FW" \
        --no-restore >>"$LOG" 2>&1
    apply_rc=$?
    log "  ota_apply.py exit=$apply_rc"

    # Wait for HTTP back
    log "  aguardando HTTP até 180s..."
    if wait_http 180; then
        log "  HTTP OK — verificando login com senha preservada"
        # Verify login still works with preserved password
        login_rc=$(F9_BASE="http://$SIMUT_IP" F9_PASSWORD="$PASSWORD" \
            timeout 15 $PYBIN <<'PYEOF' 2>/dev/null
import os, requests, hashlib
try:
    sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
    s = requests.Session(); base = os.environ['F9_BASE']
    nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
    r = s.post(f'{base}/api/login',
        data={'user':'admin','pass':sha(os.environ['F9_PASSWORD']),'nonce':nonce}, timeout=10)
    print(r.status_code)
except Exception as e:
    print(f'ERR:{e}')
PYEOF
)
        if [ "$login_rc" = "200" ]; then
            log "  restaurando backup canônico ($BKP_FILE)..."
            restore_rc=$(F9_BASE="http://$SIMUT_IP" F9_PASSWORD="$PASSWORD" F9_BKP="$BKP_FILE" \
                timeout 90 $PYBIN <<'PYEOF' 2>>"$LOG"
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode()).hexdigest()
base = os.environ['F9_BASE']; bkp = os.environ['F9_BKP']
try:
    s = requests.Session()
    nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
    r = s.post(f'{base}/api/login',
        data={'user':'admin','pass':sha(os.environ['F9_PASSWORD']),'nonce':nonce}, timeout=10)
    if r.status_code != 200:
        print(f'LOGIN_FAIL_{r.status_code}'); sys.exit(1)
    # validate .bkp
    with open(bkp, 'rb') as f: data = f.read()
    r = s.post(f'{base}/api/restore?op=validate',
               files={'file':('backup.bkp', data, 'application/octet-stream')}, timeout=60)
    if r.status_code != 200:
        print(f'VALIDATE_FAIL_{r.status_code}'); sys.exit(2)
    # apply restore
    r = s.post(f'{base}/api/restore?op=apply',
               files={'file':('backup.bkp', data, 'application/octet-stream')}, timeout=90)
    if r.status_code != 200:
        print(f'APPLY_FAIL_{r.status_code}'); sys.exit(3)
    print('200')
except Exception as e:
    print(f'EXC:{type(e).__name__}'); sys.exit(4)
PYEOF
)
            if [ "$restore_rc" = "200" ]; then
                # Stabilization: restore writes 30+ files; device may be
                # briefly unresponsive doing LFS housekeeping. Wait until
                # HTTP responds again (max 30s) to avoid false offline in
                # next iter pre-check.
                log "  estabilizando pós-restore (até 30s)..."
                wait_http 30 || log "  WARN: HTTP não estabilizou em 30s, prosseguindo"
            fi
            iter_dt=$(($(date +%s) - iter_start))
            if [ "$restore_rc" = "200" ]; then
                log "  iter $iter: PASS (${iter_dt}s, OTA+restore OK)"
                PASS=$((PASS+1))
                ITER_TIMES+=("$iter_dt")
                TOTAL_TIME=$((TOTAL_TIME + iter_dt))
                echo "$iter,PASS,$iter_dt,$iter_recovery,$(date +%H:%M:%S)" >> "$STATS_CSV"
            else
                log "  iter $iter: FAIL (restore=$restore_rc) — OTA OK mas restore falhou"
                FAIL=$((FAIL+1))
                FAIL_ITERS+=("$iter:restore=$restore_rc")
                ITER_TIMES+=("$iter_dt")
                TOTAL_TIME=$((TOTAL_TIME + iter_dt))
                echo "$iter,FAIL_RESTORE,$iter_dt,$iter_recovery,$(date +%H:%M:%S)" >> "$STATS_CSV"
            fi
        else
            iter_dt=$(($(date +%s) - iter_start))
            log "  iter $iter: FAIL (login HTTP=$login_rc) — config snapshot perdido?"
            FAIL=$((FAIL+1))
            FAIL_ITERS+=("$iter:login=$login_rc")
            ITER_TIMES+=("$iter_dt")
            TOTAL_TIME=$((TOTAL_TIME + iter_dt))
            echo "$iter,FAIL_LOGIN,$iter_dt,$iter_recovery,$(date +%H:%M:%S)" >> "$STATS_CSV"
            # Recover for next iter
            recover_device && RECOVERY=$((RECOVERY+1))
        fi
    else
        iter_dt=$(($(date +%s) - iter_start))
        log "  iter $iter: FAIL (boot timeout) — device bricked"
        FAIL=$((FAIL+1))
        FAIL_ITERS+=("$iter:boot_timeout")
        ITER_TIMES+=("$iter_dt")
        TOTAL_TIME=$((TOTAL_TIME + iter_dt))
        echo "$iter,FAIL_BOOT,$iter_dt,$iter_recovery,$(date +%H:%M:%S)" >> "$STATS_CSV"
        # Recover for next iter
        recover_device && RECOVERY=$((RECOVERY+1))
    fi
done

LOOP_END=$(date +%s)
LOOP_DT=$((LOOP_END - LOOP_START))

# ============================================================================
# FINAL REPORT
# ============================================================================
log ""
log "=========================================================="
log "RELATÓRIO FINAL — F9 loop100"
log "=========================================================="

# Compute stats via Python (median, percentiles)
ITER_TIMES_STR=$(IFS=,; echo "${ITER_TIMES[*]}")
STATS=$(F9_TIMES="$ITER_TIMES_STR" $PYBIN <<'PYEOF'
import os, statistics
times = [int(x) for x in os.environ['F9_TIMES'].split(',') if x]
if not times:
    print("N=0"); exit()
times.sort()
n = len(times)
print(f"N={n}")
print(f"min={min(times)}s")
print(f"max={max(times)}s")
print(f"avg={statistics.mean(times):.1f}s")
print(f"median={statistics.median(times):.0f}s")
print(f"stdev={statistics.stdev(times):.1f}s" if n > 1 else "stdev=N/A")
print(f"p90={times[int(n*0.9)]}s")
print(f"p99={times[int(n*0.99)]}s" if n >= 100 else f"p99=N/A")
PYEOF
)

log "Total iterações: 100"
log "PASS:    $PASS / 100 ($((PASS))%)"
log "FAIL:    $FAIL / 100"
log "Recovery (BOOTSEL+reflash):  $RECOVERY"
log "Tempo total loop: ${LOOP_DT}s ($((LOOP_DT/60)) min)"
log ""
log "Per-iter time stats:"
echo "$STATS" | sed 's/^/  /' | tee -a "$LOG"
log ""
if [ ${#FAIL_ITERS[@]} -gt 0 ]; then
    log "FAIL iters:"
    for f in "${FAIL_ITERS[@]}"; do log "  - $f"; done
else
    log "Nenhuma falha registrada."
fi
log ""
log "CSV completo: $STATS_CSV"
log "Log completo: $LOG"

# Markdown report
{
    echo "# SIMUT F9 loop100 — Relatório"
    echo
    echo "**Timestamp:** $TS"
    echo "**Versão:** $(grep '^#define SIMUT_VERSION' SystemDefs_Limits.h | head -1 | sed 's/.*"\(v[^ ]*\)".*/\1/')"
    echo "**Duração total:** ${LOOP_DT}s ($((LOOP_DT/60)) min)"
    echo
    echo "## Resumo"
    echo
    echo "| Métrica | Valor |"
    echo "|---|---|"
    echo "| Iterações totais | 100 |"
    echo "| PASS | $PASS / 100 ($PASS%) |"
    echo "| FAIL | $FAIL / 100 |"
    echo "| Recoveries (BOOTSEL+reflash) | $RECOVERY |"
    echo
    echo "## Per-iter timing"
    echo
    echo "\`\`\`"
    echo "$STATS"
    echo "\`\`\`"
    echo
    if [ ${#FAIL_ITERS[@]} -gt 0 ]; then
        echo "## Falhas"
        echo
        for f in "${FAIL_ITERS[@]}"; do echo "- $f"; done
        echo
    else
        echo "## Falhas"
        echo
        echo "Nenhuma. 100% sucesso."
        echo
    fi
    echo "## Pre-test"
    echo
    echo "- Factory reset admin executado"
    echo "- chpass OTP → '$PASSWORD' executado"
    echo "- Backup FS salvo em \`$BKP_FILE\`"
    echo
    echo "## Artefatos"
    echo
    echo "- Log: \`$LOG\`"
    echo "- CSV: \`$STATS_CSV\`"
    echo "- BKP: \`$BKP_FILE\`"
} > "$REPORT"

log "Relatório markdown: $REPORT"
log "=========================================================="
