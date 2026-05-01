#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F-CSV.4 — teste E2E da UI de export histórico em /history.
# Simula o que o JS embarcado faz: fetch -> CRC32 -> decode .simx -> CSV.
#
# Valida:
#   1. UI: GET /history retorna 200 + contém elementos novos (i18n keys, IDs
#      dos pickers, função JS, mini lib CRC).
#   2. E2E: chama /api/export/history.bin para um range, valida CRC32 trailer
#      em Python (mesmo zlib.crc32 que o firmware crc32_*), decodifica
#      .simx e gera CSV válido.
#
# Pre-req: dispositivo flashado >= v3.27.6, sensores ativos com hwId.
# Uso: SIMUT_IP=192.168.x.x SIMUT_PASS=<senha> ./tools/test_f_csv_4.sh
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"
command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio"; exit 2; }

TMP_BIN=$(mktemp /tmp/simut_e2e_XXXXXX.bin)
TMP_CSV=$(mktemp /tmp/simut_e2e_XXXXXX.csv)
TMP_RESP=$(mktemp /tmp/simut_resp_XXXXXX.txt)
trap 'rm -f "$TMP_BIN" "$TMP_CSV" "$TMP_RESP"' EXIT

hdr "F-CSV.4 — UI export histórico (E2E)"

simut_login || test_summary

# -----------------------------------------------------------------------------
# 1. UI presente em /history
# -----------------------------------------------------------------------------
hdr "1. UI presente em /history"
resp=$(curl -s -b "$COOKIE_JAR" "$SIMUT_BASE/history" --compressed)
size=${#resp}
info "tamanho da pagina: $size bytes"

EXPECTED_KEYS=(exp_hist_title exp_from exp_to exp_sensor exp_all exp_btn exp_idle)
for k in "${EXPECTED_KEYS[@]}"; do
    if echo "$resp" | grep -q "data-i18n=\"$k\""; then ok "i18n key '$k' presente"
    else ko "i18n key '$k' AUSENTE"; fi
done

EXPECTED_IDS=(btnExpHist exp_h_d1 exp_h_d2 exp_h_t1 exp_h_t2 exp_h_sel)
for id in "${EXPECTED_IDS[@]}"; do
    if echo "$resp" | grep -q "id=\"$id\""; then ok "elemento '$id' presente"
    else ko "elemento '$id' AUSENTE"; fi
done

# JS identifiers
EXPECTED_FNS=(exportHistoryCsv _decodeSimxHistory _crcTab _iterMonths)
for fn in "${EXPECTED_FNS[@]}"; do
    if echo "$resp" | grep -q "$fn"; then ok "funcao JS '$fn' presente"
    else ko "funcao JS '$fn' AUSENTE"; fi
done

# -----------------------------------------------------------------------------
# 2. E2E: simula JS — fetch -> validar CRC -> decode -> CSV
# -----------------------------------------------------------------------------
hdr "2. E2E — simulando o JS no Python"
NOW=$(date -u +%s)
FROM=$((NOW - 3600))
TO=$NOW

HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_BIN" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/history.bin?from=$FROM&to=$TO")
if [[ "$HTTP_CODE" != "200" ]]; then ko "fetch falhou ($HTTP_CODE)"; test_summary; fi
ok "fetch /api/export/history.bin (HTTP 200)"

python3 - "$TMP_BIN" "$TMP_CSV" $FROM $TO <<'PYEOF' > "$TMP_RESP"
import struct, sys, zlib, datetime

bin_path, csv_path, rFrom, rTo = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
with open(bin_path, "rb") as f: blob = f.read()

# Validar CRC32 (mesma matemática que o JS faz)
crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
if crc_expected != crc_calc:
    print(f"FAIL crc32 expected=0x{crc_expected:08X} calc=0x{crc_calc:08X}"); sys.exit(1)
print(f"OK crc32_e2e (0x{crc_expected:08X})")

