#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# run_stress_test.sh — orchestrator end-to-end do stress test de telemetria + CSV.
#
# Pipeline:
#   1. Backup do FS atual (insurance + restore source)
#   2. Gera N arquivos de histórico V2 sintéticos
#   3. Upload pra /history/ via /api/upload
#   4. Monitora drain de telemetria (metrics.telSent) até finalizar OU timeout
#   5. Trigger CSV export e valida (HTTP 200 + magic SIM2 SIMX)
#   6. Restore do FS pra estado original (--delete-extras pra limpar stress files)
#   7. Relatório final
#
# Uso:
#   SIMUT_IP=192.168.3.195 SIMUT_USER=admin SIMUT_PASS='...' \
#       ./tools/stress_test/run_stress_test.sh [opts]
#
# Opcoes:
#   --days N                 Numero de dias a gerar (default: 30, max ~37)
#   --records-per-day M      Records/dia (default: 1440 = 1/min)
#   --skip-backup            Pula backup (perigoso!)
#   --skip-restore           Pula restore (deixa stress files no FS)
#   --skip-upload            Pula upload (só gera local + report)
#   --skip-drain             Pula monitoramento de drain (vai pro CSV direto)
#   --skip-csv               Pula teste CSV
#   --drain-timeout SEC      Max tempo de drain (default: 2400 = 40 min)
#   --report-dir DIR         Onde salvar relatórios (default: /tmp/stress_report_<ts>)
# -----------------------------------------------------------------------------

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_simut_api.sh"

DAYS=30
RECORDS_PER_DAY=1440
SKIP_BACKUP=0
SKIP_RESTORE=0
SKIP_UPLOAD=0
SKIP_DRAIN=0
SKIP_CSV=0
DRAIN_TIMEOUT=2400
TS=$(date +%Y%m%d_%H%M%S)
REPORT_DIR="/tmp/stress_report_${TS}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --days) DAYS="$2"; shift 2 ;;
        --records-per-day) RECORDS_PER_DAY="$2"; shift 2 ;;
        --skip-backup) SKIP_BACKUP=1; shift ;;
        --skip-restore) SKIP_RESTORE=1; shift ;;
        --skip-upload) SKIP_UPLOAD=1; shift ;;
        --skip-drain) SKIP_DRAIN=1; shift ;;
        --skip-csv) SKIP_CSV=1; shift ;;
        --drain-timeout) DRAIN_TIMEOUT="$2"; shift 2 ;;
        --report-dir) REPORT_DIR="$2"; shift 2 ;;
        *) _simut_error "Flag desconhecida: $1"; exit 2 ;;
    esac
done

mkdir -p "$REPORT_DIR"
LOG_FILE="${REPORT_DIR}/stress.log"
exec > >(tee "$LOG_FILE") 2>&1

GEN_DIR="${REPORT_DIR}/generated"
BACKUP_DIR="${REPORT_DIR}/backup"

# Tracking pra restore: lista de arquivos que ADICIONAMOS
ADDED_FILES="${REPORT_DIR}/added_files.txt"
> "$ADDED_FILES"

_simut_log "════════════════════════════════════════════════════════════════"
_simut_log "  SIMUT STRESS TEST — $TS"
_simut_log "════════════════════════════════════════════════════════════════"
_simut_log "  Device:         $SIMUT_IP"
_simut_log "  User:           $SIMUT_USER"
_simut_log "  Days:           $DAYS"
_simut_log "  Records/day:    $RECORDS_PER_DAY"
_simut_log "  Drain timeout:  ${DRAIN_TIMEOUT}s"
_simut_log "  Report dir:     $REPORT_DIR"
_simut_log "════════════════════════════════════════════════════════════════"
echo ""

# ─── Phase 1: Backup ─────────────────────────────────────────────────────────
if [[ $SKIP_BACKUP -eq 0 ]]; then
    _simut_log "PHASE 1/6: Backup FS"
    "$SCRIPT_DIR/backup_fs.sh" "$BACKUP_DIR" || { _simut_error "Backup falhou — ABORTING"; exit 1; }
    echo ""
fi

# ─── Phase 2: Generate ───────────────────────────────────────────────────────
_simut_log "PHASE 2/6: Generate $DAYS dias × $RECORDS_PER_DAY records"
python3 "$SCRIPT_DIR/generate_history_v2.py" \
    --days "$DAYS" --records-per-day "$RECORDS_PER_DAY" \
    --output-dir "$GEN_DIR" --quiet
