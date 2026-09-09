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
| 1.3 | **V-05** | `ap` no console publica a PSK; `nmcli dev wifi list` mostra **WPA2**; notebook entra com ela | 15 min |
| 1.4 | **V-03** | `air_test_suite.py --only T15,T16` **contra esta imagem** | 20 min |
| 1.5 | **V-01a** | `bt_auth_test.py --only lockout` — o lockout tem que **sobreviver a derrubar o link** | 10 min |
| 1.6 | **V-01b** | `bt_auth_test.py --only window` — some da varredura **e** quem já conhece o endereço ainda conecta | 7 min |
| 1.7 | **V-04 / O-2** | `json_escape_cases.py` — **por último, causa reboots** | 20 min |

⚠️ **O 1.7 não vale nada sem o controle.** Rodar antes contra firmware
**anterior** ao fix, onde os casos 1 e 2 **têm** que quebrar. Um verde sem esse
A/B é um teste que não provou saber reprovar.

⚠️ **O 1.6 precisa do canal CHARGER da mão** (GP3 → alvo GP17): cinco minutos de
observação não cabem numa janela de wake, e sem segurar o aparelho acordado o
teste mede o ciclo de hibernação e chama isso de janela fechada.

**Critério de saída:** os sete com veredito registrado e md5 da imagem, e a
tabela do `IMPLEMENTACAO_2026-09-07.md` sem nenhum "não rodado".

---

## Fase 2 — Os achados do Air ainda abertos (~2 dias)

Sete, triados por **consequência**, não por esforço:

### 2.1 Os quatro baratos (dois já estavam fechados; sobram F10 e F14)

| achado | o quê | conserto |
|---|---|---|
| ~~F10~~ | ✅ **corrigido 09/09** — o alarme do RTC é hora-do-dia, então 86.400 s vira "agora"; `airSleepSecBounded( )` limita a 86.399 s onde o intervalo vira alarme, com WARN (413). A faixa de `h_int` não mudou: as imagens de tomada não têm por que recusar 24 h | teste nativo `test_sleep_sec_bounded`; ⚠️ não exercitado no ferro (um wake de 24 h não cabe numa sessão) |
| **F14** | default do pino mudou para 16 sem bump de `AIR_CONFIG_VERSION`; `air.bin` antigo mantém 255 e não há comando para trocar | bump + migração, ou comando `air pin` |
| ~~F18~~ | ✅ **já estava feito desde 06/09** — só a tabela não tinha sido virada; `platformio.ini` diz `=0` no bloco do Air e o env usa 0 | verificado 09/09 |
| ~~F20~~ | ✅ **idem** — `system ssid` no README ×3 e no `CLI-Manual.md`; `check_air_consistency.py` C1–C8 limpo | verificado 09/09 |

⚠️ **F14 tem armadilha conhecida** (F27, `isFactoryDefaults`): mexer em
`AIR_CONFIG_VERSION` quebra quem lia a versão antiga. Migração com teste.

### 2.2 F08 — o aparelho que fica offline para sempre

`DECIDE` só vai a `CONNECT` quando já está `NET_READY`, que exige NTP **sem
fallback**. Um pacote NTP perdido e o wake nunca transmite.

É o irmão exato do defeito de Wi-Fi que abriu esta sessão — *a reconexão exigia
uma varredura e o boot não* — e o conserto tem a mesma forma: um caminho que não
depende da condição que pode falhar para sempre. Prazo + tentativa às cegas.

**Teste:** bloquear NTP no roteador (ou apontar para um servidor morto) e provar
que a telemetria ainda sai.

### 2.3 F11 — a partição que nunca é limpa

O M1 pula `StorageManager::update()` (limpeza de orçamento do FS) e nunca chama
`flushPendingIfAny()`. **Em uso só-M1 — que é o uso do produto — a partição
enche.** Perda de dados silenciosa, a prazo longo.

