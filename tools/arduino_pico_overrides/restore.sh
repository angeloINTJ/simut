#!/usr/bin/env bash
# ============================================================================
# restore.sh — reverte overrides SIMUT do framework arduino-pico.
#
# Restaura lwipopts.h original + invalida cache PIO. Próximo `pio run` volta
# pra config virgin (PBUF_POOL_SIZE=24, RAM ~43.7% com screenshot heap-alloc
# ainda no código SIMUT, ou ~49.6% original sem nenhum patch).
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
cp -v "$OVR/originals/lwipopts.h" "$FW/include/"

# Invalida cache PIO
for build in "$ROOT/.pio/build"/*/FrameworkArduino/lwip; do
    if [ -d "$build" ]; then
        rm -rf "$build"
        echo "[restore] cache invalidado: $build"
    fi
done

echo "[restore] DONE — framework virgin. Próximo pio run recompila lwIP com defaults."
