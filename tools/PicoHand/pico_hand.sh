#!/usr/bin/env bash
# =============================================================================
#  pico_hand.sh
#
#  Bash wrapper for the "robotic hand" (pico_hand firmware).
#
#  Typical script usage:
#      source /path/to/pico_hand.sh
#      hand_init              # detect port, return 1 if not found
#      hand RESET             # send command, echo response, exit 0 on OK
#      hand BOOTSEL
#
#  User-configurable variables:
#      PICO_HAND_PORT     # if set, skip auto-detection
#      PICO_HAND_TIMEOUT  # seconds to wait for response (default: 2)
# =============================================================================

# Do not use 'set -e' here — this file is *sourced*, so it would kill the
# user's shell on any failure. We handle exit codes manually.

# Default response timeout (seconds).
: "${PICO_HAND_TIMEOUT:=2}"

# -----------------------------------------------------------------------------
#  Configure a serial port to raw mode suitable for the hand protocol.
#
#  Args:
#      $1 - port path (e.g., /dev/ttyACM1)
#  Returns:
#      0 on success, non-zero if the port cannot be configured.
# -----------------------------------------------------------------------------
_hand_configure_port() {
    local port="$1"
    stty -F "$port" 115200 raw -echo -echoe -echok -echoctl -echoke 2>/dev/null
}

# -----------------------------------------------------------------------------
#  Send one command and return the first substantive response line.
#
#  The port is opened ONCE, read-write, and held for the whole exchange.
#  This is not a style preference. Writing with `echo > port` and reading with
#  `head < port` opens and closes the device twice: the hand answers in the
#  window between the write handle closing and the read handle opening, with no
#  reader attached, and the reply is gone. Measured on the bench — the split
#  form returns an empty string every time while this one returns PONG.
#
#  Lines emitted by DEBUG mode ("[DBG t=...] RX line: ...") are skipped, so the
#  wrapper keeps working when verbose logging is left on. Without that, even
#  hand_detect_port fails: it reads the debug echo instead of PONG.
#
#  Args:
#      $1 - port path
#      $2 - command string
#      $3 - timeout in seconds (optional, defaults to PICO_HAND_TIMEOUT)
#  Stdout: first non-debug response line, CR stripped.
#  Returns: 0 if a line was read, 1 on timeout or I/O error.
# -----------------------------------------------------------------------------
_hand_exchange() {
    local port="$1" cmd="$2" tmo="${3:-$PICO_HAND_TIMEOUT}"
    local line deadline

    exec 3<>"$port" || return 1

    # Drain anything left over from a previous session before asking.
    while read -r -t 0.05 line <&3; do :; done

    printf '%s\n' "$cmd" >&3 || { exec 3>&-; return 1; }

    deadline=$(( SECONDS + ${tmo%.*} + 1 ))
    while (( SECONDS < deadline )); do
        if read -r -t "$tmo" line <&3; then
            line="${line%$'\r'}"
            [[ -z "$line" ]] && continue
            [[ "$line" == \[DBG* ]] && continue
            printf '%s\n' "$line"
            exec 3>&-
            return 0
        else
            break
        fi
    done

    exec 3>&-
    return 1
}

# -----------------------------------------------------------------------------
#  Auto-detect the hand's port by sending PING and waiting for PONG.
#
#  Searches /dev/ttyACM* first (common for Linux CDC), then /dev/ttyUSB*
#  as fallback.
#
#  Stdout: path of the found port.
#  Returns: 0 if found, 1 otherwise.
# -----------------------------------------------------------------------------
hand_detect_port() {
    # If user forced a port, respect it.
    if [[ -n "${PICO_HAND_PORT:-}" ]] && [[ -e "$PICO_HAND_PORT" ]]; then
        echo "$PICO_HAND_PORT"
        return 0
    fi

    local port resp
    for port in /dev/ttyACM* /dev/ttyUSB*; do
        # Glob doesn't expand when no match → skip literals.
        [[ -e "$port" ]] || continue

        _hand_configure_port "$port" || continue

        resp=$(_hand_exchange "$port" "PING" 1 2>/dev/null)
        if [[ "$resp" == "PONG" ]]; then
            echo "$port"
            return 0
        fi
    done
    return 1
}

# -----------------------------------------------------------------------------
#  Initialize internal state: detect the port and configure it.
#
#  Stderr: informational message (port found) or error.
#  Returns: 0 on success, 1 if hand was not found.
# -----------------------------------------------------------------------------
hand_init() {
    local port
    if ! port=$(hand_detect_port); then
        echo "[pico_hand] ERROR: hand not found (PING without PONG)" >&2
        return 1
    fi
    PICO_HAND_PORT="$port"
    export PICO_HAND_PORT
    _hand_configure_port "$PICO_HAND_PORT"
    echo "[pico_hand] hand detected at: $PICO_HAND_PORT" >&2
    return 0
}

# -----------------------------------------------------------------------------
#  Send a command to the hand and echo the first line of response.
#
#  Args:
#      $@ - full command (e.g., hand HOLD BOOTSEL)
#  Stdout: response line from the hand.
#  Returns:
#      0  if the response is a success reply for its command
#      1  if response starts with ERR (or is unrecognised)
#      2  if no response within timeout
#      3  if hand not initialized and detection failed
# -----------------------------------------------------------------------------
hand() {
    # Lazy-init if not done yet.
    if [[ -z "${PICO_HAND_PORT:-}" ]] || [[ ! -e "${PICO_HAND_PORT}" ]]; then
        hand_init || return 3
    fi

    local cmd="$*"
    local resp

    if ! resp=$(_hand_exchange "$PICO_HAND_PORT" "$cmd"); then
        echo "[pico_hand] timeout waiting for response to '$cmd'" >&2
        return 2
    fi

    echo "$resp"
    # Query commands answer with their own banner rather than OK — treating
    # those as failures made STATUS, PINOUT and VERIFY unusable from scripts.
    case "$resp" in
        OK*|PONG*|STATUS*|PINOUT*|VFY*|DONE*) return 0 ;;
        PROBE*|EDGE*)                         return 0 ;;
        ERR*)                                 return 1 ;;
        *)                                    return 1 ;;
    esac
}

# -----------------------------------------------------------------------------
#  Convenience: ensure everything is released if the script is interrupted
#  with Ctrl+C or error. Use at the start of long scripts:
#
#      trap hand_release_all EXIT
# -----------------------------------------------------------------------------
hand_release_all() {
    # Best-effort: don't abort if the hand already disappeared.
    hand RELEASE BOOTSEL > /dev/null 2>&1 || true
    hand RELEASE RESET   > /dev/null 2>&1 || true
}
