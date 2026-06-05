# Changelog

All notable changes to SIMUT firmware.

## Unreleased

### Documentation

- **Landing page v2** — redesigned with hero image, Why SIMUT comparison table, screenshots section, architecture diagram, and clear CTAs
- **GitHub Pages fix** — remove raw HTML blocks incompatible with kramdown; add `_config.yml` for theme configuration
- **Social preview** — add `og:image` and `twitter:image` meta tags so URL shares show the TFT dashboard on Reddit, Twitter, Discord, and WhatsApp
- **GitHub topics** — expand from 9 to 20 topics highlighting innovations (offline-first, touchscreen, dual-core, ota-updates, rbac, secure, i18n)
- **Wiring guide** — revise pin assignments and assembly details; fix Pin Reference table rendering on GitHub Pages (kramdown rejected `...` row)
- **Changelog in Portuguese** — `CHANGELOG.pt-BR.md` mirroring the English version
- **Contributor recognition** — generate `CONTRIBUTORS.md` with All-Contributors emoji grid; add badge and contributor table to both READMEs; add Lorenzo Longaretto as second contributor
- **README demo GIF** — add animated TFT dashboard demo above the fold
- **Contributions Welcome badge** — green badge signaling openness to new contributors
- **GitHub Discussions** — enable community Q&A channel
- **FUNDING.yml** — Sponsor button as professional maintenance signal
- **CONTRIBUTING.md expanded** — add "Finding Something to Work On" section with skill-to-issue mapping table; add AI tools policy

### Community

- **Second external contribution** 🎉 — Docker development environment so contributors can build and test without installing PlatformIO locally ([@JohnMartin0301](https://github.com/JohnMartin0301))
- **First external contribution** 🎉 — 672-line HistoryCodec v2 test suite covering roundtrip encoding, anchor frame boundaries, NaN compression, and buffer overflow ([@LorenzoLongaretto](https://github.com/LorenzoLongaretto))
- **12 `good first issue`** tickets created across docs, design, DevOps, embedded, i18n, and security
- **5 new labels** added: `tests`, `display`, `i18n`, `ci`, `tools`, `security`

### Infrastructure

- **Docker development environment** — `Dockerfile` + `docker-compose.yml` so contributors can build and test without installing PlatformIO locally; image size 1.66 GB ([@JohnMartin0301](https://github.com/JohnMartin0301))
- **.editorconfig** — consistent indentation across editors
- **Social preview image** — 1280×640 PNG for Open Graph sharing
- **Landing page images** — TFT dashboard, Web UI screenshots, animated demo GIF
- **Test suite verified** — 49/49 tests passing in 0.9s (27 validators + 22 HistoryCodec)

## v1.0.0 (2026-06-03)

### Initial Public Release

- **Multi-sensor support** — Up to 10 DS18B20 (1-Wire) + 1 DHT22 ambient sensor
- **Zero-trust sensor pipeline** — ROM verification, hardware mismatch detection, error hysteresis
- **320×240 ILI9341 TFT display** — Dashboard, real-time graphs, touch-driven settings (XPT2046)
- **50 built-in themes** + custom theme support via LittleFS
- **Embedded web server** — Multi-user sessions, RBAC (10 permission bits), file manager
- **gzip-compressed WebUI** — Minified inline pages with shared CSS/JS
- **Telemetry** — HTTP POST and MQTT with JSON/CSV/custom templates, TLS/SSL
- **Dual-channel CLI** — USB Serial + Bluetooth (BLE)
- **NTP time sync** — Exponential backoff, multi-server fallback, virtual RTC
- **History codec v2** — Delta + sensor-mask + anchor encoding, ~45% size reduction
- **Hardened authentication** — HMAC-SHA256, per-user random salt, 5000 rounds
- **OTA firmware updates** — Upload via web UI, config snapshot preservation, auto-reboot
- **Backup & restore** — Full LittleFS backup/restore with CRC32 integrity (BKP1 format)
- **Crash forensics** — Watchdog scratch register autopsy with cross-core health monitoring
- **Internationalization** — English + Portuguese/Spanish via external language packs
