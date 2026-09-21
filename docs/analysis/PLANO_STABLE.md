# De beta a stable — o que precisa ser verdade, e como isso se prova

**Estado:** v2.6.1-beta é a Latest (21/09/2026, `main` `19ca65a`).
**Este documento é Living.** Cada linha da matriz vira ✅ com a data e o
número, ou continua aberta. Nenhuma vira ✅ por argumento.

---

## 1. O que "stable" tem de significar aqui

Uma definição que dê para testar, senão a promoção é uma mudança de rótulo.
Para este produto, stable quer dizer três coisas:

1. **Não reinicia sozinho.** Todo reboot tem causa conhecida e registrada. Um
   `ctx` sem explicação é bloqueio, não ruído.
2. **Não perde medição.** O que o sensor leu chega ao histórico e à telemetria,
   ou o aparelho diz que não chegou.
3. **Volta sozinho.** Queda de rede, coletor morto, OTA interrompida, falta de
   energia — o aparelho retorna sem alguém ir até ele.

E uma quarta, que é do projeto e não do firmware:

4. **Toda afirmação publicada tem número ou marcação.** O manual já faz isso
   ("aritmética, não medição" na autonomia). Numa stable isso não pode
   regredir.

---

## 2. Onde a v2.6.1-beta já está

Isto não é para tranquilizar; é para não refazer o que já foi feito.

| o que | evidência | quando |
|---|---|---|
| Reconexão de Wi-Fi com AP sumindo e voltando | `wifi_outage_test.py`, 3 fases, log do próprio aparelho | 09/09, v2.4.1-beta |
| Varredura de redes, STA e **de dentro do AP** | `wifi_scan_hw_test.py --ap`, 18/18 | 21/09 |
| Painel: 32 contas, PIN, política | `panel_users_hw_test.py` 32/32 + `panel_fulltable_test.py` | 20/09 |
| Alarmes 2ª linha (HTTP/MQTT) | `alarm_hw_test.py`, `alarm_mqtt_test.py` — ⚠️ em 21/09 a suíte deu **11/14**, e as 3 falhas são o B12 (corte de 63 chars da CLI), não a linha | 23/08 |
| OTA | 24 ciclos, 3/3 na v2.2.12 | 19/08 |
| Soak 5 h sob coletor morto | 0 reboots, heap +114 B | — |
| Telemetria nos 4 transportes | dreno 204 reg/s, soak HTTPS 30 min 0 FTL | — |
| 6 imagens × 7 suítes nativas (384 casos) + 5 portões | CI em todo PR | contínuo |

---

## 3. O que falta — e o que disso bloqueia

