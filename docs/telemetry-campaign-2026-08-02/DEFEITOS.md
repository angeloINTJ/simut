# Defeitos encontrados — campanha de telemetria 2026-08-02

> 📌 **Snapshot da campanha de 02/08/2026 (v2.0.1-alpha).** Os estados abaixo NÃO foram mantidos desde então — vários itens foram corrigidos em releases posteriores (ex.: D1, senha MQTT, corrigido em `WebManager_Commit`). Confira o CHANGELOG antes de tratar qualquer item como aberto.

Cada item traz o mecanismo no código, a evidência que o provou e a correção
proposta. Os que ainda dependem de medição em curso estão marcados
**(a confirmar)** e serão fechados com o dado, não com a leitura do código.

---

## D1 — A senha MQTT digitada na web é descartada em silêncio

**Severidade: alta.** Autenticação MQTT é impossível de configurar por qualquer
caminho.

**Mecanismo.** A página tem o campo (`WebUI.h:2577`):

```html
<input type="password" id="m_pass" name="m_pass" maxlength="31"
       placeholder="Leave empty to keep">
```

e `wirePendingListeners()` (`WebUI.h:3055`) empurra **todo** input com `id`
dentro de `#sysForm` para `Pending.sys`, por `id`. Ou seja, o navegador **envia**
`sys.m_pass` no `_payload` do `/api/commit_all`.

Do outro lado, `WebManager_Commit.cpp:570-575` trata `m_topic`, `m_cid`,
`m_user`, `m_qos`, `m_retain` e `m_ka` — **não existe nenhum `has("m_pass")`**.
O valor cai no chão sem log e sem erro. `cfg.mqttPass` só é escrito em
`StorageManager.cpp:495`, que o zera nos defaults.

Consequência: `mqttEnsureConnected()` sempre chama `connect()` com senha vazia
(`TelemetryManager.cpp:710`), e qualquer broker que exija senha recusa o
CONNECT. O usuário vê "MQTT failed: Bad credentials" com a senha certa digitada
na tela.

**Correção** (`WebManager_Commit.cpp`, junto de `m_user`):

```cpp
/* Vazio = manter a atual, como o placeholder do campo promete
 * ("Leave empty to keep") e como t_key já faz com a máscara "***". */
if (has("m_pass")) {
    String mp = getStr("m_pass");
    if (mp.length( ) > 0) safeCopy(cfg.mqttPass, mp.c_str( ), sizeof(cfg.mqttPass));
}
```

---

## D2 — O cabeçalho CSV descreve 7 colunas; as linhas têm 34

**Severidade: alta.** Todo consumidor que confia no cabeçalho lê os valores nas
colunas erradas.

**Mecanismo.** `buildPayload()` monta o cabeçalho com **uma coluna por sensor
ativo** (`TelemetryManager.cpp:1017-1025`):

```cpp
s = "timestamp";
for (int i = 0; i < MAX_SENSORS; i++)
    if (cfg.sensors[i].active) { snprintf(hdrBuf, ..., ";s%d_%s", i, cfg.sensors[i].hwId); ... }
```

mas `toCsvLine()` (`SystemDefs_Records.h:611`) emite **sempre** o layout fixo
`epoch;s0..s15;h0..h15;press` = 34 campos, ativos ou não.

**Evidência medida** (corpo cru capturado pelo sink, `results/phase_payload.json`):

```
cabeçalho (7): timestamp;s0_STM0009;s1_STM0010;s3_STH0003;s4_STB0001;s6_STZ9999;s10_STH0001
linha     (34): 1783140900;23.92;24.81;;25.14;;;;;;;23.99;;;;;;;;;68.3;;;;;;;68.5;;;;;;
```

Todas as 24 linhas do lote têm exatamente 34 campos. O comentário acima do
código afirma "Header matches toCsvLine" — não casa.

**Correção**: emitir o cabeçalho com o mesmo layout fixo das linhas, nomeando
os slots ativos e deixando os inativos com nome posicional:

