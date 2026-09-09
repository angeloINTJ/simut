# Implementação do plano de correção — auditoria de 07/09/2026

Registro das correções aplicadas sobre `PLANO_CORRECAO_2026-09-07.md`
(8 achados V-01..V-08 + observações O-1..O-3). Base: `feature/simut-air` @
`0110c5a`; a auditoria fora feita em `694ef65`, e nada do que ela apontou tinha
mudado entre os dois.

O plano dizia "base `694ef65`, firmware `2.3.9-beta`". Quando a execução
começou a tag `v2.4.0-beta` **já estava publicada** (recortada em 07/09 ~22h no
merge `9e1ddc3`), então a decisão D-8 está queimada: o release desta rodada
precisa de número novo. Ver a seção *Pendências*.

---

## Resumo

| Achado | Severidade | Decisão | Commit |
|---|---|---|---|
| V-01a — CLI Bluetooth sem lockout | Alta | **Corrigido** | `4c2f2bc` |
| V-01b — pareamento sem consentimento | Alta | **Corrigido em parte** (D-1 opção b) | `f37b8c6` |
| V-02 — senha do rig no repo + ponto cego do scanner | Média | **Corrigido** (rotação pendente do Ângelo) | `17b2a39` |
| V-03 — Air acordado por requisição sem auth | Média | **Corrigido** | `8d6cf87` |
| V-04 — JSON sem escape, SSID/`@NAME` sem validação | Média | **Corrigido** | `0894af6` |
| V-05 — AP de setup aberto | Média | **Corrigido** (D-3: PSK derivada) | `a32d8da` |
| V-06 — pinos do CYW43 aceitos | Baixa | **Corrigido** | `8d043ed` |
| V-07 — `SECURITY.md` descrevia o oposto | Baixa | **Corrigido** | `0b4ab5e` + Fase 4 |
| V-08 — CI sem alpha e Air | Baixa | **Corrigido** | `d740a6d` |
| O-1 — `/download` sem bits de logs/histórico | — | **Corrigido** (D-5: gatear) | `f1bc11c` |
| O-2 — `hwId` sem validação estrita | — | **Corrigido** | `0894af6` |
| O-3 — servidores de bancada em `0.0.0.0` | — | **Corrigido** | `3709519` |

Decisões: **D-1** (b), **D-2** não expurgar, **D-3** derivada e exibida,
**D-4** `ap` continua por BT, **D-5** gatear, **D-6** não agora,
**D-7** `WEB_PREAUTH_MAX_EXT = 3`, **D-8** queimada — ver *Pendências*.

---

## Achados encontrados DURANTE a execução

Três, nenhum no relatório. Os dois primeiros são a razão de o plano mandar
escrever o portão antes de limpar o código.

### 1. O lockout da web abria sozinho depois de 29 falhas 🔴

Ao espelhar o backoff da web no Bluetooth, a fórmula não sobreviveu à leitura:

```c
_loginStates[ls].failCount++;                        // uint8_t sem teto
uint32_t penaltyMs = (1U << failCount) * 1000U;      // multiplica ANTES
if (penaltyMs > 300000U) penaltyMs = 300000U;        // e limita DEPOIS
```

Enumerado no host sobre o domínio inteiro:

| failCount | penaltyMs |
|---|---|
| 1..8 | 2 000 … 256 000 (correto) |
| 9..28 | 300 000 (teto, correto) |
| **29, 30, 31** | **0** |
| 32+ | UB (deslocamento ≥ 32) |

`2^29 × 1000 = 536 870 912 000 = 125 × 2^32`, ou seja o produto dá **zero
exato**. Com penalidade zero, `lockoutUntil = millis()`, `timeReached()` diz
que sim, e a conta está aberta. Quem aguentasse a escalada — ~108 minutos, sem
ninguém olhando — ganhava três tentativas livres, e de 32 em diante o
deslocamento é comportamento indefinido.

