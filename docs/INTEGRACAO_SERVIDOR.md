# Manual de integração — servidor da empresa × SIMUT

**Para quem vai escrever o servidor.** Tudo o que os dois lados trocam, em que
formato, com que garantias — e onde estão as armadilhas que já morderam esta
bancada.

**Base:** firmware **v2.6.1-beta** · **Estado:** Living · **Transporte desta
primeira fase: HTTP puro, sem TLS** (§8 diz o que isso custa e o que fazer
quando for a hora).

Os números e formatos deste documento vêm da fonte do firmware e de chamadas
reais ao aparelho do laboratório (`192.168.3.24`), não de leitura de código
apenas. Onde a referência completa de uma rota existir, este manual aponta para
ela em vez de copiá-la: [`API_POST.md`](API_POST.md) tem as 18 rotas `POST` e
[`AUTHORIZATION.md`](AUTHORIZATION.md) é a **fonte normativa** das permissões —
se algum dia discordarem deste texto, elas estão certas.

---

## 1. O desenho em uma página

São **dois sentidos de tráfego, independentes**, e o programador do servidor
precisa implementar os dois lados:

```
   ┌──────────────┐   POST periódico (o aparelho empurra)    ┌─────────────┐
   │              │ ───────────────────────────────────────► │             │
   │    SIMUT     │   1) telemetria (leituras do histórico)  │  servidor   │
   │  (aparelho)  │   2) 2ª linha  (alarmes e manutenção)    │  da empresa │
   │              │                                          │             │
   │              │ ◄─────────────────────────────────────── │             │
   └──────────────┘   HTTP com sessão (o servidor comanda)    └─────────────┘
                      3) contas do painel, nomes de sensor,
                         limites, bloqueio, manutenção
```

| sentido | quem começa | autenticação | o que trafega |
|---|---|---|---|
| **entrada** (§3, §4) | o aparelho | opcional, por cabeçalho que você escolhe | leituras e eventos de alarme |
| **saída** (§5, §6) | o servidor | sessão de usuário do aparelho | configuração |

**A consequência prática:** o receptor de telemetria **não precisa** de login, e
o comando **não pode** ser feito pelo receptor. São dois componentes.

---

## 2. Identidade: como saber de qual aparelho é cada coisa

**Todo POST** que o aparelho manda — telemetria e alarmes — carrega quatro
cabeçalhos:

```http
X-SIMUT-Uid: E6642815E34C1824     ← serial do RP2040. É A CHAVE. Nunca muda.
X-SIMUT-Ver: 2.6.1-beta           ← versão do firmware
X-SIMUT-Env: release              ← variante: release | alpha | air
X-SIMUT-Cfg: EEC9D73D             ← CRC-32 da configuração em RAM
```

- **Use `X-SIMUT-Uid` como chave primária do aparelho no seu banco.** Não use o
  IP (é DHCP) nem o nome (o usuário edita).
- **`X-SIMUT-Cfg` é um atalho valioso:** dois aparelhos com o mesmo `cfg` têm a
  mesma configuração; um `cfg` diferente do que você viu da última vez significa
  *"alguém mexeu na configuração deste aparelho"* — e você descobre isso **sem
  abrir sessão**, só olhando o cabeçalho do POST que já ia chegar.
- `X-SIMUT-Ver` responde "a atualização entrou?" pelo mesmo caminho.

⚠️ **MQTT não tem cabeçalho.** Se um dia a frota usar MQTT, a identidade passa a
ser o `clientId`. Neste projeto o transporte é HTTP, então os quatro valem.

---

## 3. Entrada 1 — telemetria (as leituras)

### 3.1 Quando o aparelho manda

Duas configurações governam a cadência, e as duas estão no aparelho:

| campo | significado | valor do laboratório |
|---|---|---|
| `h_int` | intervalo do **histórico**, em minutos: de quanto em quanto tempo nasce um registro | `1` |
| `t_int` | **lote mínimo em registros** para disparar um envio. `0` desliga a telemetria | `1` |
| `t_bat` | teto de registros por POST | `250` |

⚠️ **`t_int` é quantidade de registros, não milissegundos** (mudou na config
v22). Com `t_int=1` o aparelho manda assim que existe 1 registro novo — na
prática, um POST por minuto. Com `t_int=60` ele acumula uma hora e manda de uma
vez, o que é o modo que economiza rádio.

