# API POST — referência compacta

**Estado:** Living · **Base:** `bc31410` · **Conferido em 2026-09-19** contra a
fonte E contra o aparelho (rig 192.168.3.24): cada código e corpo de resposta
citado aqui foi obtido de uma chamada real, não de leitura de código — incluindo
a matriz de permissões, exercida com quatro contas de máscaras diferentes.

Todas as 18 rotas `POST` do firmware, o que cada uma recebe e o que devolve.
As permissões são as de [`AUTHORIZATION.md`](AUTHORIZATION.md), que é a fonte
normativa — se as duas discordarem, aquela está certa e esta precisa de conserto.

---

## O básico

**Sessão por cookie.** Tudo exceto `/api/login`, `/api/login_chpass` e
`/api/force_chpass` exige `SIMUTSESS`. Sem sessão: **401**. Com sessão mas sem o
bit: **403**.

```bash
# 1. nonce                          2. login (senha em SHA-256 hex do texto puro)
curl -c j -s http://IP/api/login_init          # {"nonce":"…","locked":false,"lockSec":0}
curl -b j -c j -s http://IP/api/login \
     -d "user=admin&pass=$(printf %s 'senha' | sha256sum | cut -d' ' -f1)&nonce=NONCE"
# 3. daqui em diante: curl -b j …
```

**Três formatos de corpo**, conforme a rota:
| forma | rotas |
|---|---|
| `application/x-www-form-urlencoded` (campos soltos) | login, chpass, commit_all, action, delete, mkdir, history_rebind, save_sys |
| **JSON cru** no corpo (lido como `plain`) | calib, set_time, tls |
| `multipart/form-data` | upload, restore |

**Lockout:** o login tem backoff exponencial por IP. `GET /api/login_init`
devolve `locked`/`lockSec` — consulte antes de insistir.

⚠️ **Senha pendente de troca** (`admin reset`) faz `commit_all` e `action?op=reboot`
responderem **409** com `{"next":"/api/force_chpass"}`.

⚠️ **Calibração de toque em curso** faz as rotas que mexem em config responderem **503**.

---

## Quem pode o quê

Dez bits, e o aparelho testa **o bit**, não um "nível" — não há hierarquia
implícita: um usuário com `PERM_FILE_DELETE` e nada mais apaga arquivos sem
conseguir abrir o painel.

| bit | valor | abre |
|---|---|---|
| `PERM_DASHBOARD` | `0x0001` | painel e status |
| `PERM_HISTORY` | `0x0002` | histórico e exportação |
| `PERM_LOGS` | `0x0004` | leitura do log · metade de `clear_logs` |
| `PERM_SYS_CONFIG` | `0x0008` | **a maior parte da configuração** (ver tabela) |
| `PERM_NET_CONFIG` | `0x0010` | seção `net` do `commit_all` |
| `PERM_FILE_READ` | `0x0020` | listar arquivos · `restore?op=validate` |
| `PERM_FILE_UPLOAD` | `0x0040` | `upload` · `mkdir` |
| `PERM_FILE_DELETE` | `0x0080` | `delete` |
| `PERM_USER_MGR` | `0x0100` | seção `users` · estado de segurança |
| `PERM_CALIB` | `0x0200` | `calib` |

`PERM_ALL_BITS` = `0x03FF` (os dez) · `PERM_FULL_ADMIN` = `0xFFFF`.

### 🔴 O teto que separa os dois

Quatro rotas exigem **igualdade exata** com `PERM_FULL_ADMIN` (`0xFFFF`), e a
página de usuários **não consegue conceder mais que `0x03FF`**. Logo:

> **Nenhuma conta criada pela web — nem marcando todas as caixas — baixa um
> backup completo nem grava firmware.** Isso fica com o `users[0]`, o admin de
> fábrica. É fronteira deliberada, não acidente de atribuição de bits.

As quatro: `GET /api/backup` · `POST /api/restore?op=apply` ·
`POST /api/restore?op=stage` · `POST /api/ota/apply`. (`POST /api/tls` também.)

O motivo de `restore?op=apply` estar aqui: um `.bkp` forjado pode nomear
`/config/system.bin` como destino — o `.bkp` é o dump do FS inteiro.