Corrigido junto do V-01a: `authLockoutMs()` é uma função pura, compartilhada
pelos dois caminhos, que **limita o deslocamento antes de deslocar** e satura o
contador em `AUTH_FAIL_CAP = 12`. Limitar o produto depois foi o que escondeu o
problema: um produto que dá a volta pode cair abaixo do teto e passar por uma
penalidade curta legítima.

### 2. Um quinto literal de credencial, invisível para o plano

O plano listava quatro arquivos com a senha do rig. O portão novo, rodado
**antes** de limpar qualquer coisa (era esse o controle positivo), acusou um
quinto: `tools/telemetry_bench/campaign.py` tinha
`os.environ.get('SIMUT_WEB_PASS', '<literal>')` — um default de credencial
para a conta `telb` da bancada. O comentário logo acima
registrava que aquela conta **parou de autenticar** — então toda campanha desde
então caía no caminho "Forbidden" que o próprio módulo existe para detectar. Um
default de credencial é vazamento e é mecanismo de falha silenciosa ao mesmo
tempo.

### 3. A fila da CLI derrotaria o portão "só pela USB"

O plano mandava usar `_cmdMgr->wasLastInputFromBt()`. Essa flag descreve a
**última linha analisada**, e uma linha do Bluetooth pode ficar parada na fila
da CLI enquanto o display está ocupado e executar vários inputs depois. No
momento da execução a flag descreveria outra pessoa: o comando do BT passaria e
um comando legítimo da USB seria recusado. A origem agora viaja no
`CliDemand.fromBt`, carimbada pelo `processInput`, e o `do` a propaga para o
comando interno — sem isso, `do system format confirm` chegava ao portão com
cara de cabo.

---

## Detalhes por achado

### V-01a — lockout no Bluetooth, recuperações só pela USB, política na CLI

- `authLockoutMs()` em `SystemDefs_Network.h`: pura, usada pela web e pelo BT.
- `BluetoothManager`: `_failCount` / `_lockedUntil` / `_lockNoticeSent` na RAM,
  de propósito — derrubar e reconectar o RFCOMM não toca no objeto, e é esse o
  laço que o lockout fecha. Durante o bloqueio a entrada é lida e descartada, e
  o aviso sai **uma vez** por bloqueio (repetir por byte transformaria o bloqueio
  em amplificador e avisaria quando a janela reabre).
- Código de log novo `307 SEC_BT_LOCKOUT`, com o atraso em segundos no `ctx`.
- Quatro comandos de recuperação recusados por BT; `ap` continua permitido (D-4).
- `passwordPolicyOk` em `conf user pass` **e** em `user add`.

### V-01b — janela de descoberta (D-1 opção b)

`SerialBT::begin` chama `gap_discoverable_control(1)` e ninguém desliga.
`BluetoothManager::closeDiscoveryIfDue()` desliga 5 min depois do boot, a
partir do `update()` (para o stack já estar de pé) e sob `BluetoothLock` — a
mesma guarda que o SerialBT do framework usa em volta de chamadas BTstack.

**O que isto não faz:** conectabilidade continua ligada. Um celular já pareado
segue funcionando e quem anotou o endereço ainda conecta. A opção (a) —
consentimento no pareamento, `gap_ssp_set_auto_accept(0)` mais código de
confirmação na USB — exige patch de framework e continua sendo o próximo passo
se estas unidades forem para espaço público.

Reabrir a janela é reiniciar. Um `bt pair <s>` custaria parser, entrada de help
e sincronia do `check_cli_help`, na imagem com menos folga — e seria inútil
justamente no caso que importa, uma unidade cuja CLI é o que não se alcança.

### V-02 — credenciais de bancada

Ordem obrigatória cumprida no que depende de código; a **rotação da senha do
rig continua pendente** (ver *Pendências*).

Portão novo em `tools/scan_secrets.sh`:

