# Modelo de recursos — o que é chaveável, o que é limitável, e as regras

**Estado:** Living · **Levantado em:** 2026-09-25 contra `main` `04b6fb2` (v2.7.3),
lendo `src/` (71 unidades de tradução, 47.671 linhas, mais 27.019 de headers),
`platformio.ini`, os patches do framework, `WebUI.h`, `tools/` e os documentos
Living · **Pergunta:** quais recursos do firmware fazem sentido virar chaves
ou magnitudes escolhidas pelo cliente antes de compilar, que regras prendem
uns aos outros, e o que precisa ser refeito para que isso seja uma base, e não
mais uma camada de `#if`?

A resposta curta. O firmware tem hoje **11 chaves de compilação de verdade**,
mais exclusão de arquivos por ambiente, um recorte de página web e sete pacotes
de temas comentados. O levantamento encontrou **54 recursos que valem uma
chave, duas escolhas exclusivas, uma variante e nove instrumentos de bancada**
(seção 2), **33 magnitudes, das quais 12 são escolha de cliente** (seção 3) e
**105 regras** entre eles (seção 4). Dessas regras, 24 são impostas hoje por
alguma máquina, seja o build, um script ou um validador em runtime; as outras
81 são comentário, documento, incidente, ou dedução desta análise.

O que falta não é uma ferramenta, é uma base. Três coisas a sustentam, e
nenhuma delas existe: **um manifesto** que diga o que cada recurso é e do que
depende, e que **gere** os ambientes do `platformio.ini` em vez de descrevê-los;
**uma costura por tipo de extensão** (rota, comando, tela, transporte, sensor,
página), para que um recurso seja uma unidade de tradução que entra ou sai do
link, e não 67 sítios de `#if` em 23 arquivos, que é o que o TFT é hoje; e
**um custo medido por máquina**, porque os custos que este projeto conhece
estão certos, mas estão espalhados por sete documentos e dois comentários que
mentem. A seção 6 é o plano, em oito passos; o primeiro é reproduzir os seis
ambientes de hoje byte a byte a partir do manifesto, que é a prova barata de
que o modelo é fiel. A seção 7 é o padrão para todo recurso novo daqui em
diante, com o que o CI passa a impor.

Nenhum número desta análise saiu de uma build nova. Os custos citados são os
que o projeto já mediu (`DIETA_FLASH.md`, `flash_budget.json`, o CHANGELOG) ou
os que se leem do ELF da `pico_w_release` de 25/09 com `nm -S`, marcados
**medido**; os marcados **cálc.** são somas a partir das declarações, e os
marcados **estimativa** são contas sobre a tabela de seções. A seção 5 diz o
que cada perfil proposto custaria, e diz que é estimativa.

---

## 1. O modelo

### 1.1 Vocabulário

| termo | o que é | exemplo |
|---|---|---|
| **chave** | um recurso que entra ou sai da imagem: booleano | `net.mdns`, `tel.mqtt` |
| **escolha** | um de N, exclusivos; sempre exatamente um | `display` ∈ {tft, alpha_paralelo, alpha_i2c, nenhum} |
| **variante** | uma chave que muda o ciclo de vida inteiro do aparelho, e por isso arrasta várias outras | `power.air` |
| **magnitude** | um inteiro com unidade, faixa e passo, que o firmware trata como teto | `users.max` = 32, `fs.size` = 1 MiB |
| **instrumento** | uma chave que nunca vai a um perfil de cliente: só bancada e CI | `bench.concurrency_asserts` |
| **perfil** | uma atribuição completa de chaves, escolhas e magnitudes; os seis ambientes de hoje são perfis | `pico_w_release` |
| **regra** | `requer`, `conflita`, `implica`, `padrão-se`, ou um limiar sobre uma soma | "`tel.mqtts` requer `tel.mqtt` e `tel.tls_client`" |
| **custo** | flash do `.bin`, `.data` + `.bss`, heap residente, heap de pico, bytes de LittleFS, e recursos finitos de hardware (pinos, máquinas PIO, canais de DMA, spinlocks, alarmes, o Core 1) | mDNS = 15.804 B de flash |
| **maturidade** | desde que versão existe, se foi validado no ferro e por qual documento, horas de soak, issues abertas | `net.mdns`: "nunca exercitado na bancada" |
| **costura** | o ponto do código onde um tipo de extensão é plugado; é o que decide se o corte é limpo | a lista de `_server->on` em `WebManager_Core.cpp:120-225` |

### 1.2 O que cada recurso declara

Um recurso é uma entrada num manifesto, `tools/features.toml`, com estes
campos. O apêndice A traz o rascunho inteiro.

| campo | conteúdo |
|---|---|
| `id`, `domain`, `title`, `kind` | identidade; `kind` ∈ chave, escolha, variante, magnitude, instrumento |
| `default` | ligado ou desligado no perfil de referência (ou o valor, para magnitude) |
| `requires`, `conflicts`, `implies` | as regras, por id |
| `macro` | a macro `SIMUT_*` que o código testa; sempre `#if`, nunca `#ifdef` (seção 6, P1) |
| `files` | as unidades de tradução que só existem com o recurso ligado |
| `web_omit`, `pages`, `routes`, `commands`, `logcodes` | o que o gerador recorta ou etiqueta em cada costura |
| `libs`, `build_flags`, `lib_ignore` | o que o recurso exige do PlatformIO |
| `cost` | flash, bss, heap, fs, pio_sm, dma, spinlocks, pins; cada valor com `measured_at` e fonte, ou `unmeasured` |
| `maturity` | `since`, `validated_by` (um documento), `soak_hours`, `issues` |
| `hazards` | interações conhecidas que não impedem o build, cada uma com a evidência |

### 1.3 Onde vive e o que gera

```
tools/features.toml            o manifesto: a única fonte
        │
        ├─ tools/gen_features.py ──► src/simut_features.h        (gerado; toda macro definida 0/1)
        │                        ──► tools/generated/profiles.ini (gerado; um [env:] por perfil,
        │                                                          incluído por extra_configs)
        │                        ──► src/features_registry.cpp   (gerado; chama o registro de
        │                                                          cada recurso ligado)
        │                        ──► docs/configurador/model.json (gerado; o que a página lê)
        │
        ├─ tools/check_features.py   o portão: macro não declarada, sítio de #if fora do
        │                            arquivo do recurso, rota/comando/página sem etiqueta,
        │                            perfil gerado ≠ perfil commitado
        │
        └─ tools/features_cost.json  a matriz de custo, escrita pelo CI, nunca à mão
```

A escolha de gerar um `.ini` incluído por `extra_configs`, em vez de reescrever
o `platformio.ini`, mantém o arquivo principal legível e o diff de cada perfil
visível no PR. A escolha de gerar `features_registry.cpp`, em vez de registrar
por construtor estático, é porque o linker com `--gc-sections` descarta
construtores que ninguém referencia, e é exatamente isso que se quer que ele
faça com o resto do recurso.

A semântica das regras é a do Kconfig (Linux, Zephyr, ESP-IDF): `depends on`
para `requires`, `select` para `implies`, `choice` para escolhas. Adotar a
sintaxe do Kconfig e o `kconfiglib` em vez de um TOML próprio é uma decisão em
aberto; o que não é negociável é a semântica, e o TOML é mais fácil de
estender com `cost` e `maturity`, que o Kconfig não tem.

### 1.4 A força de uma regra

Cada regra da seção 4 traz de onde vem a sua força hoje, porque isso diz o que
o modelo tem de passar a impor:

| força | quer dizer | exemplo hoje |
|---|---|---|
| **build** | `#error`, `static_assert` ou falha de link | `MAX_USERS <= 32` (`src/SystemDefs_Records.h:316`) |
| **script** | um `tools/check_*.py` que roda no pré-build ou no CI | o orçamento de flash |
| **runtime** | o firmware confere ao aplicar a configuração ou ao começar a operação | um dono por GPIO (`src/WebManager_Commit.cpp:700-711`) |
| **documento** | está escrito em comentário ou documento, e nada confere | "Choose ONE" em `src/simut_config.h:54` |
| **incidente** | só se sabe porque quebrou uma vez | o AP que para de medir |
| **inferida** | esta análise a deduziu do código e ninguém mediu | o teto real do SPI0 |

---

## 2. Inventário: o que vale uma chave

Cada tabela diz, por recurso, como ele é cortado **hoje** (macro, arquivo,
página, biblioteca, ou nada), o custo que o projeto já conhece, a maturidade
pelos documentos, e a **proposta**: chave, escolha, variante, instrumento, ou
"núcleo" (fica em toda imagem). Os ids são os do manifesto. As colunas de custo
e maturidade citam a fonte; onde dizem "desconhecido" é porque nenhum documento
registra, e isso é um dado, não uma lacuna desta análise.

### 2.1 O núcleo: o que não sai de nenhuma imagem

Não são chaves. Estão aqui para que ninguém as proponha como tal, e porque os
seus custos entram em todo perfil como piso.

| o que | por que não sai | custo conhecido |
|---|---|---|
| blob do rádio CYW43, lwIP, driver, `WiFiClient` | o produto é um aparelho de rede | 225.240 + 12.326 + 19.460 B de flash (`DIETA_FLASH.md` §2); `.bss` do lwIP 36.771 + 16.403 B (medido) |
| libc, pico-sdk, núcleo arduino-pico, LittleFS | são o chão | 72.955 + 28.820 + 51.960 + 22.231 B (`DIETA_FLASH.md` §2) |
| os cinco patches do framework em `tools/arduino_pico_overrides/` | toda imagem chama `setCorsOrigin` (2i) e `enableKeepAlive` (2f) sem `#if` (`src/WebManager_Core.cpp:102, :233`); o servidor HTTPS chama `simut_reserve_tls_server_pool` (2h); sem `patch.sh` o link morre. O Docker, o `docker-compose` e o zip de release não aplicam o `patch.sh` (apêndice B.3) | — |
| configuração persistente v25, `ConfigMigrate.h` | o layout é esquema: **um blob gravado por uma imagem é lido por outra**, então `sizeof(SystemConfig)` tem de ser o mesmo em toda imagem (`src/SystemDefs_Limits.h:31-37`). Um configurador corta código, nunca campos | 6.738 B por cópia (`src/ConfigMigrate.h:97`) |
| histórico V5 e o `.wip`, `StorageManager` | é o que o aparelho faz | 25.031 B (`ANALISE_FLASH_RAM.md` §3.4, snapshot) |
| pipeline de sensores e a tabela de canais | idem; os drivers, sim, são chaves (2.4) | 7.896 B (idem) |
| log binário, `LogPolicy`, autópsia de travamento | sem ele não há bancada nem suporte | 13.055 B com métricas e som (idem) |
| watchdog de 8.388 ms, `FlashIrqProbe` | o primeiro é hardware; o segundo é conferido pelo build (`tools/check_flash_probe.py`) | — |
| servidor web, login, sessões, `/api/perms`, `/api/status`, `LANG_JS`, `STYLE_CSS`, `LOGIN`, `DASH`, `CFG`, `NET`, `FILE` | o gerador proíbe tirar `LOGIN`, `STYLE_CSS` e `LANG_JS` (`tools/build_webui_gz.py:166-176`); tirar a `CFG` "quebrou o bootstrap" em julho (`:143-145`); `FILE` é o caminho da OTA e do backup; `NET` é o resgate pelo AP | WebServer 10.643 B; páginas: LOGIN 6.662, DASH 6.240, CFG 12.471, NET 4.303, FILE 5.172, LANG_JS 10.530, STYLE 2.937 B gz (`src/WebUI_GZ.h` de 25/09) |
| contas, senha com HMAC, bloqueio por IP, troca forçada, guardas de upload | são a segurança; as auditorias de agosto e setembro fecharam sobre elas | — |
| OTA: stage, validação, apply, etiqueta de variante; console de emergência | sem OTA e sem console não há campo | seção `.ota` 10.228 B; buffers 12.320 B de `.bss` (medido; ver 3.2) |
| NTP, relógio provisório, fuso; Wi-Fi STA com a escada de reconexão; IP estático | a hora carimba o histórico; a rede é o produto | 4.685 B a camada de rede (snapshot) |
| página `/license` | é uma das cinco cópias que `tools/check_license.py` confere, e a MIT exige distribuir o texto | 3.843 B gz |
| a etiqueta de idioma inglesa embutida (`DICTIONARY_EN`) e o `/lang.js` | os packs são dados (2.15); o inglês é o piso | — |

### 2.2 Display

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `display` | **escolha**: `tft` (ILI9341 + XPT2046, 30 telas, só o Core 1 desenha), `alpha_paralelo` (HD44780 16×2 em GP16–21), `alpha_i2c` (HD44780 no `Wire1`, GP26/27), `nenhum` | `SIMUT_DISPLAY_TFT` / `SIMUT_DISPLAY_ALPHA` / `HD44780_MODE_*` + exclusão de 10 a 12 arquivos por ambiente (`platformio.ini:467-480, :530-545`) + `custom_web_omit = tft`. **`nenhum` só existe dentro do Air**: `DisplayManager_None.cpp` está sob `SIMUT_AIR`, e `DisplayManager.cpp` trata `!SIMUT_DISPLAY_ALPHA` como TFT; a combinação "headless sem Air" tem nome (`src/simut_config.h:397-398`) e não compila. `alpha_i2c` não tem ambiente nem registro de teste | TFT: UI 88.608 B + fontes 11.696 + GFX/ILI9341/XPT2046 10.715 B (`DIETA_FLASH.md` §2; `ANALISE_FLASH_RAM.md` §3.4); canvas 28.800 B de heap (cálc.); pilha do Core 1 8.192 B (medido). Alpha: a imagem inteira mede 34.744 B a menos que o release **devolvendo** o Bluetooth de 64.732 B, então o TFT sozinho fica em torno de 100 KB (estimativa) | TFT: validado, é o rig. Alpha: "a bancada não tem HD44780" (README) | **escolha** de quatro; `nenhum` passa a ser um valor próprio, não um efeito colateral do Air |
| `tft.graph` | gráfico no painel: `DisplayManager_Graph.cpp` (1.327 linhas), `AppManager_Graph.cpp` (593), 200 pontos, baldes com banda | arquivos excluídos só junto com o TFT inteiro | 11,3 kB de flash (`DIETA_FLASH.md` §2); `_graphData` ~5,9 KB residentes no `DisplayManager`, **presentes até no alpha** (`src/DisplayManager.h:853`); 2.000 B de `.bss` em `drawGraphScreen` (medido) | validado (`graph.png`, README) | chave; requer `display = tft` |
| `tft.calendar` | calendário para escolher o dia do gráfico: `DisplayManager_Calendar.cpp` (179) | idem | desconhecido; é um arquivo pequeno | renderiza; "sempre em português, ignora o idioma" (manual, cap. 11) | chave; requer `tft.graph` |
| `tft.users_admin` | contas, bits e PIN administrados no painel: `DisplayManager_Users.cpp` (1.709 linhas) | arquivo inteiro sob `#if SIMUT_DISPLAY_TFT` (fica vazio no alpha e no Air) | parte dos +15.792 B da v24 (`flash_budget.json`) | validado 32/32 (README 20/09) | chave; requer `panel.pin` |
| `panel.pin` | identidade no painel: conta antes do PIN, teclado embaralhado, bloqueio por conta e teto do painel (`PinKeypad.h`, `DisplayManager_Auth.cpp`, `AppManager_Panel.cpp`) | **derivada**: `SIMUT_PANEL_PIN` é `#define` fixo a partir do TFT (`src/SystemDefs_Limits.h:44-48`), e aparece em 13 arquivos | v24 +15.792 B, teclado +1.752, v25 +5.960; desligá-la tirou 2.750 B do Air e 972 do alpha (`flash_budget.json`) | validado 32/32 | chave própria; requer `display = tft`. Sem ela, o menu do painel precisa de uma política (só leitura, ou aberto): **decisão de produto**, marcada na regra R-D5 |
| `tft.mirror` | espelho clicável, captura BMP, toque e teclado pela web: `WebManager_History.cpp:1543-2391`, `ScreenRle.h`, o bloco `@IF tft` da DASH, 5 rotas | `#if SIMUT_DISPLAY_TFT` em `WebManager_Core.cpp:170-176` + `custom_web_omit = tft`. **Nada liga o `custom_web_omit` à macro**: só os comentários em `platformio.ini:449-453` | +3.512, +816 e +2.048 B no release (`flash_budget.json` 18–22/09); 2.363 B gz de página; 26.624 B de heap transitório por quadro (cálc.) | validado: 0 de 76.800 pixels diferentes, 213 ms por quadro (README 19/09) | chave; requer `display = tft`; o `custom_web_omit` passa a ser derivado dela pelo gerador |
| `tft.themes_fs` | temas `.thm` carregados de `/themes` (até 8), seletor no painel e na web, `/api/themes`, `/api/save_sys` | nenhum: `Themes.cpp` compila até no alpha e no Air, e as duas rotas são registradas em toda imagem (`WebManager_Core.cpp:148, :157`) | 800 B de `.bss` (medido); `data/themes/` traz 12 arquivos e 4 somem em silêncio (`src/Themes.cpp:302`) | renderiza; os `.thm` somem a cada OTA (`PLANO_STABLE.md` §6) | chave; requer `display = tft` |
| `tft.theme_packs` | 49 paletas compiladas em 7 pacotes (`SIMUT_THEMES_HEALTH`, `PRO`, `MEDICAL`, `SAFETY`, `RETRO`, `NATURE`, `UTILITY`) | `#ifdef` por pacote, todos comentados (`src/simut_config.h:318-324`) | ~70 B por tema (`src/simut_config.h:313`) ou ~85 B (manual §5): os dois números convivem | desconhecido | sete chaves; requerem `display = tft` |
| `tft.license_text` | o texto MIT paginado no painel (item 7) | `SIMUT_LICENSE_STUB` troca o texto por um stub (`HelpLicenseEN.h:236`); não toca a página web | 1.833 B; o stub daria −1.800 B no release, "decisão de produto" (`DIETA_FLASH.md` §5) | renderiza | chave; requer `display = tft` |
| `alpha.mode` | **escolha**: `paralelo` (GP16–21) ou `i2c` (PCF8574 em GP26/27, `Wire1`) | `HD44780_MODE_PARALLEL` / `HD44780_MODE_I2C` (`src/simut_config.h:194-239`) | — | paralelo: sem bancada; i2c: sem ambiente, sem registro | escolha; requer `display = alpha_*`; `i2c` conflita com `sensors.bmx280` em par I2C1 (R-H21) |
| `bench.ui_study` | o estudo Ângulo do painel: `UiStudy.cpp`, três layouts | `SIMUT_UI_STUDY`, só em `pico_w_uistudy`, com 49 sítios em 14 arquivos | sem orçamento (`flash_budget.json`, "unbudgeted") | 17 capturas | instrumento; requer `display = tft` |

O que fica dentro do TFT sem chave própria, e por quê: o toque e a sua
calibração (a UI é de toque; sem eles o TFT é um mostrador, e nenhuma tela foi
desenhada para isso), o dashboard, o menu de configurações, a tela de alarme
(quando `alarms.limits` está ligada), o terminal de boot e as fontes. A tela
de estatísticas (`MODE_STATS_VIEW`) é **código morto**: "nenhum toque chega a
ela" (manual, cap. 11); sai no plano, não no modelo.

### 2.3 Sensores

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `sensors.ds18b20` | 1-Wire por PIO (`pio0`, 27 instruções, uma máquina para todas as sondas), ROM verificada, quarentena, 9 a 12 bits | `SIMUT_SENSOR_DS18B20`: **35 sítios em 13 arquivos**, inclusive `StorageManager.cpp:576`, `WebManager_Calib.cpp:1000`, `WebManager_Commit.cpp:971`, `AppManager_Boot.cpp:1037`; o driver é header-only e `SensorManager.h:24-26` o inclui sem condição | drivers PIO + SPI somam 16.392 B (`DIETA_FLASH.md` §2), sem separação por driver | validado (rig; Air no GP0) | chave; ao menos um sensor ligado (R-S2) |
| `sensors.dht22` | DHT22 por PIO (`pio1`, 17 instruções, uma máquina) | `SIMUT_SENSOR_DHT22`: 23 sítios em 6 arquivos; a máquina de scan encadeia DS18B20 → DHT22 por `#elif` (`src/SensorManager.cpp:395-399, :420-431`) | idem | validado (2× DHT22, checkpoint v2.3.3) | chave |
| `sensors.bmx280` | BME280 e BMP280 por I2C de hardware em par nativo, com PIO ou bit-bang como reserva; um spinlock por instância | `SIMUT_SENSOR_BME280`: 27 sítios em 6 arquivos | idem; cada instância viva custa 1 dos 8 spinlocks reivindicáveis (R-H15) | BMP280 validado (cid 0x58); BME280 desconhecido | chave |
| `sensors.scan` | procurar e adotar sondas: varre os GPIO, `/api/action?op=sensor_scan`, `sensor scan` | nenhum; o scan é DS18B20 por dentro (`AppManager_Sensors.cpp`) | desconhecido | desconhecido; a varredura invade o GP16 (R-H7) | chave; requer `sensors.ds18b20` |
| `sensors.calib_curves` | curvas de até 5 pontos por grandeza, Reta ou Suave, `/calib.csv`, `POST /api/calib`, editor na CFG | nenhum: `sensors/CalibCurve.h` (442 linhas) é usado por `StorageManager.cpp:1758-1860`, `WebManager_Calib.cpp`, `SensorManager.cpp` | 2.808 B de `.bss` (`s_changes`, medido); 512 B de heap por sensor ativo (cálc.); exige NTP (503 sem ele) | validado (21,77 → 22,27 na dieta) | chave; requer `net.ntp` (núcleo); sem ela, o offset simples fica |

O quarto canal da tabela, `lux`, não tem driver que o produza; a temperatura
do chip no slot 16 é núcleo e cabe em poucos bytes.

### 2.4 Alarmes, manutenção e som

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `alarms.limits` | mínimo e máximo por canal e por sensor, ao vivo; a tela de alarme no TFT; `/alarms` | nenhum: `AppManager_HistoryAlarm.cpp` (811 linhas) é núcleo | página ALARMS 4.491 B gz | validado (19/09) | chave, ligada por padrão; um registrador puro pode não a querer |
| `alarms.fault` | alarme de falha de sensor com limites desligados (`err`, `err_sil`, `err_off`) | nenhum | — | validado (`alarm_hw_test`) | chave; requer `alarms.limits` |
| `alarms.maintenance` | janela de manutenção por sensor, até 30 dias, `maint_on`/`maint_off`, bits por conta no painel | nenhum; é fatia de `AlarmPayload.h`, `WebManager_Commit.cpp:1212-1227`, `AppManager_Panel.cpp`; **não tem controle na UI web** | parte dos +2.896 B da v2.5 (`flash_budget.json` 19/09) | validado (19/09) | chave; requer `alarms.limits`; `MaintConfig` fica no esquema mesmo desligada (núcleo, 2.1) |
| `sound.buzzer` | cigarra por PIO em GP22: 5 classes × 6 melodias, volumes, sons web | pela variante: stub sob `SIMUT_AIR` (`SoundManager.h:71-100`), arquivo fora e `lib_ignore BuzzerPIO` no Air (`platformio.ini:543, :550`) | 2 máquinas PIO no mesmo bloco, 4 instruções, 1 spinlock, alarmes do pool por nota | desconhecido | chave; requer o GP22 e 2 máquinas PIO (R-H13); no Air a seção de sons "aparece na página e não faz nada" (cap. 7): a chave desligada tem de tirá-la da página também |

