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
  **sempre grava** o histórico, e — se **conectado** — drena a telemetria
  pendente de forma não-bloqueante até acabar / perder o servidor / cair o
  Wi-Fi, depois dorme de novo). "Online" = `isConnected()` (não
  `isTimeSynced()`, que é sempre true pelo relógio provisório do flash).
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
  usa `_telemetryMgr->update()` não-bloqueante + `refreshPendingCount()`
  até fila zerada / backoff / Wi-Fi cair (sem timeout de `telInterval`).
- ⚠️ CYW43: **não** chamar `cyw43_arch_deinit()` em `airEnterDormant()` —
  trava no 2º ciclo e deixa o chip num estado que só power-cycle recupera
  (mesma conclusão do OTA, "Fix #2 REVERTIDO"). O power-down é por hardware:
  `WiFi.disconnect(true)` + `WiFi.end()` + `GPIO23 (WL_REG_ON) LOW`. O
  boot seguinte faz o power-cycle do CYW43, então não precisa de teardown limpo.
- USB: antes de dormir, `airEnterDormant()` limpa o pull-up D+ 
  (`hw_clear_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_PULLUP_EN_BITS)`) para o
  host ver um disconnect limpo; sem isso o `clock_stop(clk_usb)` congela o
  pull-up e o cdc_acm empaca (ttyACM só volta com power-cycle do hub).
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

## SIMUT Air — revisão de 06/09/2026: plano, suíte e armadilhas

**Leia antes de mexer no Air.** A revisão de código encontrou 21 achados
(F01–F21), 3 bloqueantes; nada foi corrigido ainda. Fontes de verdade:

- Plano de correção e otimização, com fases, aceite e decisões pendentes:
  `docs/analysis/SIMUT_AIR_PLANO_FIX.md`.
- Suíte de bancada (CLI serial + web + PicoHand + coletor de telemetria):
  `tools/air_test_suite.py` — `--list`, `--selftest` (sem hardware),
  `--only T05 --cycles 3`, `--long` (T08), `--baseline`, `--report x.json`,
  `--flash fw.uf2` (picotool com recuperação pela mão). Exige
  `SIMUT_WEB_USER`/`SIMUT_WEB_PASS` para os testes web. Testes marcados
  `xfail` documentam bugs conhecidos: XFAIL é o esperado, XPASS = bug fechou,
  tirar a marca.
- Portão estático (roda em segundos, sem PlatformIO):
  `python3 tools/check_air_consistency.py` — C1–C8 cobrem F15–F19. **Hoje
  falha de propósito** (help/packs/guards); vira verde na Fase 3 e depois
  entra no CI ao lado de `check_authz`/`check_fsguard`.

**MEDIDO na bancada em 06/09 ~16h, binário `461a806` com procedência provada**
(detalhe, tabelas e a saída bruta em `SIMUT_AIR_PLANO_FIX.md` §6.1):

- **F22 — o período de sono está errado, e é REGRESSÃO.** Com `hist=120 s` o ciclo real ficou
  entre **16 e 48 min**. Mas as âncoras de bloco do histórico (o `t0` de cada chunk DATA, que é
  absoluto) mostram que **de manhã o ciclo estava certo**: 147–151 s às 11h16–11h33 com
  `h_int=2`, e 87–88 s às 10h40 com `h_int=1`. A degradação começa perto das **12h15**, a mesma
  janela em que a build nova foi gravada. **Suspeito único: o ROSC desligado antes do WFI.**
- **F01 era real, só falhava diferente do previsto.** Não trava de vez: faz o wake demorar um
  tempo longo e variável. ✅ **CORRIGIDO E VALIDADO em 06/09**: religar o ROSC logo após o
  `wfi`, antes do SYSRESETREQ, esperando `ROSC_STATUS_STABLE` (+32 B). Medição pós-fix, dois
  ciclos seguidos por `--watch`: **110,8 s e 120,8 s dormindo, 26,5 s e 26,3 s acordado** com
  alarme de 120 s — de volta aos 147 s de ciclo que a bancada tinha de manhã.
- ✅ **F02 e F03 corrigidos na mesma rodada.** FLUSH ganhou saída imediata com
  `telInterval == 0`, gate por `isNetworkHealthy()` e **teto de parede** (`flushTimeoutMs`);
  a energia dos sensores passa a ser ligada no **início do `setup()`**, então vale em M0 e no
  boot M1 (e o boot inteiro vira warm-up, o que torna o F06 sem objeto).
