#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F-CSV.2 — teste automatizado do endpoint GET /api/export/history.bin
# Valida:
#   - guards de auth, args invalidos, cap 31 dias
#   - formato do .simx: HEADER 32B (magic SIMX, ver 1, kind H, recSize 28),
#     SENSOR_TABLE bem-formada, PAYLOAD multiplo de 28, CRC32 trailer correto
#   - concorrencia (2 requests simultaneos -> 1 retorna 503)
#
# Pre-requisitos:
#   - dispositivo flashado com >= v3.27.4 (handler /api/export/history.bin presente)
#   - pelo menos 1 sensor ativo + algum historico recente em /history/
#   - SIMUT_IP / SIMUT_USER / SIMUT_PASS exportados
#
# Uso:
#   SIMUT_IP=192.168.1.50 SIMUT_PASS=<senha> ./tools/test_f_csv_2.sh
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"

command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio para CRC32"; exit 2; }

TMP_BIN=$(mktemp /tmp/simut_export_XXXXXX.bin)
TMP_RESP=$(mktemp /tmp/simut_resp_XXXXXX.txt)
trap 'rm -f "$TMP_BIN" "$TMP_RESP"' EXIT

hdr "F-CSV.2 — /api/export/history.bin"
info "device: $SIMUT_BASE"

# -----------------------------------------------------------------------------
# 1. Sem autenticacao -> 403
# -----------------------------------------------------------------------------
hdr "1. Acesso anonimo deve ser rejeitado"
NOW=$(date -u +%s)
FROM=$((NOW - 3600))
TO=$NOW
status=$(curl -s -o /dev/null -w '%{http_code}' \
    "$SIMUT_BASE/api/export/history.bin?from=$FROM&to=$TO")
if [[ "$status" == "403" ]]; then ok "anonimo rejeitado (HTTP $status)"
else ko "esperado 403, obtido $status"; fi

# -----------------------------------------------------------------------------
# 2. Login (depois rodam todos os testes autenticados)
# -----------------------------------------------------------------------------
hdr "2. Login"
simut_login || { test_summary; }

# -----------------------------------------------------------------------------
# 3. Sem args -> 400
# -----------------------------------------------------------------------------
hdr "3. Args faltando"
resp=$(simut_req GET "/api/export/history.bin")
assert_status "sem from/to" 400 "$resp"
assert_contains "msg apropriada" "Missing from/to params" "$(simut_body <<<"$resp")"

# -----------------------------------------------------------------------------
# 4. Range invalido (from=0)
# -----------------------------------------------------------------------------
hdr "4. Range invalido (from=0)"
resp=$(simut_req GET "/api/export/history.bin?from=0&to=$NOW")
assert_status "from=0" 400 "$resp"

# -----------------------------------------------------------------------------
# 5. Range invalido (from > to)
# -----------------------------------------------------------------------------
hdr "5. Range invalido (from > to)"
resp=$(simut_req GET "/api/export/history.bin?from=$NOW&to=$FROM")
assert_status "from > to" 400 "$resp"

# -----------------------------------------------------------------------------
# 6. Range > 31 dias -> 400
# -----------------------------------------------------------------------------
hdr "6. Cap de 31 dias"
THIRTY_TWO_DAYS=$((32 * 86400))
resp=$(simut_req GET "/api/export/history.bin?from=$((NOW - THIRTY_TWO_DAYS))&to=$NOW")
assert_status "32 dias rejeitado" 400 "$resp"
assert_contains "msg apropriada" "Range exceeds 31 days" "$(simut_body <<<"$resp")"

# -----------------------------------------------------------------------------
# 7. Range valido (1h) — baixa o blob e valida estrutura .simx
# -----------------------------------------------------------------------------
hdr "7. Export valido — estrutura .simx"
HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_BIN" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/history.bin?from=$FROM&to=$TO")

if [[ "$HTTP_CODE" != "200" ]]; then
    ko "export falhou (HTTP $HTTP_CODE)"
    info "body: $(head -c 200 "$TMP_BIN")"
    test_summary
fi
ok "HTTP 200"

SIZE=$(stat -c %s "$TMP_BIN")
info "tamanho do blob: $SIZE bytes"

if [[ $SIZE -lt 36 ]]; then
    ko "blob menor que minimo (HEADER 32 + CRC 4 = 36)"
    test_summary
fi
ok "blob >= 36 bytes (HEADER + CRC minimo)"

# -----------------------------------------------------------------------------
# 7.1. Parse e validacao via Python (struct + CRC32)
# -----------------------------------------------------------------------------
python3 <<PYEOF > "$TMP_RESP"
import struct, sys, zlib

with open("$TMP_BIN", "rb") as f:
    blob = f.read()

if len(blob) < 36:
    print("FAIL blob_too_small", len(blob)); sys.exit(1)

# HEADER 32 B packed LE: 4s magic, B ver, B kind, H rsv, H recSize, H rsv, I from, I to, I tblSize, I rsv, I rsv
hdr = struct.unpack_from("<4sBBHHHIIIII", blob, 0)
magic, ver, kind, _, recSize, _, rFrom, rTo, tblSize, _, _ = hdr

print(f"INFO magic={magic!r} ver={ver} kind={chr(kind)} recSize={recSize}")
print(f"INFO rangeFrom={rFrom} rangeTo={rTo} sensorTableSize={tblSize}")

