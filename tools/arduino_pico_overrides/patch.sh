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

# 2b. Patch cirúrgico no handshake TLS (WiFiClientSecureBearSSL.cpp)
#
#   O upstream só tem prazo dentro de _run_until(), que reinicia o próprio
#   contador a cada chamada. _wait_for_handshake() chama num laço sem prazo
#   global, então setTLSConnectTimeout() limita UMA iteração e nunca o handshake
#   inteiro. Contra um peer que aceita TCP mas não fecha o handshake, o Core 0
#   gira ali para sempre — e os três optimistic_yield() do arquivo estão
#   comentados, então nada cede CPU nem alimenta o watchdog: o dispositivo trava
#   de vez em vez de retornar erro. Medido na bancada em 2026-07-25.
#
#   Aqui é .cpp, não header: PIO compila as libraries do source em cada build,
#   então o patch propaga na próxima compilação (com o cache invalidado abaixo).
BEARSSL="$FW/libraries/WiFi/src/WiFiClientSecureBearSSL.cpp"
BEARSSL_PATCH="$OVR/patches/wifi_tls_handshake_deadline.patch"
if [ ! -f "$OVR/originals/WiFiClientSecureBearSSL.cpp" ]; then
    mkdir -p "$OVR/originals"
    cp -v "$BEARSSL" "$OVR/originals/"
fi
if grep -q "SIMUT override — bound the handshake as a whole" "$BEARSSL"; then
    echo "[patch] handshake TLS já tem prazo global — nada a fazer"
else
    echo "[patch] aplicando prazo global no handshake TLS"
    patch -p1 -d "$FW" < "$BEARSSL_PATCH"
fi

# 2c. Prazos nos laços de leitura do HTTPClient (HTTPClient.cpp)
#
#   Dois laços do upstream não têm limite superior nenhum:
#
#   - handleHeaderResponse(): _tcpTimeout é prazo de INATIVIDADE (lastDataTime
#     reinicia a cada linha recebida), então um servidor que responde 1 byte a
#     cada 400 ms nunca o dispara e o laço roda para sempre.
#   - disconnect(): drena o corpo byte a byte "até available() zerar". Contra um
#     peer que ainda está transmitindo, o socket enche na mesma velocidade em
#     que esvazia, e o laço percorre o corpo inteiro.
#
#   Medido na bancada em 2026-08-02: resposta de 1 MB = 4 reboots por watchdog
#   em 2 min, autópsia C0=[TEL_SEND] em todos. Um servidor de telemetria
#   defeituoso (ou hostil) derrubava o aparelho em laço.
#
#   Patch: um orçamento único em cada laço + watchdog_update() dentro — seguro
#   exatamente porque o orçamento faz os dois terminarem. Mesmo padrão do
#   prazo de handshake TLS acima.
HTTPC="$FW/libraries/HTTPClient/src/HTTPClient.cpp"
HTTPC_PATCH="$OVR/patches/httpclient_read_deadlines.patch"
if [ ! -f "$OVR/originals/HTTPClient.cpp" ]; then
    mkdir -p "$OVR/originals"
    cp -v "$HTTPC" "$OVR/originals/"
fi
if grep -q "SIMUT override — bound the header read as a whole" "$HTTPC"; then
    echo "[patch] HTTPClient já tem prazos de leitura — nada a fazer"
else
    echo "[patch] aplicando prazos nos laços de leitura do HTTPClient"
    patch -p1 -d "$FW" < "$HTTPC_PATCH"
fi

# 2c-bis. Feed do watchdog no laço de ENVIO do HTTPClient (HTTPClient.cpp)
#
#   Os prazos acima cobrem a LEITURA. O envio ficou de fora, e tem o mesmo
#   defeito com uma torção: o orçamento de 5 s de StreamConstPtr::sendAll
#   limita o LAÇO, não uma escrita. Contra um servidor que aceita a conexão e
#   para de ler (janela zero), cada dst->write( ) estaciona pelo timeout de
#   socket (4 s), então uma escrita iniciada perto do fim do orçamento termina
#   por volta de 9 s — além dos 8388 ms do watchdog de hardware — sem nada
#   alimentando.
#
#   Medido na bancada em 2026-08-10 com o sink em modo never_read: 3 reboots em
#   150 s, autópsia C0=[TEL_SEND] ctx=225, e o servidor contando 9 conexões com
#   0 requisições completas. Era a única costura de watchdog que a campanha de
#   02/08 tinha deixado aberta.
#
#   Patch: watchdog_update( ) dos dois lados da escrita — a maior lacuna sem
#   feed passa a ser uma escrita (<= 4 s). Seguro porque os dois limites que já
#   existiam continuam terminando o laço.
HTTPC_SEND_PATCH="$OVR/patches/httpclient_send_feed.patch"
if grep -q "SIMUT override — feed the watchdog around every write" "$HTTPC"; then
    echo "[patch] HTTPClient já alimenta no laço de envio — nada a fazer"
