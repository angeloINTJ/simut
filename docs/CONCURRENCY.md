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
hard-resets it; the ≤200 ms park wait falls back to the old immediate
reset, so behavior can never be worse than before.

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
