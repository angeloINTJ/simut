# Análise e Proposta — Segunda Linha de Telemetria (Alarmes)

**Data:** 2026-08-22
**Versão de referência:** v2.3.2-stable (CONFIG_VERSION 20)
**Target:** Raspberry Pi Pico W (RP2040) — Arduino Framework
**Escopo:** proposta de arquitetura para a 2ª linha de telemetria dedicada a alarmes,
com fila em RAM, confirmação de recebimento e payload totalmente editável.

> Nota de interpretação: "branch" aqui foi lido como **segunda linha/stream de dados**
> (paralela à telemetria convencional), não como branch do git — a implementação em si
> pode (e deve) nascer numa feature branch, mas a funcionalidade é um segundo fluxo de
> telemetria. O envio (servidor, porta, credenciais, criptografia TLS) **herda a
> configuração convencional**; o que é próprio da linha de alarmes é o **formato do
> payload** e a **semântica de fila/confirmação**.

---

## 1. O que já existe (levantamento)

### 1.1 Telemetria convencional

| Peça | Onde | O que faz |
|---|---|---|
| Coleta de lote | TelemetryManager::collectBatch — src/TelemetryManager.cpp:523 | Lê arquivos V5 do dia + o bloco ainda aberto na RAM (h5RamCount), a partir do cursor lastSentTimestamp persistido em flash |
| Montagem do payload | buildPayload — src/TelemetryManager.cpp:1473 | 3 modos: JSON (formatLineJsonBuf), CSV (toCsvLine), custom (formatLineCustomBuf) — todos em buffer de pilha, zero heap intermediário |
| Motor de template custom | formatLineCustomBuf — src/TelemetryManager.cpp:1669 | Tokens {TS}, {DHT_ID}, {t0..15}, {u0..15}, {p0..15}; **formas compostas** "<chave>":{<token>} já removem a chave quando o valor está ausente (linhas 1812–1878) — máquina pronta para reuso |
| Envio HTTP | attemptHttpUpload — src/TelemetryManager.cpp:734 | URL montada de telServer/telPort/telPath; TLS conforme telEncryption (setBufferSizes(4096,512), setInsecure() sem cert); header de auth via telApiKey; **2xx = confirmação de recebimento** (linhas 825–838) |
| Envio MQTT | attemptMqttPublish — src/TelemetryManager.cpp:1161 | PubSubClient 2.8; **publish é só QoS 0** (não existe parâmetro de QoS em publish() — PubSubClient.h:151-156); o "ack" hoje é uma janela de 60 ms esperando FIN/RST (linhas 1218–1244); o dispositivo **nunca assina tópico algum** |
| Decisão de projeto existente | WebManager_Commit.cpp:806-811 | D-232-QOS: QoS 1/2 rejeitado no commit porque o transporte não entrega — m_qos só aceita 0 |
| Backoff | resetBackoff/escalateBackoff — src/TelemetryManager.cpp:1363-1391 | 5 s → 300 s com jitter; contadores telSent/telFailed/telRetries |
| Gate de memória | update() — src/TelemetryManager.cpp:357-376 e safeBatchLimit (480–521) | Preflight: < 24 KB livre (TLS) / < 14 KB (plain) aborta o ciclo |
| Debug | armPayloadDump() + "tel dump" — src/TelemetryManager.cpp:1907 | Dump do último payload construído via USB/BT |
| Cursor | setLastSentTimestamp — StorageManager | Persistido; define o que já foi entregue |

### 1.2 Detecção de alarme (existente, apenas local)

- AppManager::checkAlarmConditions() — src/AppManager_HistoryAlarm.cpp:501-564: roda a cada ~5 s
  (AppManager_Events.cpp:497-509, também após salvar limites). Compara
  s.avgValue[c] com cfg.sensors[i].chMin[c]/chMax[c] por canal e aciona
  **som** + **TFT**. É **sem estado**: não detecta borda (reavalia o valor atual a cada ciclo)
  e **pula sensores em inErrorState** (linha 513) — hoje falha de sensor não gera alarme nenhum.
