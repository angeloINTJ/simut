# SIMUT — Concurrency Invariants

> Companion to the stability plan. Every rule below is enforced (or made
> enforceable) by **stability wave 1**; PRs must not violate them.
> Referenced from code comments as `docs/CONCURRENCY.md`.

## The model in one paragraph

Core 0 runs everything except the display, cooperatively, and owns the
watchdog. Core 1 runs only `DisplayManager::loopCore1( )` and is a
`multicore_lockout` victim: LittleFS flash writes stall it via
`flash_safe_execute`, which also **disables IRQs on Core 0** for the
duration of each program/erase — that window is what starves the CYW43
radio and is now measured by the `FLASH_OP` metrics (`show metrics`).
For heavy saves, Core 0 puts the display in *quiet mode*: since wave 1,
it first asks Core 1 to **park at the top of its loop** (a point outside
`malloc`, the event-queue spinlock and any SPI burst) and only then
hard-resets it; the park wait, `CORE1_QUIESCE_MS` (1.2 s), falls back to
the old immediate reset, so behavior can never be worse than before.
The alpha has no quiet mode, and its `requestQuietMode( )` says so by
returning false: a heavy save then pauses Core 1 around each flash write,
the way the history and the log are written. Its loop parks at the top
like the TFT's. Until 2026-10-03 it did neither: the call answered true
with Core 1 still running from the flash being written, and every other
pause waited out the full park timeout.

## Invariants

1. **Core 1 never touches flash/LittleFS.** Language/license loads run
   on Core 0 only (`DisplayManager.cpp`, `setLanguage`).
2. **Core 1's steady state is heap-free, log-free and queue-gated.**
   No `String`/`new` in render/touch paths (wave 1 removed the per-frame
   offenders); the single `LOG_CODE` in DisplayManager runs on Core 0
   (`forceUnpause`); every UI event goes through `pushUiEvent( )`, which
   drops events while quiesce is pending so the queue spinlock can never
   be held at reset time.
3. **Never hold `_stateMutex` across any `FLASH_OP`/LittleFS call.**
   Core 1 blocks on that mutex; a flash lockout would then deadlock.
   Setters stay short; wave 2 adds a debug assert.
4. **Every `request*` has a matching `release*`.** Quiet mode is
   refcounted; the T1.5 watchdog force-drains a leaked refcount after
   15 s and logs `APP_DISPLAY_PAUSE_STUCK` param=1.
5. **Working `static` buffers in Web/Telemetry/Graph/Storage are
   Core-0-only and non-reentrant.** They exist to spare the ~4 KB stack;
   never touch them from IRQs or Core 1.
6. **Core 1 is reset only after a confirmed park or an explicitly logged
   timeout** (`DSP_FORCE_UNPAUSE` param=1 = quiesce timeout fallback).
7. **Long flash work is sliced.** `enforceStorageLimit( )` deletes at
   most 2 files per call; the remainder drains via `update( )` one file
   per ≥15 s slice (`STO_ENFORCE_BUDGET` "deferred" marks the handoff).
8. **Anything reached from a `flash_range_*` call path lives in SRAM.**
   `src/FlashIrqProbe.cpp` wraps the two SDK flash primitives via
   `-Wl,--wrap`, so its shims sit on the OTA applier's path too — and
   `ota_applier_run( )` erases the whole application slot while running
   from SRAM. A shim in the app slot would be fetched from an erased
   sector on the next call and brick the device mid-update. Both shims
   are `__not_in_flash_func`, they read `timer_hw->timerawl` directly
   instead of calling `time_us_32( )`, and they stay in 32-bit
   arithmetic so no flash-resident libgcc helper is reached.
   `tools/check_flash_probe.py` fails the build if either shim links
   outside `0x2000_0000–0x2004_2000`.
9. **The watchdog window cannot be widened — bound the work instead.**
   The RP2040 load register saturates at `WATCHDOG_TIMEOUT_MS` = 8388 ms,
   so every `WdtWindow` asking for 30 s or 120 s gets exactly the default.
   The class is still useful (a *shorter* nested window works, and the
   save/restore is correct) but it buys no time. Two corollaries, both
   learned the expensive way:
   - **Feed synchronously, from the thread doing the work.** An interrupt
     timer feeding on the side does not survive a blocking call that masks
     interrupts; `TelemetryGuard` was exactly that and never once fed
     during the `http.POST()` it existed to cover.
   - **Never feed through a call you have not bounded.** Feeding turns a
     recoverable watchdog reboot into a permanent freeze. A TLS handshake
     with no deadline wedged Core 0 with USB still enumerated and both the
     CLI and the web dead, until a hardware reset. Bound the call first,
     then feeding inside that bound is safe — that pairing is what
     `tools/arduino_pico_overrides` restores for BearSSL.

## The alpha's glass: Core 1 publishes, Core 0 reads

The 16x2 cannot be read back, so the driver keeps a shadow of what it wrote (`src/display/LcdGlass.h`) and
`DisplayManager::lcdGlass( )` gives Core 0 one consistent copy of it (the handler of `GET /api/lcd`,
`WebManager::handleApiLcd( )`, calls it once per request). It keeps the rules above:

- **No lock, no heap, no log on Core 1** (invariants 2 and 3). The shadow is a seqlock, the pattern of the Ângulo's frame
  record (`s_seq` and `uaEstado( )` in `ui/angulo/DisplayManager_UiA.cpp`): the writer counts to an odd number, fences,
  copies, fences again and counts to the next even one; the reader trusts a copy only if it saw the same even number before
  and after, with a fence on each side of its copy. Both of the writer's fences matter: without the first, the copy can be
  seen before the odd count. A
  mutex Core 1 held when Core 0 started a flash write would freeze Core 1 with the mutex held.
- **After the bus, never before.** `blit( )` and `createChar( )` publish after their bus writes, so the shadow never leads
  the glass; `print( )`, `clear( )` and `setCursor( )` compose in the framebuffer and publish nothing. The cells and the eight
  glyphs are one snapshot under one count.
- **A publish that dies heals in one publish.** A Core 1 relaunched mid-publish (`APP_CORE1_DEAD`, "Core 1 dead >10s") keeps the
  driver, so the count stays odd. The next publish makes it odd again in a single store (a blind `+ 1`, as the Ângulo's
  writer does, turns the parity around for good), and drops the frame flag: the reader says "no frame" until the next
  `blit( )` rewrites all thirty-two cells.
- **The reader is bounded.** It waits for an odd count at most 2 ms (a 20 us step, the Ângulo's) and returns 1 copied, 0 no
  frame yet, -1 busy: the numbers of `uaEstado( )`. The caller answers "busy", not "none", for -1.
- **How it is held** (2026-11-04). `alpha_glass.cpp` and `alpha_driver_twin.cpp` in `test/test_validators/` stop the writer
  after each of its stores and land a whole publish at each byte of the reader's copy (a host thread would find a tear only
  by chance), and the same harnesses report a mix on a writer without the odd count and on two readers that skip one look at
  the count. What a host cannot see is the fence: it is `dmb ish`, read from the disassembly of the pico_wire image, and the source check now
  holds the order (`tools/check_glass_seqlock.py`, in the `gates` job): in the text of `LcdGlass.h` the writer's odd store, a fence, the data, a
  fence and the even store, and the reader's first look at the count, a fence, the copy, a fence and the second look, each fence a statement of its
  own and not in a comment or under an `if`. All 408 native cases passed with any one of the four fences taken out (the review of step 1,
  2026-11-04); the check gives exactly one finding for each of those, and for a store or a look moved across a fence. The disassembly showed `dmb ish`
  on the M0+ (the pico_wire image) and on the M33 (a copy of the writer compiled for the RP2350, since the alpha images are RP2040 builds), GCC 14.3,
  the review of 2026-11-04.
- **What it costs.** The publish of a `blit( )` is 33 instructions after the bus loops (24 in `blit( )`, 9 in `open( )`), two
  16-byte `memcpy` calls and a `millis( )` (pico_wire image; counted, not timed on a board), against at least 3.5 ms of bus
  time for the blit (34 bytes, two nibbles each, 52 us of delays per nibble). The shadow is 216 B of RAM: 108 B of static RAM in
  the driver's `_lcd`, and 108 B of heap in `DisplayManager::_driver`, an alpha driver nothing uses that is a member of the
  class (the review of 2026-11-04 read it in the pico_wire image: `.bss` +108 B, and the size AppManager passes to operator new
  2,600 to 2,708 B). It is 216 B of flash in `pico_w_alpha` and `pico_wire`; the reader is 160 B in the object and is dropped from the image until something calls it.

## Reading the new metrics

`show metrics` → `[STORAGE]`:

- `Flash ops (avg)` — total `FLASH_OP` blocks and mean duration.
- `Worst op` — longest single block since boot; the upper bound of one
  radio stall. Target after wave 1: **< 60 ms** in steady state.
- `>50ms` — count of ops that almost certainly included a 4 KB erase.
  If this grows outside history rollover/cleanup, investigate.
- `IRQ-off max / avg` — the **actual** interrupts-disabled window around
  flash program/erase, in microseconds. The three lines above measure a
  whole `FLASH_OP` block (mutex, LittleFS bookkeeping, the write); this
  measures only what LittleFS brackets with `noInterrupts( )`, which is
  the stall cyw43/lwIP really sees. The plan's soak criterion is stated
  on this number, not on `Worst op`.
- `IRQ-off erase / prog / >1ms` — call counts per primitive and how many
  windows exceeded 1 ms. Erases dominate; a rising `prog` count with
  flat erases means many small appends landing inside one block.

## Deliberately deferred (wave 3)

- lwIP `pbuf` low-water metric (build-flag change in the overrides tool,
  needs a build/RAM measurement cycle).
- History write batching (rides on the V4 pending mechanism; product
  decision on the acceptable power-loss window).

## Resolved by wave 2 (2026-07-22)

- **BMP280 on hardware I2C** — audit confirmed it was already implemented
  upstream (`SensorManager` routes valid pin pairs to `Wire`/`Wire1` via
  `i2cPeripheralForPins`, multi-driver pool per bus/address). Wave 2
  added the missing piece: the PIO/bit-bang fallback now logs a **WARN
  with the valid HW pin pairs**, so cause C1/C3 can never regress
  silently again.
- **Invariant-3 tripwire** — `ConcurrencyAsserts.h`; enable with
  `-DSIMUT_CONCURRENCY_ASSERTS` to make FLASH_OP log an ERROR if entered
  while the current core owns `_stateMutex`.
- **DS18B20 mismatch auto-recovery** (sensor doc issue #3) — a
  quarantined slot re-verifies the ROM every 10th cycle and lifts the
  quarantine when the configured chip returns; a wrong chip stays out.
