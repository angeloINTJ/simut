# SIMUT × SIMUT-RX — Esboço de integração: gestão de frota pela API nativa (v2.4.2-beta · simut-rx 1.1.0)

> **Status:** esboço. Nada aqui está implementado em nenhum dos dois repositórios.
> **Data:** 2026-09-10.
> **Base firmware:** `main` v2.4.2-beta (`SIMUT_VERSION` em `src/SystemDefs_Limits.h:38`), 56 rotas HTTP
> (`python3 tools/check_authz.py --list`: 46 gated, 10 públicas, 0 sem portão).
> **Base app:** `angeloINTJ/simut-rx` 1.1.0 (`package.json`), Expo 53 bare, RN 0.79, TypeScript strict.
> **Idioma:** pt-BR (espelha `SIMUT_AIR_ESBOCO.md` e os `ANALISE_*.md`).
> **Fontes de verdade:** o código dos handlers, não os manuais. Onde manual e código divergem, o
> documento cita o código e lista a divergência no Apêndice D — neste projeto um comentário que não bate
> com o código é defeito, não ruído.
>
> **Como este documento foi feito.** Quatro leituras independentes do firmware (autenticação, superfície
> de leitura, superfície de escrita, OTA/backup/restore) com citação `arquivo:linha` para cada afirmação,
> mais leitura direta de `AGENTS.md`, `NetworkManager`, `platformio.ini`, `WebUI.h`, das ferramentas de
> bancada e do `simut-rx` (`README`, `ESBOCO`, `PLANO`, `CLAUDE.md`, `scripts/simut-api.py`). As áreas de
> descoberta, Air, segurança e arquitetura do app foram cobertas por leitura direta, sem a segunda
> passada de verificação adversarial que as quatro primeiras tiveram — estão marcadas **[a confirmar]**
> onde a confiança é menor.

---

## 0. Decisões propostas (para o maintainer confirmar antes da F0)

| # | Decisão | Impacto |
|---|---|---|
| D1 | O gestor de frota é um **segundo papel** do simut-rx, declarado como **exceção consciente** ao critério "o dispositivo empurra, o celular escuta" (`PLANO.md` §1 e §8). | Módulo próprio (`src/services/fleet/`), zero acoplamento com `record()`/`writeQueue`, registro em `PLANO.md` §8 como reversão deliberada. Ver §2. |
| D2 | **Fase 1 sem mudança de firmware.** Tudo que a F1–F3 precisa já existe na API de v2.4.2-beta. | As mudanças de firmware (§7) entram em paralelo, cada uma com medição de flash, e o app **degrada** quando o aparelho não as tem. |
| D3 | **Identidade do aparelho = `serial`** (id único do RP2040, 16 hex, `/api/config` → `serial`), com `mac` (`/api/network`) como reserva; IP e nome são atributos mutáveis. | Tabela `devices` chaveada por `uid`; renomear ou trocar de IP não duplica o aparelho. Ver §5.3. |
| D4 | **Duas credenciais por aparelho**, guardadas em `expo-secure-store`: uma conta operadora (≤ `PERM_ALL_BITS`) para status/config e o **admin do slot 0 só quando o operador for atualizar**. | OTA/backup/restore exigem `perms == PERM_FULL_ADMIN` (0xFFFF), que nenhuma conta criada pela web pode ter (`docs/AUTHORIZATION.md:30-53`). Ver §5.5. |
| D5 | **HTTP na fase 1; HTTPS com pin por impressão digital (TOFU) na fase 4**, via módulo Kotlin local. Nunca "aceitar qualquer certificado". | O `fetch` do RN rejeita autoassinado; o Air e o alpha são sempre HTTP; o manual já recomenda HTTP para OTA (`docs/MANUAL.md:373`). |
| D6 | **Nada em massa que mova o aparelho.** Seção `net` (SSID, senha, IP estático, porta) é sempre por aparelho, com confirmação individual, commitada **por último e sozinha**. | Um template com `net` errado tira N aparelhos do alcance do app de uma vez. Ver §6.4. |
| D7 | **OTA um aparelho por vez**, com backup verificado antes, e sucesso provado **só** pela versão lida de `/api/perms` depois do boot. | Cada tentativa de stage reformata o LittleFS do alvo (§3.5); o 202 do apply não prova nada. |
| D8 | **Cadência de leitura ≥ 30 s** por aparelho; **Air é oportunista** (tenta, e "sem resposta" = "dormindo"). | Toda requisição autenticada rearma o timer de hibernação do Air (`WebManager_Auth.cpp:58`); o poll de 3 s do navegador mataria a bateria. |

---

## 1. Objetivo e escopo

Usar a **API nativa do SIMUT** (a mesma que o `WebUI.h` consome) para que o **simut-rx** consiga, com
vários SIMUTs na mesma rede:

1. **ler status** — versão, uptime, RSSI, heap, sensores, telemetria pendente, alarmes, log;
2. **configurar** — ler a configuração, comparar com um template, aplicar e verificar;
3. **atualizar por OTA** — backup → stage → apply → prova de versão → restauração.

Fora de escopo, e por quê:

- **Encaminhar telemetria para nuvem/broker externo** — continua recusado (`PLANO.md` §8): é outro produto.
- **Dashboard remoto do que o app recebeu** — idem. O gestor lê o **aparelho**, não substitui a tela Fluxo.
- **Acesso por serial/Bluetooth** — o app fala só HTTP. Os comandos `air` (idle, charger, hibernate),
  `user perm <nome> admin` e os quatro comandos de recuperação são **só pelo cabo/BT** de propósito
  (`AGENTS.md`, V-01a); o esboço aponta onde isso obriga o operador a ir ao aparelho.
- **iOS** — o app compila, mas o alvo continua Android (mesma premissa do `ESBOCO.md` §10).

---

## 2. A exceção ao critério do simut-rx — e como ela não contamina a recepção

O `PLANO.md` §1 diz: *"O dispositivo empurra, o celular escuta. Protocolo que exige o app perguntar
primeiro é outro app. Nada aqui transforma o SIMUT-RX em cliente."* E o §8 recusa o encaminhamento
para nuvem exatamente porque *"a saída faz o app virar cliente: fila, retentativa, credencial de
terceiro, comportamento com a rede caída. É outro produto, e ele engole este em vez de estendê-lo. Se um
dia entrar, entra sabendo disso — não de lado, como efeito de outra etapa."*

O gestor de frota **é** o app virando cliente: fila, retentativa, credencial do aparelho, rede caída.
Então ele entra **sabendo disso**, e as quatro objeções viram regras:

| Objeção do §8 | Regra do gestor |
|---|---|
| **Fila** | Existe, mas é a **fila de trabalhos** do gestor (`fleet/jobs.ts`), separada da `writeQueue` da recepção. Nenhum trabalho do gestor toca `record()` (`serverManager.ts:164`) — a regra "o caminho quente não engorda" (`PLANO.md` §2) continua absoluta. |
| **Retentativa** | Só em GET, só em falha de transporte/JSON truncado, no máximo 3 (1,5 s · 3 s · 4,5 s, o que `tools/air_test_suite.py` já faz). **Nunca** em POST, **nunca** em credencial recusada (lockout exponencial, §3.2). |
| **Credencial de terceiro** | É credencial **do próprio SIMUT do usuário**, guardada em `SecureStore` por aparelho, mascarada na UI, **fora** do export de perfil (mesma decisão da E8: *"Senha do MQTT e chave do HTTP saem em branco por padrão"*), fora do banco de mensagens e fora do log de eventos. |
| **Rede caída** | Estado explícito por aparelho: `online` · `dormindo` (Air) · `sem resposta` · `credencial recusada` · `bloqueado até HH:MM` (lockout). O gestor nunca fica "tentando" em silêncio. |

**O que a exceção compra, e que o papel receptor não tinha:** a tela Rede hoje gera uma "receita" que o
operador precisa **digitar no formulário web do SIMUT** — o `CLAUDE.md` registra que *"quem preenche o
formulário do dispositivo costuma estar no computador, não no celular que recebe"*. Com o gestor, a
receita vira **um `commit_all`** disparado do próprio app ("apontar este SIMUT para mim", §6.5). É o
fluxo que junta os dois papéis num só.

**Onde fica no app:** um grupo de telas **Frota** dentro de **Mais** na F1 (sem mexer nas 5 abas), com
promoção a aba se o uso justificar. Serviços em `src/services/fleet/`, todos com a mesma divisão que o
resto do app: função pura com teste em `__tests__/`, I/O sem teste e verificado no aparelho.

---

## 3. O que o firmware oferece hoje (v2.4.2-beta, lido no código)

Esta seção é o contrato que o cliente TypeScript vai implementar. Cada linha tem a citação. O Apêndice A
condensa tudo em uma tabela por rota.

### 3.1 Identidade e descoberta

| Fato | Onde |
|---|---|
| mDNS anuncia **só o hostname** `<deviceName>.local` (`MDNS.begin(_deviceName)`), sem serviço nem TXT. Não há como "listar SIMUTs" por mDNS; só resolver um nome que já se conhece. | `src/NetworkManager.cpp:282`; nenhum `addService` em `src/` |
| O nome padrão é o mesmo em todo aparelho novo — dois aparelhos sem renomear **colidem** em `.local`. `sys.name` do `commit_all` é copiado verbatim para o mDNS. | `src/NetworkManager.cpp:38`, `docs/MANUAL.md:122` |
| **`pico_w_alpha` compila sem mDNS** (`-DSIMUT_MDNS=0`); `pico_w_release` e `pico_w_air` com. O Air só o sobe em M0. Custo medido do mDNS: **15.376 B** de flash. | `platformio.ini:367`, `:208`, `:418` |
| **Não há endpoint público de identidade.** Antes do login só existem `/api/login_init` (nonce), `/api/lang`, `/lang.js`, `/style.css`, `/favicon.ico`, `/apple-touch-icon.png` (204 sem corpo). | `docs/AUTHORIZATION.md` "Unauthenticated by design"; `tools/check_authz.py:67-68` |
| A **identidade estável** é o id único do RP2040 (16 hex maiúsculos): `serial` em `GET /api/config` (exige `PERM_SYS_CONFIG`) e `picoUID` em `GET /api/calib` (`PERM_CALIB`). O MAC só em `GET /api/network` (`PERM_NET_CONFIG`). `/api/status` **não** traz nenhum dos três. | `src/StorageManager.cpp:1595-1599`; `src/WebManager_Api.cpp:609-622` (chaves de `sys`) |
| A **versão do firmware** só sai em `GET /api/perms` (`"version"`) e em `/metrics` (`simut_build_info{version=…}`). **`/api/status` não tem versão** — `docs/OTA_USAGE.md:42` manda ler de lá e está errado. | `src/WebManager_Api.cpp:37-43`; `src/WebManager_Metrics.cpp:150-155` |
| A única pista de **variante de build** na rede é `sys.cap` em `/api/status` (`== SIMUT_DISPLAY_TFT`): 1 = TFT; 0 = alpha **ou** Air, indistinguíveis. | `src/WebManager_Api.cpp:612-622`; `platformio.ini:361`, `:407-418` |
| Modo AP (credenciais Wi-Fi erradas ou aparelho novo): portal cativo em **192.168.4.1**, **HTTP só**, WPA2 com chave **derivada** do id da placa (V-05), impressa por `show system info`. | `src/NetworkManager.cpp:91-142`; `src/ApPsk.h`; `src/WebManager_Core.cpp:286-289` |

