# HistoryV5 — notas de implementação (2.0.1-alpha)

| Campo | Valor |
|---|---|
| Documento | Registro do que foi construído, medido e decidido |
| Especificação | [`HistoryV5_Instrucoes_Implementacao.md`](HistoryV5_Instrucoes_Implementacao.md) — continua sendo a fonte de verdade do **formato** |
| Versão | 2.0.1-alpha (anterior: 2.0.0-alpha, formato V4 `.sim4`) |
| Bancada | Raspberry Pi Pico W `E6642815E34C1824`, 6 slots provisionados, 11 canais |
| Data | 2026-07-31 / 08-01 |

Este documento não repete a especificação. Ele registra **o que o código faz**,
**onde ele se afasta do documento normativo e por quê**, e **o que foi medido**.
Onde formato e documento divergirem, o documento vence; onde política de
implementação divergir, vale o que está aqui, com a justificativa.

---

## 1. Mapa do que foi entregue

| Arquivo | Papel |
|---|---|
| `src/HistoryV5.h` / `.cpp` | Núcleo: CRC, zigzag, `BitWriter`/`BitReader`, `HistoryV5Encoder`, `HistoryV5Decoder`, `HistoryV5Scan`, helpers de SCHEMA. Sem dependência de Arduino — compila no host. |
| `src/StorageManager.{h,cpp}` | Dono da política: schema derivado dos slots, caminho quente, selagem, `.wip`, rotação, purga §11, correção retroativa, leitor sequencial compartilhado. |
| `src/AppManager_HistoryAlarm.cpp` | Monta o vetor de valores na ordem do schema e chama `writeHistoryEntryV5`. `preloadMinMax` passou a ler só cabeçalhos. |
| `src/AppManager_Loop.cpp` | Agenda o snapshot `.wip` (10 min). |
| `src/AppManager_Core.cpp` | Correção retroativa de timestamps ligada ao sync de NTP. |
| `src/AppManager_Boot.cpp` | Purga §11 + adoção do `.wip` no boot. |
| `src/WebManager_History.cpp` | Gráfico com dois caminhos (envelope / decode), export CSV e bundle. |
| `src/TelemetryManager.cpp` | Coleta de lote e contagem de pendentes. |
| `tools/history_v5.py` | Implementação de referência e oráculo: encoder+decoder plenos, `--selftest`, `--convert`, `--convert-v4`, `--dump-csv`, `--stats`, `--synth`. |
| `tools/check_history_v5_parity.py` + `tools/h5_parity/` | Portão de paridade bit-exata firmware × referência. |
| `test/test_history_v5/` | 33 testes unitários nativos (`pio test -e native_history_v5`). |

---

## 2. Decisões que se afastam do documento normativo

Cada uma é uma divergência consciente. As quatro primeiras foram necessárias
para o código funcionar; a quinta é escopo ampliado.

### 2.1 O schema é derivado dos slots, não a tabela do §4

**O documento diz** (§4, §8): grave sempre `kCompiledSchema`, uma tabela fixa de
12 canais (11 temperaturas + 1 umidade), "que coincide com a ordem dos campos do
`BinaryHistoryRecord` em RAM".

**O que existe hoje**: essa tabela descreve o layout **pré-V4**
(`ambientTemp`/`ambientHum` + `sensors[10]`), que saiu do código junto com o
codec v2/v3. O `BinaryHistoryRecord` atual é `sensors[16] + humidity[16] +
pressure`, e a bancada tem dois DHT22 (temperatura + umidade) e um BMP280
(temperatura + pressão).

**Consequência de seguir ao pé da letra**: o V5 gravaria temperatura dos slots
0–10 e umidade só do slot 10 — a umidade dos dois DHT22 e a pressão do BMP280
sumiriam do histórico. Isso viola R1 ("funcionalidade preservada"), que é
inviolável, para cumprir §8, que é escolha de escopo desta entrega.

**O que foi feito**: `StorageManager::buildH5Schema()` monta o conjunto de
canais a partir dos slots provisionados — a mesma fonte que o `buildMeasureSchema`
do V4 usava — e grava no chunk SCHEMA. O núcleo continua parametrizado por
schema, exatamente como o §5 exige. É para isso que R4 e R5 existem.

