# SIMUT Air — Plano de correção e otimização

> **Status:** Fases 0, 1 e 3 executadas e medidas no ferro; Fases 2, 4 e 5 abertas. Registro de
> execução na seção 6.
> **Base:** branch `feature/simut-air`, commit `461a806` (06/09/2026), sobre `main` `6d2142c` (v2.3.9-beta).
> **Origem:** revisão de código de 06/09/2026 (21 achados, F01–F21), sem validação no ferro.
> **Esboço de projeto:** [`SIMUT_AIR_ESBOCO.md`](SIMUT_AIR_ESBOCO.md).
> **Plano de energia (leitura frequente, rádio raro):** [`SIMUT_AIR_PLANO_ENERGIA.md`](SIMUT_AIR_PLANO_ENERGIA.md).
> **Suíte que prova cada item:** `tools/air_test_suite.py` (CLI + web + PicoHand) e `tools/check_air_consistency.py` (estático).
> **Idioma:** pt-BR (espelha os demais `docs/analysis/`).

---

## 0. Sumário executivo

A revisão de código mais a bancada de 06/09 acumularam **25 achados**, e **nenhum bloqueante segue
aberto**. O último a cair foi o **F25**: um watchdog durante a janela acordada devolvia o aparelho
a M0 e na bancada ele nunca mais hibernava — num produto a bateria, ficar acordado para sempre. A
intenção do operador passou a viver no `air.bin`, a volta ao ciclo tem graça de 10 s, e um contador
de boots sujos preserva a alcançabilidade que o desenho original protegia (§6.7).

> **O defeito que valia a saga era o tempo, e a causa era uma linha.** Com o intervalo
> configurado em 2 minutos o aparelho acordava a cada **16 a 48 minutos**, de forma variável.
> Causa: o ROSC era desligado antes do WFI e **não religado**, e o reset do wake não passa pelo
> domínio de reset dele. Corrigido e medido: **120,7 s ± 0,1** em quatro ciclos seguidos. A
> corrupção do histórico (F23) era consequência disso e **sumiu junto** — zero gaps negativos
> nos registros posteriores ao fix.

**Estado por fase:**

1. **Fase 0 — instrumentar e medir no ferro.** ✅ §6.1. Procedência do binário provada
   (`461a806`), F22 e F23 confirmados, três instrumentos meus desmascarados.
2. **Fase 1 — bloqueantes.** ✅ §6.2. F01/F22 (ROSC), F02 (teto do FLUSH), F03+F07 (energia dos
   sensores). +200 B no Air, release e alpha intactos.
3. **Fase 3 — consistência.** ✅ (menos o CI). `check_air_consistency.py` C1–C8 limpo e
   **−712 B no release / −272 B no alpha** — a limpeza se pagou em flash.
4. **Fase 4 — intervalo real.** ✅ §6.4 e §3. O alarme passou a ancorar no wake e a conta passou
   a arredondar, e o alarme passou a ser armado relativo ao valor que o RTC **lê** em vez do zero
   que se escreve nele — o `load` já vale um tique, e era isso que fazia o aparelho acordar 1 s
   cedo em todo ciclo. **Ciclo medido pela sonda: 120,23 s para 120 s pedidos.** A série do dia,
   para a mesma configuração: 16–48 min → 147 s → 118,9 s → 119,3 s → **120,23 s**.
5. **F24 — cursor de telemetria.** ✅ §6.5. Duas causas: a coalescência de 5 s derrotava a escrita
   pré-sono (corrigida) e o coletor da bancada aceita a conexão TCP e nunca responde (lado do
   servidor, fora do firmware).
6. **SSID indisponível.** ✅ §6.6. Teto de tentativas por wake; medido, a rede ausente **não
   alarga** a janela acordada.
7. **Resto da Fase 2 (ciclo M1), da Fase 4 (energia) e a Fase 5 (release)** seguem abertas, mais o
   CI cobrindo `pico_w_air`/`native_air`.

Ordem original de ataque, para referência:

1. **Fase 0 — instrumentar e medir no ferro.**
2. **Fase 1 — bloqueantes** (F22, F23, F02, F03).
3. **Fase 2 — ciclo M1 correto** (F04–F14, F21).
4. **Fase 3 — consistência e portões** (F15–F20 + CI cobrindo o Air).
5. **Fase 4 — otimização de tempo acordado e energia**, só depois de medir.
6. **Fase 5 — release** (CHANGELOG, manuais, bateria de ferro, merge).

Critério de pronto: **suíte `air_test_suite.py` sem FAIL nem XFAIL** (todo XFAIL vira PASS ou é
removido com justificativa), `check_air_consistency.py` limpo, `pico_w_release`/`pico_w_alpha`
byte-idênticos ao estado atual exceto pelas remoções de debug, e CI verde com `pico_w_air` e
`native_air`.

---

## 1. Achados (F01–F21)

Severidade: **B** = bloqueante, **F** = bug funcional, **I** = inconsistência.
"Teste" refere-se aos casos de `tools/air_test_suite.py` (T-xx) e `tools/check_air_consistency.py` (C-x).

Os quatro últimos achados **não** nasceram da revisão de código: vieram da bancada, e por isso
moram no registro de execução em vez desta tabela. **F22** (período de sono errado) e **F23**
(histórico não monotônico) estão em §6.1, com o desfecho em §6.2 e §6.3; **F24** (cursor de
telemetria que não avança) está em §6.5; **F25** (um reset durante o wake tira o aparelho do
ciclo, e ele pode não voltar) está em §6.7 — o único bloqueante ainda aberto.

