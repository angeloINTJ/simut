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
- Comandos CLI: `air idle <10..65535>`, `air charger <0..22|26..28|off>`, `air hibernate`, `air status`,
  `air stop`. ⚠️ O teto do `air idle` é o campo, não a frase: até 06/09 aceitava 86400 e convertia,
  e **65536 virava 0** — ocioso zero manda dormir na passada seguinte do laço, e só se volta
  pegando uma janela de wake pelo console (F09, fechado em 07/09).
  ⚠️ **23/24/25/29 são PROIBIDOS como `air charger` ou `sensorPowerPin`**: são o barramento do
  CYW43 no Pico W (WL_ON, dado SPI compartilhado, CS/LED, ADC3-VSYS). A regra é `airPinValid( )`,
  usada tanto pelo handler quanto pelo `airSanitise( )` — então um `air.bin` forjado ou restaurado
  com 25 volta ao default no load. O handler também recusa um GP já usado pela alimentação de
  sensor ou por um sensor ativo (V-06).
  (cancelam/consultam a hibernação — funcionam na CLI de emergência).
  `air status` mostra `wake=` (max de histórico/backoff), `hist=`,
  `backoff=` e `idle=`.
- **Intervalo de wake = intervalo de salvamento do histórico** (o trabalho
  principal do wake). Se o backoff de telemetria (punição por falha de envio)
  for maior que esse intervalo, dorme pelo backoff — assim não acorda só para
  ser mandado esperar de novo. **Quem decide se o wake liga o rádio é a
  quantidade pendente** contra `cfg.telInterval`, que desde a config v22 é o
  lote mínimo em registros (0 = telemetria desligada). `/config/air.bin` só
  guarda idle/stab/timeouts/pin (não toca em `CONFIG_VERSION`).
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
- 🔴 **Só uma requisição AUTENTICADA rearma o timer de inatividade**
  (`WebManager::setActivityCallback` → `airMarkActivity( )`). O rearme mora em três lugares, todos
  depois de identificar quem chamou: `getAuthPerms( )` no ramo em que o cookie casou com uma sessão
  viva, `completeLogin( )`, e o ramo de sucesso do Basic auth do `/metrics`. **Pré-login tem
  ORÇAMENTO**: `ensureLoginStateSlot( )` gasta no máximo `WEB_PREAUTH_MAX_EXT` (3) extensões por
  boot, zeradas por um login bem-sucedido — com `air idle` de 300 s dá até 15 min para terminar o
  login, e dá os mesmos 15 min, UMA vez, para quem só está batendo na porta.
  ⚠️ **Os funis `safeSendN`/`safeSend_GZ` NÃO rearmam mais.** Servir bytes não prova que tem gente:
  o funil respondia "tem alguém aí" até para um 403 devolvido a um estranho, e qualquer poll anônimo
  segurava o aparelho acordado para sempre (V-03; medido em 06/09: 491 s contra 306 s esperados).
  O F21 continua fechado — o que mudou é o critério, de "respondi" para "sei quem é".
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
- 🔴 **O CLI Bluetooth está COMPILADO nas imagens alpha e Air** (`SIMUT_BLUETOOTH=1` nas duas), e
  autentica com a **senha do admin da web** — não com o PIN do display. Desde 07/09 tem lockout
  exponencial (`authLockoutMs( )`, o mesmo da web: 2 s na 1ª falha, teto de 300 s), e o estado mora
  na RAM do `BluetoothManager`, então **derrubar e reconectar o RFCOMM não zera nada** — é esse o
  laço que o atacante usaria. Os quatro comandos de recuperação (`system factory`, `system format`,
  `admin reset`, `system https off`) são **só pela USB**; `ap` continua valendo por BT de propósito
  (é o caso de uso documentado). ⚠️ A origem viaja no `CliDemand.fromBt`, não numa flag "último
  input", porque uma linha do BT pode ficar na fila da CLI e executar vários inputs depois (V-01a).
