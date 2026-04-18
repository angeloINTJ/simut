#!/usr/bin/env bash
# backup.sh — Backup completo do projeto SIMUT após cada commit.
# Gera: diretório com fontes + git bundle + tarball comprimido.
# Chamado automaticamente via hook PostToolUse do Claude Code.

set -euo pipefail

SRC="/home/angelo/Documentos/SIMUT"
DEST="/home/angelo/Documentos"
TS="$(date +%Y%m%d_%H%M%S)"
TAG="SIMUT_backup_${TS}"

# 1. Cópia dos fontes (só arquivos rastreados pelo git)
BACKUP_DIR="${DEST}/${TAG}"
mkdir -p "${BACKUP_DIR}"
git -C "${SRC}" ls-files -z | xargs -0 -I{} cp --parents -f "${SRC}/{}" "${BACKUP_DIR}/" 2>/dev/null || \
  git -C "${SRC}" ls-files | while IFS= read -r f; do
    mkdir -p "${BACKUP_DIR}/$(dirname "$f")"
    cp -f "${SRC}/$f" "${BACKUP_DIR}/$f"
  done

# 2. Git bundle (histórico completo)
git -C "${SRC}" bundle create "${DEST}/${TAG}.bundle" --all 2>/dev/null

# 3. Tarball comprimido
tar -czf "${DEST}/${TAG}.tar.gz" -C "${DEST}" "${TAG}" 2>/dev/null

echo "{\"systemMessage\":\"Backup criado: ${TAG} (dir + bundle + tar.gz)\"}"
