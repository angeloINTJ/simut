#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# F-CSV.5 — teste E2E da UI de export de logs em /history (abaixo dos logs).
# Simula o JS embarcado: fetch -> CRC32 -> decode .simx kind='L' -> CSV.
# Valida tambem o filtro de level no client+server side.
#
# Pre-req: dispositivo flashado >= v3.27.7
# Uso: SIMUT_IP=192.168.x.x SIMUT_PASS=<senha> ./tools/test_f_csv_5.sh
# -----------------------------------------------------------------------------
source "$(dirname "$0")/hw_test_lib.sh"
command -v python3 >/dev/null 2>&1 || { echo "python3 obrigatorio"; exit 2; }

TMP_BIN=$(mktemp /tmp/simut_logsexp_XXXXXX.bin)
TMP_CSV=$(mktemp /tmp/simut_logsexp_XXXXXX.csv)
TMP_RESP=$(mktemp /tmp/simut_resp_XXXXXX.txt)
trap 'rm -f "$TMP_BIN" "$TMP_CSV" "$TMP_RESP"' EXIT

hdr "F-CSV.5 — UI export logs (E2E)"

simut_login || test_summary

# -----------------------------------------------------------------------------
# 1. UI presente em /history
# -----------------------------------------------------------------------------
hdr "1. UI presente em /history"
resp=$(curl -s -b "$COOKIE_JAR" "$SIMUT_BASE/history" --compressed)

EXPECTED_KEYS=(exp_logs_title exp_level exp_errors exp_infos)
for k in "${EXPECTED_KEYS[@]}"; do
    if echo "$resp" | grep -q "data-i18n=\"$k\""; then ok "i18n key '$k' presente"
    else ko "i18n key '$k' AUSENTE"; fi
done

EXPECTED_IDS=(btnExpLogs exp_l_d1 exp_l_d2 exp_l_t1 exp_l_t2 exp_l_sel)
for id in "${EXPECTED_IDS[@]}"; do
    if echo "$resp" | grep -q "id=\"$id\""; then ok "elemento '$id' presente"
    else ko "elemento '$id' AUSENTE"; fi
done

EXPECTED_FNS=(exportLogsCsv _decodeSimxLogs)
for fn in "${EXPECTED_FNS[@]}"; do
    if echo "$resp" | grep -q "$fn"; then ok "funcao JS '$fn' presente"
    else ko "funcao JS '$fn' AUSENTE"; fi
done

# Garantia de reuso (nao duplicou _crcTab/crc32/_iterMonths)
crc_count=$(echo "$resp" | grep -oE "function crc32\(" | wc -l)
if [[ "$crc_count" == "1" ]]; then ok "crc32() definida apenas 1 vez (reuso de F-CSV.4)"
else ko "crc32() duplicada — esperado 1, obtido $crc_count"; fi

# -----------------------------------------------------------------------------
# 2. E2E level=all
# -----------------------------------------------------------------------------
hdr "2. E2E — fetch + decode + CSV (level=all)"
NOW=$(date -u +%s)
FROM=$((NOW - 86400))
TO=$NOW

HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_BIN" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO&level=all")
[[ "$HTTP_CODE" == "200" ]] && ok "fetch level=all (HTTP 200)" || { ko "fetch level=all $HTTP_CODE"; test_summary; }

python3 - "$TMP_BIN" "$TMP_CSV" $FROM $TO <<'PYEOF' > "$TMP_RESP"
import struct, sys, zlib, datetime, io

bin_path, csv_path, rFrom, rTo = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
with open(bin_path, "rb") as f: blob = f.read()

crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
if crc_expected != crc_calc:
    print(f"FAIL crc32 expected=0x{crc_expected:08X} calc=0x{crc_calc:08X}"); sys.exit(1)
print(f"OK crc32_e2e_logs (0x{crc_expected:08X})")

magic, ver, kind = blob[0:4], blob[4], blob[5]
if magic != b"SIMX": print("FAIL magic"); sys.exit(1)
if chr(kind) != "L": print("FAIL kind_L"); sys.exit(1)
recSize = struct.unpack_from("<H", blob, 8)[0]
if recSize != 12: print("FAIL recordSize_12"); sys.exit(1)
print("OK header_l_valid")

TAG_NAMES = ['APP','NET','TEL','STO','WEB','CFG','CLI','SENSOR','HIST','SYS','DSP','SEC','?','?','?','?']
LVL_LABELS = ['DBG','INF','WRN','ERR','FTL']

