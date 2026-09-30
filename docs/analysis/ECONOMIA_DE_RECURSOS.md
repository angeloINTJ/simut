# Economia por recurso — quanto cada chave devolve quando desligada

**Estado:** Living · **Levantado em:** 2026-09-26 contra `main` `dc7c7c5`,
medindo os seis ambientes de firmware com `tools/measure_savings.py`; o rastreio
do que ainda não tem chave, em 2026-09-30 contra `main` `7b948e7` (v2.8.0) ·
**Pergunta:** ao desligar um driver ou qualquer funcionalidade, quanto de flash
e de RAM a imagem realmente devolve — e o que hoje **não** devolve o que deveria?

## Por que este documento existe

O modelo de recursos ([MODELO_DE_RECURSOS.md](MODELO_DE_RECURSOS.md)) promete
builds com recursos chaveáveis e **impacto previsível**. Previsível só se for
medido. Este levantamento mede, recurso a recurso, a diferença entre a imagem
com o recurso ligado e a mesma imagem com **só aquele recurso** desligado — e
`tools/feature_savings.json` guarda o piso dessa economia, que o portão
`measure_savings.py --check` impede de regredir. Se alguém acrescenta código
que, preso a um recurso, deixa de sumir quando o recurso é desligado, a economia
cai e o portão reprova: o recurso **vazou** para o caminho sempre-ligado.

A medida é o `used` que o próprio PlatformIO reporta (flash = `.text`+`.rodata`
do ELF; RAM = `.data`+`.bss` estáticos). O que importa aqui é a **diferença**
entre base e variante, não o número absoluto.

## O que cada recurso devolve (2026-09-26)

Ordenado pela economia de flash. `base` é um perfil onde o recurso está ligado;
a variante é o mesmo perfil com só ele desligado, derivada pelo modelo
(`tools/features.toml` → `gen_features.compose`), para que um recurso com
`off_exclude` (bluetooth, som) realmente **tire o `.cpp`** do build.

| Recurso | Base | Flash devolvido | RAM estática devolvida |
|---|---|---:|---:|
| `bluetooth` | `pico_w_alpha` | 140 132 B | 19 348 B |
| `tel_tls` (cliente TLS da telemetria, 2026-09-30) | `pico_w_air` | 67 936 B | 16 B |
| `cli_full` | `pico_w_test` | 48 232 B | 0 B |
| `web_https` (TLS) | `pico_w_release` | 24 520 B | 24 B |
| `web_history` (página de histórico, 2026-09-30) | `pico_w_air` | 21 768 B | 0 B |
| `tft_graph` (gráfico e calendário do painel, 2026-09-30) | `pico_w_release` | 20 312 B | 2 236 B (heap: 5 872 B) |
| `sensor_bme280` | `pico_w_release` | 17 724 B | 220 B |
| `mdns` | `pico_w_release` | 16 576 B | 168 B |
| `web_export_api` (`/api/history_multi` e `.simx`, 2026-09-30) | `pico_w_release` | 12 344 B | 2 052 B |
| `tel_mqtt` (MQTT e Home Assistant, 2026-09-30) | `pico_w_air` | 7 704 B | 0 B |
| `sensor_ds18b20` | `pico_w_release` | 5 432 B | 64 B |
| `sound_buzzer` | `pico_w_release` | 5 008 B | 0 B |
| `web_metrics` (`/metrics`, 2026-09-30) | `pico_w_release` | 4 912 B | 0 B |
| `sensor_dht22` | `pico_w_release` | 3 400 B | 0 B |
| `concurrency_asserts` | `pico_w_asserts` | 2 176 B | 0 B |
| `syslog` (2026-09-30) | `pico_w_release` | 1 768 B | 0 B (heap: 2 135 B) |
| `license_stub` | `pico_w_test` | −1 728 B | 0 B |

> **Cada número vale para a sua base, não para todo produto.** A matriz do
> configurador (`tools/feature_costs.json`, escrita por
> `measure_savings.py --matrix` em 26/09) mede cada chave a partir de cada
> produto publicado, e o valor muda de base para base: o buzzer devolve 5.016 B
> no SIMUT e 8.224 B no Alpha; o `web_https` custa 24.528 B no SIMUT, 21.776 B
> no Alpha e 25.768 B no Air; o Bluetooth, que devolve 140.196 B no Alpha, nem
> cabe no SIMUT (estoura o slot em 105.964 B). Este documento guarda o piso de
> economia por recurso; o custo por produto mora lá.

Os três achados que a medição trouxe:

