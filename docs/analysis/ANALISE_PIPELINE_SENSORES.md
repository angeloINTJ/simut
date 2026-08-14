# Análise Ponta a Ponta — Leitura e Armazenamento dos Sensores

**Data:** 2026-08-11
**Versão do Firmware:** 2.1.4-beta
**Target:** Raspberry Pi Pico W (RP2040)

Este documento descreve o caminho completo de um valor medido: do sinal elétrico
no pino até o byte no flash — e o que o firmware faz, em cada etapa, quando algo
dá errado. Complementa `ANALISE_INSTABILIDADE_SENSORES.md` (histórico dos bugs
de aquisição) e substitui a parte de gravação de `ANALISE_SISTEMA_HISTORICO.md`,
que documenta o formato V4 aposentado — desde a 2.1.x o único gravador é o V5.

---

## 1. ARQUITETURA GERAL

```
driver físico (PIO / I2C)                                  ── Core 0, sempre
  └→ SensorManager::processPeriodicReads()                 (máquinas de estado assíncronas)
       └→ handleSensorResult()          — histerese 3 erros / 5 acertos
            └→ pushChannelSample()      — exige isfinite
                 ├─ RingBuffer[canal]   — 10 amostras CRUAS
                 ├─ rawValue[canal]     = média aparada (2+2 de 10 descartadas)
                 └─ avgValue[canal]     = rawValue → curva de calibração
                      │                   ← ÚNICO valor que consumidores leem
                      ├→ display / alarmes / API web       (RAM, tempo real)
                      ├→ histórico V5:
                      │    encoder RAM (bloco de 1 h)
                      │      ├→ /history/.wip     (snapshot por REGISTRO)
                      │      └→ /history/AAAAMMDD.h5  (selo por HORA/dia/schema)
                      └→ telemetria     (.h5 + hora aberta lida da RAM)
```

Tudo roda no **Core 0**. `SensorManager::update()` é chamado em `core0Yield()`
(`src/AppManager_Events.cpp:470`), ao fim de cada iteração do laço principal —
não há tick fixo; cada sensor tem seu `readInterval` e as máquinas de estado
avançam de forma oportunista, sem nunca bloquear. O Core 1 (render TFT) apenas
consome `avgValue`, e é **pausado** durante programação de flash (§7).

### 1.1 As três camadas de metadados

| Camada | Arquivo | O que declara |
|---|---|---|
| Canais (grandezas) | `src/sensors/SensorChannelTable.h:47-50` | letra, largura de bits, escala, faixa plausível, alarme de fábrica, preset de exibição |
| Drivers | `src/sensors/SensorHelpers.h:192-241` | `channelMask` (quais canais o chip reporta) + pinos e papéis |
| Slots | `SensorRecord` em `src/SystemDefs_Records.h:110-133` | 16 slots (GPIO 0–15), 139 B cada: tipo, pinos, ROM, hwId, limites de alarme por canal |

Canais existentes: `CH_TEMP` (letra `t`, 16 bits, ×100, com sinal), `CH_HUM`
(`u`, 10 bits, ×10), `CH_PRESS` (`p`, 14 bits, ×10), `CH_LUX` (`l`, 24 bits,
×100). O `channelMask` é o que impede o BMP280 — que declara {temp, press} com
buraco em umidade — de gravar a compensação de lixo do registrador de umidade
que ele não tem.

---

## 2. CADÊNCIAS

| Sinal | Intervalo padrão | Janela de conversão | Estratégia |
|---|---|---|---|
| DS18B20 (temp) | 1 000 ms | 750 ms fixos (pior caso 12 bits) | conversão em **lote paralelo**: todos os pinos disparam juntos, um timer só |
| DHT22 (temp + umid) | 2 000 ms | timeout 150 ms | **sequencial**, um sensor por vez, via PIO |
| BME280 (t+u+p) / BMP280 (t+p) | 5 000 ms | 15 ms (forced mode, osrs ×1) | um driver por (barramento, endereço); independentes entre si |
| Temperatura da placa (ADC RP2040) | 1 s, só com a tela de status aberta | — | `analogReadTemp()` direto, sem filtro, **não vai ao histórico** |
| Histórico (gravação) | 1 min (config. 1–1440) | — | amostragem **nunca bloqueia**; só o snapshot de flash adia |