| # | item | bloqueia? | por quê |
|---|---|:---:|---|
| B1 | **`ctx=209`/`ctx=455` do D-C1**: watchdog do Core 0 com trace vazio, reproduzido 2×2 em 20/09 por `panel_fulltable_test.py`, **não determinístico** | 🔴 **sim** | Fere o item 1 da definição. Um reset sem causa numa stable é o defeito que volta como "o aparelho reiniciou sozinho" sem nada para investigar. ⚠️ **21/09 acrescenta uma dificuldade ao instrumento**: uma gravação de firmware escreve `SYS_BOOT` na MESMA faixa (`200+módulo`), provado nesta bancada — então varrer o anel só vale sabendo a hora das gravações. E o reprodutor precisa da `pico_w_test` (B11), que não é a imagem publicada |
| B2 | ~~Sem soak na imagem desta versão~~ — **fechado em 21/09** | 🟢 **fechado** | 7,9 h na `pico_w_release` publicada, 92 amostras: **0 reboots**, 0 kills reais do Core 1, `fx`=0, heap contíguo 32.300 → 32.266 B (mínimo 32.252) — **34 B em 7,9 h**. As duas metades que o T3 pede saíram no mesmo dia: 3 h 58 sob coletor morto e o dreno completo quando ele voltou (§6) |
| B3 | ~~OTA nunca exercitada nesta imagem~~ — **feito em 21/09: 6/6 applies** nos dois sentidos, na `pico_w_release` publicada. Fica o teto: o Air a **5.076 B**, medido hoje | 🟡 **metade fechada** | A parte "nunca exercitada" caiu com número (T4, §6). A parte do teto não é coisa que teste feche: é margem. E ela **já tem portão** — `check_flash_budget.py::check_ota_bin` compara o `.bin` com `OTA_APP_SAFE_MAX_SIZE` (1.040.384 B) em todo build, que é justamente o que o `used` do linker não vê. Folgas medidas em 21/09 sobre a build de `main`: release 15.684, air **5.076**, alpha 50.036, test 10.828, asserts 13.508, test_https 3.204 |
| B4 | **`ctx=205`** — reboot sob telemetria morta, aberto e **não reproduz** | ⚠️ não | As blindagens existem; fica onde está até reproduzir. Registrado, não esquecido |
| B5 | **Corrente real nunca medida** (os 407,8 mAh/dia são cálculo) | ⚠️ não | O manual **já diz** "aritmética, não medição". Não bloqueia porque nada é afirmado sem marcação — mas nenhuma afirmação de autonomia pode perder a marcação numa stable |
| B6 | `pico_w_test_https` a **3.204 B** do teto de OTA (medido 21/09; o 3.308 anterior era de build anterior) | ⚠️ não | Ambiente de bancada, não é imagem de produto. Vira bloqueio se alguém precisar dele no campo |
| B7 | Issue #118 — mDNS do Air | ⚠️ não | Backlog |
| B10 | **Cursor de telemetria pula registro em bloco fora de ordem** — medido em 21/09: **6 de 75.778** registros (0,0079%) em 55 arquivos de dia | ⚠️ não | Fere o item 2 *em silêncio*, e é por isso que não é ruído. Mas o registro **não se perde**: está na flash e sai pelo `/download` e pelo CSV — só a telemetria não o leva. A causa está escrita no firmware (`src/TelemetryManager.cpp:286`): um cursor escalar em tempo não alcança um registro atrás da marca-d'água. Fechá-lo é mudança de formato (cursor vira posição de varredura), não de rótulo. Vira bloqueio se o aparelho continuar **sem dizer** que pulou |
| B11 | **A bancada roda `pico_w_test`, não a imagem que a versão publica** | 🔴 **muda o B2 e o B3** | As suítes do ferro exigem `user`/`tel` da CLI, e a `pico_w_release` tem `SIMUT_CLI_FULL=0`. Provado em 21/09: `user policy` e `tel server` só aparecem no `.bin` de teste, e o aparelho responde os dois. Logo o T1 de 21/09 certifica a `pico_w_test`. O soak (T3) e a OTA (T4) **têm de ir na `pico_w_release`** — nenhum dos dois precisa de CLI, e `telemetry_bench/soak_a6.py` foi escrito exatamente para isso |
| B13 | **`/api/status` trunca a resposta** (`InvalidChunkLength`) em requisições espaçadas: **4 de 96** no soak (5 min entre elas), **0 de 150** em laço de 0,5 s, **0 de 4** no teste do socket ocioso | ⚠️ não | Não derruba o aparelho (uptime segue, próxima leitura normal) e nenhum dos quatro contadores de aborto (`ad`, `cgd`, `cgg`, `cgx`) registra — que é o caso que eles existem para tornar visível. Um cliente que faz *poll* espaçado precisa tolerar corpo incompleto e repetir. Causa não estabelecida: só aparece com silêncio antes, e 4 tentativas não bastam para provar o mecanismo |
| B12 | **A CLI corta template em 63 caracteres, em silêncio, e responde OK** — medido em 21/09: 70 chars entram, 63 ficam; 63 ficam 63; 62 ficam 62 | ⚠️ não, **mas é da família do item 2** | `alarm set line/glob/path` passa pelo `strVal2[64]` do `CommandParser.cpp:392`; o destino é `lineTemplate[512]`. O valor chega ao `safeCopy` **já cortado**, então o portão `isValidCfgString` — que na web **recusa** o que não cabe inteiro — vê algo que cabe e aceita. O template real deste aparelho tem 141 chars (veio da web) e **não pode ser reescrito pela CLI**. Efeito medido: o template de 75 chars do `alarm_hw_test.py` virou 63, o payload saiu com vírgula pendurada, o coletor não conseguiu parsear e **3 das 14 verificações falharam por isso**. O comentário em `CommandParser.cpp:388` diz que a limitação é *valor com espaço*; não menciona o corte por comprimento |