**O aparelho nunca perde a medição por não conseguir enviar.** O registro nasce
na flash (histórico) e a telemetria só carrega um **cursor** por cima dele.
Medido nesta bancada em 21/09/2026: 3 h 58 sem servidor → 237 registros
enfileirados → servidor volta → **fila a zero numa rodada, 239 registros
entregues, nenhum faltando**.

### 3.2 O formato — três modos

O modo é o campo `t_mode`: **0 = JSON**, 1 = CSV, 2 = custom.

#### Modo JSON (`t_mode: 0`) — o recomendado

Corpo: **um array puro**. `Content-Type: application/json`.

```json
[
  {"ts":1790007659,"tSTM0009":24.06,"tSTM0010":23.96,"tSTH0003":24.60,
   "uSTH0003":78.6,"tSTB0001":24.32,"pSTB0001":999.4,
   "tSTH0001":24.70,"uSTH0001":80.1},
  {"ts":1790007719,"tSTM0009":24.12, … }
]
```

**A regra das chaves** — cada leitura vira uma chave `<prefixo><hwId>`:

| prefixo | grandeza | casas decimais |
|---|---|---|
| `t` | temperatura (°C) | 2 |
| `u` | umidade (%) | 1 |
| `p` | pressão (hPa) | 1 |

- `hwId` é o identificador do sensor, definido no provisionamento do slot
  (`STM0009`, `STH0001`…). Sem `hwId`, a chave cai para `t<índice do slot>`
  (`t0`, `t3`).
- **Chave ausente = não houve leitura** naquele instante. Não é zero, não é
  nulo: a chave simplesmente não aparece. Um servidor que preenche com `0` o que
  não veio inventa dado.
- `ts` é **epoch Unix em segundos, UTC**.

#### Modo CSV (`t_mode: 1`)

`Content-Type: text/csv`. Cabeçalho + linhas, layout **fixo de 34 colunas**:
`timestamp;s0..s15;h0..h15;press` — todos os 16 slots, ativos ou não.

⚠️ O cabeçalho nomeia os slots ativos com `hwId` (`;s0_STM0009`), mas **as linhas
sempre têm as 34 colunas**. Leia por posição, não por contagem de cabeçalho.

#### Modo custom (`t_mode: 2`)

Dois templates editáveis (`t_glob` e `t_line`) para quem precisa falar com um
receptor que já existe e não pode mudar:

```
t_glob: {"dev":"{DEV}","mac":"{MAC}","data":[{DATA}]}
t_line: {"ts":{TS},"t0_ID":{t0},"u0_ID":{u0}}
```

| token | vira |
|---|---|
| `{DEV}` | nome do aparelho |
| `{MAC}` | MAC do Wi-Fi |
| `{DATA}` | as linhas, unidas pelo separador `t_sep` |
| `{TS}` | epoch |
| `{t0}`…`{t15}`, `{u0}`…, `{p0}`… | valor do canal naquele slot |

**Forma composta:** escrever `"t0_ID":{t0}` faz a **chave ser reescrita** para o
`hwId` do slot (`"tSTM0009":24.06`); se o slot não tem leitura, a chave inteira
some. É assim que o modo JSON default é reproduzível em custom.

### 3.3 O contrato de resposta — a parte que mais importa

> **2xx = entregue. Qualquer outra coisa = não entregue.**

- **2xx** → o aparelho avança o cursor. Aqueles registros não voltam.
- **Qualquer outro código** (incluindo um 500 bem formado), **timeout**, RST,
  resposta truncada → o cursor **não avança** e o lote inteiro é reenviado no
  próximo ciclo.
- O aparelho espera **4 segundos** pela resposta (`NET_SOCKET_TIMEOUT_MS`).
  **Responda em menos que isso.** Se o seu servidor grava em banco antes de
  responder, garanta que a gravação cabe nesse orçamento — ou responda 200 e
  processe depois (aceitando a consequência: você assumiu a entrega).

