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
| Web UI source | `WebUI.h` at the repository root, written in English. The other language comes from the device: `GET /api/lang` serves the `@WEBDICT` of the active `.lng` pack (`data/lang/` has pt-BR and es-ES), and a key the pack lacks shows the English text. `tools/build_webui_gz.py` compresses it into `src/WebUI_GZ.h` on every build — never edit the generated header |
| Tools | `tools/` — [`tools/README.md`](tools/README.md) says which of the 198 scripts are live |
| Documentation | `docs/` — [`docs/README.md`](docs/README.md) marks each document **Living** or **Snapshot** |
| Interface standard | [`ANGULO.md`](ANGULO.md) — the Ângulo guide for anything with a screen or a reader: web UI, site, README, docs, brand. [`AGENTS.md`](AGENTS.md) §7 says how it applies here and what `tools/check_angulo.py` enforces in CI |
| Tests | `test/` — eleven native environments, `pio test -e native…` |
| OTA keys | `keys/` — the public roots and the floors every image compiles (`keys/README.md`). Private keys never enter the repository; the bench ones live in `~/.simut-ota/` |

## Building and testing

```bash
pio run -e pico_w_release      # the shipping image
pio run -e pico_w_air          # the hibernating build; least flash headroom
pio test -e native_logpolicy   # one suite; eleven exist
```

**Eleven** firmware environments and eleven native suites, all of them built and
run by CI — `pico_w_asserts` is the one that is easy to forget, because it is
not in the table above and nothing local reminds you of it. This line said
"five" until 2026-09-20, and a change that raised the other five budgets went
to CI with that one over its own. The seventh, since 2026-10-01, is
`pico2_w_release`: the release compiled for the Pico 2 W's RP2350, which CI
builds and nothing runs on a board yet. **`pio run` with no `-e` builds only
`pico_w_release`** — `default_envs` in `platformio.ini` says so, and this line
claimed the opposite until 2026-09-22. The eighth, since 2026-10-12, is
`pico_wire`: the SIMUT Wire, a plain Pico without the radio, its network on an
ENC28J60 module (the driver: `src/net/`, `src/net/NetworkManager_Wired.cpp`; on the
bench since 2026-10-12). The ninth, since 2026-10-13, is `pico_wiretft`: the same
Wire with the TFT panel and touch in place of the 16x2 LCD (the module's INT is
GP11 with either display); CI builds it, and a build of the same switches
(`build_custom.py`) ran on the bench on 2026-10-12. The tenth, since 2026-10-14
on the `feat/ui-drivers` branch, is `pico_wiretft_angulo`: the Ângulo UI
driver's bench image, `pico_wiretft` with the touch panel drawn by
`src/ui/angulo/` (`ui = "angulo"` in the manifest). It is transitional, not
approved for products and not published; CI builds it and holds it to its own
flash budget, and it is not a required check. The eleventh, since 2026-10-21 on
the same branch, is `pico2_w_angulo`: the Ângulo on the Pico 2 W, `pico2_w_release`
with the touch panel drawn by `src/ui/angulo/` (the maintainer decided that day
that the Ângulo ships first on that board). Not approved for products and not
published either; CI builds it and holds it to its own flash and static-RAM
budgets, it is not a required check, and since 2026-10-22 the Pico 2 W on the
bench runs it (the S10b tip's image, then the S10c A' tip's: see section 2.16 of
`docs/analysis/UI_DRIVERS.md`). Name the eleven.
There is no debug environment — see the note in `platformio.ini` for why.

## What will fail your build before CI does

Several gates run as `extra_scripts` on every `pio run`, so you meet them
locally. When one fires, it is usually right:

- **`-Werror` on `src/`** (not on `.pio/libdeps` — third-party warnings are
  not ours). One exemption, `-Wno-error=array-bounds`, documented at the flag.
- **Flash budget** — `tools/check_flash_budget.py` against
  `tools/flash_budget.json`. The budget is a high-water mark, not the linker
  ceiling. Growing past it is allowed; doing so without editing the budget in
  the same change is not. Unlike the rest, this one is **not** an extra_script: CI runs it
  after each image's build, and a local `pio run` passes an image that is over.
  Run `python3 tools/check_flash_budget.py <env> <build.log>` yourself — on
  2026-10-02 four images of a PR went to CI 64 to 624 B over, past a local
  build that said nothing. It also holds each `firmware.bin` to the size
  recorded as `"bin"`: an image that grows records the new size, and its row in
  the OTA headroom table of `docs/analysis/PLANO_STABLE.md` §3.1, in the same
  change (`--table` checks the two agree; the gate prints the numbers).
  `pico_w_test_https` is exempt from the OTA ceiling (`ota_exempt`).
- **Log codes** — a new `LogCode` means editing `tools/logcodes.tsv` and
  running `tools/gen_logcodes.py`, never the generated `.h`.
- **Language packs** — a new `TRL("…")` literal must be added to the `@TRL`
  block of both `data/lang/*.lng` packs, keyed by a hash of the English. The
  gate prints the hash it wants. A missing entry is not a visible failure at
  runtime: that one line silently stays in English.
- **The Ângulo's texts** — a text the Ângulo draws (`src/ui/angulo/`) is
  `UA_TXT( "<English>" )`, and each one needs a line in the `@ANGULO` section
  of both packs, keyed by the FNV-1a of the English (the language-pack gate
  prints the hash it wants). A literal with a letter outside `UA_TXT( )`
  fails `tools/check_ui_texts.py` in CI unless `tools/ui_texts_allow.json`
  names it with a reason. Section 2.17 of `docs/analysis/UI_DRIVERS.md`
  says why, and what each rule holds.
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

- `main` is protected. Every change goes through a pull request, and the nine
  required checks must pass: every CI job except the `pico_w_test_https`,
  `pico_wire`, `pico_wiretft`, `pico_wiretft_angulo` and `pico2_w_angulo` images
  (`firmware (pico2_w_release)` joined on 2026-10-01).
- Commit messages and pull requests are written in English; conversation with
  the maintainer is in Portuguese.
- Measure before claiming. If a change is described as saving bytes, making
  something faster, or fixing a failure, the number or the reproduction goes
  in the commit message — and if it was not verified on hardware, say so.
- Tests first. Before implementing a feature, a function, or a refactor, write
  down the tests that will prove it: the native-suite cases that pin the
  behaviour, and — for what the host cannot exercise — the check on the rig
  (which values to compare, before and after). When the implementation is done,
  run them and put the result in the PR. A refactor's proof is that behaviour is
  unchanged (a normalized diff, an identical sha256, or a before/after on
  hardware); a fix's is a reproduction that fails before and passes after. New
  host-testable behaviour earns a new case in `test/`; there are eleven native
  suites for a reason.
