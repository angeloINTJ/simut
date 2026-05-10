#!/usr/bin/env bash
# ============================================================================
# test_f9_loop100_real.sh — 100 ciclos OTA + restore com config REAL preservada.
#
# Diferente de test_f9_loop100.sh:
#   - NÃO faz factory_reset_admin no pre-test (preserva senha do usuário).
#   - Backup canônico é tirado uma vez do estado real (ou reusado se já existe).
#   - Em cada iter: OTA → restore canonical → integrity check (hash arquivos críticos).
#   - Critério integridade: SHA256 individual dos 29 arquivos críticos
#     (config/system.{bin,bak}, calib, lang, themes, favicon, history/*).
#     Ignora system.blog, system.old.blog, t_cursor.bin (voláteis entre boots).
#
# Uso:
#   ITERS=10 SAFE_DIR=/path bash tools/test_f9_loop100_real.sh
#
# Variáveis ambiente:
#   ITERS       (default 100)
#   SAFE_DIR    (default /home/angelo/Documentos/simut_real_loop_<TS>)
#   PASSWORD    (default '^çarrando.1a')
#   SIMUT_IP    (default 192.168.3.195)
# ============================================================================
set -uo pipefail
cd /home/angelo/Documentos/SIMUT/

TS=$(date +%Y%m%d-%H%M%S)
LOG=docs/test_reports/f9_loop100_real_${TS}.log
STATS_CSV=docs/test_reports/f9_loop100_real_${TS}.csv
REPORT=docs/test_reports/f9_loop100_real_${TS}_report.md
SAFE_DIR="${SAFE_DIR:-/home/angelo/Documentos/simut_real_loop_${TS}}"
CANONICAL=$SAFE_DIR/canonical.bkp
# Voláteis ignorados na comparação in-memory (mudam a cada boot/telemetria).
IGNORE_PATHS='/system.blog,/system.old.blog,/config/t_cursor.bin'
mkdir -p "$SAFE_DIR" docs/test_reports

PYBIN=./.venv/bin/python3
export HAND_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00
export SIMUT_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00
SIMUT_IP="${SIMUT_IP:-192.168.3.195}"
PASSWORD="${PASSWORD:-^çarrando.1a}"
FW=.pio/build/pico_w_release/firmware.bin
ITERS="${ITERS:-100}"

PASS=0
FAIL=0
MISMATCH=0
RECOVERY=0
RESTORE_CONN_RESETS=0
declare -a ITER_TIMES=()
declare -a FAIL_ITERS=()
declare -a MISMATCH_ITERS=()

log() { echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }

echo "iter,result,duration_s,recovery_used,mismatched_count,timestamp" > "$STATS_CSV"

# ============================================================================
# Helpers
# ============================================================================
probe_http() {
    curl -fsS --max-time 3 "http://$SIMUT_IP/api/login_init" >/dev/null 2>&1
}

wait_http() {
    local timeout="$1"
    local deadline=$(( $(date +%s) + timeout ))
    while [ $(date +%s) -lt $deadline ]; do
        probe_http && return 0
        sleep 3
    done
    return 1
}

login_check() {
    F9_BASE="http://$SIMUT_IP" F9_PWD="$PASSWORD" timeout 10 $PYBIN <<'PYEOF' 2>/dev/null
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode('utf-8')).hexdigest()
s = requests.Session(); base = os.environ['F9_BASE']
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login', data={'user':'admin','pass':sha(os.environ['F9_PWD']),'nonce':nonce}, timeout=5)
sys.exit(0 if r.status_code == 200 else r.status_code)
PYEOF
}

download_backup() {
    local out_path="$1"
    F9_BASE="http://$SIMUT_IP" F9_PWD="$PASSWORD" F9_OUT="$out_path" \
      timeout 60 $PYBIN <<'PYEOF' 2>&1
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode('utf-8')).hexdigest()
s = requests.Session(); base = os.environ['F9_BASE']
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login', data={'user':'admin','pass':sha(os.environ['F9_PWD']),'nonce':nonce}, timeout=10)
if r.status_code != 200: sys.exit(11)
r = s.get(f'{base}/api/backup', stream=True, timeout=60)
if r.status_code != 200: sys.exit(12)
total=0
with open(os.environ['F9_OUT'],'wb') as f:
    for c in r.iter_content(8192): f.write(c); total += len(c)
