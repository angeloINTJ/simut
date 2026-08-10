# Tempestade de rede — relatório de execução

**Data:** 2026-08-10 · **Branch:** `feat/calib-curves` · **Imagem:** `pico_w_test`
(HEAD `19d84fc` + Passo 0) · **Alvo:** Pico W 192.168.3.24 · **Host:** 192.168.3.31

Plano executado: `~/.claude/plans/vectorized-zooming-moore.md`.

---

## 1. Pré-voo

| Item | Estado | Medida |
|---|---|---|
| Patches do framework | ✅ aplicados | `wifi_tls_handshake_deadline` (3 marcadores), `httpclient_read_deadlines` (4), `clientcontext_rx_leak`, `lwipopts` |
| PBUF pool | ✅ 24 entradas | `LWIP_STATS=1`, `MEMP_STATS=1` |
| SAN do cert vs IP do host | ✅ bate | `IP Address:192.168.3.31`, válido até 2027-08-02 |
| Serial livre | ✅ | sem `fuser`; alvo `ttyACM0`, PicoHand `ttyACM1` |
| Imagem gravada | ✅ `pico_w_test` | `SIMUT_CLI_FULL=1` |
| Config real capturada | ✅ | `config_baseline_PREVOO.json` |
| Backup do histórico | ✅ | `history_backup/` + `MANIFEST.json` (sha256 por arquivo) |

**Config de telemetria do usuário, para restaurar na Fase 3:**
`t_srv=192.168.3.206` `t_port=8443` `t_sec=true` `t_path=/api.php` `t_int=10000`
`t_bat=50` `t_mode=0` `t_transport=0` `t_key=""` `t_sep=","`
`t_glob={"dev":"{DEV}","mac":"{MAC}","data":[{DATA}]}`
`t_line={"ts":{TS},"t0_ID":{t0},"u0_ID":{u0}}`

### Correções ao que o plano assumia

1. **`SIMUT_FACTORY_RESET=1` é flag morta** — não é referenciada em nenhum fonte, e o
   próprio `pico_w_release` a carrega. Gravar `pico_w_test` **não** apaga config.
2. **PBUF pool é 24, não 12.** A mensagem final do `patch.sh` (`PBUF_POOL_SIZE=12`)
   está desatualizada; o `patched_headers/lwipopts.h` reverteu para 24 em 02/08.
   A tabela de asserções do plano estava certa.
3. **Folga real de flash: 5656 B**, não os ~18 KB que o 98,3% do PlatformIO sugere
   (`.ota` = 10228 B + padding de `.rodata` ficam de fora). Qualquer correção de
   firmware nesta campanha tem esse teto.

### Baseline do device (antes da tempestade)

- 5 sensores lendo, BMP280 inclusive (não precisou de `reload confirm` pós-flash).
- PBUF `1 em uso / pico 4 / 24 total, 0 falhas`; RSSI −44 dBm.
- `metr`: `fx=0 ad=0 cgd=0 cgg=0 cgx=0`; heap livre 46,8 KB, maior bloco 39,5 KB.
- Storage **89,1 %** (933888/1048576) — **acima do gatilho de poda de 86 %** do
  próprio firmware (`enforceStorageLimit`). Daí o backup do histórico ser
  pré-requisito e não zelo: o `upload_churn` cruza o gatilho e o device apaga os
  arquivos mais antigos, 2 por vez.
- Histórico: 93 arquivos, 2026-05-03 → 2026-08-10.

### Reboots no log antes da campanha (3,5 h de janela)

36 boots, classificados pelo primeiro registro de cada boot:
**21 por watchdog** (`[FTL][SYS] code=1`) e **15 por perda de energia** (sem autópsia).

- O boot de 00:46:54 foi **a PicoHand sendo plugada** — dois RP2040 no mesmo
  barramento USB derrubam a alimentação do alvo. Sem autópsia = sem watchdog.
- O boot de 00:37:52 **foi** watchdog (`ctx=219`) e aconteceu antes de eu tocar no
  device: é a mesma classe do defeito da §2, com a telemetria drenando o backlog.

> **Discriminador correto** (o do plano estava incompleto): `FTL`+`ctx∈[100,499]`
> não separa stall real de flash, porque o toque de 1200 bps produz `ctx=219`/`207`
> — dentro da banda. O que separa é **o primeiro registro do boot**: `[FTL][SYS]
> code=1` = watchdog; qualquer outra coisa = perda de energia.

### Validação do instrumento

Antes de confiar na serial como canal forense, medi se ela mesma mata o alvo:

| Experimento | Resultado |
|---|---|
| Abrir serial (DTR=True), 5 s, fechar | sem reboot |
| `show metrics` (46 linhas) | sem reboot |
| `show system log` completo (85 KB, 5,6 s), drenando | sem reboot |
| `show system log` e **parar de ler** no meio (fechar a porta) | sem reboot |

A serial está limpa. Os reboots observados **não** vêm do instrumento.

---

## 2. Achado bloqueante: `/download` reinicia o device por watchdog

**Reprodutível em 4 baterias independentes**, sem tempestade nenhuma: baixar
arquivos de histórico por `GET /download?file=/history/NNN.h5` mata o device.

Assinatura (capturada na serial, no boot seguinte):

```
HW WATCHDOG: Core 0 loop stalled (no feed in WDT window).
C0=[WEB_POLL] C1=[DISPLAY] at up=77660ms sc3=0x80088013 hp=721 (219)
```

### O relógio do defeito: um download por boot

Downloads em rajada davam a impressão de "morre no 2º ou 3º". Espaçá-los em 20 s
mostrou o que realmente acontece — 13 arquivos, padrão idêntico em 9 deles:

```
20260727.h5: ConnectionError 5.13s   <- device ja estava reiniciando
20260727.h5: OK 8106B 0.07s          <- retry pos-boot: instantaneo
[20 s de intervalo]
20260728.h5: ConnectionError 5.12s   <- de novo
20260728.h5: OK 8108B 0.07s
```