Origens: `sensorDefaultIntervalMs()` (`src/sensors/SensorHelpers.h:133-147`);
750 ms em `src/SystemDefs_Time.h:63-73` (pior caso mantido fixo mesmo em
resolução menor, para tolerar variância de IRQ/flash); 150 ms em
`src/SystemDefs_Time.h:83`; 15 ms em `src/sensors/BME280Driver.h:28`;
intervalo de histórico em `src/SystemDefs_Records.h:332-342`.

---

## 3. CADA SENSOR INDIVIDUALMENTE

### 3.1 DS18B20 — 1-Wire, temperatura

**Leitura** (`src/SensorManager.cpp:523-620`). Quando o primeiro DS18B20 vence
seu intervalo, o manager dispara `requestTemperatures` em **todos** os pinos com
DS18B20, espera os 750 ms num único timer e coleta todos — o custo da conversão
é pago uma vez, não por sensor (todos ficam sincronizados ao mais adiantado).
O 1-Wire é bit-bang por máquina PIO (`pio0`, lib `OneWirePIO_RP2040`).

**Validação, em ordem:**
1. CRC-8 Dallas (poly 0x8C) do scratchpad, na lib (`getTemperatureValidated`);
2. portão de plausibilidade: fora de −50…+150 °C → "Out of Range"
   (`src/SensorManager.cpp:608`);
3. histerese e filtro comuns (§5).

**Identidade e antifraude.** Cada sonda tem ROM de 64 bits gravado de fábrica:
- verificação a cada **5ª leitura**: relê o ROM e compara com o configurado
  (`src/SensorManager.cpp:585-601`); divergiu → `hardwareMismatch` =
  **quarentena**: buffers limpos, valor NAN, "Access Denied" no log;
- **auto-recuperação**: a cada 10 ciclos pulados o ROM é reverificado — se a
  sonda certa voltou ao pino, a quarentena cai sozinha; uma sonda diferente
  continua bloqueada (`src/SensorManager.cpp:547-564`);
- em paralelo, `checkAndAutoHealSensors()` roda a cada **3 s** no laço
  (`src/AppManager_Loop.cpp:236-241`, corpo em
  `src/AppManager_Sensors.cpp:19-58`) fazendo a mesma checagem por fora, com
  log de "sensor ausente" limitado a 1×/min por slot;
- ROM **zerado** = sonda não pareada: aceita qualquer DS18B20 no pino, sem
  quarentena possível. O pareamento acontece no boot seguinte ao
  provisionamento web: lê o ROM, adota-o no slot e **migra a linha de
  calibração** da chave por-hwId para a chave por-ROM
  (`src/AppManager_Sensors.cpp:147-172`).

**Resolução:** 9–12 bits configurável (padrão 12), aplicada por broadcast no
boot (`src/AppManager_Boot.cpp:629`).

### 3.2 DHT22 — fio único, temperatura + umidade

**Leitura** (`src/SensorManager.cpp:622-677`). Totalmente assíncrona via PIO
(`pio1`, lib `DHT22PIO_RP2040`), **um sensor por vez** — com N DHT22, as
leituras são serializadas conforme cada um vence os 2 s (mínimo do datasheet).

**Validação:** checksum do payload de 5 bytes (2 umid + 2 temp + 1 soma) na
lib; timeout duplo — o da máquina PIO e um teto de 150 ms no manager. Qualquer
desfecho ruim vira `handleSensorResult(false, …)` com mensagem classificada
("Sensor Timeout" / "Checksum Error").

**Histórico de silêncio:** a falha de `begin()` do PIO era descartada e o driver
seguia cutucando uma máquina que não era dele (bloco compartilhado com o rádio
CYW43 no Pico W); hoje `ready=false` transforma cada entrada em no-op e o boot
loga alto (`src/sensors/DHT22Driver.h:45-58`, `src/SensorManager.cpp:75-91`).

### 3.3 BME280 / BMP280 — I2C, temp + pressão (+ umidade só no BME)

