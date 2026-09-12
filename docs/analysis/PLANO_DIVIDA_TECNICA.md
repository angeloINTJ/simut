# Plano para sanar a dívida técnica

**Estado: Living.** Cada item fecha quando existir uma medição que o feche —
não quando o código mudar. Marcar aqui, no commit que o fechou.

Levantado em 2026-09-09 contra `main` = `7608558` = `v2.4.1-beta`.
**Execução iniciada no mesmo dia** — item riscado traz a medição que o fechou.

---

## A ordem não é arbitrária

Três dependências determinam tudo o que vem abaixo, e ignorá-las custa
retrabalho:

1. **A verificação de segurança está trancada dentro de um PR.**
   `tools/bt_auth_test.py`, que verifica V-01a e V-01b, só existe na branch do
   PR #97. Nada da Fase 1 roda antes da Fase 0.
2. **A `v2.4.1-beta` é a `main` de ontem** e não tem o 409 do #97. Qualquer
   verificação feita "na main de hoje" não é verificação da release.
3. **O achado mais caro (F11) é caro pelo TESTE, não pelo conserto.** Provar que
   uma partição para de crescer exige acelerar o relógio do problema. Deixá-lo
   por último não é procrastinação, é sequenciamento.

## Regras que valem para o plano inteiro

Não são conselhos gerais; cada uma custou um dia neste projeto.

- **Toda medição, duas vezes.** Uma rodada não é medição.
- **Todo teste novo tem que provar que sabe REPROVAR** — controle A/B contra a
  imagem anterior ao conserto, ou vetor sintético no `--selftest`.
- **Números idênticos entre rodadas separadas por trabalho real** são assinatura
  de instrumento cego, nunca de estabilidade.
- **`--only` não conclui estado.** Para dizer "a suíte está verde", rodar
  inteira: `--only` esconde as interações entre testes.
- **Anotar o md5 da imagem** em toda verificação. "Rodou na main" não identifica
  nada depois do próximo commit.
- **Verificação de segurança roda contra a imagem que vai ser promovida.**

---

## Fase 0 — Destravar (sem ferro, ~1 h)

| # | passo | por quê |
|---|---|---|
| ~~0.1~~ | ✅ **#98 mergeado** (`e511e75`) | 9/9 verde |
| ~~0.2~~ | ✅ **#97 separado**: as ferramentas foram para o **#99** (`5b5d24d`), mergeado; o #97 reescrito só com firmware + índice (`29c710a`) | `bt_auth_test.py` está na `main` |
| ~~0.3~~ | ✅ **#97 mergeado** (`d3d430f`) | 9/9 verde |
| ~~0.4~~ | ✅ **#92/#93/#94 mergeados** | o `UNKNOWN` era o GitHub sem recalcular depois de a main andar. ⚠️ O `label` check do #92 rodou com o labeler **v5** — em `pull_request_target` o workflow vem da base — então o v7 só é exercitado pelo PRÓXIMO PR |

**Alternativa ao 0.2**, se preferir velocidade a escopo: mergear o #97 inteiro.
Custa a limpeza do histórico e ganha meia hora.

**Critério de saída:** ✅ atingido — `main` em `d3d430f`, `tools/bt_auth_test.py` presente.

---

## Fase 1 — Verificar a segurança no ferro (~3 h, com ferro)

Sete itens foram **corrigidos no código e nunca verificados**
(`docs/security-audit/IMPLEMENTACAO_2026-09-07.md:332-339`). Esta é a dívida
mais séria do projeto, e a razão é histórica: a primeira ida ao ferro **reprovou**
o `system admin reset` (F27). *Corrigido no código* não é *funciona*.

Ordem deliberada — do que não derruba o aparelho para o que derruba:

| # | achado | como | ~ |
|---|---|---|---|
| ~~1.1~~ | ✅ **O-1** | conta `o1probe` com `perms=32` (só `FILE_READ`): `/download?file=/system.blog` → **403**. **Controle:** a mesma conta baixou `/lang/language_pt-BR.lng` → 200 (33.959 B) — o 403 é o portão, não sessão quebrada. Criada e apagada pelo `commit_all` (`users.actions`); a lista voltou aos 4 originais | feito |
| ~~1.2~~ | ✅ **V-06** | `air charger 25` → `ERROR: air charger <0..22\|26..28\|off>`; `air charger 17` → `OK: charger sense on GP17` | feito |
| ~~1.3~~ | ✅ **V-05 inteiro** | console + **pelo ar** (adaptador USB no host): varredura mostra `WPA1 WPA2`, host entrou com a PSK, web em `192.168.4.1` (302 → `/login`). ⚠️ resíduo baixo, **reenquadrado 10/09**: o firmware **pede `CYW43_AUTH_WPA2_AES_PSK`** (AES-only — constante distinta de `..._WPA2_MIXED_PSK`; verificado em `CYW43shim.cpp:126`), ou seja **não escolhe modo misto**. O `WPA1 WPA2` que a varredura mostrou é IE do beacon do chip CYW43 ou rótulo do scanner; separar exige dump dos IEs (monitor mode), que o adaptador só-NetworkManager não faz. Não é escolha do firmware | feito |
| 1.4 | **V-03** | `air_test_suite.py --only T15,T16` **contra esta imagem** | 20 min |
| ~~1.5~~ | ✅ **V-01a** | 3/3 na v2.4.1-beta: recusa, **lockout de 8 s sobrevive à reconexão** (sonda 3,4 s depois: silêncio), 4ª falha tranca de novo (8 → 16 s). Cinco consertos no instrumento antes do veredito — ver o tool | feito |
| ~~1.6~~ | ✅ **V-01b** | 3/3: descobrível após o boot, ausente 345 s depois, RFCOMM ainda conecta para quem sabe o endereço | feito |
| ~~1.7~~ | ✅ **V-04 / O-2, com A/B** | pré-fix `8d043ed` × release: **caso 1** aceita nas duas, `/api/network` quebra na pré-fix e parseia na release (escape); **caso 2** pré-fix aceita, release recusa `slot 0: invalid hwId` (validação). ⚠️ A metade "resposta quebra" do caso 2 não reproduziu na pré-fix; **caso 3 não rodado**. Ferramenta precisou de 2 consertos (payload cru → `_payload`; `sensors` → `slots`) — passava em toda imagem | feito |

⚠️ **O 1.7 não vale nada sem o controle — e foi exatamente o controle que salvou o dia.** A ferramenta deu
"PASS (refused)" na release **e na pré-fix**, duas vezes, por dois defeitos de forma diferentes; sem a
imagem anterior no ferro, isso teria virado um ✅.

⚠️ **O 1.6 precisa do canal CHARGER da mão** (GP3 → alvo GP17): cinco minutos de
observação não cabem numa janela de wake, e sem segurar o aparelho acordado o
teste mede o ciclo de hibernação e chama isso de janela fechada.

**Critério de saída:** os sete com veredito registrado e md5 da imagem, e a
tabela do `IMPLEMENTACAO_2026-09-07.md` sem nenhum "não rodado".

---

## Fase 2 — Os achados do Air ainda abertos (~2 dias)

Sete, triados por **consequência**, não por esforço:

### 2.1 Os quatro baratos — ✅ todos fechados (F10 corrigido; F14, F18 e F20 já estavam feitos)

| achado | o quê | conserto |
|---|---|---|
| ~~F10~~ | ✅ **corrigido 09/09** — o alarme do RTC é hora-do-dia, então 86.400 s vira "agora"; `airSleepSecBounded( )` limita a 86.399 s onde o intervalo vira alarme, com WARN (413). A faixa de `h_int` não mudou: as imagens de tomada não têm por que recusar 24 h | teste nativo `test_sleep_sec_bounded`; ⚠️ não exercitado no ferro (um wake de 24 h não cabe numa sessão) |
| ~~F14~~ | ✅ **já resolvido por desenho** — o campo tomou um slot morto; todo v2 real carrega 160 ali (byte baixo de 4000), inválido como GPIO, e o `airSanitise()` o vira no default. Versão não bumpada de propósito; `air charger` existe | `test_charger_pin_from_legacy_field`; premissa da linha estava velha |
| ~~F18~~ | ✅ **já estava feito desde 06/09** — só a tabela não tinha sido virada; `platformio.ini` diz `=0` no bloco do Air e o env usa 0 | verificado 09/09 |
| ~~F20~~ | ✅ **idem** — `system ssid` no README ×3 e no `CLI-Manual.md`; `check_air_consistency.py` C1–C8 limpo | verificado 09/09 |

⚠️ **F14 tem armadilha conhecida** (F27, `isFactoryDefaults`): mexer em
`AIR_CONFIG_VERSION` quebra quem lia a versão antiga. Migração com teste.

