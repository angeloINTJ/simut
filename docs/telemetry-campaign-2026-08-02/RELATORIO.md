# Telemetria SIMUT — campanha de desempenho e sobrevivência

**Data:** 2026-08-02 · **Firmware:** 2.0.1-alpha (`pico_w_test`) · **Alvo:** Raspberry Pi Pico W
**Metodologia e bancada:** [METODOLOGIA.md](METODOLOGIA.md) · **Defeitos detalhados:** [DEFEITOS.md](DEFEITOS.md)

---

## 1. Resumo executivo

Foram medidos os quatro transportes (HTTP, HTTPS, MQTT, MQTTS) em quatro tamanhos
de lote cada, exercidos **43 modos de falha de servidor** escritos para errar de
propósito, drenado o histórico do dispositivo duas vezes e conferido valor a valor
o que chegou contra o que está no flash.

**O que está sólido:**

- **Vazão**: até 49,7 registros/s. Os quatro transportes escalam linearmente com
  o tamanho do lote, sem falha e sem reboot em 16 corridas de desempenho.
- **Integridade**: 100% dos 1375 registros conferidos batem com o flash,
  decodificados pelo codec de referência.
- **Tratamento de erro de rede**: 40 dos 43 modos de falha foram absorvidos como
  uma linha de log e um backoff — DNS que não resolve, SYN engolido, conexão
  recusada, RST, meia resposta, lixo binário, CONNACK recusado, broker que morre
  no meio do publish.
- **Cursor**: em HTTP e HTTPS, **zero** registros perdidos em 21 modos de falha.

**O que estava quebrado (14 defeitos; 9 corrigidos e verificados, 5 abertos):**

- **3 modos de falha derrubavam o aparelho em laço de reboot.** Os três foram
  corrigidos e verificados (§7.3): `huge1mb` 4→0, `drip` 4→0 (em duas taxas de
  gotejamento), `tls_slow20` 2→0 (e um caso mais duro, 40 s, também passa).
- **Um payload MQTT acima de 8 KB parava a telemetria para sempre**, em silêncio.
- **A senha MQTT digitada na web nunca chegava ao broker.**
- **O transporte MQTT era invisível para as métricas**, e por isso a cadência
  adaptativa nunca funcionava nele.
- **`/api/ls` truncava listagens sem avisar** — duas listagens do mesmo diretório
  devolviam conjuntos diferentes.
- **O modo CSV emitia um cabeçalho de 7 colunas para linhas de 34.**
- **Uma resposta 4xx/5xx não contava como falha** e era registrada como "HTTP OK".

---

## 2. Desempenho

Cada corrida: 90 s, `tel reset` + `tel sync` antes de abrir a janela (senão a
medição cai dentro de um backoff herdado e mede o temporizador, não o transporte).

### 2.1 Vazão por transporte e tamanho de lote (registros/s)

| lote | HTTP | HTTPS | MQTT | MQTTS |
|---|---|---|---|---|
| 1 | 0,97 | 0,71 | 2,10 | 0,96 |
| 5 | — | — | 4,27 | 4,97 |
| 10 | 10,16 | 6,93 | 9,67 | 9,67 |
| 25 | 25,44 | 16,83 | — | — |
| **50** | **48,61** | **31,67** | **49,67** | **43,89** |

### 2.2 Latência por POST (medida no dispositivo, HTTP/HTTPS)

| lote | HTTP mediana | HTTP máx | HTTPS mediana | HTTPS máx |
|---|---|---|---|---|
| 1 | 14 ms | 22 ms | 506 ms | 556 ms |
| 10 | 20 ms | 35 ms | 550 ms | 640 ms |
| 25 | 29 ms | 34 ms | 610 ms | 697 ms |
| 50 | 46 ms | 64 ms | 715 ms | 884 ms |

### 2.3 O achado de desempenho que muda uma decisão de projeto

**O TLS custa 35% da vazão no HTTPS e 12% no MQTT.**

A razão é estrutural, não de implementação: o HTTPS abre e fecha uma conexão TLS
**por POST** — 490 ms de handshake que aparecem inteiros na latência de lote 1
(506 ms contra 14 ms do HTTP puro). O MQTT paga esse handshake **uma vez** e
mantém a conexão: as quatro corridas MQTTS de 90 s inteiras usaram **1 conexão**.

Para telemetria cifrada com vazão, a recomendação é **MQTTS, não HTTPS**.

