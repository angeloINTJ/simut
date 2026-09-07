# Telemetria — cadência e lote automáticos: medições e plano

> **Pedido (Ângelo, 07/09/2026):** o intervalo de telemetria regia a cadência dos lotes; a cadência
> deve virar automática, e o tamanho do lote também. Resposta rápida do servidor → próximo lote
> imediato; resposta lenta → intervalo cresce; onde couber, o aparelho hiberna e continua depois.
> Considerar todas as variáveis, maximizar eficiência, otimizar energia. Encher o LittleFS com dados
> sintéticos, medir com servidor no PC, medir com criptografia.
> **Base:** `feature/simut-air`, build Air. **Bancada:** `tools/telemetry_bench/phase_cadence.py`,
> servidor `server_http.py` (modos `ok`, `slow`, `--tls`, `--keepalive`), relatório
> `cadence_report.py`, limpeza `cadence_cleanup.py`.
> **Irmãos:** [`SIMUT_AIR_PLANO_ENERGIA.md`](SIMUT_AIR_PLANO_ENERGIA.md) (duas cadências, rádio raro),
> [`SIMUT_AIR_PLANO_FIX.md`](SIMUT_AIR_PLANO_FIX.md).

---

## 0. Resposta curta

1. **O intervalo fixo estrangulava a telemetria por 2 a 3 ordens de grandeza.** Com o piso em 1 ms
   (o mínimo que a config aceita), HTTP puro entrega **500 registros/s** com lote 50, **780/s** com
   lote 100 e **830/s** com lote 250 (§2.1). Com `t_int` = 10 s o mesmo aparelho entregava 5
   registros/s; com os 300 s da configuração atual, **0,33/s** — 37 mil registros pendentes
   levariam **31 horas** para sair, uma janela de 30 s por wake de cada vez.
2. **Uma requisição HTTP custa `54 ms + 0,94 ms × registros`** (ajuste sobre cinco lotes, §2.1).
   O servidor responde em 2 ms; os ~54 ms fixos são o aparelho e o seu loop (varrer o diretório,
   abrir e decodificar, montar o JSON, conectar, voltar ao `loop( )`). Logo a vazão cresce com o
   lote — mas com retorno decrescente: de 100 para 250 registros ganha só 6 %. **O lote útil no
   HTTP é o teto de heap (~230 registros JSON hoje)**, e a alavanca seguinte é o custo fixo, não o
   lote.
3. **No HTTPS cada lote paga um handshake TLS inteiro: `1,4 s + 2,7 ms × registros`** — 10 a 20×
   o HTTP, 7 a 87 registros/s. `attemptHttpUpload( )` fecha a sessão após toda requisição (defesa
   medida contra a falha `drip`). Manter a sessão entre lotes consecutivos com sucesso é a maior
   alavanca de criptografia; está implementada atrás de `TEL_TLS_KEEPALIVE_EXPERIMENT` (instância
   persistente do `HTTPClient` — o reuso é estado dela) e medida em A/B (§2.5).
4. **Para energia, o que importa é o wake, não o envio.** A 800 registros/s, um registro custa
   ~1,2 ms de rádio (HTTP) ou ~12 ms (HTTPS, lote 250); a amostragem custa ~27 s por wake. Portanto:
   enviar **raramente** (a cada N wakes de leitura, como já faz o `airTelemetryDue( )`) e, quando
   enviar, **drenar tudo na velocidade máxima** — nunca ficar acordado esperando um intervalo.
5. **A regra "hiberna e continua depois" já existe pela metade** (o teto `flushTimeoutMs` corta o
   dreno e o contador de wakes retoma no próximo wake de telemetria). Falta o teto ser derivado do
   intervalo de leitura, para o wake de telemetria nunca estourar a cadência de medição.
6. **Retratação no caminho:** a primeira matriz (01:00) mostrava "22–35 % de reenvio" e números
   1,5× maiores. Era a bancada — servidor contando desde antes do reboot de configuração — e não o
   firmware (§2.6). Os números deste documento são da segunda matriz, com a janela cortada por
   request. Um bug real apareceu na caçada e está corrigido: o cursor nunca ia ao flash durante um
   dreno rápido.
7. 🔴 **O wake de telemetria do Air não mandava nada com a configuração de campo** (`t_int` =
   300 s): o primeiro envio de um boot espera `t_int` inteiro, e o wake dura 57 s. Medido: 0
   registros em 57 s acordado com o rádio ligado (§2.4). **Corrigido e validado** nesta rodada
   ("modo dreno" no FLUSH, F05): 18.800 registros no mesmo wake (§2.8). É o item que tem que sair
   junto com este plano, porque sem ele as duas cadências (leitura todo minuto, telemetria a cada
   N wakes) não entregam telemetria nenhuma.
8. **Dois reboots silenciosos** (sem autópsia, sem marcador de hibernação) numa noite, os dois
   colados numa falha de telemetria — na célula de 6 s e no 1º wake de validação. Não é o
   watchdog (deixaria `[FTL]`) e nenhum caminho de reboot limpo do firmware se aplica; a hipótese
   é perda de energia no USB, mas a coincidência pede uma serial acampada (§2.7, §6).

---

## 1. Todas as variáveis da telemetria (o que existe hoje, e o que cada uma faz)

