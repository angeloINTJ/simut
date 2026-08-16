#!/bin/bash
# build_release_pio.sh — Generate PlatformIO / VS Code compatible release zip.
# Usage: ./tools/build_release_pio.sh
# Output: release/simut_pio_v{version}.zip

set -e
cd "$(dirname "$0")/.."

# Refuse to package anything while a secret is tracked. See scan_secrets.sh.
./tools/scan_secrets.sh

VERSION=$(grep -oP '(?<=SIMUT_VERSION ")[^"]*' src/SystemDefs_Limits.h)
echo "=== Building SIMUT PlatformIO release v${VERSION} ==="

DST="release/simut_pio"
rm -rf "${DST}"
mkdir -p "${DST}"

# ── Source code ──
echo "--- Copying src/ ---"
rsync -a --delete src/ "${DST}/src/"

# Pre-generate WebUI_GZ.h
echo "--- Generating src/WebUI_GZ.h ---"
python3 tools/build_webui_gz.py
cp src/WebUI_GZ.h "${DST}/src/WebUI_GZ.h"

# ── Data (only what the pre-build hooks read: lang packs + favicon) ──
echo "--- Copying data/ (lang packs + favicon) ---"
rm -rf "${DST}/data"
mkdir -p "${DST}/data/lang"
cp data/favicon.ico "${DST}/data/"
cp data/lang/language_*.lng "${DST}/data/lang/"

# ── Root files ──
echo "--- Copying root files ---"
cp platformio.ini "${DST}/"
cp WebUI.h        "${DST}/"
cp LICENSE        "${DST}/"
cp .editorconfig  "${DST}/"
cp .dockerignore  "${DST}/" 2>/dev/null || true
cp docker-compose.yml "${DST}/"
cp Dockerfile     "${DST}/"
cp .all-contributorsrc "${DST}/" 2>/dev/null || true

# ── Documentation ──
echo "--- Copying docs ---"
for f in README.md README.pt-BR.md README.es-ES.md \
         CHANGELOG.md CHANGELOG.pt-BR.md \
         CONTRIBUTING.md CONTRIBUTING.pt-BR.md CONTRIBUTING.es-ES.md \
         CONTRIBUTORS.md CODE_OF_CONDUCT.md CODE_OF_CONDUCT.es-ES.md \
         SECURITY.md; do
  [ -f "$f" ] && cp "$f" "${DST}/"
done

# ── Tools ──
echo "--- Copying tools/ ---"
rsync -a --delete tools/ "${DST}/tools/"
# Remove release scripts and PicoHand from tools in the package
rm -rf "${DST}/tools/PicoHand" "${DST}/tools/build_release.sh" "${DST}/tools/build_release_pio.sh"
rm -rf "${DST}/tools/arduino_pico_overrides"
rm -rf "${DST}/tools/test-server/certs"
rm -rf "${DST}/tools/stress_test"
rm -rf "${DST}/tools/theme-editor"

# ── Tests ──
echo "--- Copying test/ ---"
rsync -a --delete test/ "${DST}/test/"

# ── Docs ──
echo "--- Copying docs/ ---"
if [ -d "docs" ]; then
  rsync -a --delete --exclude='screenshots*' --exclude='promotion' --exclude='test_reports' docs/ "${DST}/docs/"
fi

# ── Create zip ──
echo "--- Creating release zip ---"
cd release
rm -f "simut_pio_v${VERSION}.zip"
zip -r "simut_pio_v${VERSION}.zip" simut_pio/
cd ..

# The zip is built from the working tree, so audit what actually shipped.
./tools/scan_secrets.sh "release/simut_pio_v${VERSION}.zip"

echo ""
echo "=== Done: release/simut_pio_v${VERSION}.zip ==="
ls -lh "release/simut_pio_v${VERSION}.zip"
