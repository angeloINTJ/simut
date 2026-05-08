# SIMUT — Session Status 2026-05-08

**Branch:** feature/ota-self-flash
**Latest commit:** alpha14 (CLI touch sim infra)
**Total commits this session:** 13

---

## Achievements

### Firmware (v3.44.0-alpha8 → alpha14)

| Alpha | Mudança | Status HW |
|-------|---------|-----------|
| 8 | revert alpha4 broken Fix #5 | ✅ baseline |
| 9 | Fix #5 v2 CORRETO (safeReboot MMIO igual applier_reboot) | ✅ reload confirm OK em ~60s |
| 10 | `Serial.ignoreFlowControl(true)` | ✅ debug serial visibility |
| 11 | wait `isCore1Ready` defensive | ✅ defensive (não regrediu nada) |
| 12 | cooperative quiet mode (10x boot) | ❌ revertido — instabilidade boot |
| 13 | revert alpha12 = alpha11 baseline | ✅ último estável validado |
| 14 | CLI `touch sim X Y` infra completa | 🔧 build OK, validação HW pendente |

### Test firmwares isolados

Em `tools/test_firmwares/`:

1. **`pico_blink_echo`** — recovery firmware mínimo (LED + Serial echo). Empírico do user: "Pico volta à vida com firmware bem básico" (revival de brick severo).

2. **`pico_multicore_lockout_test`** — 10/10 lockouts em 9-28us. **multicore_lockout primitive em si OK.**

3. **`pico_applier_reboot_test`** — 5/5 reboots via MMIO Fix #3 pattern. **applier_reboot pattern OK em isolamento.**

4. **`pico_cyw43_reset_test`** — 6/6 boots, WiFi connecta em ~6.6s consistente. **CYW43 NÃO é o bug do residual brick.**

### Tooling

- `tools/test_f9_snapshot.sh`: timeouts enxutos (240s→80s no ota_apply, removida cycle 2, 90s→180s wait único)
- `tools/test_f9_loop20.sh`: recovery via blink revival pattern
- `tools/ota_apply.py`: wait_for_device 180s→30s
- `tools/PicoHand` v3: `INPUT_PULLUP` em pin_release (empírico HW)

### Documentação (1425+ linhas adicionadas)