| variável | onde | papel hoje | destino no plano |
|---|---|---|---|
| `telInterval` (ms, 0 = off) | `SystemConfig` / `t_int` | **piso** entre lotes: `effectiveInt = max(telInterval, 1,5 × EMA_latência) × penalidade_RSSI`, teto 60 s | deixa de ser piso entre lotes; vira só o **período de telemetria** do Air (`airTelemetryDue( )`, em wakes inteiros) e, no M0, o período entre **drenos** |
| `telBatchSize` (1..250) | `t_bat` | máximo configurado por lote | vira **teto**; o tamanho real é automático (§3.2) |
| `safeBatchLimit( )` | `TelemetryManager.cpp:502` | clamp por heap: `(livre − reserva) / bytes_por_registro`, reserva 32 K (TLS) / 12 K (plain), 350 B JSON / 160 B CSV, cap 250 | mantido — é o teto físico do controlador de lote |
| `PREFLIGHT_FLOOR` | idem | 24.576 (TLS) / 14.336 (plain) B de heap livre para tentar | mantido |
| `telMode` JSON/CSV | `t_mode` | forma do payload; **35,8 B/registro** medidos no fio (JSON, 1 canal) | mantido; CSV a medir |
| `telTransport`, `telEncryption` | `t_transport`, `t_sec` | HTTP/MQTT, TLS on/off | mantidos; TLS ganha keep-alive de sessão |
| `_smoothedLatencyMs` | EMA α = 0,3 | mede **só** o `POST( )` — exclui conexão e handshake | passa a medir o **ciclo inteiro** (do `collectBatch` ao fim do `end( )`), que é o que a cadência precisa |
| backoff | 5 s ×2 até 300 s, jitter ±25 %, streak 10 | punição por falha | mantido para **falhas**; não se aplica a "lento" |
| penalidade RSSI | < −85 ×2, < −75 ×1,5 | alarga o intervalo | mantida como multiplicador do **gap entre lotes** |
| `NET_SOCKET_TIMEOUT_MS` | 4.000 ms | timeout de leitura do POST | é o penhasco: servidor > 4 s = falha (§4.2) |
| `NET_TLS_HANDSHAKE_MS` | 15.000 ms | teto do handshake | mantido |
| `setBufferSizes(4096, 512)` | TLS | iobuf RX 4 K, **TX 512 B** — um registro TLS a cada 512 B de payload | candidato a alavanca no HTTPS (§4.3) |
| `_httpSecurePtr->stop( )` após cada lote | `:906` | defesa contra `drip` | condicional ao **insucesso** (experimento) |
| `collectBatch( )` | `:545` | varre `/history`, abre e decodifica arquivos a cada lote | custo fixo por lote; cache da lista dentro de um dreno é alavanca secundária |
| cursor `t_cursor.bin` | coalescido 5 s; forçado antes do sono | avanço do enviado | mantido |
| piso de 30 dias | `collectBatch` | nada mais velho que `último − 30 d` sai | mantido |
| Air `flushTimeoutMs` | `air.bin`, 30 s | teto de parede do FLUSH | passa a ser **derivado** do intervalo de leitura (§3.4) |
| Air `airTelemetryDue( )` | `AppManager_Air.cpp` | rádio só a cada N wakes | é o "período" da telemetria no Air |
| MQTT ≤ 5 / > 5 | `attemptMqttPublish` | publish por registro até 5; um payload acima | mesma regra de cadência; lote automático vale igual |

---

## 2. O que a bancada mediu

Bancada: LittleFS com **28 dias sintéticos** (`gen_synth_history.py`, 1 registro/min, ~35 mil
registros dentro do piso de 30 dias, 58 KB em `/history`); servidor instrumentado no PC
(`192.168.3.31`, HTTP :18080, HTTPS :18443, certificado autoassinado — o aparelho está sem
`/cert.pem`, logo `setInsecure( )`); `t_int = 1` para o piso não mandar; `tel_reset` antes de
cada célula; janelas de 45 s **cortadas do log por request do servidor** (§2.6 explica por quê);
três instrumentos (servidor, `/api/status` a cada 1 s, USB). Firmware da branch com o fix do
cursor, sem sondas. Tudo abaixo é a segunda matriz (01:22–01:40).

### 2.1 Capacidade — HTTP puro, servidor respondendo na hora

| lote | ativo s | req | registros | **reg/s** | ms/req | reg/req | POST (aparelho) | servidor p50 | heap mín / maior bloco | falhas | reboots |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 46,0 | 629 | 6.290 | **137** | 73 | 10,0 | 13 ms | 2 ms | 95,7 / 55,9 KB | 0 | 0 |
| 25 | 45,0 | 551 | 13.775 | **306** | 82 | 25,0 | 16 ms | 5 ms | 95,6 / 55,6 KB | 0 | 0 |
| 50 | 45,1 | 453 | 22.650 | **502** | 100 | 50,0 | 24 ms | 10 ms | 95,6 / 55,9 KB | 0 | 0 |
| 100 | 41,4 ¹ | 325 | 32.400 | **782** | 127 | 99,7 | 30 ms | 19 ms | 95,6 / 55,9 KB | 0 | 0 |
| 250 | 37,1 ¹ | 132 | 30.674 | **827** | 281 | 232,4 (clamp de heap) | 52 ms | 45 ms | 95,6 / 55,9 KB | 0 | 0 |

¹ o backlog acabou antes dos 45 s; a taxa é sobre o tempo em que houve o que mandar.

Ajuste sobre as cinco células: **`t_req ≈ 54 ms + 0,94 ms × registros`**. Três leituras:

- **O custo fixo por requisição (~54 ms) é do aparelho, não do servidor nem do fio.** O servidor
  responde em 2 ms no lote 10; o POST visto pelo aparelho leva 13 ms. Os ~40 ms restantes são o
  ciclo em volta: varrer `/history` (30 arquivos), abrir e posicionar o arquivo, decodificar,
  montar o JSON, `begin( )`/`end( )`, e uma volta do `loop( )` principal (web, sensores) entre
  dois lotes — `update( )` manda **um** lote por chamada.
