# Manual Capture Toolchain

Pipeline para gerar `docs/MANUAL.pdf` automaticamente com screenshots TFT
e browser. Roda em 3 etapas:

## 1. Capturar TFT screenshots (requer device alpha14+ online)

```bash
F9_PASS="<senha admin>" bash tools/manual_capture/capture_tft_screenshots.sh
```

Output: `docs/screenshots/tft_*.bmp` (8 telas: dashboard, settings_main,
graph, alarms_action, slot_detail, etc).

Mecânica: usa CLI `touch sim X Y` (alpha14) pra navegar entre telas +
GET `/api/screenshot` (já existente no firmware) pra capturar BMP 320×240.

## 2. Capturar browser screenshots

```bash
pip install selenium
F9_PASS="<senha>" python3 tools/manual_capture/capture_browser_screenshots.py
```

Output: `docs/screenshots/web_*.png` (8 páginas: login, dashboard,
history, alarms, config, network, users, files, license).

Requer: Chrome/Chromium + chromedriver no PATH.

## 3. Gerar PDF final

```bash
sudo apt install pandoc texlive-xetex imagemagick
bash tools/manual_capture/build_manual_pdf.sh
```

Output: `docs/MANUAL.pdf` com TOC + screenshots embed.

## Troubleshooting

- **Touch sim não funciona:** alpha14+ necessário (build atual). Verifique
  `show system info` reporta `v3.44.0-alpha14` ou superior.

- **Screenshots TFT vazias:** verifique permissão `PERM_SYS_CONFIG` do
  user admin (default: ✅).

- **Chrome erro:** instale `chromium-driver`: `sudo apt install chromium-driver`.