**Onde estão os três bloqueios em 21/09, fim do dia de bancada — sobra um:**

| | |
|---|---|
| **B1** | 🔴 **aberto e REPRODUZIDO em 21/09 16:52** — `ctx=209`, C0=[CLI], numa sessão de **cinco comandos** de CLI, não em 25 `write memory`. Continua **inalcançável pelo caminho da imagem publicada** (`write memory` não existe lá; o equivalente web deu 0 em 500), mas na `pico_w_test` o custo de reproduzir é baixo. O que falta agora não é reproduzir: é **capturar o texto da autópsia** (`sc3`, `C1=[…]`, `up=`), que só existe na serial do boot e não sobrevive ao reboot |
| **B2** | 🟢 **fechado** — 7,9 h na `pico_w_release`, 0 reboots, heap −34 B |
| **B3** | metade fechada com número (6/6 applies, 75.831/75.831 registros); a outra metade é margem com portão em CI |

O B11 diz em qual imagem o B2 e o B3 se fecham. Os outros sete — B4, B5, B6,
B7, B10, B12 e B13 — ficam registrados e não impedem a promoção.

---

## 4. A matriz de testes

O que cada linha tem de produzir para virar ✅: um **número** ou um **log**,
nunca "rodou e pareceu bem".

### 4.1 O que roda nesta bancada, agora

| # | teste | como | tempo | passa quando |
|---|---|---|---:|---|
| T1 | Regressão funcional da imagem | `panel_users_hw_test.py`, `panel_fulltable_test.py`, `alarm_hw_test.py`, `wifi_scan_hw_test.py --ap`, `web_test_suite.py` | ~2 h | tudo verde na imagem **desta** versão |
| T2 | Caça ao B1 | `panel_fulltable_test.py` em repetição, com o log binário preservado entre corridas | 3–4 h | ou reproduz com `ctx` e trace utilizáveis, ou N corridas limpas dão a taxa |
| T3 | Soak ✅ **21/09, 7,9 h** | `telemetry_bench/soak_a6.py`, ≥ 8 h, coletor vivo e coletor morto, **na `pico_w_release`** | 8 h+ | 0 reboots sem causa; deriva de heap medida e declarada — **feito: 0 reboots, heap −34 B em 7,9 h** |
| T4 | OTA nesta imagem ✅ **21/09** | ciclo de stage+apply, ida e volta entre v2.6.0-beta e v2.6.1-beta, pela `:8080` | ~1 h | 3/3 nos dois sentidos, `/history` íntegro pelo `fsguard` — **feito: 6/6 e 75.831/75.831** |
| T5 | Queda de rede ✅ **21/09, 4/4, na `pico_w_release`** | `wifi_outage_test.py` — hoje ele é do **Air**: segura o aparelho em M0 (`air idle 3600` + linha CHARGER da mão) e aponta o Wi-Fi por `system ssid`/`system pass` pela CLI. Num TFT não há M0; na `pico_w_release` a CLI é o console de emergência, que **tem** `system ssid/pass` mas não o resto | ~40 min + conserto | as 3 fases, com o log do aparelho como prova |
| T6 | Telemetria | `telemetry_bench` nos 4 transportes + dreno | ~1 h | 0 FTL; vazão registrada |
| T7 | Relógio e histórico | `rig_validate_history_clock.py` | ~20 min | 3/3 |