**Idempotência é responsabilidade do servidor.** A chave natural é
`(X-SIMUT-Uid, ts)`. Duplicatas acontecem por desenho: se o seu 200 se perder no
caminho, o aparelho reenvia — e o firmware prefere explicitamente uma duplicata
a um buraco. Medido: 264 registros recebidos num dreno, 263 instantes distintos.

⚠️ **Nunca responda 2xx para dizer "recebi mas não gravei".** Para o aparelho,
2xx é a única confirmação que existe; depois dele o dado não volta mais.

---

## 4. Entrada 2 — a segunda linha (alarmes)

Uma fila **em RAM** separada, com confirmação de recebimento. Serve para o
evento chegar em segundos, enquanto a telemetria comum anda no ritmo do lote.

| característica | valor |
|---|---|
| destino | `a_path` verbatim; vazio = `t_path` + `/alarm` |
| fila | `a_qmax` registros (1..64, default 32) — **RAM: reinício perde o que não foi confirmado** |
| retentativa | a cada **15 s** enquanto houver fila |
| confirmação | **2xx esvazia a fila**; qualquer outra coisa mantém e repete |
| formato | `a_mode`: 0 JSON, 1 CSV, 2 custom (independente do `t_mode`) |

⚠️ **Se `a_path` == `t_path`, as duas linhas chegam no mesmo endpoint.** É a
configuração do laboratório hoje, e foi medida: num mesmo dreno chegaram 239
registros de histórico e 25 de alarme no mesmo caminho. **Distinga pela forma**
(registro de alarme tem `id` e um dos campos `alarm`/`err`/`maint`), ou — melhor
— **configure `a_path` diferente** e não dependa de heurística.

### 4.1 A forma de um registro de alarme

```json
{"ts":1789834707,"id":"tSTM0009","alarm":"alarm_lim","lo":2.70,"hi":24.00,"user":"pjoao","seq":1}
```

| campo | sempre? | o que é |
|---|---|---|
| `ts` | sim | epoch do evento |
| `id` | sim | `<prefixo><hwId>` — o mesmo identificador da telemetria |
| `seq` | sim | sequência **do boot**; é a chave da confirmação. Reinicia em 1 a cada boot |
| `val` | só em `alarm` | o valor lido que cruzou o limite |
| `alarm` / `err` / `maint` | um deles | o domínio do evento (tabela abaixo) |
| `lo` / `hi` | só em `alarm_lim` | os limites novos |
| `until` | só em `maint_on` | epoch do fim previsto da janela |
| `user` | quando houve gente | nome de quem agiu; vazio = o próprio aparelho |

**Um registro pertence a um único domínio.** A chave dos outros dois **não
aparece** — não vem vazia, não vem nula. Um servidor que casa por campo nunca
confunde manutenção com falha.

### 4.2 Todos os códigos

| campo | código | significa |
|---|---|---|
| `alarm` | `alarm` | limite cruzado (**único com `val`**) |
| `alarm` | `alarm_sil` | alguém silenciou no painel |
| `alarm` | `alarm_off` | alarmes daquele sensor desligados |
| `alarm` | `alarm_on` | religados |
| `alarm` | `alarm_lim` | limites editados — traz `lo` e `hi` |
| `err` | `err` | falha de hardware do sensor |
| `err` | `err_sil` | falha silenciada |
| `err` | `err_off` | alarme de falha desativado |
| `maint` | `maint_on` | entrou em manutenção — traz `until` |
| `maint` | `maint_off` | saiu, por comando ou por vencimento do prazo |

**Manutenção é o que evita o falso positivo:** enquanto a janela está aberta, o
slot **não gera** registro de limite nem de falha. Ele gera exatamente dois
registros — entrada e saída. É o jeito de dizer ao servidor *"o que vier deste
sensor até segunda ordem não é alarme"*.

### 4.3 CSV e custom

No modo CSV a linha é `seq;ts;id;v;user;lo;hi;until` (colunas vazias quando não
se aplicam). No custom valem os tokens `{TS} {ID} {HWID} {SLOT} {CH} {VAL}
{ALARM} {ERR} {MAINT} {LO} {HI} {UNTIL} {USER} {SEQ}`, com a mesma regra da
chave composta que some.

⚠️ Um template custom escrito antes da v23/v24 **não tem** `{MAINT}`, `{LO}`,
`{HI}`, `{UNTIL}` nem `{USER}` e emitirá esses eventos sem esses campos.