### 2.2 F08 — o aparelho que fica offline para sempre — ⚠️ FIX ESCRITO, repro bloqueado (PR #103, DRAFT)

`DECIDE` só ia a `CONNECT` com `NET_READY`, que exige NTP e **não tem timeout**
para sair do `NET_CONNECTED_WAIT_NTP`. Rede sem fonte de tempo = o wake dormia
sem tentar (perda silenciosa). Eram **três** portões, não um (DECIDE, CONNECT e
o `netLost` do FLUSH via `isNetworkHealthy`), todos exigindo NET_READY.

✅ **Fix escrito (10/09):** os três passaram a `isLinkUp()`/`isLinkHealthy()`
(associado + IP, sem NTP); o timestamp vem do relógio provisório até o NTP
corrigir. Branch `fix/f08-send-on-link-not-ntp`, commit `84de35b`.

✅ **Caminho saudável VALIDADO no ferro:** imagem F08, LAN real, NTP sincronizado
→ 52 registros drenados a um coletor em 6 wakes; o refactor não quebra o envio normal.

🔴 **O fix em si (enviar em WAIT_NTP) NÃO foi validado no ferro.** O repro exige
NTP falhando com o link de pé, e a bancada desta noite não produz isso: o hotspot
do NetworkManager NATeia para a internet do host (NTP sincroniza), sem sudo não dá
para bloquear NTP, e a config static-IP + DNS-morto associou instável (e estrandou
a bancada 2×). **Precisa de um AP realmente offline ou regra de firewall (udp/123)**
— infra do Ângelo. Teste pronto em `scratchpad/f08_test.py`.

⚠️ `/api/status` "ntp" = `isTimeSynced()` = relógio provisório, VERDADEIRO mesmo
offline — não indica sincronização real.

### ~~2.3~~ ✅ F11 — a partição que nunca é limpa — CORRIGIDO (redesenho, PR #102)

**09/09:** o conserto do armazenamento funciona (imagem segurou o FS no limite de 86% por 6 wakes,
+0 B, contra a release que cresceu acima sem limpar; `STO_ENFORCE_BUDGET ctx=1` disparou), **mas na
posição errada.** Pus o dreno em `airEnterDormant`, a poucas linhas do desligamento de clock e do WFI.
Afirmei que não afetaria o wake — **errado, e o controle pegou:** release 2 wakes / 240 s × a imagem
F11 **0 wakes / 240 s**, lado a lado na mesma bancada saudável. Uma amostra cada, com o intermitente
do sono ativo, então "quebra determinística" não está provado (a imagem acordou 6× antes) — mas a
assimetria é real e o lugar é exatamente onde a saga do F22 disse que a entrada de sono é delicada.
**Não sobe.** Branch `fix/f11-storage-limit-in-m1` (commit `e22ee9b`) guarda o trabalho como registro.
**Redesenho:** mover o dreno para a janela ACORDADA (depois do `processHistoryLogging` no DECIDE, onde
uma gravação de flash já acontece em segurança), fora do caminho de sono — e revalidar com o controle
de wake quando a bancada estiver estável.

✅ **09/09 noite — FEITO e MEDIDO.** O dreno foi para o `AIR_PHASE_DECIDE`, ao lado da gravação de
histórico que já roda ali. **Wake limpo: 2 wakes/240 s idênticos à release + 11 wakes limpos numa
janela de 600 s sem toque** (a regressão da 1ª tentativa sumiu). **Dreno funciona:**
`STO_ENFORCE_BUDGET ctx=1` e `ctx=2` (arquivos apagados) e o FS **fixo em 86%** — o limite — em vez de
crescer acima como a release. Em uso só-M1 o laço M0 não roda, então um dreno disparando com o aparelho
ciclando sem toque é o caminho do DECIDE por construção. Código de enforcement idêntico ao da 1ª
tentativa; só o sítio da chamada mudou. `check_air_consistency` C1–C8 limpo, `native_logpolicy` 39/39,
teto de flash 2.516 B de folga. Branch `fix/f11-storage-limit-in-m1` (`e22ee9b`) fica como registro do
porquê o sítio de sono reprova; **não mergear**.

### ~~2.3-orig~~ F11 — a partição que nunca é limpa

O M1 pula `StorageManager::update()` (limpeza de orçamento do FS) e nunca chama
`flushPendingIfAny()`. **Em uso só-M1 — que é o uso do produto — a partição
enche.** Perda de dados silenciosa, a prazo longo.