print(f'wrote {total} bytes')
PYEOF
}

restore_backup() {
    # Tolera ConnectionResetError no apply: device pode rebotar pós-write de
    # 32 arquivos LFS antes de enviar o 200 OK. Integridade é validada pelo
    # download+compare a seguir, que é a verdade de campo.
    # Exit codes: 0=clean, 4=apply_conn_reset (verify ainda), 11=login,
    #             12=validate_fail, 13=apply_http_error
    local bkp_path="$1"
    F9_BASE="http://$SIMUT_IP" F9_PWD="$PASSWORD" F9_BKP="$bkp_path" \
      timeout 90 $PYBIN <<'PYEOF' 2>&1
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode('utf-8')).hexdigest()
s = requests.Session(); base = os.environ['F9_BASE']; bkp = os.environ['F9_BKP']
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login', data={'user':'admin','pass':sha(os.environ['F9_PWD']),'nonce':nonce}, timeout=10)
if r.status_code != 200:
    print(f'LOGIN_FAIL_{r.status_code}'); sys.exit(11)
with open(bkp,'rb') as f: data = f.read()
r = s.post(f'{base}/api/restore?op=validate',
           files={'file':('backup.bkp', data, 'application/octet-stream')}, timeout=60)
if r.status_code != 200:
    print(f'VALIDATE_FAIL_{r.status_code}: {r.text[:200]}'); sys.exit(12)
try:
    r = s.post(f'{base}/api/restore?op=apply',
               files={'file':('backup.bkp', data, 'application/octet-stream')}, timeout=90)
    if r.status_code != 200:
        print(f'APPLY_FAIL_{r.status_code}: {r.text[:200]}'); sys.exit(13)
    print('RESTORE_OK')
except (requests.exceptions.ConnectionError, requests.exceptions.ChunkedEncodingError) as e:
    # Provável watchdog reboot pós-write — verify por download+compare.
    print(f'RESTORE_CONN_RESET (provável reboot pós-write): {type(e).__name__}'); sys.exit(4)
PYEOF
}

# ============================================================================
# Recovery (mesma lógica patched do loop100, sem factory)
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