### 2.5 Histórico, log e arquivos

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `history.web` | a página HIST inteira: gráficos com renderizador próprio, calendário, CSV no navegador, visor de eventos, limpar log | nenhum: `custom_fs_pages` só a **move** para o LittleFS, e o gerador recusa isso nas imagens publicadas (`SHIPPING_ENVS`, `tools/build_webui_gz.py:161`); as rotas são registradas sem condição | 22.544 B gz, a maior página (`src/WebUI_GZ.h`); `/api/history_multi` mantém 2.048 B de `.bss` (medido) | validado (suíte web 25/09) | chave; requer `history.v5` (núcleo); **não** corta `/api/ls` nem `/download`, que são da FILE |
| `history.export_simx` | `/api/export/history.bin` e `/api/export/logs.bin`, o formato `.simx` | nenhum; "nenhuma página usa (desde v2.7.2); só `tools/export_csv_bench.py`" (`flash_budget.json` 24/09) | desconhecido; 70 B por registro no fluxo | desconhecido | chave, **desligada por padrão**: é bancada |
| `log.syslog` | RFC 5424 por UDP para um IPv4, nível mínimo, `slog_*` na CFG | nenhum: `SyslogManager.cpp` (110 linhas) é próprio, mas tem ~8 pontos de fiação e o objeto de 2.136 B existe mesmo desligado (medido) | ~2.900 B de flash (CHANGELOG v2.2.14) | parcial: 7 golden vectors nativos; ao vivo pendente (`promotion/v2.3.2-stable/CHECKPOINT.md`) | chave; a chave desligada tem de tirar o objeto, não só o `begin` |
| `storage.backup_restore` | `.bkp` do FS inteiro com CRC32, validar ou aplicar (`ota/backup.cpp`, `ota/restore.cpp`), botões na FILE | nenhum: `+<ota/*.cpp>` em toda imagem | 4.496 B de `.bss` só do restore (medido); tabela CRC32 de 1 KiB | validado: 76 entradas byte a byte (promoção v2.3.2) | chave, ligada por padrão; requer `web.files` (núcleo) |

### 2.6 OTA

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `ota.web` | stage, validação, apply, snapshot da config, etiqueta de variante | núcleo (2.1) | — | 21 ciclos seguidos; 6/6 na v2.7.0 | núcleo |
| `ota.dual_slot` | dois slots com volta ao anterior | **não existe**; a seção 5 faz a conta | um segundo slot custa a imagem inteira mais um bootloader que o arduino-pico não tem | — | chave futura; só entra no modelo para a regra R-F3 dizer se a imagem cabe |

