# V-09 — quem gerencia usuários concede o que não tem

**Achado por:** Ângelo, 21/09/2026 · **Severidade: Alta** · **Estado:
CORRIGIDO e VALIDADO NO FERRO em 22/09/2026** · **Base do achado:** `main` @
v2.6.1-beta · **Correção:** PR #149 (`fix/v09-perms-escalation`, `87f9162`).

**Como foi conferido:** leitura da fonte em 21/09 (o aparelho estava no meio do
T5 e não pôde ser usado), **e a reprodução do §4 rodada no ferro em 22/09** com
controle positivo — o registro está no §7. Forma escolhida: **A, recusar**.

---

## 1. O achado, em uma frase

**Uma conta com `PERM_USER_MGR` cria outra conta com QUALQUER combinação de
bits — inclusive bits que ela própria não tem — e recebe a senha da conta nova
na mesma resposta.**

A regra que o produto deveria obedecer, nas palavras do mantenedor: *se eu não
tenho permissão de gravar, não posso dar essa permissão para um usuário novo.*

## 2. Onde

`src/WebManager_Commit.cpp:1362-1365`, seção `users`, ação `add`:

```cpp
long perms = 0;
int pp = obj.indexOf("\"perms\":");
if (pp >= 0) perms = obj.substring(pp + 8).toInt( );
if (perms < 0 || perms > PERM_ALL_BITS) {
    rejectField("users.perms"); …
}
…
cfg.users[slot].permissions = (uint16_t)perms;
```

O único teto é **o mapa de bits do produto** (`PERM_ALL_BITS` = `0x1FFF`). Não
há, aqui nem antes daqui, nenhuma comparação com a máscara de quem está pedindo.

E a porta está aberta de propósito para o `USER_MGR` sozinho — o comentário do
portão de entrada (`WebManager_Commit.cpp:295-302`) diz textualmente que uma
conta *"com `PERM_USER_MGR` e nada mais"* precisa passar, senão o papel não
gerencia usuário nenhum:

```cpp
uint16_t perms = requirePerm(0);            /* qualquer sessão */
if (!(perms & commitEntryPerms( ))) …       /* SYS_CONFIG | NET_CONFIG | USER_MGR */
```

O portão de entrada está certo. O que falta é o portão **do valor**.

⚠️ `docs/AUTHORIZATION.md` resume esta rota como "`PERM_SYS_CONFIG` **plus**
per-section authz", o que se lê como "precisa de SYS_CONFIG **e** do bit da
seção". Não é o que o código faz: basta **um** dos três bits de entrada. Quem
avaliar a severidade pelo documento vai subestimá-la. O documento precisa de
conserto junto com o código.

## 3. Até onde escala

| a conta atacante tem | consegue criar uma conta com | ganha na prática |
|---|---|---|
| `PERM_USER_MGR` (`0x0100`) e mais nada | qualquer coisa até `0x1FFF` | configuração do sistema, rede, arquivos (ler, subir, apagar), calibração, logs, histórico e os três bits do painel |

**O limite é real e vale registrar:** `PERM_ALL_BITS` é `0x1FFF` e
`PERM_FULL_ADMIN` é `0xFFFF`. As duas rotas que exigem **igualdade** com
`PERM_FULL_ADMIN` — `POST /api/ota/apply` e `POST /api/tls` — continuam fora de
alcance. Tudo o mais que a interface web oferece, não.

**A senha vem de brinde.** A resposta do `commit_all` devolve `creds` com a
senha de uso único da conta criada — é o desenho correto para quem legitimamente
cria contas, e é também o que transforma a escalação em dois passos sem nenhuma
adivinhação.

### 3.1 Segundo vetor, mesma raiz