**Medido no aparelho** (conta `0x03FF`, todas as dez caixas marcadas):

```
commit_all alarms  200      GET  /api/backup     403
commit_all net     200      POST /api/ota/apply  403
commit_all users   200
```

A conta passa em toda seção do `commit_all` e ainda assim não chega nas duas.

### Perfis nomeados (CLI `user perm <nome> <papel>`)

| papel | máscara | o que alcança |
|---|---|---|
| `admin` / `full` | `0xFFFF` | tudo, inclusive backup, OTA e TLS |
| `operator` / `operador` | `0x0207` | painel, histórico, log, calibração |
| `viewer` / `leitor` | `0x0003` | painel e histórico, só leitura |
| `none` / `nenhum` | `0x0000` | nada — a conta existe e não entra |
| `0xMASCARA` | livre | qualquer combinação dos dez bits |

**Padrões de criação:** `user add` pelo CLI dá `0x0203` (painel + histórico +
calibração). Pela web, `perms` é explícito e **ausente = 0** — uma conta criada
sem marcar caixa nenhuma não entra em lugar nenhum.

### O que cada bit destrava, rota a rota

| rota POST | bit(s) |
|---|---|
| `/api/login` `/api/login_chpass` | — (pré-auth, sob lockout por IP) |
| `/api/force_chpass` | autenticado **e** com troca pendente |
| `/api/commit_all` — entrada | qualquer bit que alguma seção use |
| ↳ seção `sys` `slots` `calib` `alarms` | `SYS_CONFIG` |
| ↳ seção `net` | `NET_CONFIG` |
| ↳ seção `users` | `USER_MGR` |
| `/api/action` (todos os `op`) | `SYS_CONFIG` — medido: `0x0008` → 200, `viewer` → 403 |
| `/api/save_sys` `/api/set_time` `/api/reset_touch_cal` `/api/history_rebind` `/api/touch` | `SYS_CONFIG` |
| `/api/clear_logs` | `LOGS` **e** `SYS_CONFIG` — os dois; `SYS_CONFIG` sozinho leva 403 |
| `/api/calib` | `CALIB` |
| `/api/mkdir` `/api/upload` | `FILE_UPLOAD` |
| `/api/delete` | `FILE_DELETE` |
| `/api/restore?op=validate` | `FILE_READ` |
| `/api/restore?op=apply` `?op=stage` · `/api/ota/apply` · `/api/tls` | **`== 0xFFFF`** |

⚠️ **`commit_all` é autorizado por seção, não só na entrada.** A entrada só prova
que há sessão com *algum* bit que a rota usa; cada seção é conferida depois. E a
recusa vale para o payload **inteiro** — nada é aplicado pela metade. Medido:

```
conta 0x0100 (só USER_MGR)      {"users":…}              200
                                {"users":…,"net":…}      403   ← o payload todo
                                {"alarms":…}             403
conta 0x0008 (só SYS_CONFIG)    {"alarms":…}             200
                                {"net":…}                403
                                {"net":…} com _dry=1     403
conta 0x0003 (viewer)           {"alarms":…}             403
```

⚠️ **`_dry=1` exige os mesmos bits** que a gravação de verdade. Validar não é
mais barato em permissão do que aplicar.

⚠️ O `commit_all` recusa qualquer `perms` acima de `0x03FF` na seção `users` —
escalonar para `0xFFFF` por payload não funciona.

---

## Autenticação

### `POST /api/login` — pré-auth
`user`, `pass` (SHA-256 hex), `nonce` (de `GET /api/login_init`).
→ `302` + cookie `SIMUTSESS`, ou 401/429.

### `POST /api/login_chpass` — troca forçada no 1º login — pré-auth
`user`, `oldpass`, `newpass`.

### `POST /api/force_chpass` — autenticado **e** com troca pendente
`p1`, `p2` (as duas iguais).

---

## Configuração

### `POST /api/commit_all` — **a rota principal** · `PERM_SYS_CONFIG` + por seção

Um campo só: `_payload` = JSON com uma ou mais seções. Teto de **6144 B**.

```bash
curl -b j -X POST http://IP/api/commit_all \
     --data-urlencode '_payload={"alarms":{"sensors":[{"idx":0,"tmax":41.5}]}}'
```

