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
  no RTC, lê sensores até estabilizar enquanto o Wi-Fi conecta em paralelo,
  **sempre grava** o histórico, e — se online — envia a telemetria pendente de
  forma não-bloqueante (janela = `cfg.telInterval`), depois dorme de novo).
- Hibernação = **SLEEP (deep sleep)**, não DORMANT: `sleep_goto_sleep_until()`
  (clk_sys→XOSC, `sleep_en0`=RTC, `__wfi`) + alarme do RTC. DORMANT (escrita
  "coma" no ROSC) foi descartado por ser não-determinístico na bancada (corre
  contra o sincronizador lento do ROSC/clk_rtc). O set do RTC usa
  `airRtcSetDatetime()` (segura o LOAD por 1 ms — o SDK perde o LOAD a
  46875 Hz); antes do WFI desabilita todas as IRQs exceto a do RTC (senão um
  IRQ pendente de USB/UART acorda imediatamente).
- Wake/hang: o watchdog era a causa do "wake de 2 s" (desarmado no início de
  `airEnterDormant()`, commit `966d5c9`); o boot M1 pós-wake travava por
  `sleep_en0` residual (commit `51d0eaf`) e por alarme de RTC velho
  (commit `a438a2a`). O FLUSH não pode usar `forceSync()` (bloqueia no HTTP);
  usa `_telemetryMgr->update()` não-bloqueante com janela = `cfg.telInterval`.
- ⚠️ Autópsia falsa: o registrador `WATCHDOG_REASON` é **somente-leitura** e
  retém o bit TIMER de qualquer disparo antigo do watchdog através de soft
  resets (só power-cycle limpa). Sem marcação, todo wake M1 vira um FATAL
  "HW WATCHDOG: Core 0 loop stalled" espúrio. Fix: `airEnterDormant()` chama
  `LogManager::instance().markCleanReboot()` (scratch[5]=0xC1EA8007) antes de
  dormir, e o banner de boot pula o aviso quando `_airActive` (M1).
- Comandos CLI: `air idle <sec>`, `air hibernate`, `air status`, `air stop`
  (cancelam/consultam a hibernação — funcionam na CLI de emergência).
  `air status` mostra `wake=` (max de histórico/backoff), `hist=`,
  `backoff=` e `idle=`.
- **Intervalo de wake = intervalo de salvamento do histórico** (o trabalho
  principal do wake). Se o backoff de telemetria (punição por falha de envio)
  for maior que esse intervalo, dorme pelo backoff — assim não acorda só para
  ser mandado esperar de novo. A janela de envio de telemetria é
  `cfg.telInterval` (configurado via web). `/config/air.bin` só guarda
  idle/stab/timeouts/pin (não toca em `CONFIG_VERSION`).
- `SIMUT_CLI_FULL=0` no Air: CLI completa + web + BT + mDNS **não cabem**
  juntos em flash (estourou ~35 KB). Mantido mDNS + BT + web; serial/BT ficam
  com CLI de emergência + comandos `air` + `ap`.