---

## 5. Saída — o servidor comandando o aparelho

### 5.1 Abrir sessão

Duas requisições. A senha viaja como **SHA-256 hexadecimal do texto puro** —
nunca em claro, mesmo sem TLS.

```bash
# 1) nonce
curl -c j -s http://IP/api/login_init
# {"nonce":"a1b2…","locked":false,"lockSec":0}

# 2) login
curl -b j -c j -s http://IP/api/login \
  -d "user=servidor&pass=$(printf %s 'senha' | sha256sum | cut -d' ' -f1)&nonce=a1b2…"
# {"ok":true,"redirect":"/"}  + cookie SIMUTSESS
```

Daí em diante, o cookie `SIMUTSESS` — **ou** o mesmo token como
`Authorization: Bearer <SIMUTSESS>`, que é o que serve para um cliente sem *cookie
jar*.

**O que seu código precisa tratar:**

| situação | resposta | o que fazer |
|---|---|---|
| sem sessão / expirada | **401** | relogar uma vez e repetir |
| sessão válida, falta o bit | **403** | não insista: a conta não tem a permissão |
| senha errada | 401 + backoff | `login_init` devolve `locked` e `lockSec`; **consulte antes de insistir** |
| troca de senha pendente | **409** com `{"next":"/api/force_chpass"}` | a conta precisa trocar a senha antes de configurar |
| calibração de toque em curso | **503** | tente de novo em alguns segundos |
| requisições rápidas demais | `{"error":"Too Fast"}` | espaçar. O aparelho é um RP2040, não um cluster |

⚠️ **Três slots de sessão.** Não abra uma sessão por requisição: reaproveite o
cookie e relogue só no 401.

### 5.2 A conta que o servidor deve usar

Permissões são **bits independentes, sem hierarquia** — o aparelho testa o bit,
não um "nível":

| bit | valor | abre |
|---|---|---|
| `PERM_DASHBOARD` | `0x0001` | `/api/status`, painel |
| `PERM_HISTORY` | `0x0002` | histórico e exportação |
| `PERM_SYS_CONFIG` | `0x0008` | **a maior parte da configuração**, inclusive alarmes e manutenção |
| `PERM_NET_CONFIG` | `0x0010` | seção `net` |
| `PERM_USER_MGR` | `0x0100` | seção `users` (criar contas) |
| `PERM_ALARM_LIMITS` | `0x0400` | painel: editar limites |
| `PERM_ALARM_BLOCK` | `0x0800` | painel: ligar/desligar alarmes |
| `PERM_MAINT` | `0x1000` | painel: abrir/encerrar manutenção |

**Recomendação:** crie no aparelho uma conta só para o servidor, com
`PERM_DASHBOARD | PERM_SYS_CONFIG | PERM_USER_MGR` = `0x0109` = **265**, e nada
mais. Não use a conta `admin` do mantenedor.

⚠️ Os três bits `ALARM_LIMITS`/`ALARM_BLOCK`/`MAINT` governam **o painel na
parede** (identificação por PIN), não a API. Pela API, o que vale é
`PERM_SYS_CONFIG`.

### 5.3 A rota que faz quase tudo: `POST /api/commit_all`

Um único campo `_payload` com JSON de uma ou mais seções. **Teto de 6144 B.**

```bash
curl -b j -X POST http://IP/api/commit_all \
  --data-urlencode '_payload={"alarms":{"sensors":[{"idx":0,"tmax":41.5}]}}'
```

**A resposta diz o que aconteceu** — leia-a, não a presuma:

```json
{"status":"ok","reboot":false,"applied":["alarms","maint"]}
{"status":"ok","reboot":true,"reboot_for":["users"],"creds":[{"u":"op1","p":"…"}]}
{"status":"ok","reboot":false,"applied":[],"rejected":["t_port"]}
```

| campo | significa |
|---|---|
| `applied` | classes aplicadas **ao vivo**, sem reiniciar |
| `reboot` / `reboot_for` | o aparelho reiniciou, e por causa de quê |
| `rejected` | campos **recusados** (fora de faixa, inválidos) — o valor anterior ficou |
| `creds` | senhas de uso único, **só nesta resposta** |

