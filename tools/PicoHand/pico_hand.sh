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

        # Drain any leftover garbage from previous sessions.
        timeout 0.1 cat "$port" > /dev/null 2>&1 || true

        # Send PING and wait up to 0.5s for a response.
        echo "PING" > "$port" 2>/dev/null || continue
        resp=$(timeout 0.5 head -n 1 < "$port" 2>/dev/null || true)
        resp="${resp%$'\r'}"   # strip trailing CR if present

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
#      0  if response starts with OK or PONG
#      1  if response starts with ERR
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

    echo "$cmd" > "$PICO_HAND_PORT" || return 3

    resp=$(timeout "$PICO_HAND_TIMEOUT" head -n 1 < "$PICO_HAND_PORT" || true)
    resp="${resp%$'\r'}"

    if [[ -z "$resp" ]]; then
        echo "[pico_hand] timeout waiting for response to '$cmd'" >&2
        return 2
    fi

    echo "$resp"
    case "$resp" in
        OK*|PONG*) return 0 ;;
        ERR*)      return 1 ;;
        *)         return 1 ;;   # anything else treated as failure
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
