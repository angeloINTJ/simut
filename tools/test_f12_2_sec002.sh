#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F12.2 (SEC-002) — uploadDir replace("..") frágil
#
# Valida que o uploadDir rejeita `..`, `%` e variantes pós-encoding,
# em vez de tentar limpar com `String::replace()` não-recursivo.
#
# Uso:
#   SIMUT_IP=192.168.1.50 ./tools/test_f12_2_sec002.sh
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/hw_test_lib.sh"

hdr "F12.2 SEC-002 — uploadDir replace('..') frágil"

simut_login || exit 1

PAYLOAD=$(mktemp --suffix=.txt)
echo "payload-f12.2-$$" > "$PAYLOAD"
trap 'rm -f "$PAYLOAD"' EXIT

# -----------------------------------------------------------------------------
# TESTE 1 — uploadDir=/history/..
# Bypass clássico: `replace("..","")` antigo transformaria em `/history/`,
# que é aceito. Com rejeição, deve falhar.
# -----------------------------------------------------------------------------
hdr "Teste 1 — uploadDir=/history/.."
resp=$(simut_req POST "/api/upload" \
    -F "uploadDir=/history/.." \
    -F "file=@$PAYLOAD;filename=evil.bin")
assert_status "uploadDir com .. deve ser rejeitado" "400" "$resp"

# -----------------------------------------------------------------------------
# TESTE 2 — uploadDir='....' (bypass do replace não-recursivo)
# `replace("..","")` aplica uma vez em `....`, retorna `..`. Com rejeição,
# nunca entra no if de `startsWith("/")`.
# -----------------------------------------------------------------------------
hdr "Teste 2 — uploadDir='....' (bypass replace não-recursivo)"
resp=$(simut_req POST "/api/upload" \
    -F "uploadDir=...." \
    -F "file=@$PAYLOAD;filename=bypass.bin")
assert_status "uploadDir='....' deve ser rejeitado" "400" "$resp"

# -----------------------------------------------------------------------------
# TESTE 3 — uploadDir com percent-encoded `..`
# `%2e%2e` seria `..` após URL-decode. O servidor faz decode de arg HTTP,
# então `%2e%2e` vira `..` literal antes da checagem. Se não decodifica,
# o `%` nu é bloqueado pela segunda checagem.
# -----------------------------------------------------------------------------
hdr "Teste 3 — uploadDir com %2e%2e / %"
resp=$(simut_req POST "/api/upload" \
    --form-string "uploadDir=%2e%2e/config" \
    -F "file=@$PAYLOAD;filename=enc.bin")
assert_status "uploadDir com %2e%2e deve ser rejeitado" "400" "$resp"

resp=$(simut_req POST "/api/upload" \
    -F "uploadDir=/valid%but-suspicious" \
    -F "file=@$PAYLOAD;filename=pct.bin")
assert_status "uploadDir com % isolado deve ser rejeitado" "400" "$resp"

# -----------------------------------------------------------------------------
# TESTE 4 — uploadDir='/a/..' e '/a/../b' (paths compostos)
# -----------------------------------------------------------------------------
hdr "Teste 4 — uploadDir com .. embutido"
for bad in '/history/..' '/config/../data' '/a/../b/../c' '..' '../' '/..'; do
    resp=$(simut_req POST "/api/upload" \
        -F "uploadDir=$bad" \
        -F "file=@$PAYLOAD;filename=x$(date +%s%N).bin")
    s=$(simut_status "$resp")
    if [[ "$s" == "400" ]]; then
        ok "uploadDir '$bad' rejeitado"
    else
        ko "uploadDir '$bad' aceito (HTTP $s)"
    fi
done

# -----------------------------------------------------------------------------
# TESTE 5 (regressão) — uploadDir legítimos continuam funcionando
# -----------------------------------------------------------------------------
hdr "Teste 5 (regressão) — uploadDir válidos aceitos"
for good in '/' '/history' '/history/'; do
    FN="f12_2_ok_$$_$(date +%s%N).bin"
    resp=$(simut_req POST "/api/upload" \
        -F "uploadDir=$good" \
        -F "file=@$PAYLOAD;filename=$FN")
    s=$(simut_status "$resp")
    if [[ "$s" == "200" ]]; then
        ok "uploadDir '$good' aceito"
        # Calcula path final — replica a lógica do servidor
        if [[ "$good" == "/" ]]; then final="/$FN"
        elif [[ "$good" == */ ]]; then final="${good}${FN}"
        else final="$good/$FN"
        fi
        sleep 0.5
        simut_req POST "/api/delete" --data-urlencode "file=$final" >/dev/null
    else
        ko "uploadDir '$good' rejeitado (HTTP $s) — regressão!"
    fi
done

# -----------------------------------------------------------------------------
# TESTE 6 — uploadDir vazio + sem arg (defaults)
# -----------------------------------------------------------------------------
hdr "Teste 6 — uploadDir ausente OU vazio (default=/)"
# Sem arg
FN1="f12_2_default_$$_a.bin"
resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=$FN1")
assert_status "upload sem uploadDir (default /)" "200" "$resp"
sleep 0.5
simut_req POST "/api/delete" --data-urlencode "file=/$FN1" >/dev/null

# Arg vazio — após trim, vira "", depois startsWith checa — aceita como "/"
FN2="f12_2_default_$$_b.bin"
resp=$(simut_req POST "/api/upload" \
    -F "uploadDir=" \
    -F "file=@$PAYLOAD;filename=$FN2")
s=$(simut_status "$resp")
# Aceita tanto 200 (tratado como /) quanto 400 (rejeitado como inválido)
if [[ "$s" == "200" || "$s" == "400" ]]; then
    ok "uploadDir vazio (HTTP $s — 200 ok, 400 também ok)"
    if [[ "$s" == "200" ]]; then
        sleep 0.5
        simut_req POST "/api/delete" --data-urlencode "file=/$FN2" >/dev/null
    fi
else
    ko "uploadDir vazio retornou HTTP $s inesperado"
fi

test_summary