### 2.7 Rede

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `net.wifi_scan` | busca de redes, até 12 por sinal, também de dentro do AP; `GET /api/wifi/scan` | nenhum; `NetworkManager.cpp:257-352` + a NET | +2.776 e +624 B no release, +1.103 e +508 B gz (`flash_budget.json` 20/09); 432 B de RAM | validado 18/18 | chave |
| `net.ap` | AP de configuração `<nome>_SETUP`, WPA2 com chave do chip, portal em 192.168.4.1; aberto por comando, painel ou gesto | nenhum; `SIMUT_AP_OPEN` só troca a chave (bancada) | dentro do "resto" de 13.447 B | validado (celular entra em 4,1 s) | chave, ligada por padrão; **com o AP no ar o aparelho não mede** (R-R1) |
| `net.ap_auto` | AP que abre sozinho sem rede configurada ou depois de perdê-la | nenhum, **nem em runtime**; o Air fica de fora por `SIMUT_AIR` (`NetworkManager.cpp:675-740`) | parte dos +1.424 B da v2.7.1 | parcial; o manual (cap. 9) diz: "não use a v2.7.1 a v2.7.3 num aparelho que vai funcionar sem Wi-Fi" | chave, requer `net.ap`; **desligada por padrão** até ter interruptor em runtime |
| `net.ap_gesture` | segurar o toque 3 s no boot para abrir o AP | nenhum (`AppManager_Boot.cpp:491`) | — | validado 4 casos (22/09); "o mais frágil dos cinco caminhos" (manual §14) | chave; requer `display = tft` e `net.ap` |
| `net.captive_dns` | DNS na UDP/53 que leva qualquer endereço ao aparelho | nenhum (`DNSServer`, `onNotFound`) | dentro do "resto" | parcial | chave; requer `net.ap` |
| `net.mdns` | `<nome>.local` e `_simut._tcp` com TXT `uid`, `ver`, `env`, `tls` | `SIMUT_MDNS`: 7 sítios em 3 arquivos, mas `#ifdef` em `SystemDefs_Network.h:61` e `#if` nos outros; vale 0 em test, alpha e Air | LEAmDNS 15.804 B; −16.608 B desligado (`DIETA_FLASH.md` §4); o comentário em `src/simut_config.h:285` diz "negligible" | "a bancada nunca exercita o responder"; no Air nunca anunciou (#118) | chave; requer 7 PCBs UDP do lwIP (R-N5) |
| `net.https_server` | servidor TLS 1.2 com EC P-256, uma conexão por vez, `POST /api/tls`, `system https off`, política de senha forte | `SIMUT_WEB_HTTPS` como **`#ifdef`** (`=0` ligaria), 11 sítios em 4 arquivos; `WebManager_Tls.cpp` inteiro; `CMD_HTTPS_OFF` sem guarda | ~20 KB de flash do lado servidor (CHANGELOG v2.2.5) + `/api/tls` 3.872 B; ~21,5 KB de heap reservados no boot (`src/WebManager_Core.cpp:41-42`) | validado: ECDHE-ECDSA e ECDHE-RSA | chave; implica `tls.lib` (a BearSSL, que a telemetria segura também usa: R-S7) |
| `net.bluetooth` | console SPP com a senha do admin, bloqueio que sobrevive à reconexão, visível 5 min | `SIMUT_BLUETOOTH` + `PIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` + `BluetoothManager.cpp` no filtro + `lib_ignore SerialBT` retirado, tudo à mão (`platformio.ini:60-79`) | **64.732 B de flash e 16.416 B de `.bss`** (`platformio.ini:72-73`, medido no ferro); ≈135 kB no Air (`DIETA_FLASH.md` §3); 8 KiB de flash gravável dentro do slot (R-S6) | validado V-01a/b 3/3 | chave; requer `cli.emergency` (núcleo); é o único canal local do Air (R-S20) |
| `fleet.hooks` | os ganchos de frota: CORS para o gestor, Bearer, cabeçalhos `X-SIMUT-*`, TXT do mDNS, `_dry`, manifesto | nenhum; espalhado por `CorsOrigin.h`, `WebManager_Core.cpp:25-35, :312-374`, `WebManager_Auth.cpp:525, :612`, `WebManager_Api.cpp:743-830`, `TelemetryManager.cpp:2385-2391`, `NetworkManager.cpp:836-872` | CORS 2.232 + 232 B (CHANGELOG v2.4.5); ~90 B por POST | parcial: "nada executado em aparelho" em 11/09 (`ANALISE_GESTOR_FROTA.md` §8) | chave; sem gestor de frota não serve a nada |

O NTP, o fuso, a escada de reconexão e o IP estático são núcleo (2.1). O
keep-alive HTTP já tem interruptor em runtime e fica.

### 2.8 Web: páginas e API

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `web.page_tel` | a página `/telemetry`: transporte, TLS, MQTT, HA, lotes, construtor de payload, 2ª linha | só mover para o FS em bancada (`pico_w_test_https`, `platformio.ini:348`) | 7.784 B gz | validado | chave; requer `tel.http`; sem ela, telemetria só por API e CLI |
| `web.page_usr` | a página `/users` | idem | 3.161 B gz | validado | chave; contas continuam pela API, CLI e painel |
| `web.page_alarms` | a página `/alarms` | idem | 4.491 B gz | validado | chave; requer `alarms.limits` |
| `web.metrics` | `GET /metrics` para Prometheus: 37 famílias, Basic ou sessão, mesmo bloqueio do login | nenhum: `WebManager_Metrics.cpp` (281 linhas) é próprio, a rota é fixa (`WebManager_Core.cpp:142`) | 4.560 B (CHANGELOG v2.2.13); ~0,7 s por coleta | validado (719 ms) | chave; corte limpo, não tem página |
| `web.api_bench` | rotas sem consumidor no produto: `/api/screenshot_chunk`, `/api/keypad`, `/api/history_multi`, `/api/mkdir`, `/api/sec_status` | nenhum; `keypad` e `chunk` saem só com o TFT | `/api/keypad` 368 B; `history_multi` 2.048 B de `.bss` | — | instrumento, **desligado por padrão** nos perfis de cliente |
| `web.a11y` | operação por teclado | dentro das páginas | +847 B gz (v2.2.15) | "ferramenta pronta, execução pós-soak" | núcleo (Ângulo, regra 8) |

### 2.9 Contas e segurança

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `tls.cert_install` | `POST /api/tls`: instalar o par no `/config` | `WebManager_Tls.cpp` inteiro sob `#ifdef SIMUT_WEB_HTTPS` | +3.872 B | validado | fica dentro de `net.https_server`; não é chave própria |
| `panel.pin`, `tft.users_admin` | ver 2.2 | | | | |
| `auth.strong_password` | política de senha forte | só existe sob HTTPS (`WebManager_Auth.cpp:722-733, :783-794`) | — | — | **defeito**: a política não deveria depender do transporte; vira chave própria, ligada por padrão |

### 2.10 Telemetria e integrações

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `tel.http` | telemetria por `POST` HTTP em lote, cursor em flash, AIMD | nenhum: o transporte é escolhido em **runtime** (`cfg.telTransport`, `src/SystemDefs_Records.h:57-60`) e tudo compila sempre (`TelemetryManager.cpp`, 2.811 linhas, 25,3 kB) | HTTPClient 6.111 B | validado (3 h 58 sem coletor, 0 faltando) | chave, ligada por padrão; a base das outras |
| `tel.tls_client` | HTTPS e MQTTS: `WiFiClientSecureBearSSL` com 4 suítes podadas, `/cert.pem` | nenhum; a poda é patch do framework | BearSSL 94.712 + `WiFiClientSecure` 19.460 B (`DIETA_FLASH.md` §2); dois contextos de "~16 KB" cada (`src/TelemetryManager.h:248, :259`); iobuf 4.096 + 512 por conexão | validado (sink HTTPS 6/6; soak 30 min) | chave; implica `tls.lib`; requer `tel.http` ou `tel.mqtt` |
| `tel.mqtt` | MQTT e MQTTS: PubSubClient, tópico, LWT, QoS 0, buffer que cresce até 8.192 B | nenhum (`PubSubClient` sempre linkado) | dentro do "resto"; o manual de integração assume HTTP puro | validado 7/7 | chave; requer `tel.http` (partilham o lote e o cursor) |
| `tel.csv_mode` | payload CSV | nenhum (`TelemetryManager.cpp:1660-2140`) | 160 B de heap por registro contra 350 do JSON | parcial | chave; requer `tel.http` |
| `tel.custom_payload` | construtor de payload: `t_glob`, `t_line`, `t_sep`, prévia ao vivo | nenhum; os templates são 776 B do esquema (ficam) | — | validado; a CLI corta em 63 caracteres e responde OK (B12) | chave; requer `tel.http` |
| `tel.alarm_line` | a 2ª linha: fila em RAM, confirmação por 2xx ou ack MQTT, reenvio a cada 15 s | nenhum; `AlarmQueue.h`, `AlarmPayload.h`, `TelemetryManager.cpp:2418-2786` | 2.048 B de `.bss` sempre, seja qual for `queueMax` (3.2) | validado 11/14 (as 3 falhas são o B12) | chave; requer `alarms.limits` e `tel.http` |
| `tel.ha_discovery` | Home Assistant por MQTT Discovery, configs retidas | nenhum; `HaDiscovery.h` dentro do `TelemetryManager` (`:1082-1227`) | 2.544 B (CHANGELOG v2.2.13) | validado (6 sensores → 8 entidades) | chave; requer `tel.mqtt`; exige JSON e lote ≤ 5 em runtime (R-S14) |
| `tel.tls_keepalive` | manter a sessão TLS entre lotes | `TEL_TLS_KEEPALIVE_EXPERIMENT = 0` (`src/simut_config.h:558`) | 5,2× mais rápido com lote de 25 (medido no plano de cadência) | não publicado | instrumento até a `phase_survive` rodar |

### 2.11 Console

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `cli.emergency` | os 14 comandos de emergência num prompt só | `SIMUT_CLI_FULL = 0` (release, alpha); `tools/check_cli_help.py` confere o conjunto | tirar a CLI completa devolveu 44.516 B (CHANGELOG v1.5.6) | validado | núcleo |
| `cli.full` | os 4 modos Cisco, 56 comandos, `write memory` | `SIMUT_CLI_FULL = 1`: **34 sítios em 9 arquivos**, tocando o enum (80 `CMD_*` sem `#if`), o parser, a máscara de modos, o help, o `switch` de execução e o `@HELP` dos packs | +45.056 B no Air (CHANGELOG v2.4.10) | validado (as suítes dependem dela) | chave; o Air a exige ou exige `net.bluetooth` (R-S20) |
| `cli.bilingual` | respostas em português quando o pack não é inglês | nenhum: 282 literais duplicados | 2.564 B de literais; "CLI só em inglês" daria −1.000 B (`DIETA_FLASH.md` §5) | validado | chave, ligada por padrão |
| `bench.cli_panel` | `screen`, `touch sim`, `touch hold`, `show display keypad` | `SIMUT_CLI_FULL` (+ `SIMUT_PANEL_PIN` no `keypad`) | `touch hold` +520 B | validado (suítes do painel) | instrumento; requer `cli.full` e `display = tft` |

### 2.12 Energia

| id | recurso | corte hoje | custo conhecido | maturidade | proposta |
|---|---|---|---|---|---|
| `power.air` | M0 acordado e M1 dormindo por alarme do RTC; acorda, lê, grava, e sobe o rádio só com `t_int` pendentes; sem Core 1, sem som, sem display | `SIMUT_AIR`: **41 sítios em 15 arquivos, tocando ~7 subsistemas** (`NetworkManager.cpp:380, :462, :675, :709, :836`; `WebManager_Commit.cpp:363`; `SoundManager.h:71`; `CommandParser.cpp:117`; `CommandManager.cpp:636`; `HelpLicenseEN.h:70`; `DisplayManager.h:363`) + filtro + `air/*.c`. `AppManager_Air.cpp:23` e `DisplayManager_None.cpp:20` testam a macro **antes de qualquer include** | folga de OTA do Air: 21.460 B | parcial: ciclo de 120,23 s medido; o soak reprovou no ciclo 119 (F28, só mitigado); **a corrente nunca foi medida** (`PLANO_DIVIDA_TECNICA.md` §3.1) | variante; implica `display = nenhum`, `!sound.buzzer`, `!tft.*`, `!net.ap_auto`; proíbe `_dry`/`_nosave` |
| `air.charger_sense` | GP17 em nível alto impede a hibernação | `AIR_CHARGER_PIN` + `air charger` em runtime | — | validado (130 s acordado com carregador) | chave; requer `power.air` |

### 2.13 Instrumentos de bancada

Nunca entram num perfil de cliente; entram no modelo para que a página os
mostre como tal e para que o CI os construa.

| id | macro hoje | onde | proposta |
|---|---|---|---|
| `bench.concurrency_asserts` | `SIMUT_CONCURRENCY_ASSERTS` (`#ifdef`), `pico_w_asserts` | `src/DisplayManager.cpp:200-206`, `ConcurrencyAsserts.h` | instrumento; **requer `display ≠ nenhum`**: a ponte `simutStateMutexHeldByCurrentCore` só existe em `DisplayManager.cpp`, que o Air exclui (inferido: Air + asserts não linkaria) |
| `bench.mirror_probe` | `SIMUT_MIRROR_PROBE`, nenhum ambiente | 8 sítios em 3 arquivos | instrumento; requer `tft.mirror` |
| `bench.wdt_disabled` | `SIMUT_WDT_DISABLED` (`#ifndef`), nenhum ambiente | `src/main.cpp:28, :48, :69` | instrumento |
| `bench.ap_open` | `SIMUT_AP_OPEN` | `src/NetworkManager.cpp:141` | instrumento; requer `net.ap` |
| `bench.flash_irq_probe` | sempre linkado; `tools/check_flash_probe.py` confere a SRAM | `src/FlashIrqProbe.cpp` | instrumento ligado por padrão em todo perfil (é a sonda das pausas de flash) |
| `bench.ui_study`, `bench.cli_panel`, `web.api_bench`, `tel.tls_keepalive` | ver acima | | |

### 2.14 Idiomas e licença

| id | recurso | corte hoje | custo | proposta |
|---|---|---|---|---|
| `i18n.pack_ptbr`, `i18n.pack_eses` | quais `.lng` viajam no pacote do cliente | copiar arquivos à mão; `tools/check_lang_packs.py` confere cada um | 39.028 B de FS o es-ES; 16 KiB de pico no load e ~2,9 KB residentes depois (medido); desde a v2.3.4-beta só carrega com idioma diferente do inglês (`src/AppManager_Boot.cpp:944-949`) | chaves de dados; o gerador escreve `data/lang/` do pacote |
| `license.web` | `/license` | núcleo (2.1) | 3.843 B gz | núcleo |

### 2.15 O que o levantamento achou morto ou errado, e que sai antes do modelo

- **`SIMUT_FACTORY_RESET=1`** está em três ambientes e não tem nenhum uso em `src/`.
- **`sampleIntervalMs` (`s_int`)** é validado, gravado e ecoado, e nenhum código o lê (3.4).
- **`cfg.loggingEnabled`**, o interruptor "Enable Local Logging" da CFG, não tem consumidor que desligue o log, e ainda assim força reinício (`ConfigApply.h:80`).
- **`handleApiOtaStagingTest`** está definido (`src/WebManager_Ota.cpp:462`) e declarado, sem rota.
- **`MODE_STATS_VIEW`** é inalcançável.
- **`SIMUT_BOOT_SERIAL_LOG`** não existe em `src/`; é variável de ambiente de duas ferramentas.
- **Duas semânticas de macro convivem**: `#if` de valor na maioria, `#ifdef` de presença em `SIMUT_WEB_HTTPS`, `SIMUT_LICENSE_STUB`, `SIMUT_CONCURRENCY_ASSERTS`, `SIMUT_THEMES_*`, `SIMUT_WDT_DISABLED` e num sítio do `SIMUT_MDNS`. `NetworkManager.h:22-26` documenta o risco.
- **`custom_web_omit = tft` e `SIMUT_DISPLAY_TFT = 0` não sabem um do outro.**
- **O `pico_w_air` não estava em `SHIPPING_ENVS`** do gerador de páginas, embora seja publicado: podia declarar `custom_fs_pages` sem recusa. **Corrigido no P0** (`tools/build_webui_gz.py`), imagem idêntica.
- **`/api/reset_touch_cal` e `/api/themes` seguem registradas** no alpha e no Air, que não têm o que calibrar nem o que colorir.
- **Comentários que mentem sobre custo**: Bluetooth "BLE UART, ~22 KB" (`src/simut_config.h:267-268`; na verdade SPP clássico, 64.732 B de flash + 16.416 B de `.bss`) e mDNS "negligible" (`:271`; ~15 KB, 15.376 B medidos) **corrigidos no P0**, imagem idêntica; ainda aberto o tema "~70 B" contra "~85 B" no manual.
- **`cfg.useHttps` e `displayPin` são campos mortos que forçam reinício** (`ConfigApply.h:138, :164, :169`): o cookie `Secure` segue o transporte real, não o campo (`src/WebManager_Auth.cpp:513-518`).
- **`SystemDefs_Network.h:140-141` cita um `TelemetryGuard` que já não existe**, e o `SendGuard` alimenta o watchdog de um timer por IRQ a cada 2 s, contra o invariante 9 de `CONCURRENCY.md`, embora o próprio código diga que essas alimentações não chegam (`src/WebManager_Core.cpp:505-545`).
- **`@TRL` não tem consumidor em runtime no TFT nem no Air**: `trlLookup` devolve `nullptr` e tudo sai em inglês (`src/DisplayManager_LangParser.cpp:332-334, :346-360`), ao contrário do que o gate e o `CLAUDE.md` prometem.

---

## 3. As magnitudes: o que se escolhe em tamanho, não em sim ou não

Uma magnitude é um inteiro com unidade, faixa e passo, que o cliente escolhe antes de compilar e que o firmware trata como teto. O levantamento achou 154 constantes de capacidade (apêndice B.4 traz as que ficam de fora); as tabelas abaixo ficam com as 33 que entram no modelo, agrupadas pelo que disputam. Doze delas são escolha do cliente e estão marcadas **cliente**; as outras seguem uma chave (o pool TLS existe se, e só se, `net.https_server` existe) ou são de projeto e entram para que a página some o custo. Os tamanhos marcados **medido** saíram de `arm-none-eabi-nm -S` sobre o ELF da `pico_w_release` de 25/09/2026; os marcados **cálc.** são somas a partir das declarações; nenhum vem de build novo.

Legenda da coluna "hoje": **F** fixo na compilação; **F·layout** fixo e muda o registro persistido (exige `CONFIG_VERSION` novo e migração); **R** ajustável em runtime; **F+R** teto fixo, valor escolhido em runtime.

### 3.1 Partição da flash: o que disputa os 2 MiB

| id | hoje | valor | onde | custo por unidade | o que precisa mudar antes de virar magnitude |
|---|---|---|---|---|---|
| `fs.size` **cliente** | F (layout) | 1 MiB | `platformio.ini:54` ↔ `src/ota/ota_layout.h:40` | 1 KiB a mais de dados é 1 KiB a menos de aplicação, e vice-versa | os dois valores são escritos à mão em dois arquivos e nada confere que batem; a magnitude tem de gerar os dois |
| `hist.quota` **cliente** | F, literal | 86 % do LittleFS, três vezes (`src/StorageManager.cpp:1393, :1404, :1440`) | idem | 5,4 a 5,7 B por registro gravado (`src/SystemDefs_Network.h:351-355`); com 11 canais a 1/min, 1 MiB a 86 % são 116 dias | **não existe orçamento dedicado ao histórico**: ele disputa o FS com o log (~19 KB), dois `.lng` (~76 KB), temas, config e `/web`. A magnitude passa a ser um teto em KiB, e a retenção em dias vira número derivado que a página mostra |
| `log.records` **cliente** | F | 800 registros × 12 B, dois arquivos (`src/LogManager.h:27`; `src/LogPolicy.h:4-8`) | — | 24 B de flash por registro na janela (19.200 B no total) | só a constante; a janela de 1.600 registros que a política de log assume (`LOGPOL_REPORT_MS`, 6 % da janela) tem de ser recalculada a partir dela |
| `ota.slots` | F (layout) | 1 slot de 1020 KiB + staging **dentro** do FS (`src/ota/ota_layout.h:44-57`) | — | um segundo slot custa o tamanho da imagem inteira; ver a seção 5 | é projeto de bootloader (seção 8); a magnitude só entra no modelo para a página dizer se a imagem cabe |
| `lang.packs` **cliente** | F (arquivos em `data/lang/`) | 2 packs, 39.028 B o es-ES (`src/DisplayManager_LangParser.cpp:60`) | — | o arquivo inteiro no FS; 16 KiB de pico no load e ~2,9 KB residentes depois (`LANG_RESIDENT_MAX`, `:59`; medido) | escolher quais packs viajam é hoje copiar arquivos à mão; o es-ES está a 79 B do teto residente (99,5 %) |
| `themes.custom` **cliente** | F | 8 `.thm` (`src/Themes.cpp:302`) | — | 100 B de `.bss` por tema (800 B, medido) | `data/themes/` traz 12 arquivos e 4 somem em silêncio; a magnitude tem de bater com o que o pacote embarca |

### 3.2 Capacidade: o que disputa a RAM

| id | hoje | valor | onde | custo por unidade | o que precisa mudar antes |
|---|---|---|---|---|---|
| `sensors.slots` **cliente** | F·layout | 16 (`src/simut_config.h:349`) | idem | 143 B por slot no `SystemConfig` (flash, RAM e cada cópia transitória); ~95 B de RAM fixa por slot em caches; ~1.092 B de heap por slot **ativo** (`RuntimeSensor`); +4 B por slot em cada registro do lote de telemetria (cálc.) | literais `16` e máscaras `uint16_t` em pelo menos 8 lugares (`src/DisplayManager.h:247, :817-818, :844`; `src/DisplayManager_Alarm.cpp:32, :139`; `src/DisplayManager_Touch.cpp:512`; `src/CommandManager.cpp:765-804`; `src/AppManager_CmdHandlers.cpp:164-175`); o id do H5 (`slot*8+ch` em `uint8`) prende a ≤ 31; `csvBuf[256]` na telemetria assume 16 |
| `hist.channels` **cliente** | F (formato) | 16 no total, somados sobre todos os slots (`src/HistoryV5.h:62`) | idem | ~260 B de RAM por canal no `StorageManager` (cálc.) | `buildH5Schema` corta o excedente **em silêncio** (`src/StorageManager.cpp:2338-2341`): 16 BME280 pediriam 48 canais e gravariam 16. É o limite real do histórico, mais apertado que `sensors.slots`, e ninguém o vê |
| `hist.block` | F (formato) | 60 registros por bloco (`src/HistoryV5.h:63`) | idem | ~70 B de RAM por registro-slot com 16 canais (cálc.) | é formato de arquivo: mudar exige versão de schema |
| `users.max` **cliente** | F·layout | 32 (`src/SystemDefs_Limits.h:27`) | idem | 70 B por conta no `SystemConfig` (2.240 B em cada cópia); +5 B por conta no `DisplayManager`; +64 B de heap transitório por conta no `GET /api/users` | teto duro 32 pelo bitmap `pinMustChange` (`src/SystemDefs_Records.h:316`); o snapshot da OTA tem 1.430 B de folga, o que daria cerca de 52 contas se o bitmap crescesse |
| `web.sessions` **cliente** | F, literal | 3, escrito em 6 lugares (`src/WebManager.h:178`; `src/WebManager_Core.cpp:56`; `src/WebManager_Auth.cpp:20, :72, :472, :476, :621`) | — | ~36 B + duas `String` no heap por sessão (cálc.) | virar uma constante |
| `web.login_slots` | F | 8 IPs (`src/SystemDefs_Network.h:199`) | idem | ~160 B por slot (1.280 B, cálc.) | — |
| `web.rate_slots` | F | 16 IPs (`src/SystemDefs_Network.h:191`) | idem | 12 B por slot | — |
| `tel.batch_max` **cliente** | F+R | 250, escrito em 4 lugares (`src/simut_config.h:546`; `src/TelemetryManager.cpp:628`; `src/WebManager_Commit.cpp:1037`; `src/AppManager_Commands.cpp:518`) | — | 70 B por registro no vetor + 350 B (JSON) ou 160 B (CSV) de heap por registro no payload | o campo é `uint8`; o teto real já é a fórmula do heap (`HEAP_RESERVE` 32 KiB com TLS, `:626-627`), não a constante |
| `alarms.queue` **cliente** | F+R | 64 posições sempre alocadas, `queueMax` 1..64 (`src/SystemDefs_Records.h:227-229`; `src/AlarmQueue.h:158`) | — | 32 B por registro: 2.048 B no `TelemetryManager` **independentemente de `queueMax`**, mais 2 KB de pilha em `ack()` | a magnitude só economiza RAM se o array passar a ter o tamanho escolhido |
| `syslog.ring` | F | 8 linhas × 256 B (`src/SyslogManager.h:64-65`) | idem | 256 B por linha; 2.048 B **sempre** alocados, mesmo com syslog desligado (medido: `sizeof(SyslogManager)` 2.136 B) | com a chave `syslog` desligada, o objeto tem de sumir |
| `lwip.pbuf_pool` | F (patch) | 24 (`tools/arduino_pico_overrides/patched_headers/lwipopts.h:60`) | idem | ~1.532 B por pbuf: **36.771 B de `.bss`, o maior consumidor da imagem** (medido) | o valor foi escolhido para seis conexões a 4×MSS; cai junto com `lwip.tcp_pcb` e com as chaves de rede que somem |
| `lwip.mem` | F | 16 KiB (`lwipopts.h:34`) | idem | 16.403 B de `.bss` (medido) | — |
| `lwip.tcp_pcb` / `lwip.udp_pcb` | F | 5 / 7 (`lwipopts.h:110, :109`) | idem | ~165 B por PCB TCP; 283 B o pool UDP (medido) | reduzir UDP quebra o mDNS (`tools/arduino_pico_overrides/README.md`); TCP = web + telemetria + MQTT ao mesmo tempo |
| `lwip.tcp_wnd` | F (patch) | 4 × 1460 (`lwipopts.h:82-83`) | idem | ~4 pbufs por conexão | a memória do projeto: 4×MSS zerou as falhas da tempestade de rede; não é knob de cliente |
| `tls.server_pool` | F (só com certificado) | 16.709 + 1.024 B (`src/WebManager_Core.cpp:293`; `bearssl_server_static_pool.patch:32`) | — | ~21,5 KB de heap reservados uma vez no boot; **uma** conexão TLS por vez | segue a chave `web.https`; o número é o tamanho de um registro TLS e não se escolhe |
| `tls.client_ctx` | F | 2 contextos (`src/TelemetryManager.h:248, :259`) | idem | "~16 KB" cada, segundo o comentário do header; na prática o iobuf é limitado a 4.096 + 512 por conexão (`:208, :932, :2664`), mais ~10 KB de scratch do BearSSL a cada reconexão (`:603`) | seguem as chaves de telemetria segura; o MQTTS e o HTTPS de telemetria poderiam partilhar um contexto |
| `tft.canvas_rows` | F | 45 linhas de 320 px (`src/DisplayManager.cpp:1209`) | idem | 640 B por linha: 28.800 B de heap residente (cálc.) | trocar por menos linhas custa mais passadas de render; o comentário em `:1209` diz por que 45 |
| `tft.graph_width` | F | 200 pontos (`src/SystemDefs_Limits.h:77`) | idem | ~100 B por ponto entre `.bss`, residente e transitório (cálc.); `_graphData` (~5,9 KB) existe até no alpha, que não tem gráfico | segue a chave `display.graph` |
| `core1.stack` | F | 8 KiB (`src/DisplayManager.cpp:228`) | idem | 8.192 B de `.bss` (medido); o SDK ainda reserva 2 KiB em SCRATCH_X sem uso | segue a chave `display.tft`; a história de "8 KB terminam a roleta" está no comentário |
| `ota.buffers` | F | applier 8.192 + validação 4.128 + restore 4.096 + 400 (`src/ota/applier.cpp:127`; `validation.cpp:100`; `restore.cpp:70-72`) | — | ~16,8 KB de `.bss` **permanentes** para algo que roda uma vez por atualização (medido) | alocar sob demanda devolve o mesmo à folga de heap que o TLS pede |
| `web.upload_buf` | F | 8 KiB (`src/WebManager.h:159`) | idem | 8.192 B sempre residentes | segue a chave `web.files`; o tamanho troca pausas do Core 1 por RAM |
| `calib.points` | F | 5 por curva (`src/sensors/CalibCurve.h:39`) | idem | 4 + 12 B por ponto; 512 B de heap por sensor ativo com 8 canais (cálc.) | segue a chave `sensors.calib_curves` |
| `calib.changes` | F | 18 por `POST` (`src/WebManager_Calib.cpp:344`) | idem | 156 B por entrada: 2.808 B de `.bss` (medido) | idem |
| `wifi.saved_nets` **cliente** | F·layout | 1 rede, senha ≤ 31 chars (`src/SystemDefs_Records.h:353-354`) | idem | 64 B por rede no config | WPA2 aceita 63; hoje truncado por escolha de layout |
| `wifi.scan_nets` | F | 12 (`src/SystemDefs_Network.h:128`) | idem | 36 B por rede | segue a chave `net.wifi_scan` |
| `lang.resident` | F | 16 KiB de pico no load (`src/DisplayManager_LangParser.cpp:59`) | idem | o es-ES ocupa 99,5 % | não é knob: é o tamanho do pack |
| `log.pending` | F | 32 registros (`src/LogManager.h:315-316`) | idem | 12 B por registro | — |

### 3.3 Política: valores que o cliente escolhe, mas em runtime

Estes já são configuráveis pela web ou pela CLI e não precisam entrar na compilação; entram no modelo só porque o simulador deve mostrar o efeito (um `h_int` de 1 min é o período de wake do Air, por exemplo).

| id | valor hoje | faixa | onde |
|---|---|---|---|
| `hist.interval` | 1 min | 1..1440 min | `src/SystemDefs_Records.h:531-533`; overlay `reserved[48..51]` |
| `tel.batch` (`t_bat`) / `tel.min_batch` (`t_int`) | 250 / 0..20000 registros | `src/simut_config.h:533-546` |
| `alarms.queue_max` (`a_qmax`) | 32 | 1..64 | `src/SystemDefs_Records.h:227-229` |
| `mqtt.keepalive` (`m_ka`) | 60 s | 10..300 | `src/WebManager_Commit.cpp:1060` |
| `air.idle` | 300 s | 10..65535 | `src/air/AirConfig.h:254-256` |
| `pin.min_len` / `pin.keypad` | 4 / 16 dígitos | 4..16 | `src/SystemDefs_Validate.h:216-217`; `src/PinKeypad.h:111-113` |
| `web.keepalive` | ligado | opt-out | `src/SystemDefs_Records.h:486` |
| `syslog.server` / `syslog.level` | — | IPv4 só | `src/SystemDefs_Records.h:586-601` |

### 3.4 O que o levantamento achou de errado nas magnitudes

Cada linha é um defeito de hoje, independente do modelo, e cada uma vira item do plano da seção 6:

- **`sampleIntervalMs` (`s_int`) é validado, gravado e ecoado, e nenhum código o lê** (`src/StorageManager.cpp:574`; `src/WebManager_Commit.cpp:979`; `src/WebManager_Api.cpp:231`). É um knob morto exposto ao usuário.
- **A retenção do histórico não tem orçamento.** É "86 % do que sobrar", três vezes como literal.
- **O teto real do histórico é `H5_MAX_CHANNELS`, e o corte é silencioso.**
- **Quatro cópias do 250, seis do 3, oito do 16.** Nenhuma delas é uma constante que uma magnitude possa gerar.
- **RAM gasta sem a chave ligada:** fila de alarmes (2 KB), ring de syslog (2 KB), buffers de OTA e restore (16,8 KB), `_graphData` no alpha (5,9 KB). Somados, cerca de 27 KB de `.bss` que existem em toda imagem e servem a recursos que nem toda imagem tem.
- **Três comentários mentem sobre número:** o `.wip` "a cada 10 min" (`src/StorageManager.cpp:269-270`; hoje é a cada registro), a "margem ~3,4 KiB" do snapshot (`src/ota/config_snapshot.cpp:27`; real 1.430 B) e "mDNS: negligible flash cost" (`src/simut_config.h:285`; medidos 15.376 B em `platformio.ini:225`).
- **A trava `sizeof(TouchCalData) <= 24`** (`src/SystemDefs_Records.h:195`) é mais fraca que a fatia real de 12 B do `reserved[]`, e não detectaria sobreposição com a configuração de som.

---

## 4. As regras

Cada regra tem um id, o enunciado como o manifesto o escreveria, a evidência e
a **força** que a sustenta hoje (1.4). As de força **build**, **script** ou
**runtime** já são impostas por alguma máquina; as outras são o que o modelo
tem de passar a impor, ou a mostrar. As marcadas **inferida** foram deduzidas
do código nesta análise e ninguém as mediu: valem como hipótese até uma
medição, e o P8 as põe na lista dos pares de risco.

### 4.1 Escolhas e display

| id | regra | evidência | força |
|---|---|---|---|
| R-D1 | `display` é exatamente um de {`tft`, `alpha_paralelo`, `alpha_i2c`, `nenhum`} | "Choose ONE" (`src/simut_config.h:54`); os pinos em disputa (R-H2) | documento |
| R-D2 | `tft.graph`, `tft.calendar`, `tft.users_admin`, `tft.mirror`, `tft.themes_fs`, `tft.theme_packs`, `tft.license_text`, `panel.pin`, `net.ap_gesture`, `bench.ui_study` requerem `display = tft` | `platformio.ini:467-480, :530-545`; `src/SystemDefs_Limits.h:44-48` | script (o filtro), só nos ambientes que existem |
| R-D3 | `tft.calendar` requer `tft.graph` | o calendário escolhe o dia do gráfico (`src/AppManager_Events.cpp:185-260`) | documento |
| R-D4 | `tft.users_admin` requer `panel.pin` | administrar contas exige identidade (`src/DisplayManager_Users.cpp`) | documento |
| R-D5 | `display = tft` sem `panel.pin` precisa de uma política para o menu do painel: só leitura, ou aberto | não existe: hoje `SIMUT_PANEL_PIN` é derivada e a combinação não é possível | decisão de produto |
| R-D6 | `display = tft` implica o Core 1, 8 KiB de pilha, 28.800 B de canvas, 1 alarme de hardware, o SPI0 (GP16, 18, 19, 28, 17, 20), de 1 a 3 canais de DMA e a IRQ do GP20 no NVIC do Core 1 | `src/DisplayManager.h:3-5`; `src/DisplayManager.cpp:228, :1106-1167, :1209`; `src/DisplayManager_Dashboard.cpp:243-247` | documento |
| R-D7 | `display = alpha_*` implica o Core 1 (o `loopCore1` do Alpha) e a mesma pilha de 8 KiB | `src/DisplayManager_Alpha.cpp:82-99` | documento |
| R-D8 | `display = nenhum` hoje só existe dentro de `power.air`; fora dele a combinação tem nome e não compila | `DisplayManager_None.cpp:20` sob `SIMUT_AIR`; `DisplayManager.cpp` trata `!SIMUT_DISPLAY_ALPHA` como TFT; `src/simut_config.h:397-398` | inferida |
| R-D9 | `tft.mirror` implica `custom_web_omit` sem `tft` no gerador de páginas; hoje nada liga os dois | `platformio.ini:449-453`, só comentário | documento |
| R-D10 | `bench.concurrency_asserts` requer `display ≠ nenhum` | a ponte `simutStateMutexHeldByCurrentCore` só existe em `src/DisplayManager.cpp:200-206`, que o Air exclui | inferida |
| R-D11 | `bench.mirror_probe` requer `tft.mirror`; `bench.cli_panel` requer `cli.full` e `display = tft`; `web.api_bench` (`/api/keypad`) requer `panel.pin` | `src/WebManager_History.cpp:1911`; `src/CommandParser.cpp:150-170, :196` | documento |

### 4.2 Hardware: pinos, PIO, DMA, barramentos, cores

| id | regra | evidência | força |
|---|---|---|---|
| R-H1 | Um dono por GPIO; sensores só em GP0–15; BME/BMP ocupa dois pinos, então cabem no máximo oito | `src/WebManager_Commit.cpp:700-711`; `src/AppManager_CmdHandlers.cpp:45-79`; `docs/WIRING.md:173-180` | runtime (o validador da config) |
| R-H2 | `display = tft` conflita com `alpha_i2c` em GP26/27 (RST × SDA, DC × SCL) e com `alpha_paralelo` em GP16–20 (MISO × RS, TOUCH_CS × EN, SCK × D4, MOSI × D5, TOUCH_IRQ × D6) | `src/simut_config.h:84-100, :208-238` | documento: não há `#error` nem `static_assert` de pino |
| R-H3 | `power.air` conflita com `display = tft` e `alpha_paralelo`: GP16 e GP17 são o power dos sensores e o carregador | `src/simut_config.h:429-438`; `platformio.ini:518-519` | script (o ambiente força `TFT=0`) |
| R-H4 | GP23, 24, 25 e 29 são do CYW43 em todo perfil; `airPinValid` barra esses pinos só para os pinos do Air, e os slots de sensor ficam fora deles pela regra dos 0–15 | `src/air/AirConfig.h:229-243`; `boards/pico_w.h:120-155` | runtime, parcial |
| R-H5 | No TFT, GP8 e GP9 são ao mesmo tempo a UART1 do log de boot e os slots 8 e 9; o pino só volta ao sensor quando o driver o re-muxa | `src/AppManager_Boot.cpp:42-48, :66-70, :111`; `docs/WIRING.md:212` diz "0–15 = slots" | nenhuma; não está documentado |
| R-H6 | GP0 é o estacionamento de PIO no boot: o DHT nos quatro perfis, o 1-Wire no TFT; quem usa GP0 depende de re-muxar depois | `sensors/DHT22Driver.h:53-57`; `src/simut_config.h:333-339` | documento |
| R-H7 | `sensors.scan` percorre GP0..GP16 com `gpio_init` e PIO em cada pino: invade o MISO do TFT, o RS do Alpha ou o power do Air; depois do scan, BMEs em I2C de hardware ficam em SIO até o reboot | `src/SensorManager.cpp:393-394, :463-464, :788, :130-131`; `Wire.cpp:119-123` do framework | inferida |
| R-H8 | Na bancada TFT a PicoHand fica em HIZ e com a sonda desarmada em GP16/17, ou a leitura da GRAM sai corrompida | `AGENTS.md` §1 | documento (bancada) |
| R-H9 | `sensors.ds18b20` reivindica uma máquina em `pio0` (27 instruções) e `sensors.dht22` uma em `pio1` (17), sem fallback; se a reivindicação falha, a família inteira fica desligada naquele boot | `src/SensorManager.cpp:68-86`; `OneWirePIO.cpp:40-58`; `DHTBus.cpp:38-56` | runtime (log `ERR_SENSOR_MISSING`, ctx = bloco) |
| R-H10 | A quantidade de DS18B20 e de DHT22 não é limitada pelo PIO (uma máquina por família); é limitada por GPIO | `src/SensorManager.h:177-180` | documento |
| R-H11 | Nos perfis TFT e alpha, `pio1` termina cheio (buzzer 2 + DHT 1 + CYW43 1) e sobram 3 máquinas e 5 instruções em `pio0` e 5 instruções em `pio1`: **nenhum programa novo de 6 instruções ou mais cabe** | contas sobre `pio.c:66-80` do SDK e a ordem de boot buzzer → 1-Wire → DHT → rádio | inferida |
| R-H12 | Qualquer reivindicação nova em `pio1` antes do rádio (1 máquina, ou 6 instruções ou mais) **derruba o Wi-Fi e o Bluetooth**: o CYW43 reivindica por último e precisa de 1 máquina + 6 instruções contíguas, e em `pio0` só há 5 | `cyw43_bus_pio_spi.c:36-37, :119-122`; `pio.c:424-457` | inferida |
| R-H13 | `sound.buzzer` requer 2 máquinas no mesmo bloco (4 instruções), 1 spinlock e o GP22; cabe em `pio0` como fallback | `BuzzerPIO_RP2040.cpp:67-81, :179-213`; `src/SoundManager.h:104-106` | documento |
| R-H14 | `sensors.bmx280` fora de par nativo nunca ganha PIO: o WirePIO precisa das 32 instruções de um bloco vazio, e o 1-Wire já ocupou 27 em `pio0` antes de `initRuntimeSensors`; resultado, bit-bang com ~1,6 ms de IRQ desligada por transação | `src/AppManager_Boot.cpp:1033-1036`; `src/SensorManager.cpp:239-247` | inferida |
| R-H15 | Spinlocks: dos 32, só 8 são reivindicáveis; TinyUSB usa 1, o buzzer 1, e cada BME/BMP vivo 1: **no máximo 6 BME nos perfis TFT e alpha, 7 no Air**; o seguinte dá `panic` em `critical_section_init`, e no `setup()` o watchdog está desligado, então o aparelho trava | `BMx280PIO_RP2040.cpp:129, :139, :147`; `sensors/BME280Driver.h:45-57`; `src/main.cpp:35` | inferida |
| R-H16 | DMA: o TFT usa até 5 dos 12 canais (rádio 2, blit 1, leitura da GRAM 2); alpha e Air 2; os caminhos do TFT caem para o modo sem DMA se faltar canal | `src/DisplayManager_Dashboard.cpp:243-247, :279-318`; `src/DisplayManager_Auth.cpp:145-214` | documento |
| R-H17 | O SPI0 tem um dono por vez: o Core 1 (render e toque) ou o Core 0 (leitura da GRAM), este só com o Core 1 estacionado; o MISO é partilhado com o XPT2046 | `src/DisplayManager_Auth.cpp:138-141`; `src/DisplayManager.h:297-299` | documento |
| R-H18 | O `clk_peri` provável é 48 MHz, não 125: o teto real do SPI0 seria 24 MHz e os "62,5 MHz" configurados não são alcançáveis; a escada medida pelo projeto (8 ≈ 10; 12 ≈ 16 ≈ 20 MHz) bate com 48 MHz | `src/simut_config.h:107-134`; `clocks.c:353-399` do SDK com `PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK=0` | inferida; medir com osciloscópio fecha |
| R-H19 | BME em I2C de hardware exige SDA e SCL no papel certo do par nativo; trocados, passam no teste de máscara e dão `panic` em `Wire.setSDA`, e no boot isso trava o aparelho | `sensors/SensorHelpers.h:257-269`; `Wire.cpp:57-80`; achado do manual v2.7.1 ("SDA/SCL trocados travam o boot") | incidente |
| R-H20 | Um par de pinos por controlador I2C: um segundo par que mapeia para o mesmo I2Cn reusa o `Wire` já iniciado e lê o sensor do primeiro | `src/SensorManager.cpp:218-237` | inferida |
| R-H21 | `alpha.mode = i2c` conflita com `sensors.bmx280` em par I2C1: o `Wire1` já roda em GP26/27 pelo LCD no Core 1, e o `setSDA` com o barramento ativo aborta | `display/HD44780_16x2.h:72-75`; `Wire.cpp:67-80` | inferida |
| R-H22 | Alarmes de hardware: o TFT usa 2 dos 4 (o pool padrão e `core1WaitUs`), alpha e Air usam 1; o pool de 16 timers é partilhado com USB, CYW43, o buzzer (um por nota) e o `SendGuard` | `src/DisplayManager.cpp:1106-1167`; `src/WebManager_Core.cpp:525-545`; `BuzzerPIO_RP2040.cpp:439` | documento |
| R-H23 | Todo display exige o Core 1; o Air não o lança; com o Core 1 vivo, **toda escrita em flash exige `Core1FlashPause`**, porque nem o LittleFS nem o BTstack do framework pausam o outro núcleo | `src/StorageManager.cpp:43-47, :77-90`; `LittleFS.cpp:187-211`; `RP2040Support.h:63-67`; `docs/CONCURRENCY.md:10-13` diz o contrário e está errado | documento |
| R-H24 | `net.bluetooth` com `display ≠ nenhum`: a gravação da TLV do BTstack roda sem `Core1FlashPause` | `btstack_flash_bank.cpp:53-56` do framework | inferida; par de risco do P8 |
| R-H25 | A FIFO entre os núcleos é do `multicore_lockout`; nenhum recurso a usa para dados | `src/DisplayManager.cpp:210-217, :244-268` | documento |
| R-H26 | `power.air` em wake só de leitura não sobe o rádio: 0 máquinas PIO e 0 DMA de rádio; o Bluetooth só existe em M0 | `src/AppManager_Boot.cpp:1094-1116, :1192-1205` | documento |
| R-H27 | O registrador scratch[7] do watchdog tem três donos: o payload do SOFT PANIC, o relógio que o Air carrega pelo sono e o `hp=` do handler web; o mapa em `LogManager.cpp` lista só os dois primeiros | `src/LogManager.cpp:664-670`; `src/AppManager.h:253`; `src/WebManager_Core.cpp:575-581`; `src/WebManager_Api.cpp:19` | documento, incompleto |

### 4.3 Software: o que requer o quê

| id | regra | evidência | força |
|---|---|---|---|
| R-S1 | `sizeof(SystemConfig)` é o mesmo em toda imagem: um recurso corta código, nunca campos; uma magnitude F·layout muda a **família de esquema** (P3.3) | `src/SystemDefs_Limits.h:31-37`; `sizeof(SystemConfig) == CFG_V25_BLOB` e as caudas em `src/ConfigMigrate.h:81-102` | build |
| R-S2 | Ao menos um de `sensors.ds18b20`, `sensors.dht22`, `sensors.bmx280` está ligado | sem sensor o produto não mede; nada confere | nenhuma |
| R-S3 | `sensors.scan` requer `sensors.ds18b20` | a varredura é 1-Wire por dentro (`src/AppManager_Sensors.cpp`) | documento |
| R-S4 | `sensors.calib_curves` exige NTP sincronizado para gravar (503 sem ele) | manual §4 | runtime |
| R-S5 | `net.bluetooth` requer, juntos: `-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH`, `BluetoothManager.cpp` no filtro, `SerialBT` fora do `lib_ignore`, e um ambiente que não herde o `lib_ignore` da base, porque **um ambiente que declara `lib_ignore` substitui a lista em vez de estender** | `platformio.ini:60-79, :143-149, :481-484`; o LDF segue o `#include <SerialBT.h>` mesmo sob `#if 0` | build (o link morre em `_needsbt.h`: "This library needs Bluetooth enabled") |
| R-S6 | `net.bluetooth` implica 8 KiB de flash gravável (a TLV do BTstack) linkados logo depois da imagem, dentro do slot: o teto efetivo da imagem cai 8 KiB, e o `check_flash_budget.py` compara o `.bin`, que não os contém | `btstack_flash_bank.cpp:15`; `nm` do alpha põe `__bluetooth_tlv` em `0x100EB000` | inferida; confirmar no mapa do linker |
| R-S7 | `net.https_server` e `tel.tls_client` implicam `tls.lib` (BearSSL + `WiFiClientSecure`, ~114 KB); a biblioteca só sai quando os dois saem; o servidor sozinho vale ~20 KB | `DIETA_FLASH.md` §2; CHANGELOG v2.2.5 | documento |
| R-S8 | `net.https_server` só ativa com o par de certificado no `/config`, e só então reserva o pool de ~21,5 KB; sem par, serve HTTP | `src/WebManager_Core.cpp:41-42, :293, :408-410` | runtime |
| R-S9 | `auth.strong_password` hoje só existe sob `net.https_server`; a regra alvo é a independência | `src/WebManager_Auth.cpp:722-733, :783-794` | defeito |
| R-S10 | `net.mdns` requer 7 PCBs UDP do lwIP (reduzir quebra o mDNS) e conflita com `power.air` (nunca anunciou; desligado desde a v2.4.4-beta) | `tools/arduino_pico_overrides/README.md`; `platformio.ini:527-529`; issue #118 | documento |
| R-S11 | Em `fleet.hooks`, só o TXT do mDNS requer `net.mdns`; o resto (CORS, Bearer, `X-SIMUT-*`, `_dry`, etiqueta) não requer nada | `src/NetworkManager.cpp:836-872` | documento |
| R-S12 | `net.ap_auto`, `net.captive_dns`, `net.ap_gesture` e `bench.ap_open` requerem `net.ap`; `net.ap_gesture` requer também `display = tft`; `power.air` implica `!net.ap_auto` | `src/NetworkManager.cpp:104-196, :675-740`; `src/AppManager_Boot.cpp:491` | documento (o Air, script) |
| R-S13 | `tel.tls_client` requer `tel.http` ou `tel.mqtt`; `tel.mqtt` requer `tel.http` (lote e cursor partilhados); `tel.csv_mode`, `tel.custom_payload` e `web.page_tel` requerem `tel.http`; `tel.alarm_line` requer `alarms.limits` e `tel.http`; `tel.ha_discovery` requer `tel.mqtt` | `src/TelemetryManager.cpp:183-253, :1082-1227, :2418-2786` | documento |
| R-S14 | `tel.ha_discovery` exige em runtime MQTT, payload JSON e lote ≤ 5 | `src/TelemetryManager.cpp:292, :1084`; manual cap. 23 | runtime |
| R-S15 | `tel.alarm_line` por MQTT exige `t_int > 0` | manual cap. 22 | documento |
| R-S16 | `alarms.fault`, `alarms.maintenance` e `web.page_alarms` requerem `alarms.limits`; `alarms.maintenance` mantém `MaintConfig` no esquema mesmo desligada (R-S1) | `src/AppManager_HistoryAlarm.cpp:548-724`; `src/SystemDefs_Records.h:262-335` | documento |
| R-S17 | `history.web` requer `/api/ls` e `/download` da FILE (núcleo): a página HIST os usa para os dias e para os `.h5` | `WebUI.h:894+`; `tools/check_authz.py --list` | documento |
| R-S18 | `log.syslog` requer Wi-Fi em modo estação: com o AP no ar nada sai | manual cap. 25 | documento |
| R-S19 | `web.metrics` partilha o bloqueio de login (`Basic` conta como tentativa) | `src/WebManager_Metrics.cpp:48, :65` | documento |
| R-S20 | `power.air` requer `cli.full` ou `net.bluetooth`: é a única interface local do Air | `AGENTS.md` §3; `platformio.ini:523` | documento |
| R-S21 | `power.air` implica `display = nenhum`, `!sound.buzzer`, `!tft.*`, `!net.mdns`, `!net.ap_auto`, e recusa `_dry` e `_nosave` no `commit_all` | `platformio.ini:509-550`; `src/SoundManager.h:71`; `src/WebManager_Commit.cpp:363-367` | script (ambiente e filtro) + código |
| R-S22 | `air.charger_sense` requer `power.air`, e o pino não pode ser GP23, 24, 25 ou 29 | `src/air/AirConfig.h:229-243` | runtime |
| R-S23 | `cli.bilingual` requer um `i18n.pack_*` no FS (com pack não inglês o `help` vem do `.lng`) | `AGENTS.md` §3; `HelpLicenseEN.h` | documento |
| R-S24 | `tft.license_text` em stub não cobre a página; `license.web` fica em todo perfil porque `check_license.py` exige as cinco cópias iguais | `tools/check_license.py`; `HelpLicenseEN.h:236` | script |
| R-S25 | `i18n.pack_*`: cada pack ≤ 49.152 B de arquivo e ≤ 16.384 B de prefixo residente; o es-ES está a 79 B do teto | `tools/check_lang_packs.py:42-53, :263-330`; `src/DisplayManager_LangParser.cpp:59-60` | script |
| R-S26 | `storage.backup_restore`: o restore aceita no máximo 200 arquivos e 4 KB de caminhos; uma `hist.quota` maior pode voltar a estourar esse pool | `src/ota/restore.cpp:65-72` | documento |
| R-S27 | `net.https_server` é `#ifdef`: `-DSIMUT_WEB_HTTPS=0` **liga** o servidor; `CMD_HTTPS_OFF` existe em todo perfil, com ou sem servidor | `src/WebManager_Core.cpp:39`; `src/CommandParser.cpp:306`; `src/AppManager_Commands.cpp:737` | defeito |
| R-S28 | `cli.full` e `power.air` são as únicas macros que `check_cli_help.py` sabe remover; `SIMUT_PANEL_PIN` e `SIMUT_UI_STUDY` são vistas como sempre presentes | `tools/check_cli_help.py:101-122` | script, com o furo |
| R-S29 | `AppManager_Air.cpp` e `DisplayManager_None.cpp` testam `SIMUT_AIR` antes de qualquer `#include`: ligar o Air só pelo `simut_config.h` os deixa vazios | `src/AppManager_Air.cpp:23`; `src/DisplayManager_None.cpp:20` | inferida |
| R-S30 | Toda imagem requer o framework com os patches 2f (`enableKeepAlive`) e 2i (`setCorsOrigin`), chamados sem `#if`; `net.https_server` requer também o 2h (`simut_reserve_tls_server_pool`); o 2d tem de ser aplicado antes do 2g | `src/WebManager_Core.cpp:47, :102, :233, :261`; `tools/arduino_pico_overrides/patch.sh:260-261` | build (link) |
| R-S31 | `display = alpha_*` exclui `DisplayManager_i18n.cpp` e `DisplayManager_LangParser.cpp` porque o Alpha **redefine** `trlLookup`, `logcodeLookup` e `findAndLoadLangFile` (linkar os dois duplica símbolos); `display = nenhum` os mantém porque a web e a CLI os usam | `src/DisplayManager_Alpha.cpp:610-615`; `src/DisplayManager_LangParser.cpp:352-367`; `src/DisplayManager_None.cpp:6-9` | build (link) |
| R-S32 | Um perfil derivado que muda a CLI re-deriva `build_flags` da base em vez de anexar: anexar deixa `SIMUT_CLI_FULL=0` e `=1` na mesma linha de comando | `platformio.ini:203-205` | documento; vira regra do gerador |
| R-S33 | `WebUI.h` nunca é incluído por uma unidade do firmware (é o único `#error` de `src/`), salvo com `SIMUT_WEBUI_BUILD_TIME_ONLY` | `WebUI.h:31-34` | build |

### 4.4 RAM e rede: os limiares

| id | regra | evidência | força |
|---|---|---|---|
| R-N1 | `.data` + `.bss` + heap + pilhas ≤ 256 KiB do banco principal; `.bss` do release é 109.276 B e o heap 144.896 B (medido); no alpha, com Bluetooth, o heap cai para 131.952 B | o linker ("region RAM overflowed"); `nm` de 25/09 | build |
| R-N2 | O TLS cliente exige heap livre ≥ 24.576 B antes de um ciclo e reserva 32.768 B ao montar o lote; sem o limite o cliente pediria ~16,7 KB contíguos, e o firmware o prende a `setBufferSizes(4096, 512)` (só o cliente oferece *max-fragment-length*; o servidor não pode ser limitado) | `src/TelemetryManager.cpp:188-208, :397, :622-627, :2502` | runtime (o ciclo é adiado) |
| R-N3 | O servidor HTTPS reserva ~21,5 KB de heap no boot, só se o par de certificado existe, e aceita **uma** conexão TLS por vez (16.709 + 1.024 B por `accept`). A história tem duas voltas: sem pool (v2.3.0-beta), `accept` + cliente de telemetria levavam ao watchdog; com o pool em `.bss` (v2.3.0), a telemetria ficava sem heap; desde a v2.3.1-beta ele é reservado no boot e só com HTTPS. Se a reserva falhar, os `accept` voltam ao heap e o risco volta | `src/WebManager_Core.cpp:256-293`; `CHANGELOG.md` v2.3.0 e v2.3.1 | incidente (resolvido; a regra fica) |
| R-N4 | O que decide o TLS é o **maior bloco contíguo**, não o heap livre: tirar o Bluetooth levou o bloco de 11.483 B para 35.776 B com +16,6 KB de heap | `ANALISE_FLASH_RAM.md` §1 (snapshot, medido no ferro) | incidente |
| R-N5 | lwIP: 5 PCBs TCP para web, telemetria e MQTT juntos; 24 pbufs (36.771 B de `.bss`) dimensionados para 6 conexões a 4×MSS; 7 PCBs UDP; ligar mais consumidores de conexão sem subir os pools devolve as falhas da tempestade de rede (4×MSS foi o que as zerou) | `patched_headers/lwipopts.h:34-36, :60, :82-87, :109-110`; `docs/netstorm-campaign-2026-08-10/RELATORIO.md` | incidente |
| R-N6 | `tft.mirror` pede 26.624 B de heap transitório por quadro e 15.360 B por captura; com `tel.tls_client` ativo, a folga tem de cobrir os dois ao mesmo tempo | `src/WebManager_History.cpp:1622-1623, :1857-1860, :1927-1940` | cálc.; par de risco do P8 |
| R-N7 | O pack de idioma pede 16.384 B de heap no load e mantém ~2,9 KB depois; só carrega com idioma diferente do inglês (era carregado sempre até a v2.3.4-beta) | `src/DisplayManager_LangParser.cpp:59, :268, :341`; `src/AppManager_Boot.cpp:944-949` | documento |
| R-N8 | Qualquer recurso novo no Core 1 tem de caber nos 8 KiB de pilha; com 2 KiB o display inteiro estourava | `src/DisplayManager.cpp:218-228`; commit `a3324b4` | incidente |

### 4.5 Flash e partição

| id | regra | evidência | força |
|---|---|---|---|
| R-F1 | O `.bin` de todo perfil publicável ≤ 1.040.384 B: uma imagem entre 1016 e 1020 KiB tem a cauda nos setores do snapshot | `src/ota/ota_layout.h:42-56`; `tools/check_flash_budget.py`; `src/ota/validation.cpp:75` | script + runtime |
| R-F2 | Com um slot, `fs.size` ≥ o `.bin`: o staging da OTA é a partição do FS | `src/ota/ota_layout.h:57, :69-71` | nenhuma hoje (1 MiB ≥ 1020 KiB sempre valeu) |
| R-F3 | Com `ota.dual_slot`: 2 × slot + `fs.size` + 4 KiB ≤ 2 MiB, e o `.bin` ≤ slot − 4 KiB; duas imagens linkadas em endereços diferentes | conta sobre o layout (5.2) | cálc. |
| R-F4 | No FS: `hist.quota` + log (19.200 B) + packs + temas + config + `/web` + reserva ≤ `fs.size`; hoje o histórico é "86 % do que sobrar" | `src/StorageManager.cpp:1393-1440` | documento |
| R-F5 | O orçamento de flash de cada perfil publicável é uma marca d'água: sobe no mesmo PR que o faz subir, com o número no commit | `tools/flash_budget.json`, `_comment` | script |
| R-F6 | `custom_fs_pages` é proibido em perfil publicável, porque o stage da OTA grava por cima do FS e a página sumiria a cada atualização; o Air está fora da lista e não deveria | `tools/build_webui_gz.py:161, :191-198` | script, com o furo |
| R-F7 | **É o stage, não o apply, que destrói o FS**: a imagem é gravada por cima da partição, um stage confirmado deixa o LittleFS desmontado até o apply ou o reboot, e a checagem de variante roda depois de o FS já ter sido gravado; histórico, packs, temas, `calib.csv` e `air.bin` não sobrevivem, e só o `system.bin` atravessa, pelo snapshot. A retenção prometida é "até a próxima atualização" (`docs/OTA_USAGE.md:46` atribui isso ao apply e precisa ser corrigido) | `src/ota/validation.cpp:92-94`; `src/WebManager_Ota.cpp:320-350`; `src/ota/config_snapshot.cpp:32` | documento |
| R-F8 | A folga de OTA anda em degraus de 4 KiB: 601 B de conteúdo custaram 4.096 B ao Air | `tools/flash_budget.json` (2026-09-20) | incidente |
| R-F9 | `SIMUT_ENV_NAME` vale "release" em `pico_w_release`, `test`, `test_https`, `asserts` e `uistudy`, então a validação de OTA aceita cruzar entre as cinco; e uma imagem sem etiqueta é aceita | `src/simut_config.h:378-399`; `src/ota/validation.cpp:92-129` | runtime, com o furo; o P5 o fecha |

### 4.6 Perigos em runtime

Não impedem o build; são o que o painel de maturidade mostra.

| id | perigo | recursos | evidência |
|---|---|---|---|
| R-R1 | Com o AP no ar o aparelho **não mede**: `_isApMode` pula sensores, alarmes e histórico | `net.ap` | manual cap. 9; achado da v2.7.1 |
| R-R2 | `net.ap_auto` numa instalação sem Wi-Fi: o aparelho abre o AP em ~7 min, para de medir e reinicia a cada 15 | `net.ap_auto` | manual cap. 9 ("não use a v2.7.1 a v2.7.3 num aparelho que vai funcionar sem Wi-Fi") |
| R-R3 | `sensors.scan` invade o GP16 (R-H7): no TFT corrompe o MISO até o Core 1 relançar; no Air, mexe no power dos sensores | `sensors.scan` × `display = tft`, `power.air` | inferida |
| R-R4 | Um payload de telemetria acima de ~15,8 KB deixava o guarda do watchdog sem alimentar; o lote é limitado pela fórmula do heap, não pelo 250 | `tel.http` | memória da bancada (TelemetryGuard); `src/TelemetryManager.cpp:622-627` |
| R-R5 | Fila de alarmes cheia: a borda recusada some e a fila guarda os mais antigos | `tel.alarm_line` | issue #161, aberta |
| R-R6 | A CLI corta em 63 caracteres e responde OK: templates de payload e da linha de alarmes só inteiros pela web | `cli.full` × `tel.custom_payload`, `tel.alarm_line` | B12, `PLANO_STABLE.md` |
| R-R7 | O keep-alive ocioso corta ~7,1 % das respostas | `web` | B13, `PLANO_STABLE.md` |
| R-R8 | Roteadores matam fluxos longos na porta 80: OTA e uploads grandes vão pela porta alternativa | `ota.web`, `web.files` | `docs/OTA_USAGE.md`; memória da bancada |
| R-R9 | No Air, alarmes, OTA, web e Bluetooth só existem em M0; um `air stop` pelo Bluetooth não funciona em M1 | `power.air` | manual cap. 20; `CLI-Manual.md` §9.1 |
| R-R10 | 6 falhas de PIN trancam a conta e 20 trancam o painel até o reboot; 8 toques ocupam ~360 ms do Core 0 | `panel.pin` | `AGENTS.md` §1; `AUTHORIZATION.md` |
| R-R11 | Três sessões web; a quarta pessoa é recusada; um login novo da mesma conta derruba o token anterior | `web` | `src/WebManager_Auth.cpp:20-72`; `INTEGRACAO_SERVIDOR.md` §5.1 |
| R-R12 | `sampleIntervalMs`, `loggingEnabled`, `useHttps` e `displayPin` são aceitos e não fazem nada, e três deles forçam reinício | núcleo | 2.15; `ConfigApply.h:138, :164, :169` |
| R-R13 | Trocar de perfil por OTA sem `schema_id` (P5) grava um blob de config que a outra imagem lê sem saber | `ota.web` | R-S1 |
| R-R14 | `MAX_CUSTOM_THEMES` é 8 e `data/themes/` traz 12: quatro somem em silêncio | `tft.themes_fs` | `src/Themes.cpp:302` |
| R-R15 | Sem SSID configurado o AP não tem saída: o timeout só volta a STA com SSID, e o aparelho nunca mede (release e alpha; o Air não abre AP sozinho) | `net.ap` | `src/AppManager_Boot.cpp:1045-1069`; `CHANGELOG.md` v2.7.3 |
| R-R16 | `display = alpha_*` com idioma diferente do inglês: `trlLookup` e `logcodeLookup` devolvem "", e `LogManager::setLanguage` roda sem condição, então rótulos de `TRL()` e de códigos de log sairiam vazios | `display = alpha_*` × `i18n.pack_*` | `src/DisplayManager_Alpha.cpp:610-615`; `src/LogManager.cpp:1300-1316`; inferida, não medida |
| R-R17 | `@TRL` não tem consumidor em runtime no TFT nem no Air: `trlLookup` devolve `nullptr` e as linhas traduzidas saem em inglês; o gate de packs e o `CLAUDE.md` prometem que uma entrada faltando afeta "só aquela linha" | `cli.bilingual`, `i18n.pack_*` | `src/DisplayManager_LangParser.cpp:332-334, :346-360`; `tools/check_lang_packs.py:321-333` |

---

## 5. Perfis de referência

### 5.1 Os seis ambientes de hoje, escritos no modelo

É a primeira prova que o plano exige (seção 6, P1): o gerador tem de produzir
estes seis a partir do manifesto e o `.bin` de cada um tem de sair **byte a
byte igual** ao de hoje. A coluna "medido" é o `used` do PlatformIO em
`tools/flash_budget.json` (25/09); o `.bin` fica ~12 KB acima disso, e é o
`.bin` que o teto de OTA compara.

| perfil | `display` | `cli.full` | `net.https_server` | `net.mdns` | `net.bluetooth` | `power.air` | `tft.license_text` | instrumentos | `custom_web_omit` | medido / orçamento |
|---|---|---|---|---|---|---|---|---|---|---:|
| `pico_w_release` | tft | não | sim | sim | não | não | sim | `flash_irq_probe` | — | 998.220 / 1.000.852 |
| `pico_w_test` | tft | sim | não | não | não | não | stub | idem | — | 1.003.764 / 1.006.396 |
| `pico_w_test_https` | tft | sim | sim | não | não | não | stub | idem; `custom_fs_pages = ALARMS, FILE, TEL` | — | 1.011.388 / 1.014.020 |
| `pico_w_asserts` | tft | não | sim | sim | não | não | sim | + `concurrency_asserts` | — | 1.000.396 / 1.003.028 |
| `pico_w_alpha` | alpha_paralelo | não | não | não | sim | não | sim | idem | `tft` | 963.476 / 966.396 |
| `pico_w_air` | nenhum (via Air) | sim | não | não | sim | sim | stub | idem | `tft` | 1.003.328 / 1.006.288 |
| `pico_w_uistudy` | tft | sim | não | não | não | não | stub | + `ui_study` | — | sem orçamento |

Todos os seis carregam os três sensores, os sete pacotes de temas desligados,
`alarms.*`, `history.web`, `history.export_simx`, `log.syslog`, `web.metrics`,
`tel.*` inteiro, `fleet.hooks`, `net.ap`, `net.ap_auto` (menos o Air),
`net.captive_dns`, `net.wifi_scan`, `sensors.calib_curves`,
`storage.backup_restore` e `cli.bilingual`, porque nada disso tem chave hoje.

### 5.2 A conta da flash, e a regra que ela impõe

O layout (`src/ota/ota_layout.h:9-15`): slot de 1020 KiB, LittleFS de 1024 KiB,
4 KiB de metadados. **O staging da OTA é a mesma região do LittleFS**, escrita
em bruto com o FS desmontado (`:69-71`; `src/ota/staging.h:5-9`); é por isso
que o stage grava por cima do histórico, dos packs e do `calib.csv`, e é também por
isso que, com um slot só, **o FS nunca pode ser menor que a imagem**: não
haveria onde receber a atualização (regra R-F2). Encolher o FS para ganhar
slot, portanto, só faz sentido junto com dois slots, em que o staging é o
outro slot.

Dois slots iguais exigem que cada imagem caiba em metade do que sobra depois
do FS e dos metadados. No RP2040 o código executa em XIP num endereço fixo:
dois slots são duas imagens linkadas em endereços diferentes, dois binários
por release, e um bootloader que o arduino-pico não tem (o MCUboot chama isso
de *direct-XIP*). Nada disso é o modelo; o modelo só responde se a imagem
cabe.

| `fs.size` | cada slot | retenção V5 a 86 %, 11 canais a 1/min (`docs/HistoryV5_Implementacao.md:256`) |
|---|---:|---:|
| 1024 KiB (hoje) | 510 KiB: menor que rádio + libc + framework (≈ 432 KB antes de qualquer código próprio) | 116 dias |
| 512 KiB | 766 KiB | ~58 dias |
| 256 KiB | 894 KiB | ~29 dias |

Contra isso, o que cada bloco de recurso pesa, pela tabela de seções de
`DIETA_FLASH.md` §2 e pelos deltas do `flash_budget.json` (**estimativa**: os
números foram medidos um a um, mas nunca somados numa build, e custo não é
aditivo, seção 8):

| bloco | flash aproximada | observação |
|---|---:|---|
| TFT inteiro (UI, fontes, GFX, canvas, Core 1) | ~100 KB | o alpha mede 34.744 B a menos que o release **devolvendo** 64.732 B de Bluetooth |
| TLS inteiro (BearSSL + `WiFiClientSecure`), se nem o servidor nem a telemetria o usam | ~114 KB | tirar só o servidor HTTPS devolve ~20 KB; a biblioteca fica pela telemetria |
| CLI completa | ~45 KB | medido no Air (v2.4.10) |
| Bluetooth | ~65 KB (+16,4 KB de `.bss`) | medido no ferro |
| telemetria (código + HTTPClient + PubSubClient) | ~35 KB | 25,3 kB de `TelemetryManager` + 6,1 kB de HTTPClient + parte do "resto" |
| página HIST | ~22,5 KB | gz |
| mDNS | ~16 KB | medido |
| página TEL | ~7,8 KB | gz |
| `/metrics` | ~4,6 KB | CHANGELOG |
| syslog, HA, CORS | ~2,9 + 2,5 + 2,2 KB | CHANGELOG |

### 5.3 Três perfis que o modelo tornaria possíveis

Cada um está aqui como exemplo do que a página do configurador responderia,
com o número que ela daria hoje, marcado como o que é. **O número certo sai da
matriz de custo do P4, não desta tabela.**

| perfil | o que liga | o que desliga | flash estimada | cabe onde |
|---|---|---|---:|---|
| **Quiosque** — um aparelho de parede, sem servidor | `display = tft`, `tft.graph`, `tft.calendar`, `panel.pin`, `tft.users_admin`, `tft.themes_fs`, três sensores, `alarms.*`, `sound.buzzer`, `history.web`, `net.ap`, `net.wifi_scan`, `cli.emergency` | `tel.*`, `tls.lib`, `net.https_server`, `net.mdns`, `net.bluetooth`, `fleet.hooks`, `log.syslog`, `web.metrics`, `web.page_tel`, `history.export_simx` | ~810 KB (998 − 114 − 35 − 16 − 8 − 5 − 8, estimativa) | um slot, com folga de ~200 KB para crescer; o FS fica em 1 MiB (R-F2) |
| **Gateway A/B** — sem tela, gerido pela frota, com volta ao firmware anterior | `display = nenhum`, `tel.http`, `tel.csv_mode`, `log.syslog`, `web.metrics`, `fleet.hooks`, `cli.full`, `storage.backup_restore`, três sensores, `alarms.limits`, `tel.alarm_line`, `ota.dual_slot` | tudo do TFT, `tls.lib`, `net.mdns`, `net.bluetooth`, `history.web`, `net.ap_auto`, `sound.buzzer` | ~790 KB (998 − 100 − 114 − 16 − 22 + 45, estimativa) | **cabe com `fs.size` = 256 KiB** (slot de 894 KiB, ~29 dias de histórico); **não cabe com 512 KiB** (766 KiB) a menos que também saia `cli.full` (~745 KB, margem menor que o erro da estimativa). A telemetria em HTTP puro é o que `INTEGRACAO_SERVIDOR.md` já assume |
| **Air** — o de hoje | como `pico_w_air` | idem | 1.003.328 medido | um slot; 21.460 B de folga de OTA (`flash_budget.json` 2026-09-25) |

O que a tabela já ensina, antes de qualquer build: a pergunta "posso ter OTA
com volta ao anterior?" tem resposta **sim, para um perfil sem tela e sem
TLS, com um mês de histórico; não, para a release**, e essa é exatamente a
frase que o configurador tem de saber dar.

---

## 6. O que refatorar e implementar

> **Estado da implementação (25/09/2026), branch `feat/features-model`.** O
> núcleo do **P1 está feito e verde**: `tools/features.toml` é o manifesto,
> `tools/gen_features.py` deriva `tools/generated/profiles.ini` e
> `features_model.json`, e `tools/check_features.py` prova, pelo resolvedor do
> próprio PlatformIO, que os seis ambientes de firmware derivados do manifesto
> reproduzem os `[env:*]` de hoje — flag a flag e unidade de tradução a unidade
> (release e asserts 67, alpha e air 57). O portão sabe reprovar: uma mutação no
> manifesto é pega (controle conferido). Os dois passos rodam no job `gates` do
> CI. A **prova de build byte a byte foi feita** e a **troca também**: o
> `platformio.ini` agora inclui `tools/generated/profiles.ini` via
> `extra_configs` e não define mais `[env:pico_w_*]` (guarda só o `pico_base` e
> os ambientes native); o porquê de cada perfil, com os bytes medidos, foi
> realocado para o manifesto, e a nota de por que não existe ambiente de debug
> ficou no `platformio.ini`. Os seis `firmware.bin` construídos do
> `platformio.ini` já trocado saem **idênticos** aos de antes da troca (sha256
> conferido, build limpo). A ordem do filtro no gerado foi escrita para bater com
> a de hoje, então a ordem de link (que já custou 427 B aqui) não muda a imagem.
> O `check_features.py` continua válido: agora compara os perfis que o build usa
> (o `profiles.ini` incluído) contra o manifesto gerado na hora, então pega
> qualquer deriva entre manifesto, `profiles.ini` e build. O `simut_features.h`
> (macros de recurso como header gerado, para o `#if` do código) fica para o P2,
> quando as costuras começarem. Itens do **P0 feitos**: o flag morto
> `SIMUT_FACTORY_RESET` saiu (imagem idêntica, confirmando que nunca era lido);
> os comentários que erravam o custo do Bluetooth, do mDNS e a margem do
> snapshot foram corrigidos; e o `pico_w_air` entrou no `SHIPPING_ENVS`. Tudo
> isso está no PR #168. O **P2 começou pela catraca**: `check_feature_sprawl.py`
> prende os 283 sítios de `#if SIMUT_` a uma marca d'água (ver o quadro no P2
> abaixo); as costuras que a fazem descer vêm a seguir.

