# Changelog

[English](CHANGELOG.md) | **Português**

Todas as mudanças notáveis do firmware SIMUT.

## v1.5.6-beta (2026-07-26)

Release web-first. Toda configuração já tinha equivalente na web, e a CLI serial
carregava uma segunda cópia disso tudo, sem teste: 42 dos seus 55 comandos
duplicavam uma página que já funcionava. A imagem de release passa a ter
**9 comandos** — os que importam quando a web é justamente o que está quebrado —
e a duplicação saiu junto com **44.516 B de flash**.

A revisão de tradução veio antes e foi o que revelou a duplicação. Ela também
achou que os fallbacks em inglês da interface web estavam em português, então
quem usava o sistema em inglês lia "Salvar e Reiniciar" na barra superior.

> **Ainda em teste.** É a maior mudança estrutural desde a 1.0.0 e não passou por
> soak longo. Veja *O que não foi verificado* no fim.

### A CLI trazia 55 comandos que a web já respondia

- **`SIMUT_CLI_FULL`** (`SystemDefs_Cli.h`, padrão 1) escolhe a superfície.
  `pico_w_release` usa 0 e mantém `show net status`, `show system info`,
  `show system log`, `debug on|off`, `system admin reset`, `system format`,
  `system factory`, `reload` e `help`. Prompt único, sem a árvore de modos Cisco.
- Os quatro arquivos da CLI foram de **56.361 para 13.904 B de text**. Flash
  983.180 → 938.664 B (94,1% → 89,9%). As cinco ações web que repõem o cortado
  custaram ~3,7 KB de volta, daí os 44.516 B líquidos.
- **`[env:pico_w_test]`** compila a CLI completa e existe para as suítes de
  `tools/`, que dirigem o dispositivo pela serial com `enable`,
  `configure terminal`, `write memory`, `user add/del/perm` e `touch sim`.
  O `web_test_suite.py` cria a conta descartável por ali porque ainda não
  consegue autenticar. **Grave a imagem de teste antes de rodar suíte.**
  Ela linka exatamente no tamanho de antes do corte, que é a prova de que o
  perfil completo não foi mexido.
- Tirar a CLI tirou junto os **282 pares de string `isPt()` hardcoded**, que era
  o que fazia um dispositivo com o pack em espanhol responder em português.

### Cinco operações que não tinham equivalente web

`POST /api/action?op=` — uma rota com seletor em vez de cinco rotas, pelo motivo
que o `/api/restore` documenta. `sensor_scan` / `scan_results` (arma e consulta;
a varredura é uma máquina de estados que o loop principal avança, então o
handler nunca bloqueia), `sensor_accept`, `sensor_wipe`, `tel_sync`, `tel_reset`.

Elas passam por fora do staging do Salvar e Reiniciar de propósito: cada uma lê
ou escreve estado de hardware neste instante, então adiar aplicaria contra outra
realidade.

### Traduções

- **O es-ES foi gerado por máquina a partir do pt-BR e nunca revisado.** Português
  cru no dicionário do display (`SALVAR`, `PULAR`, `Umid Min/Max`, `SIM`/`NÃO`,
  `%UR`), em ~25 strings da web e em cerca de 60 dos 115 códigos de log.
- Duas entradas eram bug de **renderização**: o `unaccent()` mapeia ASCII, o
  bloco 0xC3 e seis símbolos 0xC2, então o `¡` de `¡Calibración Completada!`
  chegava ao TFT como um `?` literal. `@DICT`, `@HELP` e `@LICENSE` agora são
  checados quanto a isso.
- **14 chamadas `window.t(chave, fallback)` passavam português como fallback em
  inglês** — que é exatamente o que o usuário em inglês vê. Dois literais
  `TRL()` eram frases em português no fonte C++.
- Cobertura: faltavam no pt-BR as 15 chaves `sens_rebind_*`; no es-ES faltavam 75
  chaves web mais `@HELP` e `@LICENSE` inteiros, então a CLI dele caía para
  inglês. 44 chaves mortas removidas dos dois. Os packs agora batem com o
  firmware nas 109 strings de display, 119 códigos de log, 81 traduções de log
  e 403 chaves web.
- `*.lng` estava marcado `binary` no `.gitattributes` sendo UTF-8 puro, então
  nenhuma tradução jamais apareceu em diff. Agora é texto.

### Correções achadas no caminho

- **O `AppManager_Loop.cpp` filtrava `CMD_UNKNOWN` antes do `executeCommand` nos
  dois pontos de despacho**, o que tornava o ramo de "comando desconhecido"
  código morto — um erro de digitação voltava ao prompt em silêncio, e sempre
  foi assim. Inofensivo enquanto quase tudo parseava; deixou de ser quando 46
  comandos passaram a cair em `CMD_UNKNOWN` e o silêncio virou "travou".
- O `/api/action` validava o slot antes da op, então erro no nome da op voltava
  como `{"error":"slot"}`.
- A mensagem "Page asset missing" citava `config.html.gz` literalmente, o que
  deixou de valer quando o `/config` voltou para dentro do firmware.
- O `pico_test_suite.py` não conectava em placa ligada havia tempo: o `_connect`
  esperava prompt passivamente, mas abrir com DTR não reseta esta placa e o
  firmware só imprime prompt em resposta a input.
- O teste 11 logava na conta de fábrica `viewer`, que não volta depois de
  apagada (`user add` recebe texto plano e deriva o hash). Ele traz a própria
  conta agora.

### Verificado

Os dois ambientes compilam. 136/136 testes nativos em quatro suítes. No
hardware: **11/11** no `pico_test_suite.py`, **81/0/5** no `web_test_suite.py`
incluindo a conta criada pela CLI, **8/8** nas ações web novas. O console de
emergência foi exercitado direto — os nove sobreviventes respondem, comandos
cortados devolvem a mensagem que diz para onde a configuração foi, prompt fica
em `SIMUT>`. O pack em espanhol foi carregado no dispositivo e o `help`
renderizou pelo `unaccent()` sem nenhum `?`.

### O que não foi verificado

- ~~O `tel_reset` nunca rodou em hardware.~~ **Verificado depois da publicação.**
  Contra o endpoint de teste da bancada: HTTP 200 e então 21 envios em ~3 min
  carregando 62.707 B — contra 6 envios e 3.678 B no uptime inteiro anterior —
  com **0 falhas e 0 retries**, leituras de sensor ainda sem erro e sem reboot.
  É o backlog reenviando exatamente como documentado. As cinco ações estão
  exercitadas.
- **Sem soak longo nesta build.** Releases anteriores levaram corridas de
  tempestade de horas; esta tem minutos.
- A **corrida do heartbeat do Core 1** sob carga pesada de flash
  (`APP_CORE1_DEAD` → soft panic) continua aberta, e não tem relação com este
  release.
- O pack es-ES está completo há pouco tempo e teve pouco uso real.

## v1.5.5-beta (2026-07-26)

Release de folga, e o que ela comprou. Um estudo de flash e RAM da 1.5.4-beta
encontrou que **a pilha Bluetooth estava linkada em toda imagem e nada nunca a
chamava** — 64.732 B de flash e 16.416 B de RAM por um subsistema que o
`build_src_filter` excluía e que o `SIMUT_BLUETOOTH=0` reduzia a stubs vazios. A
folga real saiu de 4.740 B para 69.472 B, e duas funções antes rejeitadas por
falta de espaço foram construídas com ela: a `/config` de volta ao firmware e
uma reescrita do histórico que preserva o dia em vez de descartá-lo.

