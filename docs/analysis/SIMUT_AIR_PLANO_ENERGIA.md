# SIMUT Air — plano de energia: leitura frequente, rádio raro

> **Pergunta que originou este documento (Ângelo, 06/09/2026):** dois alarmes, um para acordar e
> medir, outro para a telemetria; o rádio só sobe no alarme de telemetria; a telemetria sempre
> coincide com uma medição. É viável fazer **leitura a cada 1 min e telemetria a cada 15 min**?
> **Base:** branch `feature/simut-air`, commit `9d95450`, com o F25 já corrigido.
> **Documento irmão:** [`SIMUT_AIR_PLANO_FIX.md`](SIMUT_AIR_PLANO_FIX.md) (F01–F25).
> **Idioma:** pt-BR.

---

## 0. Resposta curta

**É viável, mas não com o wake de hoje — e o desenho pedido pode ser simplificado.**

1. **Dois alarmes não são necessários, e o próprio pedido dispensa o segundo.** O RTC do RP2040
   tem **um** alarme. Mas a exigência "a telemetria sempre coincide com o acordar de uma medição"
   colapsa os dois num só: acorde sempre na cadência de **leitura**, e nos wakes em que a
   telemetria vencer, suba o rádio. Um alarme, uma cadência, zero risco de os dois brigarem pelo
   mesmo periférico.
2. **O bloqueio real é o tamanho do wake, não o número de alarmes.** Medido: a janela acordada é de
   **27 a 31 s**. A 1 minuto de cadência isso é **~50% de ciclo de trabalho** — pior que inútil
   para bateria. Só **11,3 s** disso é boot, e a maior parte do boot é desperdício num aparelho
   headless.
3. **Com o wake enxuto, 1 min / 15 min fecha em ~10% de ciclo de trabalho.** É a Fase 1 deste
   plano, e ela **não depende** da mudança de arquitetura: paga sozinha.
4. ⚠️ **Nada disso está medido em corrente.** Toda a aritmética abaixo usa ordens de grandeza de
   datasheet. Se o sono do RP2040 for mesmo ~1,2 mA, **o sono passa a dominar o orçamento** a 1
   minuto, e a maior alavanca restante deixa de ser o wake e passa a ser o modo de sono. Por isso a
   **Fase 0 é medir**, e ela é bloqueante.

---

## 1. Onde o tempo vai hoje (medido no ferro, 06/09)

Marcadores de boot do capturador serial, um wake real com o SSID certo:

| bloco | custo | evitável num wake sem rádio? |
|---|---|---|
| `serial ok` | 0,18 s | não |
| `delay(1000)` | **1,00 s** | **sim** |
| janela de detecção de AP (`ap-detect`) | **4,50 s** | **sim — é desperdício puro no headless** |
| montagem do FS (`storage`) | 0,04 s | não |
| `loadAndCalibrateSensors( )` + resolução do DS18 | 1,94 s | não |
| `_netMgr->begin( )` = CYW43 + `WiFi.begin` | **1,03 s** | **sim — é a proposta do Ângelo** |
| init de telemetria + servidor web | 0,55 s | **sim** (F13) |
| preload de cache + `System ready` | **2,15 s** | **sim** (F13) |
| **subtotal do boot** | **≈ 11,3 s** | **~10,6 s evitáveis** |
| fase SAMPLE (buffer de 10 amostras × 1 s) | **≈ 14,8 s** | **parcialmente** |
| DECIDE + gravação do histórico | ≈ 1,0 s | não |
| teardown até o WFI | ≈ 1,5 s | parcialmente |
| **total (sonda: 26,5 a 30,9 s)** | **27–31 s** | |

Duas linhas merecem nome próprio:

- **A janela de AP custa 4,5 s por wake e não faz nada.** `AP_DETECT_WINDOW_MS` (3500 ms) mais o
  *touch settle* existem para ler um gesto de toque na tela. O Air não tem tela: `DisplayManager_None`
  devolve "não tocado" 3,5 s seguidos, todo wake, para sempre.
- **A estabilização é o maior bloco isolado.** `airAllStable( )` espera `bufferFull( )`, ou seja
  `MOVING_AVG_WINDOW` = **10 amostras**, e o DS18B20 amostra a cada 1000 ms. São ~15 s esperando
  uma média móvel encher — num aparelho que acabou de ligar o sensor e vai gravar **um** valor.

---

## 2. Orçamento de energia (aritmética, não medição)

Ordens de grandeza para um Pico W, a confirmar na Fase 0:

| estado | corrente estimada |
|---|---|
| SLEEP do RP2040 (WFI, XOSC vivo, RTC contando), CYW43 desligado pelo WL_REG_ON | **~1,2 mA** |
| acordado, 125 MHz, sem rádio | ~20 mA |
| acordado, associado ao AP | ~50–70 mA (picos de ~250 mA no TX) |
| DORMANT (descartado por não-determinismo, ver esboço) | ~0,18 mA |

Com isso, a 1 minuto de leitura e 15 de telemetria:

| cenário | acordado por wake | ciclo de trabalho | corrente média |
|---|---|---|---|
| **hoje**, rádio em todo wake | 27–31 s | ~50% | **~28 mA** |
| **Fase 1** (wake enxuto), rádio ainda em todo wake | ~8–10 s | ~15% | ~9 mA |
| **Fase 2** (rádio só na telemetria) | 14×5 s + 1×20 s | **~10%** | **~3,1 mA** |
| Fase 4 (retomar em vez de reiniciar) | 14×2,5 s + 1×18 s | ~6% | ~2,3 mA |

⚠️ **Leia a última coluna com desconfiança.** Na linha da Fase 2, **1,1 mA dos 3,1 são o sono** —
mais de um terço do orçamento é consumido sem fazer nada. A partir daí, encurtar o wake rende cada
vez menos e o alvo passa a ser o próprio modo de sono. É exatamente por isso que a Fase 0 vem
antes: se a medição disser que o sono custa 0,4 mA, a conclusão muda; se disser 3 mA, muda mais
ainda.

---

## 3. Desenho recomendado

### 3.1 Um alarme, não dois

Acorde sempre em `h_int` (cadência de leitura). Em cada wake, decida se **esta** vez também é de
telemetria. Nada de segundo alarme: o RTC tem um só, e a coincidência exigida pelo Ângelo é
consequência automática deste desenho, não uma restrição a fazer valer.

