# CLAUDE.md

Read [`AGENTS.md`](AGENTS.md) first — it is the operational bench manual
(flashing the Pico W, the SIMUT Air hibernation build and its traps, how the
binary log's transition filter behaves). This file exists because Claude Code
loads `CLAUDE.md` automatically and does not load `AGENTS.md`; everything of
substance lives there, not here.

## Where things are

| | |
|---|---|
| Firmware source | `src/` — ~56k lines, C++17, RP2040 / arduino-pico |
| Web UI source | `WebUI.h` at the repository root, all 8 languages. `tools/build_webui_gz.py` compresses it into `src/WebUI_GZ.h` on every build — never edit the generated header |
| Tools | `tools/` — [`tools/README.md`](tools/README.md) says which of the 104 scripts are live |
| Documentation | `docs/` — [`docs/README.md`](docs/README.md) marks each document **Living** or **Snapshot** |
| Tests | `test/` — six native environments, `pio test -e native…` |

## Building and testing

```bash
pio run -e pico_w_release      # the shipping image
pio run -e pico_w_air          # the hibernating build; least flash headroom
pio test -e native_logpolicy   # one suite; six exist
```

Five firmware environments and six native suites, all of them built and run by
CI. There is no debug environment — see the note in `platformio.ini` for why.

## What will fail your build before CI does

Several gates run as `extra_scripts` on every `pio run`, so you meet them
locally. When one fires, it is usually right:

- **`-Werror` on `src/`** (not on `.pio/libdeps` — third-party warnings are
  not ours). One exemption, `-Wno-error=array-bounds`, documented at the flag.
- **Flash budget** — `tools/check_flash_budget.py` against
  `tools/flash_budget.json`. The budget is a high-water mark, not the linker
  ceiling. Growing past it is allowed; doing so without editing the budget in
  the same change is not.
- **Log codes** — a new `LogCode` means editing `tools/logcodes.tsv` and
  running `tools/gen_logcodes.py`, never the generated `.h`.
- **Language packs** — a new `TRL("…")` literal must be added to the `@TRL`
  block of both `data/lang/*.lng` packs, keyed by a hash of the English. The
  gate prints the hash it wants. A missing entry is not a visible failure at
  runtime: that one line silently stays in English.
- **Authorization matrix** — every new HTTP route needs a gate or an
  allowlist entry with a reason (`docs/AUTHORIZATION.md`).

## House style

- Comments explain **why**, and carry the measurement that settled it. This
  codebase cites dates and byte counts in comments on purpose.
- A comment that no longer matches the code is treated as a defect, not as
  noise — a miscounted one has cost this project a day before.
- Match the surrounding file: it uses spaces inside parens (`update( )`) in
  most places, and that is deliberate consistency, not an accident to fix.

## Working agreements

- `main` is protected. Every change goes through a pull request, and the eight
  required checks must pass.
- Commit messages and pull requests are written in English; conversation with
  the maintainer is in Portuguese.
- Measure before claiming. If a change is described as saving bytes, making
  something faster, or fixing a failure, the number or the reproduction goes
  in the commit message — and if it was not verified on hardware, say so.
