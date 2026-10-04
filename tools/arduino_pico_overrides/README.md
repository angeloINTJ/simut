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
└── patches/                   ← one unified diff per override, applied by patch.sh
    ├── wifi_tls_handshake_deadline.patch
    ├── bearssl_suite_trim.patch
    ├── bearssl_server_static_pool.patch
    ├── httpclient_read_deadlines.patch
    ├── httpclient_send_feed.patch
    ├── httpclient_no_cookies.patch      ← 2026-09-24, flash: -5,864 B on every image
    ├── clientcontext_rx_leak.patch
    ├── clientcontext_acked_feed.patch
    ├── webserver_parse_deadline.patch
    ├── webserver_keepalive.patch
    ├── webserver_cors_origin.patch
    ├── cyw43_join_budget.patch          ← 2026-10-01, the join wait bounded
    ├── littlefs_rp2350_untranslated.patch  ← 2026-10-03, RP2350 only: LittleFS from a slot
    └── wifiserver_accepted_not_evicted.patch ← 2026-10-04, a long transfer is not killed for a newcomer
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

## Join wait bounded (2026-10-01)

> **Turns a radio that refuses a join into a logged refusal, not a watchdog reboot.**

`CYW43::begin( )` in `lwIP_CYW43/src/utility/CYW43shim.cpp` asks for the join
and waits for the request to take. When the radio takes it, the first pass of
the loop returns at once and the association finishes in the background. When
the radio refuses it (`cyw43_wifi_join( )` failing), the loop asked again with
no pause for the whole of `_timeout` (15 s, set by `WiFiClass`), then waited in
a `while(true)` tail with no bound at all, and nothing fed the watchdog.

Field log of 2026-09-30 22:13, a device at -79 dBm: the link dropped, two scans
never finished, the blind join that followed was refused, and Core 0 passed the
RP2040's 8.388 s watchdog inside this loop. The watchdog rebooted the device.

`cyw43_join_budget.patch` gives the wait one deadline (`SIMUT_CYW43_JOIN_WAIT_MS`,
4 s, or `_timeout` if shorter), pauses 50 ms between attempts, and feeds the
watchdog inside the loop, which is safe only because the loop now ends. At the
deadline a request the radio holds is accepted (`true`) and one it never took is
refused (`false`, which `WiFi.begin( )` answers as `WL_IDLE_STATUS`).
`NetworkManager::takeJoinAnswer( )` counts the refusals and plans the restart a
radio that keeps refusing needs (`SystemDefs_Network.h`). The library has its own
object cache (`lib*/lwIP_CYW43/`), which `patch.sh` and `restore.sh` invalidate.

## LittleFS read from a slot, RP2350 only (2026-10-03)

> **Lets the RP2350 image run from a slot of a partition table and still read its filesystem.**

On the RP2350 SIMUT boots from slot A of a partition table (step 3 of
`docs/analysis/OTA_AB_RP2350.md`; the layout is `tools/rp2350/`). The boot ROM
maps the slot at `0x10000000` with a window the size of the slot, 1,532 KB, and
an XIP read past that window faults. LittleFS sits outside the slots, at
`0x300000`, and `LittleFS.cpp` reads it by `memcpy` from its mapped address,
`_FS_start`. Without the patch the image of a slot faults the first time it
mounts LittleFS.

`littlefs_rp2350_untranslated.patch` makes that read, on the RP2350 only, go to
the physical offset through `XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE`
(`0x1C000000`), which the ROM never translates. On a board without a partition
table the read is the same, because both windows show the same bytes.
Programs and erases already used the physical offset. The RP2040 code is not
touched (`#if defined(PICO_RP2350) && PICO_RP2350`). The window bypasses the
XIP cache. `tools/check_rp2350_image.py` stops the RP2350 build when this
framework lacks the patch. The library has its own object cache
(`lib*/LittleFS/`), which `patch.sh` and `restore.sh` invalidate.

## Accepted connections are not killed for a newcomer (2026-10-04)

> **Keeps a long upload or download alive while a page of the device is open in a browser.**

`ClientContext` puts every connection at `TCP_PRIO_MIN`. When lwIP's PCB pool
runs out (`MEMP_NUM_TCP_PCB` is 5), `tcp_alloc( )` admits a newcomer, at
`TCP_PRIO_NORMAL`, by killing the most idle connection below that, and a tie
goes to the oldest. The web server serves one client at a time, so during a
long request each poll of an open page waits in `WiFiServer`'s queue, a PCB
each. A browser re-sends at once a poll that was reset; the queued ones stay as
fresh as the request being served, and that request, the oldest, is the one
killed. The reset leaves with the board's own TTL, and it passed for a router
cutting port 80 from August until 2026-10-03.