### 3.2 O critério "hoje é dia de telemetria" — SUPERADO em 07/09: agora é quantidade

> ⚠️ **A contagem de wakes descrita abaixo foi substituída.** Desde a config v22 o gatilho é a
> quantidade pendente (`cfg.telInterval` virou o **lote mínimo** em registros; 0 desliga), com uma
> penalidade de wakes após uma falha de envio. Ver
> [`SIMUT_TELEMETRIA_PLANO_CADENCIA.md`](SIMUT_TELEMETRIA_PLANO_CADENCIA.md) §3.11. O texto
> original fica como registro do desenho anterior:

```
telemetriaVence =  cfg.telInterval > 0
                && (wakesDesdeEnvio + 1) ≥ ceil(telInterval / histInterval)   [SUPERADO]
```

- Primeira linha: telemetria desligada mantém o rádio **desligado para sempre**, que já é o maior
  ganho de bateria disponível — e essa parte continua valendo, só que "desligada" agora é lote
  mínimo 0.
- Segunda: o intervalo de telemetria era expresso em **wakes inteiros** da cadência de leitura, e
  era isso que fazia o envio sempre coincidir com uma medição. Arredondava para **cima**, de
  propósito: 15 min de telemetria sobre 2 min de leitura enviava a cada 8 wakes (16 min), não a
  cada 7 (14) —
  enviar cedo quebraria a promessa de que o intervalo do operador é um piso.

⚠️ **Por que NÃO comparar relógios,** como esta seção propunha antes: num wake sem rádio não há
NTP, então o relógio é provisório. Uma regra escrita contra epochs estaria medindo exatamente a
grandeza em que ela não pode confiar. Contar wakes é exato por construção.

**Onde mora o contador:** no `scratch[1]` do watchdog, dividindo o registrador com os segundos
dormidos (bits 23..17 = wakes, 16..0 = segundos). Em flash, um aparelho que lê a cada minuto
pagaria uma escrita por minuto só para manter um contador. Perdê-lo custa **uma** telemetria
atrasada, e só em power cycle ou reset físico — reset de watchdog preserva, que é o caso que
importa.

### 3.3 A punição do coletor mudo, sem estado novo

Um wake de telemetria **zera o contador mesmo se o envio falhar**. A punição por um coletor que
não responde passa a ser esperar um intervalo inteiro de telemetria, e não tentar de novo no wake
seguinte com o rádio ligado — que é precisamente o que se queria evitar. Isso dispensou o
`proximaTentativa` persistido que a versão anterior deste plano previa.

E o alarme continua sendo sempre a cadência de leitura: o backoff decide se o rádio sobe, nunca
quando o aparelho acorda. O histórico não paga pelo pecado do coletor.

### 3.4 Regra de rede ausente

A regra que o Ângelo já pediu e que **já está implementada** continua valendo, agora só nos wakes
de telemetria: `AIR_MAX_CONNECT_ATTEMPTS` (2) tentativas, depois o SAMPLE para de bombear a rede,
o histórico é gravado e o aparelho dorme. Com o rádio subindo uma vez a cada 15 minutos, o teto de
2 finalmente vira caso real em vez de teto teórico — o wake de telemetria é longo o bastante para
duas tentativas caberem.

---

## 4. Armadilhas concretas (achadas lendo o código, não supostas)

1. 🔴 **`airSetLed( )` liga o rádio.** No Pico W, `LED_BUILTIN` é `PIN_LED = 64`: um GPIO **do
   CYW43**, não do RP2040. Escrever nele obriga a subir o chip de rádio e **destrói a economia
   inteira**. Num wake sem rádio o LED tem de ficar intocado. (`AirConfig.flags` tem bits livres
   para uma política de LED.)
2. 🔴 **Verificar se o core sobe o CYW43 sozinho.** Não basta pular `_netMgr->begin( )`: é preciso
   medir se o `arduino-pico` inicializa o chip no boot de qualquer jeito. Isso é medição da Fase 0,
   não suposição.
3. ⚠️ **O relógio provisório vira o relógio principal (F04).** Com NTP a cada 15 min, **14 de 15
   leituras** são carimbadas offline. Hoje o provisório é "último registro + 60 s + uptime", que é
   errado. Mas agora existe `slept` medido pelo RTC: `epoch = epoch_do_ultimo_registro + slept +
   acordado`. O drift do XOSC (~30 ppm) dá 0,03 s em 15 min — irrelevante. **Isto deixa de ser
   opcional e vira pré-requisito.**
4. ⚠️ **Desgaste de flash a 1 leitura/min.** `flushWipV5( )` **reescreve o `.wip` inteiro** a cada
   wake (`LittleFS.open("w")` trunca). São 1440 reescritas/dia. O LittleFS é copy-on-write com
   nivelamento, então a conta provavelmente fecha, mas **provavelmente não é medição**: contar
   erases reais é item da Fase 0.
5. ⚠️ **Alarmes de sensor atrasam até 15 min.** Se um alarme precisa sair na hora, o desenho
   precisa de uma exceção ("alarme ativo força wake de telemetria"). É a decisão **D-8**.
6. ⚠️ **A ordem do boot já ajuda, mas é preciso mantê-la.** A decisão do rádio depende do FS
   montado (cursor + config), e o FS monta em `storage ok` (5,7 s) enquanto a rede sobe em `net ok`
   (8,6 s). A decisão cabe no meio. Qualquer refatoração que suba a rede antes do FS quebra isso.

---

## 5. Fases

### Fase 0 — medir corrente (BLOQUEANTE)

Sem estes números o resto é opinião.

| id | o que medir | como |
|---|---|---|
| E1 | corrente no sono | multímetro em série no VSYS (ou INA219 no VBUS) durante um sono inteiro |
| E2 | corrente acordado **sem** rádio | wake com `telInterval=0` |
| E3 | corrente acordado **associado** | wake normal com o SSID certo |
| E4 | consumo do CYW43 desligado por WL_REG_ON | E1 com e sem o power-down |
| E5 | **o core sobe o CYW43 sem `WiFi.begin`?** | build de teste que só monta o FS e dorme |
| E6 | erases reais por dia a 1 leitura/min | contadores do LittleFS ou instrumentação do `FLASH_OP` |