else
    echo "[patch] aplicando feed do watchdog no laço de envio do HTTPClient"
    patch -p1 -d "$FW" < "$HTTPC_SEND_PATCH"
fi

# 2d. Vazamento de pbufs de recepcao no ClientContext (ClientContext.h)
#
#   close( ) e abort( ) desanexam todos os callbacks e largam o _pcb, mas
#   deixavam _rx_buf intacto. Sem o pcb esse dado nao pode mais ser entregue a
#   ninguem, entao os pbufs do pool ficam presos — e WiFiClient::stop( ) chama
#   close( ) SEM soltar o ClientContext, entao o refcount nunca chega ao
#   unref( ) que teria feito o discard.
#
#   Medido na bancada em 2026-08-02 contra um servidor que responde 1 MB: apos
#   ~19 conexoes, "PBUF pool: 12 em uso / pico 12 / 12 total, 1158 falhas". O
#   aparelho seguia vivo (IP, link -42 dBm, CLI serial respondendo) mas o
#   servidor web ficava mudo, e NAO se recuperava: com a telemetria desligada o
#   pool continuava 12/12 e as falhas subindo. So reboot devolvia.
#
#   O pool tem so 12 entradas (PBUF_POOL_SIZE 24->12 no lwipopts patchado, para
#   economizar 18 KB de RAM), entao a margem e estreita.
CTXH="$FW/libraries/WiFi/src/include/ClientContext.h"
CTXH_PATCH="$OVR/patches/clientcontext_rx_leak.patch"
if [ ! -f "$OVR/originals/ClientContext.h" ]; then
    mkdir -p "$OVR/originals"
    cp -v "$CTXH" "$OVR/originals/"
fi
if grep -q "SIMUT override: release buffered RX before abandoning the pcb" "$CTXH"; then
    echo "[patch] ClientContext ja libera o RX no close/abort — nada a fazer"
else
    echo "[patch] aplicando liberacao de _rx_buf no close/abort do ClientContext"
    patch -p1 -d "$FW" < "$CTXH_PATCH"
fi

# 3. Invalida cache PIO (lwip src + lib WiFi)
#    A lib WiFi tem cache próprio em lib*/WiFi/ — sem apagá-lo o .cpp patchado
#    não recompila e o build "passa" ainda com o handshake sem prazo.
for build in "$ROOT/.pio/build"/*/FrameworkArduino/lwip; do
    if [ -d "$build" ]; then
        rm -rf "$build"
        echo "[patch] cache invalidado: $build"
    fi
done
# Todos os .o da lib WiFi, nao so o do BearSSL: ClientContext.h e um HEADER
# incluido por varios deles, entao apagar um objeto so deixaria o vazamento de
# pbuf linkado enquanto o build "passa".
for wifiobj in "$ROOT/.pio/build"/*/lib*/WiFi/*.o; do
    if [ -f "$wifiobj" ]; then
        rm -f "$wifiobj"
        echo "[patch] cache invalidado: $wifiobj"
    fi
done
# HTTPClient tem cache próprio pelo mesmo motivo da lib WiFi: sem apagar o .o,
# o build "passa" ainda linkando os laços de leitura sem prazo.
for httpobj in "$ROOT/.pio/build"/*/lib*/HTTPClient/HTTPClient.cpp.o; do
    if [ -f "$httpobj" ]; then
        rm -f "$httpobj"
        echo "[patch] cache invalidado: $httpobj"
    fi
done

echo ""
echo "[patch] DONE. Próximo \`pio run\` recompila lwIP com PBUF_POOL_SIZE=12."
echo "[patch] RAM esperada: ~36.7% (save 18 KB BSS no memp_PBUF_POOL_base)."
echo "[patch] Reverter: bash tools/arduino_pico_overrides/restore.sh"