- **O custo marginal cresce com o payload** (0,6 ms/registro até 100, 1,2 ms/registro de 100 a
  232). O servidor demora 45 ms para *receber* os 8,3 KB do lote 250 — é o tempo de o RP2040 pôr
  esses bytes no ar e o Python parseá-los, não de processar. O retorno de crescer o lote diminui:
  100 → 250 registros rende +6 % de vazão.
- **Consequência para o desenho:** no HTTP o lote deve ir ao teto de heap (não há penalidade),
  e a partir daí o ganho está em encadear lotes sem devolver o loop (§3.6) e em não varrer o
  diretório a cada lote — os ~54 ms fixos são 40 % do tempo no lote 100.

Nenhum reboot, nenhum poll web perdido, heap estável — o aparelho continua operável durante o
dreno.

### 2.2 Capacidade — HTTPS (certificado autoassinado, `setInsecure( )`)

| lote | ativo s | req | registros | **reg/s** | ms/req | reg/req | POST (aparelho) | servidor p50 | heap mín / maior bloco | falhas | reboots |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 10 | 45,1 | 30 | 300 | **6,7** | 1.503 | 10,0 | 533 ms | 76 ms | 88,9 / 46,7 KB | 0 | 0 |
| 25 | 46,1 | 33 | 825 | **17,9** | 1.397 | 25,0 | 526 ms | 92 ms | 89,0 / 45,4 KB | 0 | 0 |
| 50 | 46,1 | 32 | 1.600 | **34,7** | 1.441 | 50,0 | 555 ms | 120 ms | 88,9 / 46,7 KB | 0 | 0 |
| 100 | 45,5 | 27 | 2.700 | **59,3** | 1.685 | 100,0 | 619 ms | 183 ms | 89,0 / 79,4 KB | 0 | 0 |
| 250 | 45,4 | 25 | 3.950 | **86,9** | 1.816 | 158,0 (clamp de heap TLS) | 682 ms | 247 ms | 88,9 / 41,4 KB | 0 | 0 |

Ajuste: **`t_req ≈ 1,38 s + 2,7 ms × registros`**.

- **Cada requisição HTTPS custa 1,4 a 1,8 s** — 10 a 20× o HTTP. O POST visto pelo aparelho já
  leva 530–680 ms, porque a conexão (e o handshake TLS) acontece dentro dele; o resto do segundo é
  o `stop( )` da sessão, o `begin( )` seguinte e a volta do loop. É o custo do handshake RSA-2048
  no RP2040, pago **em todo lote** porque a sessão é fechada após cada envio (§1). O servidor
  também sente: 76 ms no lote 10 (o handshake do lado dele) contra 2 ms no HTTP.
- O custo marginal é 3× o do HTTP (2,7 contra 0,94 ms/registro): cada 512 B de payload é um
  registro TLS cifrado em software (`setBufferSizes(4096, 512)`).
- Consequência direta: no HTTPS o lote grande é ainda mais importante (10 → 250 registros = 13×
  a vazão), e o teto é o clamp de heap TLS: 158 registros com ~41–47 KB de maior bloco.
- Heap: 88,9 KB livres, maior bloco 41–47 KB — o contexto TLS residente custa ~9 KB de bloco.

*(A/B do keep-alive de sessão em §2.5.)*

### 2.3 Latência do servidor injetada (HTTP, lote 50)

Servidor em modo `slow`: lê a requisição, dorme o atraso, responde 200. Firmware **atual** (regra
`max(t_int, 1,5 × EMA do POST)`), `t_int = 1`, janelas de 45 s.

| atraso do servidor | req | reg/s | s entre envios | POST (aparelho) | falhas | reboots |
|---|---|---|---|---|---|---|
| 0 (§2.1) | 453 | 502 | 0,10 | 24 ms | 0 | 0 |
| 0,5 s | 34 | 37,2 | 1,34 | 520 ms | 0 | 0 |
| 1 s | 18 | 19,9 | 2,52 | 1.020 ms | 0 | 0 |
| 2 s | 9 | 10,9 | 4,60 | 2.023 ms | 0 | 0 |
| 3,5 s | 5 | 6,7 | 7,50 | 3.528 ms | 0 | 0 |
| **6 s** | 3 ¹ | 3,1 | 16,4 | — (timeout) | ≥ 3 (`ctx=−11`) | **1** |

¹ requisições que o servidor recebeu; o aparelho desistiu de todas aos 4 s (`NET_SOCKET_TIMEOUT_MS`).

- **A cadência atual espera 2,5× o atraso do servidor**: `s entre envios ≈ 2,5 × d + 0,06`. O
  ciclo custa `d + 0,06 s` e a regra ainda soma um piso de `1,5 × d` depois dele. É a "punição"
  que o pedido quer trocar por crescimento controlado: no plano (§3.1) o primeiro lote lento paga
  `gap = ciclo`, o seguinte `2 × gap`, até `GAP_MAX` — e um lote rápido zera.
- **Acima de 4 s é penhasco, não degradação**: o POST estoura o timeout, cada tentativa custa 4 s
  e entra o backoff (5, 10, 20 s…). Nenhum registro passa, embora o servidor esteja vivo e aceite
  tudo. Um coletor real com 5 s de ingestão fica **invisível** para o aparelho.
- 🔴 **Houve um reboot na célula de 6 s**, 7 s depois da terceira falha (`up=288 s` → boot novo
  às 01:50:47), **sem autópsia** (nenhum `[FTL]`, boot registrado como limpo). Não reproduz o
  padrão do watchdog — parece um reset limpo. Ver §2.7.

### 2.4 Um wake M1 por configuração (sonda GP16)

Leitura a 1 min (`h_int = 1`), `tel_reset` antes de hibernar (backlog inteiro, ~35 mil
registros), um wake cronometrado pela sonda passiva da PicoHand no GP16, firmware **atual**.