| ID | Sev | Onde | Sintoma | Correção | Teste |
|---|---|---|---|---|---|
| F01 | **B — e é a causa provável do F22** | `src/air/pico_sleep.c` | ROSC desligado antes do WFI e **não religado no wake**. A hipótese original (travar de vez no `runtime_init_clocks`) estava errada: o aparelho volta, mas **demora um tempo longo e variável**. A correlação temporal é a prova circunstancial forte — o ciclo estava certo (147–151 s) até a build sem ROSC-off e degradou para 16–48 min a partir da janela em que a build nova foi gravada (§6.1 item 7b) | ✅ **CORRIGIDO em 06/09**: religar o ROSC logo após o `wfi`, antes do SYSRESETREQ, esperando `ROSC_STATUS_STABLE` — o mesmo que o `sleep_power_up` do pico-extras faz. Custo: +32 B de flash | T05, `--watch` |
| F02 | ✅ **CORRIGIDO 06/09** | `src/AppManager_Air.cpp` | FLUSH sem teto: telemetria desligada (`telInterval==0`, default de fábrica) ou RSSI abaixo do limiar com link de pé deixam `update()` retornar sem backoff e a fila nunca zera | Saída imediata quando `telInterval == 0`; `netLost` passa a usar `isNetworkHealthy()` (cobre RSSI); **teto de parede** por `flushTimeoutMs` — nenhuma trava futura no uploader segura o aparelho acordado | T06b, T05 |
| F03 | ✅ **CORRIGIDO 06/09** | `src/AppManager_Boot.cpp`, `src/AppManager_Air.cpp` | GP16 só era ligado no WARMUP: em M0 ninguém ligava o pino; no boot M1 o `setup()` inteiro rodava com os sensores desligados | `airSensorPower()` deixou de ser estática e é chamada no início do `setup()`, antes de qualquer sondagem; o pino de `air.bin` substitui o default logo após a carga. Efeito colateral bom: o boot inteiro passa a ser o warm-up dos sensores lentos | T02, T05, T09 |
| F04 | F | `src/NetworkManager.cpp:108`, `src/AppManager_Air.cpp:201` | Relógio provisório = último registro + 60 s + uptime; cada wake offline avança o histórico ~80 s em vez do intervalo real; a correção pós-NTP (`AppManager_Loop.cpp:282`) nunca roda em M1 | Usar o RTC como relógio de parede: setar o RTC com o epoch atual antes de dormir, ler após o `wfi`, guardar em `scratch[1]`, e no boot M1 semear `setProvisionalTime(scratch[1])`; escrever o registro depois do CONNECT quando houver link (ver F08) | T08 |
| F05 | F | `src/TelemetryManager.cpp:245`, `:354` | `begin()` carimba `_lastCheckTime` e cada lote espera `telInterval`; com 60 s e 40 lotes são 40 min acordado (a bancada usa 100 ms e não vê) | `TelemetryManager::airKick()` zera a cadência; chamar antes do 1º lote e após cada lote bem-sucedido no FLUSH | T06 |
| F06 | **sem objeto após o F03** | `src/AppManager_Boot.cpp` | WARMUP dura zero no boot M1: o timer é marcado no início do boot e já expirou quando o `airLoop` roda | Com o F03 os sensores passam a ser energizados no **início** do `setup()`, e o boot leva ~20 s — ou seja, o warm-up real hoje é o boot inteiro, muito acima do 1 s que o DHT22 pede. O caminho por comando (`air hibernate`) sempre teve os 400 ms corretos (medido: WARMUP→SAMPLE em 395 ms). Fica só a nota | T05, T09 |
| F07 | ✅ **CORRIGIDO 06/09** | `src/AppManager_Air.cpp` | `gpio_init` a cada iteração do WARMUP: o SDK põe o pino em entrada e nível 0 antes de subir de novo (glitch na alimentação e na sonda) | `gpio_init` uma vez por pino, com máscara estática; depois só `gpio_put` | T09 |
| F08 | F | `src/AppManager_Air.cpp:203`, `src/NetworkManager.cpp:223` | CONNECT é inalcançável: DECIDE só vai a CONNECT quando já `NET_READY`, que exige NTP sem fallback; um pacote NTP perdido (retry 20 s) ou rede sem internet = dorme sem enviar; `connectTimeoutMs` nunca atua | DECIDE→CONNECT quando `WiFi.status()==WL_CONNECTED`; CONNECT espera `NET_READY` até `connectTimeoutMs`; NTP em Air com 1º retry curto (5 s) | T05, T06 |
| F09 | F | `src/AppManager_Commands.cpp:842` | `air idle` aceita até 86400 mas o campo é uint16: 86400 vira 20864 s | Faixa 10..65535 em `airIdleSecValid()` (AirConfig.h) + teste nativo | T04, native |
| F10 | F | `src/AppManager_Air.cpp:315`, `src/WebManager_Commit.cpp:833` | Alarme do RTC com hora módulo 24; `h_int` aceita 1440 min → alarme em 00:00:00 do mesmo dia, dispara na hora ou nunca | Com F04 o alarme vira epoch+wakeSec→`gmtime`, sem limite; alternativa mínima: `wakeSec = min(wakeSec, 23*3600)` | native (cálculo do alarme) |
| F11 | F | `src/StorageManager.cpp:1216`, `src/AppManager_Core.cpp:136` | O M1 pula `StorageManager::update()` (limpeza de orçamento do FS) e nunca chama `flushPendingIfAny()`: em uso só-M1 a partição enche e logs diferidos se perdem | Antes de dormir: `flushPendingIfAny()`, `syslog flushBlocking()` se ligado, e executar a limpeza pendente até concluir (com `watchdog_update`) | T05 (longo), fs% em `/api/status` |
| F12 | F | `src/AppManager_Loop.cpp:43`, `src/BluetoothManager.cpp:73` | `air stop` por Bluetooth não funciona em M1: o laço M1 só chama `processInput` (USB) | Bombear o `BluetoothManager` no laço M1 do mesmo jeito que o M0 faz (ou documentar "só USB em M1", decisão D-1 abaixo) | T05 (variante BT, manual) |
| F13 | F | `src/AppManager_Boot.cpp:803`, `:816`, `:820` | D5 não implementado: em M1 sobem web, BT, mDNS, syslog e discovery; a web nem é bombeada (conexões aceitas ficam penduradas) | Gate `if (!_airActive)` em web/BT/mDNS/syslog/HA/metrics; pular `delay(1000)`, preload de cache do histórico, temas e pack de idioma no boot M1 | T10 |
| F14 | F | `src/air/AirConfig.h:21` | Default do pino mudou para 16 sem bump de `AIR_CONFIG_VERSION`; `air.bin` antigo mantém 255 e não há comando para trocar | `AIR_CONFIG_VERSION 3`; comando `air pin <gpio|off>`; `air status` mostra `pin=` | T03, T04 |
| F15 | ✅ **CORRIGIDO 06/09** (com resíduo) | `src/HelpLicenseEN.h`, `src/CommandParser.cpp`, `tools/check_cli_help.py`, packs | Help de emergência anunciava `air …` em release/alpha; o portão passou a exigir isso; es-ES sem `system ssid/pass` | Raw string partida em três literais adjacentes com o bloco `air` sob `#if SIMUT_AIR`; **o parser também foi guardado** (antes release/alpha reconheciam `air …` e caíam no "comando desconhecido", pagando flash por isso); `check_cli_help.py` lê `SIMUT_AIR` e monta `EMERGENCY_EXPECTED` por imagem; es-ES ganhou `system ssid/pass`. **Resíduo:** um `.lng` é compartilhado por todas as imagens, então os comandos `air` **não** entram no `@HELP` dos packs — num aparelho Air com pack pt-BR/es-ES o `help` não os lista. Fechar exige marcadores no pack que o `getActiveHelpText()` pule quando `SIMUT_AIR=0` | C2, C3, C4 |
| F16 | ✅ **CORRIGIDO 06/09** | `src/AppManager_Boot.cpp` | Dez `Serial.println("[AIR] boot: …")` sem guarda no boot compartilhado: imagens TFT e alpha imprimiam marcadores de um código que nem têm | Macro `AIR_BOOT_MARK(s)`, definida como `Serial.println` sob `#if SIMUT_AIR` e como no-op fora dele. Mantém o rastro onde ele serve (o Air não tem display e reinicia a cada wake) e some do resto | C1 |
| F17 | ✅ **parcialmente corrigido 06/09** | `src/AppManager.h`, `src/air/AirConfig.h`, `src/simut_config.h` | `airBeginWake()` declarado e nunca definido; `AIR_PHASE_PERSIST`, `wifiScanTimeoutMs`, `flushTimeoutMs` mortos; comentários prometendo o que não existe | `airBeginWake()` removido; `flushTimeoutMs` **passou a valer** (F02); comentários de `simut_config.h` e `platformio.ini` corrigidos. **Resta:** `AIR_PHASE_PERSIST` (no-op) e `wifiScanTimeoutMs` (campo morto) — saem junto com o bump de `air.bin` da F14, para não gastar duas migrações | C5, C7 |
| F18 | I | `platformio.ini:378` | Comentário diz `SIMUT_CLI_FULL=1`, o env usa 0 | Corrigir o comentário (feito em 06/09) | C6 |
| F19 | ✅ **CORRIGIDO 06/09** | `src/LogManager.cpp` | Mapa de scratch dizia que 0..2 eram reservados e intocáveis; o Air usa o 0 | `scratch[0]` documentado no mapa como o marcador de hibernação do Air, com o porquê de o slot estar livre e a semântica medida (sobrevive ao SYSRESETREQ do wake, zera no reset físico) | C8 |
| F20 | I | `src/CommandParser.cpp`, README | `system ssid/pass` entraram no console de emergência de todas as imagens sem doc; README fala em 10 comandos | Manter (útil na alpha headless) e documentar: README ×3, CLI-Manual (feito em 06/09) | C3 |
| F21 | F | `src/AppManager_Air.cpp:95` | `airMarkActivity()` só é chamado por comando serial/BT; requests web autenticados não resetam o timer de inatividade (operador na web é hibernado aos 5 min); não há `/api/air` | Chamar `airMarkActivity()` no gate de sessão da web; endpoint `/api/air` (GET status, POST hibernate/idle/pin) para fechar D2/D7 | T07 |
| **F22** | **B — o achado crítico** | `src/AppManager_Air.cpp:315`, `src/air/pico_sleep.c:101` | **MEDIDO no ferro em 06/09 por quatro caminhos independentes** (§6.1): com `hist=120 s` o ciclo real é de **16 a 30 min**. O arquivo de histórico do dia é a prova direta: gaps entre ciclos de 945, 978, 1039, 1640 e 1817 s, e **168 registros no dia inteiro onde caberiam 720**. Teto de backoff é 300 s, então nenhuma configuração explica. A não-uniformidade argumenta contra erro fixo de divisor e a favor de **o alarme não disparar como programado** | Investigar antes de corrigir: (a) ler o RTC logo após o `wfi` e imprimir quantos segundos ele contou — separa "alarme atrasado" de "acordou por outro motivo"; (b) conferir `clk_rtc` de fato em 46875 Hz (`clock_get_hz`) e o `clkdiv_m1` pós-`rtc_init`; (c) casar o alarme só por hora/min/seg (campos de data em −1) para não depender da data fictícia 2026-01-01. F04 reaproveita o mesmo conserto | T05, `--watch`, histórico |
| **F24** | **B** | `src/StorageManager.cpp` (`flushCursorIfDirty`) × ciclo M1 | **Relatado pelo Ângelo e confirmado no ferro em 06/09**: o cursor de telemetria **não avança** e o mesmo pacote é reenviado a cada wake. Causa: `flushCursorIfDirty( )` adia a escrita por `CURSOR_COALESCE_MS` (5 s) para poupar a flash — mas no ciclo M1 o envio marca o cursor sujo e a fase FLUSH sai **~150 ms depois** (fila vazia), então a janela nunca decorre e a escrita nunca acontece. O sono perde a SRAM e o boot seguinte relê o arquivo antigo. O comentário no ponto de chamada já dizia "o cursor precisa estar na flash porque o dormant perde a SRAM"; a intenção estava certa e o portão silenciosamente a anulava. Medido antes do fix: `pending=8` e subindo, com o coletor no ar | ✅ **CORRIGIDO**: `flushCursorIfDirty(bool force)` — os dois portões (coalescência e toque) só valem quando existe um "depois". Os três caminhos do Air para o sono passam `true`, incluindo o `airEnterDormant( )`, que é o ponto único por onde todo sono passa | `/api/status` `pending` ao longo de N ciclos |
| **F23** | **média** (era B; reduzido após retratação) | `src/StorageManager.cpp` (encoder V5) × ciclo M1 | **MEDIDO em 06/09, com a ressalva da §6.1**: lido com o nominal correto, o histórico do dia tem **12 gaps negativos** em 174 — o arquivo **não é monotônico**. O interior de cada bloco é reconstruído pelo passo nominal, então um bloco cujas amostras reais foram mais espaçadas avança além do `t0` do bloco seguinte. ⚠️ As "rajadas de 60 s" da primeira versão deste plano eram artefato de decodificação, não do firmware | Selar o bloco antes de dormir (hoje `flushWipV5` só faz snapshot), para que cada wake abra bloco novo com `t0` próprio e o interior nunca precise representar um intervalo de sono. Reavaliar depois que o F22 fechar: com o período correto, a distorção do interior encolhe sozinha | T11 |

