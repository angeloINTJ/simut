#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Remove arquivos lixo criados pelos testes F12.1 pré-patch do SIMUT.
#
# Contexto: a rodada de teste antes do flash do patch F12.1 criou ~30 arquivos
# com nomes maliciosos no FS. Um deles tem byte 0x01, o que quebra o JSON do
# /api/ls e torna a página /files inútil. Este script apaga todos por nome
# via /api/delete (que não precisa listar).
#
# Uso:
#   SIMUT_IP=192.168.3.195 SIMUT_USER=admin SIMUT_PASS='...' ./tools/cleanup_test_artifacts.sh
# -----------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/hw_test_lib.sh"

hdr "Cleanup de artefatos de teste F12.1"
simut_login || exit 1

# Lista exaustiva dos nomes criados (do log do SIMUT + test script)
declare -a VICTIMS=(
    '/../config/system.bin'
    '/a<b>c.txt'
    '/back\\slash.txt'
    '/quote%22.txt'
    '/colon:name.txt'
    '/pipe|name.txt'
    '/star*.txt'
    '/quest?.txt'
    '/'                                                # upload de filename=vazio virou "/"
    '/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.txt'
    '/a..b.txt'
)

# Arquivo com byte 0x01 — montado literal
CTRL_NAME="/$(printf 'x\x01y.txt')"
VICTIMS+=("$CTRL_NAME")

# /../evil1.bin .. /../evil20.bin
for i in {1..20}; do
    VICTIMS+=("/../evil$i.bin")
done

removed=0
missing=0
for p in "${VICTIMS[@]}"; do
    resp=$(simut_req POST "/api/delete" --data-urlencode "file=$p")
    status=$(simut_status "$resp")
    case "$status" in
        200)
            ok "removido: $p"
            removed=$((removed+1))
            ;;
        403|404)
            info "não encontrado (ok): $p [HTTP $status]"
            missing=$((missing+1))
            ;;
        *)
            ko "falhou ao remover $p [HTTP $status] body=$(simut_body "$resp" | head -c 100)"
            ;;
    esac
    sleep 0.3                                              # evita rate-limit
done

hdr "Resumo"
info "removidos: $removed   ausentes: $missing   total tentativas: ${#VICTIMS[@]}"
info "teste o /files no browser agora — o listing deve carregar normalmente"
test_summary