O harness manda `air hibernate` de M0: o aparelho roda **um ciclo no lugar** (FLUSH até o teto,
dorme) e depois **o wake** cronometrado pela sonda. Os dois aparecem no log do servidor como dois
trechos separados pelo sono; a tabela é só o **wake** (o trecho de dentro da janela da sonda).

| transporte | lote | `t_int` | acordado (sonda) | FLUSH do wake | req | registros | ms/req | s acordado / 1.000 reg |
|---|---|---|---|---|---|---|---|---|
| HTTP | 100 | 60.000 ms | **56,9 s** | 30 s, ocioso | 0 | **0** | — | ∞ |
| HTTP | 100 | 1.000 ms | 57,0 s | 29,1 s | 28 | 2.800 | 1.077 | 20,4 |
| HTTP | 100 | 1 ms | 54,6 s | 27,6 s | 196 | **19.600** | 142 | 2,79 |
| HTTPS | 100 | 1 ms | 57,8 s | 29,9 s | 20 | 2.000 | 1.571 | 28,9 |

- 🔴 **Com `t_int` ≥ o teto do FLUSH o wake de telemetria não manda nada** — 57 s acordado com o
  rádio ligado, zero registros. Causa: `TelemetryManager::begin( )` carimba `_lastCheckTime =
  millis( )` para o primeiro envio esperar um intervalo inteiro, e um wake M1 é um boot: com
  `t_int` = 60 s o primeiro envio cairia aos 60 s de uptime, depois do teto de 30 s do FLUSH. Com
  os 300 s da configuração de campo, **nunca**. É o F05 do plano de correções, agora medido: a
  bancada anterior usava `t_int` = 100 ms e não via.
- **Com `t_int` = 1 s, um lote por segundo**: 28 lotes em 29 s de FLUSH, 2.800 registros — o rádio
  passou 87 % do FLUSH esperando o próprio intervalo (o ciclo custa 142 ms).
- **Com `t_int` = 1 ms o wake drena 19.600 registros em 27,6 s** (710 reg/s, 142 ms/req — um pouco
  melhor que os 127 ms do M0, sem web nem polling entre lotes) e ainda assim fica 54,6 s acordado:
  o teto de 30 s do FLUSH é o que termina o wake, não o fim da fila (sobraram ~15 mil).
- **O custo fixo do wake é ~25 s** (boot + warm-up dos sensores + Wi-Fi + NTP), antes de qualquer
  lote. Por isso o registro custa 2,8 s/1.000 no HTTP mesmo a 710 reg/s: dos 54,6 s, 28 s são
  envio e 25 s são o wake. **O envio é barato; estar acordado é caro** — a conclusão do §0.
- HTTPS no wake: 20 lotes em 30 s de FLUSH (1,57 s/req, o mesmo custo do M0), 2.000 registros,
  28,9 s/1.000. Com o keep-alive (§2.5) o mesmo FLUSH levaria ~60 lotes.

**Correção implementada (F05, "modo dreno"):** o FLUSH do Air passa a chamar
`TelemetryManager::setDrainMode(true)` ao entrar e `false` ao sair (e no `air stop`); em modo
dreno o `update( )` ignora `t_int` e manda um lote por chamada enquanto houver pendentes, mantendo
o backoff após falha (é ele que encerra o wake com coletor mudo). `t_int` continua decidindo **em
que wake** o rádio liga (`airTelemetryDue( )`). Validação: §2.8.

### 2.8 Validação do modo dreno (dois wakes, `t_int` = 60 s, HTTP lote 100)

| wake | acordado (sonda) | FLUSH do wake | req | registros | ms/req | s acordado / 1.000 reg |
|---|---|---|---|---|---|---|
| antes (§2.4, mesmo `t_int`) | 56,9 s | 30 s, ocioso | 0 | 0 | — | ∞ |
| **depois, backlog inteiro** (`tel_reset`) | sonda sem janela ¹ | — | — | — | — | — |
| **depois, em regime** (sem `tel_reset`) | **57,2 s** | 30,0 s | **188** | **18.800** | 160 | **3,04** |

¹ O ciclo no lugar deste wake drenou 127 lotes (12.700 registros) em 25 s e terminou numa falha
(o servidor registrou um `TimeoutError` numa conexão), dormiu o mínimo (OVERRUN) e o boot seguinte
**subiu em M0**: um lote por minuto, sem dormir, até o harness intervir. Boot sem marcador de
hibernação e sem autópsia — a mesma assinatura do reboot da §2.7. Não se repetiu no wake em
regime, que fez o ciclo completo (dreno, sono de 60 s, wake, dreno de 30 s, sono) com a sonda
confirmando.

O wake em regime é a prova do F05: **de 0 para 18.800 registros no mesmo wake de 57 s**, com o
mesmo `t_int` de 60 s da configuração — `t_int` passou a dizer em que wake o rádio liga, e dentro
do wake o dreno vai na velocidade do servidor. O ganho seguinte é o teto derivado (§3.4): este
wake ainda parou no teto de 30 s com ~16 mil registros na fila.

### 2.5 A/B: sessão TLS mantida entre lotes

Mesma bancada, HTTPS, 45 s. "Firmware KA" = `TEL_TLS_KEEPALIVE_EXPERIMENT 1` (o `stop( )` da
sessão só no insucesso; `HTTPClient` persistente no caminho TLS; guarda de 3 s de ociosidade).
"Servidor KA" = `server_http.py --keepalive` (responde `Connection: keep-alive` e continua lendo no
mesmo socket).