- **3b — credenciais posicionais.** O passo 3 só via `nome = "valor"`; três dos
  quatro achados eram posicionais. Mesma allowlist do passo 3.
- **3c — os valores em si.** Regra de forma nenhuma pega uma senha usada como
  argumento de função (o quarto achado era `sha256_frontend('<senha>')`). A
  lista mora **fora** do repositório (`~/.simut-secrets-deny`), e no CI o
  arquivo não existe e o passo é pulado por desenho. Reporta `caminho:linha`
  apenas — ecoar o casamento imprimiria o segredo num log de CI ou numa captura
  de tela, e o portão não pode virar o vazamento.

**Controle positivo, nesta ordem:** (a) com os cinco literais no lugar, o portão
falhou, nomeando as quatro linhas posicionais e seis linhas por valor — as
quatro senhas mais dois documentos com o SSID doméstico; (b) depois da limpeza,
`SECRET GATE: clean`; (c) `git grep` do valor antigo e do SSID em arquivos
rastreados devolve zero.

`.gitignore` passou a recusar `*.pem`, `*.key`, `*.p12`, `*.pfx`, `*.jks` e
`.env` ao lado do `*.bkp`. Nenhum arquivo rastreado casa com os padrões novos.

**D-2 — histórico NÃO expurgado.** Depois da rotação o valor é inerte, e o
expurgo reescreveria 800+ commits, quebraria todo SHA citado em docs, CHANGELOG
e análises, e ainda assim não alcançaria as cópias em `refs/pull` do GitHub —
a lição de 16/08.

### V-03 — só requisição autenticada segura o Air acordado

O rearme ficava no funil `safeSendN`/`safeSend_GZ` e no topo do
`getAuthPerms()`, **antes de o cookie ser lido**. Isso responde "tem alguém
aí" para tráfego que não prova nada: um 403 devolvido a um estranho contava, e
um `GET /api/login_init` anônimo em laço segurava o aparelho acordado, rádio no
ar, pelo tempo que o laço durasse (medido em 06/09: 491 s contra 306 s).

Agora rearma onde o chamador é identificado: `getAuthPerms()` no ramo do cookie
que casou, `completeLogin()`, e o ramo de sucesso do Basic auth do `/metrics`.
Pré-login tem **orçamento** de `WEB_PREAUTH_MAX_EXT = 3` extensões por boot,
zeradas por login bem-sucedido. Zerar no sucesso importa tanto quanto o
orçamento: sem isso, um operador cujo navegador gastou as três extensões
carregando a página de login autenticaria e seria hibernado na janela seguinte,
sem como ganhar outra extensão até o próximo boot.

### V-04 / O-2 — escape e validação

Escape na saída: `jsonEscape` cobre aspas, contrabarra e **todo** byte de
controle (`\u00xx`); `/api/network` escapa os sete campos e **confere o retorno
do `snprintf`** (truncar em silêncio produziria exatamente o JSON quebrado que
o commit conserta); `/api/status`, `/api/alarms` e `/api/perms` trocaram o
`.replace("\"","\\\"")` feito à mão, que não cobria a contrabarra.

Validação na entrada: `isValidCfgString` em `net.ssid`, `net.pass` e
`net.ntp_server` (este não tinha checagem nenhuma) com o campo ecoado em
`"rejected":[...]`; `isValidHwId` (1..15 de `[A-Za-z0-9_-]`) na web e na CLI;
`langIdentSanitize` para `@NAME`/`@CODE`, que **descarta** o byte em vez de
recusar o pacote — um pacote com byte estranho no nome ainda é um pacote útil,
e recusá-lo trocaria um problema cosmético por um aparelho sem traduções.

### V-05 — WPA2 no AP de setup (D-3)

