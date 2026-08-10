# arduino_pico_overrides — SIMUT's changes to the arduino-pico framework

> **What `lwipopts.h` actually changes today**, diffed against the stock header
> (2026-08-10, framework 1.50403.0 = arduino-pico 5.4.3):
>
> | | stock | here | why |
> |---|---|---|---|
> | `TCP_WND` | `8 * TCP_MSS` | `4 * TCP_MSS` | the D14 fix, below |
> | `LWIP_STATS` | 0 | 1 | pool counters — without them D14 is unmeasurable |
> | `MEMP_STATS` | 0 | 1 | same |
>
> Nothing else. **`PBUF_POOL_SIZE` is left at the stock 24.**

## Why This Exists

The heading above used to read "Savings: ~18 KB RAM (`PBUF_POOL_SIZE` 24→12)",
and this section used to explain that SIMUT has 1-2 simultaneous TCP
connections so 12 envelopes is plenty. Neither is true of the file any more,
and the drift matters: **the D14 arithmetic depends on the pool being 24**, so
a reader who believed the old headline would compute the window budget wrong.

What the overrides are for now is bounding things upstream leaves unbounded —
a TLS handshake with no overall deadline, two HTTPClient read loops with no
upper limit, a send loop that never feeds the watchdog, received pbufs not
released when a connection is abandoned — plus the one arithmetic fix in
`lwipopts.h`.

### The `TCP_WND` fix (2026-08-10, D14)

A pool entry costs ~1514 B, and at `8 * TCP_MSS` a single connection can hold
7,7 of them. Six connections filling their windows want 46 against a pool of
24: the pool was being promised out twice over. Four clients peak at 13 and
never fail; five reach 24/24 with 45 failed allocations, six with 79.

At `4 * TCP_MSS` those failures go to 0/0 and the peak at six clients drops to
18/24. It costs nothing measurable, because the device could never use the
window it was advertising: uploads run at 26 KB/s bound by flash writes, and at
a ~5 ms round trip even 4×MSS allows about 1,1 MB/s. Downloads are governed by
`TCP_SND_BUF` and are untouched.

**Growing the pool was the wrong lever** — 24 entries are already 35,5 KB of
BSS, and doubling costs more than the whole free heap. That is why the number
that the old headline said had been halved is instead left exactly as upstream
ships it.

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
| `TCP_WND` | `8 * TCP_MSS` | **`4 * TCP_MSS`** | at 8×MSS one connection can hold 7,7 pool entries; six connections want 46 against 24 |
| `LWIP_STATS` | 0 | **1** | pool in-use / peak / failure counters |
| `MEMP_STATS` | 0 | **1** | same — without these, "is it a leak or is it pressure?" has no answer |

`PBUF_POOL_SIZE` is **at the stock 24**. An earlier revision of this file cut it
to 12 for ~18 KB of BSS, and the tables further down still measure that build;
they are kept as the historical record of the v1.0.0 slim build, not as a
description of what ships now.

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

## HW Validation (2026-05-10) — historical, describes the v1.0.0 slim build

> These numbers were taken when `PBUF_POOL_SIZE` was 12. It is 24 today, so the
> pool rows below no longer describe the shipping image. Kept because the mDNS,
> RSSI and telemetry rows are what established that the pool can be touched at
> all without breaking the shared CYW43 radio.

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