O download **sempre termina bem, em 0,05-0,09 s**. Quem falha é a requisição
*seguinte*. Linha do tempo que fecha com o watchdog de 8388 ms:

| t | evento |
|---|---|
| 0,00 s | `GET /download` responde 200 completo |
| ~0,1 s | handler retorna; Core 0 estaciona sem alimentar o watchdog |
| ~8,4 s | HW watchdog dispara |
| 8-20 s | boot (web volta em ~16-20 s) |
| 20 s | requisição seguinte pega o device no meio do boot → `ConnectionError` |

Ou seja: **exatamente um download por boot**, e o reboot é consequência *atrasada*
da resposta anterior — não acontece durante o stream. As rajadas conseguiam 2-3
porque cabiam dentro da janela de 8,4 s antes do watchdog disparar.

Isso estreita muito o suspeito: o parque começa **depois** de a resposta estar
entregue. E como `drainOrDrop` carimba `hp=600/601/602` e a autópsia diz **721**,
ele não está lá. Sobra o trecho entre o último `sendContent` e o `drainOrDrop`:
`f.close()`, o `LOG_CODE` de `handleDownload` (que é escrita em flash, dentro do
handler, com o cliente ainda conectado) ou o `_finalizeResponse` do framework.

**O que o dado descarta:**

- **Não é D14 / PBUF.** Pool ficou `1 em uso / pico 3 / 24 total, 0 falhas` durante
  a repro. O vazamento de pbuf não participa.
- **Não é o instrumento.** Ver tabela da §1.
- **Não é `waitSendRoom`.** Ele carimba `hp=710` e alimenta o watchdog no laço;
  a autópsia diz **721**, que é o carimbo *imediatamente após* `sendContent`
  retornar (`WebManager_Send.cpp:135`).

### Causa-raiz (confirmada por autópsia + leitura do framework)

O laço de `WebManager::update( )` (`WebManager_Core.cpp:296-308`) zera `hp` a cada
requisição e chama o guard depois:

```c
watchdog_hw->scratch[7] = 0;
_chunkedResponse = false;
_server.handleClient( );
if (_drainPending) { drainOrDrop( ); _drainPending = false; }
```

`drainOrDrop` carimba `HPOS(600)` dentro do seu laço. A autópsia diz **721**,
nunca 600 — **o laço do `drainOrDrop` não chegou a rodar**. Logo o parque está
dentro do próprio `handleClient( )`, depois do último `sendContent`.

E está — em `WebServerTemplate.h:129-132`:

```c
case CLIENT_MUST_STOP:
    _currentClient->stop();      // sem argumento
    break;
```

`WiFiClient::stop(maxWaitMs=0)` → `flush(0)` → e `flush` traduz 0 como
**"use o default"** (`WIFICLIENT_MAX_FLUSH_WAIT_MS`, 300 ms), não como
"não espere". Vai para `ClientContext::wait_until_acked(300)`, cujo relógio
(`last_sent`) **reinicia a cada progresso de ACK**:

```c
uint32_t last_sent = millis();
while (1) {
    if (millis() - last_sent > (uint32_t) max_wait_ms) return false;
    ...
    if (sndbuf != prevsndbuf) { last_sent = millis(); }   // progresso => renova
```

Um par que confirma a cada ~250 ms renova o prazo para sempre. E o laço **não
alimenta o watchdog**. Este é exatamente o mecanismo `stop(0) ≠ abort` que o
projeto já caçou e corrigiu em `f0f8e23` — mas ali foram trocados os `stop(0)`
**do SIMUT**; este é o do **framework**, e continua nu.

> **Correção a um erro meu, registrada de propósito:** cheguei a escrever que o
> parque era o `delete _currentClient` no fim do `handleClient( )`. Não é —
> `~WiFiClient( )` chama `unref( )`, que faz `discard_received( ); close( );`
> e `close( )` usa `tcp_close( )`, sem espera. O `delete` é inocente; quem
> espera é o `stop( )` do `CLIENT_MUST_STOP`.

**O comentário do próprio `update( )` está errado neste caso**: diz que o dreno
roda "antes de o framework aposentar o cliente com seu fechamento educado". Para
resposta com `Connection: close` o framework aposenta **dentro** do
`handleClient( )`, e `drainOrDrop` chega tarde — sempre.

O caminho chunked escapa por acidente feliz: `dropAbortedStream( )` aborta o pcb
**antes** de o handler retornar, então o `delete` encontra um pcb morto e sai na
hora. O não-chunked não tem equivalente.

**Correção indicada (dentro do projeto, preferida):** drenar ou largar a cauda
**antes de o handler retornar** — chamar `drainOrDrop( )` no fim de
`safeStreamFile( )`. Com a cauda já confirmada (ou o pcb já ido), o `stop( )` do
`CLIENT_MUST_STOP` encontra `tcp_sndbuf` cheio e sai na primeira volta. É o
mesmo movimento que `f0f8e23` fez do lado chunked, aplicado ao lado que ficou.

**Alternativa (patch de framework, nº 4):** trocar `_currentClient->stop( )` por
`_currentClient->stop(1)` em `WebServerTemplate.h:131`, pela mesma razão que o
projeto já trocou os seus. Cobre todas as respostas de uma vez, mas mora fora
do repositório e some num upgrade de framework — daí ser a segunda opção.

### Como isto se encaixa no `f0f8e23`

`hp=721` é o resíduo que `f0f8e23` deu por fechado.
Ele reaparece porque aquele fix arma o abort duro só quando a resposta é chunked:

```c
if (_chunkedResponse) { dropAbortedStream(origin); return false; }   // Send.cpp:146 e :71
```