- 🔴 **Um wake M1 NÃO sobe web, Bluetooth, mDNS nem o cache do painel** — só o que ele precisa
  para ler o sensor e, se for o caso, enviar. Tudo isso pertence ao M0 (boot a frio ou
  `air stop`, que continua subindo a web sozinho). O portão é `_airActive`, não `_airRadioWake`:
  nem o wake de telemetria sobe listener, porque enviar não precisa de ninguém escutando. T10 da
  suíte mede isso (porta 80 fechada durante o wake). ⚠️ **Consequência para OTA:** um Air
  dormente não tem `:8080` — para dar OTA nele, mantenha-o em M0 (carregador no GP17, que a
  PicoHand segura por GP3→GP17, ou logo após um boot a frio, antes de ele hibernar).
- 🔋 **Carregador no GP17 (`AIR_CHARGER_PIN`, configurável em `air.bin`).** Nível alto por um
  divisor do trilho de 5 V = carregando. Enquanto carrega: o `air idle` não se aplica (fica
  acordado) e um wake que encontra o carregador **cancela o M1 daquele boot** e sobe como M0
  completo. O ciclo continua ARMADO, então desconectar + expirar o idle volta a dormir sozinho.
  `air status` mostra `chg=`. ⚠️ O pino ocupou o `wifiScanTimeoutMs`, que era campo morto (F17) —
  mesmo tamanho de arquivo, mesmo CRC, nada se perde; nenhum código jamais escreveu esse campo,
  então todo `air.bin` que existe carrega os 4000 ms do default, cujo byte baixo cai aqui como
  **160** — não é GPIO e vira o default via `airSanitise( )`. Preso por teste nativo
  (`test_charger_pin_from_legacy_field`), que é o único portão: a versão do arquivo NÃO subiu.
  **Medido em 07/09** (mesma firmware, mesma janela, sem rede): com `chg=1` ficou acordado 130 s
  sob `idle=40`; com `chg=0` dormiu em 40,5 s.
- 🔌 **Na bancada quem finge o carregador é a mão**: `CHARGER ON|OFF` no PicoHand, saída GP3 ligada
  direto no GP17 do alvo (3,3 V, **sem divisor** — o divisor é da placa real, para os 5 V). É a
  única linha que a mão aciona em nível alto; as outras emulam botão em dreno aberto. T14 mede as
  duas metades: acordado durante a carga e dormindo depois de tirar. Detalhes no
  `tools/PicoHand/MANUAL_CLAUDE_CODE.pt-BR.md` §12.
- 💾 **Desgaste de flash: o `.wip` é reescrito INTEIRO a cada gravação, e é o maior custo do
  ciclo.** Desde 07/09 `flushWipV5( )` pula quando os bytes em flash já são idênticos (flag sujo
  **e** flag de relógio inalterado — a procedência pode mudar sem registro novo). Medido: 3–4
  gravações do bloco inteiro por ciclo M0→M1, agora 1; **T15 é o portão** (teto 2, não 1). O contador está em `air status` (`wip=`) e na linha
  `[AIR] alarm:`. ⚠️ **A segunda gravação é legítima e apareceu com o F23**: o bloco em flash
  carrega a procedência da sessão que o escreveu, e um wake carimba o registro ANTES de chegar
  ao NTP — quando o relógio é confirmado, o flush seguinte reescreve o flag de provisório para
  sincronizado, que é o que o portão de semente do próximo boot lê. Só acontece nos wakes que
  alcançam o NTP (minoria, com lote mínimo > 1). Medido: 3–4 por ciclo antes de tudo, 1 com os
  chamadores redundantes removidos, 2 depois que o boot passou a retomar. **3 ou mais é defeito.** ⚠️ **Ler `wip=` por `air status` de dentro de um
  wake devolve 0**: o console responde antes do DECIDE. Use a linha de alarme.
