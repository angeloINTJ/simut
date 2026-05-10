#!/usr/bin/env bash
# ============================================================================
# restore.sh — reverte overrides SIMUT do framework arduino-pico.
#
# Restaura headers + .a files originais a partir de tools/arduino_pico_overrides/
# originals/. Use se quiser comparar comportamento com framework virgin ou se
# tiver problemas atribuíveis ao patch.
# ============================================================================
set -e
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
OVR="$ROOT/tools/arduino_pico_overrides"

FW=""
for cand in \
    "$HOME/.platformio/packages/framework-arduinopico" \
    "$HOME/.platformio/packages/framework-arduino-pico"; do
    [ -d "$cand" ] && { FW="$cand"; break; }
done
[ -z "$FW" ] && { echo "ERRO: framework-arduinopico não encontrado"; exit 1; }

[ -f "$OVR/originals/lwipopts.h" ] || { echo "ERRO: $OVR/originals/lwipopts.h não existe"; exit 1; }

echo "[restore] revertendo overrides..."
cp -v "$OVR/originals/lwipopts.h"        "$FW/include/"
cp -v "$OVR/originals/btstack_config.h"  "$FW/include/"
cp -v "$OVR/originals/liblwip.a"         "$FW/lib/rp2040/"
cp -v "$OVR/originals/liblwip-bt.a"      "$FW/lib/rp2040/"
echo "[restore] DONE — framework virgin. Próximo SIMUT build voltará a 49.6% RAM."