```cpp
s = "timestamp";
for (int i = 0; i < MAX_SENSORS; i++) {
    if (cfg.sensors[i].active && cfg.sensors[i].hwId[0])
        snprintf(hdrBuf, sizeof(hdrBuf), ";s%d_%s", i, cfg.sensors[i].hwId);
    else snprintf(hdrBuf, sizeof(hdrBuf), ";s%d", i);
    s.concat(hdrBuf);
}
for (int i = 0; i < MAX_SENSORS; i++) { ... ";h%d..." ... }
s.concat(";press");
```

O layout das linhas **não** muda — é o formato persistido e compatível com o
upload; quem estava errado era o cabeçalho.

---

## D3 — A telemetria nunca alcança histórico além de 30 dias

**Severidade: média** (é política, não bug — mas não está documentada e não há
como contorná-la pela interface).

**Mecanismo.** `collectBatch()` (`TelemetryManager.cpp:409-412`):

```cpp
if (lastCursor == 0) {
    uint32_t lastRecorded = _storageRef->getLastRecordedTimestamp( );
    if (lastRecorded > 86400UL * 30) lastCursor = lastRecorded - 86400UL * 30;
}
```

`tel reset` zera o cursor, e é o único jeito de reenviar do começo. O piso de 30
dias entra logo em seguida, e o corte por `minDay` descarta os arquivos
anteriores. Não existe comando nem campo que peça mais.

**Evidência medida.** Drenagem completa com `tel reset`, lote 50, intervalo 1 s:
39 900 registros em 1009 s, começando em 2026-07-03 — enquanto o flash guarda
desde 2026-05-03. O restante do arquivo é inalcançável por telemetria em
qualquer tempo de execução.

**Prova de que é política e não limite de armazenamento**: semeando
`/config/t_cursor.bin` com 1 600 000 001 (logo acima de `HIST_EPOCH_MIN`) em vez
de zero, o `if (lastCursor == 0)` não dispara e o dispositivo transmite o arquivo
inteiro — ver a fase `drain_full`.

**Correção proposta**: tornar a janela configurável (`t_backfill_days`, 0 = tudo)
ou, no mínimo, registrar em log qual piso foi aplicado e por quê, para o operador
não concluir que os dados sumiram.

---

## D4 — `/api/ls` trunca a listagem em silêncio

**Severidade: alta.** Um cliente não consegue distinguir listagem parcial de
completa, e o JSON sai bem-formado nos dois casos.

**Mecanismo.** `handleApiLs()` (`WebManager_Files.cpp:186-187`):

```cpp
while (!dirDone) {
    if (isHandlerOvertime( )) break;   // <- sai e fecha o JSON como se tivesse acabado
```

`isHandlerOvertime()` compara com `_handlerDeadline`, fixado em
`handlerStart + 6000` (`WebManager_Core.cpp:269`). Ao estourar, a função sai do
laço, fecha `]}` e responde 200. Nada no corpo indica que faltou entrada.
Compare com `handleApiHistoryMulti`, que **eleva** o prazo
(`WEB_LONG_HANDLER_DEADLINE_MS`) justamente por ser um handler longo — o listador
não faz isso.

**Evidência medida.** Duas listagens de `/history` no mesmo dia devolveram
**84 entradas cada, com conjuntos diferentes**:

| arquivo | listagem A | listagem B | existe de fato? |
|---|---|---|---|
| `20260523.h5` | sim | não | **sim** — HTTP 200, 8166 B, md5 `3e26f8a71a` |
| `20260524.h5` | não | sim | **sim** — HTTP 200, 8157 B, md5 `4017ee9696` |
| `20260706.h5` | sim | não | sim |
| `20260707.h5` | não | sim | sim (baixado, decodifica 1440 registros de 07-07) |

Os dois arquivos de cada par existem e têm conteúdo distinto e correto; nenhuma
listagem mostrou os dois. O gerenciador de arquivos da web sofre do mesmo
problema, e qualquer inventário construído sobre `/api/ls` é subestimado por uma
margem desconhecida.

**Correção**: (a) sinalizar a truncagem no corpo — `"truncated":true` — para que
nenhum cliente possa confundir; e (b) elevar o prazo desse handler como o
`history_multi` faz. (a) é o essencial: sem ela, aumentar o prazo só empurra o
limite para diretórios maiores.