`safeStreamFile` (`WebManager_Send.cpp:262`) chama `_server.setContentLength(f.size())`
— resposta **não-chunked**. Logo o caminho educado de fechamento continua aberto
nela, e é onde o Core 0 estaciona sem alimentar o watchdog. O comentário do
próprio `drainOrDrop` (`Send.cpp:176-185`) descreve exatamente esse parque
(`delete _currentClient` → `stop()` → `wait_until_acked()`, cujo prazo reinicia a
cada ACK), e `drainOrDrop` roda em `update()` — **depois** do `handleClient()` que
já estacionou.

**Unificação:** é a mesma assinatura `ctx=219` do `/api/backup` já registrado como
"derruba com FTL 1 ctx=219". `/api/backup` e `/download` compartilham
`safeStreamFile`. São um defeito só: **streaming de arquivo não-chunked**.

**Não corrigido nesta sessão** — a correção mexe no caminho de fechamento do
WebManager, precisa de repro isolada e de re-teste, e o teto de flash é 5656 B.
Registrado como o item nº 1 da fila.

---

## 3. Entregas de ferramental

| Arquivo | O quê |
|---|---|
| `src/WebManager_Api.cpp` | Passo 0: `metr.cgd` / `metr.cgg` / `metr.cgx` expostos em `/api/status` |
| `tools/telemetry_bench/server_http.py` | modos `never_read` (zero-window) e `tls_bigrecord`; flags `--rcvbuf`, `--big-record-bytes` |
| `tools/telemetry_bench/storm_net.py` | orquestrador da simultaneidade |
| `scratchpad/backup_history.py` | extração de `/history` resistente aos reboots do device |

**Por que `--rcvbuf` existe:** sem ele o `never_read` não mede nada. O kernel
dimensiona o buffer de recepção em centenas de KB, então a requisição inteira do
device cabe nele e o `sendAll` retorna sem ter bloqueado em nada. Validado no
host, com o buffer de envio do cliente reduzido para emular o `TCP_SND_BUF` do
Pico:

| modo | empurrou | veredito |
|---|---|---|
| `never_read` | 8192 / 262144 B, bloqueou 6 s | contrapressão real |
| `ok` (controle) | 262144 / 262144 B em 0,0 s | passa tudo |

E o batch tem de subir junto (120, não 10): com 10 registros o payload é ~500 B,
cabe na janela de ~4 KB e a falha não acontece.

---

## 4. Defeitos confirmados por leitura de código

Dois dos alvos que o plano listou para a Fase 2 se confirmam sem precisar de
repro — o código diz. Ambos continuam **não corrigidos** nesta sessão.

### 4.1 `_inHistoryHandler` vaza na cauda "extremes" (latch permanente)

`WebManager_History.cpp`, linhas **790, 798, 801**:

```c
if (!safeSend("],\"extremes\":{")) return;      // 790
...
if (!safeSend(e)) return;                        // 798
...
if (!safeSend("}")) return;                      // 801
```

São `return` nus. O desenrolar da função está nas linhas 841-843 e é pulado:

```c
_handlerDeadline = savedDeadline;                                  // 841
if (_displayRef) _displayRef->setWebBusy(false);                   // 842
__atomic_store_n(&_inHistoryHandler, false, __ATOMIC_RELEASE);     // 843
```

Consequência de um único abort na cauda — e um abort ali é rotina, porque é
onde o prazo de 15 s do handler costuma vencer em faixas longas:

1. `_inHistoryHandler` fica **true até o reboot** → todo `/api/history_multi`
   seguinte responde `503 {"error":"Already processing"}`. Gráficos mortos.
2. `setWebBusy(true)` fica preso → o overlay "Acessando via Web / Aguarde"
   nunca sai e **o toque continua bloqueado**. É o "display travado".
3. `_handlerDeadline` não volta ao valor salvo.

Correção natural: trocar os 3 `return` por um caminho único de saída (flag +
`goto unwind`, ou um RAII que solte as três coisas no destrutor) — o mesmo
padrão que o projeto já usa em `Core1FlashPause`/`RenderGuard`.

### 4.2 `/api/sec_status` — `snprintf` acumulado pode escrever fora do buffer

`WebManager_Auth.cpp:483-508`, `char buf[512]`:

```c
pos += snprintf(buf + pos, sizeof(buf) - pos, ...);   // 499
if (pos >= (int)sizeof(buf) - 2) break;               // 505  <- depois da escrita
...
pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");  // 508
```

`snprintf` devolve o tamanho que **caberia**, não o que escreveu. Com os 8 slots
(`LOGIN_STATE_SLOTS`) preenchidos por IPs longos, `pos` ultrapassa 512; o guard
da linha 505 roda tarde (a escrita já aconteceu) e apenas sai do laço. Então a
linha 508 calcula `sizeof(buf) - pos` em `size_t` — **underflow para um valor
enorme** — e escreve `"]}"` em `buf + pos`, fora do array. Corrupção de pilha.

Não acionado no rig: exige 8 IPs de origem distintos e o host tem um só.
Correção: guardar antes de escrever e usar `int room = (int)sizeof(buf) - pos;
if (room <= 1) break;`.

---

## 5. Fase 0 — smoke (3 janelas × 90 s, carga web ativa)

| falha | veredito | reboots | fx | PBUF fails | kills C1 | ts Δ | tf Δ | ping perdido |
|---|---|---|---|---|---|---|---|---|
| `ok` | SURVIVED | 0/0 | 0 | 0 | 0 | +21 | +0 | 0 |
| `huge1mb` | **REBOOT** | 1/1 | 0 | 0 | 0 | n/a¹ | n/a¹ | 4 |
| `never_read` | SURVIVED | 0/0 | 0 | 0 | 0 | +0 | **+3** | 0 |

¹ o reboot zera os contadores do device, então o delta da janela não é medida.

**Critério da Fase 0 cumprido**: o arranjo detecta reboot (USB + uptime), os
contadores respondem, e cada costura se manifestou como previsto ou sobreviveu.