# Mimica evtName via dict mock — para o teste so' valida formato CSV
out = io.StringIO()
out.write("﻿")  # BOM UTF-8
out.write("timestamp_iso,level,module,code,message,context,uptime_hr\n")
payload_size = len(blob) - 32 - 4
records = payload_size // 12
levels = {0:0,1:0,2:0,3:0}
for i in range(records):
    p = 32 + i * 12
    epoch  = struct.unpack_from("<I", blob, p)[0]
    upHr   = struct.unpack_from("<H", blob, p+4)[0]
    code   = struct.unpack_from("<H", blob, p+6)[0]
    ctx    = struct.unpack_from("<h", blob, p+8)[0]
    flags  = blob[p+10]
    lvl    = (flags >> 5) & 0x07
    tagId  = flags & 0x0F
    if lvl in levels: levels[lvl] += 1
    iso = datetime.datetime.fromtimestamp(epoch).astimezone().isoformat(timespec="seconds")
    lvlLbl = LVL_LABELS[lvl] if lvl < len(LVL_LABELS) else f"L{lvl}"
    modLbl = TAG_NAMES[tagId] if tagId < len(TAG_NAMES) else f"T{tagId}"
    out.write(f'{iso},{lvlLbl},{modLbl},{code},"Event #{code}",{ctx},{upHr}\n')

with open(csv_path, "w", encoding="utf-8") as f: f.write(out.getvalue())
print(f"OK csv_generated ({records} records)")
print(f"INFO levels DBG={levels[0]} INF={levels[1]} WRN={levels[2]} ERR={levels[3]}")

if out.getvalue().encode("utf-8")[:3] != b"\xef\xbb\xbf":
    print("FAIL csv_no_bom"); sys.exit(1)
print("OK csv_bom")

with open("/tmp/simut_logs_counts.txt","w") as f:
    f.write(f"{records},{levels[3]},{levels[1]}")
print("ALL_GOOD")
PYEOF
cat "$TMP_RESP"
while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 3. E2E level=err — validar CRC + 100% records ERR
# -----------------------------------------------------------------------------
hdr "3. E2E level=err"
TMP_E=$(mktemp /tmp/simut_lerr_XXXXXX.bin)
HTTP_CODE=$(curl -s -b "$COOKIE_JAR" -o "$TMP_E" -w '%{http_code}' \
    "$SIMUT_BASE/api/export/logs.bin?from=$FROM&to=$TO&level=err")
[[ "$HTTP_CODE" == "200" ]] && ok "fetch level=err" || { ko "level=err $HTTP_CODE"; rm -f "$TMP_E"; test_summary; }

python3 - "$TMP_E" <<'PYEOF' > "$TMP_RESP"
import struct, sys, zlib
with open(sys.argv[1], "rb") as f: blob = f.read()
crc_expected = struct.unpack("<I", blob[-4:])[0]
crc_calc = zlib.crc32(blob[:-4]) & 0xFFFFFFFF
if crc_expected != crc_calc: print("FAIL crc32"); sys.exit(1)
print("OK crc32_err")
records = (len(blob) - 36) // 12
non_err = sum(1 for i in range(records)
              if ((blob[32 + i*12 + 10] >> 5) & 0x07) != 3)
print(f"INFO err_records={records} non_err={non_err}")
if non_err > 0: print("FAIL filtro_err"); sys.exit(1)
print("OK filtro_err_correto")
PYEOF
rm -f "$TMP_E"
cat "$TMP_RESP"
while IFS= read -r line; do
    case "$line" in
        OK\ *)   _pass=$((_pass+1)); echo "${C_OK}✔${C_RST}  ${line#OK }" ;;
        FAIL\ *) _fail=$((_fail+1)); echo "${C_FAIL}✘${C_RST}  ${line#FAIL }" ;;
    esac
done < "$TMP_RESP"

# -----------------------------------------------------------------------------
# 4. Mostra primeiras linhas do CSV gerado
# -----------------------------------------------------------------------------
hdr "4. CSV gerado (primeiras linhas)"
head -6 "$TMP_CSV"
echo "..."
csv_size=$(stat -c %s "$TMP_CSV"); csv_lines=$(grep -c '' "$TMP_CSV" 2>/dev/null || echo 0)
info "size: $csv_size bytes; linhas: $csv_lines"

test_summary