- Limites por canal: SensorRecord.chMin/chMax[MAX_SENSOR_CHANNELS] + alarmsActive
  (SystemDefs_Records.h:110-129).
- Prefixo da grandeza já existe como convenção: channelInfo(ch).letter
  (t/u/p/l — sensors/SensorChannelTable.h:47-50), usado nas chaves JSON
  t{hwid}, u{hwid}, p{hwid} e no HA discovery.

### 1.3 Configuração persistida (o ponto mais sensível)

- SystemConfig — SystemDefs_Records.h:203-270: campos de telemetria
  (telMode, telGlobalTemplate[256], telLineTemplate[512], telLineSeparator[8],
  telEncryption, telTransport, MQTT…) + reserved[64].
- **reserved[64] está CHEIO** (SystemDefs_Reserved.h:22: "further fields need a
  CONFIG_VERSION bump"). Ou seja: os campos novos da linha de alarmes **não cabem como overlay** —
  exigem bump CONFIG_VERSION 20 → 21 com migração (padrão já estabelecido em
  StorageManager::loadAndMigrate, StorageManager.cpp:93 e :608).
- Edição hoje: GET /api/config emite t_glob/t_line/t_sep/t_mode (WebManager_Api.cpp:180-186);
  POST commit_all grava (WebManager_Commit.cpp:819-821); a página web (com Live Preview de
  template) vive em WebUI.h na raiz, compilada para src/WebUI_GZ.h por
  tools/build_webui_gz.py.

---

## 2. Requisitos → decisões de projeto

| # | Requisito do usuário | Decisão proposta |
|---|---|---|
| R1 | Registro separado por grandeza ao passar do limite: {timestamp, id-com-prefixo, valor} | AlarmRecord compacto + **detecção de borda** em checkAlarmConditions (o código atual é sem estado — sem borda, reenviaria alarme a cada 5 s enquanto persistisse) |
| R2 | Valor "err" quando o sensor está em falha | Novos tokens {VAL} (número ou vazio) e {ERR} (literal "err" ou vazio) + **transição para inErrorState também gera registro** (hoje essa transição não gera nada) |
| R3 | Fila **em RAM**, apagada conforme confirmação de recebimento | AlarmQueue — buffer circular estático em RAM (sem heap), consumida como um todo: **a fila É o conjunto pendente**; sem cursor persistido |
| R4 | Payload "totalmente editável, da mesma forma que a convencional" | Generalizar os builders existentes para receberem um FormatConfig {mode, globalTemplate, lineTemplate, separator}; linha de alarmes ganha os 4 campos próprios + editor na web/CLI com preview |
| R5 | Criptografia segue a configuração convencional | cfg.telEncryption idem; mesmo telServer/telPort, mesma chave telApiKey, mesmo transporte telTransport — **um único par de credenciais por device** |

---

## 3. Proposta detalhada

### 3.1 Modelo de dados — AlarmQueue (RAM, estático)

```cpp
struct AlarmRecord {                 // ~11 B empacotado
  uint16_t seq;                      // sequência monotônica do boot (chave do ACK)
  uint32_t epoch;                    // timestamp do disparo
  uint8_t  slot;                     // 0..15
  uint8_t  channel;                  // CH_TEMP/CH_HUM/CH_PRESS/CH_LUX
  int16_t  value;                    // valor ×escala do canal (HIST_NAN_SENTINEL = erro)
  uint8_t  flags;                    // bit0 = err (R2)
};
```

- Capacidade padrão **32 registros** (≈ 352 B + cabeçalho — irrelevante frente ao heap de ~50 KB),
  configurável 1..64 (alarmQueueMax). **Estouro → descarta o mais novo** + métrica
  alarmDropped + log WARN (o mais antigo é o mais importante e o mais provável de ainda
  ser "o alarme ativo"; descartar o mais velho esconderia alarme jamais confirmado).
- seq (uint16, reinicia a cada boot) é o que o ACK referencia — resolve duplicatas e
  registros idênticos (epoch,slot,ch) sem ambiguidade.
- API: push(...), drainBatch(maxN) (só marca "em voo", não remove),
  ackUpTo(seq)/ackList(seq[]) (remove os confirmados), size(), peek().
  Sem new/malloc — array fixo, coerente com a disciplina de heap do projeto.

### 3.2 Produtor — detecção de borda em checkAlarmConditions

- Estado novo (RAM, ~18 B): bitmap de trip por (slot × canal) + bitmap de erro por slot,
  ambos zerados no boot.
- Regras de transição:
  - **ok → fora do limite**: push(epoch, slot, ch, valor).
  - **ok → inErrorState**: push(..., flags|ERR) — o campo valor vira sentinela (R2).
    Requer **deixar de pular** sensores em erro na varredura (hoje continue na linha 513)
    apenas para o caminho de telemetria; o comportamento de som/TFT não muda.
  - **fora → ok / erro → ok**: só limpa o bitmap (sem registro — escopo do pedido;
    "alarme normalizado" pode vir depois como flag opcional ALARM_SEND_CLEAR).
- Debounce leve (sugerido): confirmar a borda em 2 ciclos consecutivos (~10 s) para não
  enfileirar lixo por flapping — avgValue já é média aparada de 10 amostras, então o
  risco é baixo; se quiser zero latência, o debounce é uma constante desligável.
- Timestamp: time(nullptr) como no resto do sistema. Sem NTP o clock é provisório
  (SIMUT_BUILD_EPOCH) — o ACK não depende do epoch, e o servidor deve indexar por
  (deviceId, seq); documentar no contrato (ver §3.4).
- Disparo imediato: após push, marcar _alarmSendPending = true para o ciclo de envio
  não esperar o intervalo (o próximo update() do TelemetryManager envia).

### 3.3 Consumer — reuso dos builders (R4)

Generalizar a assinatura interna (sem mudar o comportamento da linha convencional):

```cpp
struct TelFormatConfig { uint8_t mode; const char* globalTpl; const char* lineTpl; const char* sep; };
String buildPayload(batch, TelFormatConfig fmt);          // convencional e alarmes
int formatLineAlarmBuf(const AlarmRecord&, const TelFormatConfig&, char* dest, size_t cap);
```

- Tokens novos (tabela de formatLineCustomBuf, linha 1669, ganha um braço):
  {TS} (reuso), {ID} → t{hwid} / u{hwid} / p{hwid} / l{hwid}
  (fallback {letra}{slot} — **o prefixo da grandeza é R1**), {HWID}, {SLOT},
  {CH} (letra), {VAL} (número formatado com os decimais do canal; vazio em erro),
  {ERR} (literal "err"; vazio quando ok). {DEV}/{MAC} seguem valendo no template global.
- **O truque das formas compostas já implementado resolve o JSON elegante**: com o template
  {"ts":{TS},"id":"{ID}","v":{VAL},"e":"{ERR}"}, a chave v é removida inteira quando
  {VAL} está ausente e e quando {ERR} está ausente (mesma máquina das linhas
  1812–1878, sem código novo de quoting).
- Defaults de fábrica (migração v21):
  - alarmMode = TEL_MODE_JSON
  - alarmGlobalTemplate = {"dev":"{DEV}","mac":"{MAC}","alarms":[{DATA}]}
  - alarmLineTemplate = {"ts":{TS},"id":"{ID}","v":{VAL},"e":"{ERR}","seq":{SEQ}}
    ({SEQ} a mais — útil ao servidor para deduplicar; opcional no editor)
  - alarmLineSeparator = ","

### 3.4 Transporte e confirmação (R3, R5)

Mesmo telTransport, mesma criptografia, mesmo servidor. Endpoints distintos por convenção:

| Transporte | Envio | Confirmação |
|---|---|---|
| HTTP | POST {telServer}:{telPort}{telPath}/alarm (novo campo alarmPath, default = telPath + "/alarm"), mesmo header de auth | **Resposta 2xx** = confirmação dos registros contidos naquele POST → ackList(seqs do batch) |
| MQTT | publish QoS 0 no tópico {base}/alarm (derivado como mqttStatusTopic, TelemetryManager.cpp:906-911) | **ACK por aplicação** (recomendado): o dispositivo passa a assinar {base}/alarm/ack e o servidor publica {"seq":[...]} após ingerir. A fila é esvaziada pela lista de seqs |

Por que ACK por aplicação no MQTT e não QoS 1:
- O PubSubClient usado **não publica QoS 1** (sem PUBACK) e o projeto já tomou a decisão
  D-232-QOS de não prometer o que o transporte não entrega (WebManager_Commit.cpp:806-811).
- Patching/fork da lib para QoS 1 é a alternativa séria (menos código no servidor), mas cria
  um fork a manter, muda o buffer/loop do cliente para processar PUBACK e contradiz a decisão
  existente. Fica registrada como **alternativa B**, não recomendada agora.
- O custo do ACK por aplicação é pequeno: subscribe(topic, qos=1) já existe na lib
  (PubSubClient.h:175), loop() já roda quando conectado (TelemetryManager.cpp:293-296)
  e o callback só precisa parsear uma lista de seqs. Bônus: **funciona também como
  contra-prova no HTTP** se um dia quiserem (o contrato é o mesmo {"seq":[...]}).

Semântica de entrega: *at-least-once* em ambos (reenvio dos não confirmados a cada ciclo
de alarmes). Duplicata é barata e o seq permite o servidor deduplicar — o oposto
(at-most-once) violaria R3.

Cadência do envio de alarmes:
- Gatilho imediato após push (§3.2) + reenvio periódico dos não confirmados a cada
  ALARM_RETRY_INTERVAL (proposta: 15 s, constante; sem backoff agressivo de 300 s —
  alarme não pode esperar como telemetria em massa pode).
- Mesmos gates do ciclo convencional: CAS _isSending separado (ou compartilhado —
  decidir na implementação; recomendo compartilhar o CAS para nunca haver dois POSTs/TLS
  simultâneos no Core 0), lockHeavyTask(), preflight de heap, WdtWindow,
  pular quando heavyRendering/interação do usuário (AppManager_Loop.cpp:228-241).
- HTTP: reusar _httpSecurePtr/cert/headers exatamente como attemptHttpUpload.

### 3.5 Schema persistido — bump CONFIG_VERSION 20 → 21

Novo bloco em SystemConfig (padrão do projeto exige bump + migração):

```cpp
bool    alarmTelEnabled;           // master switch da 2ª linha
uint8_t alarmMode;                 // 0 JSON / 1 CSV / 2 custom
uint8_t alarmQueueMax;             // 1..64 (default 32)
char    alarmPath[32];             // default: telPath + "/alarm"
char    alarmGlobalTemplate[256];  // defaults §3.3
char    alarmLineTemplate[512];
char    alarmLineSeparator[8];
```

- Migração v20→v21 em loadAndMigrate: preenche defaults; configurações existentes
  ficam intactas (mesma disciplina do resto do arquivo).
- CSV da linha de alarmes: layout fixo seq;ts;id;valor ("err" no valor quando falha),
  sem header gigante — linha de alarme tem 1 registro lógico por linha, não 34 colunas.

### 3.6 Edição — Web, CLI e preview (R4)

- **API**: GET /api/config emite a_en/a_mode/a_qmax/a_path/a_glob/a_line/a_sep +
  a_pending (profundidade da fila, útil no dashboard); POST commit_all ganha a
  seção correspondente com os mesmos validadores da linha convencional (modo 0..2,
  tamanhos, strict parsing estilo m_qos).
- **WebUI** (WebUI.h → regen via tools/build_webui_gz.py): segunda área "Payload de
  Alarmes" na página de telemetria — mesmos controles (modo, template global/linha,
  separador) e o mesmo **Live Preview**, agora resolvendo {ID}/{VAL}/{ERR} com um
  registro de exemplo (ok e err).