**Barramento** (`src/SensorManager.cpp:184-283`):
- preferência por **I2C de hardware** (`Wire`/`Wire1`) quando o par SDA/SCL
  mapeia num periférico válido (`i2cPeripheralForPins`,
  `src/sensors/SensorHelpers.h:257-270`); zero custo de PIO;
- fallback para bit-bang PIO **com WARN explícito** — custa ~1,6 ms de IRQs
  desligadas por transação (causa C1/C3 de `docs/CONCURRENCY.md`);
- antes do `Wire.begin()`, `recoverBus()`: se um escravo ficou segurando SDA
  após reflash (MCU resetou, sensor não), solta com até 9 pulsos de SCL + STOP
  manual (`src/sensors/BME280Driver.h:178-204`);
- até 2 sensores por barramento (0x76 + 0x77), com contabilidade de endereços
  por (SDA, SCL) refeita a cada init — era estática e um reload "roubava" o
  endereço de si mesmo (`src/SensorManager.cpp:114-135`).

**Probe resiliente** (`src/sensors/BME280Driver.h:215-247`): 2 tentativas × 2
endereços, watchdog alimentado entre cada uma. O chip recarrega a NVM de
calibração ~2 ms após reset e responde chip-ID 0x00 se sondado cedo demais —
era o "morto após `pio run -t upload`, vivo após power cycle".

**Retipagem por chip-ID** (`src/SensorManager.cpp:255-279`): 0x60 = BME280,
0x58 = BMP280. Os dois são indistinguíveis por fora; se o usuário provisionou o
tipo errado, o firmware adota o que o silício declara, corrige o conjunto de
canais e **persiste** a correção (`src/AppManager_Sensors.cpp:114-129`), para o
histórico do dia não ganhar uma coluna de umidade permanentemente NAN.

**Leitura:** forced mode — uma medição por comando, o sensor dorme entre elas;
15 ms de espera; leitura em rajada + compensação Bosch (datasheet rev 1.6
§4.2.3) na lib `BMx280PIO_RP2040`.

**Validação** (`src/sensors/BME280Driver.h:258-280`):
- BMP280: umidade **forçada a NAN pela flag em cache** `isBME280()` — o guard
  antigo relia o chip-ID por I2C logo após a rajada, glitchava, e `h = inf`
  vazava para a média;
- sanidade por canal: T fora de −40…85 °C → NAN; H fora de 0…100 % → NAN;
  P fora de 300…1100 hPa → NAN;
- falha de I2C → "I2C Read Error" (conta na histerese).

A pressão entra pelo mesmo `pushChannelSample(s, CH_PRESS, p)` dos demais
canais (`src/SensorManager.cpp:714`) — encerrou a era em que a calibração de
pressão era write-only.

### 3.4 Temperatura da placa (ADC interno do RP2040)

Lida sob demanda a 1 Hz apenas com a tela de status aberta
(`src/AppManager_Loop.cpp:341`). Sem filtro, sem alarme, sem histórico —
sinal de diagnóstico vivo. (Ver achado em §9 sobre a tela de stats.)

---

## 4. SCAN DE HARDWARE

`startScan()` varre GPIO 0–16 com máquina de estados não bloqueante
(`src/SensorManager.cpp:382-508`): por pino, reset 1-Wire + presença (espera
1,2 ms) → leitura de ROM; sem resposta, tenta DHT22 (request + wait); ao final,
probe de BMx280 por PIO nos pinos padrão 4/5, nos dois endereços. O scan roda
dentro de `update()` e suspende as leituras periódicas enquanto ativo.

---

## 5. TRATAMENTO COMUM A TODOS OS SINAIS

### 5.1 Filtro — média aparada sobre janela de 10

`RingBuffer` de **10 amostras cruas** por canal por sensor
(`MOVING_AVG_WINDOW`, `src/SystemDefs_Limits.h:23`). Com o anel **cheio**:
ordena, descarta as 2 menores e as 2 maiores, média das 6 centrais
(`src/SensorManager.cpp:37-61`) — um espeto elétrico isolado não move o valor.
Com menos de 10 amostras (aquecimento pós-boot): média simples, deliberado para
não distorcer o warm-up (`src/SensorManager.cpp:729-742`). Buffer de ordenação
estático — zero heap no caminho quente.