⚠️ **Quem decide se precisa reiniciar é o aparelho**, comparando com a
configuração corrente. Um campo reenviado igual ao valor atual **não é mudança**
e não reinicia nada. Não reimplemente essa regra no servidor: vocês vão
discordar no dia em que um campo mudar de classe.

⚠️ **`rejected` chega com HTTP 200.** Um servidor que só olha o código de status
vai achar que gravou. **Sempre leia `rejected`.**

**Três modos de ensaio**, úteis para uma plataforma que aplica modelo em frota:

| campo extra | o que faz |
|---|---|
| `_dry=1` | roda todos os portões numa **cópia**, não grava nada, devolve a classificação |
| `_nosave=1` | aplica na **RAM**; um reinício desfaz. **409** se a mudança exigir reinício |
| `_reboot=1` | grava e reinicia mesmo sem precisar |

`_dry` e `_nosave` aceitam só `sys`, `net` e `alarms`.

### 5.4 Cadastrar um usuário do painel

Seção `users` (exige `PERM_USER_MGR`). O PIN é o que identifica a pessoa **no
painel da parede**; a senha é para a web.

```bash
curl -b j -X POST http://IP/api/commit_all --data-urlencode '_payload={
  "users":{"actions":[
    {"type":"add","name":"joao","perms":6144,"pin":"482913"}
  ]}}'
```

Resposta:
```json
{"status":"ok","reboot":true,"reboot_for":["users"],"creds":[{"u":"joao","p":"Xk9mQ2vP"}]}
```

| ação | forma |
|---|---|
| criar | `{"type":"add","name":"…","perms":<int>,"pin":"…"}` — `pin` opcional |
| apagar | `{"type":"del","id":<slot>}` |
| nova senha | `{"type":"reset","id":<slot>}` → devolve em `creds` |
| definir/remover PIN | `{"type":"pin","id":<slot>,"pin":"…"}` — `""` remove; `id:0` é o admin |

**Para o caso de uso de vocês** (a pessoa vai bloquear alarme, abrir manutenção
e silenciar no painel), as permissões úteis são:

```
PERM_ALARM_BLOCK (0x0800) + PERM_MAINT (0x1000)                    = 6144
+ PERM_ALARM_LIMITS (0x0400) se também puder editar limites        = 7168
```

**Três coisas que o servidor precisa saber:**

1. **A senha só existe nessa resposta.** Não há como relê-la depois. Se o seu
   fluxo é "a plataforma cria e mostra ao usuário", mostre `creds` e siga; se é
   "a plataforma guarda", guarde **na hora**.
2. **Criar conta reinicia o aparelho** (`reboot_for:["users"]`). Ele volta
   sozinho em ~30 s, mas o painel fica fora do ar nesse tempo. Agrupe as contas
   num único `commit_all` em vez de uma por requisição.
3. **PIN malformado ou repetido rejeita o campo, não a conta** — vem `200` com
   `"rejected":["users.pin"]` e a conta entra **sem** PIN. Leia o `rejected` e
   avise o usuário, senão ele vai ao painel e não entra.

⚠️ **Política de PIN.** O comprimento aceito depende de `pin_min`, `pin_kb`
(glifos por tecla: 1/2/3) e `pin_alpha` (0 = só dígitos, 1 = `0-9A-Z`). Leia a
política em `GET /api/config` antes de validar o PIN no seu formulário — o
aparelho vai recusar o que não obedecer. Máximo 16/12/8 caracteres conforme o
teclado. **Apertar a política marca toda conta com PIN para trocá-lo.**

Estado atual das contas: `GET /api/users` →
`[{"id":0,"name":"admin","perms":65535,"pin":true},…]` — diz **se** tem PIN,
nunca qual é.

### 5.5 Editar o nome de um sensor

Seção `slots`. Mande **só os slots editados**.

```bash
curl -b j -X POST http://IP/api/commit_all --data-urlencode '_payload={
  "slots":{"s":[{"i":0,"name":"Câmara fria 1"}]}}'
```

| campo | o que é |
|---|---|
| `i` | índice do slot, 0..15 — **obrigatório** |
| `name` | o nome que aparece no painel e na web |
| `a` | ativo (`true`/`false`) |
| `t` | tipo do sensor |
| `p` | pinos |
| `hwId` | identificador que vira **prefixo das chaves da telemetria** |
| `lim` | limites por canal: `{"temp":[2,8],"hum":[20,80]}` |
| `al` | alarme ligado (`true`/`false`) |

