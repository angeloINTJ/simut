# OTA de slot duplo no Pico 2 W — desenho

Estado: etapas 1 a 6 **feitas** (a lista está em [Etapas](#etapas)). Da 7,
o workflow e o manifesto estão prontos; falta a release. Etapa S2 da Fase 4 de
[`PLANO_REVISAO_EXTERNA.md`](PLANO_REVISAO_EXTERNA.md). Decisões do
mantenedor em 03/10/2026:

- **Layout de 4 MB:** tabela de partições de 8 KB, slots A e B de 1.532 KB cada,
  LittleFS de 1.020 KB e o setor de EEPROM de 4 KB que o arduino-pico reserva no
  fim. Corrige o "1,5 + 1,5 + 1,0 MB" de 01/10, que somava os 4.096 KB inteiros
  e não deixava lugar para a tabela nem para a EEPROM.
- **Confirmação da imagem nova:** web no ar e 60 s sem falha.
- **Boot seguro e OTP:** sim nos aparelhos de produção; nunca na placa da
  bancada.

O que diz "pelo código" foi lido no framework instalado
(`framework-arduinopico` 1.50601.0, arduino-pico 5.6.1, pico-sdk 2.2.1-develop),
no `picotool` 2.1.1 em `/home/angelo/pico/picotool` e em quatro arquivos do
repositório `raspberrypi/pico-bootrom-rp2350`, na versão atual dele. Os números
de linha estão na pesquisa de 03/10/2026, guardada fora do repositório.

## Na placa da bancada (03/10/2026)

- **O chip é um RP2350 A2, não A4** (`picotool info -a`: `revision: A2`, ROM
  `gitrev 0x312e22fa`). O A4 corrige o erratum E9, um vazamento nas entradas de
  GPIO que impede o pull-down interno de segurar um nível baixo. As suítes de
  bancada do S1 precisam conferir as entradas do SIMUT com ele. A ROM do A2 pode
  diferir da versão atual do repositório da ROM, que é a fonte deste desenho, e
  os testes do S2 conferem cada comportamento citado nesta placa.
- **O boot seguro está desligado** (`secure boot: 0`), e fica assim.
- **A `pico2_w_release` do `main` sobe.** Gravada pelo USB (`badb4a8`, 993.040 B),
  ela cria a configuração de fábrica, inicia o buzzer, os sensores e a rede, pede
  a data e a hora, porque não há rede configurada, e chega ao prompt do console.
  O `picotool reboot` reinicia pelo watchdog, e a autópsia registra um falso
  travamento (`ctx=455`, rastro vazio), o mesmo artefato do `picotool -x` no
  RP2040.
- **O IMAGE_DEF que a ROM enxerga é o do stub.** O `picotool info` da imagem
  mostra um só bloco de imagem, em `0x10000124`, sem versão e sem a marca de
  teste. O laço de blocos fecha nele mesmo, pelo bloco em `0x100027f8`.

## O problema

No RP2040 a OTA tem uma área só para tudo (`src/ota/ota_layout.h`): a imagem
nova é recebida em cima do LittleFS, e o applier a copia sobre o programa em
execução.

1. **Toda atualização apaga o sistema de arquivos.** Histórico, pacotes de
   idioma, calibração: o stage escreve por cima deles, também quando o envio é
   cortado ou a imagem recusada (capítulo 17 do manual). A configuração volta de
   uma cópia em dois blocos; o resto volta só se alguém restaurar o `.bkp` que a
   página baixa antes. No portão da v2.10.0, o backup tirado depois das recusas
   e dos envios cortados tinha 8 arquivos em vez de 74.
2. **A janela do apply pede BOOTSEL.** Uma queda de energia nos ~25 s em que o
   applier apaga e regrava o programa deixa o aparelho sem firmware
   (`VERIFICATION.md`, 11/09/2026).
3. **Uma imagem que trava não volta sozinha.** A assinatura prova a origem, não
   que a imagem sobe neste aparelho.

O RP2350 resolve os três na ROM: ela conhece uma tabela de partições com pares
A/B, sobe a imagem nova em teste (*try before you buy*, TBYB) e volta para a
anterior se a nova não se confirmar.

## O que o A/B garante, e o que não

| Garante | Não garante |
|---|---|
| A atualização não toca no sistema de arquivos: histórico, pacotes e calibração ficam | Nada contra quem tem o USB: o BOOTSEL grava qualquer coisa, a não ser com o boot seguro de produção |
| Uma queda de energia durante a gravação deixa a imagem anterior inteira | Uma imagem que sobe, confirma e falha depois: depois do *buy* ela é a imagem do aparelho |
| Uma imagem que não chega à web em pé por 60 s volta sozinha para a anterior | A configuração: se uma imagem nova a migrar antes de confirmar, a anterior não a lê (ver "A configuração") |
| A assinatura do SIMUT continua valendo (`OTA_ASSINADA.md`), agora sobre o slot inativo | |

## O layout

| Região | Offset na flash | Tamanho |
|---|---:|---:|
| Tabela de partições | `0x000000` | 8 KB |
| Slot A | `0x002000` | 1.532 KB (`0x17F000`) |
| Slot B | `0x181000` | 1.532 KB |
| LittleFS | `0x300000` | 1.020 KB (`0xFF000`) |
| EEPROM do arduino-pico | `0x3FF000` | 4 KB |

- **O LittleFS e a EEPROM ficam onde o builder já os põe.** O builder da
  plataforma calcula `FS_START = 0x10000000 + flash − EEPROM − filesystem_size`
  (`builder/main.py:78-80`). Com `board_build.filesystem_size = 1020k`, isso
  dá `0x10300000`. O fim fica onde estava, e o começo anda 4 KB, de
  `0x2FF000` para `0x300000`. Uma placa que já rodava a imagem de antes perde
  o LittleFS ao mudar de layout, se ele não for levado junto; a da bancada foi
  migrada em 03/10/2026 (`tools/rp2350/README.md`, no passo 3).
- **O teto de uma imagem passa a ser 1.532 KB.** O
  `flash_budget.json` e a tabela de folga da §3.1 do `PLANO_STABLE.md` mudam
  com ele. A imagem release do RP2350 hoje tem 1.005.352 B, ou 64 % de um slot.
- **A tabela vira uma partição de dados?** O LittleFS pode ficar fora da tabela
  ou como partição de dados nela. Proposta: partição de dados. A tabela então
  descreve a flash inteira, o `picotool partition info` mostra o layout, e um
  `.uf2` gravado pelo BOOTSEL não cai em cima dela por engano, porque a ROM
  coloca cada `.uf2` pela família.

## A imagem

**Sem o stub de OTA do arduino-pico.** No RP2350 o framework põe um stub de
12 KB (`ota.o`) no início de toda imagem (`lib/rp2350/memmap_default.ld:44-60`):

- o bloco IMAGE_DEF que a ROM enxerga é o do stub, porque é o único nos
  primeiros 4 KB;
- o stub salta para um endereço fixo, `0x10003000`;
- ele monta o LittleFS lendo pelo XIP, no endereço do sistema de arquivos.

Com o slot mapeado pela ROM, essa leitura cai fora da janela traduzida e falha
antes de o programa começar. O caminho é um linker script nosso, que o
PlatformIO aceita (`board_build.ldscript`), sem a seção `.OTA`. O IMAGE_DEF do
`crt0` passa então a ser o primeiro. O PicoOTA do framework (cópia de arquivo
pelo stub) deixa de existir nesta imagem, e o SIMUT não o usa.

**Uma imagem roda em qualquer slot.** A ROM mapeia o slot escolhido em
`0x10000000`, com a janela do tamanho da partição. O mesmo `.bin` serve para A
e para B.

**O LittleFS lido pela janela sem tradução.** O LittleFS do framework lê a flash
por `memcpy` do endereço mapeado (`LittleFS.cpp:177`) e escreve pelo offset
físico (`:190`, `:206`). Fora da partição mapeada, a leitura precisa da janela
`0x1C000000` (`XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE`). Seria o 13º patch em
`tools/arduino_pico_overrides/`. As leituras diretas do SIMUT, da staging e dos
metadados, mudam do mesmo jeito.

**A marca de teste (TBYB) só na imagem de OTA.** É o bit `0x8000` do tipo de
imagem no IMAGE_DEF (`boot/picobin.h:88`). O `crt0` vem compilado em
`lib/rp2350/libpico.a`, então a marca entra por um passo depois do link, que
corrige o bit antes da assinatura do SIMUT (o trailer cobre o bit). Uma imagem
com TBYB gravada pelo USB e iniciada num boot normal nunca sobe. Por isso:

- o `.uf2` de fábrica e de USB sai **sem** a marca;
- o `.bin` de OTA sai **com** ela.

São dois artefatos por variante. Desde a etapa 5, o `check_rp2350_image.py`
escreve o `firmware_ota.bin` ao lado do `firmware.bin`: o mesmo arquivo, com o
bit ligado pelo `mark_trial` de `tools/rp2350/picobin.py`. O passo confere que
um byte só mudou e recusa um bloco com hash ou assinatura (o boot seguro, na
etapa S3, decide como marcar uma imagem selada). O `release-ota.yml` publica os
dois desde a etapa 7 (ver [A release](#a-release-etapa-7)).

**Item de versão no IMAGE_DEF.** Com versões iguais, ou sem elas, a ROM prefere
o slot A. B só vence com versão estritamente maior e sem a marca de teste
(`varm_flash_boot.c:237-318`). Ao confirmar B com versão igual, a ROM apaga o
primeiro setor de A (`:294-298`). As duas formas funcionam:

- sem versão, a confirmação de B apaga A;
- com versão, a mais nova vence sem apagar nada.

Proposta: gravar a versão. O boot seguro de produção vai precisar dela para o
anti-rollback em OTP, e numa placa sem boot seguro ela só decide qual slot
sobe.

## O stage, no slot inativo

1. **Qual é o slot inativo.** `rom_get_boot_info( )` diz em qual partição o
   aparelho subiu e se há uma confirmação pendente (`bootrom.h:1061-1083`).
   `rom_load_partition_table` (com uma área de trabalho de 3.264 B) e
   `rom_get_partition_table_info` dão os offsets.
2. **A gravação.** Ela usa `flash_range_erase`/`flash_range_program`, pelo
   offset físico e com o Core 1 pausado como hoje (`Core1FlashPause`, porque
   `FLASH_OP` não pausa). O **primeiro setor do slot é gravado por último**:
   enquanto ele não existe, o slot não tem IMAGE_DEF válido, e um corte de
   energia deixa a ROM na imagem anterior. O applier de hoje faz o contrário
   (`applier.cpp:255-272`).
3. **A verificação,** antes de pedir o reinício:
   - a assinatura do SIMUT sobre o slot, como hoje (`v` 0);
   - o `env` da variante;
   - o IMAGE_DEF de RP2350 no lugar do CRC do boot2 do RP2040
     (`validation.cpp:111-118`);
   - o tamanho, que cabe no slot.
4. **O LittleFS não é tocado.** O stage deixa de desmontar e formatar o sistema
   de arquivos. Com isso saem a cópia da configuração nos blocos 254–255, o
   applier, o estado APPLYING e o contador de tentativas.
5. **O apply** reinicia na imagem nova:
   `rom_reboot(FLASH_UPDATE | NO_RETURN, atraso, XIP_BASE + offset do slot, 0)`
   (`bootrom.h:482-505`). O `picotool` passa o mesmo endereço
   (`main.cpp:4682-4683`).

## O primeiro boot, em teste

Feito na etapa 5. As peças: `src/ota/trial.h` (a regra, pura, nas suítes
nativas), `src/ota/staging.cpp` (o que roda na placa) e
`tools/rp2350/picobin.py` (a marca).

**O que a ROM faz** (`varm_launch_image.c:208-458`):

- marca a confirmação como pendente;
- arma um reinício do tipo normal para 0xFFFFFF ms. No watchdog isso dá cerca de
  16,8 s, porque o RP2350 não tem o erratum de contagem dupla do RP2040.

Se o watchdog vencer antes da confirmação, o boot seguinte é normal, e a ROM
recusa a imagem ainda marcada e sobe a outra.

**O watchdog no `setup( )`.** Este desenho previa uma armadilha: a primeira
alimentação do watchdog dentro do `setup( )`, com o `load_value` do SDK ainda em
0, reiniciaria o chip na hora. Ela não acontece. A primeira linha do `setup( )`
desliga o watchdog em todo boot (`main.cpp`), porque o `setup( )` leva dezenas
de segundos e só o `loop( )` o alimenta. O reinício de teste da ROM morre ali
também, e uma imagem que travasse no `setup( )` ficaria travada até alguém
reiniciar a placa.

Por isso, num boot em teste, `trial_guard_begin( )` religa o watchdog com 16 s,
e um timer o alimenta a cada segundo até o `loop( )` armar o dele (8,4 s) ou o
prazo do teste passar. Passado o prazo, o timer para, o watchdog vence, e a ROM
volta para a imagem anterior. Os outros boots não mudam.

**A confirmação** (decisão de 03/10/2026) acontece depois de 60 s seguidos com:

- o LittleFS montado e a configuração lida. Uma configuração que estava na
  flash e que esta imagem não conseguiu ler conta contra ela;
- a rede como a configuração pede: conectada, quando há Wi-Fi configurado. Uma
  atualização que perdeu o Wi-Fi é a que deve voltar. Numa unidade sem Wi-Fi a
  rede não conta: ela funciona desconectada, e o AP só abre quando uma pessoa
  pede;
- o `loop( )` rodando e alimentando o watchdog. O servidor web começa no
  `setup( )`, então já escuta;
- o Core 1 sem relançamento pela checagem de saúde do painel (etapa 6). Uma
  imagem com o Core 1 morrendo a cada ~10 s se confirmava sem isso.

Não se exige que a web tenha respondido a um pedido: isso precisa de um cliente,
e uma atualização aplicada por um roteiro que não consulta a placa depois nunca
seria confirmada.

O prazo é de 300 s desde o boot, e o minuto saudável tem de começar até os
240 s. Sem isso, o SIMUT registra o código 612 e reinicia de forma normal, e a
ROM volta.

**O *buy*.** O SIMUT chama o `explicit_buy` da ROM pela tabela de funções, não
pelo `rom_explicit_buy( )` do SDK, que passa pelo `flash_safe_execute( )`. Esse
caminho pede o Core 1 preparado como vítima do `multicore_lockout`, e o SIMUT
estaciona o Core 1 do jeito dele (`enterFlashSafeMode`). A chamada roda com as
interrupções desligadas e um buffer de 4 KB.

A ROM tira a marca da imagem (relê o setor no buffer, apaga e regrava) e apaga
o primeiro setor do outro slot quando ele venceria a escolha
(`varm_launch_image.c:147-188`). **A primeira linha do `explicit_buy` desliga o
watchdog**, e a ROM do A2 da bancada faz o mesmo: o `CTRL` tinha o `ENABLE`
ligado antes da chamada e desligado logo depois (S9, abaixo). O SIMUT religa o
watchdog logo em seguida. Sem isso, quem o religaria seria a próxima `WdtWindow`,
que toda gravação de log na flash abre: na bancada, uma imagem sem o religar
travada 20 s depois do *buy* reiniciou pelo watchdog do mesmo jeito. Mas um log
guardado na RAM (durante um toque no painel ou uma tarefa pesada) não abre
nenhuma, e a placa ficaria sem watchdog até a próxima.

**Reiniciar durante o teste.** Um reinício que o SIMUT pede (um commit, uma
restauração, o `reload`) não é um veredito sobre a atualização. Ele vira um
`FLASH_UPDATE` para o mesmo slot, e a imagem sobe em teste de novo, com o prazo
recomeçado. Um reset, um corte de energia, o watchdog e o prazo voltam para a
imagem anterior.

**Nenhum stage durante o teste.** O outro slot guarda a imagem para onde a ROM
volta, e um stage a apagaria. O stage responde 409 até o *buy* ou a volta.

**Quem voltou conta.** No boot, uma imagem marcada para teste no outro slot é
uma de que a ROM voltou:

- o SIMUT lê a versão dela na etiqueta `SIMUT-ENV`;
- registra o código 613, com a versão no contexto (`M*10000+m*100+p`, porque o
  log binário guarda um número de 16 bits e não o texto: 2.10.9 vira 21009);
- põe a versão em `sys.reverted` no `/api/status`;
- apaga o primeiro setor daquela imagem, então isso é dito uma vez só.

A imagem em teste registra o 610 ao subir, o 611 no *buy* (com os segundos
desde o boot), o 612 no prazo e o 614 se a ROM recusar o *buy*. O
`sys.trial` vale 1 enquanto ela espera. O painel e a página Arquivos mostram
os dois campos numa faixa acima do primeiro cartão, desde 04/10/2026: "esta
versão está em teste" enquanto `trial` é 1, e a volta com a versão recusada
enquanto `reverted` a traz. A página Arquivos avisa, antes do envio, que os
arquivos ficam como estão e que a imagem nova sobe em teste. Tudo isso fica em
blocos `@IF slot` que as imagens do Pico W cortam. O gerenciador de frota
ainda não mostra os campos.

**A configuração.** Uma imagem anterior recusa uma configuração de outro
`CONFIG_VERSION` (`StorageManager.cpp:106`, `:748`). Num boot em teste que migra
a configuração, a recupera do `.bak` ou a descarta, o `saveConfiguration( )`
recusa gravar até o *buy*, e o *buy* grava. Assim, uma versão que mude o formato
só migra na flash depois de confirmada.

**A imagem sem a marca.** O `firmware.bin` sem a marca ainda instala pelo ar,
como na etapa 4: a ROM o confirma ao subir, sem teste. É o caminho de volta para
uma imagem anterior à etapa 5, que não tem o *buy* e nunca seria confirmada.

## Os registradores de rascunho do watchdog

A autópsia de travamento do SIMUT usa os registradores de rascunho do watchdog
(`LogManager.cpp:630-674`):

- `scratch[3]` para o rastro de módulo;
- `scratch[5]` para as marcas de pânico;
- `scratch[6]` e `scratch[7]` para a carga, o tempo ligado e a posição na web.

O reinício `FLASH_UPDATE` da ROM escreve quase todos eles
(`varm_apis.c:143-201`): `scratch[2]` e `[3]` com os parâmetros, `[4]` e `[7]`
com a assinatura `0xB007C0D3`, `[5]` com o endereço e `[6]` com o tipo de
reinício. O boot em teste zera `[2]` a `[4]`.

Também, depois de um `FLASH_UPDATE`, `watchdog_caused_reboot( )` responde falso
no RP2350 (`watchdog.c:116-123`). A autópsia veria o reinício da instalação como
um boot limpo.

Este desenho propunha levar a autópsia para os registradores de rascunho do
POWMAN, ou para uma área de RAM que o reset não limpa, e deixava a escolha para
a etapa 6, com uma medição. **A medição (04/10/2026) mostrou que não é preciso:**

- o reinício da ROM só acontece quando o próprio SIMUT o pede (o apply, um
  reinício durante o teste). A sessão que termina ali não tem o que relatar, e
  no boot seguinte o SDK não conta o watchdog, porque o boot foi
  `FLASH_UPDATE`: a autópsia fica calada, como deve;
- a ROM apaga a assinatura do vetor (`scratch[4]`) ao usá-la
  (`varm_boot_path.c`). Nada do que ela deixou é lido depois como um pedido de
  reinício novo;
- um travamento reinicia pelo contador do watchdog, que não escreve nada, e os
  valores do SIMUT chegam inteiros: o `loop( )` travado voltou como
  200 + módulo, Core 1, minutos e posição na web certos (203 / 1008 / 4000 /
  2740).

**O que estava errado, e foi consertado:** um `setup( )` travado em teste, que
só a guarda da etapa 5 pega, saía com o tempo ligado e a posição na web
deixados pela ROM: 200 / 1008 / **4000 / 2999** (zero minuto, e uma posição fora
da faixa), onde a imagem tinha rodado 300 s sem atender pedido nenhum. Agora a
guarda zera `scratch[6]` e `[7]` no começo do boot em teste e carimba o tempo
ligado a cada alimentação: **4004 / 2000** (a última alimentação vem antes dos
300 s, então os minutos arredondam para 4).

| Reinício | O que a autópsia registra | |
|---|---|---|
| Reset pelo pino RUN | nada | certo |
| `reload`, um commit, o prazo do teste | nada | certo |
| Instalação, marcada ou não, e o reinício durante o teste | nada | certo |
| `loop( )` travado | 203 / 1008 / 4000 / 2740 | certo |
| `setup( )` travado em teste | era 200 / 1008 / 4000 / 2999; agora 200 / 1008 / 4004 / 2000 | consertado |
| Core 1 parado, ou numa falha de hardware | `APP_CORE1_DEAD` e o Core 1 relançado, a cada ~10 s | é o desenho, nos dois chips |
| `picotool reboot` | FATAL 455, falso | artefato conhecido, nos dois chips |

O pânico com as bandas do Core 1 (1xx, 3xx, 400) quase não aparece num build
com painel: o laço principal declara o Core 1 morto com 10 s sem batimento e o
relança (`APP_CORE1_DEAD`), antes dos 15 s do pânico. Isso vale nos dois chips.
Na bancada, uma imagem em teste com o Core 1 parado aos 30 s foi relançada 76
vezes em 13 minutos, e mesmo assim se confirmou aos 81 s, porque a saúde do teste
não olhava o Core 1. Agora um relançamento pela checagem de saúde quebra o
minuto saudável (ver "A confirmação"): a mesma imagem, e uma com o Core 1 numa
falha de hardware (`udf`), foram relançadas 14 vezes cada e voltaram no prazo,
com o 612 e o 613.

## A release (etapa 7)

Desde a etapa 7, o `release-ota.yml` publica duas imagens do Pico 2 W, com
`releasetwo` no nome:

- **O `.bin` é a atualização.** É o `firmware_ota.bin` assinado pela chave de
  release, marcado para teste. O manifesto recusa uma atualização sem a marca:
  ela instalaria sem teste, e uma imagem ruim não voltaria.
- **O `.uf2` é a imagem de fábrica.** É a tabela de partições que o build
  escreveu e, no slot A, a atualização assinada com a marca apagada
  (`clear_trial`). Quando a imagem se confirma, a ROM apaga a marca na flash
  (`explicit_buy`). Assim uma placa gravada pelo USB guarda os mesmos bytes que
  uma atualizada pelo ar, trailer da assinatura incluído. Os dois arquivos
  diferem na tabela e num byte, em `0x12b`.

O job que publica não tem o `picotool`. O `.uf2` sai de `tools/rp2350/uf2.py`,
o mesmo código com que o build escreve o `firmware_factory.uf2`, e o build falha
se ele não der byte a byte o que o `picotool` dá. Conferências de 04/10/2026:

- quatro builds das etapas 4 a 6 deram o mesmo `.uf2` de fábrica pelos dois
  caminhos;
- de ponta a ponta, com a atualização assinada pela chave de bancada, o `.uf2`
  trouxe o `firmware.bin` inteiro e depois os 241 B da assinatura;
- o manifesto deste ramo deu, para as três imagens do Pico W, as mesmas entradas
  e os mesmos arquivos que o da `main`.

**A confiança na bancada sai do perfil.** Uma imagem que a release publica
confia só na raiz de release. O job de assinatura recusaria outra, e
`tools/test_release_manifest.py` confere isso a cada push. A imagem de bancada
sai do mesmo perfil com `PLATFORMIO_BUILD_FLAGS=-DSIMUT_OTA_TRUST_BENCH=1`. Em
04/10/2026 isso deu, byte a byte, a imagem que a `main` compilava antes da etapa
7, e a imagem sem a raiz de bancada ficou 64 B menor.

**A errata RP2350-E10.** No A2, arrastar um `.uf2` para a unidade do BOOTSEL de
uma placa que já tem tabela de partições falha. O `picotool` contorna isso com
um bloco no último setor, mas só num arquivo que vai a uma partição pela família
(`elf2uf2.cpp`). O `.uf2` de fábrica é todo na família absoluta e não leva esse
bloco. Na bancada ele só foi gravado pelo `picotool load`. Arrastá-lo para a
placa da bancada, que é A2, fica para o portão da release.

**O que fica de fora:**

- **O configurador.** O perfil continua com `publish = false` em
  `tools/features.toml`: o `build_custom.py` ainda entrega o `firmware.uf2` e o
  `firmware.bin`, e a página usa os tetos do Pico W.
- **O simut-rx.** Ele ignora de propósito uma variante que não conhece
  (`decodeManifest`), então não oferece atualização a um Pico 2 W até aprender o
  `releasetwo`. O teto de tamanho dele, 1016 KiB, também é o do Pico W.
- **Um `min_from` próprio.** O manifesto tem um só. Num Pico 2 W, a primeira
  versão publicada entra pelo USB: as imagens compiladas antes da etapa 5
  recusam a atualização marcada (a da etapa 4 com `v=6`; as anteriores, pelo
  `env` ou por recusarem toda OTA).

**O portão da primeira release**, além do de sempre (`AGENTS.md`):

1. O candidato assinado pelo CI (`workflow_dispatch`) vai pelo ar para a placa
   da bancada, que roda a imagem de bancada. Ele sobe em teste e se confirma, e
   a configuração e o histórico ficam.
2. O mantenedor arrasta o `.uf2` de fábrica do candidato para a unidade do
   BOOTSEL da mesma placa, que é A2 e tem tabela.

Depois disso a placa roda uma imagem sem a raiz de bancada. Ela recusa o que a
bancada assina (`v=12`), e a volta é pelo USB.

## Boot seguro e OTP

Decisão de 03/10/2026: os aparelhos de produção saem com boot seguro e
anti-rollback em OTP; a placa da bancada nunca recebe isso, porque o OTP não
volta atrás.

- **Duas assinaturas.**
  - A da ROM vai no IMAGE_DEF, conferida pela chave cujo hash está no OTP.
  - A do SIMUT vai no trailer, conferida pelo bloco de confiança compilado.

  A primeira protege o boot, inclusive o USB. A segunda continua protegendo a
  OTA e os ambientes (`env`).
- **Anti-rollback.** O OTP guarda a versão de rollback, 24 versões por linha
  (`main.cpp:5007-5020` do `picotool`), e a ROM a impõe só com o boot seguro
  ligado.
- **A bancada é protegida por ferramenta.** Os roteiros de bancada recusam
  qualquer `picotool otp` e `picotool seal --sign` com o número de série da
  placa de teste. O procedimento de provisionamento de produção é um documento
  à parte (etapa S3), escrito e conferido antes da primeira placa de produção.

## O que muda no código

| Fica, apontando para o slot inativo | Sai no RP2350 | Muda |
|---|---|---|
| O stage em fluxo (`firmware_stage`) | O applier e o teardown do orquestrador | O conferidor de imagem: IMAGE_DEF e chip no lugar do boot2 |
| A assinatura: `sigCheck`, `ota_trust`, `ota_sign.py` | O estado APPLYING e o contador de tentativas | A ordem de gravação: o primeiro setor por último |
| A etiqueta `env` | A cópia da configuração nos blocos 254–255 | A autópsia: os mesmos registradores; o boot em teste zera e carimba o tempo ligado e a posição na web (etapa 6) |
| A reconferência antes do reinício (`WebManager_Ota.cpp:555-582`) | Desmontar e formatar o LittleFS no stage | O `setup( )` em teste, guardado por um watchdog de 16 s que um timer alimenta |
| | O CRC da imagem instalada no boot seguinte | O LittleFS: leitura pela janela sem tradução |

O RP2040 não muda: tudo isso entra atrás de um `#if` do chip, e as seis imagens
do RP2040 saem idênticas byte a byte, como no S0 (#215). A exceção da etapa 5 é a
tabela dos códigos de log, comum a todas as imagens: os cinco códigos novos
custam 176 B na `pico_w_release`. Compiladas da `main` com só essa tabela, as
seis imagens do RP2040 saem idênticas byte a byte às do ramo da etapa 5.

## Hoje: um risco a fechar primeiro

As constantes de `src/ota/ota_layout.h` são o mapa de 2 MB do Pico W. Na Pico
2 W o LittleFS fica em `0x2FF000` dos 4 MB (`_FS_start` = `0x102FF000`). Por
isso o stage grava a imagem recebida em `0x0FF000`, na sobra do slot do próprio
programa, e só desmonta o LittleFS durante o envio.

Uma Pico 2 W com a `pico2_w_release` atual aceita pelo ar uma release
**assinada** do RP2040. Pelo código, um envio que chegasse ao fim seria
validado:

- o CRC do boot2 dela confere;
- o `env` das duas é `release` (`simut_config.h:459-467`);
- a assinatura é válida.

O applier a copiaria sobre o programa em execução, e a ROM do RP2350, sem
IMAGE_DEF, cairia no BOOTSEL.

**Na placa da bancada (03/10/2026).** A release v2.10.0 assinada foi enviada
três vezes à `main` (badb4a8):

- o aparelho aceitou o começo dos três envios;
- cada um caiu antes do fim: aos 28,7 s, duas vezes na porta 80, e aos 9,8 s na
  porta 8080;
- o aparelho não reiniciou, e o LittleFS voltou intacto, com o pacote pt-BR.

A queda não vem das gravações. A recusa do passo 2, que não grava nada, chegou
ao fim só porque durou 9,3 s. Na etapa 4 (03/10/2026, à noite), o mesmo tipo de
envio recusado caiu oito vezes em oito, entre 12,4 e 14,8 s, a 50 KB/s. Uma aba
da interface da placa estava aberta no navegador.

Quem corta é o lwIP da própria placa:

- o servidor web atende um cliente por vez, e as consultas da página esperam na
  fila;
- cada consulta ocupa um dos 5 PCBs TCP (`MEMP_NUM_TCP_PCB` do arduino-pico);
- com o pool cheio, o `tcp_alloc` mata a conexão de prioridade menor mais
  ociosa, e o arduino-pico põe todas em `TCP_PRIO_MIN` (`ClientContext.h`);
- o navegador refaz na hora a consulta que levou reset; as da fila ficam todas
  recentes, empatam com o upload, e no empate a vítima é a mais antiga: o
  upload.

O RST sai com o TTL da placa, 255: um filtro de TTL mínimo no socket não o
barra, e nenhum pacote com TTL menor chegou. O roteador não entra nisso. Com a
bancada quieta, o mesmo envio vai até o fim, 403 aos 19,9 s, duas vezes em
duas. Um script que consulta a cada 3 s sem refazer não corta. Refazendo, como o
navegador, corta aos 13,1 s, duas vezes em duas. A prova da etapa 4 roda com a
bancada quieta, e o conserto do servidor é um PR à parte.

Até a etapa 7, as releases não publicavam a imagem do RP2350, então ninguém a
tinha em campo. Mesmo assim, a primeira etapa de código fechou isso: a imagem do RP2350 recusa toda OTA
até o A/B existir.

**Dois endereços para a etapa 3.** O boot ainda lê os metadados em `0x1FF000` e
apaga a cópia da configuração em `0x1FD000`. Hoje esses endereços ficam depois
do fim do programa, e isso não faz mal. No layout A/B eles caem no slot B, então
a etapa 3 os tira do RP2350.

## Testes, antes do código

**No host, nas suítes nativas:**

- as constantes do layout, presas por `static_assert`;
- a escolha do slot inativo como função pura, a partir das respostas da ROM;
- o conferidor de IMAGE_DEF, incluindo a máscara do bit de teste na
  reconferência depois do *buy* (o slot não é mais byte a byte o arquivo
  assinado);
- o gravador com uma flash simulada, para garantir que o primeiro setor é o
  último e que um corte em qualquer ponto deixa o slot sem IMAGE_DEF.

Na etapa 5, a regra do teste (`src/ota/trial.h`) ganhou 20 casos na
`native_otasig` (`test_trial.cpp`): o minuto saudável, a quebra que o recomeça,
o prazo, a versão lida da etiqueta e o código dela no log, a marca no bloco e o
slot em execução. Antes do código a suíte não compilava; depois, 75 de 75. O
`tools/test_rp2350_trial.py` (CI, job `gates`) prende a marca: um byte só, e só
no `firmware_ota.bin`.

**No ferro (S2):**

- um envio inteiro de imagem pelo stage, sem a conexão cair, com a bancada
  quieta (os três envios de 03/10 caíram por uma aba da interface aberta no
  navegador, não pelo stage; ver acima);
- uma imagem que nunca confirma volta no prazo de 300 s (decidido na etapa 5; a
  ROM sozinha voltaria em cerca de 17 s, mas o `setup( )` desliga o watchdog
  dela);
- um corte de energia no meio da gravação sobe a anterior;
- o histórico e os pacotes **ficam** numa atualização, sem `.bkp`;
- 20 ciclos A→B→A com a configuração e o histórico idênticos;
- o `setup( )` em teste, travado, volta pela guarda do watchdog.

**A etapa 5 na bancada (04/10/2026).** A Pico 2 W, RP2350 A2, com a
configuração da Pico W. Imagens assinadas com a chave de bancada; T1 a T6 são o
mesmo código com um gancho de teste só local. O roteiro e os resultados ficam
fora do repositório (`bancada_20261003_pico2w/etapa5/`).

| Passo | O que | Resultado |
|---|---|---|
| SB | A imagem sem marca, instalada pela imagem da etapa 4 | Sobe sem teste (`trial` 0): a ROM a confirma ao subir |
| S1 | A imagem marcada (2.10.0) | Volta em teste (610 = 21000), se confirma aos 79 s do boot (611 = 79); um RESET a mantém |
| S2 | 2.10.9, e um RESET antes do *buy* | A 2.10.0 volta do slot de onde saiu, com `reverted` "2.10.9" e o 613 = 21009; o RESET seguinte não repete o relato |
| S3 | 2.10.9, e um stage durante o teste | 409, nada gravado; um RESET traz a 2.10.0 inteira de volta |
| S4 | 2.10.9, e `reload confirm` no console durante o teste | Volta à 2.10.9, em teste de novo (uptime 24,7 s); se confirma; um RESET a mantém |
| S6 | T1: nunca saudável | 612 aos 300 s; a 2.10.0 volta com o 613 = 21001 |
| S7 | T2: o `setup( )` trava | A guarda alimenta o watchdog até o prazo e para; a 2.10.0 volta 358 s depois do apply, com a autópsia de watchdog (200) e o 613 = 21002 |
| S8 | T3: o `loop( )` trava aos 40 s | O watchdog do SIMUT reinicia; a 2.10.0 volta cerca de 50 s depois do apply, com a autópsia 203 e o 613 = 21003 |
| S9 | T5: travada 20 s depois do *buy*, sem religar o watchdog; T6: o `CTRL` antes e depois da ROM | T5 reinicia pelo watchdog mesmo assim (uma `WdtWindow` o religou); em T6, o `ENABLE` estava ligado antes do `explicit_buy` e desligado depois |
| S10 | De volta à 2.10.0 | A configuração igual à do início (CRC `AC385271`); o histórico só cresceu |

Do apply até a imagem em teste responder: 25 a 42 s. O *buy* saiu entre 79 e 83 s
depois do boot.

## Etapas

1. **Este desenho.** Feito (#243).
2. **Fechar o risco de hoje:** a imagem do RP2350 recusa OTA. O `env` próprio
   fica para a etapa 4. A etiqueta `SIMUT-ENV` aceita só letras de `a` a `z`
   (`BuildIdentity.cpp`), e, com o OTA recusado, o `env` não protege nada antes
   disso. Feita (#244).
3. **A imagem em slot (S1).** Linker script sem o stub, tabela de partições,
   `.uf2` de fábrica, o LittleFS pela janela sem tradução, os endereços de
   `ota_layout.h` por chip, e o boot conferido na placa. Feita (#245).
4. **O stage no slot inativo** e o reinício `FLASH_UPDATE`, ainda sem TBYB.
   Feita (#246).
5. **TBYB:** a marca de teste, a confirmação em 60 s, a volta e o relato.
   Feita em 04/10/2026. O painel e a página Arquivos mostram o `trial` e o
   `reverted` desde 04/10/2026; o gerenciador de frota ainda não.
6. **A autópsia** com os registradores que a ROM não usa. Feita em 04/10/2026:
   a medição mostrou que os registradores do watchdog bastam, e consertou o que
   faltava (o `setup( )` travado em teste; o Core 1 na saúde do teste).
7. **A primeira release** com as imagens do RP2350 publicadas (`.uf2` de fábrica
   e `.bin` de OTA). O workflow e o manifesto ficaram prontos em 04/10/2026 (ver
   [A release](#a-release-etapa-7)). Falta a release em si, com o portão no
   ferro.
8. **S3:** o procedimento de boot seguro e OTP para produção, nunca na bancada.
