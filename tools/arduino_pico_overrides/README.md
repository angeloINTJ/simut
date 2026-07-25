# arduino_pico_overrides — slim build of the arduino-pico framework

> **Savings: ~18 KB RAM** (`PBUF_POOL_SIZE` 24→12 in `lwipopts.h`).
> Combined with firmware Tier 1.2 (screenshot heap-alloc, +15 KB), **total ~33 KB**.

## Why This Exists

To reduce the RAM footprint of the lwIP PBUF pool, which occupies 36 KB with the
default arduino-pico config (24 envelopes × ~1530 B). SIMUT has 1-2 simultaneous
TCP connections — 12 envelopes is plenty.

## Important Discovery (v1.0.0)

PIO compiles **most of lwIP from source** in each project build, NOT from the
precompiled `liblwip.a`. The `.o` files are cached in
`.pio/build/<env>/FrameworkArduino/lwip/src/`.

**Implication:** patches in `lwipopts.h` propagate to the next SIMUT build after
cache invalidation. It is **NOT necessary** to rebuild `liblwip.a` — an older
version of this script tried that (wrong path, took ~10 min and gave no extra gain).

**BTstack is still precompiled** in `liblwip-bt.a` — patches in `btstack_config.h`
would require a framework rebuild. But I tried (v1.0.0, discarded in v1.0.0):
**changing BTstack breaks cyw43 RSSI sampling** (the CYW43439 chip is shared between
WiFi and BT on the Pico W; reducing HCI or disabling profiles affects WiFi management).

That's why v1.0.0 only patches `lwipopts.h`, leaving BTstack untouched.

## Contents

```
arduino_pico_overrides/
├── README.md                  ← this file
├── patch.sh                   ← applies overrides (idempotent)
├── restore.sh                 ← reverts to originals
├── originals/                 ← virgin backup (.gitignored, created by patch.sh)
├── patched_headers/
│   └── lwipopts.h             ← SIMUT-tuned header
└── patches/
    └── wifi_tls_handshake_deadline.patch
```

## TLS handshake deadline (2026-07-25)

> **Turns a wedged device into a logged error.**

`WiFiClientSecureBearSSL.cpp::_wait_for_handshake()` has no overall deadline.
The only timeout lives inside `_run_until()`, which restarts its own
`start = millis()` on every call, so `setTLSConnectTimeout()` bounds a single
iteration and never the handshake. All three `optimistic_yield()` calls in that
file are commented out in this port, so nothing yields and nothing feeds the
watchdog either.

Against a peer that accepts TCP but never completes the handshake — a
telemetry port set to 8443 when the server listens on 443 was enough — Core 0
spins there forever. Measured on the bench: with the watchdog armed the device
rebooted every ~22 s; with it disarmed around the POST it froze permanently,
USB still enumerated and both CLI and web dead, until a hardware reset.

The patch carries one deadline across iterations, clamps the per-call timeout
to the remaining budget, and feeds the watchdog inside the loop. The feed is
safe **only** because the loop is now provably bounded — that is what lets
`NET_TLS_HANDSHAKE_MS` exceed the RP2040's 8.388 s watchdog ceiling, which a
real BearSSL handshake on a 133 MHz Cortex-M0+ needs.

Unlike `lwipopts.h` this is a `.cpp`, applied with `patch(1)` rather than
copied, so a framework update fails loudly instead of silently reverting. The
WiFi library has its own PIO object cache (`lib*/WiFi/`) which `patch.sh`
invalidates — without that the build succeeds while still linking the
unpatched handshake.

## Changes Applied

### `lwipopts.h`

| Setting | Original | Patched | Reason |
|---|---|---|---|
| `PBUF_POOL_SIZE` | 24 | **12** | SIMUT has 1-2 simultaneous TCP connections; 12 pre-allocated envelopes are plenty |

`MEMP_NUM_TCP_PCB`, `MEMP_NUM_UDP_PCB` remain at defaults (5 and 7) —
reducing UDP_PCB **breaks mDNS** (DHCP+DNS+NTP+mDNS responder = 4 PCBs minimum).

### `btstack_config.h`

**Not modified** — changes to BTstack break RSSI sampling on the Pico W.

## Usage

```bash
# Apply overrides (first time OR after arduino-pico update via PIO)
bash tools/arduino_pico_overrides/patch.sh

# Revert (debug or comparison)
bash tools/arduino_pico_overrides/restore.sh
```

`patch.sh` is idempotent — you can run it as many times as you want. `originals/`
is preserved after the first patch.

## HW Validation (2026-05-10)

| Metric | Without patch | With patch | Savings |
|---|---|---|---|
| RAM SIMUT v1.0.0 | 49.6% (129,900 B) | **36.7% (96,156 B)** | -33 KB |
| Flash | 98.7% | 98.8% | ~0 |
| `memp_memory_PBUF_POOL_base` | 36,771 B | 18,387 B | -18 KB |
| `WebManager::handleApiScreenshotChunk::payload` | 15,360 B (BSS) | 0 B (heap on demand) | -15 KB |
| mDNS responder (`simut.local:5353`) | ✅ works | ✅ **works** | — |
| RSSI display | ✅ -35 dBm | ✅ **-35 dBm** | — |
| HTTP Telemetry | ✅ | ✅ | — |
| Backup/Restore via API | ✅ (29/29 critical) | ✅ (29/29 critical) | — |

## What Was NOT Done (and Why)

| Attempt | Result | Status |
|---|---|---|
| `MEMP_NUM_UDP_PCB` 7→2 | Broke mDNS responder | ❌ reverted in v1.0.0 |
| `MEMP_NUM_TCP_PCB` 5→3 | No measurable effect (PCBs are small) | ❌ not worth the complexity |
| BTstack profiles 0 (AVRCP/HFP/HIDS/AVDTP) | Broke RSSI sampling on display | ❌ reverted in v1.0.0 |
| `MAX_NR_HCI_CONNECTIONS` 2→1 | Broke RSSI sampling on display | ❌ reverted in v1.0.0 |

## When This Breaks

- `pio update` or `pio pkg update framework-arduino-pico` → framework reinstalled,
  override lost. **Reapply:** `bash tools/arduino_pico_overrides/patch.sh`.

## Limits of This Approach

- Not portable to other projects without the patched arduino-pico.
- Reapplying the patch takes **<1 second** (just copies header + invalidates cache).
- The next PIO build recompiles lwIP source (~30s) the first time after patching.

## Upstream Alternative

If the Arduino-Pico Foundation accepts a PR adding `#ifndef` guards in
`lwipopts.h`, this patch becomes obsolete — it will suffice to add
`-D PBUF_POOL_SIZE=12` in `build_flags` of platformio.ini, without a local
framework patch.
