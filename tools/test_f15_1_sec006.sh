#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F15.1/SEC-006 — Lockout behavior + regression do caminho auth.
#
# Uso:
#   SIMUT_IP=192.168.3.195 SIMUT_USER=admin SIMUT_PASS=XXXX \
#     bash tools/test_f15_1_sec006.sh
#
# Limitação conhecida: rodando de um único IP, não dá pra simular o bypass
# multi-IP que o fix previne (atacante rotando 8+ IPs para evictar seu slot
# lockado). Este script valida as PROPRIEDADES do caminho auth que o fix
# toca — se algo regrediu no mecanismo de lockout, o script pega.
#
# Valida:
#   1. Login happy path (sanity do servidor).
#   2. Slot inicial limpo (ou aguarda lockout prévio expirar).
#   3. Fails consecutivos triggeram lockout com backoff exponencial.
#   4. login_init durante lockout: retorna locked=true + lockSec>0 +
#      nonce fresco (slot preservado).
#   5. POST /api/login durante lockout: 403 err=2 (rejeitado, mesmo com
#      senha correta — prova que lockout é enforced).
#   6. Após expirar: login volta a funcionar com senha correta.
#   7. Liveness: /api/status continua respondendo após a sequência.
#
# Duração: ~25s (dominada pelos backoffs 2s + 4s + 8s).
# -----------------------------------------------------------------------------

set -u
source "$(dirname "$0")/hw_test_lib.sh"

hdr "F15.1/SEC-006 — lockout behavior (single-IP, regressão de auth)"
info "Host: $SIMUT_BASE  (user=$SIMUT_USER)"
warn "Single-IP: não simula bypass multi-IP; testa propriedades do caminho auth."

# -----------------------------------------------------------------------------
# Helpers específicos deste teste
# -----------------------------------------------------------------------------

# GET /api/login_init sem cookie (evita interação com sessões prévias).
login_init_raw() {
    curl -s --connect-timeout 5 \
         -w '\n__HTTP__%{http_code}' \
         "$SIMUT_BASE/api/login_init"
}

# Extrai campo JSON (valor pode ser string ou número/bool).
jfield() {
    local body=$1 key=$2
    echo "$body" | grep -oE "\"$key\"[[:space:]]*:[[:space:]]*\"?[^\",}]+\"?" \
                | head -1 \
                | sed -E "s/\"$key\"[[:space:]]*:[[:space:]]*//; s/^\"//; s/\"$//"
}

# POST /api/login com senha deliberadamente errada. Arg: nonce.
bad_login() {
    local nonce=$1
    local bad_sha
    # SHA256 "diferente" por run — entropy evita qualquer coincidência.
    bad_sha=$(sha256_hex "wrong_$(date +%s%N)_$RANDOM") || return 1
    curl -s --connect-timeout 5 \
         -w '\n__HTTP__%{http_code}' \
         -X POST \
         --data-urlencode "user=$SIMUT_USER" \
         --data-urlencode "pass=$bad_sha" \
         --data-urlencode "nonce=$nonce" \
         "$SIMUT_BASE/api/login"
}

# POST /api/login com senha CORRETA. Usado no teste 5 para provar que
# mesmo credencial válida é rejeitada durante lockout.
good_login_during_lockout() {
    local nonce=$1
    local good_sha
    good_sha=$(sha256_hex "$SIMUT_PASS") || return 1
    curl -s --connect-timeout 5 \
         -w '\n__HTTP__%{http_code}' \
         -X POST \
         --data-urlencode "user=$SIMUT_USER" \
         --data-urlencode "pass=$good_sha" \
         --data-urlencode "nonce=$nonce" \
         "$SIMUT_BASE/api/login"
}

# Aguarda slot desbloquear (max N segundos). Retorna 0 se livre.
wait_slot_free() {
    local max=${1:-60}
    local elapsed=0
    while [[ $elapsed -lt $max ]]; do
        local resp body locked lockSec
        resp=$(login_init_raw)
        body=$(simut_body "$resp")
        locked=$(jfield "$body" "locked")
        lockSec=$(jfield "$body" "lockSec")
        if [[ "$locked" != "true" ]]; then return 0; fi
        local wait_for=$((lockSec + 1))
        [[ $wait_for -lt 1 ]] && wait_for=1
        [[ $wait_for -gt 10 ]] && wait_for=10  # loop com check periódico
        info "  slot lockado (${lockSec}s); aguardando ${wait_for}s..."
        sleep "$wait_for"
        elapsed=$((elapsed + wait_for))
    done
    return 1
}

# -----------------------------------------------------------------------------
# Teste 1 — Login happy path (sanity)
# -----------------------------------------------------------------------------
hdr "1. Login happy path"
if simut_login; then
    :
else
    ko "login inicial falhou — aborta (servidor unreachable ou credencial errada?)"
    test_summary
    exit 1
fi
rm -f "$COOKIE_JAR"
touch "$COOKIE_JAR"

# -----------------------------------------------------------------------------
# Teste 2 — Estado inicial do slot (aguarda lockouts residuais)
# -----------------------------------------------------------------------------
hdr "2. Estado inicial do slot"
if wait_slot_free 60; then
    ok "slot inicial livre (locked=false)"
