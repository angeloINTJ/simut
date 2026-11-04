#!/bin/bash
# build_release.sh — Generate Arduino IDE-compatible release zips for simut_tft and simut_alpha.
# Usage: ./tools/build_release.sh
# Output: release/simut_tft_v{version}.zip, release/simut_alpha_v{version}.zip
#
# Arduino IDE compatibility: ALL files are flattened to the sketch root.
# No subdirectories — Arduino does NOT add them as -I paths.

set -e
cd "$(dirname "$0")/.."

# Refuse to package anything while a secret is tracked. See scan_secrets.sh.
./tools/scan_secrets.sh

VERSION=$(grep -oP '(?<=SIMUT_VERSION ")[^"]*' src/SystemDefs_Limits.h)
echo "=== Building SIMUT release v${VERSION} ==="

# Regenerate WebUI_GZ.h (TFT only)
echo "--- Regenerating WebUI_GZ.h ---"
python3 tools/build_webui_gz.py

for VARIANT in simut_tft simut_alpha; do
  echo "--- Syncing release/${VARIANT} ---"
  DST="release/${VARIANT}"

  # Clean and recreate (preserve simut_arduino_config.h and README.md)
  if [ -f "${DST}/simut_arduino_config.h" ]; then
    cp "${DST}/simut_arduino_config.h" /tmp/simut_arduino_config_${VARIANT}.h
  fi
  if [ -f "${DST}/README.md" ]; then
    cp "${DST}/README.md" /tmp/simut_readme_${VARIANT}.md
  fi

  rm -rf "${DST}"
  mkdir -p "${DST}"/{sensors,display,ota}

  # Copy ALL source files from src/ root to release root
  rsync -a src/*.cpp src/*.h "${DST}/"

  # Rename main.cpp to variant .ino
  if [ -f "${DST}/main.cpp" ]; then
    mv "${DST}/main.cpp" "${DST}/${VARIANT}.ino"
  fi

  # Subdirectories: FLATTEN everything to root for Arduino IDE compatibility.
  # Arduino only compiles .cpp from the sketch root directory.
  # Includes are rewritten below to remove directory prefixes.
  cp src/sensors/*.h   "${DST}/" 2>/dev/null || true
  cp src/display/*.h   "${DST}/" 2>/dev/null || true
  cp src/ota/*.h       "${DST}/" 2>/dev/null || true
  cp src/ota/*.cpp     "${DST}/" 2>/dev/null || true
  # The network drivers (src/net, since 2026-10-12): net/NetCaps.h, which
  # simut_config.h includes since 2026-10-13, and WiredLink, whose unit is
  # empty in the radio images these zips hold. Missing from this list, every
  # `#include "net/..."` below was left pointing at a folder that is not there.
  cp src/net/*.h       "${DST}/" 2>/dev/null || true
  cp src/net/*.cpp     "${DST}/" 2>/dev/null || true
  # The UI driver's identity (src/ui, since 2026-10-14): ui/UiCaps.h, which
  # simut_config.h includes. Same reason as the net/ headers above.
  cp src/ui/*.h        "${DST}/" 2>/dev/null || true
  # The original panel (src/ui/original, since S11: its 14 units, and the headers UiOriginal.h, UiTouchChain.h and PinKeypadLayout.h), flat like the rest. These
  # zips are the original panel's bundle, the Pico W's. src/ui/angulo is left out on purpose: the Angulo is the Pico 2 W release's panel, built by PlatformIO
  # (-DSIMUT_UI_ANGULO=1 and the manifest's `include` of its units), its units sit in subdirectories (kit/, telas/) that a flat sketch root cannot hold,
  # and ui/UiCaps.h defaults SIMUT_UI_ANGULO to 0 (nothing in these zips sets it), so the selection headers copied above never reach it.
  cp src/ui/original/*.h   "${DST}/" 2>/dev/null || true
  cp src/ui/original/*.cpp "${DST}/" 2>/dev/null || true

  # Copy WebUI_GZ.h (both variants — Alpha still serves web pages)
  cp src/WebUI_GZ.h "${DST}/WebUI_GZ.h"

  # A MIT pede que o aviso acompanhe "todas as copias ou partes substanciais
  # do Software", e este zip e uma copia do codigo inteiro. Saiu sem ele de
  # v1.x ate 2026-09-21; o build_release_pio.sh sempre copiou.
  cp LICENSE "${DST}/LICENSE"
  # Os avisos de terceiros vao junto: as licencas BSD e MIT dos componentes da
  # imagem pedem que o texto acompanhe uma distribuicao em binario.
  cp THIRD_PARTY_NOTICES.md "${DST}/THIRD_PARTY_NOTICES.md"
  cp -r LICENSES "${DST}/LICENSES"

  # Restore Arduino-specific files
  if [ -f "/tmp/simut_arduino_config_${VARIANT}.h" ]; then
    cp "/tmp/simut_arduino_config_${VARIANT}.h" "${DST}/simut_arduino_config.h"
  fi
  if [ -f "/tmp/simut_readme_${VARIANT}.md" ]; then
    cp "/tmp/simut_readme_${VARIANT}.md" "${DST}/README.md"
  fi
done

# --- Fix include paths for flat structure ---
for VARIANT in simut_tft simut_alpha; do
  echo "--- Fixing includes in ${VARIANT} ---"
  # Rewrite #include "ota/xxx.h" → #include "xxx.h" (flat structure)
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "ota/|#include "|g' 2>/dev/null || true
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "sensors/|#include "|g' 2>/dev/null || true
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "display/|#include "|g' 2>/dev/null || true
  # src/ui/original/*.h reach src/ with "../../" (UiOriginal.h: "../../DisplayManager_Fonts.h"): take both levels off before the rule below takes one.
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "../../|#include "|g' 2>/dev/null || true
  # Fix ../ relative includes
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "../|#include "|g' 2>/dev/null || true
  # Rewrite #include "net/xxx.h" -> #include "xxx.h". After the ../ one, on
  # purpose: display/SettingsMenu.h and ota/orchestrator.cpp say "../net/xxx.h",
  # which the line above turns into "net/xxx.h".
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "net/|#include "|g' 2>/dev/null || true
  # The original panel's files are flat in the sketch root too: "ui/original/xxx.h" (from src/) and "original/xxx.h" (from src/ui/) -> "xxx.h", before the ui/ rule.
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "ui/original/|#include "|g' 2>/dev/null || true
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "original/|#include "|g' 2>/dev/null || true
  # Rewrite #include "ui/xxx.h" -> #include "xxx.h" (simut_config.h says "ui/UiCaps.h").
  find "release/${VARIANT}" -maxdepth 1 -name '*.cpp' -o -name '*.h' -o -name '*.ino' | \
    xargs sed -i 's|#include "ui/|#include "|g' 2>/dev/null || true
done

# --- Remove empty subdirs ---
for VARIANT in simut_tft simut_alpha; do
  rm -rf "release/${VARIANT}/ota" "release/${VARIANT}/display" "release/${VARIANT}/sensors" 2>/dev/null || true
done

# --- TFT variant: remove Alpha-only files ---
echo "--- Cleaning TFT variant ---"
rm -f release/simut_tft/DisplayManager_Alpha.cpp
rm -f release/simut_tft/display/BigFont_HD44780.h
rm -f release/simut_tft/display/HD44780_16x2.h
rm -f release/simut_tft/simut_alpha.ino

# --- Alpha variant: remove TFT-only files (display hardware specific) ---
echo "--- Cleaning Alpha variant ---"
rm -f release/simut_alpha/DisplayManager_Touch.cpp
rm -f release/simut_alpha/DisplayManager_Settings.cpp
rm -f release/simut_alpha/DisplayManager_Graph.cpp
rm -f release/simut_alpha/DisplayManager_Calibration.cpp
rm -f release/simut_alpha/DisplayManager_Auth.cpp
rm -f release/simut_alpha/DisplayManager_Alarm.cpp
rm -f release/simut_alpha/DisplayManager_Calendar.cpp
rm -f release/simut_alpha/DisplayManager_Dashboard.cpp
rm -f release/simut_alpha/DisplayManager_Fonts.cpp
rm -f release/simut_alpha/DisplayManager_Fonts.h
rm -f release/simut_alpha/DisplayManager_i18n.cpp
rm -f release/simut_alpha/DisplayManager_LangParser.cpp
rm -f release/simut_alpha/BluetoothManager.cpp
rm -f release/simut_alpha/AppManager_Graph.cpp
rm -f release/simut_alpha/ILI9341_320x240.h
rm -f release/simut_alpha/XPT2046.h
rm -f release/simut_alpha/simut_tft.ino
# Note: WebUI_GZ.h is KEPT — Alpha still runs the web server
# Note: TouchPriority.h, ParseFloat.h, FreeSansBold24pt7b_subset.h,
# TftWithOffset.h are KEPT — they are referenced by common code

# --- Update README version ---
for VARIANT in simut_tft simut_alpha; do
  if [ -f "release/${VARIANT}/README.md" ]; then
    sed -i "s/v[0-9]\+\.[0-9]\+\.[0-9]\+\(-beta\)\?/v${VERSION}/g" "release/${VARIANT}/README.md"
  fi
done

# --- Create .zip files ---
echo "--- Creating release zips ---"
cd release
rm -f "simut_tft_v${VERSION}.zip" "simut_alpha_v${VERSION}.zip"
zip -r "simut_tft_v${VERSION}.zip" simut_tft/
zip -r "simut_alpha_v${VERSION}.zip" simut_alpha/
cd ..

# The zips are built from the working tree, so audit what actually shipped.
./tools/scan_secrets.sh "release/simut_tft_v${VERSION}.zip" "release/simut_alpha_v${VERSION}.zip"

echo ""
echo "=== Done ==="
ls -lh release/simut_*_v${VERSION}.zip