### 4.2 O que esta bancada **não** consegue

| # | teste | o que falta | sem isso |
|---|---|---|---|
| N1 | Corrente real (B5) | INA219 ou multímetro em série | a autonomia continua marcada como cálculo — e **tem de continuar marcada** |
| N2 | Queda de energia real, repetida | tomada comandável | o caminho "volta sozinho" fica provado só por reset do PicoHand, que não é o mesmo |
| N3 | Temperatura e umidade fora da bancada | câmara | nenhuma afirmação de faixa ambiental pode ir ao manual |

### 4.3 Ordem sugerida

T1 → T4 → T5 → T7 (um dia de bancada, tudo curto e conclusivo) → T2 e T3 em
paralelo (T3 é tempo de relógio; T2 é repetição). T6 por último, porque o que
ele mede já tem histórico bom e é o menos provável de mudar o veredito.

⚠️ **T2 e T3 não rodam em paralelo** — o B11 impede. O T3 exige a
`pico_w_release`; o T2 usa o `panel_fulltable_test.py`, que precisa do `user
pin` da CLI completa, isto é, da `pico_w_test`. São duas imagens no mesmo
aparelho, então são dois turnos de bancada, não um. Ordem que isto respeita, e
que foi a de 21/09: tudo que precisa de CLI na `pico_w_test` → grava a
`pico_w_release` → T4 e T3 nela → volta para a `pico_w_test` para o T2.

---

## 5. Quando promover, e para qual número

`CONFIG_VERSION` está em 25 e nada nesta linha o move. A promoção é de
**rótulo**, não de schema: `v2.6.1-beta` → `v2.7.0`, sem `-beta`, cortada da
mesma `main` e publicada com `--latest`.

⚠️ `prerelease=true` **nunca** vira Latest — é o erro que já custou uma tag
neste repositório.

A promoção acontece quando **B1, B2 e B3 estiverem fechados**, cada um com o
número ou o log neste documento. Não antes, e não por prazo.

---

## 6. Registro

