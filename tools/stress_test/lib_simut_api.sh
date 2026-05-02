#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# SIMUT — biblioteca compartilhada para scripts de stress test.
# Source: `source "$(dirname "$0")/lib_simut_api.sh"`
#
# Variáveis de ambiente:
#   SIMUT_IP    — IP do device (ex.: 192.168.3.195)
#   SIMUT_USER  — usuário (default: admin)
#   SIMUT_PASS  — senha plaintext (será hashada client-side)
#   COOKIE_JAR  — path do cookie jar (default: /tmp/simut_jar_$$.txt)
#
# Funções exportadas:
#   simut_login                                    — POST /api/login com challenge SHA256
#   simut_logout                                   — POST /api/logout
#   simut_ls <dir>                                 — GET /api/ls?dir=<dir>, retorna JSON
#   simut_download <remote_path> <local_path>      — GET /download?file=<path>
#   simut_upload <local_path> <remote_dir>         — POST /api/upload com uploadDir
#   simut_delete <remote_path>                     — POST /api/delete?file=<path>
#   simut_mkdir <remote_dir>                       — POST /api/mkdir?dir=<dir>
#   simut_get_json <api_path>                      — GET <api_path>, valida JSON
#   simut_metrics_telsent                          — extrai metrics.telSent via Serial CLI
#
# Requisitos: bash 4+, curl, openssl, iconv, jq (opcional), python3.
# -----------------------------------------------------------------------------

set -u

: "${SIMUT_IP:?SIMUT_IP nao setado (ex.: SIMUT_IP=192.168.3.195)}"
: "${SIMUT_USER:=admin}"
: "${SIMUT_PASS:=admin}"
: "${COOKIE_JAR:=/tmp/simut_jar_$$.txt}"
: "${SIMUT_API_RATE_DELAY:=0.5}"   # seg entre chamadas pra evitar 429

SIMUT_BASE="http://${SIMUT_IP}"

# Limpa cookie jar no exit (a menos que SIMUT_KEEP_JAR=1).
[[ -z "${SIMUT_KEEP_JAR:-}" ]] && trap 'rm -f "$COOKIE_JAR"' EXIT

# --- Logging utilitário ---
_simut_log()   { echo "[$(date +%H:%M:%S)] $*"; }
_simut_warn()  { echo "[$(date +%H:%M:%S)] ⚠️  $*" >&2; }
_simut_error() { echo "[$(date +%H:%M:%S)] ❌  $*" >&2; }
_simut_ok()    { echo "[$(date +%H:%M:%S)] ✅  $*"; }

# --- SHA256 client-side: matches WebUI.h sha256() (Latin-1 charCodeAt bytes) ---
_simut_sha256_latin1() {
    printf '%s' "$1" | iconv -f UTF-8 -t LATIN1 2>/dev/null | sha256sum | cut -d' ' -f1
}

# --- Login: nonce → challenge → POST ---
simut_login() {
    rm -f "$COOKIE_JAR"
    local init nonce pre resp
    init=$(curl -s -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
                "${SIMUT_BASE}/api/login_init?u=${SIMUT_USER}")
    nonce=$(echo "$init" | python3 -c "import json,sys;print(json.load(sys.stdin).get('nonce',''))" 2>/dev/null)
    if [[ -z "$nonce" ]]; then
        _simut_error "login_init falhou: $init"
        return 1
    fi
    pre=$(_simut_sha256_latin1 "$SIMUT_PASS")
    resp=$(curl -s -c "$COOKIE_JAR" -b "$COOKIE_JAR" -X POST \
                -H "Content-Type: application/x-www-form-urlencoded" \
                --data-urlencode "user=${SIMUT_USER}" \
                --data-urlencode "pass=${pre}" \
                --data-urlencode "nonce=${nonce}" \
                "${SIMUT_BASE}/api/login")
    if echo "$resp" | grep -q '"ok":true'; then
        _simut_ok "login: $SIMUT_USER"
        return 0
    fi
    _simut_error "login falhou: $resp"
    return 1
}

simut_logout() {
    curl -s -b "$COOKIE_JAR" "${SIMUT_BASE}/api/logout" >/dev/null 2>&1 || true
}

# --- URL-encode helper (espacos, slashes etc) ---
_simut_urlenc() {
    python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1], safe=''))" "$1"
}

# --- /api/ls?dir=<dir> ---
# Uso: simut_ls /history → JSON
simut_ls() {
    sleep "$SIMUT_API_RATE_DELAY"
    local dir="${1:-/}"
    local enc
    enc=$(_simut_urlenc "$dir")
    curl -s -b "$COOKIE_JAR" "${SIMUT_BASE}/api/ls?dir=${enc}"
}

# --- /download?file=<path> → arquivo local ---
# Uso: simut_download /history/20260502.bin /tmp/local.bin
simut_download() {
    sleep "$SIMUT_API_RATE_DELAY"
    local remote="$1" local_path="$2"
    local enc http_code
    enc=$(_simut_urlenc "$remote")
    http_code=$(curl -s -b "$COOKIE_JAR" -o "$local_path" -w "%{http_code}" \
                     "${SIMUT_BASE}/download?file=${enc}")
    if [[ "$http_code" != "200" ]]; then
        _simut_error "download $remote → HTTP $http_code"
        rm -f "$local_path"
        return 1
    fi
    return 0
}

