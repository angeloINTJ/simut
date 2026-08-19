#!/bin/bash
# run_fuzz.sh — libFuzzer gate over the web-API input validators (issue #44).
# Usage: ./tools/run_fuzz.sh                 (60 s, exit 0 clean, non-zero on a finding)
#        FUZZ_SECONDS=600 ./tools/run_fuzz.sh   (longer local run)
#        CLANGXX=clang++-18 ./tools/run_fuzz.sh
#
# CI runs this exact script, so a green run here is a green run there.
#
# ---------------------------------------------------------------------------
# What this gate measures — and what it deliberately does not
#
# The harness (test/test_fuzz/fuzz_validators.cpp) checks CONTRACT oracles,
# not just "didn't crash": the validators are pure, null-guarded functions
# that random bytes cannot crash, so a crash-only run is green while
# measuring nothing. See the harness header for the oracle list; the one
# that matters most historically is "parseIntStrict answered true ⇒ out is
# exactly the number written", which the pre-fix parser failed for any
# value past int32 (atol saturation).
#
# The build compiles against test/native_stubs/Arduino.h. That stub's
# toInt/toFloat MUST keep mirroring the target's atol/atof saturation —
# golden-vector tests in test_validators pin this. If those tests are red,
# this gate's verdict is about the stub, not the firmware.
#
# libFuzzer ships inside clang (-fsanitize=fuzzer); no package beyond clang
# itself is needed. ASan+UBSan are on so an out-of-bounds read or signed
# overflow in a validator fails loudly instead of corrupting quietly.
# ---------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."

CLANGXX="${CLANGXX:-clang++}"
SECS="${FUZZ_SECONDS:-60}"
OUT=.pio/fuzz
mkdir -p "$OUT/corpus_work"

# float-cast-overflow is excluded from UBSan on purpose: the stub's
# toFloat() is `(float)strtod(...)` because that IS the target's toFloat
# (ArduinoCore-API: `float(atof(...))`), and on IEEE hardware an oversized
# double converts to ±inf — the exact saturation the harness's finiteness
# oracle exists to observe. Sanitizing that cast would flag the mirror,
# not a defect, on every ~40-digit input.
"$CLANGXX" -std=gnu++17 -g -O1 \
    -fsanitize=fuzzer,address,undefined -fno-sanitize=float-cast-overflow \
    -fno-sanitize-recover=all \
    -I src -I test/native_stubs \
    test/test_fuzz/fuzz_validators.cpp -o "$OUT/fuzz_validators"

# Seed corpus is versioned (test/test_fuzz/corpus); the working copy keeps
# whatever coverage previous local runs discovered.
cp -n test/test_fuzz/corpus/* "$OUT/corpus_work/" 2>/dev/null || true

# -max_len=256: the widest validator domain is 96 bytes (isSafeDirPath) plus
# headroom for the overflow regimes; longer inputs only slow exec/s down.
"$OUT/fuzz_validators" \
    -max_total_time="$SECS" -max_len=256 -timeout=5 -print_final_stats=1 \
    "$OUT/corpus_work"