### O que cada janela ensinou

**`ok` (controle)** — sob telemetria sã + 66 downloads + churn de upload + os
sensores, o device passa 90 s sem um arranhão e o cursor avança (`ts+21`,
`tf+0`). Isso é o que separa "o device é instável" de "este caminho é instável".

**`never_read`: RETRATAÇÃO — o teste não engajou, então "sobreviveu" não vale.**

Escrevi antes que a costura estava fechada. **Não está demonstrado.** O que a
janela mediu foi um device conversando com um sink que o ignora — não o
`sendAll` bloqueando, que é o ponto do teste.

A aritmética que eu não fiz na hora:

| grandeza | valor | fonte |
|---|---|---|
| janela do sink | 4096 B | `--rcvbuf 2048`, kernel dobra e tem piso |
| `TCP_SND_BUF` do device | 8 × 1460 = **11680 B** | `lwipopts.h:57` |
| logo, bloqueia só acima de | **~15,8 KB em voo** | soma dos dois |
| payload com batch 50 (teto do FW) | ~2,3 KB | 50 × ~45 B |

Uma ordem de grandeza abaixo do necessário. E pior: pedi `batch 120`, que o
firmware **recusa** — `AppManager_Commands.cpp:388` limita a 1-50 — então as
janelas rodaram com o batch **10** que estava lá antes, ~500 B. O
`cfg_tel` antigo mandava e seguia sem conferir; o verificador novo (§6.1) pegou
isto na re-execução do TLS, com quatro tentativas e um `DESISTIU` honesto.

O que continua **verdadeiro e medido**: `tf+3` com `ts+0`, ou seja, o device
falha e **segura o cursor**. E o relógio bate com `NET_SOCKET_TIMEOUT_MS = 4000`
(`SystemDefs_Network.h:26`) mais `BACKOFF_MIN_MS = 5000` dobrando: 4+5, 4+10,
4+20 ≈ 47 s → 3 tentativas em 90 s.

O que **não** está medido: se o `sendAll` sem feed cruza o watchdog quando de
fato bloqueia. Para isso o payload precisa passar de ~16 KB, o que exige engordar
`t_line` (512 B, só pela web) com batch 50 → ~25 KB. **Re-teste dedicado na
Fase 2.**

**`huge1mb` derruba, e a autópsia é a mesma da §2:**

```
C0=[WEB_POLL] C1=[DISPLAY] at up=214875ms sc3=0x80088013 hp=721 (219)
```

Isto une os dois defeitos. `hp` é posição do **WebManager**, e o caminho de
telemetria não passa por `safeSendN` — logo quem estaciona é o *envio web*, e a
resposta de 1 MB da telemetria é o que o empurra para lá. O sinal que fecha o
argumento veio da serial:

| sinal | mín | máx | critério | passa |
|---|---|---|---|---|
| **PBUF em uso (pico)** | 12 | **23** | < 24 | ✅ (por 1) |
| PBUF falhas | 0 | 0 | == 0 | ✅ |
| Core 1 heartbeat (ms) | 4 | 6 | < 1000 | ✅ |
| Core 1 kills de saúde | 0 | 0 | == 0 | ✅ |
| Core 1 exposto (flash) | 0 | 0 | == 0 | ✅ |
| RSSI (dBm) | −49 | −47 | [−120, 0) | ✅ |
| IRQ-off máx (µs) | 67580 | 76743 | < 60000 | ❌ (R2 conhecido) |

**PBUF chegou a 23 de 24.** Nunca falhou uma alocação, mas a margem virou um
envelope. É precisamente o cenário que o comentário do `waitSendRoom` descreve:
"com um leitor lento a fila de SEGMENTOS e o pool de PBUF acabam enquanto o
`tcp_sndbuf` ainda reporta espaço, e a escrita estaciona dentro do lwIP". O
`hp=721` é essa escrita.

Carga aplicada: 66 downloads (1 falha HTTP, **0 JSON inválido**), 31 polls de
status, 23 uploads.

---

## 6. Fase 1 — tempestade combinada

16 janelas HTTP × 173 s, carga web ininterrupta por cima (downloads de histórico,
polls de status, churn de upload) e os sensores por baixo.

| falha | veredito | reboots | fx | PBUF falhas Δ | PBUF pico | kills C1 | ts Δ | tf Δ | heap mín | ping perdido |
|---|---|---|---|---|---|---|---|---|---|---|
| `ok` | SURVIVED | 0 | 0 | +0 | **24** | 0 | +42 | +0 | 53044 | 0 |
| `error500` | SURVIVED | 0 | 0 | +0 | **24** | 0 | +0 | +4 | 52948 | 0 |
| `error401` | **REBOOT** | 1 | 0 | n/a¹ | **24** | 0 | n/a¹ | n/a¹ | 52940 | 4 |
| `refused` | SURVIVED | 0 | 0 | +0 | 12 | 0 | +0 | +3 | 53284 | 0 |
| `blackhole` | SURVIVED | 0 | 0 | +0 | 13 | 0 | +0 | +4 | 53236 | 0 |
| `slow20` | SURVIVED | 0 | 0 | +0 | 13 | 0 | +0 | +4 | 53204 | 0 |
| `half` | **REBOOT** | 1 | 0 | +0 | 14 | 0 | n/a¹ | n/a¹ | 53260 | 4 |
| `rst` | SURVIVED | 0 | 0 | +0 | 12 | 0 | +0 | +4 | 53396 | 0 |
| `rst_mid` | SURVIVED | 0 | 0 | +0 | 13 | 0 | +0 | +4 | 53356 | 0 |
| `garbage` | **REBOOT** | 1 | 0 | +0 | 14 | 0 | n/a¹ | n/a¹ | 53356 | 3 |
| `huge1mb` | **REBOOT** | 1 | 0 | n/a¹ | **24** | 0 | n/a¹ | n/a¹ | 53316 | 3 |
| `drip` | SURVIVED | 0 | 0 | +0 | 22 | 0 | +0 | +3 | 53372 | 0 |
| `close_early` | SURVIVED | 0 | 0 | +0 | 22 | 0 | +0 | +4 | 53356 | 0 |
| `never_read` | SURVIVED | 0 | 0 | +0 | 22 | 0 | +0 | +4 | 53148 | 0 |
| `syn_blackhole` | SURVIVED | 0 | 0 | +0 | 22 | 0 | +0 | +5 | 52972 | 0 |
| `dns_fail` | SURVIVED | 0 | 0 | +0 | 22 | 0 | +0 | +6 | 52780 | 0 |

