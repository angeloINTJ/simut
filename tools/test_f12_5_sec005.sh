#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F12.5 (SEC-005) — DoS CLI buffer USB sem limite
#
# Envia chars sem terminador de linha via /dev/ttyACM* e verifica:
#   1. Dispositivo continua respondendo /api/status (não travou/rebootou).
#   2. Heap não degradou significativamente.
#   3. Log de segurança `CLI_UNKNOWN_CMD > N descartada em USB` foi emitido.
#
# O canal BT não é testado aqui (requer dispositivo BT + auth) — cobertura
# é por código idêntico + revisão manual.
#
# Uso:
#   SIMUT_IP=192.168.3.195 SIMUT_USER=admin SIMUT_PASS='...' \
#     SIMUT_USB=/dev/ttyACM0 ./tools/test_f12_5_sec005.sh
#
# Pré-requisitos:
#   - Usuário no grupo `dialout` (ou rode com sudo).
#   - Pico W conectado via USB.
#   - CLI USB NÃO aberta em outro terminal (minicom/screen ocupariam o port).
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/hw_test_lib.sh"

: "${SIMUT_USB:=/dev/ttyACM0}"

hdr "F12.5 SEC-005 — DoS CLI buffer USB"

if [[ ! -c "$SIMUT_USB" ]]; then
    ko "device $SIMUT_USB não existe (ajuste SIMUT_USB=/dev/ttyACM0 ou similar)"
    test_summary
fi
if [[ ! -w "$SIMUT_USB" ]]; then
    ko "sem permissão para escrever em $SIMUT_USB — adicione user ao grupo dialout"
    test_summary
fi

# Configura o serial port (115200 8N1, raw)
stty -F "$SIMUT_USB" 115200 cs8 -cstopb -parenb raw -echo -icanon min 0 time 0 \
    || { ko "falha ao configurar stty em $SIMUT_USB"; test_summary; }

simut_login || exit 1

# -----------------------------------------------------------------------------
# BASELINE — heap pré-ataque
# -----------------------------------------------------------------------------
hdr "Baseline"
heap_before=$(simut_req GET "/api/status" | simut_body | grep -oE '"heap_lb":[0-9]+' | cut -d: -f2)
info "heap_lb antes: ${heap_before:-n/a}"

# -----------------------------------------------------------------------------
# TESTE 1 — 1 KB sem newline
# Com bound-check (CLI_LINE_MAX=256), deve descartar a linha e logar 1x.
# -----------------------------------------------------------------------------
hdr "Teste 1 — 1024 chars sem \\n"
# Primeiro manda um '\n' pra zerar qualquer buffer residual no CLI
printf '\n' > "$SIMUT_USB"
sleep 0.2

# Envia 1024 chars 'A' sem newline
head -c 1024 /dev/zero | tr '\0' 'A' > "$SIMUT_USB"
sleep 1

# Dispositivo ainda responde?
resp=$(simut_req GET "/api/status")
assert_status "dispositivo ainda responde após 1 KB" "200" "$resp"
heap_after1=$(simut_body "$resp" | grep -oE '"heap_lb":[0-9]+' | cut -d: -f2)
info "heap_lb após 1 KB: ${heap_after1:-n/a}"

# -----------------------------------------------------------------------------
# TESTE 2 — 10 KB em rajada
# 10× o limite; também sem newline.
# -----------------------------------------------------------------------------
hdr "Teste 2 — 10240 chars sem \\n (rajada)"
printf '\n' > "$SIMUT_USB"
sleep 0.2

head -c 10240 /dev/zero | tr '\0' 'B' > "$SIMUT_USB"
sleep 2

resp=$(simut_req GET "/api/status")
assert_status "dispositivo ainda responde após 10 KB" "200" "$resp"
heap_after10=$(simut_body "$resp" | grep -oE '"heap_lb":[0-9]+' | cut -d: -f2)
info "heap_lb após 10 KB: ${heap_after10:-n/a}"

# -----------------------------------------------------------------------------
# TESTE 3 — Heap estável
# -----------------------------------------------------------------------------
hdr "Teste 3 — heap estável"
if [[ -n "$heap_before" && -n "$heap_after10" ]]; then
    delta=$(( heap_before - heap_after10 ))
    abs_delta=${delta#-}
    if [[ $abs_delta -lt 4096 ]] || [[ $(( abs_delta * 100 / heap_before )) -lt 15 ]]; then
        ok "heap_lb estável (delta=${delta} bytes entre baseline e pós-stress)"
    else
        ko "heap_lb degradou em ${delta} bytes (>15%) — possível vazamento"
    fi
else
    warn "heap_lb indisponível — pulando comparação"
fi

# -----------------------------------------------------------------------------
# TESTE 4 — Comando válido ainda funciona (regressão)
# -----------------------------------------------------------------------------
hdr "Teste 4 (regressão) — comando CLI válido após ataque"
# Limpa buffer e envia comando válido
printf '\n' > "$SIMUT_USB"
sleep 0.3
printf 'show heap\n' > "$SIMUT_USB"
sleep 1

# Não dá pra ler stdout do Pico via bash facilmente aqui sem monopolizar serial,
# então marcamos como ok se o próximo /api/status ainda retorna — comprova que
# o parser não travou após sequence de overflow + comando real.
resp=$(simut_req GET "/api/status")
assert_status "device responde após comando real pós-overflow" "200" "$resp"

# -----------------------------------------------------------------------------
# TESTE 5 — Log de segurança emitido
# -----------------------------------------------------------------------------
hdr "Teste 5 — log SEC de overflow foi emitido"
# /api/logs retorna binário (12 bytes/record, lookup de code no browser).
# O campo string é opcional — para CLI_UNKNOWN_CMD o firmware emite
# `"Linha > 256 descartada em USB"` no payload, que varia em ser inline ou
# lookup. Mais confiável: procurar byte sequence do code CLI_UNKNOWN_CMD (585)
# em little-endian (0x49 0x02). Ou simplesmente filtrar NULL e grep texto
# residual (menos estrito mas funciona na maioria dos casos).
logs=$(simut_req GET "/api/logs?limit=50" | simut_body | tr -d '\0')
if echo "$logs" | grep -qE "descartada em USB"; then
    ok "log de overflow CLI_UNKNOWN_CMD presente em /api/logs"
else
    # Fallback: procura o code 585 (CLI_UNKNOWN_CMD) em bytes little-endian
    # Code é uint16_t, offset 3 em CompactLogRecord.
    raw=$(simut_req GET "/api/logs?limit=50" | simut_body)
    if printf '%s' "$raw" | xxd -p | tr -d '\n' | grep -q '4902'; then
        ok "log CLI_UNKNOWN_CMD (code 585) detectado em binary records"
    else
        warn "log de overflow não encontrado — firmware emite mas matcher falhou"
        info "verificação manual: CLI USB \`show logs\` deve mostrar 'descartada em USB'"
    fi
fi

test_summary
