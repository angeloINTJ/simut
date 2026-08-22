# Buraco no histórico 21/08 00:00–00:22 — análise e plano de resolução

**Data da análise:** 2026-08-21, manhã · **Dispositivo:** Pico W do rig (192.168.3.24), v2.3.2-beta · **Autor:** Claude (sessão de forense pós-release)

---

## 1. Sumário executivo

O gráfico de histórico não tem dados entre **21/08 00:00:00 e 00:22:20** porque a
**bateria de OTAs da release v2.3.2-beta** (rodada 00:17–00:22) **reformatou o
LittleFS duas vezes** — comportamento arquitetural conhecido: a área de staging
da OTA **é** a partição do sistema de arquivos — e o **backup feito às 00:17
não incluiu o arquivo do dia corrente** (`20260821.h5`) **nem o `.wip`** (o
bloco aberto, gravado registro a registro), que juntos guardavam os ~17
minutos de registros já feitos do dia. O restore pós-OTA devolveu `20260820.h5` +
60 dias sintéticos, mas os registros de 00:00–00:17 já não existiam em lugar
nenhum para devolver.

**Não é bug do firmware.** O log binário mostra todos os boots da noite
saudáveis: NTP em ~19 s, correções de −6 a −40 s em nível INFO,
`APP_NTP_CORRECTED ctx=0` (zero blocos deslocados), nenhum
`STO_SCHEMA_MISMATCH`, nenhum `wip_seed_rejected`. A maquinaria de relógio
consertada na saga de 16/08 está operando corretamente na v2.3.2-beta.

**Os dados são irrecuperáveis** (§5). O plano (§6) elimina a recorrência:
mesclador de arquivos-dia V5 + ferramenta de guarda do FS promovidos ao repo
(`tools/`), com testes automatizados no CI e a bateria de OTA obrigada a
usá-los. A perda residual por bateria cai de ~22 min para ~1,5–2 min
(a janela física de stage+apply+boot, inevitável sem mudar a arquitetura).

---

## 2. Sintoma

Buraco no gráfico web de histórico: nenhum registro entre a meia-noite de
21/08 e as 00h22. Dados presentes e contínuos antes (até 20/08 23:59:06) e
depois (de 21/08 00:22:20 em diante).

## 3. Evidências

### 3.1 As bordas exatas, medidas no ferro

`GET /download?file=/history/…` + varredura de chunks (`tools/history_v5.py`):

| Arquivo | Primeiro registro | Último registro | Observação |
|---|---|---|---|
| `20260820.h5` (14.198 B) | 20/08 00:01:49 | **20/08 23:59:06** | restaurado do backup das 00:17; íntegro até o fim do dia |
| `20260821.h5` (2.099 B) | **21/08 00:22:20** (t0=1787282540) | 21/08 05:55:48 + bloco aberto | criado no boot pós-OTA final |

O buraco é `[00:00:00, 00:22:20]` — **22 min 20 s ≈ 22 registros** no
intervalo de 60 s do histórico.

### 3.2 O log binário nasce junto com o buraco

`GET /api/logs` (1.348 registros) decodificado. O registro 0 do log atual é o
**boot pós-apply da OTA final** — o log anterior morreu com o FS:

```
   0 <0>            up=8   INF SYS_BOOT            ctx=0   ← autópsia pós-apply
   9 21/08 00:22:17 up=19  INF SYS_NTP_SYNC        ctx=0   ← boot começou ~00:21:58
  13 21/08 00:22:20 up=22  INF STO_H5_WIP          ctx=1   ← 20260821.h5 novo
  14 21/08 00:22:21 up=23  INF SYS_TEL_SENT        ctx=200
  17+ 00:22:49 …           INF WEB_UPLOAD ×62              ← restore (20260820 + 60 sintéticos + .lng)
```

Boots seguintes (00:24:50, 00:26:36, 00:30:27, 00:31:23 — bancada de certs +
soak HTTPS; 01:03:36 — fim da bateria; 05:56:18 — commit_all do Ângelo às
05:55:59, `SYS_STORAGE_SAVE ctx=3917`): **todos com adoção de `.wip` em INFO e
correção NTP pequena**. Zero perda de dados em qualquer um deles.

### 3.3 Linha do tempo reconstruída (git + mtimes da sessão de release + log)

