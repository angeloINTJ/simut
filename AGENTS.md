# AGENTS.md — Manual operacional da bancada

Instruções persistentes para quem trabalha neste repositório com o hardware na
mesa. Leia antes de gravar firmware ou de medir qualquer coisa. Aqui fica **só o
que ainda vale**: a história de como cada regra foi descoberta — os achados
numerados, as retratações, as medições do dia — está no `CHANGELOG.md` e em
`docs/analysis/`, e este arquivo aponta para lá em vez de repetir.

Todo número citado aqui foi medido e traz a data. Se o código deixar de bater
com uma regra, a regra está errada e deve ser corrigida aqui.

## 1. A bancada

Um Pico W (o **alvo**, que roda o SIMUT) e um Pico comum (a **PicoHand**, ou
"mão") que aciona as linhas RESET (GP0) e BOOTSEL (GP1) do alvo por comando
serial, mede a sonda e finge o carregador. O rig está em `192.168.3.24`.

**Identifique as portas pelo serial, nunca pela ordem de enumeração:**

| Placa | USB | Serial (udev) |
|---|---|---|
| PicoHand (mão) | `2e8a:000a` | `ID_SERIAL_SHORT=E660C062131E3E27` |
| Alvo SIMUT (Pico W) | `2e8a:f00a` | `ID_SERIAL_SHORT=E6642815E34C1824` |

**Fiação real** (confirmada em 06/09/2026): DS18B20 no GP0, sem chaveamento de
energia — a linha GP16 do alvo vai ao GP2 da mão como **sonda** de
acordado/dormindo, não como power-gating; o GP3 da mão vai ao GP17 do alvo e
**finge o carregador** (3,3 V direto, sem o divisor da placa real). Detalhes,
comandos e o analisador lógico: `tools/PicoHand/MANUAL_CLAUDE_CODE.pt-BR.md`.

**O alvo tem um painel TFT ligado** (ILI9341 + XPT2046, pinagem do
`simut_config.h`), o que esta lista não dizia até 22/09/2026 — `/api/screenshot`
devolve o dashboard renderizado e `POST /api/touch` dirige o painel. Vale para
validar UI de verdade; o que **não** dá para exercitar de fora é o PENIRQ, que
nenhum fio da mão alcança. O contorno é uma imagem só de bancada com
`-DTOUCH_IRQ=17 -DTOUCH_CS=21`: o PENIRQ passa a ser a linha CHARGER da mão, e
`CHARGER OFF` (nível baixo) é "dedo na tela". Foi assim que o gesto de AP do
boot foi medido nos quatro casos em 22/09.

### Gravar firmware

**Nunca peça ao usuário para resetar o Pico à mão.** A mão existe para isso.

```bash
source tools/PicoHand/pico_hand.sh
hand_init                                  # detecta a mão (PING/PONG)
trap hand_release_all EXIT

picotool load -x <firmware.uf2> || {       # caminho normal: o alvo ainda atende USB
    hand BOOTSEL                           # recuperação: força BOOTSEL pela mão
    sleep 2
    picotool load -x <firmware.uf2>
}
hand_release_all
```

- Prefira o toque de 1200 bps (`pio run -t upload`) a `picotool -f`: com dois
  RP2040 no barramento o picotool pega o primeiro que acha — a mão, que não tem
  interface de reset — e `--ser` não salva, porque em BOOTSEL a placa enumera com
  outro serial. `tools/air_test_suite.py --flash` faz o toque primeiro e só cai
  para a mão se ele falhar.
- **Regravar a mão reinicia o alvo.** BOOTSEL da mão exige `SELF_BOOTSEL` ou o
  botão — não é automatizável.
- `hand RESET` aciona o pino RUN: é reset global do chip, dá boot frio (M0 no
  Air) e restaura os clocks. Serve para recuperar; **não serve de prova** de que
  o caminho do sono funciona (ver §3).

### Dirigir o painel de fora

- **Sequências de toque vão por `POST /api/touch`, não por `touch sim`.** Um
  toque arma 5 s de prioridade do painel, e nessa janela a CLI enfileira no
  máximo **dois** comandos e descarta o resto (`CLI ocupada (display em uso)`).
  Um PIN de quatro dígitos digitado pela CLI perdeu dígitos em 19/09; pela web
  não há fila. `screen <tag>` continua servindo para saltar de tela — e desde a
  v24 abre a árvore de configuração como admin, porque a lista é filtrada pelos
  bits da sessão e um `screen set` sem sessão mostrava três itens.
- **`/api/screenshot` responde 503 nos 5 s após um toque**, inclusive para
  a sessão web que injetou o toque (medido em 19/09: 503 até ~3 s depois; a
  exceção vale só para o fluxo do espelho, `/api/screen_stream`). Capture
  6 s depois do último toque. Log binário e linha de alarmes também esperam a
  janela: registros de log pendentes só vão à flash no release do toque, e a
  telemetria não roda enquanto o painel está "em uso" — um registro chega ao
  coletor até ~30 s depois da ação (retry da linha = 15 s). Esperar isso entre
  toques bate no guarda de 30 s ociosos, que devolve o painel ao dashboard:
  faça as ações, depois confira (`tools/panel_users_hw_test.py`).