| célula | lote | req | reg/s | ms/req | POST (aparelho) | handshakes TLS | heap mín / maior bloco |
|---|---|---|---|---|---|---|---|
| base (firmware e servidor fecham) — §2.2 | 25 | 33 | 17,9 | 1.397 | 526 ms | 33 | 89,0 / 45,4 KB |
| **A: firmware KA + servidor KA** | 25 | 170 | **92,9** | **269** | 90 ms | **1** | 80,0 / 63,8 KB |
| base — §2.2 | 100 | 27 | 59,3 | 1.685 | 619 ms | 27 | 89,0 / 79,4 KB |
| **A: firmware KA + servidor KA** | 100 | 91 | **201,0** | **498** | 178 ms | **1** | 80,0 / 70,6 KB |
| B (controle): firmware KA + servidor que **fecha** | 100 | 28 | 62,2 | 1.607 | 613 ms | 28 | 88,5 / 71,6 KB |

- **Um handshake por janela em vez de um por lote: 5,2× no lote 25, 3,4× no lote 100.** O
  ms/req cai de 1,4–1,7 s para 0,27–0,50 s; o que sobra é o custo próprio do TLS por byte (2,7
  ms/registro, §2.2) mais o custo fixo do aparelho. O HTTPS com sessão mantida fica a **4× do
  HTTP** no lote 100 (498 contra 127 ms/req), não mais a 13×.
- **O controle prova que o ganho precisa dos dois lados**: o firmware KA contra um servidor que
  fecha a conexão custa o mesmo que o base (1.607 contra 1.685 ms/req, dentro do ruído) — e não
  regride. Um servidor que não mantém a conexão simplesmente não se beneficia.
- **Custo em heap: ~9 KB** (88,9 → 80,0 KB mínimos) — a sessão BearSSL fica residente entre os
  lotes em vez de ser liberada. O maior bloco **cresce** (45 → 64–71 KB): sem alocar e liberar os
  iobufs a cada lote, a fragmentação cai.
- **O reuso é estado da instância do `HTTPClient`** (`_canReuse` nasce falso em cada objeto novo e
  só vira verdadeiro ao ler a resposta) — a primeira versão do experimento, com o objeto local à
  função, não podia reutilizar nada; a instância persistente no caminho TLS foi o que fez o A
  funcionar. Pego antes de medir, lendo o framework.
- Ficam por medir antes de virar padrão (D-12): `phase_survive` (`drip`, `huge`, `close_early`,
  `rst_mid`) com a sessão mantida — a defesa contra o `drip` passa a valer só no caminho de
  insucesso — e um soak de 30 min HTTPS com keep-alive.

### 2.6 Retratação: os "22–35 % de reenvio" eram a bancada, não o firmware

A primeira matriz (01:00) mostrou, em quase toda janela HTTP, o fio **recomeçando do registro mais
antigo** no meio da medição — 22 a 35 % dos registros recebidos repetidos. Fui atrás no firmware:
instrumentei os dois únicos caminhos que zeram o cursor (`cursor ahead of data` e a leitura do
`t_cursor.bin` com o cache vazio) mais uma linha por lote (`cursor=… first=… last=… new=…`), e rodei
três janelas de lote 100 com a serial capturada o tempo todo (`serial_probe.py`):

| rodada | req | registros | reenviados | sonda de reset | cursor |
|---|---|---|---|---|---|
| 1 | 351 | 34.881 | 3 (0 %) | nenhuma | monotônico |
| 2 | 351 | 34.883 | 2 (0 %) | nenhuma | monotônico |
| 3 | 351 | 34.884 | 1 (0 %) | nenhuma | monotônico |

O fio das janelas antigas explicou o resto: **cada uma começava no meio do backlog** (o primeiro
registro recebido era de 12, 20 ou 26 de agosto, nunca o mais antigo) e só depois pulava para o
mais antigo. É a sequência do próprio harness: o servidor era ligado **antes** do `commit_all`; o
aparelho reiniciava, retomava o dreno do cursor persistido (o `saveConfiguration( )` grava o cursor
junto com a config) enquanto o harness ainda fazia login; e então o `tel_reset` do harness recomeçava
do mais antigo — como deve. Os "duplicados" eram registros enviados **antes** do `tel_reset`,
contados pelo servidor porque ninguém cortava a janela. Os 1–3 restantes são o bloco fora de ordem
que já existe nos arquivos reais de agosto (relógio provisório de 14/08), não reenvio.

Correção da bancada: `window( )` passa a cortar a janela do **log por request do servidor**, por
relógio de parede, e reporta o que chegou antes dela em `pre_window_records`. A matriz foi refeita
com isso (v2, abaixo). A coluna "únicos/s" das tabelas antigas sai: media o meu artefato.

O que **era** bug, e ficou corrigido no caminho: `setLastSentTimestamp( )` reiniciava a janela de
coalescência de 5 s a **cada** chamada. A um lote a cada ~50 ms a janela deslizava para sempre e o
cursor **nunca ia para o flash durante um dreno** — milhares de lotes sem nada persistido; uma queda
de energia no meio reenviaria o dreno inteiro no boot seguinte (no Air o flush forçado antes de
dormir cobria o caso do sono, não o da queda). A janela agora ancora no primeiro `set` sujo: uma
escrita a cada 5 s sob carga, que era a intenção original.

### 2.7 O reboot da célula de 6 s

Cronologia pelo log persistido (`/api/logs`, decodificado com `logcodes.tsv`): boot às 01:45:52
(pós-flash); `tel_reset` da célula às 01:50:11; falhas `ctx=−11` (read timeout de 4 s) às
01:50:15, 01:50:25 e 01:50:40 (`up=264/274/288 s`, backoff 5 → 10 → 20 s); **boot novo às
01:50:46** (`up=17 s` às 01:51:03), sem nenhum registro `SYS_BOOT` — nem `[FTL]` nem INFO.

