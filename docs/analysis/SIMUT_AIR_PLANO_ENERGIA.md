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
| `_netMgr->begin( )` = CYW43 + `WiFi.begin` | **2,88 s** | **sim — é a proposta do Ângelo** |
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

### 3.2 O critério "hoje é dia de telemetria"

O estado necessário **já está na flash** — o cursor de telemetria guarda o instante do último
registro enviado:

```
telemetriaVence =  cfg.telInterval > 0
                && (agora − ultimoEnviado) * 1000 ≥ cfg.telInterval
                && agora ≥ proximaTentativa        /* backoff persistido */
```

- Primeira linha: telemetria desligada mantém o rádio **desligado para sempre**, que já é hoje o
  maior ganho de bateria disponível.
- Segunda: é um critério de **tempo**, que é o que foi pedido, e não de contagem. Um só `read` do
  cursor, sem varrer o diretório de histórico como faz `refreshPendingCount( )`.
- Terceira: é o "não vale a pena ficar acordado" do pedido. **E exige trabalho novo:** hoje o
  backoff mora na RAM e o sono o apaga, então todo wake tenta de novo como se nada tivesse
  falhado. Ele precisa virar um `proximaTentativa` persistido, escrito **só quando um envio
  falha** (escritas limitadas).

### 3.3 O backoff não pode mais esticar a leitura

Hoje `sleepMs = max(backoffMs, histSleepMs)`: a punição da telemetria **atrasa a medição**. No
desenho novo isso está errado por construção — o alarme é sempre a cadência de leitura, e o
backoff só decide se o rádio sobe. O histórico não pode pagar pelo pecado do coletor.

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

### Fase 1 — encurtar o wake (independe da arquitetura nova; faça primeiro)

| id | alavanca | ganho esperado |
|---|---|---|
| L1 | pular a janela de detecção de AP quando não há tela | **−4,5 s** |
| L2 | pular o `delay(1000)` num boot M1 | −1,0 s |
| L3 | não subir web, telemetria, mDNS, BT nem preload de cache em M1 (F13) | −2,7 s |
| L4 | critério de estabilidade "N amostras válidas" (`stabSamples`, default 3) em vez de `bufferFull( )` | **−10 a −12 s** |

**Resultado esperado: 27–31 s → 8–10 s**, com o rádio ainda ligado em todo wake. Mensurável pela
sonda, sem mudar nenhuma decisão de produto. Aceite: T09 e o `--watch` da suíte.

### Fase 2 — o wake sem rádio

| id | item |
|---|---|
| R1 | persistir `proximaTentativa` (backoff) em `air.bin`, escrito só em falha de envio |
| R2 | decidir `radioNesteWake` **antes** de `_netMgr->begin( )` (FS já montado) |
| R3 | pular `_netMgr->begin( )`, init de telemetria e NTP quando a decisão for "não" |
| R4 | política de LED: intocado sem rádio (bit em `AirConfig.flags`) |
| R5 | F04 — carimbar o registro com `epoch_anterior + slept + acordado` |
| R6 | o alarme passa a ser sempre `h_int`; o backoff sai da conta do alarme |

**Resultado esperado: wake de leitura em 4–6 s, wake de telemetria em 10–25 s.**
Aceite: T09 mede as duas janelas e prova que são diferentes; a corrente medida na Fase 0 se repete
com o rádio desligado; T08 prova o carimbo offline.

### Fase 3 — a cadência pedida

Nada a construir: `h_int` já aceita **1..1440 minutos** e `telInterval` já é em ms. Só configurar
e medir 1 min / 15 min por algumas horas, conferindo o histórico com `h5_block_anchors`.

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
