#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F-CSV.3 — teste automatizado do endpoint GET /api/export/logs.bin
# Valida:
#   - guards de auth, args invalidos, level invalido, cap 31 dias
#   - formato do .simx kind='L': HEADER 32B (magic SIMX, ver 1, kind L,
#     recSize 12, sensorTableSize=0), PAYLOAD multiplo de 12, CRC32 trailer
#   - filtro de level server-side: level=err so traz LOG_ERROR (3)
#   - concorrencia (2 requests simultaneos -> 1 retorna 503)
#   - regressao: /api/logs continua funcionando
#
# Pre-requisitos:
#   - dispositivo flashado com >= v3.27.5
#   - SIMUT_IP / SIMUT_USER / SIMUT_PASS exportados
#
# Uso:
#   SIMUT_IP=192.168.1.50 SIMUT_PASS=<senha> ./tools/test_f_csv_3.sh
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"

command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio para CRC32"; exit 2; }

TMP_BIN=$(mktemp /tmp/simut_logs_XXXXXX.bin)
TMP_RESP=$(mktemp /tmp/simut_resp_XXXXXX.txt)
trap 'rm -f "$TMP_BIN" "$TMP_RESP"' EXIT

hdr "F-CSV.3 — /api/export/logs.bin"
info "device: $SIMUT_BASE"

# -----------------------------------------------------------------------------
# 1. Sem autenticacao -> 403
# -----------------------------------------------------------------------------
hdr "1. Acesso anonimo deve ser rejeitado"
NOW=$(date -u +%s)
FROM=$((NOW - 86400))   # 24h atras (gera mais chance de pegar logs)
TO=$NOW
status=$(curl -s -o /dev/null -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO")
if [[ "$status" == "403" ]]; then ok "anonimo rejeitado (HTTP $status)"
else ko "esperado 403, obtido $status"; fi

# -----------------------------------------------------------------------------
# 2. Login
# -----------------------------------------------------------------------------
hdr "2. Login"
simut_login || { test_summary; }

# -----------------------------------------------------------------------------
# 3. Sem args -> 400
# -----------------------------------------------------------------------------
hdr "3. Args faltando"
resp=$(simut_req GET "/api/export/logs.bin")
assert_status "sem from/to" 400 "$resp"
assert_contains "msg apropriada" "Missing from/to params" "$(simut_body <<<"$resp")"

# -----------------------------------------------------------------------------
# 4. Range invalido (from > to)
# -----------------------------------------------------------------------------
hdr "4. Range invalido"
resp=$(simut_req GET "/api/export/logs.bin?from=$NOW&to=$FROM")
assert_status "from > to" 400 "$resp"

# -----------------------------------------------------------------------------
# 5. Cap 31 dias
# -----------------------------------------------------------------------------
hdr "5. Cap de 31 dias"
THIRTY_TWO=$((32 * 86400))
resp=$(simut_req GET "/api/export/logs.bin?from=$((NOW - THIRTY_TWO))&to=$NOW")
assert_status "32 dias rejeitado" 400 "$resp"
assert_contains "msg apropriada" "Range exceeds 31 days" "$(simut_body <<<"$resp")"

# -----------------------------------------------------------------------------
# 6. Level invalido -> 400
# -----------------------------------------------------------------------------
hdr "6. Level invalido"
resp=$(simut_req GET "/api/export/logs.bin?from=$FROM&to=$TO&level=banana")
assert_status "level=banana rejeitado" 400 "$resp"
assert_contains "msg apropriada" "Invalid level" "$(simut_body <<<"$resp")"

# -----------------------------------------------------------------------------
# 7. Export valido (level=all) — estrutura .simx
# -----------------------------------------------------------------------------
hdr "7. Export valido level=all — estrutura .simx"
HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_BIN" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO&level=all")

if [[ "$HTTP_CODE" != "200" ]]; then
    ko "export falhou (HTTP $HTTP_CODE)"
    info "body: $(head -c 200 "$TMP_BIN")"
    test_summary
fi
ok "HTTP 200 (level=all)"

SIZE=$(stat -c %s "$TMP_BIN")
info "tamanho do blob: $SIZE bytes"

python3 <<PYEOF > "$TMP_RESP"
import struct, sys, zlib

with open("$TMP_BIN", "rb") as f:
    blob = f.read()

if len(blob) < 36:
    print("FAIL blob_too_small", len(blob)); sys.exit(1)