- ✅ **O boot RETOMA o bloco aberto (F23, corrigido 07/09).** Antes ele adotava o `.wip` anexando-o
  ao arquivo do dia e apagando-o; como todo wake é um boot, cada leitura virava um bloco próprio
  (**1,41 reg/bloco, 16,0 B/registro** contra os 5,38 do projeto). Agora `h5ResumeOpenBlock( )`
  reinjeta o snapshot no encoder, e o `.wip` **não é apagado** — ele segue sendo a cópia em flash do
  bloco aberto. Medido numa janela de 25 min sem interferência: **um bloco de 27 registros por 52 B** contra
  blocos de UM registro minutos antes — **16,0 → 1,9 B/registro**. Sela só quando o bloco enche,
  o dia vira, ou uma correção de relógio chega.
  ⚠️ **Não meça isso pelo delta do arquivo do dia**: durante a janela o bloco está ABERTO no
  `.wip`, então o arquivo não cresce e o delta dá **+0** — indistinguível de "parou de gravar".
  Olhe os blocos SELADOS (`h5_block_anchors`) e o `tel=` de pendentes.
  ⚠️ **Reinjetar é sem perda porque o formato guarda a ÉPOCA REAL de cada registro** (delta-de-delta,
  escape de 32 bits); o nominal é só o preditor. Documentação antiga que diz "o interior é
  reconstruído pelo passo nominal" está errada.
  ⚠️ **Um bloco retomado carrega carimbos da sessão anterior.** O `shiftHistoryTimeV5` sela antes de
  corrigir e marca `_h5AdoptedT0` — deslocamento parcial foi recusado de propósito, porque um delta
  negativo reordenaria o bloco.
- ⚠️ **Nenhuma medida do `air idle` vale com uma aba do painel aberta.** Cada acerto na web chama
  `airMarkActivity( )` (é o fix do F21, funcionando), então o aparelho fica acordado para sempre e
  as DUAS metades do teste do carregador dão "acordado" — o A/B não discrimina nada. Em 07/09 isso
  se disfarçou de bug do firmware por meia hora. Diagnóstico em um comando:
  `ss -tn | grep 192.168.3.24`. A janela limpa é tirar o aparelho da rede pelo próprio console
  (`system ssid <ssid>_offline` + `reload confirm`, e restaurar depois) — nunca fechar o navegador
  do Ângelo.
- 🔴 **`t_int` NÃO é mais tempo (config v22).** É o **lote mínimo**: quantos registros pendentes
  disparam um envio; 0 desliga a telemetria. `t_bat` é o **lote máximo** por requisição. Quem ler
  "intervalo em ms" em qualquer lugar está lendo documentação velha. A migração v21→v22 converte
  o valor antigo (`telMinBatchFromLegacyMs`, com teste nativo) — sem ela um `t_int=300000` viraria
  "300 mil pendentes" e a telemetria ficaria muda em silêncio.
- **No Air, um wake só liga o rádio se `pending >= t_int`** e se não houver penalidade pendente:
  um wake cujo envio falhou reserva `AIR_TEL_FAIL_SKIP_WAKES` (5) wakes de silêncio, guardados no
  mesmo campo do `scratch[1]` que era o contador de wakes. Sem isso, com coletor morto a fila só
  cresce, o gatilho é verdadeiro em todo wake e o rádio drena a bateria.
- **A bancada escondia o F05** — corrigido em 07/09 (modo dreno no FLUSH). O T06 hoje usa lote
  mínimo 1 e cobre o mesmo caso.
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
  aparelho "não dorme", conferir `t_int` (0 = desligada) antes de procurar
  outra causa; `air stop` pela serial tira dele.
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
- **CI cobre o Air desde `build.yml:93/117`** (`native_air` e os cinco envs,
  `pico_w_air` incluído) — mas só em `main`. Rodar local antes de cada push: `pio run -e pico_w_air`,
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

## Log binário — o filtro por transição (LogPolicy)

- **Os DOIS lados são por transição desde 07/09.** O sucesso já era; a falha não, e era o vazamento
  maior. Regra atual: a primeira falha de uma família grava, as seguintes ficam subentendidas até a
  recuperação, com um batimento de 1 h para "falhando em silêncio" não virar "recuperado".
- ⚠️ **O travamento é por FAMÍLIA, não por código, e isso não é detalhe.** `SYS_TEL_FAIL` (31) e
  `SYS_TEL_RETRY` (32) **se alternam** a cada tentativa; um latch por código deixaria os dois
  passarem sempre e não suprimiria nada.
- **FATAL nunca é filtrado**, checado antes da tabela. WARN/ERROR **não roteados** também passam
  sempre — o padrão seguro é: código novo só fica quieto se alguém o listar de propósito.
- **Medido no ferro (A/B alternado, 2 rodadas idênticas):** coletor morto, 6 min em M0 →
  **12 registros antes (6 FAIL + 6 RETRY), 0 depois**. Controle contra excesso: após um reboot, uma
  falha nova grava **+1 FAIL e +0 RETRY** em 5 min.
