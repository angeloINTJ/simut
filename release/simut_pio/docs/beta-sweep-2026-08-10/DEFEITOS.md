# Defeitos — varredura de promoção à beta, 2026-08-10

Fila de execução herdada de 09/08: resíduo do `users.actions`, varredura de
upload/restore, soak A6. Um defeito por seção, com repro e estado.

Imagem sob teste: `pico_w_release` v2.1.0-beta, `md5 1ce3fd62…`, gravada no rig.

---

## D-B1 · `/api/restore` gravava antes de conferir quem pedia · **CORRIGIDO**

**Assinatura** nenhuma. É o pior traço possível: a rota respondia **403** e o
arquivo aparecia no sistema de arquivos assim mesmo.

**Onde** `handleApiRestoreUploadData`, ramo não-`stage`. A checagem de permissão
existia **só** em `handleApiRestoreFinish`, e o framework chama esse handler
**depois** que o corpo multipart inteiro já passou pelo callback de upload. O
feed em modo APPLY grava cada entrada direto no caminho final
(`on_path_complete`: "sem rename"; `RestorePhase::CONTENT` escreve conforme os
bytes chegam).

**Alcance** qualquer caminho que o formato de backup nomeie — `/config`,
`/calib.csv`, `/history`, packs de idioma. `path_is_safe` recusa `..` e mais
nada. **Sem cookie nenhum.**

**Repro** `scratchpad/restore_gate_probe.py` — forja um `.bkp` de uma entrada
com o chip id do próprio aparelho (o `on_header_complete` recusa chip alheio) e
faz `POST` anônimo. **O veredito é a presença do arquivo, nunca o código HTTP**:
o 403 sempre veio.

**Correção** checagem no `UPLOAD_FILE_START`, com latch `_restoreRejected` para
as fases seguintes ficarem inertes — a forma que o `/api/upload` e o ramo
`op=stage` sempre usaram. O feed do watchdog foi para **antes** do latch: um
upload recusado ainda transmite o corpo inteiro pelo callback, e não alimentar
transformaria um 403 em reboot.

**Validação** antes: 403 + `/beta_probe.txt` gravado. Depois: 403 e nada.
Caminho legítimo intacto — validate autenticado de backup real de 807 KB /
106 arquivos responde `st:0`, `fx=0` nos dois.

> **A lição passa desta rota.** Numa rota com callback de upload, permissão
> conferida no handler final é permissão conferida tarde demais. Varredura
> feita em todas as rotas registradas: nenhuma outra faz trabalho antes do
> `getAuthPerms( )`.

---

## D-B2 · A recusa do restore derrubava o aparelho · **CORRIGIDO**

**Assinatura** `SYS_BOOT ctx=219` = `HW WATCHDOG C0=[WEB_POLL]`.

**Introduzido por D-B1.** Antes do gate, um restore não autenticado não era
recusado — era executado. Fechar o buraco tornou o caminho de **recusa**
alcançável por qualquer um, e é ele que derruba: repetir
`POST /api/restore?op=apply` anônimo reiniciou o aparelho na **12ª** requisição
numa corrida e na **31ª** noutra.

**Onde** o `403` responde não-chunked e retorna. Nada da disciplina de aborto
cobre a cauda; o framework aposenta o cliente dentro do `handleClient( )` com um
`stop( )` seco cuja espera de ACK se renova a cada progresso e nunca alimenta o
watchdog. É o **terceiro caminho** para o parque do D-NS2.

**Correção** `drainOrDrop( )` antes do return, igual ao `safeStreamFile( )` e ao
`handleApiBackup( )`.

**Validação** 100 recusas seguidas: **0 reboots**, 100/100 com 403, nada
gravado, `fx=0`.

**Vizinhos varridos na mesma passada, todos limpos**: upload anônimo recusado
com 403 (40×), upload autenticado com nome inválido (40×), `/api/logs` +
`/api/status` martelados juntos (51×).

### Duas atribuições erradas, e por que registrar isso