**Seções e a permissão de cada uma:**

| seção | permissão | conteúdo |
|---|---|---|
| `sys` | `PERM_SYS_CONFIG` | nome, fuso, log, telemetria, MQTT, 2ª linha, syslog, CORS |
| `slots` | `PERM_SYS_CONFIG` | provisionamento de sensor |
| `calib` | `PERM_SYS_CONFIG` | calibração |
| `alarms` | `PERM_SYS_CONFIG` | limites, liga/desliga, **manutenção** |
| `net` | `PERM_NET_CONFIG` | Wi-Fi, IP, porta web |
| `users` | `PERM_USER_MGR` | contas |

**`_dry=1`** roda todo portão e parser sobre uma **cópia** e não grava nada —
valide um modelo em N aparelhos antes de aplicar. Só `sys` e `net`; as outras
respondem **400** `{"error":"dry run accepts sys and net only"}`. Indisponível no Air.

#### 🆕 A resposta diz se reiniciou, e por quê

```json
{"status":"ok","reboot":false,"applied":["alarms","maint"]}
{"status":"ok","reboot":true,"reboot_for":["identity"],"newPort":8080}
{"status":"ok","reboot":false,"applied":[],"rejected":["t_port"]}
{"status":"dry","rejected":[]}
```

- `applied` — as classes aplicadas **ao vivo**, sem reiniciar.
- `reboot_for` — as classes que **forçaram** o reinício, para o próximo pedido evitá-las.
- `rejected` — campos recusados (fora de faixa, inválidos) que **mantiveram o valor anterior**.
- `creds` — senhas de uso único, quando a seção `users` criou ou resetou conta. Só aqui.

**O que aplica ao vivo** — alarmes · manutenção · 2ª linha de alarmes ·
telemetria pelo lado HTTP · tema e idioma.

**O que ainda reinicia** — rede · nome do aparelho · contas · provisionamento de
slot · cadência/resolução de sensor · MQTT e TLS da telemetria · fuso/NTP · log ·
PIN do display · porta web e overlays.

⚠️ Um campo que ninguém classificou força reinício por segurança (`unclassified`).

#### Chaves de `sys`

`name` `tz` `log` `res` `s_int` `h_int` `cors` `ntp_enabled`
· **telemetria** `t_srv` `t_port` `t_path` `t_key` `t_int` `t_bat` `t_mode`
`t_glob` `t_line` `t_sep` `t_sec` `t_transport`
· **MQTT** `m_topic` `m_user` `m_pass` `m_qos` `m_retain` `m_cid` `m_ka` `m_had`
· **2ª linha (alarmes)** `a_en` `a_mode` `a_qmax` `a_path` `a_glob` `a_line` `a_sep`
· **syslog** `slog_en` `slog_srv` `slog_port` `slog_lvl`

⚠️ `a_line`/`a_glob`/`a_sep` ficam em **`sys`**, não em `alarms`.
⚠️ `t_int` é **lote mínimo em registros**, não milissegundos (config v22+). 0 desliga.
⚠️ `m_qos` só aceita 0 — o transporte não entrega QoS 1/2.

#### Chaves de `net`
`ssid` `pass` `use_dhcp` `ip` `mask` `gw` `dns` `dns1` `dns2` `dns_auto`
`ntp_server` `web_port` `web_ka`
Endereço inválido vai para `rejected`, não é descartado em silêncio.

#### `slots` — provisionamento
```json
{"slots":{"s":[{"i":0,"a":true,"t":2,"p":[2,255,255,255],
                "hwId":"DHT0","name":"Sala","tmin":-10,"tmax":50,"al":true}]}}
```
Mande só os slots editados.

#### `users` — contas
```json
{"users":{"actions":[{"type":"add","name":"op","perms":511},
                     {"type":"del","id":3},
                     {"type":"reset","id":5}]}}
```
`add` e `reset` devolvem a senha em `creds` — entregue **só nessa resposta**.

---

## 🆕 Alarmes e manutenção

### `POST /api/commit_all` seção `alarms` · `PERM_SYS_CONFIG`