---

## D5 — O cursor pode avançar por cima de um lote que `buildPayload` encurtou

**Severidade: baixa hoje, alta se as constantes mudarem.** Latente: as margens
atuais impedem que dispare.

**Mecanismo.** `collectBatch()` devolve `newCursor` = maior epoch do lote
**completo**. Em seguida `buildPayload()` pode **encurtar o lote** sob pressão de
heap (`TelemetryManager.cpp:987-993`):

```cpp
if (freeHeap < estimatedSize + SEC_RESERVE) {
    size_t safeCount = ...;
    if (safeCount < batch.size( )) { batch.resize(safeCount); batch.shrink_to_fit( ); }
}
```

mas `update()` segue usando o `newCursor` antigo:

```cpp
String payload = buildPayload(batch);   // pode ter jogado registros fora
batch.clear( );
success = attemptHttpUpload(payload, newCursor);   // avança até o lote INTEIRO
```

Se o encurtamento acontecer, o cursor pula os registros descartados e eles nunca
são enviados — sem log. Vale para os dois ramos de `update()` e para
`forceSync()`.

**Por que não dispara hoje**: `safeBatchLimit()` já limitou o lote com uma
reserva **maior** (32768 com TLS, 12288 sem) do que a de `buildPayload`
(12288 / 6144), e com 350 B/registro contra 300. A desigualdade se mantém para
qualquer heap. Ou seja, a segurança vem de um acoplamento implícito entre quatro
constantes em duas funções — inverta qualquer uma e a perda passa a ser real.

**Correção** (barata e torna o acoplamento desnecessário): fazer o cursor seguir
o que o payload realmente contém.

```cpp
String payload = buildPayload(batch);
/* buildPayload pode encurtar o lote sob pressão de heap; o cursor tem de
 * seguir o que o payload contém, não o que foi coletado. O lote está em
 * ordem crescente de epoch (arquivos ordenados, registros em ordem). */
if (!batch.empty( )) newCursor = batch.back( ).epoch;
```

---

## D6 — `pending` é `uint16_t` e envolve sem saturar

**Severidade: baixa** (cosmético/diagnóstico), mas engana quem depende do número.

**Mecanismo.** `refreshPendingCount()` acumula em `uint16_t total` com cast
explícito (`TelemetryManager.cpp:1456,1496`):

```cpp
uint16_t total = 0;
...
total = (uint16_t)(total + hdr.pre.a);
```

Com o cursor zerado o laço conta **todos** os registros de **todos** os
arquivos. O flash desta bancada guarda ~119 mil registros — bem acima de 65535 —
e o valor envolve para um número plausível porém falso, que aparece no dashboard
e em `/api/status`.

Além disso `refreshPendingCount()` **não** aplica o piso de 30 dias que
`collectBatch()` aplica: logo depois de um `tel reset`, o contador soma registros
que o dispositivo jamais vai enviar.

**Correção**: acumular em `uint32_t` e saturar na atribuição —
`_pendingEstimate = (total > 0xFFFF) ? 0xFFFF : (uint16_t)total;` — e aplicar o
mesmo piso de 30 dias do `collectBatch` para os dois contadores concordarem.

---

## D7 — QoS 1 e 2 são oferecidos na interface e nunca saem no fio **(a confirmar)**

A página oferece `0 - At Most Once`, `1 - At Least Once`, `2 - Exactly Once`
(`WebUI.h:2582-2586`) e o firmware valida e persiste `cfg.mqttQos` de 0 a 2
(`WebManager_Commit.cpp:573`). Mas `attemptMqttPublish()` chama sempre a
sobrecarga de três argumentos do PubSubClient:

```cpp
_mqttClient.publish(topic.c_str( ), linePayload.c_str( ), cfg.mqttRetain);
```

que publica **QoS 0** — o PubSubClient não implementa publish com QoS 1 ou 2.
`cfg.mqttQos` não é lido em lugar nenhum do caminho de envio.

