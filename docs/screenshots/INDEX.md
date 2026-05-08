# SIMUT Screenshots — capturados em HW 2026-05-08

Validação end-to-end do pipeline de captura `tools/manual_capture/`.
Hardware: alpha14 com CLI `touch sim X Y` integrado.

## Display TFT (320×240, via `/api/screenshot`)

| Arquivo | Tela | Como foi navegado |
|---------|------|-------------------|
| `tft_dashboard_initial.png` | Dashboard inicial | Sem ação (estado pós-boot) |
| `tft_01_dashboard.png` | Dashboard | Captura inicial da pipeline |
| `tft_02_settings.png` | Settings | `touch sim 50 220` |
| `tft_03_graph.png` | Graph | `touch sim 280 20` (back) + `touch sim 160 220` |
| `tft_04_alarms.png` | Alarms | `touch sim 280 20` + `touch sim 270 220` |
| `tft_05_slot_detail.png` | Slot detail | `touch sim 280 20` + `touch sim 80 80` |

## Web UI (1280×800, via Selenium headless Chrome)

| Arquivo | Página | Endpoint | Title |
|---------|--------|----------|-------|
| `web_01_login.png` | Login | `/login` | (sem login) |
| `web_02_dashboard.png` | Dashboard | `/` | SIMUT - Dashboard |
| `web_03_history.png` | History | `/history` | SIMUT - History |
| `web_04_alarms.png` | Alarms | `/alarms` | SIMUT - Alarms & Sounds |
| `web_05_config.png` | Config | `/config` | SIMUT - Config |
| `web_06_network.png` | Network | `/network` | SIMUT - Network |
| `web_07_users.png` | Users | `/users` | SIMUT - Users |
| `web_08_files.png` | Files | `/files` | SIMUT - Files |
| `web_09_license.png` | License | `/license` | SIMUT - License |

## Como reproduzir

```bash
F9_PASS='F9Test@2026' bash tools/manual_capture/capture_tft_screenshots.sh
F9_PASS='F9Test@2026' .venv/bin/python3 tools/manual_capture/capture_browser_screenshots.py
```

## Para gerar PDF

```bash
sudo apt install pandoc texlive-xetex imagemagick
bash tools/manual_capture/build_manual_pdf.sh
# Output: docs/MANUAL.pdf
```
