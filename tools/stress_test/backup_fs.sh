#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# backup_fs.sh — backup completo do LittleFS do SIMUT.
# Faz walk recursivo via /api/ls e download de cada arquivo via /download.
#
# Uso:
#   SIMUT_IP=192.168.3.195 SIMUT_USER=admin SIMUT_PASS=admin \
#       ./tools/stress_test/backup_fs.sh [BACKUP_DIR]
#
# Default BACKUP_DIR: /tmp/simut_backup_<YYYYMMDD_HHMMSS>
#
# Output:
#   - Arquivos preservando estrutura de diretórios
#   - manifest.txt: lista de "<remote_path>|<size>" pra cada arquivo
#
# Exit code:
#   0 = sucesso (manifest match)
#   1 = falha
# -----------------------------------------------------------------------------

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_simut_api.sh"

BACKUP_DIR="${1:-/tmp/simut_backup_$(date +%Y%m%d_%H%M%S)}"
MANIFEST="${BACKUP_DIR}/manifest.txt"

_simut_log "Backup destino: $BACKUP_DIR"

mkdir -p "$BACKUP_DIR"

# 1) Login
simut_login || exit 1

# 2) Walk recursivo, salva manifest
_simut_log "Listando filesystem..."
WALK_OUT="${BACKUP_DIR}/_walk.tmp"
simut_walk_files / > "$WALK_OUT"

if [[ ! -s "$WALK_OUT" ]]; then
    _simut_error "Walk vazio - filesystem inacessível ou sem permissão"
    exit 1
fi

n_files=$(grep -c '^f|' "$WALK_OUT" || true)
n_dirs=$(grep -c '^d|' "$WALK_OUT" || true)
_simut_log "Encontrados: $n_files arquivos, $n_dirs diretórios"

# 3) Cria diretórios localmente preservando estrutura
while IFS='|' read -r typ path size; do
    [[ "$typ" != "d" ]] && continue
    mkdir -p "${BACKUP_DIR}${path}"
done < "$WALK_OUT"

# 4) Download de cada arquivo
echo "" > "$MANIFEST"
fail_count=0
ok_count=0
total_bytes=0

while IFS='|' read -r typ path size; do
    [[ "$typ" != "f" ]] && continue
    local_path="${BACKUP_DIR}${path}"
    mkdir -p "$(dirname "$local_path")"
    if simut_download "$path" "$local_path"; then
        actual_size=$(stat -c%s "$local_path" 2>/dev/null || stat -f%z "$local_path" 2>/dev/null || echo 0)
        printf '%s|%s|%s\n' "$path" "$size" "$actual_size" >> "$MANIFEST"
        ok_count=$((ok_count + 1))
        total_bytes=$((total_bytes + actual_size))
        printf '  ✓ %s (%s B)\n' "$path" "$actual_size"
    else
        fail_count=$((fail_count + 1))
        printf '  ✗ %s\n' "$path"
    fi
done < "$WALK_OUT"

rm -f "$WALK_OUT"

simut_logout

# 5) Relatório
echo ""
_simut_log "=== Backup ==="
_simut_log "OK:    $ok_count arquivos ($total_bytes B = $((total_bytes/1024)) KB)"
if [[ $fail_count -gt 0 ]]; then
    _simut_warn "FAIL:  $fail_count arquivos"
fi
_simut_log "Manifest: $MANIFEST"
_simut_log "Backup:   $BACKUP_DIR"

# Mostra path no stdout final pra captura via $()
echo ""
echo "BACKUP_DIR=$BACKUP_DIR"

[[ $fail_count -eq 0 ]] && exit 0 || exit 1