A sonda da PicoHand já marca acordado/dormindo no GP16, então o traço de corrente pode ser
segmentado por fase sem adivinhação.

### Fase 1 — encurtar o wake ✅ EXECUTADA em 08/09/2026 (menos a L4; ver §10)

| id | alavanca | ganho esperado |
|---|---|---|
| L1 | pular a janela de detecção de AP quando não há tela | **−4,5 s** |
| L2 | pular o `delay(1000)` num boot M1 | −1,0 s |
| L3 | não subir web, telemetria, mDNS, BT nem preload de cache em M1 (F13) | −2,7 s |
| L4 | critério de estabilidade "N amostras válidas" (`stabSamples`, default 3) em vez de `bufferFull( )` | **−10 a −12 s** |

**Resultado esperado: 27–31 s → 8–10 s**, com o rádio ainda ligado em todo wake. Mensurável pela
sonda, sem mudar nenhuma decisão de produto. Aceite: T09 e o `--watch` da suíte.

### Fase 2 — o wake sem rádio ✅ IMPLEMENTADA em 06/09

| id | item | como ficou |
|---|---|---|
| R1 | punição do coletor mudo | **Sem estado novo.** Um wake de telemetria zera o contador **mesmo se o envio falhar**, então a punição é esperar um intervalo inteiro de telemetria. Retentar no wake seguinte com o rádio ligado é o que se queria evitar. |
| R2 | decidir `radioNesteWake` antes de `_netMgr->begin( )` | `airTelemetryDue( )` no boot, depois do `_storageMgr->begin( )` (config) e do `scratch[1]` (contador). |
| R3 | pular a rede quando a decisão for "não" | `_netMgr->begin( )` e `_webMgr->begin( )` fora; marcadores `net skipped` / `web skipped`. |
| R4 | política de LED | `airSetLed( )` só age com `_airRadioUp`. O indicador de acordado é o **GP16**, e sempre foi. |
| R5 | F04 — carimbo do registro | `setProvisionalTime(lastTs, slept + millis()/1000)`: o sono **medido** substitui o palpite fixo de 60 s. |
| R6 | o alarme é sempre `h_int` | Já era desde o commit do intervalo real; a telemetria não mexe no alarme, só no rádio. |

Extra que a bancada cobrou: **`air stop` sobe o rádio** se ele estiver desligado, senão o operador
que para o ciclo num wake sem rádio fica com um M0 sem web e sem NTP.

⚠️ **O contador de wakes divide o `scratch[1]` com os segundos dormidos** (bits 23..17 e 16..0).
Em flash custaria uma escrita por minuto. Perdê-lo custa uma telemetria atrasada, e só em power
cycle — reset de watchdog preserva.

**Resultado esperado: wake de leitura em 4–6 s, wake de telemetria em 10–25 s** — mas isso só vale
**depois da Fase 1**, que não foi feita: hoje o boot ainda gasta 4,5 s na janela de AP, 1 s no
`delay(1000)` e ~2,7 s subindo cache. Sem ela, o ganho medido é só o `_netMgr->begin( )` (2,9 s) e
o servidor web. Aceite: **T13** da suíte compara as janelas acordadas com e sem rádio; T08 prova o
carimbo offline; a corrente da Fase 0 fecha a conta.

### Fase 3 — a cadência pedida

Nada a construir: `h_int` já aceita **1..1440 minutos** e `telInterval` é o lote mínimo em
registros desde a v22 (15 min de telemetria com leitura de 1 min = 15 registros). Só configurar e
medir por algumas horas, conferindo o histórico com `h5_block_anchors`.

### Fase 4 — a alavanca grande, e a arriscada: retomar em vez de reiniciar

Hoje o wake faz **SYSRESETREQ** e paga o boot inteiro (11,3 s) por escolha de projeto: "o boot ROM
reinicializa os clocks". Mas o SLEEP do RP2040 **preserva a SRAM**, e o `sleep_power_up( )` do
pico-extras restaura os clocks sem reset. Se o retorno em linha funcionar, **o boot desaparece** e
um wake de leitura passa a ser só ler o sensor: ~2–3 s.

**Risco alto e concentrado:** o core Arduino, os handles do LittleFS, o Core 1, o USB e a pilha
Wi-Fi teriam de sobreviver ao sono. Por isso é a última fase, atrás de um flag de build, com o
caminho de reset como plano B. Também é a única que muda o F25: sem reset, não há boot, e o
marcador de hibernação deixa de ser o discriminador.

---

## 6. Decisões pendentes (para o Ângelo)

- **D-7** Cadência default de fábrica: manter `h_int` alto (5 min) e deixar 1 min como escolha
  consciente, ou já entregar 1 min?
- **D-8** Alarme de sensor deve furar a fila e forçar um wake de telemetria, ou pode esperar até
  15 min?
- **D-9** Com o rádio desligado 14 de 15 wakes, o aparelho fica inalcançável pela web nesses
  minutos. Aceitável, ou queremos uma janela web garantida (por exemplo, o wake de telemetria
  mantém o servidor no ar por N segundos)?
- **D-10** Fase 4 (retomar sem reset) entra no escopo, ou fica como pesquisa depois de 1–3
  estarem estáveis?

---

## 7. O que NÃO fazer

- **Não implementar dois alarmes de RTC.** O hardware tem um; e com a coincidência exigida, o
  segundo não teria o que fazer.
- **Não encurtar o wake mexendo em `stabTimeoutMs`.** Ele é um **teto**, não a duração: quem manda
  na fase SAMPLE é `bufferFull( )`. Baixar o teto só trunca a estabilização sem tornar o critério
  correto — é a alavanca L4 que resolve.
- **Não medir consumo antes da Fase 1.** Medir o aparelho de hoje mede 4,5 s de janela de AP que
  vai deixar de existir.

---

## 8. Bancada de 06/09/2026 ~22h20 — a Fase 2 no ferro

Configuração do teste: `h_int` = 1 min, `t_int` = 180000 ms → **rádio a cada 3 wakes**.
Instrumentos: serial (marcadores de boot) e a sonda GP16 da PicoHand, na mesma janela.

**A programação faz exatamente o que promete:**