Consequência prática: sem QoS 1 não há PUBACK, então `publish()` devolve
sucesso assim que escreve no socket TCP. Um broker que morra no meio do publish
é indistinguível de um que recebeu — e o cursor avança. O teste
`mq_drop_on_publish` mede exatamente isso.

*A confirmar pelas flags do PUBLISH registradas pelo broker instrumentado.*

---

## D8 — Payload MQTT acima de ~8 KB trava a telemetria para sempre **(a confirmar)**

`attemptMqttPublish()` cresce o buffer para caber o payload, mas com teto rígido:

```cpp
if (payload.length( ) > _mqttClient.getBufferSize( )) {
    uint16_t needed = min((size_t)8192, payload.length( ) + 64);
    _mqttClient.setBufferSize(needed);
}
```

`PubSubClient::publish()` recusa qualquer pacote que não caiba no buffer. Acima
de ~8 KB o publish falha **de forma determinística**: o retry falha igual, o
backoff escala até o teto de 300 s e a telemetria fica parada para sempre — sem
reboot e sem mensagem que explique.

É alcançável pela tela de configuração: lote 50 com um `t_line` longo
(o campo aceita 512 bytes) passa de 8 KB com os sensores desta bancada.

*A confirmar pela fase `mqtt_oversize`.*

---

## D9 — Um único campo de pressão por registro, rotulado pelo slot errado

**Severidade: média** para quem tiver mais de um sensor de pressão.

`BinaryHistoryRecord` tem `pressure` como **escalar**, não um por slot. Em
`collectBatch()`:

```cpp
else if (chOf[c] == CH_PRESS) rec.pressure = BinaryHistoryRecord::floatToI16x10(v);
```

o laço percorre os canais do schema e **o último vence**. E em
`formatLineJsonBuf()` o rótulo vem do **primeiro** slot ativo capaz de pressão:

```cpp
for (int i = 0; i < MAX_SENSORS; i++)
    if (cfg.sensors[i].active && ... sensorHasChannel(..., CH_PRESS)) { pHwid = cfg.sensors[i].hwId; break; }
```

Com dois sensores de pressão provisionados, o valor publicado é o do **último**
slot sob o nome do **primeiro** — e o do primeiro nunca é publicado. Canais NaN
são pulados, então um slot fantasma sem leitura não estraga nada; dois sensores
reais, sim.

**Correção**: ou passar `pressure` a um vetor por slot no registro de telemetria,
ou (mínimo) rotular com o slot de onde o valor veio, guardando o índice junto
com o valor em `collectBatch`.

---

## D14 — Vazamento de PBUF sob respostas HTTP grandes (descoberto na revalidação)

**Severidade: alta.** O servidor web morre e não volta sem reboot.

Corrigido o D10, os reboots por watchdog somem — e o dispositivo passa a
sobreviver tempo suficiente para expor um problema que os reboots vinham
apagando a cada 8,4 s.

**Evidência.** Contra o servidor que responde HTTP 200 com 1 MB de corpo, após
~19–67 conexões seguidas:

```
show net status → PBUF pool: 12 em uso / pico 12 / 12 total, 1158 falhas
```

O dispositivo continua vivo: IP 192.168.3.24, RSSI −42 dBm, CLI serial
respondendo comandos normalmente. O que morre é só a pilha de rede — sem buffer
não há pacote de resposta, e o web fica mudo.

**Não é ocupação transitória, é vazamento.** Com a telemetria desligada
(`tel interval 0`) o pool fica em 12/12 e o contador de falhas continua subindo
(519 → 666). Um `reload confirm` devolve tudo:
`PBUF pool: 0 em uso / pico 1 / 12 total, 0 falhas`, web HTTP 200.

**Agravante de projeto**: `PBUF_POOL_SIZE` foi reduzido de 24 para 12 no patch de
lwIP (`tools/arduino_pico_overrides`), economizando 18 KB de RAM. Com 12 entradas
a margem é estreita.

**Atribuição corrigida.** Primeiro atribuí o vazamento ao `stop( )` que eu havia
acrescentado antes do `http.end( )` em `attemptHttpUpload`. Removi o `stop( )`,
regravei e refiz: **mesmo resultado**. A hipótese estava errada; o `stop( )` saiu
de vez (sem benefício medido e com risco). O vazamento antecede as mudanças desta
campanha.