Measured on the bench Pico 2 W with a refused upload of 993,801 B (no session,
so nothing is written), 2026-10-03 and 04:

| Load during the upload | Before | After |
|---|---|---|
| none, at 50 KB/s | answer at 19.9 s | answer at 19.9 s |
| a poll every 3 s, re-sent at once on a reset (`poller_repro.py --retry`), at 50 KB/s | reset at 13.1 s, 2 of 2 | answer at 19.9 s, 2 of 2 |
| a tab of the web UI open in Firefox, at 50 KB/s | reset at 12.4–14.8 s, 8 of 8 | not run |
| a poll every 3 s that its client resets after 2 s, at 16 KB/s (62 s) | — | answer at 62.1 s; pbuf pool 6 of 24, 0 failures |

`wifiserver_accepted_not_evicted.patch` does four things:

- `WiFiServer::_accept` raises every connection it accepts to
  `TCP_PRIO_NORMAL`, so nothing in the queue is killed for a newcomer. With the
  pool full, the newcomer's SYN goes unanswered and is sent again, and no reset
  reaches a browser to be re-sent.
- lwIP calls that callback with a null pcb when it cannot allocate one
  (`tcp_in.c`); upstream built a `ClientContext` on it. That path never ran
  while every connection could be killed to make room, and runs now: refused.
- `ClientContext::close( )` lowers a connection the app closes back to
  `TCP_PRIO_MIN`, so what lwIP keeps of it (FIN_WAIT) can make room as before.
- A connection lwIP drops while it still waits in the queue (reference count
  0: no `WiFiClient` ever took it) frees the request it buffered, since nothing
  will answer it. Without this, the 62 s run above took the pbuf pool to 24 of
  24 with 5 failed allocations.

A first design raised only the request being served. The upload got past 13 s,
but every queued poll killed in its place kept its pbuf until the upload
ended; the browser's retries filled the pool (24 of 24, 38 failures) and the
upload stalled at about 704 KB.

Outbound connections (telemetry, MQTT) keep `TCP_PRIO_MIN`. Cost: +32 B on
`pico_w_release`, `pico_w_air` and `pico2_w_release`, +40 B on the rest. The
WiFi library has its own object cache (`lib*/WiFi/`), which `patch.sh` and
`restore.sh` invalidate.

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

### `HTTPClient.cpp` — cookie parsing compiled out (2026-09-24)

SIMUT never installs a `CookieJar` (telemetry authenticates with a bearer
key), so the body of `HTTPClient::setCookie( )` could never run — yet it was
**linked**, because the header parser calls it on every `Set-Cookie`. Its two
`strptime( )` calls pulled newlib's `strptime_l` (2,528 B) plus the parsing
body and the locale tables it drags in. `httpclient_no_cookies.patch` puts an
early `return` after the `_cookieJar` null-check and wraps the rest of the
body in `#if 0` (kept so the diff against upstream stays readable). The public
API (`setCookieJar` / `resetCookieJar` / `clearAllCookies`) is untouched; a jar
installed at runtime is simply never populated.

Measured on `pico_w_release`: `used` 1,013,028 → 1,007,164 (**−5,864 B**);
the same delta on test / test_https / asserts, −4,6 kB on alpha and air.
`strptime` and `strptime_l` are gone from every image; `mktime` (288 B) stays,
it has other callers.

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
| mDNS responder (`simut.local:5353`) | works | **works** | — |
| RSSI display | -35 dBm | **-35 dBm** | — |
| HTTP Telemetry | works | works | — |
| Backup/Restore via API | works (29/29 critical) | works (29/29 critical) | — |

## What was not done, and why

| Attempt | Result | Status |
|---|---|---|
| `MEMP_NUM_UDP_PCB` 7→2 | Broke mDNS responder | reverted in v1.0.0 |
| `MEMP_NUM_TCP_PCB` 5→3 | No measurable effect (PCBs are small) | not worth the complexity |
| BTstack profiles 0 (AVRCP/HFP/HIDS/AVDTP) | Broke RSSI sampling on display | reverted in v1.0.0 |
| `MAX_NR_HCI_CONNECTIONS` 2→1 | Broke RSSI sampling on display | reverted in v1.0.0 |

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
