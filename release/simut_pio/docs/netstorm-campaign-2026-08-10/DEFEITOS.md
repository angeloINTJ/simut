# Defeitos — campanha tempestade de rede, 2026-08-10

Um por seção, com repro e estado. Detalhe e evidência em `RELATORIO.md`.

---

## D-NS1 · `sendAll` do HTTPClient não alimenta o watchdog · **CORRIGIDO**

**Assinatura** `C0=[TEL_SEND] ctx=225` — e, indiretamente,
`C0=[WEB_POLL] hp=721` (ver a nota de atribuição abaixo).

**Onde** `StreamConstPtr::sendAll`, `HTTPClient.cpp:88` (framework). O orçamento
de 5 s limita o **laço**, não uma escrita; cada `dst->write( )` parka os 4 s de
`NET_SOCKET_TIMEOUT_MS`, então uma escrita iniciada perto do fim do orçamento
termina por volta de 9 s — além dos 8388 ms do watchdog — sem nada alimentando.

**Repro** sink em `never_read --rcvbuf 2048` e payload acima de **~15,8 KB**
(= `TCP_SND_BUF` 8×1460 + janela do peer). Como `tel batch` é limitado a 1-50
pelo firmware, engordar `t_line` para ~398 B (512 máx, só pela web) × batch 50
= 19900 B. Com payload menor **o teste não engaja** e o device parece sobreviver.

**Correção** `tools/arduino_pico_overrides/patches/httpclient_send_feed.patch`
(4º override, ligado ao `patch.sh`): `watchdog_update( )` dos dois lados da
escrita. Lacuna máxima sem feed = 1 escrita ≤ 4 s.

**Validação** grupo HTTP completo, mesma condição da Fase 1:
**5 reboots → 1**, `hp=721` de **5 → 0**, MTBF **~10 min → 58 min**,
557 downloads com 0 JSON inválido.

> **Nota de atribuição — errei isto por quase toda a campanha.** `hp` zera no
> topo de cada iteração do `handleClient( )`. Um envio de telemetria que gastasse
> o orçamento do watchdog sem alimentar deixava a morte ser assinada pelo módulo
> seguinte, e a autópsia saía `C0=[WEB_POLL] hp=721` **com o parque real na
> telemetria**. Um número de posição web não prova que o parque é web.

---

## D-NS2 · Streams não-chunked estacionam no fecho do framework · **CORRIGIDO**

**Assinatura** `C0=[WEB_POLL] hp=721 (219)`, sem tempestade nenhuma.

**Onde** `WebServerTemplate.h:131`, caso `CLIENT_MUST_STOP`, chama
`_currentClient->stop( )` **sem argumento**. `WiFiClient::flush(0)` lê 0 como
"use o default 300 ms", não "não espere", e `ClientContext::wait_until_acked`
renova o relógio a cada progresso de ACK. O `f0f8e23` blindou só o caminho
**chunked** (`dropAbortedStream` gated em `_chunkedResponse`); tudo com
`setContentLength( )` ficou descoberto. `drainOrDrop( )` do `update( )` chega
tarde por construção — roda depois do `handleClient( )`, e o framework aposenta
o cliente lá dentro.

**Repro** `scratchpad/repro_download_park.py N 20` — downloads espaçados 20 s
(> a janela de 8388 ms). O download responde 200 em 0,05 s e o watchdog mata
~8 s depois: falha a requisição **seguinte**, não a que baixou.

**Correção** `drainOrDrop( )` antes de o handler retornar, em
`safeStreamFile( )` e em `handleApiBackup( )` (que não passa pelo primeiro).

**Validação** `/download` 6/8 com 2 reboots → **24/24 com 0**;
`/api/backup` (794 KB) 2/3 com 1 reboot → **6/6 com 0**.

---

## D-NS3 · `_inHistoryHandler` vaza na cauda "extremes" · **CORRIGIDO, não reproduzido**

**Onde** `WebManager_History.cpp`, três `if (!safeSend(...)) return;` na cauda
"extremes" pulavam o desenrolar do fim da função. Custo de um único abort ali:
o latch fica true até o reboot (todo `/api/history_multi` seguinte responde
`503 "Already processing"`), `setWebBusy` fica preso (**o toque do display
continua bloqueado**) e `_handlerDeadline` não volta.

**Correção** guard RAII `HistUnwind` — destrutor não pode ser pulado por
`return`, então cobre os três de hoje e os futuros. Build ficou 8 B **menor**.