| wake | `[AIR] wake:` | boot | janela acordada (sonda) |
|---|---|---|---|
| 1 | `radio=off (wakes since send=0)` | `net skipped` · `web skipped` | **26,18 s** |
| 2 | `radio=off (wakes since send=1)` | `net skipped` · `web skipped` | **26,04 s** |
| 3 | `radio=on (wakes since send=2)` | `net ok` · `web ok` · CONNECT · FLUSH | **56,67 s** |
| 4 | `radio=off (wakes since send=0)` | `net skipped` · `web skipped` | **26,07 s** |

O contador zerou depois do envio (o wake 4 volta a 0), e o período de leitura se manteve:
**59,95 s e 59,82 s** para 60 s configurados.

⚠️ **HONESTIDADE SOBRE O GANHO: em tempo, é pequeno — cerca de 1 s em 26.** O `_netMgr->begin( )`
custa **1,03 s**, e não os 2,88 s que a §1 dizia; a diferença era o `loadAndCalibrateSensors( )`,
que eu havia atribuído à rede por ter lido o intervalo entre dois marcadores de boot sem olhar o
que corria no meio. O servidor web custa mais 0,1 s. **O ganho que importa é o outro, e ele é
invisível para um cronômetro:** nesses wakes o CYW43 **nunca é energizado**. Quanto isso vale em
mA continua sendo a Fase 0 — sem ela, não dá para afirmar o resultado.

🔴 **Achado novo: a 1 minuto de leitura, o wake de telemetria ESTOURA o intervalo.** Medido com o
coletor mudo: a fase FLUSH rodou o teto inteiro de `flushTimeoutMs` (30 s), a janela acordada foi a
**58,3 s** e o alarme caiu no piso — `wakeSec=5 … OVERRUN`. Ou seja, 27 s de amostragem mais 30 s de
flush não cabem em 60 s. **`flushTimeoutMs` tem de ser dimensionado contra o intervalo de
leitura**, e é a Fase 1 (que corta ~12 s da amostragem) que abre espaço para ele. Como está, um
coletor fora do ar transforma um wake em cada três num wake que consome quase o dobro.

⚠️ **A D-9 deixou de ser hipótese.** Durante o teste o aparelho ficou inalcançável pela web — o
servidor só existe nos wakes de telemetria — e restaurar a configuração pelo `/api/commit_all`
**falhou por timeout**. Foi preciso parar o ciclo pela serial primeiro. O `air stop` que sobe o
rádio (extra da Fase 2) é o que torna isso recuperável, mas a decisão de fundo continua aberta: um
aparelho com web em 1 minuto de cada 3 é operável?

---

## 9. Re-medição de 08/09/2026 — a Fase 1 continua não feita, e a §1 errou uma atribuição

⚠️ **Isto não é um achado novo.** A §1 e a Fase 1 deste documento, escritas em 06/09, já dizem que
a janela de AP custa 4,5 s e que o anel de 10 amostras custa ~15 s. **A Fase 2 foi implementada e a
Fase 1 não.** O que esta seção acrescenta é: (a) a medição refeita na imagem de hoje, com carimbo
do host linha a linha; (b) **dois blocos que a tabela da §1 não lista**; (c) a **correção de uma
atribuição errada** da §1; e (d) o mecanismo exato da fase SAMPLE, que não é o que a Fase 1 supõe.

### 9.1 O wake de leitura, cronometrado no host

Aparelho ciclando sozinho, porta reaberta no instante da enumeração, um carimbo por linha:

| t (host) | Δ | marcador | o que roda no intervalo |
|---|---|---|---|
| 0,10 s | — | `[AIR] boot: serial ok` | (antes disto: ciclo de energia do CYW43 + `Serial.begin` + enumeração ≈ **1,8 s** de `millis( )`) |
| 1,11 s | **1,01** | `[AIR] boot: delay ok` | `delay(1000)` |
| 1,11 s | 0,00 | `[AIR] boot: display ok` | `_displayMgr->begin( )` — no-op no headless |
| 5,63 s | **4,52** | `[AIR] boot: ap-detect ok` | `delay(800)` + *touch settle* (~220 ms) + `AP_DETECT_WINDOW_MS` (3500 ms) |
| 5,68 s | 0,05 | `[AIR] boot: storage ok` | `_storageMgr->begin( )` + decisão do M1 |
| 7,19 s | **1,51** | `[TCH] c=0` | **espera por `isCore1Ready( )`** |
| 7,24 s | 0,05 | lang, sensores, telemetria | tema + `.lng` + `_sensorMgr->begin( )` + calibração + telemetria |
| 8,94 s | **1,70** | `System ready` / `boot: done` | laço de aquecimento (~900 ms) + `delay(800)` |
| 8,94 s | — | `[AIR] phase=WARMUP @10733` | fim do `setup( )` em **10,73 s** de `millis( )` |
| 8,94 s | 0,40 | `[AIR] phase=SAMPLE @10733` | WARMUP fixo |
| 23,77 s | **14,81** | `[AIR] phase=DECIDE @25546` | **fase SAMPLE** |

Wake total ≈ **25,5 s** (T05 do mesmo dia: acordado 23,8 e 24,0 s pela enumeração USB; T13: fila
quieta 23,8–24,2 s, barulhenta 29,2 s).

⚠️ **Onde o carimbo do host não vale:** linhas próximas chegam em rajada, porque a CDC agrupa. Os
Δ acima só são confiáveis quando há um `delay` garantindo separação — que é o caso de todos os
marcados em negrito. Para o bloco de 7,19→8,94 s o discriminador é o prefixo `[BOOT+Ns]` do próprio
firmware, que mostra `+8s` na hora e no pack de idioma e `+9s` nos sensores e na telemetria.

### 9.2 Dois blocos que a §1 não listava

- 🔴 **A espera pelo Core 1: 1500 ms, em *busy spin*, por um core que esta imagem nunca lança.**
  `AppManager_Boot.cpp` roda
  `while (!_displayMgr->isCore1Ready( ) && millis( ) - wait_start < 1500) tight_loop_contents( );`
  logo depois de `startCore1( )`. Na imagem Air `startCore1( )` é no-op
  (`DisplayManager_None.cpp`) e **`_core1Ready` só é escrito em `DisplayManager.cpp` e
  `DisplayManager_Alpha.cpp`, e nenhum dos dois entra no link do `pico_w_air`** — confira a linha
  de link do build. Logo a condição nunca vira verdadeira e o laço **sempre** gasta o timeout
  inteiro. Não é um `delay`: é um laço apertado, com o núcleo a plena corrente.