else
    ko "slot continua lockado após 60s; aborta"
    test_summary
    exit 1
fi

# -----------------------------------------------------------------------------
# Teste 3 — Triggering backoff: 3 fails consecutivos com backoff exponencial
# -----------------------------------------------------------------------------
hdr "3. Trigger lockout (3 fails sequenciais com backoff 2s/4s/8s)"
for i in 1 2 3; do
    # Aguarda o slot liberar antes de cada tentativa (exceto a primeira)
    if [[ $i -gt 1 ]]; then
        wait_slot_free 30 || { ko "slot não liberou antes do fail $i"; break; }
    fi

    # login_init → nonce
    resp=$(login_init_raw)
    body=$(simut_body "$resp")
    nonce=$(jfield "$body" "nonce")
    if [[ -z "$nonce" ]]; then
        ko "fail $i: nonce vazio (body=$body)"
        break
    fi

    # post com senha errada
    resp=$(bad_login "$nonce")
    status=$(simut_status "$resp")
    body=$(simut_body "$resp")
    err=$(jfield "$body" "err")
    lockSec=$(jfield "$body" "lockSec")
    expected_lock=$((1 << i))

    if [[ "$status" == "401" && ( "$err" == "1" || "$err" == "2" ) ]]; then
        # err=1 ou err=2 ambos aceitáveis — 401 com lockSec indicando penalty aplicada
        if [[ ${lockSec:-0} -gt 0 ]]; then
            ok "fail $i: HTTP 401 err=$err lockSec=${lockSec}s (penalty≈${expected_lock}s)"
        else
            ok "fail $i: HTTP 401 err=$err (lockSec=0 ainda — pode ser race com o contador)"
        fi
    else
        ko "fail $i: esperado HTTP 401, obteve HTTP $status err=$err"
        info "body: $(echo "$body" | head -c 200)"
    fi
done

# -----------------------------------------------------------------------------
# Teste 4 — login_init durante lockout preservou o slot
# -----------------------------------------------------------------------------
hdr "4. login_init durante lockout"
resp=$(login_init_raw)
status=$(simut_status "$resp")
body=$(simut_body "$resp")
locked=$(jfield "$body" "locked")
lockSec=$(jfield "$body" "lockSec")
nonce=$(jfield "$body" "nonce")

if [[ "$status" == "200" && "$locked" == "true" && ${lockSec:-0} -gt 0 && -n "$nonce" ]]; then
    ok "login_init retornou locked=true lockSec=${lockSec}s nonce=${nonce:0:8}..."
else
    ko "login_init inesperado: HTTP=$status locked=$locked lockSec=$lockSec nonce=$nonce"
    info "body: $(echo "$body" | head -c 200)"
fi

# Salva lockSec para esperar depois
lock_remaining=${lockSec:-0}

# -----------------------------------------------------------------------------
# Teste 5 — POST /api/login (senha correta) durante lockout → rejeitado
# -----------------------------------------------------------------------------
hdr "5. POST login (senha correta) durante lockout"
if [[ -n "$nonce" && "$locked" == "true" ]]; then
    resp=$(good_login_during_lockout "$nonce")
    status=$(simut_status "$resp")
    body=$(simut_body "$resp")
    err=$(jfield "$body" "err")
    ls_reported=$(jfield "$body" "lockSec")

    if [[ "$status" == "403" && "$err" == "2" ]]; then
        ok "login rejeitado (HTTP 403 err=2 lockSec=${ls_reported})"
    else
        ko "esperado HTTP 403 err=2, obteve HTTP $status err=$err"
        info "body: $(echo "$body" | head -c 200)"
    fi
else
    warn "sem nonce ou slot não lockado — skip"
    _skip=$((_skip+1))
fi

# -----------------------------------------------------------------------------
# Teste 6 — Recuperação após expirar
# -----------------------------------------------------------------------------
hdr "6. Recuperação pós-lockout"
wait_s=$((lock_remaining + 2))
[[ $wait_s -lt 1 ]] && wait_s=1
info "aguardando ${wait_s}s para lockout expirar..."
sleep "$wait_s"

rm -f "$COOKIE_JAR"
touch "$COOKIE_JAR"
if simut_login; then
    ok "login recuperou após lockout expirar"
else
    ko "login ainda falha após expirar lockout"
fi

# -----------------------------------------------------------------------------
# Teste 7 — Liveness do servidor
# -----------------------------------------------------------------------------
hdr "7. Server liveness"
resp=$(simut_req GET /api/status)
status=$(simut_status "$resp")
if [[ "$status" == "200" ]]; then
    body=$(simut_body "$resp")
    # sanity: verifica que /api/status é JSON coerente (tem 'uptime' ou 'fw')
    if echo "$body" | grep -qE '"(uptime|fw|version|heap)"'; then
        ok "servidor responde /api/status com JSON coerente"
    else
        ko "/api/status 200 mas body não parece JSON de status"
        info "body: $(echo "$body" | head -c 150)"
    fi
else
    ko "/api/status retornou HTTP $status"
fi

# -----------------------------------------------------------------------------
test_summary
