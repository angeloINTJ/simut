# Changelog

[English](CHANGELOG.md) | **Português**

Todas as mudanças notáveis do firmware SIMUT.

## Não lançado — branch `feature/simut-air`

### SIMUT Air: build headless com ciclo de hibernação em deep sleep (experimental)

Novo ambiente PlatformIO `pico_w_air`: sem display, sem buzzer, a pilha
web/serial/Bluetooth da Alpha no boot frio (M0) e um ciclo de hibernação (M1)
que entra por `air hibernate` ou após um tempo de inatividade. A cada wake do
RTC o firmware lê os sensores até estabilizar enquanto o Wi-Fi conecta em
paralelo, sempre grava a amostra no histórico local, drena a telemetria
pendente quando está online e volta a dormir pelo intervalo do histórico (ou
pelo backoff da telemetria, quando maior). A hibernação é o SLEEP do RP2040
(WFI no XOSC com alarme do RTC); o DORMANT foi tentado e descartado por ser
não-determinístico na bancada. O CYW43 é desligado pelo WL_REG_ON, o pull-up
do USB é solto para o host ver uma desconexão limpa, o watchdog é desarmado e o
wake é marcado como reboot limpo para a autópsia de boot ficar calada. A
configuração do Air vive em `/config/air.bin`; `CONFIG_VERSION` não muda. O
console de emergência ganha `system ssid` e `system pass` em todas as imagens,
e `air status|hibernate|stop|idle` na Air. Testes de host: `pio test -e native_air`.

**Corrigido antes de publicar: o wake nunca acontecia na hora.** Desligar o
oscilador em anel antes do WFI economizava um pouco de corrente, mas o deixava
parado através do wake, porque o reset seguinte não passa pelo domínio de reset
do ROSC. O boot ROM subia sem oscilador em anel e o wake demorava um tempo longo
e variável: medido na bancada entre 16 e 48 minutos para um intervalo de 2
minutos, contra 147 a 151 segundos na build anterior à mudança. O oscilador
passa a ser religado logo após o WFI, antes do reset, e a mesma bancada mediu
110,8 s de sono com 26,5 s de janela acordada. Custo: 32 bytes.

Pendências antes de publicar: a revisão e a bancada de 06/09/2026 deixaram três
itens abertos. A fase FLUSH sem teto de tempo, a energia dos sensores não ligada
no modo operacional nem durante o boot, e um histórico que não é monotônico em
12 dos 174 intervalos, porque o interior de cada bloco é reconstruído pelo passo
nominal em vez dos tempos em que as amostras foram tomadas. O plano, as
evidências e os testes de aceite estão em
`docs/analysis/SIMUT_AIR_PLANO_FIX.md`, `tools/air_test_suite.py` e
`tools/check_air_consistency.py`.

## v2.3.9-beta (2026-08-29)

### Seletor web da alpha corrigido (estava travado em inglês)

O seletor de idioma e a interface da build alfanumérica caíam para o inglês nas
páginas autenticadas: o localizador do pacote de idioma truncava o buffer do
cabeçalho ao ler @NAME, então @CODE nunca era interpretado e /api/perms
retornava o código de idioma vazio. O cabeçalho agora é interpretado sem mutar o
buffer, então a interface web adota o pacote PT/ES instalado como padrão.

## v2.3.8-beta (2026-08-29)

### A interface web abre no idioma instalado

Na primeira visita, a interface web agora abre no idioma do pacote instalado em
/lang (PT ou ES, quando presente e compatível) em vez de sempre em inglês. O
seletor de idioma continua funcionando da mesma forma, e a escolha é lembrada no
navegador — só o padrão da primeira visita mudou.

## v2.3.7-beta (2026-08-29)

### O help da CLI alpha fala o idioma do dispositivo

A CLI de emergência da build alfanumérica imprimia o `help` em inglês mesmo com
o pacote de idioma português ou espanhol carregado. Dois stubs na variante alpha
eram os responsáveis: `getActiveHelpText()` não retornava nada, e `unaccent()`
não fazia nada. O primeiro agora lê a seção `@HELP` do pacote de idioma sob
demanda, e o segundo converte o texto UTF-8 para ASCII no terminal serial —
assim o `help` acompanha o idioma do dispositivo (PT/ES).

## v2.3.6-beta (2026-08-29)

### Página de licença reescrita e traduções corrigidas

A página /license agora segue o padrão simut-rx: um resumo em linguagem simples
(traduzido para PT/ES), uma nota legal curta e, em seguida, o texto original em
inglês da MIT e os avisos de terceiros — o único texto com valor jurídico.

Os avisos de terceiros foram completados com as quatro bibliotecas de firmware
que faltavam — TwoWirePIO_RP2040, BMx280PIO_RP2040, lwIP e BTstack — tanto na
página web quanto na tela de licença do dispositivo.

A build alpha agora serve as traduções da web direto do pacote de idioma
(GET /api/lang), e a build TFT mantém apenas o @DICT residente na RAM, lendo
@HELP e @LICENSE do LittleFS sob demanda.

Corrigida uma regressão em que o help da CLI e a tela de licença do dispositivo
sempre caíam no inglês: o leitor sob demanda dimensionava o buffer como
`sizeof(char)` em vez do buffer inteiro, então lia sempre zero bytes.

## v2.3.5-beta (2026-08-24)

### Alpha ganha Bluetooth e um display multi-sensor

A build alfanumérica (HD44780) agora alterna por todos os sensores ativos no
display 16x2, prefixando cada leitura com `S<n>` quando há mais de um slot ativo,
e mostra `AP` em vez do ícone de Wi-Fi enquanto serve o ponto de acesso. A
pressão é exibida em caracteres comuns (dígitos grandes para hPa de 4 dígitos
ficam para depois).

O Bluetooth é habilitado na build alpha: a CLI SerialBT inicia no boot mesmo sem
conexão Wi-Fi, e um novo comando `ap` — tanto no USB quanto no Bluetooth —
inicia o modo Ponto de Acesso para configuração.

O código de screenshot ("Display Capture"), exclusivo do TFT, agora é excluído
da build alpha para liberar flash, e o painel esconde a caixa Display Capture
quando o firmware informa que não há captura disponível. O servidor web também
responde às sondas de portal cativo do sistema em modo AP, permitindo que o
seletor de arquivo de Restauração abra no iOS/Android.

## v2.3.4 (2026-08-24)

### A linha 2.3 fica estável

A v2.3.4 promove a dieta de RAM lançada na v2.3.4-beta — buffers de gráfico
para heap sob demanda e pacote de idioma carregado apenas quando necessário —
ao canal estável após a campanha de validação completa (237/237 testes nativos,
todos os gates host verdes e o A/B dirigido de bancada).

## v2.3.4-beta (2026-08-24)

### Uma dieta de RAM que mantém todos os recursos

O caminho de renderização de gráfico não segura mais seus buffers de trabalho
permanentemente. O acumulador de buckets e as duas áreas `GraphDataPackage` —
juntas ~18,6 KB de `.bss` estático — agora são alocadas sob demanda no heap e
liberadas quando o gráfico fecha. Como são transitórias, o linker devolve o
espaço ao heap: a RAM estática cai de 49,3% para 42,2% (129.256 → 110.616 bytes)
e a região de heap cresce na mesma medida. No painel, o medidor de RAM cai de
~95% para ~83%, voltando para cima do guarda de telemetria que protege o
handshake TLS.

O pacote de idioma agora só é carregado quando um idioma diferente do inglês é
selecionado, mantendo os ~15 KB de heap de tradução livres em um dispositivo em
inglês.

Validado antes da publicação: build de release completo, 237/237 testes
unitários nativos e um A/B de bancada confirmando a neutralidade da mudança no
servidor web.

## v2.3.3-beta (2026-08-23)

### Alarmes ganham a própria linha de telemetria, e erros de sensor ganham voz própria

O firmware agora envia uma **segunda linha de telemetria, independente e
dedicada a alarmes**, rodando ao lado da linha de medição que já existia. Cada
chave de alarme carrega dois domínios — `alarm` (o limite) e `err` (uma falha
de hardware/comunicação) — de modo que um sensor que estoura o limite e um
sensor que para de responder são distinguidos de ponta a ponta, na tela, no
syslog e no payload enviado. O domínio `{err}` carrega o ciclo de vida
completo: silenciar e desativar por slot viram códigos no payload, e não apenas
estado local da interface. A fila de alarmes fica em RAM com ACK, o payload é
editável e herda o transporte já configurado para a linha principal de
telemetria (CLI, Web e métricas enxergam tudo).

Erros de sensor agora são cidadãos de primeira classe. Um sensor que perde
comunicação — ou é trocado num canal vivo — dispara um **alarme de ERRO**
dedicado, com painel âmbar e branco fixado no topo do dashboard. Alarme de
limite e de erro são totalmente independentes por slot: limpar um nunca limpa o
outro, e um sensor restabelecido após a falha regenera o próprio alarme de
erro. Desativar um sensor que está em ERRO limpa o âmbar do display, e o syslog
passa a refletir o estado real. A desativação por slot é **só em RAM** — nunca
toca o sistema de arquivos, então não sobrevive a reboot nem a uma escrita
perdida.

A interface web ganha uma **página dedicada de Telemetria com Live Preview dos
alarmes** espelhando o builder principal, além de uma padronização visual de
botões, caixas de texto e toggles. No transporte, um cursor de telemetria que
ficava à frente dos dados podia travar o envio — agora ele se auto-corrige em
vez de travar o upload.

Validação em hardware concluída na bancada antes de publicar: suítes HTTP
(14/14), TLS (15/15), MQTT+ACK (8/8) e CLI dump/flush (3/3), todas passando.

## v2.3.2-beta (2026-08-21)

### A piscada branca entre páginas acabou

Toda página autenticada guardava os tokens do tema só no `/style.css`, que
chega por um fetch de JavaScript *depois* da primeira pintura (a fila de
entrega que mantém vivo o servidor TLS de uma conexão), e o HTML das páginas é
`no-store` de propósito — então todo clique pintava o branco default do
navegador e "estalava" para o escuro quando o CSS chegava. As páginas de login
e setup já carregavam o antídoto inline; as outras oito agora também: os
tokens `:root` do tema escuro mais `html`/`body` pintados com as variáveis,
inline em cada página. A primeira pintura já nasce escura; o CSS compartilhado
que chega depois só acrescenta o acabamento. Usuários do tema claro nunca
foram afetados — o `lang.js` é script síncrono no head e injeta os tokens
claros com especificidade maior antes da pintura — então a paleta clara não
precisou de cópia inline.

Custo: +976 bytes de flash nas oito páginas (~122 B gzipados cada). Nada muda
em como os assets carregam ou cacheiam (a invalidação por `?v=` de hash de
build já existia).

Também neste release: as mudanças da v2.3.1-beta abaixo são promovidas a
Latest — ver aquela entrada para a devolução de RAM e o teto do lote de
telemetria.

## v2.3.1-beta (2026-08-20)

### A RAM que o fix do stall fez refém foi devolvida, e a telemetria volta a respirar

O pool TLS estático da v2.3.0 corrigiu um stall real de watchdog, mas cobrou
seus ~21,5 KB como BSS em **toda** configuração — inclusive nas só-HTTP, onde
nunca existe accept TLS. Medido na bancada: o heap livre ocioso caiu de
~38 KB para ~16 KB, abaixo do gate pré-voo da telemetria, então todo ciclo de
envio abortava para o backoff antes de tocar a rede — em qualquer transporte,
e sem como recuperar a RAM desligando features. O pool agora é reservado uma
única vez no boot, de um heap ainda sem fragmentação, e **somente quando o
servidor HTTPS vai realmente subir**: configs HTTPS mantêm exatamente o
comportamento anti-stall (accepts continuam sem malloc), configs HTTP
recuperam os 21,5 KB (`pico_w_release` RAM 55,0% → 46,8%).

Com o espaço de volta, o teto do lote de telemetria sobe de 50 para **250
registros por upload** (interface web, `/api/commit_all` e `tel batch`
aceitam 1–250). O teto é o que se pode pedir; o que cada ciclo envia de fato
continua dimensionado pelo heap livre — reserva de 32 KB sob TLS / 12 KB em
claro, ~350 B por registro JSON, ~160 B por registro CSV (a estimativa única
antiga cortava os lotes CSV pela metade sem proteger nada), mais o
encolhimento sob pressão que já existia no construtor de payload. O gate
pré-voo também ficou ciente do transporte: telemetria HTTP/MQTT em claro não
fica mais muda abaixo de 24 KB livres — esse piso existe para o scratch do
TLS, que os transportes em claro nunca alocam.

Nada mudou no formato do payload, na lógica do cursor ou nos transportes.

## v2.3.0-beta (2026-08-20)

### Páginas HTTPS carregam em um terço do tempo, e o reboot escondido embaixo delas se foi

Toda resposta carregava `Connection: close`, então o navegador pagava um
handshake TLS completo de ~510 ms por request — oito vezes por página. O
servidor web agora mantém a conexão viva por padrão: um handshake por sessão,
e o tempo de página medido caiu de ~6,1 s para ~2,4 s (−61%). A chave fica na
página de rede, na seção Servidor Web, aparece apenas quando o par de
certificados TLS está presente (onde o handshake de fato custa), e vem ligada
por padrão; configurações existentes herdam o padrão. Páginas HTTP ganham um
corte menor (~−20%) do mesmo reuso.

Por baixo, um defeito anterior ao trabalho de keep-alive foi corrigido. Cada
accept TLS alocava ~22 KB do heap (contexto do servidor + buffers de E/S)
enquanto o cliente TLS da telemetria alocava e soltava ~10 KB por tentativa
no mesmo heap; com ~29 KB livres e a free list fragmentada, a alocação de
16,7 KB podia travar o Core 0 além da janela do watchdog. Quatro autópsias
finas independentes puseram a morte na mesma linha, e a falha se concentrava
nos primeiros minutos após cada boot — um ciclo de reboots que se
auto-alimentava sob navegação comum ("abrir página, ler, clicar"). O contexto
do servidor e os buffers agora vêm de um pool estático (+22 KB de BSS),
tirando toda alocação grande do caminho do accept: um soak de 30 minutos que
reproduzia 3 reboots por watchdog sem o pool rodou limpo com ele, e as falhas
intermitentes de página (~10-20% sob navegação com pausas) zeraram com a
mesma mudança.

As correções seguem como overrides do framework em
`tools/arduino_pico_overrides/` (`webserver_keepalive.patch`,
`clientcontext_acked_feed.patch`, `bearssl_server_static_pool.patch`),
aplicados pelo `patch.sh` como antes.

## v2.2.18-beta (2026-08-20)

### O loop de login de HTTPS para HTTP agora se explica

Entrar por HTTPS dá ao cookie de sessão o atributo `Secure`, e pela regra dos
navegadores de "não mexer em cookies seguros" uma página `http://` comum não
pode nem enviar esse cookie de volta nem sobrescrevê-lo. Então o primeiro login
depois de o HTTPS ser desligado — o certificado apagado, o aparelho de volta em
HTTP — pode aceitar as credenciais e então voltar direto para a tela de login,
porque nenhum cookie de sessão chega ao aparelho. É uma regra do navegador e não
uma falha do servidor (um login HTTP limpo, sem cookie residual, funciona), e o
servidor não consegue apagar um cookie `Secure` por HTTP para sair do loop.

A página de login agora transforma esse loop silencioso numa instrução. Um login
com sucesso carimba o navegador; chegar a uma página autenticada limpa o carimbo;
e se a página de login carrega com o carimbo ainda fresco — acabou de ser jogada
de volta — mostra um aviso, no idioma do aparelho, dizendo ao operador para abrir
uma janela anônima ou limpar os cookies deste site. O cookie de sessão é por
sessão, então fechar e reabrir o navegador também o limpa. A seção de HTTPS do
manual ganha a mesma nota.

## v2.2.17-beta (2026-08-19)

### O navegador consegue operar a interface web por HTTPS, e um certificado ruim não trava mais tudo

O transporte HTTPS foi validado de ponta a ponta por um navegador real pela
primeira vez, e isso expôs dois problemas que esta versão corrige — um que
deixava a interface quase inutilizável sobre TLS, e outro que podia travar a web
inteira.

**O slot TLS único contra o navegador.** O lado servidor do BearSSL precisa de
um buffer de 16 KB por conexão e este heap comporta exatamente um, então o
servidor web atende **uma conexão TLS por vez**. Um navegador carrega uma página
abrindo várias conexões de uma vez — a folha de estilo, o script compartilhado e
as chamadas de API do painel — e as que não pegam o buffer único são
descartadas. As chamadas `fetch` com retry acabavam vencendo; o `<link>` da
folha de estilo, que o navegador nunca repete, não, então o painel subia sem
estilo e pela metade. O login era pior: o nonce corria com os fetches paralelos
do carregamento e chegava velho, um 401 garantido.

A correção casa o cliente ao servidor. Sobre HTTPS, todo `fetch` passa por uma
**fila de concorrência 1** — um request em voo por vez, cada um com um abort de
8 segundos para uma conexão presa não travar o resto. A folha de estilo sai de
um `<link>` paralelo em disputa para um fetch na fila e com retry; o script
compartilhado continua um `<script>` bloqueante para o código da página manter
seus globais em ordem. O login busca o nonce sequencialmente na hora do envio e
repete o post. Sobre HTTP puro, onde o buffer não é o gargalo, nada disso
instala — esse caminho fica byte a byte igual.

Medido no hardware com o Chrome: o painel agora carrega 4 de 4 — estilizado, com
dados ao vivo e sem erros de console — onde era 0–1 de 4 antes; config,
history, network e alarms todos carregam; o login é 5 de 5. As cargas por HTTPS
são seriais e portanto mais lentas que por HTTP (~9,6 s contra ~4 s), mas são
confiáveis. Custo: cerca de 220 bytes de interface gzipada.

**Um certificado incompatível não prende mais você.** Um certificado e uma chave
que cada um parseia mas não pertencem um ao outro sobem um servidor HTTPS cujo
handshake sempre falha — e o fallback existente só cobre um certificado que
falha ao *parsear*, então a web ficava morta na 443 com a 80 já fechada,
alcançável por nada além do console serial. Agora existem duas recuperações. O
`system https off [confirm]` no console serial apaga o par e volta para HTTP,
tomando lugar ao lado de admin-reset, factory e format como uma saída para a web
travada. E o Access Point de setup (segurar o touch no boot) agora sempre serve
HTTP, ignorando qualquer certificado, então a interface de recuperação também é
alcançável pela rede.

## v2.2.16-beta (2026-08-19)

### Uploads via HTTPS param de morrer no quarto kilobyte

O transporte HTTPS opcional saiu na v2.2.6-beta e ficou sem teste no
hardware até hoje. A primeira bateria completa achou o buraco: todo upload
acima de ~4 KB matava a conexão no meio do corpo (bisseção limpa: 3,5 KB
passa, 5 KB morre em meio segundo) — um pack de idioma ou uma imagem de OTA
não atravessava uma sessão cifrada, enquanto downloads funcionavam, porque
os records de transmissão do próprio dispositivo são pequenos.

A causa é o enquadramento de records do TLS, não o caminho de upload. Um
record precisa caber **inteiro** no buffer de recepção; a extensão que
limitaria o tamanho (max-fragment-length) é oferecida só por clientes, e
OpenSSL e navegadores de fábrica nunca a oferecem — mandam records de 16 KB
para qualquer corpo grande. O buffer de recepção do servidor era 4.096 B,
um teto escolhido quando o heap não tinha mais para dar. Agora é 16.709 B
(`BR_SSL_BUFSIZE_INPUT`, um record inteiro), alocado por conexão aceita e
não no boot; o maior bloco livre pós-boot mede 33,6 KB, então um cliente
TLS cabe com folga.

Medido no hardware depois do conserto: o pack es-ES de 32 KB sobe via TLS
em 1,9 s, 120 KB fazem ida e volta com checksums batendo, o `/api/lang`
continua byte-idêntico ao baseline HTTP, e o handshake segura os 0,5–0,7 s
(TLSv1.2, `ECDHE-ECDSA-AES256-GCM-SHA384`, cookie `Secure`). O preço é
honesto e transitório: com uma conexão TLS viva o heap livre cai para
~15 KB — que é também o motivo de o servidor manter a outra forma dele,
**um cliente TLS por vez**; uma segunda conexão simultânea é derrubada sem
resposta. Atualizações de firmware seguem recomendadas por HTTP puro.

A feature também nunca tinha chegado à documentação. O manual ganha
*Serving the UI over HTTPS* (§6) — o comando openssl, o provisionamento
pela página de Arquivos, o padrão da porta 443 e o contrato de fallback:
um par ausente ou ilegível nunca tranca o operador para fora, e sobrescrever
a chave com lixo é o botão de desligar. A lista de superfície de ataque do
`SECURITY.md` e a linha de comparação do README passam a mencionar o modo
HTTPS nas três línguas.

## v2.2.15-beta (2026-08-19)

### A interface web aprende a ser operada sem mouse