1. **A RAM estática quase não se move ao desligar.** `bluetooth` é a exceção
   (−19 348 B: a pilha BT e o `liblwip-bt`). Os sensores devolvem 0–220 B de
   RAM porque suas instâncias vivem no heap; o que sai é código (flash), não
   `.bss`. `web_https` devolve 24 520 B de flash mas só 24 B de RAM — os buffers
   TLS são de heap (`setBufferSizes` em runtime), invisíveis ao `used`. Ou seja:
   **a alavanca de RAM ao desligar é o bluetooth; as demais são alavancas de
   flash.** Quem precisa de RAM não a ganha desligando um sensor.

2. **`license_stub` é "ligar para economizar".** O stub *remove* o texto da
   licença; desligá-lo o *adiciona* (+1 728 B). Fica fora do piso "desligar
   economiza" — não é um recurso que se desliga para poupar. E só a tela de
   Licença do mostrador touch lê esse texto: no Alpha e no Air a imagem sai
   idêntica, byte a byte, com a chave ligada ou desligada (matriz, 26/09).

3. **O buzzer ganhou a própria chave (2026-09-26).** Antes estava preso ao
   `SIMUT_AIR`: `SoundManager.h` só virava no-op lá, então excluir
   `SoundManager.cpp` numa build TFT/normal não linkava. O macro
   `SIMUT_SOUND_BUZZER` (default 1, no-op também sob `!SIMUT_SOUND_BUZZER`)
   destravou: `sound_buzzer` agora desliga em qualquer perfil (5 008 B no release;
   por produto, veja a nota acima da lista), e a troca é
   **byte-idêntica** nos seis firmwares (o caminho ligado não mudou). O `air`
   **continua acoplado**, por outro motivo: o perfil `air` usa `display=nenhum`,
   que exclui `DisplayManager.cpp`, e o caminho de boot não-air (`SIMUT_AIR=0`)
   referencia a `DisplayManager` cheia — o link quebra. `SIMUT_AIR` é uma variante
   de build (headless + ciclo dormente), não uma chave isolada; medir seu custo
   pediria desemaranhar o mostrador. Fica fora do piso.

Os temas (`SIMUT_THEMES_*`) não entram: vêm **comentados** (desligados) em todo
perfil, custo zero. São "ligar para gastar", não "desligar para poupar".

### O cliente TLS da telemetria (2026-09-30)

Até a v2.8.0, HTTPS e MQTTS para o coletor entravam em toda imagem, sem chave.
Numa imagem sem o servidor HTTPS — o Alpha e o Air — o `WiFiClientSecure` da
telemetria é o **único** que liga o motor TLS do BearSSL, e a chave
`SIMUT_TEL_TLS` devolve quase tudo dele:

| Produto | Flash `used` | `.bin` |
|---|---:|---:|
| SIMUT Air | 1 004 352 → 936 464 B (−67 888) | −69 632 B |
| SIMUT Alpha | 964 508 → 892 420 B (−72 088) | −69 632 B |
| SIMUT | 999 356 → 998 196 B (−1 160) | −1 160 B |

No Air e no Alpha sem a chave sobram 14 símbolos `br_` — SHA-256 e HMAC, da
senha e da chave do AP —, nenhum `br_ssl_*` nem `br_x509_*`. No SIMUT o servidor
HTTPS segura o `WiFiClientSecure` inteiro, cliente incluso: a classe é uma só, e
o linker não separa o lado cliente do lado servidor.

Sem a chave, a telemetria continua por HTTP e MQTT sem criptografia, e **uma
configuração que pede criptografia nunca vai em claro** — a chave da API e a
senha do MQTT viajam no que seria enviado. O boot registra uma vez
`SYS_TEL_FAIL` com `ctx=-200` e deixa o MQTT sem inicializar; os dois envios
por HTTP (dados e alarmes) recusam com o mesmo `ctx` antes de abrir socket; o
`commit_all` recusa `t_sec=1` (o aviso da página nomeia o campo) e aceita
`t_sec=0`, para que uma config restaurada de uma imagem com TLS possa ser
desfeita; e o `tel crypto on` da CLI diz que a imagem não tem cliente TLS.

Com a chave ligada, as seis imagens saem **byte a byte idênticas** à `main` de
antes dela. É por isso que o TLS ficou cercado dentro dos transportes, onde
mora, e não foi movido para uma unidade de tradução própria: movê-lo mudaria
os caminhos HTTPS e MQTTS que levaram semanas de bancada para assentar.