Pela autópsia do `LogManager` (`performCrashAutopsy`), um boot **silencioso** só sai de dois
caminhos: reboot limpo marcado por `markCleanReboot( )` ou **reset físico / power-on** (registro
`REASON` zerado). Os chamadores de `safeReboot( )` são CLI, OTA, `commit_all` e o timeout do modo
AP — nenhum se aplica ao instante; o sono do Air não estava armado. Um watchdog real teria escrito
`[FTL] SYS_BOOT ctx=2xx`, e um reset via picotool, INFO. Sobra o reset físico: o alvo vive no USB
do PC, e o rig já registrou "reboot sem causa = perda de energia". Heap 95,6 KB mínimo na célula,
polls web todos respondidos até o instante — nada apontando para o firmware.

**Repetida às 02:20 com a serial acampada** (`serial_probe.py --delay 6 --seconds 75`): quatro
timeouts de 4 s (`read Timeout (-11)`), backoff 5 → 10 → 20 → 40 s, **nenhum reboot** em 75 s e
nenhum banner de boot na serial. Fica classificado como **não reproduzido, provável perda de
energia** — com o registro de que o 1º wake de validação do modo dreno teve um boot com a mesma
assinatura (§2.8 ¹), também colado numa falha de envio. Se acontecer uma terceira vez, a serial
acampada durante um soak com coletor lento é o teste. O que a célula mostrou de verdade, e que
**é** do firmware, está na §2.3: acima de 4 s o coletor fica invisível e o aparelho gira em
backoff.

---

## 3. Desenho proposto

### 3.1 Cadência automática entre lotes

```
depois de um envio com sucesso, com ciclo medido c (ms, do collectBatch ao end):
    gap = 0                                       se c ≤ FAST_MS[transporte]   (servidor rápido: próximo lote já)
    gap = min(max(c, 2 × gap_anterior), GAP_MAX)  se c > FAST_MS[transporte]   (servidor lento: o intervalo cresce a cada lote lento)
    gap *= penalidade_RSSI
depois de uma falha (código ≠ 2xx, timeout, socket):
    backoff exponencial como hoje (5 s ×2 … 300 s, jitter); gap volta a 0 no próximo sucesso rápido
```

- `FAST_MS` é **por transporte**, porque o custo próprio do aparelho é o que define "rápido": o
  maior ciclo HTTP com sucesso medido é 281 ms (lote 250, §2.1) e o HTTPS sem keep-alive fica em
  1,4–1,8 s (§2.2). Proposta: **`FAST_MS` = 400 ms no HTTP, 2.500 ms no HTTPS sem sessão mantida**,
  e o valor do HTTPS cai para o ciclo sem handshake se o keep-alive entrar (§2.5). Um servidor que
  responde além disso está demorando **mais do que o aparelho leva para fazer a sua parte** — é
  esse o critério, não um número absoluto.
- `GAP_MAX` ≈ 10 s: acima disso o aparelho está esperando um servidor doente. No M0 (rede
  elétrica) o gap é só cortesia — não custa energia. No Air custa o rádio ligado, e por isso a
  regra lá é outra: **gap maior que o orçamento restante do wake = dormir agora** (§3.4). O dado
  espera no flash (116 dias de capacidade) e sai no próximo wake de telemetria.
- `telInterval` **não entra** nesta conta. Ele passa a dizer *de quanto em quanto tempo o aparelho
  drena*, não *quanto tempo espera entre lotes de um mesmo dreno*.

### 3.2 Lote automático (AIMD dentro do teto de heap)

```
tetoHeap  = safeBatchLimit(telBatchSize)          // já existe
lote      = clamp(loteAtual, LOTE_MIN, tetoHeap)
sucesso e c ≤ FAST_MS   → lote = min(lote × 3/2, tetoHeap)
sucesso e c > FAST_MS   → lote mantido
falha / timeout         → lote = max(lote / 2, LOTE_MIN); backoff
```

- Justificativa medida: nos dois transportes a vazão cresce monotonicamente com o lote até o
  clamp de heap (§2.1: 137 → 827 reg/s; §2.2: 6,7 → 86,9 reg/s), sem nenhuma falha em 2.237
  requisições — então o lote ótimo é **o maior que o heap permite**: hoje ~232 registros JSON
  (plain) / ~158 (TLS) com 95 / 89 KB livres. O AIMD existe para o caso que a bancada não tem: um
  servidor real que rejeita ou demora com payloads grandes (o HTTPS de agosto caía de 26 para 7,8
  reg/s ao passar de 25 registros — não se repetiu aqui, com a mesma pilha TLS).
- `LOTE_MIN` = 10. O valor inicial é o último lote bom, guardado em RAM (no Air, em `scratch`
  não vale a pena: recomeçar em 50 custa dois ciclos de crescimento).
- O tamanho do payload é limitado também pelo timeout de 4 s: 250 registros JSON ≈ 9 KB, que o
  servidor de bancada absorve em 11 ms — mas um servidor real lento de ingestão pode não. É o
  caso que o AIMD cobre.

### 3.3 Custo fixo por lote — as alavancas

| alavanca | ganho | risco |
|---|---|---|
| **keep-alive TLS entre lotes** (`TEL_TLS_KEEPALIVE_EXPERIMENT`) | elimina um handshake por lote; mede-se em §2.5 | a defesa contra `drip` fica só no caminho de insucesso; o `end( )` do framework drena resto de corpo com prazo e derruba `_canReuse` |
| TX buffer TLS 512 → 2048 B | 4× menos registros TLS por POST | +1,5 KB de heap por sessão |
| cache da lista de arquivos dentro de um dreno | corta a varredura de diretório por lote | invalidar quando o histórico sela um bloco |
| keep-alive TCP no HTTP puro | evita um connect por lote (~ms) | `WiFiClient` persistente como membro; ganho pequeno frente aos 48 ms |