**O conserto é pequeno; o teste é o trabalho.** Como provar que o consumo
estabiliza sem esperar semanas? Acelerar o relógio do problema: `h_int` no mínimo,
FS deliberadamente quase cheio, e medir `fs_u` ao longo de N ciclos com um controle
negativo (a mesma corrida com a limpeza desativada tem que ENCHER).

⚠️ **Medir `fs_u` pelo delta do arquivo do dia dá +0 e engana** — bloco aberto.
Ver `full_history( )` em `tools/air_test_suite.py` e a linha do F23 no `SIMUT_AIR_PLANO_FIX.md`.

### 2.4 F12 — `air stop` por Bluetooth não funciona em M1

O laço M1 só chama `processInput` (USB). Funcionalidade ausente, não risco.
Fechar por último ou aceitar como limitação documentada.

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

### 3.2 OTA no Air

A `:8080` não estava escutando. **Descobrir o porquê antes de testar**: é config
da bancada ou o build Air não sobe o servidor? A resposta muda o que se testa.
Depois, ciclos de OTA com o protocolo já validado (24 ciclos na v2.2.12).

### 3.3 Soak longo

≥12 h ciclando sem ninguém tocar. Contar: reboots, deriva de heap, registros
perdidos (agora mensurável — `full_history` + presença no USB), e falhas de
telemetria.

---

## Fase 4 — Higiene (~meio dia)

| # | item | nota |
|---|---|---|
| 4.1 | **PCB**: re-exportar da fonte corrigida com KiCad 10.0.6 | a `main` está nos gerbers de 06/09; a re-exportação da branch air foi descartada no merge porque vinha de fonte **sem** a correção de pinout. Conferir o pinout do display **no gerber**, não no fonte |
| ~~4.2~~ | ✅ **Folga real do Air: 4.972 B** — os dois números eram verdadeiros e mediam coisas diferentes | `used 1.025.600` do PIO é a **soma das seções**; o `.bin` tem **1.039.508 B**. A diferença (13.908 B) é o linker alinhando a LMA do `.data` em 4 KiB (`readelf -l`: o LOAD 1 termina em 0xfc000 e o `.data` começa ali). Folga real = `1.044.480 − fim do último LOAD` = **4.972 B**, e anda em degraus de 4 KiB: os próximos ~13,9 KB de `.text/.rodata` são de graça; passado o degrau, sobram **876 B**. O portão do `flash_budget.json` lê a linha do PIO por desenho (marca d'água, não teto) |
| 4.3 | **`rig_validate_history_clock.py`** | afirma "nenhum registro do trecho drenado foi pulado" com `expected − seen`; registro no bloco aberto não entra em `expected`, então um pulo ali não aparece. **Falso positivo por construção.** Medir se morde; corrigir com `full_history` se sim |
| ~~4.4~~ | ✅ Varrido: 7 leitores | `history_v5.py` é o codec; `test_webui_graph_order.py` e `test_h5_day_merge.py` usam fixture; `fsguard.py` e `h5_day_merge.py` tratam o `.wip` explicitamente (15× e 5×) — é o bloco aberto, por desenho. **Sobra só a 4.3** |

---

## Fase 5 — Recortar a release

Só depois das Fases 1 e 2.

`v2.4.2-beta` com: o 409, T11/T08 corrigidos, F04 fechado, os achados da Fase 2
e as sete verificações de segurança registradas. Usar o protocolo de promoção
que já existe em `docs/promotion/`.

⚠️ **`git push origin :refs/tags/<tag>` REBAIXA o release a draft** e derruba o
Latest. Conserto: `gh release edit --draft=false --latest`.

---

## O que este plano NÃO cobre

- As cinco issues abertas (#52, #57, #58, #60, #61) — todas `good first issue`
  de design e documentação. É backlog, não dívida.
- Reboot sob telemetria morta (`ctx=205`) — aberto e **não reproduz**; as
  blindagens já existem. Fica onde está até reproduzir.