**Consequência para o desenho:** na F1 a descoberta é (a) IP/host digitado, (b) **passiva, pelo próprio
fluxo de recepção** — todo SIMUT que empurra telemetria já aparece como origem no app, e "promover esta
origem a aparelho gerido" é um toque —, e (c) varredura de sub-rede **explícita** e barata (§6.2). A
descoberta por serviço mDNS é mudança de firmware (R2) e módulo nativo no app.

### 3.2 Autenticação, sessão e bloqueio

| Fato | Onde |
|---|---|
| Único mecanismo para as rotas JSON: **cookie de sessão** `SIMUTSESS=<32 hex>`. `GET /api/login_init` → `{"nonce","locked","lockSec"}`; `POST /api/login` (`application/x-www-form-urlencoded`: `user`, `pass`, `nonce`) → `{"ok":true,"redirect":"/"}` + `Set-Cookie`. **Não existe** API key, bearer nem token de longa duração. | `src/WebManager_Auth.cpp:245-298`, `:478-485`; `:32-63` (`getAuthPerms` lê só `Cookie`) |
| `pass` = **SHA-256 hex da senha com cada caractere como UM byte (latin-1)**, não UTF-8. Code points > U+00FF não são representáveis. O navegador só hasheia se o campo não tiver 64 chars. | `WebUI.h:115`, `:125`; `src/StorageManager.cpp:1835-1840`; `tools/web_test_suite.py:215-222` |
| O nonce **não entra no hash**; é comparado verbatim (anti-replay). Vive **60 s**, histórico de profundidade 1 (os dois últimos valem). O cabeçalho de `WebManager.h:6-7` fala em "HMAC nonces" — comentário desatualizado. | `src/WebManager_Auth.cpp:353`, `:274-278`; `src/WebManager.h:184` |
| O cookie é casado por **substring** do header `Cookie`; `HttpOnly`/`SameSite=Strict`/`Secure` são diretivas para navegador que o firmware não verifica. Um cliente nativo manda `Cookie: SIMUTSESS=<token>` e pronto. Não há checagem de `Origin`/`Referer`/CSRF fora do nonce. | `src/WebManager_Auth.cpp:49-53`; `src/WebManager_Core.cpp:78-80` |
| **3 slots de sessão**, em RAM, **15 min** de ociosidade (renovada a cada requisição autenticada), sem vida máxima. **Todo reboot apaga todas** — `commit_all`, `/api/ota/apply`, `history_rebind` e cada wake do Air. | `src/WebManager.h:148-155`; `src/WebManager_Auth.cpp:22`, `:54` |
| **O mesmo usuário não tem duas sessões**: novo login reaproveita o slot dele e derruba a anterior em silêncio (o navegador do maintainer e o app, ambos `admin`, se deslogam mutuamente). O 4.º usuário distinto recebe `403 {"ok":false,"err":3}`. | `src/WebManager_Auth.cpp:430-432`, `:530-533` |
| **Lockout por IP** (8 slots), compartilhado por todos os usuários e pelo Basic de `/metrics`: cada falha custa `(1 << falhas)` s — 2 s na 1.ª, **300 s a partir da 9.ª**, contador satura em 12. Conta como falha: senha errada, entrada fora do tamanho, nonce **expirado** mas correto, credencial Basic errada. Nonce inválido/ausente **não** penaliza. Sob lockout, `login_init` ainda responde (com `locked:true`), `/api/login` dá 403 `err:2`, `/metrics` 429; com os 8 slots bloqueados, `login_init` dá 429 + `Retry-After`. | `src/SystemDefs_Network.h:182`, `:203-220`; `src/WebManager_Auth.cpp:199-204`, `:249-268`, `:355-371`, `:492-493` |
| Verificar senha custa **~0,4–0,7 s** de CPU do aparelho (HMAC-SHA256, 5000 rodadas, pepper = id da placa). Basic em `/metrics` paga isso **a cada requisição**. | `src/SystemDefs_Network.h:184-188`; `src/StorageManager.cpp:1888-1895` |
| Sem sessão, `/api/perms` responde **401**; quase todas as outras rotas respondem **403** — o mesmo código de "sem permissão". O cliente não distingue "sessão morreu" de "conta sem o bit" pelo status. | `src/WebManager_Api.cpp:27` vs `:52` e demais handlers |
| **Troca de senha obrigatória**: admin de fábrica (slot 0) e toda conta criada/resetada pela web nascem com `mustChangePassword`. O login responde `{"ok":true,"redirect":"/force_chpass"}`; `commit_all` e `clear_logs` dão **409** até `POST /api/force_chpass` (`p1`,`p2`: sha256 hex sobre HTTP, texto puro + política sobre HTTPS). As outras rotas JSON funcionam; `/api/perms` **não** expõe a pendência. | `src/WebManager_Auth.cpp:82-86`, `:623-668`; `src/WebManager_Commit.cpp:245-249` |
| **Fronteira de privilégio**: contas criadas pela web têm no máximo `PERM_ALL_BITS` (0x03FF). `GET /api/backup`, `POST /api/restore?op=stage\|apply` e `POST /api/ota/apply` exigem `perms == PERM_FULL_ADMIN` (0xFFFF) **exatamente** — só o admin do slot 0 ou uma conta promovida pela CLI serial (`user perm <nome> admin`). Deliberado; não afrouxar. | `docs/AUTHORIZATION.md:30-53`; `src/WebManager_Ota.cpp:56`, `:166-171`, `:189-191`; `src/WebManager_Commit.cpp:1106-1108` |
| `GET /logout` zera o slot e expira o cookie. Uma sessão por operação + logout no fim é o que libera o slot para o humano. | `src/WebManager_Auth.cpp:539-561` |
| **No Air**, só requisição **autenticada** rearma o timer de hibernação (`getAuthPerms` com cookie casado, `completeLogin`, Basic bem-sucedido). Pré-login tem **orçamento de 3 extensões por boot** (`WEB_PREAUTH_MAX_EXT`), gasto por `GET /api/login_init` e por Basic falhado. Os funis `safeSend` **não** rearmam (V-03). | `src/AppManager_Boot.cpp:1145-1150`; `src/WebManager_Auth.cpp:58`, `:217-220`; `AGENTS.md` "Só uma requisição AUTENTICADA rearma" |

### 3.3 Superfície de leitura

**`GET /api/status`** (`PERM_DASHBOARD`, chunked, `Cache-Control: no-cache`) — `src/WebManager_Api.cpp:553-737`:

```
{"sys":{"name","uptime"(ms!),"rssi"(dBm, -100 sem Wi-Fi),"ip","theme","heap_f","heap_t","heap_lb",
        "fs_u"(cache 10 s),"fs_t","time"(epoch s),"ntp"(0|1),"pending"(registros, -1 sem telemetria),
        "tel"(0|1),"hi"(minutos),"cap"(0|1 = TFT)},
 "metr":{29 contadores desde o boot: lb,lbm,hm,wf,mq,rmn,rmx,ts,tf,tr,tb,tl(ms),so,se,cs,fo,fom(ms),fot(ms),f50,fx,ad,c1a(ms),…},
 "sensors":[{"val": número | "Error" | "--", "hum"?, "press"?, …}]}
```

