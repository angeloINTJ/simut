# SIMUT — Quickstart próxima sessão

Resumo executivo do estado + comandos prontos para retomar.

## Estado atual (final 2026-05-08)

- **Branch:** `feature/ota-self-flash` @ commit a5d31d5+
- **Firmware ativo:** alpha14 (CLI touch sim integrada)
- **Device:** persistente em F-USB-CDC-DEAD pós-flash — requer **power-cycle físico** (USB unplug + replug do Pico W) para reset limpo
- **Mão pico_hand:** funcional, responde PING
- **Toolchain captura:** pronto em `tools/manual_capture/` + `tools/diagnostics/` + `tools/test_firmwares/`

## Quickstart (após power-cycle)

```bash
cd /home/angelo/Documentos/SIMUT

# 1. Verificar device responde
ls /dev/serial/by-id/  # deve mostrar Raspberry_Pi_Pico_W_E6642815E34C1824
.venv/bin/python3 -c "
import serial; s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00', 115200, timeout=2)
s.write(b'\r\nshow system info\r\n'); import time; time.sleep(2)
print(s.read(2048).decode())
"

# 2. Se não responder: revival via mão
.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00', 115200, timeout=3)
time.sleep(0.3); s.reset_input_buffer()
s.write(b'BOOTSEL\n'); time.sleep(3); s.close()
"
sleep 3
picotool erase -a
picotool load -x tools/test_firmwares/pico_blink_echo/build/pico_blink_echo.ino.uf2
sleep 5
# Verifica blink alive
.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00', 115200, timeout=1)
time.sleep(0.3); print(s.read(512).decode()[:200])
"
# Esperado: '[XXX] tick led=0' / '[XXX] tick led=1'

# 3. Flash alpha14 (sobre blink)
.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00', 115200, timeout=3)
time.sleep(0.3); s.reset_input_buffer()
s.write(b'BOOTSEL\n'); time.sleep(3); s.close()
"
sleep 3
picotool load -x .pio/build/pico_w_release/firmware.uf2

# 4. Aguardar boot ~60s, configurar WiFi
sleep 60
.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00', 115200, timeout=2)
s.dsrdtr=False; s.rtscts=False
time.sleep(0.5); s.reset_input_buffer()
for c in [b'conf system ssid ProcrastinationPLUS\r\n',
          b'conf system pass A\$AGzD3XeY7xSrwAg5JF\r\n',
          b'write memory\r\n']:
    s.write(c); time.sleep(2)
s.close()
"
# HW reset (não usar reload confirm pra evitar safeReboot)
.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_E660C062131E3E27-if00', 115200, timeout=3)
time.sleep(0.3); s.reset_input_buffer()
s.write(b'RESET\n'); time.sleep(0.5); s.close()
"
sleep 60
curl -s -m 3 -o /dev/null -w "HTTP %{http_code}\n" http://192.168.3.195/api/login_init
# Esperado: HTTP 200

# 5. Validar touch sim
.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00', 115200, timeout=2)
s.dsrdtr=False; s.rtscts=False
time.sleep(0.3); s.reset_input_buffer()
s.write(b'touch sim 50 220\r\n'); time.sleep(1)
print(s.read(1024).decode()[:300])
s.close()
"
# Esperado: 'Touch injected at (50, 220)' + display TFT muda para Settings

# 6. Capturar TFT screenshot
NONCE=$(curl -s http://192.168.3.195/api/login_init | python3 -c "import json,sys; print(json.load(sys.stdin)['nonce'])")
HASH=$(echo -n 'F9Test@2026' | sha256sum | head -c 64)
curl -c /tmp/cookie -X POST -d "user=admin&pass=$HASH&nonce=$NONCE" http://192.168.3.195/api/login
curl -b /tmp/cookie -o /tmp/tft.bmp http://192.168.3.195/api/screenshot
file /tmp/tft.bmp
# Esperado: BMP 320x240 24-bit valid image

# 7. Pipeline completo de captura
F9_PASS="F9Test@2026" bash tools/manual_capture/capture_tft_screenshots.sh
F9_PASS="F9Test@2026" python3 tools/manual_capture/capture_browser_screenshots.py
sudo apt install -y pandoc texlive-xetex imagemagick
bash tools/manual_capture/build_manual_pdf.sh
# Output: docs/MANUAL.pdf

# 8. Loop20 alpha14 stats
F9_PASS="F9Test@2026" bash tools/test_f9_loop20.sh
# Output: docs/test_reports/f9_loop20_*.log com PASS/FAIL count

# 9. Investigação residual brick (se loop20 < 90% PASS)
# Use test firmwares isolados para bisecção
ls tools/diagnostics/  # test_cyw43, test_flash, test_flash_raw
ls tools/test_firmwares/  # pico_*
```

## Decisão v4 GA

- **Pass rate ≥85% no loop20 alpha14** → documentar 15% brick como known
  limitation, lançar v4.0.0 com warning + recovery procedures
- **Pass rate <85%** → continuar investigação invasiva (test firmwares
  novos: test_lfs_reformat_post_ota, test_display_init_timing)

## Investigação pendente (não crítico para v4 GA)

- Touch sim handleTouch integration: testado em código mas não validado em HW (alpha14 build OK, device dependency)
- Boot reliability: alpha14 boot dá lockout-stuck mas recovery pode falhar (intermitente)
- Possível origem: TFT init Core 1 com IRQs OFF brevemente durante SPI burst → Core 0 lockout fails

