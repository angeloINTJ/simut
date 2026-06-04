# Changelog

All notable changes to SIMUT firmware.

## Unreleased

- Add 672-line HistoryCodec v2 test suite — roundtrip encoding, anchor frame boundaries, NaN compression, buffer overflow ([@LorenzoLongaretto](https://github.com/LorenzoLongaretto) — first community contribution! 🎉)

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
