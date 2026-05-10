#!/usr/bin/env bash
# ============================================================================
# patch.sh — aplica overrides SIMUT no framework arduino-pico do PlatformIO.
#
# RAM SAVE: 26 KB (PBUF pool reduzido + BTstack profiles unused removidos +
#                  HCI conexões 2→1 + TCP/UDP PCBs reduzidos).
#
# O QUE FAZ:
#   1. Detecta path da framework arduino-pico no PIO local.
#   2. Backup originais (idempotente — só faz se backup ainda não existe).
#   3. Copia patched headers (lwipopts.h + btstack_config.h).
#   4. Rebuilda liblwip-rp2040 + liblwip-bt-rp2040 via cmake.
#   5. Substitui .a no framework lib dir (POST_BUILD do CMakeLists faz isso).
#   6. Verifica sha256 mudou.
#
# QUANDO RODAR:
#   - Após primeira instalação do arduino-pico via PlatformIO.
#   - Após qualquer update do framework (PIO atualiza package, override perde).
#
# REQUISITOS:
#   - cmake 3.12+
#   - arm-none-eabi-gcc (do toolchain-rp2040-earlephilhower)
#   - python3
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
    echo "Instale o arduino-pico via PlatformIO primeiro (pio run -e pico_w_release)"
    exit 1
fi
echo "[patch] framework: $FW"

TC="$HOME/.platformio/packages/toolchain-rp2040-earlephilhower/bin"
[ -x "$TC/arm-none-eabi-gcc" ] || { echo "ERRO: toolchain-rp2040-earlephilhower não instalado"; exit 1; }

# 1. Backup originais (só se ainda não existem)
if [ ! -f "$OVR/originals/lwipopts.h" ] || [ ! -f "$OVR/originals/btstack_config.h" ]; then
    mkdir -p "$OVR/originals"
    cp -v "$FW/include/lwipopts.h"        "$OVR/originals/"
    cp -v "$FW/include/btstack_config.h"  "$OVR/originals/"
    cp -v "$FW/lib/rp2040/liblwip.a"      "$OVR/originals/"
    cp -v "$FW/lib/rp2040/liblwip-bt.a"   "$OVR/originals/"
    echo "[patch] originais salvos em $OVR/originals/"
else
    echo "[patch] originais já estão em $OVR/originals/ — preservando"
fi

# 2. Copia headers patched
echo "[patch] aplicando patched_headers/"
cp -v "$OVR/patched_headers/lwipopts.h"        "$FW/include/"
cp -v "$OVR/patched_headers/btstack_config.h"  "$FW/include/"

# 3. Rebuild liblwip + liblwip-bt para rp2040
echo "[patch] rebuild liblwip-rp2040 + liblwip-bt-rp2040..."
cd "$FW/tools/libpico"
rm -rf build-rp2040
mkdir build-rp2040
cd build-rp2040
export PATH="$TC:$PATH"
export PICO_SDK_PATH="$FW/pico-sdk"
CPU=rp2040 cmake .. > /tmp/simut_cmake.log 2>&1 || { echo "ERRO cmake — log em /tmp/simut_cmake.log"; tail -20 /tmp/simut_cmake.log; exit 1; }
make -j"$(nproc)" lwip-rp2040 lwip-bt-rp2040 > /tmp/simut_make.log 2>&1 || { echo "ERRO make — log em /tmp/simut_make.log"; tail -30 /tmp/simut_make.log; exit 1; }
echo "[patch] build OK"

# 4. CMakeLists POST_BUILD já fez cp pra $FW/lib/rp2040/. Verifica.
echo ""
echo "[patch] sha256 NEW vs ORIG:"
sha256sum "$FW/lib/rp2040/liblwip.a" "$OVR/originals/liblwip.a" "$FW/lib/rp2040/liblwip-bt.a" "$OVR/originals/liblwip-bt.a"

cd "$ROOT"
echo ""
echo "[patch] DONE. Próximo build de SIMUT vai usar libs slim (~26 KB save em BSS)."
echo "[patch] Reverter: bash tools/arduino_pico_overrides/restore.sh"