1. **"É o `LOG_CODE` que o gate pôs dentro do callback multipart."** Tirar a
   linha deu **40 requisições limpas** — leu-se como decisivo. Era falso
   negativo: o reboot voltou na 31ª com a linha em outro lugar.
2. **"É o `/api/logs`."** 51 buscas seguidas, nada.

> Um evento que dispara **1 vez a cada 12–40 tentativas** não se descarta com
> uma corrida limpa de 40. Ou várias corridas, ou uma contagem que sustente a
> taxa. Vale para toda caça a evento estocástico neste projeto.

### Como ler a causa de um reboot sem serial

`GET /api/logs` devolve o log binário, 12 B por registro, `<IHHhBB` = epoch,
uptimeLo, **code**, **ctx**, flags, uptimeHi. `code==1` é `SYS_BOOT` e o **ctx
carrega o veredito da autópsia**:

| faixa | significado |
|---|---|
| `0` | sem causa registrada = perda de energia / reset externo |
| `100 + core` | soft panic (heartbeat do outro core parado) |
| `200 + módulo` | **HW watchdog** — 5 STORAGE_WR, 9 CLI, 18 LOOP, 19 WEB_POLL |
| `300 + fase` | soft panic com Core 1 congelado |
| `400` | Core 1 hard fault |

Isso resolve watchdog × perda de energia sem script acampado na serial.

---

## D-B3 · `users.actions` descartava ações em silêncio · **CORRIGIDO**

**Onde** `WebManager_Commit.cpp`. Duas camadas.

**(a) A família espaço-no-JSON, nunca varrida aqui.** `type` e `name` eram lidos
por agulhas com a aspa embutida (`"type":"`), então um payload com o espaço que
o JSON permite depois dos dois-pontos não casava, `type` voltava vazio, os dois
ramos eram pulados e a ação inteira evaporava sob um 200. Agora usam o
`jsonExtractStringValue`, para onde os primos float, string e bool já tinham ido.

**(b) Toda recusa era um `continue` seco** — nome inválido ou reservado,
duplicado, tabela cheia, `del` apontando para vaga inexistente. A página não
oferece verificação nenhuma no cliente, então acrescentar um quinto usuário era
clicar em Salvar & Reiniciar, esperar o reboot e descobrir a conta ausente.

**Correção** cada caso se nomeia no array `rejected` que a seção sys já usava:
`users.name`, `users.dup`, `users.full`, `users.id`, `users.type`. `perms` fica
preso aos dez bits que a página consegue marcar (`PERM_ALL_BITS`). O
`rejectField` virou idempotente e tudo-ou-nada — a seção pode oferecer a mesma
razão oito vezes, e um token que só coubesse pela metade deixaria o array sem
terminador, um 200 carregando JSON impossível de parsear.

**Validação** 7/7 no rig, um payload espaçado com todos os modos de falha:
`rejected:["t_srv","users.name","users.dup","users.perms","users.id","users.type"]`,
a ação válida criou o usuário — que é o que prova que o payload espaçado passou
a ser lido — e as contas existentes e o `t_srv` sobreviveram intactos.

---

## D-B4 · Campos de texto da sys truncavam calados · **CORRIGIDO**

Iam direto para `safeCopy`, que corta para caber: um servidor de 70 caracteres
virava um de 63 e o commit respondia ok. Agora passam pelo `isValidCfgString`,
o mesmo portão que a CLI sempre usou, e um valor que não cabe inteiro é
recusado. Vazio continua legal — limpar um template é uma edição de verdade, e
os campos onde vazio significa "mantém" dizem isso por conta própria.

---

## D-B5 · `save_sys` aceitava tema inexistente · **CORRIGIDO**

Respondia ok e não mudava nada, então a página não distinguia aplicado de
ignorado. Nada mais acontece nesse handler, então o 400 é atômico por
construção. Validado: `HTTP 400 {"error":"Theme index out of range"}`.

---

