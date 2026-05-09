# Residual brick alpha14+ — root cause análise

## Sintoma reproduzível em HW (sessão 2026-05-08)

Após picotool erase + blink revival (chip 100% saudável) + flash alpha14/15/16:

```
[BOOT step] 1: pos-banner @ 2338
[BOOT step] 2: _displayMgr->begin() @ 2338
[BOOT step] 3: _displayMgr->startCore1() @ 2338
[DSP] Lockout stuck >10s, restarting Core 1
... (silêncio total, hang permanente)
```

Boot reaches step 3, lockout-stuck recovery message imprime, mas Core 0 nunca progride pra step 4/5/6. Boot completamente hung.

## Sequência detalhada do hang

1. **Step 3:** `_displayMgr->startCore1()` lança Core 1 via `multicore_launch_core1`
2. Core 1 executa `loopCore1`: chama `multicore_lockout_victim_init()`, set `_core1Ready=true`, depois faz init pesado de TFT/touch/canvas
3. **Step 5 (não logado):** Core 0 chama `_storageMgr->begin()` que chama `enterFlashSafeMode()` → `pauseRendering(true)` → `multicore_lockout_start_timeout_us(500ms)`
4. Core 0 loop retry por 10s (`pauseRendering` line 318)
5. Após 10s, Core 0 dispara recovery: `multicore_reset_core1()` + `multicore_launch_core1(core1Entry)` (line 326-328)
6. Core 1 deveria reiniciar — mas Core 0 nunca volta a executar (provável que `multicore_launch_core1` está bloqueando OU multicore_lockout state ainda corrompido)

## Hipóteses

### A) `multicore_launch_core1` mid-recovery hangs
Após `multicore_reset_core1` + delay 50ms + `multicore_launch_core1`, Core 1 começa do início. Mas o multicore primitive interno ainda tem estado de `multicore_lockout_start_*` em curso. Core 0 retorna mas em estado inconsistente.

### B) Core 1 crash durante TFT/touch init
TFT begin chama `delay(150)` várias vezes. Touch init faz attachInterrupt. Canvas alloc faz heap allocation 28KB+11KB. Algum desses pode crashar Core 1, mas IRQ handler de Core 0 não detecta crash, só timeout.

### C) Race entre Core 0 enterFlashSafeMode e Core 1 victim_init
Core 0 chama `pauseRendering(true)` na step 5 (~6800ms). Core 1 normalmente já chamou `victim_init` em step 3.5 (~2339ms). Mas se Core 1 ainda não chamou victim_init quando Core 0 tenta lockout, Core 1 não responde ao IRQ.

### D) tinyusb USB CDC não recuperou após lockout-stuck mid-render
`pauseRendering(true)` pausa Core 1 que pode estar mid-SPI burst pra TFT. Após restart Core 1, USB CDC TX state pode estar weird → silêncio serial.

## Investigações futuras necessárias

1. **Adicionar prints early no recovery path:** `multicore_reset_core1()` antes/depois, `multicore_launch_core1()` antes/depois. Ver onde exatamente trava.

2. **Test firmware isolado:** repete sequência alpha14 mas com TFT/touch DESABILITADOS — só Core 1 victim_init + idle. Se brick desaparece, problem é no TFT/touch init time/order.

3. **Reordenar setup():** chamar `_storageMgr->begin()` ANTES de `_displayMgr->startCore1()`. Sem Core 1 ativo, multicore_lockout vira no-op (`if (!_core1Ready) return`). Boot mais conservador, sem race.

4. **Disable lockout entirely para boot path:** Use cooperative quiet mode (`_displayMgr->requestQuietMode()`) que faz hard-reset Core 1 direto sem IRQ. Mas alpha12 tentou isso e introduziu instabilidade (talvez porque Core 1 ainda em init).

5. **Usar SDK `flash_safe_execute` direto** em vez de pauseRendering:
   ```cpp
   flash_safe_execute(do_mkdirs, NULL, 5000);  // 5s timeout
   ```
   SDK sabe lidar com Core 1 não-responsivo.

## Workaround atual

A) **Power cycle físico** (USB unplug + replug): mais confiável.
B) **Blink revival cycle múltiplo:** às vezes 2ª/3ª tentativa funciona.
C) **Boot chega a CLI mas com 11.7s atraso por lockout-stuck** (em caso "afortunado").

## Evidence em loop20

V3.43.21 (sem alpha14+ mudanças) tinha 76% pass rate em loop20. Os 24% bricks residuais provavelmente eram esse mesmo padrão (lockout-stuck → recovery falha).

Solução para v4.0.0 GA exige resolver isso. Opções:
1. Reordenar setup pra evitar race
2. Trocar lockout IRQ por flash_safe_execute SDK
3. Aceitar 24% como known limitation + adicionar HW watchdog que faz REAL HW reset (RUN pin via PicoHand) em case de boot timeout

---

🚧 Status: documentado mas SEM fix funcional ainda. Próxima sessão: tentar opção 1 (reordenar setup).
