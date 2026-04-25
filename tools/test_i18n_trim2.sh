#!/bin/bash
# test_i18n_trim2.sh — Valida F-I18N-TRIM.2: remoção de idiomas mortos do WebUI.h
# Verifica que só EN e PT sobrevivem nos seletores de idioma e dicionários JS.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WEBUI="$SCRIPT_DIR/../WebUI.h"
PASS=0
FAIL=0

green() { echo -e "\033[32m  PASS\033[0m $1"; ((PASS++)); }
red()   { echo -e "\033[31m  FAIL\033[0m $1"; ((FAIL++)); }

echo "=== F-I18N-TRIM.2 Validation Suite ==="
echo "Target: $WEBUI"
echo ""

# ── 1. File exists ──────────────────────────────────────────────
echo "── 1. File integrity ──"
if [[ -f "$WEBUI" ]]; then
  green "WebUI.h encontrado ($(wc -l < "$WEBUI") linhas)"
else
  red "WebUI.h NÃO encontrado em $WEBUI"
fi

# ── 2. Zero dead language markers ───────────────────────────────
echo ""
echo "── 2. Zero dead language markers ──"
for lang in es de fr it ru zh; do
  count=$(grep -c "@LANG_BEGIN:${lang}\|@LANG_END:${lang}" "$WEBUI" 2>/dev/null || true)
  count=${count:-0}
  if [[ "$count" -eq 0 ]]; then
    green "Zero marcadores @LANG para '$lang'"
  else
    red "$count marcador(es) @LANG remanescente(s) para '$lang'"
  fi
done

# ── 3. Only EN+PT in <select> lang options ──────────────────────
echo ""
echo "── 3. Language <select> audit ──"
python3 - "$WEBUI" << 'PYEOF'
import re, sys

with open(sys.argv[1], 'r') as f:
    content = f.read()

raw_blocks = list(re.finditer(r'R"raw\((.*?)\)raw"', content, re.DOTALL))
print(f"  INFO Raw string blocks encontrados: {len(raw_blocks)}")

DEAD = {'es','de','fr','it','ru','zh'}
errors = []

for i, m in enumerate(raw_blocks):
    block = m.group(1)
    selects = re.findall(r'<select[^>]*class="[^"]*lang-select[^"]*"[^>]*>(.*?)</select>', block, re.DOTALL)
    for sel in selects:
        options = re.findall(r'<option\s+value="([^"]*)"', sel)
        opt_set = set(options)
        dead_found = opt_set & DEAD
        if dead_found:
            errors.append(f"  ERRO: Idiomas mortos {dead_found} no <select>: opcoes={options}")
        if 'en' not in opt_set:
            errors.append(f"  ERRO: EN ausente no <select>: opcoes={options}")
        if 'pt' not in opt_set:
            errors.append(f"  ERRO: PT ausente no <select>: opcoes={options}")

if errors:
    for e in errors:
        print(e)
    sys.exit(1)
else:
    print(f"  PASS Todos os <select> lang-select tem apenas EN+PT")
PYEOF
if [[ $? -eq 0 ]]; then
  green "Seletores de idioma limpos (EN+PT apenas)"
else
  red "Ha idiomas mortos em <select> lang-select"
fi

# ── 4. JS dicts audit ───────────────────────────────────────────
echo ""
echo "── 4. JS dictionary audit ──"
python3 - "$WEBUI" << 'PYEOF'
import re, sys

with open(sys.argv[1], 'r') as f:
    content = f.read()

raw_blocks = re.findall(r'R"raw\((.*?)\)raw"', content, re.DOTALL)
DEAD = {'es','de','fr','it','ru','zh'}
errors = []

for i, block in enumerate(raw_blocks):
    lang_keys = re.findall(r'^\s{8,}(es|de|fr|it|ru|zh|pt|en):\s*\{', block, re.MULTILINE)
    dead_keys = [k for k in lang_keys if k in DEAD]
    if dead_keys:
        errors.append(f"  ERRO no bloco {i}: dicionario(s) de idioma morto: {dead_keys}")

if errors:
    for e in errors:
        print(e)
    sys.exit(1)
else:
    print(f"  PASS Nenhum dicionario JS de idioma morto encontrado")
PYEOF
if [[ $? -eq 0 ]]; then
  green "Dicionarios JS sem idiomas mortos"
else
  red "Ha dicionarios JS de idiomas mortos"
fi

# ── 5. EN e PT balanceados ──────────────────────────────────────
echo ""
echo "── 5. EN/PT balance ──"
en_count=$(grep -c '<option value="en">' "$WEBUI" || true)
pt_count=$(grep -c '<option value="pt">' "$WEBUI" || true)
if [[ "$en_count" -eq "$pt_count" && "$en_count" -gt 0 ]]; then
  green "EN=$en_count, PT=$pt_count — balanceados em $en_count pagina(s)"
else
  red "Desequilibrio: EN=$en_count, PT=$pt_count"
fi

# ── 6. C++ raw string syntax check ──────────────────────────────
echo ""
echo "── 6. C++ raw string syntax ──"
python3 - "$WEBUI" << 'PYEOF'
import re, sys

with open(sys.argv[1], 'r') as f:
    content = f.read()

opens = len(re.findall(r'R"raw\(', content))
closes = len(re.findall(r'\)raw"', content))
if opens == closes and opens > 0:
    print(f"  PASS Raw string delimiters balanceados: {opens} pares")
else:
    print(f"  ERRO: R\"raw( = {opens}, )raw\" = {closes}")
    sys.exit(1)
PYEOF
if [[ $? -eq 0 ]]; then
  green "Sintaxe C++ raw strings integra"
else
  red "Raw strings desbalanceadas"
fi

# ── 7. Nenhum texto de idioma morto ─────────────────────────────
echo ""
echo "── 7. Zero labels de idiomas mortos ──"
python3 - "$WEBUI" << 'PYEOF'
import re, sys

with open(sys.argv[1], 'r') as f:
    content = f.read()

raw_blocks = re.findall(r'R"raw\((.*?)\)raw"', content, re.DOTALL)
DEAD_LABELS = [
    ('Espanol', 'es'), ('Deutsch', 'de'), ('Francais', 'fr'),
    ('Italiano', 'it'), ('Russkii', 'ru'), ('Zhongwen', 'zh'),
]
# Check as-is in content (encodings may vary)
plain_checks = ['Espa', 'Deutsch', 'Fran', 'Italiano', 'Русский', '中文']
errors = []
for i, block in enumerate(raw_blocks):
    for check in plain_checks:
        if check in block:
            errors.append(f"  ERRO no bloco {i}: texto com '{check}' ainda presente")

if errors:
    for e in set(errors):
        print(e)
    sys.exit(1)
else:
    print(f"  PASS Zero rotulos de idiomas mortos")
PYEOF
if [[ $? -eq 0 ]]; then
  green "Sem labels de idiomas mortos"
else
  red "Labels de idiomas mortos encontrados"
fi

# ── Sumario ─────────────────────────────────────────────────────
echo ""
echo "========================================"
echo "  Resultado: $PASS pass, $FAIL fail"
echo "========================================"
if [[ "$FAIL" -gt 0 ]]; then
  exit 1
fi