- **CLI/BT** (CommandManager): "alarm show", "alarm set <campo> <valor>",
  "alarm dump" (espelho de "tel dump", via armAlarmPayloadDump()), "alarm flush"
  (limpar fila manualmente).
- **Página de status/painel**: profundidade da fila + alarmSent/alarmAcked/alarmDropped.

### 3.7 Métricas, logs e observabilidade

- MetricsManager: alarmQueued, alarmSent, alarmAcked, alarmFailed, alarmDropped,
  alarmErrRecords, alarmPending (último = AlarmQueue::size()).
- Logs LOG_CODE: disparo de alarme-tele (INFO), envio OK/falha, ACK recebido,
  estouro de fila (WARN), "alarm dump" (console).
- Documentar o contrato do servidor: formato dos payloads por modo + semântica do ACK
  (2xx HTTP; {"seq":[...]} no tópico de ack MQTT) + regra de deduplicação por
  (mac, seq).

### 3.8 Orçamento de RAM/CPU (RP2040)

| Item | Custo |
|---|---|
| AlarmQueue 32×11 B | ~0,4 KB (64 registros: ~0,8 KB) |
| Bitmaps de borda/erro | ~20 B |
| Payload de 32 alarmes (JSON ~110 B/linha) | ~3,5 KB transitório — dentro do preflight existente |
| TLS | já residente/contabilizado pela linha convencional (reuso do cliente) |
| CPU | push/ack são O(1)..O(n) de 32; sem varredura de flash (nada de collectBatch de alarme) |