### 3.4 Air: hibernar e continuar depois

```
no início do FLUSH:
    orçamento = tetoFlush − (millis() − início do wake)
    tetoFlush = min(flushTimeoutMs, histIntervalMs − amostragem − margem)     // nunca estourar a leitura
a cada lote:
    estimativa = pendentes / vazãoMedida
    se estimativa > orçamento restante → envia até o orçamento e dorme; o contador de wakes retoma
    se gap calculado > orçamento restante → dorme agora (não vale a pena ficar acordado esperando)
```

- Já medido no plano de energia: com leitura a 1 min e `flushTimeoutMs` = 30 s, o wake de
  telemetria estourou o intervalo (`OVERRUN`). A derivação do teto pelo intervalo de leitura é o
  que fecha isso.
- O contador de wakes desde o último envio **zera mesmo com envio parcial**: a punição do
  coletor lento é esperar um período inteiro, não retentar a cada wake com o rádio ligado.

### 3.5 O que muda para o operador

- `t_int` continua existindo com o mesmo nome e a mesma unidade, mas significa **período entre
  drenos** (M0) e **período de telemetria** (Air). O default de fábrica pode ficar em 5 min.
- `t_bat` vira **teto**. Quem quiser fixar o lote antigo põe `t_bat` pequeno.
- `air status` mostra a vazão medida do último dreno e o lote corrente.

### 3.6 Onde mexer (esboço, com os pontos verificados no código)

| peça | hoje | mudança |
|---|---|---|
| cadência entre lotes | `TelemetryManager::update( )`, `src/TelemetryManager.cpp:349-364`: `effectiveInt = max(telInterval, 1,5 × EMA) × RSSI`, teto 60 s, e o `return` quando `now − _lastCheckTime < effectiveInt` | `effectiveInt` passa a ser o **gap** da §3.1: 0 se o ciclo anterior foi rápido, senão `min(max(ciclo, 2 × gap_anterior), GAP_MAX)` × RSSI. `telInterval` sai dessa conta e entra só em `_lastDrainEnd`: um dreno novo começa quando `now − _lastDrainEnd ≥ telInterval` **ou** quando ainda há pendentes do dreno corrente |
| o que a EMA mede | `:883-885`: `_smoothedLatencyMs` só vê o `POST( )` — 13 ms no HTTP, enquanto o ciclo inteiro custa ~70 ms | `t0` antes do `collectBatch( )`, `t1` depois do `http.end( )`; a EMA passa a ser do ciclo, que é o que o gap precisa. `metr.tl` continua sendo o POST (é o que o painel chama de latência) |
| lote automático | `safeBatchLimit( )`, `:516`: `min(configurado, HARD_CAP, teto de heap)` | `min(configurado, HARD_CAP, teto de heap, _batchAuto)`; `_batchAuto` cresce ×3/2 após sucesso rápido, mantém após sucesso lento, cai /2 após falha (§3.2); nasce em 50 |
| várias rodadas por passada do loop | `update( )` envia **um** lote por chamada e volta ao `loop( )`; o resto do loop (web, sensores) entra entre dois lotes | com gap 0, `update( )` pode encadear até N lotes na mesma chamada enquanto `millis( ) − t0 < TEL_SLICE_MS` (~250 ms), alimentando o watchdog entre eles; acima disso devolve o loop. Mede-se quanto do custo fixo é o loop e quanto é o ciclo |
| sessão TLS entre lotes | `attemptHttpUpload( )`, `:920-929`, atrás de `TEL_TLS_KEEPALIVE_EXPERIMENT`; o reuso é estado da **instância** do `HTTPClient` (`_canReuse` nasce falso), por isso a instância é membro (`_httpKeepPtr`) no caminho seguro | decisão D-12 pela §2.5: entra como padrão ou vira bit de config |
| Air: orçamento do FLUSH | `AppManager_Air.cpp:319-359`: sai quando `pendentes == 0`, backoff > 0, rede caiu ou `flushTimeoutMs` (30 s de parede) | `orçamento = min(flushTimeoutMs, histMs − amostragem_medida − margem)`; a cada lote, `pendentes / vazão_medida` contra o restante; se o gap calculado > restante, dorme já. `airTelemetryDue( )` (a regra de N wakes) não muda |
| MQTT | `attemptMqttPublish( )` publica ≤ 5 registros um a um, acima disso um payload | mesma cadência e mesmo `_batchAuto`; o "ciclo" é do `connect( )` ao último `publish( )` confirmado |
| o que **não** muda | backoff por falha (5 s ×2 … 300 s, jitter), penalidade RSSI, `PREFLIGHT_FLOOR`, reserva de heap, piso de 30 dias, cursor coalescido 5 s + forçado antes do sono | — |

---

## 4. Testes que fecham cada parâmetro

