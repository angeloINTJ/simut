#!/usr/bin/env bash
# ============================================================================
# patch.sh — aplica overrides SIMUT no framework arduino-pico do PlatformIO.
#
# O QUE lwipopts.h MUDA HOJE (conferido contra o header de fábrica em 10/08/2026):
#   TCP_WND 8*MSS -> 4*MSS  (correcao do D14 — a janela prometia o pool em dobro)
#   LWIP_STATS 0 -> 1, MEMP_STATS 0 -> 1  (contadores do pool)
# PBUF_POOL_SIZE fica nos 24 de fabrica. Uma revisao antiga cortava para 12 por
# ~18 KB de BSS; aumentar ou diminuir o pool NAO e mais a estrategia, e a
# aritmetica do D14 depende de ele ser 24.
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
# PIO_CORE respeita PLATFORMIO_CORE_DIR: a migracao de framework tem de rodar
# num core dir ISOLADO, nunca in-place, porque ~/.platformio e a toolchain que
# constroi a imagem publicada — perde-la no meio significa nao conseguir
# reconstruir o que esta no release.
PIO_CORE="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
FW=""
for cand in \
    "$PIO_CORE/packages/framework-arduinopico" \
    "$PIO_CORE/packages/framework-arduino-pico"; do
    [ -d "$cand" ] && { FW="$cand"; break; }
done
if [ -z "$FW" ]; then
    echo "ERRO: framework-arduinopico não encontrado em $PIO_CORE/packages/"
    exit 1
fi
echo "[patch] framework: $FW"

# Backups sao POR VERSAO de framework. Sem isso, migrar de framework num core
# isolado sobrescreve os originais do core de casa, e o restore.sh seguinte
# escreve fontes da versao nova dentro da versao antiga — em silencio, porque
# os arquivos existem e o cp funciona. Aconteceu na migracao 5.4.3 -> 5.6.1.
FW_VER="$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$FW/package.json" 2>/dev/null | head -1)"
[ -z "$FW_VER" ] && FW_VER="unknown"
ORIG="$OVR/originals/$FW_VER"
echo "[patch] originais desta versao: $ORIG"

# Guarda: nunca gravar como "original" um arquivo que ja carrega override
# SIMUT. Acontece quando os originais desta versao foram perdidos DEPOIS do
# primeiro patch — o cp funciona, o backup fica com o patch dentro, e o
# restore.sh seguinte "restaura" para um estado que nunca foi de fabrica.
# Custou exatamente isso na migracao 5.4.3 -> 5.6.1.
save_original( ) {  # $1 = arquivo na framework, $2 = nome no backup
    [ -f "$ORIG/$2" ] && return 0
    if grep -q "SIMUT override" "$1" 2>/dev/null; then
        echo "[patch] RECUSADO: $1 ja tem override e nao ha original de $FW_VER."
        echo "[patch]   Reinstale a framework (pio pkg install --force) antes de"
        echo "[patch]   depender do restore.sh — o backup seria falso."
        return 0
    fi
    mkdir -p "$ORIG"
    cp -v "$1" "$ORIG/$2"
}

# 1. Backup originais (só se ainda não existem)
if [ ! -f "$ORIG/lwipopts.h" ]; then
    mkdir -p "$ORIG"
    cp -v "$FW/include/lwipopts.h" "$ORIG/"
    echo "[patch] original salvo em $ORIG/"
else
    echo "[patch] original lwipopts.h já está em $ORIG/ — preservando"
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
save_original "$BEARSSL" "WiFiClientSecureBearSSL.cpp"
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
save_original "$HTTPC" "HTTPClient.cpp"
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
#   (Na epoca o pool tinha 12 entradas e a margem era estreita. Hoje sao 24 e
#   o que aperta e a JANELA, nao o tamanho do pool — ver D14.)
CTXH="$FW/libraries/WiFi/src/include/ClientContext.h"
CTXH_PATCH="$OVR/patches/clientcontext_rx_leak.patch"
save_original "$CTXH" "ClientContext.h"
if grep -q "SIMUT override: release buffered RX before abandoning the pcb" "$CTXH"; then
    echo "[patch] ClientContext ja libera o RX no close/abort — nada a fazer"
else
    echo "[patch] aplicando liberacao de _rx_buf no close/abort do ClientContext"
    patch -p1 -d "$FW" < "$CTXH_PATCH"
fi

# 2e. Parse do request sem prazo global (Parsing.cpp) — D-B8.
#   readStringUntil espera o timeout do cliente POR BYTE e o reinicia a cada
#   byte recebido, entao um cliente que goteja um byte logo abaixo do timeout
#   segura o Core 0 dentro da leitura para sempre, sem nada alimentar o watchdog
#   — o loop SIMUT alimenta ANTES do handleClient, nunca durante. Um unico GET
#   lento, sem autenticacao, reiniciou o aparelho: autopsia ao vivo
#   C0=[WEB_POLL] hp=0 sc3=0x80088013 (219), i.e. o handleClient NAO retornou.
#   Medido na bancada em 2026-08-10 com 1 request a 3 s/byte; curado nos 3
#   vetores (0,4/1,0/3,0 s por byte), 0 reboots, operacao normal 40/40.
#
PARSING="$FW/libraries/WebServer/src/Parsing.cpp"
PARSING_PATCH="$OVR/patches/webserver_parse_deadline.patch"
save_original "$PARSING" "Parsing.cpp"
if grep -q "SIMUT override — bound the request parse as a whole" "$PARSING"; then
    echo "[patch] Parsing ja tem prazo global — nada a fazer"
else
    echo "[patch] aplicando prazo global no parse do request"
    patch -p1 -d "$FW" < "$PARSING_PATCH"
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
# WebServer tem cache proprio (lib*/WebServer): sem apagar Parsing.cpp.o o build
# "passa" ainda com o parse sem prazo.
for wsobj in "$ROOT/.pio/build"/*/lib*/WebServer/Parsing.cpp.o; do
    if [ -f "$wsobj" ]; then
        rm -f "$wsobj"
        echo "[patch] cache invalidado: $wsobj"
    fi
done

echo ""
echo "[patch] DONE. Próximo \`pio run\` recompila lwIP com TCP_WND=4*MSS e stats ligados."
echo "[patch] PBUF_POOL_SIZE fica nos 24 de fábrica — ver D14 sobre por quê."
echo "[patch] Reverter: bash tools/arduino_pico_overrides/restore.sh"