- **Desde a v25, Configurações abre o SELETOR DE CONTA, não o teclado**
  (`MODE_AUTH_USER`, UI mode 27). O fluxo é dashboard → CFG → escolher a conta
  → teclado (UI mode 5). Três armadilhas medidas em 20/09: escolher a conta
  por ÍNDICE erra num rig que tem contas que o script não criou (use o NOME —
  `Rig.pick_user('nome')`); um PIN RECUSADO deixa o teclado na tela e SAIR dele
  volta ao dashboard, não ao seletor (`Rig.reopen_picker( )`); e com mais de
  oito contas as setas saltam PÁGINA, então a linha alvo não é a selecionada e
  precisa de DOIS toques — um só deixa o seletor no vidro e o teclado nunca
  abre ("keypad not on screen (got 0 faces)").
- ⚠️ **Nem todo comando roda dentro de `configure terminal`.** `tel …` e
  `alarm set …` são de CONFIGURAÇÃO; **`sensor <slot> <campo> <valor>` é EXEC
  privilegiado** e, dentro do `configure terminal`, responde "Comando requer
  modo privilegiado" e **não muda nada**. Como ninguém lê a resposta, a falha é
  silenciosa: em 20/09 um `sensor 0 alarm on` por `Rig.cfg( )` devolveu OK e
  deixou o alarme desligado, e só apareceu ao conferir o `/api/alarms` depois.
  **Confira o efeito pela API, não pelo retorno do comando.**
- 🔴 **`active` do `/api/alarms` é o bit de ALARME LIGADO** (`alarmsActive`,
  `WebManager_Api.cpp:393`), **não** "o sensor existe". A lista de alarmes do
  painel é montada de `sensors[i].active` (sensor CONFIGURADO,
  `DisplayManager_Settings.cpp:86`). Como o `/api/alarms` só emite os
  configurados, **em ordem de índice, a resposta JÁ É a lista do painel: o
  elemento `k` é a linha `k`** — não filtre nada. Filtrar por `active` deixou
  as duas bancadas apontando para o sensor 4 enquanto tocavam a linha 0 (sensor
  0), e o `ctx=500` correto do log foi lido como defeito por uma tarde.
- 🔴 **O teto de falhas do painel só o BOOT limpa.** `PinKb::PANEL_FAIL_CEILING`
  (20) falhas ligam `_permanentLockout`, e daí toda visita às Configurações
  repinta "Tentativas Excedidas" em vez de abrir o seletor. Um login
  bem-sucedido zera `_failedAttempts` e `_failBySlot`, mas **nunca**
  `_permanentLockout` nem `_slotLocked` — é de propósito. Uma suíte inteira
  gasta ~3 PINs errados, mas **re-execuções parciais acumulam**: em 20/09 sete
  verificações falharam sem NENHUM registro no log porque todo toque caía na
  tela de bloqueio. `step_prep` começa por `Rig.reboot( )` desde então.
- **O menu do sensor abre na primeira linha que a conta PODE usar**, então
  `activate_row(1)` só acerta "bloquear" para quem tem mais de um bit. Uma
  conta com um bit só já abre nessa linha.
- **O teclado do PIN é embaralhado: descubra o sorteio antes de tocar.** O
  alfabeto é distribuído em cartões a cada abertura da tela (e de novo após um
  PIN recusado e entre as duas digitações de um PIN novo), então nenhuma
  coordenada é estável — **quando o teclado é sorteado**. ⚠️ **Desde a v25 o
  NÚMERO de cartões depende da política**: dígitos com 3 glifos dá 4 cartões
  (10 dígitos + 2 de enchimento), e as outras dão 6, 12 ou 18 (`PinKb::GRIDS`).
  Um script que assume quatro só funciona sob uma política — em 20/09 o
  `_card_of` procurava em `range(4)` e anunciou "'5' is on no card" imprimindo
  uma lista cujo último cartão era `'56'`. **Leia a grade e a política de
  `GET /api/keypad`** (`grid` = keys, slots, cols, rows, x, y, w, h, pitchX,
  pitchY; `policy` = minLen, maxLen, teclado, alfabeto), nunca de constantes.
- 🔑 **Com 1 glifo por tecla NÃO há sorteio**: o teclado é ORDENADO, porque um
  conjunto de um não esconde nada de quem lê o vidro e embaralhar só custa a
  memória muscular do operador. O campo **`kb`** do `/api/keypad` diz qual
  está no vidro e **a interação de cada um é diferente**:
  | `kb` | o que é | toques por caractere |
  |---|---|---|
  | `cards` | cartões sorteados, **re-sorteados a cada toque** | 1 (o cartão) |
  | `num` | pad numérico 1..9/0, duas células VAZIAS | 1 |
  | `groups` | 9 grupos (`0-9 ABC … WXYZ`) + popup | **2** (grupo, caractere) |
  ⚠️ Num teclado ordenado **não espere re-sorteio** (`fresh_faces` esperaria
  para sempre) e **não descarte faces vazias** — elas são posicionais, e o pad
  numérico tem duas. A geometria do popup vem em **`pop`** = keyW, keyH, gap,
  y(1 linha), y(linha 0), y(linha 1); uma linha de `m` teclas começa em
  `(320 - (m*keyW + (m-1)*gap)) / 2`.