- **Mais 2,2 s de esperas fixas espalhadas:** `busy_wait_ms(500)+busy_wait_ms(100)` do ciclo de
  energia do CYW43 (600 ms, existe para consertar o rádio depois de gravação UF2 / apply de OTA /
  watchdog / RESET da mão / `picotool` — **nenhum desses é um wake**), `delay(BOOT_STEP_DELAY_MS)`
  (800 ms) e o `delay(800)` que antecede `System ready` para dar tempo de ler um splash que não
  existe.

### 9.3 Correção de uma atribuição da §1

A §1 credita **1,94 s** a `loadAndCalibrateSensors( )` + resolução do DS18. **Está errado.** A
captura mostra tema, `.lng`, `_sensorMgr->begin( )`, calibração, resolução do DS18 e init de
telemetria **todos dentro de 50 ms**, e o 1,51 s imediatamente anterior é a espera pelo Core 1 —
que a §1 não tinha. A conta total da §1 fecha porque os dois erros se compensam; a conclusão
"encurtar o boot" não muda, mas **a linha que a Fase 1 mandaria otimizar não é a que custa**.

Somando o que é espera fixa sem função nesta imagem:

| item | custo | por que existe | por que é morto no Air |
|---|---|---|---|
| ciclo de energia do CYW43 | 600 ms | rádio em estado indefinido após UF2/OTA/watchdog/RESET | um wake é um SYSRESETREQ deliberado a partir de estado bom; num wake de leitura o rádio nem sobe |
| `delay(1000)` | 1000 ms | deixar a CDC enumerar para o banner ser lido | ninguém está plugado num wake |
| `delay(BOOT_STEP_DELAY_MS)` | 800 ms | substituto de "espere o Core 1" | o Core 1 não é lançado |
| *touch settle* | ~220 ms | XPT2046 reporta tocado logo após ligar | `isScreenTouched( )` é `return false` literal |
| `AP_DETECT_WINDOW_MS` | 3500 ms | segurar a tela força modo AP | mesmo literal → o laço sempre roda a janela inteira |
| espera por `isCore1Ready( )` | 1500 ms | esperar `victim_init` do Core 1 | `_core1Ready` nunca é escrito nesta imagem |
| `delay(800)` pré-`System ready` | 800 ms | deixar ler o splash | não há splash |
| **total** | **≈ 8,4 s** | | **33% do wake** |

### 9.4 Trabalho sem consumidor num wake

Custa pouco tempo (os 50 ms acima), mas custa flash, heap e escrita:

- `scanCustomThemes( )` + `loadTheme( )` + `refreshTheme( )` — varre `/themes` e faz *parse* de cada
  `.thm`. A paleta alimenta o TFT e a UI web; num wake não existe nem um nem outro.
- `findAndLoadLangFile( )` quando o idioma não é inglês — o rig é pt-BR, então **todo wake carrega o
  pack** (~13 KB residentes, ver [[lang-pack-eats-the-heap]]). Num wake ele serve só ao texto do CLI
  que ninguém lê.
- `loadDisplayOffset( )`, e o `Serial.print("[TCH] c=")` de `gpio_get(20)` — o pino de IRQ de um
  touch que esta imagem não tem.
- O laço de aquecimento de ~900 ms no fim do `setup( )` refaz o que a fase WARMUP/SAMPLE do ciclo
  vai refazer em seguida.

✅ **Já fechado pela Fase 2/F13 e confirmado nesta captura** — não mexer: `net skipped
(reading-only wake)`, `web skipped (wake)`, Bluetooth, mDNS e `preloadMinMax` fora do wake.
`SoundManager.cpp` sequer é compilado na imagem Air.

### 9.5 A fase SAMPLE: o mecanismo não é "10 × 1 s"

A Fase 1 (L4) supõe que trocar `bufferFull( )` por "N amostras válidas" resolve. Resolve em parte,
mas o custo por amostra é maior do que a §1 diz:

- `airAllStable( )` exige `buffers[CH_TEMP].full( )`, e `full( )` é `count >= MOVING_AVG_WINDOW`,
  com **`MOVING_AVG_WINDOW = 10`**.
- `processPeriodicReads( )` só **pede** uma conversão quando `now - lastReadTime >= readInterval`,
  e `readInterval` do DS18B20 é **1000 ms**. A conversão então leva
  `DS18B20_CONVERSION_TIME_MS` = **750 ms**. E **`lastReadTime` é carimbado no fim da leitura**,
  não no pedido.
- Logo o período por amostra é **1000 ms de ociosidade + 750 ms de conversão ≈ 1,75 s**, não 1 s.
  Medido: **14,81 s** de SAMPLE, com uma ou duas amostras já colhidas pelo laço de aquecimento do
  boot.

**Os 1000 ms são ociosidade pura.** São um limitador de taxa para um aparelho que lê continuamente
para mostrar um número numa tela. Dentro de um wake eles não filtram nada — só somam 10 s de núcleo
ligado sem conversão em curso. Isso é uma alavanca **separada** da L4 e provavelmente mais barata:

| alavanca | SAMPLE resultante | o que se perde |
|---|---|---|
| hoje | 14,8 s | — |
| ler de volta a volta (sem os 1000 ms ociosos) | ~7,5 s | nada de medição; só o limitador de taxa |
| + janela de 4 amostras no Air | ~3,0 s | rejeição de ruído: média de 4 em vez de 10 |
| + DS18 em 10 bits (187,5 ms) | ~0,8 s | resolução ±0,25 °C em vez de ±0,0625 °C |

As duas últimas são **decisão de produto do Ângelo**, não escolha de implementação: mexem na
qualidade do número gravado. A primeira não.

### 9.6 A conta

Um wake de leitura de 25,5 s tem **≈ 8,4 s de espera fixa** por hardware ausente e **≈ 14,8 s** de
anel enchendo. O trabalho pelo qual o aparelho acordou — ler o sensor e anexar um registro — são os
**50 ms** da linha das 7,24 s mais o DECIDE. **Cerca de 91% do wake não é trabalho.**

