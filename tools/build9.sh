#!/usr/bin/env bash
# build9.sh <worktree> <out> [env...] — builds the product images one by one and keeps, for each,
# firmware.bin, firmware.elf and the build log in <out>/<env>/, then writes <out>/sha256.txt, one
# line per image: "<sha256>  <size>  <env>", or "FALHOU(<rc>)  -  <env>" when a build failed.
# The exit status is 0 either way: read sha256.txt (a failed build is a line, not a status).
#
# Default list, the nine product images of CLAUDE.md: pico_w_release pico_w_test pico_w_test_https
# pico_w_asserts pico_w_alpha pico_w_air pico2_w_release pico_wire pico_wiretft. The bench image
# pico_wiretft_angulo is not among them: name it to build it.
#
# What it is for (V-SHA, docs/analysis/UI_DRIVERS.md): a change that promises the product images
# unchanged builds the base and the branch with this script, and
#     diff <base>/sha256.txt <out>/sha256.txt
# prints nothing (hash, size and env). For sizes equal to CI's, build with a private core patched
# like CI's, never the shared ~/.platformio, which lacked override 2n on 2026-10-14 (its
# pico_w_release was 328 B under CI's 1,034,500 B):
#     PLATFORMIO_CORE_DIR=<private core> tools/build9.sh <worktree> <out>
# and patch that core with tools/arduino_pico_overrides/patch.sh. Never set PLATFORMIO_BUILD_FLAGS
# here: it wipes .pio/build (AGENTS.md).
#
# A new worktree's second `pio run` may wipe .pio/build (WebUI_GZ.h changes the project checksum),
# so every artefact is copied right after its own build.
set -u
wt=$1; out=$2; shift 2
envs=${*:-pico_w_release pico_w_test pico_w_test_https pico_w_asserts pico_w_alpha pico_w_air pico2_w_release pico_wire pico_wiretft}
mkdir -p "$out"
: > "$out/sha256.txt"
for e in $envs; do
  mkdir -p "$out/$e"
  ( cd "$wt" && pio run -e "$e" ) > "$out/$e/build.log" 2>&1
  rc=$?
  b="$wt/.pio/build/$e"
  if [ $rc -eq 0 ] && [ -f "$b/firmware.bin" ]; then
    cp "$b/firmware.bin" "$b/firmware.elf" "$out/$e/"
    echo "$(sha256sum "$out/$e/firmware.bin" | cut -d' ' -f1)  $(stat -c %s "$out/$e/firmware.bin")  $e" >> "$out/sha256.txt"
  else
    echo "FALHOU($rc)  -  $e" >> "$out/sha256.txt"
  fi
  tail -1 "$out/sha256.txt"
done