---

## 2. Fases

### Fase 0 — bancada segura (antes de tocar em código)

0. **Estabelecer a procedência do binário gravado.** A string de versão não discrimina builds
   (regra antiga do projeto). Discriminador barato para o Air: o `help` do console de emergência
   traz `system ssid` **apenas** a partir de `461a806`. Sem isso, qualquer veredito sobre F01
   fala de um binário desconhecido. Se houver dúvida, regravar com
   `python3 tools/air_test_suite.py --flash .pio/build/pico_w_air/firmware.uf2`.
1. `tools/fsguard.py backup` do LittleFS do alvo (regra da bancada: nada de stage/format sem backup).
2. **Provar F01**: gravar `461a806` como está, `air hibernate` com `h_int=1`, observar o wake pela suíte
   (`--only T05 --cycles 1`). **O critério é o alvo reenumerar sozinho** dentro de `wakeSec + 120 s`;
   se não reenumerar, F01 está confirmado. ⚠️ `hand RESET` **não serve de prova**: o pulso é no pino RUN,
   reset global que restaura o ROSC e os clocks de fábrica, então recupera o alvo mesmo com o F01 real —
   serve só para retomar a bancada (e dá boot frio em M0, porque o reset físico zera o `scratch[0]`,
   `src/LogManager.cpp:605`). Registrar o resultado na seção 6 e em `AGENTS.md`.
3. Rodar a suíte inteira em `--baseline` para congelar o estado atual (os XFAIL esperados estão
   marcados no código da suíte e listados na seção 4).
4. **Medir o ciclo sem tocar no aparelho**: `python3 tools/air_test_suite.py --watch 2700`.
   Esse é o instrumento a confiar para o período — qualquer comando de CLI reseta o timer de
   inatividade e abrir a porta na hora errada perturba justamente a janela medida.

### Fase 1 — bloqueantes ✅ EXECUTADA em 06/09/2026

As três correções abaixo estão **implementadas e gravadas no ferro**. Custo somado: +200 B de
flash no `pico_w_air` (97,6% → 97,7%); `pico_w_release` e `pico_w_alpha` intactos. Validação em
§6.2.

**F01 — religar o ROSC no wake** (`src/air/pico_sleep.c`) — ✅ **FEITO e MEDIDO**

```c
__asm volatile("wfi");
/* Wake. The reset that follows does NOT go through the ROSC/CLOCKS blocks
 * (sleep_en0 and the RTC alarm state were observed to survive it), so the
 * ROSC we disabled above stays disabled unless we bring it back here. The
 * boot ROM and runtime_init_clocks() switch clk_ref onto the ROSC and spin
 * until the glitchless mux sees an edge — with the ROSC stopped that never
 * happens and the chip is dead until a power cycle. */
rosc_hw->ctrl = (rosc_hw->ctrl & ~ROSC_CTRL_ENABLE_BITS)
              | (ROSC_CTRL_ENABLE_VALUE_ENABLE << ROSC_CTRL_ENABLE_LSB);
while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS)) tight_loop_contents();
```

**Resultado:** o ciclo voltou ao que a bancada tinha de manhã. Ver §6.2.

**F02 — teto do FLUSH** (`src/AppManager_Air.cpp`, caso `AIR_PHASE_FLUSH`) — ✅ **FEITO**

- saída imediata quando `cfg.telInterval == 0` (o default de fábrica): nesse estado o
  `TelemetryManager::update( )` retorna sem enviar, sem falhar e sem escalar backoff, então
  **nenhuma** das três condições de saída antigas podia se tornar verdadeira;
- `netLost` passou a usar `isNetworkHealthy( )`, que inclui o piso de RSSI — um link associado
  mas fraco demais para subir dados mantinha `isConnected( )` verdadeiro;
- **teto de parede** por `_airCfg.flushTimeoutMs`, que não depende do uploader chegar a um
  veredito: nenhuma trava futura naquele caminho segura o aparelho acordado.

⚠️ Aceite parcial: o caminho `telInterval == 0` **não foi exercitado no ferro** (exigiria mudar
a config de telemetria do Ângelo e reiniciar). O que foi observado é o FLUSH saindo em 145 ms
com a fila vazia. T06b continua sendo o teste que fecha isso.

**F03 — energia dos sensores desde o boot** (`AppManager_Boot.cpp`, `AppManager_Air.cpp`) — ✅ **FEITO**

- `airSensorPower( )` deixou de ser estática e é chamada no **início** do `setup()`, logo após
  a leitura do `scratch[0]`, antes de qualquer sondagem de sensor;
- depois de `airLoadConfig( )`, se `air.bin` nomear outro pino, o configurado sobe e o default
  desce — exatamente uma linha fica acionada;
- `gpio_init` uma vez por pino, com máscara estática (**F07**), então o WARMUP não glitcha mais
  a alimentação nem a sonda;
- em M0 o pino fica alto o tempo todo, que era o buraco principal.

Efeito colateral bom: como a energia sobe no início do boot e o boot leva ~20 s, o warm-up real
passou a ser muito maior que o 1 s que o DHT22 pede — é o que torna o **F06 sem objeto**.

⚠️ Aceite parcial, agora com a fiação confirmada pelo Ângelo: na bancada de referência o
**DS18B20 está no GP0, sem chaveamento**, e o **GP16 vai para a PicoHand como sonda**. Ou seja, o
caminho de power-gating **não é exercitado aqui** — o que se mediu é que o firmware aciona a linha
desde o início do `setup()` e a solta só ao dormir. Para a sonda isso é uma melhora: o nível alto
passa a cobrir a janela acordada inteira, em vez de começar só no WARMUP.

### Fase 2 — ciclo M1 correto

Dois itens desta fase já saíram, fora de ordem, porque a bancada os cobrou:

- ✅ **F24 — o cursor de telemetria é gravado antes de dormir** (`flushCursorIfDirty(true)`), com a
  coalescência e a prioridade de toque puladas quando a escrita é forçada. §6.5.
- ✅ **SSID indisponível não segura o wake** — `AIR_MAX_CONNECT_ATTEMPTS` limita as tentativas por
  wake; passado o teto, SAMPLE para de bombear a rede e DECIDE trata o wake como offline. §6.6.

**F04 + F10 — RTC como relógio de parede**

- Antes de dormir: `epoch = getEpoch()`; RTC ← `gmtime(epoch)`; alarme ← `gmtime(epoch + wakeSec)` (sem limite de 24 h).
- ⚠️ **`scratch[1]` JÁ TEM DONO desde 06/09** (§3): guarda `AIR_SLEPT_MAGIC | segundos dormidos`,
  medidos pelo próprio RTC depois do `wfi`. Não reivindicar o registrador sem `grep` — é a
  armadilha de sempre. E não faz falta: **segundos dormidos é o dado melhor**, porque o boot
  monta o epoch como `último epoch conhecido + dormidos + acordado`, sem depender de o RTC ter
  sido semeado com hora real antes de dormir.
- No boot M1: se o scratch trouxer o magic, `setProvisionalTime(epoch_antes + slept - 60)` (a
  função soma 60) e zerar o scratch. O boot já lê e imprime esse valor (`[AIR] woke: slept=<n>s`);
  o que falta é **usá-lo** para semear o relógio.
- Manter a semântica de proveniência: continua "provisório" até o NTP; com F08 o registro só é escrito após o NTP quando há link.
- Atualizar o mapa de scratch (F19).

Aceite: T08 (3 wakes offline com `h_int=2` → espaçamento 120 ± 25 s).