Oito passos, na ordem em que as dependências os põem. Cada um diz o que
fecha, porque um passo sem medição que o feche é um passo que nunca termina;
é a regra do `PLANO_DIVIDA_TECNICA.md`, e vale aqui. O esforço é uma ordem de
grandeza, não uma promessa: **P** = um dia ou menos, **M** = dias,
**G** = semanas.

```
P0 limpar ──► P1 manifesto + gerador + fidelidade ──┬──► P2 costuras ──────┐
                                                     ├──► P3 magnitudes ───┤
                                                     └──► P4 custo medido ─┼──► P5 identidade e OTA
                                                                            ├──► P6 maturidade
                                                                            └──► P7 configurador ──► P8 testes
```

### P0 — Tirar o que está morto ou mente (P)

Antes de modelar, o que não existe sai, e o que mente é corrigido; é o que
evita modelar defeito.

| item | o que fazer | fecha quando |
|---|---|---|
| ~~`SIMUT_FACTORY_RESET`~~ **feito** | saiu do manifesto (`tools/features.toml`); depois da troca do P1 o flag vinha do `profiles.ini` gerado, não mais do `platformio.ini` | os seis `firmware.bin` reconstruídos ficaram byte a byte iguais, confirmando que a imagem nunca lia o flag |
| `sampleIntervalMs` (`s_int`) | ou passa a ser lido pelo pipeline, ou sai da CFG, do `commit_all`, do `/api/config` e do CLI; o campo fica no esquema | ou um teste que prove o efeito, ou a página sem o campo |
| `cfg.loggingEnabled` | idem: ou o log obedece, ou o interruptor sai; hoje só força reinício | idem |
| `handleApiOtaStagingTest` | apagar, ou registrar sob `web.api_bench` | `check_authz.py --list` sem órfão |
| `MODE_STATS_VIEW` | apagar a tela | `DisplayManager.cpp` menor pelo número que a dieta medir |
| `SIMUT_BOOT_SERIAL_LOG` | renomear nas duas ferramentas, para não parecer macro do firmware | — |
| `custom_web_omit` × `SIMUT_DISPLAY_TFT` | o gerador de páginas deriva o omit da macro, lendo `CPPDEFINES` do ambiente | um ambiente com `TFT=0` e sem `custom_web_omit` recebe o mesmo blob que com |
| ~~`SHIPPING_ENVS`~~ **feito** | `pico_w_air` incluído em `tools/build_webui_gz.py` | o gerador agora recusa `custom_fs_pages` no Air; o Air não declara nenhuma, então a imagem ficou idêntica |
| rotas fantasmas | `/api/reset_touch_cal` e `/api/themes` só com TFT | alpha e Air com 54 rotas, e `AUTHORIZATION.md` dizendo isso |
| `cfg.useHttps`, `displayPin` | campos mortos que forçam reinício: sair das classes de reinício (`ConfigApply.h:138, :164, :169`); o campo fica no esquema | um `commit_all` que só os toca responde sem `_reboot` |
| `docs/OTA_USAGE.md:46` e o comentário de `SystemDefs_Network.h:140-141` | dizer que é o stage que grava sobre o FS; apagar a menção ao `TelemetryGuard` | — |
| `Dockerfile:21-22`, `docker-compose.yml:9`, `tools/build_release_pio.sh:66` | os três constroem sem `patch.sh` ou apagam os overrides que o build exige (R-S30) | um build no Docker limpo que linka |
| comentários de custo (2.15) e de hardware (apêndice B) — **parcial** | corrigidos os do Bluetooth (`src/simut_config.h`: SPP clássico, 64.732 B + 16.416 B) e do mDNS (~15 KB, 15.376 B medidos), e a margem do snapshot (`src/ota/config_snapshot.cpp`: 1.430 B na v25, não os ~3,4 KiB da v21). Faltam o `.wip` "a cada 10 min" (é literal de string, não comentário: muda o binário, item à parte) e os de hardware inferidos (o SPI a 24 MHz precisa de medição, não de outra afirmação sem número) | a regra da casa: comentário que discorda do código é defeito; imagens conferidas idênticas após os três |