O `id` do descritor é `slot × MAX_SENSOR_CHANNELS + canal` (0..127):

```
slot 0  DS18B20  temp   -> id 0
slot 1  DS18B20  temp   -> id 8
slot 3  DHT22    temp   -> id 24     hum  -> id 25
slot 4  BMP280   temp   -> id 32     press-> id 34
slot 6  BME280   temp   -> id 48     hum  -> id 49    press -> id 50
slot 10 DHT22    temp   -> id 80     hum  -> id 81
```

O §3.2 pede que o `id` "sobreviva a remapeamento físico de GPIO/ROM". Num
descritor de 4 bytes não cabe o `hwId`, e no SIMUT o slot **é** o GPIO — mudar
um sensor de pino é reprovisioná-lo. A regra §3.7-2 já responde a isso: o
conjunto mudou, grava-se um SCHEMA novo no mesmo arquivo, e os blocos anteriores
continuam legíveis sob o schema que valia quando foram escritos. Nada é
reciclado *dentro* de um schema, que é o que o formato precisa garantir.

### 2.2 `scaleExp` vem da tabela de canais, não dos valores "típicos" do §4

O §4 diz "% UR — típica −2". A `SensorChannelTable.h` deste firmware usa escala
**10** para umidade e para pressão, e 100 para temperatura. Como `scaleExp` é
por canal e vai gravado no SCHEMA, o arquivo continua autodescritivo: umidade
sai com `scaleExp = −1`. "Típica" no documento é orientação, não norma.

**Limitação registrada**: luminosidade (`CH_LUX`) é 24 bits × 100 na tabela de
canais e não cabe num `int16`. O canal é mantido, mapeado para
`H5_KIND_GENERIC` com `scaleExp = 0` — lux em unidades inteiras, 0..32767.
Nenhum sensor da bancada produz lux hoje; se algum passar a produzir, a perda é
de duas casas decimais, não do canal.

### 2.3 `add()` recusa registro fora do alcance do RAW

**O documento diz** duas coisas que não podem valer ao mesmo tempo:

- §14-1: "Bloco não coube no buffer? Impossível por construção: `seal()` compara
  com RAW e o RAW cabe em `H5_BLOCK_MAX_BYTES` por definição."
- §14-2: "Relógio saltou? Símbolo de resync. **Não abra bloco novo por causa de
  tempo.**"

O RAW endereça registros por `dt u16` a partir de `t0` (§3.6). Um salto de
relógio maior que 65535 s dentro de um bloco deixa o bloco **sem forma RAW** — e
um bloco incompressível sem forma RAW não tem cota nenhuma: 60 registros × 16
canais em símbolos absolutos dão ~2619 B contra os 2118 B de
`H5_BLOCK_MAX_BYTES`. O `seal()` devolveria 0 e o bloco se perderia em silêncio.

Isso foi **encontrado pelo teste de propriedade**, não por leitura: a varredura
aleatória incluía saltos de 100 000 s e o `seal()` falhou.

**O que foi feito**: `add()` devolve `false` quando `epoch < t0` ou
`epoch − t0 > 65535`, e o chamador fecha o bloco e abre outro — exatamente o
caminho que já existia para "bloco cheio". Mantém §14-1 (do qual R3 depende) e
limita §14-2 ao caso que ele realmente descreve: jitter e correção de NTP
continuam dentro do bloco, pelo símbolo de resync. Só um salto acima de ~18 h
força bloco novo, e um bloco que cobrisse 18 h teria envelope min/máx inútil
para gráfico de qualquer forma.

Com isso o `seal()` passa a ter garantia de tamanho: `payLen ≤ rawLen ≤ 2006 B`
e `chunk ≤ 2118 B` — verificado por teste no pior caso exato
(`test_worst_case_block_fits_the_bound`).

### 2.4 `seal()` tem uma segunda forma que transmite em vez de montar