**O conserto é pequeno; o teste é o trabalho.** Como provar que o consumo
estabiliza sem esperar semanas? Acelerar o relógio do problema: `h_int` no mínimo,
FS deliberadamente quase cheio, e medir `fs_u` ao longo de N ciclos com um controle
negativo (a mesma corrida com a limpeza desativada tem que ENCHER).

⚠️ **Medir `fs_u` pelo delta do arquivo do dia dá +0 e engana** — bloco aberto.
Ver `full_history( )` em `tools/air_test_suite.py` e a linha do F23 no `SIMUT_AIR_PLANO_FIX.md`.

### ~~2.4~~ ✅ F12 — `air stop` por Bluetooth não funciona em M1 — DOCUMENTADO (09/09)

O laço M1 só chama `processInput` (USB). Funcionalidade ausente, não risco. **Decisão do Ângelo
(09/09): não corrigir — a folga de flash do Air é apertada (4.972 B) e não é risco de segurança.**
Documentado como limitação no `CLI-Manual.md` (nota sobre `air stop` por BT em M1) e a linha do
`SIMUT_AIR_PLANO_FIX.md` virou. Fecha o achado.

**Critério de saída:** cada achado com um teste que o cobre na suíte, e a tabela
do `SIMUT_AIR_PLANO_FIX.md` sem `F` na coluna de estado.

---

## Fase 3 — O que nunca foi medido (~1 dia + soak)

### 3.1 Corrente real — a mais importante do plano

**Os 407,8 mAh/dia são CALCULADOS.** Nunca houve amperímetro no circuito. O
produto inteiro se chama "hiberna para durar"; a afirmação central não tem
medição.

- INA219 (ou multímetro em série) entre a fonte e o alvo;
- medir **dormindo** e **acordado** separadamente, não a média;
- comparar com o cálculo e registrar a diferença, seja ela qual for.

⚠️ **Enquanto isso não existir, nenhuma afirmação de autonomia pode ir para o
manual nem para a página do produto.**

### ~~3.0~~ ✅ O fix de reconexão do Wi-Fi, VALIDADO no ferro (09/09, v2.4.1-beta)

O fix que abriu esta linha de trabalho (varredura com prazo + join às cegas + dormência com saída)
saiu na v2.4.1-beta **sem nunca ter visto um AP sumir e voltar**. Com um adaptador Wi-Fi USB no
host isso virou testável: o adaptador vira o AP (`SIMUT-BENCH`, hotspot do NetworkManager), o
aparelho é apontado para ele e **segurado em M0** (todo wake do Air é um boot — a pergunta é
"volta *sem* reboot"), e o AP é derrubado e levantado. Três fases: apagão curto (60 s, escada
rápida), apagão longo (4 min, entra em **dormência** — o estado terminal do firmware antigo),
e **SSID oculto** a partir de um boot (o caminho cego). Observado dos dois lados: ping pelo hotspot
e `show net status` no console, **e o log do próprio aparelho**, que é a prova. Script: `tools/wifi_outage_test.py`.

```
curto    reconectou 45 s depois de o AP voltar (8 s no run anterior)
longo    NET_DORMANT_MODE às ~20:52:40, depois de 5 joins às cegas (backoff 42 → 62 → 102 → 181 s);
         AP de volta 20:54:50; reconectou 21:03:13 — 499 s depois, quando a espera de 10 min expirou.
         É o estado de onde o firmware antigo nunca saía.
oculto   reconectou 13 s depois de o AP oculto voltar: 2 joins às cegas, 0 por varredura
```

⚠️ Três mentiras do instrumento antes do veredito: varredura só até `.39` (o console dizia `.235`); 4 min
de apagão não chegam à dormência; "SSID oculto do boot" mede o `begin()` do boot, não o caminho cego.
⚠️ **`$` em env sem aspas** mangou a senha do Wi-Fi e deixou a bancada sem rede duas vezes — a linha agora tem
aspas simples.

### ~~3.2 OTA no Air~~ — ✅ respondido 10/09: não é bug, é o desenho

A `:8080` não escutava porque **um wake M1 não sobe o servidor web** — o portão é
`_airActive` (o wake de telemetria também não sobe listener; enviar não precisa de
ninguém escutando). Web/OTA pertencem ao **M0**: boot a frio, `air stop`, ou
**carregador presente no GP17**, que impede a hibernação — a PicoHand segura o M0
por GP3→GP17. Não é config da bancada e não há correção de firmware: para dar OTA
num Air, mantenha-o em M0. Documentado no `AGENTS.md`. O protocolo de OTA em si já
tem 24 ciclos validados (v2.2.12), então não sobra o que revalidar.