| data | o que rodou | resultado |
|---|---|---|
| 21/09 | T1: `wifi_scan_hw_test.py --ap` na imagem v2.6.1-beta | **18/18** |
| 21/09 | T1: `panel_users_hw_test.py` (sem `admin` — ver nota) | **26/26**, 19 telas, 13 registros de alarme |
| 21/09 | T7 partes A e B: relógio/histórico e leitor de gráficos | ✅ (parte C bloqueada, ver B8) |
| 21/09 | Varredura de boots no anel de log após T1+T7 | **0** boots com ctx de watchdog; 4 `REBOOT_USER` dos próprios testes e 4 `SYS_BOOT ctx=227` das gravações — **ver a nota: `ctx=227` não é a faixa do upload** |
| 21/09 | T7 parte C: dreno do histórico para coletor local | **6/7** — 16 syncs, 275 POSTs, 13.750 instantes, trecho 22/08 05:53 → 21/09 05:53; **1 registro não entregue** (19/09 03:40:36) |
| 21/09 | Varredura dos 55 arquivos de dia atrás do cursor escalar | **6 de 75.778** registros (0,0079%), em 3 blocos parciais fora de ordem → B10 |
| 21/09 | Qual imagem está no ferro | `pico_w_test` (a CLI responde `user policy`/`tel server`, que só existem nela) → B11 |
| 21/09 | T1: `web_test_suite.py` como admin | **67/67**, 1 pulado (a sessão *é* admin, então o teste de fronteira de permissão não tem o que provar) |
| 21/09 | T1: `web_test_suite.py` com conta comum criada e apagada pela CLI | **87/87**, 5 pulados (rotas de admin). As duas passagens juntas cobrem o que cada uma sozinha pula |
| 21/09 | T1: `alarm_hw_test.py` | **11/14** — as 3 falhas são o corte de 63 chars da CLI (B12), não a linha de alarmes |
| 21/09 | Onde a CLI corta um template | 70 → **63**; 63 → 63; 62 → 62, sempre respondendo OK → B12 |
| 21/09 | Assets publicados × build local (v2.6.1-beta release) | md5 **idêntico** — `447c0099…`. O que o usuário instala é o que está no `.pio/build` |
| 21/09 | O que uma gravação escreve na autópsia | `SYS_BOOT ctx=227 lvl=4` + `HW WATCHDOG: Core 0 loop stalled, C0=[HIST_SAMPLE]` — **igual a um travamento**; ver nota |
| 21/09 | `pico_w_release` publicada gravada no ferro | config atravessou inteira (nenhum campo diferente do snapshot); CLI virou console de emergência |
| 21/09 | **T4: OTA na imagem publicada, 3 idas e voltas** (v2.6.0-beta ↔ v2.6.1-beta, assets do release, pela `:8080`) | **6/6 applies**, cada um confirmado pela versão que o aparelho relê; stage 34,5 s, volta em 48–51 s |
| 21/09 | O que os 6 applies fizeram com o LittleFS | zerou: `/history` só com `.wip`, `/lang` **vazio**, `/themes` só com o README; `fs_u` 876.544 → 61.440 B |
| 21/09 | T4: histórico depois do `fsguard restore`, conferido por **registros** | **75.831 no backup, 75.831 no aparelho, 0 faltando**, 57 arquivos, 0 falhas na restauração |
| 21/09 | T4: config através de 6 OTAs | **nenhum campo diferente** do snapshot de antes da gravação — o `config_snapshot` do applier segura o que promete |
| 21/09 10:51 | **T3, metade "coletor morto" → "coletor vivo": o dreno** | Depois de **3 h 58** sem coletor (06:53 → 10:51), o aparelho tinha **237** medições enfileiradas e 52 falhas contadas, com **0 reboots**. Apontado para um coletor vivo, a fila foi a **zero numa rodada**: **239 registros de histórico** entregues cobrindo 06:53:25 → 10:50:54, **0 faltando** dentro da janela (conferido contra o `.h5` + bloco aberto), 1 duplicata inócua (o ingest é por timestamp) e **25 registros da 2ª linha** de alarmes, que cai no mesmo `path`. É o item 2 e o item 3 da definição, medidos no mesmo gesto |
| 21/09 14:46 | **T3 COMPLETO: 7,9 h na imagem publicada** | **0 reboots**, `c1kl`/`c1kh` = 0, `fx` = 0, batimento do Core 1 máx. 65 ms, heap contíguo **32.300 → 32.266 B** (mínimo 32.252). 245 envios de telemetria e 52 falhas, todas da janela sem coletor. **Fecha o B2** |
| 21/09 16:52 | 🔴 **B1 REPRODUZIDO: `SYS_BOOT ctx=209` (WATCHDOG, C0=[CLI])** na `pico_w_test` v2.6.1-beta | **E numa sessão de CINCO comandos de CLI**, não em 25 `write memory`: `enable` / `configure terminal` / `user policy 4 3 0` / `end` / `write memory`, com o display vivo. Datado em ~16:52:26 pelo anel (primeiro carimbo pós-boot 16:52:45 com `up=19s`). Três evidências independentes o põem **dentro** da sessão: as linhas de boot (`[DBG] BME HW I2C init`) chegaram na captura da própria CLI; o `APP_UI_PIN_POLICY` do comando está carimbado 16:53:02, **depois** do boot; e o `write memory` respondeu OK no aparelho já reiniciado. Anel preservado em `t2/evidencia/logs_run1.bin` |
| 21/09 17:02 | 🔴 **O B13 matou uma corrida de teste** | A corrida 1 do T2 terminou `rc=1` por `InvalidChunkLength` — a mesma resposta truncada. Aqui sob **carga** e em corpos **grandes e transmitidos por partes** (`/api/logs`, `/api/keypad` a cada login), não no `/api/status` ocioso. Deixa de ser curiosidade: quebra cliente de verdade |
| 21/09 | ⚠️ **Duas correções de instrumento, minhas** | (1) o laço do T2 atribuiu à corrida 1 um boot que era **anterior** a ela — ele relata qualquer `SYS_BOOT` no anel, não os novos; a atribuição correta é por diferença entre despejos. (2) O amostrador não registrou o reboot: ele abria **sessão nova a cada 10 s**, e um login repetido da mesma conta invalida o token anterior (`WebManager_Auth.cpp:462`), então ele colhia 401 engolidos em silêncio |
| 21/09 16:34 | **T5: queda de rede na imagem publicada — 4/4** | Entrou no hotspot em 18 s; apagão curto: reconectou **15 s** depois de o AP voltar; apagão longo: **entrou em dormência (1×)** e voltou **495 s** depois; SSID oculto: **13 s pelo caminho cego** (`SYS_WIFI_CONNECT ctx=1` ×2). Restauração conferida: de volta na rede real em 10 s. A adaptação do teste para TFT funcionou (pulou o M0, que não existe fora do Air) |
| 21/09 | 🔴 **V-09 (segurança, ABERTO): `PERM_USER_MGR` concede bits que não tem** | Achado do mantenedor, conferido na fonte: `users.add` só limita `perms` a `PERM_ALL_BITS`, nunca à máscara de quem pede, e devolve a senha da conta nova em `creds`. Alcança tudo menos `/api/ota/apply` e `/api/tls` (que exigem `PERM_FULL_ADMIN` exato). Segundo vetor: `{"type":"pin","id":0}` define o PIN de painel do admin. **Não entra na campanha** — corrigir agora invalidaria o soak de 7,9 h já medido nesta imagem; PR próprio, ver [`security-audit/ACHADO_2026-09-21_V09…`](../security-audit/ACHADO_2026-09-21_V09_ESCALACAO_POR_USER_MGR.md) |
| 21/09 15:25 | **D-C1 pela web, na imagem publicada: 500 commits que gravam flash, 0 reboots** | 5,8 min, display vivo, instrumento validado antes (o `SYS_STORAGE_SAVE` sobe a cada commit). O reprodutor conhecido do B1 usa `write memory`, **comando que não existe na `pico_w_release`** — lá a CLI é o console de emergência. Então o gatilho conhecido é inalcançável na imagem que a versão publica, e o gatilho equivalente que existe nela não reproduziu em 500 tentativas |
| 21/09 15:11 | Resposta truncada: teste do socket ocioso | **0 quebras em 4 rodadas** de 5 min de silêncio, pela sessão mantida e por sessão nova. **Não refuta nada**: a 4% por requisição, 4 tentativas esperam 0,17 evento. Teste subdimensionado; fica aberto como B13 com os números que existem |
| 21/09 15:11 | O que o mesmo teste revelou (sem querer) | Os 401 que ele deu **eram dele**: `allocSessionSlot` reaproveita o slot da MESMA conta e invalida o token anterior (`WebManager_Auth.cpp:462`). O timeout ocioso é **15 min**, não 5. Foi para o manual do servidor antes que alguém deduzisse o contrário |
| 21/09 14:50 | Taxa da resposta truncada, em laço fechado | **0 em 150** requisições a 0,5 s — mas a leitura de linha-base, a primeira depois de minutos parado, quebrou. 4 das 96 amostras do soak (todas com 5 min de silêncio antes) quebraram. Hipótese em teste: **socket ocioso reaproveitado**, não o aparelho |
| 21/09 11:52 | T3: **uma resposta do `/api/status` truncada** (`InvalidChunkLength`) em 61 amostras (5,1 h) | Sem reboot (uptime seguiu), e a amostra seguinte leu normal. **Nenhum dos quatro contadores de aborto do aparelho mexeu**: `ad`=0, `cgd`=0, `cgg`=0, `cgx` parado em 5. Causa não estabelecida — vira taxa medida depois do soak, com um laço fechado de requisições. ⚠️ Os 5 `cgx` são **meus**: subiram às 10:52, quando o laço do dreno caiu e os downloads quebraram; não são comportamento do aparelho |
| 21/09 08:42 | T3: primeira "anomalia" do soak — `CORE1_KILL_quiet 0→1`, `CORE1_RELAUNCH 1→2` | **Era o instrumento.** `c1kq` não é morte: conta o `requestQuietMode` que o Core 0 pede em volta de uma escrita de flash (`AppManager_Boot.cpp:428`, `setBigSaveQuietCallback`). Neste boot houve **1** `SYS_STORAGE_SAVE` (08:39:10, logo depois de um alarme disparar) e **1** incremento, com `c1a` de volta a 20 ms, `c1s`=0, `fx`=0 e 0 reboots. Regra corrigida e testada sobre o domínio: 6 casos sintéticos, pega relançamento sem pausa, kill por lockout, por health e batimento congelado |
| 21/09 06:53 | O coletor real (192.168.3.206) saiu da rede sozinho, no meio do soak | Vira, sem querer, a metade "coletor morto" do T3 — e o registro mostra o comportamento inteiro: `ts` parou em 7, `tf`/`tr` sobem **1 por amostra de 5 min**, `pending` cresce **+5 por amostra** (h_int=1 min), heap e batimento do Core 1 parados. Nada perdido: os registros estão na flash |