if magic != b"SIMX": print("FAIL magic"); sys.exit(1)
print("OK magic_SIMX")
if ver != 1: print("FAIL version", ver); sys.exit(1)
print("OK version_1")
if chr(kind) != "H": print("FAIL kind", chr(kind)); sys.exit(1)
print("OK kind_H")
if recSize != 28: print("FAIL recordSize", recSize); sys.exit(1)
print("OK recordSize_28")
if rFrom != $FROM: print(f"FAIL rangeFrom expected $FROM got {rFrom}"); sys.exit(1)
print("OK rangeFrom")
if rTo != $TO: print(f"FAIL rangeTo expected $TO got {rTo}"); sys.exit(1)
print("OK rangeTo")

# SENSOR_TABLE: parse entries (idx u8, hwLen u8, hw[], frLen u8, fr[])
off = 32
end = 32 + tblSize
sensors = []
while off < end:
    if end - off < 3:
        print("FAIL sensor_table_truncated_header"); sys.exit(1)
    idx = blob[off]; off += 1
    hwLen = blob[off]; off += 1
    if off + hwLen + 1 > end: print("FAIL sensor_hw_overrun"); sys.exit(1)
    hwId = blob[off:off+hwLen].decode("latin-1", errors="replace"); off += hwLen
    frLen = blob[off]; off += 1
    if off + frLen > end: print("FAIL sensor_fr_overrun"); sys.exit(1)
    friendly = blob[off:off+frLen].decode("latin-1", errors="replace"); off += frLen
    sensors.append((idx, hwId, friendly))
    print(f"INFO sensor idx={idx} hwId='{hwId}' friendly='{friendly}'")

if off != end:
    print(f"FAIL sensor_table_offset_mismatch off={off} expected={end}"); sys.exit(1)
print(f"OK sensor_table_parsed ({len(sensors)} entries)")

# PAYLOAD: multiplo de 28 ate len-4
payload_start = 32 + tblSize
payload_end = len(blob) - 4
payload_size = payload_end - payload_start
if payload_size < 0:
    print("FAIL payload_negative"); sys.exit(1)
if payload_size % 28 != 0:
    print(f"FAIL payload_not_multiple_of_28 size={payload_size}"); sys.exit(1)
print(f"OK payload_aligned ({payload_size} bytes = {payload_size // 28} records)")

# Validar epoch dentro do range em cada record
records = payload_size // 28
out_of_range = 0
for i in range(records):
    rec_off = payload_start + i * 28
    epoch = struct.unpack_from("<I", blob, rec_off)[0]
    if epoch < rFrom or epoch > rTo:
        out_of_range += 1
if out_of_range > 0:
    print(f"FAIL records_out_of_range={out_of_range}"); sys.exit(1)
print(f"OK records_in_range")

# CRC32: ultimos 4 bytes (LE)
crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
print(f"INFO crc_expected=0x{crc_expected:08X} crc_calc=0x{crc_calc:08X}")
if crc_expected != crc_calc:
    print("FAIL crc32_mismatch"); sys.exit(1)
print("OK crc32_valid")

print("ALL_GOOD")
PYEOF

py_status=$?
cat "$TMP_RESP"

if [[ $py_status -ne 0 ]]; then
    ko "validacao .simx falhou (parse/crc/struct)"
    test_summary
fi

while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
        ALL_GOOD) ;;
        INFO\ *) ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 8. Concorrencia: 2 requests simultaneos -> 1 deve retornar 503
# -----------------------------------------------------------------------------
hdr "8. Concorrencia (2 requests paralelos -> 503)"
RANGE_FROM_24H=$((NOW - 86400))
out1=$(mktemp); out2=$(mktemp)
curl -s -b "$COOKIE_JAR" -o /dev/null -w '%{http_code}' \
    "$SIMUT_BASE/api/export/history.bin?from=$RANGE_FROM_24H&to=$NOW" > "$out1" &
PID1=$!
sleep 0.05
curl -s -b "$COOKIE_JAR" -o /dev/null -w '%{http_code}' \
    "$SIMUT_BASE/api/export/history.bin?from=$RANGE_FROM_24H&to=$NOW" > "$out2"
wait $PID1
code1=$(cat "$out1"); code2=$(cat "$out2")
rm -f "$out1" "$out2"
info "code1=$code1 code2=$code2"
if [[ "$code1" == "503" ]] || [[ "$code2" == "503" ]]; then
    ok "request concorrente recebeu 503 (atomic guard ativo)"
elif [[ "$code1" == "200" && "$code2" == "200" ]]; then
    warn "ambos retornaram 200 — possivel se export 1 terminou antes do 2 iniciar"
else
    ko "esperado 1x 200 + 1x 503, obtido $code1 + $code2"
fi

# -----------------------------------------------------------------------------
# 9. Smoke: /api/history original ainda funciona (regressao)
# -----------------------------------------------------------------------------
hdr "9. Regressao /api/history (JSON)"
resp=$(simut_req GET "/api/history?sensor=-1&range=0")
status=$(simut_status <<<"$resp")
if [[ "$status" == "200" ]]; then ok "regressao /api/history (HTTP 200)"
else ko "regressao /api/history quebrada (HTTP $status)"; fi

test_summary