**A página de telemetria segue a chave (2026-09-30).** Sem o cliente TLS, ela
perde a chave de TLS e o aviso de certificado, e ganha — só nessa imagem,
por um bloco `@IF !tel_tls` — um aviso para a configuração guardada que ainda
pede criptografia: nada é enviado até a página ser salva, e ela já deixa
`t_sec=0` pendente para isso. O SIMUT sem a chave encolhe mais 136 B com o
corte. O Alpha e o Air, 0 B: a imagem com Bluetooth anda em degraus de 4 KB, e
o corte cabe no degrau (o Air sem TLS mede os mesmos 936 272 B antes e depois
dele). O piso do Air, medido de novo, é 67 936 B — a base dele mudou no #200.

### O MQTT e o Home Assistant (2026-09-30)

O segundo item da lista abaixo, com o mesmo desenho do TLS: `SIMUT_TEL_MQTT`
cerca, onde moram, o cliente MQTT do `TelemetryManager` (o `PubSubClient` e o
socket que ele usa), a inicialização no `begin( )`, a rede de segurança do
Discovery no `update( )` e os dois blocos de funções do MQTT — o dos dados e o
da linha de alarmes com o tópico de ack —, cada um com um `#else` que devolve
as portas que o resto do gerente ainda chama.

| Produto | Flash `used` | `.bin` |
|---|---:|---:|
| SIMUT | 999 356 → 990 636 B (−8 720) | −8 720 B |
| SIMUT Alpha | 964 508 → 952 708 B (−11 800) | −12 288 B |
| SIMUT Air | 1 004 352 → 996 648 B (−7 704) | −8 192 B |

Nenhum símbolo do `PubSubClient` sobra. No heap, o objeto de telemetria cai de
2 272 para 2 168 B, e somem os 256 B que o construtor do `PubSubClient` pede
ao `malloc` em toda imagem. As duas chaves juntas — sem TLS e sem MQTT — deixam
o Air em 928 880 B (−75 472) e o Alpha em 884 836 B (−79 672).

Sem a chave, **uma configuração que nomeia MQTT é recusada**, nunca enviada de
outro jeito: todo outro ramo da escolha de transporte é HTTP, e um MQTT salvo
faria POST do lote na porta do broker. O boot registra `SYS_TEL_FAIL` com
`ctx=-201` uma vez; os envios de dados e de alarmes recusam com o mesmo `ctx`
e contam a falha; o `commit_all` aceita `t_transport=0` e recusa `1`, para que
uma config restaurada de uma imagem com MQTT volte a HTTP. Com a chave ligada,
as seis imagens saem byte a byte idênticas.

**A página de telemetria segue a chave (2026-09-30).** Sem o MQTT, somem o
seletor de transporte e os campos do MQTT — tópico, cliente, usuário, senha,
QoS, keep-alive, retain e o Discovery —, ficam os do HTTP, e um aviso só dessa
imagem aparece quando a configuração guardada pede MQTT, com `t_transport=0` já
pendente. O SIMUT encolhe mais 448 B; o Alpha e o Air, 0 B, pelo mesmo degrau
de 4 KB.

### A interface web, peça a peça (2026-09-30)

Quatro itens do rastreio abaixo, cada um cercado **onde a rota é registrada**
(`WebManager_Core.cpp`): sem o registro, o linker larga o handler junto.

- `web_history` — a página `/history` (gráficos, calendário, CSV, visor de
  eventos) e as duas rotas que só ela chama, `/api/logcodes` e
  `/api/clear_logs`. A página sai da imagem por um bloco `@IF web_history` do
  `WebUI.h`, e o link do menu por outro. O aparelho grava o histórico do mesmo
  jeito; `/api/logs`, `/api/history/open` e `/api/history_days` ficam, porque as
  ferramentas de bancada e o guia de integração as leem.
- `web_export_api` — `/api/history_multi` e as duas exportações `.simx`.
  Nenhuma página as chama. O buffer estático de 2 048 B do `history_multi` sai
  com a rota: é a primeira chave, depois do Bluetooth, que devolve RAM estática
  de verdade.
- `web_metrics` — `GET /metrics`; o `WebManager_Metrics.cpp` sai da build.
- `syslog` — o `SyslogManager.h` vira uma classe vazia com a mesma interface
  (o objeto de 2 136 B do heap cai para 1 B), o `.cpp` sai da build e a seção
  Syslog remoto sai da página de configuração por um bloco `@IF syslog`. Os
  campos `slog_*` ficam na config: uma build não é um esquema.

Por produto (`measure_savings.py --matrix`, flash `used`):