**F05 — cadência do dreno**

- `TelemetryManager::airKick()` → `_lastCheckTime = millis() - _effectiveIntervalMs` (ou zero).
- No FLUSH: kick ao entrar e após cada `consumeLastSendResult(true)`.

Aceite: T06 (com `t_int=60000`, janela acordada < 30 s e todos os registros entregues).

**F06 + F07 — WARMUP real e pino sem glitch**: ver Fase 1; `AIR_WARMUP_MS` default 1000 ms; campo `warmupMs` no `air.bin` (reaproveitando `wifiScanTimeoutMs`).

**F08 — CONNECT de verdade**

```
SAMPLE (sensores estáveis ou timeout)
  ├─ WiFi.status()==WL_CONNECTED  → CONNECT: esperar NET_READY até connectTimeoutMs
  │                                  ├─ READY   → DECIDE (grava com hora real) → FLUSH
  │                                  └─ timeout → DECIDE (grava provisório)   → SLEEP
  └─ sem associação                → DECIDE (grava provisório)               → SLEEP
```

- Em Air, `NetworkManager` usa retry inicial de NTP de 5 s (hoje 20 s).
- `air status` passa a mostrar `net=` (assoc/ready/offline) para diagnóstico.

**F09 — `air idle` 10..65535** com `airIdleSecValid()` em `AirConfig.h` e teste em `test/test_air_config`.

**F11 — tarefas do loop antes de dormir**: em `AIR_PHASE_SLEEP`, antes de `airEnterDormant()`:
`LogManager::flushPendingIfAny()`, `_syslogMgr->flushBlocking()` se habilitado, e
`while (_storageMgr->cleanupPending()) { _storageMgr->update(); watchdog_update(); }` com teto de 20 s.

**F12 — Bluetooth em M1**: bombear o `BluetoothManager` no laço M1 se a decisão D-1 for "BT é canal de emergência"; senão, documentar "em M1 só USB".

**F13 — D5 (boot M1 enxuto)**: em `setup()`, sob `_airActive`: não iniciar web, BT, mDNS, syslog, HA, métricas; pular `delay(1000)`, preload de min/max do histórico, `scanCustomThemes`, `loadTheme`, pack de idioma. Medir o ganho de boot (T05 reporta `awake_s`).

**F14 — `air.bin` v3 + `air pin`**: bump de versão (arquivo antigo → defaults), comando `air pin <0..28|off>`, `air status` com `pin=`.

**F21 — web reseta o timer + `/api/air`**: `airMarkActivity()` no gate de sessão (`WebManager_Auth`), endpoint `/api/air` (GET status; POST `op=hibernate|idle|pin`), seção Air na página de config. Fecha D2/D7 do esboço.

### Fase 3 — consistência e portões ✅ EXECUTADA em 06/09/2026 (menos o CI)

`python3 tools/check_air_consistency.py` fecha **C1–C8 limpo**, e o
`tools/check_cli_help.py` passa nos quatro ambientes com a expectativa certa para cada imagem
(18 comandos no Air, 14 no release e no alpha, 68 no test).

**O que saiu das imagens de produção**, medido pelo linker:

| imagem | antes | depois | delta |
|---|---|---|---|
| `pico_w_release` | 1.017.828 B | **1.017.116 B** | **−712 B** |
| `pico_w_alpha` | 1.017.932 B | **1.017.660 B** | **−272 B** |
| `pico_w_air` | 1.020.024 B | 1.020.024 B | 0 |

São os dez marcadores de boot, o texto de ajuda dos comandos `air` e o bloco do parser que os
reconhecia sem ter handler. Ou seja: a limpeza de consistência **se pagou em flash** nas duas
imagens que mais sofrem com o teto.

**Resíduo consciente:** um `.lng` é compartilhado por todas as imagens, então os comandos `air`
ficam fora do `@HELP` dos packs — confirmado no ferro, o `help` de um Air com pack pt-BR não os
lista. Fechar isso exige marcadores no pack que o `getActiveHelpText()` pule quando
`SIMUT_AIR=0`. ⚠️ Observado de passagem: o pack pt-BR **do aparelho** é mais antigo que o do
repositório (não lista nem `system ssid`), o que é a armadilha de sempre — pack no LittleFS
sobrevive à gravação de firmware.

**Falta desta fase:** o CI (`build.yml` roda só em `main` e não cobre `pico_w_air`,
`native_air` nem `check_air_consistency.py`).

#### Itens originais da fase

- F15: `HelpLicenseEN.h` com o bloco `air` sob `#if SIMUT_AIR` (partir a raw string em duas literais adjacentes); `check_cli_help.py` lê `SIMUT_AIR` dos build flags e monta `EMERGENCY_EXPECTED` por imagem; `@HELP` dos dois packs completos.
- F16: remover os `Serial.println("[AIR] boot: …")`.
- F17: remover `airBeginWake`, `AIR_PHASE_PERSIST`; renomear campos com o bump de F14; corrigir comentários.
- F19: mapa de scratch.
- **CI**: `build.yml` ganha `pio run -e pico_w_air`, `pio test -e native_air` e `python3 tools/check_air_consistency.py`. O workflow roda só em `main`; para a branch, rodar localmente antes de cada push.
- Testes nativos novos em `test/test_air_config`: `airIdleSecValid`, cálculo do alarme por epoch (F10), `airSleepSecondsFor(hist, backoff)`.

### Fase 4 — otimização (medir antes, medir depois)

Métricas por wake (a suíte reporta): `boot_to_sample_s`, `sample_s`, `connect_s`, `flush_s`, `awake_s`, `sleep_s`, `period_error_s`; corrente média em sleep e acordado (multímetro em série no VSYS ou INA219, registro manual na tabela da seção 6).

Alavancas, por ganho estimado:

1. **Boot M1 enxuto (F13)**: −1 s do `delay(1000)`, −(preload do histórico, que varre blocos), −(web/BT/mDNS). Estimativa: 3–6 s por wake.
2. **Janela de estabilização**: `MOVING_AVG_WINDOW=10` × `s_int` domina o wake (10 amostras × 2 s = 20 s). Novo campo `stabSamples` no `air.bin` (default 3) e critério "N amostras válidas" em vez de `bufferFull()`. Estimativa: −12 s.
3. **Dreno com kick (F05)** e lote 250: o tempo de envio vira função do payload, não do `telInterval`.
4. **NTP a cada N wakes** (RTC mantém a hora, F04): dispensa esperar o NTP em todo wake; estimativa −1 a −2 s e menos dependência da internet.
5. ✅ **Alarme compensado — FEITO em 06/09 (§6.4).** Agendar o próximo wake relativo ao instante do wake atual, não ao instante de dormir, para o período não derivar `awake_s` por ciclo. Medido pela sonda: **120,23 s** para 120 s pedidos. ⚠️ Vieram junto duas lições, ambas em §3: a conta tem de **arredondar** (`sleepMs / 1000` truncava até 1 s por ciclo, sempre no mesmo sentido) e o alarme tem de ser armado sobre o valor que o RTC **lê**, porque o `load` já vale um tique.
6. **ROSC off** (já feito) só depois de F01 provado; ganho ~0,25 mA em sleep.
7. **LED**: apagado durante SAMPLE/FLUSH, um pulso de 50 ms ao gravar e ao enviar (economia pequena; melhor sinalização).
8. **USB em bateria**: se `VBUS` ausente, não esperar a enumeração nem imprimir marcadores (avaliar depois de medir).
9. **DORMANT**: só reabrir quando SLEEP estiver estável por semanas; o esboço registra por que foi descartado.

### Fase 5 — release

1. CHANGELOG (EN + pt-BR) com a entrada Air (já existe "Unreleased" apontando para este plano).
2. README ×3: tabela de envs (feito), console de emergência (feito), seção curta "SIMUT Air".
3. Manuais: `docs/CLI-Manual.md` (console de emergência + `air`, feito), `docs/WIRING.md` (GP16, feito), PicoHand (feito).
4. Bateria de ferro: suíte completa verde + 3 ciclos OTA no Air (a OTA nunca foi exercitada nesta imagem; validar `applier` com o LittleFS do Air).
5. Merge por PR em `main`, tag e release conforme `release-tag` da memória (conferir as três fontes antes de numerar).

---

## 3. Protocolo de medição

**Sinal de tempo (sem hardware extra):** o Air solta o pull-up do USB ao dormir e reenumera ao
acordar; a suíte carimba `absent`/`present` de `/dev/serial/by-id/…Pico_W…` com resolução de ~0,5 s.
`awake_s` = presente→ausente; `sleep_s` = ausente→presente.