### P1 — O manifesto, o gerador e a prova de fidelidade (M)

1. Escrever `tools/features.toml` com **exatamente** as 11 chaves de hoje, as
   duas escolhas, a variante, os instrumentos e os sete perfis, sem inventar
   chave nova. O apêndice A é o ponto de partida.
2. `tools/gen_features.py` gera `src/simut_features.h` (toda macro definida
   como 0 ou 1) e `tools/generated/profiles.ini` (um `[env:]` por perfil, com
   `build_flags`, `build_src_filter`, `lib_ignore`, `lib_deps` extras e
   `custom_web_omit`), incluído por `extra_configs`.
3. Converter as macros de presença em macros de valor: `SIMUT_WEB_HTTPS`,
   `SIMUT_LICENSE_STUB`, `SIMUT_CONCURRENCY_ASSERTS`, `SIMUT_THEMES_*`,
   `SIMUT_WDT_DISABLED`, e o `#ifdef SIMUT_MDNS` de `SystemDefs_Network.h:61`.
   Ligar **`-Wundef`** em `build_src_flags`, junto do `-Werror` que já existe:
   a partir daí, um `#if SIMUT_X` com `X` não declarada é erro de compilação,
   e um `#ifdef` de chave é achado do `check_features.py`. Como o `-Werror`,
   isso só morde em build frio: uma unidade que não recompila não avisa, e é
   por isso que o CI não guarda `.pio` desde 2026-09-08 (`platformio.ini:94-110`).
4. `AppManager_Air.cpp:23` e `DisplayManager_None.cpp:20` passam a incluir
   `simut_features.h` antes do teste, ou saem do filtro pelo gerador.
5. `tools/check_features.py`, no pré-build e no CI: toda `SIMUT_*` em `#if` está
   no manifesto; o `profiles.ini` commitado é igual ao gerado; toda rota,
   comando e página tem etiqueta de recurso (isto só a partir do P2).
6. **A prova:** gravar o sha256 dos seis `.bin` da `main` antes do P1; depois
   do P1, os seis `.bin` gerados têm o mesmo sha256. Uma diferença é um defeito
   do gerador, não um "ajuste". Só então apagar os `[env:]` escritos à mão.

A fidelidade é o que compra o direito de mexer no resto: a partir dela, todo
passo seguinte é medido contra um ponto fixo.

### P2 — Uma costura por tipo de extensão (G)

O princípio: **um recurso é uma unidade de tradução própria, com uma função
de registro, chamada por `features_registry.cpp` gerado**. O arquivo entra ou
sai do link pelo `build_src_filter` que o gerador escreve; o arquivo
compartilhado não ganha `#if`. O `flash_compose.py` já atribui flash por
arquivo, então esse padrão é também o que torna o custo mensurável sem
esforço.

| costura | hoje | alvo | arquivos que mudam |
|---|---|---|---|
| **rotas HTTP** | lista plana de 62 `_server->on` em `WebManager_Core.cpp:120-225`; cortar um recurso é editar três arquivos; `/api/action` multiplexa 7 capacidades por `?op=` | cada recurso tem `static const RouteDef kRoutes[] = {{método, caminho, permissão, handler}}` e `void <id>_registerRoutes(WebServer&)`; o núcleo registra as suas; `check_authz.py` lê as tabelas em vez de fazer regex sobre o texto (hoje ele audita o superconjunto, `tools/check_authz.py:88-117`); `/api/action` se desdobra em uma rota por recurso, ou o despacho por `op` vira tabela por recurso | `WebManager_Core.cpp`, `WebManager.h`, os 12 `WebManager_*.cpp`, `tools/check_authz.py`, `docs/AUTHORIZATION.md` |
| **páginas web** | 12 blobs sempre; `custom_fs_pages` só move; `@IF` reconhece só `tft` | cada página é `pages = [...]` de um recurso; o gerador emite só os blobs das páginas ligadas, e a rota de cada uma vem do registro do recurso; `@IF <qualquer id do manifesto>` (o verificador de dependências entre blocos, `tools/build_webui_gz.py:257-331`, já vale para qualquer feature) | `tools/build_webui_gz.py`, `WebUI.h`, `WebManager_Auth.cpp:214-222` |
| **comandos da CLI** | um comando toca de 5 a 7 lugares (enum, parser, máscara de modos, help, `switch`, `HELP_TEXT_EN`, `@HELP`); `SIMUT_CLI_FULL` tem 34 sítios | tabela por recurso: `CliCommandDef {tokens, modos, demanda, chave de ajuda}`; o parser, a máscara, o help e o `check_cli_help.py` andam sobre as tabelas; o conjunto de emergência é a tabela do núcleo, e o gate deixa de remover `#if` textualmente (`tools/check_cli_help.py:101-122`) | `CommandParser.cpp`, `CommandManager.cpp`, `AppManager_Commands.cpp`, `AppManager_CmdHandlers.cpp`, `SystemDefs_Cli.h`, `HelpLicenseEN.h` |
| **telas do display** | render e toque são duas cadeias `if/else` sobre `_uiMode` (`DisplayManager.cpp:1399-1630`; `DisplayManager_Touch.cpp:393-1640`); `None` e `Alpha` repetem stubs de tudo; `!SIMUT_DISPLAY_ALPHA` significa TFT | a escolha `display` escolhe o conjunto de arquivos; cada tela é `ScreenDef {UiMode, render, toque}` registrada pelo seu recurso (`tft.graph`, `tft.calendar`, `tft.users_admin` viram arquivos que registram telas); o despacho é uma tabela indexada por `UiMode`; `display = nenhum` vira um valor próprio, não um efeito do Air | `DisplayManager*.cpp`, `DisplayManager.h`, `display/DisplayDriver.h`, `SystemDefs_Records.h` (`UiMode`) |
| **transportes de telemetria** | tudo num arquivo de 2.811 linhas; a escolha é só em runtime | `TransportDef` por arquivo (HTTP, MQTT), o cliente TLS numa fábrica própria, HA, linha de alarmes e construtores de payload (JSON, CSV, custom) em arquivos próprios; a escolha em runtime continua, entre os transportes compilados | `TelemetryManager.cpp` (o maior objeto de código próprio: 25,3 kB) |
| **sensores** | drivers header-only incluídos sem condição; 85 sítios de `#if SIMUT_SENSOR_*` em 13 arquivos; a varredura encadeia tipos por `#elif` | um `.cpp` por driver, `SensorTypeDef` numa tabela (nome, canais, pinos, `begin`, `update`, `renderPanel`); a varredura itera a tabela; `SensorManager.h` deixa de incluir drivers; `docs/adding-a-new-sensor.md` passa a descrever isto (hoje aponta para um `SensorConfig.h` que só inclui outro arquivo) | `src/sensors/*`, `SensorManager.cpp/.h`, `SensorHelpers.h`, `SensorPanelDispatch.h`, `SensorDrawing.h` |
| **objetos que existem sem a chave** | `SyslogManager` (2.136 B), fila de alarmes (2.048 B), `_graphData` (~5,9 KB no alpha), buffers de OTA e restore (16,8 KB) | cada um vive no arquivo do seu recurso; os buffers de OTA e restore passam a heap sob demanda, dentro da pausa que a operação já faz | `AppManager_Core.cpp:29-39` (a raiz de composição), `DisplayManager.h`, `ota/*` |
| **códigos de log** | 155 códigos num `.tsv` sem coluna de recurso | coluna `feature` no `tools/logcodes.tsv`, só documental: os códigos são formato de arquivo e não mudam com a imagem | `tools/logcodes.tsv`, `tools/gen_logcodes.py` |
| **temas, packs, som, métricas, syslog, frota** | arquivos próprios com fiação espalhada | registro pelo `features_registry.cpp`; a fiação (`AppManager_Boot.cpp:805, :1218`, `AppManager_Loop.cpp:266`…) vira chamada de registro | os pontos listados na seção 2 |

Ordem dentro do P2, pelo retorno: rotas e páginas primeiro (é o que corta
mais bytes com menos risco), depois sensores e telemetria (é o que o cliente
mais escolhe), depois a CLI, depois o display (é o maior e o mais entrelaçado
com o Core 1).

> **A catraca do P2 já existe.** `tools/check_feature_sprawl.py` conta os sítios
> de `#if SIMUT_` por macro e por arquivo em `src/` e os prende a uma marca
> d'água em `tools/feature_sprawl.json`; um sítio a mais num arquivo
> compartilhado reprova, extrair um recurso para a sua unidade de tradução baixa
> a marca. É o mesmo mecanismo do orçamento de flash, aplicado ao emaranhamento,
> e roda no job `gates` do CI. O **placar de partida é 283 sítios em 24 macros**
> (`SIMUT_AIR` 41/15, `SIMUT_DISPLAY_TFT` 39/20, `SIMUT_SENSOR_DS18B20` 35/13,
> `SIMUT_CLI_FULL` 34/9, `SIMUT_PANEL_PIN` 27/13). Cada costura abaixo tem de
> fazer esse número descer, e trava o ganho com `--update` no mesmo commit. O
> controle negativo confere: um `#if SIMUT_AIR` injetado num arquivo
> compartilhado é pego.

Cada costura abaixo tem, portanto, uma medida objetiva de sucesso: o placar
desce. É o mesmo espírito da marca d'água de flash, agora sobre o
emaranhamento.

Duas armadilhas medidas que limitam a granularidade: sem LTO, **cada `.cpp`
novo custa ~2 KB** (`platformio.ini:56-57`), então um recurso de 300 B não
merece arquivo próprio e vai junto do vizinho; e um objeto vazio muda a ordem
de link e os veneers em centenas de bytes (427 B medidos, `platformio.ini:36-42`),
então arquivo desligado sai do filtro, nunca fica sob `#if` inteiro.

### P3 — As magnitudes num lugar só (M)

1. `gen_features.py` emite também `src/simut_limits.h` a partir das magnitudes
   do manifesto; `SystemDefs_Limits.h` passa a incluí-lo e as constantes
   espalhadas (3.4: seis cópias do `3`, quatro do `250`, oito do `16`, três do
   `86`) apontam para ele.
2. As travas derivadas viram `static_assert` escritas uma vez: o snapshot da
   OTA contra `sizeof(SystemConfig)`; o id do H5 (`slot*8+ch` em `uint8`)
   contra `sensors.slots`; `hist.channels` contra a soma de canais dos slots
   configurados, com **aviso em runtime** no lugar do corte silencioso de
   `StorageManager.cpp:2338-2341`; `alarms.queue` dimensionando o array;
   `lwip.tcp_pcb` e `lwip.pbuf_pool` contra o número de conexões que as chaves
   ligadas podem abrir (web + telemetria + MQTT + mDNS).
3. **Família de esquema.** As magnitudes marcadas F·layout (`users.max`,
   `sensors.slots`, `hist.channels`, `hist.block`, tamanhos de string) mudam
   `sizeof(SystemConfig)` e o formato do histórico. O modelo as permite, mas um
   perfil que as muda pertence a outra **família**: recebe um `schema_id`
   (hash do vetor dessas magnitudes), gravado na etiqueta do `.bin` e conferido
   pela validação de OTA e pelo restore. Imagens de famílias diferentes não
   trocam config nem backup, e o configurador avisa isso na hora da escolha.
   É a única forma de manter a regra de `SystemDefs_Limits.h:31-37` verdadeira
   e ainda assim deixar o cliente escolher 8 contas em vez de 32.
4. A contabilidade de RAM por magnitude sai do mapa do linker: `.bss` por
   arquivo já é atribuível; por magnitude, o gerador emite um comentário por
   símbolo grande com a fórmula (`64 × sizeof(AlarmRecord)`), e a página soma.

### P4 — Custo medido pela máquina, nunca à mão (M)

1. Workflow `features-matrix.yml`, por `workflow_dispatch` e semanal: constrói
   o perfil "tudo ligado" e o "tudo desligado", e depois **um build por chave**
   invertida a partir de cada um. O custo de uma chave vira um par: o que ela
   devolve quando sai do tudo-ligado, e o que custa quando entra no
   tudo-desligado. A diferença entre os dois é o quanto ela compartilha com as
   outras, e é a informação que faltava (a BearSSL é o caso canônico: −20 KB
   tirando só o servidor, −114 KB tirando todo TLS).
2. O resultado vai para `tools/features_cost.json`, com commit, data e o par,
   ao lado do `flash_budget.json` e com a mesma disciplina: escrito por CI,
   lido por pessoas.
3. `tools/flash_compose.py --by-feature` agrupa a atribuição por arquivo pelo
   mapa arquivo → recurso do manifesto; a mesma passada dá `.bss` e `.data`.
4. Para os perfis de referência, as suítes de hardware que já existem
   (`soak_a6`, `web_test_suite`) gravam heap livre e maior bloco contíguo
   depois do boot e sob carga; são os dois números que o TLS exige e que
   nenhum build sabe (3.2).
5. Fecha quando: um PR que liga uma chave mostra, no check, o delta de flash e
   de `.bss` de cada perfil de referência, sem que ninguém rode nada à mão.

Refinamento para depois, quando P2 estiver adiantado: um único build com
`-Wl,--cref` dá o grafo de referências entre seções; como o toolchain já
linka com `-ffunction-sections -fdata-sections --gc-sections`, o custo de
**qualquer** combinação é a soma das seções alcançáveis a partir das raízes
ligadas, que é literalmente o que o linker faz. Isso troca a matriz de N
builds por um build e um cálculo, e captura o compartilhamento de graça. O que
ele não vê é código sob `#if` dentro de função compartilhada, que é o que o P2
elimina.

### P5 — Identidade de perfil, e a OTA que a respeita (P)

1. `SIMUT_ENV_NAME` hoje distingue só a variante (`src/simut_config.h:391-398`)
   e alimenta a validação da OTA (`src/ota/validation.cpp:95-124`); cinco
   perfis se chamam "release" e a OTA aceita cruzar entre eles (R-F9). Passa a
   carregar `profile_id`, `feature_hash` e `schema_id`; `/api/status` os
   reporta; o TXT do mDNS e os cabeçalhos `X-SIMUT-*` os levam.
2. A validação de OTA recusa `schema_id` diferente (a config não sobreviveria)
   e avisa `profile_id` diferente (é legítimo trocar de perfil, mas é uma
   decisão, não um acidente).
3. `flash_budget.json` passa a ter uma entrada por perfil de referência, gerada
   pelo mesmo `gen_features.py`, com a margem de 3.000 B que ele sempre usou.

### P6 — Maturidade e perigos, escritos no manifesto (P)

1. `maturity.since`, `validated_by`, `soak_hours`, `issues` para cada recurso,
   a partir da tabela da seção 2; `check_features.py` exige que `validated_by`
   aponte para um documento que existe, e o `docs/README.md` marca esse
   documento Living ou Snapshot como já faz.
2. `hazards` com a evidência, a partir da seção 4 (R-R1 a R-R14) e das
   memórias da bancada.
3. A página mostra os três estados sem inventar um quarto: **validado no
   ferro em vX por Y**, **só em nativo**, **nunca medido**. Não existe
   "confiabilidade 87 %".

### P7 — O configurador (M)

1. `docs/configurador/index.html`: estática, no Ângulo, sem servidor; lê
   `model.json` (gerado do manifesto) e `features_cost.json`.
2. Propaga `requires`, `conflicts`, `implies` e as escolhas; mostra flash contra
   o teto de um slot ou de A/B, `.bss` + heap residente contra os limiares da
   seção 4 (R-N1 a R-N8), o FS contra a soma do que mora nele com a retenção
   em dias derivada, e os recursos finitos de hardware (máquinas PIO, spinlocks,
   DMA, pinos em disputa, Core 1).
3. O painel de maturidade e perigos do P6.
4. A saída é `profile.toml`, e um botão dispara o `features-matrix.yml` com
   ele: o CI constrói, mede e anexa o `.uf2` com os números reais. **A cotação
   vira binário medido**, que é a regra da casa.