- ⚠️ **`clear log confirm` não zera o que o `show system log` devolve** — ele costura o rotacionado
  com o corrente. Só valem DELTAS entre duas leituras, nunca contagens absolutas.

## Log binário — o preâmbulo de boot num aparelho que boota o tempo todo

- **O problema, medido:** o log do Air era dominado por BOOTS. 1.253 registros, **108 boots**, e a
  assinatura de um wake era sempre a mesma, na mesma ordem: `524 441 590 407 549 540 567 404`
  (`NET_PROVISIONAL_TIME`, `APP_UI_LANG_CHANGED`, `SENSOR_RUNTIME_LOADED`, `APP_SENSORS_CALIBRATED`,
  `TEL_ALARM_LINE_ON`, `TEL_HTTP_INIT`, `STO_H5_WIP`, `APP_READY`). Oito códigos = **68,2% da
  janela forense inteira**, que enchia em ~79 min.
- ⚠️ **RETRATAÇÃO de um diagnóstico meu de 07/09.** Eu havia escrito que a causa era "o `LogPolicy`
  mora na RAM e o `begin( )` o zera a cada boot". Está errado para **7 dos 8 códigos**: eles não
  têm regra nenhuma na `EDGE_RULES` e caíam no default "código não roteado → grava sempre". Fazer o
  estado do filtro sobreviver ao sono não teria mudado nada. O verificador é uma linha:
  `python3 -c` cruzando a lista com a tabela — feito, 7 de 8.
- **A correção (07/09):** `LogPolicy::setQuietPreamble( )`, armado em `LogManager::begin( )` **só
  quando o boot veio da hibernação** e desarmado por `endBootPreamble( )` no fim do `setup( )`.
  Enquanto armado, os códigos da lista `BOOT_PREAMBLE` em nível INFO são suprimidos.
- ⚠️ **A contabilidade horária (`SYS_LOG_SUPPRESSED`) não vale nada num Air, e isso é limitação
  conhecida, não descuido.** Os registros suprimidos são contados, mas o relatório sai a cada 1 h de
  `millis( )` — e num Air o `millis( )` reinicia a cada wake, então ele nunca sai. Aqui isso não
  custa: o que foi suprimido é uma lista fixa e conhecida de 8 códigos, ao contrário da supressão de
  telemetria, onde o número **é** a informação (quanto durou a queda). Emitir o relatório antes de
  dormir devolveria um registro por wake, que é exatamente o que se está tirando.
- 🔑 **O discriminador já existia e não custa flash nenhum:** `_airActive`, lido do
  `watchdog_hw->scratch[0]` como **primeiro efeito colateral do `setup( )`** (`AppManager_Boot.cpp`),
  427 linhas antes de o log sequer existir. O `scratch[0]` **é** a "abertura/fechamento" de boot que
  se pensaria em guardar no LittleFS: é escrito antes de dormir e **apagado fisicamente por uma
  queda de energia**, que é justamente o caso que precisa gravar. Persistir isso em arquivo custaria
  uma escrita de flash por ciclo e não seria mais confiável.
- **Reforço:** o portão exige `_airActive && _airSleptSec != 0`. O `scratch[1]` só recebe os
  segundos medidos **depois** que o WFI retorna, então um reset apertado DURANTE o sono cai como
  boot frio e leva o preâmbulo inteiro — a direção segura, e a certa: ninguém acordou, alguém
  interveio.
- **`412 APP_AIR_COLD_BOOT`** é a linha que um wake nunca escreve. Quando aparece, o aparelho
  reiniciou sem vir da hibernação: numa implantação a bateria isso é interrupção de energia.
  `ctx=1` boot limpo (energia, RUN, `reload`, OTA), `ctx=0` watchdog chegou antes.
- ⚠️ **Dois códigos da lista também são ação de operador** (`441` idioma pela CLI, `407` calibração
  por `/api/calib`). É por isso que a janela fecha no fim do `setup( )` e não dura o wake inteiro —
  e é o único teste que reprova quando o filtro fica ligado para sempre
  (`test_closing_the_window_makes_operator_actions_visible_again`).