O estudo está publicado inteiro em `docs/ANALISE_FLASH_RAM.md` — medido, não
estimado, incluindo os experimentos que acabaram não economizando nada.

### O Bluetooth que nunca esteve lá

- **`-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` selecionava a variante `liblwip-bt.a` e o blob de rádio *combinado* WiFi+BT**, enquanto `BluetoothManager.cpp` estava excluído de todos os ambientes que embarcam. Removendo: flash 1.039.740 → 975.008 B, RAM estática 131.436 → 115.020 B, heap 130.704 → 147.120 B. No ferro, o número que importa não é o heap livre e sim o maior bloco contíguo, **11.483 → 35.776 B** — o que o BearSSL pede, e a razão de o `setBufferSizes(4096, 512)` ter precisado existir.
- `lib_ignore = SerialBT` é obrigatório junto: o LDF do PlatformIO percorre o `#include <SerialBT.h>` dentro do `#if SIMUT_BLUETOOTH` mesmo com o ramo desligado. Ambientes que declaram o próprio `lib_ignore` substituem a lista herdada em vez de estendê-la, então o `pico_w_alpha` repete a entrada.
- O `pico_w_debug`, documentado como estourando o slot em ~69 KB, agora estoura **16.576 B**. Ainda não linka, mas ficou ao alcance.

### Um sensor adicionado hoje é gravado hoje

- **Um `.sim4` congela o schema no cabeçalho e casa valores por hwId**, então um slot criado ou renomeado depois que o arquivo do dia existe não tinha coluna para gravar. O registro continuava sendo anexado, aquele canal só ficava na sentinela NaN, e nada no log dizia isso. O único remédio era `sensor reschema confirm`, que recria o arquivo e joga o dia fora.
- **`migrateV4Schema` preserva o dia.** Reescreve o arquivo contra um schema montado dos slots atuais, carrega registro a registro cada coluna que ainda existe e preenche as novas com a sentinela NaN até 00:00. Sequência: parar tudo, verificar a origem (reparando cauda rasgada antes), gravar um `.mig` temporário, re-decodificar origem e substituto **em lockstep comparando cada coluna carregada**, e só então remover o original e renomear. O original fica intocado até o substituto ter sido relido do flash e comparado.
- **É streaming, não cópia para RAM, e essa é a decisão que importa.** Medido com o codec de produção, um dia completo no intervalo mínimo de 1 min é 9,7 KB com 9 medições mas **42,8 KB com 48** e 55,9 KB no teto do formato — contra ~47 KB de heap livre e maior bloco de ~36 KB. Bufferizar passaria numa bancada de cinco sensores e falharia num device cheio, que é exatamente quando a função importa. O streaming custa 5,6 KB constantes, independente do dia, do número de sensores ou do intervalo.
- Valores são carregados **crus** quando `(bitWidth, scale)` batem dos dois lados — o caso normal. Só uma mudança real de largura ou escala passa por float, porque um inteiro cru não significa nada sem o def contra o qual foi empacotado.
- Acessível por um botão no editor de slot da `/config`, que é onde você está quando percebe o problema. O cliente bloqueia enquanto houver edições não salvas: a migração lê os slots do flash, então rodá-la sobre alterações não gravadas congelaria o schema antigo de novo e gastaria o dia à toa.
- `POST /api/history_rebind` migra por padrão; `?force=1` seleciona o caminho destrutivo antigo, e a página só o oferece quando a migração falha por origem ilegível.

### A `/config` não precisa mais ser enviada à mão

- **A página vivia no LittleFS via `FS_PAGES`**, de quando a imagem tinha 660 bytes de folga. Isso carregava uma armadilha de bootstrap: num device recém-formatado ou recém-montado o arquivo não está lá, e a `/config` — a página que você precisa para configurar o aparelho — respondia *"Page asset missing"* até alguém subir o `config.html.gz` pela `/files`.
- De volta ao firmware por **11.544 B**, não os 12.152 B do array: a `serveProtectedFsPage` tinha a `/config` como único chamador, então o helper e a página de erro dele saem no gc-sections junto. O mecanismo continua no lugar, sem uso, para quando a folga apertar de novo.
- **Atualizar da 1.5.4-beta deixa um `/web/config.html.gz` órfão no device**, ocupando ~12 KB do LittleFS. Apague pela página `/files` ou com `POST /api/delete?file=/web/config.html.gz`. Não use `uploadfs`, que reformata a partição e leva o `/history` junto.

### Achados registrados, ainda não aplicados

- **O percentual de flash do PlatformIO não é a folga.** Ele omite a seção `.ota` (10.228 B) e o padding de alinhamento entre `.text` e `.rodata`: "98,3% usado" eram na verdade 4.740 bytes de sobra. O `docs/ANALISE_FLASH_RAM.md` traz a única medição que se sustenta.
- **O knob de mDNS documentado nunca funcionou.** O `NetworkManager` testa `#ifdef SIMUT_MDNS` num símbolo que o `simut_config.h` sempre define, então `-DSIMUT_MDNS=0` produz uma imagem de tamanho idêntico com os 236 símbolos `MDNSResponder` ainda linkados. O comentário dele fala em ~196 KB; o custo medido é 15.036 B.
- **A fragmentação do heap nasce no boot, não do tráfego.** 32 KB livres mas 11,4 KB contíguos, e não se moveu ao longo de 9,6 MB de tráfego e 679 requisições. Os candidatos são as duas alocações `GFXcanvas16` (40 KB de heap) e a excisão do pack de idioma, que tem pico de ~42 KB e deixa um buraco de 28 KB.
- **`-DNDEBUG` vale 6.600 B** e um `sscanf` remanescente vale 7.532 B; ambos medidos, nenhum aplicado aqui.
- **O patch do `lwipopts.h` que economiza 18 KB de `.bss` mora fora da árvore de build**, então um clone limpo compila sem ele em silêncio.

### Verificado

- 136/136 testes nativos em quatro suítes, incluindo 7 casos novos da migração: coluna adicionada, coluna removida, reordenação casada por hwId, NaN atravessando mudança de largura, conversão de escala, rebobinagem do codec, tamanho do cabeçalho.
- Migração no ferro contra 32 registros reais: adicionar um sexto sensor deu `meas` 9→10 com **32/32 registros e 240/240 valores idênticos**; remover deu 10→9 com 32/32 e 240/240 de novo.
- Carga na imagem publicada: **1.343 requisições HTTP em duas corridas, uma falha**. O dispositivo a contou como desconexão de cliente (`desconexao 1`), não como defeito próprio — o heap ficou estável, o PBUF reportou 0 falhas de alocação e o uptime foi contínuo nas duas corridas. A segunda corrida deu 691 requisições com 0 erros. Heap 41.268–41.572 B, maior bloco nunca abaixo de 30.075 B, pico de PBUF 7/12, 0 erros de leitura de sensor.
- Os números de heap são menores que os da 1.5.4-beta porque a migração custa 5,7 KB de `.bss`. Contra a 1.5.4-beta como publicada continua sendo um ganho grande: heap livre 32.220 → 41.572 B e maior bloco contíguo **11.483 → 30.075 B**.

## v1.5.4-beta (2026-07-26)

Release da interface web. A interface era utilizável no desktop e hostil no
celular, e a razão era estrutural, não cosmética: **nenhum breakpoint do código
mirava abaixo de 600 px**, então todo telefone existente caía inteiramente
abaixo do menor que havia. Páginas inteiras eram o layout de desktop espremido,
e quatro delas não eram apenas feias — eram inoperáveis.