### P8 — Testar perfis, não o produto cartesiano (M)

1. Suítes nativas por recurso, para o que hoje é uma só (`native_network`,
   `native_air`, `native_cli` já são o começo).
2. O CI constrói os perfis de referência (os seis de hoje mais os da seção
   5.3 que forem adotados), cada um com o seu orçamento.
3. Um job semanal constrói **os pares de risco**, que são poucos e conhecidos:
   `net.https_server` × `tel.tls_client` (a saga do pool TLS); `tft.mirror` ×
   `tel.tls_client` (26 KB transitórios contra o heap do TLS); `net.bluetooth`
   × `display ≠ nenhum` (a TLV do BTstack sem `Core1FlashPause`, R-H24);
   `alpha_i2c` × `sensors.bmx280` (R-H21); `sensors.scan` × `display = tft`
   (R-H7); `net.ap` × tudo (R-R1). Um par novo entra quando um incidente o
   revelar, como o `flash_budget.json` faz com bytes.

---

## 7. O padrão daqui em diante

Vale para todo recurso novo depois do P1, e é o que o `check_features.py`
cobra. É deliberadamente parecido com o que `tools/logcodes.tsv`,
`flash_budget.json` e a matriz de autorização já fazem: uma tabela, um
gerador, um portão.

### 7.1 Adicionar um recurso

1. **Uma entrada em `tools/features.toml`** com `id`, `domain`, `kind`,
   `default`, as regras, `macro`, `files`, o que ele registra em cada costura,
   `cost = "unmeasured"` e `maturity.validated_by = ""`. Sem entrada, a macro
   não compila (`-Wundef`).
2. **Uma unidade de tradução própria**, ou a do vizinho se o recurso for
   pequeno (o custo de ~2 KB por `.cpp` decide). Ela exporta
   `<id>_register(Registry&)`, e é só aí que rotas, comandos, telas,
   transportes ou sensores entram.
3. **Nenhum `#if SIMUT_<id>` em arquivo compartilhado.** Se um arquivo do
   núcleo precisa se comportar diferente, ele consulta um ponto de extensão
   registrado (uma tabela, um ponteiro, um `weak`), não a macro. O gate conta
   os sítios por arquivo e só aceita que o número desça.
4. **Toda rota, comando, página e código de log do recurso leva a etiqueta
   dele.** `check_authz.py`, `check_cli_help.py` e `gen_logcodes.py` já leem
   tabelas; a etiqueta é uma coluna.
5. **Uma suíte nativa ou um roteiro de ferro**, e o documento que o registra é
   o `validated_by`. Sem isso o recurso nasce marcado "nunca medido", e a
   página diz isso ao cliente.
6. **O custo é medido pelo `features-matrix.yml` antes do merge**, e o PR
   mostra o delta por perfil. Se o recurso entra num perfil de referência,
   o orçamento desse perfil sobe no mesmo PR, com o número no commit.
7. **Uma linha na seção 2 deste documento.** Este documento é Living: a tabela
   da seção 2 e o manifesto têm de concordar, e o gate confere que todo `id`
   do manifesto aparece aqui.

### 7.2 Adicionar uma magnitude

1. Entrada no manifesto com `unit`, `min`, `max`, `step`, `default`, a fórmula
   de custo por unidade (`bss = n * 32`) e a marca F·layout se mudar o esquema.
2. A constante só existe em `simut_limits.h`; literal repetido é achado do gate.
3. Uma `static_assert` para cada trava derivada, no arquivo que a impõe.
4. Se for F·layout: sobe `CONFIG_VERSION`, escreve a migração, e a família de
   esquema muda (P3.3).

### 7.3 Adicionar uma regra

1. `requires`, `conflicts` ou `implies` no manifesto, com um campo `why` que
   cita o arquivo ou o documento.
2. Se a regra é de hardware (pino, máquina PIO, spinlock), ela também vira
   `static_assert` ou `#error` no arquivo que reivindica o recurso, para que o
   build a imponha mesmo fora do gerador.
3. Se a regra nasceu de um incidente, ela entra em `hazards` com a evidência
   e o par entra no job semanal do P8.

### 7.4 O que a máquina passa a impor, e o que fica com a pessoa

| a máquina | a pessoa |
|---|---|
| macro não declarada (`-Wundef`); `#ifdef` de chave; sítio novo de `#if SIMUT_` em arquivo compartilhado | decidir se um recurso merece arquivo próprio ou vai com o vizinho |
| `profiles.ini` commitado ≠ gerado; `.bin` de um perfil de referência fora do orçamento | decidir se um custo vale o recurso |
| rota, comando, página ou código sem etiqueta de recurso | escrever o `why` de cada regra |
| `validated_by` apontando para documento inexistente; `id` do manifesto ausente da seção 2 | validar no ferro, e escrever o documento |
| regra de hardware sem `static_assert` correspondente | julgar o que é hazard e o que é bug |
| custo `unmeasured` num recurso ligado em perfil de referência | ler o par de custos e entender o compartilhamento |

---

## 8. O que o modelo não faz, e os riscos

- **Custo não é aditivo.** Com `--gc-sections`, uma seção só sai quando o
  último recurso que a referencia sai; o par de medições do P4 limita o erro,
  e o grafo de `--cref` o elimina, mas a resposta certa para um perfil que
  ninguém construiu é sempre "construa". A página tem de dizer "estimativa"
  até o CI dizer "medido".
- **Confiabilidade não é número.** O que o modelo oferece é maturidade com
  fonte e perigos com evidência. Prometer mais seria a promessa que um número
  não sustenta.
- **Dois slots são um projeto de bootloader**, com dois binários por release e
  um gestor que sabe qual slot está livre. O modelo só diz se a imagem cabe.
- **Granularidade custa flash.** Sem LTO, cada `.cpp` custa ~2 KB, e o
  toolchain não tem LTO (`platformio.ini:56`). O padrão "um recurso, um
  arquivo" tem de ser aplicado com esse número na mão; 54 chaves não são 54
  arquivos novos.
- **O esquema é uma família, não uma chave.** As magnitudes F·layout partem o
  produto em famílias que não trocam config nem backup. O configurador tem de
  dizer isso com todas as letras, e a OTA tem de recusar a troca.
- **A explosão combinatória é real e não se testa.** 54 chaves são mais
  perfis do que átomos que interessam; o P8 testa perfis de referência e pares
  de risco, e um perfil de cliente que não é de referência é construído e
  medido pelo CI, mas só é validado no ferro se alguém o validar. A página
  mostra isso.
- **O Air é uma variante, não uma chave.** Ele arrasta o display, o som, o
  Core 1, o AP automático e os modos do `commit_all`. Modelá-lo como `implies`
  sobre várias chaves é honesto; fingir que é uma chave a mais não é.
- **O modelo não decide produto.** Que o painel sem PIN seja só leitura ou
  aberto, que a senha forte deixe de depender do HTTPS, que o texto da licença
  saia do painel: são decisões que o modelo torna visíveis, e que alguém tem
  de tomar.

---

## Apêndice A — `tools/features.toml`, o rascunho

É o ponto de partida do P1, escrito a partir das seções 2 a 4. Os custos
trazem a fonte quando existe medição; onde não existe, `unmeasured`, e é
assim que a página tem de mostrá-los. Os caminhos de arquivo são relativos a
`src/`. As regras usam `display=tft` para "a escolha `display` vale `tft`".

```toml
# tools/features.toml — a única fonte do modelo de recursos.
# Gera: src/simut_features.h, tools/generated/profiles.ini,
#       src/features_registry.cpp, docs/configurador/model.json.
# Semântica (Kconfig): requires = depends on; implies = select;
# conflicts = !; choice = exatamente um. Custo sem measured_at = unmeasured.

[schema]
version = 1
flash_total   = 2097152
ota_safe_max  = 1040384          # src/ota/ota_layout.h:54
ram_main      = 262144
framework_patches = ["2f keep-alive", "2i CORS"]   # toda imagem (R-S30); 2h só com net.https_server; 2d antes de 2g

# ---------------------------------------------------------------- escolhas
[choice.display]
title   = "Mostrador"
default = "tft"

[choice.display.tft]
macros  = ["SIMUT_DISPLAY_TFT=1", "SIMUT_DISPLAY_ALPHA=0"]
files   = ["DisplayManager.cpp", "DisplayManager_Touch.cpp", "DisplayManager_Settings.cpp",
           "DisplayManager_Dashboard.cpp", "DisplayManager_Calibration.cpp", "DisplayManager_Auth.cpp",
           "DisplayManager_Alarm.cpp", "DisplayManager_Fonts.cpp", "DisplayManager_i18n.cpp",
           "DisplayManager_LangParser.cpp", "UiWidgets.cpp"]
implies = ["hw.core1"]
pins    = [16, 17, 18, 19, 20, 26, 27, 28]
hw      = { spi = "SPI0", dma = 3, alarms = 1, irq_gpio = 20 }
cost    = { flash = 100000, bss = 8192, heap = 28800, source = "estimativa 5.2; nm 2026-09-25", measured_at = "" }
maturity = { since = "1.0.0", validated_by = "AGENTS.md §1" }

[choice.display.alpha_paralelo]
macros  = ["SIMUT_DISPLAY_TFT=0", "SIMUT_DISPLAY_ALPHA=1", "HD44780_MODE_PARALLEL=1"]
files   = ["DisplayManager.cpp", "DisplayManager_Alpha.cpp", "DisplayManager_i18n.cpp", "DisplayManager_LangParser.cpp"]
implies = ["hw.core1"]
web_omit = ["tft"]
pins    = [16, 17, 18, 19, 20, 21]
maturity = { since = "1.3.0-beta", validated_by = "" }        # "the bench has no HD44780"

[choice.display.alpha_i2c]
macros  = ["SIMUT_DISPLAY_TFT=0", "SIMUT_DISPLAY_ALPHA=1", "HD44780_MODE_I2C=1"]
files   = ["DisplayManager.cpp", "DisplayManager_Alpha.cpp", "DisplayManager_i18n.cpp", "DisplayManager_LangParser.cpp"]
implies = ["hw.core1"]
web_omit = ["tft"]
pins    = [26, 27]
hw      = { i2c = "I2C1" }
conflicts = ["sensors.bmx280@i2c1"]                          # R-H21
maturity = { since = "1.3.0-beta", validated_by = "" }        # sem ambiente, sem registro

[choice.display.nenhum]
macros  = ["SIMUT_DISPLAY_TFT=0", "SIMUT_DISPLAY_ALPHA=0"]
files   = ["DisplayManager_None.cpp"]                         # hoje sob SIMUT_AIR: P2 o desacopla
web_omit = ["tft"]

[choice.alpha_mode]                                           # só quando display=alpha_*
title = "Ligação do LCD"
options = ["paralelo", "i2c"]

# ---------------------------------------------------------------- display
[feature."tft.graph"]
kind = "chave"; domain = "display"; default = true
requires = ["display=tft"]
files = ["DisplayManager_Graph.cpp", "AppManager_Graph.cpp"]
cost = { flash = 11300, bss = 2000, heap = 5900, source = "DIETA_FLASH §2; nm 2026-09-25" }
maturity = { since = "1.0.0", validated_by = "docs/images/screens/graph.png" }

[feature."tft.calendar"]
kind = "chave"; domain = "display"; default = true
requires = ["tft.graph"]
files = ["DisplayManager_Calendar.cpp"]
cost = { source = "unmeasured" }
hazards = ["sempre em português, ignora o idioma (manual cap. 11)"]

[feature."panel.pin"]
kind = "chave"; domain = "display"; default = true
requires = ["display=tft"]
macro = "SIMUT_PANEL_PIN"                                     # hoje derivada em SystemDefs_Limits.h:44-48
files = ["DisplayManager_Auth.cpp", "AppManager_Panel.cpp"]   # PinKeypad.h vai junto
cost = { flash = 23504, source = "flash_budget.json v24+v25+teclado" }
maturity = { since = "2.5.0-beta", validated_by = "README 2026-09-20 (32/32)" }
hazards = ["6 falhas trancam a conta, 20 trancam o painel até o reboot (AGENTS §1)"]

[feature."tft.users_admin"]
kind = "chave"; domain = "display"; default = true
requires = ["panel.pin"]
files = ["DisplayManager_Users.cpp"]
cost = { flash = 15792, source = "flash_budget.json v24" }
maturity = { since = "2.5.0-beta", validated_by = "tools/panel_users_hw_test.py" }

[feature."tft.mirror"]
kind = "chave"; domain = "display"; default = true
requires = ["display=tft"]
routes = ["GET /api/screenshot", "GET /api/screenshot_chunk", "GET /api/screen_stream", "POST /api/touch"]
web_blocks = ["tft"]                                          # os @IF tft da DASH
cost = { flash = 6376, page_gz = 2363, heap_peak = 26624, source = "flash_budget.json 18-22/09; CHANGELOG v2.6.1; cálc." }
maturity = { since = "2.4.7-beta", validated_by = "README 2026-09-19" }
hazards = ["26,6 KB transitórios por quadro contra o heap do TLS (R-N6)"]

[feature."tft.themes_fs"]
kind = "chave"; domain = "display"; default = true
requires = ["display=tft"]
routes = ["GET /api/themes", "POST /api/save_sys"]
cost = { bss = 800, source = "nm 2026-09-25" }
hazards = ["os .thm somem a cada OTA (PLANO_STABLE §6)", "MAX_CUSTOM_THEMES=8 e data/themes traz 12 (R-R14)"]

[feature."tft.theme_packs"]
kind = "chave"; domain = "display"; default = false
requires = ["display=tft"]
macro = "SIMUT_THEMES_HEALTH SIMUT_THEMES_PRO SIMUT_THEMES_MEDICAL SIMUT_THEMES_SAFETY SIMUT_THEMES_RETRO SIMUT_THEMES_NATURE SIMUT_THEMES_UTILITY"
cost = { flash_per_theme = 70, source = "simut_config.h:313 (o manual diz 85)" }

[feature."tft.license_text"]
kind = "chave"; domain = "display"; default = true
requires = ["display=tft"]
macro = "SIMUT_LICENSE_STUB"                                  # invertida: stub = desligada
cost = { flash = 1833, source = "DIETA_FLASH §5" }

[feature."net.ap_gesture"]
kind = "chave"; domain = "rede"; default = true
requires = ["display=tft", "net.ap"]
maturity = { since = "1.1.0-beta", validated_by = "AGENTS.md §1 (4 casos, 22/09)" }
hazards = ["o mais frágil dos cinco caminhos para o AP (manual §14)"]

# ---------------------------------------------------------------- sensores
[feature."sensors.ds18b20"]
kind = "chave"; domain = "sensores"; default = true
macro = "SIMUT_SENSOR_DS18B20"
files = ["sensors/DS18B20Driver.cpp"]                          # hoje header-only: P2
libs = ["OneWirePIO_RP2040"]
hw = { pio = "pio0", pio_sm = 1, pio_instr = 27 }
cost = { source = "unmeasured; drivers PIO+SPI somam 16.392 B (DIETA §2)" }
maturity = { since = "1.0.0", validated_by = "PLANO_STABLE §6" }

[feature."sensors.dht22"]
kind = "chave"; domain = "sensores"; default = true
macro = "SIMUT_SENSOR_DHT22"
files = ["sensors/DHT22Driver.cpp"]
libs = ["DHT22PIO_RP2040"]
hw = { pio = "pio1", pio_sm = 1, pio_instr = 17 }
maturity = { since = "1.0.0", validated_by = "promotion/v2.3.2-stable/CHECKPOINT.md" }

[feature."sensors.bmx280"]
kind = "chave"; domain = "sensores"; default = true
macro = "SIMUT_SENSOR_BME280"
files = ["sensors/BME280Driver.cpp"]
libs = ["TwoWirePIO_RP2040", "BMx280PIO_RP2040"]
hw = { i2c = "I2C0|I2C1", spinlocks_per_instance = 1 }
rules = ["no máximo 6 instâncias no TFT e alpha, 7 no Air (R-H15)"]
maturity = { since = "1.4.1-beta", validated_by = "plano de estabilidade T1.3 (BMP280); BME280 desconhecido" }

[feature."sensors.scan"]
kind = "chave"; domain = "sensores"; default = true
requires = ["sensors.ds18b20"]
routes = ["POST /api/action?op=sensor_scan|scan_results|sensor_accept"]
commands = ["sensor scan", "sensor accept"]
hazards = ["a varredura percorre GP0..GP16 (R-H7)"]

[feature."sensors.calib_curves"]
kind = "chave"; domain = "sensores"; default = true
requires = ["net.ntp"]
routes = ["GET /api/calib", "POST /api/calib"]
cost = { bss = 2808, heap_per_sensor = 512, source = "nm 2026-09-25; cálc." }
maturity = { since = "2.0.2-alpha", validated_by = "DIETA_FLASH §7" }

# ---------------------------------------------------------------- alarmes e som
[feature."alarms.limits"]
kind = "chave"; domain = "alarmes"; default = true
pages = ["ALARMS"]
routes = ["GET /alarms", "GET /api/alarms"]
maturity = { since = "1.0.0", validated_by = "API_POST.md (19/09)" }

[feature."alarms.fault"]
kind = "chave"; domain = "alarmes"; default = true
requires = ["alarms.limits"]
maturity = { since = "2.3.3-beta", validated_by = "tools/alarm_hw_test.py" }

[feature."alarms.maintenance"]
kind = "chave"; domain = "alarmes"; default = true
requires = ["alarms.limits"]
cost = { flash = 2896, source = "flash_budget.json 2026-09-19 (parte)" }
maturity = { since = "2.5.0-beta", validated_by = "API_POST.md (19/09)" }

[feature."sound.buzzer"]
kind = "chave"; domain = "som"; default = true
files = ["SoundManager.cpp"]
libs = ["BuzzerPIO_RP2040"]
pins = [22]
hw = { pio_sm = 2, pio_instr = 4, spinlocks = 1 }
conflicts = ["power.air"]
cost = { source = "unmeasured" }

# ---------------------------------------------------------------- histórico, log, arquivos
[feature."history.web"]
kind = "chave"; domain = "histórico"; default = true
pages = ["HIST"]
routes = ["GET /history", "GET /api/history_days", "GET /api/history/open", "GET /api/logs", "GET /api/logcodes", "POST /api/clear_logs"]
requires = ["web.files"]                                     # /api/ls e /download (R-S17)
cost = { page_gz = 22544, bss = 2048, source = "WebUI_GZ.h 2026-09-25; nm" }
maturity = { since = "2.1.8-beta", validated_by = "README 2026-09-25 (suíte web)" }

[feature."history.export_simx"]
kind = "chave"; domain = "histórico"; default = false
routes = ["GET /api/export/history.bin", "GET /api/export/logs.bin", "GET /api/history_multi"]
cost = { source = "unmeasured" }

[feature."log.syslog"]
kind = "chave"; domain = "log"; default = true
files = ["SyslogManager.cpp"]
requires = ["net.wifi_sta"]
cost = { flash = 2900, bss = 2136, source = "CHANGELOG v2.2.14; nm 2026-09-25" }
maturity = { since = "2.2.14-beta", validated_by = "promotion/v2.3.2-stable/CHECKPOINT.md (golden vectors; ao vivo pendente)" }
hazards = ["nada sai com o AP no ar (manual cap. 25)"]

[feature."storage.backup_restore"]
kind = "chave"; domain = "arquivos"; default = true
files = ["ota/backup.cpp", "ota/restore.cpp"]
routes = ["GET /api/backup", "POST /api/restore?op=validate|apply"]
cost = { bss = 4496, source = "nm 2026-09-25" }
maturity = { since = "1.0.0", validated_by = "promotion/v2.3.2-stable/RELATORIO.md:168" }

# ---------------------------------------------------------------- rede
[feature."net.wifi_scan"]
kind = "chave"; domain = "rede"; default = true
routes = ["GET /api/wifi/scan"]
cost = { flash = 3400, page_gz = 1611, ram = 432, source = "flash_budget.json 2026-09-20" }
maturity = { since = "2.6.1-beta", validated_by = "README 2026-09-21 (18/18)" }

[feature."net.ap"]
kind = "chave"; domain = "rede"; default = true
commands = ["ap"]
maturity = { since = "1.1.0-beta", validated_by = "README 2026-09-22" }
hazards = ["com o AP no ar o aparelho não mede (R-R1)", "fecha em 15 min com reinício"]

[feature."net.ap_auto"]
kind = "chave"; domain = "rede"; default = false
requires = ["net.ap"]
conflicts = ["power.air"]
hazards = ["numa instalação sem Wi-Fi o aparelho para de medir (R-R2)"]

[feature."net.captive_dns"]
kind = "chave"; domain = "rede"; default = true
requires = ["net.ap"]
libs = ["DNSServer"]

[feature."net.mdns"]
kind = "chave"; domain = "rede"; default = true
macro = "SIMUT_MDNS"
libs = ["LEAmDNS"]
conflicts = ["power.air"]
requires_magnitude = { "lwip.udp_pcb" = 7 }
cost = { flash = 15804, source = "DIETA_FLASH §2" }
maturity = { since = "1.4.4-beta", validated_by = "" }        # nunca exercitado na bancada
hazards = ["o TXT anuncia a versão a toda a LAN (SECURITY.md §8)"]

[feature."net.https_server"]
kind = "chave"; domain = "rede"; default = true
macro = "SIMUT_WEB_HTTPS"
files = ["WebManager_Tls.cpp"]
implies = ["tls.lib"]
framework_patch = "2h"                                        # simut_reserve_tls_server_pool
routes = ["POST /api/tls"]
cost = { flash = 23872, heap = 22000, source = "CHANGELOG v2.2.5 (~20 KB) + 3.872 B; WebManager_Core.cpp:41-42 (~21,5 KB)" }
maturity = { since = "2.2.5-beta", validated_by = "CHANGELOG v2.4.10" }
hazards = ["uma conexão TLS por vez", "handshake 0,5–0,7 s", "OTA melhor por HTTP (manual §6)"]

[feature."net.bluetooth"]
kind = "chave"; domain = "rede"; default = false
macro = "SIMUT_BLUETOOTH"
files = ["BluetoothManager.cpp"]
build_flags = ["-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH"]
lib_unignore = ["SerialBT"]
flash_reserved = 8192                                         # TLV do BTstack (R-S6)
cost = { flash = 64732, bss = 16416, source = "platformio.ini:72-73 (ferro)" }
maturity = { since = "2.3.5-beta", validated_by = "PLANO_DIVIDA_TECNICA.md Fase 1 (V-01a/b 3/3)" }
hazards = ["TLV gravada sem Core1FlashPause quando há Core 1 (R-H24)", "só em M0 no Air"]

[feature."fleet.hooks"]
kind = "chave"; domain = "rede"; default = true
commands = ["system cors"]
cost = { flash = 2464, per_post = 90, source = "CHANGELOG v2.4.5; ANALISE_GESTOR_FROTA §8" }
maturity = { since = "2.4.3-beta", validated_by = "" }        # "nada executado em aparelho" (11/09)

[feature."tls.lib"]
kind = "interna"; domain = "rede"                              # ninguém a liga à mão
libs = ["BearSSL", "WiFiClientSecureBearSSL"]
cost = { flash = 114172, source = "DIETA_FLASH §2 (94.712 + 19.460)" }

# ---------------------------------------------------------------- web
[feature."web.page_tel"]
kind = "chave"; domain = "web"; default = true
requires = ["tel.http"]
pages = ["TEL"]
cost = { page_gz = 7784, source = "WebUI_GZ.h 2026-09-25" }

[feature."web.page_usr"]
kind = "chave"; domain = "web"; default = true
pages = ["USR"]
routes = ["GET /users", "GET /api/users"]
cost = { page_gz = 3161, source = "WebUI_GZ.h 2026-09-25" }

[feature."web.page_alarms"]
kind = "chave"; domain = "web"; default = true
requires = ["alarms.limits"]
pages = ["ALARMS"]
cost = { page_gz = 4491, source = "WebUI_GZ.h 2026-09-25" }

[feature."web.metrics"]
kind = "chave"; domain = "web"; default = true
files = ["WebManager_Metrics.cpp"]
routes = ["GET /metrics"]
cost = { flash = 4560, source = "CHANGELOG v2.2.13" }
maturity = { since = "2.2.13-beta", validated_by = "promotion/v2.3.2-stable (719 ms)" }

[feature."web.api_bench"]
kind = "instrumento"; domain = "web"; default = false
routes = ["GET /api/keypad", "GET /api/screenshot_chunk", "GET /api/history_multi", "POST /api/mkdir", "GET /api/sec_status"]
requires = ["panel.pin@keypad", "tft.mirror@screenshot_chunk"]

[feature."auth.strong_password"]
kind = "chave"; domain = "segurança"; default = true          # hoje presa ao HTTPS (R-S9)

# ---------------------------------------------------------------- telemetria
[feature."tel.http"]
kind = "chave"; domain = "telemetria"; default = true
libs = ["HTTPClient"]
cost = { flash = 31400, source = "DIETA_FLASH §2 (TelemetryManager 25,3 kB + HTTPClient 6.111)" }
maturity = { since = "1.0.0", validated_by = "PLANO_STABLE §6" }

[feature."tel.tls_client"]
kind = "chave"; domain = "telemetria"; default = true
requires = ["tel.http|tel.mqtt"]
implies = ["tls.lib"]
cost = { heap_ctx = 16000, ctx = 2, iobuf = 4608, source = "TelemetryManager.h:248,:259; .cpp:208" }
maturity = { since = "1.0.0", validated_by = "DIETA_FLASH §7 (sink HTTPS 6/6)" }

[feature."tel.mqtt"]
kind = "chave"; domain = "telemetria"; default = true
requires = ["tel.http"]
libs = ["PubSubClient"]
cost = { source = "unmeasured (dentro do 'resto' de 13.447 B)" }
maturity = { since = "1.0.0", validated_by = "promotion/v2.3.2-stable (7/7)" }

[feature."tel.csv_mode"]
kind = "chave"; domain = "telemetria"; default = true
requires = ["tel.http"]

[feature."tel.custom_payload"]
kind = "chave"; domain = "telemetria"; default = true
requires = ["tel.http"]
hazards = ["a CLI corta em 63 caracteres e responde OK (B12)"]

[feature."tel.alarm_line"]
kind = "chave"; domain = "telemetria"; default = true
requires = ["alarms.limits", "tel.http"]
commands = ["alarm show", "alarm set"]
cost = { bss = 2048, source = "AlarmQueue.h:158; nm" }
maturity = { since = "2.3.3-beta", validated_by = "PLANO_STABLE §6 (11/14)" }
hazards = ["fila cheia perde a borda recusada (#161)", "por MQTT exige t_int > 0"]

[feature."tel.ha_discovery"]
kind = "chave"; domain = "telemetria"; default = true
requires = ["tel.mqtt"]
cost = { flash = 2544, source = "CHANGELOG v2.2.13" }
maturity = { since = "2.2.13-beta", validated_by = "CHANGELOG v2.2.13" }
hazards = ["exige JSON e lote ≤ 5 em runtime (R-S14)"]

[feature."tel.tls_keepalive"]
kind = "instrumento"; domain = "telemetria"; default = false
macro = "TEL_TLS_KEEPALIVE_EXPERIMENT"
requires = ["tel.tls_client"]

# ---------------------------------------------------------------- console
[feature."cli.full"]
kind = "chave"; domain = "console"; default = false
macro = "SIMUT_CLI_FULL"
cost = { flash = 45056, source = "CHANGELOG v2.4.10 (Air)" }
maturity = { since = "1.0.0", validated_by = "as suítes de bancada" }

[feature."cli.bilingual"]
kind = "chave"; domain = "console"; default = true
requires = ["i18n.pack_ptbr|i18n.pack_eses"]
cost = { flash = 1000, source = "DIETA_FLASH §5" }

[feature."bench.cli_panel"]
kind = "instrumento"; domain = "console"; default = false
requires = ["cli.full", "display=tft"]
commands = ["screen", "touch sim", "touch hold", "show display keypad"]

# ---------------------------------------------------------------- energia
[feature."power.air"]
kind = "variante"; domain = "energia"; default = false
macro = "SIMUT_AIR"
files = ["AppManager_Air.cpp", "air/pico_sleep.c"]
implies = ["display=nenhum", "!sound.buzzer", "!net.mdns", "!net.ap_auto", "!hw.core1"]
requires = ["cli.full|net.bluetooth"]
pins = [16, 17]
maturity = { since = "2.4.0-beta", validated_by = "README 2026-09-23 (parcial: F28 mitigado; corrente nunca medida)" }
hazards = ["alarmes, OTA, web e BT só em M0", "recusa _dry e _nosave"]

[feature."air.charger_sense"]
kind = "chave"; domain = "energia"; default = true
requires = ["power.air"]
pins = [17]

# ---------------------------------------------------------------- bancada
[feature."bench.concurrency_asserts"]
kind = "instrumento"; macro = "SIMUT_CONCURRENCY_ASSERTS"; requires = ["display!=nenhum"]
[feature."bench.mirror_probe"]
kind = "instrumento"; macro = "SIMUT_MIRROR_PROBE"; requires = ["tft.mirror"]
[feature."bench.wdt_disabled"]
kind = "instrumento"; macro = "SIMUT_WDT_DISABLED"
[feature."bench.ap_open"]
kind = "instrumento"; macro = "SIMUT_AP_OPEN"; requires = ["net.ap"]
[feature."bench.ui_study"]
kind = "instrumento"; macro = "SIMUT_UI_STUDY"; files = ["UiStudy.cpp"]; requires = ["display=tft"]
[feature."bench.flash_irq_probe"]
kind = "instrumento"; default = true; files = ["FlashIrqProbe.cpp"]; build_flags = ["-Wl,--wrap=flash_range_erase", "-Wl,--wrap=flash_range_program"]

# ---------------------------------------------------------------- dados
[feature."i18n.pack_ptbr"]
kind = "dados"; default = true; data = ["data/lang/pt-BR.lng"]
cost = { fs = 39000, heap_peak = 16384, heap = 2900, source = "check_lang_packs; nm" }
[feature."i18n.pack_eses"]
kind = "dados"; default = false; data = ["data/lang/es-ES.lng"]
cost = { fs = 39028, heap_peak = 16305, source = "check_lang_packs (99,5 % do teto residente)" }

# ---------------------------------------------------------------- magnitudes (as 12 de cliente)
[magnitude."fs.size"]
unit = "KiB"; min = 256; max = 1024; step = 64; default = 1024; layout = true
rule = "com um slot, >= tamanho do .bin (R-F2)"
[magnitude."hist.quota"]
unit = "KiB"; min = 64; max = 960; default = 880              # hoje: 86 % do FS
derived = "retencao_dias = quota * 1024 / (5.4 * canais * 1440 / intervalo_min)"
[magnitude."log.records"]
unit = "registros"; min = 200; max = 4000; step = 100; default = 800
cost = "flash_fs = n * 12 * 2"
[magnitude."lang.packs"]
unit = "packs"; min = 0; max = 2; default = 1
[magnitude."themes.custom"]
unit = "temas"; min = 0; max = 16; default = 8
cost = "bss = n * 100"
[magnitude."sensors.slots"]
unit = "slots"; min = 1; max = 16; default = 16; layout = true
cost = "config = n * 143; ram = n * 95; heap_ativo = n * 1092"
[magnitude."hist.channels"]
unit = "canais"; min = 4; max = 32; default = 16; layout = true
cost = "ram = n * 260"
[magnitude."users.max"]
unit = "contas"; min = 1; max = 32; default = 32; layout = true
cost = "config = n * 70"
[magnitude."web.sessions"]
unit = "sessoes"; min = 1; max = 4; default = 3
[magnitude."tel.batch_max"]
unit = "registros"; min = 10; max = 250; default = 250
cost = "heap_pico = n * (70 + 350)"
[magnitude."alarms.queue"]
unit = "registros"; min = 8; max = 64; default = 64
cost = "bss = n * 32"
[magnitude."wifi.saved_nets"]
unit = "redes"; min = 1; max = 3; default = 1; layout = true
cost = "config = n * 64"

# ---------------------------------------------------------------- perfis de hoje
[profile.pico_w_release]
display = "tft"
on  = ["net.https_server", "net.mdns", "tft.license_text", "bench.flash_irq_probe"]
off = ["cli.full", "net.bluetooth", "power.air", "tft.theme_packs"]
# net.ap_auto fica LIGADA aqui, embora o default do modelo seja desligada: a fidelidade
# do P1 reproduz o que a release de hoje compila; desligá-la é decisão de perfil novo.
publish = true

[profile.pico_w_test]
extends = "pico_w_release"
on  = ["cli.full"]
off = ["net.https_server", "net.mdns", "tft.license_text"]

[profile.pico_w_test_https]
extends = "pico_w_test"
on  = ["net.https_server"]
fs_pages = ["ALARMS", "FILE", "TEL"]

[profile.pico_w_asserts]
extends = "pico_w_release"
on = ["bench.concurrency_asserts"]

[profile.pico_w_alpha]
display = "alpha_paralelo"
on  = ["net.bluetooth", "tft.license_text"]
off = ["cli.full", "net.https_server", "net.mdns"]
publish = true

[profile.pico_w_air]
display = "nenhum"
on  = ["power.air", "net.bluetooth", "cli.full"]
off = ["net.https_server", "net.mdns", "tft.license_text", "sound.buzzer"]
publish = true

[profile.pico_w_uistudy]
extends = "pico_w_test"
on = ["bench.ui_study"]
```