- **`uptime` é em milissegundos** — a armadilha n.º 2 da bancada (`AGENTS.md`: "ler como segundos dá 23,5 h
  num aparelho com 84 s de vida"). `val` pode ser **string**. `hum`/`press` só existem quando o canal
  existe e o valor é finito.
- Cada chamada custa uma sondagem de heap (~16 malloc/free), um ioctl de RSSI e, no Air, o rearme do
  timer (`:589-596`; `MetricsManager.h:157-159`). Tamanho estimado: ~0,8 KB com 2 sensores, ~3 KB com 16.
- O navegador faz poll a cada **3 s** (`WebUI.h:690`) com `fetchSafe` (timeout 15 s, 2 retentativas,
  1 s/2 s, 5xx ≠ 503 retentável — `WebUI.h:6990`). **Não copiar a cadência** (D8).

Demais rotas de leitura (formas exatas no Apêndice A): `/api/perms` (versão, perms, ntp, time, idioma;
buffer de 336 B), `/api/config` (todos os campos que o `commit_all` ecoa, `t_key` mascarado `abcd***`,
sem `m_pass`, `serial`, `sensors[16]{hwid,active,hum,press}`), `/api/network` (ip/mask/gw/dns, `mac`,
`ssid`, DHCP/estático, `ntp_server`, `web_port`, `web_ka`, `web_tls`), `/api/sensors` (**mapa de slots +
catálogo de drivers, não leituras** — `docs/MANUAL.md:857` está errado), `/api/alarms`, `/api/users`,
`/api/sec_status` (tabela de lockout por IP), `/api/themes`, `/api/logs` (binário cru, 12 B/registro,
**sem paginação**, 429 a 200 ms/IP, trava a tela com "web busy"), `/api/export/logs.bin?from&to&level`
(envelope SIMX + CRC32, ≤ 31 dias), `/metrics` (Prometheus ~2,5 KB, cookie **ou** Basic).

Servidor: **um cliente por vez** (`_currentClient` único), até 4 requisições por tick de 50 ms, deadline
de 6 s por handler (15 s nas pesadas); keep-alive **ligado** desde v2.3.0; JSON em fatias de 512 B com
4 s de tolerância por fatia (leitor mais lento que ~132 B/s é derrubado); resposta abortada é fechada com
RST → **JSON truncado é possível** e `/api/config` **sabidamente** vem truncado logo após o boot
(`tools/air_test_suite.py:573-577`). Nada JSON tem ETag ou gzip. Sobre TLS, uma **segunda conexão
simultânea é descartada** (`src/WebManager_Core.cpp:376-395`, `src/SystemDefs_Network.h:289-300`,
`src/WebManager.h:93-97`).

### 3.4 Superfície de escrita

**`POST /api/commit_all`** — `src/WebManager_Commit.cpp:220-1373`, `src/WebCommitSections.h`:

- Corpo `application/x-www-form-urlencoded` com **um** campo `_payload` = JSON de 1..6144 B. Não é
  corpo JSON.
- Seis seções planas: `sys`, `slots`, `calib`, `alarms` (`PERM_SYS_CONFIG`), `net` (`PERM_NET_CONFIG`),
  `users` (`PERM_USER_MGR`). O portão é um `indexOf` **plano** sobre o corpo inteiro — um template com a
  substring `"net"` dentro de um texto livre (`t_glob`) custa 403 a uma conta restrita. Tudo-ou-nada.
  `_payload={}` → 400 `No section` (não é botão de reboot).
- Ordem interna: pré-checagem de limites de alarme → `slots` (valida, depois aplica) → `sys` → `alarms`
  → `users` → `net` → `calib` → um `saveConfiguration()` → resposta **200 `{"status":"ok"[,"newPort"]
  [,"rejected":[…]][,"creds":[{"u","p"}]]}`** → fecha o socket → **reboot** (`safeReboot`). Em TFT há
  ~3 s de telas de status **antes** da resposta. **Não existe commit sem reboot nem modo de ensaio.**
- Campo **ausente mantém o valor gravado** (guardas `has()`/`jsonValuePos`). Booleano aceita só `1`/`0`
  e `true`/`false` (com ou sem aspas). Inteiro estrito. Fora de faixa **não é erro**: vai para `rejected`
  e o aparelho reinicia mesmo assim. Só `slots` inválido e limite de alarme fora do canal dão 400 sem
  tocar em nada.
- A cicatriz do "espaço depois dos dois-pontos" (`scripts/simut-api.py:24-27`) **está corrigida** em
  v2.4.2-beta para sys/net/users/slots/alarms (`WebManager_Commit.cpp:37-45`) — a sub-seção `calib` ainda
  exige a aspa colada, mas falha **fechado** (ignora). Manter JSON compacto de qualquer forma: aparelhos
  antigos ainda existem.
- **Armadilha viva:** valor **não-string** (`null`, número) sob chave string de `sys`/`net` satisfaz
  `has()`, `jsonExtractStringValue` devolve `""` e o campo é **apagado sob 200** (`t_srv`, `t_path`,
  `t_glob`, `t_line`, `t_sep`, `m_topic`, `m_cid`, `m_user`, `a_*`, `ntp_server`, `t_key` sem `***`).
  Exceções guardadas: `name` (obrigatório não-vazio), `m_pass`, `ssid`, `pass` (vazio = mantém). O
  montador de payload do app **tem** de remover `undefined`/`null` e tipar cada campo (§5.1).
- `net`: `ip/mask/gw/dns` inválidos são **ignorados em silêncio** (não entram em `rejected`) e só valem
  com `use_dhcp=false`; `web_port` usa `toInt()` não-estrito. **HTTPS não é campo de config**:
  `cfg.useHttps` é morto; TLS existe se `/config/web_cert.pem`+`web_key.pem` existirem no boot, e com
  porta 80 o listener vai para 443 (`src/WebManager_Core.cpp:210-226`).
- `users`: só `add` / `del` / `reset` — **não há** ação de editar permissões (o `AUTHORIZATION.md:32`
  diz "editing"; defeito de doc). `add` cunha senha temporária de 8 chars devolvida **uma vez** em
  `creds`, com `mustChangePassword`. Slot 0 não se apaga nem reseta.
- O JS de referência acumula as seções em `window.Pending` (sessionStorage), manda checkboxes como
  `'1'`/`'0'`, aplica `/api/calib` **antes** (e espera 1 s pelo rate limit) e só então o `commit_all`
  (`WebUI.h:7197-7337`). Ao cair a conexão, assume que o reboot começou e recarrega em 12 s
  (`:7329-7336`) — a mesma ambiguidade que o app herda.

Outras escritas (`Apêndice A`): `POST /api/action?op=` **exatamente** `tel_sync`, `tel_reset`,
`sensor_scan`, `scan_results`, `sensor_wipe&slot`, `sensor_accept&slot` (`PERM_SYS_CONFIG`, nenhum
reinicia; **não existe `reboot` nem `factory`** por HTTP); `POST /api/set_time` (corpo JSON cru
`{"epoch":N}`, imediato, sem reboot); `POST /api/calib` (JSON cru ≤ 8192 B, `PERM_CALIB`, exige NTP,
1 chamada/5 s); `POST /api/save_sys` (só o tema); `POST /api/history_rebind` (**reinicia**);
`POST /api/clear_logs` (`PERM_LOGS` **e** `PERM_SYS_CONFIG`); `POST /api/reset_touch_cal` (deixa o TFT
no assistente — jamais em massa).

### 3.5 OTA, backup e restore

Sequência de referência (`WebUI.h:5823-5924`, `docs/OTA_USAGE.md`), todos os passos com `perms ==
PERM_FULL_ADMIN`:

| Passo | Rota | O que acontece | Medido |
|---|---|---|---|
| 1 | `GET /api/backup` | Despeja o LittleFS inteiro: cabeçalho de 40 B (`BKP1`, chip id, versão, `payload_size`, `payload_crc32`) + TLV por arquivo. Headers `X-Backup-Files/-Schema/-PSize/-PCrc`. Inclui `/config/system.bin` (**todos os segredos e hashes**) e os PEM de TLS. **Preso ao chip id**: não se restaura em outro aparelho (st=6). | ~794 KB numa unidade de bancada; 3 downloads seguidos já causaram reboot por watchdog antes do dreno (`WebManager_Ota.cpp:141-143`) |
| 2 | `POST /api/restore?op=stage&commit=1` (multipart, campo `file`, o **`.bin` cru**, não `.uf2`) | No **primeiro byte**: snapshot da config em RAM, Core 1 congelado, `LittleFS.end()`. O upload vai direto para a partição de staging (**que é a partição do LittleFS**). No fim: snapshot gravado nos 2 últimos setores, validação (100 KiB ≤ tamanho ≤ 1020 KiB + CRC do boot2), e com `commit=1`+válido grava metadata `COMMITTED` **sem remontar** o FS. Resposta 200 `{"st":5,"v":0,"committed":1,"dsize","dcrc",…}` ou 422. | ~29–31 s por ~1 MB (32 KiB/s); leitor multipart tolera 3 s de silêncio por byte; roteadores domésticos matam fluxos na porta 80 aos ~13 s (`CHANGELOG.md:2059-2073`) |
| 3 | `POST /api/ota/apply` | 202 `{"accepted":true}` **antes** do teardown; 409 se não há `COMMITTED`; 503 `Display in use` (+`Retry-After: 5`) se alguém toca o TFT. Depois: `WiFi.end()`, applier em SRAM, reset por watchdog. `?test=1` existe e **nunca** deve ser enviado. | 202 em 0,1 s; applier 25,1 s; boot até "image verified" 9,4 s; **48,4 s** sem web (21/21 ciclos) |
| 4 | prova | O boot seguinte confere o CRC do slot e loga `SEC_CONFIG_CHANGED` (303) em INFO ("image verified") ou ERROR. **A única prova é a versão em `GET /api/perms`.** | `src/AppManager_Boot.cpp:653-721` |
| 5 | `POST /api/restore?op=validate` → `?op=apply` | Só `/config/system.bin` sobrevive ao apply (via snapshot de 8 KiB). **Perde-se:** `/history`, packs de idioma, temas, `calib.csv`, log de eventos, **PEM de TLS**. `validate` primeiro é obrigatório: `apply` grava cada arquivo no caminho final enquanto recebe (sem rename), e um CRC ruim no fim não desfaz sobrescritas. Sucesso → o aparelho **reinicia de novo**. | `src/ota/restore.cpp:224-231`; `src/WebManager_Ota.cpp:433-444` |

Armadilhas que o gestor tem de codificar, não documentar:

- **Todo stage reformata o FS** — rejeitado (422), interrompido, sem `commit=1` (ensaio), tanto faz. Sem
  backup verificado em mãos, não se inicia stage. Retentativa "cega" de upload é o pior laço possível.
- **`COMMITTED` sobrevive ao reboot; os bytes do staging não** (o boot reformata a região). Um reboot
  entre stage e apply, ou um segundo stage que falha na validação, deixa uma mina: `apply` responde 202 e
  copia lixo para o slot. **Regra:** só chamar `apply` na **mesma sessão**, imediatamente após um stage
  `200 && v==0 && committed==1`, e **abortar se `sys.uptime` diminuiu** entre os dois. **[a confirmar
  no ferro — conclusão por leitura de código]** (`src/AppManager_Boot.cpp:655-656` só limpa
  `APPLYING`/`POST_BOOT`). A correção R6 fecha isso no firmware.
- **Nenhuma checagem de versão, downgrade ou variante**: a validação é tamanho + CRC do boot2. Uma
  imagem `pico_w_air` num TFT (ou vice-versa) é aceita e "verificada". A responsabilidade de casar
  imagem ↔ aparelho é **inteira do gestor** (§6.6) até o R5 existir.
- **Abaixo de v1.6.2-beta o applier era defeituoso**: relatava sucesso, apagava o FS e não instalava
  nada (`docs/MANUAL.md:620-632`). O gestor **recusa** OTA em `version < 1.6.2`.
- **Teto prático de imagem: 1016 KiB** (o snapshot ocupa os setores 254–255 do staging;
  `src/ota/ota_layout.h:448-458`). A release atual tem ~1004 KiB.
- **Air:** um wake M1 **não sobe o servidor web** — OTA só em M0 (boot frio, `air stop`, ou carregador
  em GP17, que cancela o M1 daquele boot). Cada requisição autenticada estende a janela. Não chamar
  `cyw43_arch_deinit()` (Fix #2 revertido) é problema do firmware, não do app, mas explica por que a
  janela pós-apply é longa (`AGENTS.md` "SIMUT Air").
- **Artefatos de release:** os scripts do repo geram só **zips de fonte** (`tools/build_release*.sh`);
  nenhum job de CI publica o `simut_vX.Y.Z.bin`, e o nome **não carrega o env**. Para o app baixar e
  escolher a imagem certa é preciso um **manifesto de release** (§7, R14).

### 3.6 SIMUT Air — o que muda para um gestor

| Fato | Onde |
|---|---|
| Boot frio = **M0** (web + serial + BT + sensores). `air hibernate` ou `air idle` (padrão 300 s) sem requisição **autenticada** → **M1** (deep sleep, acorda no RTC no intervalo do histórico, lê, grava, envia se `pending ≥ t_int`, dorme). | `AGENTS.md` "SIMUT Air — build headless"; `src/AppManager_Air.cpp:6-7` |
| Em M1 **não há porta 80**. "Connection refused" num Air é **"dormindo"**, não "morto". O T10 da suíte mede isso. | `src/AppManager_Boot.cpp:1131-1137`; `AGENTS.md` "Um wake M1 NÃO sobe web" |
| Boot **limpo** (power, RUN, `reload`, OTA) dá o `air idle` inteiro; boot **sujo** (watchdog) dá 10 s (`AIR_RESUME_GRACE_SEC`), até 3 boots sujos. Carregador alto em GP17 mantém M0 e suspende o idle. | `AGENTS.md` F25 e "Carregador no GP17" |
| Os campos do Air (`idleTimeoutSec`, `stabTimeoutMs`, `chargerPin`, `connectTimeoutMs`, `flushTimeoutMs`, `sensorPowerPin`, `flags`) vivem em `/config/air.bin` e são **só CLI** (`air idle`, `air charger`, …). O `commit_all` **não tem seção `air`**. O que a web controla no Air é a **cadência**: `sys.h_int` (intervalo do histórico = intervalo de wake, 1..1440 min) e `sys.t_int`/`t_bat` (lote mínimo/máximo). | `src/air/AirConfig.h:75-97`; `src/WebCommitSections.h:86-93`; `AGENTS.md` "DUAS CADÊNCIAS, UM ALARME" |
| `/api/status` **não expõe** fase, `armed`, `wake` nem `idle` — só `cap:0` (igual ao alpha). `air status` na CLI mostra `wake= hist= backoff= idle= armed= dirty= chg= wip=`. | `src/WebManager_Api.cpp:609-622`; `AGENTS.md` |
| Air é sempre **HTTP** (`SIMUT_WEB_HTTPS` não definido no env), com mDNS em M0, BT CLI ligado, CLI serial reduzida (`SIMUT_CLI_FULL=0`). Flash a **~97 %**: folga real ~4,9 KB (`tools/flash_budget.json`, nota de 2026-09-09). | `platformio.ini:407-418`; `tools/flash_budget.json` |
| **Nenhuma medida de idle vale com sessão aberta** — o próprio gestor, fazendo poll, é o que impede o Air de dormir (`ss -tn \| grep <ip>` é o diagnóstico). | `AGENTS.md` "Nenhuma medida do `air idle` vale com uma aba do painel aberta" |

**Regra do gestor para Air:** perfil "bateria" por aparelho (marcado pelo operador; `cap:0` sozinho
não basta). Nada de poll periódico: tenta **quando o usuário abre** o aparelho ou quando um trabalho
está pendente, e ao conseguir uma sessão **executa tudo de uma vez** (cada requisição autenticada estende
a janela) e faz `GET /logout` no fim para não segurar o aparelho acordado. Estado visível: *"dormindo —
visto pela última vez às HH:MM"*. Para OTA, o app **instrui** o operador: colocar no carregador ou
reiniciar e agir dentro do `air idle`.

### 3.7 Transporte

- **Um servidor só**, HTTP **ou** HTTPS, decidido no boot pela presença do par PEM; HTTPS na 443 quando a
  porta configurada é 80; **um cliente TLS por vez**; certificado autoassinado por aparelho (EC P-256
  recomendado); não há como ler a impressão digital pela API nem pela CLI (`system https off confirm`
  só apaga). `GET /api/network` diz `web_tls` e `web_port` — depois de logado.
- Variantes: `release` = HTTPS possível + mDNS; `alpha` = HTTP só, **sem mDNS**; `Air` = HTTP só, mDNS
  em M0; AP = HTTP só em 192.168.4.1.
- O limite de **record TLS ≤ 4096 B** (`setBufferSizes(4096, 512)`) é do SIMUT como **cliente** de
  telemetria e continua valendo para os servidores do simut-rx; para o gestor (SIMUT como servidor
  BearSSL) o que importa é o cliente único e a lentidão do upload de 1 MB sob TLS
  (`docs/MANUAL.md:365-375`).

---

## 4. O que o simut-rx oferece hoje, e as regras que valem para o módulo novo

| Já existe | Onde | Serve para |
|---|---|---|
| Cliente Python da API do aparelho (nonce → sha256 → form → cookie jar; `config`, `status`, `action`, `commit`, `wait_up`) com as três lições que custaram tempo | `scripts/simut-api.py:1-122`; `CLAUDE.md` "Dirigir o dispositivo em testes" | Semente do `fleet/protocol.ts`. **Não copiar** `http://` fixo sem porta, nem a explicação da cicatriz do espaço (corrigida no firmware). |
| `sha1.ts` escrito à mão, síncrono, com vetores oficiais — e o **argumento** (Hermes não tem digest; `expo-crypto` é assíncrono) | `src/services/sha1.ts:1-22` | Mesmo molde para `sha256.ts`. Aqui **é** primitiva de autenticação, então os vetores FIPS 180-4 + o caso latin-1 são obrigatórios. |
| `config.ts`/`profile.ts`: config em `SecureStore` como JSON único, merge com `DEFAULT_CONFIG`, perfil versionado que **exclui segredos por padrão** | `src/services/config.ts:89-122`; `src/services/profile.ts:29-78` | Molde para a config do gestor; **credenciais ficam fora do perfil**. |
| `db.ts`: `migrate()` por `user_version` (hoje 6), tabela nova entra pelo DDL idempotente sem bumpar versão | `src/services/db.ts:20-231`; `PLANO.md` §3 | `devices`, `fleet_jobs`, `fleet_events` entram como **tabelas novas** (DDL), sem migração numerada. |
| `store.ts`: `revision`/`slowRevision` com `useSyncExternalStore`; listas leem `slowRevision` e ficam atrás de `useIsFocused` | `src/services/store.ts:75-120`; `PLANO.md` §2 | O gestor tem **seu próprio** store (`fleet/store.ts`); nunca bumpa a `revision` da recepção. |
| Módulo Expo local em Kotlin (`modules/foreground-service`) | `modules/foreground-service/` | Molde para dois módulos nativos futuros: **cliente HTTP com pin TOFU** (F4) e **NsdManager** para mDNS (F5). |
| `credentials.ts`: `SENSITIVE_HEADERS`, `MASK`, `constantTimeEqual`, redação por cabeçalho | `src/services/credentials.ts:25-86` | Reusar `MASK` e a lista de portadores para nunca logar `Cookie`/senha do aparelho. |
| Tela **Rede** com a "receita" e botão de compartilhar; tela **Acesso** com origens observadas | `src/screens/NetworkScreen.tsx:52-89`; `AccessScreen` | Receita → `commit_all` (§6.5); origem → "promover a aparelho gerido" (§6.2). |
| i18n: chave nova entra em `pt.ts` primeiro, `tsc` acusa en/es; **nenhum texto ao usuário fora do dicionário**, evento vai ao banco como código + dado | `CLAUDE.md` "Convenções" | Todo estado/erro do gestor é código (`t.fl_*`), traduzido na hora de mostrar. |
| Restrições de runtime: **Hermes sem digest**, `fetch` do RN sobre OkHttp (cookies automáticos via `CookieManager` — evitar: mandar `Cookie:` à mão e `credentials:'omit'`), `AbortController` para timeout, `FormData` com `{uri,name,type}` de `expo-file-system`, `XMLHttpRequest.upload.onprogress` para progresso | `package.json`; `CLAUDE.md` "Restrições" | Ver §5.1. **[a confirmar]** o manifesto não declara `usesCleartextTraffic`; HTTP puro para IP de LAN funciona no Android por padrão só com `network_security_config` permitindo cleartext — conferir no primeiro build. |

Regras herdadas, sem exceção: código em inglês, comentários e commits em pt-BR (**atenção:** no repo do
firmware commits são em inglês — `simut/CLAUDE.md`); imports relativos; estilos por token de paleta;
teste só para função pura; **não criar `.md`/`.bak` sem necessidade** (este esboço mora no repo do
firmware; no app entra só a nota do `PLANO.md` §8 e a seção do `CLAUDE.md` quando a F1 fechar).

---

## 5. Arquitetura proposta

```
 simut-rx (Android)                                        SIMUT (N aparelhos na LAN)
 ┌───────────────────────────────────────────────┐          ┌──────────────────────────┐
 │ PAPEL 1 · RECEPTOR (intocado)                 │  push    │ TelemetryManager         │
 │  httpServer/mqtt/mllp/udp/ws → record() → DB  │◀─────────│  (HTTP/MQTT, cursor)     │
 ├───────────────────────────────────────────────┤          ├──────────────────────────┤
 │ PAPEL 2 · GESTOR DE FROTA (novo)              │  pull    │ WebManager (API JSON)    │
 │  screens/Fleet*  ──▶ hooks/useFleet*          │─────────▶│  /api/login_init, /login │
 │  services/fleet/                              │          │  /api/status /perms      │
 │   protocol.ts  (puro: login, payload, decode) │          │  /api/config /network    │
 │   sha256.ts    (puro, latin-1)                │          │  /api/commit_all         │
 │   client.ts    (fetch + fila por aparelho)    │          │  /api/backup /restore    │
 │   session.ts   (cookie, relogin, lockout)     │          │  /api/ota/apply          │
 │   jobs.ts      (máquinas de estado)           │          └──────────────────────────┘
 │   discovery.ts (manual · passiva · varredura) │
 │   store.ts     (runtime da frota)             │   correlação origem ↔ aparelho:
 │   db.ts        (devices, fleet_jobs, events)  │   IP da origem == IP do aparelho,
 │   credentials  (SecureStore por uid)          │   hwId nas chaves do payload == sensors[].hwid
 └───────────────────────────────────────────────┘
```

### 5.1 Camadas no app (`src/services/fleet/`)

| Módulo | Puro? | Responsabilidade | Teste |
|---|---|---|---|
| `sha256.ts` | sim | SHA-256 síncrono, entrada `Uint8Array`. `passwordDigest(senha)` codifica **latin-1** (um byte por code unit) e **lança** se houver code point > 0xFF — melhor recusar na entrada do que gastar lockout no aparelho. | Vetores FIPS 180-4 (`abc`, vazio, 448 bits, 1 M × `a`) + `"senha_ç"` conferido contra `tools/web_test_suite.py:sha256_frontend`. |
| `protocol.ts` | sim | Tudo que vira bytes ou lê bytes: `buildLoginForm(user, digest, nonce)`, `parseLoginInit`, `parseLoginResponse` (`ok`, `redirect`, `err`, `lockSec`), `decodeStatus` (converte **unidades na fronteira**: `uptime` ms→s, `val: number \| 'Error' \| '--'`, `rssi -100 → null`), `decodePerms` (`version` → `{major,minor,patch,tag}`), `buildCommitPayload(sections)` (**remove `null`/`undefined`, tipa por tabela do Apêndice C, `JSON.stringify` compacto, recusa payload > 6144 B**), `parseCommitResponse` (`rejected`, `creds`, `newPort`), `parseStageResponse`, `parseBackupHeader` (40 B, magic `BKP1`, CRC), `parseRestoreResponse`, `decodeLogRecord` (12 B LE, `WebUI.h:2580-2596`), `compareVersion`. | Um teste por função; o do `buildCommitPayload` confere a saída contra a lista de agulhas de `WebCommitSections.h` (nenhum `"net"` acidental) e contra os casos de `tools/commit_bool_cases.py`. |
| `client.ts` | não | Um `DeviceClient` por aparelho: **fila com concorrência 1**, `fetch` com `AbortController`, timeouts por classe (GET 15 s · POST commit 25 s · backup 120 s · stage 180 s · apply 10 s), `Cookie:` manual, `credentials:'omit'`, keep-alive. GET: até 3 retentativas em falha de rede/JSON truncado (1,5 · 3 · 4,5 s). **POST nunca retenta.** Em 403: sonda `/api/perms`; 401 lá ⇒ relogin uma vez e repete **só se for GET**; 200 lá ⇒ erro de permissão real. | Verificado no aparelho (`scripts/`), como os servidores. |
| `session.ts` | não | `login()` lê `locked/lockSec` de `login_init` **antes** de postar; nunca retenta `err:2`; guarda `needsPasswordChange` do `redirect`; `logout()` no fim de cada trabalho; cache da sessão com validade < 15 min. | idem |
| `liveness.ts` | não | Sonda de vida **sem custo**: `GET /apple-touch-icon.png` (204, pública, não toca slot de login nem orçamento pré-auth do Air). É o que o `waitReboot` usa, não `login_init` como os scripts de bancada. | — |
| `jobs.ts` | sim (máquinas) + não (executor) | `commitJob`, `otaJob`, `setTimeJob`, `probeJob` como **máquinas de estado puras** (`next(state, event) → state`), executadas por um runner que fala com `client.ts`. Estado persistido em `fleet_jobs` para retomar por aparelho (Air acorda depois). | Máquinas testadas com respostas sintéticas: 200/422/409/503/RST/timeout, uptime que diminuiu, versão que não mudou. |
| `discovery.ts` | parcial | Manual (host:porta, esquema), passiva (origens do receptor com IP que responde 204 em `/apple-touch-icon.png`), varredura `/24` opcional (§6.2). `parseCidr`/`hostsOf` puros. | puros com teste |
| `store.ts` | não | Snapshot da frota para as telas (`useSyncExternalStore`), com o mesmo par `revision`/`slowRevision`, **independente** do store da recepção. | — |
| `db.ts` | não | DDL de `devices`, `fleet_jobs`, `fleet_events` (§5.2). Sem migração numerada. | — |
| `credentials.ts` | não | `SecureStore` por `uid`: `{operatorUser, operatorDigest?, adminUser?}`; a **senha em texto** só é pedida na hora e pode não ser guardada (opção do usuário). Guardar o **digest latin-1** já basta para `/api/login` — e é o que um celular perdido expõe (§5.5). | — |

### 5.2 Modelo de dados

```sql
-- Aparelhos geridos. uid = id único do RP2040 (serial de /api/config). Chaveado por
-- ele porque IP muda com DHCP e nome muda por commit; o que não muda é o chip.
devices      (uid TEXT PRIMARY KEY, name TEXT, mac TEXT, host TEXT, port INTEGER,
              scheme TEXT,           -- 'http' | 'https'
              profile TEXT,          -- 'tft' | 'alpha' | 'air'  (escolhido pelo operador; cap só separa TFT)
              version TEXT,          -- último /api/perms.version lido
              last_seen INTEGER,     -- epoch ms da última resposta autenticada
              last_state TEXT,       -- 'online'|'sleeping'|'unreachable'|'auth_failed'|'locked'
              tls_fingerprint TEXT,  -- SHA-256 do cert pinado (F4), NULL em HTTP
              notes TEXT)
-- Trabalhos por aparelho, retomáveis. state é o da máquina de jobs.ts.
fleet_jobs   (id INTEGER PRIMARY KEY, uid TEXT, kind TEXT, state TEXT, payload TEXT,
              created INTEGER, updated INTEGER, result TEXT)
-- Trilha de auditoria do que o app FEZ em cada aparelho: código + dado, traduzido na tela.
fleet_events (id INTEGER PRIMARY KEY, ts INTEGER, uid TEXT, code TEXT, data TEXT)
```

- **Status ao vivo não vai para o banco** — fica no `fleet/store.ts` (mesma decisão da E5 para latência:
  saúde não é telemetria). Só `version`, `last_seen` e `last_state` são persistidos, e por `UPDATE`.
- **Backups `.bkp`** vão para `FileSystem.documentDirectory/fleet/<uid>/backup_<ts>.bkp`, nunca para o
  SQLite (~800 KB cada) e nunca para o export/perfil (contêm `system.bin` e a chave TLS). Apagar após
  restauração confirmada ou por pedido.
- **Nada disso entra em `messages`/`records`**. Correlação origem ↔ aparelho é feita **na hora de
  mostrar** (mapa `ip → uid` em memória), como o `useSourceNames()` da E3.

### 5.3 Identidade e correlação com o papel receptor

1. Ao adicionar: login → `GET /api/perms` (versão, perms) → `GET /api/config` (`serial` = **uid**,
   `name`, `sensors[].hwid`) → `GET /api/network` (`mac`, `web_port`, `web_tls`) se a conta tiver o bit.
   Se `/api/config` vier truncado (boot recente), retentar como o `Web.get` da suíte.
2. O aparelho aparece **duas vezes** no app quando também empurra telemetria: como origem (papel 1) e
   como gerido (papel 2). A ligação é por (a) **IP da origem == host do aparelho** e (b) **hwIds**: no
   modo JSON as chaves são `t<hwId>`/`u<hwId>` (`ESBOCO.md` §3), no CSV as colunas `s0_<hwid>`, e no
   template custom `{DEV}`/`{MAC}` — todos casáveis com `sensors[].hwid`/`name`/`mac` lidos do aparelho.
   (b) sobrevive à troca de IP; (a) é o atalho.
3. Dois aparelhos com o mesmo `name` são **avisados** antes de qualquer commit (colidem no mDNS).

### 5.4 Política de rede

| Parâmetro | Valor | Motivo |
|---|---|---|
| Requisições em voo por aparelho | **1** | Servidor de um cliente; TLS descarta a 2.ª conexão (§3.3). |
| Aparelhos em paralelo | ≤ 4 (config) | Sockets do celular e contenção Wi-Fi; OTA sempre **1**. |
| Cadência de status (release/alpha) | ≥ 30 s, só com a tela Frota em foco (`useIsFocused`) | Custo por chamada no aparelho (§3.3); compete com o navegador do humano. |
| Cadência de status (Air) | **nenhuma**; sob demanda | Rearma o timer de hibernação (§3.6). |
| Sonda de vida | `GET /apple-touch-icon.png` → 204 | Pública, sem corpo, sem slot de login, sem orçamento pré-auth. |
| Espera de reboot pós-commit | 3 s · sonda a cada 2 s · até 90 s · +2 s ("responder não é estar pronto") | `tools/commit_bool_cases.py:70-71,159-171` |
| Espera pós-apply | 20 s · sonda a cada 3 s · até 120 s · depois login + `/api/perms` | 48,4 s medidos sem web (§3.5) |
| Timeouts | GET 15 s · commit 25 s · backup 120 s · stage 180 s · restore 180 s · apply 10 s | Stage ~30 s/MB + tolerância de rede; backup ~800 KB |
| Login | 1 por trabalho; `logout` no fim; **nunca** dois logins com o mesmo usuário em paralelo no mesmo aparelho | 3 slots; mesmo usuário derruba o outro |
| Lockout | `err:2` é terminal até `lockSec`; UI mostra "bloqueado até HH:MM"; nenhuma retentativa automática de credencial | 2 s → 300 s por IP, compartilhado com o navegador do celular |

### 5.5 Segurança

- **Modelo de ameaça:** celular perdido/roubado com N credenciais de admin; captura de tela; folha de
  compartilhamento; logs; relatório de falha. Regras: credenciais só em `SecureStore` (Keystore
  Android), **fora** de `messages`, do perfil exportado, do relatório de campanha e do `fleet_events`;
  UI mostra `MASK`; opção "não guardar a senha do admin" (pedir a cada OTA) **ligada por padrão**.
- **Duas contas por aparelho (D4):** o app cria, no onboarding, uma conta `rx` via `users.add` com
  `perms = PERM_DASHBOARD|PERM_SYS_CONFIG|PERM_NET_CONFIG|PERM_LOGS` (ou o que o operador marcar, ≤
  0x03FF), recebe a senha temporária em `creds`, faz login como `rx` e completa o `force_chpass` com
  uma senha gerada — tudo em um fluxo. O **admin do slot 0** só é pedido na tela de OTA/backup. Custo
  aceito: a conta `rx` ocupa um dos 3 slots enquanto logada (por isso `logout` no fim).
- **TLS:** F1 = HTTP (Air/alpha só têm isso; release com PEM é caso do operador). F4 = módulo Kotlin com
  OkHttp e `TrustManager` **TOFU por host**: primeira conexão grava a impressão digital SHA-256 do
  certificado; mudança ⇒ bloqueia e avisa. Jamais `rejectUnauthorized:false` global, jamais "confiar em
  todos". O firmware não expõe a impressão digital (§3.7): confirmação fora de banda fica como R13.
- **Em massa, nunca:** `net.*`, `users.del/reset`, `reset_touch_cal`, `tel_reset` (reenvia o histórico
  inteiro), `sensor_wipe/accept`, `history_rebind`, `clear_logs`. Só na tela do aparelho, com
  confirmação que diz o que vai acontecer ("vai reiniciar", "vai apagar").
- **Trilha:** cada escrita vira `fleet_events` (`uid`, rota, campos alterados **sem valores sensíveis**,
  resultado, `rejected`). Do lado do firmware, `commit_all` loga `SEC_CONFIG_CHANGED` (303) e o login
  `SEC_LOGIN_SUCCESS` (300) — o app pode conferir pelo `/api/export/logs.bin`.
- **Auto-limitação:** a política do §5.4 é o que impede o app de virar o "atacante" que o V-03 e o
  lockout foram feitos para conter.

---

## 6. Fluxos

### 6.1 Adicionar um aparelho

```
host[:porta] digitado (ou origem promovida)
  → sonda 204 em http://host/apple-touch-icon.png   (2 s; se falhar, tenta :443 https só na F4)
  → GET /api/login_init  → se locked: mostrar lockSec e parar
  → POST /api/login (rx ou admin, digest latin-1, nonce)
      redirect == "/force_chpass" ⇒ fluxo de troca de senha antes de qualquer outra coisa
  → GET /api/perms  (version, perms)  → gate: version ≥ 2.x para os recursos que o app usa
  → GET /api/config (uid, name, sensors)  → GET /api/network (mac, web_port, web_tls) se PERM_NET_CONFIG
  → INSERT devices; SecureStore(uid) ← credencial
  → GET /logout
```

### 6.2 Descoberta

| Modo | Custo no aparelho | Quando |
|---|---|---|
| **Passiva** — origens do receptor cujo IP responde 204 na sonda | zero | Sempre ligada; é o caso comum: o SIMUT já está empurrando para o celular. |
| **Manual** — IP, `nome.local` (**[a confirmar]** Android normalmente não resolve `.local` sem NsdManager; aceitar mas avisar) | zero | Sempre. |
| **Varredura /24** — 254 × sonda 204 em :80 (e :443 na F4), 8 em paralelo, ~10 s | zero em slots de login; **um pacote por host** | Ação explícita do operador, com a explicação na tela. Não usa `login_init` (gastaria o orçamento pré-auth de todo Air acordado na rede). |
| **mDNS `_simut._tcp`** (R2 + módulo NsdManager) | zero | F5. Único modo que descobre um aparelho **novo** sem digitar nada. Não alcança `alpha` (sem mDNS). |

O 204 em `/apple-touch-icon.png` **não é assinatura forte** (qualquer coisa pode responder 204); é
filtro de candidatos. A identificação de verdade é o login + `serial`.

### 6.3 Leitura de status

Cartão por aparelho: nome, versão (do `perms`), uptime (**s**, convertido), RSSI, heap livre / menor
bloco, FS usado, NTP, telemetria (`tel`, `pending`), sensores (`val` numérico ou `Error`/`--`), `cap`,
"visto pela última vez". Estados: `online` · `dormindo` (perfil Air + sem resposta) · `sem resposta` ·
`credencial recusada` · `bloqueado até` · `senha a trocar`. Detalhe do aparelho: `/api/network`,
`/api/alarms`, `/api/sensors`, últimos eventos via `/api/export/logs.bin?from=<último>&to=<agora>`
(incremental; tabela código→texto gerada de `tools/logcodes.tsv` no build do app — não depender de
`/api/lang`). `/api/logs` inteiro e `history_multi` só por ação explícita, um aparelho por vez (trancam a
tela do TFT com "web busy").

### 6.4 Configuração

```
GET /api/config (+ /api/alarms, /api/sensors quando o template tocar neles)
  → diff local: template ∖ valor atual  (só campos DIFERENTES entram no payload — é o que torna o
    commit parcial seguro e o "rejected" legível)
  → pré-validação client-side pela tabela do Apêndice C (faixas, tamanhos, IPv4, porta)
  → tela de revisão: "N campos mudam · o aparelho VAI REINICIAR (~10 s) · seções: sys, alarms"
  → POST /api/commit_all  _payload=<JSON compacto>   (25 s)
      200 ⇒ ler rejected/creds/newPort; RST ⇒ "reboot provavelmente começou" (ambíguo, como no WebUI)
  → waitReboot (§5.4)  → login → GET /api/config → diff de novo
      vazio ⇒ OK; não-vazio ⇒ mostrar exatamente quais campos não ficaram como pedido
  → logout · fleet_events
```

- **Templates** (aplicáveis a N aparelhos): `sys.t_*`, `sys.m_*`, `sys.a_*`, `sys.h_int`, `sys.s_int`,
  `sys.tz`, `sys.log`, `sys.slog_*`, `alarms.sounds`, limites de alarme por hwId. **Por aparelho, nunca
  template:** `sys.name`, tudo em `net`, `slots`, `calib`, `users`.
- Em N aparelhos: **sequencial ou ondas de ≤ 4**, cada um com seu wait/verify; um aparelho que falhar
  **não** interrompe os outros, e o resumo final lista por aparelho.
- Air: o trabalho fica `pending` até o aparelho responder; quando responder, roda inteiro na mesma
  janela.

### 6.5 "Apontar este SIMUT para mim"

A receita da tela Rede (`t_srv` = IP do celular, `t_port`, `t_path`, `t_mode`, `t_key`, `t_sec`, ou os
`m_*` do MQTT) vira um commit de template com pré-visualização. Como `t_key` volta mascarado, o app
**não consegue verificar** a chave depois — só o cabeçalho chegando na porta do receptor prova. O fluxo
termina esperando **a primeira mensagem** desse aparelho no Fluxo (correlação §5.3), que é a prova
ponta a ponta.

### 6.6 OTA de um aparelho

Pré-voo (tudo antes de tocar no aparelho):

1. `version ≥ 1.6.2`; se não, mostrar a instrução de gravação por USB e parar.
2. Perfil do aparelho (`tft`/`alpha`/`air`) escolhido pelo operador **e** coerente com `cap`
   (`cap:1` ⇒ imagem TFT; `cap:0` ⇒ alpha ou air — perguntar). A imagem vem com env no manifesto (R14)
   ou é escolhida à mão com aviso.
3. Tamanho da imagem 100 KiB ≤ n ≤ **1016 KiB**; CRC do boot2 calculado localmente (mesma rotina de
   `src/ota/validation.cpp:129-136`) para recusar `.uf2`/`.gz` **antes** do upload.
4. Versão alvo > versão atual, senão pedir confirmação explícita de downgrade.
5. Air: aparelho respondendo **e** aviso "coloque no carregador / acabou de reiniciar"; TFT: aviso "não
   toque na tela".
6. Credencial **admin** (slot 0) pedida agora.

Execução (uma máquina de estados, cada transição registrada em `fleet_jobs`):

```
BACKUP     GET /api/backup (120 s) → conferir Content-Length == 40 + PSize e CRC do payload
           → gravar em fleet/<uid>/ → falhou? parar aqui (nada foi tocado no aparelho)
STAGE      POST /api/restore?op=stage&commit=1  (multipart 'file', 180 s, progresso por XHR)
           → exigir 200 && st==5 && v==0 && committed==1; guardar dsize/dcrc
           → qualquer outra coisa: "FS do aparelho foi reformatado; restaure o backup" (não retentar)
CHECK      GET /api/status → sys.uptime NÃO diminuiu desde o STAGE (senão: NÃO aplicar; reiniciar e re-stagear)
APPLY      POST /api/ota/apply (10 s) → 202 ok; 503 ⇒ esperar Retry-After (5 s) e repetir ≤ 6×; 409 ⇒ falha
WAIT       20 s + sonda 204 a cada 3 s até 120 s
VERIFY     login admin → GET /api/perms.version == versão da imagem  ⇒ SUCESSO
           ≠ ou timeout ⇒ "desconhecido — inspecione fisicamente" + link para docs/RECOVERY.md
RESTORE    decisão: config sobreviveu pelo snapshot; histórico/idiomas/temas/calib/TLS não.
           F3: oferecer restore integral (validate → apply → reboot) SÓ se o aparelho subiu de fábrica
               (nome/usuários padrão) — senão avisar que o restore integral sobrescreve o que foi
               gravado desde o backup.
           F6: restore seletivo (porta de tools/fsguard.py: só arquivos ausentes + merge de dia
               via porte de tools/h5_day_merge.py; nunca README/system.bin/system.blog/*.pem).
LOGOUT     fleet_events(uid, 'ota', {from, to, result})
```

Frota: **um aparelho por vez**, sempre. Um upload de 1 MB satura o Wi-Fi do celular e o roteador; dois
em paralelo dobram a chance do RST aos 13 s.

### 6.7 Hora e diagnósticos

- `POST /api/set_time {"epoch":<agora do celular>}` para aparelhos com `ntp:0` — não reinicia, é seguro
  em massa, e é a única ação de frota "de graça".
- `tel_sync` é seguro em massa (força o envio). `tel_reset` **não** (reenvia tudo).
- Tabela de lockout (`/api/sec_status`, `PERM_USER_MGR`) mostra ao operador se **o próprio celular**
  está bloqueado no aparelho.

---

## 7. Mudanças propostas no firmware (nenhuma é pré-requisito da F1)

Cada item lista o custo esperado e os **portões** que toca (`simut/CLAUDE.md` "What will fail your
build"). Ordem = valor ÷ custo. O Air tem ~4,9 KB reais de folga: toda mudança **mede** nos cinco envs
e edita `tools/flash_budget.json` na mesma PR quando crescer.

| # | Mudança | Por quê | Custo esperado | Portões | Prioridade |
|---|---|---|---|---|---|
| R1 | `"ver":SIMUT_VERSION`, `"uid":<serial>`, `"mac"` no bloco `sys` de `/api/status` (`WebManager_Api.cpp:609-615`; buffer de 1024 B comporta) | Um poll de `PERM_DASHBOARD` identifica e versiona o aparelho; verificação de OTA sem segundo request; `OTA_USAGE.md` passa a estar certo | ~80–120 B | flash budget; `docs/MANUAL.md` | **recomendada** |
| R2 | `MDNS.addService("simut","tcp",porta)` + TXT `uid`, `ver`, `tls`, `env` (`NetworkManager.cpp:280-283`) **[a confirmar a API `addServiceTxt` no LEAmDNS do arduino-pico instalado]** | Descoberta por browse sem tocar no servidor web nem no timer do Air; é passiva | algumas centenas de B (lib já linkada); **medir no Air** | flash budget; nenhuma rota nova | **recomendada** (não alcança o alpha) |
| R3 | 401 quando `getAuthPerms()==0`, 403 só com sessão sem o bit — helper `requirePerm(bit)` | Cliente distingue "relogar" de "sem permissão" sem sonda extra | ~0 (troca de código) | `check_authz` continua verde; `AUTHORIZATION.md` nota | recomendada |
| R4 | `"mc":0\|1` em `/api/perms` | Sessão reaproveitada descobre a troca de senha pendente antes do 409 | ~20 B | — | recomendada |
| R5 | `"env":"release\|alpha\|air"` em `/api/perms` + string etiquetada em `.rodata` (`"SIMUT-ENV:pico_w_air;v=2.4.2-beta"`) que o app procura no `.bin` antes de subir; opcionalmente `ota_validate_staging` compara com o env em execução | Fecha o único modo de bricar por imagem errada que o firmware hoje aceita e "verifica" | ~60 B + ~200 B se validar no staging | `docs/MANUAL.md` §12 | **recomendada** |
| R6 | (a) no boot, tratar `STATE_COMMITTED` como `APPLYING`/`POST_BOOT` (limpar metadata + log); (b) `GET /api/ota/state` `{meta,attempts,size,crc,stage,lfs}`; (c) `POST /api/ota/abort` | (a) desarma a mina "COMMITTED sem staging" **hoje**, sem app; (b)/(c) dão ao gestor leitura idempotente e cancelamento | (a) ~100–200 B; (b)+(c) ~700 B | `AUTHORIZATION.md` (== FULL_ADMIN), `logcodes.tsv` para o código novo | (a) **recomendada**; (b)/(c) opcionais |
| R7 | `commit_all` com `_dry=1`: roda portão + parsers em cópia de `cfg`, devolve `{"status":"dry","rejected":[…]}` sem gravar nem reiniciar (`WebManager_Commit.cpp:1316-1373`) | Um template é validado em N aparelhos **antes** de N reboots | ~300 B + cópia de `SystemConfig` na pilha (conferir tamanho) | — | opcional |
| R8 | `setStr` recusa valor não-string (`rejectField`) e `net.ip/mask/gw/dns/web_port` entram em `rejected` | Elimina o apagamento silencioso (§3.4) e o "reiniciou com o IP velho" sem aviso | ~150 B | — | recomendada |
| R9 | `op=reboot` em `/api/action` (`PERM_SYS_CONFIG`, mesmos 409/503) | Reiniciar um aparelho travado sem inventar um commit | ~120 B | `AUTHORIZATION.md` (op novo na rota existente) | opcional |
| R10 | `{"type":"perms","id":N,"perms":M}` em `users` (mesmo teto 0x03FF, slot 0 excluído) + corrigir "editing" no `AUTHORIZATION.md` | Mudar papel de uma conta em N aparelhos sem `del`+`add` (que cunha senha nova em cada um) | ~200 B | `AUTHORIZATION.md` | opcional |
| R11 | Air: `"air":{"idle":s,"awake":ms,"armed":0\|1}` em `sys` e `?quiet=1` em `/api/status` que **não** dispara `_activityCb` | O gestor lê o Air sem segurá-lo acordado e mostra "dorme em N s" | ~150 B **no Air**, que não tem folga — medir primeiro | flash budget do Air | opcional |
| R12 | Aceitar `Authorization: Bearer <SIMUTSESS>` ao lado do cookie em `getAuthPerms` | Clientes sem cookie jar; sem mudar o modelo de sessão | ~60 B | `AUTHORIZATION.md` nota | opcional |
| R13 | Impressão digital SHA-256 do cert em `show system info` / `GET /api/network` (`PERM_NET_CONFIG`) | Confirmação fora de banda do pin TOFU | ~200 B (release só) | — | opcional (F4) |
| R14 | **Repo, não firmware:** job de release que publica `simut_v<ver>_<env>.bin` por env + `manifest.json` `{version, env, size, sha256, min_from:"1.6.2"}` | O app baixa a imagem certa de GitHub Releases (`expo-file-system.downloadAsync`, sem seletor de documentos, que o app não tem) e confere o SHA-256 antes do stage | CI | `build.yml`; `docs/OTA_USAGE.md` | **recomendada** para a F3 |
| R15 | Correções de doc/comentário do Apêndice D | O projeto trata comentário desatualizado como defeito | 0 | — | recomendada, PR pequena |

Toda rota nova = linha no `docs/AUTHORIZATION.md` com o bit ou entrada na allowlist com motivo; todo
`LogCode` novo = `tools/logcodes.tsv` + `gen_logcodes.py`; toda `TRL("…")` nova = `@TRL` nos dois
`.lng`; e CI constrói os cinco envs.

---

## 8. Fases

| Fase | Entrega | Aceite | No ferro |
|---|---|---|---|
| **F0** | `sha256.ts` + `protocol.ts` puros com testes; DDL das três tabelas; `fleet/store.ts`; tela **Frota** em Mais com lista vazia; chaves i18n pt/en/es; nota em `PLANO.md` §8 | `npm run check` e `npm test` verdes; digest de `"senha_ç"` igual ao de `web_test_suite.py`; payload de exemplo idêntico byte a byte ao que o `WebUI.h` mandaria | — |
| **F1** | Adicionar aparelho (manual + origem promovida), login/logout, cartão de status, detalhe, perfil Air com estado "dormindo", lockout visível, troca de senha forçada | 3 aparelhos (TFT, alpha, Air) lado a lado; RSSI/uptime batem com o display; Air dorme com o app aberto na lista (medido pela sonda da PicoHand, `ss -tn` limpo) | bancada: release + Air; `air idle 40` |
| **F2** | Configuração: leitura, diff, template, revisão, commit, wait/verify, `rejected` legível, "apontar para mim", `set_time` em massa | Mesmo template em 3 aparelhos: 3 reboots, 3 verificações verdes; campo fora de faixa aparece em `rejected` e na tela; `null` nunca sai do montador (teste) | `tools/commit_bool_cases.py` como controle |
| **F3** | OTA de um aparelho: manifesto de release (R14) → download + SHA-256 → pré-voo → backup → stage (progresso) → apply → verify → restore integral condicional; `docs/RECOVERY.md` linkado na falha | 5 ciclos seguidos num TFT de bancada com versão provada por `/api/perms`; um ciclo com imagem `.uf2` recusada **antes** do upload; um ciclo com o socket derrubado no stage terminando em "restaure o backup", sem retentativa | bancada com `fsguard.py` como controle; **nunca** o Air sem carregador |
| **F4** | HTTPS com pin TOFU (módulo Kotlin), esquema/porta por aparelho, sonda :443 | Aparelho release com PEM: primeira conexão grava a impressão; trocar o cert bloqueia com aviso; OTA sobre HTTPS medido (lento, mas termina) | release com `web_cert.pem` |
| **F5** | Descoberta: varredura /24 explícita; mDNS `_simut._tcp` (R2 no firmware + módulo NsdManager) | Aparelho novo na rede aparece sem digitar IP; alpha aparece só pela varredura (documentado na tela) | release + Air em M0 |
| **F6** | Restore seletivo (porte de `fsguard.py`/`h5_day_merge.py`), logs incrementais com tabela de códigos, trilha de auditoria exportável | Histórico do dia sobrevive a um OTA (blocos selados conferidos com `h5_block_anchors`) | bancada |

Corte natural para o primeiro uso real: **F0→F2**. Já resolve "configurar vários e ver o status", que é
o que a tela Rede pede hoje. A F3 é a que mais exige bancada e é a primeira que precisa de algo fora do
app (R14).

---

## 9. Testes

- **simut-rx, puros (Jest):** `sha256` (FIPS + latin-1 + recusa > 0xFF); `buildCommitPayload` (remove
  `null`, tipa, compacto, agulhas, teto 6144); `decodeStatus` (ms→s, `val` string, `rssi -100`, `hum`
  ausente); `compareVersion` (`2.4.2-beta` < `2.4.10`, `-beta` ignorado); `parseLoginResponse` (todos
  os `err` e `lockSec`); `parseBackupHeader` (40 B, CRC do cabeçalho); `decodeLogRecord` (12 B LE,
  uptime 24 bits); máquinas de `jobs.ts` (commit: 200/RST/timeout/rejected; OTA: 422, uptime caiu,
  503×6, versão igual). **Bloco de polyfill de `Buffer`** copiado de `snmp.test.ts` — o `subarray()`
  do RN devolve `Uint8Array` e o Jest mente (`PLANO.md` E11).
- **simut, nativos (`pio test -e native*`):** cada R do §7 com portão ganha teste no
  `test_validators` (R8: `setStr` com `null`), no `native_network` (R2 só se houver stub) e no
  `native_air` (R11).
- **Ferro (campanha datada, pasta em `docs/`):** custo do poll no Air (janela acordada com e sem app,
  pela sonda); lockout (sequência de `tools/repro_lockout.py` vista pelo app); OTA ×5 no TFT e ×3 no
  alpha; commit em 3 aparelhos; HTTPS na F4. Cada campanha registra o binário (`commit`) e a versão do
  app — como as campanhas de `docs/` fazem.

---

## 10. Riscos

| Risco | Mitigação |
|---|---|
| O gestor "contamina" a recepção (import cruzado, `revision` compartilhada, escrita por mensagem) | Diretório próprio, store próprio, revisão de PR com a regra do §2 explícita; nenhum arquivo de `fleet/` importa `serverManager` |
| Lockout auto-infligido em N aparelhos (senha trocada num deles, laço de retentativa) | `err:2` terminal + `lockSec` visível; um login por trabalho; `sec_status` na tela |
| Air drenado pelo próprio app | Perfil bateria sem poll; `logout` ao fim; medido na F1 pela sonda |
| Stage interrompido reformata o FS | Backup verificado antes; sem retentativa; estado "restaure o backup" |
| Imagem errada / downgrade / aparelho < 1.6.2 | Pré-voo §6.6; R5/R14 |
| Template move o aparelho (`net`) | D6: nunca template; por aparelho; commit por último e sozinho; sonda no endereço novo |
| `null` apaga campos sob 200 | Montador tipado + teste; R8 |
| Cert autoassinado / "aceitar tudo" | D5: HTTP na F1, TOFU na F4 |
| Celular perdido com credenciais | SecureStore; senha admin não guardada por padrão; nada no export |
| Sessões: app + navegador + script disputam 3 slots | Conta `rx` própria; logout; UI explica `err:3` |
| Acoplamento de versões app ↔ firmware | O app declara a versão mínima por recurso (`FEATURE_MIN` no `protocol.ts`), lê `/api/perms.version` e **desabilita** o que o aparelho não tem, dizendo qual firmware traz |
| Flash do Air não comporta R2/R11 | Medir antes de prometer; o app funciona sem elas |

---

## 11. Decisões em aberto

| # | Pergunta | Opções | Recomendação |
|---|---|---|---|
| Q1 | Onde o gestor mora na navegação? | (a) grupo em **Mais**; (b) 6.ª aba; (c) substitui "Servidores" | (a) na F1; (b) se virar uso diário |
| Q2 | Guardar a senha do admin? | (a) nunca (pedir a cada OTA); (b) por aparelho, opt-in; (c) sempre | (b), padrão desligado |
| Q3 | Conta `rx` automática no onboarding? | (a) sim, com perms escolhidas; (b) usar a conta que o operador digitar | (a) — evita derrubar o navegador do maintainer |
| Q4 | Imagem de OTA de onde? | (a) manifesto em GitHub Releases (R14); (b) "abrir com" (intent) ; (c) seletor de documentos (dep nova) | (a); (b) depois |
| Q5 | HTTPS na F1? | (a) não (D5); (b) sim com `network_security_config` por CA do usuário | (a) |
| Q6 | mDNS: firmware primeiro ou app primeiro? | (a) R2 e módulo NsdManager juntos na F5; (b) só varredura | (a), medido no Air |
| Q7 | Restore após OTA na F3 | (a) integral condicional; (b) seletivo desde já; (c) nenhum (só aviso) | (a); (b) na F6 |
| Q8 | R6(a) (limpar `COMMITTED` no boot) entra já, sem app? | sim/não | **sim** — é correção de segurança independente deste esboço |

---

## 12. Obrigações de documentação e versionamento

**simut:** linha deste documento em `docs/README.md` (Snapshot, v2.4.2-beta); cada R do §7 traz
`AUTHORIZATION.md`/`logcodes.tsv`/`.lng`/`flash_budget.json` conforme o portão; `OTA_USAGE.md` e
`MANUAL.md` §12 corrigidos (Apêndice D); `CHANGELOG.md` cita "simut-rx" onde a mudança existe por ele.
Commits em **inglês**.

**simut-rx:** `PLANO.md` §8 ganha o parágrafo da reversão (§2); `CLAUDE.md` ganha a seção "Gestor de
frota" quando a F1 fechar (o que custou tempo, medidas); `README.md` só quando houver release; a
política de privacidade em `play/` passa a mencionar credenciais de aparelhos guardadas no Keystore.
Comentários e commits em **pt-BR**.

**Acoplamento:** tabela `FEATURE_MIN` no app — `status/config/commit: ≥ 2.4.2-beta` (referência deste
esboço; aparelhos mais velhos "melhor esforço"), `ota: ≥ 1.6.2-beta`, `R1..R12: ≥ versão que os
trouxer`. O app mostra por aparelho o que está desabilitado e por quê.

---

## Apêndice A — Rotas usadas pelo gestor (contrato condensado)

| Rota | Perm | Requisição | Resposta / efeitos |
|---|---|---|---|
| `GET /apple-touch-icon.png` | pública | — | 204 sem corpo. **Sonda de vida.** Sem slot, sem orçamento pré-auth. |
| `GET /api/login_init` | pública | — | 200 `{"nonce","locked","lockSec"}`; 429 + `Retry-After` com 8 slots bloqueados. **Gasta** 1 extensão pré-auth no Air. Nonce 60 s. |
| `POST /api/login` | pública | form `user`,`pass`(sha256 latin-1),`nonce` | 200 `{"ok":true,"redirect":"/"\|"/force_chpass"}` + `Set-Cookie: SIMUTSESS=`; 400/401 `err:1`; 401/403 `err:2,lockSec`; 403 `err:3` (3 sessões ocupadas). Consome o nonce. |
| `POST /api/force_chpass` | autenticado + pendente | form `p1`,`p2` (HTTP: sha256 hex; HTTPS: texto + política) | 200 `{"status":"ok"}`; 400; 403 |
| `GET /api/perms` | autenticado | cookie | 200 `{"user","perms","ntp","time","version","langCode","langName"}`; **401** sem sessão. **Oráculo da versão.** |
| `GET /api/status` | `DASHBOARD` | cookie | chunked `{sys,metr,sensors}` (§3.3). Rearma o Air. |
| `GET /api/config` | `SYS_CONFIG` | cookie | chunked; todos os `t_*/m_*/a_*/slog_*`, `h_int`, `serial`, `sensors[16]`; `t_key` mascarado. Pode vir truncado pós-boot. |
| `GET /api/network` | `NET_CONFIG` | cookie | `{connected,ip,mask,gw,dns,mac,ssid,use_dhcp,static_*,dns_auto,dns2,ntp_server,ntp_enabled,web_port,web_ka,web_tls}` (1024 B) |
| `GET /api/alarms` · `/api/sensors` · `/api/users` · `/api/sec_status` · `/api/themes` | ver `AUTHORIZATION.md` | cookie | Apêndice A do mapa de leitura; `/api/sensors` = slots + drivers, não leituras |
| `GET /api/export/logs.bin?from&to[&level]` | `LOGS` | cookie | SIMX(24 B) + N×12 B + CRC32; ≤ 31 dias; 503 concorrente; trava o TFT |
| `GET /metrics` | `DASHBOARD` | cookie **ou** `Authorization: Basic` (senha crua) | Prometheus; falha Basic alimenta o lockout; ~0,5 s de CPU por chamada Basic |
| `POST /api/commit_all` | entrada `SYS\|NET\|USER_MGR` + por seção | form `_payload`=JSON ≤ 6144 B | 200 `{"status":"ok"[,newPort][,rejected][,creds]}` → **reboot**; 400 (`No section`, slots, limites); 403 `{"section"}`; 409 senha pendente; 503 toque |
| `POST /api/action` | `SYS_CONFIG` | `op=` + `slot=` (query ou form) | `tel_sync` `{"ok"}`; `tel_reset`; `sensor_scan` 202; `scan_results`; `sensor_wipe`; `sensor_accept`. Sem reboot. |
| `POST /api/set_time` | `SYS_CONFIG` | JSON cru `{"epoch":N}` (> 1600000000) | 200 `{"ok":true,"now"}`; sem reboot |
| `POST /api/calib` | `CALIB` | JSON cru ≤ 8192 B | 200 `{"ok","version"}`; 429 (5 s); 503 sem NTP |
| `GET /api/backup` | `== FULL_ADMIN` | cookie | `.bkp` (40 B + TLV), headers `X-Backup-*`; 503 ocupado/toque |
| `POST /api/restore?op=validate` | `FILE_READ` | multipart `bkp` | `{"st","chip","fwv","psz","fc","fsm":0}`; 422 por `st` |
| `POST /api/restore?op=apply` | `== FULL_ADMIN` | multipart `bkp` | idem `fsm:1` → **reboot**; grava direto no destino |
| `POST /api/restore?op=stage&commit=1` | `== FULL_ADMIN` | multipart `file` (`.bin` cru) | 200 `{"st":5,"bytes","crc32","v":0,"dsize","dcrc","committed":1}`; 422; **reformata o FS sempre** |
| `POST /api/ota/apply` | `== FULL_ADMIN` | — (**nunca** `?test=1`) | 202 → ~48 s sem web; 409 nada commitado; 503 toque + `Retry-After: 5` |
| `GET /logout` | — | cookie | 302 + cookie expirado; libera o slot |

## Apêndice B — Decodificador de `/api/status` (unidades na fronteira)

| Chave | Tipo no fio | No app | Nota |
|---|---|---|---|
| `sys.uptime` | ms | `uptimeSec = floor(ms/1000)` | armadilha n.º 2 da bancada; reboot detectado por **queda** do valor |
| `sys.rssi` | dBm, `-100` = sem Wi-Fi | `number \| null` | |
| `sys.time` | epoch s | `Date` | provisório quando `ntp:0` |
| `sys.pending` | registros, `-1` | `number \| null` | telemetria pendente |
| `sys.tel` | 0/1 | boolean | `telInterval > 0` |
| `sys.hi` | minutos | s | intervalo do histórico (= wake do Air) |
| `sys.cap` | 0/1 | `'tft' \| 'headless'` | alpha e Air iguais |
| `sys.heap_*`, `fs_*` | bytes | bytes | `fs_u` cache de 10 s |
| `metr.tl`, `fom`, `fot`, `c1a` | ms | ms | |
| `metr.rmn/rmx` | dBm, `0` = nunca | `number \| null` | |
| `sensors[].val` | número ou `"Error"`/`"--"` | `number \| 'error' \| 'nan'` | |
| `sensors[].hum/press` | opcionais | `number?` | só quando o canal existe e é finito |

## Apêndice C — Contrato dos campos do `commit_all` que o app pode emitir

Tudo em `sys` salvo indicação. Faixas de `src/WebManager_Commit.cpp:700-1250` e `SystemDefs_Validate.h`.

| Campo | Tipo/faixa | Template? | Observação |
|---|---|---|---|
| `name` | string 1..31 `isValidName` | **não** | vira hostname mDNS |
| `tz` | int −12..14 | sim | aplicado ao vivo antes do reboot |
| `log`, `t_sec`, `m_retain`, `m_had`, `a_en`, `ntp_enabled`, `slog_en` | `1`/`0` | sim | como string `'1'`/`'0'` (igual ao WebUI) |
| `t_key` | string ≤ 63; contém `***` ⇒ mantém | sim | não se verifica depois (mascarado) |
| `res` | 9..12 | sim | só builds com DS18B20 |
| `s_int` | 1000..60000 ms | sim | |
| `t_srv` ≤63, `t_port` 1..65535, `t_path` ≤31, `t_mode` 0..2, `t_transport` 0..1 | | sim | **a receita** |
| `t_int` | 0..20000 **registros** (lote mínimo; 0 = telemetria off) | sim | **não é ms** desde a v22 |
| `t_bat` | 1..250 | sim | lote máximo |
| `m_topic` ≤63, `m_cid` ≤23, `m_user` ≤31, `m_pass` ≤31 (vazio = mantém), `m_qos` = 0, `m_ka` 10..300 | | sim | MQTT usa `t_srv`/`t_port` |
| `t_glob` ≤255, `t_line` ≤511, `t_sep` ≤7 | | sim | texto livre — cuidado com as agulhas `"net"`/`"users"` |
| `a_mode` 0..2, `a_qmax` 1..64, `a_path` ≤31, `a_glob`, `a_line`, `a_sep` | | sim | linha de alarmes |
| `h_int` | 1..1440 min | sim | cadência de wake do Air |
| `slog_srv` IPv4 ou `""`, `slog_port`, `slog_lvl` 0..4 | | sim | |
| `net.*` | ver §3.4 | **nunca** | por aparelho, por último, sozinho |
| `slots`, `calib` | ver `Commit.cpp:357-359`, `Calib.cpp:467-481` | **não** | 400 sem tocar em nada se inválido |
| `alarms.sensors[].{idx,active,tmin,tmax,hmin,hmax,<ch>:[lo,hi]}` | pré-validado contra o canal | por hwId | `idx` inativo é ignorado em silêncio |
| `alarms.sounds` | bools + 0..100 + melodias 0..5 | sim | merge, não substituição |
| `users.actions[]` | `add{name,perms≤0x03FF}` / `del{id 1..4}` / `reset{id}` | **não** | `creds` volta uma vez; sem `edit` |

## Apêndice D — Divergências doc/código encontradas (para PR de correção, R15)

| Onde | Diz | Código |
|---|---|---|
| `docs/OTA_USAGE.md:42` | ler a versão de `/api/status` | não existe; está em `/api/perms` (`WebManager_Api.cpp:37-43`) |
| `docs/MANUAL.md:857` | `/api/sensors` = leituras ao vivo | é o mapa de slots + drivers (`WebManager_Calib.cpp:95-181`) |
| `docs/MANUAL.md:819-822` | snapshot de 4 KB no setor de metadata | 8 KiB em `0x1FD000`, nos 2 últimos setores do staging (`ota_layout.h:459-461`) |
| `docs/AUTHORIZATION.md:32` | `commit_all` refuses perms "when creating or **editing** users" | não há ação de editar (`Commit.cpp:1087`, `:1126`) |
| `src/WebManager.h:6-7` | "HMAC nonces" | nonce comparado verbatim (`Auth.cpp:353`) |
| `src/WebManager.h:167-169` | nonce de 64 hex | 32 hex (`WebManager_Util.cpp:41-47`) |
| `src/WebManager_Auth.cpp:206` | `/api/login` passa por `ensureLoginStateSlot` | usa `findLoginStateForIp` (`:490`) |
| `src/simut_config.h:217` | hostname `SIMUT.local` | é `<deviceName>.local` |
| `src/MetricsManager.h:84-85` | "future web exposure" | já exposto em `/api/status.metr` e `/metrics` |
| `src/WebManager_History.cpp:59-68` | `id=-1` ambiente, chave `h` | rejeita `id<0`, arrays por canal |
| `tools/commit_bool_cases.py:95-97` | "ONE session slot" | 3 slots (`WebManager.h:155`) |
| `AGENTS.md` (nota do Air) | CI não constrói `pico_w_air` | `build.yml:118-122` constrói |
| `simut-rx/scripts/simut-api.py:24-27` | espaço após `:` apaga config | corrigido em 2.4.2-beta (`Commit.cpp:37-45`); `null` ainda apaga |
