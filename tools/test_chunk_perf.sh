#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Mede tempo/throughput de chunks vs duração do range para descobrir o
# "sweet spot" — maior chunk com 100% sucesso, e tempo total estimado para
# baixar 7d com cada tamanho.
#
# Faz 5 trials por tamanho com janelas DENTRO da janela de 7d (se houver
# dados gravados). Reporta tabela + recomendação.
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"
command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio"; exit 2; }

simut_login || exit 1

NOW=$(date -u +%s)
TARGET_TOTAL=$((7 * 86400))      # 7 dias = 604800s
TRIALS=5

# Pega heap atual antes do teste (para correlacao)
heap_info=$(curl -s -b "$COOKIE_JAR" "$SIMUT_BASE/api/status" | \
    python3 -c "import sys,json; d=json.load(sys.stdin); s=d.get('sys',{}); m=d.get('metr',{}); print(f\"heap_f={s.get('heap_f',0)} heap_lb={s.get('heap_lb',0)} lbm={m.get('lbm',0)}\")")
info "$heap_info"
echo

SIZES=(3600 7200 14400 21600 43200 86400 172800)
LABELS=("1h" "2h" "4h" "6h" "12h" "24h" "48h")

printf "%-6s %-8s %-12s %-13s %-13s %-13s %s\n" \
    "label" "secs" "chunks_7d" "ok/total" "avg_chunk_ms" "total_est_s" "kbps_avg"
printf "%-6s %-8s %-12s %-13s %-13s %-13s %s\n" \
    "-----" "----" "---------" "--------" "------------" "-----------" "--------"

RESULTS=$(mktemp /tmp/perfres_XXXXXX.tsv)
echo -e "label\tsecs\tn_chunks\tok\ttotal\tavg_ms\ttotal_est_s\tbytes_avg" > "$RESULTS"

for i in "${!SIZES[@]}"; do
    chunk_secs=${SIZES[i]}
    label=${LABELS[i]}
    n_chunks=$(( (TARGET_TOTAL + chunk_secs - 1) / chunk_secs ))

    OK=0; TOT_MS=0; TOT_BYTES=0
    fails=""

    for trial in $(seq 1 $TRIALS); do
        # Offset aleatorio dentro de 7d
        offset=$((RANDOM % (TARGET_TOTAL - chunk_secs - 1)))
        cFrom=$(( NOW - TARGET_TOTAL + offset ))
        cTo=$(( cFrom + chunk_secs ))
        TMP=$(mktemp /tmp/cp_XXXXXX.bin)

        START_NS=$(date +%s%N)
        HC=$(curl -s -b "$COOKIE_JAR" --connect-timeout 5 --max-time 30 \
                  -o "$TMP" -w '%{http_code}' \
                  "$SIMUT_BASE/api/export/history.bin?from=$cFrom&to=$cTo" 2>/dev/null)
        END_NS=$(date +%s%N)
        ELAPSED_MS=$(( (END_NS - START_NS) / 1000000 ))

        if [[ "$HC" == "200" ]]; then
            SIZE=$(stat -c %s "$TMP")
            if python3 -c "
import sys, struct, zlib
b = open('$TMP', 'rb').read()
if len(b) < 36: sys.exit(2)
exp = struct.unpack('<I', b[-4:])[0]
calc = zlib.crc32(b[:-4]) & 0xFFFFFFFF
sys.exit(0 if exp == calc else 1)" 2>/dev/null; then
                OK=$((OK+1))
                TOT_MS=$((TOT_MS + ELAPSED_MS))
                TOT_BYTES=$((TOT_BYTES + SIZE))
            else
                fails="${fails} crc(${ELAPSED_MS}ms)"
            fi
        else
            fails="${fails} HTTP$HC"
        fi
        rm -f "$TMP"
        sleep 1
    done

    if [[ $OK -gt 0 ]]; then
        AVG_MS=$((TOT_MS / OK))
        AVG_BYTES=$((TOT_BYTES / OK))
        # Tempo total estimado p/ 7d com pausa de 80ms entre chunks
        TOTAL_EST_MS=$(( n_chunks * (AVG_MS + 80) ))
        TOTAL_EST_S=$(( TOTAL_EST_MS / 1000 ))
        if [[ $AVG_MS -gt 0 ]]; then
            KBPS=$(( AVG_BYTES * 8 / AVG_MS ))
        else
            KBPS=0
        fi
    else
        AVG_MS=0; AVG_BYTES=0; TOTAL_EST_S=0; KBPS=0
    fi

    PASS_FRAC="${OK}/${TRIALS}"
    if [[ $OK -eq $TRIALS ]]; then COL=$C_OK
    elif [[ $OK -eq 0 ]]; then COL=$C_FAIL
    else COL=$C_WARN; fi

    printf "%-6s %-8s %-12s ${COL}%-13s${C_RST} %-13s %-13s %s\n" \
        "$label" "${chunk_secs}s" "$n_chunks" "$PASS_FRAC" "${AVG_MS}ms" "${TOTAL_EST_S}s" "${KBPS}kbps"

    echo -e "${label}\t${chunk_secs}\t${n_chunks}\t${OK}\t${TRIALS}\t${AVG_MS}\t${TOTAL_EST_S}\t${AVG_BYTES}" >> "$RESULTS"

    if [[ -n "$fails" ]]; then info "  falhas:${fails}"; fi
done

echo
echo "=== RECOMENDAÇÃO ==="
python3 <<PYEOF
import csv
rows = []
with open("$RESULTS") as f:
    for r in csv.DictReader(f, delimiter='\t'):
        rows.append({
            'label': r['label'], 'secs': int(r['secs']), 'n': int(r['n_chunks']),
            'ok': int(r['ok']), 'tot': int(r['total']),
            'ms': int(r['avg_ms']), 'total_s': int(r['total_est_s']),
            'bytes': int(r['bytes_avg']),
        })

# Filtra apenas com 100% OK
safe = [r for r in rows if r['ok'] == r['tot'] and r['ok'] > 0]

if not safe:
    print("Nenhum tamanho passou 100% — investigar conectividade")
else:
    # Sweet spot: menor total_est_s entre os 100% OK
    best = min(safe, key=lambda r: r['total_s'])
    largest = max(safe, key=lambda r: r['secs'])
    print(f"Maior chunk com 100% OK: {largest['label']} ({largest['secs']}s)")
    print(f"  tempo medio: {largest['ms']}ms, bytes/chunk: {largest['bytes']}")
    print(f"  tempo total p/ 7d: ~{largest['total_s']}s ({largest['total_s']/60:.1f}min, {largest['n']} chunks)")
    print()
    print(f"Sweet spot (menor tempo total p/ 7d): {best['label']} ({best['secs']}s)")
    print(f"  tempo medio: {best['ms']}ms, bytes/chunk: {best['bytes']}")
    print(f"  tempo total p/ 7d: ~{best['total_s']}s ({best['total_s']/60:.1f}min, {best['n']} chunks)")
    print()
    # Comparacao com fixo 6h (atual)
    cur = next((r for r in rows if r['secs'] == 21600), None)
    if cur and cur['ok'] > 0:
        speedup = cur['total_s'] / max(best['total_s'], 1)
        print(f"vs chunk fixo de 6h atual ({cur['total_s']}s p/ 7d): {speedup:.2f}x mais rapido com sweet spot")
PYEOF

rm -f "$RESULTS"
