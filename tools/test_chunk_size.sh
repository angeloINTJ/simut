#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Mede taxa de sucesso de CRC32 em /api/export/history.bin vs duração do range
# Reporta tabela: duração | trials_ok/total | bytes_avg | time_ms_avg
# Para descobrir o "ponto de quebra" empiricamente e calibrar tamanho do
# chunk no frontend.
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"
command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio"; exit 2; }

simut_login || exit 1

NOW=$(date -u +%s)
TRIALS_PER_SIZE=3

# (segundos, label)
DURATIONS=(300 600 1800 3600 7200 21600 43200 86400 259200 604800 2592000)
LABELS=("5min" "10min" "30min" "1h" "2h" "6h" "12h" "24h" "3d" "7d" "30d")

printf "\n%-8s %-10s %-12s %-14s %-12s %s\n" "label" "duracao" "trials" "bytes_avg" "ms_avg" "kbps"
printf "%-8s %-10s %-12s %-14s %-12s %s\n" "-----" "-------" "------" "---------" "------" "----"

# Salva resultados para JSON final
RESULTS_FILE=$(mktemp /tmp/chunkres_XXXXXX.tsv)
echo -e "label\tduration\tok\ttotal\tbytes_avg\tms_avg" > "$RESULTS_FILE"

for i in "${!DURATIONS[@]}"; do
    d=${DURATIONS[i]}
    lbl=${LABELS[i]}
    OK=0
    TOT_BYTES=0
    TOT_TIME_MS=0
    FAIL_REASONS=""

    for trial in $(seq 1 $TRIALS_PER_SIZE); do
        FROM=$((NOW - d))
        if [[ $FROM -lt 0 ]]; then FROM=1; fi
        TMP=$(mktemp /tmp/csz_XXXXXX.bin)

        START_NS=$(date +%s%N)
        HC=$(curl -s -b "$COOKIE_JAR" --connect-timeout 5 --max-time 90 \
                  -o "$TMP" -w '%{http_code}' \
                  "$SIMUT_BASE/api/export/history.bin?from=$FROM&to=$NOW" 2>/dev/null)
        END_NS=$(date +%s%N)
        ELAPSED_MS=$(( (END_NS - START_NS) / 1000000 ))

        if [[ "$HC" == "200" ]]; then
            SIZE=$(stat -c %s "$TMP")
            # Valida CRC32: ultimos 4 bytes vs zlib.crc32(blob[:-4])
            if python3 -c "
import sys, struct, zlib
b = open('$TMP', 'rb').read()
if len(b) < 36: sys.exit(2)
exp = struct.unpack('<I', b[-4:])[0]
calc = zlib.crc32(b[:-4]) & 0xFFFFFFFF
sys.exit(0 if exp == calc else 1)" 2>/dev/null; then
                OK=$((OK+1))
                TOT_BYTES=$((TOT_BYTES + SIZE))
                TOT_TIME_MS=$((TOT_TIME_MS + ELAPSED_MS))
            else
                FAIL_REASONS="$FAIL_REASONS crc"
            fi
        else
            FAIL_REASONS="$FAIL_REASONS HTTP$HC"
        fi
        rm -f "$TMP"
        sleep 1   # respira entre trials para nao saturar o atomic guard
    done

    if [[ $OK -gt 0 ]]; then
        AVG_BYTES=$((TOT_BYTES / OK))
        AVG_MS=$((TOT_TIME_MS / OK))
        if [[ $AVG_MS -gt 0 ]]; then
            KBPS=$(( AVG_BYTES * 8 / AVG_MS ))
        else
            KBPS=0
        fi
    else
        AVG_BYTES=0; AVG_MS=0; KBPS=0
    fi

    PASS_FRAC="${OK}/${TRIALS_PER_SIZE}"
    if [[ $OK -eq $TRIALS_PER_SIZE ]]; then COL=$C_OK
    elif [[ $OK -eq 0 ]]; then COL=$C_FAIL
    else COL=$C_WARN; fi

    printf "%-8s %-10s ${COL}%-12s${C_RST} %-14s %-12s %s\n" \
        "$lbl" "${d}s" "$PASS_FRAC" "$AVG_BYTES" "${AVG_MS}ms" "${KBPS}kbps"

    echo -e "${lbl}\t${d}\t${OK}\t${TRIALS_PER_SIZE}\t${AVG_BYTES}\t${AVG_MS}" >> "$RESULTS_FILE"

    if [[ -n "$FAIL_REASONS" ]]; then
        info "  falhas:${FAIL_REASONS}"
    fi
done

echo
echo "=== Recomendação de chunk_size ==="
python3 <<PYEOF
import csv
rows = []
with open("$RESULTS_FILE") as f:
    r = csv.DictReader(f, delimiter='\t')
    for row in r:
        rows.append({
            'label': row['label'], 'd': int(row['duration']),
            'ok': int(row['ok']), 'tot': int(row['total']),
            'bytes': int(row['bytes_avg']), 'ms': int(row['ms_avg']),
        })

# Maior tamanho com 100% de sucesso
safe = [r for r in rows if r['ok'] == r['tot']]
if safe:
    largest_safe = max(safe, key=lambda r: r['d'])
    print(f"Maior duracao com 100% sucesso: {largest_safe['label']} "
          f"({largest_safe['d']}s = {largest_safe['bytes']} bytes em {largest_safe['ms']}ms)")
    # Recomendacao: usar metade do maior seguro como margem
    rec = largest_safe['d'] // 2
    if rec < 300: rec = 300
    print(f"Chunk_size recomendado: {rec}s ({rec/3600:.2f}h) — margem 50%")
else:
    print("Nenhuma duracao passou 100% — investigar conectividade")

# Primeira duracao com falha
fail = [r for r in rows if r['ok'] < r['tot']]
if fail:
    first_fail = min(fail, key=lambda r: r['d'])
    print(f"Primeira falha: {first_fail['label']} "
          f"({first_fail['d']}s, {first_fail['ok']}/{first_fail['tot']} sucesso)")
PYEOF

rm -f "$RESULTS_FILE"