**Não corrigido**, deliberadamente: localizar onde os pbufs da resposta abortada
deixam de ser liberados exige instrumentar o lwIP, e não quis propor correção que
não pudesse validar na bancada. Duas direções para quem for atacar:

1. o caminho de `tcp_abort`/close com fila de recepção pendente, que é onde a
   resposta grande é largada pela metade;
2. um teto no tamanho de corpo que a telemetria aceita — recusar antes de
   começar a ler é mais barato do que abortar no meio.


---

## D15 — O aparelho sai da rede WiFi e o firmware não percebe

**Severidade: alta.** Fica inacessível até alguém reiniciar, anunciando-se saudável.

Descoberto ao investigar por que o servidor web morria sob rajada de respostas
grandes mesmo depois de o pool de PBUF parar de saturar (D14).

**Não era rede nem buffer.** A prova foi separar as camadas, do host:

```
ping 192.168.3.24 → 4 pacotes, 0 recebidos, 100% de perda
ip neigh          → 192.168.3.24 dev enp6s0 INCOMPLETE
```

ARP incompleto está **abaixo** de TCP e de IP: o aparelho não estava na rede.
Enquanto isso, pela serial:

```
IP: 192.168.3.24
RSSI: 4 dBm
PBUF pool: 7 em uso / pico 20 / 24 total, 0 falhas
```

Zero falhas de buffer, IP anunciado, e um **RSSI de +4 dBm** — força de sinal
recebido é negativa por definição, então o ioctl do cyw43 havia parado de
devolver dado real.

**Mecanismo** (`NetworkManager.cpp`, `case NET_READY`):

```cpp
if (WiFi.status( ) != WL_CONNECTED) { ...rebaixa e reconecta... }
```

O único teste de vida é a palavra do próprio driver, e ele seguia respondendo
`WL_CONNECTED` com o enlace morto. `isConnected( )` é `_state == NET_READY`, o
estado nunca era rebaixado, e o caminho de reconexão — que existe e funciona —
nunca era acionado.

Agravante: `isNetworkHealthy( )` é `isConnected( ) && getRssi( ) > RSSI_MIN_THRESHOLD`
com o limiar em −78. O valor corrompido de **+4 passa folgado**: a leitura
quebrada não escapava da detecção, ela **confirmava saúde**.

**Correção.** O RSSI já era amostrado ali mesmo, 1×/min. Ele é o sinal
independente que faltava:

- `NET_READY` rebaixa para `NET_DISCONNECT_PENDING` após **duas** leituras fora
  da faixa plausível (`RSSI_IMPLAUSIBLE_HIGH = 0`, `RSSI_IMPLAUSIBLE_LOW = -120`).
  Duas, não uma: um valor esquisito pode ser glitch, e reconectar à toa custa
  mais do que esperar um minuto. Detecção em ~2 min em vez de nunca.
- `getRssi( )` devolve −100 para leitura implausível, então `isNetworkHealthy( )`
  também reprova e a telemetria para de tentar.
- Cadência mantida em 1×/min de propósito: RSSI é ioctl vivo, e martelá-lo é
  risco próprio — a assinatura `C0=[WIFI]` por leitura-viva-que-bloqueia já está
  registrada no histórico do projeto.

**O que NÃO está resolvido, e a ressalva do teste.** A causa de o rádio morrer
segue desconhecida; a correção troca "fora da rede até reiniciar" por "detectado
e reconectado", que é o dano operacional real, mas não impede a queda.

E na corrida de validação **o rádio não morreu** — RSSI ficou em −46 dBm o tempo
todo, e o aparelho atravessou 150 s de rajada e voltou a responder em ~40 s. Ou
seja: está provado que o aparelho aguenta e se recupera, **não** que o novo
caminho de detecção dispara corretamente quando o cyw43 corrompe. Reproduzir a
morte do rádio não é determinístico, e não vou registrar como verificado o que
não vi disparar.
