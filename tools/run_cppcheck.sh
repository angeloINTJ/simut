#!/bin/bash
# run_cppcheck.sh — static-analysis gate over src/ (issue #35).
# Usage: ./tools/run_cppcheck.sh          (exit 0 clean, exit 1 with findings)
#        CPPCHECK=/path/to/cppcheck ./tools/run_cppcheck.sh
#
# CI runs this exact script, so a green run here is a green run there.
#
# ---------------------------------------------------------------------------
# Why not `--enable=all --inconclusive`, which is what issue #35 proposed
#
# Measured on this tree, 2026-08-18, cppcheck 2.11: that command takes
# 5m01s single-threaded and reports 532 findings — 265 style, 155
# performance, 63 warning, 37 information, 8 portability, 4 error. 153 of the
# performance findings are functionStatic ("this method could be static") and
# 87 of the style ones are cstyleCast. A gate that starts 528 findings in the
# red is not a gate; it is a file nobody reads.
#
# So the gate runs the severities that describe defects — error (always on),
# warning, portability — and leaves style/performance/information to the
# reviewer. --inconclusive is off for the same reason: it is explicitly
# cppcheck's "might be wrong" tier, and a build must not fail on a maybe.
#
# What is checked instead is checked properly:
#   --platform=arm32-wchar_t4  4-byte long/pointer/size_t, unsigned char —
#                              the RP2040 model. Without it cppcheck assumes
#                              the 64-bit host and every size-dependent
#                              finding is about a machine we do not ship on.
#   --inline-suppr             site-specific false positives are suppressed
#                              next to the code, each with its reason.
#   --suppressions-list        tree-wide exceptions, each with its reason.
#                              See tools/cppcheck-suppressions.txt.
#
# Do NOT add --relative-paths. It rewrites the path of a finding before the
# inline suppressions are matched against it, so every `cppcheck-suppress`
# comment in src/ silently stops working: measured on this tree, 8 of the 9
# suppressed findings came back, with no diagnostic saying why. The paths it
# would have shortened are already short.
#
# -j is a real trade-off, not just speed. It turns off cppcheck's whole-program
# pass, so cross-file findings are not reported. The only one this tree
# produces is a claimed one-definition-rule violation on BluetoothManager,
# which has a real class and a stub class behind #if SIMUT_BLUETOOTH — cppcheck
# compares the two configurations against each other and calls that a
# redefinition. Nothing is being hidden that a serial run would catch today.
#
# Configurations are NOT pinned with -D. pico_w_release builds with
# SIMUT_CLI_FULL=0, so pinning it would hide the whole CLI parser from the
# analyser — the opposite of what this is for. cppcheck walks the #if
# combinations itself, which is most of the runtime cost.
#
# ---------------------------------------------------------------------------
# Why the version is pinned
#
# --error-exitcode=1 makes this a gate, and a gate must only turn red when
# the code changes. `apt-get install cppcheck` follows the runner image, so a
# GitHub image bump would ship a new analyser with new checks and fail a PR
# that touched nothing. The version below comes from the PlatformIO registry,
# which CI already has, and is the same binary on a developer machine.

set -u
cd "$(dirname "$0")/.."

CPPCHECK_PKG="platformio/tool-cppcheck@1.21100.230717"   # cppcheck 2.11
EXPECTED_VERSION="2.11"
PIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
PIO_TOOL_BIN="${PIO_CORE_DIR}/packages/tool-cppcheck/cppcheck"

CPPCHECK="${CPPCHECK:-}"
if [ -z "$CPPCHECK" ]; then
  if [ -x "$PIO_TOOL_BIN" ]; then
    CPPCHECK="$PIO_TOOL_BIN"
  elif command -v pio >/dev/null 2>&1; then
    echo "--- cppcheck not installed; fetching the pinned build ---"
    pio pkg install --global --tool "$CPPCHECK_PKG" || exit 1
    CPPCHECK="$PIO_TOOL_BIN"
  elif command -v cppcheck >/dev/null 2>&1; then
    CPPCHECK="$(command -v cppcheck)"
  else
    echo "STATIC ANALYSIS — cppcheck not found."
    echo "    Install PlatformIO and re-run, or set CPPCHECK=/path/to/cppcheck."
    exit 1
  fi
fi

VERSION="$("$CPPCHECK" --version 2>/dev/null | awk '{print $2}')"
echo "--- cppcheck ${VERSION} (${CPPCHECK}) ---"
if [ "$VERSION" != "$EXPECTED_VERSION" ]; then
  echo "    NOTE: the gate was tuned against ${EXPECTED_VERSION}. A different"
  echo "    version may report findings this tree has never seen."
fi

JOBS="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null) || echo 2 )"

"$CPPCHECK" \
  --quiet \
  --enable=warning,portability \
  --error-exitcode=1 \
  --platform=arm32-wchar_t4 \
  --language=c++ \
  --std=c++17 \
  --inline-suppr \
  --suppressions-list=tools/cppcheck-suppressions.txt \
  -I src \
  -I src/sensors \
  -I src/display \
  -I src/ota \
  -j "$JOBS" \
  src/
status=$?

if [ "$status" -ne 0 ]; then
  echo "STATIC ANALYSIS: FAILED"
  echo "    Fix the finding, or — if it is wrong about this target — suppress"
  echo "    it inline with a comment saying why. See the header of this file."
  exit 1
fi
echo "STATIC ANALYSIS: clean"