O preço do MQTTS é heap: 69 KB livres contra 96–102 KB dos demais, porque o
contexto BearSSL fica residente. Ainda é folga confortável.

### 2.4 Custo de memória

| transporte | heap livre mín | maior bloco mín |
|---|---|---|
| HTTP | 95 656 B | 85 740 B |
| HTTPS | 95 592 B | 74 967 B |
| MQTT | 96 576 B | — |
| MQTTS | 69 056 B | — |

Nenhuma tendência de queda ao longo das corridas: o heap volta ao mesmo valor a
cada ciclo.

---

## 3. Integridade dos dados

Duas verificações independentes.

**Valor a valor.** 1375 epochs recebidos pela telemetria foram comparados campo a
campo contra os mesmos registros lidos dos `.h5` do dispositivo e decodificados
pelo codec de referência (`tools/history_v5.py`): **1375 de 1375 conferem
(100%)**, 0 ausentes no disco.

**Formatos de payload.** Os três modos foram capturados crus no servidor:

- `json` — `[{"ts":1783134000,"tSTM0009":22.00,"tSTM0010":22.86,...}]` ✔
- `custom` — `{"dev":"simut","mac":"28:cd:c1:15:4e:99","data":[{"ts":...,"tSTM0009":23.92}]}` ✔
  (reescrita de chave `t0_ID` → `tSTM0009` e remoção de token sem canal funcionam)
- `csv` — **cabeçalho não descreve as linhas** (defeito D2, corrigido)

---

## 4. Sobrevivência — 43 modos de falha

Estrutura de cada teste: linha de base com servidor bom → falha → recuperação com
servidor bom. A diferença entre o último epoch aceito antes e o primeiro depois
mede se o dispositivo avançou o cursor por cima de dado que ninguém recebeu.

### 4.1 HTTP (14 modos)

| falha | veredito | reboots | reg. perdidos |
|---|---|---|---|
| refused, blackhole, slow20, half, rst, rst_mid, garbage, close_early, syn_blackhole, dns_fail | sobreviveu | 0 | 0 |
| error401, error500 | sobreviveu, **mas falha invisível** (D11) | 0 | 0 |
| **huge1mb** | **REBOOT ×4 + FTL** | 4 | 0 |
| **drip** | **REBOOT ×4 + FTL** | 4 | 0 |

### 4.2 HTTPS (7 modos)

| falha | veredito | reboots | reg. perdidos |
|---|---|---|---|
| tls_blackhole, tls_garbage, tls_rst, tls_refused, tls_error500, tls_blackhole_http | sobreviveu | 0 | 0 |
| **tls_slow20** | **REBOOT ×2 + FTL** | 2 | 0 |

O `tls_blackhole` (aceita TCP e nunca fala) **sobreviver** é a prova de que o
prazo global de handshake TLS que já existia no repositório funciona: o
dispositivo desiste em 15 s e reporta erro. O que ainda matava era o **dreno
pós-falha**, não o handshake.

### 4.3 MQTT (12 modos) e MQTTS (5 modos)

Todos sobreviveram — **zero reboots em 17 modos**. O cliente MQTT trata
corretamente broker ausente, RST no accept, CONNACK que nunca chega, CONNACK
lento, meio CONNACK, CONNACK recusado, queda pós-CONNACK, queda no publish, RST
no publish, lixo binário e PINGREQ ignorado.

---

## 5. Descarga do histórico

### 5.1 Pelo caminho normal (`tel reset`)

| métrica | valor |
|---|---|
| duração | 1009 s |
| envios HTTP | 799 |
| registros aceitos | 39 900 |
| epochs únicos | 39 659 |
| taxa sustentada | 39,5 reg/s |
| reboots | 0 |
| primeiro registro | 2026-07-03 |
| último registro | agora |

### 5.2 O teto de 30 dias

`tel reset` zera o cursor, e `collectBatch` então recusa olhar mais para trás do
que `lastRecorded − 30 dias`. Com **92 dias de histórico no flash**, isso deixa
dois terços do arquivo inalcançáveis pela telemetria, em qualquer tempo de
execução. Não há comando nem campo de configuração que peça mais.