- **A tela de ESCOLHER um PIN é sempre ordenada**, qualquer que seja a
  política — escolher não é o problema que o embaralhamento resolve. Ela também
  responde ao `/api/keypad` agora, então `pin_exact( )` lê a geometria como
  todo o resto em vez de carregar a sua cópia do pad numérico.
  `show display keypad` imprime os cartões em ordem de sorteio — só
  responde com o teclado na tela. ⚠️ **São DUAS telas**: ao IDENTIFICAR, os cartões
  sorteados, e o cartão inteiro é um botão (`Rig.pin( )` toca no centro do
  cartão que contém o dígito); ao DEFINIR um PIN, um teclado numérico COMUM de
  posições fixas (`Rig.pin_exact( )`, que não lê sorteio nenhum). Trocar os
  dois faz a entrada virar outro PIN sem erro nenhum. ⚠️ O rodapé mudou em
  19/09: ⌫ / SAIR / ENTRAR nos rects padrão (y=195, h=40) e sem o botão da
  licença — as coordenadas antigas caem no lugar errado. ⚠️ **O sorteio muda a cada TOQUE**, então é uma leitura
  por dígito. **Leia por `GET /api/keypad`, não pela serial.** O mesmo sorteio
  sai em 0,01 s contra 1,2 s do `show display keypad`, que ainda espera a
  janela de 5 s da prioridade do toque; um PIN de 4 dígitos leva 4,6 s contra
  31,1 s (medido 19/09, 32 contas). Pela serial um PIN de 8 toques passa dos
  30 s de ociosidade e o painel volta ao dashboard no meio da digitação — foi
  o que matou as duas primeiras rodadas de tabela cheia, nas contas 18 e 21
  de 25. `Rig.keypad_faces( )` usa o HTTP; `keypad_faces_cli( )` guarda o
  caminho serial para imagem sem servidor web.
  ⚠️ Identificar com 8 toques bloqueia o Core 0 por ~360 ms (medido 19/09:
  449 ms de pior resposta HTTP contra 91 ms ocioso) — uma requisição web que
  caia nessa janela simplesmente espera.
- **Captura que mostra a tela ANTERIOR com o modo já trocado não é o
  instrumento mentindo** — era o Core 1 perdendo o pedido de repintura que
  chegava durante um desenho (7 de 7 logins em 19/09 deixavam o teclado no
  vidro com `show metrics` já em `UI mode: 6`). Corrigido no despacho de
  `DisplayManager.cpp`; se voltar, compare o modo do `show metrics` com a
  captura antes de acusar o fluxo.
- **O coletor da linha de alarmes é um processo à parte**
  (`tools/alarm_collector.py`): dentro do script de teste ele não estava no ar
  no boot do aparelho, e a primeira tentativa falhada empurrava o retry para
  depois da espera.

### Quem mais está na porta

- **O monitor serial do Arduino IDE rouba a porta da mão** e, com ela, a sonda e
  o caminho de recuperação por BOOTSEL. `fuser -v /dev/ttyACM*` mostra quem
  segura. Nunca mate o processo do usuário; espere ou peça.
- **O ModemManager está ativo nesta máquina** e sonda todo `ttyACM` recém
  enumerado: a primeira sessão serial depois de um reset pode morrer com
  *"device reports readiness to read but returned no data"* sem culpa do alvo.
  Reabrir resolve; `ID_MM_DEVICE_IGNORE` no udev evita.
- **Todo comando na CLI rearma o timer de inatividade do alvo**, e no Air isso é
  o que o impede de dormir. Para medir período ou sono, não fale com ele: use a
  sonda (`PROBE START|READ` na mão) ou `tools/air_test_suite.py --watch`, que só
  observa a enumeração USB.

## 2. Portões antes de um push

O CI cobre tudo isto, mas só em pull request — meça antes:

```bash
pio run -e pico_w_release -e pico_w_test -e pico_w_test_https \
        -e pico_w_asserts -e pico_w_alpha -e pico_w_air
pio test -e native -e native_history_v5 -e native_cli -e native_logpolicy \
         -e native_alarmqueue -e native_network -e native_air
./tools/run_fuzz.sh                       # 60 s; NÃO está no pio test e já pegou defeito real
python3 tools/check_air_consistency.py
python3 tools/check_flash_budget.py <env> build.log   # o CI roda assim; local, leia a linha "used"
```

- **`pico_w_test_https` é a imagem de bancada para HTTPS**, e a única com CLI
  completa e servidor TLS juntos: o servidor só é compilado no `pico_w_release`,
  cujo console de emergência não cria o usuário descartável que as suítes web
  usam para entrar — e resetar a senha do admin do rig para conseguir um não é
  caminho. Não é imagem de campo e a `release-ota.yml` não a publica.
