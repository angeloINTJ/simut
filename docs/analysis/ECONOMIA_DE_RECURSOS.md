# Economia por recurso — quanto cada chave devolve quando desligada

**Estado:** Living · **Levantado em:** 2026-09-26 contra `main` `dc7c7c5`,
medindo os seis ambientes de firmware com `tools/measure_savings.py`; o rastreio
do que ainda não tem chave, em 2026-09-30 contra `main` `7b948e7` (v2.8.0); o
piso, medido de novo sem o enchimento do banco do BTstack, em 2026-10-22 contra
`main` `25e169df`, e sem todo enchimento do linker, medido em 2026-10-29 e gravado em
2026-10-30 contra `main` `e7768221`; o do `tft_graph`, que desce 68 B com a UI como
driver, medido e gravado em 2026-10-30 contra o ramo `feat/ui-drivers-s11` (`7d550359`) ·
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

A medida é o `used` que o próprio PlatformIO reporta (flash = `.boot2`+`.text`+
`.rodata`+`.data` do ELF; RAM = `.data`+`.bss` estáticos). O que importa aqui é a **diferença**
entre base e variante, não o número absoluto. Desde 2026-10-30 o piso compara o
flash **sem o enchimento do linker**, todo `*fill*` de `.boot2`, `.text`,
`.rodata` e `.data` lido do mapa, e deixa passar uma queda de 8 B (`FLASH_TOL`): contado, o
enchimento mexia a economia 8 a 16 B por commit sem símbolo nenhum mudar de
lugar ([O enchimento de alinhamento](#o-enchimento-de-alinhamento-e-os-pisos-de-2026-10-30)).
O do banco do BTstack, que desde 2026-10-22 já ficava de fora, é só o maior
deles: mexia a economia de toda chave do Alpha e do Air a cada byte de `.rodata`
da imagem ([O enchimento do banco do BTstack](#o-enchimento-do-banco-do-btstack-e-os-pisos-de-2026-10-22)).

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

### O enchimento do banco do BTstack e os pisos de 2026-10-22

O job noturno (`savings.yml`) reprovou todas as noites de 2026-10-02 a
2026-10-22, 21 seguidas, sem que PR nenhum visse: ele não roda em PR. Na
última, cinco recursos estavam abaixo do piso: `tel_tls` (−10 096 B),
`bluetooth` (−2 776 B), `cli_full` (−80 B de flash e −2 048 B de RAM),
`web_export_api` (−40 B) e `sound_buzzer` (−24 B). Nenhum era vazamento: o do
`bluetooth` era só o instrumento, e os outros, mudanças de verdade que ninguém
registrou no piso, a do `tel_tls` com o instrumento por cima.

**O instrumento.** O framework guarda o banco do BTstack em 8 KB de `.rodata`
alinhados a um setor de 4 KB (`__bluetooth_tlv`,
`cores/rp2040/sdkoverride/btstack_flash_bank.cpp`), em toda imagem com
Bluetooth: entre as bases, o Alpha e o Air. O banco é a última coisa da
`.rodata`, e o linker enche até 4 095 B na frente dele; o `used` conta esse
enchimento. Numa base com o banco, um byte a mais de `.rodata` em qualquer
lugar da imagem some no enchimento, e na variante sem Bluetooth ele conta: a
economia do `bluetooth` caía a cada mudança e subiria 4 096 B de uma vez
quando o enchimento acabasse. No Air os dois lados têm o banco, e a economia
pulava 4 096 B para um lado ou para o outro. É o mesmo degrau que as notas de
2026-09-30 acima viram ("o corte cabe no degrau"). Medido aqui, com o mesmo
toolchain e o mesmo framework nos quatro commits. O framework daqui já tem os
patches de outubro: em `2721eb2e`, o único dos três primeiros que o job mediu
(em 01/10), o `used` do `bluetooth` sai 8 B longe do número dele, e o do
`tel_tls`, igual:

| | `2721eb2e` (01/10) | `2690c0a2` (#220) | `6109ddb3` (#221) | `25e169df` (22/10) |
|---|---:|---:|---:|---:|
| `bluetooth`, `used` | 140 268 | 139 572 | 138 892 | 137 356 |
| `bluetooth`, sem o enchimento | 136 792 | 136 816 | 136 808 | 136 808 |
| enchimento na base Alpha | 3 476 | 2 756 | 2 084 | 548 |
| `tel_tls`, `used` | 67 936 | 72 032 | 57 880 | 57 840 |
| `tel_tls`, sem o enchimento | 69 968 | 69 976 | 58 120 | 58 080 |
| enchimento na base Air / na variante | 444 / 2 476 | 4 084 / 2 028 | 3 412 / 3 652 | 2 300 / 2 540 |

Os símbolos que a chave do Bluetooth tira somam 132 474 B em `2721eb2e` e
132 482 B em `25e169df`: o código dela mudou 8 B, e o `used` acusava −2 912 B.
Os que a do `tel_tls` tira são os mesmos 69 879 B em `2721eb2e` e em
`2690c0a2`, e o `used` acusava +4 096 B.

**O conserto.** (Em 2026-10-30 isto passou de "o enchimento do banco" a "todo
enchimento do linker": [adiante](#o-enchimento-de-alinhamento-e-os-pisos-de-2026-10-30).
O parágrafo diz o que valia em 2026-10-22.) O piso (`--check`, `--update`) compara o `used` menos o
`*fill*` que termina no endereço do banco, lido do mapa que a build pede ao
linker (`-Wl,-Map`, por `PLATFORMIO_BUILD_FLAGS`). Com o mapa, as cinco bases
saem byte a byte as mesmas imagens: sha256 do `firmware.bin` igual com e sem o
flag (`pico_w_test` `b4074dd0`, `pico_w_release` `e971dfe8`, `pico_w_asserts`
`abd422b5`, `pico_w_alpha` `93001b45`, `pico_w_air` `e5935de2`). Uma build que
não deixa o mapa, que roda sem zopfli ou cujo mapa o leitor não entende é
recusada, não medida; também a cujo mapa tem o objeto do banco
(`btstack_flash_bank.cpp.o`) sem o banco com o nome dele, que é o que um
framework que o renomeasse deixaria. A `--matrix`
segue com o `used` inteiro, porque ali o que importa é caber no slot, e o
enchimento ocupa o slot.

**As quedas de verdade**, cada uma com a causa, medidas sem o enchimento:

- `tel_tls`, −11 856 B, no #221 (`016dacab`, o stage e o apply só aceitam
  imagem assinada). A imagem confere a assinatura ECDSA P-256 de toda
  atualização com o verificador do BearSSL, e ele passou a ser sempre-ligado:
  52 símbolos, 12 884 B, que a chave tirava em `2721eb2e` e não tira em
  `11d38223` (`mul_f256`, `square_f256`, `p256_*`, `br_ecdsa_i31_vrfy_raw`,
  `br_i31_*`). Desligar o TLS da telemetria não os tira mais.
- `cli_full`, RAM 0 → −2 048 B, no #223 (`3335d115`). A tela de Licença do
  painel passou a ler o `@WEBDICT` por um pedaço na pilha, sem o `lazyRead( )`.
  O `help` da CLI completa nunca lê o pacote, então a imagem de teste perdeu o
  `lazyRead( )` e o buffer estático de 2 048 B dele (`_lazyReadBuf`,
  `DisplayManager_LangParser.cpp`). A imagem sem a CLI completa guarda os dois
  para o `@HELP` do console de emergência. Desligar a CLI completa custa agora
  2 048 B de RAM, e esses 2 048 B são só do `help` de emergência, em toda imagem
  que o tem.
- `cli_full`, flash −80 B. O #214 (`ab1e5b3b`) pôs o `set time` em toda
  imagem (`case CMD_SET_TIME: /* every image */`), para o aparelho sem rede que
  pede a hora: `cmdHandleSetTime( )` e `parse_3ints( )`, 412 B, saíram do que a
  chave tira. O que ela tira cresceu em volta (`executeCommand( )` +396 B,
  `cmdHandleSensorField( )` +124 B, `renderMetrics( )` +88 B), e a `.rodata`
  dela encolheu 128 B.
- `sound_buzzer`, −24 B. O #234 (`f24e1b71`) moveu o corpo de
  `SoundManager::fillConfig( )` (218 → 22 B) para `soundStateToConfig( )`
  (186 B), que o ensaio a seco do `commit_all` também usa
  (`SoundManager::stateToConfig( )`), e o que a chave tira do `commit_all` saiu
  12 B menor.
- `web_export_api`, −40 B. O compilador passou a emitir fora de linha o
  caminho de crescimento do `std::vector<String>` (`_M_check_len( )`, 60 B) no
  `/api/history_days`, que toda imagem tem; em `2721eb2e` a variante sem a
  exportação não tinha esse caminho fora de linha.
- `bluetooth`: só o enchimento (acima).

**Os pisos gravados em 2026-10-22** (`--update` sobre `25e169df` com a medida
nova; os que subiram também sobem o piso, que só aceita subir):

| Recurso | Base | Flash, piso anterior | Flash, piso novo | RAM, anterior | RAM, nova | Causa |
|---|---|---:|---:|---:|---:|---|
| `bluetooth` | `pico_w_alpha` | 140 132 | 136 808 | 19 348 | 19 348 | o enchimento (548 B na base) fica fora |
| `tel_tls` | `pico_w_air` | 67 936 | 58 080 | 16 | 16 | #221; o enchimento fica fora |
| `cli_full` | `pico_w_test` | 48 232 | 48 152 | 0 | −2 048 | #214 (flash) e #223 (RAM) |
| `web_https` | `pico_w_release` | 24 520 | 24 568 | 24 | 16 | RAM dentro da tolerância de 64 B |
| `web_history` | `pico_w_air` | 21 768 | 23 872 | 0 | 0 | o enchimento fica fora |
| `tft_graph` | `pico_w_release` | 20 312 | 20 432 | 2 236 | 2 232 | RAM dentro da tolerância de 64 B |
| `sensor_bme280` | `pico_w_release` | 17 724 | 17 804 | 220 | 220 |  |
| `mdns` | `pico_w_release` | 16 568 | 16 632 | 168 | 168 |  |
| `web_export_api` | `pico_w_release` | 12 336 | 12 296 | 2 052 | 2 052 | o `vector<String>` (acima) |
| `tel_mqtt` | `pico_w_air` | 7 704 | 9 344 | 0 | 0 | o enchimento fica fora |
| `sensor_ds18b20` | `pico_w_release` | 5 432 | 5 520 | 64 | 64 |  |
| `sound_buzzer` | `pico_w_release` | 5 008 | 4 984 | 0 | 0 | #234 |
| `web_metrics` | `pico_w_release` | 4 904 | 4 968 | 0 | 0 |  |
| `sensor_dht22` | `pico_w_release` | 3 400 | 3 456 | 0 | 0 |  |
| `concurrency_asserts` | `pico_w_asserts` | 2 176 | 2 240 | 0 | 0 |  |
| `syslog` | `pico_w_release` | 1 760 | 1 768 | 0 | 0 |  |

### O enchimento de alinhamento e os pisos de 2026-10-30

O job noturno voltou a reprovar, e de novo sem vazamento nenhum. Em 28/10
(`main` `567140f9`, depois do #294) caiu `concurrency_asserts`, 2 232 B contra
2 240 B. Em 29/10 (`229d5d59`, depois do #295, #297, #298, #299 e #300), nove
recursos (issue #296): `web_https` 24 552 < 24 568, `concurrency_asserts`
2 232 < 2 240, `sensor_ds18b20` 5 512 < 5 520, `sensor_dht22` 3 448 < 3 456,
`sensor_bme280` 17 788 < 17 804, `tel_mqtt` 9 336 < 9 344, `web_export_api`
12 288 < 12 296, `web_metrics` 4 960 < 4 968 e `tft_graph` 20 424 < 20 432. As
quedas são de 8 e 16 B.

**O instrumento.** A mesma conta do job, nesta máquina (PlatformIO 6.1.19 e
zopfli 0.4.1, os do atalho que a máquina usa desde 24/10; o CI usa 6.2.0 e
0.4.3), reproduz o CI: 48 de 48 pares (recurso, commit) iguais na economia de
flash, na de RAM e no enchimento do banco onde o log o imprime, nos três
commits dos logs (`6adf1023` de 27/10, `567140f9` e `229d5d59`). Dois commits
que não mudam a imagem (o #297, que só toca o manifesto da imagem de teste, e
`e7768221`, só documentação) saem com o sha256 do `.bin` igual ao do anterior. O
`explain.py` do kit de medição de 22/10 (lê os símbolos pelo `nm` e não pelo mapa;
o kit fica fora do repositório e o `--check` não precisa dele) dá os mesmos
símbolos tirados.

**O que se mexeu.** O número que o portão comparava, em seis commits da `main`
(o #297 é a mesma imagem do #295):

| Recurso | `6adf1023` (27/10) | #294 (28/10) | #295 | #298 | #299 | #300 (29/10) |
|---|---:|---:|---:|---:|---:|---:|
| `web_https` | 24 568 | 24 568 | 24 568 | 24 552 | 24 560 | 24 552 |
| `concurrency_asserts` | 2 240 | 2 232 | 2 240 | 2 232 | 2 232 | 2 232 |
| `sensor_ds18b20` | 5 520 | 5 528 | 5 520 | 5 512 | 5 520 | 5 512 |
| `sensor_dht22` | 3 456 | 3 464 | 3 456 | 3 448 | 3 456 | 3 448 |
| `sensor_bme280` | 17 804 | 17 804 | 17 804 | 17 788 | 17 796 | 17 788 |
| `tel_mqtt` | 9 344 | 9 344 | 9 344 | 9 336 | 9 336 | 9 336 |
| `web_export_api` | 12 296 | 12 304 | 12 296 | 12 288 | 12 296 | 12 288 |
| `web_metrics` | 4 968 | 4 968 | 4 968 | 4 960 | 4 960 | 4 960 |
| `tft_graph` | 20 432 | 20 432 | 20 432 | 20 424 | 20 432 | 20 424 |

Nas 45 células (nove recursos, cinco passos) o que a chave tira, o que ela
acrescenta e o que as strings mescladas somam não mudaram um byte; mexeram só
os `*fill*`, o enchimento de alinhamento que o linker põe entre as seções de
entrada. A única exceção é `AppManager::core0Yield( )`: a chave do `tft_graph`
tirava 104 B dele, 100 B no #294 e 104 B de novo no #295, e o enchimento
escondeu os 4 B. Nenhum símbolo passou de "tirado pela chave" para "sempre
ligado".

**Por que o enchimento anda.** A `.text` tem, perto do fim, uma seção de 688 B
alinhada a 8 (a `.fini.__stub`, depois do `.fini` de 4 B), e a `.rodata` tem tabelas
`time_t[]` alinhadas a 8 (o `rangeDuration` de `WebManager_History.cpp`, o do
`AppManager_Graph.cpp`, o do `core0Yield( )`) atrás dos pools de strings
`.str1.1`. O enchimento na frente delas vale 0 a 7 B e segue o tamanho de tudo
o que veio antes; a base e a variante têm cadeias diferentes, então andam
diferente, e a economia anda 8 a 16 B. A `pico_w_release` é a base de 7 dos
9 recursos, por isso eles caem juntos. Duas provas de que não é código:

- O mesmo `229d5d59` com um caractere da versão trocado, nenhum código: com
  `"2.12.1"` seis dos nove sobem 8 B (24 560, 5 520, 3 456, 17 796, 12 296,
  20 432); com `"2.13.0"` nenhum se mexe.
- Do que entra na imagem, o #299 só muda o `WebUI.h`. A página de Arquivos passou
  de 6 670 a 6 779 B comprimida, e seis recursos subiram 8 B.

As páginas comprimidas mudaram nos merges que se esperava (`DASH_PAGE_GZ` +126 B
no #298, `FILE_PAGE_GZ` +109 B no #299, `LOGIN_PAGE_GZ` +1 B no #300, que carimba
a versão), mas não é a diferença entre as páginas da base e as da variante que
anda: oito das nove chaves não cortam página nenhuma, e a `TEL_PAGE_GZ`, que o
`tel_mqtt` corta (448 B), ficou em 8 928 B nos seis commits. A página é só mais
uma coisa que muda o tamanho do que vem antes do alinhamento. No `tel_mqtt` o
`used` cru é 7 840 B em todos os commits (o setor de 4 KB do banco esconde
tudo), e o que anda é o enchimento antes do banco: 2 300 → 2 180 B na base e
3 804 → 3 676 B na variante.

**O conserto.** O piso (`--check`, `--update`) compara o `used` menos todo
`*fill*` que o mapa lista dentro de `.boot2`, `.text`, `.rodata` e `.data`, as
seções que o `used` soma (o da `.boot2` é 0 B nos 145 mapas); `flash_fill( )`
generaliza o `bank_pad( )`, e o
enchimento do banco do BTstack é só o maior dos termos. O leitor recusa, em vez
de ler zero, o mapa cujas quatro seções não somam o `used` (outro chip, outra
conta do PlatformIO), o que não tem uma delas e o `*fill*` fora de qualquer
seção, fora da própria seção ou numa linha que ele não entende; lê as 145
imagens que medi, sem uma recusa, e dá o que o leitor do kit dá. Uma queda de até
8 B (`FLASH_TOL`) passa: o que sobra, tirado o enchimento, é código de verdade e
anda pouco (4 B no `tft_graph`; 12 B no `cli_full`, 8 B no `sound_buzzer` e 1 B
no `web_history`, os três últimos medidos em três commits): é esse resíduo que os
8 B cobrem. A menor queda que o piso já registrou foi de 24 B (`sound_buzzer`,
#234, uma mudança de código medida em 22/10 com o enchimento ainda dentro); o
#234 sozinho tira 38 B da economia do buzzer (4 999 → 4 961 B, medido em 30/10
sem o enchimento), bem acima dos 8 B. A `--matrix` segue com o `used` inteiro, e
o `tools/feature_costs.json` não mudou: o custo de uma chave é o que a imagem
ocupa, e o enchimento ocupa o slot.

**A prova.** `tools/test_measure_savings.py` passou de 21 para 45 casos. Antes
da mudança, 32 dos 45 reprovam (os outros 13 fixam o que não pode mudar: uma
alta passa, a tolerância de RAM, o pedido do mapa, as recusas da build, o que o
`--update` grava, o que a `--matrix` mede e a limpeza dos ambientes
temporários); depois, 45 de 45. Quinze mudanças pequenas na ferramenta (tirar a
subtração, zerar ou alargar a tolerância, tirar a conferência do `used`,
aceitar seção ausente, contar toda entrada ou toda seção como enchimento, ler o
que está acima do mapa, tirar a conferência de seção, aceitar `*fill*` ilegível
ou solto, ler como zero na build, gravar o piso menos a tolerância, tirar 1 B
do `used` da `--matrix`) reprovam cada uma só os casos escritos para ela.
Refeita sobre as nove execuções (seis commits da `main`, as duas trocas de
versão e o vazamento plantado), a conta nova tem 0 achado falso em 81 medidas
(9 execuções x 9 recursos), com o `tft_graph` -4 B no #294, tolerado. Com o
vazamento plantado de propósito (o `main.cpp` pega o endereço de
`PromMetrics::lineF` e de `escapeLabel`, dois auxiliares da chave
`web_metrics`), o `web_metrics` cai de 4 955 para 4 745 B e é o único achado
dos 16; uma leitura por símbolo (do mesmo kit, fora do repositório) nomeia os
dois símbolos passando de "tirado pela chave" para "sempre ligado". O `--check`
do ramo fecha verde, com os 16 pisos novos.

**Os pisos gravados em 2026-10-30** (`--update` sobre `e7768221`, as mesmas
imagens de `229d5d59`). Os números não são mais baixos nem mais altos: medem
outra coisa, bytes de conteúdo sem o enchimento (por isso 13 descem de 1 a 102 B
e 3 sobem de 1 a 5 B, e deixam de ser múltiplos de 8). A RAM não mudou:

| Recurso | Base | Flash, piso anterior | Flash, piso novo |
|---|---|---:|---:|
| `bluetooth` | `pico_w_alpha` | 136 808 | 136 706 |
| `tel_tls` | `pico_w_air` | 58 080 | 58 025 |
| `cli_full` | `pico_w_test` | 48 152 | 48 157 |
| `web_https` | `pico_w_release` | 24 568 | 24 546 |
| `web_history` | `pico_w_air` | 23 872 | 23 871 |
| `tft_graph` | `pico_w_release` | 20 432 | 20 433 |
| `sensor_bme280` | `pico_w_release` | 17 804 | 17 776 |
| `mdns` | `pico_w_release` | 16 632 | 16 604 |
| `web_export_api` | `pico_w_release` | 12 296 | 12 290 |
| `tel_mqtt` | `pico_w_air` | 9 344 | 9 340 |
| `sensor_ds18b20` | `pico_w_release` | 5 520 | 5 508 |
| `sound_buzzer` | `pico_w_release` | 4 984 | 4 985 |
| `web_metrics` | `pico_w_release` | 4 968 | 4 955 |
| `sensor_dht22` | `pico_w_release` | 3 456 | 3 443 |
| `concurrency_asserts` | `pico_w_asserts` | 2 240 | 2 235 |
| `syslog` | `pico_w_release` | 1 768 | 1 760 |

### O piso do `tft_graph` com a UI como driver (2026-10-30)

A UI como driver (`feat/ui-drivers`, da S10c à S11) chega à `main` com o
`tft_graph` economizando 20 365 B, não os 20 433 B do piso da `main`: 68 B a
menos, 60 B além da tolerância. Sem esta entrada o job noturno
(`.github/workflows/savings.yml`) reprovaria na primeira manhã depois da fusão, e
nenhum PR veria o vermelho. Não vazou código do gráfico: a diferença está em
cinco funções do `DisplayManager`, decodificadas abaixo. O piso desce porque a
conta de bytes dessas cinco mudou, não porque o recurso deixou de sumir.

**Medido em 2026-10-30** (`pico_w_release` e `_sav_tft_graph_off`, o enchimento
do linker lido do mapa), com a mesma ferramenta nos dois lados:

| Árvore | `used` ligada | `used` desligada | Enchimento | Economia |
|---|---:|---:|---:|---:|
| `main` `43b275d8` (as imagens são, byte a byte, as de `cf97b955`: o #303 só mexeu em `docs/` e em `tools/build_manual.py`) | 1 023 860 | 1 003 436 | 692 / 701 | 20 433 |
| ramo `22d83de3`, antes da fusão da sync 8 | 1 023 924 | 1 003 556 | 686 / 683 | 20 365 |
| ramo `7d550359`, a ponta da S11 (`caafa549` mede o mesmo, nome a nome) | 1 024 060 | 1 003 692 | 685 / 682 | 20 365 |

A RAM não muda: 2 232 B nas três. Os 68 B estão todos em símbolos com nome (a
`main` tira 19 574 B por eles, o ramo 19 506 B); os 859 B sem nome (textos
literais, alinhamento dentro dos objetos) saem iguais nos dois lados, e o
enchimento sai das duas pontas da conta.

**Por símbolo** (`nm -S` das duas imagens de cada árvore):

- as 21 funções e dados que a chave tira por inteiro (16 354 B:
  `renderGraphOptimized` 4 892, `drawGraphDetailScreen` 3 068, `drawGraphScreen`
  2 888, `drawPeriodButtons` 1 232, `drawCalendarScreen` 1 012,
  `graphAccumRecord` 904, `drawGraphHeaderBar` 748, ...; a menor tira
  40 B) são as mesmas, com os mesmos bytes, na `main` e no ramo, e nenhuma
  está na imagem desligada do ramo;
- os únicos símbolos com nome de gráfico que a imagem desligada guarda são quatro
  stubs e um flag, 17 B, os mesmos da `main` (`requestLoadingScreen( )` 12 B,
  `setGraphNavOffset( )` 2 B, `showCalendar( )` 2 B, `_isRenderingGraph` 1 B);
- cinco funções do `DisplayManager` tiram bytes diferentes:

| Função | Tira na `main` | Tira no ramo | Diferença |
|---|---:|---:|---:|
| `loopCore1( )` | 484 | 436 | −48 |
| `handleTouch( )` | 1 832 | 1 804 | −28 |
| `drawUserEdit( )` | 0 | 4 | +4 |
| `pinAppendChar(char)` | 0 | 4 | +4 |
| `showSettingsAlarms( )` (a assinatura ganhou `const`: só o nome muda) | 12 | 12 | 0 |
| Soma | | | −68 |

**Por que as cinco.** Nenhuma leva código do gráfico para a imagem desligada. Em
`handleTouch( )` as classes de instrução que carregam a lógica do gráfico
(chamadas, comparações, desvios, leituras e escritas) saem com os mesmos 1 084 B
nos dois lados; o que difere são palavras de literal do `ldr =`: a `main` tira 66
palavras e 3 ilhas de literais, o ramo 59 e 4 (−7 palavras, −28 B). Em
`loopCore1( )` o líquido do resto é zero e a diferença é de 12 palavras de
literal, 10 delas as cinco constantes da sonda `C1_PHASE`, que cada ilha repete: a
`main` perde duas cópias com as duas ilhas que o gráfico mantinha, o ramo
nenhuma. Em `pinAppendChar( )` é um `nop` que alinha o literal da função, e que
na `main` está na imagem desligada e no ramo na ligada (−2 contra +2); em
`drawUserEdit( )`, oito `ldr r,=0x1a2c` a mais na imagem ligada do ramo contra
seis acessos de pilha a menos (+16 B, −12 B). Com a chave desligada o objeto
`DisplayManager` perde 5 872 B (a linha do gráfico na tabela de 2026-09-26) e todo
membro depois do pacote do gráfico desce o mesmo tanto: o deslocamento `0x1a28`
da imagem ligada é `0x338` na desligada, e `0x1c38` é `0x548`. Montar esse
deslocamento (uma palavra de literal, `movs` com `lsls`, um registrador que fica) e fechar
cada ilha de literais (o `ldr` de `pc` alcança 1 020 B) dependem do valor e da
distância, e o layout do objeto no ramo não é o da `main` (o mesmo ponto de uso
carrega `0x07c3` na `main` e `0x07f5` no ramo): cortar os mesmos blocos devolve
menos bytes.

**Como se provou.** A soma por nome fecha com o `used` que o PlatformIO reporta
nas imagens medidas. Os nomes que a `main` tira por inteiro, procurados na imagem
desligada do ramo: 0 de 21; procurados na imagem ligada, posta no lugar da
desligada: 21 de 21. A varredura por nomes com gráfico lê 33 na imagem ligada e 4
na desligada, e os 4 são os da `main`. Um par que não tirasse nada faria 107
linhas diferirem da `main`, não as 6 reais. Os movimentos da S11 (as 14 unidades
do painel original para `src/ui/original/`) não mudaram o tamanho de símbolo
nenhum: de `caafa549` à ponta, nenhum dos 4 512 nomes muda de tamanho em
`pico_w_release` nem em `_sav_tft_graph_off`, e `loopCore1( )` e `handleTouch( )`
ficam iguais instrução a instrução, menos o endereço dos `bl` para destinos sem
nome.

**O piso.** O `tft_graph` desce de 20 433 para 20 365 B
(`--only tft_graph --update`; a RAM fica em 2 232 B). Os outros 15 pisos não se
mexem: no `--check` sobre `7d550359`, 12 medem o piso exato e 3 medem acima
(`tel_mqtt` +2 B, `web_history` +4 B, `syslog` +3 B), então ficam como estão. A
tolerância de 8 B continua valendo: o portão volta a reprovar o `tft_graph`
abaixo de 20 357 B, e uma função do gráfico que passasse a ficar na imagem
desligada tira ao menos 40 B da economia.

**O que não está provado.** O mapa membro a membro do `DisplayManager` (o C++ do
projeto é construído sem `-g`; o DWARF só tem as unidades C do framework) e uma
recompilação controlada das ilhas de literais, que pediria mexer em `src/`: o
mecanismo acima é o que serve a todos os números, não um experimento. Medido na
máquina, `pico_w_release`, sem prova no ferro. Qualquer mudança nessas cinco
funções ou nos membros do objeto move o número de novo por dezenas de bytes, e a
decodificação se refaz (o kit, fora do repositório, é o `nm -S` das duas imagens somado por nome).

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
python3 tools/measure_savings.py --only tft_graph --update   # grava só o piso desse recurso
python3 tools/measure_savings.py --check       # reprova se a economia caiu
python3 tools/flash_compose.py --env pico_w_air --objects   # o rastreio: flash e RAM por objeto
```

O `--check` reconstrói base e variante de cada recurso do ledger e falha se a
economia de flash ficou **abaixo** do piso por mais de 8 B (`FLASH_TOL`; a RAM
entra com tolerância de 64 B, ruído de alinhamento do linker). O flash dos dois
lados é o `used` sem o enchimento do linker, todo `*fill*` de `.text`, `.rodata` e
`.data` lido do mapa que cada build pede ao linker; a build sem o mapa, sem zopfli
ou de mapa que não soma o `used` é recusada. É um ratchet, como o orçamento de
flash e a marca d'água do sprawl: a economia só sobe; abaixá-la exige `--update`
e justificativa no commit.

**Custo em CI.** O portão constrói 21 imagens (as 5 bases e uma variante por
recurso do piso, em 2026-10-22), o que não cabe no job `gates` (livre de
framework). Pertence a um job próprio, agendado (noturno) ou disparado à mão —
ver `.github/workflows/savings.yml` —, e por isso **nenhum PR vê o vermelho
dele**: confira com `gh run list --workflow "SIMUT savings watermark"`. O que o
`gates` roda em todo PR é o `tools/test_measure_savings.py`, a conta que o piso
faz.
Um `--check --only <recurso>` é barato e serve para conferir um recurso após
mexer no que ele guarda.