recover_device() {
    log "  [RECOVERY] device offline — iniciando recovery"
    log "  [RECOVERY] PicoHand HOLD BOOTSEL + RESET pulse"
    hand_cmd 'HOLD BOOTSEL' >> "$LOG"
    hand_cmd 'HOLD RESET'   >> "$LOG"
    sleep 0.4
    hand_cmd 'RELEASE RESET'   >> "$LOG"
    sleep 0.6
    hand_cmd 'RELEASE BOOTSEL' >> "$LOG"
    sleep 3

    for try in 1 2 3 4; do
        sleep 2
        [ -e /dev/disk/by-label/RPI-RP2 ] && { log "  [RECOVERY] RPI-RP2 detected (try $try)"; break; }
    done
    if [ ! -e /dev/disk/by-label/RPI-RP2 ]; then
        log "  [RECOVERY] RPI-RP2 NOT detected — falhou"
        return 1
    fi
    log "  [RECOVERY] picotool load -x firmware.uf2"
    picotool load -x .pio/build/pico_w_release/firmware.uf2 2>&1 | tail -1 >> "$LOG"

    for try in 1 2 3 4 5 6 7 8; do
        [ -e "$SIMUT_PORT" ] && break
        sleep 1
    done
    if [ ! -e "$SIMUT_PORT" ]; then
        log "  [RECOVERY] CDC não re-enumerou em 8s"
        return 1
    fi

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
            print('SYS_READY_DETECTED'); sys.exit(0)
print('SYS_READY_TIMEOUT'); sys.exit(2)
PYEOF
)
    if echo "$READY_OUT" | grep -q SYS_READY_DETECTED; then
        log "  [RECOVERY] SYS READY detectado"
    else
        log "  [RECOVERY] SYS READY não chegou em 60s — seguindo"
    fi

    log "  [RECOVERY] probe HTTP rápido (até 30s)"
    if wait_http 30; then
        log "  [RECOVERY] WiFi OK direto do NVS"
    else
        log "  [RECOVERY] HTTP timeout — reconfigurando WiFi via CLI"
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
        log "  [RECOVERY] aguardando WiFi pós-reconfigure (até 90s)"
        if ! wait_http 90; then
            log "  [RECOVERY] WiFi não veio após reload — fallback PicoHand RESET (HW)"
            hand_cmd 'HOLD RESET' >> "$LOG"
            sleep 0.4
            hand_cmd 'RELEASE RESET' >> "$LOG"
            log "  [RECOVERY] aguardando boot pós-RESET (até 60s)"
            wait_http 60 || { log "  [RECOVERY] WiFi não veio nem após HW RESET"; return 1; }
            log "  [RECOVERY] WiFi OK pós-HW RESET"
        fi
    fi

    # Após recovery, senha pode ter sumido (NVS resetado pelo reflash em alguns casos).
    # Tentar login com senha do usuário; se falhar, restaurar canonical (que tem hash certo).
    if login_check >/dev/null 2>&1; then
        log "  [RECOVERY] login OK com senha preservada"
    else
        log "  [RECOVERY] login falhou — fazendo admin reset CLI + chpass + restore canonical"
        OTP_OUT=$(timeout 15 $PYBIN -u <<'PYEOF' 2>&1
import serial, time, os, re, sys
s = serial.Serial(os.environ['SIMUT_PORT'], 115200, timeout=2)
time.sleep(0.5); s.reset_input_buffer()
s.write(b'\r\nconf system admin reset confirm\r\nwrite memory\r\n')
time.sleep(5)
buf = s.read(8192).decode('utf-8','replace')
m = re.search(r'^\s*([A-Z0-9]{6,16})\s*$', buf, re.MULTILINE)
print(m.group(1) if m else 'NO_OTP')
s.close()
PYEOF
)
        OTP=$(echo "$OTP_OUT" | tail -1 | tr -d ' \r\n\t')
        [ "$OTP" = "NO_OTP" ] || [ -z "$OTP" ] && { log "  [RECOVERY] OTP não capturado"; return 1; }
        log "  [RECOVERY] OTP: $OTP — chpass para senha do usuário"
        sleep 4
        F9_OTP="$OTP" F9_NEWPASS="$PASSWORD" F9_BASE="http://$SIMUT_IP" \
          timeout 15 $PYBIN <<'PYEOF' 2>&1 | tee -a "$LOG"
import os, requests, hashlib, sys
sha = lambda x: hashlib.sha256(x.encode('utf-8')).hexdigest()
s = requests.Session(); base = os.environ['F9_BASE']
nonce = s.get(f'{base}/api/login_init', timeout=5).json()['nonce']
r = s.post(f'{base}/api/login_chpass',
    data={'user':'admin','oldpass':sha(os.environ['F9_OTP']),
          'newpass':sha(os.environ['F9_NEWPASS']),'nonce':nonce}, timeout=10)
print(f'chpass {r.status_code}: {r.text[:120]}')
sys.exit(0 if r.status_code == 200 else 1)
PYEOF
    fi
    log "  [RECOVERY] OK"
    return 0
}

# ============================================================================
# Pre-flight
# ============================================================================
log "=========================================================="
log "F9 loop100 REAL — start $TS"
log "  SIMUT: $SIMUT_IP   port: $SIMUT_PORT"
log "  Hand:  $HAND_PORT"
log "  Senha: $PASSWORD"
log "  Canonical: $CANONICAL"
log "  Iters: $ITERS"
log "=========================================================="

probe_http || { log "FATAL: device offline"; exit 1; }
log "Pre-flight HTTP: OK"

login_check
LOGIN_RC=$?
if [ $LOGIN_RC -ne 0 ]; then
    log "FATAL: login com senha '$PASSWORD' falhou (rc=$LOGIN_RC)"
    exit 1
fi
log "Pre-flight login: OK"

# Use canonical existente OU baixa novo
if [ ! -f "$CANONICAL" ]; then
    log "Baixando backup canônico..."
    download_backup "$CANONICAL" | tee -a "$LOG"
fi