| Desligada | SIMUT | SIMUT Alpha | SIMUT Air |
|---|---:|---:|---:|
| `web_history` | −23 896 B | −25 872 B | −21 768 B |
| `web_export_api` | −12 344 B (−2 052 B de RAM) | −15 408 B (−2 052 B de RAM) | −10 952 B (−2 048 B de RAM) |
| `web_metrics` | −4 912 B | −7 464 B | −3 368 B |
| `syslog` | −1 768 B | −1 256 B | −1 248 B |

As quatro juntas, compiladas de verdade no Air: 1 004 352 → 962 912 B
(−41 440); a soma das quatro erra essa build por 4 104 B.

O portão de rotas por recurso do `build_webui_gz.py` passou a ler também rotas
cercadas por `#if <MACRO>` no C++, e confere que nenhuma página chama as da
página de histórico ou da exportação fora do bloco delas.

**O carimbo dos assets.** O gerador carimba `?v=<tag>` nas URLs de `/style.css`
e `/lang.js` de toda página, servidos com cache de 7 dias. O tag era o hash do
`WebUI.h` cru: qualquer edição nele, até um marcador `@IF` num comentário,
recarimbava toda página e mudava toda imagem, com os dois assets intactos.
Agora é o hash **desses dois**, como servidos. Com o tag fixado no valor que a
`main` gerava, as seis imagens deste PR saíram byte a byte idênticas à `main`:
a única diferença delas é o carimbo, e daqui em diante uma edição só de página
não muda mais a imagem.

### O gráfico do painel (2026-09-30)

`SIMUT_TFT_GRAPH` segue o TFT no `simut_config.h`: o Alpha e o Air são 0 sem
dizer, e `-DSIMUT_TFT_GRAPH=1` sem o TFT para num `#error`. Isso trouxe o
primeiro ganho, que não dependia de chave nenhuma: o `_graphData` (5 872 B, o
maior membro do `DisplayManager`) vivia no objeto do Alpha e do Air, no heap,
para um gráfico que nenhuma das duas desenha — `sizeof(DisplayManager)` caiu de
8 552 para 2 676 B no Alpha e de 8 192 para 2 320 B no Air. O Air é quem sente:
o lote da telemetria dele é cortado pelo heap antes do `t_bat`.

Numa build TFT, a chave desligada tira as três unidades do gráfico, o botão da
faixa de min/max (um toque ali cai no liga/desliga do min/max), as zonas de
toque dos dois cartões que o abrem, os ramos de toque e o despacho de desenho
das telas de gráfico, detalhe e calendário, e o `_graphData` também. O
`screen gra` da CLI responde `?screen`, como o `screen pin` sem o PIN (o Air,
que também não tem gráfico, respondia OK sem tela nenhuma). O SIMUT sem o
gráfico: 999 356 → 979 044 B de flash (−20 312) e 2 236 B de RAM estática — os
cinco vetores de coordenadas da curva (400 B cada, `static` no
`drawGraphScreen`) e as linhas da tela de detalhe —, mais os 5 872 B de heap.

É também o que abre espaço para a CLI completa no SIMUT: sozinha, ela estoura o
flash dele por 15 060 B (a inversão `cli_full` do SIMUT na matriz); sem o
gráfico, cabe — 1 027 108 B, compilado uma vez em 2026-09-30. A soma das
inversões não diz isso, porque uma das parcelas não linka: por isso a
combinação não entrou nas conferências do `measure_savings.py`, que medem o
erro da soma.

As três cercas que o laço de eventos já tinha em volta do
`renderGraphOptimized` passaram a ser do gráfico, e o sinal de "desenhando o
gráfico" agora é baixado fora delas: com a cerca em volta dos dois, um evento
perdido deixaria o sinal de pé e pararia todos os eventos da interface. Com a
chave ligada, as quatro imagens com painel saem byte a byte idênticas.

## O que ainda não tem chave — o rastreio de 2026-09-30

A tabela acima mede o que já se desliga. Esta seção olha o contrário: **o que
mais gasta flash e RAM e ainda não tem chave**, para decidir a ordem das
próximas. Os números são do mapa do linker da imagem que o build fez
(`tools/flash_compose.py --env <env> --objects`), agrupados por símbolo quando
um objeto mistura recursos, e do `sizeof( )` dos gerentes, que vivem no heap e
não aparecem em mapa nenhum. São **estimativas do que sairia**: o número medido
só existe quando a chave existe, e aí ele vem para a tabela de cima e para o
piso do `feature_savings.json`.

### Flash