Primeira fatia de acessibilidade da interface web embarcada (o gap declarado
da issue #60). O ponto de partida, contado na fonte: seis atributos `aria-*`
na interface inteira, zero `tabindex`, uma chamada a `.focus()`, nenhum
tratamento de reduced-motion. A fatia cobre a infraestrutura compartilhada,
então toda página autenticada herda:

- O diálogo de credenciais virou um diálogo de verdade: `role="dialog"`/
  `aria-modal`, o foco entra, o Tab fica preso no único botão e o foco volta
  para quem abriu ao fechar. Escape deliberadamente **não** fecha — a senha
  aparece exatamente uma vez, e um Esc por reflexo antes de copiar custaria a
  credencial.
- O select customizado era só-mouse. As setas agora mudam o valor como um
  select nativo fechado, Enter/Espaço abre e fecha o menu, Escape fecha e
  mantém o foco; o botão carrega `aria-haspopup`/`aria-expanded` e o menu
  papéis `listbox`/`option` com `aria-selected`.
- A gaveta rastreia `aria-expanded`, move o foco para o primeiro link ao
  abrir, devolve ao hambúrguer quando fechada pelo teclado, e fecha no
  Escape. O toast virou live region educada. Nove botões só-ícone
  (calendário, navegação do gráfico, testes de som) ganham nome acessível.
- Login e troca forçada de senha, que não carregam o stylesheet
  compartilhado, ganham `:focus-visible` e bloco de `prefers-reduced-motion`
  próprios; o stylesheet compartilhado ganha o bloco de reduced-motion que
  faltava.

Zero strings traduzíveis novas — nomes acessíveis reutilizam texto visível
(`aria-labelledby`) ou a convenção inglesa de `aria-label` já existente,
porque o pack es-ES tinha 723 B de teto quando isto foi escrito (ver abaixo).
O comportamento foi verificado com teclado real via CDP contra a bancada
local, branch contra `main`: 30/30. Uma alegação morreu por medição no
caminho: o `:focus-visible` compartilhado já alcançava o select customizado —
o `outline:none` injetado perde o empate de cascata porque o `<style>`
injetado entra antes do `<link>` do stylesheet. Verificado por computed
style, e a regra redundante foi descartada em vez de publicada.

Custo: **+847 B** de interface gzipada. Também corrigido: o único warning
`-Wcomment` (um glob `/config/*` dentro de comentário de bloco, presente
desde o commit do HTTPS) — o único que todo build incremental de rotina mostrava.

### O teto do pack de idioma para de taxar RAM que as strings web nunca usam

O teto de 32.768 B do pack era um número só guardando duas coisas diferentes.
Cerca de 60% de um pack (`@WEBDICT`, 18,8 KB no es-ES) é JSON servido ao
navegador pelo `GET /api/lang` direto do flash — mas o loader malocava o
arquivo inteiro, parseava, e então excisava o blob para um segundo buffer,
com pico perto de 45 KB de heap para ficar com 13 KB. Enquanto isso o es-ES
estava em 32.045 B — **a 723 B do teto** — e toda string web nova era cobrada
contra RAM que ela nunca usou.

O `loadLangFile()` agora localiza o marcador `@WEBDICT` fazendo streaming do
arquivo por um chunk de 256 B no stack e lê para a RAM **só o prefixo
residente**. O range de bytes do blob é registrado para o handler web com uma
fórmula byte a byte igual à que a excisão antiga servia; o passo de
excisão-e-rebase foi deletado. Um contrato aparece: o `@WEBDICT` tem que ser
o **sufixo** do arquivo (os dois packs distribuídos já são) — o dispositivo
recusa violações e o `tools/check_lang_packs.py` se recusa a publicá-las,
agora lendo os dois tetos da fonte do parser para o gate não derivar. O
`tools/test_lang_gate.py` provoca cada modo de falha novo em packs
sintéticos: 8/8.

Dois tetos substituem o um: `LANG_RESIDENT_MAX` = 16.384 B para o malloc que
vive o uptime inteiro, `LANG_FILE_MAX` = 49.152 B para o arquivo no flash. O
es-ES sai de 97% de um teto para **residente 80% + arquivo 65%**: cerca de
17 KB ficam disponíveis para traduções web. O pico de heap no carregamento
cai de ~45 KB transitórios para 13,3 KB. Custo de flash: −8 B.

Validado no hardware por OTA: com o pack inalterado, o `/api/lang` é
byte-idêntico antes e depois da troca; um es-ES inflado a 36.545 B — que o
loader antigo recusa inteiro, revertendo a interface para inglês em silêncio
— carrega com o es-ES ativo e serve seu corpo de 23.285 B byte a byte.

**Ordem de deploy**: um pack acima de 32.768 B exige este firmware antes. Os
packs distribuídos estão inalterados e funcionam nos dois firmwares.

## v2.2.14-beta (2026-08-19)

### Syslog remoto — a trilha de auditoria sai da caixa

O log de eventos no aparelho vive num anel rotativo de no máximo ~1600
registros; um ambiente regulado precisa de uma cópia que saia do dispositivo,
append-only, e é isso que esta versão adiciona. Quando habilitado
(Configurações do Sistema → *Syslog Remoto*), cada evento de log qualificado é
encaminhado como uma mensagem [RFC 5424](https://www.rfc-editor.org/rfc/rfc5424)
sobre UDP para um coletor ou SIEM.

Deliberadamente **não** é um terceiro transporte de telemetria. UDP é
fire-and-forget: sem handshake, sem cliente TLS, sem cursor em flash, sem
máquina de reconexão — nada da maquinaria que o laço de envio da telemetria
carrega. O threading é o desenho inteiro: `logCode()`/`log()` rodam em qualquer
core, mas toda a rede neste chip é do Core 0, então o sink do log formata e
enfileira sob o mutex do log (onde a tag e a descrição do código ainda são
válidas para ler) e o laço principal drena e envia no Core 0. A ordem de lock é
sempre mutex-do-log → anel, nunca invertida. Um `WARN`/`FATAL` levantado
pouco antes de um reboot é despejado na saída; um travamento duro dos dois
cores não salva nada, o que nada salvaria.

Duas armadilhas em torno das quais o formato foi construído, ambas fixadas por
golden vectors nativos: o relógio nunca lê 0 mas cai para o build epoch e
viaja no tempo, então abaixo do limiar de sincronização o timestamp é o
NILVALUE `-` do RFC 5424 em vez de uma linha carimbada no passado; e o
cabeçalho delimitado por espaços cisalharia num nome de dispositivo com espaço,
então todo campo estruturado é saneado para ASCII imprimível. O contexto, o
core e o uptime viajam num elemento de structured-data; o código numérico do
log é o MSGID (estável e independente de idioma).

A config é um overlay de 8 bytes nos últimos bytes livres de `reserved[]` —
que agora está cheio. O coletor é um IPv4 cru, não um hostname: não há espaço
para um nome de 64 caracteres em 8 bytes, um coletor de LAN é endereçado por IP
na prática, e evita um caminho de falha de resolução DNS no laço quente de log.
O endereço reusa o mesmo validador de entrada endurecido abaixo.

Custo: ~2 900 B de flash para a feature inteira (motor, config, UI web e seis
chaves i18n novas). O pacote de idioma es-ES sobe para 97 % do seu teto de 32 KB.

### Parsers estritos de inteiro/float agora rejeitam overflow

O `parseIntStrict` conferia "só dígitos" mas não "cabe num int": respondia true
para `"2147483648"` com o valor silenciosamente saturado em `2147483647`,
porque `String::toInt()` é `atol()` e o `strtol` da newlib satura em vez de
falhar — um valor que o cliente nunca escreveu, entregue a chamadores cujo
contrato dizia "bem-formado". O parser agora acumula os dígitos ele mesmo com
uma guarda de overflow, então o resultado é exatamente o número escrito e não
depende da largura do `long` da plataforma. O `parseFloatStrict` tinha a versão
float do mesmo buraco: ~40 dígitos saturam o `atof` em ±inf, o que envenenaria
qualquer limiar comparado a ele — um resultado não-finito agora retorna false.
Achado e fechado ao adicionar cobertura de fuzz para os validadores de entrada
da API web.

Custo: 112 B de flash.

### Uma rota morta removida e uma matriz fixada para não apodrecer

O `/favicon.ico` estava registrado duas vezes; o servidor web casa o primeiro
handler de um caminho, então o segundo registro (um stub 204 depois do handler
do ícone real) nunca chegava a um cliente — removido. Ao lado dele, a matriz de
autorização — a permissão exigida por cada rota HTTP — agora é imposta no CI:
um portão de build lê a tabela de rotas e falha se uma rota não checa permissão
nem está numa allowlist pública documentada, então uma rota nova não pode ir ao
ar sem gate como o caminho de restore já foi. A matriz está escrita por inteiro
em `docs/AUTHORIZATION.md`, incluindo a fronteira de privilégio de dois níveis
que mantém backup completo, OTA e stage de restore alcançáveis só pelo admin
embutido, nunca por uma conta criada pela web.

## v2.2.13-beta (2026-08-19)

### O aparelho agora se apresenta ao Home Assistant

Com o transporte MQTT e payload JSON selecionados, um novo checkbox opt-in
faz o aparelho publicar mensagens retidas de [MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
a cada conexão com o broker. O Home Assistant cria o dispositivo e uma
entidade por medição automaticamente — temperatura e umidade por slot ativo,
mais pressão — com disponibilidade guiada pelo tópico de status LWT que já
existia. Zero YAML do lado do HA. No rig da bancada, seis sensores viraram
oito entidades cujos templates `value_json` bateram com o payload vivo chave
a chave.

O desenho se curva a um fato: o `commit_all` reinicia o aparelho. Então não
há ganchos de refresh ao vivo — quem reconcilia é a conexão após o reboot,
guiada por um bit persistido `FLAG_HA_PUBLISHED` que lembra que há configs
retidas no broker. É esse bit que permite ao checkbox recém-**des**marcado
publicar os payloads vazios retidos que removem as entidades: verificado num
mosquitto real, um subscriber novo vê zero configs retidas após desligar.

Unificação de brinde: o tópico do LWT derivava do `cfg.mqttTopic` cru
enquanto os publishes de dados usavam uma cópia com fallback `simut/data` —
um tópico em branco punha o will no degenerado `/status`. Will, dados e
discovery agora saem dos mesmos dois resolvedores e não podem divergir.

Custo: 2.544 B de flash, 0 B de RAM.

### O reserved[52..53] tinha posseiro, e ele comeu o magic byte da feature

Todo mapa do `SystemConfig::reserved[]` dizia que `[48..63]` estava livre.
Não estava: o dashboard do TFT persiste a seleção de slot em `[52..53]` por
literais crus registrados em lugar nenhum. O overlay do discovery pousou
nesses bytes e teve o magic sobrescrito pelo sentinela `0xFF` de "não
fixado" três segundos depois de cada boot — na bancada isso lia como um
toggle que não colava, e o que fechou o caso foi despejar os bytes crus pela
API em vez de confiar nos acessores.

Os dois bytes agora têm nome (`RESERVED_DASH_TOP_IDX` / `RESERVED_DASH_CUR_IDX`),
os literais crus morreram, os dois mapas falam a verdade, e o overlay mora em
`[54..55]`. Se for adicionar um overlay: grep por `reserved[<offset>` antes
de acreditar em qualquer comentário.

### GET /metrics — Prometheus sem escrever servidor

O complemento pull da telemetria push. Tudo que a rota serve já existia na
RAM para o `/api/status` ou o `show metrics`; ela só soletra no formato de
exposição em texto — 37 famílias de métricas: build info, heap e sistema de
arquivos, estado WiFi/MQTT, os contadores de telemetria, os contadores de
flash-op e do ciclo de vida do Core 1 (a observabilidade da imagem de
release que um soak longo precisa ler de fora), e um gauge por medição viva
com labels `slot`/`hwid`/`name`. O corpo parseia limpo no parser oficial
`prometheus_client`.

Um scraper não executa o fluxo de login, então além do cookie de sessão a
rota aceita HTTP Basic contra a tabela de usuários existente, exigindo a
mesma permissão de dashboard do `/api/status`. Credencial errada alimenta o
**mesmo lockout exponencial por IP do formulário de login** — a alocação de
slot saiu do `login_init` para um helper compartilhado exatamente para esta
rota não virar a porta barata em volta do rate-limit que o login já impõe.
Travado, até credencial correta leva 429; verificado ao vivo. Cada scrape
verifica a senha por inteiro (~0,7 s no aparelho), então mantenha o
`scrape_interval` em 15 s ou mais — seis scrapes seguidos seguraram ~690 ms
cada com o heap parado.

Custo: 4.560 B de flash, 0 B de RAM, nenhuma config nova e nenhuma string de
UI — o teto do pack es-ES segue intocado em 96%.

## v2.2.12-beta (2026-08-19)

### Um ícone que ninguém olhava segurava 11 KB de flash

`data/favicon.ico` não é um recurso do sistema de arquivos — um gancho de
pré-build embute os bytes literais em `src/Favicon.cpp`, então um byte no ícone
é um byte de firmware. Ele tinha **11 047 B**, ou seja, **22 % de toda a folga
restante**, gastos numa imagem de 32×32.

O peso nunca foi o desenho. A marca tem três cores — ciano `#00DCFF` sobre
azul-marinho `#0A172F`, com um anel `#7ADFFF` — mas estava guardada com **982
valores RGBA distintos** no quadro de 48 px. O rasterizador deixou ruído de ±1,
então pixels que parecem idênticos diferem numericamente:

    (10,23,48)  (10,23,47)  (10,22,47)  (10,23,49)  (11,25,49)

todos o mesmo marinho, nenhum deles comprimível como padrão. Além disso, o
contêiner carregava quatro quadros, e só o de 48 px custava 5 283 B.

Então isto é recodificação, não redesenho. Zerar o alfa abaixo de 32 — o que
também impede o quantizador de promover pixels quase transparentes a manchas
opacas em volta do disco —, quantizar para 16 cores, manter os dois quadros que
uma aba de navegador de fato usa, e montar o contêiner ICO à mão, porque o
escritor de ICO do Pillow recodifica as cargas e desfaz a quantização.

    16×16   306 B  +  32×32   491 B  +  38 B de cabeçalho  =  835 B

    flash do release   994 212 → 983 996 B     folga 50 268 → 60 484 B

O Chrome decodifica o resultado, e ele é indistinguível do original em 16 e
32 px. Largar os quadros de 24 e 48 px significa que pedidos acima de 32 px
passam a escalar a partir do quadro de 32.

### Um portão de análise estática, e os dois defeitos reais que ele achou

`tools/run_cppcheck.sh` roda o cppcheck 2.11 — fixado, porque o conjunto de
verificações muda entre versões e um portão sem versão fixa quebra num dia em
que ninguém mexeu em código. Roda na CI como job próprio, então seus minutos
não custam tempo de parede contra a compilação.

Quase tudo que ele apontou já estava correto e agora é respondido no lugar, com
supressão documentada: o alastramento de sinal do zigzag contra o qual o codec
do histórico é especificado, o espelho de octante do Bresenham, uma
inicialização de membro que o cppcheck lê como chamada. Dois achados eram reais,
ambos no contador de pacotes do painel:

    snprintf(pktBuf, sizeof(pktBuf), "%u", state.pendingPkts);

`pendingPkts` é `uint32_t`, que neste alvo é `unsigned long`, e não
`unsigned int` — então `%u` era a conversão errada para o tipo real do
argumento. Os dois pontos de chamada agora convertem explicitamente.

### Todo campo de CliDemand tem inicializador

Três dos seus nove campos não tinham, e o que é lido sem ser escrito é um
`bool` cujos dois valores são "cifrar telemetria" e "não cifrar". Não é
alcançável hoje — o único produtor o define na mesma linha em que define o tipo
—, mas é o mesmo formato do defeito do `/api/commit_all` fechado na
v2.2.10-beta, em que um booleano lido ao contrário ligava uma opção que o
usuário havia desligado. Nenhuma mudança de comportamento.

### A CI roda os cinco ambientes nativos

Rodava dois. Os outros três — HistoryV4, o analisador da CLI, LogPolicy —
existiam e passavam localmente, sem nada que os cobrasse num pull request.
Os cinco agora rodam como passos separados, para que a falha se identifique em
vez de se esconder atrás do primeiro ambiente que quebrou, e o
`tools/scan_secrets.sh` roda como primeiro passo de todos: um segredo versionado
não é uma falha de compilação para se descobrir no fim.

    252 casos   95 validadores · 56 HistoryV4 · 54 HistoryV5 · 29 CLI · 18 LogPolicy

### O projeto tem um logotipo

`docs/images/logo-mark.svg` (880 B) e `logo-wordmark.svg` (2 146 B) — o ícone
que já embarcava, redesenhado como vetor, para que a identidade seja uma coisa
só em vez de um ícone e um selo sem relação. As letras são contornos do DejaVu
Sans Bold, escolhido por sobreposição medida contra a marca original, e não a
olho: 91,2 % de IoU, contra 85,3 % do segundo colocado.

O logotipo com texto usa `#1a73e8` fixo, sem chave `prefers-color-scheme`. Essa
chave foi escrita primeiro e removida: dentro de um `<img>`, a media query segue
o tema do sistema operacional do leitor, não o tema que ele escolheu no GitHub,
então os dois podem discordar e o texto cai branco sobre branco. Uma cor só que
se sustenta nos dois é mais segura — medidos 4,51:1 sobre `#ffffff` e 4,20:1
sobre `#0d1117`.

## v2.2.11-beta (2026-08-18)

### "Conexão perdida", com o aparelho na rede local

A página de histórico carregava o Chart.js do `cdn.jsdelivr.net`. Sem internet, a
chamada `new Chart(...)` lançava um ReferenceError, o `catch` do carregador
engolia, e o usuário lia **Conexão perdida.** — com o aparelho na rede local e o
`.h5` já baixado e decodificado. A falha culpava a rede por um script ausente,
num produto cuja primeira promessa é funcionar offline.

A mesma tag trazia mais dois defeitos. Não fixava versão: `npm/chart.js` resolve
para a última major que o jsDelivr publicar, e a v3 para a v4 já quebrou a API de
opções uma vez — uma versão futura pararia o gráfico em aparelhos já entregues,
sem nada que se pudesse fazer. E não tinha `integrity`, num documento que carrega
o cookie de sessão, servido sem CSP.

### Podar a biblioteca foi medido, e não resolve

Registrando só o que a página usa — `LineController`, `LineElement`,
`PointElement`, `LinearScale`, `Legend`, `Tooltip` — o Chart.js 4.5.1 vai de
70 592 para 56 818 B em gzip. Vinte por cento, porque o peso não está nos tipos
de gráfico que ninguém usa:

    núcleo só, nada registrado ......... 43 527 B   desenha zero pixels
    + LinearScale ...................... 43 534 B   +7
    + Line/Point ....................... 49 049 B   +5 515
    + Legend + Tooltip ................. 56 818 B   +7 769
    + todo o resto ..................... 70 719 B   +13 901

Somar a escala linear a esse núcleo custa **sete bytes**, porque o motor de
escala — ticks, autoSkip, rotação, medição de rótulo, layout dos eixos — já está
nele. O que é irredutível é a generalidade: o resolvedor de opções em `Proxy`, o
motor de animação embarcado mesmo com `animation: false`, seis modos de
interação, um parser de cor CSS completo, matemática de spline. Nada disso é
usado aqui.

### h5g

Um renderizador que já sabe que tem três eixos, um tipo de série e passos de tick
fixos: 784 linhas, **4 721 B em gzip**, 15× menor que o pacote do CDN. Reproduz a
página como ela estava — x linear em epoch ms com a janela igual ao período
pedido mesmo quando vazio; três eixos Y independentes, porque a pressão perto de
1000 hPa achataria %RH e °C em retas; linha quebrada em `null`, para que um
período sem dado pareça um; tracejado por grandeza, para que a identidade nunca
dependa só da cor; raio visível na amostra isolada entre duas lacunas; a banda
mín/máx; legenda clicável que esconde a banda junto com a série; tooltip pelo
ponto mais próximo em X; redimensionamento, densidade de pixel e toque.

Dois comportamentos foram mudados de propósito, em vez de copiados:

- **A banda entra no intervalo do eixo Y.** O Chart.js a desenha de um plugin que
  a escala não enxerga, então o pico dela saía cortado pela borda. Numa cadeia
  fria esse pico *é* a excursão. Os badges MAX/MIN acima do gráfico dão o número,
  mas só com um sensor selecionado — com dois ou mais os badges somem e o extremo
  desaparecia por completo. A linha média perde cerca de um quarto da resolução
  vertical, com consciência.
- **Os rótulos do X giram 45° quando não cabem**, em vez de afinar os ticks.
  Afinar custava a grade junto: a 375 px o eixo ia de sete linhas verticais para
  três, e localizar um evento no tempo virava estimar entre marcas de doze horas.
  Girar custa ~17 px de altura. É reserva, não padrão: a 390 px com um sensor os
  rótulos ficam retos, porque cabem.

### Provado contra o motor que substitui

`scratchpad/h5g_20260818/` desenha oito casos congelados e sete capturas de
interação através do **`renderChart` da própria página**, extraído do `WebUI.h` em
vez de copiado, com o Chart.js fixado no diretório e sem tocar a rede. O primeiro
portão foi o controle: duas execuções do *mesmo* Chart.js, exigindo zero pixels
de diferença — um A/B cujo A-contra-A não fecha mede o próprio ruído.

Todos os eixos X batem com o Chart.js em intervalo, passo e número de ticks. Os
dez eixos Y que diferem, diferem pela decisão da banda, e o portão diz isso em
vez de acusar falha; o único caso sem banda ficou idêntico, que é o controle na
outra direção. Trinta trocas de gráfico deixam os ouvintes em 5 → 5 e retêm
0,36 KB cada, contra 3,91 do Chart.js — e o detector de vazamento foi ele próprio
verificado removendo o `_unbind()` do `destroy()`, o que leva a contagem a 93.

No ferro, entregue por OTA duas vezes: a página servida pelo aparelho deixou de
trazer `cdn.jsdelivr` e passou a trazer o renderizador, e um navegador apontado
para o aparelho com o DNS externo derrubado fez **33 requisições, nenhuma
externa, sem erro de JS**, e desenhou o gráfico.

### Flash

    folga do release ... 44 044 B -> 38 604 B

5 440 bytes, em troca de tirar um download externo de 70 592 B de toda visita à
página.

## v2.2.10-beta (2026-08-18)

### `false` ligava a configuração

Quatro campos da seção `sys` do `POST /api/commit_all` e dois da `net` liam seus
booleanos assim:

    cfg.telEncryption = (getNum("t_sec") != "0");

`getNum` extrai um *número*. Entregue a ele o booleano JSON legítimo `false` e a
comparação com a string `"0"` dá verdadeiro: o campo **liga**. Só o literal `0`
desligava alguma coisa, e não havia grafia booleana capaz de desligar nenhum dos
seis.

O que faz disso mais que uma curiosidade é de onde vem esse payload. O `GET
/api/config` emite `"log":false`, `"t_sec":false`, `"m_retain":false`,
`"ntp_enabled":false`; o `GET /api/network` emite `"use_dhcp":false` e
`"dns_auto":false`. **A saída do próprio aparelho era o payload que o parser lia
ao contrário.** Buscar a configuração, mudar um campo e devolvê-la — a coisa mais
comum que um script faz com uma API de configuração — ligava os seis. A interface
web nunca viu isso, porque os formulários dela emitem `1`/`0`.

O `t_sec` decide se a telemetria sai cifrada do aparelho, então um parser que
grava ali o oposto do pedido é achado de segurança, e não só de correção. O
`m_retain` ligado à revelia deixa a última medição parada no broker para qualquer
assinante que chegue depois.

### Três leitores, três respostas erradas diferentes

O arquivo carregava três maneiras distintas de ler um booleano de JSON, e elas
tinham divergido. Dezesseis campos, todos calados, todos sob `200 OK`:

    getNum/getN(k) != "0"   o literal `false` LIGAVA o campo
                            log, t_sec, m_retain, ntp_enabled,
                            use_dhcp, dns_auto
    startsWith("true")      o numérico `1` DESLIGAVA o slot
                            slot "a", slot "al"
    jsonBoolValue           1/0 caíam no valor guardado, sem mudar nada
                            alarms.active e os sete avisos sonoros

`{"a":1}` desativava o slot de sensor que pedia para ativar; o histórico segue
gravando aquele canal como NaN e nada no log diz por quê. `{"sounds":{"mute":1}}`
respondia 200 e não mudava nada.

### Um leitor só, e um terceiro estado

`jsonValuePos`, `jsonRawToken` e `jsonFlag` passam a morar no `WebJsonSlice.h`, o
cabeçalho que já existe para as varreduras manuais de JSON, sobre um
`parseBoolStrict` ao lado do `parseIntStrict`. As duas grafias são aceitas —
`true`/`false` e `1`/`0` — porque as duas estão em uso e nenhuma vai embora.

O terceiro estado é o que faltava. O `jsonFlag` separa *ausente* de *ilegível*, e
os dois são negativos, então todo chamador mantém o valor guardado nos dois
casos; só o ilegível é reportado, pelo array `"rejected":[...]` que os campos
numéricos já usavam. `{"log":2}` ligava o log sem dizer nada. Trocar pelo
`getBool` que já existia não resolveria: ele devolve 0 para `1` e quebraria a
página.

Unificar os leitores matou outros dois defeitos. A cópia do `net` nunca aprendeu
a pular o espaço que um payload formatado põe depois dos dois-pontos, então
`{"use_dhcp": 0}` produzia o token `" 0"` — aquele campo estava quebrado duas
vezes. E o leitor dos slots era o espelho exato do bug relatado, do outro lado.

### Provado contra um aparelho que ainda tinha o bug

O `tools/commit_bool_cases.py` escreve cada booleano em cada grafia que um
cliente real usa e o lê de volta pelo endpoint que o publica. Rodou como A/B na
mesma placa: **21/32 contra o firmware que ainda carregava o defeito, 32/32
depois**. O primeiro número é o que importa — uma suíte que passa nos dois lados
não mede nada. Os casos numéricos passam nos dois, que é o controle na outra
direção: a grafia da própria página nunca deixou de funcionar.

Onze casos entraram no `test/test_validators` (de 84 para 95), incluindo
transliterações dos três leitores removidos que afirmam a resposta errada que
cada um dava. Repor a semântica antiga dentro do `jsonFlag` derruba 6 dos 11 —
um teste que passou de primeira ainda não mostrou que sabe reprovar.

A cobertura no ferro é de 5 dos 16 campos; os demais ficam nos testes nativos. O
`use_dhcp:false` de propósito não é exercitado no hardware, porque commitá-lo
joga o aparelho no endereço estático.

### Flash

    folga do release ... 43 676 B -> 44 044 B
    folga do test ...... 46 916 B -> 47 284 B

A correção devolve 368 bytes: sessenta e duas linhas de varredura manual viram
cinco delegações e uma implementação compartilhada. Ela é barata porque é
subtração — escrita como um quarto leitor ao lado dos outros três, teria custado.

## v2.2.9-beta (2026-08-18)

### O build embarcava os próprios comentários

O `tools/build_webui_gz.py` minifica cada página antes de comprimir. O
minificador casava os literais de string com **um** regex, guardava o que casava,
tirava os comentários do resto e devolvia os literais ao lugar. Um regex não tem
contexto: para ele, toda aspa abre ou fecha uma string. Uma aspa que não é
delimitador — dentro de um comentário, ou dentro de um literal de regex como
`/[<>&"]/` — desloca o pareamento em um, e daquele byte em diante ele acredita
estar dentro de uma string quando está no código, e no código quando está dentro
de uma string.

Esse defeito já havia custado uma release na forma barulhenta, em que um trecho
de código é tomado pelo interior de uma string e **descartado**: a
`ALARMS_PAGE` saiu uma vez com 9% do tamanho do fonte, todos os handlers
perdidos, e o `node --check` não viu nada de errado — o que sobrou continuava
sendo JavaScript válido.

A forma silenciosa nunca tinha sido medida. Uma vez que o casador
dessincroniza, a região seguinte é tratada como um literal só, a minificação
nunca a alcança, e comentário e indentação viajam intactos para o firmware:

    LANG_JS ......... reteve 96,5% — 9.990 B só de comentário sobrevivente
    HIST_PAGE ....... reteve 93,2% — região protegida de 92.432 B, 84% da página
    ALARMS_PAGE ..... reteve 97,7%
    páginas sadias .. reteve 68-76%

A cura não é um regex melhor. O minificador agora é um escâner com estado: ele
sabe se está em HTML, num `<script>`, num `<style>`, numa string, num template,
num comentário ou num literal de regex, porque chegou ali passando por cada byte
anterior. Sem dependência nova — compilar a partir do zip da release precisa
funcionar com o Python da máquina e mais nada.

    12 assets, comprimidos ..... 103.418 B -> 83.861 B   (-19.557)

### Três portões, para a falha silenciosa não voltar

O modo de falha antigo passava por todas as checagens que o build tinha. Código
sumia, o que restava compilava, e o build imprimia SUCCESS.

O `_assert_only_whitespace_removed` reescaneia a **saída** e exige que os
literais, byte a byte, e o código sem espaço algum, sejam idênticos aos da
entrada. Nada além de espaço e comentário pode sair. Ele tem controle positivo
na suíte, provando que dispara com um símbolo renomeado, uma função apagada e
uma string alterada — um portão que nunca dispara é indistinguível de um que
funciona.

O `node --check` agora alcança a `LANG_JS`. É o maior bloco de JavaScript do
projeto e ficou fora desse portão a vida inteira, porque a busca só olhava dentro
de uma tag `<script>` e o `/lang.js` não é HTML.

A razão de retenção fica como o terceiro teste, o mais grosseiro. Ela também
ficou mais significativa: os números dos doze assets agora são consistentes
(56-97%) em vez de irem de 60% a 97%, e essa dispersão era o próprio sintoma.

O `tools/test_webui_minify.py` guarda 49 casos — cada armadilha em que o
minificador antigo caiu, mais duas que o escâner teve de aprender.

### O esqueleto repetido das páginas passou a ser pago uma vez

Oito páginas autenticadas carregavam cada uma a sua cópia da barra de topo e da
`initSession`, e cada página é comprimida sozinha, então os mesmos bytes eram
pagos oito vezes. Medido isolando cada bloco:

    initSession -> /lang.js .................... 3.766 B
    HTML da barra de topo e da trilha -> /lang.js  1.272 B
    4 regras de CSS -> /style.css .............. 1.109 B

O `/lang.js` e o `/style.css` são servidos com `Cache-Control: max-age=604800`,
então esses bytes também deixam de ser baixados a cada navegação.

A barra de topo é instalada durante a análise do documento, por um `<script>`
logo depois do `<body>`, e não no `DOMContentLoaded` como a gaveta: ela fica
acima da dobra, e instalada por evento apareceria depois do primeiro quadro,
empurrando a página para baixo na frente de quem lê.

### Corrigido no caminho

O minificador colapsava as linhas em branco dentro de `<pre>`, então o texto da
licença MIT na `/license` era exibido como um parágrafo corrido. `<pre>` e
`<textarea>` agora passam intactos, e a página ficou 43 B maior e correta.

A `/alarms` era a única das oito páginas cuja paleta não tinha
`color-scheme: dark`, e redefinia `--ok` para o valor que o `/style.css` já
trazia. Deriva, não intenção; agora usa a paleta compartilhada.

### Flash

    folga do release ... 18.740 B -> 43.676 B   (2,33x)
    folga do test ...... 22.044 B -> 46.916 B

Medido com `arm-none-eabi-size`, nos dois ambientes, antes e depois,
construindo cada árvore.

## v2.2.8-beta (2026-08-17)

### A interface web inteira voltou para dentro do firmware

Duas páginas — `/license` e `/alarms` — vinham do LittleFS desde agosto, o que
significava que o aparelho só as servia se alguém tivesse subido os arquivos
antes. Voltaram para dentro da imagem, junto com todas as outras. Uma unidade
gravada agora responde as doze rotas sem mais nada implantado, e o aviso "Page
asset missing" não pode sequer ser produzido: a função que o emite não está em
nenhum binário de produção.

As páginas saíram porque o `FS_PAGES` era uma constante **global** no
`tools/build_webui_gz.py`. Um layout de página para todos os ambientes. Quem
estava sem flash era o `pico_w_test`, que carrega a CLI serial completa — mas
uma página listada ali saía de todas as imagens, então o release pagava a conta
de um problema que não era dele. E pagava duas vezes: perdia as páginas e
ganhava um passo de implantação que nenhum pacote de release cumpria, o defeito
que a v2.2.6-beta teve que corrigir depois do fato.

Medido com `arm-none-eabi-size` — o percentual do PlatformIO omite a `.ota` e a
`.partition` — com os doze assets embutidos:

    release, 10 embutidas + 2 no LittleFS (antes) ..... 30 892 B livres
    release, as 12 embutidas ......................... 18 740 B livres
    test, as 12, sem alavanca ........................ estourou por 856 B
    test, as 12 + parseIntStrict ..................... 6 604 B livres
    test, as 12 + parseIntStrict + SIMUT_MDNS=0 ...... 21 980 B livres

O release nunca precisou da dieta. A imagem instrumentada terminou com quase o
dobro da folga que tinha, carregando mais páginas do que carregava.

### A dieta de páginas virou propriedade do ambiente

O `custom_fs_pages`, no `platformio.ini`, nomeia as páginas que um ambiente
serve do LittleFS. Declarar uma num ambiente de produção **falha o build**,
dizendo qual. Só as oito rotas que passam pelo `serveProtectedPage` são
elegíveis: `/login` e `/force_chpass` trancam o aparelho se o arquivo faltar, e
`/style.css` e `/lang.js` não têm rota de filesystem.

O gerador passou a emitir a rota junto do asset — uma macro `<PAGE>_SERVE`
ligada ao `serveProtectedPage` ou ao `serveProtectedFsPage` conforme o layout —
então uma página que troca de partição não exige mais edição no handler. Eram
dois lugares que podiam discordar em silêncio.

### Duas alavancas, ambas cobradas só da imagem de teste

O `SIMUT_MDNS=0` devolve **15 376 B**. O knob já tinha sido consertado antes e
nunca fora gasto; nenhuma suíte resolve `SIMUT.local`, elas acham a placa pelo
USB serial e falam com ela por IP. O release mantém o mDNS.

O último `sscanf` da árvore devolve **7 524 B**. Ele lia um argumento da CLI e
puxava a maquinaria inteira de scanf para isso; agora usa o `parseIntStrict` do
próprio projeto, que a suíte nativa já cobre. O handler vive dentro de
`#if SIMUT_CLI_FULL`, então o release nunca o linkou. O comportamento é o mesmo
para tudo que o tokenizador consegue produzir — lixo no fim, como `1,5x`, que o
`sscanf` aceitava, agora recebe a linha de uso.

### Também neste release

O atalho de "já está atualizado" do gerador lia a linha 1 enquanto o hash era
escrito na linha 2, então nunca casou uma vez sequer e todo build regerava o
header. Agora funciona, e o carimbo cobre a fonte, o gerador e o layout de
páginas — sem essa última parte, compilar dois ambientes em sequência entregaria
ao segundo o header do primeiro.

A `web_test_suite.py` passou a reprovar a rota que responde "Page asset
missing". Essa resposta volta como HTTP 200, com corpo longo o bastante para
passar numa checagem de tamanho, então nenhuma das asserções existentes a via.

Atualização: nada a implantar. Se o seu aparelho tiver `/web/alarms.html.gz` ou
`/web/license.html.gz` de uma versão anterior, agora são peso morto — apagá-los
pela página de Arquivos libera 12,8 KB da partição e é também a forma mais
limpa de provar que as páginas estão vindo do flash.

## v2.2.7-beta (2026-08-17)

### Uma tradução pode sumir sem que nada pareça quebrado

As duas packs de idioma foram auditadas entrada por entrada contra as fontes
que as definem: o enum `LangKey` para o `@DICT`, o `tools/logcodes.tsv` para o
`@LOGCODES`, todo literal `TRL()` sob `src/` para o `@TRL`, e todo consumidor
`data-i18n` / `t()` para o `@WEBDICT` — no bundle `WebUI.h` **e** nas duas
páginas que foram para o LittleFS, que leem o mesmo dicionário.

O `@TRL` é chaveado por um hash FNV-1a do texto em **inglês**. Editar esse
inglês órfã a tradução: a busca erra, a linha sai em inglês e nada é registrado.
Três entradas carregavam texto morto assim, e um literal vivo não tinha entrada
nenhuma.

Ao `@WEBDICT` faltavam cinco chaves que o navegador de fato pede, entre elas o
aviso de TLS sem validação de certificado e o diálogo inteiro de senha
temporária — texto de segurança que caía calado para o inglês. Outras dezoito
chaves não tinham consumidor em lugar algum do repositório e gastavam bytes
contra um teto do qual o `es-ES` está a 4%.

Conteúdo, não só cobertura: o `es-ES` dizia *Histórico*, *Reter*, *resetada* —
português vazando para dentro do espanhol, onde as mesmas packs usam
*Historial*, *Retener*, *restablecida* em outros pontos. *Fallo la subida* e
*Fallo la validación* pediam o verbo, *Falló*. Faltava o sinal de abertura nas
exclamações em espanhol. No `pt-BR`: *indisponivel*, *conexao*, *Pre-carga*, um
*Apply falhou* sem traduzir, e uma `Colisão de flash ocupada` cuja linha em
espanhol já trazia a preposição certa. O `@LICENSE` do `pt-BR` ficou meio
acentuado pelo gerador histórico, a ponto de trazer *E concedida* onde cabe o
verbo *É concedida* — invisível no TFT, que dobra aquela tela para ASCII, mas é
o texto que se distribui.

    language_pt-BR.lng   30.405 B -> 30.000 B
    language_es-ES.lng   31.770 B -> 31.382 B   (95% do teto de 32.768 B)

Quatro posições do `@DICT` parecem sem tradução no `es-ES` e foram deixadas de
propósito: nada desenha `TR_TEMP_MIN/MAX` nem `TR_HUM_MIN/MAX` desde que as
linhas de alarme passaram a usar `channelLabel() + MIN/MAX`. Elas ficam porque
o `@DICT` é posicional — remover uma linha desloca todas as chaves seguintes.

### O terminal de boot desenhava Latin-1 através de uma fonte CP437

A caixa de log do boot chama `setFont(NULL)` — a fonte embutida 5x7 do GFX —
mas imprime `tr()`, que devolve Latin-1 desde que as fontes do TFT passaram a
carregar acento. As duas não se encontram: no `glcdfont.c`, o byte `0xE7` é um
tau, `0xE9` um teta, `0xE3` um pi. **Dezoito linhas de boot nas duas packs**
saíam como símbolo matemático. A dobra para ASCII que a `drawSettingsLicense`
sempre aplicou para essa mesma fonte agora é aplicada aqui também.

A dobra também precisa vir antes do preenchimento. Um acento são dois bytes
entrando e um saindo, então medir o preenchimento pelo comprimento anterior à
dobra deixava a cauda de uma linha anterior mais longa na tela — um segundo
defeito, mais quieto, nas mesmas cinco linhas.

O `unaccent()` ganhou os sinais de abertura do espanhol de passagem. Eles caíam
no `'?'` padrão, o que transformava *¡Sistema Listo!* em *?Sistema Listo!*;
descartá-los lê certo, e o sinal de fechamento já diz o tipo da frase.

### O build agora vigia as seções que uma contagem de linhas não enxerga

O `check_lang_packs.py` contava linhas do `@DICT`, o que pega a falha que joga a
UI inteira para o inglês. Ele não via nada do `@TRL` nem do `@WEBDICT`, e essas
apodrecem uma string por vez — que foi exatamente como as lacunas acima se
acumularam. Uma entrada faltando agora quebra o build nomeando o `file:line` da
chamada `TRL()` ou o consumidor que pede a chave; uma órfã apenas avisa, já que
custa bytes mas não renderiza nada errado.

Atualizando: envie a `data/lang/language_pt-BR.lng` revisada (ou a `es-ES`) para
`/lang/` pela página de Arquivos ou por `POST /api/upload` com
`uploadDir=/lang`, e reinicie — uma pack só é lida no boot. Nunca use
`uploadfs`; ele reformata a partição.

## v2.2.6-beta (2026-08-16)

### O build de teste tinha 152 bytes, e nenhum zip levava as páginas dele

O trabalho de segurança da v2.2.5-beta consumiu os 3,5 KB que mover a página de
licença para o LittleFS tinha devolvido mais cedo no mesmo dia. A folga real do
`pico_w_test` caiu para **152 bytes** — o próximo teste escrito para aquele
ambiente não linkaria, e o sintoma é um `overflowed by N bytes` que **não muda**
quando se encolhe um asset, porque a `.rodata` é alinhada em página e absorve
variações pequenas em degraus. O percentual que o PlatformIO imprime diz 98,8%
e esconde tudo isso: ele deixa de fora a seção `.ota` e o padding de
alinhamento.

A página de alarmes (8507 B comprimidos) passa para o sistema de arquivos. É a
única página grande que passa nas três partes do critério — grande, raramente
aberta, e não necessária para levantar o aparelho. Uma unidade sem o arquivo
continua amostrando, registrando e disparando os alarmes que já tem; ela apenas
não permite editá-los pela web. A página de histórico é quatro vezes maior mas
é a mais quente que existe, a de configuração quebrou o bootstrap quando isso
foi tentado em julho, e a de arquivos é circular, já que é por ela que estes
próprios arquivos são enviados.

    folga real   pico_w_test   152 B -> 8 640 B
                 release    19 740 B -> 28 228 B

**Um bug de deploy foi publicado no release anterior, e isto o encontrou.**
`data/web/` estava no gitignore e nenhum script de release o copiava, então
nenhum zip jamais levou o `license.html.gz` — enquanto o comentário do
`build_webui_gz.py` afirmava que todos levavam. Compilada a partir do zip pio
da v2.2.5-beta, a `/license` responde "Page asset missing". A regra de ignore
agora exclui o conteúdo e nega as páginas, como o `data/themes` já fazia — o
git não desce em diretório excluído, então uma negação dentro de um nunca casa
— e o zip pio leva `data/web/*.html.gz`. Os zips Arduino não levam `data/` por
projeto.

Ao atualizar: suba `data/web/alarms.html.gz` para `/web/` junto com o
`license.html.gz`, pela página de Arquivos ou por `POST /api/upload`. Nunca
`uploadfs` — ele reformata a partição.

## v2.2.5-beta (2026-08-16)

### Toda permissão é conferida onde o payload é lido

Uma auditoria contra `docs/diretrizes_seguranca_vibecoding.md` achou um caminho
de duas requisições, sem exploit nenhum, de uma conta de baixo privilégio até
admin pleno. O `/api/commit_all` exigia `PERM_SYS_CONFIG` uma vez e depois
processava as seções que o corpo trouxesse — `users.actions` e `net` entre
elas — então quem podia editar um limiar também podia criar um administrador.
Na bancada, uma conta `sectest` com `perms=8` criou um admin com `perms=1023`
e recebeu HTTP 200 por isso.

A autorização passou a ser por seção: o `WebCommitSections.h` mapeia cada seção
ao bit que ela exige, e cada parser lê o offset que o portão anotou em vez de
procurar de novo no corpo, de modo que portão e parser não podem divergir. A
varredura é **flat** de propósito — `indexOf` no corpo inteiro, o mesmo formato
que o parser usa — porque um portão ciente de aninhamento na frente de um
parser que não é deixaria `{"sys":{"users":{...}}}` passar direto. Mesma conta,
mesma requisição: o 200 virou **403 section=users** e **403 section=net**,
enquanto a própria seção `sys` segue devolvendo 200.

A outra metade do caminho era a senha temporária. Conta nova nascia `*PENDING*`
e o `getDynamicExpectedHash` fazia a senha ser `Nome@DDMMYYYY` — pública por
construção. Agora a conta recebe 8 caracteres aleatórios de um alfabeto sem
ambiguidade (~40 bits), com salt aleatório e `mustChangePassword`, devolvidos
uma única vez na resposta do `commit_all` para o admin repassar. O ramo
`*PENDING*` do login sumiu, então contas deixadas por builds antigos
simplesmente não logam mais e precisam de reset — que é o resultado desejado,
não uma regressão.

Mais dois achados da mesma auditoria: o `/download` servia `/config/*` a
qualquer conta com `PERM_FILE_READ`, entregando o `system.bin` — agora recusa
caminhos secretos pelo `FsSecretPath.h`, e o `/api/backup` exige
`PERM_FULL_ADMIN` para fazer par com o `/api/restore`. E o `/api/mkdir`
filtrava `..` apagando a substring, o que não é filtro; nomes de diretório
passam por allowlist, enquanto o gerenciador de arquivos e a lista de usuários
escapam o que renderizam e navegam por um handler delegado `data-nav` em vez de
interpolar nomes dentro de `onclick`.

### A UI web fala TLS, e o servidor julga a senha

Com `/config/web_cert.pem` e a chave presentes o servidor web sobe em HTTPS e o
flag `Secure` do cookie de sessão finalmente casa com o transporte real; sem
eles segue em HTTP, para nunca trancar o operador do lado de fora. É
release-only — o lado servidor do BearSSL custa uns 20 KB e o build de teste não
tem espaço. Sobre TLS o navegador manda a própria senha e o servidor aplica a
política (8 caracteres, letra e dígito) antes de gerar o hash, fechando a
brecha em que um cliente que pulasse o JavaScript podia definir qualquer coisa.
O formato do hash armazenado não mudou.

### A telemetria admite quando não está autenticando nada

Um `/cert.pem` ausente fazia o cliente TLS cair para `setInsecure()` — ainda
cifrado, sem autenticar ninguém — atrás de um aviso de mensagem vazia que lia
como erro de arquivo. Agora registra `TEL_CERT_READ_ERR ctx=1` uma vez por boot
dizendo que MITM é possível, expõe `t_cert` no `/api/config`, e a página de
configuração ganha um selo quando o transporte é seguro mas não verificado.

O cliente MQTTS também nunca recebeu o teto de buffer que o caminho HTTPS ganhou
lá na v1.5.3-beta. O BearSSL pede 16 KB contíguos; a primeira conexão levava
esse bloco e toda reconexão seguinte encontrava 9,5 KB de maior bloco livre.
Medido com o mesmo backlog e o TLS como única variável, o MQTTS congelou em
39.234 registros pendentes com `telSent` preso em 1, enquanto o MQTT puro
drenou até `telSent` 72. O broker completou todos os handshakes que viu — nunca
foi culpa dele.

### Os segredos saem do repositório, e um portão os mantém fora

Um backup do dispositivo carregando a senha real do Wi-Fi estava commitado e
público desde que o kit de bancada entrou. A senha foi rotacionada, o arquivo e
a senha foram expurgados do histórico, e os scripts de bancada leem
`SIMUT_WIFI_SSID` / `SIMUT_WIFI_PASS` do ambiente. A árvore `scratchpad/`
inteira — 1267 arquivos, incluindo uma chave privada TLS — saiu do índice; o
`.gitignore` nunca desrastreou o que já tinha sido commitado.

O `tools/scan_secrets.sh` agora roda antes de qualquer script de release
empacotar coisa alguma, e recusa as duas formas do vazamento: o tipo de arquivo
e o literal no código. Ele também audita os zips prontos, porque eles são
montados a partir da working tree — um segredo não rastreado em `tools/`
entraria num pacote que uma checagem só do git nunca olha. Credenciais
publicadas conscientemente ficam em `tools/.secretscan-allow`.

### O log registra transições, não batimentos

Eventos de rotina são gravados quando o estado realmente muda, em vez de a cada
passagem, então silêncio no log passa a significar "nada mudou" e não "nada foi
escrito". Ler o log é ler transições: o ritmo dos registros de rotina não é mais
a taxa de amostragem.

## v2.2.4-beta (2026-08-16)

### O snapshot diz qual relógio o carimbou

O teto da semente limitava o `.wip` a um vão de bloco além do dia a que ele
pertencia, e esse dia vinha do nome do arquivo mais novo **selado**. Só que um
`.wip` é, por definição, mais novo que tudo o que está selado — os dois só
coincidem depois que o dia corrente selou algum bloco. Antes disso a janela é a
de ontem (ou mais velha, se o equipamento ficou desligado) e um snapshot
perfeitamente bom era recusado:

- ligado às 03:00 depois de uma noite desligado e reiniciado antes de selar os
  primeiros 60 registros: o `.wip` marca 03:00 e a janela para em 01:00;
- o bloco que atravessou a meia-noite começa em ontem, então, assim que o
  arquivo de hoje existe, ele falha no piso da janela.

Nenhum dos dois é distinguível, só pelo valor do `t0`, do snapshot de
14/08/2026 que de fato estava carimbado no futuro — o que os separa não é o
valor do `t0` e sim a sua procedência, que só é conhecida enquanto o snapshot
está sendo escrito. Então passa a ser registrada ali. O
`H5_FLAG_CLOCK_SYNCED` (bit 2 das flags do chunk, até agora livre) marca o
bloco cujos carimbos vieram do tempo real, e não do relógio provisório. A
semeadura dispensa a janela do dia para um snapshot que o carregue, e aplica o
gate antigo, inalterado, para um que não carregue.

O bit está dentro da primeira faixa coberta pelo CRC, então não dá para forjá-lo
num bloco sem invalidá-lo. O armazenamento descobre a resposta por callback, em
vez de alcançar a rede, e um callback não instalado lê como "provisório" — uma
build que esqueça de ligá-lo recebe o gate estrito, não passe livre. Leitores
ignoram bits de flags que não conhecem, então o `H5_VERSION` não muda.

Um limite foi na direção oposta e saiu: a varredura de registros passou
brevemente a recusar qualquer coisa além de `t0 + 60 × intervalo`. Blocos fecham
por **contagem**, não por relógio, e um que atravessou uma queda de sensor cobre
muito mais tempo do que a contagem sugere — o limite descartaria dado bom, e o
payload já está dentro do CRC.

Medido na bancada com o arquivo do dia removido (para a janela ficar velha),
reset duro e um `.wip` carimbado 7 h 47 min além do teto antigo: com o bit, a
semente é aceita e o NTP corrige −40 s em INFO; com o bit limpo e o CRC
refeito, a semente é recusada e o NTP corrige 31757 s em WARN. Mesmo carimbo,
mesmo payload, mesma janela — só a procedência muda.

## v2.2.3-beta (2026-08-16)

### O boot sabe o que é um dia antes de perguntar

O gráfico ficou vazio entre 00:00 e 00:46 no dia 16 de agosto. As 46 medições
nunca se perderam: estavam em `20260816.h5` carimbadas `04:19:07`, exatamente
15546 s adiante de onde pertenciam — o tamanho de uma correção de NTP que foi
aplicada sobre dados que já estavam certos.

`getLastRecordedTimestamp( )` converte o nome do arquivo mais novo numa janela
de dia com `mktime( )` e julga carimbos locais contra ela. `mktime( )` lê o
fuso do processo, e o único `applyTimezone( )` do caminho de boot ficava dentro
de `NetworkManager::begin( )`, cerca de 130 linhas depois da semeadura. A
janela era montada em UTC contra dados em −03 e saía três horas adiantada.
Tudo o que foi gravado depois das 21:00 caía fora do próprio dia: quatro blocos
da noite foram descartados da semente, o `.wip` que atravessara a meia-noite
passou do `h5SeedCeiling`, e o relógio provisório começou 4 h 20 min atrasado.
O NTP então mediu a diferença e o `shiftHistoryTimeV5` levou o bloco da
meia-noite junto com a correção.

O fuso passa a ser aplicado antes da semeadura, e essa é a correção inteira.
Qualquer reboot entre 21:00 e a meia-noite estava exposto a isso, não apenas um
na virada do dia.

Duas mudanças acompanham. A conversão nome→janela passa para
`h5DayWindowFromName( )`, ao lado dos portões que a consomem — a suíte vinha
recebendo `dayStart`/`dayEnd` como constantes, exercitando os portões e nunca o
passo que os deriva, que era justamente o passo errado. E o bloco que um boot
herda da sessão anterior passa a ser nomeado explicitamente: o
`shiftHistoryTimeV5` se limita pela base provisória, que só vale enquanto valer
a semente que a produziu, então `_h5AdoptedT0` o exclui sem consultar um
relógio sob suspeita.

Medido na bancada com o mesmo `.wip` e reset duro nas duas imagens: a antiga
recusou a semente, alertou em 18545 s e reescreveu o `t0` do bloco adotado de
`00:49:59` para `05:59:04`; esta aceita a semente, corrige 2047 s em INFO e não
move bloco nenhum.

## v2.2.2-beta (2026-08-15)

### O bloco que atravessa a meia-noite volta a ser lido

Um bloco é arquivado sob o dia do seu **primeiro** registro, então um bloco
ainda aberto às 00:00 segue recolhendo dados no arquivo do dia anterior. As
duas varreduras do histórico — o remetente de telemetria e o contador de
pendentes — cortavam a lista de arquivos no dia do cursor, de modo que no
instante em que o cursor cruzava a meia-noite aquele arquivo deixava de ser
aberto. Tudo que o bloco recolheu depois das 00:00 ficava preso: não sumiu do
flash, não é mais velho que o cursor, apenas está num arquivo que ninguém
reabre.

Pior que a lacuna nos dados, o remetente **travava** ali. Sem nada a ler nos
arquivos que ainda se dispunha a abrir, o cursor parava de avançar.

O piso agora recua um vão de bloco atrás do cursor, em vez de cortar no dia
dele (`h5ScanFloor`, ao lado do `h5SeedCeiling`, que limita a mesma coisa pelo
outro lado). Um bloco não alcança mais adiante do próprio início do que os
registros que cabe nele, então o arquivo de ontem permanece no escopo por
exatamente o tempo em que pode importar — uma hora no intervalo padrão, e nem
um minuto do resto do dia. O contador de pendentes recebe o mesmo piso, porque
uma contagem que concorda com o bug o esconde em vez de mostrá-lo.

Medido em hardware com um bloco atravessando a fronteira e carregando 32
registros depois dela, com lote pequeno o bastante para o cursor cruzar
enquanto o bloco ainda está meio lido:

| | antes | depois |
|---|---|---|
| registros pós-meia-noite entregues | **0 de 32** | **32 de 32** |
| registros drenados antes de travar | 10 | 829 |

Encontrado pela validação em hardware escrita para o trabalho do relógio da
v2.2.1-beta, que é o argumento para tê-la escrito.

## v2.2.1-beta (2026-08-15)

### O buraco do histórico que nunca foi um buraco

Um aparelho registrou normalmente pela noite de 14 de agosto e o gráfico não
mostrou nada entre 21:32 e 02:15. As medições estavam no flash o tempo todo,
carimbadas 4 h 43 min no futuro, e parte da lacuna era o leitor jogando dado
bom fora por cima disso. As duas metades foram corrigidas.

**A semente do relógio.** Antes do NTP chegar, o relógio é semeado pelo
snapshot do bloco aberto em `/history/.wip`, e esse snapshot era acreditado
duas vezes: o `t0` era lido direto do cabeçalho, antes do CRC que o certifica,
e qualquer época até um dia inteiro além do dia do arquivo era aceita. Tudo que
é escrito antes da sincronização herda esse valor. A janela agora é de um vão
de bloco após a meia-noite — derivada do intervalo de amostragem, já que blocos
fecham por contagem e não por relógio, ou seja uma hora no intervalo padrão em
vez de um dia — e a semente só se move depois que o decodificador validou o
bloco. Um `t0` forjado agora falha no CRC em vez de chegar ao relógio.

Medido na mesma placa que teve o incidente, plantando um snapshot que a regra
antiga aceitava: sem a correção o NTP do boot satura o campo em 18,5 horas; com
ela, 77 segundos.

**Correção de NTP maior que uma hora agora sai como WARN.** Ela continua sendo
aplicada — recusá-la deixaria os carimbos errados para sempre depois de uma
queda longa — mas deixa de ser invisível, e é o número que mede o quanto o
relógio havia derivado enquanto os registros eram escritos.

**O leitor do gráfico.** A montagem das séries descartava qualquer registro que
não fosse mais novo que o máximo corrente. O guarda existia para remover
duplicatas, mas não distingue duplicata de registro fora de ordem, e ordem de
arquivo é ordem de escrita. Um bloco carimbado à frente escondia todos os
blocos atrás dele: 65 de 205 registros naquele arquivo, incluindo 31 cujos
carimbos estavam corretos desde sempre. As séries agora são ordenadas e
deduplicadas por instante após a montagem.

**Varredura de blocos.** O `HistoryV5Scan::seek` presumia ordem temporal, que é
uma afirmação sobre o relógio de quem escreveu, não uma propriedade do formato.
Fora de ordem, mais de um bloco atravessa o corte e só o último era guardado,
pulando registros que estavam no flash. Agora ele confere a premissa durante a
varredura de cabeçalhos que já fazia, e recusa pular qualquer coisa num arquivo
que falhe nela. Arquivos ordenados recebem a mesma resposta e o mesmo caminho
rápido de antes.

**Cursor de telemetria.** O cursor avançava para o último elemento do lote,
documentado como marca d'água — verdade apenas enquanto os registros sobem. Um
bloco carimbado horas à frente enterrava todos os registros corretos atrás
dele, permanentemente e em silêncio. O cursor agora é o máximo sobre o que o
transporte de fato levou, limitado ao presente.

### Alarme, atenção e seleção passam a ser temáveis

A paleta descrevia 17 papéis e a tela desenhava mais. Preenchimentos de alarme,
o botão de silenciar, o fundo de seleção de slot e os carimbos de data do
gráfico eram fixos no código, então um tema personalizado podia reestilizar
tudo que o usuário olha e ainda piscar um painel vermelho de fábrica por cima.
Sete papéis fecham a lacuna, levando a paleta a 24; arquivos com apenas os 17
antigos continuam carregando, com as chaves ausentes caindo no valor de
fábrica. Os brilhos dos ícones agora derivam da cor de baixo em vez de um azul
claro fixo.

Onze paletas prontas acompanham em `data/themes/`, cada uma auditada por
contraste contra os fundos em que de fato é desenhada.

### Conhecido, e deliberadamente não declarado resolvido

O teto da semente reduz o raio de estrago de um dia para uma hora; não torna
uma semente ruim impossível, e um carimbo a menos de um vão de bloco da
meia-noite continua indistinguível de um bloco que a atravessou legitimamente.

A limitação do cursor de telemetria cobre o carimbo que está no futuro no
instante do envio, que é a falha real de campo. Um carimbo à frente dos
vizinhos mas já no passado ainda avança o cursor por cima de registros antigos
não enviados — fechar isso exige o cursor virar posição de varredura em vez de
instante.

A validação em hardware revelou um defeito separado, que não é tratado aqui: um
bloco que atravessa a meia-noite vive no arquivo do dia anterior, e assim que o
cursor de telemetria cruza 00:00 a seleção de arquivos deixa de olhar para trás,
de modo que esse bloco nunca é enviado.

## v2.2.0-beta (2026-08-15)

### Reforma visual da interface web

Os dois temas redesenhados com contraste WCAG medido, verificados no
hardware real com uma varredura de 36 capturas (9 páginas × claro/escuro ×
1366 px/390 px) antes e depois. Custo total de flash: +860 B de UI gzip.

**Tema escuro** — as superfícies finalmente se separam: fundo azulado frio
(`#0c0f13`), cartões (`#161b22`) com borda que cumpre o papel (1,36:1
contra o cartão, ante 1,19:1), inputs rasos (`--bg`) em vez de poços de
preto puro, e topbar distinta da página.

**Tema claro** — cartões brancos sobre papel frio, e o accent vira
`#0072CD`: o mesmo matiz SIMUT, mas com 4,9:1 sobre branco (o `#0096FF`
anterior media 3,1:1 e reprovava no WCAG AA em links e botões). Corrigidas
cinco sobras do tema escuro que nunca ganharam estilo claro:

- A grade do gráfico era quase preta hard-coded (13,8:1 sobre branco — o
  elemento mais forte da tela) e os rótulos dos eixos sumiam a 2,4:1;
  ambos agora leem os tokens do tema (`--border`/`--sub`) no render.
- Toggles LIGADOS perdiam o accent por conflito de especificidade —
  ligado e desligado ficavam idênticos.
- O campo de busca do log de eventos mantinha fundo preto puro dentro de
  cartão claro.
- Os trilhos das barras de RAM/armazenamento continuavam escuros
  (`#3f3f46`).
- Verdes/âmbares de status mediam ~2:1 em cartão claro; os novos tokens
  `--ok`/`--warn` usam tons grau-700 (≥4,9:1).

**Login e troca forçada de senha** agora seguem o tema salvo — são páginas
pré-sessão que nunca carregavam o motor de tema, então quem usava o claro
entrava por uma tela preta antes de um app branco.

**Gráficos de histórico** — a paleta de 11 cores de temperatura tinha
quatro vermelhos quase idênticos e dois amarelos que sumiam no branco;
agora são 6 tons quentes distintos que seguram ≥3:1 nos dois fundos. A
pressão troca o lilás só-de-escuro por um violeta que funciona nos dois.
Grade e rótulos acompanham o tema ativo.

**Consistência** — nomes de sensor com a mesma cor de texto em toda parte
(eram cinza no painel, ciano nos alarmes); o colorido âmbar/verde da
coluna TIPO do painel — que referenciava tokens inexistentes — agora
renderiza de fato; filtros INF/WRN/ERR do log cabem numa linha no celular;
estilo compartilhado `.b-pri` para botão primário; contorno global
`:focus-visible` para navegação por teclado.

## v2.1.10 (2026-08-14)

### A linha 2.1 fica estável

O mesmo firmware publicado como v2.1.10-beta — os únicos bytes que mudam
são a string de versão. Onze betas entre 10/08/2026 e 14/08/2026 levaram
a linha 2.1 da primeira varredura de reboots via web até um display que
sobrevive ao próprio offset de alinhamento; cada uma passou pelos mesmos
quatro portões de release antes de publicar (os dois builds de firmware,
34/34 testes nativos de histórico, paridade do codec com 20.000 casos,
selftest de 200.000 sorteios) e foi gravada e verificada em hardware
real. Este release promove esse firmware a estável e passa a ser a
imagem recomendada.

### Revisão geral da documentação

- README reescrito nos três idiomas, com screenshots novos da interface
  2.1.10 — o conjunto anterior era anterior à reforma visual da v2.1.5.
- Manual do usuário (pt-BR) recapturado e revisado para a 2.1.10: o
  teclado de senha para a ponta do dedo, os gráficos de histórico em
  baldes, a série de pressão e o editor de alinhamento de tela estão
  documentados com telas reais.
- Manual em inglês reescrito para o firmware atual (ainda descrevia a
  v1.6.3).
- Guia de fiação conferido contra o mapa de pinos do código.
- Raiz do repositório organizada: análises de engenharia movidas para
  `docs/`.

## v2.1.10-beta (2026-08-14)

### O offset de alinhamento não corta mais a tela

O SIMUT permite deslocar a imagem inteira em até 4 pixels por eixo
(Config → Alinhamento da Tela) para compensar painéis cuja janela
visível fica levemente fora da matriz de pixels. Mas vários elementos
eram desenhados a menos de 4 px de uma borda, então os ajustes extremos
raspavam pixels deles — e um, a linha fina de "trabalhando…" pintada
enquanto um gráfico carrega, morava inteira nas 3 primeiras linhas:
um offset vertical de −4 removia o único feedback de que o toque
tinha sido aceito.

Todos os renderizadores foram varridos contra uma regra só: conteúdo
vive dentro de x 4..315, y 4..235 — exatamente o retângulo que a
moldura verde da tela de alinhamento desenha. O que se moveu: o hint
de ocupado do gráfico (agora sobre o cabeçalho), os botões de período
do gráfico (iam de 2..317, agora 4..315), os rótulos do eixo Y, os
rótulos de hPa do eixo direito, o marcador de último valor e o
cabeçalho full-bleed do gráfico, a marca "SIMUT" do dashboard e o
banner de web ocupado (virou um chip recuado que trunca usuários
longos por medida em vez de cortar no meio do glifo), a grade do
teclado de senha (chegava a x=317 e y=237; as teclas agora têm 74 px
e a última fileira termina em y=235), a barra de título do status do
sistema e o número da sensibilidade do toque.

Onde uma tela e seu handler de toque mantinham cópias separadas da
mesma geometria, os números foram promovidos a constantes
compartilhadas — `GRAPH_PBTN_*` para o rodapé do gráfico, ao lado do
header compartilhado que o teclado já tinha — para que os botões
desenhados e suas zonas de toque não possam mais divergir.

Validado no hardware: 19 capturas do framebuffer cobrindo todas as
telas, com a borda de 4 px verificada 100 % fundo em todas, e o offset
exercitado ao vivo até os extremos e restaurado.

## v2.1.9-beta (2026-08-14)

### Um teclado de senha para a ponta do dedo

A tela de troca de senha exigia cirurgia: teclas de 30 pixels — 5,4 mm no
painel de 2,8" — três camadas escondidas atrás de Shift e 123, e uma
fileira de setas embaixo como contorno oficial, cerca de cinco toques para
acertar um caractere. O teclado novo são oito teclas de grupo de 76×54 px
(13,7×9,7 mm): toque em `pqrs` e abre um popup com `p q r s` sobre
`P Q R S` — as duas caixas de uma vez, 68×56 px cada — e toque na que
queria. Sem Shift, sem camadas, sem cursor para pilotar. `123` e `@#!`
abrem o mesmo tipo de popup para os dez dígitos e os 28 símbolos; espaço e
backspace agem direto; o OK fica ao lado das caixinhas; tocar fora do
popup cancela. Cada caractere do mesmo conjunto de 91 agora custa
exatamente dois toques em alvos do tamanho de um dedo, e o fluxo
digitar-e-confirmar de 4–7 caracteres, suas mensagens e o alternar de
máscara continuam os mesmos.

Um único header passa a ser dono da geometria e das tabelas de caracteres
para o renderizador E o mapeador de toque — a tela antiga mantinha três
cópias das tabelas de camada sincronizadas na mão — e a composição usa o
renderizador de tela cheia em 6 strips no lugar de cinco blits parciais
posicionados um a um. A reescrita devolve ~2,5 KB de flash: a imagem
release ficou menor, e o ambiente `pico_w_test`, que estava a 224 bytes do
teto, volta a linkar com 2,7 KB de folga real.

### OTA revalidado na imagem nova

Dois ciclos stage+apply na bancada com este firmware, veredito lido de
volta como a string de versão (nunca inferido de tempo ou código HTTP):
1.001.964 B em stage de 30,7 s cada, apply aceito na primeira tentativa
nas duas vezes, CRCs distintos por imagem. O 503 "Display in use" visto em
13/08 não reapareceu. Os ciclos rodaram na :8080, contornando a injeção de
RST na porta 80 do roteador da bancada documentada na v2.1.7-beta.

## v2.1.8-beta (2026-08-13)

### Os gráficos web de histórico passam a ler o próprio arquivo

Os `.h5` sempre estiveram completos; o gráfico é que não estava. A página
pedia ao `/api/history_multi` um JSON já encolhido, e o encolhimento mentia
duas vezes: o caminho decode emitia um registro a cada N (pico sobrevivia por
sorte), e acima de um certo tamanho o caminho de envelope emitia o mínimo do
bloco em t0 e o máximo em t0+30 min **na mesma série** — desenhado como
linha, isso é uma serra que o sensor nunca produziu, e um degelo único da
geladeira vira dois picos com um vale no meio. Pior: o limiar era estimado
pelos bytes dos arquivos varridos, não pela janela pedida, então um 1 h
ancorado no passado chegava com **3 pontos** (6 h: 13; 24 h: 51) e o
comportamento mudava com a hora do dia.

A página agora baixa os próprios arquivos diários por `/download` — a mesma
estrada que o export CSV já usava — decodifica no navegador com o `h5Decode`
que ela já tinha, e reduz para a tela com baldes por coluna de pixel
guardando **mínimo, máximo e média**: uma banda atrás da linha média. Pico de
1 minuto sobrevive a qualquer janela porque o extremo É a aresta do balde;
balde vazio é null que o gráfico desenha como lacuna real; amostra sozinha
entre duas lacunas ganha um ponto visível; e o registro mais novo sempre sai
com o próprio carimbo. Dia fechado nunca muda, então o cache por (nome,
tamanho) faz troca de faixa e de sensor não baixar nada; o dia corrente e a
hora aberta (`/api/history/open`) são sempre rebuscados, a cauda por último —
uma selagem no meio da carga custa no máximo uma lacuna, nunca duplicata. Os
badges de extremos saem de todos os registros da janela na mesma passada, e o
export CSV reusa o cache de bytes em vez de baixar de novo.

Medido na bancada contra 64 dias de dado sintético com gabarito: 1 h 3→60
pontos, 6 h 13→360, 24 h 51→1 398 (resolução plena), 7 d 339→885, e o dia em
que o aparelho passou 6,5 h desligado finalmente mostra um buraco em vez de
uma ponte. Robustez de brinde: cada arquivo é uma requisição curta, imune ao
RST que o roteador injetava na resposta única de 500 KB. O lado firmware do
`/api/history_multi` está intocado e continua servindo ferramentas.

### Os gráficos do TFT ganham baldes de tempo e um envelope honesto

Mesma doença, renderer nativo: decimação por stride fixa por faixa (1 em 51
no 7 dias) calibrada para cadência de 1/min, X espaçado por índice em vez de
tempo, e um eixo Y na escala dos extremos REAIS sobre uma curva que os tinha
perdido — o eixo anunciava −6,5 °C que a linha nunca alcançava, e degelos
idênticos da geladeira saíam com alturas aleatórias, vários sumindo por
completo. Um apagão de 6,5 h comprimia num passo invisível de índice, e no
7 dias cheio o teto de 200 pontos cortava em silêncio a cauda da hora aberta,
deixando a borda direita velha.

O loader agora agrega em baldes uniformes no TEMPO
(`clamp(janela/intervalo, 40, 200)`), cada um com mín/máx/média — o que torna
o X por índice do renderer proporcional ao tempo de graça, transforma balde
vazio em lacuna com a largura verdadeira e nunca estoura o teto. O renderer
pinta a banda mín/máx atrás da linha média de 2 px (substituindo o
preenchimento até a base), dá um ponto 3×3 a balde solitário, e senta os
marcadores de pico na aresta da banda do balde do extremo real — marcador,
rótulo do eixo e badge finalmente concordam. As estatísticas do detalhe
(MÉDIA/DESVIO/Δ e o n=) agora saem de todos os registros da janela: o n= de
um 24 h foi de 180 para 1 435. Custo: ~13 KB de RAM estática (41,6% → 47,0%)
e menos de 1 KB de flash.

### Notas de bancada e build

O `pico_w_test` vivia a 224 bytes do teto de flash e o JS novo o estourou; o
env agora compila com `-DNDEBUG` (a alavanca documentada de ~6,6 KB) e
`-DSIMUT_LICENSE_STUB` (a tela de licença mostra um ponteiro curto; a imagem
de release sempre carrega o texto MIT completo). A dieta de verdade — migrar
páginas para a LittleFS via FS_PAGES — fica como trabalho futuro. Limitação
conhecida, pré-existente: a página de gráficos carrega o Chart.js de um CDN,
então o gráfico no navegador precisa de internet mesmo com todo dado vindo do
aparelho.


## v2.1.7-beta (2026-08-13)

### A pressão entra nos gráficos de histórico

O leitor do gráfico só resolvia temperatura e umidade, então o único sensor
que existe para medir pressão (BMP280) plotava temperatura sozinha — e a
pressão não tinha tela nenhuma no TFT. O canal de pressão agora é lido dos
arquivos diários e da hora ainda aberta na RAM: numa peça com pressão e sem
umidade ele assume a segunda curva do gráfico e o eixo direito (hPa, uma
casa, na mesma cor que a pressão veste no dashboard); num BME280 — onde a
umidade fica com a curva — ele ganha a própria página de métricas. O toque no
centro da tela de detalhe circula temperatura → umidade → pressão → volta ao
gráfico.

### A tela de métricas vira tabela de instrumento

Os quatro cartões MAX/MIN/MÉDIA/DESVIO deram lugar a linhas de instrumento de
largura inteira sob uma faixa de seção que nomeia o canal e a unidade —
"Pressão (hPa)" — e mostra os page dots das páginas do ciclo de toque. Cada
linha carrega um ícone com cor semântica (MAX quente, MIN frio), o valor num
eixo decimal único, e uma coluna à direita com o **carimbo completo
dd/mm/aa hh:mm** do extremo, a **variação da janela** com setinha de
tendência na linha da MÉDIA, e a contagem de amostras na do DESVIO. O layout
é medido em tempo de execução a partir dos glifos reais, então qualquer
idioma ou unidade mantém as folgas; o único rótulo que não coube, o inglês
"AVERAGE", virou "AVG".

Três consertos foram de carona: os rótulos do detalhe não corrompem mais com
um `.lng` carregado (guardavam ponteiros do scratch rotativo de 4 slots do
`tr()` e as chamadas de unidade da página de umidade os reciclavam no meio do
render — o inglês nunca mostrava); o mínimo do eixo secundário não trava mais
no sentinela 1000.0 para pressão (o nível do mar fica acima dele, e a curva
saía esmagada no topo); e o intervalo do cabeçalho do gráfico agora veste a
cor do relógio do dashboard e carrega o ano de dois dígitos.

### Uploads longos param de morrer no primeiro soluço

O leitor multipart do servidor web mantinha o padrão do Stream: um segundo de
paciência por byte. Com a janela de recepção em 4×MSS (o fix de lwIP da
v2.1.4), um upload de ~1 MB fecha a janela várias vezes por segundo; quando o
segmento que a reabre se perde num apagão de rádio, o fluxo trava até o peer
retransmitir — e um segundo transformava esse trave recuperável num upload
abortado aos ~13–15 s, todas as vezes. O leitor agora espera 3 s, e o ciclo
de OTA stage→apply foi revalidado de ponta a ponta duas vezes na bancada,
com a versão lida de volta pelo console serial em cada uma.

Na caça a isso, um segundo assassino — ambiental — foi isolado e vale
registro: APs domésticos com "proteção anti-flood" podem injetar RST em
fluxos porta-80 sustentados rumo à estação numa idade fixa de conexão,
independente da vazão — ICMP intacto, contadores do dispositivo limpos. Se
transferências grandes morrem num ~13 s suspeitamente constante na sua rede,
experimente tirar a web do SIMUT da porta 80 ou relaxar a proteção DoS do
roteador.

## v2.1.6-beta (2026-08-12)

### As telas param de carregar de cima para baixo

A 2.1.5 trouxe o blit por DMA, mas ele só disparava em envios com exatamente
320 px de largura. Os cards do dashboard (312 px), toda linha de menu (285 px)
e o chrome inteiro das telas ainda saíam pelo caminho de ~2 µs/pixel da
biblioteca — e dez telas abriam com um `fillScreen` por-pixel de ~150 ms. Essa
combinação era o que aparecia como a tela "carregando de cima para baixo".
Esta versão termina o serviço:

- **Blits de sub-largura também vão pelo DMA.** Fatias mais estreitas que o
  canvas são compactadas no lugar e enviadas num burst só. Um card do
  dashboard cai de ~40 ms para ~12 ms por redesenho (no clock antigo — o novo
  está logo abaixo).
- **Limpeza de tela na velocidade do fio.** Um fill sólido dedicado por DMA
  (fonte sem incremento) substitui o `fillScreen` por-pixel na entrada de cada
  tela, no fundo do dashboard e nas faixas da página de licença.
- **Chrome dos menus pelo canvas.** Barras de título e rodapés são compostos
  no canvas compartilhado e enviados em largura cheia, em vez de desenhados
  widget a widget no painel. Os clears redundantes do strip renderer saíram, e
  os menus principal/sons repintam só as duas linhas cuja seleção mudou.

### SPI no teto do silício

O clock de escrita sobe de 31,25 MHz para 62,5 MHz — o divisor PL022 do
RP2040 não oferece nada entre os dois. Os dois caminhos de escrita
(biblioteca e DMA) agora compartilham uma constante única, `SIMUT_TFT_SPI_HZ`
em `simut_config.h`, para não divergirem em silêncio. Validado em hardware
lendo o GRAM do painel de volta em três capturas consecutivas: todo pixel
divergente estava em conteúdo vivo do top bar, nenhum em região estática. Se
a sua fiação mostrar artefatos nessa velocidade, sobrescreva a constante para
`31250000u` — todo o resto da versão fica de pé sozinho.

Efeito combinado, medido/derivado em hardware: entrar numa tela de ajustes
caiu de ~240 ms para **~50 ms**, o redesenho completo do dashboard de 121 ms
para **~35 ms**, e a piscada de alarme custa um quarto do que custava.

### O gráfico responde no instante do toque

Abrir um gráfico de qualquer tela agora sempre mostra a tela de carregamento
(ela pinta em ~45 ms, então lê-se como transição, não como tela em branco).
Zoom, navegação e toques de calendário *dentro* do gráfico mantêm de
propósito o plot antigo na tela para contexto — e acendem uma linha fina de
destaque no topo no momento em que o toque cai, para uma leitura de flash de
um segundo nunca parecer toque morto. O próximo render cobre a linha.

### Também

- Removido um canvas off-screen de 140×40 alocado a cada boot e desenhado por
  ninguém desde a 2.1.5 — **11,5 KB de heap devolvidos** (heap livre pós-boot
  na bancada foi de 46,6 KB para 58,2 KB).
- `blitCanvas` agora tem contrato documentado: ele consome o canvas; componha
  antes de cada blit. Todos os callers existentes já faziam isso.
- Corrigida a documentação do strip renderer (strips de 6×40 px, não 3×80) e
  comentários de tempo de fio defasados.

## v2.1.5-beta (2026-08-12)

### O display ganha um sistema visual único

As 17 telas do TFT cresceram uma de cada vez, e isso aparecia: três tipografias
conviviam (a tela de Status usava a fonte 5×7 de terminal esticada), o símbolo de
grau tinha três grafias ("o" em 9pt, um "c" minúsculo na fonte clássica, um "oC"
literal), fechar uma tela era diferente em cada tela que fechava, a paginação tinha
quatro idiomas e sobravam cores RGB fixas que ignoravam o sistema de temas. Esta
versão substitui tudo isso por uma camada de widgets compartilhada (`UiWidgets.h`)
da qual toda tela se compõe — barra de título com aba accent e page dots, dois
estilos de botão (a ação primária é sempre o canto inferior direito; sair/fechar
nunca é primário), um botão de fechar padrão, uma scrollbar, ícones no menu.
**Nenhuma zona de toque mudou**: os widgets desenham nos mesmos retângulos dos quais
o handler de toque já deriva suas áreas.

### Acentos de verdade no TFT

Os packs de idioma sempre carregaram UTF-8 ("Configurações" estava no `.lng` desde
sempre) — o display é que transliterava para ASCII em runtime, porque as fontes GFX
de 7 bits não tinham os glifos acentuados. As faces 9pt e 12pt agora são regeradas
do mesmo FreeSansBold.ttf (GNU) de onde vieram as originais, com cobertura Latin-1
subsetada para ASCII + os 32 glifos que pt-BR/es-ES precisam (+3,4 KB de flash), e o
`tr()` mapeia UTF-8 para Latin-1 em vez de descartar. Português e espanhol aparecem
acentuados no painel; a CLI serial continua na transliteração de 7 bits. O símbolo
de grau agora é o glifo da própria fonte em todo lugar — inclusive no helper de
unidades, então o "°C" do editor de alarmes, dos cartões do painel, das estatísticas
e do status finalmente concordam.

### O renderizador de strips sai por DMA

Os strips de largura cheia do canvas — o caminho quente de toda tela — são empurrados
com o periférico SPI em modo de quadro de 16 bits alimentado por um canal de DMA
(sem troca de bytes, sem buffer intermediário, síncrono de propósito para não tocar
no protocolo de quiesce/pausa de flash). O redesenho completo do painel, medido no
hardware, foi de **254 ms para 121 ms**. Quando o offset de alinhamento da tela
empurra um strip para fora do painel, vale o caminho antigo da biblioteca.

### Também

- Gráfico de histórico: preenchimento sutil sob a curva de temperatura; botões da
  barra com a borda padrão; lupas de zoom na cor do tema.
- Status do Sistema: valores alinhados à direita para Serial/SSID/MAC caberem em uma
  linha; corrigidos dois vazamentos pré-existentes — a faixa do rodapé nunca era
  limpa (botões da tela anterior sobreviviam nas frestas) e a reserva fixa da
  unidade cortava o "°C" na borda direita.
- Calendário: navegação de mês só na barra de baixo; "Mês" enfim com acento, assim
  como os outros literais pt fixos no código (Atenção, serão).
- Teclado de senha: OK/123 na fonte da UI, traços do espaço e do confirmar mais
  grossos.
- Custo líquido de binário da versão inteira: cerca de +2,2 KB de flash; nenhuma
  chave de tradução nova, então os packs `.lng` instalados continuam válidos byte a
  byte.

## v2.1.4-beta (2026-08-11)

### A hora ainda aberta na RAM chega aos gráficos e ao CSV

Um bloco V5 fica na RAM e só chega ao arquivo do dia quando sela, o que a um registro
por minuto é uma vez por hora. Tudo que lia `.h5` ficava, portanto, até uma hora atrás
do presente: abrir um gráfico — no display ou na web — não mostrava os últimos minutos,
e um export CSV parava na última selagem por mais recente que fosse a janela pedida. A
telemetria já tinha ganhado um caminho para além disso quando se descobriu que um
aparelho novo ficava mudo nos primeiros 60 minutos; os gráficos e o export, não.

As amostras nunca estiveram faltando. Elas ficam em texto claro no encoder, sem
empacotamento de bits, então alcançá-las custa uma cópia e nenhuma decodificação — e o
`/history/.wip` ao lado delas é um limite para queda de energia, não um caminho de
leitura: o boot o adota no arquivo do dia e mais nada o abre.

- **Gráfico do display** (`renderGraphOptimized`) e **gráfico da web**
  (`/api/history_multi`, tanto o caminho de decode quanto o de envelope) agora seguem
  para o bloco aberto depois dos arquivos do dia. Os canais são resolvidos contra o
  schema vivo, e não o do leitor, porque o bloco aberto é codificado com o conjunto de
  sensores em vigor agora, não o do arquivo mais novo em flash.
- O registro mais novo sai **independentemente da decimação**. Sem isso, uma faixa de
  24 h (passo 8) ainda deixaria a borda direita até oito minutos velha, e uma faixa
  decimada 40:1 deixaria quarenta — a borda direita estar em dia é o objetivo.
- **Export CSV**: o aparelho serve o bloco aberto em `GET /api/history/open` como uma
  corrente V5 de um bloco só — um chunk SCHEMA seguido do bloco selado PARTIAL, byte a
  byte o que um `.h5` parece. A página o busca depois dos arquivos do dia e roda nele o
  decodificador que já tem, então não há segundo formato nem segundo decodificador.
  Buscado por último de propósito: uma selagem no meio do export passa a custar no
  máximo uma lacuna, nunca uma linha duplicada. Um export cuja janela não tem arquivo
  nenhum — um aparelho na primeira hora depois de um factory reset — devolve a hora
  aberta em vez de "nenhum dado recuperado".

O `/api/history_multi` informa `"ram"` (registros vindos do bloco aberto) e marca
`"path"` como `decode+ram` / `envelope+ram` quando a cauda contribuiu, que é o único
campo capaz de distinguir uma resposta viva de uma parada.

Medido no hardware: a borda direita do gráfico saiu de até uma hora atrás para **0 s**,
com a costura visível através de um reboot (o flash termina em 18:33:51, a RAM carrega
18:35 → 18:41). A corrente do bloco aberto foi decodificada pelo `tools/history_v5.py`
— a implementação de referência que os testes nativos já usam como oráculo — com **0
erros de frame/CRC** sobre um bloco de 9 registros cujo payload de 37 bytes carrega 64
valores, e independentemente pelo próprio decodificador da página, ambos concordando
valor a valor com as cópias planas que o caminho JSON emite.

**Fora do escopo:** o `/api/export/history.bin` (o bundle `.simx`) continua lendo só
arquivos. Ele não é mais o caminho do botão CSV — a página baixa os `.h5` e decodifica
localmente — mas segue alcançável por URL e para na última selagem.

## v2.1.3-beta (2026-08-11)

### O Core 1 parka antes de a pausa de flash matá-lo — o wedge da tempestade de display acabou

Uma escrita de flash no Core 0 (um save de config, um registro do histórico) pausa o
Core 1 antes, para o erase nunca rodar com o Core 1 buscando de XIP. Essa pausa pedia
ao Core 1 para parkar no topo do laço de render e esperava só 200 ms por isso — girando
ali **sem alimentar o watchdog**. Mas um render mede até ~1 s sob carga, então 200 ms
expiravam mid-render: o Core 1 era hard-resetado segurando um lock (o mutex de estado do
render, o alocador, um spinlock), e a próxima aquisição desse lock pelo caminho de flash
do Core 0 travava para sempre com o watchdog sem alimentar. Na bancada isso reiniciava
como `C0=[CLI] C1=[DISPLAY]` sob uma tempestade de save+toque+leitura — a mesma forma do
reboot `C0=[STORAGE_WR]` do histórico que um usuário pegou configurando o aparelho — e no
pior caso escalava para um **wedge** do QSPI: um travamento morto que só um power-cycle
resolvia.

A janela de park agora cobre um render inteiro (1200 ms) e alimenta o watchdog enquanto
espera, então o Core 1 chega a um ponto sem locks antes do reset, em vez de morrer
mid-work. Aplicado aos dois caminhos de pausa — o quiet-mode do save e o lockout IRQ do
histórico. Medido contra a mesma tempestade: o **wedge sumiu** (o aparelho se recupera
sozinho em vez de travar), os reboots de watchdog caíram ~3×, e nenhuma escrita de flash
rodou sem pausa (`fx` ficou 0).

**Ainda aberto:** um reboot residual `C0=[CLI]` sobrevive à tempestade — às vezes o Core 1
não parka nem em 1200 ms. Fechá-lo exige a passada de marcadores por-instrução que
localizou o reboot do drain; anotado para o próximo ciclo. A falha do dia-a-dia (um único
reboot que também perdia ou desalinhava dado — ambos corrigidos nesta linha) não trava
mais o aparelho.

## v2.1.2-beta (2026-08-11)

### Um reboot não arrasta mais o bloco recém-recuperado 15 minutos para o futuro

Um reboot no meio da hora perdia um quarto de hora de histórico para a hora
errada, não para a gravação. O snapshot `.wip` recuperava o bloco aberto
corretamente, com os timestamps próprios de antes do reboot — e então a correção
de NTP no boot o movia. A cadeia: o `getLastRecordedTimestamp()` semeia o relógio
provisório do registro mais novo, mas lia só o arquivo diário selado, nunca o
`.wip`. Então, após um reboot no meio da hora, semeava do último bloco *selado* —
até uma hora atrás do dado mais novo real, que estava no `.wip`. O NTP media essa
base velha como um erro grande (na bancada: +919 s) e o `shiftHistoryTimeV5()`
desloca todo bloco com `t0 >= base` — pegando o bloco que o `recoverWipV5()` acabou
de restaurar, já com a hora certa, e empurrando-o para frente pelo erro inteiro.
05:48–06:03 virou 06:04–06:18; a janela de 05:48 lia vazia e o leitor parava no
fim dela.

Correção: o `getLastRecordedTimestamp()` agora também lê o `.wip`, pegando o mais
novo entre o arquivo selado e o snapshot. O relógio provisório cai perto do real
(o erro do shift encolhe para segundos) e, decisivo, o piso do shift sobe acima do
`t0` do bloco recuperado, então o bloco que o reboot acabou de restaurar fica
isento e permanece exatamente onde os timestamps dele mandam. Verificado em
hardware: um bloco parcial em 06:32–06:36 foi snapshotado, o alvo resetado por
hardware, e o bloco voltou em 06:32–06:36 sem se mover, com a correção de NTP em
−13 s (era +919 s) e os blocos horários selados intocados. O reboot que disparou
tudo — um travamento de watchdog no caminho de escrita de armazenamento — é uma
questão de estabilidade separada, ainda aberta.

### Trocar a seleção de sensores durante o load agora cancela a transferência e começa um load limpo

A página do gráfico busca o histórico em fatias, e cada carregador (`fetchAndDraw`)
era `async` mas sem coordenação: trocar a seleção de sensores — ou a faixa, ou a
data — enquanto um gráfico ainda carregava iniciava um *segundo* laço de fatias sem
parar o primeiro. Dois laços disputavam a mesma barra de progresso e o mesmo abort
compartilhado, e disparavam requisições `/api/history_multi` sobrepostas ao
aparelho — a "barra de carregamento atrapalhada, vários downloads ao mesmo tempo"
que o usuário relatou. Essa sobreposição também é o que expunha o reboot do drain
(D-B8c, abaixo), então isto corrige a aparência e remove o gatilho na origem.

O `fetchAndDraw` virou um coordenador: incrementa uma geração, aborta a
transferência em voo e enfileira o novo load atrás dela numa cadeia de promessas,
de modo que exatamente uma transferência fica viva e a seleção mais nova vence. Um
load superado antes de começar é pulado; um superado no meio do fetch descarta o
resultado em vez de desenhar por cima do mais novo. Só no cliente (`WebUI.h`); faz
par com o null-guard do firmware, então o aparelho fica seguro mesmo se outro
cliente ainda sobrepuser.

### Um corpo de POST lento reiniciava o aparelho — a perda por trás de "reinicia ao configurar"

As medições se perdiam por um reboot, não pela gravação. Perseguir na bancada o
"dado perdido ao reiniciar ou configurar" achou um reboot vivo de watchdog com a
assinatura `C0=[WEB_POLL] hp=0 (219)` — `hp=0` significa que o `handleClient()`
não retornou, então a travada foi dentro dele. A correção do D-B8 limitou a linha
de request e os cabeçalhos com um leitor que alimenta o watchdog e tem prazo de
parede; o **corpo** do request ficou nas leituras de fábrica, que não alimentam
nada e são consumidas **durante** o parse, antes do dispatch e da auth. Um POST
cujo corpo goteja segura o Core 0 além da janela de 8388 ms e reinicia — no exato
caminho usado para salvar configuração.

Reproduzido de forma determinística (`scratchpad/repro_post_slow.py`): `POST
/api/save_sys` a 1 s/byte levou o aparelho de uptime 2815 s a 31 s; `/api/upload`
e `/api/restore` fizeram o mesmo pelo laço RAW de upload.

Três leituras de corpo sem limite, agora sob a mesma disciplina do `simutReadLine`:
- **`plain`/urlencoded/json** (`readBytesWithTimeout`): alimenta o watchdog
  enquanto o corpo goteja e limita a leitura inteira por relógio de parede
  (`SIMUT_BODY_BUDGET_MS = 15000`). Só alimentar trocaria "reboot em 8 s" por Core
  0 congelado por horas com um `Content-Length` grande declarado; o teto faz o
  estouro voltar parcial e derrubar o cliente. Um POST de config real é uns poucos
  KB em um segmento em <1 ms, então o orçamento só é gasto por stall.
- **upload RAW** (`/api/upload`, `/api/restore`): um novo `simutReadRaw` lê só o
  que já está no buffer — então o `readBytes` não bloqueia como bloqueava por byte
  —, alimenta o watchdog enquanto espera, e desiste após um intervalo curto sem
  dados. Sem teto total: upload de firmware/arquivo é longo e limitado por flash.
- **multipart** (`_uploadReadByte`, `_parseForm`): a espera por byte agora
  alimenta o watchdog e as linhas de header usam o leitor com prazo.

Mesmo quinto override do framework (`webserver_parse_deadline.patch`), regenerado
para que `restore → patch → rebuild` reproduza o `firmware.bin` gravado byte a
byte. Validado na bancada: o reboot sumiu nos três caminhos (0 novo `hp=0 (219)`
na captura de boot), um upload legítimo rápido segue funcionando, `/`, `/history`
e `/config` servem inteiros, o slowloris na linha de request ainda dropa, `fx=0`.
Os dois ambientes de firmware compilam (release 93,8 %, test 98,5 %).

O reboot que o usuário também relatou ao *ler gráficos* é um **mecanismo
separado**, e o usuário fixou o gatilho: **trocar a seleção de sensores enquanto o
gráfico carrega** ("vários downloads ao mesmo tempo, a barra de progresso
atrapalhada"). Três autópsias em três builds levaram até ele. `hp=740` disse que o
`handleClient()` retornou e a travada era no drain seguinte; uma 1ª correção supôs
as consultas lwIP e só moveu o marcador para `hp=603`; uma 2ª (o light-yield do
`feedWatchdog` no drain) também errou. Marcadores por-instrução então nomearam a
instrução exata: `hp=6031` = `WiFiClient c = _server.client();`, a cópia do cliente
atual.

Causa-raiz, provada pelo framework: `_server.client()` devolve
`*(ClientType*)_currentClient`, e o `handleClient()` deleta o `_currentClient` e o
zera sempre que o peer não está mais conectado — o que a troca de sensores no meio
do load causa, dando RST no gráfico em voo e abrindo conexão nova. Mas o
`_drainPending`, travado `true` pelo envio já concluído dessa resposta, continua
ligado, então o `drainOrDrop()` copia `*(ClientType*)nullptr`: a cópia lê através
de um `this` nulo, tira um `ClientContext*` lixo da ROM e faz `ref()` nele — uma
leitura a endereço selvagem que trava o barramento até o watchdog. Só sob
requisições sobrepostas, uma corrida de microssegundos que nenhum cliente sintético
acertou (o drain foi exercitado ~5000× em cinco estilos sem ela). Correção: o
`drainOrDrop()` e o `dropAbortedStream()` pegam o ponteiro, não uma cópia —
`&_server.client()` dobra para `_currentClient` sem dereferenciar, então um cliente
já retirado (nulo) cai no guard em vez de ser lido, e a cópia por-drain some. Lição
registrada em dobro: o `hp` localiza a posição; a cura exige saber *o que* roda ali
— raciocinar "é instantâneo" estava errado sobre código que, com o cliente nulo,
não era.

### Medições se perdiam no corte de energia e em toda reinicialização para configuração

O bloco de histórico aberto vive em RAM e só chega à flash quando enche, uma vez
por hora. Um snapshot em `/history/.wip` limitava a exposição — e o limite era
dez minutos, porque é literalmente o que o R8 pedia: *"power-loss: perda máxima
de 10 min de dados"*. O requisito estava sendo cumprido exatamente como escrito,
e o que estava escrito não servia.

Três vias de perda distintas, com custos muito diferentes:

**Seis das sete reinicializações voluntárias não gravavam nada.** O `reload
confirm` da CLI era o único caminho que fazia certo — sela o bloco explicitamente
antes de chamar `safeReboot()`. Os outros seis não, e um deles é o `commit_all` da
web: o reboot que você dá *para configurar o aparelho*. Esses reiniciavam direto e
descartavam tudo desde o último snapshot periódico, até dez minutos,
determinístico, toda vez. O caminho da CLI ter a selagem e o da web não é a forma
do defeito — a proteção estava escrita por chamador, então valia só até o próximo
chamador esquecer. Daí o gancho no ponto de estrangulamento: o próprio
`safeReboot()` grava o snapshot na saída, e um caminho de reboot novo não tem como
esquecer.

Dois chamadores precisam suprimir o gancho, e suprimem: `system format confirm` e
o *apply* de restore com `fs_mod`, onde um snapshot do bloco em RAM anterior ao
apagamento ressuscitaria no boot seguinte o dado que o usuário mandou destruir.

**O timer de dez minutos saiu.** O snapshot passa a ser feito a cada registro
aceito, inline, então corte de energia não perde nada. O custo foi medido antes
da escolha, não depois: 1.440 regravações do `.wip` por dia contra 144. O limite
não é endurance (~2,6k erases por bloco por ano contra 100k nominais) — é o *duty
cycle* de lockout do Core 1, e por isso a escrita continua cedendo a vez à
prioridade de toque e à trava de tarefa pesada.

**Alguns minutos simplesmente não eram medidos.** O laço condicionava a *amostra
inteira* às mesmas duas condições, então um portão retido na virada do minuto
deixava aquele minuto sem leitura — um buraco que nenhum snapshot preenche, porque
nada foi registrado.

Só um dos dois portões podia disparar, e vale dizer porque um rascunho anterior
desta entrada afirmava os dois. O `isUserInteracting()` é real: o timestamp do
toque é posto pelo Core 1 e lido pelo laço, então pode estar verdadeiro enquanto a
linha da amostragem roda. O `isHeavyTaskLocked()` não podia: todo detentor —
`_webMgr->update()`, `_telemetryMgr->update()`, o gráfico via eventos de UI — roda
antes, no **mesmo** laço do Core 0, estritamente sequencial com a amostragem, e
nada no caminho do Core 1 pega a trava. Medido: trava pesada retida a 57% de *duty
cycle* por seis minutos adiou exatamente zero snapshots e pulou exatamente zero
registros. Remover essa metade do portão é correto mas não muda nada observável; a
metade do toque é a que perdia leituras. Amostrar e
gravar agora são coisas separadas: o registro sempre entra no encoder em RAM (um
memcpy, seguro sob qualquer gate) e só a escrita em flash adia, latchada para que
a varredura de recuperação grave em até 2 s da abertura do gate, em vez de
esperar o próximo registro carregar o anterior.

Uma troca é deliberada e vale dizer em voz alta: selar bloco cheio, e a selagem
de virada de dia, agora rodam mesmo com gate fechado. Um bloco cheio não aceita
outro registro, então a escolha ali é entre uma janela de lockout e uma amostra
perdida — 24 janelas forçadas por dia contra a promessa de que nenhuma se perde.

Três perdas silenciosas achadas auditando a mesma função, as três por um retorno
de `sealHourV5()` que ninguém lia:

- **A selagem horária descartava o bloco inteiro ao falhar.** O `reset()` que vem
  depois esvazia o encoder de qualquer forma, então uma selagem falhada jogava
  fora até 60 registros sem dizer nada além de um aviso genérico de escrita. É a
  selagem que dispara *toda hora*, de longe a mais provável de falhar entre as
  três. Agora recusa o registro que estava entrando enquanto o bloco retido ainda
  tem tentativas, porque o bloco é o que vale proteger.

- **Selagem de virada de dia falhada arquivava o bloco no dia errado.** O código
  adotava o dia novo de qualquer forma, então o registro seguinte era enxertado
  no bloco de ontem e o bloco inteiro ia para o arquivo de *hoje* — §14-6
  quebrado, e junto com ele o "o nome do arquivo É o limite". Agora recusa aquele
  único registro e deixa o bloco intacto para o minuto seguinte tentar de novo.
  Um registro em risco num sistema de arquivos já degradado é melhor que até 60
  arquivados errado sem erro nenhum em lugar nenhum.
- **Selagem falhada na troca de conjunto de sensores descartava até 60 registros
  em silêncio.** O `ensureH5Schema()` logo em seguida reexecuta `_h5Enc.begin()`,
  que descarta qualquer bloco em andamento. O `.wip` não é escapatória — ele
  carregaria o schema antigo e o `recoverWipV5()` valida contra o compilado, então
  o boot seguinte o rejeitaria. Agora tenta uma segunda vez (o que cobre um
  timeout transitório de mutex) e, se ainda falhar, registra quantos registros se
  perderam em vez de sumir atrás de um aviso genérico de escrita.

### Um reboot ainda perdia uma leitura, e o bloco não tinha nada a ver com isso

Relatado na bancada depois do acima entrar, e as duas metades eram verdade. Num
`commit_all` da web: `STO_H5_WIP ctx=50` do gancho pré-reboot, o boot seguinte
adotando `ctx=50`, bloco intacto — e 108 s entre o último registro antes e o
primeiro depois, contra um intervalo de 60 s. Um registro faltando na sequência.

`_lastHistoryTime` começa em 0, então a checagem de intervalo não pode disparar
antes de `millis()` passar um intervalo inteiro: o primeiro registro de todo boot
caía em `up=60s`, somados aos ~20 s que o próprio boot leva. Preservar o bloco
nunca ia corrigir isso, porque o minuto não era amostrado.

O primeiro registro agora sai assim que o relógio é confiável, com o portão no
relógio **cru** do sistema — deliberadamente não `getEpoch()` nem `isTimeSynced()`.
O `getEpoch()` semeia um relógio provisório com `SIMUT_BUILD_EPOCH` (2025-09-20) e
devolve isso, que fica acima do `HIST_EPOCH_MIN`; logo, os dois reportam relógio
bom num aparelho que não tem nenhum, e o registro seria arquivado dois anos no
passado. Carimbo errado envenena o arquivo do dia pior que minuto faltando.

Medido num `reload confirm` real: primeiro registro em `up=23s`, buraco de 41 s,
zero registros faltando; o caso de 108 s vira 71 s, também zero. `up=23s` está
perto do piso, porque o NTP chega por volta dos 20 s e é aí que o carimbo passa a
ser verdadeiro. Resíduo: derrubar um registro agora exige buraco acima de 120 s, o
que pede um boot atrasando ~37 s além do vencimento do registro — uma retentativa
de WiFi ou timeout de DHCP ainda consegue.

### Recuperação limitada para selagem falhada

Os dois lados de uma selagem falhada são perda, então a recuperação é **limitada**
em vez de escolhida: descartar o bloco na primeira falha joga fora até 60
registros por causa do que normalmente é um timeout transitório do mutex do
`FLASH_OP`, e segurá-lo para sempre significa um aparelho que para de registrar
em silêncio, definitivamente. Cinco registros recusados — a paciência de um
intervalo, bem abaixo do bloco que se está protegendo — e então o bloco é dado
por perdido, a perda é registrada com a contagem, e o registro volta a andar.

Sem mudança de formato em disco: bytes gravados antes continuam legíveis, e o
`.wip` segue sendo exatamente um chunk DATA `PARTIAL`. Emenda E10 em
`docs/HistoryV5_Emendas_Rev2.md`; R8, §7.1, §7.2 e a matriz de aceitação do §11
reescritos na Rev 2.0 normativa.

## v2.1.1-beta (2026-08-10)

### Um único request HTTP lento reiniciava o aparelho — remotamente, sem auth

A v2.1.0-beta saiu com o `C0=[WEB_POLL]` listado como resíduo aberto, descrito
como problema de concorrência pesada. O soak o pegou na imagem publicada com o
aparelho praticamente ocioso, e o mecanismo não era nem concorrência nem
resposta grande.

O `WebServer::handleClient` faz o parse do request com `readStringUntil`, que
espera o timeout do cliente **por byte** e reinicia esse timeout a cada byte
recebido. Um cliente que goteja um byte logo abaixo do timeout segura o Core 0
dentro da leitura para sempre, e nada alimenta o watchdog de hardware enquanto
isso acontece — o loop principal alimenta o watchdog **antes** do
`handleClient`, nunca durante. Então um único request lento, sem autenticação e
sem concorrência, derrubou o aparelho em cerca de oito segundos. A autópsia ao
vivo é inequívoca: `C0=[WEB_POLL] hp=0 sc3=0x80088013 (219)` — `hp=0` significa
que o `handleClient` não retornou.

O parser agora lê cada linha sob um orçamento de relógio único, com o watchdog
alimentado a cada byte. Um request que estoura o orçamento volta parcial, e o
servidor descarta o cliente em vez de travar nele — um request lento dropado em
vez de um reboot. Numa LAN um request real chega em um único segmento em bem
menos de um milissegundo, então o orçamento só é gasto por um stall.

Medido na bancada: o mesmo repro que reiniciou a v2.1.0-beta (um GET a 3 s/byte)
não reinicia mais, em três taxas de gotejamento (0,4 / 1,0 / 3,0 s por byte),
zero reboots; requisições normais intactas (40/40 sequencial, página e download
de log inteiros). Mesmo padrão dos quatro overrides de framework já em
`tools/arduino_pico_overrides/`, e aplica limpo em 5.4.3 e 5.6.1 (o parser é
byte-idêntico entre as duas).

Isso fecha o reboot remoto sem autenticação. Continua aberto o caso mais brando
por trás da mesma autópsia sob seis clientes concorrentes — estreitado, não
refeito aqui. Detalhe como D-B8 em `docs/beta-sweep-2026-08-10/`.

## v2.1.0-beta (2026-08-10)

Primeira beta. A versão sai da linha alpha porque os defeitos que a
seguravam ali foram fechados e medidos, não porque o calendário andou.

### O `/api/restore` gravava os arquivos antes de conferir quem estava pedindo

A verificação de permissão do restore morava só no handler de fim. O framework
chama esse handler **depois** que o corpo multipart inteiro já passou pelo
callback de upload, e um feed em modo apply grava cada entrada direto no
caminho final — o nome real, sem rename, escrito conforme os bytes chegam.

Ou seja: o 403 estava certo no veredito e atrasado no efeito. Um POST **sem
autenticação nenhuma** em `/api/restore?op=apply` sobrescrevia qualquer coisa
que o formato de backup consiga nomear: `/config`, `/calib.csv`, `/history`,
os pacotes de idioma. A checagem de caminho só recusa `..`, e não era preciso
cookie nenhum para chegar até ali.

Medido na bancada com um backup de uma entrada carregando o chip id do próprio
aparelho: antes, a requisição respondia 403 e o arquivo aparecia no sistema de
arquivos; depois, 403 e nada gravado. Os caminhos legítimos ficaram intactos —
um validate autenticado de um backup real de 807 KB continua respondendo sobre
seus 106 arquivos, e a exposição do Core 1 ficou em `metr.fx=0` nos dois.

Se você roda um aparelho numa rede que não controla por inteiro, é por isto
que vale pegar esta versão.

### Os últimos descartes silenciosos passam a se anunciar

O `users.actions` nunca tinha sido varrido junto com a família espaço-no-JSON.
Ele lia `type` e `name` por agulhas com a aspa embutida, então um payload com
o espaço que o JSON permite depois dos dois-pontos não casava com nada e a
ação inteira evaporava sob um 200. Passando disso, toda recusa era um
`continue` seco: nome inválido ou reservado, duplicado, tabela cheia, um `del`
apontando para uma vaga que não existe. A página não oferece verificação
nenhuma no cliente para nada disso — então acrescentar um quinto usuário era
clicar em Salvar & Reiniciar, esperar o reboot e descobrir a conta
simplesmente ausente. Cada caso agora se nomeia no array `rejected` que a
seção sys já usava, e as permissões ficam presas aos dez bits que a página
consegue marcar.

Os campos de texto da sys iam direto para uma cópia que trunca para caber: um
servidor de 70 caracteres virava um de 63 e o commit ainda respondia ok. Agora
passam pelo mesmo validador que a CLI sempre usou, e um valor que não cabe
inteiro é recusado em vez de guardado errado.

O `save_sys` respondia ok para um índice de tema que a build não carrega, e a
página não tinha como distinguir aplicado de ignorado. Agora responde 400.

### O Core 1 fica visível na imagem que é publicada

O heartbeat, a contagem de launches e os três contadores de kill chegavam só
ao `show metrics` — comando que o perfil de release não carrega. Um display
travado, visto de fora, é idêntico a um saudável; um soak ligado nessa imagem
poderia relatar sucesso atravessando uma morte do Core 1. O `/api/status`
agora carrega `c1a` (idade do carimbo que o Core 1 escreve a cada volta do
laço), `c1n` (launches), `c1kl`/`c1kh`/`c1kq` (kills separados por causa) e
`c1s` (lockouts travados), pelo mesmo motivo que o `fx` e o `cgd`/`cgg`/`cgx`
já estão lá.

### Um terceiro caminho para o parque `C0=[WEB_POLL]`, achado ao fechar o de cima

Colocar o gate no restore tornou o caminho de recusa alcançável por qualquer
um — e o caminho de recusa derrubava o aparelho. Repetir um apply sem
autenticação reiniciou na 12ª requisição numa corrida e na 31ª noutra, com a
mesma autópsia que estava registrada como resíduo aberto desde a campanha de
tempestade de rede.

É o mesmo defeito que aquela campanha curou em dois lugares: o 403 responde
não-chunked e retorna, então nada da disciplina de aborto cobre a cauda, e o
framework aposenta o cliente com um `stop()` seco cuja espera de ACK se renova
a cada progresso e nunca alimenta o watchdog. Drenar antes de retornar é o que
o `safeStreamFile()` e o `/api/backup` já fazem. 100 recusas depois: nenhum
reboot, todas respondidas com 403, nada gravado.

Duas atribuições foram tentadas e descartadas no caminho, ambas convincentes
na hora: que a linha de log que o gate acrescentou dentro do callback multipart
era a culpada (tirar a linha deu 40 requisições limpas — falso negativo, o
reboot voltou na 31ª com a linha em outro lugar), e que o `/api/logs` era o
gatilho (51 buscas, nada). Um evento que dispara uma vez a cada algumas dezenas
de requisições não se descarta com uma corrida limpa de quarenta.

### Continua aberto

O parque residual `C0=[WEB_POLL]` sob carga concorrente de seis clientes,
documentado em `docs/netstorm-campaign-2026-08-10/`, está estreitado mas não
fechado: três caminhos para ele estão drenados, e o caso de seis clientes não
foi refeito aqui. A janela de IRQ desligada de 68–78 ms contra o critério de
60 ms (D-NS7) segue intocada.

## v2.0.3-alpha (2026-08-10)

### A janela de recepção deixa de prometer o pool de pbuf em dobro

O `D14` constava como vazamento de pbuf "com uma segunda fonte ainda não
localizada". Não é vazamento — e ninguém achava a segunda fonte porque não havia
mais nenhuma para achar.

O que se media era o **pico** do pool, uma marca d'água que por definição nunca
desce, e a contagem de falhas. O número que separa vazamento de pressão é o que
continua **em uso depois que a carga para**, e ele nunca tinha sido lido. Ele
volta ao basal em todos os níveis de concorrência, inclusive naquele que esvaziou
o pool e falhou 79 alocações. Nada fica retido.

A causa real é aritmética. Um envelope do pool custa ~1514 B e o `TCP_WND` era
8×MSS, então uma conexão pode segurar 7,7 deles; seis conexões com as janelas
cheias pedem 46 contra um pool de 24. Quatro clientes chegam a 13 e nunca falham,
cinco atingem 24/24 com 45 alocações falhadas, seis com 79.

O `TCP_WND` agora é 4×MSS. Não custa nada mensurável porque o device nunca
conseguiu usar a janela que anunciava: uploads rodam a 26 KB/s, limitados por
escrita em flash, e num ida-e-volta de ~5 ms mesmo 4×MSS permitiria ~1,1 MB/s.
Downloads são governados pelo `TCP_SND_BUF` e ficam intocados.

| | antes | depois |
|---|---|---|
| falhas de alocação, 5 / 6 clientes | 45 / 79 | **0 / 0** |
| pico do pool com 6 clientes | 24/24 | 18/24 |
| requisições bem-sucedidas com 6 clientes | 98 | 166 |
| download | 221 KB/s | 216 KB/s |
| upload | 26 KB/s | 25 KB/s |

Aumentar o pool era a alavanca errada: 24 envelopes já são 35,5 KB de BSS, e
dobrar custa mais que o heap livre inteiro.

Vale dizer com todas as letras, porque o nome antigo sugeria o contrário: secar
o pool nunca reiniciou o device. As requisições falham e o pool volta inteiro.

Isso não é o mesmo que dizer que concorrência pesada é segura. Seis clientes
martelando ainda batem no resíduo `C0=[WEB_POLL]` documentado em
`docs/netstorm-campaign-2026-08-10/` — visto uma vez aqui em cerca de dois
minutos de carga a seis, e não reproduzido numa repetição de 90 s. Ele é
anterior a esta mudança, que ataca falhas de alocação e nada mais, e segue
aberto.

## v2.0.2-alpha (2026-08-10)

### Sobrevive a uma rede hostil: a costura de watchdog no caminho de envio

Uma campanha rodou a matriz de falhas de telemetria, um martelo web concorrente
e os sensores **ao mesmo tempo** — 26 janelas de falha em cerca de duas horas —
porque todas as corridas anteriores exercitaram essas cargas uma de cada vez, e
é na sobreposição que os defeitos moravam. Relatório e lista numerada de
defeitos em `docs/netstorm-campaign-2026-08-10/`.

**O laço de envio do `HTTPClient` nunca alimentava o watchdog, e era a maior
parte dos reboots.** O orçamento de 5 s do `StreamConstPtr::sendAll` limita o
laço, não uma escrita; cada `write()` estaciona pelos 4 s do timeout de socket,
então uma escrita iniciada perto do fim do orçamento termina por volta de 9 s —
além dos 8388 ms do watchdog de hardware. Fechado por um quarto override de
framework, ligado ao `patch.sh` para não sumir num upgrade. Medido no grupo HTTP
completo, mesmas condições antes e depois: **5 reboots → 1**, MTBF sob
tempestade **~10 min → 58 min**, 557 downloads de histórico sem um JSON inválido.

**Uma resposta não-chunked deixava a cauda para o framework estacionar.** O
fechamento duro existente valia só para respostas chunked, então `/download` e
`/api/backup` seguiam pelo caminho educado — e esse caminho espera por ACKs com
um relógio que reinicia a cada um deles, sem alimentar. Reproduzia sem
tempestade nenhuma: **um download por boot**. Drenar antes de o handler retornar
resolveu — `/download` foi de 6/8 com 2 reboots para **24/24 sem nenhum**, e
`/api/backup` (794 KB cada) de 2/3 com 1 reboot para **6/6 sem nenhum**.

### Corrigido

- **Um único envio abortado na cauda do histórico podia travar o display.** Três
  `return` na cauda `extremes` pulavam o desenrolar do handler, deixando o latch
  `_inHistoryHandler` preso — todo `/api/history_multi` seguinte respondendo
  `503 Already processing` — e o overlay de "web ocupada" do display travado com
  **o toque bloqueado**, ambos até o próximo reboot. A posse agora vive num
  destrutor, que um `return` não consegue pular.
- **`/api/sec_status` podia escrever fora do buffer.** O `pos += snprintf(...)`
  acumulado passa do array assim que uma entrada trunca, e a aritmética do
  espaço restante é sem sinal, então ela dá a volta em vez de ficar negativa. O
  espaço agora é limitado antes de cada escrita, e uma entrada truncada é
  desfeita para o JSON seguir parseável com menos slots.

### Adicionado

- `metr.cgd` / `metr.cgg` / `metr.cgx` em `/api/status`: as três razões para uma
  resposta chunked ser cortada — prazo, latch do guard, desconexão real. O `show
  metrics` já as imprimia, mas esse comando não existe fora da imagem de CLI
  completa, então pela rede um download truncado e um cliente que foi embora
  eram indistinguíveis.
- `tools/telemetry_bench/storm_net.py`, o arranjo da tempestade combinada, mais
  o `storm_report.py` e dois modos de falha no sink (`never_read`,
  `tls_bigrecord`).

### Curvas de calibração: até 5 pontos por grandeza

A calibração cresce de um offset constante para uma **curva de correção de até
5 pontos (bruto → referência) por grandeza**, editada no diálogo de slot do
`/config`. A correção interpola linearmente entre os pontos e segura o offset
das pontas além delas; um ponto é exatamente o offset constante de sempre, e
zero pontos é o estado explícito "sem correção — padrão do sensor". Os pontos
podem ser digitados de uma tabela de bancada ou captados da leitura ao vivo
(bruto vazio capta no momento do salvar).

Com 3+ pontos a interpolação é escolhível por grandeza: **Reta** (linear por
partes) ou **Suave** — uma cúbica monótona (Fritsch–Carlson) sobre os offsets,
que dobra pelas âncoras sem jamais ultrapassá-las e aplaina ao encontrar as
zonas seguradas. Linhas suaves levam uma célula `cub` depois do nome no
`calib.csv`; a API aceita `{"m":"cub","p":[[bruto,ref],…]}` além da forma de
array simples (reta).

As correções agora se aplicam à **média filtrada em vez de cada amostra
bruta**: a rejeição de outliers passa a operar sempre sobre valores físicos e
uma correção editada vale na hora, sem atravessar uma janela de 10 amostras.
Para offsets constantes a aritmética é idêntica — os valores lidos não mudam.

O `/calib.csv` põe tudo depois do nome, como células CSV planas:
`key,id,name,bruto,ref[,bruto,ref,…]` — um número por coluna, direto na
planilha, sem coluna dedicada de offset. As formas de linha se distinguem
pela contagem de campos: arquivos legados de 4 colunas
(`key,id,offset,name`) continuam lendo como o offset constante que sempre
foram (e um offset sem âncora carregado adiante ainda é gravado nessa forma —
não há pontos em que ele possa virar); `key,id,name` é linha de identidade.
Firmware antigo lendo uma linha de pontos vê ausência de correção, nunca uma
correção errada. Remover uma correção apaga a linha
(linhas de DS18B20 ficam — são também o banco de identidade ROM→ID/nome).
`POST /api/calib` aceita `"cal":{"<canal>":[[bruto,ref],…]}` com validação
completa antes de qualquer gravação; o `GET /api/calib` ganha `raw`, `min`,
`max` e `pts` por canal.

**Mudança de comportamento:** os campos legados `refs`/`refTemp` (páginas em
cache) agora definem uma correção absoluta de um ponto na leitura bruta atual,
em vez de acumular `offset += ref − leitura`. Repetir a mesma referência virou
operação idempotente — que é o que todo mundo sempre esperou.

O editor de slot desenha um **mini-gráfico ao vivo por grandeza**: a linha
tracejada é o padrão do sensor (correção zero), a curva é a correção staged
com suas âncoras, simulada na interpolação escolhida enquanto você digita.
O **pareamento do DS18B20 virou automático**: um sensor provisionado pelo
editor tem o ROM lido e gravado no `calib.csv` no reinício que segue o
Salvar e Reiniciar, migrando qualquer correção salva enquanto estava sem
par; a verificação de ROM passa a proteger contra troca de sonda.

### Corrigido

- **A caixa "Alarmes ativos" do editor de slot nunca salvava.** Todos os
  walkers do `commit_all` fatiavam elementos no primeiro `}`, então qualquer
  chave staged depois do objeto aninhado `lim{}` — onde mora o `al` — era
  truncada em silêncio e mantinha o valor gravado. Os walkers de JSON
  artesanais agora casam chaves por profundidade (cientes de aspas), que é
  também o que permite o payload de calibração carregar arrays de pontos
  aninhados.

## v2.0.1-alpha (2026-08-01)

O histórico passa para o V5: um formato de série temporal comprimido e
autodescritivo cujo caminho quente nunca toca a flash. O dispositivo grava um
dia em 7,6 KiB em vez de 10,6, guarda quatro meses na mesma partição em vez de
menos de três, e responde um gráfico de 30 dias a partir dos envelopes dos
blocos em 187 ms — uma consulta que o formato anterior não conseguia terminar.

> **Faça backup antes.** O V5 não lê V4. No primeiro boot depois desta
> atualização, `/history` é varrido de tudo que não for `.h5` e o histórico
> recomeça vazio. Baixe seus arquivos `.sim4` antes de atualizar e converta no
> computador com `python3 tools/history_v5.py --convert-v4 ent.sim4 sai.h5`.

> **A atualização pelo ar não funciona nesta versão, e também não funcionava na
> 2.0.0-alpha.** O staging aborta no meio do upload e o dispositivo reinicia;
> nada em `src/ota/` mudou nesta entrega. Pior: um staging que falha apaga o
> sistema de arquivos, porque a área de staging *é* a partição do LittleFS.
> Grave por USB até isso ser corrigido. Ver `docs/test_reports/`.

### O caminho quente parou de escrever na flash

A flash do RP2040 é XIP: todo program ou erase significa congelar o Core 1 e
rodar o Core 0 com interrupções desligadas, e são essas janelas que o trabalho
de estabilidade do último mês vem perseguindo. O V4 gravava um registro por
amostra — ~1440 escritas por dia, agrupadas em ~360 janelas de lockout.

O V5 mantém a hora na RAM. `writeHistoryEntryV5( )` é um `memcpy`. A flash só é
tocada quando um bloco fecha (60 registros), na virada do dia, quando o conjunto
de sensores muda, e a cada dez minutos para um snapshot `.wip` que limita a
perda por queda de energia a essa janela. Cerca de 168 escritas por dia em vez
de 1440.

### Os gráficos desenham os picos em vez de passar por cima deles

Faixas longas eram decimadas: um registro a cada N, e o que caísse entre eles
não era desenhado. Um pico de um minuto num mês tinha cerca de uma chance em 72
de aparecer.

O cabeçalho de um bloco V5 carrega o mínimo e o máximo reais de cada canal
naquela hora, então uma faixa longa emite esses valores — dois pontos por bloco,
sem ler payload. O extremo *é* o ponto; não há como perdê-lo na amostragem. O
gráfico de 24 h lê em 5,8 ms por esse caminho contra 107,6 ms decodificando cada
registro, e 30 dias respondem em 187 ms. `?mode=decode|envelope` força qualquer
um dos dois.

### Trocar um sensor deixou de custar o dia

Um `.sim4` congelava o schema no cabeçalho do arquivo, então mudar a identidade
de um sensor obrigava a recriar o arquivo do dia e perder o que havia nele — daí
o `confirm` exigido pela CLI — ou a rodar uma migração streaming para carregar
os registros. O V5 grava um segundo chunk SCHEMA no mesmo arquivo e segue; os
blocos anteriores continuam legíveis sob o schema que valia quando foram
escritos. O `sensor reschema` e o endpoint web de rebind mantiveram suas
assinaturas e deixaram de ser destrutivos.

### Os timestamps agora são corrigidos de verdade

O `handleTimeSync` registrava "corrigindo timestamps" e em seguida "timestamps
corrigidos", com `/* V4: variable-length records — in-place correction
unsupported. */` entre os dois. Tudo que fosse gravado antes de o NTP subir
ficava com o relógio provisório para sempre, e o log dizia o contrário. No V5 o
único carimbo absoluto é o `t0` do cabeçalho de cada bloco, então a correção é
uma reescrita em fluxo que toca quatro bytes e um CRC por bloco, limitada aos
blocos que o boot atual escreveu.

### Corrupção custa uma hora, não um dia

Cada bloco carrega o próprio CRC e decodifica sozinho. Um bloco corrompido é
pulado e o resto do dia é servido; um arquivo corrompido é pulado e os outros
dias são servidos. Dez corrupções injetadas — payload, caudas, `t0`, CRC do
SCHEMA, magic, `nCh`, truncamento — produziram zero reboots e zero respostas
inválidas. No V4 uma quebra na cadeia de deltas comprometia o resto do dia.

### Os arquivos são legíveis sem o firmware

`python3 tools/history_v5.py --dump-csv dia.h5` decodifica um arquivo do
dispositivo apenas com o documento do formato: o chunk SCHEMA diz quais canais
existem, o que cada um mede e em que escala. A mesma ferramenta converte
arquivos legados (`--convert`, `--convert-v4`), reporta compressão (`--stats`),
gera histórico sintético (`--synth`) e roda os vetores de teste do próprio
formato (`--selftest`).

### Menor, e com muito menos RAM estática

A RAM estática cai 44 060 B e o maior bloco contíguo de heap cresce 63%
(29 733 → 48 522 B), que é o número que importa para o TLS do BearSSL. Isso não
vem do formato: o V4 tinha cinco cópias do laço de decodificação — gráfico web,
export CSV, bundle, telemetria, preload, gráfico do TFT — cada uma com seus
~2,8 KiB de estado de codec. O V5 tem um leitor no `StorageManager`. A flash de
código cresce 1 024 B.

### O primeiro boot com um `.wip` em disco travava

Achado e corrigido na bancada antes do lançamento. O `recoverWipV5( )` apagava o
snapshot `.wip` fora do seu `Core1FlashPause`: a pausa ficava dentro do ramo que
decodifica o snapshot, enquanto a remoção acontece em todos os caminhos de saída
da função — inclusive nos que nunca decodificam nada. Remover é uma rajada de
erase, e um erase com o Core 1 ainda buscando instruções do XIP trava o QSPI, que
é exatamente a regra enunciada no comentário do `FLASH_OP` e quebrada por essa
chamada.

Travou em vez de reiniciar porque o watchdog só é armado na primeira volta do
`loop( )`, ou seja, todo o `setup( )` roda desprotegido. O dispositivo parava com
a tela de boot congelada no passo anterior, USB enumerado mas sem responder, e
sem reboot para autopsiar. Exigia um `.wip` em disco, que só existe depois que o
V5 começa a gravar — por isso não apareceu antes de o formato entrar em uso.

O `sealHourV5( )` tinha o mesmo defeito na sua própria remoção do `.wip`, esse
alcançado em operação normal, onde o watchdog armado o transformaria num reboot
inexplicado. Os dois agora seguram a pausa durante a remoção.

### Também

- `/api/status` reporta os contadores de escrita em flash (`fo`, `fom`, `fot`,
  `f50`). Eles existiam desde o T0.1 mas só eram alcançáveis por uma CLI que a
  imagem de release não carrega.
- `/api/history_multi` reporta `path`, `readMs` e `rejected`, então dá para
  separar o tempo de leitura do dispositivo da latência do Wi-Fi.
- `preloadMinMax( )` lê cabeçalhos de bloco em vez de decodificar o dia, então o
  dashboard não mostra mais extremos de meia-manhã depois de um boot à tarde.
- Quatro códigos de log: `STO_H5_SEALED`, `STO_H5_WIP`, `STO_SCHEMA_MISMATCH`,
  `STO_LEGACY_PURGED`.
- Corrigido: posicionar num instante anterior ao primeiro bloco de um arquivo
  deixava o scanner no fim dele, então uma consulta cujo corte precedesse o
  arquivo o descartava em silêncio.
- `HistoryV4.cpp` saiu do build de release. Os pontos de entrada do V4 continuam
  como fachadas que delegam, para que nada que os chamava pare de compilar.

### Limitações conhecidas

- Os orçamentos de latência do §10 não são cumpridos: 0,28 ms por bloco no
  caminho de envelope contra um orçamento que implica 0,111 ms, e 4,48 ms para
  decodificar um bloco contra 1 ms. O piso é a leitura indexada do LittleFS,
  confirmado por duas otimizações que não moveram o número.
- Sem soak de 72 h e sem a campanha de 20 cortes de energia.
- A luminosidade mantém o canal mas cai para unidades inteiras: ela é 24 bits
  x100 na tabela de canais e os valores do V5 são `int16`. Nenhum sensor a
  produz hoje.

## v1.6.3-beta (2026-07-30)

Salvar a calibração de um sensor podia falhar de forma permanente e, para duas
das três famílias de sensores, não fazia nada desde a v1.6.2-beta. A pressão,
que os caminhos de sensor e de histórico já carregavam há meses, enfim chega ao
painel de calibração e aos extremos do histórico.

> Atualizações pelo ar funcionam da v1.6.2-beta em diante, então esta pode ser
> aplicada por esse caminho. Validada em 20 applies consecutivos, abaixo.

### Um save de calibração podia falhar para sempre

O `calib.csv` tem uma linha VERSION, e o commit só renomeia o arquivo
temporário sobre o real quando a versão nova supera a gravada. A versão vinha do
`getEpoch( )`, que nunca falha de forma visível: sem NTP ele cai no RTC virtual
e, na falta dele, no `SIMUT_BUILD_EPOCH`, uma constante de compilação. Os dois
ficam atrás do epoch real que um save sincronizado anterior gravou.

Num device com relógio dessincronizado, portanto, todo save produzia uma versão
*menor* que a do disco, a comparação falhava, e o commit apagava o `calib.tmp` e
respondia HTTP 500. O sintoma relatado — "o calib.tmp é criado, mas o .csv não é
substituído" — é exatamente isso.

A falha é absorvente: uma vez que a versão gravada ultrapassa o relógio, nenhuma
calibração pode mais ser salva, porque toda tentativa seguinte perde a mesma
comparação. Com o fallback do build epoch isso significava meses. Os carimbos de
versão agora são monotônicos, então um save sempre avança, com ou sem relógio.

A guarda que deveria ter pego isso estava morta. O `/api/calib` se recusa a
rodar quando `isTimeSynced( )` é falso, mas essa função é
`getEpoch( ) > 1600000000` e o `getEpoch( )` nunca devolve menos que isso — a
verificação não tinha como disparar.

### Calibrar um DHT22 ou um BMP280 não fazia nada

Sensores sem ROM 1-Wire são identificados no `calib.csv` pelo serial da placa,
com a letra da grandeza e o hwId do sensor na coluna de id. Ao reescrever o
arquivo, o código separava a chave do início da linha mas deixava a coluna de id
como `<id>,<offset>,<nome>`, que nunca era igual a um id puro. Toda linha por
serial de placa errava a própria atualização, era copiada intacta, e o valor
novo era anexado no fim.

Os leitores param no primeiro match, que é a linha velha do topo. O offset era
gravado certo e nunca lido, e o arquivo crescia uma linha por sensor a cada
save. Introduzido em 4cff8ca, então afeta só a v1.6.2-beta. Arquivos que já têm
duplicatas voltam a uma linha por sensor no próximo save.

### Um upload interrompido deixava um arquivo que ninguém recolhia

O handler de upload não tinha ramo `UPLOAD_FILE_ABORTED`, então uma conexão
cortada no meio deixava o handle aberto e o arquivo parcial na flash. Para o
`calib.csv` isso significava um `/calib.tmp` órfão, e o commit que o resolveria
só rodava a partir dos dois handlers web — nunca no boot.

As duas metades foram fechadas: o caminho de aborto descarta o parcial, e o boot
recolhe um `calib.tmp` estagnado. A recuperação recusa um arquivo truncado em
vez de promover meia calibração sobre um arquivo bom, já que um reset pode cair
no meio da escrita.

### A pressão chega à calibração e aos extremos do histórico

Um BMP280 reporta temperatura e pressão, e nenhuma umidade. A API de calibração
e as estatísticas do histórico foram construídas em torno de temperatura e
umidade, então a pressão dele não tinha onde aparecer: nenhum campo no
`/api/calib`, nenhum input de referência no `/config`, e nenhum selo MIN/MAX no
`/history` mesmo com o gráfico desenhando a série. O offset também não era
aplicado à leitura, então seria só de escrita mesmo que o resto existisse.

### Uma tabela para o que é uma medida

Consertar o acima exigiu editar cinco camadas que mantinham cada uma sua cópia
privada do que é um canal — os switches de prefixo, largura e escala do V4, o
teste de sinal do codec, o whitelist de letras do leitor de calibração, o writer
de linhas, e uma repetição por driver da unidade e do ícone de cada canal. Foi
por isso que a pressão precisou ser adicionada em cinco lugares e ainda assim
não funcionou: uma das cópias era um whitelist que recusava a letra.

O `sensors/SensorChannelTable.h` agora guarda uma linha por grandeza, ligando-a
à sua identidade de armazenamento e a um preset de exibição do
`SensorPresets.h` — um catálogo de 80 unidades que estava na árvore, sem
nenhuma referência, desde que foi escrito. Adicionar uma grandeza é uma linha
mais um bit na máscara de canais do driver.

Os formatos de fio acompanham. O `/api/calib` reporta `channels[]` e aceita
`refs{}`; o `/api/history_multi` reporta `extremes{}`; as páginas iteram em vez
de nomear um campo por grandeza. As chaves fixas seguem em paralelo por uma
release, para que uma página em cache continue funcionando.

O `tools/check_channels.py` quebra o build se uma letra de canal aparecer fora
da tabela, e reporta o que ainda não foi generalizado. Os limiares de alarme
estão nessa lista: o `SensorRecord` ainda tem limites fixos de temperatura e
umidade, então **ainda não existem alarmes de pressão** — isso exige mudança de
schema da configuração gravada e não está nesta release.

### Validado: 20 atualizações pelo ar consecutivas

Cada ciclo subiu uma imagem com um marcador de versão que nenhuma outra tinha,
então "aplicou" é uma versão lida de volta do firmware em execução, não uma
inferência de código HTTP ou de tempo decorrido — toda camada de um OTA reporta
sucesso, tenha ou não trocado alguma coisa.

| etapa | n=20 |
|---|---|
| upload + stage (962.476 B) | 29,2 s |
| apply → web acessível de novo | 47,5 s (45,6–49,8) |
| ciclo completo | 83,2 s (81,3–85,6) |

Os 20 applies foram confirmados por marcador. Nenhum soft panic, nenhum
`APP_CORE1_DEAD`, nenhum reset por watchdog nos 20 boots; heap livre terminou em
55.452 B, sem variação além do ruído.

### Também

- O `SIMUT_BUILD_EPOCH` estava carimbado em 2025-09-20 e comentado como
  2026-07-21. Ele é o relógio de fallback de um device que nunca alcançou o NTP,
  e quanto mais atrasado, pior se comportava a regressão de versão acima.

## v1.6.2-beta (2026-07-27)

O destaque não é um recurso: **o OTA nunca aplicou uma atualização**, em nenhuma
versão publicada, e esta é a release em que ele passa a aplicar.

> **Grave esta por USB.** Quem está na v1.6.1-beta ou anterior roda o applier
> quebrado, então não existe caminho pelo ar até a versão que conserta a
> atualização pelo ar. Deste build em diante o OTA funciona — medido em 21
> atualizações consecutivas, abaixo.

### O OTA reportava sucesso em cada etapa e nunca trocava o firmware

O feed de watchdog do applier era um reboot.

Alimentar o watchdog do RP2040 significa recarregar LOAD no offset 0x04. Em vez
disso, `applier_wdt_feed()` escrevia o bit 31 do CTRL no offset 0x00 — o bit
TRIGGER, que força um reset imediato. `WATCHDOG_CTRL_OFFSET` é 0x00, ou seja,
ele escrevia o mesmo bit no mesmo endereço que `applier_reboot()`. A primeira
chamada, logo após gravar o setor 0 no passo (1a), resetava o chip antes de um
único setor ser apagado ou copiado.

O que isso produz na bancada é indistinguível de um apply bem-sucedido que não
mudou nada: o slot da app mantém o firmware antigo, a causa do reset é watchdog
forçado, a metadata fica em APPLYING e a LittleFS some — destruída não pelo
applier, mas pelo upload, já que o staging divide a partição com ela. Stage,
apply e reboot reportam sucesso. Nada comparava o slot da app com o que foi
preparado, então a versão simplesmente não mudava.

Isso está inalterado desde a v1.4.4-beta, e o mesmo código está na v1.0.0.

Dois outros bugs estavam atrás dele, em linhas que o applier nunca alcançou:

- **O `memcpy` mora no slot da app**, que o passo (1b) apaga. A primeira cópia
  do passo (2) teria executado flash apagada. Substituído por cópia de palavras
  em SRAM; ponteiros voláteis impedem o GCC de reconhecer o laço e chamar
  `memcpy` de novo. Verificado por disassembly: todo alvo de desvio em
  `ota_applier_run` agora resolve para SRAM.
- **`WATCHDOG_SCRATCH4_OFFSET` era 0x18, que é o SCRATCH3.** `applier_reboot()`
  limpava o registrador de rastro que a autópsia de boot lê como `sc3` e deixava
  intacto o magic de watchdog da bootrom. Corrigido para 0x1C.

O boot pós-apply agora calcula o CRC do slot da app contra a metadata e registra
o veredito. O applier também calcula isso, mas roda de SRAM com interrupções
desligadas e não tem como reportar nada, então descartava o resultado — que é
justamente por que três bugs distintos sobreviveram tanto tempo. A verificação
pertence a onde existe logging, e a metadata ainda está em flash nesse ponto.

Falhas do `/api/ota/apply` agora chegam ao usuário: a página de firmware não
checava nenhuma das três respostas e engolia as próprias exceções.

### A verificação pós-apply comparava um CRC contra o comprimento errado

O staging reportava o tamanho com padding nos dois campos de tamanho da
metadata, então o par (tamanho, CRC) nunca descrevia os mesmos bytes. O
`stage_session_end` completa a última página de 256 B com 0xFF e o
`bytes_written` conta esse padding — corretamente, pois é o que o applier tem
que copiar — enquanto o CRC cobre só os bytes que chegaram. Verificar o CRC de
957.460 bytes contra o CRC de 957.696 falha numa cópia byte a byte perfeita.

A sessão agora rastreia `bytes_received` separadamente e o reporta como tamanho
descomprimido, dando aos dois campos os significados que a struct já
documentava. O `dsize` e o `dcrc` do `/api/restore` passam a descrever a mesma
faixa.

Isso sozinho não ajudaria uma atualização preparada por um build antigo, que é
toda atualização para esta versão: o comprimento com padding é tudo que a
metadata dele carrega. Então a verificação pós-apply aceita qualquer
comprimento dentro da página final.

### Validado: 21 atualizações pelo ar consecutivas

Medido na bancada (Pico W, `pico_w_release`), 21 ciclos de stage+apply em
sequência. Cada ciclo preparou uma imagem com uma string de versão distinta, de
modo que "aplicou" é lido de volta do dispositivo em vez de inferido de um
status HTTP:

| Etapa | Tempo |
|---|---|
| Upload + stage (957.500 B) | 29,2 s ± 0,07 (32,1 KiB/s) |
| `/api/ota/apply` → 202 | 0,1 s |
| Janela do applier (erase + program) | 25,1 s ± 0,10 |
| Reboot → imagem verificada | 9,4 s ± 0,06 |
| **Interface web inacessível** | **48,4 s** |

21 de 21 aplicaram. O comprimento verificado voltou como exatamente 957.500 B
todas as vezes, o snapshot de configuração sobreviveu a todas as reformatações
com o Wi-Fi voltando sozinho, o heap livre variou 24 B na corrida inteira, e
nenhum boot produziu soft panic — sob a carga de flash mais pesada que o
firmware tem.

O download é a parte lenta, e a interface web fica inacessível por cerca de 50
segundos. Dois terços disso são o applier; o resto é o Wi-Fi reassociando.

### Tela branca depois de ajustar o offset do display

Dois escritores independentes disputavam o display. O bloco de auto-ajuste da
calibração de toque chamava `saveConfiguration()`, que escreve flash, cerca de
190 linhas depois do `startCore1()` — e o boot adia o Core 1 exatamente para que
o trabalho de flash siga o caminho de núcleo único. O `setDisplayOffset()`
também repintava as margens incondicionalmente, então o Core 0 desenhava no TFT
enquanto o Core 1 renderizava.

O sintoma era tela em branco no boot seguinte ao ajuste do offset, que parecia
configuração corrompida mas era escrita rasgada.

### A coluna de uptime do log sempre marcava zero

O `CompactLogRecord` guardava uptime como `millis() / 3600000` num `uint16_t`.
Qualquer dispositivo que reinicie mais de uma vez por hora escreve 0 em todo
registro que já fez, o que numa placa de bancada é todo registro. A coluna não
estava sem implementação — tinha uma, numa resolução que arredondava toda a
faixa útil para zero.

O uptime agora é em segundos sobre 24 bits, reaproveitando um byte `reserved`
que era escrito como 0 e lido por ninguém, de modo que o registro continua com
12 bytes. O `setUptimeSec` satura em vez de dar a volta, porque um número grande
truncado leria como um número pequeno plausível. O dump serial, o decodificador
do `/api/logs` e a exportação CSV acompanham, e essa coluna muda de `uptime_hr`
para `uptime_sec`.

**Arquivos `.blog` antigos decodificam diferente.** Não há marcador de versão no
formato, então um registro escrito antes disso lê seu antigo campo de horas como
segundos — na prática 0, que é o que aquele campo já continha.

Flash 944.600 -> 945.464 B (+864).

## v1.6.1-beta (2026-07-27)

Correção única, publicada sozinha porque o sintoma é silencioso e o gatilho é
uma ação de manutenção comum.

### Trocar o pacote de idioma quebrava todas as traduções até reiniciar

O `/api/lang` transmite o bloco `@WEBDICT` direto do flash usando um intervalo
de bytes que o parser grava **uma vez, no boot**. Ao subir um pack novo pelo
`/files`, esses números continuam descrevendo o arquivo anterior: o handler
busca no deslocamento velho e envia o comprimento velho, então a resposta
termina no meio de uma string.

JSON inválido faz o `JSON.parse` do navegador lançar, e isso derruba o
dicionário **inteiro** — as ~400 chaves caem para o inglês, não só as que
mudaram. Nada é registrado no log; a interface simplesmente troca de idioma.
Medido na bancada: um pack 143 B maior que o residente gerou 15.868 B de corpo
truncado.

O intervalo passou a ser varrido do arquivo a cada requisição, em vez de
herdado do boot. Varrer em vez de recarregar o pack é deliberado — um reload
custa ~28 KB transitórios e reescreve as strings que o Core 1 está lendo no
display, enquanto este endpoint nunca toca o dicionário residente e só precisa
do intervalo. Uma passada por ~28 KB de flash, num endpoint que o cliente
mantém em cache por cinco minutos.

Verificado contra a falha real: um pack com o bloco deslocado em +105 B, e o
`/api/lang` seguiu válido com as 404 chaves, sem reiniciar.

**Quem deve atualizar.** Quem sobe ou troca um `.lng` pelo `/files`. Se você
nunca fez isso, a v1.6.0-beta se comporta igual — o intervalo obsoleto só fica
errado depois que o arquivo embaixo dele muda.

Flash 944.408 -> 944.600 B (+192). Nenhuma outra mudança.

## v1.6.0-beta (2026-07-27)

Release do modelo universal. Três casos especiais faziam o papel de regras
gerais, e cada um chegava ao usuário como defeito, não como escolha de projeto:
um slot que não dava para liberar, um sensor cuja pressão nunca aparecia e uma
camada de histórico carregando dois formatos onde só um é gravado.

Bump de minor, não de patch: o `SensorFormat` mudou de forma, existe
`TYPE_BMP280` e o padrão de fábrica não provisiona mais slot nenhum.

> **Ainda em teste.** Verificado na bancada contra hardware real (2 DS18B20,
> 1 DHT22, 1 BMP280), mas sem soak longo.

### O slot 10 deixou de ser "o sensor ambiente"

Oito pontos tratavam um slot como especial: o `/api/calib` emitia um objeto
`ambient` fixado em `cfg.sensors[10]`; o `/alarms` aceitava `idx == -1` como
apelido dele; o `/api/config` publicava o hwId dele como `ambHwId`; os tokens de
telemetria `{tAMB}`/`{uAMB}`/`{pAMB}` resolviam a chave por ele; o gráfico do
histórico usava o sensor 10 como padrão e enxertava o `ambientHum` do registro
só nesse slot; e o `loadDefaults` o pré-ativava como DHT22 chamado `AMB` no GP10.

O último é o que o usuário encontra. O seletor de pinos do `/config` desabilita
todo GPIO pertencente a um slot ativo, então um sensor fantasma ligado a nada
tornava o **GP10 inatribuível**, e um reset de fábrica o trazia de volta. **Os
16 slots agora nascem vazios e não reivindicam GPIO.**

Três defeitos apareceram dentro desse trabalho:

- **Nenhum offset de calibração chegava a um sensor rodando.** O
  `loadAndCalibrateSensors` aplicava os offsets e *depois* chamava o
  `initRuntimeSensors`, que reconstrói o vetor com todos os offsets zerados.
- **O `/api/calib` indexava os vetores por GPIO e os lia por número de slot.**
  Isso só coincide enquanto cada slot estiver no GPIO do próprio número — o
  layout de fábrica, e mais nada.
- **Uma calibração de umidade por placa.** Offsets de peças sem ROM eram um par
  de linhas único do dispositivo, achado por "primeira linha começando com
  `t`/`u`" e aplicado ao "primeiro DHT22 da lista". Uma placa com dois DHT22
  calibrava exatamente um, e qual dependia da ordem dos slots. As linhas agora
  levam o hwId do próprio sensor; **o formato do `calib.csv` não mudou**.

### Um BMP280 não é um BME280

O `sensorHasChannel()` era `channel < valueCount` — os canais tinham que ser um
prefixo contíguo do enum. Um BMP280 mede **temperatura e pressão e nenhuma
umidade**: `{CH_TEMP, CH_PRESS}` com um buraco em `CH_HUM`, coisa que uma
contagem não expressa. Por isso as duas peças dividiam `TYPE_BME280`, que
declarava umidade e era *exibido* como "BMP280" — qualquer que fosse o seu chip,
o firmware errava sobre um dos dois.

- O `SensorFormat` passa a carregar uma **máscara de canais**; `values[]` é
  indexado por canal.
- **`TYPE_BMP280`** é um tipo distinto, acrescentado ao fim para não deslocar
  nenhum valor já gravado.
- **O chip decide.** O `initRuntimeSensors` adota o que a peça informa
  (0x60 = BME280, 0x58 = BMP280) e persiste, então um slot existente se corrige
  sozinho no boot seguinte, sem ninguém precisar saber qual chip soldou.
- A umidade fantasma sumiu do schema V4, do `/api/calib`, do `/api/alarms`, do
  `/api/config` e do gráfico. **A pressão ganhou série e eixo próprios** no
  `/history` — ela fica na casa de 1000 hPa e achataria °C e %RH se dividisse
  eixo com qualquer um dos dois.
- O catálogo de tipos saiu de uma faixa `t <= TYPE_BME280` para lista explícita.
  Aquela faixa excluía todo tipo acrescentado depois dela, e só evitava o
  `TYPE_UNKNOWN_ACTIVITY` por acaso da ordem do enum.

### Histórico: v2/v3 removidos, V4 é o único formato

O `HistoryCodec` foi apagado — 441 linhas, mais cinco leitores `.bin`, a suíte
de testes de 653 linhas e o conversor v1→v2. **O escritor legado estava sem
chamadores havia várias releases**: um codec de delta inteiro vivo para não
servir ninguém.

Dois leitores não podiam simplesmente sumir, porque liam *só* o formato legado:
o pacote de export `.simx` e o contador de pendentes da telemetria. Ambos foram
reescritos sobre V4 — senão o export sairia vazio em silêncio e o contador do
dashboard marcaria zero para sempre. O `getLastRecordedTimestamp`, que semeia a
RTC virtual no boot, tinha o mesmo problema.

O `BinaryHistoryRecord` perde `ambientTemp`/`ambientHum`. Nada os escrevia desde
que o V4 entrou; sobreviviam só como os dois primeiros campos do layout v2/v3.
A struct agora é o que o nome nunca disse: um **carrier em RAM**, não um formato
de arquivo.

> **Migração.** Arquivos `.sim4` não foram tocados e seguem funcionando.
> Qualquer histórico `.bin` ainda no dispositivo fica ilegível para este
> firmware — converta antes com `tools/history_v2_to_v4.py`, que foi mantido
> exatamente para isso.

### Falhas silenciosas que passaram a falar

- **Uma falha de claim de PIO desabilitava uma família inteira de sensores sem
  dizer nada.** Os dois drivers descartavam o retorno do `begin()`, e o `DHTBus`
  só consulta o próprio `_isInitialized` no destrutor — então o `requestReading`
  seguia acionando a state machine 0 do `pio1`, que este firmware nunca possuiu
  e que num Pico W é compartilhada com o rádio CYW43. Sintoma: toda leitura
  daquele tipo dá timeout, em todo pino, sem nada no log.
- **O prefixo `STH` sequestrava IDs escolhidos pelo usuário.** O auto-ID
  regenerava qualquer hwId começando com essas três letras — marcador de um
  esquema antigo que o gerador atual nunca emite, então a cláusula só podia
  acertar um ID digitado por uma pessoa. Grave `STH0001`, reinicie, receba
  `DHT2202` de volta.
- **O template padrão de telemetria publicava só o timestamp.** Era
  `{"ts":{TS},"tAmb":{tAMB},"hAmb":{uAMB}}`, e os dois tokens AMB liam colunas
  do registro que nada escrevia desde o V4.

### Manual do sistema de arquivos, e um favicon que para de sumir

- **`/README.txt`** é escrito pelo firmware no boot: um mapa de cada diretório e
  arquivo, o que vai onde, e as armadilhas (`uploadfs` reformata a partição; o
  schema V4 congela quando o arquivo do dia é criado). Não dá para apagar pelo
  `/files` — a linha não tem caixa de seleção e o `/api/delete` responde 403.
- **`/themes` e `/web` passam a ser criados no boot**, e cada pasta de sistema
  carrega uma nota de uma linha. Essa nota sustenta a pasta: o LittleFS remove
  do listing do pai qualquer diretório sem entradas, então um `/themes` vazio
  não existia para o gerenciador de arquivos — **e pasta que não se vê é pasta
  onde não se sobe tema**.
- **O favicon voltou para a imagem do firmware.** Foi para o LittleFS quando a
  folga real de flash era de 660 B; isso deixou de ser a restrição, e a cópia no
  sistema de arquivos sumia a cada `system format`. O gerador que deveria
  produzi-lo lia um diretório inexistente e escrevia fora do `build_src_filter`
  — não poderia ter funcionado, e nada o chamava.

### Interface web

- **O dashboard nunca dizia "sincronizado".** A linha sob o contador de
  pendentes era markup estático com `data-i18n`, escrito uma vez no
  carregamento e nunca revisitado. O `/api/status` ganhou `tel` para distinguir
  os quatro estados; sem essa flag, `pending == 0` significa tanto "não há mais
  o que enviar" quanto "nada é enviado nunca".
- **O IP era apagado no celular**, não ajustado: o breakpoint de 640 px tinha
  `.status-pill span { display: none }`.
- **O export CSV estava quebrado para todo mundo.** O leitor no navegador
  testava `recordSize !== 28` contra um firmware que emitia 74.
- **Mínimos e máximos do histórico só aparecem com um sensor selecionado** — o
  servidor mede os extremos sobre todos os pedidos.
- **Série sem nenhum ponto numérico não é mais desenhada**, que foi o que tirou
  a linha fantasma de umidade do BMP280.
- **Os botões da `/files` ficaram uniformes** e os nomes de arquivo viraram
  links de download.

### Números

| | v1.5.6-beta | v1.6.0-beta |
|---|---|---|
| Flash (`pico_w_release`) | 939.096 B | 944.408 B |
| Folga real | 93.348 B | 89.092 B |
| RAM (`.bss` + `.data`) | 120.492 B | 122.540 B |
| Testes nativos | 141 | 119 |

O favicon responde por 11.047 B do delta de flash e a remoção do v2/v3 devolve
8.888 B. A RAM cresce por um `HistV4State` no `getLastRecordedTimestamp` — os
registros V4 são delta-encoded, não há como saltar ao fim de um arquivo, e a
pilha do RP2040 tem ~4 KB. A contagem de testes cai porque a suíte de 653 linhas
do codec v2/v3 foi junto com o codec.

### O que não foi verificado

- Sem soak longo. A classe R1 (corrida do heartbeat do Core 1 sob carga pesada
  de flash) continua inalterada e aberta.
- O caminho do BME280 não foi testado contra hardware real — a bancada tem um
  BMP280. A separação é simétrica, mas só um lado foi exercitado.
- O `pico_w_alpha` não linka (`DisplayManager::showTouchSensitivity` indefinido).
  Pré-existente, sem relação, e confirmado contra a v1.5.6-beta.

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