### 5.2 Calibração — curva de até 5 pontos sobre a média

`CalibCurve` (`src/sensors/CalibCurve.h`): guarda o **offset** em até 5 âncoras
de valor cru; interpolação linear por partes ou cúbica monótona
(Fritsch-Carlson/PCHIP — escolhida porque **não** cria overshoot entre âncoras);
fora do intervalo medido o offset da ponta é **mantido, nunca extrapolado**.
`n=1` degenera no offset constante legado; NAN atravessa intacto.

Ordem deliberada (`src/SensorManager.h:64-76`): **o anel guarda cru e a curva é
aplicada sobre a média filtrada** — editar uma curva em runtime tem efeito
imediato, sem 10 amostras de correção velha na janela, e a correção não move o
corte de outliers.

Persistência em `calib.csv` (LittleFS), chaveada por **ROM** (DS18B20 pareado)
ou por **`<letra><hwId>`** por canal (DHT22, BMx, DS18B20 não pareado); carga no
boot em `loadAndCalibrateSensors()` (`src/AppManager_Sensors.cpp:174-223`),
dirigida pelo `channelMask` — grandeza nova carrega offset sem editar nada ali.

### 5.3 Histerese de erro — 3 para cair, 5 para voltar

`handleSensorResult()` (`src/SensorManager.cpp:337-380`):
- **3 falhas consecutivas** → `inErrorState`, log com código classificado por
  substring da mensagem (Timeout / Checksum / CRC / Range / Missing / Mismatch);
- **5 sucessos consecutivos** → recuperação logada ("Sensor recovered");
- leituras boas **durante** o estado de erro não entram no anel — as 4
  primeiras pós-falha são quarentena de reaquecimento, descartadas;
- contadores OK/erro alimentam o `MetricsManager`.

### 5.4 Portão anti-infinito

`pushChannelSample` exige `isfinite`, não `!isnan`
(`src/SensorManager.cpp:750-761`): a compensação de umidade do BMP280 produz
**+INF**, que passa por `isnan` — e o printf do newlib-nano imprimia "inf" como
"NaN", mascarando o vazamento na forense.

### 5.5 Publicação entre contextos

Novo valor seta `_newDataAvailable` com `__atomic_store_n(RELEASE)`; o
consumidor drena com compare-exchange ACQ_REL (`src/SensorManager.cpp:760-769`).
Display e alarmes leem `avgValue` a cada volta do laço.

### 5.6 Alarmes

`checkAlarmConditions()` (`src/AppManager_HistoryAlarm.cpp:498-561`), a cada
volta após 5 s do boot: **cada canal declarado** de cada sensor ativo contra seu
par `chMin[c]/chMax[c]` da config (o modelo antigo de 4 campos nomeados não
tinha onde pôr alarme de pressão). Sensor em erro e valores não finitos são
pulados. Dispara som + máscara de slots no display, com janela de silêncio;
canal não configurado carrega a faixa plausível inteira da tabela — nunca
dispara sozinho.

---

## 6. ARMAZENAMENTO — HISTÓRICO V5

### 6.1 Portões de tempo e cadência

`processHistoryLogging()` roda a cada `historyInterval` (padrão **1 min**;
overlay `HistoryConfigData` em `reserved[48..51]`) — gate em
`src/AppManager_Loop.cpp:269-301`:

- a amostragem **não é bloqueada** por toque nem tarefa pesada: o registro
  sempre cai no encoder em RAM (um memcpy); só o snapshot de flash adia;
- **primeiro registro do boot não espera o intervalo cheio**: dispara assim que
  `time(nullptr)` for real, com retry de 2 s — o gate usa o relógio cru de
  propósito, porque `getEpoch()` devolve o provisório do build (2025-09-20) e
  arquivaria o registro dois anos no passado;
- em `processHistoryLogging` (`src/AppManager_HistoryAlarm.cpp:283-310`):
  epoch ≤ 1,6e9 → skip com warn-once (e log de recuperação ao voltar);
- em `writeHistoryEntryV5` (`src/StorageManager.cpp:1935-1941`): recusa
  epoch < `HIST_EPOCH_MIN` e futuro > agora + 24 h.