### Notas destas corridas

⚠️ **O passo `admin` do `panel_users_hw_test.py` não foi rodado.** Ele executa
`user pin admin 2468` + `write memory` — **sobrescreve o PIN de admin do
mantenedor e grava na flash**. Rodar isso precisa de autorização explícita, e
nenhum outro passo depende dele (`state['admin_pin']` não é lido em lugar
nenhum).

🔴 **Dois processos órfãos de sessões antigas falsificaram uma corrida
inteira.** Um `alarm_collector.py` de **1 dia e 12 h** antes segurava a porta
18081 e gravava no scratchpad de *outra* sessão. O coletor novo não conseguiu
ligar, a suíte leu o arquivo vazio e reportou **5 falhas** — inclusive
"registro não assinado por quem agiu", que seria um defeito de auditoria. O
aparelho dizia o tempo todo *"Enfileirados 6, Enviados 6, Confirmados 6,
Falhas 0"*, e os registros estavam lá, no arquivo do órfão, com o `user`
correto. **Antes de qualquer corrida:** `ss -ltnp | grep python3`.

✅ **B8 FECHADO em 21/09.** O mantenedor deu a senha provisória de `admin`,
o `~/.simut-bench.env` foi corrigido e o login confere: `/api/login` 200 e
`/api/status` 200 na mesma sessão. O arquivo continua fora do repositório,
`chmod 600`.