Projeção com as correntes de bancada do Ângelo (25 mA lendo, 80 mA transmitindo, 2 mA dormindo),
1 leitura/min e telemetria 1:5 — **aritmética, não medição, e a corrente nunca foi medida
(Fase 0 segue bloqueante)**:

| cenário | wake | ciclo útil | média | 18650 de 3400 mAh |
|---|---|---|---|---|
| hoje | 25,5 s | 42,5% | 17,0 mA | ~8,3 d |
| só as esperas fixas fora | ~17 s | 28% | ~12 mA | ~12 d |
| + SAMPLE de volta a volta | ~10 s | 17% | ~8 mA | ~18 d |
| + janela de 4 amostras | ~5,5 s | 9% | ~5 mA | ~28 d |

### 9.7 Ordem sugerida, por risco

1. **Risco nulo, ganho 5,2 s** — as três esperas provadamente mortas nesta imagem: janela de AP +
   *touch settle* (4,52 s, `isScreenTouched( )` é constante de compilação) e a espera pelo Core 1
   (1,51 s, `_core1Ready` não tem escritor no link). Não precisam de portão por modo: são mortas na
   imagem Air inteira, M0 incluído.
2. **Risco baixo, ganho 3,2 s, exige portão `_airActive`** — `delay(1000)`, `delay(800)` do
   `BOOT_STEP_DELAY_MS`, `delay(800)` do splash e o ciclo de energia do CYW43. As imagens de
   release e alpha continuam precisando deles; o portão é "veio da hibernação".
3. **Risco baixo, ganho ~7 s** — tirar a ociosidade de 1000 ms entre conversões durante SAMPLE.
4. **Decisão do Ângelo** — tamanho da janela de média e resolução do DS18.
5. **Higiene, ganho em heap e flash** — temas, `.lng` e offset de display fora do wake.

**Aceite para tudo isto:** T05 (período e janela acordada), T09 (sonda passiva, mede de fora),
T13 (compara wake quieto × barulhento), T16 (teto de 1 registro por wake) e o `--watch` da suíte.
O número que fecha a conta é a corrente, que continua não medida.

---

## 10. Fase 1 implementada e medida — 08/09/2026

Feito o que a §9.7 listou nos passos 1, 2, 3 e a higiene dos temas. **O passo 4 (tamanho da janela
de média e resolução do DS18) NÃO foi feito**: muda o número gravado, e é decisão do Ângelo.

### 10.1 O A/B, mesmo rig, mesmo procedimento

| | antes (`137c8e96`⁻) | depois | Δ |
|---|---|---|---|
| `setup( )` até a primeira fase | 10,73 s | **2,47 s** | −8,26 s |
| fase SAMPLE | 14,81 s | **6,83 s** | −7,98 s |
| wake até o DECIDE | 25,55 s | **9,31 s** | **−16,24 s (2,7×)** |

Confirmado pela **sonda passiva** (`--watch 300`, não toca no aparelho): acordado
**[6,0 / 7,8 / 7,5 / 8,8 / 12,8] s** (o de 12,8 s é o wake de telemetria), dormindo
[53,0 / 52,8 / 53,0 / 51,8] s, **período 60,5–60,6 s** contra `hist=60 s` — erro de **0,6 s**,
contra os 1,8 s de antes. Ciclo útil **~13%** contra 42,5%.

A previsão da §9 acertou: −8,42 s previstos no `setup( )` contra −8,26 medidos; SAMPLE previsto
~7,5 s contra 6,83 medidos.

### 10.2 O que foi feito, e com que portão

| item | portão | por quê |
|---|---|---|
| janela de AP + *touch settle* | `DisplayManager::kHasTouch` (compilação) | o que os laços consultam é constante de compilação; some da imagem Air **e** da alpha |
| espera por `isCore1Ready( )` | `DisplayManager::kUsesCore1` (compilação) | `_core1Ready` não tem escritor no link |
| `BOOT_STEP_DELAY_MS` | `kUsesCore1` | é substituto da espera acima |
| `delay(1000)`, `delay(800)` do splash, ciclo do CYW43 | `!_airActive` (runtime) | release e alpha continuam precisando; M0 do Air também |
| ociosidade entre conversões | `SensorManager::setFastSampling( )` | ligado em WARMUP, desligado em DECIDE **e** no `air stop` |
| varredura de temas | `!_airActive` | a paleta só é lida pelo display e por `/api/themes` |

Flash: Air **−624 B** (1.025.896 → 1.025.272), alpha **−568 B**, release +8 B. As duas primeiras
economias são código que deixou de existir.

### 10.3 ⚠️ O efeito colateral: a bancada perdeu a corrida pela janela

**A primeira rodada completa depois da mudança deu 9 passou / 7 falhou** — e nenhuma das sete era
asserção: seis eram `serial write failed: (5, 'Input/output error')` e uma era `Connection refused`
na porta 80. **A janela de enumeração USB encolheu junto com o wake**, e `ensure_m0( )` gastava
mais tempo abrindo a porta e lendo um `air status` do que a janela inteira tem.

Como isso foi separado de "o firmware quebrou", que é o que aquele placar parece:

- **112 requisições `/api/status` em M0, 4 minutos, 0 erros.**
- **Delta de FTL na mesma janela: 0.** ⚠️ O primeiro `show system log` da medição devolveu **0**
  num log que tem **8** — o leitor mentiu logo na primeira chamada, exatamente como
  [[validate-the-instrument]] avisa. Os 8 registros (`ctx=219` = `MOD_WEB_POLL`, achado antigo)
  já estavam lá antes e não cresceram.
- **A sonda passiva da §10.1**, que não abre a porta serial nem fala com a web: 9 transições
  limpas, nenhum wake perdido, período dentro de 0,6 s.
- Passaram, na rodada ruim, justamente os testes que dependem de M0 e da web: T02, T06 (com
  `awake_s=8,5` contra 25,0), T06b, T07, T09, T14, T15.

**Conserto na bancada, não no firmware:** `ensure_m0( )` continua caçando a janela por 110 s, e
então **usa a mão**. Um RESET dirige o RUN, que é boot limpo, e boot limpo mantém o `air idle`
inteiro em M0 — uma janela de 300 s em vez de 7. Só quando não há mão o método continua sendo uma
corrida.