- O `pio run` já roda os portões de fonte como *extra scripts*: `-Werror` em
  `src/`, códigos de log, packs de idioma, matriz de autorização, ajuda da CLI,
  sondas de flash em SRAM.
- **`tools/flash_budget.json` desce na mesma mudança que encolhe a imagem.** Um
  orçamento que só sobe deixa de ser marca d'água. Compare o `.bin`, não o
  `used` do PlatformIO (soma de seções, ~12 kB abaixo).
- `PLATFORMIO_BUILD_FLAGS` muda o checksum do projeto e **apaga `.pio/build`
  inteiro**, todos os ambientes. Cada experimento por flag é uma build completa.
- O `zopfli` é opcional (`pip install zopfli`): sem ele as páginas web caem para
  `gzip -9` e a imagem fica 2.888 B acima do orçamento, dentro da margem.

## 3. SIMUT Air — o build que hiberna

`pio run -e pico_w_air`. Em 18/09/2026, com a CLI completa ligada:
**1.023.020 B, 96,6% do slot, 17.364 B de folga de OTA**. A dieta da
v2.4.9-beta (`docs/analysis/DIETA_FLASH.md`) tinha deixado a imagem em
977.964 B com 62.420 B de folga; `SIMUT_CLI_FULL=1` gastou 45.056 B disso. A
conta de 06/09 que dizia "CLI completa + web + BT + mDNS não cabem juntos
(estourou ~35 KB)" estava certa quando foi feita — a folga era de 876 B. O que
mudou foi a folga, não a aritmética. Este é o build que mais precisa da CLI:
sem display, serial e BT são a única interface local.

### O ciclo

- Boot frio = **M0** (headless: web + serial + BT + sensores). `air hibernate`
  ou `air idle` segundos de inatividade → **M1**: dorme por WFI com alarme do
  RTC, acorda, lê sensores até estabilizar enquanto o Wi-Fi conecta em paralelo,
  **sempre grava** o histórico, e — só se a quantidade pendente atingir
  `t_int` — sobe o rádio e drena a telemetria de forma não bloqueante até
  acabar, perder o servidor ou cair o Wi-Fi. Depois dorme de novo.
- **Um wake M1 não sobe web, Bluetooth, mDNS nem o cache do painel**; nem o
  wake de telemetria sobe listener. Tudo isso é do M0. Consequência: um Air
  dormente não tem `:8080` — **OTA só em M0** (carregador no GP17, ou logo após
  um boot a frio).
- **Duas cadências, um alarme.** O aparelho acorda sempre no intervalo do
  histórico (`h_int`); a telemetria é expressa em wakes inteiros dessa cadência
  (`airTelemetryDue( )`), então o envio sempre coincide com uma medição — o caro
  é estar acordado, não transmitir. O contador de wakes mora no `scratch[1]`
  (bits 23..17), junto com os segundos dormidos (16..0). Um wake de telemetria
  zera o contador **mesmo se o envio falhar**, e uma falha reserva
  `AIR_TEL_FAIL_SKIP_WAKES` (5) wakes de silêncio — sem isso, com coletor morto,
  o rádio drena a bateria a cada wake.
- **`t_int` é lote mínimo em registros, não tempo** (config v22; 0 desliga a
  telemetria). `t_bat` é o lote máximo por requisição. Migração v21→v22 converte
  o valor antigo; sem ela um `t_int=300000` viraria "300 mil pendentes".
- **Telemetria desligada + Wi-Fi de pé = acordado para sempre.** Se o aparelho
  "não dorme", confira `t_int` antes de procurar outra causa.
- **Quem causou o boot decide quanto o M0 dura.** Boot limpo (energia, RUN,
  `reload`, OTA) = há alguém querendo entrar → vale o `air idle` inteiro. Boot
  sujo (watchdog) com o ciclo armado (`air.bin` flag bit 0) = graça de
  `AIR_RESUME_GRACE_SEC` (10 s). A partir de 3 boots sujos seguidos (`flags`
  bits 4..7) a graça volta a ser o `air idle` inteiro e o log registra
  `APP_AIR_CYCLE_HELD` (411) — para um aparelho que trava em M1 não ficar
  inalcançável. `air status` mostra `armed=` e `dirty=`.
- **Só uma requisição autenticada rearma o timer de inatividade**
  (`airMarkActivity( )` em `getAuthPerms( )` com sessão viva, `completeLogin( )`
  e o Basic auth do `/metrics`). Pré-login tem orçamento: `WEB_PREAUTH_MAX_EXT`
  (3) extensões por boot. Servir bytes não prova que há gente — os funis
  `safeSend*` não rearmam.
- **Carregador no GP17** (`AIR_CHARGER_PIN`, em `air.bin`): alto = carregando,
  o `air idle` não se aplica e um wake que o encontra cancela o M1 daquele boot
  e sobe M0 completo. O ciclo continua armado. Medido em 07/09: `chg=1` ficou
  acordado 130 s sob `idle=40`; `chg=0` dormiu em 40,5 s.