¹ contador zerado pelo reboot da janela — delta não é medida.
`PBUF pico` é marca d'água **desde o boot**, então reinicia a cada reboot: os
12-14 que aparecem logo depois de um reboot são o contador recomeçando, não
alívio de pressão.

**Sobreviveram: 12/16.** Mais um reboot na transição de grupo (fora de janela,
capturado em `events.json`) → **5 reboots** na fase.

### Asserções

| sinal | medido | critério | veredito |
|---|---|---|---|
| `sys.uptime` monotônico | 5 regressões | sem reboot | ❌ |
| `metr.fx` | **0** em todas as janelas | == 0 | ✅ |
| `g_core1KillsHealth` | **0** em todas | == 0 | ✅ |
| Core 1 heartbeat | 1-33 ms | < 1 s | ✅ |
| Core 1 exposto (flash) | **0** | == 0 | ✅ |
| PBUF falhas | chegou a 3 | == 0 | ❌ **D14** |
| PBUF pool | pico **24/24** | < 24 | ❌ |
| `metr.lbm` | ≥ 52780 B | > ~5 KB | ✅ |
| cursor: avança no ok, segura no erro | `ts+42` no `ok`; `ts+0` em **todos** os 15 modos de erro | sem perda | ✅ |
| `APP_CORE1_DEAD` (502) | ausente | ausente | ✅ |
| ping do host | 14 perdas, todas em janela com reboot | responde | ⚠️ |
| ARP `INCOMPLETE` (D15) | **nunca** | nunca | ✅ |
| RSSI | −51 a −47 dBm | [−120, 0) | ✅ |
| IRQ-off máx | 68-78 ms | < 60 ms | ❌ (R2 conhecido) |

Carga aplicada: **723 downloads** (7 falhas HTTP, **1 JSON inválido**), 347 polls
de status, 248 uploads.

### O que a fase estabelece

**O Core 1 está limpo.** `fx=0`, `kills de saúde=0`, `exposto=0`, heartbeat
sempre abaixo de 33 ms, `APP_CORE1_DEAD` ausente — em 16 janelas de tempestade.
A classe R1 e a corrida do heartbeat, que o plano listava como suspeitas
principais, **não aparecem**. A pilha de 8 KB de `250428e` e o gate do
light-yield seguram.

**A telemetria não perde dado.** `ts+0` em quinze modos de falha distintos —
500, 401, RST no meio, resposta picotada, lixo, DNS morto, SYN engolido,
zero-window — e `ts+42` quando o servidor está são. O cursor faz exatamente o
que deve.

**O que falha é o lado web, e é uma coisa só.** 5 reboots, 5 autópsias
idênticas, e o `up=` diz que o gatilho é tempo sob carga (458 s a 1093 s), não o
modo de falha. Ver §2 e §6.1.

**D14 é real e o pool satura.** Pico em 24/24 e 3 falhas de alocação. Nunca se
recuperou sozinho dentro de um boot — só o reboot devolveu o pool.

### 6.1 Falha de instrumento encontrada e corrigida no meio da fase

O grupo TLS da primeira execução **não mediu TLS**. Um reboot caiu dentro do
`cfg_tel` da transição HTTP→TLS, o `write memory` nunca rodou, e o device
seguiu com `t_port=18080`/`t_sec=false` — apontado para uma porta onde o sink
HTTP já tinha sido morto. O sink TLS registrava `listening on 0.0.0.0:18443` e
recebia **zero conexões**; as janelas reportavam falhas de telemetria, que é
exatamente o que um servidor quebrado deve parecer. Nada no veredito denunciava.

Pego por uma checagem cética de `srvConns=0`, confirmado lendo `/api/config`
do device. **Correção**: `cfg_tel` agora relê a configuração por HTTP e repete
a sequência até bater, tratando "sem resposta" como reboot em andamento.
Grupo TLS re-executado do zero com o instrumento corrigido — primeira janela já
com 9 conexões, 9 handshakes, 0 falhas, 90 registros.

É a lição de [[validate-the-instrument]] outra vez: **mandar não é aplicar**.

### 6.2 Grupo TLS (re-execução com config verificada)

| falha | veredito | reboots | fx | PBUF falhas Δ | PBUF pico | kills C1 | ts Δ | tf Δ | heap mín | sink conns |
|---|---|---|---|---|---|---|---|---|---|---|
| `tls_ok` | SURVIVED | 0 | 0 | +0 | 15 | 0 | +36 | +0 | 46268 | 40 |
| `tls_error500` | SURVIVED | 0 | 0 | +0 | 15 | 0 | +0 | +4 | 46316 | 6 |
| `tls_blackhole` | SURVIVED | 0 | 0 | +0 | 15 | 0 | +0 | +4 | 46372 | 5 |
| `tls_garbage` | **PING-LOST=2** | 0 | 0 | +0 | 14 | 0 | +0 | +3 | 46612 | 5 |
| `tls_rst` | SURVIVED | 0 | 0 | +0 | 14 | 0 | +0 | +4 | 46564 | 5 |
| `tls_slow20` | SURVIVED | 0 | 0 | +0 | 14 | 0 | +0 | +2 | 46628 | 0 |
| `tls_refused` | SURVIVED | 0 | 0 | +0 | 14 | 0 | +0 | +4 | 46524 | — |
| `tls_never_read` | SURVIVED² | 0 | 0 | +0 | 14 | 0 | +0 | +5 | 45980 | 6 |
| `tls_bigrecord` | SURVIVED³ | 0 | 0 | +0 | 22 | 0 | +38 | +0 | 45964 | 41 |
| `tls_blackhole_http` | SURVIVED | 0 | 0 | +0 | 22 | 0 | +0 | +4 | 46020 | 7 |