**É política, não limite de armazenamento — e isso foi provado.** Semeando
`/config/t_cursor.bin` com 1 600 000 001 em vez de zero, o `if (lastCursor == 0)`
não dispara e o aparelho transmitiu **o arquivo inteiro**: 124 800 registros em
3038 s (41,07 reg/s), 2496 POSTs, 14,66 MB, **zero reboots**, cobrindo
2026-05-03 → 2026-08-02.

| caminho | epochs únicos | cobertura do que há no flash |
|---|---|---|
| `tel reset` (padrão do produto) | 39 659 | **31,7 %** |
| cursor semeado | 124 609 | **99,64 %** |

Inventário real, obtido por data em vez de confiar no `/api/ls`: **88 arquivos,
125 058 epochs únicos, 0 erros de decodificação**.

---

## 6. Correções aplicadas

Onze defeitos corrigidos, compilando nos dois ambientes
(`pico_w_test` 96,9%, `pico_w_release` 92,3%) e com os 40 testes nativos passando.

| # | defeito | correção |
|---|---|---|
| D10 | servidor lento/grande = laço de reboot | patch de framework: prazo e feed **entre caracteres** na leitura de cabeçalho, mais dreno limitado no `disconnect( )`. Resolve os três casos (§7.3) |
| D8 | payload MQTT > 8 KB = parada permanente | publica registro a registro quando o payload não cabe |
| D1 | senha MQTT descartada | `commit_all` passa a ler `m_pass`; vazio = manter |
| D2 | cabeçalho CSV de 7 colunas para linhas de 34 | cabeçalho passa a nomear as 34 colunas reais |
| D4 | `/api/ls` perdia 1 arquivo a cada lote de 20 | `batchCount < 20 && dir.next( )` — conta antes de avançar; mais `"truncated"` e prazo longo como defesa |
| D5 | cursor pula lote encurtado por heap | cursor segue `batch.back().epoch` nos 3 caminhos de envio |
| D6 | `pending` estoura em 65535 | acumulador de 32 bits saturado, e o mesmo piso de 30 dias do `collectBatch` |
| D11 | 4xx/5xx não contava como falha | ramo próprio, `telFailed++` e log de erro honesto |
| D12 | MQTT invisível às métricas | as duas rotas alimentam `telSent`/`telFailed`/`telTotalBytes`/`telLastLatencyMs` e a média de latência |

Não corrigidos (decisão de produto ou risco documentado): D3 (teto de 30 dias),
D7 (QoS 1/2 oferecido mas não implementável com PubSubClient), D9 (um campo de
pressão por registro).

---

## 7. Revalidação com o firmware corrigido

Firmware gravado (`pico_w_test`, 96,9%) e os testes refeitos. Para não medir um
efeito colateral em vez do alvo, a ordem foi invertida e há reboot entre grupos:
o `huge1mb` esgota o pool de PBUF (D14) e mataria a web de todos os testes
seguintes, então roda por último.

### 7.1 Corrigido e verificado

| defeito | evidência antes | evidência depois |
|---|---|---|
| **D2** cabeçalho CSV | 7 colunas para linhas de 34 | **34/34, `match: true`** |
| **D4** `/api/ls` incompleto | 84 de 88, conjuntos variando entre chamadas | **88 de 88, estável em 4 chamadas, `missing: []`** |
| **D1** senha MQTT | CONNECT com `pass: ""` | **`pass: "benchsecret"`** no fio |
| **D11** 4xx/5xx invisível | `devFail+0`, log "HTTP OK ... code 500" | **`devFail+4`**, e recupera |
| **D10** `huge1mb` | 4 reboots + `[FTL] HW WATCHDOG` | **0 reboots** (3 corridas) |

Fidelidade MQTT: 6 de 7 verificações passam (client id, usuário, **senha**,
keepalive, tópico, retain). A que falha é `qos1_honoured` — D7, não corrigido
porque o PubSubClient não implementa publish em QoS 1/2.

**Sem regressão de desempenho**: HTTP lote 50 = 44,44 reg/s (era 48,61), HTTPS
lote 50 = 27,78 reg/s (era 31,67), 0 falhas e 0 reboots nas duas. Dentro da
variação de bancada.

### 7.2 A causa real do D4 não era a que eu diagnosticou

A primeira hipótese foi o guarda de tempo (`isHandlerOvertime`). Errada: a
listagem corrigida devolvia 84 de 88 com `truncated: false` — o laço terminava
normalmente e ainda assim perdia arquivos. O mecanismo é ordem de avaliação:

