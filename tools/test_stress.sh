#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Stress test sequencial — todos os tipos de download CSV em rajada.
# Monitora entre cada operação:
#   - uptime (detecta reboots: cai = WDT/crash)
#   - heap_f / heap_lb / lbm (detecta leak ou fragmentação progressiva)
#   - ping (detecta travamento de rede)
#   - HTTP status / tempo / CRC32
#
# Gera stress_report_TIMESTAMP.md com timeline + flags + análise.
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"
command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio"; exit 2; }

simut_login || exit 1

REPORT="$(dirname "$0")/../stress_report_$(date +%Y%m%d_%H%M%S).md"
EVENTS=$(mktemp /tmp/stevt_XXXXXX.tsv)
echo -e "step\tlabel\thttp\ttime_ms\tbytes\tcrc\theap_f\theap_lb\tlbm\tuptime\tflags" > "$EVENTS"

INITIAL_UPTIME=0
INITIAL_HEAP_LB=0
LAST_UPTIME=0
LAST_HEAP_LB=0
STEP=0

# -----------------------------------------------------------------------------
# Coleta estado do device — uptime, heap, etc.
# -----------------------------------------------------------------------------
get_state() {
    curl -s -b "$COOKIE_JAR" --connect-timeout 5 --max-time 8 \
         "$SIMUT_BASE/api/status" 2>/dev/null | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    s = d.get('sys', {}); m = d.get('metr', {})
    print(f\"{s.get('uptime',0)}|{s.get('heap_f',0)}|{s.get('heap_lb',0)}|{m.get('lbm',0)}\")
except Exception:
    print('0|0|0|0')
" 2>/dev/null || echo "0|0|0|0"
}

# -----------------------------------------------------------------------------
# Executa step: GET URL, valida, registra. Detecta CRC se kind != 'page'.
# Args: label url kind(simx|page|json) trial
# -----------------------------------------------------------------------------
run_step() {
    local label="$1" url="$2" kind="$3" trial="$4"
    STEP=$((STEP+1))
    local TMP=$(mktemp /tmp/stbody_XXXXXX.bin)
    local START=$(date +%s%N)
    local HC
    HC=$(curl -s -b "$COOKIE_JAR" --connect-timeout 5 --max-time 60 \
              -o "$TMP" -w '%{http_code}' \
              "$SIMUT_BASE$url" 2>/dev/null) || HC="000"
    local END=$(date +%s%N)
    local MS=$(( (END - START) / 1000000 ))
    local SIZE=$(stat -c %s "$TMP" 2>/dev/null || echo 0)

    local CRC="-"
    if [[ "$kind" == "simx" && "$HC" == "200" ]]; then
        if python3 -c "
import sys, struct, zlib
b = open('$TMP', 'rb').read()
if len(b) < 36: sys.exit(2)
exp = struct.unpack('<I', b[-4:])[0]
calc = zlib.crc32(b[:-4]) & 0xFFFFFFFF
sys.exit(0 if exp == calc else 1)
" 2>/dev/null; then CRC="OK"; else CRC="FAIL"; fi
    fi
    rm -f "$TMP"

    # Estado pós-step (com tolerância — se device caiu, get_state pode falhar)
    local STATE
    STATE=$(get_state)
    local UP HF HLB LBM
    IFS='|' read -r UP HF HLB LBM <<< "$STATE"

    # Flags
    local FLAGS=""
    if [[ "$HC" == "000" ]]; then FLAGS="${FLAGS}TIMEOUT,"; fi
    if [[ "$HC" =~ ^5 ]]; then FLAGS="${FLAGS}HTTP${HC},"; fi
    if [[ "$CRC" == "FAIL" ]]; then FLAGS="${FLAGS}CRC_FAIL,"; fi
    if [[ "$LAST_UPTIME" -gt 0 && "$UP" -gt 0 && "$UP" -lt "$LAST_UPTIME" ]]; then
        FLAGS="${FLAGS}REBOOT(${LAST_UPTIME}s→${UP}s),"
    fi
    if [[ "$LAST_HEAP_LB" -gt 0 && "$HLB" -gt 0 ]]; then
        local DIFF=$((LAST_HEAP_LB - HLB))
        if [[ "$DIFF" -gt 5000 ]]; then FLAGS="${FLAGS}HEAP_DROP(-${DIFF}B),"; fi
    fi

    # Cor
    local COL=$C_OK
    if [[ -n "$FLAGS" ]]; then COL=$C_FAIL; fi

    printf "%3d. %-40s ${COL}%-3s${C_RST} t=%-6s b=%-7s crc=%-4s hlb=%-6s up=%-7s %s\n" \
        "$STEP" "$label[$trial]" "$HC" "${MS}ms" "$SIZE" "$CRC" "$HLB" "${UP}s" "$FLAGS"

    echo -e "${STEP}\t${label}[${trial}]\t${HC}\t${MS}\t${SIZE}\t${CRC}\t${HF}\t${HLB}\t${LBM}\t${UP}\t${FLAGS}" >> "$EVENTS"

    if [[ "$UP" -gt 0 ]]; then
        if [[ "$INITIAL_UPTIME" -eq 0 ]]; then INITIAL_UPTIME=$UP; fi
        LAST_UPTIME=$UP
    fi
    if [[ "$HLB" -gt 0 ]]; then
        if [[ "$INITIAL_HEAP_LB" -eq 0 ]]; then INITIAL_HEAP_LB=$HLB; fi
        LAST_HEAP_LB=$HLB
    fi

    # Se TIMEOUT prolongado, espera por possível reboot
    if [[ "$HC" == "000" ]]; then
        info "  device pode estar reiniciando — aguardando até 30s..."
        for i in $(seq 1 15); do
            sleep 2
            local recheck
            recheck=$(curl -s --connect-timeout 2 -o /dev/null -w '%{http_code}' \
                "$SIMUT_BASE/" 2>/dev/null)
            if [[ "$recheck" == "302" || "$recheck" == "200" ]]; then
                info "  device voltou após $((i*2))s"
                # Re-login (cookie pode ter sido invalidado)
                simut_login >/dev/null 2>&1
                return
            fi
        done
        info "  device ainda fora após 30s — abortando"
        return 1
    fi
}

# -----------------------------------------------------------------------------
# Estado inicial
# -----------------------------------------------------------------------------
hdr "Estado inicial"
INIT=$(get_state)
IFS='|' read -r INITIAL_UPTIME INIT_HF INIT_HLB INIT_LBM <<< "$INIT"
LAST_UPTIME=$INITIAL_UPTIME
LAST_HEAP_LB=$INIT_HLB
INITIAL_HEAP_LB=$INIT_HLB
info "uptime=${INITIAL_UPTIME}s heap_f=${INIT_HF} heap_lb=${INIT_HLB} lbm=${INIT_LBM}"

NOW=$(date -u +%s)

# -----------------------------------------------------------------------------
# Sequência de stress (em rajada, sem pausa entre — replica uso real do front)
# -----------------------------------------------------------------------------

hdr "Bloco 1: history_multi todos os ranges (1 sensor)"
for range in 0 1 2 3 4 5 6; do
    run_step "history_multi r=$range (-1)" \
             "/api/history_multi?sensors=-1&range=${range}&end=${NOW}" \
             "json" 1
done

hdr "Bloco 2: history_multi 24h com multi-sensor (3, 5, 11 sensores)"
run_step "multi 3 sensors 24h" "/api/history_multi?sensors=-1,0,5&range=2&end=${NOW}" json 1
run_step "multi 5 sensors 24h" "/api/history_multi?sensors=-1,0,1,2,5&range=2&end=${NOW}" json 1
run_step "multi 11 sensors 24h" "/api/history_multi?sensors=-1,0,1,2,3,4,5,6,7,8,9&range=2&end=${NOW}" json 1
run_step "multi 11 sensors 7d" "/api/history_multi?sensors=-1,0,1,2,3,4,5,6,7,8,9&range=3&end=${NOW}" json 1

hdr "Bloco 3: export/history.bin tamanhos crescentes"
for span in 3600 21600 86400 259200; do
    run_step "export.bin span=${span}s" "/api/export/history.bin?from=$((NOW-span))&to=${NOW}" simx 1
done

hdr "Bloco 4: simulação chunked 7d (chunks 24h = 7 chunks)"
for ((i=7; i>0; i--)); do
    run_step "chunk_24h_d${i}" "/api/export/history.bin?from=$((NOW-i*86400))&to=$((NOW-(i-1)*86400))" simx 1
done

hdr "Bloco 5: simulação chunked 30d (chunks 24h = 30 chunks)"
for ((i=30; i>0; i--)); do
    run_step "chunk_30d_d${i}" "/api/export/history.bin?from=$((NOW-i*86400))&to=$((NOW-(i-1)*86400))" simx 1
done

hdr "Bloco 6: export/logs.bin todos os filtros"
for level in all err inf; do
    run_step "logs.bin level=$level 7d" "/api/export/logs.bin?from=$((NOW-7*86400))&to=${NOW}&level=${level}" simx 1
    run_step "logs.bin level=$level 30d" "/api/export/logs.bin?from=$((NOW-30*86400))&to=${NOW}&level=${level}" simx 1
done

hdr "Bloco 7: APIs leves intercaladas (vê se ainda responde)"
for i in 1 2 3; do
    run_step "/api/status" "/api/status" json $i
    run_step "/api/perms" "/api/perms" json $i
    run_step "/api/history_days" "/api/history_days" json $i
done

hdr "Bloco 8: rajada rápida (5x history_multi 24h sem pausa)"
for i in 1 2 3 4 5 ; do
    run_step "rajada history_multi 24h" "/api/history_multi?sensors=-1&range=2&end=${NOW}" json $i
done

# -----------------------------------------------------------------------------
# Estado final
# -----------------------------------------------------------------------------
hdr "Estado final"
FINAL=$(get_state)
IFS='|' read -r FIN_UP FIN_HF FIN_HLB FIN_LBM <<< "$FINAL"
info "uptime=${FIN_UP}s heap_f=${FIN_HF} heap_lb=${FIN_HLB} lbm=${FIN_LBM}"
TEST_DURATION=$((FIN_UP - INITIAL_UPTIME))
info "duração do teste: ${TEST_DURATION}s"

# -----------------------------------------------------------------------------
# Geração do relatório markdown
# -----------------------------------------------------------------------------
hdr "Gerando relatório"

python3 <<PYEOF > "$REPORT"
import csv, datetime
events = []
with open("$EVENTS") as f:
    r = csv.DictReader(f, delimiter='\t')
    for row in r:
        try:
            row['step'] = int(row['step'])
            row['time_ms'] = int(row['time_ms'])
            row['bytes'] = int(row['bytes'])
            row['heap_f'] = int(row['heap_f'])
            row['heap_lb'] = int(row['heap_lb'])
            row['lbm'] = int(row['lbm'])
            row['uptime'] = int(row['uptime'])
            events.append(row)
        except Exception:
            pass

now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
print(f"# Stress test report — {now}\n")
print(f"**Device:** {'$SIMUT_BASE'.replace('http://','')}")
print(f"**Total steps:** {len(events)}")
print(f"**Initial state:** uptime={$INITIAL_UPTIME}s heap_lb={$INITIAL_HEAP_LB}B")
print(f"**Final state:** uptime={$FIN_UP}s heap_lb={$FIN_HLB}B")
print(f"**Duration:** {$TEST_DURATION}s\n")

# Detecções
reboots = []
timeouts = []
crc_fails = []
http_5xx = []
heap_drops = []

prev_up = $INITIAL_UPTIME
prev_hlb = $INITIAL_HEAP_LB
for e in events:
    if 'TIMEOUT' in e['flags']: timeouts.append(e)
    if 'CRC_FAIL' in e['flags']: crc_fails.append(e)
    if e['http'].startswith('5'): http_5xx.append(e)
    if 'REBOOT' in e['flags']: reboots.append(e)
    if 'HEAP_DROP' in e['flags']: heap_drops.append(e)

print("## Eventos críticos\n")
print(f"- **Reboots detectados:** {len(reboots)}")
print(f"- **Timeouts (HTTP 000):** {len(timeouts)}")
print(f"- **CRC32 fails:** {len(crc_fails)}")
print(f"- **HTTP 5xx:** {len(http_5xx)}")
print(f"- **Heap drops > 5KB:** {len(heap_drops)}")

if reboots:
    print("\n### 🔴 REBOOTS\n")
    for e in reboots:
        print(f"- step {e['step']}: `{e['label']}` — flags: {e['flags']}")
if timeouts:
    print("\n### ⚠ TIMEOUTS\n")
    for e in timeouts:
        print(f"- step {e['step']}: `{e['label']}` — t={e['time_ms']}ms")
if crc_fails:
    print("\n### ⚠ CRC FAILS\n")
    for e in crc_fails:
        print(f"- step {e['step']}: `{e['label']}` — bytes={e['bytes']}")
if http_5xx:
    print("\n### ⚠ HTTP 5xx\n")
    for e in http_5xx:
        print(f"- step {e['step']}: `{e['label']}` — HTTP {e['http']}")
if heap_drops:
    print("\n### ⚠ HEAP DROPS > 5KB\n")
    for e in heap_drops:
        print(f"- step {e['step']}: `{e['label']}` — {e['flags']}")

# Tendência de heap
print("\n## Tendência de heap\n")
hlb_series = [(e['step'], e['heap_lb']) for e in events if e['heap_lb'] > 0]
if hlb_series:
    first_hlb = hlb_series[0][1]
    last_hlb = hlb_series[-1][1]
    diff = first_hlb - last_hlb
    print(f"- heap_lb: início={first_hlb}B, fim={last_hlb}B, delta={-diff:+d}B")
    if diff > 2000:
        print(f"- ⚠ **POSSÍVEL LEAK**: heap_lb caiu {diff}B durante o teste")
    elif diff < -2000:
        print(f"- ✓ heap_lb cresceu {-diff}B (compactou)")
    else:
        print("- ✓ heap_lb estável")

# Min/max time por bloco
print("\n## Tempo por step\n")
print("| step | label | http | t_ms | bytes | crc | heap_lb | uptime | flags |")
print("|---|---|---|---|---|---|---|---|---|")
for e in events:
    label = e['label'][:50]
    flags = e['flags'].rstrip(',') or '-'
    print(f"| {e['step']} | `{label}` | {e['http']} | {e['time_ms']} | {e['bytes']} | {e['crc']} | {e['heap_lb']} | {e['uptime']} | {flags} |")

# Análise por categoria
print("\n## Análise por categoria\n")
categs = {
    'history_multi': [e for e in events if 'history_multi r=' in e['label'] or 'multi ' in e['label']],
    'export.bin': [e for e in events if 'export' in e['label'] and 'logs' not in e['label']],
    'logs.bin': [e for e in events if 'logs.bin' in e['label']],
    'apis_leves': [e for e in events if e['label'].startswith('/api/') and 'history_multi' not in e['label']],
}
for cat, items in categs.items():
    if not items: continue
    times = [e['time_ms'] for e in items if e['time_ms'] > 0]
    fails = sum(1 for e in items if e['flags'])
    avg = sum(times)//len(times) if times else 0
    mx  = max(times) if times else 0
    print(f"- **{cat}**: {len(items)} steps, fails={fails}, avg={avg}ms, max={mx}ms")

print("\n---\nGerado por tools/test_stress.sh")
PYEOF

ok "relatório salvo em $REPORT"
info "abra com: less $REPORT"

rm -f "$EVENTS"
