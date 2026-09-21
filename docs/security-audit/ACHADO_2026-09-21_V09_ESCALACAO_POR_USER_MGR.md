# V-09 — quem gerencia usuários concede o que não tem

**Achado por:** Ângelo, 21/09/2026 · **Severidade: Alta** · **Estado: ABERTO,
correção urgente** · **Base:** `main` @ v2.6.1-beta, conferido em
`docs/plano-stable` @ `87b768f`.

**Como foi conferido:** leitura da fonte. **Não foi exercitado no ferro** — o
aparelho estava no meio do T5 (queda de rede) quando o achado chegou. A
reprodução de bancada está no §4 e leva dois minutos; o código, porém, não
deixa margem: a comparação que faltaria não existe em lugar nenhum da seção.

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

## 6. Por que não corrigi agora

A imagem no ferro está sob campanha de estabilidade (`PLANO_STABLE.md`): o B2
fechou hoje com 7,9 h de soak **naquela imagem**, e o B1 ainda depende de
corridas nela. Mudar firmware agora invalida o que já foi medido e o que falta
medir. A correção é pequena e isolada, mas merece PR próprio, com o teste do §5,
e uma decisão explícita sua sobre a forma (A, B ou C).
