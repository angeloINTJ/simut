# Docs images

This directory contains screenshots and diagrams for the SIMUT project documentation.

## Needed Images

The following images would significantly improve the README and documentation:

- `tft-dashboard.jpg` — TFT display showing the main dashboard (temperature, humidity, WiFi status)
- `web-dashboard.jpg` — Web UI dashboard with sensor cards and graphs
- `hardware-assembly.jpg` — Pico W + ILI9341 TFT + sensors wired on a breadboard
- `web-login.jpg` — Web login screen
- `architecture.png` — System architecture diagram (dual-core, data flow)

## How to Add Images

1. Take photos/screenshots and save them to this directory
2. Reference them in README.md as: `![Description](docs/images/filename.jpg)`
3. Keep file sizes under 500 KB for fast loading
4. Use JPEG for photos, PNG for screenshots and diagrams

To generate a TFT screenshot programmatically:
```bash
curl -s http://<device-ip>/api/screenshot -o docs/images/tft-dashboard.bmp
convert docs/images/tft-dashboard.bmp docs/images/tft-dashboard.jpg
```