- **`air stop` sobe o rádio se estiver desligado** — parar num wake sem rádio
  deixaria o M0 sem web, sem NTP, alcançável só pelo cabo por onde o comando
  chegou. Por Bluetooth, `air stop` **não funciona em M1** (o laço M1 só lê
  USB; limitação documentada, F12).

### Como ele dorme e acorda

- Hibernação é **SLEEP** (`sleep_goto_sleep_until( )`: clk_sys→XOSC,
  `sleep_en0`=RTC, `__wfi`), não DORMANT — DORMANT corre contra o sincronizador
  lento do ROSC e não era determinístico. Antes do WFI, todas as IRQs exceto a
  do RTC são desabilitadas; senão um IRQ pendente de USB/UART acorda na hora.
- **O ROSC é religado logo após o `wfi`**, esperando `ROSC_STATUS_STABLE`, antes
  do SYSRESETREQ. Sem isso o wake demorava entre 16 e 48 min em vez de 120 s.
- **O `load` do RTC já vale um tique**: escreve-se 00:00:00 e 3 ms depois lê-se
  1. O alarme é `baseSec + wakeSec`; `airRtcSetDatetime( )` segura o LOAD por
  1 ms porque o SDK o perde a 46875 Hz.
- **O intervalo real desconta o tempo já acordado**: alarme =
  `h_int − millis( )` na hora de dormir (um wake **é** um boot). Piso
  `AIR_MIN_SLEEP_SEC`, com sufixo `OVERRUN` no log quando o wake não cabe no
  intervalo. Só compensa quando o boot foi mesmo um wake (`_airWokeFromSleep`).
- **Ciclo medido pela sonda, config de 120 s: 120,23 s** (resíduo +0,11 s,
  explicado pelo trabalho entre `millis( )` e o `load`). Wake até DECIDE:
  **9,31 s** (08/09), dos quais 6,83 s são a conversão do DS18B20.
- **CYW43: não chame `cyw43_arch_deinit( )` antes de dormir** — trava no 2º
  ciclo e deixa o chip num estado que só power-cycle recupera. O power-down é
  por hardware (`WiFi.disconnect(true)` + `WiFi.end( )` + GPIO23 LOW) e o boot
  seguinte faz o power-cycle.
- **USB: solte o pull-up de D+ antes de dormir** (`USB_SIE_CTRL_PULLUP_EN_BITS`)
  para o host ver um disconnect limpo; sem isso o cdc_acm empaca e o `ttyACM`
  só volta com power-cycle do hub. Por desenho, **alvo ausente do USB ≠ morto**.
- **`WATCHDOG_REASON` é somente-leitura** e retém o bit TIMER de qualquer
  disparo antigo através de soft resets. `airEnterDormant( )` chama
  `markCleanReboot( )` (`scratch[5]`) antes de dormir; sem isso todo wake vira
  um FATAL "loop stalled" espúrio.
- **"Adiar para depois" não existe no sono.** Todo mecanismo que coalesce ou
  posterga uma escrita (o cursor de telemetria adiava 5 s; a fase FLUSH saía em
  150 ms) tem de receber `force` nos caminhos que levam ao sono. Padrão a
  procurar em qualquer coisa nova nesse caminho.
- **O `.wip` é reescrito inteiro a cada gravação** e é o maior custo do ciclo;
  `flushWipV5( )` pula quando os bytes já são idênticos. **T15 é o portão** —
  teto 2 por ciclo (a segunda gravação é legítima: reescreve o flag de relógio
  provisório→sincronizado quando o NTP chega). 3 ou mais é defeito. `wip=` lido
  por `air status` de dentro de um wake devolve 0; use a linha `[AIR] alarm:`.
- **O boot retoma o bloco aberto** (`h5ResumeOpenBlock( )`): o `.wip` é a cópia
  em flash do bloco aberto e não é apagado. Foi isso que levou o histórico de
  16,0 para 1,9 B/registro (07/09). Um bloco retomado carrega carimbos da sessão
  anterior; `shiftHistoryTimeV5` sela antes de corrigir, de propósito.
- **O relógio atravessa o sono.** Ao armar o alarme, `airEnterDormant( )` grava
  o instante em `scratch[7]` (segundos) e `scratch[6]` (milissegundos, sob
  `AIR_CLOCK_MAGIC` e com os 6 bits baixos dos segundos como conferência). O
  wake soma a isso o sono que o RTC mediu (`scratch[1]`), `AIR_WAKE_BOOT_MS`
  (140 ms de boot ROM + crt0, que o `millis( )` não vê) e o próprio `millis( )`,
  e zera os dois registradores antes de armar o watchdog — nenhuma autópsia lê o
  relógio. Nos wakes sem rádio não há NTP: o que esse relógio disser é o que o
  histórico guarda. Medido em 23/09 contra um host com NTP, ciclo físico de
  59,77 s: a semente antiga (último registro + sono, truncados ao segundo)
  atrasava 0,8 s por wake e o NTP do wake de telemetria saltava +9–10 s de uma
  vez — os intervalos gravados iam de 54 a 69 s, o "acorda antes, acorda
  depois" relatado. Com o relógio carregado: erro entre −0,085 e +0,030 s em 10
  wakes, sem acumular, e o NTP corrige 0,08 s. Sem carga válida (boot frio,
  firmware anterior) volta a semente antiga; o boot do carregador usa a carga e
  deixou de nascer 27 s adiantado.
