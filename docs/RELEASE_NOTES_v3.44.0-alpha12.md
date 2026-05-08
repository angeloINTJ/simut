# SIMUT v3.44.0-alpha12 — Pre-release / Alpha

**Status:** EXPERIMENTAL — pré-release com mudanças funcionais novas e investigação acumulada. Não usar em produção.

**Data:** 2026-05-08
**Branch:** feature/ota-self-flash
**Baseado em:** v3.43.21 (último estável com 76% OTA pass rate)

---

## Highlights

### 🎯 Achado #5 RESOLVIDO (alpha9)

`LogManager::safeReboot()` agora replica EXATAMENTE a sequência MMIO de `applier_reboot` Fix #3 (PSM_WDSEL → CLR_ENABLE → scratch[4]=0 → LOAD=0xFFFFFF → TRIGGER → dsb).

Antes:
- alpha4 tentou Fix #5 mas omitiu `Clear ENABLE` + `scratch[4]=0` com ordem trocada → regressão grave (1/20 PASS).
- alpha5-8 reverteram para `watchdog_enable(500,1)` original (instável: bricks ocasionais em `reload confirm`).

Agora: alpha9 valida em HW que `reload confirm` recupera HTTP 200 em ~60s (era 100% brick em alpha4).

### 🎯 Achado #7 RESOLVIDO (alpha12) — boot 10x mais rápido

`StorageManager::begin` agora usa **cooperative quiet mode** via `_bigSaveQuietCb` em vez de `enterFlashSafeMode` IRQ-based.

**Captura HW alpha11:**
```
[BOOT step] 5: pre _storageMgr->begin() @ 6840
[DSP] Lockout stuck >10s, restarting Core 1
[BOOT step] 6: pos _storageMgr->begin() fsOk=1 @ 18508
```
→ **11.7s** entre step 5 e step 6 (timeout do lockout + restart Core 1).

**Captura HW alpha12 (mesmo board, mesmo build sequência):**
```
[BOOT step] 5: pre _storageMgr->begin() @ 6843
[BOOT step] 6: pos _storageMgr->begin() fsOk=1 @ 8496
```
→ **1.7s** (cooperative quiet mode = `multicore_reset_core1` direto).

Boot total `SYS READY`: alpha11 ~52s → alpha12 **42.6s** (10s economia).

### 🛠 Test firmwares isolados em `tools/test_firmwares/`

Três firmwares novos pra debug independent dos componentes:

1. **`pico_blink_echo`** — recovery firmware mínimo (LED blink + Serial echo). Usado pra reviver Pico em brick state quando outros firmwares falham. Empírico do user: "Pico volta à vida com firmware bem básico".

2. **`pico_multicore_lockout_test`** — replica condições do SIMUT setup (Core 0 attempts lockout sobre Core 1 doing busy work). **Resultado: 10/10 lockouts em 9-28 microsegundos.** Multicore_lockout primitive em si está OK; "Lockout stuck >10s" no SIMUT alpha9-11 é específico do código (não da API).