O rascunho não inventa chave: tudo o que ele nomeia existe no código de hoje,
com ou sem chave própria. O que ele muda é só o lugar onde isso está escrito.

---

## Apêndice B — Listas brutas do levantamento

### B.1 Emaranhamento por macro

"cond." conta linhas `#if`/`#ifdef`/`#ifndef`/`#elif`; "arq." os arquivos,
fora de `simut_config.h`. É a linha de base do gate que só aceita descer.

| macro | cond. / arq. | fora do subsistema natural |
|---|---:|---|
| `SIMUT_AIR` | 41 / 15 | `NetworkManager.cpp` (5), `WebManager_Commit.cpp`, `SoundManager.h`, `CommandParser.cpp`, `CommandManager.cpp`, `HelpLicenseEN.h`, `DisplayManager.h` |
| `SIMUT_DISPLAY_TFT` | 40 / 21 | `WebManager_Core.cpp`, `WebManager.h`, `WebManager_History.cpp`, `WebManager_Api.cpp`, os três drivers de sensor, `AppManager_Boot.cpp`, `SystemDefs_Limits.h`, `simut_config.h` |
| `SIMUT_UI_STUDY` | 39 / 14 | `AppManager_Commands.cpp`, `Themes.cpp`, `DisplayManager_i18n.cpp` |
| `SIMUT_SENSOR_DS18B20` | 35 / 13 | `StorageManager.cpp`, `WebManager_Calib.cpp`, `WebManager_Commit.cpp`, `AppManager_Boot.cpp`, `AppManager_CmdHandlers.cpp`, `AppManager_Commands.cpp` |
| `SIMUT_CLI_FULL` | 34 / 9 | `AppManager_Sensors.cpp`, `AppManager.h`, `HelpLicenseEN.h` |
| `SIMUT_PANEL_PIN` (derivada) | 27 / 13 | `WebManager_Commit.cpp` (4), `CommandManager.cpp` (4), `StorageManager.cpp` |
| `SIMUT_SENSOR_BME280` | 27 / 6 | — |
| `SIMUT_SENSOR_DHT22` | 23 / 6 | — |
| `SIMUT_DISPLAY_ALPHA` | 11 / 3 | — |
| `SIMUT_WEB_HTTPS` (`#ifdef`) | 11 / 4 | — (`CMD_HTTPS_OFF` sem guarda) |
| `SIMUT_MIRROR_PROBE` | 8 / 3 | `DisplayManager_Auth.cpp` (6), `CommandManager.cpp` |
| `SIMUT_MDNS` | 7 / 3 | — (um `#ifdef` em `SystemDefs_Network.h:61`) |
| `SIMUT_THEMES_*` (7, `#ifdef`) | 7 / 1 | — |
| `SIMUT_BLUETOOTH` | 6 / 4 | `AppManager_Boot.cpp`, `AppManager_Commands.cpp` |
| `SIMUT_WDT_DISABLED` (`#ifndef`) | 3 / 1 | — |
| `SIMUT_CONCURRENCY_ASSERTS` (`#ifdef`) | 2 / 2 | — |
| `SIMUT_LICENSE_STUB` (`#if defined`) | 1 / 1 | — |
| `SIMUT_AP_OPEN` | 1 / 1 | — |
| `SIMUT_FACTORY_RESET` | 0 / 0 | flag morta |
| `SIMUT_BOOT_SERIAL_LOG` | 0 / 0 | não existe em `src/`; variável de ambiente de ferramentas |

### B.2 O que cada ambiente exclui hoje

| ambiente | arquivos fora do link | bibliotecas | páginas |
|---|---|---|---|
| base (`pico_base`) | `UiStudy.cpp` | `SerialBT` (`lib_ignore`) | — |
| `pico_w_release`, `test`, `test_https`, `asserts` | + `DisplayManager_Alpha.cpp`, `BluetoothManager.cpp` | idem | `test_https`: `ALARMS`, `FILE`, `TEL` movidas para o FS |
| `pico_w_uistudy` | release + `UiStudy.cpp` de volta | idem | — |
| `pico_w_alpha` | `DisplayManager_Touch/Settings/Graph/Dashboard/Calibration/Auth/Alarm/Calendar/Fonts/i18n/LangParser.cpp`, `AppManager_Graph.cpp` | `lib_ignore` próprio (substitui a base) | `custom_web_omit = tft` |
| `pico_w_air` | alpha menos `i18n`/`LangParser`, mais `DisplayManager.cpp`, `DisplayManager_Alpha.cpp`, `SoundManager.cpp`; entra `DisplayManager_None.cpp` (redundante com `+<*.cpp>`) e `air/*.c` | + `BuzzerPIO` | `custom_web_omit = tft` |

Unidades que ficam **vazias** em vez de sair do filtro (o que muda a ordem de
link, 427 B medidos): `AppManager_Air.cpp`, `DisplayManager_None.cpp`,
`DisplayManager_Users.cpp`, `UiWidgets.cpp`, `WebManager_Tls.cpp`,
`BluetoothManager.cpp`.

O que cada ambiente diz de si, nos comentários do `platformio.ini`, e o que
não diz: a **release** ganhou `NDEBUG` só em 2026-09-18, quando era a única
sem ele e saía com um caminho absoluto de 116 B (`:168-175`); a **test** tem
a CLI completa porque as suítes usam `enable`, `configure terminal`, `write
memory` e `touch sim`, e tira o HTTPS ("cannot spare it") e o mDNS (15.376 B)
porque a bancada acha a placa por USB e IP (`:183-241`); a **test_https** é a
única com CLI completa e servidor TLS, não é publicada, e recebeu a dieta de
páginas em 2026-09-20 porque o `.bin` passava 1.420 B do teto (`:300-342`); a
**asserts** substitui a `pico_w_debug`, que nunca linkou (69 a 101 KB acima
do slot, `:264-298`); o **alpha** troca o TFT pelo HD44780 ("saves ~30 KB"),
liga o Bluetooth para o modo AP e tirou o mDNS para abrir espaço para ele
(commit `afe1d87`, 2026-08-24); o **Air** tem CLI completa desde 2026-09-18
porque serial e Bluetooth são a única interface local (+45.056 B, `:493-500`)
e tirou o mDNS por duas razões, o responder que nunca anunciava e a imagem
acima do teto de OTA (commit `7eefad8`, `:527-529`); **nenhum comentário diz
por que o Air usa `SIMUT_LICENSE_STUB`**, e o `HelpLicenseEN.h:237` o descreve
como "bench/test image only".

### B.3 Divergências entre documento, comentário e código

Cada linha é um defeito pela regra da casa (um comentário que discorda do
código é defeito). Nenhuma foi corrigida nesta análise; o P0 as leva.

| onde | o que diz | o que o código faz |
|---|---|---|
| `src/simut_config.h:281-282` | Bluetooth "BLE UART, ~22 KB" | SPP clássico; 64.732 B de flash e 16.416 B de `.bss` medidos |
| `src/simut_config.h:285` | mDNS "negligible flash cost" | 15.804 B |
| `src/simut_config.h:313` vs manual §5 | tema ~70 B vs ~85 B | um dos dois |
| `src/simut_config.h:107-117` | SPI do TFT a 62,5 MHz | teto provável de 24 MHz (R-H18) |
| `src/StorageManager.cpp:269-270` | `.wip` regravado "a cada 10 min" | a cada registro (1.440 vezes por dia a 1/min) |
| `src/ota/config_snapshot.cpp:27` | "margem atual ~3,4 KiB" | 1.430 B (esquema v25) |
| `src/AppManager_Boot.cpp:40-41` | o alpha paralelo usa GP8/9 | usa GP16–21 (`simut_config.h:222-237`) |
| `src/SensorManager.cpp:71-73` | buzzer em "pio0, com fallback pio1" | prefere `pio1`, cai para `pio0` |
| `sensors/BME280Driver.h:4-5, :68-69` | "PIO bit-bang em qualquer par" | prefere I2C de hardware em par nativo |
| `SystemDefs_Cli.h:24`, `HelpLicenseEN.h:19`, `AppManager_Commands.cpp:204`, `platformio.ini:305` | "10", "nine", "nine", "fourteen" comandos de emergência | 15 `CMD_*` no gate, contando `HELP` e `UNKNOWN` |
| `ConfigApply.h:176` | `CFG_USERS` não reinicia | está em `CFG_REBOOT_CLASSES` |
| `docs/CONCURRENCY.md:10-13` | o LittleFS pausa o Core 1 | não pausa; é o `Core1FlashPause` do SIMUT |
| `docs/CONCURRENCY.md:37-39` | pilha do Core 0 "~4 KB" | 2 KiB reservados no SCRATCH_Y, com os 8 KB dos dois bancos disponíveis |
| `docs/ANALISE_FLASH_RAM.md:212-213` | pilhas de 2.048 B nos dois núcleos | Core 1 com 8 KiB desde `a3324b4` (snapshot: registrar, não editar) |
| `docs/PIO_ANALYSIS.md` | 2 DHTBus, BME só em GPIO | 1 DHTBus partilhado; I2C de hardware (snapshot) |
| `docs/README.md:60` | `feat/fleet-api` "unmerged" | PR #114 na `main` desde a v2.4.3-beta |
| `docs/API_POST.md`, `SECURITY.md` §8 | "dez bits", teto `0x03FF` | `PERM_ALL_BITS 0x1FFF` |
| `docs/INTEGRACAO_SERVIDOR.md` §5.8 | `/api/alarms` e `/api/sensors` exigem DASHBOARD | exigem SYS_CONFIG |
| `docs/MANUAL.md` §6 | `/network` tem mDNS; `/files` cria diretórios; sem `/telemetry` na tabela | nenhum controle de mDNS; ninguém chama `/api/mkdir`; a página existe |
| `docs/MANUAL.pt-BR.md` §16 | sem "Fleet hooks", `/api/sensors` como "leituras ao vivo", `/api/action` sem `reboot` | o inglês tem os três |
| `docs/MANUAL.md` §9 | PIN de 4 a 8 dígitos | até 16 |
| `docs/CLI-Manual.md` | sem `alarm *`, `show display keypad`, `pressmin`, `pressmax`, `bmp280`; §9 põe o Air no console de emergência; o help diz "max 50" e ms | todos existem; o Air tem CLI completa; o lote vai a 250 e é contagem |
| `docs/adding-a-new-sensor.md:34` | a flag vai em `SensorConfig.h` | esse arquivo só inclui `simut_config.h`; o `SensorManager` não está na lista |
| `docs/analysis/PLANO_DIVIDA_TECNICA.md` (topo) | "F08, PR #103 em draft" | `84de35b` está na `main` (v2.4.4-beta) |
| `CHANGELOG.md` | sem v2.4.2, v2.4.3, v2.4.4 | as três tags existem |
| `CLAUDE.md:14` | `WebUI.h` "all 8 languages" | não há blocos `@LANG`; o produto tem três idiomas |
| `docs/WIRING.md:212` | "0–15 = slots" | GP8/9 são também a UART1 de boot no TFT (R-H5) |
| `docs/OTA_USAGE.md:46` | "o LittleFS é reformatado pelo apply" | é o stage que grava por cima do FS; um stage confirmado deixa o FS desmontado (R-F7) |
| `platformio.ini:146-147` | "repeat SerialBT there (see pico_w_alpha)" | o alpha não repete, e não pode: usa Bluetooth; a regra certa é repetir só num ambiente sem BT |
| `platformio.ini:490-491, :507` | o Air tem "same source set as pico_w_alpha… HD44780 driver kept" e "CONFIG_VERSION stays 21" | o Air tira `DisplayManager.cpp` e `_Alpha.cpp` e mantém `i18n`/`LangParser`; o esquema é v25 |
| `platformio.ini:187-193` | test e release: "two things differ" | diferem também em HTTPS e `LICENSE_STUB` |
| `src/WebManager_Core.cpp:250-253`; `AUTHORIZATION.md:140` | "HTTPS só na release" | `asserts` e `test_https` também compilam o servidor |
| `HelpLicenseEN.h:237` | `LICENSE_STUB` é "bench/test image only" | o Air publicado o usa |
| `src/simut_config.h:5` | "the ONLY FILE YOU NEED TO EDIT" | as variantes vivem no `platformio.ini` |
| `AGENTS.md:300-301` | telemetria desligada com Wi-Fi de pé mantém o Air acordado para sempre | com `telInterval = 0` a fase de envio vai para SLEEP (`src/AppManager_Air.cpp:457-465`) |
| `AGENTS.md:250-252`; `CLAUDE.md` ("matriz de autorização" entre os portões do `pio run`) | `pio run` roda a matriz de autorização | `check_authz.py` só roda no CI; não está nos `extra_scripts` (`platformio.ini:153-160`) |
| `platformio.ini:68-70`; `src/AppManager_Boot.cpp:1197` | "combined WiFi+BT radio blob" | `DIETA_FLASH.md` §11: "combined" é firmware + CLM |
| `src/SystemDefs_Network.h:140-141` | cita o `TelemetryGuard` | já não existe; o `SendGuard` alimenta o watchdog por IRQ contra o invariante 9 |
| `src/AppManager_Loop.cpp:335` vs `src/SystemDefs_Limits.h:74` | `SIMUT_BUILD_EPOCH` = 2025-09-20 num, 2026-07-30 no outro | um dos dois |
| `src/DisplayManager_LangParser.cpp:332-360`; `tools/check_lang_packs.py:321-333`; `CLAUDE.md` | uma entrada `@TRL` faltando deixa "só aquela linha" em inglês | `@TRL` não tem consumidor em runtime no TFT nem no Air: tudo sai em inglês (R-R17) |
| `Dockerfile:21-22`; `docker-compose.yml:9` | constroem a release com `pio run` | sem `patch.sh`; pelo R-S30 o link falharia (leitura de código, não executado) |
| `tools/build_release_pio.sh:66` | o zip PIO de release | apaga `tools/arduino_pico_overrides`, que o build exige |
| `release/simut_tft/`, `release/simut_alpha/` (`simut_arduino_config.h`) | zips para Arduino IDE | precisam do framework patcheado sem dizer, e trazem UI completa com flags padrão: combinação que nenhum ambiente de CI constrói |

### B.4 As outras magnitudes

As constantes de capacidade que o levantamento encontrou e que ficam fora do modelo: são de projeto (tempos de conversão, escadas de reconexão, prazos de parser, tamanhos de formato) e continuam fixas. Estão aqui para que ninguém as procure de novo; o valor é o literal do código em 25/09/2026.