- **SSID ausente não alarga o wake**: `AIR_MAX_CONNECT_ATTEMPTS` (2); na prática
  só uma tentativa começa por wake (custa até 20 s num wake de ~28 s).
- **O LED não é o indicador de acordado — o GP16 é.** `LED_BUILTIN` é um GPIO do
  CYW43: acender sobe o rádio. `airSetLed( )` só age com `_airRadioUp`.
- **GP23/24/25/29 são proibidos** como `air charger` ou `sensorPowerPin` — são o
  barramento do CYW43. `airPinValid( )` vale no handler e no `airSanitise( )`,
  então um `air.bin` forjado volta ao default no load.
- **`air idle` aceita 10..65535** e o teto é o campo: 65536 virava 0, e ocioso
  zero manda dormir na passada seguinte.

### O Bluetooth nas imagens alpha e Air

`SIMUT_BLUETOOTH=1` nas duas: é superfície de ataque viva, autenticada pela
senha do admin da web (não o PIN do display), com lockout exponencial
(`authLockoutMs( )`, 2 s → 300 s) que mora na RAM do `BluetoothManager` —
derrubar e reconectar o RFCOMM não zera nada. `system factory`, `system format`,
`admin reset` e `system https off` são **só pela USB**; `ap` vale por BT de
propósito. A origem viaja em `CliDemand.fromBt`, porque uma linha do BT pode
ficar na fila e executar vários inputs depois.

### Comandos, suíte e portões

- CLI: `air idle <10..65535>`, `air charger <0..22|26..28|off>`, `air hibernate`,
  `air stop`, `air status` (`wake=`, `hist=`, `backoff=`, `idle=`, `armed=`,
  `dirty=`, `tel=<pendentes>/<lote>`, `skip=`, `chg=`, `wip=`). Ficam fora do
  `#if SIMUT_CLI_FULL`, então funcionam nos dois perfis — no console de
  emergência de antes de 18/09/2026 e na CLI completa que o Air traz hoje.
- `tools/air_test_suite.py`: `--list`, `--selftest` (sem hardware),
  `--only T05 --cycles 3`, `--long`, `--baseline`, `--report x.json`,
  `--flash fw.uf2`, `--watch <s>`. Exige `SIMUT_WEB_USER`/`SIMUT_WEB_PASS` para
  os testes web. `xfail` documenta bug conhecido: XPASS = fechou, tire a marca.
  Portões que valem regra: **T12** (volta a dormir após reset), **T15** (`.wip`
  ≤ 2 por ciclo), **T16** (wake ≤ 1 registro de preâmbulo), **T17** (`admin
  reset` sobrevive ao boot).
- `python3 tools/check_air_consistency.py` (C1–C8) roda em segundos, sem
  PlatformIO, e está no CI.

### Como medir sem ser enganado

- **A única prova do caminho do sono é o alvo reenumerar sozinho** dentro de
  `wakeSec` + margem. `hand RESET` recupera *mesmo que* o sono esteja quebrado.
- **Nenhuma medida do `air idle` vale com uma aba do painel aberta**: cada
  acerto autenticado rearma o timer. `ss -tn | grep 192.168.3.24` diagnostica.
  A janela limpa é tirar o aparelho da rede pelo próprio console (`system ssid
  <ssid>_offline` + `reload confirm`, e restaurar depois) — nunca fechar o
  navegador do usuário.
- **Não meça o histórico pelo delta do arquivo do dia**: o bloco aberto vive no
  `.wip` e o arquivo dá +0 — indistinguível de "parou de gravar". Olhe os blocos
  selados (`h5_block_anchors`) e o `tel=` de pendentes.
- **Deriva do relógio não se mede no histórico**: o carimbo guarda segundos
  inteiros e um atraso de 0,8 s por wake aparece como um intervalo "errado" de
  vez em quando, indistinguível de wake fora de hora. Meça pela linha
  `[AIR] clock=<s>.<ms> prov|ntp` que o DECIDE imprime, contra o relógio de um
  host com NTP.
- **Passe sempre o nominal do aparelho ao decodificar um `.h5`**: o V5 guarda
  desvios do passo nominal, e o nominal errado reescreve todos os tempos
  interiores e fabrica rajadas e gaps que não existem.
- **O `help` não discrimina firmware**: com pack não-inglês, o console serve o
  `@HELP` do `.lng` do LittleFS, que sobrevive à gravação. Discriminador
  não-destrutivo: `system ssid` sem argumento.
