# Docs images

Screenshots and photos for the SIMUT documentation. Everything in this tree is a **real capture** from running hardware or the live web interface — no mockups.

## How the captures are made

- **TFT screens** — captured with:
  ```bash
  python3 tools/screen_mapper.py --out docs/images/screens --scale 2
  ```
  against a device flashed with the `pico_w_test` env. The frames are read back from the panel itself over `GET /api/screenshot`, so they show exactly what the display renders.
- **Web pages** — captured with Playwright + Chrome against the device's web interface.

## Notable files

| File | What it shows |
|---|---|
| `tft-dashboard.png` | TFT main dashboard |
| `tft-tour.gif` | Animated tour of the TFT screens |
| `web-dashboard.png` | Web UI dashboard |
| `web-sensors.png` | Web UI sensors page |
| `screens/` | Full gallery of TFT screens (see `screens/screens.md`) |
| `social-preview.png` | GitHub social preview card |
| `bench-assembly.jpg`, `hardware-assembly.png`, `IMG_20260605_082253.jpg` | Bench photos of the Pico W + TFT + sensors rig |
