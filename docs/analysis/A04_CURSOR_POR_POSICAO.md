# A-04 — o cursor da telemetria por posição (proposta)

> Proposta de 02/10/2026, para decisão do mantenedor. É o item A-04 da revisão
> externa e o B10 do [PLANO_STABLE.md](PLANO_STABLE.md). Nada disto está
> implementado; o código citado é o da `main` em `35c9678`.

## O defeito

O cursor da telemetria é um carimbo de tempo. Depois de um envio, ele passa a
valer o maior carimbo entregue (`telDeliveredCursor`, `src/TelemetryCursor.h`),
e a coleta só pega registros com `epoch > cursor` (`TelemetryManager::collectBatch`).
Um bloco gravado **depois** de outro, mas com carimbos **anteriores**, fica atrás
da marca e nunca é oferecido. O registro não se perde: está na flash e sai pelo
histórico e pelo CSV. Mas a telemetria não o leva, e o aparelho não diz que pulou.

Medido:

- **21/09**, varredura dos 55 arquivos de dia do rig: **6 de 75.778** registros
  (0,0079%), em 3 blocos parciais fora de ordem;
- **23/09**, dreno do Air para um coletor estrito: **1 de 13.671**
  (19/09 03:40:36), o único faltante depois do conserto do #156.

O próprio `TelemetryCursor.h` já diz o limite: um cursor escalar no tempo não
alcança um registro atrás da marca-d'água.

## Como um bloco fica fora de ordem

O relógio volta. Os casos conhecidos:

- o relógio provisório do boot, corrigido depois pelo NTP. O Air acorda com ele
  a cada minuto;
- um acerto manual;
- o relógio adiantado de 14/08.

Na correção, só o bloco aberto é deslocado (`shiftOpenBlockTimeV5`). Os blocos
selados guardam os carimbos com que foram escritos, e um reboot deixa um bloco
parcial. Acertar os carimbos na origem (o teto de semente `h5SeedCeiling`, o
carry do relógio do Air no #156) diminuiu os casos, mas não os zera: o relógio
pode voltar de verdade.

## As duas saídas

| | A — sequência no bloco | B — posição por arquivo |
|---|---|---|
| O que muda | o formato do histórico (V6): +4 B por bloco | só o arquivo do cursor (`/tcursor`, hoje 4 B) |
| Quem lê o que mudou | o firmware, `tools/history_v5.py`, o portão de paridade, o decodificador da página (caminho de envelope) e o `.wip` | a coleta e a contagem de pendentes da telemetria |
| Dados que já existem | os arquivos V5 continuam com o cursor por tempo | na 1ª coleta, o carimbo vira posições pela regra de hoje, então nada muda no dia da troca |
| O que continua pulando | nada | uma escrita atrasada num arquivo de mais de K dias. "Recompor o histórico" reescreve o arquivo do dia, e esse arquivo volta uma vez à regra por tempo |

**B em detalhe.**

- **A posição.** Um arquivo de dia só cresce no fim: o bloco selado é
  acrescentado, nunca reescrito. Então "bloco *n*, registro *r* do arquivo D" é
  uma posição de gravação estável, que não depende do carimbo.
- **O que o cursor guarda.** Para cada arquivo de dia dos últimos K dias, a
  posição do próximo registro não enviado. Um arquivo mais velho que isso conta
  como enviado; um arquivo novo começa do zero.
- **O bloco aberto** já tem posição antes de selar: será o próximo bloco do
  arquivo do seu dia.
- **Na dúvida, reenvia.** A ingestão do servidor é por carimbo
  (`TelemetryCursor.h`): uma duplicata custa uma escrita, e uma lacuna custa a
  medição.

## Recomendação

**B.** Fecha o B10 sem mexer no formato que três implementações independentes
leem (o firmware, a referência em Python e a página), e o custo fica na
telemetria, que é onde está o defeito.

## Como provar

- **Host.** A regra de posição num header puro, testável no `native`. Os casos:
  - bloco atrasado no mesmo arquivo;
  - bloco atrasado no arquivo de ontem;
  - bloco aberto enviado em parte e depois selado;
  - arquivo reescrito;
  - a migração do carimbo.

  Cada caso roda antes contra a regra por tempo e tem de falhar exatamente onde
  ela pula.
- **Ferro.** O roteiro:
  1. adiantar o relógio à mão;
  2. deixar gravar e enviar para o coletor de bancada;
  3. voltar o relógio;
  4. deixar gravar de novo.

  Na `main`, os registros gravados depois da volta não chegam ao coletor; na
  branch, chegam todos. A conferência é contra a flash, como no dreno de 23/09.

## Decisões pendentes

- A ou B.
- K. A proposta é 3 dias, o que cobre um relógio que volta até três dias.
