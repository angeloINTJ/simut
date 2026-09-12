# Gestor de frota — o que o firmware precisa dar a quem cuida de centenas de SIMUTs

> **Branch:** `feat/fleet-api` (nada mesclado em `main`; nada gravado em aparelho).
> **Base:** `main` = `29ccf08`, v2.4.2-beta. **App:** `simut-rx` ≥ `d83ff97`, tela Frota.
> **Data:** 2026-09-11. **Estado:** código escrito e **compilado nos cinco envs**; portões de fonte
> (`check_authz`, `check_lang_packs`, `gen_logcodes --check`, `check_air_consistency`,
> `check_fsguard`) verdes; **nenhum byte testado em hardware** — a bancada estava em outro ensaio.
> O que cada item mede em flash está na §6, e o Air está no limite (§6.1).

Este documento é a metade do esboço `simut-rx/FROTA.md` que cabe a este repositório: o §7 de lá
(R1–R15) virou código aqui, com o custo medido, e os quatro fluxos que o app precisa — configuração,
telemetria, atualização e status — estão descritos como contrato entre os dois sistemas. Quem lê só
um dos dois lados lê metade: o app degrada quando o aparelho não responde ao que está aqui, e o
aparelho não sabe que existe um gestor.

---

## 1. O problema, em uma frase por fluxo

Uma base instalada de centenas de aparelhos idênticos, cada um com uma API web pensada para **uma
pessoa em um navegador**: sessão por cookie, três slots, bloqueio por IP, `commit_all` que reinicia,
OTA que reformata o sistema de arquivos. Um gestor precisa das mesmas quatro coisas que a pessoa,
só que sem olhar para cada tela:

| fluxo | a pergunta do gestor | o que faltava no firmware |
|---|---|---|
| **status** | quem é, que versão roda, está vivo, mudou alguma coisa? | identidade e versão espalhadas em três rotas com três permissões; nada dizia se o aparelho era alpha ou Air; nada dizia "a configuração mudou" |
| **telemetria** | o que chega no receptor é de qual aparelho, com que configuração? | o lote não carrega quem o mandou; o receptor só vê o IP de origem |
| **configuração** | aplicar o mesmo modelo em N aparelhos sem N surpresas | valor errado apagava campo sob 200; endereço inválido era ignorado em silêncio; validar exigia reiniciar; reiniciar exigia inventar um commit |
| **atualização** | mandar a imagem certa para o hardware certo, e saber que entrou | a imagem não diz para que hardware é; o aparelho aceita qualquer `.bin` com boot2 válido; um `COMMITTED` órfão sobrevive a um reboot; imagem entre 1016 e 1020 KiB era corrompida em silêncio |

O critério para cada mudança foi o mesmo do resto do projeto: **o firmware conta a verdade e recusa
o que não pode garantir**, em vez de aceitar e deixar o gestor descobrir depois.

## 2. Como os dois sistemas se falam — o contrato

### 2.1 Status (leitura, `PERM_DASHBOARD`)

`GET /api/status` passa a identificar o aparelho no bloco `sys`, na única rota que toda conta lê:

```
"sys":{"name":"…","ver":"2.4.2-beta","env":"release","uid":"E66038B713A72C34",
       "mac":"28:CD:C1:0A:3F:B2","cfg":"3A1F9C22","uptime":…,…,"cap":1}
```

- `ver` — antes só em `/api/perms`. Um poll identifica **e** versiona; a conferência pós-OTA deixa de
  precisar de segunda requisição.
- `env` — `release` | `alpha` | `air` (§2.4). `cap` continua, por compatibilidade, e continua não
  distinguindo alpha de Air.
- `uid` — o serial do RP2040, antes só em `/api/config` (`PERM_SYS_CONFIG`, e truncado logo após o
  boot). É a chave da credencial no app e do nó da árvore.
- `mac` — casa com o `mac` que o template custom da telemetria já emite.
- `cfg` — **CRC-32 da configuração em RAM**. Dois aparelhos com o mesmo `cfg` têm a mesma
  configuração; um `cfg` diferente do da última leitura é "alguém editou". É a resposta barata para
  "o modelo entrou em todos?" sem baixar `/api/config` de cada um.

`GET /api/status?quiet=1` autentica **sem rearmar o timer de hibernação do Air**. Um gestor que
lê vinte Airs não pode segurá-los acordados por isso; até aqui a única requisição que não rearmava
era a sonda pública.