# Validate canonical (header CRC + magic)
$PYBIN tools/verify_backup.py "$CANONICAL" --quiet 2>&1 | tee -a "$LOG"
[ ${PIPESTATUS[0]} -ne 0 ] && { log "FATAL: canonical inválido"; exit 1; }

# Sanity-check do comparator: canonical vs canonical = 0 mismatches.
SELF_OUT=$($PYBIN tools/bkp_compare.py "$CANONICAL" "$CANONICAL" --ignore "$IGNORE_PATHS")
SELF_RC=$?
log "Self-compare canonical: $(echo "$SELF_OUT" | tail -1) (rc=$SELF_RC)"
[ $SELF_RC -ne 0 ] && { log "FATAL: comparator não passou self-check"; exit 1; }
CRIT_COUNT=$(echo "$SELF_OUT" | tail -1 | grep -oE 'count=[0-9]+' | cut -d= -f2)
log "Critical files: $CRIT_COUNT arquivos (in-memory hash, sem extrair)"

log ""
log "=========================================================="
log "PRE-TEST COMPLETO — iniciando $ITERS iters"
log "=========================================================="
log ""

# ============================================================================
# LOOP
# ============================================================================
LOOP_START=$(date +%s)
for iter in $(seq 1 "$ITERS"); do
    iter_start=$(date +%s)
    iter_recovery=0
    log "==================== ITER $iter / $ITERS ===================="

    # Pre-iter: verifica online + login
    if ! probe_http; then
        log "  pre-iter: device offline — recovery"
        if recover_device; then
            iter_recovery=1
            RECOVERY=$((RECOVERY+1))
        else
            log "  iter $iter: pre-iter recovery FAILED"
            FAIL=$((FAIL+1)); FAIL_ITERS+=("$iter:pre_recovery")
            iter_dt=$(($(date +%s) - iter_start))
            echo "$iter,FAIL_RECOVERY,$iter_dt,1,,$(date +%H:%M:%S)" >> "$STATS_CSV"
            continue
        fi
    fi

    # 1. OTA stage+commit+apply
    log "  ota_apply.py executando (stage+apply, sem restore embutido)..."
    timeout 200 ./tools/ota_apply.py \
        --ip "$SIMUT_IP" --user admin --pass "$PASSWORD" \
        --firmware "$FW" --no-restore >> "$LOG" 2>&1
    apply_rc=$?
    log "  ota_apply.py exit=$apply_rc"

    # 2. Aguardar HTTP voltar
    log "  aguardando HTTP até 180s..."
    if ! wait_http 180; then
        log "  iter $iter: HTTP não voltou — recovery"
        if recover_device; then
            iter_recovery=1; RECOVERY=$((RECOVERY+1))
        fi
        FAIL=$((FAIL+1)); FAIL_ITERS+=("$iter:boot_timeout")
        iter_dt=$(($(date +%s) - iter_start))
        echo "$iter,FAIL_BOOT,$iter_dt,$iter_recovery,,$(date +%H:%M:%S)" >> "$STATS_CSV"
        continue
    fi
    log "  HTTP OK pós-OTA"

    # 3. Restore canonical (sobrescreve config + senha + tudo)
    log "  restaurando canonical via /api/restore..."
    restore_out=$(restore_backup "$CANONICAL")
    restore_rc=$?
    echo "$restore_out" >> "$LOG"
    if [ $restore_rc -eq 0 ]; then
        log "  restore: clean 200 OK"
        restore_note=""
    elif [ $restore_rc -eq 4 ]; then
        log "  restore: ConnReset (provável reboot pós-write — integridade decidirá PASS/FAIL)"
        restore_note="conn_reset"
        RESTORE_CONN_RESETS=$((RESTORE_CONN_RESETS+1))
    else
        log "  iter $iter: restore FAIL (rc=$restore_rc): $(echo "$restore_out" | tail -1)"
        FAIL=$((FAIL+1)); FAIL_ITERS+=("$iter:restore=$restore_rc")
        iter_dt=$(($(date +%s) - iter_start))
        echo "$iter,FAIL_RESTORE,$iter_dt,$iter_recovery,,$(date +%H:%M:%S)" >> "$STATS_CSV"
        continue
    fi

    # 4. Aguardar reboot pós-restore (se houver) + estabilizar até 120s
    log "  estabilizando pós-restore (até 120s)..."
    sleep 4
    if ! wait_http 120; then
        log "  iter $iter: HTTP pós-restore não voltou em 120s"
        FAIL=$((FAIL+1)); FAIL_ITERS+=("$iter:post_restore_timeout")
        iter_dt=$(($(date +%s) - iter_start))
        echo "$iter,FAIL_POST_RESTORE,$iter_dt,$iter_recovery,,$(date +%H:%M:%S)" >> "$STATS_CSV"
        continue
    fi

    # 5. Download novo backup via API web e comparar in-memory
    NEW_BKP="$SAFE_DIR/iter${iter}.bkp"
    log "  baixando backup pós-restore via API..."
    download_backup "$NEW_BKP" >> "$LOG" 2>&1
    dl_rc=$?
    if [ $dl_rc -ne 0 ]; then
        log "  iter $iter: download backup FAIL (rc=$dl_rc)"
        FAIL=$((FAIL+1)); FAIL_ITERS+=("$iter:bkp_download=$dl_rc")
        iter_dt=$(($(date +%s) - iter_start))
        echo "$iter,FAIL_BKP_DOWNLOAD,$iter_dt,$iter_recovery,,$(date +%H:%M:%S)" >> "$STATS_CSV"
        continue
    fi

    # 6. Comparar in-memory: parseia ambos .bkp em RAM, compara SHA dos críticos
    CMP_OUT=$($PYBIN tools/bkp_compare.py "$CANONICAL" "$NEW_BKP" --ignore "$IGNORE_PATHS" 2>&1)
    CMP_RC=$?
    iter_dt=$(($(date +%s) - iter_start))
    MM_LINE=$(echo "$CMP_OUT" | tail -1)
    MM_COUNT=$(echo "$MM_LINE" | grep -oE 'mismatches=[0-9]+' | cut -d= -f2)
    : "${MM_COUNT:=0}"

    if [ "$CMP_RC" -eq 0 ]; then
        log "  iter $iter: PASS (${iter_dt}s, integrity OK — $MM_LINE)"
        PASS=$((PASS+1))
        ITER_TIMES+=("$iter_dt")
        echo "$iter,PASS,$iter_dt,$iter_recovery,0,$(date +%H:%M:%S)" >> "$STATS_CSV"
        # cleanup do .bkp da iter (não precisa mais — comparação foi in-memory)
        rm -f "$NEW_BKP"
    elif [ "$CMP_RC" -eq 1 ]; then
        DIFF_LINES=$(echo "$CMP_OUT" | head -n -1 | tr '\n' ';')
        log "  iter $iter: MISMATCH (${iter_dt}s, ${MM_COUNT} diff)"
        log "    $DIFF_LINES"
        MISMATCH=$((MISMATCH+1))
        MISMATCH_ITERS+=("iter $iter ($MM_COUNT): $DIFF_LINES")
        ITER_TIMES+=("$iter_dt")
        echo "$iter,MISMATCH,$iter_dt,$iter_recovery,${MM_COUNT},$(date +%H:%M:%S)" >> "$STATS_CSV"
        # mantém o .bkp da iter pra forensics (basta o file binário, sem dir)
    else
        # PARSE_ERROR ou IO error
        log "  iter $iter: COMPARE FAIL (rc=$CMP_RC): $MM_LINE"
        FAIL=$((FAIL+1)); FAIL_ITERS+=("$iter:compare_error=$CMP_RC")
        echo "$iter,FAIL_COMPARE,$iter_dt,$iter_recovery,,$(date +%H:%M:%S)" >> "$STATS_CSV"
    fi