Chave **derivada**, não configurada: SHA-256 sobre o id único da placa mais uma
string de domínio, mapeada para 10 caracteres de `[A-HJ-NP-Z2-9]` (~50 bits).
Derivar em vez de configurar dá três coisas que um campo não dá: modo AP é o
que um aparelho **não configurado** faz no boot; a chave é estável por placa,
então uma etiqueta na caixa sobrevive a um factory reset; e não custa campo de
flash, migração nem bump de `CONFIG_VERSION`.

**Não é segredo, e o cabeçalho diz isso**: quem lê o id da placa recalcula a
chave, e `show system info` imprime esse id. Sobe a barra de "está no alcance
do rádio" para "recebeu a chave", que é a barra que uma rede de setup deve ter.

Distribuição é o console e o display. `startApMode` também imprime a chave no
canal por onde o comando chegou — `ap` é o comando de recuperação que continua
valendo por Bluetooth (D-4), e por ali o print da USB é invisível.

**Desvio deliberado do plano:** o display mostra `PSK XXXXXXXXXX` como sufixo
não traduzido da linha de AP que já existia, **não** como chave de tradução
nova. `@DICT` é posicional e o `check_lang_packs` exige exatamente
`TR_KEYS_COUNT` linhas, então uma chave nova **rejeita todo `.lng` já em
campo** — e pacote rejeitado derruba a UI inteira para o inglês, num aparelho
cuja rede é justamente o que acabou de falhar. "PSK" é sigla e o valor é
aleatório: não havia o que traduzir.

`SIMUT_AP_OPEN` (default 0) mantém o AP aberto para bancada, e o `beginAP`
avisa no console quando está ligado.

### V-06 — pinos do CYW43

`airPinValid()` era `pin <= 29`, que é o alcance do RP2040 e não o que a placa
deixa livre. 23/24/25/29 são o barramento do CYW43 (WL_ON, dado SPI
compartilhado, CS que também aciona o LED, ADC3/VSYS). A regra fica no
`airPinValid()` porque o `airSanitise()` já a chama no load: um `air.bin`
forjado ou restaurado de backup antigo tem o pino devolvido ao default em vez
de acionar uma linha do rádio. O handler recusa também um GP já usado pela
alimentação de sensor ou por um sensor ativo.

### O-1 — `/download` por caminho (D-5)

`PERM_FILE_READ` era o portão inteiro, então uma conta com "ler arquivos" e
mais nada puxava `/history/*.h5` e `/system.blog` — os dois conjuntos que a
página de usuários apresenta como permissões separadas e revogáveis.

### O-3 — servidores de bancada

Bind padrão `127.0.0.1`; `bench.py` passa `--bind 0.0.0.0` explicitamente
(o aparelho está na LAN). Log do `air_telemetry_server.py` criado com modo
`0600` **antes** da primeira escrita, e cabeçalhos `Authorization`/`X-Api-Key`
redigidos. O coletor de dentro do `air_test_suite.py` continua em `0.0.0.0` de
propósito: o aparelho precisa alcançá-lo e ele vive só durante a corrida.

---

## Custo de flash

Medido **por símbolo** (`nm --print-size`), nunca pelo total de seção — o `-A`
engana por alinhamento de 4096.

| Fase | release | alpha | Air |
|---|---:|---:|---:|
| V-01a | +48 | +376 | +400 |
| V-03 | +24 | +28 | +20 |
| V-06 | 0 | 0 | +176 |
| V-04 / O-2 | +310 | +296 | +306 |
| O-1 | +188 | +188 | +188 |
| V-05 | +381 | +381 | +381 |
| V-01b | 0 | +124 | +124 |
| **Total** | **+951** | **+1 393** | **+1 595** |

Folga reportada pelo próprio build depois de tudo: release 24 380 B,
alpha 24 020 B, Air 19 040 B (de 1 044 480 B).

> ⚠️ A memória do projeto registra "folga real" bem menor (Air 4 972 B). Os dois
> números não foram reconciliados nesta rodada — o do build é o que o linker
> impõe; o da memória vem de outra contabilidade. **Conferir antes do release.**