**Sinal de tempo fino (GP16) — ✅ FEITO em 06/09.** O pino fica alto durante toda a janela
acordada e baixo dormindo. O **GP2 da PicoHand** está ligado ao **GP16 do alvo**, e o firmware da
mão ganhou o canal `PROBE`: pega carona no laço de 10 kHz que o Core 1 já roda para o `VERIFY`,
guarda só as transições num anel de 64 bordas carimbadas com `micros()`, e responde a
`PROBE STATUS`, `PROBE START` e `PROBE READ` (linhas `EDGE <n> <H|L> <t_us>` terminadas em
`DONE PROBE`). GP4/GP5 seguem sendo a ponte serial — a sonda foi para GP2 para não desativá-la.

Primeira medição, 06/09 ~18h15, ciclo completo:

| janela | medida |
|---|---|
| sono (sem compensação, alarme de 120 s) | **120,715 s** |
| acordado | **29,455 s** |
| sono (compensado) | **89,413 s** |
| **ciclo** | **118,868 s** para 120 s pedidos (−0,94%) |

O caso **T09** da suíte automatiza isso: arma a sonda, hiberna, espera o wake, lê as bordas,
recusa qualquer par de bordas a menos de 5 ms (que seria o glitch do F07) e compara o sono medido
com o alarme. Rodou verde no ferro: `2 edges; asleep=120.705s`.

✅ **O resíduo de ~1 s foi RESOLVIDO em 06/09, e a hipótese acima estava errada.** O que fechou a
questão foi parar de inferir e mandar o **aparelho medir o próprio sono**: depois do `wfi` ele lê
o RTC (que é o relógio que atravessou o sono), guarda os segundos em `scratch[1]` e o boot
seguinte imprime `[AIR] woke: slept=<n>s`. Com isso:

| pedido | dormiu de fato |
|---|---|
| 120 s | **120 s** |
| 91 s | **91 s** |
| 92 s | **92 s** |

Isso revelou o **primeiro** dos dois erros: `wakeSec = sleepMs / 1000` **truncava**, jogando fora
até um segundo inteiro por ciclo e sempre no mesmo sentido (pedir 91817 ms virava 91 s). Trocado
por arredondamento (`(sleepMs + 500) / 1000`).

⚠️ **RETRATAÇÃO — eu escrevi aqui que "o alarme sempre foi exato", e não era.** O autorrelato do
aparelho **também mentia**, pelo mesmo motivo que ele existia para corrigir. Quem pegou foi a
sonda, que é passiva: enquanto o aparelho jurava um ciclo de 119,84 s, a PicoHand media
**119,31 s e 118,69 s** de subida a subida. Instrumento contra instrumento, o passivo ganha.

**A segunda causa, e a raiz de verdade: o `load` do RTC já vale um tique.** A linha de depuração
que o próprio firmware imprime dizia isso o tempo todo, e ninguém tinha lido:

```
[AIR] rtc after set:  2026-01-01 00:00:01 dotw=4
```

O contador é escrito com 00:00:00 e, 3 ms depois, lê **00:00:01**. Um alarme armado em `wakeSec`
estava portanto a `wakeSec − 1` tiques de distância, e o aparelho acordava **um segundo cedo em
todo ciclo**. É por isso que ele reportava `slept=120s` (o valor do alarme) enquanto tinha dormido
119. Com sonda e serial na mesma janela, a conta fechou em **−0,90 s por ciclo**, constante:

| ciclo | `awake=` | `wakeSec=` | período pretendido | período medido | erro |
|---|---|---|---|---|---|
| 1 | 29218 ms | 91 s | 120,218 s | **119,314 s** | −0,904 s |
| 2 | 30592 ms | 89 s | 119,592 s | **118,686 s** | −0,906 s |

**Correção:** armar o alarme relativo ao que o RTC **lê**, não ao zero que se escreveu nele —
`alarmSec = baseSec + wakeSec`. É imune ao mecanismo: se um SDK futuro parar de emitir aquele
tique, `baseSec` vale 0 e o alarme volta a ser onde sempre foi. O autorrelato passou a descontar
a base, então `slept=` virou tempo real de sono em vez do valor do alarme.

**Medido depois, com os dois instrumentos na mesma janela:**

| ciclo | `base=` | `awake=` | `wakeSec=` | período pretendido | período medido | erro |
|---|---|---|---|---|---|---|
| 1 | 1 s | 32120 ms | 88 s | 120,120 s | **120,229 s** | +0,109 s |
| 2 | 1 s | 32240 ms | 88 s | 120,240 s | **120,346 s** | +0,106 s |

O resíduo caiu de −0,90 s para **+0,106 s**, e agora ele é explicável inteiro: é o trabalho entre
a leitura de `millis( )` e o `load` do RTC, mais o boot ROM até o GP16 subir. Contra os 120 s
configurados o erro final é de **+0,23 a +0,35 s** (0,2 a 0,3%), e o que sobra dele é o
arredondamento do meio segundo que a resolução de 1 s do RTC custa.

**Série completa do dia para os mesmos 120 s configurados:** 16–48 min (ROSC parado) → 147 s (ROSC
corrigido) → 118,9 s (ancorado no wake, com truncamento) → 119,3 s (com arredondamento, medido
pela sonda) → **120,23 s** (alarme relativo à base do RTC).

**A lição de método, que vale mais que o número:** o aparelho medindo a si mesmo é um instrumento
como qualquer outro, e herda os vieses do relógio que usa. Foi preciso um instrumento **externo e
passivo** — a sonda, que não toca no alvo — para pegar o viés. E a evidência estava impressa no
console desde o começo: `rtc after set: 00:00:01`. Ler o que o próprio firmware já diz vem antes
de construir instrumento novo.

⚠️ **Regravar a mão reinicia o alvo** (observado: uptime zerado e boot frio logo depois da cópia
do `.uf2`). E pôr a mão em BOOTSEL exige `SELF_BOOTSEL` ou o botão físico — não é automatizável.

**Corrente:** multímetro em série no VSYS (ou INA219 no VBUS) durante um ciclo completo; anotar
sleep, boot, sample, flush. Tabela na seção 6.

---

## 4. Matriz achado × teste

| Teste | Cobre | Estado esperado em `461a806` |
|---|---|---|
| T01 `hand_health` | bancada | PASS |
| T02 `target_boot_m0` | F03 (com VCC no GP16) | PASS sem gating físico; FAIL com gating |
| T03 `air_status_fields` | F14 (`pin=`) | PASS (campo `pin=` é XFAIL) |
| T04 `air_idle_bounds` | F09 | XFAIL (aceita 86400) |
| T05 `hibernate_cycles` | F01, F06, F11, F13, **F22** | XFAIL — em 06/09 o erro de período foi de 6 a 13× |
| `--watch N` (passivo) | F22 | mede acordado/dormindo sem tocar no aparelho |
| T06 `telemetry_drain` | F05, F08 | entrega PASS; janela acordada XFAIL |
| T06b `telemetry_off_sleeps` | F02 | XFAIL (fica acordado) |
| T07 `web_activity_resets_idle` | F21 | XFAIL (hiberna com a web em uso) |
| T08 `offline_timestamps` | F04, F10 | XFAIL (espaçamento ~80 s com `h_int=2`) |
| T09 `gp16_probe` | F03, F07 | SKIP sem extensão da PicoHand |
| T10 `m1_services_off` | F13 | XFAIL (porta 80 aceita em M1) |
| T11 `history_integrity` | F22, F23 | **XFAIL medido 06/09**: 170 registros, 21 no intervalo, 19 para trás |
| C1–C8 (`check_air_consistency.py`) | F15–F19 | FAIL em C1, C2, C3, C4, C5, C8 |

---

## 5. Decisões pendentes (para o Ângelo)

- **D-1** Bluetooth em M1: canal de emergência (bombear em M1, custa tempo de boot e energia) ou "só USB em M1"?
- **D-2** Web em M1: manter fora (D5, recomendado) ou permitir uma janela curta para diagnóstico?
- **D-3** `system ssid/pass` no console de emergência de todas as imagens: manter (recomendado, documentado) ou restringir ao Air?
- **D-4** `air.bin` v3: aceitar perder a config Air existente nos aparelhos de bancada (defaults voltam)?
- **D-5** RTC como relógio de parede (F04): aceita que registros offline fiquem "provisórios" com erro do drift do XOSC (~20 ppm ≈ 1,7 s/dia) até o próximo NTP?
- ~~**D-6** Volta ao ciclo após boot sujo (F25, §6.7)~~ — ✅ **RESOLVIDA em 06/09**: os três caminhos foram combinados (intenção no `air.bin`, graça de 10 s, contador de boots sujos com teto de 3). Ver §6.7.

---

## 6. Registro de execução

Preencher a cada fase (data, commit, resultado da suíte, medições).