done

# ============================================================================
# Relatório final
# ============================================================================
LOOP_END=$(date +%s)
TOTAL=$((LOOP_END - LOOP_START))

log ""
log "=========================================================="
log "RELATÓRIO FINAL — F9 loop100 REAL"
log "=========================================================="
log "Total iterações: $ITERS"
log "PASS:     $PASS / $ITERS ($((PASS*100/ITERS))%)"
log "MISMATCH: $MISMATCH / $ITERS"
log "FAIL:     $FAIL / $ITERS"
log "Recovery (BOOTSEL+reflash): $RECOVERY"
log "Restore ConnResets (reboot pós-write): $RESTORE_CONN_RESETS"
log "Tempo total: ${TOTAL}s ($((TOTAL/60)) min)"
log ""

if [ ${#ITER_TIMES[@]} -gt 0 ]; then
    log "Per-iter time stats:"
    printf '%s\n' "${ITER_TIMES[@]}" | $PYBIN -c "
import sys, statistics
times = [int(x) for x in sys.stdin.read().split()]
print(f'  N={len(times)}')
print(f'  min={min(times)}s  max={max(times)}s  avg={sum(times)/len(times):.1f}s  median={statistics.median(times):.0f}s')
if len(times) > 1: print(f'  stdev={statistics.stdev(times):.1f}s')
sorted_t = sorted(times)
def pct(p):
    k = int(len(sorted_t)*p/100); return sorted_t[min(k, len(sorted_t)-1)]
print(f'  p90={pct(90)}s  p99={pct(99)}s')
" | tee -a "$LOG"
fi

if [ ${#FAIL_ITERS[@]} -gt 0 ]; then
    log ""
    log "Falhas:"
    for f in "${FAIL_ITERS[@]}"; do log "  - $f"; done
fi

if [ ${#MISMATCH_ITERS[@]} -gt 0 ]; then
    log ""
    log "Mismatches:"
    for m in "${MISMATCH_ITERS[@]}"; do log "  - $m"; done
fi

# Markdown
{
echo "# F9 loop100 REAL — relatório $TS"
echo ""
echo "**Senha:** \`$PASSWORD\`"
echo "**Canonical:** \`$CANONICAL\` ($(stat -c %s "$CANONICAL" 2>/dev/null || echo 0) bytes)"
echo "**Critério integridade:** SHA256 individual de $(wc -l < "$CRIT_LIST") arquivos críticos"
echo "**Voláteis ignorados:** system.blog, system.old.blog, t_cursor.bin"
echo ""
echo "## Resultados"
echo "| Métrica | Valor |"
echo "|---|---|"
echo "| Iterações | $ITERS |"
echo "| PASS | $PASS ($((PASS*100/ITERS))%) |"
echo "| MISMATCH | $MISMATCH |"
echo "| FAIL | $FAIL |"
echo "| Recoveries | $RECOVERY |"
echo "| Restore ConnResets | $RESTORE_CONN_RESETS |"
echo "| Tempo total | ${TOTAL}s ($((TOTAL/60)) min) |"
if [ ${#ITER_TIMES[@]} -gt 0 ]; then
echo ""
echo "## Per-iter timing"
printf '%s\n' "${ITER_TIMES[@]}" | $PYBIN -c "
import sys, statistics
t = [int(x) for x in sys.stdin.read().split()]
print(f'| Métrica | s |')
print(f'|---|---|')
print(f'| N | {len(t)} |')
print(f'| min | {min(t)} |')
print(f'| max | {max(t)} |')
print(f'| avg | {sum(t)/len(t):.1f} |')
print(f'| median | {statistics.median(t):.0f} |')
if len(t) > 1: print(f'| stdev | {statistics.stdev(t):.1f} |')
s=sorted(t)
def pct(p): return s[min(int(len(s)*p/100), len(s)-1)]
print(f'| p90 | {pct(90)} |')
print(f'| p99 | {pct(99)} |')
"
fi
echo ""
if [ ${#FAIL_ITERS[@]} -gt 0 ]; then
    echo "## Falhas"
    for f in "${FAIL_ITERS[@]}"; do echo "- $f"; done
    echo ""
fi
if [ ${#MISMATCH_ITERS[@]} -gt 0 ]; then
    echo "## Mismatches (integridade)"
    for m in "${MISMATCH_ITERS[@]}"; do echo "- $m"; done
    echo ""
fi
echo "## Artefatos"
echo "- CSV: \`$STATS_CSV\`"
echo "- Log:  \`$LOG\`"
echo "- Backups .bkp das iters MISMATCH (forensics): \`$SAFE_DIR/iter*.bkp\`"
} > "$REPORT"
log ""
log "CSV: $STATS_CSV"
log "Log: $LOG"
log "Relatório: $REPORT"
log "=========================================================="
