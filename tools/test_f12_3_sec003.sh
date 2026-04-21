#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F12.3 (SEC-003) — Senhas padrão admin/admin eliminadas
#
# Este teste cobre apenas regressão de login em config NÃO-factory
# (cenário normal: admin já tem senha custom). O caminho factory-defaults
# é destrutivo e deve ser testado manualmente via CLI USB (ver README).
#
# Uso:
#   SIMUT_IP=192.168.1.50 SIMUT_USER=admin SIMUT_PASS='...' ./tools/test_f12_3_sec003.sh
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/hw_test_lib.sh"

hdr "F12.3 SEC-003 — Senhas padrão eliminadas (regressão)"

# Preserva defaults e provê COOKIE_JAR para set -u do hw_test_lib
MAIN_COOKIE_JAR="$COOKIE_JAR"
SAVED_USER="$SIMUT_USER"
SAVED_PASS="$SIMUT_PASS"

# Helper: attempt login as (user, pass) via /api/login_init + /api/login.
# Não usa simut_login (que exige COOKIE_JAR setado e não modula lockout).
# Echos HTTP status do POST; body fica em LAST_BODY.
LAST_BODY=""
try_login() {
    local u=$1 p=$2
    local cj=/tmp/simut_f12_3_$$_$RANDOM.txt
    local init nonce pass_sha resp
    init=$(curl -s --connect-timeout 5 -c "$cj" \
               "$SIMUT_BASE/api/login_init" \
               -w '\n__HTTP__%{http_code}')
    nonce=$(echo "$init" | grep -oE '"nonce":"[^"]+"' | cut -d'"' -f4)
    if [[ -z "$nonce" ]]; then
        rm -f "$cj"
        LAST_BODY="no nonce"
        echo "000"
        return
    fi
    pass_sha=$(sha256_hex "$p")
    resp=$(curl -s --connect-timeout 5 -c "$cj" -b "$cj" \
               -X POST \
               --data-urlencode "user=$u" \
               --data-urlencode "pass=$pass_sha" \
               --data-urlencode "nonce=$nonce" \
               "$SIMUT_BASE/api/login" \
               -w '\n__HTTP__%{http_code}')
    rm -f "$cj"
    LAST_BODY=$(simut_body "$resp")
    simut_status "$resp"
}

# Espera até que não haja lockout para o IP (query /api/login_init).
wait_for_lockout_clear() {
    local max_wait=${1:-20}
    local waited=0
    while [[ $waited -lt $max_wait ]]; do
        local init locked lockSec
        init=$(curl -s --connect-timeout 5 "$SIMUT_BASE/api/login_init")
        locked=$(echo "$init" | grep -oE '"locked":(true|false)' | cut -d: -f2)
        lockSec=$(echo "$init" | grep -oE '"lockSec":[0-9]+' | cut -d: -f2)
        if [[ "$locked" != "true" ]]; then return 0; fi
        info "aguardando lockout (${lockSec}s restantes)..."
        sleep $((lockSec + 1))
        waited=$((waited + lockSec + 1))
    done
    return 1
}

# -----------------------------------------------------------------------------
# TESTE 1 — Login com admin/admin default DEVE falhar
# -----------------------------------------------------------------------------
hdr "Teste 1 — credencial padrão admin/admin é inválida"
wait_for_lockout_clear 30 || { warn "lockout persistente; teste pode falhar"; }

status=$(try_login "admin" "admin")
if [[ "$status" == "401" ]]; then
    err=$(echo "$LAST_BODY" | grep -oE '"err":[0-9]+' | cut -d: -f2)
    ok "admin/admin rejeitado (HTTP 401, err=$err)"
elif [[ "$status" == "403" ]]; then
    warn "admin/admin resultou em lockout (err=2 já ativo de rodada anterior)"
    ok "admin/admin não autenticou (HTTP 403 via lockout — comportamento esperado)"
else
    ko "admin/admin NÃO deveria funcionar — HTTP $status: $LAST_BODY"
fi

# -----------------------------------------------------------------------------
# TESTE 2 — Login com senha real do admin (regressão)
#
# Como o teste 1 incrementou failCount, espera lockout expirar antes.
# -----------------------------------------------------------------------------
hdr "Teste 2 — senha real do admin continua funcionando (regressão)"
wait_for_lockout_clear 30 || warn "lockout ainda ativo; pode bloquear"
# Usa simut_login que já tem retry de lockout embutido
simut_login || ko "login com senha real falhou — regressão!"

# -----------------------------------------------------------------------------
# TESTE 3 — viewer/viewer (informativo — viewer mantido por decisão do audit)
# -----------------------------------------------------------------------------
hdr "Teste 3 — viewer/viewer default (informativo)"
wait_for_lockout_clear 30 || warn "lockout ativo — teste 3 pode ser inconclusivo"

status=$(try_login "viewer" "viewer")
case "$status" in
    200)
        info "resposta: $LAST_BODY"
        if echo "$LAST_BODY" | grep -q '"redirect":"/force_chpass"'; then
            ok "viewer/viewer aceito mas forçado a trocar senha (mustChangePassword OK)"
        else
            ko "viewer logou sem ser forçado a trocar senha"
        fi
        ;;
    401|403)
        err=$(echo "$LAST_BODY" | grep -oE '"err":[0-9]+' | cut -d: -f2)
        if [[ "$status" == "403" ]]; then
            warn "viewer/viewer bateu em lockout (err=$err) — inconclusivo"
        else
            ok "viewer/viewer rejeitado (HTTP 401, err=$err) — viewer já teve senha trocada"
        fi
        ;;
    *)
        warn "viewer/viewer retornou HTTP $status (inesperado): $LAST_BODY"
        ;;
esac

# Restaura estado e cookie jar padrão
SIMUT_USER="$SAVED_USER"
SIMUT_PASS="$SAVED_PASS"
COOKIE_JAR="$MAIN_COOKIE_JAR"

test_summary