## D-B6 · O Core 1 era invisível na imagem publicada · **CORRIGIDO**

O A6 pede "heartbeat/WDT sem regressão", e na imagem de release essa grandeza
não era legível: heartbeat, launches e os três contadores de kill chegavam só
ao `show metrics`, comando que o perfil de release não carrega. Um display
travado, de fora, é idêntico a um saudável — um soak ligado nessa imagem
relataria PASS atravessando uma morte do Core 1.

`/api/status` agora carrega `c1a` (idade do carimbo que o Core 1 escreve a cada
volta do laço), `c1n`, `c1kl`/`c1kh`/`c1kq` e `c1s`. Custo: 168 B contra 52,5 KB
de folga real.

---

## D-B7 · `pico_w_alpha` não linkava · **CORRIGIDO**

O `DisplayManager_Alpha.cpp` carrega um corpo vazio para cada tela que a
variante HD44780 não desenha; o `showTouchSensitivity` chegou com a tela dele e
nunca ganhou o seu. A variante sem touchscreen nenhum parou de linkar na única
tela que jamais poderá mostrar, no momento em que `screen touchsens` entrou no
`AppManager_Commands`.

Importa além da bancada: o `build_release.sh` empacota essa árvore no
`simut_alpha_v*.zip`, que é **artefato publicado**. A quebra saía como fonte que
ninguém conseguia compilar. Aberto desde 31/07.

Alpha volta a linkar em 89,0 % de flash.

---

## D-B8 · Um request lento reinicia o aparelho — sem auth · **CORRIGIDO em v2.1.1-beta**

**Isto reabriu o que o D-B2 tinha estreitado, e era mais grave do que as notas
do release v2.1.0-beta diziam — um reboot remoto sem autenticação.**

### Mecanismo, confirmado byte a byte

`WebServer::handleClient` faz o parse do request com `readStringUntil`, que
espera o timeout do cliente **por byte** e o **reinicia a cada byte recebido**
(`Stream::timedRead`). Um cliente que goteja um byte logo abaixo do timeout
mantém o Core 0 dentro da leitura para sempre, e o loop SIMUT alimenta o
watchdog **antes** do `handleClient`, nunca durante. Um único GET lento derruba
o aparelho.

**Autópsia ao vivo** (captura serial camped no boot, o instrumento que faltou
nas 3 campanhas): `C0=[WEB_POLL] C1=[DISPLAY] hp=0 sc3=0x80088013 (219)`.
- `sc3=0x80088013`: byte 0x13 = módulo 19 = WEB_POLL, byte 0x80 = marca válida.
- **`hp=0`**: `scratch[7]` nunca virou 740 → o `handleClient( )` **não retornou**.
  O travamento foi dentro dele, exatamente no `readStringUntil`.

### Repro determinística

`scratchpad/repro_slowloris.py N M` — abre um socket cru e envia um GET
bem-formado a N segundos por byte. **Não é malícia, é latência**: a forma exata
de um cliente meio-aberto ou de uma rede congestionada. O veredito é o uptime
por uma conexão limpa SEPARADA, nunca o sucesso do socket lento.
- **Antes (v2.1.0-beta)**: 1 GET a 3 s/byte reiniciou em ~10 bytes.

### Correção

`patches/webserver_parse_deadline.patch` (5º override, ligado ao `patch.sh`):
um leitor `simutReadLine` que **alimenta o watchdog a cada byte** e limita o
parse inteiro por um orçamento de relógio (`SIMUT_PARSE_BUDGET_MS = 3000`). Um
request que estoura o orçamento volta parcial → linha malformada → o servidor
descarta o cliente. Numa LAN um request real chega em um segmento em <1 ms, o
orçamento só é gasto por stall. Só o `_parseRequest` (linha de request +
cabeçalhos, o caminho sem auth); o `_parseForm` (multipart, pós-auth) fica de
follow-up.

### Validação

- **DoS curado**: 0,4 / 1,0 / 3,0 s por byte → todos dropados, **0 reboots**,
  uptime sempre subindo.