🔴 **A limpeza do T7-C restaurava chaves que não existem.** Ela lia
`telServer`/`telPort` de um `/api/config` que emite `t_srv`/`t_port` — então o
`.get()` caía no default e o "restore" escrevia `tel server ' '` e `tel port
80` **por cima do coletor real** (192.168.3.206:8080 `/telemetry`), enquanto
`path`, `batch` e `mode` ficavam com os valores do teste. Corrigido: lê o que o
aparelho manda, devolve os seis campos e **confere lendo de volta** — a corrida
desta manhã terminou com a config idêntica à de antes, só o relógio adiantado.
Uma restauração que não se confere é como o coletor real fica apagado.

🔴 **As 3 falhas do `alarm_hw_test.py` são do instrumento, não da linha de
alarmes — e a prova está no próprio resultado dele.** A seção [06], que usa um
template de **29** caracteres, passou; as seções [02] e [03], que usam um de
**75**, falharam com o payload cortado depois de `"alarm":"alarm",`. Entre as
duas não há nada além do comprimento. A medição direta (70 → 63, 63 → 63,
62 → 62) fecha: é o `strVal2[64]`. A fila esvaziou, as métricas bateram
(`Enfileirados 2 = Confirmados 2`, 0 descartados) e os modos custom e CSV
passaram — a segunda linha está sã. ⚠️ O `alarm show` também **mostra** o
template cortado, em ~113 chars, o que esconde o defeito de quem o procura
pela CLI.

