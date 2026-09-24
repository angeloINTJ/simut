# Telemetria de medições {#cap-21}

Este capítulo explica como o SIMUT envia as medições do histórico para um coletor: cada campo da página **Telemetria**, quando e como o aparelho envia, o formato exato de cada modo e o que ele faz com cada resposta. Ele é para quem configura o aparelho e para quem escreve o coletor que vai receber os dados.

## Como a telemetria funciona {#cap-21-como}

O aparelho grava uma medição por intervalo do histórico: um registro com a hora e o valor de cada canal ativo ([capítulo 15](#cap-15)). A telemetria envia esses registros, em lotes, para o coletor que você indicar.

Três ideias explicam todo o comportamento:

- **O registro nasce na flash.** A telemetria só lê o histórico. Se o coletor está fora do ar, nada se perde: os registros esperam na flash e saem quando ele volta.
- **Um cursor marca o que já foi entregue.** O cursor é a hora do registro mais novo que o coletor confirmou. Cada lote leva os registros com hora depois do cursor.
- **Só uma confirmação avança o cursor.** No HTTP, a confirmação é uma resposta 2xx. No MQTT, é a publicação aceita com a conexão ainda viva. Qualquer outra coisa deixa o cursor onde está, e o mesmo trecho sai de novo.

A telemetria vem desligada de fábrica.

::: {.figura #fig-21-cursor tipo="diagrama" arquivo="21-cursor.png" captura="Linha do tempo horizontal com os registros do histórico como pequenos quadrados. Uma marca vertical 'cursor' separa os registros já confirmados (cinza, à esquerda) dos pendentes (coloridos, à direita). Um colchete agrupa os primeiros pendentes como 'lote (até o Lote máximo)'. Uma seta leva o lote ao 'coletor'; a volta '2xx' move o cursor para o fim do lote; uma volta alternativa 'falha' deixa o cursor parado e aponta para 'espera e reenvia'. À direita, o bloco da hora aberta, ainda na RAM, também entra no lote."}
Legenda: o cursor separa o que o coletor já confirmou do que ainda falta. Só uma confirmação o move.
:::

## A página Telemetria {#cap-21-pagina}

Abra o menu da interface web e escolha **Telemetria** (*Telemetry*). A página exige a permissão **Sistema** [PERM_SYS_CONFIG]{.perm}.

A página tem três blocos:

- **Telemetria** (*Telemetry Engine*): transporte, servidor, acesso e lotes;
- **Construtor** (*Payload Builder*): o formato do corpo;
- **Payload de Alarmes — 2ª Linha de Telemetria** (*Alarm Payload — 2nd Telemetry Line*): a linha de alarmes, descrita no [capítulo 22](#cap-22).

As edições ficam pendentes até você usar um dos botões de gravação: **Testar**, **Aplicar agora** ou **Salvar e reiniciar**. O [capítulo 5](#cap-05) explica os três. A coluna **Reinício** das tabelas abaixo diz se o campo exige reiniciar o aparelho.

Os botões **Enviar agora** e **Resetar cursor de envio** são diferentes: eles agem na hora, sobre o aparelho em funcionamento, e não passam pelos botões de gravação.

::: {.figura #fig-21-pagina-http tipo="web" arquivo="21-pagina-http.png" captura="rota /telemetry; largura 1280; sessão admin; imagem release; Transporte HTTP(S); IP Servidor coletor.exemplo.com.br; Porta 8080; TLS desligado; Endpoint /api/telemetria; API Key preenchida (mostrada mascarada); Lote mínimo 1; Lote máximo 250; Formato JSON"}
Legenda: a página **Telemetria** com o transporte HTTP. Note a chave mascarada no campo **API Key** e os botões **Enviar agora** e **Resetar cursor de envio** logo abaixo dos lotes.
:::

### Transporte e servidor {#cap-21-transporte}

| Campo | Chave na API | Faixa | De fábrica | Reinício |
|---|---|---|---|---|
| **Transporte** (*Transport Protocol*) | `t_transport` | **HTTP(S)** = 0; **MQTT(S)** = 1 | HTTP(S) | Sim |
| **IP Servidor** (*Server Domain / IP*) | `t_srv` | Endereço IP ou nome, até 63 caracteres | Vazio | Não (veja a nota) |
| **Porta** (*Port*) | `t_port` | 1 a 65535 | 80 | Não (veja a nota) |
| **Usar TLS / SSL** (*Use TLS / SSL*) | `t_sec` | Ligado ou desligado | Desligado | Sim |
| **Endpoint** (*Endpoint Path (URL)*) | `t_path` | Até 31 caracteres | `/api.php` | Não |
| **API Key** (*API Key / Token*) | `t_key` | Até 63 caracteres | Vazio | Não |

- **Transporte.** Escolhe entre POST HTTP e publicação MQTT. O TLS vem do campo **Usar TLS / SSL**: HTTP com TLS é HTTPS, e MQTT com TLS é MQTTS. Com o MQTT escolhido, o rótulo do TLS vira **Usar MQTTS (TLS)**, e os campos **Endpoint** e **API Key** dão lugar aos campos do MQTT.
- **IP Servidor.** Apesar do rótulo, aceita um nome, como `coletor.exemplo.com.br`. Sem servidor, nada é enviado.
- **Porta.** Continua em 80 quando você troca para MQTT. Ajuste-a: o usual é 80 para HTTP, 443 para HTTPS, 1883 para MQTT e 8883 para MQTTS.
- **Endpoint.** O caminho do POST, começando com `/`, como `/api/telemetria`. O endereço completo é `http://` ou `https://`, o servidor, `:`, a porta e o caminho.
- **API Key.** A chave de acesso que o aparelho põe em todo POST ([A chave de acesso](#cap-21-credencial)).

::: nota
**Servidor e porta no MQTT.** Esses dois campos se aplicam sem reiniciar no HTTP. No MQTT, o cliente só lê a porta ao iniciar, e o servidor novo só vale na próxima reconexão. Depois de mudar servidor ou porta no MQTT, use **Salvar e reiniciar**.
:::

::: nota
**Por que ligar o TLS exige reinício.** O aparelho lê o arquivo `/cert.pem` uma única vez, ao iniciar. Ligar o TLS sem reiniciar deixaria o aparelho tentando TLS sem o certificado carregado.
:::

### A chave de acesso {#cap-21-credencial}

O valor do campo **API Key** vira um cabeçalho HTTP em todo POST de telemetria e da linha de alarmes. O aparelho tira os espaços das pontas e escolhe a forma assim:

| Você digita | O aparelho envia |
|---|---|
| `troque-este-segredo` | `Authorization: Bearer troque-este-segredo` |
| `X-Api-Key: 9f2c41d7` | `X-Api-Key: 9f2c41d7` |
| `Authorization: Basic c2ltdXQ6c2VncmVkbw==` | `Authorization: Basic c2ltdXQ6c2VncmVkbw==` |
| Nada | Nenhum cabeçalho de acesso |

A regra: se o valor tem `:` depois do primeiro caractere, o que vem antes é o nome do cabeçalho e o que vem depois é o valor. Sem `:`, o valor vai como `Bearer`. Os nomes `Connection`, `User-Agent` e `Host` não podem ser usados: o aparelho os ignora.

Depois de gravada, a chave aparece mascarada: os 4 primeiros caracteres seguidos de `***`, ou só `***` para chaves de até 4 caracteres. Enquanto o campo mostrar a máscara, o aparelho mantém a chave gravada. Para trocar, apague o campo e digite a nova. Para remover, apague o campo e grave.

A chave de acesso não vale para o MQTT, que usa **Usuário** e **Senha**.

::: atencao
**A chave viaja em texto claro sem TLS.** Qualquer máquina que enxergue o tráfego lê a chave. Numa rede que você não controla, ligue o TLS ([TLS e o arquivo /cert.pem](#cap-21-tls)).
:::

### Campos do MQTT {#cap-21-campos-mqtt}

Aparecem com **Transporte** em **MQTT(S)**.

| Campo | Chave na API | Faixa | De fábrica | Reinício |
|---|---|---|---|---|
| **Tópico** (*MQTT Topic*) | `m_topic` | Até 63 caracteres | `simut/data` | Sim |
| **Client ID** | `m_cid` | Até 23 caracteres | Vazio | Sim |
| **Usuário** (*MQTT User*) | `m_user` | Até 31 caracteres | Vazio | Sim |
| **Senha** (*MQTT Password*) | `m_pass` | Até 31 caracteres | Vazio | Sim |
| **QoS** (*QoS Level*) | `m_qos` | Só 0 | 0 | Sim |
| **Keep-Alive** (*Keep-Alive (s)*) | `m_ka` | 10 a 300 s | 60 s | Sim |
| **Reter** (*Retain Message*) | `m_retain` | Ligado ou desligado | Desligado | Sim |
| **Home Assistant Discovery** | `m_had` | Ligado ou desligado | Desligado | Sim |

- **Tópico.** O tópico das medições. Os outros tópicos derivam dele ([MQTT](#cap-21-mqtt)). Em branco, o aparelho usa `simut/data`.
- **Client ID.** Em branco, o aparelho usa `simut_` seguido dos seis últimos dígitos do MAC, como `simut_0a1b2c`.
- **Usuário** e **Senha.** Com o usuário em branco, o aparelho conecta sem usuário e senha. A senha nunca volta para a página: o campo aparece vazio, com o texto *Leave empty to keep*. Deixe-o vazio para manter a senha gravada.
- **QoS.** O aparelho publica sempre com QoS 0. A página só oferece 0, e o aparelho recusa outro valor.
- **Keep-Alive.** O intervalo de vida combinado com o broker.
- **Reter.** Publica as medições como mensagens retidas: o broker guarda a última e a entrega a quem assinar depois.
- **Home Assistant Discovery.** Exige o formato JSON. Está no [capítulo 23](#cap-23).

::: {.figura #fig-21-pagina-mqtt tipo="web" arquivo="21-pagina-mqtt.png" captura="rota /telemetry; largura 1280; sessão admin; Transporte MQTT(S); IP Servidor coletor.exemplo.com.br; Porta 1883; TLS desligado (rótulo Usar MQTTS (TLS)); Tópico fabrica/camara1/data; Client ID vazio; Usuário simut; Senha vazia; QoS 0; Keep-Alive 60; Reter desligado"}
Legenda: com o transporte MQTT, os campos **Endpoint** e **API Key** saem e entram os do broker. Note que a **Porta** não muda sozinha.
:::

### Lote mínimo e lote máximo {#cap-21-lotes}

| Campo | Chave na API | Faixa | De fábrica | Reinício |
|---|---|---|---|---|
| **Lote mínimo (registros)** (*Minimum batch (records)*) | `t_int` | 0 a 20.000 | 0 (desligada) | Não |
| **Lote máximo (registros)** (*Maximum batch (records)*) | `t_bat` | 1 a 250 | 10 | Não |

- **Lote mínimo.** Quantos registros precisam estar esperando para o aparelho transmitir. **0 desliga a telemetria.** Com o valor em 0, a página mostra a faixa **Telemetria desabilitada (lote mínimo = 0). Defina um valor para ativar.**
- **Lote máximo.** O máximo de registros num envio. Uma fila maior sai em vários lotes, um atrás do outro, até acabar.

O ritmo dos registros vem do **Intervalo Histórico (min)**, na página **Configurações** (1 a 1440 min; de fábrica, 1). Com o histórico a cada minuto:

| Lote mínimo | Resultado |
|---|---|
| 1 | Um envio por minuto, com o registro novo |
| 60 | Um envio por hora, com a hora inteira |
| 1440 | Um envio por dia |

Um lote mínimo maior economiza rádio e conexões; um menor entrega mais cedo. Para eventos que não podem esperar, use a linha de alarmes ([capítulo 22](#cap-22)).

::: nota
**Por que o campo se chama `t_int`.** Até a versão 21 do formato de configuração, o campo era um intervalo em milissegundos. Desde a versão 22, é uma quantidade de registros; o nome da chave ficou.
:::

### Enviar agora e Resetar cursor de envio {#cap-21-acoes}

**Enviar agora** (*Send now*) começa um envio na hora, sem esperar o lote mínimo, e zera a espera de uma falha anterior. A página responde **Envio disparado. Acompanhe o resultado no log.** e o aparelho registra o evento 546, **Forçando sync de telemetria**.

- O resultado aparece no log de eventos, não na página ([Códigos do log de eventos](#cap-21-eventos)).
- Com o **Lote mínimo** em 0, **Enviar agora** manda um único lote e para.
- Se o sinal estiver fraco ou outro envio estiver em andamento, nada sai, embora a página diga que o envio foi disparado.

**Resetar cursor de envio** (*Reset send cursor*) apaga o cursor. A página pede confirmação: **Resetar o cursor de telemetria? O dispositivo vai reenviar até 30 dias de histórico no próximo envio. Espere uma rajada de tráfego.** Depois do reset, os envios recomeçam 30 dias antes do registro mais novo. O aparelho registra o evento 303, **Config alterada**, com o texto `Telemetry cursor reset via web`.

Use o reset depois que o coletor perdeu dados, ou ao apontar o aparelho para um coletor novo que precisa do histórico.

::: atencao
**O reset gera duplicatas de propósito.** Tudo dos últimos 30 dias sai de novo, inclusive o que o coletor já tinha. Um coletor idempotente ([Duplicatas e idempotência](#cap-21-duplicatas)) absorve isso sem estragos.
:::

### Construtor {#cap-21-construtor}

| Campo | Chave na API | Faixa | De fábrica | Reinício |
|---|---|---|---|---|
| **Formato** (*Format*) | `t_mode` | **JSON** = 0; **CSV** = 1; **Dinâmico** = 2 | JSON | Não |
| **1. Global** (*1. Global Template (The Envelope)*) | `t_glob` | Até 255 caracteres | `{"dev":"{DEV}","mac":"{MAC}","data":[{DATA}]}` | Não |
| **2. Linha** (*2. Row Template (Single Reading)*) | `t_line` | Até 511 caracteres | `{"ts":{TS},"t0_ID":{t0},"u0_ID":{u0}}` | Não |
| **3. Separador** (*3. Separator*) | `t_sep` | Até 7 caracteres | `,` | Não |

Os três modelos só aparecem, e só valem, no formato **Dinâmico**. As opções do **Formato** têm, em inglês, os nomes *JSON Array (Standard)*, *CSV Raw (Standard)* e *Dynamic Builder (Advanced)*.

A **Prévia ao Vivo** (*Live Preview*) mostra um exemplo montado com valores de demonstração, não com as suas medições. No CSV, a prévia não mostra as 34 colunas que o aparelho envia de verdade ([Formato CSV](#cap-21-csv)).

::: {.figura #fig-21-construtor tipo="web" arquivo="21-construtor.png" captura="rota /telemetry; largura 1280; sessão admin; bloco Construtor com Formato Dinâmico; quadro de tags visível; 1. Global com o modelo de fábrica; 2. Linha com o modelo de dois sensores da seção Exemplos deste capítulo (t0_ID, t1_ID, u1_ID e p1_ID); 3. Separador vírgula; Prévia ao Vivo preenchida"}
Legenda: o construtor no formato **Dinâmico**, com o quadro de tags e a prévia. A prévia usa valores de demonstração.
:::

## Quando o aparelho envia {#cap-21-quando}

1. Cada registro novo do histórico soma 1 ao contador de pendentes, que o **Painel de Controle** mostra em **Registros Pendentes** ([capítulo 13](#cap-13)).
2. Quando os pendentes chegam ao **Lote mínimo**, começa um esvaziamento. Depois de ligar, o primeiro espera pelo menos 8 s.
3. Cada lote leva os registros com hora depois do cursor, até o limite do lote. Isso inclui a hora ainda aberta na RAM: o aparelho não espera o bloco do histórico fechar para enviar.
4. Com a confirmação, o cursor avança até o registro mais novo que o lote levou.
5. O aparelho repete até não sobrar nada e volta a esperar o lote mínimo.

O aparelho não envia:

- com o **Lote mínimo** em 0;
- com o sinal Wi-Fi em −78 dBm ou abaixo;
- durante a espera depois de uma falha ([Falhas e novas tentativas](#cap-21-falhas));
- enquanto o painel está no teclado de PIN ou num menu, e até 5 s depois do último toque na tela;
- com menos de 24 KB de memória livre (com TLS) ou 14 KB (sem TLS). Essa falta de memória conta como falha.

### O tamanho de cada lote {#cap-21-tamanho}

Três limites decidem quantos registros vão num lote, e vale o menor:

- **o Lote máximo** que você configurou;
- **a memória livre.** O aparelho reserva 12 KB (sem TLS) ou 32 KB (com TLS) e conta 350 bytes por registro no JSON e no **Dinâmico**, ou 160 bytes no CSV;
- **o ajuste automático.** Começa no **Lote máximo**, cai pela metade a cada falha, até o mínimo de 10, e cresce 50 % a cada sucesso, até voltar ao **Lote máximo**.

### Um registro inteiro por vez {#cap-21-registro-inteiro}

[main]{.img}

Na `main`, o aparelho monta o corpo um registro inteiro por vez: cada registro entra completo, com o separador e o fechamento garantidos, ou fica para o lote seguinte. O cursor avança só até o registro mais novo que o corpo levou de fato.

Até a v2.7.1, um lote montado com pouca memória livre podia chegar ao coletor como JSON inválido, com registros faltando no fim, e o cursor passava por cima dos registros que ficaram de fora. Medido em 23/09/2026 num Air v2.7.1, esvaziando a fila para um coletor que guardava todo corpo recebido: 68 de 69 lotes chegaram como JSON inválido, cada um com cerca de nove registros a menos.

::: atencao
**Na v2.7.1, recuse corpo inválido.** Responda com um código que não seja 2xx a todo corpo que não for JSON válido: o cursor fica parado, o lote seguinte sai menor e o dado não se perde. Registros pulados em corpo válido não têm como ser detectados pelo coletor; recupere-os pelo histórico do aparelho ([capítulo 15](#cap-15)).
:::

### O ritmo entre lotes {#cap-21-ritmo}

Durante um esvaziamento, quem dita o ritmo é o coletor. O aparelho mede o ciclo inteiro de cada lote (ler, montar, conectar, enviar e fechar) e decide a pausa até o próximo:

| Ciclo do lote | Pausa até o próximo lote |
|---|---|
| Até 0,4 s sem TLS, ou até 2,5 s com TLS | Nenhuma |
| Mais lento que isso | Igual ao próprio ciclo |
| Mais de 25 % pior que a média recente | Dobra a cada lote que piora |

A pausa nunca passa de 10 s. Com o sinal Wi-Fi abaixo de −75 dBm, ela cresce 50 %.

::: nota
**Por que um coletor lento não é castigado.** Um coletor que responde em 0,5 s, mas sempre responde, está dizendo que aguenta o lote; o que ele não aguenta é o ritmo. Por isso o aparelho dá ao coletor tanto tempo quanto ele levou para responder, e só dobra a pausa quando o tempo de resposta piora.
:::

### Falhas e novas tentativas {#cap-21-falhas}

Conta como falha:

- uma resposta HTTP fora da faixa 200 a 299;
- nenhuma resposta em 4 s;
- um erro de conexão, de DNS ou de TLS;
- no MQTT, a conexão recusada pelo broker, a publicação recusada ou a conexão caindo logo depois dela;
- a falta de memória antes do envio.

Depois de uma falha, o cursor não se move, o lote seguinte cai pela metade e o aparelho espera antes de tentar de novo. A espera começa em 5 s e dobra a cada falha seguida, até 300 s. Cada espera varia 25 % para mais ou para menos, para que vários aparelhos não voltem todos no mesmo instante.

| Falha seguida | Espera aproximada |
|---|---|
| 1ª | 5 s |
| 2ª | 10 s |
| 3ª | 20 s |
| 4ª | 40 s |
| 5ª | 80 s |
| 6ª | 160 s |
| 7ª em diante | 300 s |

O primeiro sucesso zera a sequência.

## O que conta como entrega {#cap-21-entrega}

**No HTTP, só um código de 200 a 299 é entrega.** O aparelho lê apenas o código de status: o corpo da resposta é descartado, e a conexão é fechada logo depois.

| O coletor responde | O aparelho faz | Evento |
|---|---|---|
| 200 a 299 | Avança o cursor e segue para o próximo lote | 30, **Telemetria enviada** |
| 1xx ou 3xx | Falha. O aparelho não segue redirecionamentos | 31, **Falha de telemetria**, com o código, e 32 |
| 4xx ou 5xx | Falha; espera e reenvia o mesmo trecho | 31, com o código, e 32 |
| Nada em 4 s | Falha por tempo esgotado | 31, com o contexto −11, e 32 |
| Conexão recusada, nome não resolvido ou TLS recusado | Falha de conexão | 31, com o contexto −1, e 32 |

::: perigo
**Nunca responda 2xx a um lote que você não gravou.** Para o aparelho, 2xx é a única confirmação que existe. Depois dela, aquele trecho não volta mais. Grave primeiro e responda depois, dentro dos 4 s.
:::

Um coletor que redireciona HTTP para HTTPS faz toda tentativa falhar com o código 301 ou 302. Aponte o aparelho direto para o endereço final e, para HTTPS, ligue o TLS.

**No MQTT, a entrega é a publicação aceita.** O aparelho publica com QoS 0, que não tem confirmação do broker. Ele considera o lote entregue quando todas as mensagens foram escritas e a conexão continua viva 60 ms depois. Se a conexão cai nesse intervalo, o cursor não avança e o trecho sai de novo.

## Anatomia do POST HTTP {#cap-21-anatomia}

Um lote de 3 registros, em JSON, para um coletor na porta 8080:

```http
POST /api/telemetria HTTP/1.1
Host: coletor.exemplo.com.br:8080
User-Agent: Pico
Accept-Encoding: identity;q=1,chunked;q=0.1,*;q=0
Connection: keep-alive
Content-Type: application/json
Authorization: Bearer troque-este-segredo
X-SIMUT-Uid: E6614C311B7A2F2D
X-SIMUT-Ver: 2.7.1
X-SIMUT-Env: release
X-SIMUT-Cfg: 3F2A91C4
Content-Length: 265

[{"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4},{"ts":1790168460,"tSTM0001":4.31,"tBME28001":22.84,"uBME28001":55.0,"pBME28001":1013.4},{"ts":1790168520,"tSTM0001":4.28,"tBME28001":22.90,"uBME28001":54.9,"pBME28001":1013.3}]
```

| Parte | Valor |
|---|---|
| Método | Sempre `POST` |
| Caminho | O campo **Endpoint** |
| `Host` | O **IP Servidor**; a porta só aparece quando não é 80 nem 443 |
| `User-Agent`, `Accept-Encoding`, `Connection` | Fixos, da biblioteca HTTP do aparelho |
| `Content-Type` | `application/json` (JSON), `text/csv` (CSV) ou `text/plain` (**Dinâmico**) |
| Chave de acesso | O cabeçalho montado a partir da **API Key**, se houver |
| `X-SIMUT-*` | A identidade do aparelho ([capítulo 20](#cap-20-cabecalhos)) |
| `Content-Length` | O tamanho do corpo. O corpo nunca vai em partes (*chunked*) |
| Corpo | Em UTF-8, sem espaços nem quebras de linha no JSON |

Embora o cabeçalho diga `keep-alive`, o aparelho fecha a conexão logo depois de ler o código de status. Cada lote abre uma conexão nova, e com TLS isso inclui uma negociação completa.

A linha de alarmes usa o mesmo formato de requisição, no caminho dela ([capítulo 22](#cap-22)).

## Formato JSON {#cap-21-json}

O corpo é um array. Cada elemento é um registro: um objeto com `ts` e uma chave por canal medido.

```json
[{"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4},{"ts":1790168460,"tSTM0001":4.31,"tBME28001":22.84,"uBME28001":55.0,"pBME28001":1013.4},{"ts":1790168520,"tSTM0001":4.28,"tBME28001":22.90,"uBME28001":54.9,"pBME28001":1013.3}]
```

O exemplo vem de um aparelho com dois sensores: um DS18B20 com o identificador `STM0001`, no slot 0, e um BME280 com o identificador `BME28001`, no slot 1.

| Chave | Conteúdo | Unidade | Casas decimais |
|---|---|---|---|
| `ts` | Hora do registro, em segundos desde 01/01/1970, UTC | s | 0 |
| `t` + identificador | Temperatura | °C | 2 |
| `u` + identificador | Umidade relativa | % | 1 |
| `p` + identificador | Pressão | hPa | 1 |

Regras das chaves:

- **O identificador é o do sensor**, definido no provisionamento do slot ([capítulo 6](#cap-06)). Sem identificador, a chave usa o número do slot, como `t0` ou `u3`. Na prática, o aparelho dá um identificador a todo sensor ativo ao iniciar.
- **Chave ausente quer dizer "sem leitura" naquele instante.** Não é zero nem nulo: a chave não aparece. Não preencha com 0 o que não veio.
- **Só sensores ativos entram**, na ordem dos slots: a temperatura e depois a umidade de cada um, e a pressão no fim.
- **Cada registro leva uma pressão, no máximo.** A chave usa o identificador do primeiro sensor ativo que mede pressão. Com dois sensores de pressão ativos, o valor enviado é o do último canal de pressão lido do histórico (em geral, o do slot mais alto), mas a chave leva o identificador do primeiro: a pressão sai com o rótulo trocado. Se a telemetria precisa da pressão, use um só sensor de pressão.
- **A ordem das chaves não é contrato.** Leia por nome.

## Formato CSV {#cap-21-csv}

O corpo é uma linha de cabeçalho seguida de uma linha por registro. Toda linha termina com uma quebra de linha (`\n`). O separador é `;` e o separador decimal é o ponto.

```text
timestamp;s0_STM0001;s1_BME28001;s2;s3;s4;s5;s6;s7;s8;s9;s10;s11;s12;s13;s14;s15;h0_STM0001;h1_BME28001;h2;h3;h4;h5;h6;h7;h8;h9;h10;h11;h12;h13;h14;h15;press
1790168400;4.25;22.81;;;;;;;;;;;;;;;;55.2;;;;;;;;;;;;;;;1013.4
1790168460;4.31;22.84;;;;;;;;;;;;;;;;55.0;;;;;;;;;;;;;;;1013.4
1790168520;4.28;22.90;;;;;;;;;;;;;;;;54.9;;;;;;;;;;;;;;;1013.3
```

As colunas são sempre 34, na mesma ordem:

| Coluna | Conteúdo | Casas decimais |
|---|---|---|
| 1 | Hora do registro (epoch, UTC) | 0 |
| 2 a 17 | Temperatura dos slots 0 a 15 | 2 |
| 18 a 33 | Umidade dos slots 0 a 15 | 1 |
| 34 | Pressão | 1 |

- **Célula vazia quer dizer "sem leitura".** Um slot inativo, ou um sensor sem aquela grandeza, deixa a célula vazia.
- **O cabeçalho dá nome aos slots ativos** com o identificador, como `s0_STM0001` e `h0_STM0001`, mesmo quando o sensor não mede umidade. Os demais ficam `s2`, `h2` e assim por diante.
- **Leia por posição, não pelos nomes do cabeçalho.** Os nomes mudam quando um slot é ativado ou o identificador muda; as posições não.

## Formato Dinâmico {#cap-21-dinamico}

O formato **Dinâmico** existe para falar com um receptor que já existe e cujo formato você não pode mudar. Ele monta o corpo com três peças:

- **2. Linha:** o modelo de um registro, repetido para cada registro do lote;
- **3. Separador:** o que vai entre um registro e o próximo;
- **1. Global:** o envelope. O marcador `{DATA}` recebe as linhas unidas pelo separador.

O `Content-Type` é sempre `text/plain`, mesmo quando o resultado é JSON.

### Marcadores {#cap-21-marcadores}

| Marcador | Onde | Vira |
|---|---|---|
| `{DEV}` | **1. Global** | O nome do aparelho |
| `{MAC}` | **1. Global** | O MAC, como `28:cd:c1:0a:1b:2c` |
| `{DATA}` | **1. Global** | As linhas do lote, unidas pelo separador |
| `{TS}` | **2. Linha** | A hora do registro (epoch, UTC) |
| `{DHT_ID}` | **2. Linha** | O `uid` do aparelho, como `E6614C311B7A2F2D` |
| `{t0}` a `{t15}` | **2. Linha** | Temperatura do slot, com 2 casas |
| `{u0}` a `{u15}` | **2. Linha** | Umidade do slot, com 1 casa |
| `{p0}` a `{p15}` | **2. Linha** | Pressão, com 1 casa, só se aquele slot for o sensor de pressão |

Apesar do nome, `{DHT_ID}` não tem relação com sensores DHT: é o número de série da placa. Os marcadores `{tAMB}`, `{uAMB}` e `{pAMB}` não existem mais; use os marcadores numerados. Um `{` que não abre um marcador conhecido é copiado como está, então chaves de JSON no modelo funcionam normalmente.

### As três formas de um marcador de canal {#cap-21-formas}

| Você escreve | Com leitura | Sem leitura |
|---|---|---|
| `"t0_ID":{t0}` | `"tSTM0001":4.25` (a chave vira `t` + identificador) | A chave inteira some |
| `"t0":{t0}` | `"t0":4.25` | A chave inteira some |
| `"camara":{t0}`, ou `{t0}` fora de uma chave | `"camara":4.25` | `"camara":null` |

A forma com `_ID` reproduz as chaves do formato JSON. Depois de montar a linha, o aparelho limpa as vírgulas que sobraram, em sequências como `,,`, `{,`, `[,`, `,}` e `,]`.

Para uma quebra de linha no **3. Separador**, digite só `\n`: a barra e a letra, e nada mais no campo. Com o separador vazio, as linhas saem coladas.

### Exemplos {#cap-21-dinamico-exemplos}

Todos os exemplos usam os mesmos três registros do formato JSON; no terceiro, o sensor do slot 0 ficou sem leitura.

Com os modelos de fábrica:

```json
{"dev":"simut","mac":"28:cd:c1:0a:1b:2c","data":[{"ts":1790168400,"tSTM0001":4.25},{"ts":1790168460,"tSTM0001":4.31},{"ts":1790168520}]}
```

O modelo de fábrica da linha só fala do slot 0. Para os dois sensores, use **2. Linha** = `{"ts":{TS},"t0_ID":{t0},"t1_ID":{t1},"u1_ID":{u1},"p1_ID":{p1}}`:

```json
{"dev":"simut","mac":"28:cd:c1:0a:1b:2c","data":[{"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4},{"ts":1790168460,"tSTM0001":4.31,"tBME28001":22.84,"uBME28001":55.0,"pBME28001":1013.4},{"ts":1790168520,"tBME28001":22.90,"uBME28001":54.9,"pBME28001":1013.3}]}
```

Com nomes próprios, **1. Global** = `{"device":"{DEV}","payload":[{DATA}]}` e **2. Linha** = `{"time":{TS},"camara":{t0},"ambiente":{t1},"umidade":{u1},"placa":"{DHT_ID}"}`:

```json
{"device":"simut","payload":[{"time":1790168400,"camara":4.25,"ambiente":22.81,"umidade":55.2,"placa":"E6614C311B7A2F2D"},{"time":1790168460,"camara":4.31,"ambiente":22.84,"umidade":55.0,"placa":"E6614C311B7A2F2D"},{"time":1790168520,"camara":null,"ambiente":22.90,"umidade":54.9,"placa":"E6614C311B7A2F2D"}]}
```

Um texto simples, uma linha por registro: **1. Global** = `{DATA}`, **2. Linha** = `{TS};{t0};{t1};{u1};{p1}` e **3. Separador** = `\n`:

```text
1790168400;4.25;22.81;55.2;1013.4
1790168460;4.31;22.84;55.0;1013.4
1790168520;null;22.90;54.9;1013.3
```

Nesse último, a última linha não termina com quebra de linha: o separador só vai entre os registros.

::: atencao
**Um modelo mal escrito produz um corpo inválido, e o aparelho envia assim mesmo.** O aparelho não valida o resultado. Confira a **Prévia ao Vivo** antes de gravar e confira o primeiro corpo que chegar ao coletor.
:::

## MQTT {#cap-21-mqtt}

### Tópicos {#cap-21-topicos}

Todos os tópicos derivam do campo **Tópico**. Com o valor `fabrica/camara1/data`:

| Tópico | Uso | Retida | Quem publica |
|---|---|---|---|
| `fabrica/camara1/data` | Medições | Conforme **Reter** | O aparelho |
| `fabrica/camara1/status` | Estado da conexão | Sim | O aparelho e o broker |
| `fabrica/camara1/data/alarm` | Linha de alarmes | Não | O aparelho ([capítulo 22](#cap-22)) |
| `fabrica/camara1/data/alarm/ack` | Confirmação dos alarmes | — | O seu coletor ([capítulo 22](#cap-22)) |

O tópico de estado troca o último nível do **Tópico** por `status`: `simut/data` vira `simut/status`. Sem nenhuma `/` no tópico, o aparelho acrescenta `/status`.

O Home Assistant usa tópicos próprios, sob `homeassistant/` ([capítulo 23](#cap-23)).

### Mensagens de estado {#cap-21-estado}

Ao conectar, o aparelho registra no broker uma última vontade (*Last Will*) e publica a mensagem de conexão, as duas no tópico de estado, retidas, com QoS 0:

```json
{"device":"simut","status":"online","ip":"192.0.2.10"}
```

Se o aparelho some sem se despedir, o broker publica a última vontade:

```json
{"device":"simut","status":"offline"}
```

### Mensagens de medição {#cap-21-mensagens}

O formato de cada mensagem depende do tamanho do lote:

| Lote | Mensagens | Conteúdo de cada uma |
|---|---|---|
| Até 5 registros | Uma por registro | JSON: um objeto, sem os colchetes. CSV: uma linha, sem cabeçalho. **Dinâmico**: a linha, sem o envelope |
| Mais de 5 registros | Uma só, com o lote inteiro | O mesmo corpo do POST HTTP |
| Corpo acima de cerca de 8 KB | Uma por registro | Como no lote pequeno |

Com o lote mínimo em 1 e o aparelho em dia, cada mensagem é um único objeto:

```json
{"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4}
```

Depois de uma parada, a fila sai em mensagens com arrays, como no HTTP.

::: atencao
**O seu assinante precisa aceitar as duas formas.** A mesma assinatura recebe ora um objeto, ora um array. No JSON, teste o primeiro caractere: `{` é um registro, `[` é um lote.
:::

As mensagens de medição não trazem o `uid` nem o nome do aparelho. No MQTT, o tópico é a identidade ([capítulo 20](#cap-20-identidade-mqtt)).

### Conexão {#cap-21-conexao-mqtt}

- O aparelho tenta conectar no máximo uma vez a cada 5 s.
- Cada operação de rede do MQTT tem 4 s de limite.
- Uma conexão recusada registra o evento 36, **MQTT desconectado**, com o motivo no texto: `Connection timeout` (−4), `Connection lost` (−3), `Connect failed` (−2), `Disconnected` (−1), `Bad protocol` (1), `Client ID rejected` (2), `Server unavailable` (3), `Bad credentials` (4) ou `Not authorized` (5).
- O aparelho só assina um tópico: o de confirmação da linha de alarmes, e só com a linha ligada ([capítulo 22](#cap-22)).

## TLS e o arquivo /cert.pem {#cap-21-tls}

Com **Usar TLS / SSL** ligado, o aparelho fala HTTPS ou MQTTS com o coletor, na **Porta** configurada. Ajuste a porta: 443 para HTTPS e 8883 para MQTTS são o usual.

O que o aparelho oferece na negociação:

- **TLS 1.2**, e nenhuma outra versão;
- **ECDHE com AES-GCM**: `ECDHE-ECDSA` ou `ECDHE-RSA`, com AES-128-GCM-SHA256 ou AES-256-GCM-SHA384;
- **fragmentos de até 4.096 bytes**, pela extensão *Maximum Fragment Length* (RFC 6066).

Um servidor que só aceite CBC, ChaCha20-Poly1305, TLS 1.1 ou troca de chave RSA estática não conecta. Um servidor que ignore o pedido de fragmento e mande um registro TLS maior que 4.096 bytes derruba a conexão: o aparelho não tem memória para um registro de 16 KB.

No HTTPS, a negociação tem 15 s de limite. Como o aparelho fecha a conexão depois de cada lote, cada lote paga uma negociação completa, que leva segundos neste processador.

### Com e sem /cert.pem {#cap-21-certpem}

O arquivo `/cert.pem` é o certificado, em PEM, da autoridade (CA) que assinou o certificado do coletor.

1. Obtenha o certificado da CA, em formato PEM.
2. Salve-o com o nome `cert.pem`.
3. Na página **Arquivos**, envie o arquivo para a raiz do sistema de arquivos ([capítulo 17](#cap-17)).
4. Ligue **Usar TLS / SSL** e use **Salvar e reiniciar**.

Não confunda com o certificado da interface web do aparelho, instalado pela página **Rede** ([capítulo 9](#cap-09)).

| Situação | O que acontece | Evento no início |
|---|---|---|
| `/cert.pem` válido, até 16 KB | O aparelho confere o certificado do coletor contra ele | 34, **Cert SSL carregado**, com o tamanho |
| Sem `/cert.pem` | Conexão cifrada, mas o aparelho aceita qualquer certificado | 545, **Sem cert.pem, modo inseguro**, e 544 com o contexto 1 |
| Arquivo vazio | Como sem arquivo | 543, **cert.pem vazio, modo inseguro**, e 544 com o contexto 1 |
| Arquivo acima de 16 KB | Recusado; como sem arquivo | 544, **Erro de leitura de cert.pem**, com o tamanho, e 544 com o contexto 1 |

Sem certificado válido, a página mostra a faixa **TLS sem validação de certificado — a conexão é cifrada mas não autenticada (MITM possível). Envie /cert.pem em Arquivos para validar o servidor.**

- O aparelho lê o arquivo só ao iniciar, e só com o TLS ligado. Depois de enviar ou trocar o arquivo, reinicie.
- Cadeias assinadas com MD5 ou SHA-1 não passam na verificação.
- Use um certificado emitido para o nome ou o endereço que você digitou em **IP Servidor**, dentro do prazo de validade, e mantenha o relógio do aparelho sincronizado. Assim a verificação não depende de nenhum detalhe de como o aparelho compara nome e data.

::: atencao
**TLS sem certificado não autentica.** Qualquer máquina no caminho pode se passar pelo coletor, ler a chave de acesso e as medições e responder 200. Numa rede que você não controla, envie o `/cert.pem`.
:::

## Filas longas {#cap-21-filas}

Quando o coletor volta de uma parada, o aparelho esvazia a fila em lotes, no ritmo do coletor ([O ritmo entre lotes](#cap-21-ritmo)), até alcançar o presente.

- **A janela é de 30 dias.** Sem cursor, depois de **Resetar cursor de envio**, ou na primeira vez que a telemetria é ligada, o aparelho começa 30 dias antes do registro mais novo. O que é mais antigo que isso não sai pela telemetria; recupere pelo histórico ([capítulo 15](#cap-15)).
- **Ligar a telemetria pela primeira vez envia até 30 dias de histórico.** Prepare o coletor para essa primeira carga.
- **O contador de pendentes segue a mesma janela**, e para de contar em 65.535.

### Registros com hora fora de ordem {#cap-21-fora-de-ordem}

O cursor é um instante, não uma posição no arquivo. Isso tem três consequências:

- Um registro com hora mais de 1 dia à frente do relógio do aparelho fica para depois, até o relógio alcançá-lo.
- Um registro com hora no futuro sai, mas não empurra o cursor além da hora atual. Ele é oferecido de novo mais tarde, o que gera uma duplicata inofensiva.
- Um registro gravado com hora adiantada em relação aos vizinhos, mas ainda no passado, empurra o cursor por cima de registros mais antigos ainda não enviados, e eles não saem mais pela telemetria. Continuam no histórico do aparelho.

Se o cursor ficar mais de 1 h à frente do registro mais novo, ou do relógio, o aparelho o zera e registra um aviso com o texto `Telemetry cursor ahead of data — reset to 0`. Os envios recomeçam pela janela de 30 dias.

## Duplicatas e idempotência {#cap-21-duplicatas}

O aparelho prefere enviar duas vezes a deixar um buraco. Duplicatas acontecem por desenho:

- a resposta 2xx se perde no caminho de volta, e o aparelho reenvia o lote;
- o aparelho perde energia antes de gravar o cursor na flash, o que ele faz no máximo a cada 5 s;
- no MQTT, a conexão cai logo depois da publicação;
- um registro com hora no futuro é oferecido de novo;
- alguém usa **Resetar cursor de envio**.

Por isso, o coletor precisa ser idempotente. A chave natural de uma medição é o aparelho, a hora e o canal: (`X-SIMUT-Uid`, `ts`, chave do canal). Grave com "insira se não existir":

```sql
CREATE TABLE leitura (
  uid   TEXT    NOT NULL,
  ts    INTEGER NOT NULL,
  canal TEXT    NOT NULL,
  valor REAL    NOT NULL,
  PRIMARY KEY (uid, ts, canal)
);

-- SQLite
INSERT OR IGNORE INTO leitura (uid, ts, canal, valor) VALUES (?, ?, ?, ?);

-- PostgreSQL
INSERT INTO leitura (uid, ts, canal, valor) VALUES ($1, $2, $3, $4)
ON CONFLICT (uid, ts, canal) DO NOTHING;
```

## Receptores mínimos {#cap-21-receptores}

Os receptores abaixo gravam, sem duplicar, e só então confirmam. Todos esperam a configuração:

- **Transporte:** HTTP(S), com TLS desligado;
- **IP Servidor:** o endereço da máquina do receptor;
- **Porta:** 8080;
- **Endpoint:** `/api/telemetria`;
- **API Key:** `troque-este-segredo`;
- **Formato:** JSON. O receptor em Python puro também aceita CSV.

Os receptores leem o token da variável de ambiente `SIMUT_TOKEN`, que deve ter o mesmo valor do campo **API Key** do aparelho. Use um valor diferente para cada aparelho, e não o escreva no código.

### Python, só com a biblioteca padrão {#cap-21-rx-python}

Este receptor atende a telemetria em JSON e em CSV e também a linha de alarmes em JSON, no caminho de fábrica dela, `/api/telemetria/alarm` ([capítulo 22](#cap-22)). Ele grava num arquivo SQLite e só responde 204 depois de gravar.

```python
#!/usr/bin/env python3
"""Coletor minimo do SIMUT: telemetria (JSON ou CSV) e linha de alarmes (JSON)."""
import csv
import io
import json
import os
import sqlite3
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOKEN = os.environ["SIMUT_TOKEN"]       # o mesmo valor do campo API Key
PORTA = 8080

banco = sqlite3.connect("simut.db", check_same_thread=False)
trava = threading.Lock()
banco.executescript("""
CREATE TABLE IF NOT EXISTS leitura (
  uid TEXT NOT NULL, ts INTEGER NOT NULL, canal TEXT NOT NULL, valor REAL NOT NULL,
  PRIMARY KEY (uid, ts, canal));
CREATE TABLE IF NOT EXISTS alarme (
  uid TEXT NOT NULL, ts INTEGER NOT NULL, seq INTEGER NOT NULL, id TEXT NOT NULL,
  codigo TEXT, usuario TEXT, registro TEXT NOT NULL,
  PRIMARY KEY (uid, ts, seq, id));
""")


def leituras_json(corpo):
    dados = json.loads(corpo)
    if isinstance(dados, dict):          # um registro sozinho
        dados = [dados]
    for registro in dados:
        ts = int(registro.pop("ts"))
        for canal, valor in registro.items():
            yield ts, canal, float(valor)


def leituras_csv(corpo):
    linhas = csv.reader(io.StringIO(corpo), delimiter=";")
    cabecalho = next(linhas, None)
    if cabecalho is None:
        raise ValueError("CSV vazio")
    for linha in linhas:
        if not linha:
            continue
        ts = int(linha[0])
        for coluna, valor in zip(cabecalho[1:], linha[1:]):
            if valor != "":              # vazio = sem leitura
                yield ts, coluna, float(valor)


class Coletor(BaseHTTPRequestHandler):
    def responder(self, codigo):
        self.send_response(codigo)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        if self.path not in ("/api/telemetria", "/api/telemetria/alarm"):
            return self.responder(404)
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            return self.responder(401)
        uid = self.headers.get("X-SIMUT-Uid", "desconhecido")
        tamanho = int(self.headers.get("Content-Length", "0"))
        corpo = self.rfile.read(tamanho).decode("utf-8")
        tipo = self.headers.get("Content-Type", "")
        try:
            with trava, banco:           # grava tudo ou nada
                if self.path == "/api/telemetria":
                    origem = leituras_csv(corpo) if tipo.startswith("text/csv") else leituras_json(corpo)
                    banco.executemany(
                        "INSERT OR IGNORE INTO leitura VALUES (?, ?, ?, ?)",
                        [(uid, ts, canal, valor) for ts, canal, valor in origem])
                else:
                    for r in json.loads(corpo):
                        codigo = r.get("alarm") or r.get("err") or r.get("maint")
                        banco.execute(
                            "INSERT OR IGNORE INTO alarme VALUES (?, ?, ?, ?, ?, ?, ?)",
                            (uid, r["ts"], r["seq"], r["id"], codigo, r.get("user"),
                             json.dumps(r)))
        except (ValueError, KeyError, TypeError, AttributeError) as erro:
            self.log_error("corpo recusado de %s: %s", uid, erro)
            return self.responder(400)   # nao confirma: o aparelho reenvia
        self.responder(204)              # gravado: pode confirmar


if __name__ == "__main__":
    print(f"Coletor SIMUT na porta {PORTA}")
    ThreadingHTTPServer(("0.0.0.0", PORTA), Coletor).serve_forever()
```

Salve como `coletor.py`, ponha o token na variável `SIMUT_TOKEN` e rode com `python3 coletor.py`. O programa precisa do Python 3.7 ou mais novo.

### Flask {#cap-21-rx-flask}

```python
# pip install flask
import os
import sqlite3
from contextlib import closing

from flask import Flask, abort, request

TOKEN = os.environ["SIMUT_TOKEN"]
app = Flask(__name__)

with closing(sqlite3.connect("simut.db")) as con:
    con.execute("""CREATE TABLE IF NOT EXISTS leitura (
        uid TEXT NOT NULL, ts INTEGER NOT NULL, canal TEXT NOT NULL, valor REAL NOT NULL,
        PRIMARY KEY (uid, ts, canal))""")


@app.post("/api/telemetria")
def telemetria():
    if request.headers.get("Authorization") != f"Bearer {TOKEN}":
        abort(401)
    uid = request.headers.get("X-SIMUT-Uid", "desconhecido")
    dados = request.get_json(force=True, silent=True)   # formato JSON
    if dados is None:
        abort(400)                       # nao confirma: o aparelho reenvia
    if isinstance(dados, dict):
        dados = [dados]
    linhas = []
    for registro in dados:
        ts = registro.pop("ts")
        linhas += [(uid, ts, canal, valor) for canal, valor in registro.items()]
    with closing(sqlite3.connect("simut.db")) as con, con:
        con.executemany("INSERT OR IGNORE INTO leitura VALUES (?, ?, ?, ?)", linhas)
    return "", 204                       # gravado: pode confirmar


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)
```

### Node.js e Express {#cap-21-rx-express}

```js
// npm install express
const express = require("express");

const TOKEN = process.env.SIMUT_TOKEN;
const app = express();
app.use(express.text({ type: "*/*", limit: "1mb" }));  // corpo cru, qualquer Content-Type

const gravados = new Set();  // troque por um banco com chave unica (uid, ts, canal)

app.post("/api/telemetria", (req, res) => {
  if (req.get("Authorization") !== `Bearer ${TOKEN}`) return res.sendStatus(401);
  const uid = req.get("X-SIMUT-Uid") || "desconhecido";

  let dados;
  try {
    dados = JSON.parse(req.body);            // formato JSON
  } catch {
    return res.sendStatus(400);              // nao confirma: o aparelho reenvia
  }
  if (!Array.isArray(dados)) dados = [dados];

  for (const { ts, ...canais } of dados) {
    for (const [canal, valor] of Object.entries(canais)) {
      const chave = `${uid}|${ts}|${canal}`;
      if (gravados.has(chave)) continue;     // duplicata: ignora
      gravados.add(chave);
      console.log(uid, ts, canal, valor);
    }
  }
  res.sendStatus(204);                       // gravado: pode confirmar
});

app.listen(8080, () => console.log("Coletor SIMUT na porta 8080"));
```

### MQTT com mosquitto {#cap-21-rx-mosquitto}

Uma configuração mínima do broker mosquitto, com senha:

```ini
# /etc/mosquitto/conf.d/simut.conf
listener 1883
allow_anonymous false
password_file /etc/mosquitto/senhas
```

1. Crie o usuário do aparelho: `sudo mosquitto_passwd -c /etc/mosquitto/senhas simut`.
2. Crie o usuário do coletor: `sudo mosquitto_passwd /etc/mosquitto/senhas coletor`.
3. Reinicie o broker: `sudo systemctl restart mosquitto`.
4. No aparelho, use **Porta** 1883, **Usuário** `simut` e a senha criada no passo 1.

Para MQTTS, acrescente um segundo *listener* com o certificado do broker:

```ini
listener 8883
certfile /etc/mosquitto/certs/broker.crt
keyfile /etc/mosquitto/certs/broker.key
cafile /etc/mosquitto/certs/ca.crt
```

No aparelho, use **Porta** 8883, ligue **Usar MQTTS (TLS)** e envie o `ca.crt` como `/cert.pem`.

Para ver tudo o que o aparelho publica, com o nome do tópico antes de cada mensagem:

```bash
mosquitto_sub -h coletor.exemplo.com.br -p 1883 -u coletor -P 'senha-do-coletor' -v -t 'simut/#'
```

```text
simut/status {"device":"simut","status":"online","ip":"192.0.2.10"}
simut/data {"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4}
```

O `mosquitto_sub` só mostra. Um programa que grave as medições precisa das mesmas regras do HTTP: aceitar objeto e array, e gravar sem duplicar. O [capítulo 22](#cap-22) traz um assinante completo em Python, que também confirma os alarmes.

## Testar a integração {#cap-21-testar}

Antes de ligar um aparelho, teste o coletor com o `curl`, mandando exatamente o que o aparelho manda:

```bash
curl -i http://coletor.exemplo.com.br:8080/api/telemetria \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer troque-este-segredo' \
  -H 'X-SIMUT-Uid: E6614C311B7A2F2D' \
  -H 'X-SIMUT-Ver: 2.7.1' \
  -H 'X-SIMUT-Env: release' \
  -H 'X-SIMUT-Cfg: 3F2A91C4' \
  --data-binary '[{"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4}]'
```

A primeira linha da resposta deve trazer o código 204, ou outro código 2xx. Mande o mesmo comando duas vezes e confira que o banco não duplicou a medição. Mande também um corpo inválido, como `--data-binary '[{'`, e confira que o coletor responde 400 sem gravar nada.

Depois, com o aparelho:

1. Rode o receptor numa máquina da mesma rede e anote o endereço dela.
2. Na página **Telemetria**, preencha **IP Servidor**, **Porta**, **Endpoint** e **API Key**.
3. Ponha **Lote mínimo (registros)** em 1.
4. Use **Aplicar agora**.
5. Toque em **Enviar agora**.
6. Confira no receptor o POST recebido.
7. No **Painel de Controle**, confira **Registros Pendentes** caindo até **Sincronizado**, e **Telemetria Env.** subindo ([capítulo 13](#cap-13)).
8. Na página **Histórico e Logs**, em **Eventos do Sistema**, confira o evento 30, **Telemetria enviada**.

Nas imagens com o console completo ([capítulo 14](#cap-14)), o comando `tel dump` imprime no console o próximo corpo montado, entre `=== PAYLOAD SYNC (<n> B) ===` e `=== END ===`, quebrado em linhas a cada vírgula.

## Códigos do log de eventos {#cap-21-eventos}

| Código | Evento | Quando |
|---|---|---|
| 30 | **Telemetria enviada** | Lote aceito com 2xx; o contexto é o código HTTP |
| 31 | **Falha de telemetria** | Cada resposta fora de 2xx (contexto = código) ou erro de conexão (contexto negativo). Depois do 547, também uma vez por hora, com o texto `Still failing (#n)` |
| 32 | **Retry de telemetria** | Nova tentativa agendada, com o texto `Upload failed (#n). Retry in <s>s`; até a 10ª falha seguida |
| 33 | **Telemetria enfileirada** | MQTT: corpo grande demais, publicado registro a registro |
| 34 | **Cert SSL carregado** | Início, com `/cert.pem` válido |
| 35 | **MQTT conectado** | Conexão com o broker |
| 36 | **MQTT desconectado** | Conexão recusada, ou perdida logo depois de uma publicação |
| 37 | **MQTT publicado** | Mensagens de medição publicadas |
| 303 | **Config alterada** | Cursor apagado por **Resetar cursor de envio** |
| 540 | **Transporte HTTP inicializado** | Início, transporte HTTP; o contexto é a porta |
| 541 | **Transporte MQTT inicializado** | Início, transporte MQTT; o contexto é a porta |
| 542 | **MQTT conectando** | Cada tentativa de conexão, com o Client ID |
| 543 | **cert.pem vazio, modo inseguro** | Início, com TLS e arquivo vazio |
| 544 | **Erro de leitura de cert.pem** | Início, com TLS: arquivo ilegível ou grande demais; com o contexto 1, aviso de TLS sem validação |
| 545 | **Sem cert.pem, modo inseguro** | Início, com TLS e sem o arquivo |
| 546 | **Forçando sync de telemetria** | **Enviar agora**; o contexto é a conta |
| 547 | **Logs de retry suprimidos** | A partir da 11ª falha seguida |

O log de eventos guarda transições, não repetições. Da família da telemetria, ele grava a primeira falha depois de um período saudável, o primeiro sucesso depois de uma falha e um registro por hora enquanto nada muda. O console serial mostra todas as linhas ([capítulo 16](#cap-16)).

### Contexto negativo do evento 31 {#cap-21-erros-http}

| Contexto | Significado |
|---|---|
| −1 | Conexão falhou: servidor fora do ar, porta fechada, nome não resolvido ou TLS recusado |
| −2 | Falha ao enviar os cabeçalhos |
| −3 | Falha ao enviar o corpo |
| −4 | Sem conexão |
| −5 | Conexão perdida |
| −7 | O servidor não respondeu como HTTP |
| −8 | Memória insuficiente |
| −11 | Tempo esgotado: nenhuma resposta em 4 s |

## Solução de problemas {#cap-21-problemas}

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| **Registros Pendentes** cresce e nada sai | **Lote mínimo** em 0, ou sinal em −78 dBm ou abaixo | Defina o **Lote mínimo**; melhore o sinal |
| Evento 31 com o contexto −1 | Servidor, porta ou nome errado; firewall; TLS incompatível | Confira **IP Servidor** e **Porta**; teste com `curl` da mesma rede |
| Evento 31 com o contexto −11 | O coletor leva mais de 4 s para responder | Grave mais rápido ou responda assim que gravar |
| Evento 31 com o contexto 301 ou 302 | O coletor redireciona, e o aparelho não segue | Aponte para o endereço final; para HTTPS, ligue o TLS |
| Evento 31 com o contexto 401 ou 403 | Chave de acesso errada ou ausente | Confira a **API Key** e a forma do cabeçalho ([A chave de acesso](#cap-21-credencial)) |
| Evento 31 com o contexto 404 | **Endpoint** errado | Confira o caminho, começando com `/` |
| HTTP funciona, HTTPS falha com o contexto −1 | Versão de TLS, cifras ou fragmento de 4.096 bytes não aceitos | O servidor precisa de TLS 1.2 com ECDHE-GCM e da extensão *Maximum Fragment Length* |
| A faixa **TLS sem validação de certificado** aparece | Sem `/cert.pem` válido | Envie o certificado da CA como `/cert.pem` e reinicie |
| MQTT: evento 36 com `Bad credentials` ou `Not authorized` | **Usuário** ou **Senha** errados, ou falta de permissão no broker | Confira o usuário, a senha e a lista de acesso do broker |
| MQTT: nada chega e a porta é 80 | A **Porta** não muda ao trocar o transporte | Use 1883 (MQTT) ou 8883 (MQTTS) |
| O assinante MQTT quebra com um array | Depois de uma parada, a fila sai em lotes com array | Aceite objeto e array ([Mensagens de medição](#cap-21-mensagens)) |
| O coletor recebe `"camara":null` | Forma simples do marcador, com o sensor sem leitura | Use `"t0_ID":{t0}` ou `"t0":{t0}` para a chave sumir |
| JSON inválido no formato **Dinâmico** | Modelo mal escrito | Confira a **Prévia ao Vivo** e o primeiro corpo recebido |
| Medições repetidas no banco | Duplicatas por desenho | Grave com chave única (`uid`, `ts`, canal) |
| Buraco nas medições depois de horas sem coletor | Registros com mais de 30 dias antes do mais novo, ou hora fora de ordem | Recupere pelo histórico do aparelho ([capítulo 15](#cap-15)) |
| Primeira ativação manda milhares de registros | O aparelho começa 30 dias antes do registro mais novo | Esperado; prepare o coletor para a carga inicial |
| Aviso `Telemetry cursor ahead of data — reset to 0` | O relógio andou para trás, e o cursor ficou no futuro | Nada a fazer; confira o NTP ([capítulo 10](#cap-10)) |
| Os envios param enquanto alguém usa o painel | O painel tem prioridade | Esperado; os envios voltam 5 s depois do último toque |