# --- /api/upload com uploadDir + multipart file ---
# Uso: simut_upload /tmp/local.bin /history
simut_upload() {
    sleep "$SIMUT_API_RATE_DELAY"
    local local_path="$1" remote_dir="${2:-/}"
    if [[ ! -f "$local_path" ]]; then
        _simut_error "arquivo local nao existe: $local_path"
        return 1
    fi
    local fname
    fname=$(basename "$local_path")
    local resp
    resp=$(curl -s -b "$COOKIE_JAR" -X POST \
                -F "uploadDir=${remote_dir}" \
                -F "file=@${local_path};filename=${fname}" \
                "${SIMUT_BASE}/api/upload")
    if echo "$resp" | grep -q '"status":"ok"'; then
        return 0
    fi
    _simut_error "upload $local_path → $remote_dir falhou: $resp"
    return 1
}

# --- /api/delete?file=<path> ---
simut_delete() {
    sleep "$SIMUT_API_RATE_DELAY"
    local remote="$1"
    local resp
    resp=$(curl -s -b "$COOKIE_JAR" -X POST \
                --data-urlencode "file=${remote}" \
                "${SIMUT_BASE}/api/delete")
    if echo "$resp" | grep -q '"status":"ok"'; then
        return 0
    fi
    _simut_error "delete $remote falhou: $resp"
    return 1
}

# --- /api/mkdir?dir=<dir> ---
simut_mkdir() {
    sleep "$SIMUT_API_RATE_DELAY"
    local remote="$1"
    local resp
    resp=$(curl -s -b "$COOKIE_JAR" -X POST \
                --data-urlencode "dir=${remote}" \
                "${SIMUT_BASE}/api/mkdir")
    echo "$resp"
}

# --- GET genérico em endpoint API que retorna JSON ---
simut_get_json() {
    sleep "$SIMUT_API_RATE_DELAY"
    curl -s -b "$COOKIE_JAR" "${SIMUT_BASE}$1"
}

# --- Walk recursivo: imprime "f|<path>|<size>" pra cada arquivo ---
# Uso: simut_walk_files /
simut_walk_files() {
    local start="${1:-/}"
    local stack=("$start")
    while [[ ${#stack[@]} -gt 0 ]]; do
        local cur="${stack[-1]}"
        unset 'stack[-1]'
        local listing
        listing=$(simut_ls "$cur")
        if ! echo "$listing" | python3 -c "import json,sys;json.load(sys.stdin)" 2>/dev/null; then
            _simut_warn "ls $cur falhou"
            continue
        fi
        # Parse entries
        local entries
        entries=$(echo "$listing" | python3 -c "
import json,sys
d = json.load(sys.stdin)
for e in d.get('entries', []):
    n, t, s = e.get('n','?'), e.get('t','?'), e.get('s',0)
    print(f'{t}|{n}|{s}')
")
        while IFS='|' read -r typ name size; do
            [[ -z "$typ" ]] && continue
            local fullpath
            if [[ "$cur" == "/" ]]; then
                fullpath="/${name}"
            else
                fullpath="${cur}/${name}"
            fi
            if [[ "$typ" == "f" ]]; then
                printf 'f|%s|%s\n' "$fullpath" "$size"
            elif [[ "$typ" == "d" ]]; then
                stack+=("$fullpath")
                printf 'd|%s|%s\n' "$fullpath" "$size"
            fi
        done <<< "$entries"
    done
}

# --- Serial CLI helper: roda comando e retorna output ---
# Requer python3 + /tmp/serial_query.py disponível
simut_serial_cmd() {
    local cmd="$1" duration="${2:-3}"
    if [[ ! -x /tmp/serial_query.py ]]; then
        cat > /tmp/serial_query.py <<'PYEOF'
#!/usr/bin/env python3
import sys, time, os, fcntl, termios
PORT = "/dev/ttyACM0"
fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
attrs = termios.tcgetattr(fd)
attrs[0] = 0; attrs[1] = 0
attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
attrs[3] = 0
attrs[4] = termios.B115200; attrs[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, attrs)
termios.tcflush(fd, termios.TCIOFLUSH)
cmd = sys.argv[1] if len(sys.argv) > 1 else "show system info"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
os.write(fd, (cmd + "\r\n").encode())
time.sleep(0.1)
end = time.time() + duration
buf = b""
while time.time() < end:
    try:
        chunk = os.read(fd, 1024)
        if chunk: buf += chunk
    except BlockingIOError: pass
    time.sleep(0.05)
os.close(fd)
sys.stdout.buffer.write(buf)
PYEOF
        chmod +x /tmp/serial_query.py
    fi
    python3 /tmp/serial_query.py "$cmd" "$duration" 2>/dev/null
}