O §5 especifica `seal(uint8_t* out, size_t cap, uint8_t extraFlags)`. Ela existe
e é o que os testes usam. Mas o modelo de RAM do §5 ("estado ≈ 90 B + buffer
estático `H5_BLOCK_MAX_BYTES`") não fecha: com 90 B de estado não há como
produzir o RAW, que R3 exige.

O encoder guarda as **amostras** (`_epoch[59]` + `_v[59][16]` = 2124 B) em vez
de um bitstream incremental. Isso torna o caminho quente um `memcpy` e deixa
`seal()` livre para escolher entre comprimido e RAW. Para não precisar também de
um buffer de saída de 2118 B, existe `sealStream(sink, ctx, flags)`: três
passagens sobre as amostras — dimensionar o payload, calcular o CRC de um chunk
que nunca está inteiro na RAM, e emitir — com janela de 64 B. É por ela que o
`StorageManager` grava em flash.

`test_seal_and_sealstream_agree` compara byte a byte as duas formas para
`nCh ∈ {1, 6, 11, 16}`; se divergissem, o firmware gravaria arquivos que ele
próprio rejeitaria na leitura.

### 2.5 Escopo ampliado: um leitor, não cinco

O §6 lista os módulos a integrar. O V4 tinha **cinco cópias** do laço de
decodificação (gráfico web, export CSV, bundle, telemetria ×2, preload, gráfico
TFT), cada uma com seu `HistV4State` de ~2,8 KiB e sua própria janela de refill
— e foi por isso que o bug A1 (refill pós-falha) precisou ser corrigido em cinco
lugares de uma vez.

O V5 tem **um** leitor, no `StorageManager` (`h5OpenDay` / `h5NextRecord` /
`h5NextBlock` / `h5SeekTo`), com um único buffer de bloco. Todos os consumidores
passaram a usá-lo. É a maior parte dos 44 KB de RAM estática que sumiram.

---

## 3. Como o dia funciona

```
sensor tick (1/min)
   │
   ▼
writeHistoryEntryV5()          100% RAM, sem flash, sem heap
   │  memcpy de nCh int16 + epoch para o bloco aberto
   │
   ├── bloco cheio (60 reg) ──▶ sealHourV5(partial=false)
   ├── virou o dia ──────────▶ sealHourV5(partial=true)  §14-6
   ├── mudou o conjunto de sensores ─▶ onSensorSetChangedV5()  §3.7-2
   │
   └── a cada 10 min (AppManager_Loop) ──▶ flushWipV5()
                                            regrava /history/.wip inteiro;
                                            o bloco continua aberto

boot: purgeNonV5History()  §11
      recoverWipV5()       adota o .wip válido no arquivo do seu t0, ou descarta
      ensureH5Schema()

sync de NTP: shiftHistoryTimeV5(delta, arquivo, a partir do boot)  §7.3
```

Um bloco nunca cruza dois arquivos: o arquivo é escolhido pelo `t0` do bloco.
Essa é a mesma armadilha que o A2 corrigiu no batch do V4 — amostras de 23h58
drenando no arquivo do dia seguinte.

### 3.1 O `.wip`

Contém **exatamente um** chunk DATA selado com `PARTIAL`, sem SCHEMA. É
regravado inteiro **a cada registro aceito** (nunca acrescido: um `.wip` meio
atualizado que ainda passasse no CRC reproduziria um bloco que nunca existiu).
No boot é validado pelo decodificador comum — sem tratamento especial, sem
tentativa de conserto (§14-4) — e acrescentado ao arquivo do dia a que seu `t0`
pertence. É apagado quando o bloco que ele espelha é selado, senão o boot
seguinte reproduziria dados que já estão no arquivo.

**Era a cada 10 min até a emenda E10 (10/08/2026).** O intervalo não era folga
de implementação: era o R8 escrito, "perda máxima de 10 min". Três coisas
mudaram junto com ele, e as três estão em `HistoryV5_Emendas_Rev2.md` §E10 — o
gancho pré-reboot (o `commit_all` reiniciava sem gravar nada), o snapshot por
registro, e a separação entre amostrar e escrever, porque condicionar a
amostragem aos gates deixava minutos sem medição alguma.

### 3.2 Correção retroativa (§7.3) — o que o V4 anunciava e não fazia

O `handleTimeSync` do V4 registrava `APP_NTP_CORRECTING` e em seguida
`APP_NTP_CORRECTED`, com este comentário no meio:

> `/* V4: variable-length records — in-place correction unsupported. */`

Ou seja: tudo que fosse gravado antes do NTP subir ficava com o relógio
provisório para sempre, e o log dizia que tinha sido corrigido.

No V5 o único carimbo absoluto é o `t0` do cabeçalho DATA. `shiftHistoryTimeV5`
faz stream-rewrite para `.tmp` + `rename` (mesmo padrão da config), tocando 4
bytes e o CRC por bloco, **limitado a blocos cujo `t0` é do boot atual** — os de
sessões anteriores tinham relógio que já estava certo. O bloco ainda aberto na
RAM anda junto (`HistoryV5Encoder::shiftTime`).

---

## 4. Testes e portões

| Portão | Como rodar | Resultado |
|---|---|---|
| Selftest da referência (§3, vetores) | `python3 tools/history_v5.py --selftest --trials 1000000` | **PASS** — inclui CRC `0x29B1`, zigzag em toda a faixa int16, largura exata de cada prefixo, NAN, resync, RAW, rejeição, troca de schema |
| Propriedade `decode(encode(s)) == s` | idem, 10⁶ séries | **0 falhas em 1 000 000** |
| Unitários do firmware | `pio test -e native_history_v5` | **33/33** |
| Paridade bit-exata firmware × referência | `python3 tools/check_history_v5_parity.py --cases 200000` | **0 divergências em 200 000 casos / 4 013 387 registros** — bytes do encoder, decode do firmware e decode da referência sobre os bytes do firmware |
| Golden replay (dado real) | `--convert-v4` dos `.sim4` da bancada + comparação com o que o próprio device decodificou | **0 divergências em 1 325 registros × 11 canais** |
| Autodescrição (R4) | `--dump-csv` de um arquivo gravado pelo firmware, sem `kCompiledSchema` | **CSV correto**, valores conferem com `/api/status` |
| Validadores e CLI (regressão) | `pio test -e native`, `-e native_cli` | 40/40 e 29/29 |

O portão de paridade é o que impede as duas implementações de derivarem. Elas
foram escritas separadamente a partir do mesmo documento; um `diff` de bytes é a
única prova de que concordam.

---

## 5. Medições

### 5.1 Compressão em dado real da bancada

Os dois `.sim4` do dispositivo (1 325 registros, 36 h, 11 canais) convertidos
com `--convert-v4`:

| Arquivo | V4 `.sim4` | V5 `.h5` | Ganho |
|---|---|---|---|
| `20260731` (709 reg, 11 canais) | 5 367 B | 3 816 B | **1,41×** |
| `20260730` (616 reg, 8 canais) | 3 748 B | 2 757 B | **1,36×** |

Em B/registro e projeção diária (11 canais, 1 reg/min):

| | B/registro | KiB/dia | Autonomia em 1 MiB a 86% |
|---|---|---|---|
| Formato plano `epoch+int16` | 26,0 | 36,6 | 24 dias |
| V4 `.sim4` | 7,57 | 10,6 | 82 dias |
| **V5 `.h5`** | **5,38** | **7,57** | **116 dias** |

Histograma de símbolos do arquivo real: 66,1% dos valores em 1 bit (Δ = 0),
24,4% em 5 bits, 9,2% em 9 bits, 0,2% em 14 bits, 0,1% absolutos —
**2,76 bits por valor** e 1,53 bit por timestamp. Cabeçalhos de bloco são 27%
do arquivo (82 B por bloco de 60 registros = 1,37 B/registro): é o preço do
CRC por bloco, do keyframe e do envelope min/máx, e é o que torna o caminho de
envelope possível.

Dado sintético é mais ruidoso de propósito (ruído gaussiano de ±4 contagens por
minuto em cada canal) e fica em 9,5 B/registro / 13,3 KiB/dia. O número real da
bancada é o de cima.

### 5.2 Latências no alvo (medidas no dispositivo, campo `readMs`)

`/api/history_multi` passou a reportar `path` e `readMs` — o tempo **do
dispositivo**, sem o envio. Sem isso toda medição por HTTP é dominada pelo
Wi-Fi: um gráfico de 30 dias transmite 286 KB, o que na bancada são ~5 s
independentemente de quão rápido foi a leitura.

Com 30 dias sintéticos em flash (410 KB, 30 arquivos, 720 blocos):

| Operação | Orçamento §10 | Medido | Veredito |
|---|---|---|---|
| Gráfico 24 h — decode (24 blocos, 1 440 reg) | ≤ 60 ms | **107,6 ms** | acima (1,8×) |
| Gráfico 24 h — **envelope** (24 cabeçalhos) | — | **5,8 ms** | — |
| Gráfico 7 d — envelope (161 blocos) | ≤ 25 ms | **44,1 ms** | acima (1,8×) |
| Gráfico 30 d — envelope (669 blocos) | ≤ 80 ms | **187 ms** | acima (2,3×) |
| MAX — envelope (721 blocos) | — | **204,8 ms** | — |
| Decodificar 1 bloco (60 reg × 11 canais) | ≤ 1 ms | **4,48 ms** | acima (4,5×) |

**Os orçamentos do §10 não foram cumpridos.** O que os limita:

- **Caminho de envelope: 0,28 ms por bloco**, estável em 7 d, 30 d e MAX
  (0,274 / 0,280 / 0,284). São 112 B lidos por bloco, salteando o payload. Ler
  salteado no LittleFS caminha a skip-list do arquivo a cada leitura, então o
  custo é por *chamada*, não por byte. Duas otimizações foram aplicadas e
  medidas: uma leitura por chunk em vez de duas (preâmbulo + cabeçalho) e
  supressão do `seek` redundante quando o cursor já está no lugar. Juntas não
  moveram o número — confirmando que o custo é o acesso indexado do LittleFS,
  não o número de chamadas de `seek`. Os 80 ms do §10 para 720 cabeçalhos
  (0,111 ms/bloco) não são alcançáveis por acesso aleatório nesse sistema de
  arquivos.
- **Caminho de decode: 4,48 ms por bloco** = 74,7 µs por registro. O
  `BitReader::get` foi trocado de leitura bit a bit para byte a byte (ganho
  medido: 121,6 → 107,6 ms no gráfico de 24 h, 12%), o que mostra que o resto
  não é a decodificação: é a leitura do chunk e o par de mutex por registro que
  o laço do handler tira e devolve — o mesmo padrão que o V4 tinha.

  **Corrigido em 01/08 pela medição — a frase acima errava o termo dominante.**
  O handler passou a reportar `blocks`, `loadMs` e `loopMs`, e aceita `emit=0`
  (decodifica e mede sem formatar nem enviar). Com isso o custo se separa, em
  janela de 24 h, 6 canais, 1 062 registros em 32 blocos:

  | | emit=1 (normal) | emit=0 |
  |---|---|---|
  | laço de registros (`loopMs`) | **1 503 ms** | 28,8 ms |
  | leitura + decode (`readMs`) | 44,0 ms | 21,2 ms |
  | só flash (`loadMs`) | 13,8 ms → 0,43 ms/bloco | 6,6 ms → 0,21 ms/bloco |
  | só decodificador | 30,2 ms → 27,9 µs/reg | 14,6 ms → **13,7 µs/reg** |

  Três coisas que a tabela diz e a hipótese anterior não previa: **(1)** o mutex
  por registro existia e custava, mas era ~20% do caminho de leitura (2,88 →
  2,31 ms/bloco ao removê-lo), não o termo principal; **(2)** o chunk era mesmo
  lido duas vezes — `verifyDataCrc()` percorria o payload em pedaços de 64 B e
  `readChunk()` relia tudo —, e unificar as duas numa leitura só com CRC em RAM
  **não moveu o número**, porque essas releituras caíam no cache do LittleFS;
  **(3)** o mesmo `h5DecodeNext( )` custa **o dobro** quando roda entrelaçado
  com a formatação JSON (27,9 contra 13,7 µs), que é a assinatura de recarga de
  código por XIP — entre dois registros o handler roda `snprintf` de seis floats
  e despeja o cache de instruções.

  E o enquadramento que importa: **o decode não é o que faz o gráfico demorar**.
  Dos 1 503 ms do laço, 44 ms são leitura e decodificação — 3%. O resto é
  formatar e enviar. Otimizar o decodificador até zero encurtaria a resposta de
  24 h em 3%; trocar para o envelope encurta de 1,64 s para 0,20 s.

**O que mudou na prática, apesar disso**: o envelope resolve o mesmo gráfico de
24 h em **5,8 ms contra 107,6 ms** do decode — 18× — e é o que torna 30 dias
viável. E ele cumpre R6 de verdade: a decimação por amostragem descarta o que
cai entre as amostras (um pico de um minuto num mês tinha ~1 chance em 72 de ser
desenhado); o envelope emite o mínimo e o máximo **reais** de cada bloco, então
o extremo *é* o ponto. `?mode=decode|envelope` força qualquer um dos caminhos,
que é como estes números foram levantados.

### 5.3 Flash de código e RAM

| | 2.0.0-alpha (V4) | 2.0.1-alpha (V5) | Δ |
|---|---|---|---|
| Folga real de flash¹ | 81 292 B | 80 268 B | **−1 024 B** |
| RAM estática (build) | 122 548 B | 78 488 B | **−44 060 B** |
| Heap total (medido no device) | 139 404 B | 183 464 B | **+44 060 B** |
| Maior bloco contíguo de heap | 29 733 B | 48 522 B | **+63%** |

¹ medida por `arm-none-eabi-size`, não pelo % do PlatformIO — ver
`docs/ANALISE_FLASH_RAM.md`.

O orçamento do §10 é "flash de código adicional ≤ +2 KB": **+1 024 B**, dentro.
Vale dizer que esse número já é líquido da remoção do codec V4 (`HistoryV4.cpp`
saiu do `build_src_filter` do release, R7).

Os 44 KB de RAM não são economia do formato: são as cinco cópias de
`HistV4State` + buffers de header + janelas de refill que os consumidores
carregavam e que o leitor único substituiu. O bloco contíguo de heap é o número
que o BearSSL usa — ver `docs/ANALISE_FLASH_RAM.md`.

RAM do V5 propriamente dita (dentro do objeto `StorageManager`, que vive no
heap): encoder 2 156 B + buffer de bloco do leitor 2 118 B + schema 64 B ≈
**4,3 KiB**, contra ~4,1 KiB que o `HistV4State` + o batch T2.1 ocupavam no
mesmo objeto. O §10 pede "RAM estática adicional ≤ 2,2 KiB"; a estática caiu 44
KB, e o custo real no heap é ~+200 B.

---

## 6. O que ficou de fora

- **Soak de 72 h (§12, A6)** — não cabe na janela desta sessão. O soak é a única
  forma de fechar A6 e a classe R1 (`APP_CORE1_DEAD`) continua aberta e
  independente do formato: ela apareceu uma vez durante os testes de gráfico,
  na mesma assinatura já documentada em sessões anteriores, e não reproduziu em
  4 repetições seguidas do mesmo pedido.
- **20 cortes de energia aleatórios (A4)** — foram feitos cortes dirigidos (§7 do
  relatório), não os 20 aleatórios do critério.
- **Orçamentos do §10** — medidos e **não cumpridos**, com a análise acima.
  Nenhum foi ajustado no documento: o documento é normativo e a medição é o que
  é.

---

## 7. Armadilhas encontradas (para quem mexer nisso depois)

1. **`HistV4MeasureDef` tem 12 bytes, não 10.** As tabelas do `.sim4` são
   `memcpy` direto da RAM, então carregam o padding do compilador: seis `uint8_t`
   + 2 bytes de alinhamento + `uint32_t scale`. Um parser que assuma 10 bytes lê
   a tabela de sensores fora de lugar e falha com `IndexError` no primeiro
   `sensorIdx`.
2. **O V4 empacota bits LSB-first dentro de cada byte; o V5 empacota MSB-first.**
   Trocar os dois no conversor devolve lixo plausível, não erro.
3. **`getHistoryIntervalMin()` devolve minutos e vai até 1440.** Multiplicar por
   60 estoura o `uint16_t` do intervalo nominal — `h5NominalSeconds()` satura.
4. **O `check_logcodes.py` valida cinco tabelas.** Adicionar código de log exige
   `logcodes.tsv` + enum + `LogManager.cpp` + `EVT_NAMES_EN`/`PT` em `WebUI.h` +
   os dois `.lng`. O `--sync-lng` cuida dos packs; o resto é manual.
5. **`isValidHistoryFileName()` conferia 13 caracteres** (`YYYYMMDD.sim4`). Com
   `.h5` são 11 — e é essa função que o coletor de lixo usa para decidir o que
   pode apagar, então deixá-la desatualizada isentaria os arquivos novos da
   rotação para sempre.