### 6.2 Montagem do registro

(`src/AppManager_HistoryAlarm.cpp:311-412`)

- **Schema** derivado dos slots ativos (`buildH5Schema`,
  `src/StorageManager.cpp:1879-1920`): um canal por grandeza por sensor,
  `id = slot×4 + canal` (estável, nunca reciclado), `kind`
  (temp/umid/press; lux vira GENERIC com escala ×1 — não cabe 24 bits num
  int16) e `scaleExp` derivado da escala da tabela (×100 → −2, ×10 → −1);
- cada valor é `avgValue` (filtrado + calibrado) escalado para **int16**, clamp
  em ±32767; **sentinela NAN = 0x8000** para sensor ausente ou em erro — custa
  1 bit no bloco, e uma queda transitória **não** é mudança de schema;
- registro em que **nenhum** canal casou é recusado com log warn-once
  ("timestamp válido sem dado parece dado"); só um registro que realmente
  aterrissou fecha a janela do primeiro-sample do boot.

### 6.3 Encoder em RAM — o bloco de uma hora

`HistoryV5Encoder` (`src/HistoryV5.h`): até **60 registros crus** por bloco
(1 h no intervalo padrão). `add()` é o caminho quente e não comprime nada; a
compressão inteira acontece no selo: keyframe + deltas zigzag em bitstream
MSB-first, com **fallback RAW** para bloco incompressível — possível justamente
porque as amostras cruas foram mantidas. Cada bloco leva CRC-16/CCITT-FALSE,
`t0`, contagem e **envelope min/max por canal** no cabeçalho: gráficos de
semanas leem cabeçalhos, não payloads, e um bloco corrompido custa 1 h, não o
dia. Sem heap, sem FPU; ~2,1 KiB estáticos.

**Regra da casa:** a hora ainda aberta **só existe no encoder** — quem lê apenas
`.h5` fica até 1 h atrás. Consumidores alcançam a RAM por
`h5RamCount()/h5RamRecord()` (`src/StorageManager.h:360-362`) ou
`h5StreamOpenBlock()` (schema + bloco PARTIAL, `src/StorageManager.cpp:2247-2259`).

### 6.4 Tolerância a queda de energia — o `.wip`

(`src/StorageManager.cpp:2016-2024, 2216-2245, 2261-2331`)