Nada aqui custou flash de aplicação. A `.rodata` é alinhada em página de 4096
bytes e o conjunto todo coube no padding existente — folga medida em 4.740 B
antes e depois.

### Páginas que não davam para operar no celular

- **A tela de primeiro acesso tinha 432 px numa tela de 360** — `width:350px` mais `padding:40px` sem `border-box`. Transbordava em todo telefone comum em retrato e, como o corpo a centralizava por flex, metade do transbordo caía à esquerda, onde não existe rolagem para antes da origem. A caixa de login tinha o mesmo defeito, com 382 px. As duas agora são fluidas, e a centralização vertical saiu do `align-items` para `margin:auto`, que colapsa em vez de empurrar o conteúdo para fora do topo.
- **O botão de gravar saía da tela** — a topbar tinha `height:48px` rígido, sem quebra de linha, com ~475 px de conteúdo. O `#commit-btn` era o primeiro a ser expulso; em `/config`, `/network` e `/users` ele é o *único* jeito de gravar um formulário, já que o botão do formulário foi removido em favor dele. No celular ele agora ancora numa barra fixa no rodapé.
- **A tabela de sensores arrastava a página inteira de lado** — seis colunas, 168 px só de padding de célula num espaço de 228, e nenhum ancestral com `overflow-x`. A causa real estava um nível acima: `.main-content` é item de grid, e item de grid tem `min-width:auto` por padrão, que impede encolher abaixo do conteúdo — a tabela esticava a coluna para 824 px numa tela de 360 e o `overflow-x` do próprio card nunca chegava a ser consultado. Resolvido com `min-width:0` nos filhos do grid.
- **As linhas de som exigiam 428 px de viewport** — 278 px de largura inegociável (seletor de melodia de 140 px num grupo `flex-shrink:0`, botão de teste, interruptor de 44 px, gaps e padding) dentro de 272 px, sem `flex-wrap` para nada descer de linha. O rótulo agora toma a primeira linha e os controles dividem a segunda. Os sliders de volume também precisaram de `min-width:0`: `flex:1` deixa `min-width:auto`, e o mínimo automático de um `range` é sua largura intrínseca de ~129 px, então ele se recusava a encolher.

### A escala de celular, aplicada uma vez só

O `/style.css` é servido como um único blob gzipado que todas as páginas
linkam, então regras colocadas ali custam flash uma vez e alcançam as dez
páginas. É onde vive o breakpoint de celular: padding de container e card de
20/24 px para 12/16 px, piso de 44 px em botões e itens da gaveta, `100dvh` na
gaveta e limite de viewport no menu do seletor customizado.

- **O rodapé da gaveta era inalcançável** — `height:100%` resolve contra a viewport grande, então Licença, idioma e Sair ficavam sob a barra do navegador, e o `nav { flex:1 }` consumia toda a folga, de modo que o `overflow-y` nunca gerava rolagem para chegar até eles.
- **Oito páginas redeclaravam `toggleDrawer()`** — cópias idênticas da função compartilhada e, como o `<script>` inline da página roda depois do `/lang.js`, cada uma sobrescrevia a original. Qualquer melhoria na versão compartilhada virava código morto. As duplicatas foram removidas; agora existe exatamente uma no firmware.
- **O toast de rede cobria a topbar** — largura total em `top:0` com `z-index:9999` contra o 50 da topbar. Em erro persistente, o hambúrguer — única navegação no celular — ficava escondido enquanto o toast durasse.
- **A posse dos GPIOs só existia em tooltip** — `title="slot N"` não existe no toque, então a única forma de saber de quem era o GP7 era abrir os dezesseis slots. O número do slot agora vai impresso no pino, e os pinos ficam numa grade uniforme em vez de pílulas dimensionadas pelo próprio texto.

### Cache

- **Atualizar o firmware não chegava ao navegador** — `/style.css` e `/lang.js` são servidos com `Cache-Control: public, max-age=604800`. Sete dias, sem forma de invalidar: gravar firmware novo não mudava nada que o navegador fosse perguntar de novo. O build agora carimba `?v=<hash>` nessas URLs, derivado do hash do `WebUI.h` que o packer já calculava, então o cache longo continua valendo e se quebra sozinho exatamente quando os assets mudam.

### Tela de login

- **A marca virou vetor, não texto** — ela renderizava na fonte que a pilha do sistema oferecesse, então mudava de desenho entre Safari, Chrome e Android. Cinco glifos traçados do Liberation Sans Bold em paths SVG estáticos: idênticos em qualquer lugar, sem fonte a carregar, e 638 B gzipados contra ~1.130 B do WOFF2 recortado equivalente em base64. Nenhum arquivo de fonte é embarcado; ver a nota 13 nos Avisos de Terceiros.
- A marca ficou maior, recebeu o acento da interface e ganhou a expansão da sigla como legenda. Essa legenda deliberadamente não é traduzida: SIMUT é a sigla da frase em português, e traduzi-la quebraria a correspondência com as letras.

### Corrigido

- **Os campos de limite de alarme saíram do diálogo de sensor** — duplicavam a `/alarms`, que edita as mesmas quatro chaves e ainda acopla `min < max`. O payload de staging continua carregando as quatro: seu helper `num(id, d)` devolve o valor gravado quando o elemento não existe, então nada é zerado. Vale notar que a `/api/alarms` só devolve sensores ativos, então os limites passam a ser definidos depois de ativar o slot.
- **O filtro de log do histórico estilizava seus checkboxes como campos de texto** — `.log-header input` é seletor de elemento e pegava `#chkInf`, `#chkWrn` e `#chkErr`; abaixo de 600 px a media query lhes dava `width:100%`, transformando três checkboxes em barras pretas de largura total. O único breakpoint da página estava piorando-a.
- **O aviso de alterações pendentes dizia "no topo"** — verdade no desktop, errado no celular desde que o botão de gravar foi para o rodapé. Agora neutro quanto à posição, nos dois pacotes de idioma.

## v1.5.3-beta (2026-07-25)

Release de estabilidade e telemetria. A maior parte veio de perseguir reboots até
a causa real em vez da primeira plausível — vários itens abaixo registram uma
hipótese que a medição derrubou, justamente as que teriam mais chance de voltar.

### Ciclo de vida do Core 1 e reboots (classe R1)

- **O Core 1 era resetado à força estando saudável** — o `getHeartbeat()` protegia com `_isPausedForFlash`, uma flag declarada, zerada em cinco lugares, lida ali e **nunca setada como true**. Cada milissegundo de lockout de flash virava obsolescência aparente; passando de 10 s o watchdog matava um núcleo são, travando o Core 0 e quebrando respostas HTTP em voo.
- **Escrita de flash sem pausar o Core 1** — o `writeHistoryEntryFlashV4` gravava enquanto o Core 1 buscava de XIP, travando o árbitro QSPI. Corrigido com o RAII `Core1FlashPause`, refcontado.
- **A autópsia de crash imprimia uma constante** — `scratch[3]` e `scratch[5]` eram destruídos nos primeiros instantes do `setup()`, então todo reboot era classificado como travamento de watchdog em `C0=[BOOT]`. Duas sessões foram gastas lendo um canal forense que devolvia a mesma resposta acontecesse o que acontecesse. Agora é fotografado antes de qualquer coisa sobrescrever.
- **A janela do watchdog nunca foi de 15 s** — o registrador do RP2040 satura em 8,388 s, então todo `WdtWindow` que pedia mais recebia exatamente o padrão. A classe fica, o comentário parou de mentir, e operações longas são dimensionadas **alimentando** o watchdog.
- **Ciclo de vida do Core 1 visível no `show metrics`** — marcadores de fase, pior travada por fase, latência QSPI, contabilidade de lockout.