- `docs/MANUAL.md` (519 linhas) — Manual completo do sistema com cenário hipotético baseado em `data/calib.csv` (12 sensores cadeia fria laboratorial)
- `docs/INVESTIGATION_BOOTLOOP.md` +200 linhas (Achados #5 RESOLVIDO, #6 DESCARTADO, #7 partial)
- `docs/RELEASE_NOTES_v3.44.0-alpha12.md`
- `docs/SESSION_STATUS_20260508.md` (este arquivo)
- `STABILITY_PLAN.md` atualizado

### Memórias persistidas

- `feedback_pico_revival.md` — flashar firmware básico revive brick
- `feedback_test_timeout_3min.md` — Pico bota em ≤3 min ou não bota
- `feedback_test_firmwares_individual.md` — testar componentes isolados
- `feedback_v4_autonomy.md` — autonomia full pra commit/push até v4

---

## Status do residual OTA brick (PENDENTE)

Após múltiplas iterações, o residual brick OTA persiste:
- Sintoma: USB CDC enumera (`2e8a:f00a`) mas zero serial output após apply
- HW reset (mão RESET) recupera reliably
- Não acontece em test firmwares isolados

**Hipóteses ruled out via test firmwares:**
- ❌ `applier_reboot` MMIO pattern (5/5 PASS)
- ❌ CYW43 + WiFi reset cycle (6/6 PASS)
- ❌ `multicore_lockout` primitive (10/10 em 10us)

**Hipóteses ativas (não isoladas ainda):**
1. Flash erase+program post-applier deixa QSPI controller em estado weird
2. LFS reformat automático pós-OTA tem race com snapshot restore
3. Display/touch init pós-OTA tem timing diferente
4. Watchdog scratch state interaction

---

## Próximos passos para v4 stable

### Imediato (próxima sessão)

1. **Revival reliable do device** — possivelmente power-cycle físico necessário (USB unplug + replug)
2. **Validar alpha14 boot** + WiFi config via `reload confirm` (alpha9 fix valida `safeReboot` correto)
3. **Testar touch sim:**
   ```
   SIMUT> touch sim 160 120     # tap centro
   SIMUT> touch sim 50 220      # botão Settings
   ```
4. **Capturar screenshots TFT** via `/api/screenshot` (autenticado, BMP 320×240)

### Gerar manual com screenshots automáticos

```bash
# Login + obter cookie
NONCE=$(curl -s http://192.168.3.195/api/login_init | python3 -c "import json,sys; print(json.load(sys.stdin)['nonce'])")
HASH=$(echo -n "F9Test@2026" | sha256sum | head -c 64)
curl -c cookie -X POST -d "user=admin&pass=$HASH&nonce=$NONCE" http://192.168.3.195/api/login

# Sequência de telas: tap + capture + save
for screen_target in "dashboard:0,0" "settings:50,220" "graph:160,220" "alarms:270,220"; do
    name=$(echo $screen_target | cut -d: -f1)
    coord=$(echo $screen_target | cut -d: -f2)
    x=$(echo $coord | cut -d, -f1)
    y=$(echo $coord | cut -d, -f2)
    
    # Tap via CLI
    .venv/bin/python3 -u -c "
import serial
s = serial.Serial('/dev/serial/by-id/...if00', 115200, timeout=2)
s.write(b'touch sim $x $y\\r\\n')
s.close()
"
    sleep 1  # aguarda render
    
    # Capture screenshot
    curl -b cookie -o "docs/screenshots/tft_${name}.bmp" http://192.168.3.195/api/screenshot
done
```

### Browser screenshots via Selenium

```python
from selenium import webdriver
from selenium.webdriver.chrome.options import Options
opts = Options()
opts.add_argument('--headless')
driver = webdriver.Chrome(options=opts)

# Login (usar API direto pra simplificar)
driver.get('http://192.168.3.195/login')
# ... fill form, submit ...

for url in ['/', '/history', '/alarms', '/config', '/network', '/users', '/files', '/license']:
    driver.get('http://192.168.3.195' + url)
    driver.save_screenshot(f'docs/screenshots/web{url.replace("/","_")}.png')
```

### PDF final

```bash
# Insert image references in MANUAL.md placeholders ([ ])
# Then convert
pandoc docs/MANUAL.md -o docs/MANUAL.pdf \
  --pdf-engine=xelatex --toc --highlight-style=tango \
  -V geometry:margin=2cm
```

### Investigação residual OTA brick

Próximas tentativas:
1. **Test firmware: WiFi + LFS reset cycle** — isola se LFS é o componente buggy
2. **Test firmware: TFT/touch init timing** — captura tempo de cada operação
3. **Sniff JTAG/SWD** durante o brick (requires HW probes)
4. **Picotool save** firmware partition após brick + comparar bytes com fresh build

### Decisão v4 GA

Se loop20 alpha13/14 mostrar pass rate ≥85%:
- Documentar 15% brick rate como known limitation
- Adicionar warning no /files OTA modal
- Lançar v4.0.0 com recovery instructions
- Manual completo + screenshots

Se pass rate <85%:
- Continuar investigação invasiva
- v4.0.0 reservado até atingir estabilidade

---

## GitHub

Branch: https://github.com/angeloINTJ/SIMUT/tree/feature/ota-self-flash

Últimos 13 commits:
```
1d0979e [Alpha v3.44.0-alpha14] CLI touch sim X Y
fbfa88b docs: MANUAL.md — sistema completo
dd6ada5 [Alpha v3.44.0-alpha13] Reverte cooperative quiet mode
d1f167b docs: release notes v3.44.0-alpha12
7c33bb6 test_firmwares: CYW43 reset test
156995d test_firmwares: applier_reboot isolated test
de0c96c [Alpha v3.44.0-alpha12] StorageManager cooperative quiet mode
a625f83 [Alpha v3.44.0-alpha11] isCore1Ready wait
4069b3d [Alpha v3.44.0-alpha10] ignoreFlowControl debug aid
c8a0652 docs: alpha4-9 journey
21fa8c7 loop20 recovery: blink revival
8bccb33 [Alpha v3.44.0-alpha9] Fix Achado #5 v2
8c35204 [Alpha v3.44.0-alpha8] Reverte Fix #5 + tooling
```

---

🤖 Status gerado 2026-05-08. Próxima sessão começa com revival do device e validação alpha14 touch sim para captura automatizada de screenshots do manual.
