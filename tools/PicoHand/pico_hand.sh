#!/usr/bin/env bash
# =============================================================================
#  pico_hand.sh
#
#  Wrapper bash para a "mao robotica" (firmware pico_hand).
#
#  Uso tipico em scripts:
#      source /caminho/para/pico_hand.sh
#      hand_init              # detecta porta, retorna 1 se nao achar
#      hand RESET             # envia comando, ecoa resposta, exit 0 se OK
#      hand BOOTSEL
#
#  Variaveis controlaveis pelo usuario:
#      PICO_HAND_PORT   # se setada, pula a deteccao automatica
#      PICO_HAND_TIMEOUT # segundos de espera por resposta (padrao: 2)
# =============================================================================

# Nao usar 'set -e' aqui porque este arquivo e *sourced* — derrubaria o shell
# do usuario em qualquer falha. Tratamos exit codes manualmente.

# Timeout padrao para resposta de comando (em segundos).
: "${PICO_HAND_TIMEOUT:=2}"

# -----------------------------------------------------------------------------
#  Configura uma porta serial em modo "raw" adequado para o protocolo da mao.
#
#  Args:
#      $1 - caminho da porta (ex.: /dev/ttyACM1)
#  Retorna:
#      0 em sucesso, !=0 se a porta nao puder ser configurada.
# -----------------------------------------------------------------------------
_hand_configure_port() {
    local port="$1"
    stty -F "$port" 115200 raw -echo -echoe -echok -echoctl -echoke 2>/dev/null
}

# -----------------------------------------------------------------------------
#  Tenta descobrir automaticamente a porta da mao enviando PING e
#  esperando PONG.
#
#  Procura primeiro em /dev/ttyACM* (comum no Linux para CDC), depois em
#  /dev/ttyUSB* como fallback.
#
#  Stdout: caminho da porta encontrada.
#  Retorna: 0 se achou, 1 caso contrario.
# -----------------------------------------------------------------------------
hand_detect_port() {
    # Se o usuario forcou uma porta, respeita.
    if [[ -n "${PICO_HAND_PORT:-}" ]] && [[ -e "$PICO_HAND_PORT" ]]; then
        echo "$PICO_HAND_PORT"
        return 0
    fi

    local port resp
    for port in /dev/ttyACM* /dev/ttyUSB*; do
        # O glob nao expande se nao houver match -> ignora literais.
        [[ -e "$port" ]] || continue

        _hand_configure_port "$port" || continue

        # Esvazia qualquer lixo pendente no buffer (lixo de sessoes anteriores).
        timeout 0.1 cat "$port" > /dev/null 2>&1 || true

        # Envia PING e espera ate 0.5s por resposta.
        echo "PING" > "$port" 2>/dev/null || continue
        resp=$(timeout 0.5 head -n 1 < "$port" 2>/dev/null || true)
        resp="${resp%$'\r'}"   # remove CR final, se houver

        if [[ "$resp" == "PONG" ]]; then
            echo "$port"
            return 0
        fi
    done
    return 1
}

# -----------------------------------------------------------------------------
#  Inicializa o estado interno: descobre a porta e configura-a.
#
#  Stderr: mensagem informativa (porta encontrada) ou erro.
#  Retorna: 0 em sucesso, 1 se a mao nao foi encontrada.
# -----------------------------------------------------------------------------
hand_init() {
    local port
    if ! port=$(hand_detect_port); then
        echo "[pico_hand] ERRO: mao nao encontrada (PING sem PONG)" >&2
        return 1
    fi
    PICO_HAND_PORT="$port"
    export PICO_HAND_PORT
    echo "[pico_hand] mao detectada em: $PICO_HAND_PORT" >&2
    return 0
}

# -----------------------------------------------------------------------------
#  Envia um comando para a mao e ecoa a primeira linha de resposta.
#
#  Args:
#      $@ - comando completo (ex.: hand HOLD BOOTSEL)
#  Stdout: linha de resposta da mao.
#  Retorna:
#      0  se a resposta comeca com OK ou PONG
#      1  se a resposta comeca com ERR
#      2  se nao houve resposta dentro do timeout
#      3  se a mao nao foi inicializada e a deteccao falhou
# -----------------------------------------------------------------------------
hand() {
    # Inicializa sob demanda se ainda nao foi feito.
    if [[ -z "${PICO_HAND_PORT:-}" ]] || [[ ! -e "${PICO_HAND_PORT}" ]]; then
        hand_init || return 3
    fi

    local cmd="$*"
    local resp

    echo "$cmd" > "$PICO_HAND_PORT" || return 3

    resp=$(timeout "$PICO_HAND_TIMEOUT" head -n 1 < "$PICO_HAND_PORT" || true)
    resp="${resp%$'\r'}"

    if [[ -z "$resp" ]]; then
        echo "[pico_hand] timeout aguardando resposta de '$cmd'" >&2
        return 2
    fi

    echo "$resp"
    case "$resp" in
        OK*|PONG*) return 0 ;;
        ERR*)      return 1 ;;
        *)         return 1 ;;   # qualquer outra coisa tratamos como falha
    esac
}

# -----------------------------------------------------------------------------
#  Conveniencias: garantem que tudo seja liberado caso o script seja
#  interrompido com Ctrl+C ou erro. Use no inicio de scripts longos:
#
#      trap hand_release_all EXIT
# -----------------------------------------------------------------------------
hand_release_all() {
    # Best-effort: nao queremos abortar se a mao ja sumiu.
    hand RELEASE BOOTSEL > /dev/null 2>&1 || true
    hand RELEASE RESET   > /dev/null 2>&1 || true
}