**9/10 sobreviveram**, mais 1 reboot fora de janela (`hp=721`, o mesmo de sempre).
Heap sob TLS fica em ~46 KB contra ~53 KB em HTTP — é o BearSSL, esperado.
`ts+36`/`ts+38` nas janelas sãs e `ts+0` em todos os modos de erro: o cursor se
comporta igual sob TLS.

² Ver a retratação em §5: com batch limitado a 50 o payload não chega perto do
limiar de bloqueio, então este "sobreviveu" não vale como teste da costura.
Re-testado de verdade em §7.3.

³ **Também não testa o que pretendia.** `ts+38 tf+0` com 41 conexões diz que o
device recebeu tudo bem — o que significa que o OpenSSL **honrou** a extensão
`max_fragment_length` (RFC 6066) que o BearSSL negocia por causa do
`setBufferSizes(4096,512)`, e fragmentou em 4096. O registro grande nunca
existiu. O modo confirma que a negociação funciona ponta a ponta (resultado útil:
contra servidor bem-comportado o buffer de 4096 é seguro), mas testar um
servidor que **ignora** a extensão exige uma pilha TLS que se possa mandar
ignorá-la, o que o `ssl` do Python não oferece. Fica em aberto.

---

## 7. Fase 2 — correções e re-teste

Política do plano: causa-raiz → correção → re-rodar a mesma janela até verde.
Cada correção abaixo tem medição **antes** na imagem sem ela e **depois** na
imagem com ela, no mesmo teste.

### 7.1 `/download` — parque não-chunked · **CORRIGIDO E VALIDADO**

Correção: `drainOrDrop( )` no fim de `safeStreamFile( )`, antes de o handler
retornar (§2).

| | downloads ok | reboots | veredito |
|---|---|---|---|
| **antes** | 6/8 | **2** (`hp=721`) | FALHOU |
| **depois** | 8/8 | 0 | PASSOU |
| **depois (confirmação)** | 16/16 | 0 | PASSOU |

**24 downloads seguidos sem um reboot**, contra 2 reboots em 8. Mesmo script,
mesmo espaçamento de 20 s (maior que a janela de 8,4 s do watchdog), serial
capturando a autópsia dos dois lados.

### 7.2 `/api/backup` — mesmo defeito, outro caminho · **CORRIGIDO E VALIDADO**

`handleApiBackup` também usa `setContentLength( )` e não passa por
`safeStreamFile( )`, então a correção de 7.1 **não o cobria** — e o teste provou
isso na prática, o que é a melhor evidência de que o mecanismo estava certo.

| | backups ok | reboots | veredito |
|---|---|---|---|
| **antes** (já com 7.1 aplicado) | 2/3 | **1** (`hp=721`) | FALHOU |
| **depois** | 6/6 | 0 | PASSOU |

Cada backup são ~794 KB de resposta.

### 7.3 `sendAll` sem feed — a costura que o plano nomeou · **PARCIAL**

Com o payload finalmente acima do limiar (`t_line` engordado para 398 B ×
batch 50 = **19900 B**), a costura mordeu:

```
C0=[TEL_SEND] C1=[DISPLAY] at up=85491ms sc3=0x80088019 hp=600 (225)
```

**3 reboots em 150 s**, e o sink contou 9 conexões com **0 requisições
completas** — o device nunca terminou de enviar. É a
[[telemetry-guard-never-feeds]] demonstrada causando reboot pela primeira vez.

Causa-raiz, em `StreamConstPtr::sendAll` (`HTTPClient.cpp:88`):

```c
uint32_t start = millis();
while ((sent < _size) && (millis() - start < 5000)) {
    auto wrote = dst->write(_payload, towrite);   // parka o timeout de socket
```

O orçamento de 5 s limita o **laço**, não uma escrita. Cada `write( )` pode
parar os 4 s do `NET_SOCKET_TIMEOUT_MS`, então uma escrita iniciada perto do fim
do orçamento termina por volta de **9 s** — além dos 8388 ms — e não há
`watchdog_update( )` em lugar nenhum do laço.

**Correção**: 4º patch de framework, `patches/httpclient_send_feed.patch`,
ligado ao `patch.sh` (idempotente, verificado). Alimenta dos dois lados da
escrita; a maior lacuna sem feed passa a ser uma escrita (≤ 4 s). Seguro porque
os dois limites que já terminavam o laço continuam lá.

| | reboots / janela | autópsia |
|---|---|---|
| **antes** | 3 em 150 s | `C0=[TEL_SEND] ctx=225` |
| **depois** | 2 em 180 s | `C0=[WEB_POLL] ctx=219 hp=0` |

**A assinatura mudou: o parque dentro do `sendAll` acabou.** Mas o device ainda
morre sob essa falha extrema, agora do lado web e com `hp=0` — nenhum handler em
curso. Mecanismo provável: a conexão de telemetria travada segura recursos do
lwIP e o poll web morre de inanição. **Fica aberto como o item nº 1 da fila.**
O patch é melhora estrita e fica.

### 7.3b Re-teste da tempestade e uma hipótese DERRUBADA

Repeti as 5 janelas relevantes (`ok`, `error401`, `half`, `garbage`,
`huge1mb`, 173 s cada) na imagem corrigida. Previsão registrada **antes** de
rodar: os reboots deviam persistir, porque as correções de 7.1/7.2 tocam
`safeStreamFile`/`handleApiBackup` e o martelo web usa `/api/history_multi`,
que é chunked.