🔑 **A regra que fica:** *encurtar o wake encurta o instrumento junto. Um placar que desaba logo
depois de uma otimização de tempo merece primeiro a pergunta "a bancada ainda alcança o aparelho?",
e a resposta tem que vir de um instrumento passivo — o `--watch` mede sem tocar, e foi ele que
separou as duas hipóteses aqui.*

⚠️ **Foram QUATRO camadas de suposição de tempo na bancada, uma escondendo a outra.** Cada conserto
revelava a próxima, e o sintoma mudava de nome sem mudar de causa — todas vinham de constantes
calibradas para um wake de 25 s e um boot de 10 s:

| # | sintoma | causa |
|---|---|---|
| 1 | `serial write failed: (5, EIO)` | `if not self.ser` só testa se o handle existe; **todo sono re-enumera o Pico**. → `Target.alive( )` + `reopen( )` |
| 2 | testes seguintes em cascata | `ensure_m0( )` era pura caça de janela. → cai para **RESET da mão** após 110 s (boot limpo = janela de 300 s, não 7) |
| 3 | `air status unparsable: '…boot: net ok'` | o `air stop` era escrito **dentro do banner de boot** e se perdia. → confirmar o console **antes** de agir |
| 4 | `air status unparsable: '…Graph cache preload done'` | `reset_input_buffer( )` limpa o que CHEGOU, não o que está em voo; e um boot limpo **transmite log por ~15 s**. → drenar até a linha ficar quieta, e esperar `[AIR] boot: done` depois de um reset |

Havia uma quinta: o `stop_on_wake` do `hibernate_and_observe( )` reimplementava à mão o que o
`ensure_m0( )` já faz — e sem a queda para a mão. Passou a reusar.

Placar por rodada, **mesma firmware**, só a bancada mudando:
**9/7 → 13/3 → 12/4 → 12/4 → 14/2**. Os testes que passaram em todas elas são justamente os que
medem o produto: T06 (`awake_s` 25,0 → **8,5–8,8**), T09, T12, T13 (`quiet` 23,8 → **7,4–7,6 s**,
`loud` 29,2 → **8,5–12,9 s**), T14, T17.

**Rodada final (`suite8`): 14 passou, 2 falhou, 1 xfail, 1 pulado.**
- ✅ **T05 `awake_s=[8,8; 8,5]`** contra `[23,8; 24,0]` da manhã, `period_err` 1,8 s.
- ✅ **T16 deu ZERO registros de preâmbulo por wake** (o teto é 1, o valor de 07/09 era 1).
- ❌ T15 caiu na mesma corrida: a linha de alarme sai no fim do wake e a porta morre logo atrás.
- ❌ T11 — ver abaixo.

✅ **RESOLVIDO — T11 `history_integrity`: era o arquivo do dia poluído, e agora está MEDIDO.**
Janela de **32 minutos ciclando sem ninguém tocar**, avaliando só os registros dela:

```
52 registros: 48 no ritmo (94,1%)  1 curto (2 s)  2 longos (76 s)  0 para trás  → PASSA
o mesmo critério sobre o arquivo do dia inteiro:  680/869 = 78,3%  → REPROVA
```

**A cadência do firmware está sã; o arquivo do dia é que carrega o dia.** Ele atravessou a troca de
firmware, ~15 reinícios forçados pela recuperação por mão nova e várias janelas de M0 com outra
cadência de gravação. ⚠️ **Isso é uma limitação do T11 como está escrito**, não um defeito que
sumiu: o teste julga o arquivo inteiro, então **uma sessão de bancada intensa sempre vai reprová-lo**
— e a piora monotônica rodada após rodada (12 → 63 → 77 → 103 gaps curtos) foi o próprio indício.
Se ele for ficar como portão, precisa julgar uma janela, não o dia.

#### 09/09 — o T11 foi reescrito, e o motivo não era o que esta seção dizia

A frase acima ("precisa julgar uma janela, não o dia") estava certa e **incompleta**. Ao repetir o
teste depois de **12 minutos de silêncio deliberado**, ele devolveu **os mesmos 17 registros, byte
por byte**, da rodada anterior. Não era o arquivo estar poluído: é que **o arquivo do dia só tem
blocos SELADOS**. Os **15 registros que aquela janela acabara de produzir** estavam no bloco aberto,
em `/api/history/open`, e o teste não lia esse endpoint. **O instrumento não conseguia enxergar
justamente aquilo que estava esperando.** É a mesma armadilha já registrada no F23 (medir pelo delta
do arquivo do dia dá +0 e engana), aparecendo agora do lado do teste.

Somaram-se a isso duas coisas que o critério antigo contava contra o firmware sendo comportamento
correto:

* o firmware **grava um registro logo depois do boot sem esperar o intervalo**
  (`_histFirstDone`, `src/AppManager_Loop.cpp`) — foi acrescentado justamente porque cada reinício
  custava um minuto nunca amostrado. Como **no Air todo wake é um boot**, gap curto é estrutural;
* todo reinício e toda mudança de `h_int` de outros testes (o T08 põe 2 min) ficam no arquivo do dia.

**O teste agora fabrica a própria janela** e julga só ela. Arma o ciclo, **não toca no aparelho** por
7 minutos enquanto observa a presença no USB — que diz quando o aparelho esteve realmente acordado
sem falar com ele — e então lê o histórico completo (selado **+** aberto). São quatro perguntas:

1. **monotonicidade**, sobre tudo o que o aparelho tem — arquivo corrompido é corrompido, seja de
   quem for a culpa — e também sobre os registros nascidos dentro da janela;
2. **todo registro carrega carimbo de um instante em que o aparelho estava comprovadamente
   acordado.** Esta é a segunda falha do T11 dita de forma mensurável: registro carimbado no meio do
   sono é medição que não foi tomada. **Nenhuma checagem de espaçamento pode ver isso** — uma rajada
   retrodatada no intervalo nominal tem contagem certa e gaps de manual; a prova está no `--selftest`,
   com um vetor em que o `gap_report` aprova e as janelas de vigília reprovam;
3. **um registro por wake** — no Air o intervalo de wake É o intervalo de histórico
   (`AppManager_Air.cpp`), então acordar sem gravar é medição perdida e gravar sem acordar é medição
   inventada;
4. **os gaps dentro da janela batem com o `h_int`**.