| Data | Commit | Fase | Suíte (PASS/FAIL/XFAIL/XPASS) | awake_s médio | sleep mA | acordado mA | Notas |
|---|---|---|---|---|---|---|---|
| 06/09 ~15h | `461a806` | 0 | não rodada (alvo fora do USB) | — | — | — | plano criado; F01 pendente de prova |
| 06/09 ~16h | **`461a806` (procedência provada)** | 0 ✅ | T01 PASS · T11 XFAIL (F23) · T05 XFAIL (erro do instrumento, corrigido) | — | — | — | F22 e F23 confirmados; ver §6.1 |
| 06/09 ~16h30 | `461a806` + **fix do ROSC** | 1 ✅ | `--watch`: sono 110,8 / 120,8 s | 26,4 | — | — | **F22/F01 RESOLVIDOS** — era 16–48 min |
| 06/09 ~16h48 | + **F02, F03, F07** | 1 ✅ | `--watch 620`: 4 ciclos, sono 116,8 / 120,8 / 120,8 / 120,6 s | 28,9 | — | — | alarme 120 s, erro +0,6 a +0,8 s; nada regrediu |
| 06/09 ~16h55 | idem | — | histórico pós-fix: 12 registros, **0 gaps negativos** | — | — | — | **F23 deixa de ser bloqueante** (§6.3) |
| 06/09 ~17h00 | + **F15, F16, F17p, F19** | 3 ✅ | `check_air_consistency` **C1–C8 limpo**; `check_cli_help` OK nos 4 envs | — | — | — | release −712 B, alpha −272 B |
| 06/09 ~17h10 | idem, gravada | 3 ✅ | `--watch 500`: 3 ciclos, sono 112,1 / **120,8** / **120,8** s | 29,9 | — | — | nada regrediu; native 121/121 + 7/7, autoteste 16/16 |

### 6.1 Bancada de 06/09/2026, ~16h — o que foi MEDIDO

Alvo: Pico W `E6642815E34C1824`, firmware reportando `2.3.9-beta`, IP `192.168.3.24`,
RSSI −34 dBm, `hist=120 s` (`h_int=2`), `idle=300 s`, telemetria HTTP para
`192.168.3.206:8080` **funcionando** (`pending=0`), heap livre 95,7 KB, FS 13% usado,
1 sensor ativo, pack pt-BR carregado.

**Procedência do binário: ESTABELECIDA — é o `461a806`** (a ponta da branch). O discriminador
que funciona é mandar `system ssid` **sem argumento**: o binário responde
`ERROR: SSID invalido (1-31 chars, sem ctrl chars)`, que é exatamente a mensagem e a faixa
introduzidas nesse commit; antes dele o comando nem era alcançável com `SIMUT_CLI_FULL=0` e a
resposta seria "Comando desconhecido" (confirmado no mesmo teste com `air pin`, que não existe).
É não-destrutivo: a validação rejeita antes de gravar.

1. **O alvo estava vivo e alimentado o tempo todo, mas ausente do USB por horas.**
   `hand RESET` trouxe-o de volta em **3 s**. Ou seja: ausência prolongada do USB **não** era
   falta de energia nem cabo.
2. **Reset físico entrega M0 — MEDIDO.** A primeira leitura de `air status` depois do pulso de
   RUN, antes de qualquer `air stop`, deu `phase=0`. Confirma o mapa de scratch
   (`src/LogManager.cpp:605`, "zeroed on power cycle / physical reset") e **derruba** a nota
   anterior deste plano, que dizia que o `hand RESET` acordava em M1.
3. **F01 NÃO REPRODUZ — veredito.** O binário gravado é o `461a806`, que **contém** o ROSC-off,
   e o aparelho ciclou sono→wake dezenas de vezes ao longo do dia. Logo, desligar o ROSC antes
   do WFI **é sobrevivível neste silício**: a hipótese de travamento no `runtime_init_clocks`
   está descartada para este alvo. A correção continua recomendada como endurecimento (o
   `sleep_power_up` do pico-extras religa o ROSC, e depender de comportamento não documentado do
   reset é frágil), mas **sai da fila de bloqueantes**.
4. **F22 — o período está errado por 6 a 13× (o achado do dia).**

   A aritmética, para ser auditável. O relógio provisório é semeado no boot com
   `_provisionalBase = getLastRecordedTimestamp() + 60` (`NetworkManager.cpp:108`) e anda com o
   `millis()`; no sync o `NetworkManager` calcula
   `delta = time(nullptr) − (_provisionalBase + uptime_s)` e o `AppManager::handleTimeSync`
   grava esse **delta em segundos** no contexto do código 408
   (`AppManager_Core.cpp:91`, saturado em int16). Como o `time()` real vale
   `ultimo_registro + ciclo_real`, sai `ciclo_real = delta + 60 + uptime` — e o uptime no sync
   é ~20 s pelo próprio log:

   | wake | ctx do código 408 | ciclo real = ctx + 80 |
   |---|---|---|
   | 1 | +1565 s | 27,4 min |
   | 2 | +783 s | 14,4 min |
   | 3 | +690 s | 12,8 min |
   | 4 | +1386 s | 24,4 min |
   | 5 | +723 s | 13,4 min |

   Boots consecutivos no log ficam 17–29 min apart, batendo com a tabela por outro caminho. O
   pedido era 120 s e o teto de backoff é 300 s (`BACKOFF_MAX_MS` em `TelemetryManager.h:165`),
   então **nenhuma configuração explica o observado**. O histórico grava um registro por wake
   (o `AIR_DECIDE` chama `processHistoryLogging()` sem gate), então o ciclo medido aqui é o
   ciclo de amostragem do produto.
5. **F13 confirmado no ferro**: todo wake M1 registra `WEB_SERVER_STARTED ctx=80` e
   `APP_CACHE_PRELOAD_DONE` (ctx 83→88, subindo 1 por wake) — a web sobe e o preload de gráfico
   varre blocos em cada acordada, exatamente o que a D5 mandava não fazer.
6. **F04 quantificado**: as mesmas correções de +690 a +1565 s são o erro que um wake **offline**
   gravaria no histórico, porque aí não há NTP para corrigir.
7. **A prova direta do F22 — o arquivo de histórico.** `GET /download?file=/history/20260906.h5`
   (2.116 B) decodificado com `tools/history_v5.py`: **168 registros no dia inteiro**, onde um
   intervalo de 2 min renderia 720. Os 25 últimos, com o gap em segundos:

   ```
   14:07:44   14:08:44 +60   14:09:44 +60   14:09:27  −17   14:39:44 +1817
   14:40:44 +60   14:41:44 +60   14:41:31  −13   14:58:50 +1039
   14:59:50 +60   15:00:50 +60   15:00:33  −17   15:16:18  +945
   15:17:18 +60   15:18:18 +60   15:18:01  −17   15:45:21 +1640
   15:46:21 +60   15:47:21 +60   15:47:05  −16   16:03:23  +978
   ```

   ⚠️ **RETRATAÇÃO PARCIAL, e ela importa.** A primeira leitura deste arquivo foi feita com o
   intervalo nominal **errado**: `history_v5.read_series()` assume 60 s por default e o aparelho
   usa 120 s (`h5NominalSeconds(h_int=2)`). O V5 codifica cada registro como desvio do passo
   nominal, então decodificar com o nominal errado **reescreve todos os tempos interiores** —
   foi isso, e não o firmware, que produziu as "rajadas de 3 registros a exatos 60 s" e boa
   parte dos gaps negativos que a primeira versão desta seção reportou. Números corretos, com
   nominal 120 s:

   | decodificado com | registros | no intervalo | curtos | longos | para trás |
   |---|---|---|---|---|---|
   | 60 s (errado) | 175 | 21 | 77 | 56 | 20 |
   | **120 s (certo)** | **175** | **83** | **30** | **49** | **12** |

   O que **sobra** de real depois da correção, e é o F23: **12 gaps negativos** — o arquivo
   ainda não é monotônico. A causa é estrutural: o interior de cada bloco é reconstruído pelo
   passo nominal, então um bloco cujas amostras reais foram mais espaçadas "avança" além do
   `t0` do bloco seguinte e produz um retrocesso na leitura.

7b. **A leitura que decide o F22: as âncoras de bloco.** Cada chunk DATA carrega seu próprio
   `t0` absoluto, imune ao nominal. Lendo só os `t0` do dia:

   ```
   11:16:18  11:18:45  11:21:16  11:23:45  11:26:13  11:28:40  11:31:08  11:33:35
      gaps:   147  151  149  148  147  148  147   (segundos)
   ```

   **O ciclo do Air estava CORRETO de manhã**: 147–151 s = 120 s de sono + ~28 s acordado, com
   `h_int=2`. Antes disso, por volta das 10h40, os gaps eram de 87–88 s — o mesmo ciclo com
   `h_int=1`. A degradação começa perto das **12h15**: daí em diante os gaps de bloco viram
   2642, 2915, 1920, 1146, 1048, 1743 e 1082 s.

   **Portanto o F22 é uma REGRESSÃO, não um defeito de nascença do desenho** — e a janela em que
   ela aparece é a mesma em que a build nova foi gravada (o `.uf2` da árvore tem mtime 13h04 e o
   alvo sumiu do USB a partir das 12h28). O único candidato nessa build que mexe em clock é o
   **ROSC desligado antes do WFI**, que era o F01. Ou seja: o F01 não era inofensivo, apenas
   falhava de um jeito diferente do previsto — em vez de travar de vez, faz o wake demorar um
   tempo longo e variável.

