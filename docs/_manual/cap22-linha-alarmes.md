# Linha de alarmes {#cap-22}

Este capítulo explica a segunda linha de telemetria do SIMUT: os eventos de alarme, falha, manutenção e ação no painel, entregues em segundos e com confirmação de recebimento. Ele é para quem configura a linha e para quem escreve o coletor que vai receber e confirmar os eventos.

## Para que serve {#cap-22-para-que}

A telemetria de medições entrega tudo, no ritmo do lote ([capítulo 21](#cap-21)). Com um lote mínimo de 60 registros, um limite ultrapassado agora só chega ao coletor daqui a uma hora. A linha de alarmes resolve isso: cada evento vai para o coletor assim que acontece, separado das medições.

A linha manda eventos, não medições:

- um canal saiu dos limites de alarme;
- um sensor entrou em falha;
- alguém silenciou, desligou ou religou um alarme, ou mudou um limite, no painel;
- uma janela de manutenção abriu ou fechou.

As duas linhas se completam. A telemetria diz quanto; a linha de alarmes diz o que aconteceu e quem fez.

| | Telemetria | Linha de alarmes |
|---|---|---|
| O que manda | Registros do histórico | Eventos |
| Quando | Ao juntar o lote mínimo | Assim que o evento acontece |
| Onde espera | Na flash | Numa fila na RAM |
| Um reinício | Não perde nada | Perde o que não foi confirmado |
| Confirmação | 2xx (HTTP) ou publicação aceita (MQTT) | 2xx (HTTP) ou mensagem de confirmação (MQTT) |

A linha de alarmes vem desligada de fábrica.

## Como a linha funciona {#cap-22-como}

1. Um evento entra na fila, com um número de sequência, `seq`.
2. O aparelho envia a fila inteira ao coletor, na mesma passada.
3. O coletor confirma os registros que recebeu.
4. O aparelho tira da fila só os registros confirmados.
5. Enquanto sobrar algo na fila, o aparelho envia de novo a cada 15 s. Um evento novo dispara um envio imediato.

A linha usa o transporte, o servidor, a porta, a chave de acesso e o TLS da telemetria de medições. Só o formato e o caminho são próprios dela.

::: {.figura #fig-22-fluxo tipo="diagrama" arquivo="22-fluxo.png" captura="Diagrama em duas raias, HTTP e MQTT. À esquerda, a fila na RAM do aparelho com registros seq 1, 2 e 3. Raia HTTP: seta 'POST com a fila inteira' até o coletor; volta '2xx' e os três registros saem da fila; volta alternativa 'outro código ou nada em 4 s' e a fila fica, com 'novo envio em 15 s'. Raia MQTT: seta 'publish em <tópico>/alarm' até o broker e daí ao coletor; o coletor grava e publica a confirmação com os seq 1, 2 e 3 em '<tópico>/alarm/ack'; o broker entrega ao aparelho, que tira os três da fila. Uma nota mostra que, sem a confirmação, a fila inteira é publicada de novo a cada 15 s."}
Legenda: no HTTP, a resposta 2xx confirma o lote. No MQTT, a publicação não confirma nada: só a mensagem de confirmação do coletor tira os registros da fila.
:::

## Os códigos {#cap-22-codigos}

Cada registro pertence a um só domínio, e o domínio dá o nome do campo que carrega o código:

| Campo | Código | Significa | Traz também |
|---|---|---|---|
| `alarm` | `alarm` | Um canal saiu dos limites | `val` |
| `alarm` | `alarm_sil` | Alguém silenciou o alarme de limite no painel | — |
| `alarm` | `alarm_off` | Os alarmes do sensor foram desligados no painel | `user` |
| `alarm` | `alarm_on` | Os alarmes do sensor foram religados no painel | `user` |
| `alarm` | `alarm_lim` | Um limite foi mudado no painel | `lo`, `hi`, `user` |
| `err` | `err` | O sensor entrou em falha | — |
| `err` | `err_sil` | Alguém silenciou o alarme de falha no painel | — |
| `err` | `err_off` | O alarme de falha do sensor foi desativado no painel | `user` |
| `maint` | `maint_on` | Uma janela de manutenção abriu | `until`, e `user` quando alguém a abriu |
| `maint` | `maint_off` | A janela fechou, por comando ou por prazo | `user` quando alguém a fechou |

### Quando cada código aparece {#cap-22-quando}

- **`alarm`.** Um canal ficou fora da faixa entre o limite mínimo e o máximo em duas verificações seguidas. Só com os alarmes do sensor ligados e fora de manutenção. Sai um registro por saída da faixa, por canal: enquanto o valor continua fora, nada mais sai. O `val` é o valor lido naquela hora.
- **`err`.** O sensor parou de responder ou foi trocado por outro (divergência de hardware). Sai mesmo com os alarmes de limite do sensor desligados, mas não com a falha desativada (`err_off`) nem em manutenção.
- **`alarm_sil` e `err_sil`.** Alguém tocou em **Silenciar 120s** no aviso de alarme do painel. O código depende do alarme ativo no sensor: falha ou limite. O registro não traz `user`.
- **`alarm_off` e `err_off`.** Alguém tocou em **Desativar** no aviso de alarme do painel, depois do PIN, com a permissão [PERM_ALARM_BLOCK]{.perm}. Em falha, isso desativa só o alarme de falha (`err_off`), e os limites continuam armados; em limite, desliga os alarmes de limite do sensor (`alarm_off`).
- **`alarm_off` e `alarm_on`** também saem quando alguém desliga ou religa os **Alarmes** do sensor no menu do painel, com a mesma permissão.
- **`alarm_lim`.** Alguém gravou o editor de **Limites de alarme** do sensor no painel, com a permissão [PERM_ALARM_LIMITS]{.perm}. Sai um registro por canal cujo par de limites mudou.
- **`maint_on` e `maint_off`.** Uma janela de manutenção abriu ou fechou, pelo painel, com a permissão [PERM_MAINT]{.perm}, ou pela interface web ([capítulo 7](#cap-07)). A saída por prazo gera `maint_off` sem `user`.

::: atencao
**Algumas ações não geram registro.** Mudar limites, ligar ou desligar alarmes pela interface web ou pela API não gera `alarm_lim`, `alarm_on` nem `alarm_off`: só as ações feitas no painel geram. Mudar o prazo de uma janela de manutenção já aberta também não gera registro. Para conhecer o estado atual, consulte a API ([capítulo 26](#cap-26)).
:::

::: atencao
**A volta ao normal não gera registro.** Quando o valor volta para dentro dos limites, ou o sensor volta a responder, a linha não manda nada. Para saber que o valor normalizou, use a telemetria de medições.
:::

### Manutenção evita o falso alarme {#cap-22-manutencao}

Enquanto a janela de manutenção de um sensor está aberta, ele não gera `alarm` nem `err`. O coletor recebe exatamente dois registros, `maint_on` na entrada e `maint_off` na saída, e sabe que o silêncio entre eles é intencional. Ao sair da janela com o valor ainda fora da faixa, o sensor volta a alarmar e gera um `alarm` novo.

### Depois de um reinício {#cap-22-reinicio}

O aparelho volta sem memória das bordas. Depois de cada reinício, e também ao ligar a linha:

- um canal que já está fora da faixa gera um `alarm` novo;
- um sensor que já está em falha gera um `err` novo;
- uma janela de manutenção ainda aberta gera um `maint_on` novo, sem `user`.

A fila da RAM se perdeu no reinício, então esses registros refazem o estado para o coletor. Trate-os como a confirmação do estado, não como eventos novos.

## O registro {#cap-22-registro}

| Campo | Sempre? | Conteúdo |
|---|---|---|
| `ts` | Sim | A hora do evento, em segundos desde 01/01/1970, UTC |
| `id` | Sim | A letra da grandeza e o identificador do sensor, como `tSTM0001` |
| `seq` | Sim | O número de sequência desde o último reinício. É a chave da confirmação |
| `val` | Só em `alarm` | O valor que saiu da faixa |
| `alarm`, `err` ou `maint` | Um deles | O código do evento |
| `lo` e `hi` | Só em `alarm_lim` | Os limites novos, mínimo e máximo |
| `until` | Só em `maint_on` | A hora prevista do fim da janela, com precisão de 1 minuto |
| `user` | Quando houve uma pessoa | O nome da conta que agiu |

- **`id`.** A letra vem da grandeza: `t` temperatura, `u` umidade, `p` pressão, `l` luminosidade. Num evento de limite, é a grandeza que saiu da faixa, e num `alarm_lim`, a que mudou. Nos demais códigos, é a primeira grandeza do sensor: `t` para todos os tipos atuais. Sem identificador, vem o número do slot, como `t3`.
- **Números.** `val`, `lo` e `hi` usam 2 casas para temperatura e 1 casa para as demais grandezas.
- **`seq`.** Começa em 1 a cada reinício e só cresce, até 65.535, quando volta a 1. Não use o `seq` sozinho como chave: use (`X-SIMUT-Uid`, `ts`, `seq`).
- **`user`.** O nome fica gravado no registro no momento da ação. Se a conta for apagada, ou o nome for reaproveitado, o registro que já está na fila continua com o nome de quem agiu.
- **Campos ausentes somem.** Um campo que não se aplica ao código não aparece: não vem vazio nem nulo.

## Formatos {#cap-22-formatos}

Os exemplos usam seis eventos de um aparelho com dois sensores: um DS18B20 com o identificador `STM0001`, no slot 0, e um BME280 com o identificador `BME28001`, no slot 1.

1. A temperatura do slot 0 passou do máximo, com 8,52 °C.
2. Alguém tocou em **Silenciar 120s**.
3. O BME280 parou de responder.
4. A conta `joao` abriu 2 h de manutenção no slot 0.
5. A conta `joao` mudou os limites de umidade do slot 1 para 30,0 e 70,0.
6. A janela do slot 0 venceu.

### JSON {#cap-22-json}

O corpo é um array, com um objeto por registro, montado pelo modelo **2. Linha** da linha de alarmes. Com o modelo de fábrica:

```json
[{"ts":1790168700,"id":"tSTM0001","val":8.52,"alarm":"alarm","seq":1},{"ts":1790168760,"id":"tSTM0001","alarm":"alarm_sil","seq":2},{"ts":1790169000,"id":"tBME28001","err":"err","seq":3},{"ts":1790169300,"id":"tSTM0001","maint":"maint_on","until":1790176500,"user":"joao","seq":4},{"ts":1790169420,"id":"uBME28001","alarm":"alarm_lim","lo":30.0,"hi":70.0,"user":"joao","seq":5},{"ts":1790176500,"id":"tSTM0001","maint":"maint_off","seq":6}]
```

O modelo de fábrica da linha é:

```text
{"ts":{TS},"id":"{ID}","val":{val},"alarm":{alarm},"err":{err},"maint":{maint},"lo":{lo},"hi":{hi},"until":{until},"user":{user},"seq":{seq}}
```

::: atencao
**No formato JSON, o aparelho também usa o modelo 2. Linha.** A página só mostra os modelos no formato **Dinâmico**, mas um modelo editado continua valendo no JSON. Se o JSON sair diferente do exemplo acima, confira o **2. Linha** no formato **Dinâmico**.
:::

### CSV {#cap-22-csv}

Um cabeçalho fixo e uma linha por registro, separados por `;`, cada linha terminada por `\n`:

```text
seq;ts;id;v;user;lo;hi;until
1;1790168700;tSTM0001;8.52;;;;
2;1790168760;tSTM0001;alarm_sil;;;;
3;1790169000;tBME28001;err;;;;
4;1790169300;tSTM0001;maint_on;joao;;;1790176500
5;1790169420;uBME28001;alarm_lim;joao;30.0;70.0;
6;1790176500;tSTM0001;maint_off;;;;
```

A coluna `v` traz o valor num `alarm` e o código em todos os outros registros. As colunas `user`, `lo`, `hi` e `until` ficam vazias quando não se aplicam. O CSV não usa os modelos.

### Dinâmico {#cap-22-dinamico}

O corpo é o modelo **1. Global**, com `{DATA}` trocado pelas linhas unidas pelo **3. Separador**. Com os modelos de fábrica:

```json
{"dev":"simut","mac":"28:cd:c1:0a:1b:2c","alarms":[{"ts":1790168700,"id":"tSTM0001","val":8.52,"alarm":"alarm","seq":1},{"ts":1790168760,"id":"tSTM0001","alarm":"alarm_sil","seq":2},{"ts":1790169000,"id":"tBME28001","err":"err","seq":3},{"ts":1790169300,"id":"tSTM0001","maint":"maint_on","until":1790176500,"user":"joao","seq":4},{"ts":1790169420,"id":"uBME28001","alarm":"alarm_lim","lo":30.0,"hi":70.0,"user":"joao","seq":5},{"ts":1790176500,"id":"tSTM0001","maint":"maint_off","seq":6}]}
```

O **1. Global** aceita `{DEV}`, `{MAC}` e `{DATA}`, como na telemetria. O **2. Linha** aceita:

| Marcador | Vira | Forma composta |
|---|---|---|
| `{TS}` | A hora do evento | Não |
| `{ID}` | Letra da grandeza e identificador, sem aspas | Não |
| `{HWID}` | O identificador do sensor, sem aspas | Não |
| `{SLOT}` | O número do slot, de 0 a 15 | Não |
| `{CH}` | A letra da grandeza: `t`, `u`, `p` ou `l` | Não |
| `{VAL}` ou `{val}` | O valor, só em `alarm` | Sim |
| `{ALARM}` ou `{alarm}` | O código de limite, com aspas | Sim |
| `{ERR}` ou `{err}` | O código de falha, com aspas | Sim |
| `{MAINT}` ou `{maint}` | O código de manutenção, com aspas | Sim |
| `{LO}` ou `{lo}`, `{HI}` ou `{hi}` | Os limites novos, só em `alarm_lim` | Sim |
| `{UNTIL}` ou `{until}` | O fim previsto, só em `maint_on` | Sim |
| `{USER}` ou `{user}` | O nome da conta, com aspas | Sim |
| `{SEQ}` ou `{seq}` | O número de sequência | Sim |

**A forma composta** é uma chave com o mesmo nome do marcador, escrito do mesmo jeito, seguida do marcador sem aspas: `"val":{val}`, `"ALARM":{ALARM}`, `"user":{user}`. Quando o marcador não tem valor naquele registro, a chave inteira some. Depois de montar a linha, o aparelho limpa as vírgulas que sobraram.

Fora da forma composta, um marcador sem valor vira texto vazio, e não `null`, como na telemetria. `"valor":{VAL}` num registro de falha produz `"valor":,`, que é JSON inválido. Use a forma composta para todo campo que pode faltar.

Os códigos já vêm com aspas: escreva `"alarm":{alarm}`, e não `"alarm":"{alarm}"`.

::: nota
**A página mostra só parte dos marcadores.** O quadro de tags da página lista `{TS}`, `{ID}`, `{HWID}`, `{SLOT}`, `{CH}`, `{VAL}`, `{ALARM}`, `{ERR}` e `{SEQ}`. O aparelho também aceita `{MAINT}`, `{LO}`, `{HI}`, `{UNTIL}` e `{USER}`, mas a **Prévia ao Vivo** não os conhece e os mostra como texto. A prévia do CSV também está desatualizada: mostra 4 colunas, e o aparelho envia 8.
:::

::: atencao
**Modelos próprios antigos não têm os campos novos.** A manutenção (`{MAINT}`) entrou na versão 23 do formato de configuração, e `{LO}`, `{HI}`, `{UNTIL}` e `{USER}` na versão 24. Um modelo próprio escrito antes disso envia `maint_on`, `maint_off` e `alarm_lim` sem esses campos. Um modelo que era exatamente um dos de fábrica antigos foi trocado pelo novo na atualização; um modelo editado foi mantido como estava.
:::

## Entrega por HTTP {#cap-22-http}

O aparelho faz um POST com a fila inteira, no máximo 64 registros, para:

- o **Caminho HTTP** da linha, se estiver preenchido;
- senão, o **Endpoint** da telemetria seguido de `/alarm`. Com o **Endpoint** `/api/telemetria`, o caminho é `/api/telemetria/alarm`; com o de fábrica, `/api.php/alarm`.

A requisição tem a mesma forma da telemetria ([capítulo 21](#cap-21-anatomia)): mesmos cabeçalhos de identidade, mesma chave de acesso, `Content-Type` conforme o formato da linha (`application/json`, `text/csv` ou `text/plain`).

| O coletor responde | O aparelho faz | Evento |
|---|---|---|
| 200 a 299 | Tira da fila todos os registros daquele corpo | 550 e 552 |
| Outro código | Mantém a fila e tenta de novo em 15 s | 551, com o código |
| Nada em 4 s, ou erro de conexão | Mantém a fila e tenta de novo em 15 s | 551, com o contexto negativo |

- A linha de alarmes não tem espera crescente: um coletor fora do ar recebe uma tentativa a cada 15 s, e mais uma a cada evento novo.
- Um coletor que responde 2xx sem gravar perde os eventos: depois da confirmação, eles não voltam.
- Com pouca memória livre, o aparelho pode mandar só uma parte da fila; o resto vai no envio seguinte.

::: atencao
**Separe os caminhos.** Se o **Caminho HTTP** for igual ao **Endpoint**, medições e eventos chegam no mesmo endereço, e o coletor só distingue um do outro pela forma: um registro de alarme tem `id` e `seq`; um de medição, não. Deixe o **Caminho HTTP** vazio ou use um caminho próprio.
:::

## Entrega por MQTT {#cap-22-mqtt}

Com o **Tópico** `simut/data`:

| Tópico | Quem publica | Conteúdo |
|---|---|---|
| `simut/data/alarm` | O aparelho | A fila inteira, numa mensagem, com QoS 0, não retida |
| `simut/data/alarm/ack` | O seu coletor | A confirmação |

A publicação não confirma nada: com QoS 0, o broker não avisa o aparelho. Os registros continuam na fila até o coletor publicar a confirmação:

```json
{"seq":[1,2,3]}
```

- **Publique no tópico da mensagem recebida, seguido de `/ack`.** O aparelho assina esse tópico com QoS 1, a cada conexão.
- **Liste os `seq` que você gravou.** O aparelho tira da fila os registros com esses números e ignora os que não conhece. Uma mensagem de confirmação vale para até 64 números.
- **Escreva a lista compacta, com números sem aspas.** O aparelho lê a primeira lista entre colchetes da mensagem e para no primeiro caractere inesperado. Aspas em volta dos números, ou uma quebra de linha logo depois de um número, cortam a leitura ali.
- **Nunca publique a confirmação como retida.** O `seq` recomeça em 1 a cada reinício. Uma confirmação retida seria entregue de novo depois do reinício e apagaria registros novos com os mesmos números.
- **Confirme em menos de 15 s.** Enquanto a confirmação não chega, o aparelho publica a fila inteira de novo a cada 15 s, e o coletor recebe os mesmos registros repetidos.

::: perigo
**No MQTT, a linha de alarmes depende da telemetria de medições ligada.** O aparelho só lê as mensagens que chegam do broker enquanto a telemetria está ligada, com o **Lote mínimo** acima de 0. Com a telemetria desligada, as confirmações nunca são lidas: a fila não esvazia, é publicada de novo a cada 15 s e, cheia, passa a recusar eventos novos.
:::

::: atencao
**No MQTT, a fila inteira precisa caber em cerca de 2 KB.** O cliente MQTT do aparelho começa com um espaço de 2.048 bytes por mensagem. Só a publicação de um lote grande de medições aumenta esse espaço; a linha de alarmes não o aumenta. Com o modelo de fábrica, cada registro ocupa até cerca de 120 bytes. Uma fila maior que isso não é publicada: o aparelho registra o evento 551 com o contexto 0 a cada tentativa, e, sem publicação, o coletor não tem o que confirmar. Com o transporte MQTT, use um **Tamanho da fila (RAM)** de até 16 e confirme rápido.
:::

Vários aparelhos no mesmo broker precisam de tópicos diferentes. Com o mesmo **Tópico**, eles dividiriam o tópico de confirmação, e a confirmação de um tiraria da fila do outro os registros com os mesmos números.

## A fila {#cap-22-fila}

- **Capacidade.** O **Tamanho da fila (RAM)**, de 1 a 64; de fábrica, 32.
- **Fila cheia.** O evento novo é recusado, e os mais antigos ficam. O aparelho registra o evento 553, **Estouro da fila de alarmes**, com o total recusado desde o último reinício. O registro mais antigo é o que mais provavelmente ainda descreve um alarme em curso, e descartá-lo esconderia um alarme nunca confirmado.
- **Diminuir a capacidade** com a fila cheia descarta os registros mais antigos até caber.
- **Reinício.** A fila vive na RAM: um reinício perde tudo o que não foi confirmado. Os registros de estado depois do reinício ([Depois de um reinício](#cap-22-reinicio)) refazem o que ainda vale.
- **Linha desligada.** Com a linha desligada, nada entra na fila e nada sai. O que já estava nela volta a sair quando a linha é religada.
- **Sinal e memória.** Como na telemetria, nada sai com o sinal Wi-Fi em −78 dBm ou abaixo, com menos de 24 KB livres (com TLS) ou 14 KB (sem TLS), ou enquanto o painel está num menu. A fila espera.

No Air, os alarmes só são avaliados com o aparelho acordado (M0). Num despertar do ciclo (M1), ele lê os sensores e grava o histórico, mas não confere limites nem gera eventos da linha de alarmes. E cada despertar termina num reinício do processador, então a fila, que vive na RAM, não atravessa o sono.

## Os campos {#cap-22-campos}

Os campos ficam no bloco **Payload de Alarmes — 2ª Linha de Telemetria** (*Alarm Payload — 2nd Telemetry Line*), no fim da página **Telemetria** ([capítulo 21](#cap-21-pagina)). Nenhum deles exige reinício.

| Campo | Chave na API | Faixa | De fábrica |
|---|---|---|---|
| **Habilitar linha de telemetria de alarmes** (*Enable alarm telemetry line*) | `a_en` | Ligado ou desligado | Desligado |
| **Formato** (*Format*) | `a_mode` | **JSON** = 0; **CSV** = 1; **Dinâmico** = 2 | JSON |
| **Tamanho da fila (RAM)** (*Queue size (RAM)*) | `a_qmax` | 1 a 64 | 32 |
| **Caminho HTTP** (*HTTP path*) | `a_path` | Até 31 caracteres; vazio = **Endpoint** + `/alarm` | Vazio |
| **1. Global** (*1. Global Template (The Envelope)*) | `a_glob` | Até 255 caracteres | `{"dev":"{DEV}","mac":"{MAC}","alarms":[{DATA}]}` |
| **2. Linha** (*2. Row Template (Single Alarm)*) | `a_line` | Até 511 caracteres | O modelo de fábrica ([JSON](#cap-22-json)) |
| **3. Separador** (*3. Separator*) | `a_sep` | Até 7 caracteres | `,` |

- O **Formato** da linha de alarmes é independente do **Formato** da telemetria.
- O texto do bloco mostra **Pendentes:** (*Pending:*) e o tamanho da fila no momento em que a página abriu. Para atualizar o número, recarregue a página.
- Os três modelos só aparecem no formato **Dinâmico**, mas o **2. Linha** também vale no JSON ([JSON](#cap-22-json)).
- A linha precisa de um **IP Servidor** preenchido na telemetria; sem ele, nada sai.

::: {.figura #fig-22-bloco-alarmes tipo="web" arquivo="22-bloco-alarmes.png" captura="rota /telemetry; largura 1280; sessão admin; rolar até o bloco Payload de Alarmes — 2ª Linha de Telemetria; Habilitar linha de telemetria de alarmes ligado; Formato JSON; Tamanho da fila (RAM) 32; Caminho HTTP vazio; texto Pendentes: 0 visível"}
Legenda: o bloco da linha de alarmes, no fim da página **Telemetria**. Note o número de **Pendentes** no texto do bloco e o **Caminho HTTP** vazio, que usa o **Endpoint** da telemetria seguido de `/alarm`.
:::

## Códigos do log de eventos {#cap-22-eventos}

| Código | Evento | Quando |
|---|---|---|
| 549 | **Linha de telemetria de alarmes ligada** | Início, com a linha ligada; o contexto é a capacidade da fila |
| 550 | **Payload de alarmes enviado** | HTTP aceito, ou MQTT publicado; o contexto é o número de registros |
| 551 | **Falha no envio de alarmes** | Resposta fora de 2xx (contexto = código), erro de conexão (contexto negativo) ou publicação MQTT recusada (contexto = estado do cliente MQTT) |
| 552 | **Recebimento de alarmes confirmado** | Registros tirados da fila; o contexto é quantos. Também ao assinar o tópico de confirmação, com o contexto 0 |
| 553 | **Estouro da fila de alarmes** | Evento recusado com a fila cheia; o contexto é o total recusado |

Como na telemetria, o log de eventos guarda as transições da família da linha de alarmes, e não cada repetição ([capítulo 16](#cap-16)).

Não confunda com os eventos 470, **Alarme disparado**, e 471, **Alarme zerado**: eles registram o alarme do próprio aparelho, com som e tela, e não a linha de alarmes.

## Receptores {#cap-22-receptores}

### Por HTTP {#cap-22-rx-http}

O receptor em Python do [capítulo 21](#cap-21-rx-python) já atende a linha de alarmes em JSON, no caminho `/api/telemetria/alarm`. Ele grava cada registro com a chave (`uid`, `ts`, `seq`, `id`), ignora as duplicatas e só responde 204 depois de gravar. O 204 é a confirmação: o aparelho tira da fila todos os registros daquele corpo.

### Por MQTT {#cap-22-rx-mqtt}

Este assinante grava os eventos e publica a confirmação. Ele usa a biblioteca paho-mqtt, versão 2 ou mais nova (`pip install "paho-mqtt>=2"`).

```python
#!/usr/bin/env python3
"""Coletor da linha de alarmes do SIMUT por MQTT: grava e confirma."""
import json
import sqlite3

import paho.mqtt.client as mqtt

BROKER = "coletor.exemplo.com.br"
ASSINATURA = "simut/data/alarm"      # varios aparelhos: "fabrica/+/data/alarm"

banco = sqlite3.connect("alarmes.db")
banco.execute("""CREATE TABLE IF NOT EXISTS alarme (
    topico TEXT NOT NULL, ts INTEGER NOT NULL, seq INTEGER NOT NULL, id TEXT NOT NULL,
    codigo TEXT, usuario TEXT, registro TEXT NOT NULL,
    PRIMARY KEY (topico, ts, seq, id))""")


def ao_conectar(cliente, dados, flags, motivo, propriedades):
    cliente.subscribe(ASSINATURA, qos=1)


def ao_receber(cliente, dados, msg):
    try:
        registros = json.loads(msg.payload)
        if not isinstance(registros, list):
            raise ValueError("esperava um array: use o formato JSON")
        with banco:                              # grava tudo ou nada
            for r in registros:
                codigo = r.get("alarm") or r.get("err") or r.get("maint")
                banco.execute(
                    "INSERT OR IGNORE INTO alarme VALUES (?, ?, ?, ?, ?, ?, ?)",
                    (msg.topic, r["ts"], r["seq"], r["id"], codigo, r.get("user"),
                     json.dumps(r)))
    except (ValueError, KeyError, TypeError, AttributeError) as erro:
        print(f"alarme recusado em {msg.topic}: {erro}")
        return                                   # sem confirmacao: volta em 15 s
    confirmacao = json.dumps({"seq": [r["seq"] for r in registros]},
                             separators=(",", ":"))
    cliente.publish(msg.topic + "/ack", confirmacao, qos=1, retain=False)


cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="coletor-alarmes")
cliente.username_pw_set("coletor", "senha-do-coletor")
cliente.on_connect = ao_conectar
cliente.on_message = ao_receber
cliente.connect(BROKER, 1883)
cliente.loop_forever()
```

Com vários aparelhos, cada um com o seu **Tópico**, assine com um curinga, como `fabrica/+/data/alarm`: o programa responde sempre no tópico de confirmação do aparelho que mandou. A chave de gravação usa o tópico porque, no MQTT, é ele que identifica o aparelho.

## Testar a linha {#cap-22-testar}

Para testar o coletor antes de ligar um aparelho, mande o que o aparelho mandaria.

Por HTTP:

```bash
curl -i http://coletor.exemplo.com.br:8080/api/telemetria/alarm \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer troque-este-segredo' \
  -H 'X-SIMUT-Uid: E6614C311B7A2F2D' \
  -H 'X-SIMUT-Ver: 2.7.1' \
  -H 'X-SIMUT-Env: release' \
  -H 'X-SIMUT-Cfg: 3F2A91C4' \
  --data-binary '[{"ts":1790168700,"id":"tSTM0001","val":8.52,"alarm":"alarm","seq":1}]'
```

Por MQTT, com o assinante acima rodando, publique um evento como se fosse o aparelho e confira a confirmação numa segunda janela de comandos:

```bash
mosquitto_sub -h coletor.exemplo.com.br -u coletor -P 'senha-do-coletor' -v -t 'simut/data/alarm/ack'
mosquitto_pub -h coletor.exemplo.com.br -u coletor -P 'senha-do-coletor' -t 'simut/data/alarm' \
  -m '[{"ts":1790168700,"id":"tSTM0001","val":8.52,"alarm":"alarm","seq":1}]'
```

A janela do `mosquitto_sub` deve mostrar `simut/data/alarm/ack {"seq":[1]}`.

Depois, com o aparelho:

1. Na página **Telemetria**, ligue **Habilitar linha de telemetria de alarmes** e use **Aplicar agora**. No MQTT, confira também que o **Lote mínimo (registros)** está acima de 0.
2. Provoque um evento. Três jeitos simples:
   - na página **Alarmes e Sons**, ponha o limite máximo de um sensor abaixo da leitura atual: sai um `alarm` em segundos;
   - abra uma janela de manutenção de poucos minutos num sensor: sai um `maint_on` agora e um `maint_off` no fim do prazo ([capítulo 7](#cap-07));
   - desconecte um sensor: sai um `err`.
3. Confira o registro no coletor.
4. Na página **Histórico e Logs**, em **Eventos do Sistema**, confira os eventos 550 e 552.
5. Recarregue a página **Telemetria** e confira **Pendentes: 0**.
6. Desfaça o que fez no passo 2.

Nas imagens com o console completo ([capítulo 14](#cap-14)), `alarm show` mostra a configuração e a fila, `alarm dump` imprime o próximo corpo e dispara o envio, e `alarm flush` esvazia a fila sem confirmar.

## Solução de problemas {#cap-22-problemas}

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Nenhum evento chega | Linha desligada, ou **IP Servidor** vazio | Ligue **Habilitar linha de telemetria de alarmes** e confira o servidor |
| Evento 551 com 404 | Caminho errado | Confira o **Caminho HTTP**; vazio, ele vale **Endpoint** + `/alarm` |
| Os mesmos eventos chegam a cada 15 s | O coletor não confirma: HTTP fora de 2xx, ou falta a mensagem de confirmação no MQTT | Responda 2xx depois de gravar, ou publique `{"seq":[...]}` em `<tópico>/alarm/ack` |
| MQTT: a fila nunca esvazia, embora o coletor confirme | Telemetria de medições desligada; o aparelho não lê as confirmações | Ponha o **Lote mínimo** acima de 0 |
| MQTT: evento 551 com o contexto 0 a cada tentativa | A fila não cabe numa mensagem de 2 KB | Use um **Tamanho da fila (RAM)** de até 16 e confirme rápido; em último caso, reinicie o aparelho, o que esvazia a fila |
| MQTT: registros somem da fila sem terem chegado | Confirmação retida no broker, ou dois aparelhos no mesmo **Tópico** | Publique a confirmação sem retenção e apague a retida; dê um **Tópico** a cada aparelho |
| Evento 553 | Fila cheia: o coletor não confirma há tempo | Resolva a confirmação; aumente o **Tamanho da fila (RAM)** se o volume for real |
| Eventos se perdem depois de uma queda de energia | A fila vive na RAM | Esperado; confirme rápido para a fila ficar curta |
| `alarm_lim` não chega quando o limite muda pela web | Só o painel gera `alarm_lim`, `alarm_on` e `alarm_off` | Consulte o estado pela API ([capítulo 26](#cap-26)) |
| Um novo `alarm` ou `maint_on` chega depois de um reinício | O aparelho refaz o estado das bordas ao iniciar | Trate como confirmação de estado |
| `maint_on` e `alarm_lim` chegam sem `until`, `lo`, `hi` ou `user` | Modelo próprio anterior à configuração versão 23 | Acrescente `"maint":{maint}`, `"lo":{lo}`, `"hi":{hi}`, `"until":{until}` e `"user":{user}` ao **2. Linha** |
| JSON inválido com `"valor":,` | Marcador fora da forma composta, sem valor naquele registro | Use a forma composta, como `"val":{val}` |
| O JSON da linha não é o do exemplo | O **2. Linha** foi editado e vale também no formato JSON | Confira o **2. Linha** no formato **Dinâmico** |
