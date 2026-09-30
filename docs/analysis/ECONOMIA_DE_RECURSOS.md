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
| `tel_tls` (cliente TLS da telemetria, 2026-09-30) | `pico_w_air` | 67 888 B | 16 B |
| `cli_full` | `pico_w_test` | 48 232 B | 0 B |
| `web_https` (TLS) | `pico_w_release` | 24 520 B | 24 B |
| `sensor_bme280` | `pico_w_release` | 17 724 B | 220 B |
| `mdns` | `pico_w_release` | 16 576 B | 168 B |
| `sensor_ds18b20` | `pico_w_release` | 5 432 B | 64 B |
| `sound_buzzer` | `pico_w_release` | 5 008 B | 0 B |
| `sensor_dht22` | `pico_w_release` | 3 400 B | 0 B |
| `concurrency_asserts` | `pico_w_asserts` | 2 176 B | 0 B |
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
| Página de histórico e as rotas sem página | `HIST_PAGE` (22 543 B gz), `/api/history_multi` (7 236), `/api/export/*` (3 376) | ~33 KB | ~33 KB | `/api/logs`, `/api/history/open` e `/api/history_days` ficam: as ferramentas de bancada e o `INTEGRACAO_SERVIDOR.md` dependem delas |
| Gráfico e calendário do painel | `DisplayManager_Graph`, `AppManager_Graph`, `DisplayManager_Calendar` | ~16,5 KB | — | só o TFT |
| Contas no painel | `DisplayManager_Users` | ~15 KB | — | só o TFT; preso ao PIN do painel |
| MQTT e Home Assistant | `PubSubClient` e ~5,5 KB do `TelemetryManager` | ~7 KB | ~7 KB | quase tudo num bloco contíguo; um `telTransport=1` salvo numa imagem sem MQTT precisa ser recusado, ou cai no ramo HTTP e faz POST no broker |
| Curvas de calibração | `/api/calib`, o motor das curvas | ~8 KB | ~8 KB | não há offset separado: o offset simples **é** uma curva de 1 ponto, e o `calib.csv` também guarda a identidade dos DS18B20. Sem a chave, a leitura sai crua |
| `/metrics` (Prometheus) | handler, autenticação, `PromMetrics` | ~3,3 KB | ~3,3 KB | o corte mais limpo: uma rota, sem página, sem config |
| Backup e restauração | validar e aplicar o `.bkp` | ~3,5 KB | ~3,5 KB | o CRC32 do `backup.cpp` fica (o estágio da OTA usa), e a atualização pela página chama `/api/backup` antes de gravar |
| Busca de redes Wi-Fi | handler, `pollScan`, o bloco da NET | ~2,8 KB | ~2,8 KB | o `native_network` testa a busca |
| Pacotes de idioma | `DisplayManager_LangParser` | ~2,6 KB | ~2,1 KB | uma imagem só em inglês precisa forçar EN, ou a CLI bilíngue responde em português |
| Syslog | `SyslogManager` e acessores | ~1,5 KB | ~1,5 KB | |

### RAM que existe em toda imagem

| O quê | Onde | Bytes | Tipo | Observação |
|---|---|---:|---|---|
| pools do lwIP | `memp.c`, `mem.c` | 39 670 + 16 415 | `.bss` | núcleo; dimensionados pelo `lwipopts.h` para seis conexões a 4×MSS |
| buffers da OTA | `ota/applier` 8 620, `ota/restore` 4 500, `ota/validation` 4 128 | 17 248 | `.bss` | usados uma vez por atualização ou restauração |
| objeto `StorageManager` | `AppManager` | 11 684 | heap | núcleo (histórico) |
| objeto `WebManager` | `AppManager` | 10 652 | heap | 8 KiB de buffer de upload |
| objeto `DisplayManager` | `AppManager` | 8 576 (Alpha 8 552, Air 8 192) | heap | no Alpha e no Air carrega o `_graphData` do gráfico (~5,7 KB) e buffers de PIN que essas imagens nunca tocam |
| pilha do Core 1 | `DisplayManager.cpp` | 8 192 | `.bss` | segue o TFT |
| `/api/calib` | `WebManager_Calib` | 3 640 | `.bss` | segue as curvas de calibração |
| pacote de idioma | `DisplayManager_LangParser` | 2 785 | `.bss` | segue os pacotes |
| objeto `TelemetryManager` | `AppManager` | 2 272 | heap | a fila de alarmes de 2 KiB existe com a linha desligada |
| objeto `SyslogManager` | `AppManager` | 2 136 | heap | o anel existe com o syslog desligado |
| `/api/history_multi` | `WebManager_History` | 2 048 | `.bss` | segue a página de histórico |

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