**Lição de instrumento (registrada porque custou uma execução):** a primeira rodada da T05 falhou
com "serial vanished before the alarm line". Não era o firmware — era a suíte: `cmd()` lia por
2 s e descartava o transcrito, e um aparelho com sensores já estáveis vai de `air hibernate` ao
`[AIR] alarm:` em menos de um segundo. Corrigido escrevendo o comando e lendo um único fluxo.
Daí também nasceu o modo `--watch`, que mede o ciclo **sem tocar** no aparelho.

**Três instrumentos que mentiram nesta bancada — registrados para não custarem de novo:**

1. **O `help` NÃO discrimina firmware.** Num aparelho com pack não-inglês o console de
   emergência serve o `@HELP` do `.lng` do LittleFS (mudança da v2.3.7-beta), que sobrevive à
   gravação de firmware. O `help` do alvo não lista `system ssid` nem os comandos `air`, e ainda
   assim o binário é o `461a806` e os comandos **funcionam**. Discriminar por comportamento
   (`system ssid` sem argumento), nunca por texto de ajuda.
2. **`/api/status` reporta `uptime` em MILISSEGUNDOS.** Ler 84.494 como segundos dá 23,5 h num
   aparelho que tinha 84 s de vida. Bate exatamente com `millis()`.
3. **O ModemManager está ativo nesta máquina** e sonda todo `ttyACM` recém-enumerado. Logo
   depois de um reset a primeira sessão serial pode morrer com *"device reports readiness to
   read but returned no data"* sem que o alvo tenha feito nada. Reabrir e seguir; se incomodar,
   regra de udev com `ID_MM_DEVICE_IGNORE`.

### 6.2 Validação da Fase 1 — 06/09/2026, ~16h30

**Fix do ROSC, medido isolado** (build = `461a806` + o religamento do ROSC, gravada pela
PicoHand porque o `picotool` sozinho não achou o alvo em BOOTSEL). `air hibernate` pela serial,
alarme `00:02:00 wakeSec=120`, e o ciclo medido por `--watch` (passivo, enumeração USB):

| ciclo | dormindo | acordado |
|---|---|---|
| 1 | 110,8 s * | 26,5 s |
| 2 | **120,8 s** | 26,3 s |

\* o primeiro valor é parcial: a observação começou com o aparelho já dormindo.

**Com as três correções da Fase 1 gravadas, quatro ciclos seguidos** (`--watch 620`):

| medida | ciclo 1 | ciclo 2 | ciclo 3 | ciclo 4 |
|---|---|---|---|---|
| dormindo | 116,8 s * | **120,8 s** | **120,8 s** | **120,6 s** |
| acordado | 33,0 s | 26,5 s | 26,5 s | 29,5 s |

\* parcial pelo mesmo motivo. Alarme pedido: 120 s. **Erro dos três ciclos completos: +0,6 a
+0,8 s.**

**De 16–48 min para 120,7 s ± 0,1.** O ciclo total (~147–150 s) bate com os 147–151 s que a
mesma bancada tinha de manhã, antes da build que desligou o ROSC — ou seja, o comportamento
voltou ao conhecido-bom, e não para um valor novo qualquer. Isso fecha F22 e F01 de uma vez, e
mostra que F02/F03 não regrediram nada.

**Fases do ciclo, com a build final** (`air hibernate` → dormindo, lido do console):

```
WARMUP @21378  SAMPLE @21772  DECIDE @35787  CONNECT @35868  FLUSH @35868  SLEEP @36013
```

Ou seja: warm-up 394 ms (o valor certo — o F06 nunca afetou o caminho por comando), amostragem
14,0 s, decisão 81 ms, **FLUSH 145 ms** (fila vazia, sem travar) e entrada em sono 145 ms depois.
Janela acordada total ~26 s.

**Portões de host, com as três correções:** `pico_w_air` 97,7% de flash (+200 B),
`pico_w_release` 97,4% e `pico_w_alpha` 97,5% **inalterados**, `native_air` 7/7,
autoteste da suíte 16/16.

### 6.3 F23 depois do fix — a monotonicidade voltou sozinha

Medido às ~16h55, com o período já correto, baixando o mesmo arquivo do dia e olhando **só os
registros posteriores ao fix** (≥ 16:30):

| recorte | registros | gaps para trás |
|---|---|---|
| dia inteiro (inclui a manhã quebrada) | 188 | 12 |
| **após o fix** | **12** | **0** |

Tempos pós-fix: 16:31:09, 16:31:24, 16:33:51, 16:36:18, 16:38:20, 16:38:35, 16:41:02, 16:43:36,
16:44:54, 16:48:31, 16:51:01, 16:53:38. Nenhum retrocesso.

Confirma a previsão que este plano já registrava: os gaps negativos vinham de blocos cujo
interior era reconstruído pelo passo nominal enquanto as amostras reais estavam muito mais
espaçadas. Com o wake no intervalo, a reconstrução volta a bater com a realidade. **O F23 deixa
de ser bloqueante**; o endurecimento (selar o bloco antes de dormir) continua valendo para o caso
de o período voltar a escorregar, mas não segura mais nada.

⚠️ **O que sobrava:** o intervalo real entre registros era ~147 s com `h_int=2` (120 s), porque o
alarme era ancorado no instante em que o aparelho **dorme**, não no instante em que **acordou** —
o período efetivo virava `h_int + janela acordada`, ~22% de amostras a menos por dia.
✅ **Corrigido em 06/09** — ver §6.4.

### 6.4 Intervalo real: alarme ancorado no wake

O alarme passa a ser `h_int − (tempo que este wake já passou acordado)`. Como um wake do M1 **é**
um boot, esse tempo é exatamente `millis( )` no instante de dormir, e a subtração faz o período
boot-a-boot valer o intervalo configurado.

Três detalhes que o desenho precisou tratar:

- **Só quando o boot foi mesmo um wake.** Depois de um boot frio — ou de um `air stop` que
  devolveu o aparelho ao operador — `millis( )` mede tempo de bancada, não de ciclo, e não há
  wake anterior a que ancorar. Daí o `_airWokeFromSleep`, separado do `_airActive` (que o
  `airStartHibernate( )` também liga).
- **O backoff não é compensado.** `getBackoffRemainingMs( )` já conta a partir de agora, e
  encurtar uma punição derrotaria o propósito dela. A compensação vale só para a cadência.
- **Piso de `AIR_MIN_SLEEP_SEC` (5 s).** Se um wake durar mais que o próprio intervalo, a
  subtração pediria zero e o aparelho entraria em boot-dorme-boot. Com o piso ele degrada para
  "o mais rápido que dá" e a linha do log ganha o sufixo `OVERRUN`, que é o sinal de que a
  cadência configurada não cabe no trabalho do wake.

A linha do alarme agora carrega a conta inteira, para a bancada não precisar inferi-la:

```
[AIR] alarm: 00:02:00 wakeSec=120 awake=39394ms target=120000ms
```

**Medido no ferro logo em seguida** (`--watch 560`, `h_int=2`):

| ciclo | dormindo | acordado | período |
|---|---|---|---|
| 1 — boot frio, **sem** compensar | 114,3 s | 26,8 s | 141,1 s |
| 2 | **91,8 s** | 26,8 s | **118,6 s** |
| 3 | **91,8 s** | 26,8 s | **118,6 s** |
| 4 | **91,8 s** | 27,0 s | 118,8 s |

O primeiro ciclo é o boot frio, que por desenho não compensa (não há wake anterior a que
ancorar) — e ele serve de controle: 141 s, o comportamento antigo. Os três seguintes ficam em
**118,6 s de período observado** contra 120 s configurados, com o tempo de sono repetindo
**91,8 s nos três**. ⚠️ Os ~1,4 s que faltavam para 120 **não** eram o atraso da
enumeração USB, como esta seção afirmou primeiro: eram truncamento na conta do alarme.
Ver §3, onde o aparelho passa a medir o próprio sono, a sonda pega o viés desse autorrelato e o
ciclo fecha em **120,23 s**.

**Série completa do dia, para a mesma configuração de 120 s:** 16–48 min (ROSC parado) → 147 s
(ROSC corrigido) → **118,6 s** (alarme ancorado no wake).

**Pendências desta bancada:** exercitar o caminho `telInterval == 0` do F02 (T06b); confirmar se
o sensor da bancada está mesmo chaveado pelo GP16 (nesta bancada o DS18B20 está no GP0, sem
chaveamento, então o F03 não é exercitado aqui); medir corrente.