| Hora (−03) | Evento | Fonte |
|---|---|---|
| 20/08 23:59:38 | commit `1356df2` (M1 anti-piscada) | git |
| 21/08 00:00:00 | vira o dia; `20260821.h5` começa a receber registros | — |
| ~00:14 | commit `002bbbf` (bump v2.3.2-beta) | git |
| ~00:15 | flash USB 2.3.1+M1 (LittleFS **preservado** — sem perda) | notas da sessão |
| **00:17** | **backup `fsbak/`: só `20260820.h5` + `language_pt-BR.lng`** — sem o dia corrente | mtime do dir da sessão `92118e48` |
| ~00:18 | OTA #1 (→ v2.3.0 publicada): stage escreve 1.009.964 B **sobre a partição do FS**; apply apaga o resto | mtime `ota.py`/`simut_v2.3.0-beta.bin`; código §4.1 |
| ~00:20 | OTA #2 (→ v2.3.2-beta, pelo applier da 2.3.0 = caminho do usuário): **segunda reformatação** | idem |
| 00:22:20 | boot final; primeiro registro do dia novo | log + h5 |
| 00:22:49 | começa o restore (rajada de `WEB_UPLOAD`) | log |
| 00:32–01:02 | soak HTTPS 30 min | `soak_https_stats.json` |
| 01:04:16 | commit `4773fd4` + release publicada | git |

### 3.4 O mesmo mecanismo mordeu na véspera

`20260820.h5` tem um buraco interno **17:56:00 → 20:30:35 (2 h 34 min,
~154 registros)**: a bancada de OTA da noite de 20/08 (v2.3.0) apagou o FS e o
restore veio do backup das 17:56 (`backup_pre_bench7_1756.bkp`). É a mesma
falha, um dia antes — isto é um **modo de falha recorrente**, não um acidente
pontual.

## 4. Causa-raiz (três camadas)

### 4.1 Arquitetural — conhecida e por desenho

A staging da OTA ocupa a mesma região de flash da partição LittleFS
(`src/ota/staging.cpp:37`). No apply, depois de copiar a imagem, o applier
apaga da staging até o snapshot (`src/ota/applier.cpp:344-351`,
`LFS_ERASE_END = OTA_SNAPSHOT_OFFSET`) — o boot seguinte encontra a região
em 0xFF e formata. Só a config sobrevive (snapshot no último setor). Ou seja:
**toda OTA destrói `/history` inteiro, inclusive o dia corrente.** Trade-off
deliberado (flash 100% ocupada, não há espaço para um segundo slot); não é o
que vamos mudar.

### 4.2 Ferramental — a causa acionável

O driver de OTA da bancada (`ota.py`, recriado a cada sessão no scratchpad)
**não tem passo de backup** — é só stage+apply+verify. O backup foi manual e
capturou 2 arquivos; ficaram de fora **os dois carregadores do dia corrente**:

- `20260821.h5` — existia desde o reboot das ~00:15 (o gancho pré-reboot sela
  o bloco aberto e o adota no arquivo do dia), com os registros de
  00:00–00:15;
- `/history/.wip` — o snapshot do bloco aberto, gravado a cada registro, com
  o rabo de 00:16–00:17. Fora do topo da hora, **o `.wip` é o único portador
  dos registros ainda não selados** (blocos só selam a cada 60 registros ou
  em reboot limpo) — num backup pré-OTA ele pode valer até 59 minutos de
  dados.

Duas reformatações depois, nada disso existia mais em mídia nenhuma.

### 4.3 Processo — a regra que virou armadilha

A regra operacional "não sobrescrever o arquivo do dia corrente no restore"
(correta: o firmware escreve nele) degenerou em "não copiar nem devolver o dia
corrente". Sem um passo de **mesclagem**, o dia corrente fica sem proteção
nenhuma — é justamente o único arquivo que sempre tem dados novos na hora da
bateria. E como a ferramenta de guarda nunca foi promovida ao repo, cada
sessão reescreve a sua versão, e a cobertura varia com a memória de quem
reescreve.

## 5. Os dados são recuperáveis? Não.

1. **Na flash:** a região foi fisicamente sobrescrita duas vezes (imagem de
   ~1 MB staged sobre a partição de 1 MB) e apagada pelo applier. Nada a
   esculpir.
2. **Na telemetria:** o log mostra `SYS_TEL_FAIL ctx=-1` (conexão recusada)
   contra o coletor configurado (`192.168.3.206:8443`) durante a madrugada
   inteira e ainda às 06:27. A fila de reenvio lê de `/history` — que já não
   tem os registros. O coletor local da bancada só existiu 00:26–00:29
   (pós-wipe). **Não há cópia.** (Vale um olhar no teu coletor `.206` por
   desencargo — se algo de 00:00–00:15 chegou lá antes de recusar, é a única
   cópia possível; o log sugere que não.)