### ~~3.3 Soak longo~~ — ✅ **FEITO 10–11/09, e REPROVOU o firmware (achado F28)**

12 h hands-off, instrumento passivo (`tools/air_soak.py`: presença crua do USB + console só-leitura + coletor
HTTP). **119 ciclos impecáveis em 1,99 h** — 0 atrasados, 0 OVERRUN, 0 FATAL, 160 registros entregues — e então,
às **13:51:24, um sleep ordinário nunca acordou** (esperados 720, entregues 119). O histórico em flash confirma
ao segundo. O checkpoint de 24 h passou em branco; responde a RESET. **É o "intermitente do sono" reproduzido
sob observação, no código que a v2.4.2-beta lançada carrega.** Detalhes, suspeitas e conserto proposto:
[`SIMUT_AIR_PLANO_FIX.md`](SIMUT_AIR_PLANO_FIX.md) **F28**; evidências em `evidence/2026-09-10-soak/`.

### ~~3.3 (redação original)~~

≥12 h ciclando sem ninguém tocar. Contar: reboots, deriva de heap, registros
perdidos (agora mensurável — `full_history` + presença no USB), e falhas de
telemetria.

---

## Fase 4 — Higiene (~meio dia)

| # | item | nota |
|---|---|---|
| ~~4.1~~ | ✅ **PCB: nada a fazer — a `main` já está correta, verificado 09/09** | re-exportei os gerbers da fonte de `main` (`simut PCB.kicad_pcb`, corrigida em `d64bacd`) com o `kicad-cli` 10.0.6 e comparei com os commitados: **geometria idêntica como conjunto** — 213 flashes no F_Cu, 173 no B_Cu, mesmas aperturas; a diferença linha-a-linha era só renumeração de aperturas. O "falta re-exportar" era da cópia da branch air, descartada no merge; a `main` nunca teve o problema |
| ~~4.2~~ | ✅ **Folga real do Air: 4.972 B** — os dois números eram verdadeiros e mediam coisas diferentes | `used 1.025.600` do PIO é a **soma das seções**; o `.bin` tem **1.039.508 B**. A diferença (13.908 B) é o linker alinhando a LMA do `.data` em 4 KiB (`readelf -l`: o LOAD 1 termina em 0xfc000 e o `.data` começa ali). Folga real = `1.044.480 − fim do último LOAD` = **4.972 B**, e anda em degraus de 4 KiB: os próximos ~13,9 KB de `.text/.rodata` são de graça; passado o degrau, sobram **876 B**. O portão do `flash_budget.json` lê a linha do PIO por desenho (marca d'água, não teto) |
| ~~4.3~~ | ✅ **corrigido 10/09** | `rig_validate_history_clock.py` afirmava "nenhum registro do trecho drenado foi pulado" com `expected − seen`, e `expected` só vinha dos `.h5` **selados** — um pulo no bloco aberto nunca entrava e passava calado. Agora dobra o `/api/history/open` no `expected` (204 = nada aberto). Seguro por construção: o filtro `lo ≤ e ≤ hi` derruba o bloco aberto quando a drenagem é curta demais para chegar ao dia de hoje, então a mudança **só remove um ponto cego, nunca cria falso positivo**. Mesma lição do T11 |
| ~~4.4~~ | ✅ Varrido: 7 leitores | `history_v5.py` é o codec; `test_webui_graph_order.py` e `test_h5_day_merge.py` usam fixture; `fsguard.py` e `h5_day_merge.py` tratam o `.wip` explicitamente (15× e 5×) — é o bloco aberto, por desenho. **Sobra só a 4.3** |

---

## ~~Fase 5 — Recortar a release~~ — ✅ **v2.4.2-beta recortada em 10/09, Latest** (tag `49086b5`)

`https://github.com/angeloINTJ/simut/releases/tag/v2.4.2-beta` — 11 assets. Carrega F10, F11, F27, T11/T08/F04
e a auditoria de setembro verificada no ferro; **não carrega o F08** (fix no #103, draft). Revalidada antes do
recorte: suíte completa via USB no `8377423` (T01–T11, T13/T14/T16/T17), **OTA fim-a-fim** na imagem bumpada
(`/api/perms` → `2.4.2-beta`, config preservada pelo snapshot) e ciclo medido por presença crua do USB (3/3 na
hora). Firmware publicado = o validado, md5 conferido 6/6 após o upload (air bin `86885c69`).
⚠️ O recorte foi barrado pelo `scan_secrets.sh` — ver "SSID no histórico" abaixo — e só saiu depois do #107.

⚠️ **`git push origin :refs/tags/<tag>` REBAIXA o release a draft** e derruba o
Latest. Conserto: `gh release edit --draft=false --latest`.

---

## O que apareceu executando (novo, baixo)

- 🔴 **SSID da bancada no histórico público** (`tools/wifi_outage_test.py:88`, desde `c1a876b`/PR #100): o nome
  do Wi-Fi de casa estava hardcoded como default de `SIMUT_REAL_SSID`. **Só o SSID — a senha nunca teve literal.**
  O `scan_secrets.sh` pegou no recorte (o gate funcionou). Removido da árvore no PR #107; o valor **continua no
  histórico do git**. Severidade baixa (SSID é transmitido pelo AP, não é credencial), mas identifica a rede de
  casa num repo público. **Decisão do mantenedor:** aceitar como baixo risco, rotacionar o SSID, ou reescrever o
  histórico (`filter-repo` — ver as armadilhas já documentadas). O teste agora exige `SIMUT_REAL_SSID` no env.
- ⚠️ **Corrida do read-path na suíte — T05/T10/T12/T15 acusam "no alarm line"/"serial vanished"/"stuck awake"
  com o device ciclando certo.** Causa raiz: a linha `[AIR] alarm` sai em `Serial.printf` (`AppManager_Air.cpp:648`)
  e logo em seguida (661–675) o firmware solta o pull-up do D+ e dorme — o host raramente drena a linha antes do
  SE0. Provado em 10/09: T05 falhou 2× enquanto a presença crua do USB mostrou 3 ciclos na hora. **O firmware
  JÁ faz `Serial.flush()` + `delay(100)` antes de soltar o D+ (667–669)** — não é flush faltando. **Conserto
  parcial no instrumento (PR #109, mergeado):** `read_until` lia por linha e no disconnect retornava sem drenar;
  agora lê em blocos e drena o buffer no close — T05 foi de falha→**3/3**, mas o T10 ainda erra às vezes (a corrida
  é intrínseca). **Cura completa (follow-up, decisão do mantenedor):** a suíte não julgar por essa linha de
  diagnóstico — detectar sono pela ausência no USB (100% confiável, medido) — ou um settle pré-disconnect no
  firmware, ao custo de bateria em todo wake. ⚠️ O `delay(100)` PÓS-disconnect é load-bearing (sem ele o cdc_acm
  trava e o ttyACM não reenumera): NÃO reduzir. Enquanto isso, qualquer "no wake" da suíte se confere com
  `os.path.exists` no nó by-id antes de acusar o firmware.

- ✅ **BT: `_authenticated` não zerava na desconexão — bypass CONFIRMADO no ferro e CORRIGIDO (PR #110, mergeado).**
  Em 10/09, com o adaptador BT do host (`tools/bt_reconnect_probe.py`): autenticar → derrubar o link RFCOMM →
  reconectar → `air status` respondeu **sem senha** — o 2º cliente herdava a sessão de admin (`BluetoothManager`
  não tem onDisconnect; `_authenticated` só zerava em boot, expiração e senha recusada). Bypass real, não
  usabilidade. **Conserto:** `update()` pega a borda conectado→desconectado via `SerialBT.availableForWrite()` —
  a ÚNICA visão pública do `_connected` (o `operator bool()` devolve `_running`, não serve) — e zera
  `_authenticated`, `_promptSent` (o problema de usabilidade original cai junto) e `_authBuffer`; o lockout fica
  intacto de propósito (V-01a). **Validado:** a reconexão agora devolve "Senha do admin:"; `bt_auth_test.py
  --only lockout` = V-01a **3/3** (lockout de 8 s sobrevive à reconexão, escada 8→16). +56 B no air. Ship no
  próximo release. ⚠️ Framework pinado: um bump precisa reconferir o `availableForWrite()`; a sonda é a regressão.

## O que este plano NÃO cobre

- As cinco issues abertas (#52, #57, #58, #60, #61) — todas `good first issue`
  de design e documentação. É backlog, não dívida.
- Reboot sob telemetria morta (`ctx=205`) — aberto e **não reproduz**; as
  blindagens já existem. Fica onde está até reproduzir.