### 6.5 O cursor de telemetria — duas causas, uma em cada lado

Sintoma relatado pelo Ângelo: *"o cursor não está avançando e o mesmo pacote é reenviado quando o
simut-air acorda"*. A investigação achou **duas causas independentes**, e as duas produzem
exatamente esse sintoma.

**Causa 1 — firmware (corrigida, F24).** `flushCursorIfDirty( )` adia a escrita por
`CURSOR_COALESCE_MS` (5 s). No ciclo M1 o envio marca o cursor sujo e a fase FLUSH sai ~150 ms
depois, com a fila drenada: a janela nunca decorre, o sono perde a SRAM e o boot relê o arquivo
antigo. Medido antes: `pending=8`, subindo 1 por ciclo. Depois do fix: 12 ciclos renderam +5, ou
seja, a subida 1:1 parou.

**Causa 2 — o coletor, e ela é a dominante.** O endpoint `192.168.3.206:8080/telemetry` **aceita
a conexão TCP e nunca responde**. Medido desta máquina, fora do aparelho:

```
$ curl -X POST --max-time 20 http://192.168.3.206:8080/telemetry
http=000  conectou=0,0035s  primeiro_byte=0,000000s  total=20,002s   (rc=28, timeout)
```

O log do aparelho diz o mesmo pelo lado dele: `[ERR][TEL] code=31 ctx=-11` (SYS_TEL_FAIL com
`HTTPC_ERROR_READ_TIMEOUT`) seguido de `[WRN][TEL] code=32` (retry), três vezes com backoff
crescente. **O firmware está certo**: um envio sem resposta é um envio não confirmado, e o
store-and-forward existe justamente para não avançar o cursor nesse caso. O resultado correto é
reenviar o mesmo lote.

**Prova cruzada.** Apontando o mesmo aparelho, sem tocar em mais nada, para um coletor que
responde 200 na hora (`tools/air_test_suite.py` sobe um em `:8010`):

| coletor | pending |
|---|---|
| `.206:8080` (não responde) | 8 → subindo |
| local `:8010` (responde 200) | **9 → 1 em 3 ciclos** |

A configuração original foi salva, restaurada e conferida ao fim do teste.

**O que falta fazer, e é do lado do servidor:** o endpoint precisa devolver uma resposta HTTP
completa (qualquer 2xx serve) e prontamente. Enquanto ele só aceitar a conexão e calar, o
aparelho vai continuar reenviando o mesmo lote — por desenho.

### 6.6 SSID indisponível: tentativas limitadas e sono imediato

Pedido do Ângelo: com o SSID fora do ar, no máximo **2 tentativas**, e assim que a leitura e a
gravação do histórico terminarem, hibernar — tentando de novo no wake seguinte.

**Implementado:** `AIR_MAX_CONNECT_ATTEMPTS` (default 2). A fase SAMPLE só bombeia o
`NetworkManager` enquanto `getConnectCycles( ) < AIR_MAX_CONNECT_ATTEMPTS`; estourado o limite,
para de bombear pelo resto do wake, registra no console, e o DECIDE trata o wake como offline
mesmo que o link apareça depois — os dados já estão na flash e o próximo wake envia. Como cada
wake é um boot novo, o contador zera sozinho e a tentativa recomeça.

**Medido no ferro com o SSID errado** (`ProcrastinationPLUS2`, posto pelo Ângelo justamente para
isso), três wakes seguidos:

| wake | janela acordada |
|---|---|
| 1 | 26,7 s |
| 2 | 26,4 s |
| 3 | 26,2 s |

Com o SSID **certo** a mesma janela é de 26,3 a 29,5 s. Ou seja: **a rede ausente não alarga o
wake em nada** — o que fecha a fase é a estabilização dos sensores, o histórico é gravado e o
aparelho dorme.

⚠️ **Honestidade sobre o limite:** o teto de 2 é um **teto**, não o caso comum. Cada tentativa
custa até 20 s dentro do `NetworkManager`, e o wake inteiro dura ~28 s, então na prática **uma**
tentativa começa por wake e o limite não chega a disparar (a linha `[AIR] wifi: no link after…`
não apareceu em nenhum dos três wakes). Ele existe para o caso de uma janela acordada longa —
sensores lentos, `stabTimeoutMs` grande — em que o segundo ciclo de reconexão caberia.

### 6.7 F25 — um reset durante o wake tirava o aparelho do ciclo ✅ CORRIGIDO

**Achado novo, e é o pior modo de falha visto até agora nesta build.** Descoberto às 20h30 de
06/09, com o SSID errado ainda configurado, quando uma medição encontrou o aparelho **em M0,
acordado, varrendo a rede** — e não no ciclo em que ele estava meia hora antes.

**O mecanismo é deliberado**, e está escrito em `src/AppManager_Boot.cpp:132`: o marcador de
hibernação é lido e **zerado em todo boot**, "so a watchdog reset during the cycle does not
re-enter M1". A válvula de segurança existe por um bom motivo — um aparelho que trava em M1 fica
inalcançável, acordando e travando para sempre. O preço, porém, é este: **um watchdog durante a
janela acordada devolve o aparelho a M0**, com rádio ligado e consumo cheio, e a única volta para
o ciclo é o timeout de inatividade (`air idle`, 300 s por padrão).

**Na bancada, essa volta nunca aconteceu.** Com o SSID inexistente, o Core 0 travava de novo antes
dos 300 s. Do `show system log`: 72 boots, 6 FATAL, quatro deles no trecho final. As causas são
`ctx=455` (= 200 + 0xFF, "HW WATCHDOG: Core 0 loop stalled, trace channel empty") e `ctx=209`
(= 200 + módulo 9). Entre resets o aparelho ficou acordado 54, 58, 86 e 107 s — sempre menos que
os 300 s de que precisava para voltar a dormir. **Resultado: acordado para sempre, que é
exatamente o que a build Air existe para evitar.**

⚠️ **Um boot que dura mais que o `air idle` não prova que a válvula funciona:** todo comando da
CLI chama `airMarkActivity( )` e rearma o timer. O boot de 6m18s que apareceu aqui foi mantido
acordado pelas MINHAS consultas, não pelo firmware.

✅ **CORRIGIDO em 06/09 ~22h**, combinando os três caminhos que a decisão D-6 listava — e nenhum
deles sozinho bastava:

1. **A intenção do operador virou estado de flash.** `AirConfig.flags` bit 0
   (`AIR_FLAG_CYCLE_ARMED`) diz "o ciclo está armado", escrito por `air hibernate` e apagado por
   `air stop`. O marcador do scratch responde outra pergunta — "acabei de acordar?" — e continua
   sendo zerado em todo boot, de propósito. **Sem bump de versão do `air.bin`:** o campo `flags` já
   existia como reservado, e um arquivo v2 lê 0, que significa "não armado" — o default seguro.
2. **Graça curta para voltar.** Se o boot não é um wake mas o ciclo está armado, o timer de
   inatividade do M0 passa a valer `AIR_RESUME_GRACE_SEC` (10 s) em vez dos 300 s configurados.
   Dez segundos porque, enquanto ele espera, o aparelho está acordado com rádio ligado — o estado
   que esta build existe para evitar.
3. **Guarda de laço de crash.** `flags` bits 4..7 contam boots sujos consecutivos
   (`LogManager::bootWasClean( )`, novo: falso só para watchdog e soft panic — reboot pedido pelo
   próprio firmware, incluindo o wake do Air, carrega a marca de `markCleanReboot`). Um boot limpo
   zera a conta; a partir de `AIR_MAX_DIRTY_BOOTS` (3) a graça volta a ser o `air idle` inteiro e o
   log ganha `APP_AIR_CYCLE_HELD` (411). **A janela larga passa a ser merecida**, e não o padrão.
   Escrita em flash só quando o número muda: a operação saudável não toca no `air.bin`, e um laço
   de crash gasta no máximo uma escrita por boot sujo até o teto.

`air status` passou a mostrar `armed=` e `dirty=`, porque sem eles `phase=0` fica idêntico para
"o operador parou o ciclo" e "um watchdog derrubou o aparelho para fora dele".

**Teste T12 da suíte** (`cycle_survives_reset`): arma o ciclo, confirma um wake, reseta pela
PicoHand no meio da janela acordada e **não manda mais nenhum comando** — porque todo comando
chama `airMarkActivity( )` e rearma o timer, ou seja, perguntar ao aparelho se ele voltou a
dormir é exatamente o que o impede de dormir. O veredito vem da enumeração USB: ausente = dormindo.

**Pendente de investigação separada:** a travada do Core 0 em si, sob varredura de SSID
inexistente. É parente do R1 histórico ("Core 0 na varredura"), e o `ctx=455` diz que nem o canal
de rastreio de módulo sobreviveu.