`{"type":"pin","id":0,"pin":"……"}` — **o slot 0 é aceito de propósito**
(`WebManager_Commit.cpp:1406-1407`: *"Slot 0 is allowed: this is how the admin's
panel PIN is set from the web"*). Uma conta com `USER_MGR` define **o PIN do
painel do admin** e passa a agir como admin **no painel**: bloquear alarme,
abrir manutenção, entrar no item Usuários.

Isto não é o mesmo bug, mas é a mesma pergunta não feita: *quem pede tem
autoridade sobre o que está mudando?*

### 3.2 O que já está protegido (para não exagerar o relato)

- `del` e `reset` **recusam o slot 0**: ninguém apaga nem reseta a senha do
  admin por esta via (`users.id` rejeitado).
- `perms` acima de `0x1FFF` é recusado, então `PERM_FULL_ADMIN` não é alcançável.
- O nome `admin` é recusado na criação.
- A conta nova nasce com `mustChangePassword` armado.

## 4. Como reproduzir na bancada (2 min, quando o ferro estiver livre)

```bash
# 1. como admin, crie a conta atacante com USER_MGR e NADA mais (0x0100 = 256)
curl -b j -X POST http://IP/api/commit_all --data-urlencode \
  '_payload={"users":{"actions":[{"type":"add","name":"gestor","perms":256}]}}'
#    guarde a senha devolvida em creds e troque-a no primeiro login

# 2. logue como "gestor" e peça uma conta com TUDO (0x1FFF = 8191)
curl -b j2 -X POST http://IP/api/commit_all --data-urlencode \
  '_payload={"users":{"actions":[{"type":"add","name":"escalou","perms":8191}]}}'

# esperado HOJE: 200 + creds da conta "escalou", com todos os bits
# esperado DEPOIS da correção: 200 com "rejected":["users.perms"], conta não criada
```

Confirme com `GET /api/users` que `escalou` não existe (ou existe com os bits
que o gestor tinha, se a decisão for truncar em vez de recusar).

## 5. Correção — três formas, e a que eu recomendo

A regra é uma linha: **ninguém concede o que não possui.**

| # | forma | efeito | custo |
|---|---|---|---|
| **A** | **recusar**: `if (perms & ~callerPerms) rejectField("users.perms")` | a conta **não é criada**; a resposta nomeia o campo | mínimo; usa o `rejectField` que a seção já tem |
| B | **truncar**: `perms &= callerPerms` | a conta nasce com o que o criador podia dar, em silêncio | perigoso: cria conta diferente da pedida sob 200 — a família do B12/`slots` |
| C | exigir `PERM_FULL_ADMIN` para mexer em `perms` | só o admin define permissões | quebra o papel "gerente de usuários" que o portão de entrada existe para permitir |

**Recomendo A.** É a postura do resto do arquivo — *recusar em vez de estragar* —
e é a única que deixa rastro para quem pediu.

Aplicar a mesma regra em:

1. `add` (`WebManager_Commit.cpp:1362`) — o achado;
2. a ação `pin` com `id == 0` — exigir que o chamador **seja** o slot 0 ou tenha
   `PERM_FULL_ADMIN`, senão `rejectField("users.id")`;
3. a CLI `user perm <nome> <papel>` (`AppManager_CmdHandlers.cpp`) — o console
   serial é acesso físico e tem outra classe de ameaça, mas a regra deve ser a
   mesma nos dois caminhos, senão a web vira a porta estreita e a serial a larga;
4. `docs/AUTHORIZATION.md` — corrigir o resumo desta rota (§2).

**Teste que deve nascer junto:** um caso no `test_authz`/`web_test_suite.py` que
crie a conta `USER_MGR` pura, tente `perms=0x1FFF` e exija `rejected`. Sem ele a
correção não tem quem a defenda na próxima refatoração.

## 6. Por que a correção esperou um dia

A imagem no ferro estava sob a campanha de estabilidade (`PLANO_STABLE.md`): o
B2 fechou em 21/09 com 7,9 h de soak **naquela imagem**, e o B1 dependia de
corridas nela. Gravar firmware novo invalidava o que já tinha sido medido. Então
a correção foi escrita, revisada e testada em nativo no mesmo dia (PR #149,
`+64 B` por imagem, 10/10 no CI), e **só foi ao ferro depois** que a caçada do
B1 e o A/B do B13 liberaram o aparelho — em 22/09, §7.

O preço dessa ordem está pago e anotado: a imagem da campanha mudou, então o
soak do B2 e o OTA do B3 precisam ser remedidos na imagem que a versão vai
publicar. Isso está na tabela de bloqueadores do `PLANO_STABLE.md`.

## 7. Validação no ferro — 22/09/2026

Rodada na imagem `pico_w_test` construída de `fix/v09-perms-escalation`
(`87f9162`), gravada no rig às 01h; aparelho em 192.168.3.24, HTTP puro.
Instrumento: `v09_verify.py` (scratchpad da sessão), **10/10 veredictos**.

O que dá peso à corrida é o **controle positivo**: sem os casos B e D,
"recusou" não distingue a regra do subconjunto de um portão que recusa tudo.

| caso | quem pede | pedido | resposta do aparelho | veredito |
|---|---|---|---|---|
| **A** | `gestor` (`perms`=256, só `USER_MGR`) | `add` com `perms=8191` | `200` `{"applied":[],"rejected":["users.perms"]}`, **sem `creds`** | recusa, e a conta `escalou` não existe em `/api/users` |
| **B** | `gestor` | `add` com `perms=256` — o bit que ele tem | `200` `{"rejected":[],"creds":[{"u":"parceiro",…}]}` | **aceita o subconjunto**: a regra é `perms & ~caller`, não um veto |
| **C** | `gestor` | `pin` no `id:0` (painel do admin) | `200` `{"applied":[],"rejected":["users.id"]}` | segundo vetor fechado |
| **D** | `admin` (`perms`=65535) | `add` com `perms=8191` | `200` `{"creds":[{"u":"plenos",…}]}` | o admin não perdeu nada |

Conferência final: `/api/users` mostrou `parceiro` com 256 e `plenos` com 8191,
`escalou` ausente, e as seis contas originais voltaram byte a byte à linha de
base depois da limpeza.

Duas coisas que a conta nova continua fazendo certo, confirmadas na mesma
corrida: o login de `gestor` com a senha de uso único devolveu
`{"ok":true,"redirect":"/force_chpass"}` (troca forçada armada), e uma tentativa
com senha errada já veio com `lockSec`, ou seja o lockout cobre a conta nova.

### Três armadilhas da rota que o instrumento me cobrou

Nenhuma é defeito — são contratos que um cliente novo (o servidor da empresa,
`INTEGRACAO_SERVIDOR.md`) descobre do jeito difícil:

1. **`del` e `reset` são por `id`, não por nome** (`WebManager_Commit.cpp:1432`).
   Um `{"type":"del","name":"gestor"}` volta `200` e não apaga nada — meu
   primeiro instrumento "apagou" a conta e o `add` seguinte deu `users.dup`.
2. **O destino do login vem no CORPO, não em `Location`**
   (`WebManager_Auth.cpp:544`): `{"ok":true,"redirect":"/force_chpass"}`. Ler o
   header devolve `''` **sempre**, e a troca de senha forçada passa batida.
3. **Toda escrita na seção `users` reinicia o aparelho.** Esta corrida sozinha
   custou **7 reboots** (3 `add` + 3 `del` + um 401 no meio), cada um ~25 s: é a
   dívida já registrada em `PLANO_DIVIDA_TECNICA.md` e o que faz o servidor da
   empresa precisar de retentativa em cada cadastro de usuário.