`GET /api/perms` ganha `env` e `mc` (troca de senha pendente). Sem `mc`, uma sessão reaproveitada
só descobre a pendência pelo 409 da primeira escrita.

**401 contra 403.** Toda rota JSON passa por `requirePerm(bits)`: sem sessão viva é **401**
(`Unauthorized`), sessão sem o bit é **403** (`Forbidden`). Antes só `/api/perms` fazia a distinção;
o cliente não sabia se devia relogar ou desistir da conta sem uma segunda requisição.

**`Authorization: Bearer <SIMUTSESS>`** vale como o cookie — mesmo token, mesmos três slots, mesmo
prazo. É para cliente sem cookie jar; `Basic` continua sendo só do `/metrics`.

### 2.2 Telemetria (o aparelho empurra, o receptor correlaciona)

O payload **não muda** — o lote JSON é um array e o CSV uma tabela, e mudar a forma quebraria todo
receptor que os lê hoje. A identidade vai em **cabeçalhos** do POST:

```
X-SIMUT-Uid: E66038B713A72C34
X-SIMUT-Ver: 2.4.2-beta
X-SIMUT-Env: release
X-SIMUT-Cfg: 3A1F9C22
```

Um receptor que guarda os cabeçalhos (o simut-rx guarda) casa o IP de origem com o serial, vê uma
troca de versão e uma mudança de configuração **sem abrir sessão nenhuma** — é a "camada 0" da
política de rede do esboço, de graça. MQTT 3.1.1 não tem cabeçalho: lá a identidade continua sendo
o `clientId`.

### 2.3 Configuração (`commit_all`)

Três mudanças no mesmo handler, todas no sentido de "recusar em vez de estragar":

- **Valor não-string sob chave de texto vai para `rejected`**, não para a flash. `{"t_srv":null}`
  satisfazia o `has()`, o extrator devolvia `""` e o servidor de telemetria era apagado sob 200. Era
  a armadilha viva do §3.4 do esboço; o app já se defendia dela no montador, e agora o firmware
  também.
- **Endereço inválido em `net` vai para `rejected`** (`net.ip`, `net.mask`, `net.gw`, `net.dns`,
  `net.dns2`, `net.web_port`), em vez de ser descartado em silêncio com o aparelho reiniciando "com o
  IP velho" e a página dizendo "salvo".
- **`_dry=1`** roda todo portão e todo parser sobre uma **cópia** da configuração e responde
  `{"status":"dry","rejected":[…]}` sem gravar nem reiniciar. Só `sys` e `net` (as seções que viram
  modelo); `users` cunha senhas, `slots`/`calib` tocam arquivo, `alarms` mexe em estado de runtime —
  nada disso tem cópia onde rodar, e a resposta é 400 explícito. Um modelo é validado em N aparelhos
  **antes** de N reinícios. Não disponível no Air (§6.1), e o Air diz isso em vez de fingir.

`POST /api/action?op=reboot` (`PERM_SYS_CONFIG`; 409 com senha pendente, 503 durante calibração de
toque — os mesmos portões do commit) reinicia sem inventar um commit para isso.

### 2.4 Atualização (OTA)

O modo de bricar por imagem errada que o firmware aceitava — e "verificava" — fecha em três camadas:

1. **A imagem diz o que é.** `SIMUT_ENV_NAME` (`simut_config.h`, derivado de `SIMUT_AIR` /
   `SIMUT_DISPLAY_ALPHA` / `SIMUT_DISPLAY_TFT`) e uma etiqueta em `.rodata`
   (`BuildIdentity.cpp`): `SIMUT-ENV:air;v=2.4.2-beta;`. O gestor procura a etiqueta no `.bin`
   **antes** de subir um byte.
2. **O aparelho confere o que recebeu.** `ota_validate_staging` varre o staging em janelas de 4 KiB
   (com sobreposição do tamanho da etiqueta) e recusa com `v=7` (`ENV_MISMATCH`) quando a etiqueta
   nomeia outra variante. Imagem **sem** etiqueta é build anterior a esta: aceita como antes, e a
   resposta do stage diz `"env":""` para o cliente saber que a conferência não aconteceu.
