#!/usr/bin/env bash
# build_manual_pdf.sh — gera MANUAL.pdf via pandoc + chromium (sem LaTeX)
#
# Pré-requisitos (sem sudo):
#   - pandoc 3.5+ (baixa pra /tmp/pandoc_local se não existir)
#   - chromium / google-chrome (para HTML→PDF)
#   - .venv com PIL/Pillow (para BMP→PNG)
#   - screenshots em docs/screenshots/ (gerados via capture_*.sh/py)
#
# Output: docs/MANUAL.pdf, docs/MANUAL.html, docs/MANUAL_with_images.md

set -e
cd "$(dirname "$0")/../.."

# 1. Find or download pandoc
PANDOC=""
if command -v pandoc >/dev/null; then
    PANDOC=$(which pandoc)
elif [ -x /tmp/pandoc_local/pandoc-3.5/bin/pandoc ]; then
    PANDOC=/tmp/pandoc_local/pandoc-3.5/bin/pandoc
else
    echo "Downloading pandoc 3.5..."
    mkdir -p /tmp/pandoc_local
    cd /tmp/pandoc_local
    wget -q "https://github.com/jgm/pandoc/releases/download/3.5/pandoc-3.5-linux-amd64.tar.gz" -O pandoc.tar.gz
    tar -xzf pandoc.tar.gz
    cd "$OLDPWD"
    PANDOC=/tmp/pandoc_local/pandoc-3.5/bin/pandoc
fi
echo "Using pandoc: $PANDOC"

# 2. Find chromium
CHROMIUM=""
for cmd in chromium chromium-browser google-chrome google-chrome-stable; do
    if command -v $cmd >/dev/null; then CHROMIUM=$(which $cmd); break; fi
done
if [ -z "$CHROMIUM" ]; then
    echo "ERROR: chromium/chrome não encontrado. Install: snap install chromium"
    exit 1
fi
echo "Using browser: $CHROMIUM"

# 3. Convert BMP → PNG via Pillow
.venv/bin/python3 -u <<'PYEOF'
import os
from PIL import Image
src_dir = 'docs/screenshots'
for f in sorted(os.listdir(src_dir)):
    if f.endswith('.bmp'):
        bmp = os.path.join(src_dir, f)
        png = bmp.replace('.bmp', '.png')
        if os.path.exists(png) and os.path.getmtime(png) >= os.path.getmtime(bmp):
            continue
        try:
            Image.open(bmp).save(png, 'PNG')
            print(f'  converted {f}')
        except Exception as e:
            print(f'  ERR {f}: {e}')
PYEOF

# 4. Generate MANUAL_with_images.md
.venv/bin/python3 -u <<'PYEOF'
import re
with open('docs/MANUAL.md') as f: content = f.read()
mappings = {
    'boot_screen.bmp': 'tft_dashboard_initial.png',
    'auth_screen.bmp': 'tft_dashboard_initial.png',
    'dashboard.bmp': 'tft_01_dashboard.png',
    'graph.bmp': 'tft_03_graph.png',
    'alarm_action.bmp': 'tft_04_alarms.png',
    'web_login.png': 'web_01_login.png',
    'web_force_chpass.png': 'web_01_login.png',
    'web_dashboard.png': 'web_02_dashboard.png',
    'web_history.png': 'web_03_history.png',
    'web_alarms.png': 'web_04_alarms.png',
    'web_config.png': 'web_05_config.png',
    'web_network.png': 'web_06_network.png',
    'web_users.png': 'web_07_users.png',
    'web_files.png': 'web_08_files.png',
    'web_license.png': 'web_09_license.png',
}
def repl(m):
    fname = m.group(1)
    actual = mappings.get(fname)
    if actual: return f'![{fname}](screenshots/{actual})\n\n*Figura: {fname}*'
    return f'*[Imagem: {fname}]*'
new = re.sub(r'\[ \] Captura: `([^`]+)`[^\n]*', repl, content)
with open('docs/MANUAL_with_images.md', 'w') as f: f.write(new)
print('Generated MANUAL_with_images.md')
PYEOF

# 5. Markdown → HTML via pandoc
$PANDOC docs/MANUAL_with_images.md -o docs/MANUAL.html \
    --standalone --toc --toc-depth=3 \
    --highlight-style=tango \
    --css=https://cdn.jsdelivr.net/npm/water.css@2/out/water.css \
    --metadata title="SIMUT — Manual Completo" \
    --metadata author="Ângelo Moisés Alves" \
    --metadata date="$(date +%Y-%m-%d)"
echo "Generated docs/MANUAL.html ($(stat -c%s docs/MANUAL.html) bytes)"

# 6. HTML → PDF via Chrome headless
$CHROMIUM --headless --disable-gpu --no-sandbox \
    --print-to-pdf=docs/MANUAL.pdf \
    --print-to-pdf-no-header \
    "file://$(realpath docs/MANUAL.html)" 2>&1 | grep -v "^$" | tail -2

echo "Generated docs/MANUAL.pdf ($(stat -c%s docs/MANUAL.pdf) bytes, $(file docs/MANUAL.pdf | grep -oP '\d+ page'))"
