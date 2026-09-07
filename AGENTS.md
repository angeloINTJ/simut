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
- ✅ **F24 — o cursor de telemetria não avançava e o mesmo pacote era reenviado a cada wake.**
  Causa: `flushCursorIfDirty( )` adia a escrita por 5 s (`CURSOR_COALESCE_MS`) para poupar a
  flash, e no ciclo M1 a fase FLUSH sai **~150 ms** depois do envio — a janela nunca decorre, o
  sono perde a SRAM e o boot relê o cursor velho. ⚠️ **Padrão a procurar em qualquer coisa nova
  no caminho do sono:** todo mecanismo que "adia para depois" está errado no Air, porque não
  existe depois. Fix: `flushCursorIfDirty(bool force)`, com `true` nos três caminhos para o sono
  (inclusive `airEnterDormant( )`, o ponto único). Medido: `pending` era 8 e crescia 1 por
  ciclo; virou 1→2 em três ciclos. `flushWipV5( )` foi conferido e **não** tem esse portão.
- ⚠️ **E havia uma SEGUNDA causa para o mesmo sintoma, do lado do servidor.** O coletor
  `192.168.3.206:8080/telemetry` **aceita a conexão e nunca responde** (`curl` daqui: conecta em
  3,5 ms, 20 s sem um byte). O aparelho registra `code=31 ctx=-11` (read timeout) + `code=32`
  (retry). **O firmware está certo**: envio sem resposta não é envio confirmado, então o cursor
  não deve avançar. Prova cruzada, só trocando o destino: com um coletor que responde 200,
  `pending` foi de **9 para 1 em 3 ciclos**. Antes de culpar o cursor, medir o endpoint por fora.
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
- ✅ **O aparelho MEDE o próprio sono.** Depois do `wfi` ele lê o RTC (o único relógio que
  atravessa o sono), desconta a base, guarda os segundos em `scratch[1]` e o boot seguinte imprime
  `[AIR] woke: slept=<n>s`. Foi assim que apareceu o truncamento do `wakeSec = sleepMs / 1000`.
- 🧭 **O `load` do RTC já vale um tique — e o autorrelato mentiu por causa disso.** O firmware
  imprime `[AIR] rtc after set: … 00:00:01 base=1s`: escreve-se 00:00:00 e 3 ms depois lê-se 1.
  Um alarme armado em `wakeSec` ficava a `wakeSec − 1` tiques, e o aparelho acordava **1 s cedo
  todo ciclo** enquanto reportava `slept=` igual ao alarme. Quem pegou foi a **sonda**, que é
  passiva. Hoje o alarme é `baseSec + wakeSec`. Série do dia para 120 s: 16–48 min → 147 s →
  118,9 s → 119,3 s → **120,23 s** (resíduo +0,11 s, explicado inteiro pelo trabalho entre o
  `millis( )` e o `load`). ⚠️ **Regra:** o aparelho medindo a si mesmo é um instrumento como outro
  qualquer — confira com um externo e passivo antes de crer.
- ✅ **DUAS CADÊNCIAS, UM ALARME.** O aparelho acorda sempre no intervalo do histórico; a
  telemetria é expressa em **wakes inteiros** dessa cadência (`airTelemetryDue( )`, arredondando
  para cima). É isso que faz o envio **sempre coincidir** com uma medição — o caro não é
  transmitir, é estar acordado, e um wake que já vai acontecer sai de graça. **Não existe segundo
  alarme:** o RTC do RP2040 tem um só, e a coincidência exigida dispensa o outro.
  - **Conta wakes, não relógio.** Num wake sem rádio não há NTP, então o relógio é provisório;
    uma regra escrita contra epochs mediria justamente o que não pode confiar.
  - **O contador mora no `scratch[1]`** junto com os segundos dormidos (bits 23..17 = wakes,
    16..0 = segundos). Em flash custaria uma escrita por minuto. Perdê-lo custa **uma** telemetria
    atrasada, e só em power cycle — reset de watchdog preserva.
  - **Um wake de telemetria zera o contador mesmo se o envio falhar.** A punição por um coletor
    mudo é esperar um intervalo inteiro, não tentar de novo no wake seguinte com o rádio ligado.