- **`/api/status` traz `uptime` em milissegundos.**
- **O log do servidor de um wake tem dois trechos** (`air hibernate` de M0 roda
  um ciclo no lugar e só depois vem o wake); contadores acumulados do servidor
  não são a janela — corte por relógio de parede.
- **Reboot silencioso ≠ watchdog.** Um boot sem `SYS_BOOT` nenhum só sai de
  reboot marcado limpo ou de reset físico; o watchdog deixa `[FTL] ctx=2xx`.
- **"Verificado na bancada" no commit vale para aquele commit.** Confira a data
  da validação contra a do commit antes de confiar.
- **O instrumento mente antes do firmware.** A primeira rodada da suíte acusou
  "serial vanished before the alarm line" — era a própria suíte engolindo a
  linha num `cmd( )` de 2 s. Prove o instrumento antes de culpar o alvo.

## 4. Log binário

- **Os dois lados do filtro são por transição, e o latch é por FAMÍLIA.**
  `SYS_TEL_FAIL` (31) e `SYS_TEL_RETRY` (32) se alternam a cada tentativa; um
  latch por código deixaria os dois passarem sempre. A primeira falha de uma
  família grava, as seguintes ficam subentendidas até a recuperação, com um
  batimento de 1 h. Medido (07/09): coletor morto, 6 min → 12 registros antes,
  0 depois.
- **FATAL nunca é filtrado**; WARN/ERROR não roteados passam sempre — código
  novo só fica quieto se alguém o listar de propósito.
- **A autópsia de um travamento é UM registro com faixa de `ctx`, e desde
  22/09 são QUATRO.** O registro de 12 B tem um `int16` de contexto, e a
  frase inteira (`C0=[…] C1=[…] at up=…ms sc3=0x… hp=…`) só existe na serial
  do boot seguinte. Os três fatos que não cabiam no primeiro registro viraram
  registros irmãos do mesmo código, separados por faixa:

  | faixa | veredito / fato | alcance |
  |---|---|---|
  | `0` | reset externo (`picotool`, toque de 1200 bps) | — |
  | `100+core` | soft panic: batimento parado | 100..101 |
  | `200+mod` | **HW watchdog**: módulo em que o Core 0 estava (`209` = CLI, `455` = trace vazio) | 200..455 |
  | `300+phase` | Core 1 congelado, na fase | 300..3xx |
  | `400` | Core 1 hard fault | — |
  | `1000+mod` | **módulo do Core 1** quando o Core 0 parou de alimentar (`1255` = sem trace) | 1000..1255 |
  | `2000+KB` | heap livre no travamento, em KB inteiros | 2000..2999 |
  | `4000+min` | uptime do aparelho no travamento, em MINUTOS | 4000..32000 |

  Leia os quatro como um grupo: são gravados consecutivamente, no boot logo
  depois do travamento. ⚠️ **As faixas são disjuntas de propósito** — a
  primeira versão pôs minutos em `2000` saturando em 20000, e um aparelho de
  pé há 1000 min gravava `ctx=3000`, que se lê como "heap 0 KB". Quem inventar
  uma faixa nova confere o alcance inteiro, saturação incluída
  (`test_log_policy`, caso `..._never_collide_with_EACH_OTHER`).
- **O arnês de bancada engolia a autópsia até 22/09.** `Rig.cmd( )` dormia 12 s
  atravessando o reboot e `reconnect( )` chamava `reset_input_buffer( )`;
  medido A-contra-A, o caminho antigo colhia **0 B** de um banner de 550 B.
  Hoje `reconnect( )` espera LENDO e guarda em `SIMUT_BOOT_SERIAL_LOG`. Se for
  escrever um arnês novo: **esperar por um reboot é ler, não dormir**.
- **`clear log confirm` não zera o que `show system log` devolve** (ele costura
  o rotacionado com o corrente). Só valem deltas entre duas leituras.
- **Num wake, o preâmbulo de boot é suprimido** (`LogPolicy::setQuietPreamble( )`,
  armado em `LogManager::begin( )` só quando `_airActive && _airSleptSec != 0`,
  desarmado no fim do `setup( )`). Os oito códigos da lista `BOOT_PREAMBLE` em
  INFO somem; medido: 8,38 → 1,25 registros por wake. A janela fecha no fim do
  `setup( )` porque dois desses códigos também são ação de operador (`441`
  idioma, `407` calibração). **`412 APP_AIR_COLD_BOOT`** é a linha que um wake
  nunca escreve: `ctx=1` boot limpo, `ctx=0` watchdog. **T16 é o portão** (teto 1
  — o segundo `STO_H5_WIP` do flush do ciclo é legítimo).
- A contabilidade horária de `SYS_LOG_SUPPRESSED` **não vale num Air**: o
  `millis( )` reinicia a cada wake e o relatório de 1 h nunca sai. Limitação
  conhecida, e barata — a lista suprimida é fixa.