**Medido na bancada, 09/09 — duas rodadas seguidas, uma não vale** (`--only T11`, código final):

```
01:00   6 registros / 6 wakes completos em 420 s, skew -0,9 s, 44 selados + 10 abertos
01:08   6 registros / 6 wakes completos em 420 s, skew -0,2 s, 44 selados + 19 abertos
        pior margem 0,0 s (folga 10 s)   ← todo registro caiu DENTRO de uma vigília observada
        no ritmo=5  curtos=0  longos=0  para trás=0     → PASSA nas duas
```

Repare no que separa as duas rodadas: **selados parados em 44, abertos indo de 10 a 19.** Todo o
trabalho dos oito minutos entre elas ficou no bloco aberto — que é exatamente o que o teste antigo
não lia, e é por isso que ele devolvia números idênticos rodada após rodada.

⚠️ **A folga de 10 s é generosa e a medição diz isso**: a pior margem foi **0,0 s**, ou seja a folga
não foi necessária nenhuma vez. Ela começou em `intervalo/2` e isso era **vacuidade disfarçada de
tolerância** — o aparelho fica acordado ~9 s a cada 60, então alargar cada vigília em 30 s dos dois
lados faz as vigílias se encostarem e **nenhum carimbo pode cair fora**. O valor está reportado a
cada rodada para poder ser apertado com evidência, não com gosto.
⚠️ **Armadilha do roteiro que quase custou a medição:** depois de 32 min ciclando o aparelho está em
M1, e **em M1 não existe servidor web** — o download do `/history` nunca ia acontecer. É preciso
trazer para M0 (mão ou console) ANTES de ler; os registros já gravados não são afetados por isso.

O que dizia o texto anterior desta seção:
De manhã, antes da mudança: 437 registros, 12 gaps curtos, 10 longos, 0 para trás (PASS). Depois:
**684 registros, 63 curtos, 47 longos, 0 para trás, mais longo 131 s** (FAIL — 83,9% no ritmo
contra o piso de 90%). **A monotonicidade continua intacta**, então o defeito histórico desta linha
não voltou.
⚠️ **O arquivo do dia usado nessa avaliação NÃO é medição limpa:** atravessa a troca de firmware,
~12 reinícios forçados pela recuperação por mão nova, e várias janelas de M0, onde a gravação segue
outra cadência. **Essa é a hipótese, e ela NÃO foi testada.** O que fecha a questão é uma janela de
~30 min ciclando sem ninguém tocar, avaliando só os registros dela. O contra-argumento a favor do
firmware é que o `--watch` mediu o período em **60,5–60,6 s** contra `hist=60` — que é exatamente a
cadência que o T11 diz não estar batendo.

### 10.4 O passo que sobra: resolução do DS18B20 — e por que hoje ela não economiza nada

Com a ociosidade removida, **a fase SAMPLE virou tempo de conversão puro**: dez amostras, uma
atrás da outra, cada uma custando exatamente o que o sensor leva para converter. E esse tempo é
função direta da resolução escolhida:

| resolução | passo (LSB) | conversão (datasheet) | SAMPLE = 10× | wake até DECIDE | ciclo útil a 1/min |
|---|---|---|---|---|---|
| 12 bits (hoje) | 0,0625 °C | 750 ms | 7,5 s (**medido 6,83**) | **9,31 s** | 15,5% |
| 11 bits | 0,125 °C | 375 ms | 3,75 s | ~6,2 s | 10,4% |
| 10 bits | 0,25 °C | 187,5 ms | 1,88 s | ~4,4 s | 7,3% |
| 9 bits | 0,5 °C | 93,75 ms | 0,94 s | ~3,4 s | 5,7% |

Na conta de energia (mesmas correntes de bancada — 25 mA lendo, 80 mA transmitindo, 2 mA dormindo
— a 1 leitura/min e telemetria 1:5; **aritmética, a corrente segue não medida**):

| cenário | wake | média | 18650 de 3400 mAh |
|---|---|---|---|
| antes de hoje | 25,5 s | 16,95 mA | ~8,4 d |
| **hoje, 12 bits** | **9,31 s** | **8,16 mA** | **~17,4 d** |
| 11 bits | ~6,2 s | 6,41 mA | ~22,1 d |
| 10 bits | ~4,4 s | 5,35 mA | ~26,5 d |
| 9 bits | ~3,4 s | 4,82 mA | ~29,4 d |

**A leitura interessante é que o retorno cai rápido.** Sair de 12 para 11 bits compra **4,7 dias**
por meio grau de passo; de 11 para 10, mais 4,4 dias; de 10 para 9, só **2,9 dias** — porque abaixo
de ~4 s de wake o consumo já é dominado pelo `setup( )` e pelo próprio sono, não mais pelo sensor.
**11 ou 10 bits é onde a troca compensa; 9 bits paga pouco e custa meio grau.**

🔴 **MAS: hoje nenhuma dessas linhas é alcançável, e o motivo é uma constante.**
`DS18B20_CONVERSION_TIME_MS` (`SystemDefs_Time.h`) é **750 ms fixos**, e é o único relógio que o
driver usa:

```c
else if (_ds18.state == DS18B20Driver::DS_WAITING) {
 if (now - _ds18.timer >= DS18B20_CONVERSION_TIME_MS) {   /* 750, sempre */
```

`setDs18Resolution( )` programa o registrador do chip, e o chip passa a converter mais rápido — mas
o firmware **continua esperando os 750 ms**. Ou seja: **baixar a resolução hoje piora a medição e
não devolve um milissegundo.** Antes dos números acima valerem, a espera precisa seguir a
resolução configurada (93,75 / 187,5 / 375 / 750 ms). É uma tabela de quatro linhas, mas é uma
mudança no caminho de leitura do sensor e **não foi feita** — junto com o tamanho da janela de
média, é o que resta da Fase 1 e depende da decisão do Ângelo sobre a qualidade do número gravado.

⚠️ **A janela de média é a outra metade da mesma conta.** `MOVING_AVG_WINDOW = 10` multiplica
qualquer tempo de conversão. Uma janela de 4 a 12 bits daria SAMPLE de ~3,0 s sem tocar na
resolução — e as duas alavancas se multiplicam: janela de 4 **e** 10 bits dariam SAMPLE de ~0,75 s.