### Telemetria

- **Handshake TLS podia travar o Core 0 para sempre** — o `_wait_for_handshake()` do upstream não tem prazo global: o `_run_until()` reinicia o próprio contador a cada chamada, então o `setTLSConnectTimeout()` limita uma iteração e nunca o handshake. Contra um peer que aceita TCP sem fechar o handshake — uma porta errada bastou — o Core 0 girava ali permanentemente. Corrigido via `tools/arduino_pico_overrides`, que agora também alimenta o watchdog dentro do laço limitado.
- **O BearSSL pedia 16 KB contíguos e o heap tinha 11,3** — `setBufferSizes(4096, 512)` derruba o buffer de recepção para o que cabe. Medido no instante da tentativa: 31.900 B livres, 11.370 B contíguos. Liberar memória não adianta com o heap fragmentado; o que importa é o bloco.
- **`TelemetryGuard` removido, não consertado** — alegava alimentar o watchdog durante chamadas de rede bloqueantes com um timer de 2 s. Medido: o timer tiquetaqueia certo até o `http.POST()` e para no instante em que ele bloqueia. Nunca funcionou em build nenhum. Consertá-lo seria pior — alimentar através de um handshake travado converte um reboot recuperável num congelamento permanente.
- **Templates recusavam `{u..}` e `{p..}`** — o `{pAMB}` comparava 7 bytes contra um token de 6 caracteres, então só resolvia no fim absoluto do template. Adicionada pressão por slot `{p0}`..`{p15}`, resolvendo contra o slot que de fato a reporta, de modo que a chave reescrita casa com a chave do histórico V4 daquele canal.
- **Live Preview alinhado ao firmware** — o editor conhecia só `{t0}`..`{t9}` de um dígito, então todo `{u..}`, `{p..}` e `{t10}`..`{t15}` era ecoado literal e um template que funcionava parecia quebrado. O `/api/config` passou a expor `hum`/`press` por slot.

### Histórico (V4)

- **Registros gravados com timestamp e sem dado, reportados como sucesso** — trocar a identidade de um sensor no meio do dia para toda gravação de valores: o schema vive no cabeçalho do `.sim4` e o casamento é por `hwId`, enquanto o `ensureV4Schema` restaura esse cabeçalho do arquivo existente em vez de reconstruí-lo. O `writeHistoryEntryV4` retornava sucesso assim mesmo, então o log seguia dizendo "History record saved" a cada minuto. Uma linha vazia é pior que uma lacuna, porque parece dado. Agora é recusada, com `APP_HIST_SCHEMA_MISMATCH` (código 515) avisando uma vez.
- **`sensor reschema confirm`** — comando privilegiado novo que religa o histórico do dia aos slots como estão configurados. Destrutivo: recria o arquivo de hoje, perdendo os registros anteriores do dia.
- **Correções de codec** — refill pós-falha, decode transacional em duas passadas, virada de meia-noite, e o valor `-0,01 °C` colidindo com a sentinela NaN.
- **Streaming de gráficos secava o Core 1** — faixas grandes decimavam dezenas de milhares de registros sem alimentar o watchdog entre emissões.

### Memória

- **O pack de idioma segurava 14 KB de heap em benefício do navegador** — o carregador de `.lng` faz malloc do arquivo inteiro e nunca libera; o `@WEBDICT` é metade dele e nenhum caminho do firmware o lê. Agora é excisado do buffer e transmitido do LittleFS sob demanda. Medidos 14.052 B recuperados contra 14.124 B previstos; a RAM do dashboard foi de 81% para 70%.
- **`/config` mudou para o sistema de arquivos** — o slot da aplicação tinha 660 bytes livres. Servir a página comprimida do LittleFS devolveu folga real de 8.852 B.

### Web e UI

- **Sensores configuráveis pelo `/config`** — o dashboard volta a ser só status.
- **`/api/logs` enviava sem guarda**, e dois handlers travavam em si mesmos no lock de leitura.
- **O gráfico do painel superior pedia o sensor -1**, não achava nada, e reiniciava o dispositivo.
- **O redraw completo pintava 90% dos pixels duas vezes** — 254 ms → 126 ms.
- **Falhas de toque agora dizem o motivo** em vez de apagar a tela.

### i18n

- **Pack pt-BR completado** — faltavam todas as chaves de sensor e 35 mensagens de log.

### Limitação conhecida

A corrida do heartbeat do Core 1 sob carga pesada de flash (classe R1, `APP_CORE1_DEAD` → soft panic) **não** está fechada. É rara e ortogonal a tudo acima, e é o gap de estabilidade restante.

## v1.5.1-beta (2026-07-19)

### Correção do Modo AP — Toque na Inicialização

- **Removido wake-up SPI do XPT2046** — A transação SPI manual (`0x90`) durante o boot colocava o XPT2046 em power-down com PENIRQ desabilitado (PD0=0). Os bytes de dados no protocolo pipeline herdavam PD0=0, mantendo o PENIRQ permanentemente desligado e impedindo a ativação do modo AP por toque. O circuito de touch-detect do XPT2046 está sempre ativo após power-up — nenhuma inicialização SPI é necessária. Correção: modo AP agora ativa corretamente ao segurar o toque na inicialização.

### Correções de Persistência da Calibração

- **Alterações de calibração agora persistem no reboot** — O caminho de reboot do `commit_all` salva corretamente os dados de calibração. Antes eram perdidos em reboot por watchdog.
- **Evita reescrita do `calib.csv` quando `nChanges==0`** — Evita escritas flash desnecessárias quando não há alterações.
- ** salvamento rápido para sensores sem ROM** — Sem travamento no modo silencioso ao salvar calibração de sensores sem identificador ROM.
- **Alterações de hwId/nome na calibração agora instantâneas** — Mudanças aplicam em 0,4s em vez de exigir reload completo.

### Melhorias no Dashboard & UI

- **Persistência do slot 0 no painel superior** — O slot 0 agora persiste corretamente no painel superior após alterações de offset ou tema.
- **Troca automática do painel inferior** — Quando o slot do painel superior muda, o painel inferior agora alterna automaticamente para o próximo slot disponível.

### Pacotes de Release para Arduino IDE

- **`tools/build_release.sh`** — Script automatizado para gerar `.zip` compatíveis com Arduino IDE para as variantes `simut_tft` (ILI9341) e `simut_alpha` (HD44780).
- **Estrutura de arquivos achatada** — Todos os arquivos na raiz do sketch; includes de subpastas (`ota/`, `display/`, `sensors/`) reescritos para caminhos planos.
- **Ambas variantes compilam com arduino-cli** — TFT: 911.888 bytes (87%), Alpha: 819.636 bytes (78%) no RP2040 Pico W com filesystem de 1 MB.

### Arquivos de Atualização OTA

- **Binários do firmware** — `release/simut_v1.5.1-beta.bin` (atualização OTA) e `release/simut_v1.5.1-beta.uf2` (flash via USB mass-storage).

## v1.5.0-beta (2026-07-19)

### Configuração de Hardware Centralizada — `simut_config.h`