- após **cada registro**, o bloco aberto inteiro é fotografado em
  `/history/.wip` (nunca append: ou é o bloco atual, ou nada — um `.wip`
  meio-atualizado que ainda passasse CRC "reproduziria um bloco que nunca
  existiu");
- se toque ou tarefa pesada seguram o flash, o snapshot **adia** e o laço
  retenta a cada 2 s (`H5_WIP_RETRY_MS`, varredura em
  `src/AppManager_Loop.cpp:305-323`) — a medição em si nunca se perde;
- no boot, `recoverWipV5()` valida o snapshot pelo decodificador normal
  (CRC + schema compilado; §14-4: **nunca reparar** chunk) e o adota no arquivo
  do dia — ou descarta com log. Roda antes de o watchdog armar, por isso é toda
  coberta por `Core1FlashPause` + `WdtWindow(30 s)`;
- perda máxima por corte de energia: **1 registro**. Reboot voluntário drena o
  `.wip` no gancho pré-reboot (`flushHistoryBatch`).

Custo assumido: até 1 440 reescritas de `.wip`/dia ≈ 2,6 mil erases por
bloco/ano contra 100 mil nominais — endurance não é o gargalo; o duty cycle de
lockout do Core 1 é, e por isso a escrita cede a vez ao toque.

### 6.5 Selo e arquivo do dia

(`src/StorageManager.cpp:1935-2013` e `2116-2214`)

O bloco fecha por três motivos:
1. **contagem** — 60 registros;
2. **virada de dia** — um bloco jamais cruza dois arquivos: o nome vem do `t0`
   do próprio bloco; se o selo da virada falha, o dia antigo **mantém** o bloco
   (adotar o dia novo emendaria o registro de hoje no bloco de ontem);
3. **mudança do conjunto de sensores** — `onSensorSetChangedV5()` sela PARTIAL
   e o arquivo ganha um novo chunk SCHEMA (§3.7-2), numerado a partir do
   arquivo (não da RAM — dois reboots no dia geravam três schemas "seq 1");
   os blocos antigos continuam legíveis pelo schema vigente quando escritos.

Arquivo `/history/AAAAMMDD.h5` (dia LOCAL) abre com SCHEMA; arquivo cujo SCHEMA
de abertura não parseia é apagado e recriado ("apendar nele enterraria blocos
bons atrás de lixo"). **Falha de selo:** o registro novo é recusado e o bloco
retido por até **5 tentativas** (`H5_SEAL_MAX_FAILS`) antes de ser dado por
perdido e o encoder reiniciado — fronteira deliberada entre "descartar 60
registros por um timeout transitório de mutex" e "parar de gravar para sempre",
com log distinto em cada desfecho (`h5_seal_retry` / `h5_seal_lost` /
`h5_rollover_seal_*`).

### 6.6 Proteções de flash

(`src/StorageManager.cpp:40-87`)

- `FLASH_OP{}`: mutex do FS com tentativas de 100 ms até 5 s, watchdog
  alimentado no espinho, métricas de duração (>50 ms ≈ um erase de 4 KB);
- `Core1FlashPause` (RAII): pausa o render do Core 1 em volta de qualquer
  program/erase — o LittleFS do arduino-pico **não** faz lockout multicore
  (`idleOtherCore()` é no-op sem `setup1/loop1`) e o fetch XIP do Core 1
  durante um erase trava o árbitro QSPI (família de reboots D-C1);
- `WdtWindow(30 s)` nos selos; `enforceStorageLimit()` antes de criar arquivo
  novo (cota de retenção do diretório).

---

## 7. CONSUMIDORES

| Consumidor | Fonte | Como lê |
|---|---|---|
| Display / alarmes | `avgValue` (RAM) | a cada volta do laço |
| Min/max diário no boot | cabeçalhos `.h5` | só envelopes por bloco — 24 leituras/dia, sem decodificar payload (`preloadMinMax`, `src/AppManager_HistoryAlarm.cpp:198-281`) |
| Gráficos (TFT e web) | `.h5` + bloco aberto | walk de cabeçalhos; decodifica apenas os blocos da janela |
| CSV (botão web) | `.h5` via `/download` | decodificação em JS no navegador (não passa pelo `.simx`) |
| Telemetria | `.h5` + RAM | cursor por epoch; conta pendências por cabeçalhos e decodifica no máximo 1 bloco "a cavalo" do cursor (`TelemetryManager.cpp:1640-1745`); alcança a hora aberta por `h5RamRecord` |

---

## 8. RESUMO — FALHA → DEFESA

| Falha | Defesa | Onde |
|---|---|---|
| Espeto elétrico numa amostra | média aparada (descarta 2+2 de 10) | `SensorManager.cpp:37-61` |
| Dado corrompido no fio | CRC-8 Dallas (DS18B20) / checksum 5 B (DHT22) | libs PIO |
| Valor absurdo mas plausível | portões de faixa: −50…150 °C; sanidade BME por canal | `SensorManager.cpp:608`; `BME280Driver.h:275-277` |
| +INF da compensação BMP280 | `isfinite` no push + umidade NAN por flag em cache | `SensorManager.cpp:754`; `BME280Driver.h:272` |
| Sensor ausente / fio partido | timeout classificado + histerese 3/5 + log 1×/min | `SensorManager.cpp:337-380`; `AppManager_Sensors.cpp:47-55` |
| Sonda DS18B20 trocada no pino | ROM check a cada 5 leituras → quarentena → auto-recuperação a cada 10 ciclos | `SensorManager.cpp:547-601` |
| Flapping (recupera e cai) | 5 sucessos exigidos; 4 primeiros pós-falha descartados | `SensorManager.cpp:356-366` |
| I2C preso pós-reflash | `recoverBus`: 9 pulsos SCL + STOP manual | `BME280Driver.h:178-204` |
| Probe antes da NVM do BMx | 2 tentativas × 2 endereços, WDT alimentado | `BME280Driver.h:219-243` |
| Tipo BME/BMP provisionado errado | retipagem por chip-ID + persistência | `SensorManager.cpp:255-279` |
| PIO esgotado no boot | `begin()` checado + ERROR explícito; família desativada de forma audível | `SensorManager.cpp:75-91` |
| Queda de energia | `.wip` por registro → perda máx. 1 registro; recuperação validada por CRC no boot | `StorageManager.cpp:2216-2331` |
| Reboot voluntário | flush do `.wip` no gancho pré-reboot | `flushHistoryBatch` |
| Relógio ausente / provisório | gates 1,6e9 + `time(nullptr)` cru p/ 1º registro; recusa futuro >24 h | `AppManager_Loop.cpp:283-299`; `StorageManager.cpp:1937-1941` |
| Registro sem nenhum dado real | `matched == 0` → recusado com warn-once | `AppManager_HistoryAlarm.cpp:389-399` |
| Bloco cruzando a meia-noite | arquivo escolhido pelo `t0` do bloco; selo forçado na virada; falha mantém o dia antigo | `StorageManager.cpp:1945-1970` |
| Mudança do conjunto de sensores | novo chunk SCHEMA no mesmo arquivo; dia permanece legível | `StorageManager.cpp:2147-2165, 2333-2354` |
| Selo falhando (mutex, FS) | recusa registros por até 5 tentativas antes de descartar o bloco | `StorageManager.cpp:1992-2012` |
| Corrupção no flash | CRC-16 por bloco; chunk ruim é pulado e contado — nunca reparado | `HistoryV5.h` (§3.7-4, §14-4) |
| Flash × Core 1 (XIP/QSPI) | `Core1FlashPause` + `FLASH_OP` (mutex + WDT) + `WdtWindow` | `StorageManager.cpp:40-87` |
| Toque durante gravação | amostra sempre em RAM; snapshot adia e retenta a cada 2 s | `StorageManager.cpp:2020-2024`; `AppManager_Loop.cpp:305-323` |
| Wrap de `millis()` (49,7 dias) | `timeSince`/`timeReached` com subtração assinada | `SystemDefs_Time.h:158-197` |
| Travamento geral do Core 0 | watchdog HW 8 388 ms alimentado incondicionalmente; `scratch[6]` guarda o uptime p/ autópsia | `src/main.cpp:37-59` |

---

## 9. ACHADOS ABERTOS

1. **Min/max da temperatura da placa nunca é alimentado.** As telas de
   stats/gráfico leem `_cachedMin/Max[MINMAX_SLOT_BOARD_TEMP]`
   (`AppManager_HistoryAlarm.cpp:457-473`, `AppManager_Graph.cpp:83-87`), mas
   nenhum código escreve nesses índices — a tela existe, o dado não.
2. **`ANALISE_SISTEMA_HISTORICO.md` descreve o V4** e permanece útil como
   referência do formato legado (leitura de arquivos `.sim4` antigos), mas não
   descreve mais o gravador — `writeHistoryEntryFlashV4` hoje é um stub que
   retorna `false` (`StorageManager.cpp`).

---

## 10. REFERÊNCIAS RÁPIDAS

| Tema | Arquivo |
|---|---|
| Tabela de canais | `src/sensors/SensorChannelTable.h` |
| Metadados por driver (mask, pinos) | `src/sensors/SensorHelpers.h` |
| Aquisição e máquinas de estado | `src/SensorManager.cpp` |
| Curva de calibração | `src/sensors/CalibCurve.h` |
| Carga de calibração / pareamento | `src/AppManager_Sensors.cpp` |
| Gate de gravação + varredura `.wip` | `src/AppManager_Loop.cpp` |
| Montagem do registro + alarmes | `src/AppManager_HistoryAlarm.cpp` |
| Formato V5 (espec. executável) | `src/HistoryV5.h` + `docs/HistoryV5_Instrucoes_Implementacao*.md` |
| Gravador V5 + proteções de flash | `src/StorageManager.cpp` |
| Constantes de tempo | `src/SystemDefs_Time.h` |
| Config persistida (SensorRecord) | `src/SystemDefs_Records.h` |
