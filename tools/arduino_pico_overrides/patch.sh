#!/usr/bin/env bash
# ============================================================================
# patch.sh — aplica overrides SIMUT no framework arduino-pico do PlatformIO.
#
# RAM SAVE: ~18 KB (PBUF_POOL_SIZE 24→12 em lwipopts.h).
#
# DESCOBERTA IMPORTANTE (v4.2.1):
#   PIO compila a maior parte do lwIP DO SOURCE em cada build do projeto, NÃO
#   da liblwip.a precompilada. Os .o cacheados ficam em
#   .pio/build/<env>/FrameworkArduino/lwip/src/. Isso significa:
#     - Patches em lwipopts.h propagam pra próxima build SIMUT (após cache invalidation).
#     - NÃO é necessário rebuildar liblwip.a / liblwip-bt.a (versão antiga
#       deste script tentava — caminho errado, complicava sem ganho real).
#   BTstack ainda É precompilada em liblwip-bt.a — patches em btstack_config.h
#   exigem rebuild da framework. Mas alterar BTstack quebra cyw43 RSSI sampling,
#   então NÃO patcheamos BTstack. Mantemos defaults.
#
# O QUE FAZ:
#   1. Detecta path da framework arduino-pico no PIO local.
#   2. Backup originais (idempotente).
#   3. Copia patched lwipopts.h.
#   4. Invalida cache PIO em .pio/build/*/FrameworkArduino/lwip/ (próxima build
#      recompila lwIP do source com novo header).
#
# QUANDO RODAR:
#   - Após primeira instalação do arduino-pico via PlatformIO.
#   - Após qualquer update do framework via PIO.
#
# REVERSÃO:
#   bash tools/arduino_pico_overrides/restore.sh
# ============================================================================
set -e
cd "$(dirname "$0")/../.."  # SIMUT root
ROOT="$(pwd)"
OVR="$ROOT/tools/arduino_pico_overrides"

# Detecta framework path
FW=""
for cand in \
    "$HOME/.platformio/packages/framework-arduinopico" \
    "$HOME/.platformio/packages/framework-arduino-pico"; do
    [ -d "$cand" ] && { FW="$cand"; break; }
done
if [ -z "$FW" ]; then
    echo "ERRO: framework-arduinopico não encontrado em ~/.platformio/packages/"
    exit 1
fi
echo "[patch] framework: $FW"

# 1. Backup originais (só se ainda não existem)
if [ ! -f "$OVR/originals/lwipopts.h" ]; then
    mkdir -p "$OVR/originals"
    cp -v "$FW/include/lwipopts.h" "$OVR/originals/"
    echo "[patch] original salvo em $OVR/originals/"
else
    echo "[patch] original lwipopts.h já está em $OVR/originals/ — preservando"
fi

# 2. Copia patched lwipopts.h
echo "[patch] aplicando patched lwipopts.h"
cp -v "$OVR/patched_headers/lwipopts.h" "$FW/include/"

# 3. Invalida cache PIO (apenas lwip src)
for build in "$ROOT/.pio/build"/*/FrameworkArduino/lwip; do
    if [ -d "$build" ]; then
        rm -rf "$build"
        echo "[patch] cache invalidado: $build"
    fi
done

echo ""
echo "[patch] DONE. Próximo \`pio run\` recompila lwIP com PBUF_POOL_SIZE=12."
echo "[patch] RAM esperada: ~36.7% (save 18 KB BSS no memp_PBUF_POOL_base)."
echo "[patch] Reverter: bash tools/arduino_pico_overrides/restore.sh"