- **Arquivo único de configuração** — Todas as opções configuráveis agora ficam em `src/simut_config.h`: tipo de display, pinos, sensores, Bluetooth, mDNS, pacotes de temas, pino do buzzer e limites avançados. Antes espalhadas por mais de 8 arquivos.
- **9 seções documentadas** — Tipo de display, pinos TFT, pinos Alpha/HD44780 (I2C e paralelo), buzzer, sensores, comunicação, pacotes de temas, pino padrão 1-Wire, limites avançados. Cada opção com comentários explicativos.
- **Guardas `#ifndef` em todas as definições** — Cada define permite override em tempo de compilação via flags `-D` no `platformio.ini`. Os defaults correspondem à configuração de release existente.
- **Retrocompatível** — Headers de configuração antigos (`DisplayConfig.h`, `SensorConfig.h`) delegam ao `simut_config.h`. Todas as cadeias de `#include` preservadas. Sem breaking changes.
- **Suporte a Arduino IDE** — Guarda `__has_include("simut_arduino_config.h")` no topo do `simut_config.h` para pacotes de release. Configs de release simplificadas para definir overrides antes do include.

### Limpeza do Sistema de Build

- **`platformio.ini` desduplicado** — Flags de sensores e features removidos do `[pico_base]` (agora no `simut_config.h`). Apenas overrides específicos por ambiente permanecem no `[env:pico_w_alpha]`.
- **Pacotes de release simplificados** — `release/*/simut_arduino_config.h` agora inclui `simut_config.h` em vez de duplicar todas as definições.

### Correções de Bugs

- **BluetoothManager.cpp** — Adicionado guarda `#if SIMUT_BLUETOOTH` ausente em todas as implementações dos métodos. Previne erros de redefinição quando `SIMUT_BLUETOOTH=0` e o arquivo é compilado (builds debug).
- **HD44780_16x2.h** — Envolvido `_initLcd()` e seu ponto de chamada em `#if HD44780_MODE_PARALLEL`. A sequência de inicialização paralela 4-bit era incorretamente compilada no modo I2C.

### Seleção de Pacotes de Temas

- **Movida para `simut_config.h`** — Pacotes de temas (`SIMUT_THEMES_HEALTH`, `_PRO`, `_MEDICAL`, `_SAFETY`, `_RETRO`, `_NATURE`, `_UTILITY`) agora são habilitados descomentando linhas no arquivo de configuração, não editando `Themes.cpp`.
- **`Themes.h` inclui `simut_config.h`** — Flags de temas são visíveis onde quer que `Themes.h` seja incluído.

### Coexistência de Recursos PIO — Resolução de Conflitos Multi-Sensor

- **Conflito no pio0 identificado** — OneWirePIO (DS18B20, 27 slots de instrução) + WirePIO (BME280 I2C, 32 slots) = 59 > 32 disponíveis. WirePIO carregava primeiro, bloqueando OneWirePIO (DS18B20 morria — sem fallback GPIO).
- **Saturação de SMs no pio1** — 2× DHT22 (2 SMs) + CYW43 WiFi (1 SM) + BuzzerPIO (2 SMs) = 5 > 4 SMs. Resolvido pelo fallback automático do BuzzerPIO para pio0.
- **Correção no `BME280Driver.h`** — Adicionado `forceGPIO(true)` antes de cada `begin()`. BMx280PIO agora usa apenas GPIO bit-bang I2C (sem PIO+DMA), mantendo os slots de instrução do pio0 livres para OneWirePIO. Modo GPIO é um pouco mais lento mas totalmente confiável.
- **`docs/PIO_ANALYSIS.md`** — Análise completa de alocação de recursos PIO cobrindo todas as bibliotecas (OneWirePIO, DHTBus, WirePIO, BuzzerPIO, CYW43), orçamento de slots de instrução por bloco, contagem de state machines, canais DMA, cenários de conflito e mecanismos de resolução.

### Validação em Hardware — Teste de Coexistência com 4 Sensores

Testado no Pico W com display TFT + buzzer + WiFi:

| Sensor | GPIOs | Tipo | Status |
|--------|-------|------|--------|
| BMP280 | GP0 (SDA), GP1 (SCL) | driver BME280 | ✅ Lendo (GPIO bit-bang) |
| DHT22 #1 | GP2 | DHT22 | ✅ Detectado, lendo |
| DHT22 #2 | GP3 | DHT22 | ✅ Detectado, lendo |
| DS18B20 | GP4 | DS18B20 | ✅ Detectado (ROM: 283C21…), lendo |

- **WiFi**: Conectado (RSSI -45 dBm), servidor web respondendo
- **PIO após correção**: pio0 31/32 slots (OneWirePIO + BuzzerPIO fallback), pio1 23/32 slots (DHTBus×2 + CYW43)
- **Heap**: 94,3 KB estável, sem vazamentos após 11+ minutos de operação contínua
- **Leituras**: 857/916 OK (93,2%), 59 erros concentrados na inicialização
- Todos os 4 sensores configurados e ativados via CLI, configuração persistida no flash

### Orçamento de Flash

- **Release (TFT + todos sensores + mDNS)**: 94,1% (982604 / 1044480 bytes)
- **Alpha (HD44780 paralelo + todos sensores + mDNS)**: 85,4% (891920 / 1044480 bytes)
- **RAM (release)**: 35,8% (93760 / 262144 bytes)

## v1.4.4-beta (2026-06-07)

### Gestão de Recursos GPIO — Montagem Guiada de Slots

- **Comando `gpio`** — Mapa de recursos GPIO mostrando todos os 16 pinos com estado de alocação (FREE ou `[Slot XX] Tipo (Função)`), mais uma lista consolidada de GPIOs livres. GPIOs agora são um recurso limitado visível e rastreável.
- **`sensor <slot> create <tipo>`** — Criação guiada de slot. Define o tipo do driver, limpa atribuições de pinos anteriores, ativa o slot e mostra: contagem de pinos, função e flags de cada pino (ex.: `1-Wire (pull-up)`), GPIOs livres disponíveis e uma dica para o próximo comando (`sensor <slot> pin <idx>,<gpio>`).
- **`sensor <slot> type <tipo>`** — Agora mostra os requisitos de pinos e as atribuições GPIO atuais por pino após mudar o tipo, para que o usuário saiba o que conectar.
- **`sensor <slot> pin <idx>,<gpio>`** — Agora mostra o rótulo da função para contexto (ex.: `pin[0]=GPIO 3 (1-Wire)`). Detecta quando todos os pinos necessários estão atribuídos e sugere o próximo passo (`sensor <slot> name "<nome>"`).
- **`sensor <slot> active on`** — Valida pré-requisitos antes de ativar: tipo deve estar definido, driver deve estar compilado e todos os pinos declarados devem estar atribuídos. Reporta exatamente quais pinos estão faltando.
- **`show sensor types`** — Lista drivers de sensores compilados com contagem de pinos, resumo de canais e rótulos de função (ex.: `BME280 | 2 pinos | Temp+Hum+Press | SDA,SCL`).

### Driver BME280 — Temperatura + Umidade + Pressão

- **`BME280Driver.h`** (~9KB flash) — Driver I2C autocontido usando medições em modo forçado. Sem dependência de biblioteca externa (evita Adafruit_BME280 com ~15KB).
- **Máquina de estados assíncrona** — BME_IDLE → dispara medição forçada → BME_WAITING → lê resultados, seguindo o padrão assíncrono do DS18B20/DHT22.
- **Fórmulas de compensação** — Matemática inteira conforme datasheet Bosch BME280 §4.2.3 para temperatura, umidade e pressão. Oversampling ×1 em todos os canais (~9ms por leitura).
- **Renderização no painel TFT** — Temperatura + umidade no dashboard (espelha layout do DHT22), suporte a painel min/max. Pressão disponível via API (canal `CH_PRESS`).
- **Auto-detecção I2C** — Sonda endereços 0x76 e 0x77. Scan de hardware detecta BME280 no barramento I2C ativo.
- **Inicialização GPIO multi-pino** — `gpioInitForRole()` agora chamada para TODOS os pinos declarados (não apenas `pins[0]`). Barramento I2C inicializado uma vez quando o primeiro sensor I2C é encontrado. `ROLE_POWER` padrão para output LOW.