GEN_FILES_COUNT=$(ls "$GEN_DIR"/*.bin 2>/dev/null | wc -l)
GEN_TOTAL_BYTES=$(du -sb "$GEN_DIR" | cut -f1)
_simut_log "  → $GEN_FILES_COUNT arquivos, $GEN_TOTAL_BYTES B ($((GEN_TOTAL_BYTES/1024)) KB)"
echo ""

# ─── Phase 3: Upload ─────────────────────────────────────────────────────────
if [[ $SKIP_UPLOAD -eq 0 ]]; then
    _simut_log "PHASE 3/6: Upload pra /history/"
    simut_login || exit 1
    UP_OK=0
    UP_FAIL=0
    UP_START=$(date +%s)
    for f in "$GEN_DIR"/*.bin; do
        fname=$(basename "$f")
        if simut_upload "$f" "/history"; then
            echo "/history/$fname" >> "$ADDED_FILES"
            UP_OK=$((UP_OK + 1))
            printf '  ↑ %s\n' "$fname"
        else
            UP_FAIL=$((UP_FAIL + 1))
        fi
    done
    UP_DUR=$(( $(date +%s) - UP_START ))
    _simut_log "  → $UP_OK OK / $UP_FAIL FAIL em ${UP_DUR}s"
    [[ $UP_FAIL -gt 0 ]] && _simut_warn "  Algumas uploads falharam — continuando"
    simut_logout
    echo ""
fi

# ─── Phase 3.5: Reset telemetry cursor via CLI 'tel reset' ──────────────────
# Desde v3.30.4, o CLI command 'tel reset' invalida cache RAM + apaga flash
# file num único call atômico, sem precisar reboot. Mais rápido e confiável
# que o approach antigo (api/delete + Serial reload confirm).
if [[ $SKIP_UPLOAD -eq 0 && $SKIP_DRAIN -eq 0 ]]; then
    _simut_log "PHASE 3.5/6: Reset telemetry cursor via 'tel reset'"
    out=$(simut_serial_cmd "tel reset" 3 2>/dev/null || true)
    if echo "$out" | grep -qE "Cursor de telemetria resetado|Telemetry cursor reset"; then
        _simut_log "  ✓ Cursor resetado (cache RAM + flash file)"
    else
        _simut_warn "  Resposta inesperada do 'tel reset': $(echo "$out" | tr -d '\r' | head -c 80)"
        _simut_warn "  (Firmware < v3.30.4? Comando talvez não exista. Próximo drain pode pegar só dados novos.)"
    fi
    sleep 2
    echo ""
fi

# ─── Phase 4: Drain telemetria (via /api/status; Serial pode estar ocupado) ──
if [[ $SKIP_DRAIN -eq 0 ]]; then
    _simut_log "PHASE 4/6: Monitor telemetry drain (timeout=${DRAIN_TIMEOUT}s)"
    simut_login || exit 1

    # Snapshot inicial (imediatamente após upload)
    s0=$(simut_get_json /api/status)
    sent0=$(echo "$s0" | python3 -c "import json,sys;print(json.load(sys.stdin)['metr']['ts'])" 2>/dev/null || echo 0)
    bytes0=$(echo "$s0" | python3 -c "import json,sys;print(json.load(sys.stdin)['metr']['tb'])" 2>/dev/null || echo 0)
    pending0=$(echo "$s0" | python3 -c "import json,sys;print(json.load(sys.stdin)['sys']['pending'])" 2>/dev/null || echo 0)
    _simut_log "  Inicial: telSent=$sent0 telBytes=$bytes0 pending=$pending0"

    DRAIN_CSV="${REPORT_DIR}/drain.csv"
    echo "epoch,uptime,telSent,telFailed,telTotalBytes,pendingEstimate,deltaSent" > "$DRAIN_CSV"
    DRAIN_START=$(date +%s)
    last_sent=$sent0
    stable_count=0
    while true; do
        elapsed=$(( $(date +%s) - DRAIN_START ))
        if [[ $elapsed -ge $DRAIN_TIMEOUT ]]; then
            _simut_warn "  Timeout atingido (${DRAIN_TIMEOUT}s)"
            break
        fi
        sleep 30
        st=$(simut_get_json /api/status)
        sent=$(echo "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['metr']['ts'])" 2>/dev/null || echo "$last_sent")
        fail=$(echo "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['metr']['tf'])" 2>/dev/null || echo 0)
        bytes=$(echo "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['metr']['tb'])" 2>/dev/null || echo 0)
        pending=$(echo "$st" | python3 -c "import json,sys;print(json.load(sys.stdin)['sys']['pending'])" 2>/dev/null || echo "?")
        ts=$(date +%s)
        delta_sent=$((sent - sent0))
        printf '%s,%s,%s,%s,%s,%s,%s\n' "$ts" "$elapsed" "$sent" "$fail" "$bytes" "$pending" "$delta_sent" >> "$DRAIN_CSV"
        printf '  [%4ds] sent=%s (+%s) fail=%s bytes=%s pending=%s\n' "$elapsed" "$sent" "$delta_sent" "$fail" "$bytes" "$pending"

        # Stop conditions:
        # 1) pending=0 e telSent estável por 2 ciclos
        if [[ "$pending" == "0" ]]; then
            if [[ "$sent" == "$last_sent" ]]; then
                stable_count=$((stable_count + 1))
                if [[ $stable_count -ge 2 ]]; then
                    _simut_log "  ✓ Drain finalizado (pending=0, telSent estável)"
                    break
                fi
            else
                stable_count=0
            fi
        fi
        last_sent="$sent"
    done
    _simut_log "  → Drain: ${delta_sent:-0} envios novos, $((bytes - bytes0)) bytes novos"
    simut_logout
    echo ""
fi

# ─── Phase 5: CSV export ─────────────────────────────────────────────────────
if [[ $SKIP_CSV -eq 0 ]]; then
    _simut_log "PHASE 5/6: CSV export test"
    simut_login || exit 1

    # Pega range das datas geradas
    FIRST_FN=$(ls "$GEN_DIR"/*.bin | head -1 | xargs -n1 basename | sed 's/\.bin$//')
    LAST_FN=$(ls "$GEN_DIR"/*.bin | tail -1 | xargs -n1 basename | sed 's/\.bin$//')
    # YYYYMMDD → epoch (00:00 UTC)
    epoch_from=$(date -u -d "${FIRST_FN:0:4}-${FIRST_FN:4:2}-${FIRST_FN:6:2} 00:00:00" +%s)
    epoch_to=$(date -u -d "${LAST_FN:0:4}-${LAST_FN:4:2}-${LAST_FN:6:2} 23:59:59" +%s)

    _simut_log "  Range: $FIRST_FN → $LAST_FN (epoch $epoch_from..$epoch_to)"

    # Cap 31 dias do firmware — divide em chunks
    chunk_start=$epoch_from
    csv_total_bytes=0
    csv_chunks_ok=0
    csv_chunks_fail=0

    while [[ $chunk_start -lt $epoch_to ]]; do
        chunk_end=$(( chunk_start + 31 * 86400 ))
        [[ $chunk_end -gt $epoch_to ]] && chunk_end=$epoch_to

        out_file="${REPORT_DIR}/csv_chunk_${chunk_start}.simx"
        url="${SIMUT_BASE}/api/export/history.bin?from=${chunk_start}&to=${chunk_end}"
        http_code=$(curl -s -b "$COOKIE_JAR" -o "$out_file" -w "%{http_code}" "$url")

        if [[ "$http_code" == "200" ]]; then
            sz=$(stat -c%s "$out_file" 2>/dev/null || stat -f%z "$out_file")
            magic=$(head -c 4 "$out_file" | xxd -p)
            csv_total_bytes=$((csv_total_bytes + sz))
            if [[ "$magic" == "53494d58" ]]; then  # "SIMX"
                csv_chunks_ok=$((csv_chunks_ok + 1))
                printf '  ✓ chunk %s..%s: HTTP %s, %s B, magic OK\n' "$chunk_start" "$chunk_end" "$http_code" "$sz"
            else
                csv_chunks_fail=$((csv_chunks_fail + 1))
                _simut_warn "  ⚠ chunk %s..%s: magic invalido (%s) - %s B\n" "$chunk_start" "$chunk_end" "$magic" "$sz"
            fi
        else
            csv_chunks_fail=$((csv_chunks_fail + 1))
            printf '  ✗ chunk %s..%s: HTTP %s\n' "$chunk_start" "$chunk_end" "$http_code"
        fi

        chunk_start=$(( chunk_end + 1 ))
        sleep 2
    done

    _simut_log "  → CSV: $csv_chunks_ok chunks OK, $csv_chunks_fail FAIL, total $csv_total_bytes B ($((csv_total_bytes/1024)) KB)"
    simut_logout
    echo ""
fi

# ─── Phase 6: Restore ────────────────────────────────────────────────────────
if [[ $SKIP_RESTORE -eq 0 && $SKIP_BACKUP -eq 0 ]]; then
    _simut_log "PHASE 6/6: Restore FS (delete-extras pra limpar stress files)"
    "$SCRIPT_DIR/restore_fs.sh" "$BACKUP_DIR" --delete-extras || _simut_warn "  Restore reportou falhas — verifique manualmente"
    echo ""
fi

# ─── Final report ────────────────────────────────────────────────────────────
_simut_log "════════════════════════════════════════════════════════════════"
_simut_log "  RELATÓRIO FINAL"
_simut_log "════════════════════════════════════════════════════════════════"
[[ $SKIP_BACKUP -eq 0 ]]   && _simut_log "  ✓ Backup:    $BACKUP_DIR"
_simut_log "  ✓ Generate:  $GEN_FILES_COUNT arquivos / $GEN_TOTAL_BYTES B"
[[ $SKIP_UPLOAD -eq 0 ]]   && _simut_log "  ✓ Upload:    $UP_OK OK / $UP_FAIL FAIL em ${UP_DUR}s"
[[ $SKIP_DRAIN -eq 0 ]]    && _simut_log "  ✓ Drain:     telSent=$last_sent (CSV: $DRAIN_CSV)"
[[ $SKIP_CSV -eq 0 ]]      && _simut_log "  ✓ CSV:       $csv_chunks_ok chunks / $csv_total_bytes B"
[[ $SKIP_RESTORE -eq 0 ]]  && _simut_log "  ✓ Restore:   FS recuperado"
_simut_log "  ✓ Logs:      $LOG_FILE"
_simut_log "════════════════════════════════════════════════════════════════"

exit 0
