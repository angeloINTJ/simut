# Plano depois da revisão externa da v2.7.3

**Estado: Living.** Cada item fecha quando existir a medição que o fecha, não
quando o código mudar. Marque aqui, no PR que o fechou.

Levantado em 2026-10-01 contra `main` = `2721eb2` (v2.8.0 mais os PRs #197 a
#202). Conferido de novo em 2026-10-02 contra `main` = `11d3822` (v2.9.0),
item a item, no código e na release publicada: as colunas de estado abaixo são
as dessa conferência.

## De onde vem

Um documento de "impressões de um engenheiro experiente" sobre a v2.7.3,
escrito só a partir do README, levantou 15 pontos (A-01 a A-15). Conferidos
contra o código, três coisas valem registrar:

1. **Quase toda a lista era a própria seção *Known limitations* do README.** A
   franqueza do README virou a lista de críticas, o que é bom sinal. Mas
   alguns números estavam velhos: os 97,2 % do slot e os 13.196 B de folga
   eram da v2.7.1 (na v2.8.0: 95,7 % e 28.996 B), e a contagem de testes
   estava atrás das suítes.
2. **O que mais ameaça a impressão não estava na revisão.** Os redatores do
   manual da v2.7.1 acharam 75 defeitos lendo o código (23/09/2026). A triagem
   de 01/10 (seção [Triagem dos 75 achados](#triagem-dos-75-achados-de-2309))
   encontrou **72 ainda presentes**, 17 deles de severidade alta. Um revisor que
   lesse o código acharia esses antes de qualquer ponto da revisão.
3. **Parte do A-03 era artefato de bancada.** O `ctx=455` (trace vazio) aparece
   no primeiro boot depois de toda gravação com `picotool load -x` (11 de 11,
   e 0 de 7 depois de RESET pelo pino, 30/09). O `ctx=209` é real: o B1 de
   21/09, com o Core 0 no console.

## A ordem

Primeiro o que contradiz a promessa do produto ("keeps working with or without
a network"), depois o texto público, e o Pico 2 W como segunda plataforma,
com um spike antes de qualquer promessa. Toda correção começa pela prova que
falha antes (`CLAUDE.md`, *Tests first*).

## Fase 0 — o que é público diz a verdade

| Item | Estado |
|---|---|
| Números do README nos três idiomas, e um portão que os amarra às fontes (`tools/check_readme_numbers.py`, provado por mutação, 9 de 9) | Mergeado (#205, 01/10) |
| Tom: "zero-trust", "signed audit trail" e "obfuscated at rest" sem a ressalva | Mergeado (#205) |
| A-03 dividido: o `ctx=209` real, o `ctx=455` do `picotool -x` explicado | Mergeado (#205) |
| `SHA256SUMS` e atestado de proveniência (Sigstore) em cada release | Mergeado (#207). Exercido na v2.9.0: o sha256 da `release.bin` publicada está no `SHA256SUMS` e tem um atestado (conferido em 02/10) |
| Descrição do repositório no GitHub ("Professional-grade") | Decisão do mantenedor (01/10): esperar. Em 02/10 ainda começa com "Professional-grade" |
| Nota "como o SIMUT é desenvolvido" (A-14): agentes de IA ajudam, nada entra sem PR, nove checks e prova | #228: a seção *How SIMUT is developed* nos três READMEs, e a seção *AI Tools* do `CONTRIBUTING.md` reescrita para o fluxo de verdade |

## Fase 1 — o que contradiz a promessa

| Item | Estado |
|---|---|
| Com o AP de configuração aberto, o aparelho não mede, não grava e não alarma (achado 48) | Mergeado (#204, 01/10), na v2.9.0. Provado na bancada: 34 → 34 leituras na main, 38 → 257 com o PR, e o alarme 470 |
| SDA e SCL trocados travam todo boot; um segundo BME/BMP no mesmo periférico lê o primeiro (achados 63 e 64) | Mergeado (#203, 01/10), na v2.9.0. Provado na bancada: a main entra em PANIC, o PR sobe em bit-bang; o 64 só no teste nativo (uma placa) |
| Alarme recusado com a fila cheia nunca é anunciado (#161) | Mergeado (#217, 01/10), na v2.9.0: opção (a), latch só com push aceito e reanúncio do que segue ativo |
| Linha de alarmes por MQTT: ACK nunca lido com a telemetria desligada; a fila não cabe no buffer de 2.048 B (achados 28 e 29) | Mergeado (#208, 01/10), na v2.9.0. Provado na bancada: fila presa em 5/16 e maior lote de 2.001 B na main; fila 0 e lote de 2.794 B com o PR |
| `isTimeSynced( )` sempre verdadeiro (achado 66) | Mergeado (#209, 01/10), na v2.9.0. Provado na bancada: com o NTP desligado a main diz `ntp=1` com o relógio em 1970; o PR diz 0 |
| Com NTP ligado e sem servidor alcançável, o aparelho nunca chega a `NET_READY`, e telemetria e linha de alarmes esperam para sempre (achado 20). Documentado no manual, cap. 10, mas é armadilha para a LAN sem internet que o README promete | A fazer: desenho (sair para `NET_READY` depois de N falhas e seguir tentando o NTP). Presente na v2.9.0: `NET_CONNECTED_WAIT_NTP` só sai com o NTP respondendo |
| Os achados de severidade alta que restam: 12 dos 17 (tabela abaixo; 28, 29, 48, 63 e 64 corrigidos) | Um PR por grupo |

## Fase 2 — os itens de firmware da revisão

| Item | Estado |
|---|---|
| A-03: o `ctx=209` com um laço dirigido (os cinco comandos de 21/09, painel desenhando) | A fazer, na bancada. À parte, um watchdog de campo foi corrigido no #218 (01/10, na v2.9.0): um join de Wi-Fi recusado segurava o Core 0 |
| A-04: cursor de telemetria por posição de escrita, com o codec de referência em Python e o portão de paridade junto | A fazer: documento curto e teste nativo que falha antes. Na v2.9.0 o cursor ainda é uma época (`TelemetryCursor.h`) |
| A-06 e #189: respostas truncadas e o enquadramento chunked | A fazer: soak que conta e relê; reprodução no host. O #189 está entre os conhecidos da v2.9.0 |
| A-08: contas sem reinício | Plano pronto em `PLANO_DIVIDA_TECNICA.md`. Na v2.9.0, `CFG_USERS` ainda está em `CFG_REBOOT_CLASSES` (`ConfigApply.h`), e o comentário mais abaixo no mesmo arquivo diz que não está |
| A-05: soak longo do Air (3 dias ou mais) | A fazer, na bancada. O Air está parado desde 11/09 |
| A-10: medir o login a 5.000 rodadas e escrever o número no `SECURITY.md` | #228: o custo e a troca escritos no `SECURITY.md`, com a única medição que havia (0,69 s por leitura do `/metrics` com HTTP Basic, v2.2.13, 19/08). Falta a medição na v2.9.0, na bancada |
| As oito suítes nativas sob AddressSanitizer e UBSan | Mergeado (#206); achou uma referência pendurada no `min( )` do stub de teste |

## Fase 3 — o Pico W sem trocar de chip

A/B não cabe nos 2 MB: a `.bin` da release tem 1.011.388 B e comprime só para
71 % com `gzip -9` (718.476 B; 62 % com `xz -9e`). Nem uma área de staging
comprimida deixaria o histórico de pé.

| Item | Estado |
|---|---|
| OTA assistida: a página e o simut-rx restauram o `.bkp` sozinhos depois do apply, mesclando o dia corrente | A fazer. Na v2.9.0 a página ainda pede a restauração à mão (o passo 6 do fluxo da OTA no `WebUI.h`) |
| Imagem assinada, verificada no stage | Feito: #219 a #222 (01/10); a v2.9.0 (02/10) é a primeira release assinada. Desenho e medidas em [`OTA_ASSINADA.md`](OTA_ASSINADA.md) |
| README e `RECOVERY.md` dizendo por que o slot é único, com os números acima, e a janela de cerca de 26 s do applier | A fazer |

Não recomendado: tornar o applier retomável no RP2040 (boot2 próprio com um
stub de recuperação). É complexo e disputa os cerca de 29 KB de folga.

## Fase 4 — o Pico 2 W como segunda plataforma

O que o RP2350 resolve: a ROM tem partições A/B com *try before you buy* (a
imagem nova roda em teste e, sem `rom_explicit_buy( )` em 16,7 s, a ROM volta
para a anterior); 4 MB de flash, 520 KB de RAM, três blocos PIO, SHA-256 e
TRNG em hardware, OTP de 8 KB e boot assinado. A pinagem e o rádio
(CYW43439) são os do Pico W.

O que foi conferido no framework instalado (`framework-arduinopico`
1.50601.0, arduino-pico 5.6.1, pico-sdk 2.2.1) e que o spike precisa vencer:

- A placa `rpipico2w` existe, mas o PicoOTA é por arquivo no LittleFS, sem
  A/B. O A/B seria nosso, pelas funções da ROM (`rom_explicit_buy`,
  `rom_pick_ab_partition`, `rom_get_partition_table_info`, em `bootrom.h`).
- O `crt0.S.o` vem pré-compilado em `lib/rp2350/libpico.a`: a marca de imagem
  em teste (`PICO_CRT0_IMAGE_TYPE_TBYB`) exige recompilar o `libpico` ou
  corrigir o bloco IMAGE_DEF depois do link.
- O LittleFS do framework lê a flash por `memcpy` do endereço mapeado. Com a
  tradução de endereços do A/B, a partição de dados precisa ser lida pela
  janela sem tradução (`XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE`, `0x1c000000`):
  seria o 12º patch no framework.
- Endereços do RP2040 escritos à mão: watchdog `0x40058000` e PSM
  `0x40010000` em `LogManager.cpp`, `AppManager_Boot.cpp`,
  `ota/orchestrator.cpp` e `ota/applier.cpp`. No RP2350 são `0x400d8000` e
  `0x40018000`: o código compila e escreve no lugar errado. Corrigido no S0
  (#215): os endereços vêm do SDK do chip, e as seis imagens do RP2040 saíram
  idênticas byte a byte. A mesma leitura achou as duas reinicializações do
  `LogManager.cpp` escrevendo o WDSEL em `PSM_BASE + 0x18`, onde não há
  registrador (é `0x08` nos dois chips).
- O Air usa `hardware/rtc.h`, e o RP2350 não tem RTC (tem o timer *always-on*
  do POWMAN). O Air no RP2350 é outro projeto.
- O stepping A4 corrige o erratum E9 e exige pico-sdk 2.1.0 ou mais novo.
- O firmware do rádio ocupa 225.240 B da imagem release (22 %); 232.408 B mais
  6.970 B de Bluetooth no alpha e no Air.

| Etapa | O que prova |
|---|---|
| S0, só no CI | Dimensão `chip` no `features.toml`; os registradores por `watchdog_hw`/`psm_hw`; as seis imagens do RP2040 idênticas byte a byte; os tamanhos das imagens do RP2350. **Mergeado (#215, 01/10/2026):** a release no RP2350 ocupa 972.928 B de um slot de 3.141.632 B e 123.184 B de RAM estática |
| S1, uma placa A4 | As suítes de bancada existentes; o layout de 4 MB com staging própria, que já acaba com o reformatar |
| S2 | A/B com TBYB: imagem que nunca confirma volta em cerca de 17 s; corte de energia no meio da gravação sobe a anterior; 20 ciclos com config e histórico idênticos |
| S3 | O que o chip compra a mais: ambiente de debug, RAM, KDF com SHA-256 em hardware, boot assinado numa placa de teste (o OTP é irreversível) |
| S4 | O Air no RP2350 |

Layout decidido em 01/10/2026: **1,5 MB + 1,5 MB de código e 1,0 MB de
LittleFS** — mais espaço para código, e o histórico nos 116 dias de hoje. A
outra proposta era 1,25 + 1,25 + cerca de 1,5 MB (a release ocuparia 77 % do
slot e o histórico iria a cerca de 170 dias).

## Fase 5 — organização

| Item | Estado |
|---|---|
| A-15: README com cerca de 200 linhas; recursos em `docs/FEATURES.md`; a tabela de bancada em `docs/VERIFICATION.md` | #228: saiu do README o que a revisão apontou, nos três idiomas: a referência de recursos (para `docs/FEATURES.md`), as notas de versão (ficam no `CHANGELOG`) e a tabela de bancada (para `docs/VERIFICATION.md`). O README foi de 586 para 432 linhas (pt-BR 584 → 430, es-ES 588 → 434), não para 200: arquitetura, início rápido, compilação e testes ficaram |
| A-07: mostrar o mapa núcleo × opcional que o `features.toml` já tem | #228: o núcleo descrito em `docs/FEATURES.md` e a tabela das chaves gerada do `features.toml` pelo `gen_features.py` (o CI confere com `--check`) |
| A-13: resumo em inglês do essencial do `AGENTS.md`; registros de decisão (V5, OTA, modelo de recursos) | A fazer |
| A-12: mover o `WebUI.h` (44 arquivos o citam), provando as imagens idênticas | Por último |

## Decisões do mantenedor

1. **Posicionamento.** Manter laboratórios, farmácias e bancos de sangue no
   README põe no roteiro: trilha de auditoria completa (mudança de limite pela
   web não gera `alarm_lim`, achado 31), origem da hora por registro,
   exportação que acuse adulteração e relatório de lacunas.
2. **Pico 2 W** — layout decidido em 01/10/2026: 1,5 + 1,5 + 1,0 MB. A placa
   chega até 03/10/2026 e abre o S1; o S0 foi mergeado (#215). Falta conferir
   se a placa é A4.
3. **#161** — decidida em 01/10/2026: opção (a). Só marcar o alarme como
   anunciado quando a fila o aceitou, e reanunciar o que segue ativo quando
   ela voltar a ter espaço. A fila continua guardando os mais antigos, e o
   contrato do R3 não muda. Mergeado no #217 (01/10), na v2.9.0.
4. **OTA só com imagem assinada** — decidida em 01/10/2026: sim, e nas
   escolhas de chave, bancada e build local, "o mais profissional": raiz
   offline e chave de assinatura no CI com aprovação, anti-rollback, chave de
   bancada separada, build local para o campo só por USB. O desenho está em
   [`OTA_ASSINADA.md`](OTA_ASSINADA.md). Feito: a v2.9.0 (02/10) é a primeira
   release assinada.
5. **Descrição do repositório no GitHub** — 01/10/2026: esperar.
6. **Aparelho sem Wi-Fi** — decidida em 01/10/2026: o AP só abre a pedido
   (Configurações > Modo de Configuração, `ap`, o gesto do boot), e um
   aparelho sem rede configurada pede a data e a hora no boot, com a opção de
   pular. Mergeado (#214, 01/10), na v2.9.0. O gesto do boot, que também abre
   o AP sem passar pelo menu, fica (decidido em 01/10/2026).
7. **Check obrigatório do Pico 2 W** — decidida em 01/10/2026:
   `firmware (pico2_w_release)` entra na proteção da `main` quando o #215 for
   mergeado. Feito (#216, 01/10): nove checks obrigatórios.

## Triagem dos 75 achados de 23/09

Achados da redação do manual da v2.7.1, feitos lendo o código. Conferidos em
01/10 contra `origin/main` (`2721eb2`): **72 presentes, 3 corrigidos** (56,
59, 75). Nenhum foi reproduzido no ferro. Até a v2.9.0 (02/10), mais seis
corrigidos, cada um com a prova no PR: 28 e 29 (#208), 48 (#204), 63 e 64
(#203) e 66 (#209). Restam 66. Severidade do ponto de vista de um
monitor de cadeia fria: **alta** perde ou corrompe medição, alarme ou config,
trava o boot ou abre brecha de segurança; **média** é comportamento errado que
o usuário nota ou contrato de API quebrado; **baixa** é cosmético, de
documentação ou de comentário.

### Severidade alta

| # | Grupo | Onde | O que acontece |
|---|---|---|---|
| 1 | slot | `WebManager_Calib.cpp` | "Adotar a sonda" lê o GPIO de número igual ao slot e grava `pins[0]=slot`; sem reload, a divergência volta a cada 5 leituras |
| 6, 38 | ensaio | `WebManager_Commit.cpp` | Sons e mudo aplicados ao vivo durante o ensaio (`_dry`) que a página manda a cada edição |
| 8 | PIN | `AppManager_Panel.cpp` | No painel, quem tem Usuários concede bits que não tem, inclusive a si mesmo |
| 9 | PIN | `AppManager_Panel.cpp` | A troca obrigatória de PIN é pulada com SAIR, inclusive o `1234` de fábrica |
| 18 | ensaio | `WebManager_Commit.cpp` | O ensaio aplica o fuso de verdade |
| 20 | NTP | `NetworkManager.cpp` | Com NTP ligado e sem servidor, nunca `NET_READY`: telemetria e alarmes esperam |
| 25 | AP | `NetworkManager.cpp` | O AP aberto no boot nunca expira |
| 28 | alarmes | `TelemetryManager.cpp` | ACK do MQTT nunca lido com a telemetria desligada. Corrigido (#208) |
| 29 | alarmes | `TelemetryManager.cpp` | A fila não cabe no buffer de 2.048 B. Corrigido (#208) |
| 41 | API | `WebManager_Auth.cpp` | Por HTTP, o SHA-256 da senha é a senha para quem o captura (limitação conhecida) |
| 44 | API | `WebManager_Commit.cpp` | `null` num limite grava 0, com resposta 200 |
| 48 | AP | `AppManager_Loop.cpp` | Com o AP aberto, o aparelho não mede. Corrigido (#204) |
| 58 | OTA | `ota/restore.cpp` | Restauração: o limite de caminhos só existe ao aplicar; a falha apaga o que já gravou, inclusive `/config` |
| 63 | I2C | `sensors/SensorHelpers.h` | SDA e SCL trocados travam todo boot. Corrigido (#203) |
| 64 | I2C | `sensors/BME280SensorDriver.h` | Segundo BME/BMP no mesmo periférico lê o primeiro. Corrigido (#203) |
| 65 | busca | `SensorManager.cpp` | A busca de sondas não devolve o GP16, que no Air é a energia dos sensores |

### Severidade média

| # | Grupo | O que acontece |
|---|---|---|
| 2 | busca | A busca pela web varre GP0–GP16, inclusive pinos ativos, e não recarrega os sensores |
| 3, 19, 60 | campo morto | `s_int`, `log` e "Resetar época" são gravados e ninguém lê |
| 5, 17, 21, 69 | ensaio | Campos de overlay fora do ensaio; o ensaio grava auditoria; `_nosave` muda a RAM |
| 7, 10, 12, 13 | PIN | PIN pela web ignora a política; recusas somem da página; política de PIN pela web reinicia; a web não edita permissões |
| 14, 31 | alarmes | "Dois ciclos" não são 10 s; mudança de limite pela web não gera `alarm_lim` |
| 26, 46 | OTA | O par TLS e a origem CORS não atravessam a OTA |
| 32, 33, 35, 68 | telemetria | Servidor MQTT "ao vivo" que só vale no boot; prévias velhas; pressão com rótulo trocado; HA sempre `http://` |
| 37, 39, 40, 42, 43, 45, 57 | API | 403 sem sessão; `\uXXXX` literal; cookie vence o Bearer; um relógio de limite por IP; nonce por IP; ordem do multipart; "Upload concluído" com 400 |
| 47 | doc | Avisos e documentos de API velhos |
| 49 | AP | Alpha: o boot no AP nunca chama `endBoot( )` |
| 50, 51, 52 | painel | Âncora do gráfico presa; mín/máx por GPIO e não por slot; "Falhas" sempre 0 |
| 53, 61 | console | Argumento cortado no espaço; `system factory` não mostra a senha nova |
| 66 | NTP | `isTimeSynced( )` sempre verdadeiro. Corrigido (#209) |
| 67, 71 | log | Syslog nunca envia FATAL; `/metrics` não zera falhas |
| 73 | idioma | `uploadfs` põe os dois packs e o aparelho carrega o es-ES (documentado) |

### Severidade baixa

Comentários, textos e documentos: 4, 11, 15, 16, 22, 23, 24, 27, 30, 34, 36,
54, 55, 62, 70, 72, 74. Corrigidos: 56 (`430b765`), 59 (`266c9cf` e
`4c201cf`), 75 (`6a879f7`).