### Diagnósticos Melhorados

- **`show sensors`** — Saída redesenhada: slot, GPIOs, tipo do driver, canais (ex.: `T+H+P`), nome, ROM (1-Wire), HWID, estado de alarme e limites por canal.
- **`show sensor types`** — Drivers disponíveis com contagem de pinos, resumo de canais e rótulos de função dos pinos.
- **`PIN_ONEWIRE_DEFAULT`** — Corrigido aviso de redefinição do pré-processador (8 instâncias eliminadas).
- **Todos os 4 canais inicializados** — Loop `MAX_SENSOR_CHANNELS` define `avgValue` para NAN e `calibrationOffset` para 0.

### Outras Mudanças

- **mDNS habilitado por padrão** — `-DSIMUT_MDNS=1` no platformio.ini. Dispositivo acessível via `http://simut.local`. Custo: ~15KB flash, RAM desprezível.
- **Auto-detecção I2C0/I2C1** — `i2cPeripheralForPins()` seleciona o periférico correto em tempo de execução. Qualquer par de GPIO 0-15 funciona para sensores I2C (sujeito ao hardware).
- **`checkAndAutoHealSensors()`** — Não reporta mais falsos avisos "Sensor ausente" para tipos de sensor não-DS18B20 (DHT22, BME280).
- **Guarda de boot do BME280** — Timeout I2C (50ms) + sonda ACK previne travamento no boot quando BME280 está configurado mas não fisicamente conectado.
- **Suposições de GPIO hardcoded removidas** — `begin()` do DHT22 não referencia mais GPIO 10. Métodos legados do DS18B20 usam o pino do primeiro sensor ativo. Zero acoplamento fixo GPIO-tipo.

### Orçamento de Flash

- **Release (DS18B20 + DHT22 + mDNS)**: 93,1% (972KB / 1044KB) — ~72KB livres
- **Com BME280**: 93,7% (979KB / 1044KB) — ~65KB livres
- **RAM**: 35,7% (~93,7KB / 262KB)

## v1.4.3-beta (2026-06-07)

### Dieta de Flash — 86KB Liberados (97,8% → 91,2%)

- **LEAmDNS desabilitado por padrão** — Envolvido com `#ifdef SIMUT_MDNS`. Habilite com `-DSIMUT_MDNS` nas build_flags quando necessário. Economiza ~196KB de biblioteca do link.
- **Stub do BluetoothManager** — Quando `SIMUT_BLUETOOTH=0` (padrão), a classe inteira é inline no-ops. `BluetoothManager.cpp` excluído da build. `SerialBT` ainda compilado pelo framework mas símbolos não usados são removidos pelo linker.
- **CLI `sensor pin <slot> <índice> <gpio>`** — Atribui GPIOs específicos a slots de sensor com detecção de conflitos entre todos os sensores ativos. Valida faixa GPIO (0-15) e índice de pino (< MAX_SENSOR_PINS).
- **Orçamento de flash**: 91,2% (952KB / 1044KB) — 92KB livres para futuras features.

## v1.4.2-beta (2026-06-07)

### Arquitetura de Entidade de Sensor — Funções de Pino Baseadas em Driver

- **Enum PinRole** — Cada pino GPIO agora tem uma função declarada (`ROLE_DATA`, `ROLE_I2C_SDA`, `ROLE_I2C_SCL`, `ROLE_SPI_MOSI`, `ROLE_SPI_MISO`, `ROLE_SPI_SCK`, `ROLE_SPI_CS`, `ROLE_UART_TX`, `ROLE_UART_RX`, `ROLE_ANALOG`, `ROLE_POWER`).
- **PinRequirement no SensorFormat** — Cada driver declara contagem de pinos, função, rótulo e flags (pull-up, open-drain) via `SensorFormat::forType()`. Sem configuração GPIO hardcoded por tipo.
- **`gpioInitForRole()`** — Auto-configura direção GPIO, pulls e função baseado na função declarada. Substitui blocos de inicialização hardcoded `#if SIMUT_SENSOR_DHT22`.
- **Metadados de pino na API** — `/api/status` agora retorna `pc` (contagem de pinos) e `pr` (rótulos de função: "Data", "SDA,SCL") por sensor.
- **Info de pinos na WebUI** — Tabela do dashboard mostra contagem de pinos + funções ao lado do tipo do sensor (ex.: `DHT22 ⚡1p Data`, `BME280 ⚡2p SDA,SCL`).
- **Adicionar um novo sensor** agora requer apenas um arquivo de driver + entrada `SensorFormat::forType()` — display, API, calibração e inicialização GPIO seguem os metadados automaticamente.

## v1.4.1-beta (2026-06-07)

### Arquitetura Universal de Slots — 16 Slots GPIO

- **16 slots universais de sensor** — `MAX_SENSORS` expandido de 10 para 16, cobrindo GPIO0–GPIO15. Todos os slots agora são uniformes com tipo, hwId, friendlyName, pinos e limites de alarme configuráveis.
- **Sensor ambiente eliminado** — O campo especial `ambientSensor` no `SystemConfig` foi removido. O Slot 10 (GPIO10) agora é um slot universal regular, tratado identicamente aos demais. A convenção `idx: -1` da API é substituída pelo índice de slot padrão `10`.
- **Generalização de canais** — `RuntimeSensor` agora usa arrays `avgValue[4]`, `buffers[4]` e `calibrationOffset[4]` com enum `SensorChannel` (CH_TEMP, CH_HUM, CH_PRESS, CH_LUX). Cada driver declara seus canais via `SensorFormat::forType()`. Adicionar um novo tipo de sensor requer apenas um driver — display, API web e calibração adaptam-se automaticamente.
- **Coluna de tipo na tabela do dashboard web** — Tabela agora mostra o tipo do driver (DHT22/DS18B20) por sensor. Formulário de calibração mostra campos de umidade condicionalmente por sensor baseado na flag `hasHum`.
- **Sistema de alarme unificado** — Máscara de alarme por slot agora cobre todos os 16 slots. As flags separadas `ambTempAlarm`/`ambHumAlarm` foram removidas.
- **Migração de config v16→v17** — Migração automática: `ambientSensor` movido para `sensors[10]`, slots 11–15 inicializados como inativos.

### Correções

- **Travamento no boot após flash** — Eliminadas chamadas `Serial` bloqueantes no caminho de boot (`BLOG`, `LogManager`, `CommandManager`, `SoundManager`). Removido `Serial.ignoreFlowControl(true)` que causava atrasos de 1s por linha de log.
- **Prevenção de stack overflow** — Alocações de `SystemConfig` movidas para heap (`tempConfig`, `encBuf`) para evitar o limite de 4KB de stack do RP2040 com a struct v17 maior.
- **Bluetooth desabilitado** — `SerialBT.begin()` causa hardfault no CYW43 após warm boot (reset via picotool). Bluetooth agora desabilitado para garantir boot confiável. USB Serial + interface Web fornecem funcionalidade equivalente.
- **Correções de JSON na API** — Restauradas chamadas `first = false` e `if (!safeSend(buf))` em `/api/sensors`, `/api/status` e `/api/users` que causavam JSON inválido (vírgulas faltando entre objetos).
- **Calibração na WebUI** — Removido card ambiente duplicado. Todos os sensores renderizados uniformemente com campos cientes do tipo.