| id | constante | valor | o que limita | onde |
|---|---|---|---|---|
| `sensor-pins` | `MAX_SENSOR_PINS` | 4 | GPIOs por sensor | src/simut_config.h:353 |
| `sensor-ch-defined` | `CH_COUNT` | 4 | quantidades conhecidas (temp, hum, press, lux) | src/sensors/SensorChannels.h:43 |
| `sensor-avg` | `MOVING_AVG_WINDOW` | 10 | janela da média aparada | src/SystemDefs_Limits.h:53 |
| `sensor-read` | `sensorDefaultIntervalMs()` | DS18B20 1000; DHT22 2000; BME/BMP280 5000 | cadência de leitura por tipo | src/sensors/SensorHelpers.h:133-145 |
| `sensor-sample-cfg` | `sampleIntervalMs` (`s_int`) | 2000 por padrão; aceita 1000..60000 | **nada**: é validado, gravado e ecoado, e nenhum código o lê | src/StorageManager.cpp:574; src/WebManager_Commit.cpp:979; src/WebManager_Api.cpp:231 |
| `sensor-timeouts` | `DS18B20_CONVERSION_TIME_MS` / `DHT22_READ_TIMEOUT_MS` / `BME280_MEAS_TIME_MS` | 750 / 150 / 15 | espera de conversão | src/SystemDefs_Time.h:73, :83; src/sensors/BME280Driver.h:28 |
| `calib-pts-buf` | `CALIB_PTS_BUF` | 224 | campo `pts` serializado | src/sensors/CalibCurve.h:44 |
| `calib-post` | corpo `> 8192`; `isRateLimited(5000)` | 8192 B; 1 POST a cada 5 s | tamanho e taxa do POST /api/calib | src/WebManager_Calib.cpp:531, :513 |
| `user-legacy` | `CFG_LEGACY_MAX_USERS` | 5 | layout v15..v23 lido pela migração | src/ConfigMigrate.h:58 |
| `user-name` | `username[16]` / `isValidName(…,15)` | 15 | nome de usuário | src/SystemDefs_Records.h:164; src/WebManager_Commit.cpp:1347; src/AppManager_Panel.cpp:354 |
| `user-password` | `passwordPolicyOk`; `p.length() > 128`; `strVal2[64]` | mínimo 8; ≤128 no login web; ≤63 na CLI | tamanho e força da senha (hash fixo `password[33]`) | src/SystemDefs_Validate.h:376; src/WebManager_Auth.cpp:575; src/SystemDefs_Cli.h:181 |
| `user-hmac` | `PASSWORD_HMAC_ROUNDS` | 5000 | custo do hash de senha | src/SystemDefs_Network.h:205 |
| `pin-lockout` | `SLOT_FAIL_MAX` / `PANEL_FAIL_CEILING` | 6 / 20 | bloqueio por conta e do painel inteiro | src/PinKeypad.h:126-127 |
| `pin-hash` | `PIN_HASH_LEN` | 8 | digest do PIN | src/SystemDefs_Limits.h:52 |
| `web-session-idle` | literal `900000` | 900000 | expiração da sessão por inatividade | src/WebManager_Auth.cpp:22 |
| `web-nonce` | `NONCE_LIFETIME_MS` | 60000 | validade do nonce de login | src/WebManager.h:207 |
| `auth-lockout` | `AUTH_FAIL_CAP` / `AUTH_LOCKOUT_MAX_MS` | 12 / 300000 | backoff de autenticação (web e BT) | src/SystemDefs_Network.h:220, :223 |
| `web-rate-intervals` | `isRateLimited(200)` / `isRateLimited(5000)` | 200 ms (ls, logs); 5000 ms (calib, tls) | taxa por IP e por rota | src/WebManager_Files.cpp:156; src/WebManager_History.cpp:1424; src/WebManager_Tls.cpp:162 |
| `web-preauth` | `WEB_PREAUTH_MAX_EXT` | 3 | quanto tráfego anônimo pode manter o Air acordado | src/SystemDefs_Network.h:248 |
| `hist-block-bytes` | `H5_BLOCK_MAX_BYTES` | 2118 | buffer de leitura de bloco `_h5Chunk` | src/HistoryV5.h:70-73; src/StorageManager.h:747 |
| `hist-wip` | `FILE_H5_WIP` | 1 arquivo, regravado inteiro a cada registro | snapshot do bloco aberto na RAM | src/StorageManager.h:33; src/HistoryV5.h:75-86 |
| `hist-wip-seal` | `H5_WIP_RETRY_MS` / `H5_SEAL_MAX_FAILS` | 2000 ms / 5 | retentativa do .wip / recusas antes de descartar o bloco | src/HistoryV5.h:89, :100 |
| `hist-prune-pace` | `deletionsLeft = 2`; fatias de 15000 e 60000 ms; `drainStorageLimit(4)` | 2 arquivos/chamada; 15 s; 60 s; 4 arquivos/wake | ritmo da poda | src/StorageManager.cpp:1403, :457-458; src/AppManager_Air.cpp:406 |
| `hist-epoch-min` | `HIST_EPOCH_MIN` | 1600000000 | piso de timestamp aceito | src/SystemDefs_Limits.h:92 |
| `hist-web-single` | `WEB_HISTORY_SINGLE_MAX` | 65536 | maior resposta de histórico sem exigir fatias | src/SystemDefs_Network.h:349 |
| `hist-web-est` | `WEB_HISTORY_BYTES_PER_RECORD` / `_PER_POINT` | 6 / 126 | estimativa de payload | src/SystemDefs_Network.h:356-357 |
| `hist-web-points` | literal `estRecs / 600` | 600 | alvo da decimação | src/WebManager_History.cpp:323 |
| `hist-web-files` | `filesToRead.size() > 40` | 41 | dias lidos por fatia from/to | src/WebManager_History.cpp:252 |
| `hist-web-ranges` | `rangeDuration[]` | 3600, 21600, 86400, 604800, 2592000, 31536000, 0 | faixas do gráfico web (1H a 1Y, MAX) | src/WebManager_History.cpp:167 |
| `hist-export` | `SIMX_MAX_RANGE_SECS` | 31×86400 | janela máxima do export binário | src/WebManager_History.cpp:1002 |
| `hist-multi-buf` | `static chunkBuf[2048]` | 2048 | acumulador JSON de /api/history | src/WebManager_History.cpp:507 |
| `graph-budget` | `GRAPH_BUDGET_MS`; preload de min/max 5000 | 6000 / 5000 | tempo para montar o gráfico / preload | src/AppManager_Graph.cpp:126; src/AppManager_HistoryAlarm.cpp:260 |
| `log-record` | `LOG_RECORD_SIZE` / `CompactLogRecord` | 12 | formato do registro binário | src/SystemDefs_Logging.h:482, :389-418 |
| `log-policy` | `LOGPOL_HEARTBEAT_MS` / `LOGPOL_REPORT_MS` | 3600000 / 3600000 | re-persistência por família / relatório de supressão | src/LogPolicy.h:93, :96 |
| `log-console` | `MAX_CHUNK` | 200 | fatia de linha no console | src/LogManager.cpp:116 |
| `tel-batch-min` | `TEL_BATCH_MIN` | 10 | piso do controle AIMD | src/simut_config.h:536 |
| `tel-cadence` | `TEL_FAST_MS_PLAIN` / `TEL_FAST_MS_TLS` / `TEL_GAP_MAX_MS` / `TEL_FIRST_SEND_DELAY_MS` | 400 / 2500 / 10000 / 8000 | ritmo do dreno | src/simut_config.h:515, :518, :521, :543 |
| `tel-heap` | `HEAP_RESERVE` / `BYTES_PER_ENTRY` | 32768 (TLS) ou 12288; 350 (JSON) ou 160 (CSV) | teto dinâmico do lote pelo heap livre | src/TelemetryManager.cpp:626-627 |
| `tel-preflight` | `PREFLIGHT_FLOOR` | 24576 (TLS) ou 14336 | heap mínimo para tentar um ciclo | src/TelemetryManager.cpp:397, :2502 |
| `tel-build` | `SEC_RESERVE` / `perLine` / `fixedPart` | 12288 ou 6144; 300 ou 120; 256 ou 640 | encolhimento do payload | src/TelemetryManager.cpp:1668-1677 |
| `tel-backoff` | `BACKOFF_MIN_MS` / `BACKOFF_MAX_MS` / `BACKOFF_MAX_STREAK` | 5000 / 300000 / 10 | backoff após falha | src/TelemetryManager.h:218, :222, :223 |
| `tel-30d` | `86400UL * 30` | 30 | backlog enviado quando o cursor é 0; o que for mais antigo nunca sobe | src/TelemetryManager.cpp:671, :2184 |
| `tel-pending` | saturação em `0xFFFF` | 65535 | contador de pendentes | src/TelemetryManager.cpp:2317 |
| `tel-cursor` | `CURSOR_COALESCE_MS` | 5000 | escrita do cursor na flash | src/SystemDefs_Network.h:369 |
| `tel-templates` | `telGlobalTemplate[256]` / `telLineTemplate[512]` / `telLineSeparator[8]`; `lineBuf[512]` e `[1024]` | 256/512/8; 512/1024 | templates de payload e linha formatada | src/SystemDefs_Records.h:372-374; src/TelemetryManager.cpp:1718, :1776 |
| `tel-csv-line` | `csvBuf[256]` | 256 | linha CSV para 16 slots | src/TelemetryManager.cpp:1358, :1442, :1755 |
| `tel-strings` | `telServer[64]`, `telPath[32]`, `telApiKey[64]`, `mqttTopic[64]`, `mqttUser[32]`, `mqttPass[32]`, `mqttClientId[24]` | — | servidor e credenciais | src/SystemDefs_Records.h:364-383 |
| `tel-tls-ka` | `TEL_TLS_KEEPALIVE_EXPERIMENT` | 0 | manter a sessão TLS entre lotes | src/simut_config.h:558 |
| `mqtt-buffer` | `setBufferSize(2048)` / `MQTT_BUFFER_CEILING` / `MQTT_PACKET_OVERHEAD` | 2048 / 8192 / 16 | pacote MQTT (cresce até o teto) | src/TelemetryManager.cpp:227, :60-61, :1462 |
| `alarm-batch` | `ALARM_BATCH_MAX` / `ALARM_RETRY_INTERVAL_MS` | 64 / 15000 | lote e retentativa | src/TelemetryManager.h:167-168 |
| `alarm-templates` | `AlarmTelConfig` (`path[32]`, `globalTemplate[256]`, `lineTemplate[512]`, `lineSeparator[8]`) | 811 | templates da linha de alarmes | src/SystemDefs_Records.h:243-252 |
| `maint-max` | `MAINT_MAX_SEC` | 30×24×3600 | duração máxima da manutenção por slot | src/SystemDefs_Records.h:262 |
| `web-clients` | `for (i < 4)`; orçamento `+50`; prazo `+6000`; `HTTP_MAX_DATA_AVAILABLE_WAIT` 30 | 4 requisições/tick; 50 ms; 6000 ms; 30 ms | vazão; o WebServer atende **1 cliente por vez** | src/WebManager_Core.cpp:572, :566, :552; tools/arduino_pico_overrides/originals/1.50601.0/HTTPServer.h:49 |
| `web-long-deadline` | `WEB_LONG_HANDLER_DEADLINE_MS` | 15000 | handlers longos (histórico, logs, screenshot) | src/SystemDefs_Network.h:286 |
| `web-upload-limits` | `UPLOAD_FILENAME_MAX`; `isSafeDirPath` 96; `slashCount > 2`; `cl > freeBytes` | 64 chars; 96 chars; 2 níveis; espaço livre | nome, profundidade e tamanho de upload | src/SystemDefs_Validate.h:308, :344; src/WebManager_Files.cpp:345, :419-422 |
| `web-ls-batch` | `DirEntry batch[20]` | 20 | lote de listagem de diretório | src/WebManager_Files.cpp:249-261 |
| `web-commit-body` | `body.length() > 6144` | 6144 | JSON do /api/commit_all (a config via web) | src/WebManager_Commit.cpp:331 |
| `web-tls-body` | `> 8192` | 8192 | POST /api/tls e cada PEM lido no boot | src/WebManager_Tls.cpp:176; src/WebManager_Core.cpp:408, :410 |
| `web-cors` | `CORS_ORIGIN_MAX_LEN`; `cors.txt` ≤128 | 64 / 128 | origem CORS | src/CorsOrigin.h:38; src/WebManager_Core.cpp:326 |
| `web-chunks` | `WEB_STREAM_CHUNK_SOFT` / `SCREEN_SEND_COALESCE` / `CHUNK` do stream de arquivo / `SEND_FRAMING_SLACK` | 512 / 1024 / 1024 / 16 | fatias de envio HTTP | src/SystemDefs_Network.h:306, :326; src/WebManager_Send.cpp:359, :45 |
| `web-send` | `WEB_SEND_STALL_MS` / `WEB_STREAM_BREATH_RECORDS` / `WEB_STREAM_BREATH_DELAY_MS` / `WDT_FEED_MAX_WINDOW_MS` | 4000 / 64 / 2 / 120000 | cliente lento, pausas entre fatias, teto do SendGuard | src/SystemDefs_Network.h:337-338, :358, :153 |
| `web-parse` | `SIMUT_PARSE_BUDGET_MS` / linha ≥2048 / `SIMUT_BODY_BUDGET_MS` | 3000 / 2048 / 15000 | parse de request e de corpo | tools/arduino_pico_overrides/patches/webserver_parse_deadline.patch:23, :34, :76 |
| `web-post-args` | `WEBSERVER_MAX_POST_ARGS` / `HTTP_UPLOAD_BUFLEN` | 32 / 1436 | limites do framework | tools/arduino_pico_overrides/originals/1.50601.0/Parsing.cpp:35; …/HTTPServer.h:41 |
| `metrics` | `acc[1024]`; `creds[160]`; senha ≤128 | — | Prometheus /metrics | src/WebManager_Metrics.cpp:122, :51, :58 |
| `web-pages` | `*_PAGE_GZ_LEN` (12 assets) | 90.138 B no total (HIST 22.544; CFG 12.471; LANG_JS 10.530…) | UI embutida | src/WebUI_GZ.h:428…5705 (arquivo gerado) |
| `net-timeouts` | `NET_TLS_HANDSHAKE_MS` / `NET_SOCKET_TIMEOUT_MS` | 15000 / 4000 | prazos de rede | src/SystemDefs_Network.h:42, :26 |
| `lwip-snd` | `TCP_SND_BUF` / `TCP_SND_QUEUELEN` | 8×1460 = 11680 / 32 (cálc.) | buffer de envio | …/patched_headers/lwipopts.h:84-85 |
| `lwip-backlog` | `TCP_LISTEN_BACKLOG` / `TCP_DEFAULT_LISTEN_BACKLOG` | 1 / 2 | fila de accept | …/patched_headers/lwipopts.h:86-87 |
| `lwip-misc` | `MDNS_MAX_SERVICES` / `SNTP_MAX_SERVERS` / `LWIP_NUM_NETIF_CLIENT_DATA` / `LWIP_STATS`+`MEMP_STATS` | 4 / 2 / 5 / 1+1 | — | …/patched_headers/lwipopts.h:106, :120, :91, :128, :132 |
| `tft-spi` | `SIMUT_TFT_SPI_HZ` / `SIMUT_TFT_READ_HZ` | 62500000 / 12000000 | escrita e leitura do painel | src/simut_config.h:117, :153 |
| `screen-stream` | `STRIP_ROWS` 8, `STRIP_RAW` 5120, DMA 2×7680 | — | /api/screen_stream | src/WebManager_History.cpp:1857-1860, :1927-1940 |
| `screenshot` | `ROWS_PER_CHUNK` 16 | 15360 | /api/screenshot | src/WebManager_History.cpp:1622-1623, :1702 |
| `rle` | `MAX_PALETTE` / `MAX_RUN` | 256 / 256 | codec do espelho de tela | src/ScreenRle.h:88-89 |
| `ui-queues` | `UI_EV_RING` / `CLI_QUEUE_CAP` / `QUEUE_SIZE` (som) | 16 / 2 / 4 | filas entre núcleos, CLI adiada, som | src/DisplayManager.h:719; src/AppManager.h:206; src/SoundManager.h:164 |
| `text-bufs` | `_licenseBuf[2048]` / `_alphaHelpBuf[2048]` / `_lazyReadBuf[2048]` | 2048 cada | licença e ajuda na tela (truncam em 2.047) | src/DisplayManager.cpp:64; src/DisplayManager_Alpha.cpp:23; src/DisplayManager_LangParser.cpp:410 |
| `core0-stack` | SCRATCH_Y do linker | 4096 (2048 reservados em `.stack_dummy`) | pilha do Core 0 (NO_SYS, sem tarefas RTOS) | .pio/build/pico_w_release/memmap_default.ld:29, :269-284 (gerado pelo framework) |
| `lang-keys` | `TR_KEYS_COUNT` | 154 | strings do painel | src/DisplayManager.h:40-148 |
| `lang-count` | `LANG_COUNT` | 2 | idiomas embutidos | src/SystemDefs_Records.h:72 |
| `theme-strings` | `THM_ID_MAX` / `THM_NAME_MAX` | 16 / 24 | id e nome do tema | src/Themes.cpp:304-305 |
| `theme-builtin` | `SIMUT_THEMES_*`; `themeIndex` é int8_t | 1 tema core + packs; índice ≤127 | temas compilados | src/simut_config.h:313-324; src/SystemDefs_Records.h:393 |
| `wifi-reconnect` | `WIFI_RECONNECT_BASE_MS`, `MAX_RECONNECT_DELAY`, `WIFI_MAX_CONNECT_CYCLES`, `WIFI_DORMANT_DELAY_MS`, `WIFI_DORMANT_MAX_WAITS`, `WIFI_SCANS_BEFORE_BLIND_JOIN`, `WIFI_SCAN_TIMEOUT_MS`, associação | 5000; 120000; 5; 600000; 3; 2; 15000; 20000 | escada de reconexão | src/SystemDefs_Network.h:67, :73, :76, :88, :105, :118; src/NetworkManager.h:266; src/NetworkManager.cpp:656 |
| `ntp` | `NTP_MAX_RETRY_DELAY_MS` / `NTP_FAILS_BEFORE_FALLBACK` / `NTP_SUSPECT_DELTA_S` / `ntpServer[32]` | 900000 / 3 / 3600 / 31 chars | NTP | src/SystemDefs_Network.h:162, :169, :186; src/SystemDefs_Records.h:400 |
| `hostname` | `deviceName[32]` | ≤31 chars | hostname mDNS, SSID do AP (+"_SETUP"), syslog | src/SystemDefs_Records.h:351; src/NetworkManager.h:244-247; src/NetworkManager.cpp:460 |
| `mdns` | `SIMUT_MDNS` / `MDNS_UPDATE_INTERVAL_MS` | 1 no release (0 em test, alpha e air) / 2000 | responder mDNS | src/simut_config.h:293; src/SystemDefs_Network.h:63 |
| `ap` | `AP_MODE_TIMEOUT_MS` / `AP_PSK_LEN` / `AP_HOLD_DURATION_MS` | 900000 / 10 / 3000 | modo AP | src/SystemDefs_Network.h:363; src/ApPsk.h:45; src/SystemDefs_Time.h:23 |
| `rssi` | `RSSI_MIN_THRESHOLD` | -78 | abaixo disso adia telemetria e upload | src/SystemDefs_Network.h:49 |
| `bt` | `BT_AUTH_BUFFER_MAX` / `BT_DISCOVERABLE_MS` / `_timeoutMs` | 64 / 300000 / 300000 | Bluetooth (alpha e air) | src/SystemDefs_Network.h:253, :274; src/BluetoothManager.h:50 |
| `cli` | `CLI_LINE_MAX` / `strVal1/2[64]`, `strVal3[32]` / `parts[6]` | 256 / 63 / 31 / 6 tokens | linha e argumentos da CLI | src/SystemDefs_Network.h:280; src/SystemDefs_Cli.h:180-182; src/CommandParser.cpp:80 |
| `air-wake` | `AIR_WAKE_INTERVAL_MIN` | 5 | período de wake quando `h_int` lê 0 | src/simut_config.h:403 |
| `air-phases` | `AIR_STAB_TIMEOUT_MS` / `AIR_CONNECT_TIMEOUT_MS` / `AIR_FLUSH_TIMEOUT_MS` / `AIR_FLUSH_TAIL_MS` | 30000 / 30000 / 30000 / 5000 | duração das fases do wake | src/simut_config.h:409, :415, :418, :427; src/air/AirConfig.h:139, :153-154 |
| `air-scan-dead` | `AIR_WIFI_SCAN_TIMEOUT_MS` | 4000 | nada (o campo virou `chargerPin`) | src/simut_config.h:412; src/air/AirConfig.h:140-150 |
| `air-attempts` | `AIR_MAX_CONNECT_ATTEMPTS` | 2 | tentativas de Wi-Fi por wake | src/simut_config.h:451 |
| `air-sleep` | `AIR_MIN_SLEEP_SEC` / `AIR_MAX_SLEEP_SEC` | 5 / 86399 | limites do alarme de RTC | src/simut_config.h:459; src/air/AirConfig.h:269 |
| `air-boot` | `AIR_WAKE_BOOT_MS` | 140 | calibração do relógio no wake | src/simut_config.h:472 |
| `air-resume` | `AIR_RESUME_GRACE_SEC` / `AIR_MAX_DIRTY_BOOTS` | 10 / 3 (campo de 4 bits, ≤15) | guarda contra crash-loop | src/simut_config.h:484, :498; src/air/AirConfig.h:179-193 |
| `air-tel-skip` | `AIR_TEL_FAIL_SKIP_WAKES` | 5 | silêncio após falha de envio | src/simut_config.h:491 |
| `air-scratch` | `AIR_WAKES_MAX` / `AIR_SLEPT_SEC_MASK` | 127 / 131071 | contadores em registrador scratch | src/air/AirConfig.h:55-57 |
| `air-guard` | `AIR_WAKE_GUARD_MS` | 3000 | watchdog de guarda no wake | src/air/pico_sleep.h:28 |
| `air-pins` | `AIR_SENSOR_POWER_PIN` / `AIR_CHARGER_PIN` | 16 / 17 | alimentação dos sensores / detecção do carregador | src/simut_config.h:430, :437 |
| `ota-meta` | `OTA_METADATA_SIZE` / `UpdateMetadata` | 4096 / 256 | metadata da OTA | src/ota/ota_layout.h:60; src/ota/metadata.h:66-75 |
| `ota-snapshot` | `OTA_SNAPSHOT_SIZE` / `CONFIG_SNAPSHOT_PAYLOAD_MAX` | 8192 / 8172 | cópia da config que sobrevive à OTA | src/ota/ota_layout.h:77-79; src/ota/config_snapshot.h:48-51 |
| `ota-attempts` | `OTA_MAX_APPLY_ATTEMPTS` | 3 | anti-loop do apply | src/ota/metadata.h:63 |
| `ota-walk` | `MAX_WALK_DEPTH` / `RESTORE_WALK_MAX_DEPTH` / `IO_CHUNK` | 8 / 8 / 512 | backup e restore | src/ota/backup.cpp:118-119; src/ota/restore.cpp:21 |
| `ota-env-tag` | `SIMUT_ENV_TAG_MAX` | 32 | tag da variante gravada na imagem | src/BuildIdentity.h:24 |
| `config-size` | `sizeof(SystemConfig)` = `CFG_V25_BLOB` | 6738 | registro de configuração | src/ConfigMigrate.h:72, :97 |
| `config-reserved` | `reserved[64]` / `RESERVED_FREE_OFFSET` | 64 / 64 (cheio) | overlays que dispensam migração | src/SystemDefs_Records.h:417; src/SystemDefs_Reserved.h:80-81 |
| `config-version` | `CONFIG_VERSION` | 25 | versão do schema | src/StorageManager.cpp:105 |
| `config-save-rate` | `MIN_SAVE_INTERVAL_MS` | 1000 | intervalo mínimo entre saves | src/StorageManager.cpp:1119 |
| `wdt` | `WATCHDOG_TIMEOUT_MS` | 8388 | teto de qualquer operação sem alimentar o watchdog | src/SystemDefs_Time.h:53 |
| `heap-obs` | limites de log de heap 32768 e 16384; sonda de maior bloco a 90% com margem de 512 | — | observabilidade do heap | src/AppManager_HistoryAlarm.cpp:453, :463; src/MetricsManager.cpp:34-35 |
