#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F12.1 (SEC-001) — Path traversal em upload.filename
#
# Valida que `isSafeUploadFilename` rejeita nomes maliciosos e que uploads
# legítimos continuam funcionando.
#
# Uso:
#   SIMUT_IP=192.168.1.50 ./tools/test_f12_1_sec001.sh
#   SIMUT_IP=192.168.1.50 SIMUT_USER=admin SIMUT_PASS=suasenha ./tools/test_f12_1_sec001.sh
#
# Rotas usadas (fonte: WebManager.cpp:73-120):
#   POST /api/upload   — multipart, handlers START/WRITE/END
#   POST /api/delete   — arg `file`
#   GET  /api/ls       — arg `dir`
#   GET  /api/status   — heap/status
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/hw_test_lib.sh"

hdr "F12.1 SEC-001 — Path traversal em upload.filename"

# Precisa de login como admin (PERM_FILE_UPLOAD)
simut_login || exit 1

# Payload mínimo
PAYLOAD=$(mktemp --suffix=.txt)
echo "payload-test-$$" > "$PAYLOAD"
trap 'rm -f "$PAYLOAD"' EXIT

# Baseline: /config/system.bin (se existir) — detecta sobrescrita por traversal
# Note: /api/ls usa JSON compacto — chave "n" (name), "t" (type), "s" (size).
baseline=$(simut_req GET "/api/ls?dir=/config")
config_size_before=$(simut_body "$baseline" | grep -oE '"n":"system.bin"[^}]*"s":[0-9]+' | grep -oE '[0-9]+$' | head -1)
info "baseline: /config/system.bin size=${config_size_before:-ausente}"

# -----------------------------------------------------------------------------
# TESTE 1 — path traversal clássico: ../config/system.bin
# -----------------------------------------------------------------------------
hdr "Teste 1 — filename=../config/system.bin"
resp=$(simut_req POST "/api/upload" \
    -F "uploadDir=/" \
    -F "file=@$PAYLOAD;filename=../config/system.bin")
assert_status "path traversal deve ser rejeitado" "400" "$resp"
assert_contains "body indica upload inválido" "Invalid upload" "$(simut_body "$resp")"

# Verifica que system.bin não foi sobrescrito — retry no /api/ls (rate-limit 200ms)
config_size_after=""
for wait in 0.5 1 2 3; do
    sleep "$wait"
    after=$(simut_req GET "/api/ls?dir=/config")
    config_size_after=$(simut_body "$after" | grep -oE '"n":"system.bin"[^}]*"s":[0-9]+' | grep -oE '[0-9]+$' | head -1)
    # Se baseline tinha size, aguarda encontrar de novo; se baseline era ausente, aceita primeiro resultado.
    if [[ -n "$config_size_before" && -n "$config_size_after" ]] || [[ -z "$config_size_before" ]]; then
        break
    fi
done
if [[ "$config_size_before" == "$config_size_after" ]]; then
    ok "/config/system.bin não foi sobrescrito (size=${config_size_after:-ausente})"
elif [[ -n "$config_size_before" && -z "$config_size_after" ]]; then
    warn "/api/ls não retornou system.bin após 4 retries — rate-limit? (baseline=$config_size_before)"
    info "inconclusivo; logs do SIMUT confirmam se há 'Upload rejeitado' para traversal"
else
    ko "/config/system.bin FOI alterado: $config_size_before → $config_size_after"
fi

# -----------------------------------------------------------------------------
# TESTE 2 — caracteres perigosos
# -----------------------------------------------------------------------------
hdr "Teste 2 — caracteres especiais"
for bad in 'a<b>c.txt' 'back\\slash.txt' 'colon:name.txt' 'pipe|name.txt' 'star*.txt' 'quest?.txt'; do
    resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=$bad")
    status=$(simut_status "$resp")
    if [[ "$status" == "400" ]]; then
        ok "filename '$bad' rejeitado"
    else
        ko "filename '$bad' aceito (HTTP $status)"
    fi
done

# -----------------------------------------------------------------------------
# TESTE 3 — filename vazio / longo / com .. embutido / controle
# -----------------------------------------------------------------------------
hdr "Teste 3 — filename vazio/longo/.. embutido/controle/encoded"
resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=")
assert_status "filename vazio" "400" "$resp"

LONG=$(printf 'a%.0s' {1..65}).txt
resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=$LONG")
assert_status "filename >64 chars" "400" "$resp"

resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=a..b.txt")
assert_status "filename com .. no meio" "400" "$resp"

CTRL_NAME=$(printf 'x\x01y.txt')
resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=$CTRL_NAME")
assert_status "filename com byte de controle" "400" "$resp"

# Bypass via percent-encoding: curl encoda '"' como %22; atacante real poderia
# mandar %2e%2e%2f (../). A blocklist de '%' impede qualquer encoding literal.
resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=%2e%2e%2fsystem.bin")
assert_status "filename com percent-encoding '..'" "400" "$resp"

resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=quote\".txt")
assert_status "filename com aspas (curl encoda %22)" "400" "$resp"

# -----------------------------------------------------------------------------
# TESTE 4 — REGRESSÃO: upload legítimo continua funcionando
# -----------------------------------------------------------------------------
hdr "Teste 4 (regressão) — upload de filename legítimo"
FN="f12_sec001_test_$$.csv"
echo "ts,val" > "$PAYLOAD"
echo "1,42" >> "$PAYLOAD"
resp=$(simut_req POST "/api/upload" \
    -F "uploadDir=/history" \
    -F "file=@$PAYLOAD;filename=$FN")
assert_status "upload legítimo para /history" "200" "$resp"
assert_contains "resposta ok" "ok" "$(simut_body "$resp")"

# Verifica que o arquivo aparece — retry com backoff (rate-limit 200ms + hiccups)
found=0
for wait in 0.5 1 2 3; do
    sleep "$wait"
    after=$(simut_req GET "/api/ls?dir=/history")
    if simut_body "$after" | grep -q "\"n\":\"$FN\""; then
        found=1
        break
    fi
done
if [[ $found -eq 1 ]]; then
    ok "/history/$FN presente no listing"
    simut_req POST "/api/delete" --data-urlencode "file=/history/$FN" >/dev/null
else
    ko "/history/$FN não apareceu no listing após 4 retries"
    info "último body: $(simut_body "$after" | head -c 300)"
fi

# -----------------------------------------------------------------------------
# TESTE 5 — REGRESSÃO: filenames variados válidos
# -----------------------------------------------------------------------------
hdr "Teste 5 (regressão) — filename com chars válidos variados"
for good in 'simple.txt' 'UPPER_CASE.CSV' 'with-dash.bin' 'with_underscore.log' 'num123.dat' 'a.b.c.txt'; do
    resp=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=$good")
    status=$(simut_status "$resp")
    if [[ "$status" == "200" ]]; then
        ok "filename '$good' aceito"
        simut_req POST "/api/delete" --data-urlencode "file=/$good" >/dev/null
        sleep 0.3                                    # evita rate-limit no delete consecutivo
    else
        ko "filename '$good' rejeitado (HTTP $status) — regressão!"
    fi
done

# -----------------------------------------------------------------------------
# TESTE 6 — Estabilidade: 20 ataques rápidos não degradam heap nem travam
# -----------------------------------------------------------------------------
hdr "Teste 6 — stress (20× filename malicioso em rajada)"
heap_before=$(simut_req GET "/api/status" | simut_body | grep -oE '"heap_lb":[0-9]+' | cut -d: -f2)
info "heap_lb antes: ${heap_before:-n/a}"

failed=0
for i in {1..20}; do
    r=$(simut_req POST "/api/upload" -F "file=@$PAYLOAD;filename=../evil$i.bin")
    s=$(simut_status "$r")
    [[ "$s" == "400" ]] || failed=$((failed+1))
done
if [[ $failed -eq 0 ]]; then
    ok "20/20 tentativas rejeitadas com HTTP 400"
else
    ko "$failed/20 tentativas não retornaram 400"
fi

# Dispositivo ainda responde?
resp=$(simut_req GET "/api/status")
assert_status "dispositivo ainda responde /api/status" "200" "$resp"

heap_after=$(simut_body "$resp" | grep -oE '"heap_lb":[0-9]+' | cut -d: -f2)
info "heap_lb depois: ${heap_after:-n/a}"
if [[ -n "$heap_before" && -n "$heap_after" ]]; then
    delta=$(( heap_before - heap_after ))
    abs_delta=${delta#-}
    if [[ $abs_delta -lt 2048 ]] || [[ $(( abs_delta * 100 / heap_before )) -lt 10 ]]; then
        ok "heap_lb estável (delta=${delta} bytes)"
    else
        ko "heap_lb degradou em ${delta} bytes (>10%) — possível fragmentação"
    fi
fi

test_summary
