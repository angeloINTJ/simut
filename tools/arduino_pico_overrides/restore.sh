#!/usr/bin/env bash
# ============================================================================
# restore.sh — reverte overrides SIMUT do framework arduino-pico.
#
# Restaura os arquivos originais + invalida cache PIO. Proximo `pio run` volta
# pra config de fabrica (TCP_WND 8*MSS, stats desligados, e — atencao — SEM os
# prazos de TLS/HTTPClient e SEM a liberacao de _rx_buf: ver patch.sh para o
# que cada um segura).
#
# Respeita PLATFORMIO_CORE_DIR, e le os originais de originals/<versao>/. As
# duas coisas existem pelo mesmo motivo: numa migracao de framework ha dois
# cores e duas versoes vivas ao mesmo tempo, e restaurar a versao errada dentro
# da framework errada nao falha — o cp funciona e o build sai silenciosamente
# com fontes de outra versao.
# ============================================================================
set -e
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
OVR="$ROOT/tools/arduino_pico_overrides"

PIO_CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
FW=""
for cand in \
    "$PIO_CORE/packages/framework-arduinopico" \
    "$PIO_CORE/packages/framework-arduino-pico"; do
    [ -d "$cand" ] && { FW="$cand"; break; }
done
[ -z "$FW" ] && { echo "ERRO: framework-arduinopico não encontrado em $PIO_CORE/packages/"; exit 1; }

FW_VER="$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$FW/package.json" 2>/dev/null | head -1)"
[ -z "$FW_VER" ] && FW_VER="unknown"
ORIG="$OVR/originals/$FW_VER"
echo "[restore] framework: $FW (versao $FW_VER)"
echo "[restore] originais:  $ORIG"

[ -f "$ORIG/lwipopts.h" ] || {
    echo "ERRO: $ORIG/lwipopts.h não existe."
    echo "      Sem original DESTA versao nao ha o que restaurar — rode patch.sh"
    echo "      uma vez contra esta framework para gravar os originais, ou"
    echo "      reinstale a framework pelo PlatformIO."
    exit 1
}

echo "[restore] revertendo overrides..."
cp -v "$ORIG/lwipopts.h" "$FW/include/"

# Todos os cinco arquivos que patch.sh toca, lidos de $ORIG (originais desta
# versao de framework). Antes este script revertia so lwipopts e o BearSSL,
# entao um "restore" deixava HTTPClient, ClientContext e Parsing patchados —
# dois deles com override real ainda ativo. ATENCAO: cada um segura algo (ver
# patch.sh) — TLS sem prazo trava o Core 0, Parsing sem prazo aceita o DoS do
# request lento (D-B8), HTTPClient/ClientContext idem. Os dois patches do
# HTTPClient (prazos de leitura e feed no envio) moram no mesmo arquivo, entao
# um unico original reverte os dois.
[ -f "$ORIG/WiFiClientSecureBearSSL.cpp" ] && \
    cp -v "$ORIG/WiFiClientSecureBearSSL.cpp" "$FW/libraries/WiFi/src/"
[ -f "$ORIG/HTTPClient.cpp" ] && \
    cp -v "$ORIG/HTTPClient.cpp" "$FW/libraries/HTTPClient/src/"
[ -f "$ORIG/ClientContext.h" ] && \
    cp -v "$ORIG/ClientContext.h" "$FW/libraries/WiFi/src/include/"
[ -f "$ORIG/Parsing.cpp" ] && \
    cp -v "$ORIG/Parsing.cpp" "$FW/libraries/WebServer/src/"
# Keep-alive opt-in (patch 2f) vive nestes tres + Parsing.cpp acima.
[ -f "$ORIG/HTTPServer.h" ] && \
    cp -v "$ORIG/HTTPServer.h" "$FW/libraries/WebServer/src/"
[ -f "$ORIG/HTTPServer.cpp" ] && \
    cp -v "$ORIG/HTTPServer.cpp" "$FW/libraries/WebServer/src/"
[ -f "$ORIG/WebServerTemplate.h" ] && \
    cp -v "$ORIG/WebServerTemplate.h" "$FW/libraries/WebServer/src/"

# Invalida cache PIO — FrameworkArduino (lwip) + os .o das libs patchadas, senao
# o build "passa" religando os objetos antigos ainda patchados.
for obj in "$ROOT/.pio/build"/*/lib*/WiFi/*.o \
           "$ROOT/.pio/build"/*/lib*/HTTPClient/HTTPClient.cpp.o \
           "$ROOT/.pio/build"/*/lib*/WebServer/*.o; do
    [ -f "$obj" ] && { rm -f "$obj"; echo "[restore] cache invalidado: $obj"; }
done
for build in "$ROOT/.pio/build"/*/FrameworkArduino/lwip; do
    if [ -d "$build" ]; then
        rm -rf "$build"
        echo "[restore] cache invalidado: $build"
    fi
done

echo "[restore] DONE — framework de fabrica. Proximo pio run recompila lwIP com defaults."
