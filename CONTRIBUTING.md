# Contributing to SIMUT

**English** | [Português](CONTRIBUTING.pt-BR.md) | [Español](CONTRIBUTING.es-ES.md)

Thanks for your interest in contributing! SIMUT is an open-source IoT firmware for the Raspberry Pi Pico W. Here's how you can help.

## Before You Start

- **Open an issue first** to discuss your idea before writing code. This avoids wasted effort if the change doesn't fit the project roadmap.
- Check the [Security Policy](SECURITY.md) if your contribution touches authentication, networking, or data handling.

## Finding Something to Work On

Look for issues labeled [`good first issue`](https://github.com/angeloINTJ/simut/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) — these are curated for new contributors and range from documentation to embedded testing. Each has clear scope, acceptance criteria, and reference files to get you started.

| Skill | Example issues |
|-------|---------------|
| C/C++ embedded | HistoryCodec tests, CLI parser tests, fuzz testing |
| Python / DevOps | Docker dev environment, cppcheck CI, pre-commit hooks |
| Documentation / i18n | Documentation improvements, social proof badges |
| Design | New built-in color theme |

Not sure where to start? Comment on any `good first issue` and the maintainer will help you scope it.

## Development Setup
### Option A — Docker (recommended for new contributors)

No need to install PlatformIO, Python, or the ARM toolchain locally. Docker handles everything.

**Prerequisites:** [Docker Desktop](https://www.docker.com/products/docker-desktop/) (macOS / Windows) or [Docker Engine](https://docs.docker.com/engine/install/) (Linux)

```bash
# Clone the repository
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Build firmware for the Raspberry Pi Pico W
docker compose run build

# Run all native unit tests
docker compose run test
```

> **First run note:** Docker will build the image and download the ARM toolchain (~500 MB). This only happens once — subsequent runs use the cached image and are fast.
>
> **Linux users:** Export `UID` and `GID` before running so build artifacts aren't owned by root:
> ```bash
> export UID GID
> docker compose run build
> ```

| Command | Equivalent PlatformIO command |
|---|---|
| `docker compose run build` | `pio run -e pico_w_release` |
| `docker compose run test` | `pio test -e native -e native_history_v5 -e native_cli -e native_logpolicy -e native_alarmqueue -e native_air -e native_network` |

---

### Pre-commit hooks

```bash
pip install pre-commit
pre-commit install
```

Runs the whitespace and YAML/JSON checks, plus the repository's own fast gates — the secret scan, the log-code tables, the authorization matrix and the `/config` filesystem guard — so you meet them in the second it takes to commit rather than in the four minutes it takes CI to answer. The build, cppcheck and the fuzz run are deliberately *not* hooked: a pre-commit that takes minutes is one people disable.

### Option B — Local PlatformIO

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) 6.x or later
- Raspberry Pi Pico W
- For hardware testing: ILI9341 TFT display + XPT2046 touch + DS18B20 sensor

### Build

```bash
# Clone the repository
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Build firmware
pio run -e pico_w_release

# Run unit tests
pio test -e native
```

### Flash to Device

```bash
# Flash firmware
pio run -e pico_w_release -t upload

# Upload LittleFS data (language packs, favicon)
pio run -e pico_w_release -t uploadfs
# WARNING: uploadfs REFORMATS the LittleFS partition — it destroys history, config and calibration on a device already in service. First flash only.
```

## Code Conventions

- **Language:** All comments, variable names, and documentation must be in English
- **Naming:** `camelCase` for methods and variables, `_underscorePrefix` for private members, `UPPER_SNAKE_CASE` for constants
- **Indentation:** Tabs for indentation, spaces for alignment
- **Brace style:** K&R — opening brace on the same line as the statement
- **Doxygen:** All public methods in headers must have `@brief` documentation
- **NULL:** Use `nullptr` in C++ code (not `NULL`)
- **Comments:** Explain *why*, not *what* — the code is the "what"

Adding a new sensor driver? Follow the step-by-step guide in [docs/adding-a-new-sensor.md](docs/adding-a-new-sensor.md).

## Flash Budget

Flash is critically tight — ~97.6 % of the 1020 KB app slot in the release env, and ~98 % in `pico_w_air`, which has the least room of the five images. Before adding features, consider:

1. Can it be optimized to use less space?
2. Can it replace something of lower value?
3. Can it live in LittleFS instead of the firmware binary?

Every image has a budget in `tools/flash_budget.json`, checked by CI. The budget is a high-water mark, not the linker ceiling: the linker already refuses an image that does not fit, and what it cannot do is make growth visible while there is still room. Growing past a budget is allowed — raising it in the same change, so the number lands in the diff, is the requirement.

## Pull Request Process

1. Open an issue describing the change you want to make
2. Fork the repository and create a branch (`feature/my-feature`)
3. Write your code and test on hardware if possible
4. Ensure the firmware builds. Warnings in `src/` are errors, so a build that succeeds already has none
5. Ensure all seven native suites pass (see Testing below)
6. Update documentation in `docs/` if your change affects user-facing behavior
7. Submit the PR with a clear description, referencing the issue number
8. The PR template checklist will guide you through remaining steps

## Testing

Unit tests use the [Unity](http://www.throwtheswitch.org/unity) framework. Seven native environments, all of them run by CI:

| environment | what it covers |
|---|---|
| `native` | validators — IP validation, CRC8, float encoding |
| `native_history_v5` | HistoryCodec roundtrip |
| `native_cli` | CLI parser — tokenizing and routing |
| `native_logpolicy` | the edge-triggered log-persistence filter |
| `native_alarmqueue` | the alarm queue's invariants |
| `native_air` | SIMUT Air configuration and bounds |
| `native_network` | the WiFi reconnect state machine, against a controllable radio |

Add tests for new validation logic, encoding/decoding, parser changes, and security-critical paths. Hardware testing is still required for display, sensor and OTA changes.

### A test is what makes a fix a fix

`native_network` exists because a device in the field could not reconnect for three and a half hours and the cause was found by reading its log — there was no test that could have found it. Its stub is not a simulation of the radio; it is a set of knobs shaped like failures that actually happened, and eight of its seventeen cases fail against the code as it stood before the fix.

That is the bar for a test accompanying a bug fix: **run it against the unfixed code and watch it fail.** A regression test that passes either way is worse than none, because it is claimed as cover.

The two largest modules, `TelemetryManager.cpp` and `StorageManager.cpp`, have thin coverage for their size. Rather than a campaign to write tests for them, the standing rule is incremental: **a bug fixed in either of them ships with a native case that reproduces it.** In a year the coverage exists without there ever having been a project to create it.

## Community

- Report bugs via [GitHub Issues](https://github.com/angeloINTJ/simut/issues)
- Ask questions in [GitHub Discussions](https://github.com/angeloINTJ/simut/discussions)
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md)
- Security vulnerabilities: follow the [Security Policy](SECURITY.md) — do not open a public issue

## AI Tools

We use AI assistants (Claude, Copilot, etc.) as **engineering tools**, not as substitutes for judgment. AI helps with boilerplate, documentation drafts, and test scaffolding — but architecture decisions, PIO timing, flash budgeting, and security hardening are human work. If you use AI in your contribution, that's fine — just review the output. Generated code is your responsibility.

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