| parâmetro | teste | aceite | resultado 07/09 |
|---|---|---|---|
| `FAST_MS` | §2.1 + §2.2: maior ciclo com sucesso sem handshake | ciclo p90 do HTTP < FAST_MS < ciclo p10 do HTTPS sem keep-alive | HTTP ≤ 281 ms, HTTPS ≥ 1,4 s → **400 ms / 2.500 ms** por transporte (§3.1) |
| `GAP_MAX_MS` / penhasco | §2.3: atrasos 0,5 … 6 s | gap medido ≈ atraso injetado até 3,5 s; a 6 s o aparelho entra em backoff sem reboot | cadência atual = 2,5 × atraso; a 6 s backoff **e um reboot silencioso** (§2.7) — aceite **não** atingido até a serial acampada dizer o que foi |
| lote máximo por transporte | §2.1/§2.2 até 250 | vazão cresce até o clamp de heap sem falhas; HTTPS revela ou não um teto menor | ✅ monotônico nos dois; clamp 232 (plain) / 158 (TLS); 0 falhas em 2.237 req |
| keep-alive TLS | §2.5 A/B, mesma célula com e sem | ms/req cai pelo custo do handshake; `drip`, `huge`, `close_early` da `phase_survive` continuam sem reboot | ✅ 5,2× / 3,4×, controle = base; **`phase_survive` com KA ainda por rodar** (D-12) |
| modo dreno (F05) | um wake com `t_int` = 60 s e leitura a 1 min | registros entregues > 0 e o wake dorme | ✅ 0 → 18.800 registros, dormiu (§2.8) |
| teto do FLUSH derivado | um wake com leitura a 1 min e coletor mudo | sem `OVERRUN`; o wake seguinte cai na cadência | por implementar (§3.4); hoje o wake de 57 s com leitura a 60 s já é OVERRUN |
| energia | §2.4: s acordado por 1.000 registros | HTTP ≪ 1 s / 1.000; HTTPS com keep-alive próximo disso | HTTP 2,8–3,0 s/1.000 **dominado pelos 25 s fixos do wake**; o envio em si custa 1,4 ms/registro |

---

## 5. Decisões para o Ângelo

- **D-11** `t_int` muda de significado (piso entre lotes → período entre drenos). Manter o nome ou
  criar `t_period` e deixar `t_int` só como legado?
- **D-12** Keep-alive TLS entrar como padrão depois da `phase_survive` limpa, ou ficar opcional
  (bit de config)?
- **D-13** Lote inicial: recomeçar em 50 a cada boot, ou persistir o último lote bom no `air.bin`
  (uma escrita por dreno bem-sucedido)?
- **D-14** CSV: medir e oferecer como padrão para o Air? 160 B/registro de estimativa de heap
  contra 350 do JSON dobra o lote possível.
- **D-15** Servidor **permanentemente** lento (ex.: ingestão em nuvem a 0,6–1 s): com a regra da
  §3.1 o gap dobra a cada lote até `GAP_MAX` = 10 s e fica lá — 50 registros a cada ~11 s no M0,
  um backlog de 35 mil leva ~2 h. Aceitar (é cortesia com o servidor) ou limitar o gap a `k ×
  ciclo` (ex.: 4×) para o dreno não ficar refém de um servidor só moderadamente lento?
- **D-16** `NET_SOCKET_TIMEOUT_MS` = 4 s é o penhasco (§2.3): um coletor que leve 5 s fica
  invisível. Subir para 8 s custa 4 s a mais acordado por tentativa falhada no Air (e o
  `WdtWindow` real é 8,388 s — o POST tem que caber com folga). Manter 4 s e documentar, ou 6 s?

---

## 6. Registro de execução

| quando | o quê | resultado |
|---|---|---|
| 07/09 00:30 | 28 dias sintéticos gerados e enviados por `/api/upload` (26 arquivos; 3 reais preservados em backup) | 30 arquivos, 58.066 B, `fs_u` 262 KB de 1 MB |
| 07/09 00:40 | shakedown do harness, HTTP lote 50, 30 s | 552 req, 27.600 reg, **920 reg/s**, 0 falhas |
| 07/09 00:44 | matriz de capacidade HTTP+HTTPS × {10,25,50,100,250}, 45 s (v1, contadores acumulados do servidor) | HTTP até 994 reg/s (lote 100); HTTPS 8,5–111 reg/s; "22–35 % de reenvio" — ver §2.6 |
| 07/09 01:05–01:15 | caça ao reenvio: 3 sondas seriais com lote 100 (`serial_probe.py`) | 0 % de reenvio, nenhum reset; artefato do harness (§2.6); fix real da coalescência do cursor |
| 07/09 01:22–01:40 | matriz v2 HTTP+HTTPS × {10,25,50,100,250}, 45 s, janela por request | HTTP 137→827 reg/s (`54 ms + 0,94 ms/reg`); HTTPS 6,7→86,9 reg/s (`1,38 s + 2,7 ms/reg`); 0 falhas em 2.237 req (§2.1, §2.2) |
| 07/09 01:40–01:45 | keep-alive TLS: A (firmware KA + servidor KA, lotes 25 e 100) e B (controle, servidor fecha) | 5,2× e 3,4×; controle = base; 1 handshake por janela (§2.5) |
| 07/09 01:45–01:51 | latência injetada HTTP lote 50: 0,5 / 1 / 2 / 3,5 / 6 s | `s entre envios ≈ 2,5 × d`; a 6 s só timeouts + backoff e **um reboot silencioso** (§2.3, §2.7) |
| 07/09 01:51–02:06 | wakes com sonda GP16, `h_int` 1 min: `t_int` 60 s / 1 s / 1 ms e HTTPS 1 ms | 0 / 2.800 / 19.600 / 2.000 registros por wake de ~57 s (§2.4) — F05 medido |
| 07/09 02:08–02:18 | modo dreno (F05) gravado; dois wakes de validação com `t_int` = 60 s | em regime: 18.800 registros no wake, 3,04 s/1.000 (§2.8); 1º wake subiu em M0 após um ciclo no lugar terminado em falha (§2.8 ¹) |
| 07/09 02:20 | célula de 6 s repetida com a serial capturada (banner de boot), 75 s | 4 timeouts, backoff 5→40 s, **0 reboots** — não reproduziu (§2.7) |
| 07/09 02:23 | portões: 6 envs nativos, `check_air_consistency` C1–C8, builds release/test/asserts/alpha/air | tudo OK; `pico_w_debug` já não cabia na flash (−99.884 B, pré-existente) |
| 07/09 02:24 | limpeza: dias sintéticos removidos, 3 arquivos reais devolvidos, cursor zerado, config do Ângelo restaurada | `cadence_cleanup.py` |
