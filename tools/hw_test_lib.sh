#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# SIMUT — biblioteca comum para scripts de teste em hardware.
# Source este arquivo em scripts de teste: `source "$(dirname "$0")/hw_test_lib.sh"`
#
# Variáveis de ambiente esperadas:
#   SIMUT_IP    — IP ou host:porta do dispositivo (ex.: 192.168.1.50, 192.168.1.50:80)
#   SIMUT_USER  — usuário admin (default: admin)
#   SIMUT_PASS  — senha em plaintext (default: admin)
#
# Requisitos: bash 4+, curl, openssl (ou sha256sum), jq opcional.
# -----------------------------------------------------------------------------

set -u

: "${SIMUT_IP:?Set SIMUT_IP=<host[:port]>}"
: "${SIMUT_USER:=admin}"
: "${SIMUT_PASS:=admin}"

# URL base: aceita SIMUT_IP com ou sem porta.
if [[ "$SIMUT_IP" == *":"* ]]; then
    SIMUT_BASE="http://$SIMUT_IP"
else
    SIMUT_BASE="http://$SIMUT_IP"
fi

# Cookie jar por-script (permite rodar vários testes em paralelo se necessário).
COOKIE_JAR="${COOKIE_JAR:-/tmp/simut_cookies_$$.txt}"
trap 'rm -f "$COOKIE_JAR"' EXIT

# -----------------------------------------------------------------------------
# Contadores e formatação
# -----------------------------------------------------------------------------
_pass=0
_fail=0
_skip=0

# cores se stdout for tty
if [[ -t 1 ]]; then
    C_OK=$'\e[32m'; C_FAIL=$'\e[31m'; C_WARN=$'\e[33m'; C_DIM=$'\e[2m'; C_RST=$'\e[0m'
else
    C_OK=''; C_FAIL=''; C_WARN=''; C_DIM=''; C_RST=''
fi

hdr()  { echo; echo "=== $* ==="; }
info() { echo "${C_DIM}·  $*${C_RST}"; }
ok()   { echo "${C_OK}✔${C_RST}  $*"; _pass=$((_pass+1)); }
ko()   { echo "${C_FAIL}✘${C_RST}  $*"; _fail=$((_fail+1)); }
warn() { echo "${C_WARN}!${C_RST}  $*"; }

# -----------------------------------------------------------------------------
# SHA-256 compatível com o frontend SIMUT.
#
# O JS embarcado usa `sha256(pass)` onde a função itera `charCodeAt(i)` — retorna
# o code-unit UTF-16 e hasheia cada char como 1 byte. Para strings Latin-1
# (cobre a maioria dos teclados ocidentais) isso corresponde a ISO-8859-1.
# Para chars com codepoint > 255, o JS faz `if(j>>8)return;` e retorna undefined,
# então senhas com (por exemplo) chineses/emoji NÃO funcionam no frontend —
# também aqui.
#
# Bash recebe a string em UTF-8; convertemos para ISO-8859-1 antes de hashear
# para bater com o JS. Se iconv falhar (char fora de Latin-1), warn e aborta.
# -----------------------------------------------------------------------------
sha256_hex() {
    local s=$1
    local bytes
    bytes=$(printf '%s' "$s" | iconv -f UTF-8 -t ISO-8859-1 2>/dev/null) || {
        warn "senha contém char fora de Latin-1 (UTF-16 codepoint >255) — frontend também rejeita"
        return 1
    }
    if command -v openssl >/dev/null 2>&1; then
        printf '%s' "$bytes" | openssl dgst -sha256 -r | awk '{print $1}'
    else
        printf '%s' "$bytes" | sha256sum | awk '{print $1}'
    fi
}

