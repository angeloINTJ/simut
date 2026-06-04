# Contributing to SIMUT

Thanks for your interest in contributing! SIMUT is an open-source IoT firmware for the Raspberry Pi Pico W. Here's how you can help.

## Before You Start

- **Open an issue first** to discuss your idea before writing code. This avoids wasted effort if the change doesn't fit the project roadmap.
- Check the [Security Policy](SECURITY.md) if your contribution touches authentication, networking, or data handling.

## Development Setup

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
```

## Code Conventions

- **Language:** All comments, variable names, and documentation must be in English
- **Naming:** `camelCase` for methods and variables, `_underscorePrefix` for private members, `UPPER_SNAKE_CASE` for constants
- **Indentation:** Tabs for indentation, spaces for alignment
- **Brace style:** K&R — opening brace on the same line as the statement
- **Doxygen:** All public methods in headers must have `@brief` documentation
- **NULL:** Use `nullptr` in C++ code (not `NULL`)
- **Comments:** Explain *why*, not *what* — the code is the "what"

## Flash Budget

Flash is critically tight (~98.7% used). Before adding features, consider:

1. Can it be optimized to use less space?
2. Can it replace something of lower value?
3. Can it live in LittleFS instead of the firmware binary?

## Pull Request Process

1. Open an issue describing the change you want to make
2. Fork the repository and create a branch (`feature/my-feature`)
3. Write your code and test on hardware if possible
4. Ensure `pio run -e pico_w_release` builds with **zero warnings**
5. Ensure `pio test -e native` passes all tests
6. Update documentation in `docs/` if your change affects user-facing behavior
7. Submit the PR with a clear description, referencing the issue number
8. The PR template checklist will guide you through remaining steps

## Testing

- Unit tests use the [Unity](http://www.throwtheswitch.org/unity) framework
- Run with `pio test -e native`
- Add tests for new validation logic, encoding/decoding, and security-critical paths
- Hardware testing is required for display, sensor, WiFi, and OTA changes

## Community

- Report bugs via [GitHub Issues](https://github.com/angeloINTJ/simut/issues)
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md)
- Security vulnerabilities: follow the [Security Policy](SECURITY.md) — do not open a public issue

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