- Como ler o log de verdade: `/api/logs` são registros binários de 12 B; o
  `ctx` satura em int16; um código novo exige `tools/logcodes.tsv` +
  `gen_logcodes.py`, nunca o `.h`. A rota é ~12× mais rápida que o console
  (0,15 s contra 1,76 s para 1.189 registros, medido 19/09), e é ela que
  `Rig.log_records( )` usa.
  ⚠️ **`/api/logs` RECUSA**: 429 para duas leituras dentro de 200 ms e 503
  dentro da janela de toque. Um leitor que devolva lista vazia nessas duas
  respostas transforma "não consegui olhar" em "o registro não existe" — foi
  exatamente o que a primeira versão de `log_records( )` fez, dizendo 0
  registros do código 308 enquanto o console via 88. Quem consome essa rota
  tem que distinguir recusa de ausência: hoje ela repete e, esgotadas as
  tentativas, levanta exceção.

## 5. Console de emergência — quem muda algo que precisa sobreviver ao boot salva ali mesmo

- **Quem ainda tem este console:** release e alpha (`SIMUT_CLI_FULL == 0`). O
  Air saiu do grupo em 18/09/2026 e hoje traz a CLI completa, com `write
  memory`; a regra abaixo continua valendo para quem editar o console de
  emergência ou portar um comando para ele.
- **A regra:** onde `SIMUT_CLI_FULL == 0` **não existe `write memory`**, e o
  `changed = true` do `switch` não salva nada: no perfil completo imprime *"use
  write memory"*, no de emergência imprime *"vale para esta sessão"*. Todo
  comando desse console cuja mudança precise sobreviver ao boot **chama
  `saveConfiguration( )` na própria caixa do `switch`** — e não
  seta `changed`, porque a frase impressa passaria a mentir. É o que `system
  ssid`, `system pass` e, desde 08/09, `system admin reset` fazem.
- `saveConfiguration( )` pode ser chamada direto do handler: ela já traz
  `WdtWindow(30000)`, o `BigSaveGuard` que congela o Core 1 num laço só-RAM e o
  skip de no-op por CRC.
- **Salve antes de anunciar.** A senha impressa é uma promessa; se o save falhar
  o comando diz `NAO SALVOU: vale so ate reiniciar`.
- **Persistir um flag muda quem mais o lê.** `mustChangePassword` sobrevivendo
  em flash quebrou `isFactoryDefaults( )`, que era literalmente "admin pendente
  de troca". Antes de persistir um campo que era volátil, `grep` quem mais o lê.
- **Um comentário que enumera ("o único", "sempre", "nunca") se confere com
  `grep` antes de ser acreditado.** Um `#else` afirmando que `debug` era o único
  comando a setar `changed` escondeu o F27 por um dia.
- **T17 é o portão**: reseta no console, reinicia, exige que a senha impressa
  ainda logue.

## 6. Um laço que consulta uma constante de compilação nunca sai mais cedo

- **O padrão:** `while (!cond( ) && millis( ) - t0 < TIMEOUT)` só é uma espera se
  `cond( )` puder virar verdadeira. Quando ela é `return false` literal, ou lê um
  flag sem escritor **no link daquela imagem**, o timeout deixa de ser o pior
  caso e vira o custo fixo. Não aparece em revisão; aparece no cronômetro. Três
  casos no mesmo `setup( )` valiam 5,2 s por wake do Air (08/09) — um deles era
  `tight_loop_contents( )`, núcleo a plena corrente, 1,5 s por minuto.
- **Como procurar:** para cada laço com timeout no boot, pergunte *quem escreve a
  condição nesta imagem* e confirme com `grep` mais a **linha de link**
  (`.pio/build/<env>/src/*.o`) — não pelo `#include`, que mente.
- **Como consertar sem criar outro:** uma constante de capacidade `static
  constexpr` ao lado do estado que ela descreve (`DisplayManager::kUsesCore1`,
  `kHasTouch`), decidida pelo pré-processador.
- **Espera fixa por operador é outra categoria**: `delay(1000)`, o splash, o
  ciclo de energia do CYW43 existem para gente — o portão deles é `_airActive`,
  não a imagem. Num wake não há ninguém; em M0 há.
- **`readInterval` de sensor não se sobrepõe à conversão**: `lastReadTime` é
  carimbado no fim da leitura, então o período real é `readInterval + conversão`.
  `setFastSampling( )` zera só o intervalo nas fases WARMUP/SAMPLE de um wake.
  Mexer em `MOVING_AVG_WINDOW` ou na resolução do DS18 muda o número gravado — é
  decisão do mantenedor.

## 7. O que este manual não guarda, e onde está

- A história dos achados do Air (F01–F28), com medições e retratações:
  `docs/analysis/SIMUT_AIR_PLANO_FIX.md` (snapshot) e o `CHANGELOG.md`.
- O que ainda se deve: `docs/analysis/PLANO_DIVIDA_TECNICA.md` (living).
- Onde o 1 MB de flash vai e o que devolve bytes: `docs/analysis/DIETA_FLASH.md`.
- A segurança, do modelo de ameaça às rotações: `SECURITY.md`; a matriz de
  autorização das rotas: `docs/AUTHORIZATION.md`.
- O que cada script de `tools/` é: `tools/README.md`.