Não há varredura de arquivos: a fila é a fonte única (R3). A linha convencional continua
intocada em comportamento — o refactor de buildPayload é mecânico e coberto pelos
testes existentes + "tel dump" comparado.

---

## 4. Alternativas avaliadas (e por que não)

| Alternativa | Custo | Veredito |
|---|---|---|
| Gravar alarmes no histórico V5 e reusar collectBatch + cursor | Confirmação por cursor de tempo é frágil (registros fora de ordem já morderam o projeto — deliveredCursor), e "apagar da RAM ao confirmar" não combina com arquivo de dia | ✗ |
| MQTT QoS 1 via fork do PubSubClient | Fork a manter, PUBACK no loop, contradiz D-232-QOS | Alternativa B — só se o servidor não puder publicar ACK |
| Heurística de 60 ms (como a linha atual) | Não é confirmação | ✗ (viola R3) |
| Overlay em reserved[] para os templates de alarme | reserved[64] está cheio; templates de 256/512 B não caberiam de qualquer forma | ✗ |
| Fila com std::vector/heap | Fragmentação; disciplina do projeto é estática | ✗ |

---

## 5. Plano de implementação (fases, nesta ordem)

1. **Schema**: AlarmTelConfig + CONFIG_VERSION 21 + migração + defaults + static_asserts
   (SystemDefs_Records.h, StorageManager.cpp). Pode ser a primeira PR isolada.