### Breaking Changes

- **Formato de config v17** — Layout do `SystemConfig` alterado. Configs v16 são auto-migradas no primeiro boot. Downgrade para ≤v1.3.x requer factory reset.
- **API `/api/sensors`** — Sensor ambiente não mais reportado como `idx: -1`. Slot 10 aparece no array padrão de sensores.
- **Formato de histórico** — `BinaryHistoryRecord` alterado de 28 para 40 bytes. Arquivos `.bin` existentes são incompatíveis.
- **Bluetooth removido** — `SerialBT` desabilitado devido a hardfault no CYW43 em warm boot. Use USB Serial ou interface Web.
- **Formato de sensor no `/api/status`** — Adicionados campos `type` e `ch`. Campo de umidade agora usa `sensorHasChannel()` genérico em vez de verificação hardcoded `TYPE_DHT22`.

## v1.3.0-beta (2026-06-07)

### Display Alpha — Suporte a HD44780 16×2 Alfanumérico

- **Driver dual-mode HD44780** — I2C (mochila PCF8574) e GPIO paralelo 4-bit, selecionável via flags `HD44780_MODE_I2C` / `HD44780_MODE_PARALLEL`
- **Seleção de display em tempo de compilação** — Flags `SIMUT_DISPLAY_TFT` e `SIMUT_DISPLAY_ALPHA` permitem compilar para ILI9341 TFT (padrão) ou HD44780 16×2 (alpha), mutuamente exclusivas
- **Modo I2C** — Usa I2C1 nos GPIO 26 (SDA) / GPIO 27 (SCL), endereço 0x27 (configurável via `HD44780_I2C_ADDR`). Zero conflitos com slots de sensor — todos os 10× DS18B20 + DHT22 disponíveis
- **Modo paralelo 4-bit** — RS=GPIO 16, EN=GPIO 17, D4=GPIO 18, D5=GPIO 19, D6=GPIO 20, D7=GPIO 21. Também zero conflitos com slots de sensor
- **GPIO 0-15 reservados para sensores** — Pinos do display mapeados exclusivamente para GPIO 16+, sem deslocamento de sensores
- **Loop do display alpha no Core 1** — Framebuffer de caracteres com blit(), exibição com alternância automática de temperatura/umidade
- **Exclusão da GFX Library** — Adafruit GFX Library, ILI9341 e XPT2046 excluídos da build alpha via `lib_ignore`. Inicialização SPI e detecção de toque protegidas com `#if SIMUT_DISPLAY_TFT`
- **Clock UART1 preservado** — `uart_init()` chamado no modo alpha (apenas clock, sem takeover de GPIO) para manter marcadores de debug do StorageManager seguros
- **Timeout de skip WiFi** — Builds alpha sem botão de skip por toque têm timeout de 30 segundos para conexão WiFi e evitar travamento infinito no boot
- **Ambiente de build `pico_w_alpha`** — Build limpa com 89,0% flash (929 KB), 34,6% RAM (90 KB). Economiza ~84 KB vs build release

### Correções

- **Loop infinito na calibração de touch** — Protegido com `#if SIMUT_DISPLAY_TFT`; build alpha não tem controlador de toque
- **Conflito de pinos SPI no alpha paralelo** — `SPI.begin()` estava configurando GPIO 16-19 antes da inicialização do HD44780, causando falha de boot no modo paralelo
- **Recuperação de corrupção de armazenamento flash** — `picotool erase` completo resolve partição de filesystem corrompida após flashing repetido

### Documentação

- **WIRING.md** — Reescrita completa com três diagramas de pinagem (ILI9341 TFT, HD44780 I2C, HD44780 Paralelo), tabela comparativa, referência de pinos HD44780 e checklists de montagem para cada modo

## v1.2.1-beta (2026-06-06)

### Painéis de Dashboard Duplos Independentes

- **Arquitetura de painel unificada** — Ambos os painéis usam a mesma função `drawSlotPanel()`. O painel ambiente dedicado (`drawAmbientPanel`) foi eliminado (~280 linhas economizadas).
- **Painel superior: modos fixo/interativo** — Pressionamento longo (1s) alterna entre modo fixo (sensor fixado, estilo normal) e modo interativo (fundo cinza escuro + elementos brancos, segue o seletor de slot para escolher qual sensor fixar).
- **Painel inferior: sempre interativo** — Toque curto alterna apenas min/max. Sempre segue os botões SLOT inferiores.
- **Botão S10** — Adicionado slot 10 (DHT22 ambiente no GPIO 10) à barra de botões inferior. Oculto quando o painel superior está fixado nele.
- **Renderização min/max movida para drivers** — `DS18B20_renderMinMax()` e `DHT22_renderMinMax()` nos respectivos drivers, despachados via `sensorRenderMinMax()`. Primitivas compartilhadas em `SensorDrawing.h` reutilizam ícones existentes.
- **Rastreamento de min/max de umidade por slot** — Arrays de umidade por slot com acumulação em tempo real a cada ciclo do loop.
- **Dados independentes do painel superior** — Campos `topSlot*` no `SystemState` com setters dedicados `setTopSlotData()`/`setTopSlotMinMax()`.
- **Atualizações instantâneas de painel** — Render incremental agora compara campos `topSlot*`. `pullSnapshot()` mantém `topSlotIdx` sincronizado para espelhamento no AppManager.
- **Correção de flash de alarme** — Flash de alarme do painel superior verifica `isSlotAlarming(topSlotIdx)` em vez das antigas flags de ambiente.
- **Correção de cor de borda** — Faixa de conteúdo do modo normal usa `borderColor` em vez de `C_TEXT_SUB` hardcoded.
- **Correção de preenchimento de fundo** — Faixa de conteúdo usa `panelBg` em vez de `C_BG_MAIN` para vermelho de alarme e cinza de seleção corretos.

### Comunidade & Documentação

