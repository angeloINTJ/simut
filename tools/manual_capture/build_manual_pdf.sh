#!/usr/bin/env bash
# build_manual_pdf.sh — gera MANUAL.pdf via pandoc inserindo screenshots
#
# Pré-requisitos:
#   - pandoc + xelatex instalados (apt install pandoc texlive-xetex)
#   - Screenshots em docs/screenshots/ (gerados via capture_*.sh/py)
#
# Output: docs/MANUAL.pdf

set -e
cd "$(dirname "$0")/../.."

if ! command -v pandoc >/dev/null; then
    echo "ERROR: pandoc não instalado. Run: sudo apt install pandoc texlive-xetex"
    exit 1
fi

# Convert BMPs to PNGs for pandoc compatibility
for bmp in docs/screenshots/*.bmp; do
    [ -f "$bmp" ] || continue
    png="${bmp%.bmp}.png"
    if [ ! -f "$png" ] || [ "$bmp" -nt "$png" ]; then
        echo "Converting $bmp → $png"
        convert "$bmp" "$png" 2>/dev/null || \
        ffmpeg -i "$bmp" "$png" 2>/dev/null || \
        echo "WARN: cannot convert $bmp (install imagemagick or ffmpeg)"
    fi
done

# Generate MANUAL_with_images.md by replacing [ ] placeholders with actual images
$(which python3) <<'PYEOF'
import re, os

with open('docs/MANUAL.md') as f:
    content = f.read()

# Map placeholders to image filenames
mappings = {
    'boot_screen.bmp': 'tft_dashboard.png',  # boot capture would need fresh boot
    'auth_screen.bmp': 'tft_dashboard.png',  # placeholder
    'dashboard.bmp': 'tft_dashboard.png',
    'graph.bmp': 'tft_graph.png',
    'alarm_action.bmp': 'tft_alarms_action.png',
    'web_login.png': 'web_login.png',
    'web_force_chpass.png': 'web_login.png',  # similar
    'web_dashboard.png': 'web_dashboard.png',
    'web_history.png': 'web_history.png',
    'web_alarms.png': 'web_alarms.png',
    'web_config.png': 'web_config.png',
    'web_network.png': 'web_network.png',
    'web_users.png': 'web_users.png',
    'web_files.png': 'web_files.png',
    'web_license.png': 'web_license.png',
}

# Replace [ ] Captura: `name.ext` with embedded image
def repl(match):
    fname = match.group(1)
    actual = mappings.get(fname, fname)
    path = f'docs/screenshots/{actual}'
    if os.path.exists(path):
        return f'![{fname}]({path})\n\n*Figura: {fname}*'
    return f'*[Imagem pendente: {fname}]*'

content = re.sub(r'\[ \] Captura: `([^`]+)`[^\n]*', repl, content)

with open('docs/MANUAL_with_images.md', 'w') as f:
    f.write(content)

print('Generated docs/MANUAL_with_images.md')
PYEOF

# Convert to PDF
pandoc docs/MANUAL_with_images.md -o docs/MANUAL.pdf \
    --pdf-engine=xelatex \
    --toc \
    --highlight-style=tango \
    -V geometry:margin=2cm \
    -V documentclass=article \
    -V mainfont="DejaVu Sans" \
    -V monofont="DejaVu Sans Mono" \
    --metadata title="SIMUT — Manual Completo do Sistema" \
    --metadata author="Ângelo Moisés Alves" \
    --metadata date="$(date +%Y-%m-%d)"

echo "Generated docs/MANUAL.pdf ($(stat -c%s docs/MANUAL.pdf) bytes)"