3. **`pico_applier_reboot_test`** — replica APENAS o pattern MMIO de `applier_reboot` (Fix #3). Conta boots via `scratch[1]`. **Resultado: 5/5 reboots successful** com USB CDC enumera, serial flui, watchdog_caused_reboot()==YES. **Pattern de reset funciona perfeitamente em isolamento.**

4. **`pico_cyw43_reset_test`** — `WiFi.begin → connect → WiFi.end → applier_reboot` em loop. **Resultado: 6/6 boots, WiFi connecta em ~6.6s consistente, IP/RSSI estáveis.** **CYW43 chip + driver são INOCENTES** do residual brick — recuperam de watchdog reset sem necessidade de power-cycle ou deinit.

### ⏱ Timeouts enxutos em test scripts (user 2026-05-08)

- `tools/ota_apply.py`: `wait_for_device` 180s → 30s (wrapper do `test_f9_snapshot.sh` faz wait extra)
- `tools/test_f9_snapshot.sh`: `timeout 240` → `80s` no `ota_apply.py` call; removida `cycle 2 + reset longo` no `wait_post_apply_with_recovery`; bump 90s → **180s** wait único (cap absoluto 3 min do user).
- `tools/test_f9_loop20.sh`: nova recovery via blink revival pattern (erase + flash blink + erase + flash alpha). Adiciona ~2 min/brick mas funciona reliably.

Total snapshot test agora: ~2.5 min PASS, 4 min worst case BRICK (vs 7 min antes).

### 🔧 PicoHand v3 (mão robótica) — INPUT_PULLUP em pin_release

Empírico em HW: `pinMode(INPUT)` puro após `digitalWrite(LOW)` às vezes não libera totalmente a linha — `read_back=L` observado. `INPUT_PULLUP` garante HIGH via pull-up interno.

Bonus achado: **GP0 da mão = QSPI_CS do Pico target** (BOOTSEL = QSPI_SS_N). Durante operação normal, RP2040 SoC drive QSPI CS continuamente. `read_back=L` na GP0 em runtime é normal, NÃO é bug da mão.

### 🐛 Achado #6 DESCARTADO (alpha6, alpha7 reverted)

Hipótese inicial do user: `ota_snapshot_restore_to_lfs` em `StorageManager::begin` sem `enterFlashSafeMode` poderia causar race com Core 1 XIP fetch.

**Tentativas:**
- alpha6 (wrap com 2 enter/exit pairs): boot brick (segundo enter trava 10s).
- alpha7 (single enter/exit pair): **deadlock reentrante** — `LittleFS.write` internamente já usa `multicore_lockout` via `flash_safe_execute`. Wrap externo + lock interno = Core 0 hold + LFS try acquire = forever stuck.

**Conclusão:** LFS protege a write sozinho. Race hipotética estava coberta. Hipótese descartada, alpha8 voltou à lógica alpha5 (sem wrap).

---

## Status do residual brick OTA (PENDENTE pra v4.0.0)

Após múltiplas iterações:
- alpha9 fixou `reload confirm`
- alpha10 adicionou `Serial.ignoreFlowControl(true)` (debug aid)
- alpha11 adicionou wait `isCore1Ready` (defensivo)
- alpha12 substituiu IRQ lockout por cooperative quiet mode

**Mas OTA apply ainda brick.** Sintoma: USB CDC enumera (`2e8a:f00a`) mas zero serial output mesmo com 180s wait. HW reset via mão recupera o device.

**Ruled out via test firmwares isolados:**
- ❌ `applier_reboot` MMIO pattern: 5/5 PASS
- ❌ CYW43 + WiFi reset cycle: 5/5 PASS
- ❌ `multicore_lockout` primitive: 10/10 em 10us

**Próximas hipóteses (não isoladas ainda):**

1. **Flash erase+program post-applier deixa QSPI controller em estado inconsistente.** Fix v3.43.7-9 supostamente abordou sector 0 program, mas pode ter case residual.

2. **LFS reformat automático pós-OTA tem race** com snapshot restore. Boot detecta state==APPLYING, restora system.bin, depois LFS reformata novamente?

3. **Display/touch init pós-OTA tem timing diferente** — Core 1 startup pode ter ordem diferente quando `_displayMgr->_tftFirstInit=true` vs `=false`.

4. **autopsia + scratch state interaction** — watchdog scratch persiste de boot anterior. Se algum estado weird estiver lá, pode disparar path raro.

**Próximo passo recomendado:** test firmware adicional que reproduz o caminho COMPLETO de OTA mas sem o display/touch (apenas WiFi + LFS + applier_reboot). Se brick aqui, é LFS. Se não, é Display/Touch.

---

## Arquivos modificados

```
LogManager.cpp                     — Fix #5 v2 (Achado #5)
StorageManager.cpp                 — cooperative quiet mode (Achado #7)
AppManager_Boot.cpp                — Serial.ignoreFlowControl + isCore1Ready wait
SystemDefs_Limits.h                — version bumps + history dos achados
docs/INVESTIGATION_BOOTLOOP.md     — +200 linhas com Achados #5-#7 detalhados
STABILITY_PLAN.md                  — alpha8-12 journey
tools/PicoHand/pico_hand/...       — v3 INPUT_PULLUP fix
tools/ota_apply.py                 — wait_for_device 180s→30s
tools/test_f9_snapshot.sh          — timeouts enxutos + 180s wait
tools/test_f9_loop20.sh            — blink revival recovery pattern
tools/test_firmwares/              — 4 test firmwares novos
```

## Memórias persistidas (`/.claude/projects/.../memory/`)

- `feedback_pico_revival.md` — flashar firmware básico revive Pico
- `feedback_test_timeout_3min.md` — Pico bota em ≤3 min ou não bota
- `feedback_test_firmwares_individual.md` — testar componentes isolados
- `feedback_v4_autonomy.md` — autonomia full pra commit/push até v4

---

## Validação em HW (2026-05-08)

| Teste                              | Resultado |
|-----------------------------------|-----------|
| alpha9 boot via picotool          | ✅ SYS READY 52s |
| alpha9 `reload confirm`           | ✅ HTTP 200 em ~60s |
| alpha9 OTA apply                  | ❌ 1/1 brick |
| alpha10 boot capture (com ignoreFlowControl) | ✅ Steps 1-3, [DSP] lockout visible |
| alpha11 boot                      | ✅ SYS READY 52.6s (com lockout-stuck recovery) |
| alpha12 boot                      | ✅ SYS READY 42.6s (sem lockout-stuck!) |
| alpha12 `reload confirm`          | ✅ HTTP 200 |
| alpha12 OTA apply                 | ❌ 1/1 brick (residual) |

**Pass rate baseline (alpha12 OTA): n=1, FAIL — não-statistical.** Loop20 pendente.

---

## Próximos passos pra v4.0.0

1. Loop20 alpha12 → confirma pass rate vs v3.43.21 baseline
2. Test firmware: WiFi + LFS reset cycle (isola LFS)
3. Test firmware: TFT/touch init timing
4. Se identificar causa root → fix → loop20 → 0 bricks → v4.0.0-rc1
5. Decisão: aceitar 24% brick rate como known limitation (libera v4 com warning) OU continuar fix

---

🔗 Branch GitHub: https://github.com/angeloINTJ/SIMUT/tree/feature/ota-self-flash