🔴 **`alarm_hw_test.py` não tem limpeza nenhuma.** Ele aponta a telemetria para
a bancada, troca `alarm set path`, os dois templates e deixa o modo em CSV — e
termina assim. Enquanto ele não ganhar um teardown, quem roda tem de restaurar
depois (nesta sessão, por snapshot do `/api/config` + conferência).

🔴 **B9 (novo, não bloqueia): o `rig_validate_history_clock.py` fixava a porta
8099**, que já estava ocupada por um servidor `node` da própria bancada — e
morria **no meio**, depois de ter mexido na config de telemetria do aparelho.
Agora a porta é `SIMUT_TEL_COLLECTOR_PORT` e a recusa diz o que fazer.

🔴 **PROVADO em 21/09, e é pior do que a dúvida: uma gravação é
indistinguível de um travamento no log.** A leitura de 21/09 ("as quatro
`ctx=227` são as gravações") estava certa na conclusão, mas ninguém tinha
provado. O experimento: gravar de propósito a `pico_w_release` publicada, pelo
caminho normal (toque de 1200 bps + `picotool load -x`), acampando na serial. O
boot seguinte imprimiu

```
[BOOT] WATCHDOG_REBOOT detected
[C0][FTL][SYS] System boot: HW WATCHDOG: Core 0 loop stalled (no feed in WDT
window). C0=[HIST_SAMPLE] C1=[DISPLAY] at up=537141248ms sc3=0x8008801b
hp=7049 (227)
```

e o anel guardou `SYS_BOOT ctx=227 lvl=4`. O `picotool` reinicia **pelo
temporizador do watchdog**, então a autópsia cai no ramo do estouro
(`LogManager.cpp:1022`) e carimba o módulo em que o Core 0 estava — `HIST_SAMPLE`,
que é onde o laço passa a maior parte do tempo. O comentário do ramo vizinho
(`:1033`, `ctx=0` INFO, "likely picotool upload") descreve um caminho que **este**
upload não toma.

**Consequência para o B1 e para a definição de stable:** o registro persistido
tem 12 bytes e guarda só código + `ctx`, então *"o aparelho travou no
HIST_SAMPLE"* e *"alguém gravou firmware"* produzem a MESMA linha. O único
sinal que os separa está no texto serial — o `up=537141248ms` (6,2 dias, resíduo
de scratch; o aparelho tinha 16 min) — e esse texto não sobrevive ao boot.
Enquanto isso valer, "todo reboot tem causa conhecida e registrada" tem um furo:
qualquer varredura de anel precisa saber a hora das gravações para não chamar
upload de travamento — ou o contrário. O `ctx=209` (módulo `CLI`) e o `ctx=455`
(trace vazio) do B1 **não** são desta família, e continuam de pé.

ℹ️ **`SYS_BOOT` só é gravado quando houve algo a dizer** (`LogManager.cpp`
955–1035): panic de software, estouro do watchdog, ou reset forçado externo.
Boot limpo e **queda de energia são silenciosos por desenho** — e o alvo vive
no USB do PC, que reiniciou às 05:35 desta manhã e levou o aparelho junto.
Ausência de `SYS_BOOT` é, portanto, o sinal saudável; não é o instrumento
falhando.