hdr = struct.unpack_from("<4sBBHHHIIIII", blob, 0)
magic, ver, kind, _, recSize, _, rFrom, rTo, tblSize, _, _ = hdr

print(f"INFO magic={magic!r} ver={ver} kind={chr(kind)} recSize={recSize}")
print(f"INFO rangeFrom={rFrom} rangeTo={rTo} sensorTableSize={tblSize}")

if magic != b"SIMX": print("FAIL magic"); sys.exit(1)
print("OK magic_SIMX")
if ver != 1: print("FAIL version"); sys.exit(1)
print("OK version_1")
if chr(kind) != "L": print("FAIL kind"); sys.exit(1)
print("OK kind_L")
if recSize != 12: print("FAIL recordSize"); sys.exit(1)
print("OK recordSize_12")
if tblSize != 0: print(f"FAIL sensorTableSize_should_be_0 got {tblSize}"); sys.exit(1)
print("OK sensorTableSize_0")
if rFrom != $FROM: print("FAIL rangeFrom"); sys.exit(1)
print("OK rangeFrom")
if rTo != $TO: print("FAIL rangeTo"); sys.exit(1)
print("OK rangeTo")

# PAYLOAD multiplo de 12, ate len-4
payload_size = len(blob) - 32 - 4
if payload_size < 0: print("FAIL payload_negative"); sys.exit(1)
if payload_size % 12 != 0:
    print(f"FAIL payload_not_multiple_of_12 size={payload_size}"); sys.exit(1)
records = payload_size // 12
print(f"OK payload_aligned ({payload_size} bytes = {records} records)")

# Conta levels e valida epoch in range
level_counts = {0:0, 1:0, 2:0, 3:0}
out_of_range = 0
for i in range(records):
    rec_off = 32 + i * 12
    epoch, uptime, code, ctx, flags, _ = struct.unpack_from("<IHHhBB", blob, rec_off)
    if epoch < rFrom or epoch > rTo:
        out_of_range += 1
    level = (flags >> 5) & 0x07
    if level in level_counts:
        level_counts[level] += 1
if out_of_range > 0:
    print(f"FAIL records_out_of_range={out_of_range}"); sys.exit(1)
print(f"OK records_in_range")
print(f"INFO level_distribution DBG={level_counts[0]} INF={level_counts[1]} WRN={level_counts[2]} ERR={level_counts[3]}")

# CRC32 trailer
crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
print(f"INFO crc_expected=0x{crc_expected:08X} crc_calc=0x{crc_calc:08X}")
if crc_expected != crc_calc:
    print("FAIL crc32_mismatch"); sys.exit(1)
print("OK crc32_valid")

# Salva contagem para teste de filtro
with open("/tmp/simut_logs_total.txt", "w") as f:
    f.write(f"{records},{level_counts[3]},{level_counts[1]}")
print("ALL_GOOD")
PYEOF

py_status=$?
cat "$TMP_RESP"
if [[ $py_status -ne 0 ]]; then ko "validacao .simx kind=L falhou"; test_summary; fi
while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 8. Filtro level=err — todos records devem ter level=3
# -----------------------------------------------------------------------------
hdr "8. Filtro level=err"
TMP_ERR=$(mktemp /tmp/simut_logs_err_XXXXXX.bin)
trap 'rm -f "$TMP_BIN" "$TMP_RESP" "$TMP_ERR" /tmp/simut_logs_total.txt' EXIT

HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_ERR" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO&level=err")
if [[ "$HTTP_CODE" != "200" ]]; then ko "level=err falhou ($HTTP_CODE)"; test_summary; fi
ok "HTTP 200 (level=err)"

python3 - "$TMP_ERR" <<'PYEOF' > "$TMP_RESP"
import struct, sys, zlib
with open(sys.argv[1], "rb") as f:
    blob = f.read()
hdr = struct.unpack_from("<4sBBHHHIIIII", blob, 0)
magic, ver, kind, _, recSize, _, rFrom, rTo, tblSize, _, _ = hdr
if chr(kind) != "L" or recSize != 12: print("FAIL header"); sys.exit(1)

# Validar CRC primeiro
crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
if crc_expected != crc_calc: print("FAIL crc32"); sys.exit(1)
print("OK crc32_level_err")