3. **O teto passa a ser o teto de verdade.** `OTA_APP_SAFE_MAX_SIZE` = 1016 KiB: uma imagem entre
   1016 e 1020 KiB tinha o rabo nos setores 254–255 do staging, onde o snapshot da configuração é
   gravado — o validador via um CRC bom sobre bytes que o snapshot depois sobrescrevia, e o aparelho
   bootava uma imagem corrompida com todas as camadas dizendo sucesso. Validador e applier passam a
   recusar acima de 1016 KiB. (Este número morde o Air — §6.1.)

E uma mina desarmada no boot: metadata em `STATE_COMMITTED` **sem que o apply tenha rodado** (queda
de energia entre o commit e o `/api/ota/apply`) é limpa com uma linha no log. O staging é o sistema
de arquivos: o que o LittleFS gravou desde então foi por cima da imagem, e a metadata passava a
avalizar bytes que não existiam mais.

**Descoberta.** `_simut._tcp` no mDNS, com TXT `uid`, `ver`, `env`, `tls`: um browse de serviço lista a
frota sem abrir sessão e sem tocar no servidor web. Não no Air (a wake desliga o mDNS antes de o link
subir) nem no alpha por outra razão — o alpha não tem rádio de sobra para descoberta ativa, mas
responde ao browse como qualquer outro.

**Repositório, não firmware (R14).** `tools/release_manifest.py` renomeia as três imagens por
variante, confere a etiqueta de cada uma contra o env que ela diz ser, e escreve `manifest.json`
(`version`, `min_from`, `images.{release,alpha,air}.{file,size,sha256}`). O workflow
`release-ota.yml` anexa tudo ao release da tag. É a origem que o app não tinha: ele baixa o
manifesto, escolhe pelo `env` do `/api/perms`, confere o SHA-256 e a etiqueta, e só então faz stage.

## 3. O que muda no app quando o aparelho tiver esta branch

O app já está escrito para degradar: nada aqui é pré-requisito do que ele faz hoje.

| o app hoje | com a branch |
|---|---|
| perfil deduzido de `cap` e marcado "não confirmado"; OTA bloqueado por isso | `env` confirma; a página de atualização perde o primeiro motivo de bloqueio |
| 401 só no `/api/perms`; nas outras rotas o 403 é lido como "sem permissão" | `unauthorized` em toda rota → relogar uma vez, como já faz |
| identidade só depois do login, e serial só com `PERM_SYS_CONFIG` | `uid`/`ver`/`cfg` no status: adoção com conta de painel ganha serial; painel mostra "configuração mudou" |
| sinal de vida passivo casa por IP | os cabeçalhos casam por serial — dois aparelhos atrás do mesmo NAT deixam de ser uma origem só |
| aplicar modelo = N commits, N reinícios, N `rejected` | `_dry=1` em N aparelhos, um relatório, **depois** os reinícios |
| "reiniciar" não existe | `op=reboot` na tela do aparelho |
| adicionar por IP digitado | browse `_simut._tcp` lista os que estão na rede |
| OTA recusado (variante, origem, transporte) | variante e origem resolvidas; **falta o transporte** (§5) |

## 4. O que foi feito, arquivo a arquivo

| item | arquivos | portões tocados |
|---|---|---|
| R1 · R4 · R5 (identidade) | `simut_config.h` (`SIMUT_ENV_NAME`), `BuildIdentity.{h,cpp}` (etiqueta + scanner puro), `WebManager_Api.cpp` (`perms`: `env`,`mc`; `status`: `ver`,`env`,`uid`,`mac`,`cfg`), `StorageManager.{h,cpp}` (`getConfigCrc`) | `MANUAL.md` |
| R3 · R12 · R11 (sessão) | `WebManager.h` (`requirePerm`, `_quietRequest`), `WebManager_Auth.cpp` (401/403, Bearer, quiet), gates de `Api.cpp`/`Calib.cpp`/`Commit.cpp`/`Auth.cpp` convertidos | `check_authz` continua verde (o token `PERM_` segue no corpo); `AUTHORIZATION.md` |
| R8 (commit robusto) | `WebManager_Commit.cpp` (`jsonValueIsString`, `setIp`, `web_port` estrito, `net.dns2`) | — |
| R7 (`_dry`) | `WebManager_Commit.cpp` (cópia em `unique_ptr`, guardas nos setters de overlay, resposta `dry`) | `AUTHORIZATION.md` |
| R9 (`reboot`) | `WebManager_Calib.cpp` (`handleApiAction`), `@TRL` nos dois `.lng` | `check_lang_packs`, `AUTHORIZATION.md`, `MANUAL.md` |
| R5 (staging) | `ota_layout.h` (`OTA_APP_SAFE_MAX_SIZE`), `validation.{h,cpp}` (`ENV_MISMATCH`, `image_env`, varredura), `applier.cpp`, `WebManager_Ota.cpp` (`"env"` na resposta) | `MANUAL.md` §12 |
| R2 (mDNS) | `NetworkManager.{h,cpp}` (`setServiceAdvert`, `addService`/`addServiceTxt`), `WebManager_Core.cpp` | — |
| telemetria | `TelemetryManager.cpp` (`addIdentityHeaders`, nos dois POSTs) | — |
| R6a (boot) | `AppManager_Boot.cpp` (`COMMITTED` órfão), `@TRL` | `check_lang_packs` |
| R14 | `tools/release_manifest.py`, `.github/workflows/release-ota.yml` | — |
| R15 | os treze pontos do Apêndice D do esboço, em comentários e docs | — |
| não feito | R6b/c (`/api/ota/state`, `abort`), R10 (`perms` em users), R11 bloco `air` em `sys`, R13 (fingerprint TLS) | ver §5 |