| rodada | reboots | assinatura |
|---|---|---|
| Fase 1 (sem correções), 16 janelas ~50 min | 5 | `hp=721` |
| re-teste, 5 janelas ~15 min | 1 | `hp=721` |
| re-teste **+ dreno chunked**, 5 janelas ~15 min | 1 | `hp=721` |

Previsão confirmada. **E a correção que tentei em cima dela falhou.**

Hipótese testada: `_finalizeResponse( )` escreve o terminador `0\r\n\r\n` dentro
do `handleClient( )`, e sob PBUF esgotado essa escrita estaciona. Tratamento:
drenar no destrutor do `HistUnwind`, que roda antes do terminador.

**Resultado: nenhuma mudança.** Mesmo 1 reboot em 5 janelas, mesmo
`C0=[WEB_POLL] hp=721`. A hipótese **não se sustenta**, e o código foi
**revertido** — deixar no repositório uma alteração de comportamento visível ao
cliente (corpo chunked sem terminador) justificada só por uma teoria que o
experimento não apoiou seria o oposto do que esta campanha existe para fazer. O
comentário revertido fica no lugar como aviso para não re-adicionar sem medida.

**Taxa de reboot não melhorou de forma demonstrável** pelo lado da tempestade:
~1 a cada 10 min antes, ~1 a cada 15 min depois, com n pequeno. As correções
7.1/7.2 estão validadas nas suas repros determinísticas, não aqui.

**Suspeitos que sobram** para o `hp=721` sob tempestade, em ordem:
1. Terminador chunked de **outro** handler sem dreno — `/api/status` é chunked e
   é chamado a cada 3 s pelo próprio amostrador.
2. `CLIENT_MUST_STOP` → `stop( )` nu do framework, alcançado por
   `_parseForm( )` falhando no churn de upload multipart.
3. Saturação de PBUF (24/24) como causa suficiente por si só.

Distinguir os três exige carimbos `HPOS` mais finos dentro do `update( )` e do
framework, não mais hipóteses. É onde a próxima sessão deve começar.

### 7.3c O `hp=721` da tempestade ERA a telemetria — e sumiu

Depois de derrubar a hipótese do terminador chunked (7.3b), instrumentei o que
faltava: **`HPOS(722)`** no fim do `safeSendN` e **`hp=740`** logo após o
`handleClient( )` retornar. Até então `721` era ambíguo — cobria "dentro da
cauda do `safeSendN`" e "qualquer lugar do framework depois do handler" com o
mesmo número, e é exatamente por isso que o resíduo não tinha endereço.

Rodei então o **grupo HTTP completo**, a mesma condição da Fase 1:

| | Fase 1 (sem correções) | agora |
|---|---|---|
| janelas | 16 × 173 s | 16 × 150 s |
| reboots | **5** | **1** |
| assinaturas `hp=721` | **5** | **0** |
| MTBF sob tempestade | ~10 min | **58 min** |
| downloads / JSON inválido | 723 / 1 | 557 / **0** |

**O `hp=721` desapareceu da campanha.** A única autópsia da corrida longa é
outra coisa:

```
C0=[CLI] C1=[DISPLAY] at up=3477483ms sc3=0x80088009 hp=740 (209)
```

**Atribuição corrigida — eu estava olhando para o lugar errado.** `hp` é zerado
no topo de cada iteração do `handleClient( )`. Um envio de telemetria que
consumisse ~8-9 s **sem alimentar** (a costura de 7.3) gastava o orçamento do
watchdog; o controle voltava ao `update( )`, uma requisição web era servida e
carimbava 721, e o watchdog vencia logo depois. A autópsia saía
`C0=[WEB_POLL] hp=721` **com o parque real estando na telemetria**. O lado web
não era o culpado: era o módulo que estava rodando quando a conta venceu.

Isso reconcilia tudo o que parecia contraditório: por que o defeito era
insensível ao modo de falha (qualquer um força retentativas de envio), por que
era proporcional ao tempo sob carga (mais envios, mais chances), e por que
drenar o terminador chunked não mudou nada (não era ali).

**O patch `httpclient_send_feed` é, portanto, a correção do resíduo da
tempestade também** — não só do `never_read`. Foi o único que moveu o número.

**O que sobra é o instrumento, não o device**: `C0=[CLI]` (ctx=209) com
`hp=740` (o web terminou limpo) é o amostrador serial desta campanha rodando
`show metrics`/`show net status` a cada 30 s — provavelmente a leitura de RSSI
viva, o velho suspeito `C0=[WIFI]` raro. 1 ocorrência em 58 min de tempestade.

### 7.4 `_inHistoryHandler` — corrigido, não reproduzido

Correção: guard RAII `HistUnwind` (§4.1). Destrutor não pode ser pulado por
`return`, então cobre os 3 `return` de hoje e os que alguém acrescentar amanhã.

**A sonda não reproduziu** em 6 rodadas de leitura lenta. O motivo está no
código: quando o prazo vence **durante o array de dados**, `aborted` fica true e
o bloco `if (!aborted)` pula a cauda inteira — o desenrolar normal roda. O
vazamento exige que o array termine com sucesso e o cliente suma **dentro** da
cauda, janela estreita demais para a leitura lenta acertar.

Defeito real por leitura, não reproduzido na bancada. A correção entra porque
custa zero (o build ficou 8 B **menor**) e é estritamente melhor.

Nota lateral: os contadores do Passo 0 pagaram por si aqui — `metr.cgd=2`
mostrou que houve dois abortos por prazo durante a sonda, informação que antes
não saía do device.

### 7.5 `/api/sec_status` — corrigido, não acionável no rig

Espaço checado **antes** de cada escrita, com backout da entrada truncada
(§4.2). Não acionável a partir de uma máquina só (precisa de 8 IPs de origem).