**Campo ausente = campo preservado.** O parser só escreve o que veio: mandar
`{"i":0,"name":"Câmara fria 1"}` não apaga tipo, pinos, `hwId` nem limites.

🔴 **`tmin`/`tmax`/`hmin`/`hmax` NÃO funcionam nesta seção.** O comentário do
firmware e a tabela do `API_POST.md` os listavam, mas o parser da seção `slots`
nunca os leu: o commit responde **200, `applied` vazio, `rejected` vazio** e
**nada acontece**. Achado em 21/09/2026 ao escrever este manual; comentário e
documento corrigidos no mesmo dia. Para limites, use a seção `alarms` (§5.6) ou
o campo `lim` acima.

⚠️ **Mudar `hwId` muda as chaves do payload** (`tSTM0009` → `tOUTRO`) e o
histórico já gravado continua com o antigo. Trate `hwId` como imutável depois de
instalado; para renomear o que a pessoa vê, use `name`.

⚠️ **Provisionamento de slot reinicia o aparelho.**

### 5.6 Limites de alarme, bloqueio e manutenção

Seção `alarms` — **tudo isto aplica ao vivo, sem reiniciar**.

```bash
# limites por canal (forma nova, vence sobre tmin/tmax)
curl -b j -X POST http://IP/api/commit_all --data-urlencode '_payload={
  "alarms":{"sensors":[{"idx":0,"temp":[2.0,8.0],"hum":[20,80]}]}}'

# bloquear (desligar) os alarmes de limite do slot 3
… '_payload={"alarms":{"sensors":[{"idx":3,"active":false}]}}'

# manutenção por 2 horas
… '_payload={"alarms":{"sensors":[{"idx":3,"maint":7200}]}}'

# encerrar a manutenção agora
… '_payload={"alarms":{"sensors":[{"idx":3,"maint":0}]}}'
```

| campo | efeito |
|---|---|
| `idx` | slot 0..15, **obrigatório**; slot inativo é ignorado |
| `active` | liga/desliga o alarme **de limite** deste slot (é o "bloquear") |
| `"<canal>":[min,max]` | `temp`, `hum`, `press`, `lux` — a forma recomendada |
| `tmin` `tmax` `hmin` `hmax` | nomes antigos, ainda aceitos |
| `maint` | **segundos a partir de agora**, `0` fecha. Teto **30 dias** |

**Por que `maint` é em segundos e não epoch:** "este sensor sai por duas horas"
não deve depender de os dois relógios concordarem. Acima de 30 dias o valor é
cortado **e** aparece em `rejected`.

**A janela persiste em flash** e o aparelho sai dela sozinho no vencimento,
emitindo o `maint_off` — mesmo que o servidor nunca mais fale com ele.

⚠️ **Limite fora da faixa plausível do canal → HTTP 400 e NENHUMA seção é
aplicada.** O commit é atômico: se você mandou cinco sensores e um limite é
absurdo, os cinco ficam como estavam. Banda invertida (min ≥ max) é corrigida
automaticamente, sem erro.

### 5.7 Silenciar: **não dá pela rede**

Não existe rota HTTP para silenciar. `alarm_sil` e `err_sil` nascem **só** de um
toque no painel (`EVT_ALARM_SILENCE`, 120 s por default).

O servidor **recebe** o evento de silenciamento na 2ª linha e pode mostrá-lo;
**não pode causá-lo**. O que o servidor pode fazer remotamente com efeito
parecido:

| intenção | o que usar |
|---|---|
| "pare de tocar agora" | nada pela rede — é ação local, por desenho |
| "este sensor não deve alarmar por um tempo" | `maint` (§5.6) — e o servidor **sabe** que é manutenção |
| "este sensor não alarma mais" | `active:false` |

### 5.8 Ler o estado