- **Terceira contribuição da comunidade** 🎉 — Suite completa de documentação em espanhol por [@f-p-0](https://github.com/f-p-0): README.md (337 linhas, PR #66), CONTRIBUTING.md (140 linhas, PR #68) e CODE_OF_CONDUCT.md (39 linhas, PR #68), tornando o SIMUT acessível para usuários hispanofalantes em todo o mundo
- **Segunda contribuição da comunidade** 🎉 — Ambiente de desenvolvimento Docker para que contribuidores possam compilar e testar sem instalar PlatformIO localmente ([@JohnMartin0301](https://github.com/JohnMartin0301))
- **Primeira contribuição da comunidade** 🎉 — Suite de testes HistoryCodec v2 com 672 linhas cobrindo codificação roundtrip, limites de frame âncora, compressão NaN e buffer overflow ([@LorenzoLongaretto](https://github.com/LorenzoLongaretto))

### Orçamento de Flash

| Configuração | Flash |
|---|---|
| Ambos sensores ON | 1030872 (98,7%) |

## v1.2.0-beta (2026-06-06)

### Subsistema OTA — Atualização Completa para v4.6.2

- **F-OTA-BOOTLOOP corrigido** — Loop20 OTA 100% PASS. Causa raiz: deadlock reentrante do LittleFS durante escrita do README.md + inicialização do Core 1 adiada para pós-WiFi + safeReboot usa MMIO idêntico ao applier_reboot.
- **F-RESTORE** — Backup/restore confiável via API (98/100 PASS). Snapshot de configuração preservado através do apply OTA com integridade CRC32. Reescreve atômica do calib.csv com VERSION=epoch.
- **F-RAM-SLIM** — Uso de RAM 49,6% → 33,7% (-41 KB / -16pp). Eliminados graph caches, removidos glifos de fonte não usados, buffers compartilhados.
- **F-TEL-HTTPS-RESILIENT** — Corrige crash+reboot quando servidor HTTPS cai. Heap budget mais conservador para conexões TLS.
- **F-OTA-STAGE-NOBLOCK + F-FLASH-DIET** — Corrige TCP drop durante staging de firmware OTA. Upload não-bloqueante com chunk sizing adaptativo.
- **F-DISPLAY-MARGINS** — `fillMarginsBlack` + override do `fillScreen` no `TftWithOffset` para bordas limpas.
- **F-BOOT-CYW43-CYCLE** — Power-cycle do `WL_REG_ON` sempre no `setup()` para inicialização confiável do WiFi.
- **F-SCREENSHOT-INTEGRITY** — Elimina perda/corrupção de linhas no `/api/screenshot` via leitura multi-amostra com voto majoritário.
- **F-OTA-ADMIN-ONLY** — Endpoints OTA exigem `PERM_FULL_ADMIN`.
- **F-TEL-ADAPTIVE** — Telemetria com vazão adaptativa (dimensionamento de lote apenas no backend).
- **F-UI-OTA-FLOW** — Mensagens de UX para OTA + restore com feedback de progresso.

### Documentação & Ferramentas

- **Glossário** — `docs/GLOSSARY.md` decodificando todas as tags inline (F-\*, BUG-\*, SEC-\*, CON-\*, DOC-\*, REF-\*) usadas nos comentários do código.
- **Limpador de comentários** — `tools/cleanup_comments.py` remove referências de histórico de versão e marcadores de changelog dos comentários para preparação de releases.

### Orçamento de Flash

| Configuração | Flash |
|---|---|
| Ambos sensores ON | 1031464 (98,8%) |
| Apenas DS18B20 | ~1028400 (98,5%) |
| Apenas DHT22 | ~1029500 (98,6%) |
| Ambos OFF | ~1024900 (98,1%) |

### Testes

49/49 testes passando (27 validators + 22 HistoryCodec).

## v1.1.0-beta (2026-06-06)

### Arquitetura de Sensores — Sistema Modular de Drivers

- **Flags de compilação por sensor** — `SIMUT_SENSOR_DS18B20`, `SIMUT_SENSOR_DHT22`, `SIMUT_SENSOR_BME280` no `platformio.ini` permitem desabilitar drivers não usados para liberar flash (DS18B20: -2,7 KB, DHT22: -1,6 KB, ambos: -6,1 KB)
- **Configuração universal de slot** — `SensorRecord` v16 com campo `sensorType` explícito + suporte multi-pinos (`pins[4]`), pronto para sensores I2C, SPI, ADC e UART
- **Drivers organizados** — diretório `src/sensors/` com `DS18B20Driver.h`, `DHT22Driver.h`, `SensorConfig.h`, `SensorHelpers.h`
- **Migração de flash v15→v16** — Atualização automática de schema preservando todas as configs de sensores, detecção de tipo via ROM durante a migração
- **Catálogo SensorPresets** — 130+ formatos de exibição pré-definidos em `sensors/SensorPresets.h` cobrindo 30+ grandezas físicas (temperatura, umidade, pressão, peso, luz, química, elétrica, vazão, etc.)
- **Sistema SensorFormat** — `SensorValueFormat` (unidade, decimais, ícone) + `SensorFormat` (1-3 valores por sensor) + factory `forType()` em `sensors/SensorHelpers.h`

### Display — Renderização de Painel Controlada por Driver

- **Ícones nos drivers** — `sensors/SensorDrawing.h` com ícones procedurais (termômetro, gota, manômetro, lâmpada, régua, tubo, raio, pulso, tubulação, bússola, bandeira, átomo, bateria, etc.) protegidos por flags de compilação
- **Painel renderizado pelo driver** — `DHT22_renderPanel()` e `DS18B20_renderPanel()` cuidam do layout completo (ícones, formatação, unidades) via dispatch `sensorRenderPanel()`
- **Painel de slot agora mostra umidade** — DHT22 em qualquer slot exibe temperatura e umidade com ícone de gota e sufixo traduzido (%UR/%RH)
- **Cores do tema** — Drivers recebem `C_TEXT_SUB`, `C_TEMP_OK`, `C_TEMP_HOT`, `C_HUMIDITY` do tema ativo; ícones acompanham mudanças de tema
- **Posicionamento original exato** — `textAnchor=92`, `iconX=14`, `rightMargin=15` copiados do `drawAmbientPanel` original
- **Formatador genérico** — `formatSensorValue()` em `DisplayManager_FmtFloat.h` trata NaN e casas decimais variáveis

### Correções de Bugs

- **Modo AP via toque no boot** — XPT2046 recebe comando SPI de ativação durante o boot inicial; pino PENIRQ lido diretamente via `gpio_get()`. Janela AP sempre abre independente do estado de settle.
- **Calibração de touch obrigatória no primeiro boot** — Sensibilidade + calibração de 4 pontos executa antes do dashboard quando `magic != 0xCA`. Cancelar durante o boot aplica defaults seguros.
- **Comando `sensor define`** — Sintaxe estendida aceita tipo do sensor: `sensor define <gpio> <rom> <tipo> <hwId> <nome>`. Sintaxe legada de 4 tokens auto-detecta pelo ROM.
- **Comando `sensor accept`** — Define `sensorType` explicitamente nos sensores DS18B20 aceitos.

### Orçamento de Flash

| Configuração | Flash |
|---|---|
| Ambos sensores ON | 1031464 (98,8%) |
| Apenas DS18B20 | ~1028400 (98,5%) |
| Apenas DHT22 | ~1029500 (98,6%) |
| Ambos OFF | ~1024900 (98,1%) |

### Testes

49/49 testes passando (27 validadores + 22 HistoryCodec).

## v1.0.0 (2026-06-03)

### Lançamento Público Inicial

- **Suporte a múltiplos sensores** — Até 16 sensores em slots configuráveis: DS18B20 (1-Wire), DHT22 (Data), BME280 (I2C)
- **Pipeline de sensor zero-trust** — Verificação de ROM, detecção de mismatch de hardware, histerese de erro
- **Display TFT ILI9341 320×240** — Dashboard, gráficos em tempo real, configurações via toque (XPT2046)
- **50 temas integrados** + suporte a temas customizados via LittleFS
- **Servidor web embarcado** — Sessões multi-usuário, RBAC (10 bits de permissão), gerenciador de arquivos
- **WebUI comprimida com gzip** — Páginas inline minificadas com CSS/JS compartilhado
- **Telemetria** — HTTP POST e MQTT com templates JSON/CSV/customizados, TLS/SSL
- **CLI de canal duplo** — USB Serial + Bluetooth (BLE)
- **Sincronização NTP** — Backoff exponencial, fallback multi-servidor, RTC virtual
- **Codec de histórico v2** — Delta + sensor-mask + codificação anchor, ~45% de redução de tamanho
- **Autenticação reforçada** — HMAC-SHA256, salt aleatório por usuário, 5000 rounds
- **Atualização OTA** — Upload via interface web, preservação de snapshot de configuração, auto-reboot
- **Backup e restauração** — Backup/restauração completa do LittleFS com integridade CRC32 (formato BKP1)
- **Perícia de crash** — Autópsia via scratch registers do watchdog com monitoramento cross-core
- **Internacionalização** — Inglês + Português/Espanhol via pacotes de idioma externos