---

## Testes

### Nativos — todos os seis ambientes verdes

| Ambiente | casos |
|---|---:|
| `native` (validators) | 134 |
| `native_history_v5` | 59 |
| `native_cli` | 29 |
| `native_logpolicy` | 35 |
| `native_air` | 12 |
| `native_alarmqueue` | 17 |

Casos novos: `test_lockout_backoff_doubles`,
`test_lockout_reaches_and_holds_the_ceiling`,
`test_lockout_never_falls_below_the_ceiling`, `test_hwid_accepts_real_ids`,
`test_hwid_rejects_key_breakers`, `test_langident_strips_json_breakers`,
`test_langident_keeps_legitimate_names`,
`test_langident_terminates_and_respects_cap`,
`test_download_perm_gates_history_and_logs`,
`test_download_perm_leaves_ordinary_files_alone`, `test_ap_psk_shape`,
`test_ap_psk_refuses_rather_than_truncates`, `test_pin_denylist_cyw43`,
`test_sanitise_resets_denied_pin`.

Vários são escritos sobre o **domínio inteiro** (`uint8_t` 0..255) em vez dos
valores conhecidos: num teto ou numa lista de negação, um off-by-one **move** o
penhasco em vez de removê-lo, e uma lista testada só nas próprias entradas não
diz se ela começou a recusar algo legítimo.

### Controles positivos executados

| O quê | Como | Resultado |
|---|---|---|
| Portão de segredos | rodar com os 5 literais no lugar | acusou 4 posicionais + 6 por valor |
| `authLockoutMs` | restaurar a fórmula antiga | 2 falhas, `Expected 300000 Was 0`; `backoff_doubles` continua passando (1..8 não mudou) |
| `airPinValid` | restaurar `pin <= 29` | `Expected FALSE Was TRUE` + `Expected Not-Equal` |
| `isValidHwId` / `langIdentSanitize` / `downloadPermFor` | neutralizar os três | 4 casos falham; as metades "aceita o legítimo" continuam passando |
| Oráculo de fuzz | deixar a aspa passar no `langIdentSanitize` | `ORACLE FAILED: langIdentSanitize emitted a byte JSON cannot hold` em 20 s |
| `gen_logcodes --check` | TSV com código novo, tabelas velhas | 6 divergências nomeadas |
| `check_lang_packs` | `TRL()` novo sem entrada `@TRL` | falhou nos dois pacotes, com o hash |

O último par não foi provocado: os portões dispararam sozinhos durante o
trabalho, que é o controle positivo mais barato que existe.

### Fuzz

`tools/run_fuzz.sh` verde com dois oráculos novos: `isValidHwId` contra o
alfabeto documentado, e o contrato de que `langIdentSanitize` nunca emite byte
que uma string JSON não aguenta, em qualquer tamanho de buffer inclusive 1.
14,2 M execuções, 130 unidades novas (o fuzzer explorou o código novo).

### Bancada — parcialmente executada em 08/09

⚠️ **A primeira coisa que o ferro disse foi que uma das correções não funcionava.**
Ver "V-01a no ferro" logo abaixo da tabela: `system admin reset` anunciava uma senha
que o boot seguinte esquecia. Nada nesta rodada tinha tocado o hardware, e foi
exatamente o comando de recuperação que a auditoria adicionou que estava quebrado.

Estado em 08/09 (o que foi ao ferro está marcado; o resto continua não rodado):