| rota | permissão | serve para |
|---|---|---|
| `GET /api/status` | `DASHBOARD` | identidade, versão, uptime, heap, `pending`, métricas |
| `GET /api/alarms` | `DASHBOARD` | limites por canal, `active`, **`maint` em segundos restantes** |
| `GET /api/config` | `SYS_CONFIG` | a configuração inteira, inclusive política de PIN |
| `GET /api/users` | `USER_MGR` | contas, permissões, se tem PIN |
| `GET /api/sensors` | `DASHBOARD` | leituras correntes |
| `GET /api/history_days` | `HISTORY` | dias disponíveis |
| `GET /api/history/open` | `HISTORY` | o bloco **ainda não selado** do dia corrente |

⚠️ **`active` no `/api/alarms` é o bit de ALARME, não "o sensor existe".** Um
sensor instalado e funcionando com alarme desligado vem `active:false`. Já
confundiu gente nesta bancada.

⚠️ **O arquivo `<dia>.h5` só tem blocos selados.** Quem baixa só ele é cego na
ponta recente: o que foi medido desde o último selo está em
`GET /api/history/open`. Se o seu servidor reconciliar histórico, leia os dois.

---

## 6. Receita completa: cadastrar um usuário de painel, do zero

```bash
IP=192.168.3.24
PASS='senha-da-conta-do-servidor'

# 1. sessão
NONCE=$(curl -c j -s http://$IP/api/login_init | sed 's/.*"nonce":"\([^"]*\)".*/\1/')
curl -b j -c j -s -o /dev/null http://$IP/api/login \
  -d "user=servidor&pass=$(printf %s "$PASS" | sha256sum | cut -d' ' -f1)&nonce=$NONCE"

# 2. ler a política de PIN antes de validar no formulário
curl -b j -s http://$IP/api/config | python3 -c \
  'import sys,json; d=json.load(sys.stdin); print({k:d[k] for k in ("pin_min","pin_kb","pin_alpha")})'

# 3. criar a conta com PIN e as permissões de painel (bloqueio + manutenção)
curl -b j -s -X POST http://$IP/api/commit_all --data-urlencode \
  '_payload={"users":{"actions":[{"type":"add","name":"joao","perms":6144,"pin":"482913"}]}}'
# → {"status":"ok","reboot":true,"reboot_for":["users"],"creds":[{"u":"joao","p":"…"}]}
#   GUARDE creds AGORA. O aparelho vai reiniciar: espere ~30 s antes da próxima chamada.

# 4. conferir (depois do reboot, com sessão nova)
curl -b j -s http://$IP/api/users
```

---

## 7. Armadilhas medidas — leia antes de codar

| # | armadilha | o que fazer |
|---|---|---|
| 1 | **`rejected` vem com HTTP 200** | sempre leia `rejected`; status 200 não é "gravou" |
| 2 | **2ª linha e telemetria no mesmo `path`** quando `a_path == t_path` | configure `a_path` distinto, ou distinga pela forma do registro |
| 3 | **`ts` ausente de uma chave = sem leitura** | não substitua por `0` |
| 4 | **Duplicatas por desenho** | chave `(uid, ts)`; o firmware prefere duplicar a perder |
| 5 | **4 s de timeout** | responda rápido; processe depois se precisar |
| 6 | **Fila de alarmes é RAM** | um reinício perde o que não foi confirmado — confirme com 2xx rápido |
| 7 | **`seq` reinicia a cada boot** | não use `seq` como chave global; use `(uid, ts, seq)` |
| 8 | **Criar conta reinicia** | agrupe num commit só |
| 9 | **Senha de conta nova só aparece uma vez** | guarde no momento |
| 10 | **Segredos voltam mascarados** (`"Bobi***"`) no `GET /api/config` | nunca reenvie a máscara; omita o campo para manter |
| 11 | **Fluxo grande na porta 80 morre em alguns roteadores** (>12–15 s) | para upload/OTA use a porta alternativa (`web_port`) |
| 12 | **`Too Fast`** | espaçar requisições; não faça polling agressivo |
| 13 | **Cursor escalar pode pular registro** de bloco fora de ordem — medido: **6 em 75.778 (0,0079%)** | não é falha do seu servidor; o dado está na flash e sai pelo histórico |
| 14 | **A CLI serial corta template em 63 caracteres em silêncio** | configure templates **pela web**, nunca pela serial |
| 15 | **Manutenção some do histórico de alarmes** (não gera limite nem falha) | trate `maint_on`/`maint_off` como o par que explica o silêncio |
| 16 | **Campo que o parser não conhece é ignorado em silêncio** — `tmin` em `slots` foi o caso encontrado | confira o efeito lendo o estado de volta (`GET /api/alarms`), não o `applied` |