```cpp
while (dir.next( ) && batchCount < 20)   // <- avança ANTES de testar o limite
```

Quando `batchCount` chega a 20, `dir.next( )` já moveu o iterador e a entrada é
descartada. **Cada lote cheio perde uma**: 88 arquivos, 4 lotes cheios, 84
listados. E como a ordem de iteração do LittleFS varia, variam quais somem — o
que explica as duas listagens divergentes. Correção: `batchCount < 20 && dir.next( )`.
O marcador `"truncated"` e o prazo maior ficaram como defesa em profundidade.

### 7.3 As três mortes por watchdog — resolvidas

| falha | reboots antes | reboots depois |
|---|---|---|
| `huge1mb` (corpo de 1 MB) | 4 | **0** (3 corridas) |
| `drip` 400 ms/byte | 4 | **0** |
| `drip` 900 ms/byte (mais duro) | — | **0** |
| `tls_slow20` (dorme 20 s) | 2 | **0** |
| `tls_slow40` (dorme 40 s, mais duro) | — | **0** |

Zero linhas `[FTL]` em todos, e as falhas contadas corretamente (`devFail+3/+4`).

**A primeira correção não bastava, e eu cheguei a reportar `drip` como resolvido
antes da hora.** Aquele "sobreviveu" veio de um aparelho já degradado pelo
`huge1mb` imediatamente anterior — com o pool de PBUF esgotado ele nem abria
conexão, portanto nunca alcançava o caminho que trava. Com reboot entre os casos
o resultado real apareceu: 3 reboots.

**A raiz que faltava.** O orçamento global que pus em `handleHeaderResponse` era
checado **entre linhas**, mas o bloqueio está dentro de uma única chamada:

```cpp
String headerLine = _client()->readStringUntil('\n');
```

`readStringUntil` lê caractere a caractere sem alimentar o watchdog. A
400 ms/byte, `Content-Type: application/json\r\n` (32 bytes) consome **12,8 s
numa só chamada** — o dobro do teto de 8,388 s do RP2040. O prazo nunca chegava
a ser consultado.

A correção substitui essa leitura por um laço próprio que honra o prazo **no
meio da linha** e alimenta o watchdog **entre caracteres**, com o mesmo contrato
(acumula até `\n`, não o inclui, devolve o que tem em timeout). O feed é seguro
exatamente porque o prazo faz o laço terminar.

Isso explica por que o `tls_slow20` caiu junto: o handshake TLS lento entra pelo
mesmo `handleHeaderResponse`.

**Sobre os "DATA-LOSS" que aparecem nessas corridas**: são o ruído já
documentado. No `tls_slow20` o gap deu +660 s e no `tls_slow40`, no mesmo
cenário, **−13 140 s** — fisicamente impossível. Ambos com `faultRecs=0` e
`conns=0`: o servidor defeituoso não recebeu nada em nenhum dos dois, então não
há assimetria que sustente perda em um e não no outro.

### 7.4 D14 — o reboot escondia um vazamento de PBUF

Sem os reboots do `huge1mb`, o aparelho sobrevive tempo suficiente para expor um
segundo problema, independente e até então mascarado:

```
show net status → PBUF pool: 12 em uso / pico 12 / 12 total, 1158 falhas
```

O dispositivo continua vivo — IP, link a −42 dBm, CLI serial respondendo — mas o
servidor web fica mudo por falta de buffer. **Não se recupera sozinho**: com a
telemetria desligada o pool segue 12/12 e as falhas sobem (519 → 666). Só
`reload confirm` devolve (`0 em uso / pico 1 / 12 total, 0 falhas`, web 200).

Atribuí isso ao `stop( )` que eu havia acrescentado; removi, refiz e o vazamento
continuou igual. A hipótese estava errada — o vazamento antecede as mudanças
desta campanha, e o que mudou foi ele deixar de ser apagado a cada 8,4 s.

Agravante: `PBUF_POOL_SIZE` foi reduzido de 24 para 12 no patch de lwIP do
projeto, para economizar 18 KB de RAM. A margem é estreita.

### 7.5 Estado da bancada ao final

Configuração original restaurada (`192.168.3.206:8443/api.php`, HTTPS,
intervalo 10 s, lote 50, JSON), usuário descartável `telb` removido, aparelho
saudável: `IP 192.168.3.24 · RSSI −44 dBm · PBUF 1/12 · 0 falhas · web HTTP 200`.