| Achado | Instrumento | Estado |
|---|---|---|
| V-01a (lockout BT) | `tools/bt_auth_test.py` | **não escrito** — precisa de adaptador BT no host, ou app de terminal serial no celular + cronômetro |
| V-01a (recuperação USB) | `system admin reset confirm` no cabo | ✅ **RODADO 08/09 — REPROVOU e foi corrigido**; portão novo **T17** passa |
| V-03 | T15/T16 da `air_test_suite.py` | existem; **não rodados** contra esta imagem |
| V-04 / O-2 | `tools/json_escape_cases.py` | **escrito nesta rodada**, não rodado |
| V-05 | `ap` → console mostra a PSK → notebook entra; `nmcli dev wifi list` mostra WPA2 | não rodado |
| V-06 | `air charger 25` recusado, `air charger 17` aceito | não rodado |
| V-01b | scan de BT 6 min depois do boot não acha o aparelho; celular já pareado ainda conecta | não rodado |
| O-1 | conta com FILE_READ e sem LOGS: `/download?file=/system.blog` → 403 | não rodado |

### V-01a no ferro — a recuperação que não sobrevivia ao boot (F27)

`system admin reset confirm` é a recuperação documentada para uma web em que
ninguém consegue entrar, e a parte B deste achado a tornou **só pela USB**.
Rodada no rig em 08/09, ela reprovou: o console imprimia a senha nova e o boot
seguinte trazia a velha de volta.

| momento | login com a senha impressa |
|---|---|
| mesmo boot em que foi impressa | **OK** |
| depois de `reload confirm` | **401 `{"ok":false,"err":2}`** |

O console de emergência não tem `write memory`; lá o `changed = true` só imprime
*"vale para esta sessão"* e ninguém chama `saveConfiguration( )`. **Num Air todo
wake é um boot**, então a senha impressa valia cerca de um minuto.

O que escondeu isso foi um comentário que afirmava que `debug` era o único
comando sobrevivente a setar aquele flag. Não era.

Causa, conserto, o efeito colateral que o conserto abriu (`mustChangePassword`
persistido ia fazer todo wake anunciar factory defaults e estourar o T16) e o
portão novo **T17** estão em `docs/analysis/SIMUT_AIR_PLANO_FIX.md` §6.11.
Custo: **+16 B** na imagem Air.

**Para a próxima auditoria:** uma correção que só foi lida não foi verificada.
Esta passou por revisão, seis ambientes nativos e três imagens compilando, e o
defeito estava na linha que ninguém escreveu.

---

## Pendências

1. ✅ **Senha do rig rotacionada em 08/09.** Feita e verificada: sobrevive a
   `reload confirm`. Gravada em `~/.simut-bench.env` (`chmod 600`), que até
   então tinha o **placeholder** com que foi criado — é por isso que o primeiro
   login da sessão respondeu `401 err=2`, e foi por esse caminho que o F27
   apareceu. ⚠️ **Falta acrescentar a senha antiga ao `~/.simut-secrets-deny`**;
   o valor publicado no histórico do git não abre mais o rig, mas o portão de
   segredos deve conhecê-lo.
   ⚠️ O caminho que a pendência mandava usar (`conf user pass admin` +
   `write memory`) **não existe na imagem Air** — o console de emergência não
   tem nenhum dos dois. Na Air a rotação é `system admin reset confirm` (agora
   que persiste) ou `/api/login_chpass` pela web.
2. 🔴 **Validar o resto no ferro** — a tabela acima é a lista; V-01a (parte USB)
   saiu, e reprovou antes de passar.
3. 🟡 **D-8 queimada**: `v2.4.0-beta` já é a Latest (recortada em 07/09 ~22h em
   `9e1ddc3`). Este trabalho precisa de número novo.
4. 🟡 **Reconciliar a folga de flash** (build diz 19 040 B no Air; a memória diz
   4 972 B).
5. ⚪ **D-6** (`t_strict` na telemetria): não agora, por decisão. Revisitar
   quando houver instalação real com telemetria HTTPS.
6. ⚪ **V-01b opção (a)** (consentimento no pareamento): patch de framework, se
   as unidades forem para espaço público.
7. ⚪ **`check_authz.py --doc`** (comparar rotas vivas com as tabelas do `.md`):
   opcional, não feito.
