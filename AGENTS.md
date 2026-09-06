# AGENTS.md — Notas operacionais da bancada

Instruções persistentes para agentes que trabalham neste repositório.
Leia antes de gravar firmware no hardware ou mexer na bancada.

## Gravação de firmware no Pico W (alvo SIMUT)

**Nunca peça ao usuário para resetar o Pico manualmente.** A bancada tem uma
**PicoHand** — um Pico comum que aciona as linhas RESET (GP0) e BOOTSEL (GP1) do
alvo via serial. É o caminho automático para forçar o alvo de volta ao BOOTSEL
quando ele trava a ponto de não aceitar flash por `picotool` (toque de 1200 bps).

**Identificação das portas (use o serial, não a ordem de enumeração):**

| Placa | USB | Serial (udev) |
|---|---|---|
| PicoHand (mão) | `2e8a:000a` | `ID_SERIAL_SHORT=E660C062131E3E27` |
| Alvo SIMUT (Pico W) | `2e8a:f00a` | `ID_SERIAL_SHORT=E6642815E34C1824` |

**Receita de flash com recuperação automática:**

```bash
source tools/PicoHand/pico_hand.sh
hand_init                                  # detecta a mão (PING/PONG)
trap hand_release_all EXIT

# Caminho normal (alvo ainda atende USB):
picotool load -x <firmware.uf2> || {
    # Recuperação: força BOOTSEL pela mão, espera o RPI-RP2 enumerar, grava:
    hand BOOTSEL
    sleep 2
    picotool load -x <firmware.uf2>
}
hand_release_all
```

Referência completa (comandos, armadilhas, analisador lógico):
`tools/PicoHand/MANUAL_CLAUDE_CODE.pt-BR.md`.

## SIMUT Air — build headless com hibernação

- Build: `pio run -e pico_w_air` (Flash ~97%, RAM ~45%).
- Ciclo: cold boot = **M0** (Alpha headless: web + serial + BT + sensores);
  `air hibernate` ou 5 min de inatividade → **M1** (deep sleep via WFI, acorda
  no RTC, lê sensores até estabilizar, checa Wi-Fi, grava/envia telemetria,
  dorme de novo).
- Hibernação = **SLEEP (deep sleep)**, não DORMANT: `sleep_goto_sleep_until()`
  (clk_sys→XOSC, `sleep_en0`=RTC, `__wfi`) + alarme do RTC. DORMANT (escrita
  "coma" no ROSC) foi descartado por ser não-determinístico na bancada (corre
  contra o sincronizador lento do ROSC/clk_rtc). O set do RTC usa
  `airRtcSetDatetime()` (segura o LOAD por 1 ms — o SDK perde o LOAD a
  46875 Hz); antes do WFI desabilita todas as IRQs exceto a do RTC (senão um
  IRQ pendente de USB/UART acorda imediatamente).
- ⚠️ Em aberto: o wake (RTC) já funciona — o watchdog era a causa do "wake de
  2 s" e foi desarmado no início de `airEnterDormant()` (commit `966d5c9`).
  Resta: o **ciclo M1 trava após o wake** (2º ciclo, com o CYW43 recém ciclado
  no boot M1); local exato não confirmado — suspeita em `_netMgr->update()` /
  `_telemetryMgr->forceSync()` (bloqueantes sem timeout real) ou no scan WiFi.
  Localizar com o log de fase `[AIR] phase=...` (já no firmware).
- Comandos CLI: `air idle <sec>`, `air hibernate`, `air status`, `air stop`
  (cancelam/consultam a hibernação — funcionam na CLI de emergência).
- **Intervalo de wake = intervalo de telemetria** (`cfg.telInterval`, em ms,
  configurado via web). Sem botão separado — `/config/air.bin` só guarda
  idle/stab/timeouts/pin (não toca em `CONFIG_VERSION`).
- `SIMUT_CLI_FULL=0` no Air: CLI completa + web + BT + mDNS **não cabem**
  juntos em flash (estourou ~35 KB). Mantido mDNS + BT + web; serial/BT ficam
  com CLI de emergência + comandos `air` + `ap`.
