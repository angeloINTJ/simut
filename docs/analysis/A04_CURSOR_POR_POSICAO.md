# A-04 — o cursor da telemetria por posição

> Proposta de 02/10/2026 (#231), para decisão do mantenedor. É o item A-04 da
> revisão externa e o B10 do [PLANO_STABLE.md](PLANO_STABLE.md). O mantenedor
> escolheu a B, com K = 3, no mesmo dia; ela está no #232. O que a
> implementação acrescentou à proposta está em
> [O que a implementação acrescentou](#o-que-a-implementação-acrescentou). O
> código citado no defeito é o da `main` em `35c9678`.

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
| Dados que já existem | os arquivos V5 continuam com o cursor por tempo | o carimbo antigo segue valendo para os arquivos de dia até o do último envio, até a primeira entrega em cada um; o resto vai por posição desde a troca |
| O que continua pulando | nada | uma escrita atrasada num arquivo de mais de K dias. Um arquivo de dia apagado, ou trazido de volta por uma restauração de backup, sai de novo inteiro |

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

## O que a implementação acrescentou

- **A posição confere o que sobrou nela.** Cada arquivo guarda, além da
  posição, o carimbo do último registro que ela contou como enviado, e a
  coleta confere esse registro antes de confiar na posição
  (`telSlotHolds( )`). Se outro registro está ali, a posição não diz mais o que
  saiu, e o arquivo vai inteiro de novo, com o evento **554**. Acontece em três
  casos:
  - o aparelho perde energia logo depois de mandar registros que só estavam na
    RAM, e as leituras seguintes ocupam os mesmos índices;
  - uma selagem falha, e o bloco seguinte começa no mesmo lugar;
  - um arquivo de dia é apagado, ou uma restauração de backup o traz de volta.

  Sem a conferência, esses casos seriam lacunas.
- **Tabela cheia fecha o dia mais velho.** São 8 posições. Quando um envio
  precisa de uma nona, o dia mais velho é fechado (o piso sobe), e não
  esquecido. Um dia antes do que está sendo entregue já foi esvaziado pelo
  mesmo lote. Esquecer o dia fazia a fila longa de um aparelho de intervalo
  lento, com um lote cobrindo mais dias do que há posições, girar para sempre:
  um arquivo de duplicatas por lote (teste
  `test_telpos_a_drain_longer_than_the_table_ends`).
- **O relógio.** O piso segue hoje menos 3 dias, contados do meio-dia, para que
  a hora de verão não mude a data. Um relógio que voltou puxa o piso de volta.
  Com o relógio confiável (NTP ou acerto à mão), a posição de um dia depois de
  amanhã é descartada: nada tão à frente é enviado.
- **O lote guarda trechos, não posições.** Um trecho é uma sequência de
  registros seguidos de um mesmo bloco. Um lote de 250 registros de blocos de
  uma hora são cinco trechos. Uma posição por registro seriam 4 KB de heap ao
  lado do handshake TLS; os 16 trechos custam 256 B.
- **A contagem de pendentes lê só cabeçalhos.** Na transição, um arquivo ainda
  na regra antiga tem só o primeiro carimbo de cada bloco, e o bloco que cruza
  o carimbo antigo conta como enviado até a primeira entrega, que dá posição ao
  arquivo. A coleta decide registro a registro, então isso é a estimativa
  curta, nunca um registro retido.
- **Custo:** +1.992 B de flash na release e cerca de 400 B de heap. A imagem
  `pico_w_test_https` ficou 699 B abaixo do teto de OTA (B6 do
  [PLANO_STABLE.md](PLANO_STABLE.md)).

## Como provar

- **Host.** A regra de posição num header puro (`src/TelemetryPosition.h`),
  testável no `native`. Os casos:
  - bloco atrasado no mesmo arquivo;
  - bloco atrasado no arquivo de ontem;
  - bloco aberto enviado em parte e depois selado;
  - arquivo reescrito;
  - a migração do carimbo.

  Cada caso roda antes contra a regra por tempo e tem de falhar exatamente onde
  ela pula. No #232, 11 dos 12 primeiros falharam assim. Os da conferência e da
  tabela cheia vieram depois, cada um falhando contra a regra anterior.
- **Ferro.** O roteiro:
  1. adiantar o relógio à mão;
  2. deixar gravar e enviar para o coletor de bancada;
  3. voltar o relógio;
  4. deixar gravar de novo.

  Na `main`, os registros gravados depois da volta não chegam ao coletor; na
  branch, chegam todos. A conferência é contra a flash, como no dreno de 23/09.

## Decisões

- B (02/10/2026).
- K = 3 dias, o que cobre um relógio que volta até três dias.