2. **AlarmQueue** + testes nativos (padrão test/ com native_stubs): push, estouro,
   drain/ack por seq, sem vazamento.
3. **Produtor**: bordas em checkAlarmConditions + transição de erro + gatilho imediato.
4. **Builder**: TelFormatConfig + formatLineAlarmBuf + tokens novos; garantir
   byte-idêntico na linha convencional (diff de "tel dump" antes/depois).
5. **Consumer**: attemptAlarmHttpUpload (2xx → ack) e attemptAlarmMqttPublish +
   assinatura {base}/alarm/ack + callback; cadência/gates §3.4.
6. **Interface**: API GET/POST, seção no WebUI.h com preview, CLI "alarm *", métricas,
   profundidade no painel.
7. **Validação**: servidor fake HTTP/MQTT nas tools/ (estilo ha_discovery_test.py),
   teste de estouro, teste de perda de conexão (reenvio), heap_gate_interaction.py
   adaptado, campanha de estabilidade.

## 6. Pontos abertos para sua decisão

1. **SEQ no template default** — incluir ou deixar invisível? (recomendo incluir; o servidor agradece)
2. **Registro de "normalização"** (volta ao normal) — agora ou depois? (custa 1 flag)
3. **Debounce de 2 ciclos (~10 s)** nas bordas — aceita o atraso?
4. **alarmPath separado no HTTP** vs. reuso exato do telPath — confirmar.
5. Capacidade default da fila: **32** ok?
6. Em MQTT, o ACK por aplicação exige o servidor publicar no tópico de ack — seu coletor
   consegue? (se não, avaliar Alternativa B do §4).