- ✅ **F23 resolvido de carona.** Medido depois do fix do ROSC, olhando só os registros
  posteriores a ele: **12 registros, 0 gaps negativos** (contra 12 negativos no dia inteiro). Os
  retrocessos vinham de blocos cujo interior era reconstruído pelo passo nominal enquanto as
  amostras reais estavam muito mais espaçadas; com o wake no intervalo, bate de novo.
- ✅ **Fase 3 (consistência) feita, e se pagou em flash**: `check_air_consistency.py` C1–C8
  limpo, `check_cli_help.py` OK nos 4 envs. **release −712 B, alpha −272 B** — eram os dez
  marcadores de boot, o texto de ajuda dos comandos `air` e o bloco do parser que os reconhecia
  sem ter handler. ⚠️ Resíduo consciente: um `.lng` é compartilhado, então os comandos `air`
  ficam fora do `@HELP` dos packs (confirmado no ferro: `help` de um Air pt-BR não os lista).
- ✅ **Intervalo real corrigido**: o alarme passou a ser `h_int − tempo já acordado neste wake`
  (um wake do M1 **é** um boot, então esse tempo é o `millis( )` na hora de dormir). Medido:
  sono de **91,8 s nos três ciclos** e período de **118,6 s** contra 120 s configurados — antes
  era 147 s. Só compensa quando o boot foi mesmo um wake (`_airWokeFromSleep`, separado do
  `_airActive`); o backoff **não** é compensado; piso de `AIR_MIN_SLEEP_SEC` com sufixo
  `OVERRUN` no log quando o wake não cabe no intervalo. Série do dia, mesma config de 120 s:
  16–48 min → 147 s → **118,6 s**.
- 🔌 **Fiação real da bancada (confirmada pelo Ângelo em 06/09):** **DS18B20 no GP0, sem
  chaveamento**; **GP16 vai para a PicoHand como sonda** de acordado/dormindo. Ou seja, o
  caminho de power-gating do F03 **não é exercitado aqui** — o que a linha faz nesta bancada é
  medir. Como o firmware agora a levanta no início do `setup()`, ela cobre a janela acordada
  inteira, o que melhora a sonda. A extensão `PROBE` da mão (§3 do plano) já tem fiação; falta
  o firmware da mão, e gravá-lo exige `SELF_BOOTSEL`, proibido em automação.
- **F23 — RETRATAÇÃO PARCIAL.** A primeira medição decodificou o `.h5` com o nominal **errado**
  (60 s default do `history_v5.read_series` × 120 s do aparelho). O V5 guarda desvios do passo
  nominal, então o nominal errado **reescreve todos os tempos interiores** e fabrica "rajadas"
  e gaps negativos que não estão nos dados. Com o nominal certo: 175 registros, 83 no intervalo,
  **12 gaps negativos** (o arquivo ainda não é monotônico — isso é o F23 de verdade, e é bem
  menor do que eu havia reportado). **Sempre passar o nominal do aparelho** (`h5_epochs` da
  suíte agora exige o parâmetro) e, para datar wakes, usar `h5_block_anchors`, que é imune.
- **F13 confirmado**: todo wake M1 loga `WEB_SERVER_STARTED ctx=80` e `APP_CACHE_PRELOAD_DONE`.
- **Reset físico entrega M0** (medido duas vezes: `air status` = `phase=0` após o pulso de RUN).
- **Ausência prolongada do USB não é falta de energia**: o alvo ficou horas sumido, alimentado,
  e voltou em 3 s com `hand RESET`.

**⚠️ Três instrumentos que mentiram nesta bancada — não repetir:**

1. **O `help` NÃO discrimina firmware.** Com pack não-inglês instalado, o console serve o
   `@HELP` do `.lng` do LittleFS (v2.3.7-beta), que sobrevive à gravação. O alvo não lista
   `system ssid` nem os comandos `air` no help e mesmo assim roda o `461a806`, com os comandos
   funcionando. **Discriminador certo, não-destrutivo: mandar `system ssid` SEM argumento** —
   `461a806` responde `SSID invalido (1-31 chars, sem ctrl chars)` (rejeita antes de gravar),
   binários anteriores respondem "Comando desconhecido".