- 🔴 **O LED NÃO é o indicador de acordado — o GP16 é.** No Pico W, `LED_BUILTIN` é `PIN_LED = 64`,
  um GPIO **do CYW43**: um `digitalWrite` nele sobe o rádio e gasta a economia inteira do wake sem
  rádio. `airSetLed( )` só age quando `_airRadioUp`. Quem mostra acordado/dormindo é o
  `AIR_SENSOR_POWER_PIN` (GP16), alto a janela acordada toda, baixo o sono todo — é nele que a
  sonda da PicoHand cronometra o ciclo.
- 🔴 **QUEM causou o boot decide quanto tempo o M0 dura.** Boot **limpo** (power cycle, RUN,
  `reload`, OTA) = tem gente ali querendo entrar, provavelmente pelo navegador → vale o `air idle`
  inteiro. Boot **sujo** (watchdog) = não tem ninguém → graça curta do F25, para voltar a dormir
  antes de a falha repetir. ⚠️ **A primeira versão do F25 dava 10 s para TODOS os boots** e deixou
  o aparelho inutilizável pela web: o operador não conseguia terminar o login.
- 🔴 **Toda resposta web rearma o timer de inatividade** (`WebManager::setActivityCallback` →
  `airMarkActivity( )`, chamado no funil `safeSendN`/`safeSend_GZ`). A CLI serial fazia isso desde
  sempre; a web não fazia, e por isso uma sessão de navegador era hibernada por baixo de quem
  estava usando — inclusive no meio do login. Era o F21 do plano.
- ✅ **`air stop` sobe o rádio se ele estiver desligado.** Parar o ciclo num wake sem rádio
  deixaria o M0 sem web, sem NTP e sem LED, alcançável só pelo cabo serial por onde o comando
  chegou.
- ✅ **O relógio provisório passou a ser semeado com o sono MEDIDO** (`setProvisionalTime(lastTs,
  slept + millis()/1000)`), não com o palpite fixo de 60 s. Vira obrigatório quando o rádio sobe
  uma vez a cada N wakes: os registros do meio nunca veem NTP, então o que esse relógio disser é o
  que o histórico guarda.
- ✅ **SSID ausente não alarga o wake.** `AIR_MAX_CONNECT_ATTEMPTS` (2) limita o que um wake gasta
  atrás de rede; passado o teto, SAMPLE para de bombear o `NetworkManager` e o DECIDE trata como
  offline. Medido com SSID errado: 26,7 / 26,4 / 26,2 s acordado, contra 26,3–29,5 s com o SSID
  certo. ⚠️ O teto é **teto**, não caso comum: uma tentativa custa até 20 s e o wake dura ~28 s,
  então só **uma** começa por wake e o limite não chega a disparar.
- ✅ **F25 — o ciclo volta sozinho depois de um reset.** O marcador do scratch é zerado em TODO
  boot de propósito (para um aparelho que trava em M1 não ficar inalcançável), então quem carrega
  a intenção é o **`air.bin`**: `flags` bit 0 = ciclo armado. Boot que não é wake + ciclo armado →
  o timer de inatividade vale `AIR_RESUME_GRACE_SEC` (10 s) em vez do `air idle`. A
  alcançabilidade é preservada por um **contador de boots sujos** (`flags` bits 4..7,
  `LogManager::bootWasClean( )`): a partir de 3, a graça volta a ser o `air idle` inteiro e o log
  registra `APP_AIR_CYCLE_HELD` (411). `air status` mostra `armed=` e `dirty=`.
  ⚠️ **Para medir isso, não fale com o aparelho:** todo comando chama `airMarkActivity( )` e
  rearma o timer, então perguntar se ele voltou a dormir é o que o impede de dormir. Use a
  enumeração USB ou a sonda (é o que o T12 faz). O sintoma original: com o SSID inexistente o
  Core 0 travava a cada 54–107 s, antes dos 300 s, e o aparelho ficava **acordado para sempre**;
  autópsia em `show system log`, `ctx=455` (= 200 + 0xFF, watchdog sem canal de rastreio) e
  `ctx=209`.
