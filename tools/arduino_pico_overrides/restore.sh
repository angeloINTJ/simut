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

# Todos os cinco arquivos que patch.sh toca. Antes este script revertia so
# lwipopts e o BearSSL, entao um "restore" deixava HTTPClient, ClientContext e
# Parsing patchados — dois deles com override real ainda ativo. ATENCAO: cada
# um segura algo (ver patch.sh) — TLS sem prazo trava o Core 0, Parsing sem
# prazo aceita o DoS do request lento (D-B8), HTTPClient/ClientContext idem.
[ -f "$OVR/originals/WiFiClientSecureBearSSL.cpp" ] && \
    cp -v "$OVR/originals/WiFiClientSecureBearSSL.cpp" "$FW/libraries/WiFi/src/"
[ -f "$OVR/originals/HTTPClient.cpp" ] && \
    cp -v "$OVR/originals/HTTPClient.cpp" "$FW/libraries/HTTPClient/src/"
[ -f "$OVR/originals/ClientContext.h" ] && \
    cp -v "$OVR/originals/ClientContext.h" "$FW/libraries/WiFi/src/include/"
[ -f "$OVR/originals/Parsing.cpp" ] && \
    cp -v "$OVR/originals/Parsing.cpp" "$FW/libraries/WebServer/src/"

# Invalida cache PIO — FrameworkArduino (lwip) + os .o das libs patchadas, senao
# o build "passa" religando os objetos antigos ainda patchados.
for obj in "$ROOT/.pio/build"/*/lib*/WiFi/*.o \
           "$ROOT/.pio/build"/*/lib*/HTTPClient/HTTPClient.cpp.o \
           "$ROOT/.pio/build"/*/lib*/WebServer/Parsing.cpp.o; do
    [ -f "$obj" ] && { rm -f "$obj"; echo "[restore] cache invalidado: $obj"; }
done
for build in "$ROOT/.pio/build"/*/FrameworkArduino/lwip; do
    if [ -d "$build" ]; then
        rm -rf "$build"
        echo "[restore] cache invalidado: $build"
    fi
done

echo "[restore] DONE — framework virgin. Próximo pio run recompila lwIP com defaults."