2. **`/api/status` traz `uptime` em MILISSEGUNDOS** — ler como segundos dá 23,5 h num aparelho
   com 84 s de vida.
3. **O ModemManager está ativo nesta máquina** e sonda todo `ttyACM` recém-enumerado: a primeira
   sessão serial após um reset pode morrer com *"device reports readiness to read but returned
   no data"* sem culpa do alvo. Reabrir; se incomodar, udev com `ID_MM_DEVICE_IGNORE`.

Armadilhas de bancada específicas do Air:

- **⚠️ F01 primeiro.** O commit `461a806` desliga o ROSC antes do WFI e o
  SYSRESETREQ do wake não passa pelos blocos de clock (o próprio projeto viu
  `sleep_en0` e o alarme do RTC sobreviverem ao reset). Provar com 1 ciclo
  antes de qualquer outra coisa; a correção é religar o ROSC logo após o `wfi`.
- **⚠️ `hand RESET` NÃO serve de prova do F01.** Ele aciona o pino RUN, que é
  reset global do chip: restaura o ROSC e os defaults de clock, então recupera
  o alvo *mesmo que* o F01 seja real. **A única prova do caminho do sono é o
  alvo reenumerar sozinho** dentro de `wakeSec` + margem. Se nem o RESET
  trouxer o alvo de volta, ele está sem energia/desconectado, não travado.
- **Alvo ausente do USB ≠ morto**: em M1 ele solta o pull-up por desenho.
  Esperar um intervalo de histórico antes de diagnosticar.
- **`hand RESET` dá boot FRIO (M0), não M1** — reset físico zera os scratch
  registers, incluindo o marcador de hibernação em `scratch[0]` (o mapa em
  `src/LogManager.cpp:605` diz isso explicitamente: "zeroed on power cycle /
  physical reset"). ⚠️ Uma versão anterior desta nota afirmava o contrário;
  a suíte agora **mede** isso em T02 (lê `air status` antes de qualquer
  `air stop`) em vez de assumir. Confirmar na próxima bancada.
- **A bancada esconde F05**: com `t_int=100 ms` o dreno parece rápido; com o
  default de 60 s cada lote espera um intervalo inteiro. Testar com
  `t_int=60000` (T06).
- **Telemetria desligada + Wi-Fi de pé = acordado para sempre** (F02). Se o
  aparelho "não dorme", conferir `t_int` antes de procurar outra causa;
  `air stop` pela serial tira dele.
- **GP16** é a linha de energia dos sensores (alto acordado). Hoje só liga no
  WARMUP do M1 (F03): sensores chaveados pelo GP16 não leem em M0.
- **`air.bin` antigo mantém `sensorPowerPin=255`** (default mudou sem bump de
  versão, F14). Sem comando para trocar o pino: apagar `/config/air.bin` ou
  esperar o `air pin` da Fase 2.
- **A PicoHand não tem canal de sonda**: `VERIFY` só vê RESET/BOOTSEL. A
  suíte mede acordado/dormindo pelos carimbos de enumeração USB; a extensão
  `PROBE` (GP2) está especificada na §3 do plano.
- **"Verified on bench" no commit vale para o commit, não para o seguinte**:
  o ROSC-off entrou no último commit, sem registro de validação. Conferir
  a data do `AGENTS.md`/log de bancada contra a do commit antes de confiar.
- **CI não cobre o Air** (`build.yml` roda só em `main`, sem `pico_w_air` nem
  `native_air`). Rodar local antes de cada push: `pio run -e pico_w_air`,
  `pio test -e native_air`, `python3 tools/check_air_consistency.py`, e
  `pio run -e pico_w_release -e pico_w_alpha` (o Air toca arquivos comuns).
- **Para medir o período, use `--watch`, não a CLI.** Todo comando de CLI reseta o timer de
  inatividade do M0 e abrir a porta na hora errada perturba a janela medida.
  `python3 tools/air_test_suite.py --watch 2700` só observa a enumeração USB.
- **⚠️ O instrumento mente antes do firmware.** A primeira rodada da suíte no ferro acusou
  "serial vanished before the alarm line" — era a própria suíte engolindo a linha num
  `cmd()` de 2 s, porque o aparelho vai de `air hibernate` a dormindo em menos de 1 s quando
  os sensores já estão estáveis. Antes de culpar o firmware, provar o instrumento
  ([[validate-the-instrument]] da memória do projeto).