- ⚠️ **Gravar o alvo: use o toque de 1200 bps**, não `picotool -f`. Com dois RP2040 no barramento
  o picotool pega o primeiro que acha — a PicoHand, que não tem interface de reset ("Unable to
  locate reset interface") — e `--ser` não salva, porque em BOOTSEL a placa enumera com outro
  serial e o filtro não casa mais. `tools/air_test_suite.py --flash` já faz o toque primeiro e
  só cai para a mão se ele falhar.
- ⚠️ **O monitor serial do Arduino IDE rouba a porta da mão** (`.arduino15/.../serial-monitor`),
  e com isso somem a sonda e o caminho de recuperação por BOOTSEL. `fuser -v /dev/ttyACM*`
  mostra quem segura.
- ✅ **Intervalo real corrigido**: o alarme passou a ser `h_int − tempo já acordado neste wake`
  (um wake do M1 **é** um boot, então esse tempo é o `millis( )` na hora de dormir). Medido:
  sono de **91,8 s nos três ciclos** e período de **118,6 s** contra 120 s configurados — antes
  era 147 s. Só compensa quando o boot foi mesmo um wake (`_airWokeFromSleep`, separado do
  `_airActive`); o backoff **não** é compensado; piso de `AIR_MIN_SLEEP_SEC` com sufixo
  `OVERRUN` no log quando o wake não cabe no intervalo. Série do dia, mesma config de 120 s:
  16–48 min → 147 s → **118,6 s**.
- 🔌 **Fiação real da bancada (confirmada pelo Ângelo em 06/09):** **DS18B20 no GP0, sem
  chaveamento**; **GP16 do alvo → GP2 da PicoHand** como sonda de acordado/dormindo. Ou seja, o
  caminho de power-gating do F03 **não é exercitado aqui** — o que a linha faz nesta bancada é
  medir. Como o firmware agora a levanta no início do `setup()`, ela cobre a janela acordada
  inteira, o que melhora a sonda.
- ✅ **Canal `PROBE` da mão IMPLEMENTADO e validado** (`PROBE STATUS|START|READ`, entrada GP2,
  anel de 64 bordas no laço de 10 kHz que o Core 1 já roda). É o cronômetro a usar: a
  enumeração USB atrasa ~1 s em relação ao boot e a serial reseta o timer de inatividade do
  alvo. Medido: sono 120,715 s / acordado 29,455 s / sono 89,413 s → ciclo **118,868 s** para
  120 s. Detalhes e receita de regravação no manual da PicoHand §11. ⚠️ `micros()` dá a volta
  em ~71 min (ler diferenças); **regravar a mão reinicia o alvo**; BOOTSEL da mão exige
  `SELF_BOOTSEL` ou botão — não é automatizável.
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
- **A bancada esconde F05** — ✅ corrigido e medido 07/09: com `t_int=100 ms` o dreno parecia
  rápido; com `t_int` ≥ o teto do FLUSH o wake **não mandava nada** (0 registros em 57 s
  acordado, sonda GP16). Hoje o FLUSH liga o "modo dreno" (`setDrainMode`) e o mesmo wake
  entregou 18.800. Continuar testando com `t_int=60000` (T06), não com 1 ms.
- **O log do servidor de um wake tem DOIS trechos.** `air hibernate` de M0 roda um ciclo no lugar
  (FLUSH até o teto, dorme) e só depois vem o wake que a sonda cronometra. Contar "registros do
  wake" pelo total do servidor soma os dois; cortar pelo tempo (trechos separados pelo sono) é o
  que `phase_cadence.py` faz agora.
- **Contadores acumulados do servidor não são a janela.** A primeira matriz de cadência (07/09)
  leu "22–35 % de reenvio" e taxas 1,5× maiores porque o servidor contava desde antes do reboot
  do `commit_all` (o aparelho retoma o dreno do cursor persistido enquanto o harness faz login) e
  o `tel_reset` recomeça do mais antigo. A janela sai do **log por request** por relógio de parede
  (`window( )`), e o que veio antes fica em `pre_window_records`.
- **Reuso de sessão TLS é estado da INSTÂNCIA do `HTTPClient`** (`_canReuse` nasce falso): um
  objeto local nunca reaproveita o socket, mesmo com o `stop( )` condicionado. O experimento
  `TEL_TLS_KEEPALIVE_EXPERIMENT` mantém a instância (`_httpKeepPtr`) e mede 5,2×/3,4× no HTTPS —
  só quando o servidor também mantém a conexão (controle = base).
- **Reboot silencioso ≠ watchdog.** Um boot sem `SYS_BOOT` nenhum (nem `[FTL]`, nem INFO) só sai
  de reboot marcado limpo ou de reset físico/power-on; o watchdog deixa `[FTL] ctx=2xx` e o
  picotool deixa INFO. Dois assim em 07/09, os dois colados numa falha de telemetria (célula de
  6 s; 1º wake do modo dreno) — sem serial acampada não há como fechar. `serial_probe.py --delay`
  é o instrumento.
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
