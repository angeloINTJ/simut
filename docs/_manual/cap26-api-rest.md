# API REST {#cap-26}

Este capítulo é a referência da interface de programação do aparelho: como abrir sessão, o que cada uma das 62 rotas faz, que permissão ela exige e o que ela responde. É para quem escreve um cliente, um script ou um servidor de gestão que fala com o aparelho.

## Antes de começar {#cap-26-antes}

A interface de programação é a mesma que a interface web usa. Toda página do navegador busca os dados em rotas `/api/...`, e tudo que a página faz um cliente seu também pode fazer, com a mesma conta e as mesmas permissões.

### Endereço e porta {#cap-26-endereco}

| Situação | Endereço base |
|---|---|
| HTTP, porta de fábrica | `http://192.0.2.10` |
| HTTP, outra porta | `http://192.0.2.10:8080` (o campo **Porta HTTP**, [capítulo 9](#cap-09-servidor-web)) |
| [release]{.img} HTTPS, com o par de certificados instalado | `https://192.0.2.10` (porta 443) |
| [release]{.img} HTTPS com outra porta configurada | `https://192.0.2.10:8443`, na porta configurada |
| Ponto de acesso de configuração | Sempre HTTP ([capítulo 9](#cap-09-ap)) |
| [release]{.img} Pelo nome | `http://simut.local`, com o **Nome** do aparelho ([capítulo 9](#cap-09-mdns)) |

O aparelho atende um protocolo por vez. Com o par de certificados instalado, a imagem release sobe só em HTTPS e não abre um HTTP ao lado. Quando a porta configurada é a de fábrica, 80, o HTTPS usa a 443; qualquer outra porta configurada é usada como está, já com TLS. Sem par, ou no ponto de acesso de configuração, tudo é HTTP. O capítulo 9 explica a instalação do par ([Instalar o par](#cap-09-instalar)).

### O que existe em cada imagem {#cap-26-imagens}

| Imagem | Rotas | Diferença |
|---|---|---|
| release | 62 | Todas |
| alpha | 56 | Sem as 5 rotas do painel e sem `POST /api/tls` |
| Air | 56 | As mesmas da alpha, e só enquanto o Air está acordado em M0 ([capítulo 19](#cap-19)) |

Uma rota que não existe na imagem responde como qualquer caminho desconhecido ([Convenções](#cap-26-convencoes)).

### Um pedido por vez {#cap-26-um-por-vez}

O aparelho é um microcontrolador e atende um pedido de cada vez. Pedidos em paralelo ao mesmo aparelho esperam na fila e podem estourar o tempo do cliente. Mande um pedido, espere a resposta e só então mande o seguinte. As conexões persistentes (*keep-alive*) vêm ligadas de fábrica ([capítulo 9](#cap-09-servidor-web)) e evitam refazer a conexão a cada pedido, o que pesa principalmente em HTTPS.

Use um tempo de espera generoso no cliente: 10 s para leituras simples, 30 s para exportações, listagens e histórico, e 60 s para envio de arquivo e atualização de firmware.

### Formatos do corpo {#cap-26-corpo}

| Forma | Rotas |
|---|---|
| Formulário (`application/x-www-form-urlencoded`) | `login`, `login_chpass`, `force_chpass`, `commit_all`, `action`, `delete`, `mkdir`, `history_rebind`, `save_sys`, `touch` |
| JSON cru no corpo, com `Content-Type: application/json` | `calib`, `set_time` |
| Texto PEM cru, com um `Content-Type` que não seja de formulário | `tls` |
| `multipart/form-data` | `upload`, `restore` |
| Sem corpo | `clear_logs`, `reset_touch_cal`, `ota/apply` |

Uma rota que espera JSON cru não enxerga um corpo enviado como formulário, e responde como se o corpo estivesse vazio.

## Autenticação {#cap-26-autenticacao}

### A entrada, passo a passo {#cap-26-entrada}

Entrar leva dois pedidos: um para pegar um código de uso único, outro para enviar a conta, a senha e o código.

1. Peça o código com `GET /api/login_init`.
2. Calcule o SHA-256 da senha, em hexadecimal minúsculo ([A senha em SHA-256](#cap-26-hash)).
3. Envie `POST /api/login` com os campos de formulário `user`, `pass` e `nonce`.
4. Guarde o token de sessão que volta no cabeçalho `Set-Cookie`.
5. Mande o token em todos os pedidos seguintes, como cookie ou como `Authorization: Bearer`.

```bash
H=http://192.0.2.10
HASH=$(printf '%s' "$SIMUT_WEB_PASS" | sha256sum | cut -d' ' -f1)
NONCE=$(curl -s "$H/api/login_init" | sed 's/.*"nonce":"\([^"]*\)".*/\1/')
curl -s -c jar -X POST "$H/api/login" \
  --data-urlencode "user=$SIMUT_WEB_USER" \
  --data-urlencode "pass=$HASH" \
  --data-urlencode "nonce=$NONCE"
```

```json
{"ok":true,"redirect":"/"}
```

Depois disso, `curl -b jar ...` leva o cookie em cada pedido. Mantenha a senha numa variável de ambiente, nunca no script.

::: {.figura #fig-26-entrada tipo="diagrama" arquivo="26-entrada.png" captura="diagrama de sequência entre um cliente e o aparelho: GET /api/login_init devolve nonce; o cliente calcula SHA-256 da senha; POST /api/login com user, pass e nonce; o aparelho confere e devolve Set-Cookie SIMUTSESS e o corpo {ok, redirect}; pedidos seguintes levam o cookie ou Authorization: Bearer; GET /logout encerra"}
A entrada em dois pedidos. O código de uso único vale 60 s e serve para uma única tentativa.
:::

**`GET /api/login_init`** responde:

```json
{"nonce":"9f2c4e1a7b3d5f60a1c2e3f4a5b6c7d8","locked":false,"lockSec":0}
```

| Campo | Significado |
|---|---|
| `nonce` | Código de uso único, 32 algarismos hexadecimais |
| `locked` | `true` se o seu endereço está bloqueado por senha errada |
| `lockSec` | Segundos que faltam para o bloqueio acabar |

Regras do código:

- ele pertence ao endereço IP de quem pediu, não a um cookie;
- vale 60 s;
- serve para uma única tentativa, certa ou errada: peça outro código antes de cada tentativa;
- um segundo `login_init` do mesmo endereço gera um código novo, e o anterior continua valendo até vencer. Um terceiro pedido descarta o primeiro.

Sem um `login_init` antes, o `POST /api/login` é recusado.

**`POST /api/login`** responde:

| Status | Corpo | Quando |
|---|---|---|
| 200 | `{"ok":true,"redirect":"/"}` | Entrada aceita |
| 200 | `{"ok":true,"redirect":"/force_chpass"}` | Entrada aceita, mas a conta precisa trocar a senha ([Troca de senha](#cap-26-troca)) |
| 400 | `{"ok":false,"err":1}` | Falta o campo `user` ou `pass` |
| 401 | `{"ok":false,"err":1}` | Código ausente, errado, já usado ou vencido; ou nome e senha fora do tamanho |
| 401 | `{"ok":false,"err":2,"lockSec":2}` | Conta ou senha errada. `lockSec` é a espera antes da próxima tentativa |
| 403 | `{"ok":false,"err":2,"lockSec":37}` | O endereço está bloqueado. A senha nem foi conferida |
| 403 | `{"ok":false,"err":3}` | As três vagas de sessão estão ocupadas por outras contas |

O destino vem no corpo, em `redirect`. A resposta não tem cabeçalho `Location`: um cliente que procura o destino ali nunca vê a troca de senha obrigatória.

Conferir a senha leva cerca de 0,4 s no aparelho, porque o resumo guardado é recalculado com 5000 rodadas ([capítulo 29](#cap-29-senhas)).

### A senha em SHA-256 {#cap-26-hash}

O campo `pass` do `POST /api/login` não é a senha: é o SHA-256 dela, em 64 algarismos hexadecimais minúsculos. É o que a página de entrada calcula no navegador, e vale igual em HTTP e em HTTPS.

O resumo é calculado sobre os bytes da senha em Latin-1 (ISO-8859-1), um byte por caractere. Para senhas só com caracteres ASCII, isso é o mesmo que calcular sobre o UTF-8, e `printf '%s' | sha256sum` dá o valor certo. Para uma senha com acentos, codifique em Latin-1 antes de calcular. Caracteres fora do Latin-1, como emojis, não funcionam nem na página de entrada: para contas usadas por programas, prefira senhas ASCII.

::: perigo
**Em HTTP, o resumo vale tanto quanto a senha.** O SHA-256 esconde o texto da senha de quem escuta a rede, o que protege outros sistemas onde a mesma senha seja usada. Mas quem captura o resumo entra no aparelho com ele: basta pedir um código próprio e reenviar o resumo. Em rede que você não controla, use HTTPS ([capítulo 9](#cap-09-https)).
:::

### Cookie ou Bearer {#cap-26-token}

A sessão é um token de 32 algarismos hexadecimais. Ele chega no cabeçalho da resposta de entrada:

```http
Set-Cookie: SIMUTSESS=5d0c9e7a41b2f3e8c6a9d0b1e2f3a4b5; Path=/; HttpOnly; SameSite=Strict
```

Em HTTPS, o cookie ganha também o atributo `Secure`. O cookie não tem prazo: ele vale enquanto a sessão existir no aparelho.

O mesmo token funciona de dois jeitos:

| Forma | Cabeçalho do pedido | Para quem |
|---|---|---|
| Cookie | `Cookie: SIMUTSESS=<token>` | Navegador, `curl -b`, bibliotecas com pote de cookies |
| Bearer | `Authorization: Bearer <token>` | Clientes sem pote de cookies e o gestor de frota no navegador ([capítulo 27](#cap-27)) |

Os dois são a mesma sessão, com as mesmas vagas e o mesmo prazo.

::: atencao
**Com um cabeçalho `Cookie`, o `Authorization` é ignorado.** Se o pedido traz qualquer cabeçalho `Cookie`, mesmo de outro site, o aparelho procura a sessão só nele. Um cliente que usa Bearer não deve mandar cookie nenhum.
:::

Um cliente fora do navegador lê o token no próprio `Set-Cookie`:

```bash
TOKEN=$(curl -s -D - -o /dev/null -X POST "$H/api/login" \
  --data-urlencode "user=$SIMUT_WEB_USER" \
  --data-urlencode "pass=$HASH" \
  --data-urlencode "nonce=$NONCE" \
  | sed -n 's/^[Ss]et-[Cc]ookie: SIMUTSESS=\([0-9a-f]*\).*/\1/p')
curl -s -H "Authorization: Bearer $TOKEN" "$H/api/status"
```

Uma página de outra origem não consegue ler o `Set-Cookie`. Para ela, o aparelho põe o token no corpo da resposta, mas só quando o pedido vem da origem autorizada no CORS ([CORS](#cap-26-cors)):

```json
{"ok":true,"redirect":"/","token":"5d0c9e7a41b2f3e8c6a9d0b1e2f3a4b5"}
```

### Sessões {#cap-26-sessoes}

| Regra | Valor |
|---|---|
| Sessões ao mesmo tempo | 3, divididas entre navegadores, scripts e o gestor de frota |
| Sessões por conta | 1. Entrar de novo com a mesma conta substitui a sessão anterior, e o token antigo para de valer |
| Ociosidade | 15 min sem nenhum pedido autenticado encerram a sessão |
| O que renova | Qualquer pedido autenticado, em qualquer rota |
| Reinício | Encerra todas as sessões |
| Vagas cheias | Uma quarta conta recebe `403` `{"ok":false,"err":3}`. Ninguém é despejado |

Duas consequências para quem escreve um cliente:

- **Não abra uma sessão por pedido.** Entre uma vez, guarde o token e só entre de novo quando a sessão morrer ([401 e 403](#cap-26-401-403)).
- **Não compartilhe uma conta entre dois processos.** Cada entrada derruba a sessão do outro. Dê uma conta a cada processo, lembrando o teto de três sessões.

Uma conta sem nenhuma permissão consegue entrar, mas o aparelho a trata como sem sessão em todas as rotas.

### Sair {#cap-26-sair}

`GET /logout` encerra a sessão cujo token vem no pedido, e só ela.

| Pedido | Resposta |
|---|---|
| Com `Authorization: Bearer` e sem cookie | `204`, sem corpo |
| Com cookie | `302` para `/login`, com um `Set-Cookie` que apaga o cookie |

A rota não exige autenticação: apresentar o token já é a prova, e a única coisa que ele permite é encerrar a própria sessão. Saia ao terminar, para devolver a vaga sem esperar os 15 min.

```bash
curl -s -o /dev/null -w '%{http_code}\n' -H "Authorization: Bearer $TOKEN" "$H/logout"
```

```text
204
```

### Troca de senha {#cap-26-troca}

As contas de fábrica, uma conta nova, uma conta resetada e o administrador depois de um reset pelo console precisam trocar a senha antes de configurar ([capítulo 13](#cap-13-troca-obrigatoria)). O cliente fica sabendo de três jeitos:

- a entrada responde `"redirect":"/force_chpass"`;
- `GET /api/perms` traz `"mc":1`;
- `POST /api/commit_all`, `POST /api/action?op=reboot` e `POST /api/clear_logs` respondem `409`:

```json
{"error":"Password change required","next":"/api/force_chpass"}
```

As leituras continuam funcionando enquanto a troca está pendente. As páginas da interface web levam para `/force_chpass`.

Há duas rotas de troca:

| Rota | Sessão | Campos |
|---|---|---|
| `POST /api/force_chpass` | Exige a sessão da conta, com a troca pendente | `p1` e `p2`, a senha nova duas vezes |
| `POST /api/login_chpass` | Não exige sessão; usa o código de `login_init` | `user`, `oldpass`, `newpass`, `nonce` |

O formato das senhas nessas duas rotas depende do protocolo, ao contrário da entrada:

| Protocolo | O que enviar | Regra da senha nova |
|---|---|---|
| HTTP | O SHA-256 hexadecimal de cada senha, 64 caracteres | O aparelho não consegue conferir a força; a página confere antes de enviar ([capítulo 8](#cap-08-senhas)) |
| HTTPS | A senha em texto, sem resumo | Pelo menos 8 caracteres, com letra e algarismo. O aparelho confere e calcula o resumo |

Respostas de `POST /api/force_chpass`:

| Status | Corpo | Quando |
|---|---|---|
| 200 | `{"status":"ok"}` | Senha trocada. A sessão continua |
| 400 | `{"error":"Invalid payload"}` | HTTP: `p1` sem 64 caracteres, ou diferente de `p2` |
| 400 | `{"error":"Weak password: min 8 chars with a letter and a digit"}` | HTTPS: senha fraca, ou `p1` diferente de `p2` |
| 403 | `Forbidden` (texto) | Sem sessão, ou a conta não tem troca pendente |
| 503 | `{"error":"Display in use. Retry shortly."}` | Alguém tocou no painel há menos de 5 s |

Respostas de `POST /api/login_chpass`:

| Status | Corpo | Quando |
|---|---|---|
| 200 | `{"ok":true}` | Senha trocada. Nenhuma sessão é aberta: entre de novo |
| 400 | `{"ok":false,"err":1}` | Falta um campo |
| 400 | `{"ok":false,"err":5}` | A senha nova é igual à atual |
| 401 | `{"ok":false,"err":1}` | Código inválido, ou senha nova fora da regra |
| 401 | `{"ok":false,"err":2,"lockSec":2}` | Senha atual errada, com bloqueio |
| 403 | `{"ok":false,"err":2,"lockSec":37}` | O endereço está bloqueado |

Trocar a senha pela web não reinicia o aparelho.

### Bloqueio por tentativas {#cap-26-bloqueio}

Cada senha errada bloqueia o endereço IP de origem por um tempo que dobra a cada erro: 2 s, 4 s, 8 s e assim por diante, até 300 s a partir do nono erro. A contagem só volta a zero com uma entrada, ou uma troca de senha, bem-sucedida a partir daquele endereço ([capítulo 13](#cap-13-bloqueio)).

- O bloqueio vale para `POST /api/login`, `POST /api/login_chpass` e a conta e senha do HTTP Basic de `/metrics` ([capítulo 24](#cap-24)).
- Um código vencido também conta como erro.
- O aparelho guarda o estado de até 8 endereços. Se os 8 estiverem bloqueados, um endereço novo recebe, já no `login_init`:

```http
HTTP/1.1 429 Too Many Requests
Retry-After: 42

{"ok":false,"err":3,"retryAfter":42}
```

Consulte `locked` e `lockSec` no `login_init` antes de tentar de novo. Tentar durante o bloqueio não adianta: o aparelho responde sem conferir a senha.

::: atencao
**Clientes atrás do mesmo endereço dividem o bloqueio.** O código de uso único e a contagem de erros pertencem ao IP de origem. Um gestor de frota e os navegadores dos operadores atrás do mesmo NAT se bloqueiam uns aos outros quando alguém erra a senha.
:::

Quem tem a permissão **Usuários** [PERM_USER_MGR]{.perm} vê a tabela de bloqueios em `GET /api/sec_status`:

```json
{"slots":[{"ip":"192.0.2.55","fails":3,"lockSec":5,"ageSec":3}]}
```

| Campo | Significado |
|---|---|
| `ip` | Endereço de origem |
| `fails` | Erros seguidos. Para de subir em 12 |
| `lockSec` | Segundos de bloqueio que faltam; 0 = livre |
| `ageSec` | Segundos desde o último pedido de entrada desse endereço |

## CORS {#cap-26-cors}

Uma página hospedada em outro servidor, como o gestor de frota ([capítulo 27](#cap-27)), só fala com o aparelho se ele autorizar a origem dela. A origem se configura pelo console serial ou pela chave `cors` da seção `sys` do `commit_all`, e vale depois de um reinício ([capítulo 9](#cap-09-cors)).

Com uma origem configurada:

- toda resposta, inclusive as de erro, leva `Access-Control-Allow-Origin: <a origem>`;
- o pedido de verificação do navegador (`OPTIONS`, a *preflight*) recebe `204`, sem pedir autenticação, com:

```http
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Authorization, Content-Type
Access-Control-Max-Age: 600
```

- a resposta de entrada traz o token no corpo quando o cabeçalho `Origin` do pedido é exatamente a origem configurada.

Sem origem configurada, um `OPTIONS` recebe `405` com o texto `CORS disabled`. Isso separa "este aparelho não foi liberado para o gestor" de "caminho errado".

O aparelho não envia `Access-Control-Allow-Credentials`. Por isso a página de outra origem não usa cookie: ela guarda o token e manda `Authorization: Bearer` em cada pedido.

```js
const base = 'http://192.0.2.10';
async function status(token) {
  const r = await fetch(base + '/api/status', {
    headers: { 'Authorization': 'Bearer ' + token }
  });
  if (r.status === 401) throw new Error('sessão encerrada: entre de novo');
  return r.json();
}
```

O aparelho também não envia `Access-Control-Expose-Headers`. Uma página de outra origem lê o status e o corpo, mas não lê cabeçalhos como `Retry-After` ou os `X-Backup-*` do backup.

## Respostas e erros {#cap-26-respostas}

### Códigos HTTP {#cap-26-codigos}

| Status | Significado | O que fazer |
|---|---|---|
| 200 | Feito. Em `commit_all`, leia `rejected`, `reboot` e `applied` no corpo | — |
| 202 | Aceito e em andamento (busca de sensores, atualização de firmware) | Acompanhe pela rota indicada |
| 204 | Nada a devolver (bloco aberto vazio, saída com Bearer, *preflight*) | — |
| 302 | Página que exige sessão, pedida sem sessão; ou saída com cookie | Siga só se for um navegador |
| 400 | Pedido malformado ou parâmetro fora da regra. Nada foi aplicado | Corrija o pedido |
| 401 | Sem sessão viva ([401 e 403](#cap-26-401-403)) | Entre de novo, uma vez, e repita |
| 403 | Sessão viva sem a permissão; ou, em várias rotas, sem sessão | Veja [401 e 403](#cap-26-401-403) |
| 404 | Caminho inexistente, arquivo inexistente, ou sensor não encontrado | — |
| 405 | `OPTIONS` com o CORS desligado | Configure a origem ([capítulo 9](#cap-09-cors)) |
| 409 | Estado errado: troca de senha pendente, captura de tela em andamento, atualização sem imagem preparada, ou alteração que exige reinício num `_nosave` | Leia o `error` do corpo |
| 413 | Corpo acima do teto: 8.192 bytes em `calib` e `tls` | Divida o pedido |
| 416 | Pedaço de captura fora de 0 a 14 | — |
| 422 | Arquivo recusado na validação: backup, imagem de firmware, sonda com endereço inválido | Leia `st` ou `v` no corpo |
| 429 | Limite de ritmo ou bloqueio de entrada | Espere o que o corpo ou o `Retry-After` indicar |
| 500 | Falha interna: gravação na flash, sem painel, resposta longa demais | Tente de novo; se repetir, veja o log de eventos |
| 501 | Recurso fora desta imagem (aceitar sonda DS18B20 sem o driver) | — |
| 503 | Ocupado agora ([Ocupado](#cap-26-ocupado)) | Tente de novo em alguns segundos |

### 401 e 403: nem toda rota distingue {#cap-26-401-403}

Parte das rotas distingue "sem sessão" (`401`) de "sessão sem a permissão" (`403`). As demais respondem `403` nos dois casos.

| Respondem 401 sem sessão | Respondem 403 também sem sessão |
|---|---|
| `/api/status`, `/api/perms`, `/api/config`, `/api/network`, `/api/wifi/scan`, `/api/users`, `/api/themes`, `/api/alarms`, `/api/sec_status`, `/api/action`, `/api/commit_all`, `/api/set_time`, `/api/screen_stream`, `/api/touch`, `/api/keypad`, `/metrics` | Todas as outras rotas protegidas: histórico, log de eventos, arquivos, calibração, sensores, backup, restauração, atualização, TLS, captura de tela, `save_sys`, `reset_touch_cal`, `history_rebind`, `clear_logs`, `force_chpass` |

Receita para um cliente:

1. Recebeu `401`: entre de novo uma vez e repita o pedido.
2. Recebeu `403`: pergunte `GET /api/perms`.
3. Se `/api/perms` responde `401`, a sessão morreu: entre de novo e repita.
4. Se `/api/perms` responde `200`, a conta não tem a permissão. Não insista: entrar de novo não muda nada.

As páginas HTML não usam nenhum dos dois: sem sessão, elas respondem `302` para `/login`; sem permissão, `403` com uma página curta.

### O corpo de erro {#cap-26-corpo-erro}

A maioria das rotas responde erro em JSON, com um campo `error`:

```json
{"error":"Forbidden"}
```

Não conte com isso sempre. Confira o `Content-Type` antes de interpretar:

| Formato | Onde aparece |
|---|---|
| `{"error":"..."}` | A maioria das rotas `/api/...` |
| `{"ok":false,"err":N}` | Entrada e troca de senha |
| Texto puro, como `Forbidden`, `Too Fast`, `System Busy`, `File Not Found.` | Arquivos, log de eventos, backup, restauração, capturas e `/metrics` |
| Corpo vazio | `GET /api/history_days` sem a permissão |

Os códigos `err` da entrada:

| `err` | Significado |
|---|---|
| 1 | Pedido inválido: campo ausente, código errado ou vencido, tamanho fora da regra |
| 2 | Senha errada ou endereço bloqueado. Vem com `lockSec` |
| 3 | Sem vaga: sessões cheias, ou os 8 bloqueios ocupados (com `retryAfter`) |
| 5 | Troca de senha: a nova é igual à atual |

### Limites de ritmo {#cap-26-ritmo}

Quatro rotas exigem um intervalo mínimo entre pedidos:

| Rota | Intervalo | Resposta quando rápido demais |
|---|---|---|
| `GET /api/logs` | 200 ms | `429` `Too Fast` (texto) |
| `GET /api/ls` | 200 ms | `429` `{"error":"Too Fast"}` |
| `POST /api/calib` | 5 s | `429` `{"error":"rate limited"}`, com `Retry-After: 5` |
| `POST /api/tls` | 5 s | `429` `{"error":"rate limited"}`, com `Retry-After: 5` |

O intervalo conta por endereço IP de origem, e as quatro rotas dividem o mesmo relógio: um `GET /api/ls` seguido, menos de 5 s depois, de um `POST /api/calib` recebe `429` no segundo pedido. Um pedido recusado não reinicia a contagem. A permissão é conferida antes do ritmo.

### Ocupado: 503 {#cap-26-ocupado}

| Corpo | Causa | Espera sugerida |
|---|---|---|
| `{"error":"Display in use. Retry shortly."}` | Alguém tocou no painel, ou mandou `POST /api/touch`, há menos de 5 s. Vale para `commit_all`, `op=reboot`, a troca de senha obrigatória, apagar arquivo, criar pasta, restaurar, atualizar, limpar o log, capturas de tela, gráficos e exportações do histórico e a leitura do log de eventos | O `Retry-After` do cabeçalho: 5 s nas gravações, 3 s nas leituras |
| `System Busy` ou `{"error":"System Busy"}` | Outra tarefa pesada ocupa a flash | Alguns segundos |
| `{"error":"Already processing"}` | Outro pedido de histórico está em andamento | Espere o outro terminar |
| `{"scanning":false,"error":"busy"}` | O rádio está ocupado ([Busca de redes](#cap-26-wifi)) | Alguns segundos |
| `{"error":"NTP not synced"}` | A calibração exige o relógio acertado | Acerte o relógio ([capítulo 10](#cap-10)) |
| `{"error":"no memory"}` | Sem memória para a cópia do ensaio | Tente de novo |

### Convenções {#cap-26-convencoes}

- **Horários** são segundos desde 1970 em UTC (*epoch*), como `1790164800`. A exceção é `uptime`, em milissegundos.
- **Tamanhos** são em bytes.
- **Valores lógicos** chegam como `true` e `false` na maioria das respostas, e como `1` e `0` em alguns campos, como `ntp`, `tel` e `cap`; confira a tabela de cada rota. Nos pedidos, o aparelho aceita `true`, `false`, `1` e `0`, com ou sem aspas.
- **O tipo do corpo decide como ele é lido.** Com `Content-Type: application/x-www-form-urlencoded`, que é o padrão do `curl -d`, o aparelho lê campos de formulário. Com qualquer outro tipo, ele guarda o corpo cru e não lê campos. Por isso `set_time` e `calib` precisam de `-H 'Content-Type: application/json'`, e `commit_all` precisa ir como formulário.
- **Texto com acento** vai em UTF-8 direto no JSON. O aparelho não decodifica a forma `\uXXXX`: um nome enviado como `"Cozinha \u00e7"` é gravado com a barra e os seis caracteres. Em Python, use `json.dumps(dados, ensure_ascii=False)`.
- **Campo desconhecido** num pedido é ignorado em silêncio. Confira o efeito lendo o estado de volta.
- **Caminho inexistente** responde `404` com o texto `404: Not Found`. Se o cabeçalho `Host` do pedido não é o IP do aparelho nem termina em `.local`, a resposta é um `302` para `http://<ip>/network`, o comportamento de portal do ponto de acesso. Uma rota com o método errado, como `GET /api/commit_all`, conta como caminho inexistente.

## As 62 rotas {#cap-26-rotas}

As tabelas usam o nome do bit de cada permissão. O nome que a página **Usuários** mostra e o que cada bit libera estão no [capítulo 8](#cap-08-permissoes). "Administrador completo" é a conta com todas as permissões, `0xFFFF` ([capítulo 8](#cap-08-admin)); nenhuma combinação das 13 permissões substitui esse nível.

### Páginas {#cap-26-rotas-paginas}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/login` | Pública | Página de entrada |
| GET | `/` | [PERM_DASHBOARD]{.perm} | Página **Painel de Controle** |
| GET | `/history` | [PERM_HISTORY]{.perm} ou [PERM_LOGS]{.perm} | Página **Histórico e Logs** |
| GET | `/config` | [PERM_SYS_CONFIG]{.perm} | Página **Configurações** |
| GET | `/alarms` | [PERM_SYS_CONFIG]{.perm} | Página **Alarmes e Sons** |
| GET | `/telemetry` | [PERM_SYS_CONFIG]{.perm} | Página **Telemetria** |
| GET | `/network` | [PERM_NET_CONFIG]{.perm} | Página **Rede** |
| GET | `/users` | [PERM_USER_MGR]{.perm} | Página **Usuários** |
| GET | `/files` | [PERM_FILE_READ]{.perm} | Página **Arquivos** |
| GET | `/license` | [PERM_DASHBOARD]{.perm} | Página **Licença** |
| GET | `/force_chpass` | Sessão com troca de senha pendente | Página de troca de senha obrigatória |

### Arquivos estáticos {#cap-26-rotas-estaticos}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/lang.js` | Pública | Script de tradução das páginas |
| GET | `/style.css` | Pública | Folha de estilo |
| GET | `/favicon.ico` | Pública | Ícone do site |
| GET | `/apple-touch-icon.png` | Pública | Responde `204`, sem conteúdo |
| GET | `/api/lang` | Pública | Dicionário de textos da interface web do pacote de idioma ativo; `{}` sem pacote |

### Sessão e senha {#cap-26-rotas-sessao}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/api/login_init` | Pública | Entrega o código de uso único e o estado do bloqueio |
| POST | `/api/login` | Pública, com bloqueio | Confere conta e senha e abre a sessão |
| POST | `/api/login_chpass` | Pública, com bloqueio | Troca a senha sem sessão, com a senha atual |
| POST | `/api/force_chpass` | Sessão com troca pendente | Conclui a troca de senha obrigatória |
| GET | `/logout` | Pública | Encerra a sessão do token apresentado |
| GET | `/api/perms` | Qualquer sessão | Conta, permissões, versão e idioma da sessão |
| GET | `/api/sec_status` | [PERM_USER_MGR]{.perm} | Tabela de bloqueios de entrada |

### Estado e configuração {#cap-26-rotas-config}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/api/status` | [PERM_DASHBOARD]{.perm} | Identidade, saúde e leituras atuais |
| GET | `/api/themes` | [PERM_DASHBOARD]{.perm} | Temas do painel disponíveis |
| GET | `/metrics` | [PERM_DASHBOARD]{.perm}, por sessão ou HTTP Basic | Métricas no formato do Prometheus ([capítulo 24](#cap-24)) |
| GET | `/api/config` | [PERM_SYS_CONFIG]{.perm} | Configuração geral, telemetria, linha de alarmes, syslog e política de PIN |
| GET | `/api/alarms` | [PERM_SYS_CONFIG]{.perm} | Limites, alarmes ligados e janelas de manutenção por slot, e os sons |
| GET | `/api/sensors` | [PERM_SYS_CONFIG]{.perm} | Tipos de sensor e o mapa completo dos 16 slots |
| GET | `/api/network` | [PERM_NET_CONFIG]{.perm} | Estado e configuração de rede |
| GET | `/api/wifi/scan` | [PERM_NET_CONFIG]{.perm} | Busca de redes Wi-Fi |
| GET | `/api/users` | [PERM_USER_MGR]{.perm} | Contas, permissões e se cada uma tem PIN |
| GET | `/api/calib` | [PERM_CALIB]{.perm} | Curvas de calibração dos sensores |
| POST | `/api/commit_all` | [PERM_SYS_CONFIG]{.perm}, [PERM_NET_CONFIG]{.perm} ou [PERM_USER_MGR]{.perm}, e a de cada seção | Grava configuração, alarmes, rede e contas |
| POST | `/api/calib` | [PERM_CALIB]{.perm} | Grava curvas de calibração ([capítulo 6](#cap-06)) |
| POST | `/api/action` | [PERM_SYS_CONFIG]{.perm} | Reinício, telemetria e sondas, pelo parâmetro `op` |
| POST | `/api/set_time` | [PERM_SYS_CONFIG]{.perm} | Acerta o relógio na hora |
| POST | `/api/save_sys` | [PERM_SYS_CONFIG]{.perm} | Troca o tema do painel na hora |
| POST | `/api/reset_touch_cal` | [PERM_SYS_CONFIG]{.perm} | Apaga a calibração do toque e abre o assistente no painel |

### Histórico e log de eventos {#cap-26-rotas-historico}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/api/history_days` | [PERM_HISTORY]{.perm} | Lista os dias com histórico |
| GET | `/api/history_multi` | [PERM_HISTORY]{.perm} | Séries de vários slots em JSON, para gráficos |
| GET | `/api/history/open` | [PERM_HISTORY]{.perm} | O bloco aberto, ainda não selado |
| GET | `/api/export/history.bin` | [PERM_HISTORY]{.perm} | Exporta até 31 dias num pacote `.simx` |
| POST | `/api/history_rebind` | [PERM_SYS_CONFIG]{.perm} | Adapta o dia corrente ao esquema atual e reinicia |
| GET | `/api/logs` | [PERM_LOGS]{.perm} | O log de eventos inteiro, em registros binários |
| GET | `/api/logcodes` | [PERM_LOGS]{.perm} | Os nomes dos eventos, em texto: no idioma do pacote e em inglês |
| GET | `/api/export/logs.bin` | [PERM_LOGS]{.perm} | Exporta até 31 dias do log num pacote `.simx` |
| POST | `/api/clear_logs` | [PERM_LOGS]{.perm} e [PERM_SYS_CONFIG]{.perm} | Apaga o log de eventos |

### Arquivos {#cap-26-rotas-arquivos}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/api/ls` | [PERM_FILE_READ]{.perm} | Lista uma pasta |
| GET | `/download` | [PERM_FILE_READ]{.perm}; mais [PERM_HISTORY]{.perm} em `/history` e [PERM_LOGS]{.perm} em `.blog` | Baixa um arquivo |
| POST | `/api/upload` | [PERM_FILE_UPLOAD]{.perm} | Envia um arquivo |
| POST | `/api/mkdir` | [PERM_FILE_UPLOAD]{.perm} | Cria uma pasta |
| POST | `/api/delete` | [PERM_FILE_DELETE]{.perm} | Apaga um arquivo |

### Backup, atualização e certificado {#cap-26-rotas-ota}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/api/backup` | Administrador completo | Baixa o backup `.bkp` do sistema de arquivos inteiro |
| POST | `/api/restore?op=validate` | [PERM_FILE_READ]{.perm} | Confere um `.bkp` sem gravar |
| POST | `/api/restore?op=apply` | Administrador completo | Restaura um `.bkp` e reinicia |
| POST | `/api/restore?op=stage` | Administrador completo | Recebe e valida uma imagem de firmware |
| POST | `/api/ota/apply` | Administrador completo | Aplica a imagem preparada e reinicia |
| POST | `/api/tls` | Administrador completo | [release]{.img} Instala o par de certificados HTTPS ([capítulo 9](#cap-09-instalar)) |

As três operações de `/api/restore` são uma única rota com o parâmetro `op`, e contam como uma rota no total de 61.

### Painel {#cap-26-rotas-painel}

[release]{.img}

| Método | Rota | Permissão | O que faz |
|---|---|---|---|
| GET | `/api/screenshot` | [PERM_SYS_CONFIG]{.perm} | Captura da tela do painel em BMP |
| GET | `/api/screenshot_chunk` | [PERM_SYS_CONFIG]{.perm} | Um pedaço da captura, com CRC |
| GET | `/api/screen_stream` | [PERM_SYS_CONFIG]{.perm} | Um quadro do painel no formato compacto do espelho |
| POST | `/api/touch` | [PERM_SYS_CONFIG]{.perm} | Um toque no painel, ou um toque longo com `ms` |
| GET | `/api/keypad` | [PERM_SYS_CONFIG]{.perm} | As faces do teclado de PIN na tela |

São 62 rotas: 52 exigem uma permissão ou uma sessão, e 10 são públicas por projeto. O repositório confere essa lista a cada mudança, e nenhuma rota fica sem portão ([capítulo 29](#cap-29-abuso)).

## Receitas {#cap-26-receitas}

Os exemplos usam `curl`, as variáveis de [A entrada, passo a passo](#cap-26-entrada) e o pote de cookies `jar`. Os valores nas respostas são ilustrativos; os nomes e a forma dos campos são os do aparelho.

### Estado do aparelho {#cap-26-status}

`GET /api/status` é a rota de monitoramento. Ela exige só a permissão **Painel** [PERM_DASHBOARD]{.perm} e responde em três blocos: `sys`, `metr` e `sensors`.

```bash
curl -s -b jar "$H/api/status"
```

```json
{"sys":{"name":"simut","ver":"2.7.1","env":"release","uid":"E6614C311B7A2F2D",
 "mac":"28:cd:c1:0a:1b:2c","cfg":"3F2A91C4","uptime":29260512,"rssi":-58,
 "ip":"192.0.2.10","theme":0,"heap_f":61240,"heap_t":230400,"heap_lb":40960,
 "fs_u":413696,"fs_t":1048576,"time":1790164800,"ntp":1,"pending":0,"tel":1,
 "hi":1,"cap":1},
 "metr":{"lb":40960,"lbm":33792,"hm":52112,"wf":0,"mq":0,"rmn":-63,"rmx":-55,
 "ts":135,"tf":0,"tr":0,"tb":412345,"tl":180,"so":24000,"se":2,"cs":3,
 "fo":210,"fom":38,"fot":2100,"f50":0,"fx":0,"ad":0,"c1a":12,"c1n":1,
 "c1kl":0,"c1kh":0,"c1kq":0,"c1s":0,"cgd":0,"cgg":0,"cgx":0},
 "sensors":[
  {"slot":0,"gpio":2,"id":"FRIDGE","name":"Geladeira","type":"DS18B20","ch":1,
   "pc":1,"pr":"1-Wire","val":4.25},
  {"slot":1,"gpio":4,"id":"AMB","name":"Sala","type":"BME280","ch":3,"pc":2,
   "pr":"SDA,SCL","val":23.10,"hum":55.2,"press":1012.8}]}
```

O bloco `sys`:

| Campo | Significado |
|---|---|
| `name` | **Nome** do aparelho |
| `ver` | Versão do firmware |
| `env` | Imagem: `release`, `alpha` ou `air` |
| `uid` | Número de série da placa, 16 algarismos hexadecimais. Use como chave do aparelho ([capítulo 20](#cap-20-identidade)) |
| `mac` | Endereço MAC do Wi-Fi |
| `cfg` | CRC-32 da configuração em uso, o mesmo valor do cabeçalho `X-SIMUT-Cfg` da telemetria |
| `uptime` | Milissegundos desde o boot. Volta a zero a cada 49,7 dias sem reinício |
| `rssi` | Sinal Wi-Fi agora, em dBm; −100 sem conexão ou com leitura impossível |
| `ip` | Endereço IP |
| `theme` | Índice do tema do painel ([Temas](#cap-26-temas)) |
| `heap_f`, `heap_t` | Memória livre e total, em bytes |
| `heap_lb` | Maior bloco de memória livre, em bytes |
| `fs_u`, `fs_t` | Sistema de arquivos usado e total, em bytes. Atualiza a cada 10 s |
| `time` | Relógio do aparelho, em *epoch* UTC |
| `ntp` | `1` se o relógio foi acertado pela rede |
| `pending` | Estimativa de registros de telemetria ainda não entregues. Para em 65535; `-1` sem telemetria |
| `tel` | `1` se a telemetria está ligada (lote mínimo acima de 0) |
| `hi` | Intervalo do histórico, em minutos |
| `cap` | `1` se a imagem tem painel |

O bloco `metr` traz contadores de diagnóstico, zerados a cada boot:

| Campo | Significado |
|---|---|
| `lb`, `lbm` | Maior bloco livre agora e o menor já visto, em bytes |
| `hm` | Menor memória livre já vista, em bytes |
| `wf`, `mq` | Reconexões do Wi-Fi e do MQTT |
| `rmn`, `rmx` | Menor e maior sinal Wi-Fi vistos, em dBm; 0 = ainda sem amostra |
| `ts`, `tf`, `tr` | Envios de telemetria aceitos pelo coletor, envios que falharam e novas tentativas |
| `tb`, `tl` | Bytes dos envios aceitos, e a latência do último, em ms |
| `so`, `se` | Leituras de sensor boas e com erro |
| `cs` | Gravações da configuração |
| `fo`, `fom`, `fot`, `f50` | Gravações na flash: quantidade, a mais longa em ms, o total em ms e quantas passaram de 50 ms |
| `fx` | Gravações na flash feitas sem pausar o núcleo do painel. Qualquer valor acima de 0 é um defeito a relatar |
| `ad` | Respostas longas interrompidas e fechadas à força |
| `c1a` | Idade, em ms, do último sinal de vida do núcleo que desenha o painel. Dezenas de ms é normal; segundos indicam o núcleo parado |
| `c1n`, `c1kl`, `c1kh`, `c1kq`, `c1s` | Partidas desse núcleo e interrupções por três causas diferentes; `c1s` conta travamentos na pausa |
| `cgd`, `cgg`, `cgx` | Respostas cortadas por prazo, por proteção e por desconexão do cliente |

O bloco `sensors` lista só os slots ativos:

| Campo | Significado |
|---|---|
| `slot` | Número do slot, 0 a 15 |
| `gpio` | Primeiro GPIO do sensor |
| `id` | Identificador de hardware (`hwId`), que vira a chave da telemetria |
| `name` | Nome do sensor |
| `type` | `DS18B20`, `DHT22`, `BME280` ou `BMP280` |
| `ch`, `pc`, `pr` | Número de grandezas, número de pinos e o papel de cada pino |
| `val` | Temperatura média, com 2 casas. `"Error"` com o sensor em falha; `"--"` antes da primeira leitura |
| `hum`, `press` | Umidade (%) e pressão (hPa), só nos tipos que medem e com leitura válida |

::: nota
**Para monitorar um Air sem prendê-lo acordado.** [air]{.img} Todo pedido autenticado adia a hibernação do Air. Acrescente `?quiet=1` ao `GET /api/status` para ler sem adiar: `"$H/api/status?quiet=1"`. A sessão é renovada do mesmo jeito ([capítulo 19](#cap-19)).
:::

### Quem sou eu {#cap-26-perms}

`GET /api/perms` responde para qualquer sessão, com qualquer permissão. Use-a para conferir se a sessão está viva e o que a conta pode fazer.

```json
{"user":"servidor","perms":7433,"ntp":1,"time":1790164800,"version":"2.7.1",
 "env":"release","mc":0,"langCode":"pt-BR","langName":"Portugues"}
```

| Campo | Significado |
|---|---|
| `user` | Conta da sessão |
| `perms` | Máscara de permissões, em decimal ([capítulo 8](#cap-08-permissoes)) |
| `ntp`, `time` | Relógio acertado e a hora atual |
| `version`, `env` | Versão e imagem do firmware |
| `mc` | `1` se a conta precisa trocar a senha |
| `langCode`, `langName` | Pacote de idioma ativo; vazios sem pacote |

### Ler a configuração {#cap-26-config}

`GET /api/config` exige **Sistema** [PERM_SYS_CONFIG]{.perm} e devolve um único objeto JSON. As chaves são as mesmas que a seção `sys` do `commit_all` aceita, com algumas só de leitura. O significado, a faixa e o valor de fábrica de cada campo estão nos capítulos de cada página:

| Chaves | Assunto | Onde está cada campo |
|---|---|---|
| `name`, `tz`, `log`, `res`, `s_int`, `h_int`, `ntp_enabled` | Identidade, hora e amostragem | [Capítulo 5](#cap-05-referencia) |
| `pin_min`, `pin_max`, `pin_kb`, `pin_alpha` | [release]{.img} Política de PIN do painel. `pin_max` é só de leitura | [Capítulo 8](#cap-08-politica) |
| `t_transport`, `t_sec`, `t_srv`, `t_port`, `t_path`, `t_key`, `t_int`, `t_bat`, `t_mode`, `t_glob`, `t_line`, `t_sep` | Telemetria | [Capítulo 21](#cap-21-pagina) |
| `m_topic`, `m_cid`, `m_user`, `m_qos`, `m_retain`, `m_ka` | MQTT | [Capítulo 21](#cap-21-campos-mqtt) |
| `m_had` | Home Assistant Discovery | [Capítulo 23](#cap-23) |
| `a_en`, `a_mode`, `a_qmax`, `a_path`, `a_glob`, `a_line`, `a_sep` | Linha de alarmes | [Capítulo 22](#cap-22-campos) |
| `slog_en`, `slog_srv`, `slog_port`, `slog_lvl` | Syslog | [Capítulo 5](#cap-05-syslog) e [capítulo 25](#cap-25) |

Chaves só de leitura:

| Chave | Significado |
|---|---|
| `t_cert` | `true` se o arquivo `/cert.pem` da telemetria foi carregado no boot ([capítulo 21](#cap-21-certpem)) |
| `now_epoch` | Hora do aparelho |
| `a_pending` | Registros na fila da linha de alarmes agora |
| `serial` | Número de série da placa, o mesmo `uid` do `/api/status` |
| `sensors` | Os 16 slots, em ordem: `hwid`, `active` e se o tipo mede umidade (`hum`) e pressão (`press`) |

Dois segredos não voltam inteiros:

- `t_key`, a chave de acesso da telemetria, volta mascarada: os 4 primeiros caracteres e `***`, ou só `***` para chaves de até 4 caracteres. Ao gravar, um valor com `***` é ignorado e a chave atual fica.
- A senha do MQTT e a senha do Wi-Fi não aparecem em nenhuma rota. Na gravação, `m_pass` vazio mantém a senha atual.

::: atencao
**Não copie a leitura inteira de volta para a gravação.** Algumas chaves só existem na leitura e são ignoradas na gravação, e os segredos voltam mascarados. Mande no `commit_all` só os campos que você quer mudar.
:::

A configuração de rede está em `GET /api/network`, com a permissão **Rede** [PERM_NET_CONFIG]{.perm}:

```json
{"connected":true,"ip":"192.0.2.10","mask":"255.255.255.0","gw":"192.0.2.1",
 "dns":"192.0.2.1","mac":"28:cd:c1:0a:1b:2c","ssid":"MinhaRede","use_dhcp":true,
 "static_ip":"192.168.1.100","static_mask":"255.255.255.0",
 "static_gw":"192.168.1.1","static_dns":"8.8.8.8","dns_auto":true,"dns2":"",
 "ntp_server":"","ntp_enabled":true,"web_port":80,"web_ka":true,"web_tls":false}
```

`ip`, `mask`, `gw` e `dns` são os valores em uso agora; os `static_*` são os configurados para quando o DHCP está desligado. `web_tls` diz se o par de certificados está instalado, não se o servidor subiu em HTTPS: um par instalado só vale depois do reinício. Os campos e as chaves de gravação estão no [capítulo 9](#cap-09-referencia).

### Gravar com commit_all {#cap-26-commit}

`POST /api/commit_all` grava quase toda a configuração. É a rota dos três botões da interface web ([capítulo 5](#cap-05-gravacao)). O pedido tem um único campo de formulário, `_payload`, com um JSON de uma ou mais seções, e até 6.144 bytes:

```bash
curl -s -b jar -X POST "$H/api/commit_all" \
  --data-urlencode '_payload={"alarms":{"sensors":[{"idx":0,"temp":[2.0,8.0]}]}}'
```

```json
{"status":"ok","reboot":false,"applied":["alarms"]}
```

Use `--data-urlencode`: o JSON tem caracteres que um formulário precisa codificar.

**As seções.** Cada seção exige a sua permissão, e a gravação é tudo ou nada: com uma seção proibida, nada é gravado.

| Seção | Permissão | Conteúdo | Chaves |
|---|---|---|---|
| `sys` | [PERM_SYS_CONFIG]{.perm} | Identidade, hora, amostragem, política de PIN, telemetria, MQTT, linha de alarmes, syslog e a origem `cors` | [Capítulo 5](#cap-05-referencia), [21](#cap-21), [22](#cap-22-campos), [9](#cap-09-cors) |
| `net` | [PERM_NET_CONFIG]{.perm} | Wi-Fi, IP, DNS, NTP e o servidor web | [Capítulo 9](#cap-09-referencia) |
| `alarms` | [PERM_SYS_CONFIG]{.perm} | Limites, alarmes ligados, janelas de manutenção e sons | [Alarmes pela API](#cap-26-alarmes) |
| `users` | [PERM_USER_MGR]{.perm} | Contas e PIN | [Contas pela API](#cap-26-contas) |
| `slots` | [PERM_SYS_CONFIG]{.perm} | Tipo, pinos, identificação, nome, limites e alarme de cada slot | [Slots pela API](#cap-26-slots) |
| `calib` | [PERM_SYS_CONFIG]{.perm} | Calibração, como a página a envia | [Capítulo 6](#cap-06) |

Para entrar na rota, a conta precisa de pelo menos uma entre **Sistema**, **Rede** e **Usuários**. Uma conta só com **Usuários**, por exemplo, grava a seção `users` e nada mais.

::: nota
**Como o aparelho acha as seções.** Ele procura cada nome de seção entre aspas, como `"net"`, em qualquer ponto do corpo. Um texto igual ao nome de uma seção, como um sensor chamado `"alarms"`, é lido como a seção e pode receber um `403` com o nome dela. Evite valores de texto iguais a `sys`, `net`, `alarms`, `users`, `slots` e `calib`.
:::

**Os modos.** Um parâmetro extra, ao lado de `_payload`, muda o que a gravação faz:

| Parâmetro | O que faz | Botão da página |
|---|---|---|
| Nenhum | Grava e aplica. Reinicia só se algum grupo alterado exigir | **Aplicar agora** |
| `_dry=1` | Ensaio: roda todas as verificações numa cópia e responde o que mudaria. Não grava nada | O ensaio automático da página |
| `_nosave=1` | Aplica na memória, sem gravar. Um reinício desfaz. Recusa com `409` se a mudança exigir reinício | **Testar** |
| `_reboot=1` | Grava e reinicia sempre, mesmo sem necessidade | **Salvar e reiniciar** |

`_dry` e `_nosave` só aceitam as seções `sys`, `net` e `alarms`. Com qualquer outra, a resposta é `400` `{"error":"accepts sys, net and alarms only"}`. [air]{.img} O Air recusa os dois com `400` `{"error":"dry run not available on this build"}`: no Air, toda gravação é real e reinicia ([capítulo 5](#cap-05-air)).

**A resposta.** Ela diz o que aconteceu. Leia sempre o corpo: um campo recusado volta com status 200.

```json
{"status":"ok","reboot":false,"applied":["alarms","maint"]}
{"status":"ok","reboot":true,"reboot_for":["identity"]}
{"status":"ok","reboot":true,"reboot_for":["net"],"newPort":8080}
{"status":"ok","reboot":true,"reboot_for":["requested"]}
{"status":"ok","reboot":false,"applied":[],"rejected":["t_port"]}
{"status":"ok","reboot":false,"saved":false,"applied":["alarms"]}
{"status":"dry","reboot":true,"applied":["time"],"reboot_for":["time"]}
{"status":"ok","reboot":true,"reboot_for":["users"],"creds":[{"u":"operador1","p":"K7M2QX9A"}]}
```

| Campo | Significado |
|---|---|
| `status` | `ok` numa gravação ou num `_nosave`; `dry` num ensaio |
| `reboot` | `true` se o aparelho vai reiniciar, ou reiniciaria no ensaio |
| `applied` | Os grupos alterados que se aplicaram sem reinício. Só aparece quando não há reinício, e no ensaio |
| `reboot_for` | Os grupos que obrigam o reinício. Com `_reboot=1` sobre uma mudança que não exigia, vem `["requested"]` |
| `rejected` | Campos recusados, que ficaram com o valor anterior ([Recusas](#cap-26-recusas)) |
| `saved` | Só com `_nosave=1`, e sempre `false` |
| `newPort` | A porta web nova, quando ela mudou. Reconecte nela depois do reinício |
| `creds` | As senhas de uso único das contas criadas ou resetadas. Só nesta resposta |

**O que reinicia.** O aparelho decide comparando a configuração de antes com a de depois, campo por campo. Um campo reenviado com o valor atual não conta como mudança.

| Grupos | Efeito |
|---|---|
| `alarms`, `maint`, `alarm_tel`, `telemetry`, `display` | Aplicam sem reiniciar |
| `net`, `identity`, `users`, `slots`, `sensing`, `mqtt`, `time`, `logging`, `display_pin`, `web`, `unclassified` | Reiniciam |

O que cada grupo inclui está em [Os grupos de configuração](#cap-05-grupos). Qualquer alteração de conta ou de PIN, e também da política de PIN, cai em `users` e reinicia. Não copie essa tabela para o seu cliente: use o ensaio, que responde pela regra do próprio aparelho.

Quando há reinício, a resposta chega antes dele. O aparelho fica fora do ar por alguns segundos e encerra todas as sessões. Para saber quando ele voltou, repita `GET /api/login_init` a cada 3 s até ele responder, e entre de novo.

**O ensaio.** Use `_dry=1` para validar uma configuração antes de gravá-la, por exemplo antes de mandá-la a vários aparelhos ([capítulo 27](#cap-27)). O ensaio tem três limites:

- **Não enxerga alguns campos.** `h_int`, `ntp_enabled`, `slog_en`, `slog_srv`, `slog_port`, `slog_lvl`, `m_had`, `dns_auto`, `dns2` e `web_ka` ficam de fora da cópia que ele compara. Um ensaio só com esses campos responde `"reboot":false` e `"applied":[]`, mas a gravação real reinicia pelo grupo `web`. O `_nosave=1` também não os aplica.
- **Aplica o fuso horário de verdade.** Um `tz` no ensaio muda na hora a hora local que o aparelho mostra, até o próximo reinício ou a próxima gravação de fuso.
- **Aplica os sons de verdade.** Um objeto `sounds` no ensaio muda na hora volume, melodias e silêncio do aparelho, sem gravar.

::: atencao
**A origem CORS é gravada na hora, mas só vale depois de um reinício.** A chave `cors` grava o arquivo da origem em qualquer modo, menos no ensaio, e não altera nenhum grupo: a resposta não pede reinício. Para ativá-la, mande a mesma gravação com `_reboot=1` ou reinicie depois com `POST /api/action?op=reboot`.
:::

**Erros que recusam a gravação inteira.** Nesses casos, nada muda:

| Status | Corpo | Causa |
|---|---|---|
| 400 | `{"error":"Missing _payload"}` | Falta o campo `_payload` |
| 400 | `{"error":"Bad payload"}` | `_payload` vazio ou acima de 6.144 bytes |
| 400 | `{"error":"No section"}` | Nenhuma seção reconhecida no corpo |
| 400 | `{"error":"Alarm limit outside channel range"}` | Um limite fora da faixa possível do canal ([capítulo 7](#cap-07-limites)) |
| 400 | O slot e o GPIO, como `slot 4: GP2 already used by slot 2` | Conflito de GPIO na seção `slots` ([capítulo 6](#cap-06-erros-gravar)) |
| 400 | `{"error":"accepts sys, net and alarms only"}` | `_dry` ou `_nosave` com outra seção |
| 403 | `{"error":"Forbidden"}` | A conta não tem nenhuma das três permissões de entrada |
| 403 | `{"error":"Forbidden","section":"users"}` | A conta não tem a permissão da seção indicada |
| 409 | `{"error":"Password change required","next":"/api/force_chpass"}` | Troca de senha pendente |
| 409 | `{"error":"needs a restart; cannot be applied without saving","reboot_for":["time"]}` | `_nosave=1` com uma mudança que exige reinício |
| 500 | `{"error":"save failed"}` | A gravação na flash falhou. Tente de novo |
| 503 | `{"error":"Display in use. Retry shortly."}` | Alguém tocou no painel há menos de 5 s |

### Recusas campo a campo {#cap-26-recusas}

Um valor fora da faixa, ou em formato errado, é descartado sozinho: o campo mantém o valor anterior, o resto da gravação segue e o nome do campo aparece em `rejected`. Os nomes seguem a seção:

| Seção | Forma do nome | Exemplos |
|---|---|---|
| `sys` | O nome da chave | `t_port`, `m_qos`, `h_int`, `cors`, `pin_min` |
| `net` | `net.` e a chave | `net.ssid`, `net.dns2`, `net.web_port`, `net.ntp_server` |
| `alarms` | `maint` | Janela negativa, ou acima de 30 dias |
| `users` | `users.` e o motivo | `users.perms`, `users.id`, `users.pin` ([Contas pela API](#cap-26-contas)) |

A lista em `rejected` tem espaço limitado. Numa gravação com muitos campos errados, alguns nomes podem faltar: confira o resultado lendo o estado de volta.

### Contas pela API {#cap-26-contas}

A seção `users` recebe uma lista de ações, executadas na ordem em que chegam, até 16 por pedido. Ela exige **Usuários** [PERM_USER_MGR]{.perm}. As regras de cada campo e o que a página mostra estão no [capítulo 8](#cap-08-pagina).

| Ação | Forma |
|---|---|
| Criar | `{"type":"add","name":"operador1","perms":6144,"pin":"482913"}`. `pin` é opcional |
| Apagar | `{"type":"del","id":3}` |
| Nova senha | `{"type":"reset","id":3}` |
| Definir ou apagar o PIN | `{"type":"pin","id":3,"pin":"482913"}`. `""` apaga o PIN |

**Listar.** Leia as contas e os `id` antes de apagar ou resetar:

```bash
curl -s -b jar "$H/api/users"
```

```json
[{"id":0,"name":"admin","perms":65535,"pin":true},
 {"id":1,"name":"servidor","perms":7433,"pin":false},
 {"id":3,"name":"operador1","perms":6144,"pin":true}]
```

`id` é a posição da conta na tabela, de 0 a 31, e pode ter buracos. `pin` diz só se a conta tem PIN, nunca qual. [alpha]{.img} [air]{.img} Sem painel, a chave `pin` não aparece.

**Criar.**

```bash
curl -s -b jar -X POST "$H/api/commit_all" --data-urlencode \
  '_payload={"users":{"actions":[{"type":"add","name":"operador1","perms":6144,"pin":"482913"}]}}'
```

```json
{"status":"ok","reboot":true,"reboot_for":["users"],"creds":[{"u":"operador1","p":"K7M2QX9A"}]}
```

A senha de uso único vem em `creds`, só nesta resposta: o aparelho guarda apenas o resumo. Guarde-a ou entregue-a na hora. Ela tem 8 caracteres, entre letras maiúsculas e algarismos, sem `O`, `0`, `I` e `1`. Na primeira entrada, a conta nova precisa trocar a senha ([Troca de senha](#cap-26-troca)).

**Apagar e resetar** são sempre por `id`. Uma ação com `name` no lugar do `id` não apaga nada e volta com `"rejected":["users.id"]`. A conta `admin`, `id` 0, não pode ser apagada nem resetada pela API.

**Definir o PIN.** O PIN pela API segue sempre a regra fixa de 4 a 8 algarismos, qualquer que seja a política de PIN do aparelho. Esse é um defeito conhecido, e o comportamento é este:

- um PIN com letras, ou com mais de 8 caracteres, é recusado mesmo que a política o aceite no painel;
- um PIN mais curto que o mínimo da política é aceito pela API, mas o painel o recusa na hora de entrar.

Para um PIN que funcione nos dois lados, use só algarismos, com um comprimento entre o maior de 4 e `pin_min` e o menor de 8 e `pin_max`, lidos em `GET /api/config`. Se a política exige mais de 8 caracteres, defina o PIN no próprio painel ([capítulo 8](#cap-08-proprio-pin)).

**Tudo em `users` reinicia.** Criar, apagar, resetar e definir PIN mudam o grupo `users`, e o aparelho reinicia depois de responder. Agrupe as ações num único pedido: três criações em pedidos separados custam três reinícios. Depois de cada gravação de contas, entre de novo.

**Recusas.** Uma ação recusada não impede as outras. A resposta traz o motivo em `rejected`, com status 200:

| Motivo | Causa |
|---|---|
| `users.name` | Nome vazio, com mais de 15 bytes, com aspas, barra invertida ou caractere de controle, ou igual a `admin` |
| `users.dup` | Já existe uma conta com esse nome, sem diferença entre maiúsculas e minúsculas |
| `users.perms` | `perms` fora de 0 a 8191, ou com uma permissão que a conta que pede não tem. A conta não é criada |
| `users.full` | As 32 posições estão ocupadas |
| `users.pin` | PIN fora da regra de 4 a 8 algarismos, ou já usado por outra conta. Numa criação, a conta entra sem PIN. [alpha]{.img} [air]{.img} Todo PIN é recusado |
| `users.id` | `id` ausente ou de conta inexistente; o `admin` numa exclusão ou reset; ou o PIN do `admin` pedido por quem não é o `admin` nem administrador completo |
| `users.type` | Ação desconhecida |

Uma resposta de recusa, sem nenhuma outra mudança:

```json
{"status":"ok","reboot":false,"applied":[],"rejected":["users.perms"]}
```

::: atencao
**Ninguém concede o que não tem.** O valor de `perms` pedido precisa estar contido nas permissões da conta que faz o pedido. Uma conta de serviço que cria contas de painel precisa portar também as permissões de painel que vai distribuir, mesmo sem nunca usá-las ([capítulo 8](#cap-08-subconjunto), [capítulo 27](#cap-27-servico)). O nível de administrador completo nunca se concede pela API: o teto é 8191, as 13 permissões.
:::

**Mudar as permissões** de uma conta existente não tem ação própria. Apague e crie de novo, no mesmo pedido, com a exclusão antes da criação ([capítulo 8](#cap-08-mudar)).

### Alarmes pela API {#cap-26-alarmes}

A seção `alarms` exige **Sistema** [PERM_SYS_CONFIG]{.perm}, e tudo nela se aplica sem reiniciar. Cada item de `sensors` altera um slot:

```json
{"alarms":{"sensors":[
  {"idx":0,"active":true,"temp":[2.0,8.0],"hum":[20,80]},
  {"idx":3,"maint":7200}
]}}
```

| Campo | Efeito |
|---|---|
| `idx` | Slot, de 0 a 15. Obrigatório. Um slot inativo é ignorado em silêncio |
| `active` | Liga ou desliga os alarmes de limite do slot (bloquear) |
| `temp`, `hum`, `press`, `lux` | Limites `[mínimo, máximo]` do canal. Mande sempre os dois números: `null` vira 0 |
| `tmin`, `tmax`, `hmin`, `hmax` | Nomes antigos dos limites de temperatura e umidade. Se os dois estilos vierem, vale o de canal |
| `maint` | Janela de manutenção em segundos a partir de agora. `0` encerra |

Campo ausente mantém o valor atual. Uma banda invertida, com mínimo igual ou acima do máximo, é corrigida sozinha: o máximo passa a ser o mínimo mais 0,1. Um limite fora da faixa possível do canal recusa a gravação inteira com `400` ([capítulo 7](#cap-07-limites)).

**Janela de manutenção.** O valor é uma duração, e não uma hora, para não depender de o relógio do servidor e o do aparelho concordarem.

```bash
# slot 3 em manutenção por 2 h
curl -s -b jar -X POST "$H/api/commit_all" \
  --data-urlencode '_payload={"alarms":{"sensors":[{"idx":3,"maint":7200}]}}'
# encerrar agora
curl -s -b jar -X POST "$H/api/commit_all" \
  --data-urlencode '_payload={"alarms":{"sensors":[{"idx":3,"maint":0}]}}'
```

```json
{"status":"ok","reboot":false,"applied":["maint"]}
```

O teto é 30 dias, 2.592.000 s. Um pedido acima disso é cortado para 30 dias e o campo aparece em `rejected`. Um valor negativo é recusado e nada muda. A janela fica gravada, sobrevive a reinícios e se encerra sozinha no prazo. O que ela faz com os alarmes e com a linha de alarmes está no [capítulo 7](#cap-07-manutencao) e no [capítulo 22](#cap-22-manutencao).

**Sons.** O objeto `sounds`, dentro da seção `alarms`, altera só as chaves que vierem: `touch`, `confirm`, `error`, `alarm`, `web`, `attention` e `mute` (ligado ou desligado), `volume` e `alarmVolume` (0 a 100) e `melTouch`, `melConfirm`, `melError`, `melAlarm` e `melAttention` (melodia 0 a 5) ([capítulo 7](#cap-07-sons-web)).

**Ler o estado.** `GET /api/alarms` lista só os slots ativos, em ordem de slot:

```json
{"sensors":[
  {"idx":0,"name":"Geladeira","type":"DS18B20","gpio":2,"has_hum":false,
   "tmin":2.0,"tmax":8.0,"hmin":20.0,"hmax":80.0,"active":true,
   "lim":{"temp":[2.0,8.0]},"maint":0},
  {"idx":3,"name":"Câmara","type":"DHT22","gpio":5,"has_hum":true,
   "tmin":-20.0,"tmax":-10.0,"hmin":20.0,"hmax":80.0,"active":true,
   "lim":{"temp":[-20.0,-10.0],"hum":[20.0,80.0]},"maint":7140}],
 "sounds":{"touch":true,"confirm":true,"error":true,"alarm":true,"web":false,
  "attention":true,"mute":false,"volume":70,"alarmVolume":100,"melTouch":0,
  "melConfirm":1,"melError":2,"melAlarm":3,"melAttention":4}}
```

`lim` traz os limites de todo canal que o sensor mede. `maint` é quanto falta da janela, em segundos; 0 fora dela. `active` é o alarme de limite ligado, não "o sensor existe": um sensor que funciona com o alarme bloqueado vem com `active:false`.

Silenciar um alarme que está tocando não tem rota: é uma ação local, no painel ([capítulo 7](#cap-07-silenciar)). Pela API, use `active:false` para bloquear ou `maint` para uma pausa com prazo.

### Slots pela API {#cap-26-slots}

A seção `slots` altera a configuração dos sensores. Mudar `a`, `t`, `p`, `hwId` ou `name` reinicia o aparelho (grupo `slots`); `lim` e `al` sozinhos se aplicam sem reiniciar (grupo `alarms`). Mande só os slots e só os campos alterados: campo ausente mantém o valor.

```json
{"slots":{"s":[{"i":0,"name":"Câmara fria 1"}]}}
```

| Campo | Significado |
|---|---|
| `i` | Slot, de 0 a 15. Obrigatório |
| `a` | Ativo |
| `t` | Tipo: `1` DS18B20, `2` DHT22, `3` BME280, `5` BMP280 |
| `p` | Até 4 GPIOs, como `[2,255,255,255]` (`255` = sem pino) |
| `hwId` | Identificador de hardware, que vira a chave da telemetria |
| `name` | Nome mostrado no painel e na web |
| `lim` | Limites por canal, como `{"temp":[2,8],"hum":[20,80]}` |
| `al` | Alarme de limite ligado |

`tmin`, `tmax`, `hmin` e `hmax` não valem nesta seção e são ignorados em silêncio. Use `lim` ou a seção `alarms`. Mudar `hwId` muda as chaves da telemetria daquele sensor; para mudar só o que as pessoas veem, mude `name` ([capítulo 6](#cap-06-editar)).

O mapa completo dos slots, com os tipos que a imagem aceita e os papéis de cada pino, está em `GET /api/sensors` ([capítulo 6](#cap-06-slots)).

### Ações pontuais {#cap-26-acoes}

`POST /api/action` reúne operações que não são configuração. O parâmetro `op` escolhe a operação:

| `op` | Efeito | Resposta |
|---|---|---|
| `reboot` | Reinicia o aparelho | `200` `{"ok":true}` e reinicia; `409` com troca de senha pendente; `503` com o painel em uso |
| `tel_sync` | Dispara um envio de telemetria agora ([capítulo 21](#cap-21-acoes)) | `200` `{"ok":true}` |
| `tel_reset` | Volta o cursor da telemetria para reenviar até 30 dias ([capítulo 21](#cap-21-acoes)) | `200` `{"ok":true}` |
| `sensor_scan` | Começa a busca de sondas nos GPIOs ([capítulo 6](#cap-06-procurar)) | `202` `{"started":true}`; `200` `{"busy":true}` se já está buscando |
| `scan_results` | Resultado da busca | `{"scanning":true}` enquanto busca; depois `{"scanning":false,"found":[{"pin":2,"type":1,"rom":"28FF641E8016034A"}]}` |
| `sensor_wipe` | Com `slot=N`: grava a hora atual como início do histórico do slot, sem apagar o slot nem os arquivos | `200` `{"ok":true}` |
| `sensor_accept` | Com `slot=N`: adota a sonda DS18B20 encontrada no GPIO de mesmo número | `200` `{"ok":true,"epoch_moved":false}`; `404` `nosensor`; `422` `badrom` |

Um `op` desconhecido responde `400` `{"error":"op"}`, e um `slot` ausente ou fora de 0 a 15, `400` `{"error":"slot"}`.

```bash
curl -s -b jar -X POST "$H/api/action?op=reboot"
```

Não existe uma rota `/api/reboot`. Para reiniciar, use `op=reboot`, ou `_reboot=1` junto de uma gravação. O `commit_all` sempre exige uma seção: para reiniciar por ele sem mudar nada, mande uma seção vazia que a conta possa gravar, como `_payload={"sys":{}}` com `_reboot=1`. A resposta é `{"status":"ok","reboot":true,"reboot_for":["requested"]}`.

Outras três rotas agem na hora, sem reiniciar:

| Rota | Corpo | Resposta |
|---|---|---|
| `POST /api/set_time` | JSON `{"epoch":1790164800}`, com `Content-Type: application/json` | `{"ok":true,"now":1790164800}`. Recusa valores até `1600000000` (13/09/2020) com `400` `{"error":"epoch too low"}`. Com o NTP ligado, o próximo acerto sobrescreve ([capítulo 10](#cap-10)) |
| `POST /api/save_sys` | Formulário `theme=<índice>` | `{"status":"ok"}`; `400` com índice fora da lista |
| `POST /api/reset_touch_cal` | Nenhum | `{"status":"ok","wizard":true}`. O painel abre o assistente de calibração do toque |

`POST /api/history_rebind` adapta o dia corrente do histórico ao esquema atual dos sensores, mantendo os registros, e reinicia. Com `force=1`, recria o esquema e descarta o dia. Use-a só quando o log de eventos indicar esquema de histórico diferente ([capítulo 15](#cap-15)).

### Temas {#cap-26-temas}

`GET /api/themes` lista os temas do painel na ordem do índice:

```json
[{"id":0,"name":"Simut Default"},{"id":1,"name":"Janeiro Branco"},{"id":2,"name":"Fevereiro Roxo"}]
```

O índice é o que `theme` do `/api/status` mostra e o que `POST /api/save_sys` recebe ([capítulo 13](#cap-13-tema-painel)).

### Histórico {#cap-26-historico}

O histórico fica em arquivos por dia, `/history/AAAAMMDD.h5`, no formato binário do aparelho ([capítulo 15](#cap-15)). Cada arquivo só tem os blocos já selados; as medições mais recentes ficam num bloco aberto, na memória, até o selo. Quem reconcilia histórico precisa ler os dois.

**Listar os dias.** `GET /api/history_days` devolve os nomes dos arquivos sem a extensão, sem ordem garantida:

```json
["20260921","20260922","20260923"]
```

**Baixar um dia.** Use `/download` com o caminho do arquivo. Exige **Leitura** [PERM_FILE_READ]{.perm} e **Histórico** [PERM_HISTORY]{.perm}:

```bash
curl -s -b jar -o 20260922.h5 "$H/download?file=/history/20260922.h5"
```

**Ler o bloco aberto.** `GET /api/history/open` devolve o bloco ainda não selado, no mesmo formato, como `open.h5`. Sem nada aberto, a resposta é `204`, sem corpo:

```bash
curl -s -b jar -o open.h5 -w '%{http_code}\n' "$H/api/history/open"
```

**Séries para gráfico.** `GET /api/history_multi` devolve JSON pronto para desenhar:

| Parâmetro | Significado |
|---|---|
| `sensors` | Slots separados por vírgula, como `0,3`. Sem ele, o primeiro slot ativo |
| `range` | Período para trás a partir de `end`: `0` 1 h, `1` 6 h, `2` 24 h (padrão), `3` 7 dias, `4` 30 dias, `5` 1 ano, `6` tudo |
| `end` | Fim do período, em *epoch*. Padrão: agora |
| `from`, `to` | Janela explícita, em *epoch*. Quando os dois vêm, valem no lugar de `range` e `end` |
| `probe` | `1` responde só a estimativa de tamanho, sem dados |

```json
{"cutoff":1790078400,"end":1790164800,"now":1790164800,"rangeUsed":2,
 "sensors":[{"id":0,"hwId":"FRIDGE","name":"Geladeira","type":"DS18B20",
  "hasH":false,"hasP":false}],
 "data":[{"t":1790078460,"v":{"tFRIDGE":4.31}},{"t":1790078520,"v":{"tFRIDGE":4.28}}],
 "minT":3.95,"maxT":4.62,"tsMinT":1790101200,"tsMaxT":1790089200}
```

Cada ponto traz `t`, o horário, e `v`, com uma chave por grandeza: a letra do canal (`t` temperatura, `u` umidade, `p` pressão, `l` luz) seguida do `hwId`. O aparelho reduz os pontos para cerca de 600 por resposta. Um pedido grande demais para uma resposta só não é atendido: volta a estimativa, com `"sliceRequired":1`, e o cliente deve pedir o período em fatias com `from` e `to`. Um pedido de histórico por vez: um segundo em paralelo recebe `503` `{"error":"Already processing"}`.

**Exportar.** `GET /api/export/history.bin?from=<epoch>&to=<epoch>` devolve um pacote `.simx` com os registros do período, até 31 dias. A página **Histórico e Logs** não usa esta rota: ela baixa os arquivos `.h5` do dia por `/download` e monta o CSV no navegador. Fora da regra, a resposta é `400` com `Missing from/to params`, `Invalid range` ou `Range exceeds 31 days`.

### Log de eventos {#cap-26-logs}

`GET /api/logs` devolve o log de eventos inteiro, do mais antigo ao mais novo, como uma sequência de registros binários de 12 bytes. Exige **Logs** [PERM_LOGS]{.perm} e respeita o intervalo de 200 ms.

```bash
curl -s -b jar -o eventos.bin "$H/api/logs"
```

Cada registro, em *little-endian*:

| Bytes | Tipo | Campo |
|---|---|---|
| 0 a 3 | inteiro de 32 bits sem sinal | Horário, em *epoch* |
| 4 e 5 | inteiro de 16 bits sem sinal | Tempo desde o boot, em segundos: os 16 bits baixos |
| 6 e 7 | inteiro de 16 bits sem sinal | Código do evento ([apêndice B](#ap-b)) |
| 8 e 9 | inteiro de 16 bits com sinal | Contexto do evento |
| 10 | byte | Nível nos bits 7 a 5 (0 depuração, 1 informação, 2 aviso, 3 erro, 4 fatal), núcleo no bit 4 e subsistema nos bits 3 a 0 |
| 11 | byte | Tempo desde o boot: os 8 bits altos |

Os subsistemas, de 0 a 12: `APP`, `NET`, `TEL`, `STO`, `WEB`, `CFG`, `CLI`, `SENSOR`, `HIST`, `SYS`, `DSP`, `SEC`, `OTA`; 15 é desconhecido. O registro binário não traz o texto do evento: a página o busca pelo código em `GET /api/logcodes`, descrito abaixo.

```python
import struct
TAGS = ["APP","NET","TEL","STO","WEB","CFG","CLI","SENSOR","HIST","SYS","DSP","SEC","OTA"]
with open("eventos.bin", "rb") as f:
    dados = f.read()
for i in range(0, len(dados) - 11, 12):
    ts, up_lo, code, ctx, flags, up_hi = struct.unpack_from("<IHHhBB", dados, i)
    nivel, tag = flags >> 5, flags & 0x0F
    print(ts, code, ctx, nivel, TAGS[tag] if tag < len(TAGS) else "?", (up_hi << 16) | up_lo)
```

`GET /api/logcodes` devolve os nomes dos eventos em texto puro, uma linha `<código> <nome>` por evento. Exige **Logs** [PERM_LOGS]{.perm}. Com um pacote de idioma instalado, vêm primeiro os nomes do pacote e depois os nomes em inglês de todos os códigos: fique com o primeiro nome de cada código, e um evento que o pacote ainda não conhece sai em inglês. Com `?l=en`, ou sem pacote, só inglês.

```bash
curl -s -b jar "$H/api/logcodes" | head -3
```

`GET /api/export/logs.bin?from=<epoch>&to=<epoch>&level=<all|inf|err>` exporta um período de até 31 dias num pacote `.simx`, filtrado por nível. `POST /api/clear_logs` apaga o log e exige **Logs** e **Sistema** ao mesmo tempo; a própria limpeza fica registrada.

### Arquivos {#cap-26-arquivos}

**Listar.** `GET /api/ls?dir=/lang`:

```json
{"path":"/lang","entries":[{"n":"README.txt","t":"f","s":212,"p":1},
 {"n":"language_pt-BR.lng","t":"f","s":61234}]}
```

| Campo | Significado |
|---|---|
| `n` | Nome |
| `t` | `f` arquivo, `d` pasta |
| `s` | Tamanho em bytes; 0 nas pastas |
| `p` | `1` num arquivo protegido contra exclusão |

Uma listagem que passa do tempo termina com `"truncated":true`. `dir` com `..` ou `%` recebe `400`, e `/config` recebe `403`.

**Baixar.** `GET /download?file=/pasta/arquivo`. Um arquivo em `/history` exige também **Histórico**, e um arquivo `.blog` exige também **Logs**. Arquivo inexistente: `404`.

**Enviar.** `POST /api/upload`, em `multipart/form-data`. A pasta de destino vai no campo `uploadDir`, ou no endereço como `?uploadDir=/lang`:

```bash
curl -s -b jar -F uploadDir=/lang -F file=@language_pt-BR.lng "$H/api/upload"
```

```json
{"status":"ok"}
```

::: atencao
**A ordem dos campos importa.** O aparelho lê a pasta de destino no começo do arquivo. Um `uploadDir` enviado depois do arquivo não é visto, e o arquivo vai para a raiz. Ponha `-F uploadDir=...` antes de `-F file=@...`, ou use o parâmetro no endereço.
:::

O nome do arquivo tem até 64 caracteres, sem `..`, sem `\ " : < > | ? * %` e sem caracteres de controle. Um destino dentro de `/config` é recusado. Um envio recusado, por nome inválido, destino inválido ou falta de espaço, responde `400` `{"error":"Invalid upload"}`. Um pacote de idioma novo só vale depois de um reinício ([capítulo 13](#cap-13-idioma)).

**Criar pasta.** `POST /api/mkdir` com `dir=/dados`. No máximo dois níveis, como `/a/b`. O aparelho põe na pasta nova um `README.txt`, que a mantém visível na listagem.

**Apagar.** `POST /api/delete` com `file=/pasta/arquivo`. O parâmetro é `file`, não `path`.

| Resposta | Quando |
|---|---|
| `200` `{"status":"ok"}` | Apagado |
| `403` `{"error":"Forbidden"}` | Caminho com `..` ou `%`, ou dentro de `/config` |
| `403` `{"error":"Protected file — the firmware rewrites it on boot"}` | Arquivo protegido |
| `404` `{"error":"Not found"}` | O arquivo não existe |

**Caminhos protegidos.**

| Caminho | Regra |
|---|---|
| `/config` e tudo dentro | Não se lista, não se baixa, não se envia e não se apaga pela rede. Ali ficam a configuração, as contas, o par de certificados e a origem CORS |
| `/README.txt`, `/themes/README.txt`, `/web/README.txt`, `/lang/README.txt` | Não se apagam. O aparelho os reescreve no boot |

### Backup e restauração {#cap-26-backup}

`GET /api/backup` baixa o sistema de arquivos inteiro num arquivo `.bkp`, inclusive `/config`, com as contas e os segredos. Só o administrador completo pode ([capítulo 17](#cap-17)).

```bash
curl -s -b jar -OJ "$H/api/backup"
```

O arquivo se chama `backup_<id do chip>_<epoch>.bkp`. A resposta traz cabeçalhos para conferir a integridade antes de usar o arquivo:

| Cabeçalho | Significado |
|---|---|
| `X-Backup-Files` | Número de arquivos |
| `X-Backup-Schema` | Versão do formato |
| `X-Backup-PSize` | Tamanho da carga, em bytes |
| `X-Backup-PCrc` | CRC-32 da carga, em decimal |

No arquivo, os 4 primeiros bytes são `BKP1`, o tamanho da carga fica no byte 24 e o CRC no byte 28, os dois em inteiros de 32 bits *little-endian*. A interface web compara esses dois valores com os cabeçalhos e descarta um backup que não bate.

A restauração tem dois passos, os dois em `multipart/form-data`:

```bash
# 1. conferir, sem gravar (permissão Leitura)
curl -s -b jar -F bkp=@backup.bkp "$H/api/restore?op=validate"
# 2. restaurar (administrador completo); o aparelho reinicia
curl -s -b jar -F bkp=@backup.bkp "$H/api/restore?op=apply"
```

```json
{"st":0,"chip":"e6614c311b7a2f2d","fwv":132865,"psz":913408,"fc":57,"fsm":0}
```

| Campo | Significado |
|---|---|
| `st` | Resultado: `0` bom; os outros códigos na tabela abaixo |
| `chip` | Identificador do chip que gerou o backup |
| `fwv` | Versão do firmware que gerou o backup: `maior × 65536 + menor × 256 + correção`; 132865 é a 2.7.1 |
| `psz`, `fc` | Tamanho da carga e número de arquivos |
| `fsm` | `1` se o sistema de arquivos foi alterado |

| `st` | Significado |
|---|---|
| 1 | Não é um backup |
| 2 | Formato não suportado |
| 3 | Cabeçalho corrompido |
| 4 | Arquivo truncado |
| 5 | Carga corrompida |
| 6 | O backup é de outro aparelho |
| 7 | Caminho inválido dentro do backup |
| 8 | Caminho longo demais |
| 9 | Erro de leitura ou gravação |
| 10 | Erro interno |

Um resultado ruim volta com `422`; um erro de gravação, com `500`. Um backup só restaura no mesmo aparelho que o gerou. A restauração bem-sucedida reinicia o aparelho logo depois de responder, e a conexão pode cair antes de a resposta chegar: confirme depois do reinício.

### Atualização de firmware pela API {#cap-26-ota}

A atualização exige o administrador completo e segue quatro passos. A interface web faz exatamente estes passos ([capítulo 17](#cap-17)).

::: {.figura #fig-26-ota tipo="diagrama" arquivo="26-ota.png" captura="diagrama de sequência entre um cliente e o aparelho: GET /api/backup e conferência do CRC; POST /api/restore?op=stage&commit=1 com o .bin, resposta com v=0 e committed=1; POST /api/ota/apply, resposta 202; o aparelho reinicia; o cliente repete GET /api/login_init até responder; entra e lê a versão em /api/status; restaura o .bkp com op=apply"}
A atualização pela API. O único comprovante de sucesso é a versão nova informada pelo próprio aparelho.
:::

1. **Baixe o backup.** A atualização reformata o sistema de arquivos. Só a configuração principal, o arquivo `/config/system.bin`, sobrevive: Wi-Fi, contas, slots de sensor, limites, telemetria e os demais campos de configuração. O histórico, os pacotes de idioma, os temas personalizados, a calibração dos sensores (`/calib.csv`), a pasta `/web`, o log de eventos, o par de certificados HTTPS e a origem CORS se perdem até você restaurar o backup.

   ```bash
   curl -s -b jar -o antes.bkp "$H/api/backup"
   ```

2. **Envie a imagem.** O campo do formulário leva o `.bin` da mesma imagem do aparelho (`release`, `alpha` ou `air`, o `env` do `/api/status`). `commit=1` deixa a imagem pronta para aplicar.

   ```bash
   curl -s -b jar -F file=@simut_v2.7.1_release.bin \
     "$H/api/restore?op=stage&commit=1"
   ```

   ```json
   {"st":5,"bytes":982844,"crc32":"1A2B3C4D","v":0,"dsize":982844,"dcrc":"1A2B3C4D","committed":1,"env":"release"}
   ```

   Siga só com `"st":5`, `"v":0` e `"committed":1`. Uma imagem recusada volta com `422`:

   | `v` | Motivo |
   |---|---|
   | 4 | Imagem pequena demais |
   | 5 | Imagem grande demais |
   | 6 | Início da imagem inválido |
   | 7 | A imagem é de outra variante. `env` diz qual |

   Se o envio não terminou, a resposta traz só `st`, `bytes` e `crc32`, com `st` diferente de 5: `1` falha ao começar, `2` envio incompleto, `3` imagem maior que a área de preparo, `4` falha de gravação, `6` envio abortado.

   O envio leva cerca de 30 s para uma imagem de 1 MB. Ele grava a imagem na mesma área da flash que guarda o sistema de arquivos: a partir daqui, os arquivos já se perderam. Para desistir de uma imagem preparada, reinicie o aparelho e restaure o backup.

3. **Aplique.**

   ```bash
   curl -s -b jar -X POST "$H/api/ota/apply"
   ```

   ```json
   {"accepted":true,"mode":"apply"}
   ```

   A resposta `202` chega antes de o aparelho desligar o Wi-Fi. Sem imagem preparada, a resposta é `409` `{"error":"no committed update pending"}`; com o painel em uso, `503`.

4. **Confirme.** Espere o aparelho voltar, repetindo `GET /api/login_init` a cada 3 s. Entre e leia `ver` em `GET /api/status`, ou `version` em `GET /api/perms`. Um status HTTP ou um tempo decorrido não provam nada: só a versão nova informada pelo aparelho prova a atualização. Depois, restaure o backup para recuperar o histórico e o resto ([Backup e restauração](#cap-26-backup)).

::: perigo
**Não desligue o aparelho durante a aplicação.** Existe um único espaço de firmware. Uma queda de energia na janela de gravação deixa o aparelho sem firmware, e a recuperação exige o cabo USB ([capítulo 18](#cap-18)).
:::

Se a conexão cair no meio do envio de uma imagem grande, um equipamento da rede pode estar cortando conexões longas na porta 80. Configure outra porta web e tente de novo ([capítulo 9](#cap-09-servidor-web)).

### Certificado HTTPS {#cap-26-tls}

[release]{.img}

`POST /api/tls` recebe o certificado e a chave privada juntos, em PEM, e exige o administrador completo. A receita completa, as respostas e os erros estão no [capítulo 9](#cap-09-instalar). O par vale depois do próximo reinício.

### Busca de redes {#cap-26-wifi}

`GET /api/wifi/scan` exige **Rede** [PERM_NET_CONFIG]{.perm}. A busca leva alguns segundos, então a rota trabalha em dois tempos:

1. Peça `GET /api/wifi/scan?again=1` para começar uma busca nova. A resposta é `{"scanning":true}`.
2. Repita `GET /api/wifi/scan`, sem `again`, a cada segundo, até `scanning` ser `false`.

```json
{"scanning":false,"nets":[{"ssid":"MinhaRede","rssi":-48,"enc":4,"ch":6},
 {"ssid":"Visitantes","rssi":-71,"enc":0,"ch":11}]}
```

| Campo | Significado |
|---|---|
| `ssid` | Nome da rede |
| `rssi` | Sinal, em dBm |
| `enc` | Tipo de proteção informado pelo rádio; `0` = rede aberta |
| `ch` | Canal |

A lista traz até 12 redes. Uma busca que não terminou volta como `{"scanning":false,"error":"failed","nets":[]}`. Com o rádio ocupado, por exemplo reconectando à própria rede, a rota responde `503` `{"scanning":false,"error":"busy"}`: espere alguns segundos. A busca funciona também de dentro do ponto de acesso de configuração ([capítulo 9](#cap-09-busca)).

### Métricas {#cap-26-metrics}

`GET /metrics` entrega os números do aparelho no formato de texto do Prometheus. Aceita a sessão normal ou HTTP Basic com conta e senha, porque um coletor do Prometheus não faz a entrada em dois passos. No Basic, a senha vai em texto, e o aparelho calcula o resumo. A rota exige **Painel** [PERM_DASHBOARD]{.perm} e divide o bloqueio de senha errada com a entrada. A configuração do Prometheus e a lista de métricas estão no [capítulo 24](#cap-24).

```bash
curl -s -u "metricas:$SIMUT_METRICS_PASS" "$H/metrics"
```

### As rotas do painel {#cap-26-painel}

[release]{.img}

As cinco rotas do painel exigem **Sistema** [PERM_SYS_CONFIG]{.perm}. A interface web as usa no espelho do painel e na captura de tela ([capítulo 13](#cap-13-espelho)).

| Rota | Resposta |
|---|---|
| `GET /api/screenshot` | Um BMP de 320 × 240 pixels e 24 bits, 230.454 bytes |
| `GET /api/screenshot_chunk?n=N` | Um pedaço de 16 linhas, `n` de 0 a 14: 12 bytes de cabeçalho (índice, tamanho e CRC-32, cada um em 32 bits *big-endian*) e 15.360 bytes de pixels BGR |
| `GET /api/screen_stream` | Um quadro inteiro no formato compacto do espelho, em 30 faixas de 8 linhas. O formato é interno e pode mudar entre versões |
| `POST /api/touch` | Com `x` de 0 a 319 e `y` de 0 a 239, em coordenadas do painel, um toque. Com `ms`, o toque fica pressionado esse tempo antes de soltar — é o toque longo, como o de 3 s que fixa o cartão de cima; de 100 a 15000, e um valor fora disso é levado à borda. Responde `{"ok":true,"x":160,"y":120,"ms":100}`; `x` ou `y` fora da faixa, ou `ms` que não é número, `400` |
| `GET /api/keypad` | As faces do teclado de PIN que está na tela, a geometria das teclas e a política de PIN |

```bash
curl -s -b jar -o tela.bmp "$H/api/screenshot"
curl -s -b jar -X POST "$H/api/touch" -d 'x=160&y=120'
```

Regras que um cliente precisa seguir:

- **O toque não espera a tela mudar.** A resposta sai antes de o painel redesenhar, e o aparelho só redesenha entre dois pedidos. Espere entre o toque e a captura seguinte, sem mandar nada: o espelho da interface web espera 600 ms.
- **Um toque ocupa o painel por 5 s.** Depois de um toque no vidro ou de um `POST /api/touch`, a captura em BMP e as gravações de configuração respondem `503` por 5 s. O espelho, `GET /api/screen_stream`, continua respondendo depois de um toque enviado pela web.
- **Uma captura por vez.** Uma segunda captura enquanto a primeira corre responde `409` e cancela a primeira.
- **O PIN continua valendo.** Um toque remoto passa pelo mesmo teclado de PIN que um dedo. A rota não dá acesso a nada que a conta não tenha.

`GET /api/keypad` responde, por exemplo:

```json
{"faces":["123","456","789","0"],"up":true,"kb":"num",
 "grid":[4,3,2,2,20,60,130,60,150,70],"policy":[4,8,3,0]}
```

`faces` descreve as teclas como estão desenhadas, sem dizer qual caractere é o do PIN. `up` diz se o teclado está na tela; `kb` é `cards` (teclado embaralhado), `num` ou `groups`. `grid` traz a geometria das teclas em pixels, e `policy` traz o mínimo, o máximo, os glifos por tecla e o alfabeto da política de PIN.

## Um cliente em Python {#cap-26-python}

Este cliente usa a biblioteca `requests`. Ele entra, lê o estado, sai e trata a sessão vencida do jeito descrito em [401 e 403](#cap-26-401-403). A conta e a senha vêm do ambiente.

```python
#!/usr/bin/env python3
"""Cliente mínimo da API REST do SIMUT: entra, lê o estado e sai.

Uso:
  export SIMUT_URL=http://192.0.2.10
  export SIMUT_WEB_USER=servidor
  export SIMUT_WEB_PASS='...'
  python3 simut_status.py
"""
import hashlib
import json
import os

import requests  # pip install requests


class SimutError(Exception):
    pass


class Simut:
    def __init__(self, base, user, password, verify=True, timeout=30):
        self.base = base.rstrip("/")
        self.user = user
        # A senha vai como SHA-256 hexadecimal dos bytes Latin-1,
        # igual ao que a página de entrada calcula.
        try:
            raw = password.encode("latin-1")
        except UnicodeEncodeError:
            raise SimutError("a senha tem caracteres fora do Latin-1")
        self.pw_hash = hashlib.sha256(raw).hexdigest()
        self.timeout = timeout
        self.http = requests.Session()   # guarda o cookie SIMUTSESS
        self.http.verify = verify        # em HTTPS autoassinado: o caminho do .pem

    def login(self):
        r = self.http.get(self.base + "/api/login_init", timeout=self.timeout)
        if r.status_code == 429:
            raise SimutError("bloqueios cheios; tente em %s s" % r.json().get("retryAfter"))
        r.raise_for_status()
        init = r.json()
        if init.get("locked"):
            raise SimutError("endereço bloqueado por %d s" % init["lockSec"])
        r = self.http.post(self.base + "/api/login", timeout=self.timeout,
                           data={"user": self.user, "pass": self.pw_hash,
                                 "nonce": init["nonce"]})
        body = r.json()
        if not body.get("ok"):
            raise SimutError("entrada recusada: HTTP %d %s" % (r.status_code, body))
        if body.get("redirect") == "/force_chpass":
            raise SimutError("a conta precisa trocar a senha antes de usar a API")

    def _session_alive(self):
        r = self.http.get(self.base + "/api/perms", timeout=self.timeout)
        return r.status_code != 401

    def request(self, method, path, **kw):
        kw.setdefault("timeout", self.timeout)
        r = self.http.request(method, self.base + path, **kw)
        # 401 = sessão morta. 403 pode ser sessão morta nas rotas que não
        # distinguem; /api/perms desempata.
        if r.status_code == 401 or (r.status_code == 403 and not self._session_alive()):
            self.login()
            r = self.http.request(method, self.base + path, **kw)
        return r

    def status(self, quiet=False):
        r = self.request("GET", "/api/status", params={"quiet": "1"} if quiet else None)
        r.raise_for_status()
        return r.json()

    def commit(self, payload, mode=None):
        """mode: None, "dry", "nosave" ou "reboot"."""
        data = {"_payload": json.dumps(payload, ensure_ascii=False, separators=(",", ":"))}
        if mode:
            data["_" + mode] = "1"
        r = self.request("POST", "/api/commit_all", data=data)
        body = r.json()
        if r.status_code != 200:
            raise SimutError("commit recusado: HTTP %d %s" % (r.status_code, body))
        return body   # leia body.get("rejected") e body.get("reboot")

    def logout(self):
        try:
            self.http.get(self.base + "/logout", allow_redirects=False, timeout=self.timeout)
        except requests.RequestException:
            pass   # o aparelho pode estar reiniciando; a sessão morre com ele


if __name__ == "__main__":
    dev = Simut(os.environ.get("SIMUT_URL", "http://192.0.2.10"),
                os.environ["SIMUT_WEB_USER"], os.environ["SIMUT_WEB_PASS"])
    dev.login()
    try:
        st = dev.status()
        s = st["sys"]
        print("%s  v%s (%s)  uid=%s  cfg=%s" % (s["name"], s["ver"], s["env"], s["uid"], s["cfg"]))
        print("ligado há %.1f h, sinal %d dBm, %d registros pendentes"
              % (s["uptime"] / 3.6e6, s["rssi"], s["pending"]))
        for x in st["sensors"]:
            print("  slot %2d  %-16s %s" % (x["slot"], x["name"], x["val"]))
    finally:
        dev.logout()
```

Saída:

```text
simut  v2.7.1 (release)  uid=E6614C311B7A2F2D  cfg=3F2A91C4
ligado há 8.1 h, sinal -58 dBm, 0 registros pendentes
  slot  0  Geladeira        4.25
  slot  1  Sala             23.1
```

O método `commit` já monta o JSON em UTF-8 direto, como o aparelho espera, e devolve o corpo da resposta para você conferir `rejected` e `reboot`.

## Referência rápida {#cap-26-referencia}

| Assunto | Valor |
|---|---|
| Rotas | 62 na release, 56 na alpha e no Air |
| Porta | 80 de fábrica; 443 em HTTPS com a porta de fábrica |
| Entrada | `GET /api/login_init`, depois `POST /api/login` com `user`, `pass` = SHA-256 hexadecimal minúsculo, `nonce` |
| Código de uso único | 32 algarismos hexadecimais, 60 s, uma tentativa, por endereço IP |
| Token | 32 algarismos hexadecimais, no cookie `SIMUTSESS` ou em `Authorization: Bearer` |
| Sessões | 3 ao todo, 1 por conta, 15 min de ociosidade, encerradas por reinício |
| Bloqueio | 2 s dobrando até 300 s, por IP, 8 endereços |
| Ritmo | 200 ms em `/api/logs` e `/api/ls`; 5 s em `POST /api/calib` e `/api/tls` |
| `commit_all` | Campo `_payload` até 6.144 bytes; modos `_dry`, `_nosave`, `_reboot` |
| Corpo de `calib` e `tls` | Até 8.192 bytes |
| Janela de manutenção | Segundos a partir de agora, até 2.592.000 (30 dias) |
| Contas | Até 32; nome até 15 bytes; `perms` até 8191; PIN pela API de 4 a 8 algarismos |
| Histórico exportado | Até 31 dias por pedido |
| Toque no painel | Ocupa o painel por 5 s |
