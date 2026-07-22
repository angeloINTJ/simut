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

## Reading the new metrics

`show metrics` → `[STORAGE]`:

- `Flash ops (avg)` — total `FLASH_OP` blocks and mean duration.
- `Worst op` — longest single block since boot; the upper bound of one
  radio stall. Target after wave 1: **< 60 ms** in steady state.
- `>50ms` — count of ops that almost certainly included a 4 KB erase.
  If this grows outside history rollover/cleanup, investigate.

## Deliberately deferred (wave 2)

- BMP280 → hardware `Wire`/`Wire1` (needs the device on the bench;
  option D of `ANALISE_INSTABILIDADE_SENSORES.md`).
- Debug assert for invariant 3; lwIP `pbuf` low-water metric
  (build-flag change in the overrides tool).
- History write batching (rides on the V4 pending mechanism).
