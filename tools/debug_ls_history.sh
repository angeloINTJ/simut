#!/usr/bin/env bash
# Diagnóstico: /api/ls?dir=/history retorna JSON inválido.
# Mostra body completo, tamanho, últimos chars, e erro específico do jq.
#
# Uso:
#   SIMUT_IP=... SIMUT_USER=admin SIMUT_PASS='...' bash tools/debug_ls_history.sh

set -u
source "$(dirname "$0")/hw_test_lib.sh"

simut_login || { ko "login falhou"; exit 1; }
sleep 0.5

resp=$(simut_req GET "/api/ls?dir=/history")
st=$(simut_status "$resp")
body=$(simut_body "$resp")

echo "=== STATUS: $st ==="
echo "=== TAMANHO: ${#body} bytes ==="
echo
echo "=== PRIMEIROS 300 CHARS ==="
echo "${body:0:300}"
echo
echo "=== ÚLTIMOS 300 CHARS ==="
echo "${body: -300}"
echo
echo "=== JQ DIAGNÓSTICO ==="
if echo "$body" | jq -e . >/dev/null 2>/tmp/jq_err; then
    echo "JSON OK"
else
    echo "JSON INVÁLIDO:"
    cat /tmp/jq_err
    rm -f /tmp/jq_err
fi
echo
echo "=== ESTRUTURA BRUTA (últimos 500 bytes, hex) ==="
printf '%s' "${body: -500}" | xxd | tail -20
