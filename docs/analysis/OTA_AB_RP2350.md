# OTA de slot duplo no Pico 2 W — desenho

Estado: **proposta**, etapa S2 da Fase 4 de
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
  dá `0x10300000`. Muda só o tamanho, de 1 MB para 1.020 KB.
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

São dois artefatos por variante, e o `release-ota.yml` publica os dois.

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

**O que a ROM faz** (`varm_launch_image.c:208-458`):

- marca a confirmação como pendente;
- arma um reinício do tipo normal para 0xFFFFFF ms. No watchdog isso dá cerca de
  16,8 s, porque o RP2350 não tem o erratum de contagem dupla do RP2040.

Se o watchdog vencer antes da confirmação, o boot seguinte é normal, e a ROM
recusa a imagem ainda marcada e sobe a outra.

**A armadilha, pelo código.** O `watchdog_update( )` do SDK escreve o
`load_value`, que fica 0 até alguém chamar `watchdog_enable` (`watchdog.c:24-28`,
`:68`). O SIMUT só chama `watchdog_enable` no `loop( )` (`main.cpp:48-55`). Mas
a `FLASH_OP` já alimenta o watchdog dentro do `setup( )`. Com o reinício de
teste armado, essa primeira alimentação põe o contador em 0 e reinicia o chip
na hora, e toda atualização voltaria para a anterior. Conserto: chamar
`watchdog_enable` no começo do `setup( )` quando a confirmação estiver
pendente.

**A confirmação** (decisão de 03/10/2026) acontece com:

- o boot completo e a configuração lida;
- o servidor web respondendo;
- 60 s sem falha, com o watchdog alimentado o tempo todo.

Então o SIMUT chama `rom_explicit_buy( )`, que desliga o reinício de teste, tira
a marca da imagem (relê, apaga e regrava aquele setor, com um buffer de pelo
menos 4 KB) e apaga o primeiro setor do outro slot, se for o caso
(`varm_launch_image.c:147-188`). Se a imagem travar antes, o watchdog reinicia e
a ROM volta.

**Quem voltou conta.** A imagem anterior, ao subir depois de um teste que falhou,
registra um código de log novo e põe no `/api/status` a versão recusada. A
página de arquivos e o gerenciador de frota mostram isso. Os detalhes ficam para
a etapa 5.

**A configuração.** Uma imagem anterior recusa uma configuração de outro
`CONFIG_VERSION` (`StorageManager.cpp:106`, `:748`). Regra: nenhuma migração
grava antes da confirmação. Uma versão que mude o formato migra só depois do
*buy*.

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

Proposta: no RP2350, a autópsia passa para os registradores de rascunho do
POWMAN ou para uma área de RAM que o reset não limpa. A escolha fica para a
etapa 6, com uma medição no ferro.

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
| A etiqueta `env` | A cópia da configuração nos blocos 254–255 | A autópsia: outros registradores de rascunho |
| A reconferência antes do reinício (`WebManager_Ota.cpp:555-582`) | Desmontar e formatar o LittleFS no stage | O watchdog no começo do `setup( )` em teste |
| | O CRC da imagem instalada no boot seguinte | O LittleFS: leitura pela janela sem tradução |

O RP2040 não muda: tudo isso entra atrás de um `#if` do chip, e as seis imagens
do RP2040 saem idênticas byte a byte, como no S0 (#215).

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

A causa da queda não foi encontrada.

As releases não publicam a imagem do RP2350, então ninguém a tem em campo. Mesmo
assim, a primeira etapa de código fecha isso: a imagem do RP2350 recusa toda OTA
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

**No ferro (S2):**

- uma imagem que nunca confirma volta em cerca de 17 s;
- um corte de energia no meio da gravação sobe a anterior;
- o histórico e os pacotes **ficam** numa atualização, sem `.bkp`;
- 20 ciclos A→B→A com a configuração e o histórico idênticos;
- o watchdog em teste não reinicia o `setup( )`.

## Etapas

1. **Este desenho.**
2. **Fechar o risco de hoje:** a imagem do RP2350 recusa OTA. O `env` próprio
   fica para a etapa 4. A etiqueta `SIMUT-ENV` aceita só letras de `a` a `z`
   (`BuildIdentity.cpp`), e, com o OTA recusado, o `env` não protege nada antes
   disso.
3. **A imagem em slot (S1).** Linker script sem o stub, tabela de partições,
   `.uf2` de fábrica, o LittleFS pela janela sem tradução, os endereços de
   `ota_layout.h` por chip, e o boot conferido na placa.
4. **O stage no slot inativo** e o reinício `FLASH_UPDATE`, ainda sem TBYB.
5. **TBYB:** a marca de teste, a confirmação em 60 s, a volta e o relato.
6. **A autópsia** com os registradores que a ROM não usa.
7. **A primeira release** com as imagens do RP2350 publicadas (`.uf2` de fábrica
   e `.bin` de OTA).
8. **S3:** o procedimento de boot seguro e OTP para produção, nunca na bancada.