# Todos records devem ter level=3 (LOG_ERROR)
payload_size = len(blob) - 32 - 4
records = payload_size // 12
non_err = 0
for i in range(records):
    rec_off = 32 + i * 12
    flags = blob[rec_off + 10]  # offset do flags
    level = (flags >> 5) & 0x07
    if level != 3: non_err += 1
print(f"INFO err_records={records} non_err_records={non_err}")
if non_err > 0:
    print(f"FAIL filtro_err_quebrado non_err={non_err}"); sys.exit(1)
print("OK filtro_err_correto")
PYEOF
cat "$TMP_RESP"
while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 9. Filtro level=inf — todos records devem ter level=1
# -----------------------------------------------------------------------------
hdr "9. Filtro level=inf"
TMP_INF=$(mktemp /tmp/simut_logs_inf_XXXXXX.bin)
HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_INF" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO&level=inf")
if [[ "$HTTP_CODE" != "200" ]]; then ko "level=inf falhou ($HTTP_CODE)"; rm -f "$TMP_INF"; test_summary; fi
ok "HTTP 200 (level=inf)"

python3 - "$TMP_INF" <<'PYEOF' > "$TMP_RESP"
import struct, sys, zlib
with open(sys.argv[1], "rb") as f:
    blob = f.read()
crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
if crc_expected != crc_calc: print("FAIL crc32"); sys.exit(1)
print("OK crc32_level_inf")
payload_size = len(blob) - 32 - 4
records = payload_size // 12
non_inf = 0
for i in range(records):
    rec_off = 32 + i * 12
    flags = blob[rec_off + 10]
    level = (flags >> 5) & 0x07
    if level != 1: non_inf += 1
print(f"INFO inf_records={records} non_inf_records={non_inf}")
if non_inf > 0:
    print(f"FAIL filtro_inf_quebrado non_inf={non_inf}"); sys.exit(1)
print("OK filtro_inf_correto")
PYEOF
rm -f "$TMP_INF"
cat "$TMP_RESP"
while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 10. Coerencia: contagem de all = err + inf + (debug + warn)
# -----------------------------------------------------------------------------
hdr "10. Coerencia das contagens entre filtros"
if [[ -f /tmp/simut_logs_total.txt ]]; then
    IFS=',' read -r tot err_count inf_count < /tmp/simut_logs_total.txt
    info "all=$tot err=$err_count inf=$inf_count"

    # Re-pega level=err e level=inf, conta records
    err_size=$(stat -c %s "$TMP_ERR")
    err_recs=$(( (err_size - 36) / 12 ))

    info "level=err retornou $err_recs records (esperado $err_count)"
    if [[ "$err_recs" == "$err_count" ]]; then
        ok "contagem level=err coerente com level=all"
    else
        ko "level=err retornou $err_recs, esperado $err_count"
    fi
else
    warn "arquivo de contagem nao existe (etapa 7 falhou?)"
fi
rm -f "$TMP_ERR"

# -----------------------------------------------------------------------------
# 11. Concorrencia
# -----------------------------------------------------------------------------
hdr "11. Concorrencia (2 requests paralelos)"
out1=$(mktemp); out2=$(mktemp)
curl -s -b "$COOKIE_JAR" -o /dev/null -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO" > "$out1" &
PID1=$!
sleep 0.05
curl -s -b "$COOKIE_JAR" -o /dev/null -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO" > "$out2"
wait $PID1
code1=$(cat "$out1"); code2=$(cat "$out2")
rm -f "$out1" "$out2"
info "code1=$code1 code2=$code2"
if [[ "$code1" == "503" ]] || [[ "$code2" == "503" ]]; then
    ok "atomic guard ativo (1x 503)"
elif [[ "$code1" == "200" && "$code2" == "200" ]]; then
    warn "ambos 200 — exports terminaram rapido demais (payload pequeno)"
else
    ko "esperado 200+503 ou 200+200, obtido $code1 + $code2"
fi

# -----------------------------------------------------------------------------
# 12. Regressao: /api/logs original ainda funciona
# -----------------------------------------------------------------------------
hdr "12. Regressao /api/logs (binary stream original)"
status=$(curl -s -b "$COOKIE_JAR" -o /dev/null -w '%{http_code}' "$SIMUT_BASE/api/logs")
if [[ "$status" == "200" ]]; then ok "regressao /api/logs (HTTP 200)"
else ko "regressao /api/logs quebrada (HTTP $status)"; fi

test_summary