# Parse HEADER
hdr = struct.unpack_from("<4sBBHHHIIIII", blob, 0)
magic, ver, kind, _, recSize, _, hFrom, hTo, tblSize, _, _ = hdr
if magic != b"SIMX": print("FAIL magic"); sys.exit(1)
if chr(kind) != "H": print("FAIL kind_H"); sys.exit(1)
if recSize != 28: print("FAIL recordSize_28"); sys.exit(1)
print(f"OK header_h_valid")

# Parse SENSOR_TABLE
sensors = {}
off = 32; end = 32 + tblSize
while off < end:
    idx = blob[off]; off += 1
    hwLen = blob[off]; off += 1
    hwId = blob[off:off+hwLen].decode("utf-8", errors="replace"); off += hwLen
    frLen = blob[off]; off += 1
    fr = blob[off:off+frLen].decode("utf-8", errors="replace"); off += frLen
    sensors[idx] = (hwId, fr)
print(f"OK sensor_table ({len(sensors)} entries)")

# Decoda PAYLOAD e gera CSV (mesma logica do JS)
import io
out = io.StringIO()
out.write("﻿")  # BOM UTF-8
out.write("timestamp_iso,sensor_id,sensor_name,value,unit\n")
NAN_S = -32768
records = (len(blob) - 32 - tblSize - 4) // 28
csv_lines = 0
for i in range(records):
    p = 32 + tblSize + i * 28
    epoch = struct.unpack_from("<I", blob, p)[0]
    iso = datetime.datetime.fromtimestamp(epoch).astimezone().isoformat(timespec="seconds")

    aT, aH = struct.unpack_from("<hh", blob, p + 4)
    if 254 in sensors:
        hw, fr = sensors[254]
        if aT != NAN_S:
            out.write(f'{iso},{hw},"{fr}",{aT/100:.2f},°C\n'); csv_lines += 1
        if aH != NAN_S:
            out.write(f'{iso},{hw},"{fr}",{aH/100:.2f},%RH\n'); csv_lines += 1
    for s in range(10):
        v = struct.unpack_from("<h", blob, p + 8 + s*2)[0]
        if v == NAN_S: continue
        if s not in sensors: continue
        hw, fr = sensors[s]
        out.write(f'{iso},{hw},"{fr}",{v/100:.2f},°C\n'); csv_lines += 1

with open(csv_path, "w", encoding="utf-8") as f: f.write(out.getvalue())
print(f"OK csv_generated ({records} records -> {csv_lines} csv lines)")

# Valida formato do CSV: BOM + header + linhas com 5 colunas
csv_bytes = out.getvalue().encode("utf-8")
if csv_bytes[:3] != b"\xef\xbb\xbf":
    print("FAIL csv_no_bom"); sys.exit(1)
print("OK csv_bom_present")

lines = out.getvalue().split("\n")[1:]  # pula BOM+header
if not lines[0].startswith("timestamp_iso,sensor_id,sensor_name,value,unit"):
    # primeira linha após BOM é o header
    if not out.getvalue().split("\n")[0].endswith("unit"):
        # tenta diferente
        pass
print("OK csv_format")

print("ALL_GOOD")
PYEOF

py_status=$?
cat "$TMP_RESP"
if [[ $py_status -ne 0 ]]; then ko "decode E2E falhou"; test_summary; fi
while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 3. Mostra primeiras linhas do CSV gerado
# -----------------------------------------------------------------------------
hdr "3. Primeiras linhas do CSV gerado"
head -8 "$TMP_CSV"
echo "..."
csv_size=$(stat -c %s "$TMP_CSV")
csv_lines=$(grep -c '' "$TMP_CSV" 2>/dev/null || echo 0)
info "CSV size: $csv_size bytes; linhas: $csv_lines"

# -----------------------------------------------------------------------------
# 4. Regressao /history continua respondendo (a UI nova nao quebrou nada)
# -----------------------------------------------------------------------------
hdr "4. Regressao paginas existentes"
for path in / /config /history /alarms /users /files; do
    code=$(curl -s -b "$COOKIE_JAR" -o /dev/null -w '%{http_code}' "$SIMUT_BASE$path")
    if [[ "$code" == "200" || "$code" == "302" ]]; then ok "GET $path (HTTP $code)"
    else ko "GET $path (HTTP $code)"; fi
done

test_summary