**Não reproduzido** em 6 rodadas de leitura lenta: quando o prazo vence durante
o array de dados, `aborted` fica true e a cauda inteira é pulada. O vazamento
exige que o array termine com sucesso e o cliente suma **dentro** da cauda.

---

## D-NS4 · `/api/sec_status` escreve fora do buffer · **CORRIGIDO, não acionável no rig**

**Onde** `WebManager_Auth.cpp`, `char buf[512]` com `pos += snprintf(...)`.
`snprintf` devolve o que **caberia**, então `pos` passa de 512 e
`sizeof(buf) - pos` (size_t) faz underflow para ~4 bilhões; a escrita final do
`"]}"` sai do array. O guard antigo rodava **depois** da escrita.

**Correção** espaço checado antes de cada escrita, com backout da entrada
truncada para o JSON seguir parseável com menos slots.

**Não acionável** exige os 8 `LOGIN_STATE_SLOTS` cheios com IPs longos, ou seja
8 IPs de origem distintos.

---

## D-NS5 · PBUF satura sob concorrência (D14) · **CORRIGIDO**

**Re-caracterizado: NÃO era vazamento.** O D14 estava registrado como
"vazamento que não se recupera, com uma segunda fonte não localizada". Isso era
verdade na era de 12 entradas; o `clientcontext_rx_leak.patch` fechou aquilo, e
o que restava era outra coisa.

**O que a campanha mediu e o que faltava.** Só o *pico* (marca d'água, que nunca
desce) e as *falhas* foram registrados. O número que separa vazamento de pressão
é o **"em uso" depois que a carga para** — e ele nunca tinha sido medido.

**Medição (`scratchpad/pbuf_probe.py`, `pbuf_conc.py`)**: carga sequencial não
retém nada (history, download, upload: **+0** pbufs em 25 requisições cada).
Varrendo concorrência, o "em uso" volta ao basal em **todos** os níveis —
inclusive no que esgotou o pool e falhou 79 alocações. **Nada vaza.**

**Causa real: a janela anunciada não tem lastro.** Cada envelope custa ~1514 B,
e `TCP_WND = 8 × MSS` são **7,7 envelopes por conexão**. Seis conexões com a
janela cheia pedem **46 contra um pool de 24** — o pool está prometido em dobro.

| clientes | pico do pool (8×MSS) | falhas |
|---|---|---|
| 1 | 2 | 0 |
| 2 | 3 | 0 |
| 4 | 13 | 0 |
| 5 | **24/24** | **+45** |
| 6 | **24/24** | **+79** |

**Correção: `TCP_WND` 8×MSS → 4×MSS** (`patched_headers/lwipopts.h`). Custa
nada porque o device não usa a janela: uploads rodam a 26 KB/s, limitados por
escrita em flash, e num RTT de ~5 ms mesmo 4×MSS permitiria ~1,1 MB/s. Downloads
são governados pelo `TCP_SND_BUF`, não por isto.

| | antes | depois |
|---|---|---|
| falhas de alocação (k=5 / k=6) | 45 / 79 | **0 / 0** |
| pico do pool em k=6 | 24/24 (esgotado) | 18/24 |
| requisições OK em k=6 | 98 | 166 |
| download | 221 KB/s | 216 KB/s |
| upload | 26 KB/s | 25 KB/s |

Sem reboot em nenhum nível, antes ou depois — a exaustão sempre foi degradação
graciosa, nunca morte.

> Aumentar o pool era a saída errada: 24 envelopes já são 35,5 KB de BSS, e
> dobrar custaria mais que o heap livre inteiro.

---

## D-NS6 · `C0=[CLI] hp=740` · **ABERTO, mora no instrumento**

1 ocorrência em 58 min de tempestade. `hp=740` diz que o `handleClient( )`
retornou limpo; `ctx=209` é a **CLI serial** — o amostrador desta campanha
rodando `show metrics`/`show net status` a cada 30 s, provavelmente a leitura
de RSSI viva. É o velho suspeito `C0=[WIFI]`. Não afeta operação normal.

---

## D-NS7 · IRQ-off 68-78 ms contra critério de 60 ms · **ABERTO (R2 conhecido)**

Medido em toda a campanha. Não tocado aqui.

---

## Não testados, e por quê

- **`tls_bigrecord`** — o OpenSSL **honra** a extensão `max_fragment_length`
  (RFC 6066) que o BearSSL negocia por causa do `setBufferSizes(4096,512)`, e
  fragmenta em 4096. O registro grande nunca existe. Testar um servidor que a
  ignora exige uma pilha TLS que se possa mandar ignorá-la.
- **D15 (queda silenciosa)** — não ocorreu: ARP jamais `INCOMPLETE`.