```json
{"alarms":{"sensors":[
  {"idx":0, "active":true,
   "tmin":-10, "tmax":45,
   "temp":[-10,45], "hum":[20,80], "press":[900,1100], "lux":[0,5000],
   "maint":7200}
]}}
```

| campo | efeito |
|---|---|
| `idx` | slot 0..15, **obrigatório**; slot inativo é ignorado |
| `active` | liga/desliga o alarme **de limite** deste slot |
| `tmin` `tmax` `hmin` `hmax` | nomes antigos de temperatura e umidade |
| `"<canal>":[min,max]` | forma por canal (`temp` `hum` `press` `lux`) — **vence** sobre a anterior |
| 🆕 `maint` | **segundos a partir de agora**; `0` fecha a janela |

**Tudo isso aplica ao vivo.** Resposta: `{"reboot":false,"applied":["alarms","maint"]}`.

Limite fora da faixa plausível do canal → **400**
`{"error":"Alarm limit outside channel range"}`, e *nenhuma* seção é aplicada —
o commit é atômico. Banda invertida (min ≥ max) é corrigida automaticamente.

#### O modo manutenção

`maint` em **segundos**, não epoch: "este sensor sai por duas horas" não deve
depender de os dois relógios concordarem. Teto **30 dias** — acima disso é
cortado **e** aparece em `rejected`.

Enquanto a janela está aberta, aquele slot:
- **não gera** registro de limite nem de falha na 2ª linha;
- **não pisca nem apita** no painel — desconectar o sensor *é* a manutenção;
- gera exatamente **dois** registros: ao entrar e ao sair.

A janela **persiste em flash** e o aparelho sai dela sozinho no vencimento.

```bash
# põe o slot 3 em manutenção por 2 h
curl -b j -X POST http://IP/api/commit_all \
     --data-urlencode '_payload={"alarms":{"sensors":[{"idx":3,"maint":7200}]}}'
# tira
curl -b j -X POST http://IP/api/commit_all \
     --data-urlencode '_payload={"alarms":{"sensors":[{"idx":3,"maint":0}]}}'
```

`GET /api/alarms` devolve `"maint":<segundos restantes>` por slot (0 = fora).

#### No payload da 2ª linha

Terceiro domínio, separado de `alarm` e `err`, para um servidor que casa por
campo nunca confundir manutenção com falha:

```json
{"ts":1789797592,"id":"tSTM0009","maint":"maint","seq":9}
{"ts":1789797606,"id":"tSTM0009","maint":"maint_end","seq":10}
```

Token `{MAINT}` (alias `{maint}`), forma composta `"maint":{maint}` — a chave
some sozinha quando o registro é de outro domínio. Já está no template default.

⚠️ **Um template custom escrito antes da v23 não tem `{MAINT}`** e emitirá os dois
registros sem marcador. Acrescente o token, ou pergunte por `GET /api/alarms`.

---

## Ações pontuais

### `POST /api/action?op=…` · `PERM_SYS_CONFIG`

| `op` | efeito |
|---|---|
| `reboot` | reinicia sem inventar um commit para isso |
| `tel_sync` | força um ciclo de telemetria agora |
| `tel_reset` | rearma o cursor de telemetria (rebobina o pendente) |
| `sensor_scan` | inicia varredura de sensores → `202 {"started":true}` |
| `scan_results` | resultado da varredura; `{"scanning":true}` enquanto corre |
| `sensor_wipe` | `&slot=N` — apaga o slot e carimba `provisionEpoch` = agora |
| `sensor_accept` | `&slot=N` — aceita o hardware encontrado no slot |

`op` desconhecido → **400** `{"error":"op"}`; slot ausente ou fora de 0..15 →
**400** `{"error":"slot"}`. O `op` é validado **antes** do slot, para um `op`
errado não apontar o chamador para o parâmetro errado. `tel_sync` → `{"ok":true}`.

### `POST /api/set_time` · `PERM_SYS_CONFIG`
JSON cru `{"epoch":1789797592}` → `{"ok":true,"now":1789797592}`. Aplica na hora,
**sem reiniciar**. Com NTP ligado, a próxima sincronização sobrescreve.

### `POST /api/save_sys` · `PERM_SYS_CONFIG`
`theme=<n>` → `{"status":"ok"}`. Troca o tema na hora, sem passar pelo commit.