- **Medido no ferro** (mesmo procedimento nos dois firmwares, telemetria desligada para nenhum wake
  levantar o rádio, 8 despertares contados **de fora** pela enumeração do USB): **67 registros em 8
  wakes antes (8,38/wake), 10 depois (1,25/wake) — 6,7× menos.** No log depois: um boot frio traz
  `412 ctx=1` + o preâmbulo inteiro; um wake não traz nenhum dos oito. E o `412 ctx=0` apareceu
  sozinho logo abaixo de um `[FTL] code=1 ctx=227` — autópsia de watchdog mais a linha dizendo que
  aquele boot não veio da hibernação.
- **`T16 wake_writes_no_preamble` é o portão** (`tools/air_test_suite.py`): conta só os 8 códigos
  do preâmbulo antes e depois de um ciclo inteiro. Teto **1**, não 0 — e esse 1 é de propósito:
  o `STO_H5_WIP` sai uma segunda vez no flush do próprio ciclo, **depois** que a janela fechou, e
  como um registro suprimido **não** marca a família como vista, ele chega como a primeira
  transição de `LOGGRP_HIST` e é gravado, com a contagem do bloco no `ctx`. Ou seja: o wake troca
  oito registros por **um**, e o que sobra é o que descreve o trabalho que ele acordou para fazer.
  ⚠️ Delta negativo = o log rotacionou no meio; o teste dá SKIP, porque só valem deltas.
- **Custo:** +176 B de flash na imagem Air.

## Console de emergência — quem muda algo que precisa sobreviver ao boot, salva ali mesmo

- **A regra:** na imagem Air e na de release (`SIMUT_CLI_FULL == 0`) **não existe `write memory`**.
  O `changed = true` que o `switch` de comandos usa não salva nada em lugar nenhum: no perfil
  completo ele imprime *"use `write memory`"*, e no console de emergência imprime *"vale para esta
  sessão"*. Portanto, **todo comando desse console cuja mudança precise sobreviver ao boot tem que
  chamar `saveConfiguration( )` na própria caixa do `switch`** — e não setar `changed`, porque a
  frase que ele imprime passaria a ser mentira. É o que `CMD_SET_WIFI_SSID` e `CMD_SET_WIFI_PASS`
  sempre fizeram.
- 🔴 **O caso que provou a regra (F27, 08/09):** `system admin reset` — a recuperação documentada
  para uma web trancada, e a única — só mexia na RAM. Ela imprimia uma senha nova e **o boot
  seguinte trazia a velha de volta**. Medido nas duas pontas: login OK dentro do boot que imprimiu,
  `401 err=2` depois de `reload confirm`. **Num Air todo wake é um boot**, então a senha valia
  cerca de um minuto.
- ⚠️ **O que escondeu por um dia:** um comentário no `#else` afirmando *"`debug` is the only
  survivor that sets this flag"*. Era falso. **Uma afirmação errada num comentário é pior que
  nenhum comentário** — ela dispensa a verificação em vez de convidá-la. Se um comentário enumera
  ("o único", "sempre", "nunca"), confira a enumeração com um `grep` antes de confiar: aqui era
  `grep -n "changed = true"` e uma olhada nos `#if`.
- 🔑 **`saveConfiguration( )` pode ser chamada direto do handler.** Ela já cuida do que dá medo:
  `WdtWindow(30000)`, o `BigSaveGuard` que congela o Core 1 num laço só-RAM, e o skip de no-op por
  CRC. Não precisa de `Core1FlashPause` do lado de fora.
- **Salve ANTES de anunciar.** A senha impressa é uma promessa; se o save falhar, o comando tem que
  dizer (`NAO SALVOU: vale so ate reiniciar`) em vez de deixar o operador achar que guardou.
- ⚠️ **Persistir um flag muda quem mais o lê.** `mustChangePassword` sobrevivendo em flash quebrou
  `isFactoryDefaults( )`, que era literalmente *"admin pendente de troca"* — e o anúncio
  `SEC-003: FACTORY DEFAULTS ATIVADO` passaria a sair **a cada wake**, sobre um aparelho sem factory
  defaults, estourando o teto de 1 registro/wake do T16. O predicado honesto é o **texto claro de
  uma vez só** (`_initialAdminPassword`), escrito só por `loadDefaults( )` e zerado assim que uma
  config válida vem da flash: a presença dele data a resposta **ao boot que regenerou a config**.
  Antes de persistir um campo que antes era volátil, **procure quem mais o lê** (`grep`), porque
  eles vinham confiando na volatilidade sem dizer.