- **Operação normal intacta**: 40/40 status sequenciais, `GET /` (17.961 B) e
  `/api/logs` (16.524 B) inteiros, `fx=0`.
- **Patch versionado == imagem validada**: `diff` do patch aplicado na virgem
  contra a edição no ferro = vazio; restore→patch→rebuild reproduz o `md5`.
- **Aplica em 5.4.3 e 5.6.1** (o `Parsing.cpp` é byte-idêntico entre as duas).

### O que fica aberto

O caso **brando** por trás da mesma autópsia sob **seis clientes concorrentes**
(`docs/netstorm-campaign-2026-08-10/`) — estreitado, não refeito aqui. Aquilo
pode ter uma segunda fonte que não é o parse lento; o soak segue rodando para
pegá-la se existir.

---

<details><summary>Histórico: como estava quando ABERTO (achado pelo soak)</summary>

**Isto reabre o que o D-B2 tinha estreitado, e é mais grave do que as notas do
release v2.1.0-beta dizem.**

**Medido.** Na imagem publicada (`1ce3fd62`), gravada às ~10:20, o aparelho
rodou **130 minutos** e reiniciou às 12:32 com `SYS_BOOT ctx=219` =
`HW WATCHDOG C0=[WEB_POLL]`, nível FATAL, bit TIMER — watchdog genuíno, não
perda de energia (que sai com `ctx=0`) nem toque de gravação (que sai INFO com
`ctx=0`, ver `LogManager.cpp`, ramo `else if (wdReset)`).

**A carga durante esses 130 min era o soak e nada mais**: um `GET /api/status`
a cada 5 minutos. As buildas da migração de framework rodavam em core isolado,
sem tocar no aparelho. Os registros imediatamente anteriores ao boot são
rotina — `APP_HISTORY_SAVED` de minuto em minuto e `SYS_TEL_FAIL` do servidor
fora do ar.

**Por que isso muda o diagnóstico.** As notas do release descrevem esse parque
como o residual "sob seis clientes concorrentes". Não é só isso: ele dispara
**em repouso**. Os três caminhos drenados (D-NS2 ×2, D-B2) eram todos respostas
grandes ou recusas; nenhum explica um aparelho parado.

**Ressalva de leitura, para não repetir o erro do D-B2.** O log retido cobre
apenas 07:45→12:45 de 10/08 e contém **12 sessões encerradas por watchdog** com
módulos variados (`STORAGE_WR`, `CLI`, `LOOP`, `WEB_POLL`). A maioria é dos
testes destrutivos desta própria varredura — as recusas repetidas do D-B2 e as
gravações. **Não se pode tirar MTBF desse número.** A única sessão limpa é a de
130 min descrita acima, e ela é uma amostra, não uma taxa.

**Próximo passo** (na hora em que estava aberto): o soak segue rodando sobre a
mesma imagem e sem carga artificial — o que levou à repro determinística e à
correção acima.

</details>

---

## Aberto, e por quê

- **`pico_w_debug` estoura o flash em ~81 KB.** Já estourava na v2.0.3-alpha
  (81040 B lá, 81208 B aqui) — pré-existente e não é perfil publicado.
- **Parque `C0=[WEB_POLL]` — a causa ociosa foi CORRIGIDA (D-B8 acima).** O que
  resta é o caso brando sob seis clientes concorrentes do
  `docs/netstorm-campaign-2026-08-10/`, que pode ter uma segunda fonte que não é
  o parse lento e não foi refeito aqui.
- **D-NS7, IRQ desligada 68–78 ms contra critério de 60 ms.** Intocado.
- **Soak A6.** Rodando sobre a imagem final. O critério pede 72 h com Wi-Fi
  instável induzido; o que roda hoje é a carga de falha real do servidor de
  telemetria fora do ar (falha de envio a cada 10 s) mais a amostragem web.
  Não é o mesmo que derrubar o AP, e o relatório final deve dizer isso.