---

## 8. Segurança nesta primeira fase (HTTP puro)

O que **já** protege, mesmo sem TLS:

- a senha **nunca** viaja em claro: vai como SHA-256 do texto puro, com nonce;
- sessão por token com prazo e três slots;
- backoff exponencial por IP no login;
- permissões por bit, testadas rota a rota (61 rotas, nenhuma sem portão —
  `AUTHORIZATION.md` é auditada por portão de CI).

O que **não** protege, e o programador precisa saber:

- **quem estiver na rede lê tudo**: leituras, nomes, PINs enviados num `commit_all`,
  e o token de sessão do cookie;
- **quem estiver na rede pode repetir uma requisição** que capturou;
- **nada autentica o aparelho para o servidor**: qualquer um pode POSTar um
  payload com os cabeçalhos `X-SIMUT-*` de outro aparelho.

**Recomendações para esta fase:**

1. **Rede fechada.** Aparelhos e servidor na mesma VLAN, sem rota para fora.
2. **Um segredo no cabeçalho do POST de telemetria.** O campo `t_key` do
   aparelho vira `Authorization: Bearer <valor>` — ou, se contiver `:`, vira o
   par `Nome: valor`. Configure um por aparelho e **recuse no servidor o POST que
   não trouxer o seu**. Não é criptografia; é a diferença entre "qualquer um na
   rede" e "quem capturou o tráfego daquele aparelho".
3. **Conta dedicada e mínima** para o servidor (§5.2), nunca a do mantenedor.
4. **Não exponha o aparelho à internet.** Nem com porta redirecionada.

**Quando for a hora do TLS:** o firmware já serve HTTPS (imagem com
`SIMUT_WEB_HTTPS`), o certificado se instala por `POST /api/tls` e a telemetria
tem modo cifrado (`t_sec`). O que muda no servidor é a URL e a validação do
certificado — o resto deste manual continua valendo.

---

## 9. Checklist de aceitação do servidor

Antes de ligar em produção, prove cada linha:

- [ ] recebe um lote JSON de 250 registros e responde 2xx em **menos de 4 s**
- [ ] responde **não-2xx** quando não gravou, e o aparelho reenvia
- [ ] grava `(uid, ts)` de forma idempotente — reenviar o mesmo lote não duplica
- [ ] distingue registro de histórico de registro de alarme no mesmo endpoint
- [ ] trata os dez códigos de alarme, inclusive `maint_on`/`maint_off`
- [ ] guarda `X-SIMUT-Uid` como chave e nota mudança de `X-SIMUT-Cfg` e `-Ver`
- [ ] relogar no 401 e **não** insistir no 403
- [ ] lê `rejected` de todo `commit_all` e mostra ao usuário
- [ ] trata o reinício depois de criar conta (espera e reconecta)
- [ ] guarda `creds` na resposta que as traz
- [ ] valida PIN contra `pin_min`/`pin_kb`/`pin_alpha` do aparelho
- [ ] não tenta silenciar pela rede (§5.7)

---

## 10. Onde está o resto

| assunto | documento |
|---|---|
| todas as 18 rotas `POST`, campo a campo | [`API_POST.md`](API_POST.md) |
| matriz de permissões (normativa) | [`AUTHORIZATION.md`](AUTHORIZATION.md) |
| desenho da 2ª linha de alarmes | [`analysis/ANALISE_TELEMETRIA_ALARMES.md`](analysis/ANALISE_TELEMETRIA_ALARMES.md) |
| cadência e lote da telemetria | [`analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md`](analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md) |
| gestor de frota (centenas de aparelhos) | [`analysis/ANALISE_GESTOR_FROTA.md`](analysis/ANALISE_GESTOR_FROTA.md) |
| atualização pelo ar | [`OTA_USAGE.md`](OTA_USAGE.md) |
| formato do histórico em disco | [`HistoryV5_Instrucoes_Implementacao.md`](HistoryV5_Instrucoes_Implementacao.md) |