- **`T17 admin_reset_persists` é o portão** (`tools/air_test_suite.py`): reseta no console,
  reinicia, e exige que a senha impressa **ainda** logue; depois devolve a senha da bancada pela
  web. Falhou de propósito no firmware anterior antes de existir — controle negativo medido à mão.
- **Custo:** +16 B de flash na imagem Air.

## Um laço que consulta uma constante de compilação nunca sai mais cedo

- 🔑 **O padrão:** `while (!cond() && millis() - t0 < TIMEOUT)` só é uma *espera* se `cond()` puder
  virar verdadeira. Quando ela é `return false` literal, ou lê um flag sem escritor no link daquela
  imagem, **o timeout deixa de ser o pior caso e vira o custo fixo**. Não aparece em revisão porque
  a linha lê como uma espera com guarda; aparece no cronômetro.
- 🔴 **Três casos, todos no mesmo `setup( )`, achados em 08/09 e valendo 5,2 s por wake:**
  o portão de silêncio do touch e a janela de AP consultam `isScreenTouched( )`, que é
  `return false` em `DisplayManager_None.cpp` **e** em `DisplayManager_Alpha.cpp`; e a espera por
  `isCore1Ready( )` lê `_core1Ready`, escrito só em `DisplayManager.cpp` e `DisplayManager_Alpha.cpp`
  — **nenhum dos dois entra no link do `pico_w_air`**. Esse último não era nem um `delay`: era
  `tight_loop_contents( )`, núcleo a plena corrente, 1500 ms, uma vez por minuto.
- 🔎 **Como procurar:** para cada `while`/`for` com timeout no boot, pergunte *quem escreve a
  condição nesta imagem* e confirme com `grep` mais a **linha de link do build**
  (`.pio/build/<env>/src/*.o`) — não pelo `#include`, que mente: o header está lá, o `.cpp` não.
- ✅ **Como consertar sem criar outro:** uma constante de capacidade `static constexpr` **ao lado do
  estado que ela descreve** (`DisplayManager::kUsesCore1`, `kHasTouch`), decidida pelo
  pré-processador e não por expressão sobre macros — assim uma ordem de include que ainda não viu
  `simut_config.h` não transforma um define ausente num `true` silencioso. Fica junto de
  `_core1Ready`, então dar um display ao Air move os dois no mesmo lugar.
- ⚠️ **Espera fixa por operador é outra categoria, e o portão é outro.** `delay(1000)`,
  `BOOT_STEP_DELAY_MS`, o `delay(800)` do splash e o ciclo de energia do CYW43 existem para gente
  ou para caminhos de reinício reais — o portão deles é `_airActive` (veio da hibernação), não a
  imagem. Num wake não há ninguém; em M0 há.
- ⚠️ **`readInterval` de sensor NÃO é parte da medição, e não se sobrepõe à conversão.**
  `lastReadTime` é carimbado no **fim** da leitura, então o período real é
  `readInterval + tempo de conversão` — 1000 + 750 ms no DS18B20, dez vezes, antes de a fase SAMPLE
  liberar um registro. `SensorManager::setFastSampling( )` zera só o intervalo, nas fases
  WARMUP/SAMPLE de um wake, e o `air stop` o desliga junto com o modo dreno. **Mexer em
  `MOVING_AVG_WINDOW` ou na resolução do DS18 é outra coisa: muda o número gravado, e é decisão do
  Ângelo, não de implementação.**
- **Medido no ferro, mesma bancada, 08/09:** `setup( )` 10,73 → **2,47 s**; SAMPLE 14,81 →
  **6,83 s**; wake até DECIDE 25,55 → **9,31 s**. Flash: Air **−624 B**, alpha −568 B, release +8 B.
  ⚠️ **Efeito colateral na bancada:** a janela de enumeração USB encolheu junto, então capturar o
  começo de um wake ficou mais difícil — o `System ready` chega antes de a porta abrir. Mantenha a
  porta aberta desde o M0, ou leia os carimbos `@millis` das linhas `[AIR] phase=`.