## 5. O que fica, e por quê

- **O transporte do OTA no app (D5 do esboço).** `fetch` do RN roda sobre OkHttp com retentativa
  automática: um POST com corpo repetível é reenviado sozinho depois de um RST, e para o stage isso é
  um segundo reformat. O módulo Kotlin com `retryOnConnectionFailure(false)` e progresso de upload não
  foi feito. **É o único bloqueio real do OTA pelo app** depois desta branch; a tela diz isso.
- **R6b/c, R10, R13.** Opcionais no esboço, e o Air não tem flash para nenhum deles (§6.1). R10 (mudar
  `perms` sem `del`+`add`) é o mais útil dos três para uma frota; entra quando houver folga, ou só nos
  envs com tela.
- **O bloco `air` em `sys` (R11).** Precisa expor o estado da hibernação (`idle`, `awake`, `armed`)
  de `AppManager_Air`; `quiet=1` já resolve a metade que importa (ler sem segurar acordado). Custo no
  Air, que é justamente onde não cabe.
- **Native test para `simut_env_tag_scan`.** É puro e merece; o `[env:native]` compila por
  `build_src_filter` e `BuildIdentity.h` puxa `simut_config.h`/`SystemDefs_Limits.h` — entra junto
  com a validação em hardware, quando o scanner for exercitado contra um `.bin` de verdade.

## 6. Custo medido (flash, `pio run`, 2026-09-11)

`used` é o que o PlatformIO imprime; `.bin` é o que o slot guarda (o `.data` alinha em 4 KiB). O
teto físico é 1 044 480 B (1020 KiB); o teto **seguro** para OTA passa a ser 1 040 384 B (1016 KiB).

| env | `used` em `main` | `used` na branch | Δ | `.bin` na branch | folga até 1016 KiB |
|---|---|---|---|---|---|
| `pico_w_release` | 1 018 820 | **1 024 340** | +5 520 | 1 036 372 | 4 012 B |
| `pico_w_test` | 1 018 692 | **1 023 356** | +4 664 | 1 035 388 | 4 996 B |
| `pico_w_asserts` | 1 020 988 | **1 026 508** | +5 520 | 1 038 540 | 1 844 B |
| `pico_w_alpha` | 1 019 124 | **1 022 284** | +3 160 | 1 035 508 | 4 876 B |
| `pico_w_air` | 1 025 600 | **1 027 424** | +1 824 | 1 039 508 | **876 B** |

Os budgets em `tools/flash_budget.json` foram subidos para cobrir a medição com a folga de sempre
(~2–4 KB), e o motivo está no commit. O grosso do custo nos envs com tela é o serviço mDNS com TXT
(a LEAmDNS já estava linkada, mas `addService`/`addServiceTxt` não) e o `_dry`; os dois saíram do
Air pelo `#if !SIMUT_AIR`.

### 6.1 O Air está no limite — e já estava

O `.bin` do Air em `main` é **1 039 508 B**, contra o teto seguro de 1 040 384 B: **876 B** de folga
real para OTA, não os 4 972 B que a nota de 2026-09-09 do `flash_budget.json` contava contra o teto
físico. A diferença é exatamente o setor do snapshot. Esta branch mantém o `.bin` do Air no mesmo
tamanho (a soma cresceu 1 824 B, mas não cruzou o alinhamento de 4 KiB), e nada além dos 876 B cabe
sem cortar algo. Três consequências que valem mais que qualquer item do §4:

1. **Todo item futuro no Air é troca, não soma.** R10/R11/R6b não entram sem tirar.
2. O `check_flash_budget.py` mede `used`, não `.bin`; o Air precisa de um portão sobre o `.bin`
   contra `OTA_APP_SAFE_MAX_SIZE`, senão o dia em que o alinhamento cruzar, a imagem sai da CI válida
   e o próprio validador desta branch a recusa — o firmware bloqueando a própria atualização. Fica
   como pendência explícita, não escondida num orçamento.
3. Uma imagem de Air de `main` (sem etiqueta) continua atualizável por esta branch: é o caso "sem
   etiqueta, aceita, `env:""`".

## 7. Plano de implementação — o que falta, na ordem

| # | passo | onde | critério de aceite |
|---|---|---|---|
| 1 | Bancada com esta branch num aparelho com tela | firmware | `/api/status` com `ver/env/uid/mac/cfg`; 401 sem cookie em `/api/network`; `Bearer` abre sessão; `?quiet=1` e o timer do Air (num Air); `_dry=1` devolve `rejected` sem reiniciar; `op=reboot`; stage de um `.bin` de **outra** variante recusado com `v=7`; stage de um `.bin` de `main` aceito com `env:""` |
| 2 | Portão de `.bin` do Air | `tools/check_flash_budget.py` | falha quando `.bin` > 1 040 384 |
| 3 | `simut-rx`: consumir `env`/`uid`/`cfg` e os cabeçalhos | `fleet/protocol.ts`, `devices.ts`, `pages.ts` | perfil confirmado sem `cap`; correlação por serial no sinal de vida; "configuração mudou" no painel |
| 4 | `simut-rx`: `_dry` e modelo em lote | `devices.ts`, tela nova | modelo validado em N, relatório, depois ondas de 4 com conferência |
| 5 | `simut-rx`: browse `_simut._tcp` | módulo Expo `react-native-zeroconf` ou mDNS próprio sobre `react-native-udp` | lista da rede com `uid/ver/env`, "adicionar" pré-preenchido |
| 6 | Release com manifesto | tag `v2.4.3-beta` | `manifest.json` + três `.bin` anexados; `release_manifest.py` verde |
| 7 | `simut-rx`: transporte do OTA (D5) | `modules/simut-http/` (Kotlin, OkHttp, `retryOnConnectionFailure(false)`, progresso) | um POST de 1 MB **sem** reenvio depois de um RST injetado pela bancada |
| 8 | `simut-rx`: OTA de um aparelho (F3) | `fleet/ota.ts`, página de atualização | manifesto → SHA-256 → etiqueta → backup → stage → apply → versão lida depois do boot; um por vez |
| 9 | TLS/TOFU (F4) | módulo Kotlin + `/api/network` fingerprint (R13) | pin por aparelho, aviso quando muda |

Corte natural: **1–3 fecham "status e configuração de verdade"**; 4–6 fecham "frota como lote"; 7–9 é
o OTA, e é o que mais exige bancada.

## 8. Riscos e o que este documento não prova

- **Nada foi executado em aparelho.** Compilar nos cinco envs e passar nos portões de fonte diz que o
  código é coerente com o resto; não diz que a varredura do staging termina no tempo esperado nem que
  o `addServiceTxt` da LEAmDNS instalada responde ao browse. O passo 1 do §7 existe para isso.
- **`cfg` é o CRC da struct em RAM**, inclusive campos de overlay em `reserved`. Dois aparelhos com
  a mesma configuração *lógica* mas versões de struct diferentes têm `cfg` diferentes — é
  comparação dentro da mesma versão de firmware, e o app trata assim.
- **`X-SIMUT-*` custa ~90 B por POST.** Num Air a bateria: quatro cabeçalhos por lote, não por
  registro. Medir na bancada do Air se o custo de rádio aparece; se aparecer, condicionar ao
  `t_transport`.
- **`requirePerm` mudou o código de resposta de rotas existentes** (403 → 401 sem sessão). O WebUI
  trata os dois como "voltar ao login"; um cliente de terceiro que dependesse do 403 sem sessão
  quebra — está no `AUTHORIZATION.md`.
