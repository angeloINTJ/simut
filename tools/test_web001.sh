#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# WEB-001 — escape JSON em /api/ls + regressão geral de GETs principais.
#
# Uso:
#   SIMUT_IP=192.168.3.195 SIMUT_USER=admin SIMUT_PASS=XXXX bash tools/test_web001.sh
#
# Valida:
#   1. /api/ls (raiz) retorna JSON válido com path+entries.
#   2. /api/ls?dir=/config idem; nomes sem byte de controle literal.
#   3. /api/ls?dir=/history idem; conta .bin files.
#   4. /api/ls?dir=/etc retorna 403 (path proibido).
#   5. /api/status, /api/network, /api/config, /api/perms retornam JSON
#      com campos esperados (inclui campos de F-NET-TIME: dns_auto,
#      ntp_enabled, now_epoch, t_int).
# -----------------------------------------------------------------------------

set -u
source "$(dirname "$0")/hw_test_lib.sh"

hdr "WEB-001 — auto-test /api/ls + regressão GETs"
info "Host: $SIMUT_BASE  (user=$SIMUT_USER)"

simut_login || { ko "login falhou — abortando"; test_summary; exit 1; }

# -----------------------------------------------------------------------------
# Helper: GET JSON + valida status + parse jq
# -----------------------------------------------------------------------------
# Args: <nome> <path> [status-esperado]
# Retorna 0 se ok e seta GLOB_BODY com o body parseado.
GLOB_BODY=""
json_get() {
    local name=$1 path=$2 expected=${3:-200}
    local resp st body attempt
    for attempt in 1 2; do
        resp=$(simut_req GET "$path")
        st=$(simut_status "$resp")
        if [[ "$st" != "$expected" ]]; then
            # Rate-limit ou erro transiente: aguarda e retry.
            [[ $attempt -eq 1 ]] && { sleep 0.6; continue; }
            ko "$name (status $st, esperado $expected; 2 tentativas)"
            return 1
        fi
        body=$(simut_body "$resp")
        if echo "$body" | jq -e . >/dev/null 2>&1; then
            GLOB_BODY=$body
            ok "$name (JSON ok, ${#body} bytes$([ $attempt -gt 1 ] && echo \", retry $attempt\"))"
            return 0
        fi
        # JSON inválido — possivelmente truncado por GC/HIST write. Retry.
        [[ $attempt -eq 1 ]] && { sleep 0.6; continue; }
        ko "$name (JSON inválido em 2 tentativas; tam=${#body}; últimos 80: ${body: -80})"
        return 1
    done
}

# Checa se algum entry.n contém byte de controle literal (0x00-0x1F ou 0x7F)
check_no_ctrl_in_names() {
    local body=$1 label=$2
    # Extrai todos os 'n' e testa com python (mais preciso que grep -P em bytes)
    local has_ctrl
    has_ctrl=$(echo "$body" | jq -r '.entries[].n' 2>/dev/null | python3 -c '
import sys
bad = []
for line in sys.stdin:
    line = line.rstrip("\n")
    for c in line:
        if ord(c) < 0x20 or ord(c) == 0x7F:
            bad.append(repr(line))
            break
print("\n".join(bad))
')
    if [[ -z "$has_ctrl" ]]; then
        ok "$label: nenhum byte de controle literal em entries[].n"
    else
        ko "$label: bytes de controle detectados em $(echo "$has_ctrl" | wc -l) nomes"
        echo "$has_ctrl" | head -3 | sed 's/^/    /'
    fi
}

# -----------------------------------------------------------------------------
# 1. /api/ls raiz
# -----------------------------------------------------------------------------
hdr "Teste 1 — /api/ls (raiz)"
if json_get "GET /api/ls" "/api/ls"; then
    echo "$GLOB_BODY" | jq -e '.path and (.entries | type == "array")' >/dev/null 2>&1 \
        && ok "resposta tem path e entries[]" \
        || ko "campos path/entries ausentes"
    local_count=$(echo "$GLOB_BODY" | jq '.entries | length')
    info "  entries na raiz: $local_count"
fi

# /api/ls tem rate-limit de 200ms — pausa entre chamadas consecutivas.
sleep 0.4

# -----------------------------------------------------------------------------
# 2. /api/ls dir=/config
# -----------------------------------------------------------------------------
hdr "Teste 2 — /api/ls?dir=/config (core WEB-001)"
if json_get "GET /api/ls?dir=/config" "/api/ls?dir=/config"; then
    check_no_ctrl_in_names "$GLOB_BODY" "/config"
    q_count=$(echo "$GLOB_BODY" | jq -r '.entries[].n' | grep -c '?' || true)
    if [[ "$q_count" -gt 0 ]]; then
        info "  $q_count nome(s) sanitizado(s) com '?' (arquivos legados com bytes ruins)"
    fi
fi

sleep 0.4

# -----------------------------------------------------------------------------
# 3. /api/ls dir=/history
# -----------------------------------------------------------------------------
hdr "Teste 3 — /api/ls?dir=/history"
if json_get "GET /api/ls?dir=/history" "/api/ls?dir=/history"; then
    check_no_ctrl_in_names "$GLOB_BODY" "/history"
    bin_count=$(echo "$GLOB_BODY" | jq -r '.entries[] | select(.t=="f" and (.n | endswith(".bin"))) | .n' | wc -l)
    info "  arquivos .bin em /history: $bin_count"
fi

sleep 0.4

# -----------------------------------------------------------------------------
# 4. /api/ls path proibido (regressão F12.1)
# -----------------------------------------------------------------------------
hdr "Teste 4 — path proibido (regressão)"
resp=$(simut_req GET "/api/ls?dir=/etc")
st=$(simut_status "$resp")
if [[ "$st" == "403" ]]; then
    ok "path /etc bloqueado (403)"
else
    ko "path /etc deveria retornar 403; retornou $st"
fi

# -----------------------------------------------------------------------------
# 5-8. Regressão GETs principais
# -----------------------------------------------------------------------------
hdr "Testes 5-8 — Regressão GETs principais"

json_get "GET /api/status"   "/api/status" && {
    echo "$GLOB_BODY" | jq -e '.sys' >/dev/null 2>&1 \
        && ok "/api/status tem .sys" || ko "/api/status sem campo sys"
}

json_get "GET /api/network"  "/api/network" && {
    for f in connected use_dhcp dns_auto ntp_enabled; do
        echo "$GLOB_BODY" | jq -e ".${f}" >/dev/null 2>&1 \
            && ok "/api/network tem .${f}" || ko "/api/network sem .${f}"
    done
}

json_get "GET /api/config"   "/api/config" && {
    for f in name tz t_int ntp_enabled now_epoch; do
        echo "$GLOB_BODY" | jq -e ".${f}" >/dev/null 2>&1 \
            && ok "/api/config tem .${f}" || ko "/api/config sem .${f}"
    done
}

json_get "GET /api/perms"    "/api/perms" && {
    echo "$GLOB_BODY" | jq -e '.perms and .version' >/dev/null 2>&1 \
        && ok "/api/perms tem .perms e .version" || ko "/api/perms campos faltando"
}

# -----------------------------------------------------------------------------
test_summary