**Perda registrada em definitivo:** 21/08 00:00–00:22 (~22 registros) e
20/08 17:56–20:30 (~154 registros). Ambos em dias de bancada, maioria do
acervo sintético — perda de valor baixo, mas o mecanismo atingiria igualmente
dados reais.

## 6. Plano de resolução

| # | Ação | Entregável | Status |
|---|---|---|---|
| F1 | **Mesclador de arquivo-dia V5**: junta N versões do mesmo dia em ordem cronológica, byte-preserving (chunks copiados verbatim, CRC intacto), deduplicação exata, recusa schema divergente, política explícita para conflito de t0 | `tools/h5_day_merge.py` | feito |
| F2 | **Guarda do FS promovida ao repo**: `backup` baixa `/history` completo **incluindo o dia corrente e o `.wip`** (manifesto + sha256, com o pacing/retry que o `/api/ls` exige); `restore` devolve o que falta e, para arquivo-dia presente dos dois lados com conteúdo diferente, **mescla** (download → merge → upload → verificação com re-download, com retry se o device selar bloco no meio). O `.wip` do backup **nunca sobe cru** (o boot seguinte adotaria snapshot velho — armadilha de 16/08): é absorvido como bloco no arquivo do dia dele — **exceto** se o device ainda for dono daquele bloco (mesmo t0 vivo no `.wip` atual, ou já selado no arquivo), o caso "restore sem wipe no meio", em que absorver duplicaria registros | `tools/fsguard.py` | feito |
| F3 | **Testes automatizados** do mesclador (disjunto, dedup, idempotência, conflito, CRC corrompido, schema divergente, ordem) + **gate no CI** | `tools/test_h5_day_merge.py` + passo em `.github/workflows/build.yml` | feito |
| F4 | **Processo**: bateria de OTA passa a ser `fsguard backup` → OTAs → `fsguard restore`, sem exceção; documentado no cabeçalho da ferramenta e na memória do fluxo de release | doc + memória | feito |
| F5 | *(proposto, follow-up de firmware)* Código de log `STO` explícito de "FS recém-formatado" no boot, para que um buraco futuro seja atribuível pelo próprio log do device (hoje a evidência é indireta). Custo: código novo nas 5 tabelas de logcodes; entra num ciclo normal de release, não agora (v2.3.2 recém-publicada) | issue/backlog | proposto |

**Perda residual esperada com F1–F4:** a janela entre o último backup possível
e o primeiro registro pós-boot — stage (~31 s) + apply/boot (~50 s) por
bateria, ~1,5–2 min no total, 1–2 registros. Inevitável sem mudar a
arquitetura da staging (§4.1).

## 7. Verificação executada

- `tools/test_h5_day_merge.py`: **25/25 casos passando** — mesclagem
  disjunta, dedup exato, idempotência (`merge(merge(a,b),b) == merge(a,b)`),
  fatiar-e-mesclar byte-idêntico, recusas explícitas (schema divergente,
  conflito ambíguo, CRC ruim, entrada vazia/lixo, multi-schema, dias
  diferentes pelo nome no CLI), validação e absorção de `.wip`.
- Validação com dados reais do rig (offline): `20260820.h5` (155 blocos)
  fatiado em dois e mesclado de volta → **byte-idêntico ao original**; chunk
  de SCHEMA reconstruído pelo codec host → **paridade byte a byte com o do
  firmware**; fragmento pré-wipe simulado + `20260821.h5` real → 352
  registros em ordem cronológica começando 00:00:20 — exatamente o resgate
  que teria evitado o buraco.
- Validação ao vivo no rig (sem nenhuma escrita no device): `fsguard backup`
  → **63 arquivos, 0 falhas**; `restore --dry-run` → 62 idênticos +
  demonstração da absorção do `.wip` (45 registros não-selados); segunda
  passada com o guarda anti-duplicação → `.wip` **pulado** por bloco ainda
  aberto no device (mesmo t0) e restore integralmente idempotente,
  0 falhas.
- CI: gate novo (`History V5 day-merge tests` em `build.yml`) roda em
  segundos, sem dependência de rede/hardware.

## 8. Anexos forenses

Coleta e decodificadores em `scratchpad/forense_20260821/` (`collect.py`,
`logs.bin`, `perms.json`, `ls_history.json`, os dois `.h5`). Ferramentas de
leitura: `scratchpad/forense_20260816/logdec.py` e `blocks.py`
(candidatas a promoção futura para `tools/`).