| Candidato | O que sairia | SIMUT | Air | Observação |
|---|---|---:|---:|---|
| Identidade no painel | `DisplayManager_Users`: teclado do PIN, sessão, menu de alarme por conta, janela de manutenção, contas | ~15 KB | — | só o TFT. Só a administração de contas são ~3 KB; o resto é o PIN do painel (`SIMUT_PANEL_PIN`), e desligá-lo pede decidir o que um painel sem PIN deixa fazer — aberto ou só leitura (R-D5, decisão de produto) |
| Curvas de calibração | `/api/calib`, o motor das curvas | ~8 KB | ~8 KB | não há offset separado: o offset simples **é** uma curva de 1 ponto, e o `calib.csv` também guarda a identidade dos DS18B20. Sem a chave, a leitura sai crua |
| Backup e restauração | validar e aplicar o `.bkp` | ~3,5 KB | ~3,5 KB | o CRC32 do `backup.cpp` fica (o estágio da OTA usa), e a atualização pela página chama `/api/backup` antes de gravar |
| Busca de redes Wi-Fi | handler, `pollScan`, o bloco da NET | ~2,8 KB | ~2,8 KB | o `native_network` testa a busca |
| Pacotes de idioma | `DisplayManager_LangParser` | ~2,6 KB | ~2,1 KB | uma imagem só em inglês precisa forçar EN, ou a CLI bilíngue responde em português |

### RAM que existe em toda imagem

| O quê | Onde | Bytes | Tipo | Observação |
|---|---|---:|---|---|
| pools do lwIP | `memp.c`, `mem.c` | 39 670 + 16 415 | `.bss` | núcleo; dimensionados pelo `lwipopts.h` para seis conexões a 4×MSS |
| buffers da OTA | `ota/applier` 8 620, `ota/restore` 4 500, `ota/validation` 4 128 | 17 248 | `.bss` | usados uma vez por atualização ou restauração |
| objeto `StorageManager` | `AppManager` | 11 684 | heap | núcleo (histórico) |
| objeto `WebManager` | `AppManager` | 10 652 | heap | 8 KiB de buffer de upload |
| objeto `DisplayManager` | `AppManager` | 8 576 (Alpha 2 676, Air 2 320) | heap | até 2026-09-30 o Alpha e o Air carregavam o `_graphData` do gráfico (5 872 B), que nenhuma das duas desenha: 8 552 e 8 192 B. Sobram ~2 KB de membros só-TFT nelas |
| pilha do Core 1 | `DisplayManager.cpp` | 8 192 | `.bss` | segue o TFT |
| `/api/calib` | `WebManager_Calib` | 3 640 | `.bss` | segue as curvas de calibração |
| pacote de idioma | `DisplayManager_LangParser` | 2 785 | `.bss` | segue os pacotes |
| objeto `TelemetryManager` | `AppManager` | 2 272 | heap | a fila de alarmes de 2 KiB existe com a linha desligada |
| coordenadas do gráfico | `DisplayManager_Graph` | 2 260 | `.bss` | chave `tft_graph` (2026-09-30): só nas imagens com painel; a variante sem o gráfico mede −2 236 B de RAM usada |
| objeto `SyslogManager` | `AppManager` | 2 136 | heap | chave `syslog` (2026-09-30): desligada, 1 B |
| `/api/history_multi` | `WebManager_History` | 2 048 | `.bss` | chave `web_export_api` (2026-09-30): sai com a rota |

O heap acima é o `sizeof( )` de cada gerente, lido por uma unidade de
compilação de sondagem com os flags de cada ambiente (o `build_type = release`
não gera DWARF, então o `gdb` não os vê). A RAM estática soma o que o
PlatformIO chama de "RAM used"; o `--objects` fecha nela a 12–17 B nos três
produtos (alinhamento que o mapa não atribui a objeto nenhum).

## Como rodar

```bash
python3 tools/measure_savings.py              # a tabela acima
python3 tools/measure_savings.py --only air   # só um recurso (rápido)
python3 tools/measure_savings.py --update      # grava o piso no ledger
python3 tools/measure_savings.py --check       # reprova se a economia caiu
python3 tools/flash_compose.py --env pico_w_air --objects   # o rastreio: flash e RAM por objeto
```

O `--check` reconstrói base e variante de cada recurso do ledger e falha se a
economia de flash ficou **abaixo** do piso (a RAM entra com tolerância de 64 B,
ruído de alinhamento do linker). É um ratchet, como o orçamento de flash e a
marca d'água do sprawl: a economia só sobe; abaixá-la exige `--update` e
justificativa no commit.

**Custo em CI.** O portão constrói ~13 imagens (base + variante por recurso), o
que não cabe no job `gates` (livre de framework). Pertence a um job próprio,
agendado (noturno) ou disparado à mão — ver `.github/workflows/savings.yml`.
Um `--check --only <recurso>` é barato e serve para conferir um recurso após
mexer no que ele guarda.