### `POST /api/reset_touch_cal` · `PERM_SYS_CONFIG`
Sem corpo. Invalida a calibração de toque.

### `POST /api/history_rebind` · `PERM_SYS_CONFIG`
`force=1` opcional. Reancora o histórico ao esquema atual. **Reinicia** ao fim.

### `POST /api/clear_logs` · `PERM_LOGS` **e** `PERM_SYS_CONFIG`
Sem corpo.
⚠️ Não zera o que `show system log` devolve (ele costura o rotacionado) — só
valem *deltas* entre duas leituras.

### `POST /api/touch` · `PERM_SYS_CONFIG`
`x=0..319`, `y=0..239` — um toque no painel, em coordenadas **do painel**.
Fora da faixa é 400, não clamp.
⚠️ **Não espera o painel repintar** antes de responder: o `UiEvent` é consumido
pelo mesmo core que atende a requisição. Quem chama é que espera.

---

## Calibração

### `POST /api/calib` · `PERM_CALIB`
JSON cru, até ~8 KB. Exige NTP sincronizado (**503** se não) e tem limite de
taxa (**429**).

```json
{"sensors":[{"slot":0, "name":"Sala",
             "refs":{"temp":25.0,"hum":60.0,"press":1013.2},
             "end":false}]}
```
`slot` (ou `gpio`) identifica; `refs` por canal é a forma nova —
`refTemp`/`refHum`/`refPress` continuam aceitos.

---

## Arquivos

### `POST /api/upload` · `PERM_FILE_UPLOAD`
`multipart/form-data`, campo de arquivo; `uploadDir` opcional define o destino.
```bash
curl -b j -F "file=@pack.lng" -F "uploadDir=/lang" http://IP/api/upload
```
⚠️ Pacote `.lng` só passa a valer **no boot seguinte**.

### `POST /api/mkdir` · `PERM_FILE_UPLOAD`
`dir=/caminho`. Ausente → 400 `Missing dir`.

### `POST /api/delete` · `PERM_FILE_DELETE`
`file=/caminho` — o parâmetro é **`file`**, não `path`. Ausente → 400 `Bad Request`.

---

## OTA e restauração

### `POST /api/restore?op=…` `multipart`
| `op` | permissão | efeito |
|---|---|---|
| `validate` | `PERM_FILE_READ` | confere o `.bkp` sem gravar |
| `apply` | **`== PERM_FULL_ADMIN`** | sobrescreve o LittleFS inteiro |
| `stage` | **`== PERM_FULL_ADMIN`** | prepara uma imagem de firmware |

A permissão é conferida no **primeiro byte** do fluxo, não só no fim.

### `POST /api/ota/apply` · **`== PERM_FULL_ADMIN`**
`test=1` para um ensaio sem aplicar.
⚠️ **O apply zera o `/history` e o LittleFS** — faça backup antes.
⚠️ Transferência grande na porta 80 cai (RST do roteador): use `:8080`.

### `POST /api/tls` · **`== PERM_FULL_ADMIN`** · só onde há HTTPS
Corpo cru com os **dois blocos PEM concatenados** (certificado + chave privada),
até 8 KB. Os dois precisam **casar** antes de qualquer arquivo ser escrito.
```bash
curl -b j -X POST --data-binary @<(cat cert.pem key.pem) http://IP/api/tls
```
Chave com passphrase é recusada com a instrução para decifrar.
⚠️ `br_rsa_compute_pubexp` exige `p ≡ 3 mod 4` e recusa ~metade das chaves RSA —
**P-521 (EC) funciona sempre**.

---

## Códigos de resposta

| | |
|---|---|
| **200** | feito (veja `reboot`/`applied`/`rejected` no corpo) |
| **202** | aceito e rodando (varredura de sensores) |
| **302** | login OK |
| **400** | corpo/parâmetro inválido — nada foi aplicado |
| **401** | sem sessão |
| **403** | sessão sem o bit necessário |
| **409** | senha pendente de troca → `/api/force_chpass` |
| **413** | corpo acima do teto |
| **429** | limite de taxa (login, calib, tls) |
| **500** | falha ao gravar |
| **503** | ocupado (calibração de toque, varredura, sem NTP, sem memória) |