---

## 8. Fase 3 — recuperação e restauração

| item | estado |
|---|---|
| `reload confirm` | feito |
| `t_line` restaurado | ✅ idêntico ao baseline |
| 12 campos de telemetria | ✅ todos idênticos ao baseline |
| `/storm.bin` | ausente (404) |
| **Histórico** | ✅ **93/93 arquivos, 0 perdidos, 0 novos** |
| PBUF pós-reload | **1 em uso / pico 3 / 24, 0 falhas** — recuperou |
| heap / maior bloco | 46836 / 39520 (baseline: 46804 / 39493) |
| sensores | 5 lendo |
| NTP | sincronizado |
| storage | 88,3 % (era 88,7 %) |

**O backup do histórico acabou sendo seguro que não precisou ser acionado**: a
poda nunca disparou. Continua em `history_backup/` com `MANIFEST.json`.

**Telemetria do usuário está falhando, e não é a campanha.** O servidor
`192.168.3.206:8443` está **inalcançável do próprio host** ("No route to host",
100 % de perda no ping) — saiu do ar durante a madrugada. No começo da sessão
ele respondia (`ts` subindo, latência 2042 ms). A config do device está correta
e volta a drenar sozinha quando a bancada voltar.

---

## 9. Veredito

**O SIMUT sobrevive à tempestade de rede na parte que o plano mais temia, e
falha numa que ninguém tinha olhado.**

O que **passou**, sob 26 janelas de falha × carga web ininterrupta:

- **Core 1 intocado**: `fx=0`, kills de saúde 0, exposto 0, heartbeat ≤ 33 ms,
  `APP_CORE1_DEAD` ausente. A classe R1 não apareceu.
- **Zero perda de dado**: `ts+0` em quinze modos de falha distintos, `ts+42`
  quando o servidor está são. O cursor faz exatamente o que deve.
- **Nunca saiu da rede em silêncio**: ARP jamais `INCOMPLETE` (D15 não ocorreu).
- **Heap estável**: sem vazamento em ~2 h de tempestade.
- **1104 downloads** de histórico com 1 JSON inválido.

O que **falhou, e onde ficou**:

1. **`sendAll` sem feed** — a costura que o plano nomeou, e a **causa-raiz da
   maior parte dos reboots da campanha**. Corrigida pelo 4º patch de framework.
   Efeito medido no grupo HTTP completo: **5 reboots → 1**, `hp=721` de **5 → 0**,
   MTBF de ~10 min → **58 min**.
2. **Parque de resposta não-chunked** (`hp=721` em `/download` e `/api/backup`) —
   defeito separado e real, com repro determinística. **Corrigido e validado**
   (24/24 e 6/6).
3. **D14 / PBUF** — pool satura em 24/24 e falha alocações. **Não corrigido.**
   Nunca se recupera dentro de um boot.
4. **`C0=[CLI]` `hp=740`** — o resíduo que sobrou, e ele mora no **instrumento**
   desta campanha (o amostrador serial), não na operação normal do device.
5. **IRQ-off 68-78 ms** contra critério de 60 ms — R2 conhecido, não tocado.

**Erro de atribuição que a campanha cometeu e corrigiu** (§7.3c): por quase toda
a investigação eu li `C0=[WEB_POLL] hp=721` como "o parque é no envio web". Não
era. `hp` zera por requisição, então um envio de telemetria que gastasse o
orçamento do watchdog sem alimentar deixava a morte ser assinada pelo módulo
seguinte. Só instrumentar `722`/`740` desfez a ambiguidade — e a correção que
funcionou foi a da telemetria, não a do web.

O que a campanha **não** conseguiu testar: `tls_bigrecord` (o servidor honra a
extensão), e o latch do `_inHistoryHandler` (janela estreita demais).

### Observação que muda a leitura do defeito

Os reboots da Fase 1 **não seguem o modo de falha da telemetria**. Nas primeiras
9 janelas houve 2, em `error401` e em `half` — modos sem relação entre si — e os
dois com autópsia **byte a byte idêntica**:

```
C0=[WEB_POLL] C1=[DISPLAY] at up=779034ms sc3=0x80088013 hp=721 (219)
C0=[WEB_POLL] C1=[DISPLAY] at up=696842ms sc3=0x80088013 hp=721 (219)
```

E os `up=` dizem o resto: **779 s e 697 s**, ou seja ~11-13 min de uptime nos
dois casos. O gatilho é **pressão acumulada da carga web**, não o modo de falha;
o modo é incidental.

### Segundo mecanismo (hipótese) — o terminador chunked

Isto não pode ser o mesmo parque da §2, porque `/api/history_multi` **é**
chunked e `safeStreamFile` não participa. O candidato é o fim de
`_handleRequest( )`, ainda **dentro** do `handleClient( )`:

```c
void HTTPServer::_finalizeResponse() {
    if (_chunked) {
        sendContent("");        // o terminador 0\r\n\r\n
    }
}
```

Com o pool de PBUF em **23 de 24** (medido, §5), essa escrita do terminador
encontra a fila de segmentos esgotada e estaciona dentro do lwIP — depois do
último `sendContent` do handler, o que veste exatamente `hp=721`, sem alimentar
o watchdog.

O projeto já conhece esse caminho: é o que o comentário do `dropAbortedStream`
descreve. Mas aquele fix cobre a resposta chunked **abortada** (aborta o pcb
antes, e aí o terminador vira no-op). A resposta chunked **bem-sucedida sob
pressão de PBUF** ficou de fora.

**Consequência honesta para as correções da §7: a do `safeStreamFile` cobre o
`/download`, e NÃO cobre isto.** Fica como hipótese até o re-teste medir —
acrescentar correção especulativa antes de medir é o erro que este plano existe
para evitar.

---

## 5. Fase 3 — recuperação e restauração

*(a preencher)*
