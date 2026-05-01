#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Teste de desempenho geral do servidor SIMUT.
# Mede:
#   1. Latência de páginas HTML
#   2. Latência de APIs leves (status, perms, lang, history_days)
#   3. Latência de /api/history_multi por range (1h..7d)
#   4. Throughput de /api/export/history.bin (KB/s)
#   5. Concorrência (1, 2, 3 requests simultâneos)
# Reporta avg/p95/min/max + flags se algo sai do esperado.
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"
command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio"; exit 2; }

simut_login || exit 1

# Helpers
percentile() {
    # uso: echo "$values" | percentile <p>  (p=50,95,etc)
    python3 -c "
import sys
v = sorted(int(x) for x in sys.stdin.read().split() if x.isdigit())
if not v: print(0); exit()
p = int('$1')
i = max(0, min(len(v)-1, int(len(v)*p/100)))
print(v[i])
"
}

measure_endpoint() {
    # uso: measure_endpoint <label> <path> <trials>
    local label=$1 path=$2 trials=$3
    local times=""
    local fails=0
    local total_bytes=0
    for t in $(seq 1 $trials); do
        local START=$(date +%s%N)
        local SIZE
        SIZE=$(curl -s -b "$COOKIE_JAR" --connect-timeout 5 --max-time 30 \
                    -o /dev/null -w '%{size_download}' \
                    "$SIMUT_BASE$path" 2>/dev/null)
        local END=$(date +%s%N)
        if [[ -z "$SIZE" || "$SIZE" -le 0 ]]; then
            fails=$((fails+1))
        else
            local MS=$(( (END - START) / 1000000 ))
            times="$times $MS"
            total_bytes=$((total_bytes + SIZE))
        fi
        sleep 0.3
    done
    local avg=$(python3 -c "
v = [int(x) for x in '$times'.split() if x]
print(sum(v)//len(v) if v else 0)")
    local p95=$(echo "$times" | tr ' ' '\n' | percentile 95)
    local mn=$(echo "$times" | tr ' ' '\n' | percentile 0)
    local mx=$(echo "$times" | tr ' ' '\n' | percentile 100)
    local avg_kb=$(( (total_bytes / (trials - fails > 0 ? trials - fails : 1)) / 1024 ))
    local kbps=0
    if [[ $avg -gt 0 ]]; then
        kbps=$(( total_bytes / (trials - fails > 0 ? trials - fails : 1) * 8 / avg ))
    fi
    printf "%-32s avg=%-6s p95=%-6s min=%-6s max=%-6s kb/req=%-5s kbps=%-5s fail=%d\n" \
        "$label" "${avg}ms" "${p95}ms" "${mn}ms" "${mx}ms" "${avg_kb}" "${kbps}" "$fails"
    echo "$label|$avg|$p95|$mn|$mx|$avg_kb|$kbps|$fails" >> "$RESULTS"
}

# Heap inicial
hdr "Estado inicial"
heap=$(curl -s -b "$COOKIE_JAR" "$SIMUT_BASE/api/status" | python3 -c "
import sys,json
d = json.load(sys.stdin); s = d.get('sys',{}); m = d.get('metr',{})
print(f\"heap_f={s.get('heap_f',0)} heap_lb={s.get('heap_lb',0)} lbm={m.get('lbm',0)} uptime={s.get('uptime',0)}s\")
")
info "$heap"

RESULTS=$(mktemp /tmp/perf_XXXXXX.tsv)
echo "label|avg|p95|min|max|kb_req|kbps|fail" > "$RESULTS"

# ============================================================
hdr "1. Páginas HTML (TTFB + body completo)"
# ============================================================
measure_endpoint "GET /history" "/history" 5
measure_endpoint "GET /dash"    "/" 5
measure_endpoint "GET /config"  "/config" 5
measure_endpoint "GET /lang.js" "/lang.js" 3

# ============================================================
hdr "2. APIs leves (esperado <200ms)"
# ============================================================
measure_endpoint "GET /api/status"        "/api/status" 10
measure_endpoint "GET /api/perms"         "/api/perms" 5
measure_endpoint "GET /api/history_days"  "/api/history_days" 5
measure_endpoint "GET /api/sec_status"    "/api/sec_status" 5
measure_endpoint "GET /api/lang"          "/api/lang" 3

# ============================================================
hdr "3. /api/history_multi por range (1 sensor)"
# ============================================================
NOW=$(date -u +%s)
measure_endpoint "history_multi range=0 (1h)"  "/api/history_multi?sensors=-1&range=0&end=$NOW" 5
measure_endpoint "history_multi range=2 (24h)" "/api/history_multi?sensors=-1&range=2&end=$NOW" 5
measure_endpoint "history_multi range=3 (7d)"  "/api/history_multi?sensors=-1&range=3&end=$NOW" 3
measure_endpoint "history_multi range=4 (1M)"  "/api/history_multi?sensors=-1&range=4&end=$NOW" 3
measure_endpoint "history_multi range=6 (MAX)" "/api/history_multi?sensors=-1&range=6&end=$NOW" 3

# Multi-sensor (3 sensores)
measure_endpoint "history_multi 3 sensors 24h" "/api/history_multi?sensors=-1,0,5&range=2&end=$NOW" 3

# ============================================================
hdr "4. /api/export/history.bin throughput"
# ============================================================
measure_endpoint "export 6h"  "/api/export/history.bin?from=$((NOW-21600))&to=$NOW" 3
measure_endpoint "export 24h" "/api/export/history.bin?from=$((NOW-86400))&to=$NOW" 3
measure_endpoint "export 3d"  "/api/export/history.bin?from=$((NOW-259200))&to=$NOW" 3

# ============================================================
hdr "5. /api/logs"
# ============================================================
measure_endpoint "GET /api/logs" "/api/logs" 3

# ============================================================
hdr "6. Concorrência (3 requests paralelos)"
# ============================================================
parallel_test() {
    local n=$1
    local pids=()
    local results=()
    for ((i=0; i<n; i++)); do
        (
            local s=$(date +%s%N)
            local hc=$(curl -s -b "$COOKIE_JAR" --connect-timeout 5 --max-time 30 \
                          -o /dev/null -w '%{http_code}' \
                          "$SIMUT_BASE/api/history_multi?sensors=-1&range=2&end=$NOW")
            local e=$(date +%s%N)
            echo "$hc $(( (e - s) / 1000000 ))" > "/tmp/par_$$_$i"
        ) &
        pids+=($!)
    done
    for p in "${pids[@]}"; do wait "$p"; done
    local sum=0; local oks=0; local errs=0
    for ((i=0; i<n; i++)); do
        local out=$(cat "/tmp/par_$$_$i" 2>/dev/null)
        local hc=$(echo "$out" | awk '{print $1}')
        local ms=$(echo "$out" | awk '{print $2}')
        if [[ "$hc" == "200" ]]; then
            oks=$((oks+1))
            sum=$((sum+ms))
        else
            errs=$((errs+1))
            info "  paralelo[$i]: HTTP $hc"
        fi
        rm -f "/tmp/par_$$_$i"
    done
    local avg=$((oks > 0 ? sum / oks : 0))
    printf "%-30s n=%d ok=%d err=%d avg_ms=%d\n" "history_multi 24h x$n paralelo" "$n" "$oks" "$errs" "$avg"
    echo "concurrent_${n}|${avg}|0|0|0|0|0|${errs}" >> "$RESULTS"
}

parallel_test 1
parallel_test 2
parallel_test 3

# ============================================================
hdr "Estado final"
# ============================================================
heap_end=$(curl -s -b "$COOKIE_JAR" "$SIMUT_BASE/api/status" | python3 -c "
import sys,json
d = json.load(sys.stdin); s = d.get('sys',{}); m = d.get('metr',{})
print(f\"heap_f={s.get('heap_f',0)} heap_lb={s.get('heap_lb',0)} lbm={m.get('lbm',0)}\")
")
info "$heap_end"

# ============================================================
hdr "AVALIAÇÃO"
# ============================================================
python3 <<PYEOF
rows = []
with open("$RESULTS") as f:
    next(f)
    for line in f:
        parts = line.strip().split("|")
        if len(parts) < 8: continue
        rows.append({
            'label': parts[0], 'avg': int(parts[1]), 'p95': int(parts[2]),
            'min': int(parts[3]), 'max': int(parts[4]),
            'kb': int(parts[5]), 'kbps': int(parts[6]), 'fail': int(parts[7]),
        })

flags = []
print()
# 1. APIs leves devem ser <200ms
for r in rows:
    if r['label'].startswith('GET /api/') and 'history_multi' not in r['label'] and 'logs' not in r['label']:
        if r['avg'] > 200:
            flags.append(f"⚠ {r['label']} avg={r['avg']}ms (>200ms esperado p/ APIs leves)")

# 2. Falhas: nenhuma deveria falhar
for r in rows:
    if r['fail'] > 0:
        flags.append(f"⚠ {r['label']}: {r['fail']} falha(s)")

# 3. p95 vs avg: variancia alta
for r in rows:
    if r['avg'] > 0 and r['p95'] > r['avg'] * 2.5:
        flags.append(f"⚠ {r['label']} p95={r['p95']}ms vs avg={r['avg']}ms (variância alta — concorrência?)")

# 4. Throughput de export
exp_24h = next((r for r in rows if r['label'] == 'export 24h'), None)
exp_3d = next((r for r in rows if r['label'] == 'export 3d'), None)
if exp_24h and exp_3d:
    print(f"Export throughput: 24h={exp_24h['kbps']} kbps, 3d={exp_3d['kbps']} kbps")
if exp_3d and exp_3d['avg'] > 9000:
    flags.append(f"⚠ export 3d avg={exp_3d['avg']}ms — beira do WEB_LONG_HANDLER_DEADLINE_MS=10000ms")

# 5. Concorrência
con1 = next((r for r in rows if r['label'] == 'concurrent_1'), None)
con3 = next((r for r in rows if r['label'] == 'concurrent_3'), None)
if con1 and con3:
    if con3['fail'] >= 2:
        print(f"\nConcorrência: x1 ok, x3 com {con3['fail']} falhas — _inHistoryHandler atomic guard ATIVO (esperado)")
    if con3['avg'] > con1['avg'] * 2:
        flags.append(f"⚠ concorrência: 3x mais lento ({con3['avg']}ms) que 1x ({con1['avg']}ms)")

# 6. /history page load
hist = next((r for r in rows if r['label'] == 'GET /history'), None)
if hist and hist['avg'] > 1000:
    flags.append(f"⚠ /history page load avg={hist['avg']}ms — gzipped pode ser melhorado")

if flags:
    print()
    for f in flags:
        print(f)
else:
    print("\nNenhum flag — sistema dentro do esperado para todas as métricas.")
PYEOF

rm -f "$RESULTS"