# -----------------------------------------------------------------------------
# simut_login — challenge-response: GET /api/login_init → POST /api/login
# Frontend envia SHA256(plaintext) como `pass`; servidor re-hashea com HMAC+salt+pepper.
# Resultado: cookie SIMUTSESS salvo em $COOKIE_JAR.
# -----------------------------------------------------------------------------
simut_login() {
    local init body nonce locked status pass_sha lockSec resp

    # 1) GET /api/login_init — com detecção de lockout e auto-espera curta.
    local attempts=0
    while : ; do
        init=$(curl -s --connect-timeout 5 -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
                   -w '\n__HTTP__%{http_code}' \
                   "$SIMUT_BASE/api/login_init") || {
            ko "login_init: curl falhou — verifique SIMUT_IP=$SIMUT_IP (URL base: $SIMUT_BASE)"
            return 1
        }
        status=$(echo "$init" | awk -F'__HTTP__' '/__HTTP__/{print $2}')
        body=$(echo "$init" | sed '/__HTTP__/d')

        if [[ "$status" != "200" ]]; then
            ko "login_init retornou HTTP $status: $body"
            return 1
        fi

        nonce=$(echo "$body" | grep -oE '"nonce":"[^"]+"' | cut -d'"' -f4)
        locked=$(echo "$body" | grep -oE '"locked":(true|false)' | cut -d: -f2)
        lockSec=$(echo "$body" | grep -oE '"lockSec":[0-9]+' | cut -d: -f2)

        if [[ -z "$nonce" ]]; then
            ko "nonce não extraído de: $body"
            return 1
        fi

        if [[ "$locked" != "true" ]]; then break; fi

        attempts=$((attempts+1))
        if [[ ${lockSec:-0} -le 30 && $attempts -le 5 ]]; then
            warn "lockout ativo (${lockSec}s restantes) — aguardando..."
            sleep $((lockSec + 1))
            continue
        fi
        ko "lockout ativo de ${lockSec}s — rode o CLI USB \`reset lockout\` ou aguarde"
        return 2
    done

    # 2) POST /api/login com SHA256 Latin-1 da senha (bate com JS do frontend).
    pass_sha=$(sha256_hex "$SIMUT_PASS") || return 1

    resp=$(curl -s --connect-timeout 5 -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
               -w '\n__HTTP__%{http_code}' \
               -X POST \
               --data-urlencode "user=$SIMUT_USER" \
               --data-urlencode "pass=$pass_sha" \
               --data-urlencode "nonce=$nonce" \
               "$SIMUT_BASE/api/login") || { ko "login POST: curl falhou"; return 1; }

    status=$(echo "$resp" | awk -F'__HTTP__' '/__HTTP__/{print $2}')
    body=$(echo "$resp" | sed '/__HTTP__/d')

    if [[ "$status" != "200" ]]; then
        local err
        err=$(echo "$body" | grep -oE '"err":[0-9]+' | cut -d: -f2)
        case "${err:-?}" in
            1) ko "login falhou (err=1): nonce inválido ou user/pass vazio" ;;
            2)
                local ls
                ls=$(echo "$body" | grep -oE '"lockSec":[0-9]+' | cut -d: -f2)
                ko "login falhou (err=2): senha incorreta; lockSec=${ls:-0}s"
                info "verifique SIMUT_USER='$SIMUT_USER' e SIMUT_PASS (não logando valor)"
                ;;
            3) ko "login falhou (err=3): limite de sessões concorrentes (3) atingido" ;;
            *) ko "login retornou HTTP $status: $body" ;;
        esac
        return 1
    fi
    if ! grep -q SIMUTSESS "$COOKIE_JAR" 2>/dev/null; then
        ko "cookie SIMUTSESS não recebido"
        return 1
    fi
    ok "login como $SIMUT_USER — sessão estabelecida"
    return 0
}

# -----------------------------------------------------------------------------
# simut_req — helper genérico para requests autenticados.
# Uso: simut_req METHOD PATH [curl-args...]
# Sai em stdout: body. Stderr: status code.
# -----------------------------------------------------------------------------
simut_req() {
    local method=$1; shift
    local path=$1; shift
    curl -s -b "$COOKIE_JAR" -c "$COOKIE_JAR" \
         -X "$method" \
         -w '\n__HTTP__%{http_code}' \
         "$@" \
         "$SIMUT_BASE$path"
}

# simut_status / simut_body — aceitam arg OU stdin (pipe-friendly)
simut_status() {
    if [[ $# -gt 0 ]]; then echo "$1"; else cat; fi \
        | awk -F'__HTTP__' '/__HTTP__/{print $2}'
}
simut_body() {
    if [[ $# -gt 0 ]]; then echo "$1"; else cat; fi \
        | sed '/__HTTP__/d'
}

# -----------------------------------------------------------------------------
# assert_status — verifica HTTP status code esperado
# Uso: assert_status "msg" $expected "$response"
# -----------------------------------------------------------------------------
assert_status() {
    local msg=$1 expected=$2 resp=$3
    local got
    got=$(simut_status "$resp")
    if [[ "$got" == "$expected" ]]; then
        ok "$msg (HTTP $got)"
    else
        ko "$msg (esperado HTTP $expected, obtido HTTP $got)"
        info "body: $(simut_body "$resp" | head -c 200)"
    fi
}

assert_contains() {
    local msg=$1 needle=$2 haystack=$3
    if echo "$haystack" | grep -qF -- "$needle"; then
        ok "$msg (contém '$needle')"
    else
        ko "$msg (não contém '$needle')"
        info "texto: $(echo "$haystack" | head -c 200)"
    fi
}

assert_not_contains() {
    local msg=$1 needle=$2 haystack=$3
    if echo "$haystack" | grep -qF -- "$needle"; then
        ko "$msg (não deveria conter '$needle')"
        info "texto: $(echo "$haystack" | head -c 200)"
    else
        ok "$msg (não contém '$needle')"
    fi
}

# -----------------------------------------------------------------------------
# test_summary — imprime total e sai com código adequado
# -----------------------------------------------------------------------------
test_summary() {
    echo
    local total=$((_pass+_fail+_skip))
    if [[ $_fail -eq 0 ]]; then
        echo "${C_OK}TODOS OS TESTES PASSARAM${C_RST}  (${_pass}/${total})"
        exit 0
    else
        echo "${C_FAIL}FALHAS${C_RST}  pass=${_pass} fail=${_fail} skip=${_skip} total=${total}"
        exit 1
    fi
}
