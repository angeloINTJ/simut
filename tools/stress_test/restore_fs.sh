#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# restore_fs.sh — restaura LittleFS do SIMUT a partir de um backup local.
# Estratégia: para cada arquivo no manifest, deleta o remote (se existir) e
# faz upload do local. Verifica via /api/ls que tudo bateu.
#
# Uso:
#   SIMUT_IP=... ./tools/stress_test/restore_fs.sh BACKUP_DIR
#
# Modos:
#   --selective <prefix>   Só restaura arquivos sob /prefix/ (ex.: /history)
#   --delete-extras        Remove arquivos remote que não estão no backup
#                          (USE COM CUIDADO — bom pra rollback de stress test)
# -----------------------------------------------------------------------------

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_simut_api.sh"

DELETE_EXTRAS=0
SELECTIVE_PREFIX=""
BACKUP_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --delete-extras) DELETE_EXTRAS=1; shift ;;
        --selective)     SELECTIVE_PREFIX="$2"; shift 2 ;;
        -*) _simut_error "flag desconhecida: $1"; exit 2 ;;
        *) BACKUP_DIR="$1"; shift ;;
    esac
done

if [[ -z "$BACKUP_DIR" ]]; then
    _simut_error "Uso: $0 <BACKUP_DIR> [--delete-extras] [--selective /prefix]"
    exit 2
fi

MANIFEST="${BACKUP_DIR}/manifest.txt"
if [[ ! -f "$MANIFEST" ]]; then
    _simut_error "Manifest nao encontrado: $MANIFEST"
    exit 1
fi

_simut_log "Restore source: $BACKUP_DIR"
[[ -n "$SELECTIVE_PREFIX" ]] && _simut_log "Selective prefix: $SELECTIVE_PREFIX"
[[ "$DELETE_EXTRAS" == "1" ]] && _simut_warn "DELETE_EXTRAS ativo - arquivos extras no remote serão removidos"

simut_login || exit 1

# 1) Walk current state pra comparar com manifest (delete-extras)
_simut_log "Snapshot do estado atual..."
CUR_FILES="$(mktemp)"
simut_walk_files / | grep '^f|' | awk -F'|' '{print $2}' > "$CUR_FILES"
n_cur=$(wc -l < "$CUR_FILES")
_simut_log "  $n_cur arquivos atualmente no remote"

# 2) Lê manifest
BACKUP_FILES="$(mktemp)"
grep -v '^$' "$MANIFEST" | awk -F'|' '{print $1}' > "$BACKUP_FILES"
n_bkp=$(wc -l < "$BACKUP_FILES")
_simut_log "  $n_bkp arquivos no backup"

# 3) Deleta extras (arquivos que estão no remote mas NÃO no backup)
if [[ "$DELETE_EXTRAS" == "1" ]]; then
    _simut_log "Deletando extras..."
    EXTRAS="$(comm -23 <(sort "$CUR_FILES") <(sort "$BACKUP_FILES"))"
    if [[ -z "$EXTRAS" ]]; then
        _simut_log "  (nenhum extra)"
    else
        while IFS= read -r path; do
            [[ -z "$path" ]] && continue
            if [[ -n "$SELECTIVE_PREFIX" && "$path" != "$SELECTIVE_PREFIX/"* ]]; then
                continue
            fi
            if simut_delete "$path"; then
                printf '  ✗ del %s\n' "$path"
            else
                printf '  ! err del %s\n' "$path"
            fi
        done <<< "$EXTRAS"
    fi
fi

# 4) Upload de cada arquivo do manifest
_simut_log "Restaurando do backup..."
ok_count=0
fail_count=0

while IFS='|' read -r path expected_size actual_size; do
    [[ -z "$path" ]] && continue
    if [[ -n "$SELECTIVE_PREFIX" && "$path" != "$SELECTIVE_PREFIX/"* ]]; then
        continue
    fi
    local_path="${BACKUP_DIR}${path}"
    if [[ ! -f "$local_path" ]]; then
        _simut_warn "  arquivo backup ausente: $local_path"
        fail_count=$((fail_count + 1))
        continue
    fi
    remote_dir="$(dirname "$path")"
    if simut_upload "$local_path" "$remote_dir"; then
        printf '  ↑ %s (%s B)\n' "$path" "$actual_size"
        ok_count=$((ok_count + 1))
    else
        printf '  ! upload falhou: %s\n' "$path"
        fail_count=$((fail_count + 1))
    fi
done < "$MANIFEST"

# 5) Verifica
_simut_log "Verificação..."
NEW_CUR="$(mktemp)"
simut_walk_files / | grep '^f|' | awk -F'|' '{print $2"|"$3}' > "$NEW_CUR"

# Compara files+sizes do manifest vs estado atual
verify_ok=0
verify_fail=0
while IFS='|' read -r path _ expected_size; do
    [[ -z "$path" ]] && continue
    if [[ -n "$SELECTIVE_PREFIX" && "$path" != "$SELECTIVE_PREFIX/"* ]]; then
        continue
    fi
    # Files que crescem naturalmente durante teste (logs, cursor) — restore
    # é best-effort: arquivo presente é suficiente, size pode divergir.
    case "$path" in
        /system.blog|/system.log|/config/t_cursor.bin)
            actual_remote=$(grep "^${path}|" "$NEW_CUR" | awk -F'|' '{print $2}' | head -1)
            if [[ -z "$actual_remote" ]]; then
                printf '  ✗ AUSENTE remote: %s\n' "$path"
                verify_fail=$((verify_fail + 1))
            else
                if [[ "$actual_remote" != "$expected_size" ]]; then
                    printf '  ⓘ %s: size differ (backup=%s, remote=%s) — esperado, log/cursor cresce\n' \
                           "$path" "$expected_size" "$actual_remote"
                fi
                verify_ok=$((verify_ok + 1))
            fi
            continue
            ;;
    esac
    actual_remote=$(grep "^${path}|" "$NEW_CUR" | awk -F'|' '{print $2}' | head -1)
    if [[ -z "$actual_remote" ]]; then
        printf '  ✗ AUSENTE remote: %s\n' "$path"
        verify_fail=$((verify_fail + 1))
    elif [[ "$actual_remote" != "$expected_size" ]]; then
        printf '  ⚠ size mismatch %s: backup=%s remote=%s\n' "$path" "$expected_size" "$actual_remote"
        verify_fail=$((verify_fail + 1))
    else
        verify_ok=$((verify_ok + 1))
    fi
done < "$MANIFEST"

rm -f "$CUR_FILES" "$BACKUP_FILES" "$NEW_CUR"

simut_logout

echo ""
_simut_log "=== Restore ==="
_simut_log "Upload OK:   $ok_count"
[[ $fail_count -gt 0 ]] && _simut_warn "Upload FAIL: $fail_count"
_simut_log "Verify OK:   $verify_ok"
[[ $verify_fail -gt 0 ]] && _simut_warn "Verify FAIL: $verify_fail"

[[ $fail_count -eq 0 && $verify_fail -eq 0 ]] && exit 0 || exit 1
