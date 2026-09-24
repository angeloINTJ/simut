# Home Assistant {#cap-23}

Este capítulo mostra como fazer o SIMUT aparecer sozinho no Home Assistant, com um dispositivo e uma entidade por medição, sem escrever YAML. É para quem já tem um broker MQTT e um Home Assistant funcionando e quer ver as medições do aparelho neles.

## Como funciona {#cap-23-como}

O aparelho não conversa com o Home Assistant diretamente. Os dois se encontram no broker MQTT:

1. O aparelho conecta ao broker com a telemetria no transporte MQTT ([capítulo 21](#cap-21)).
2. Logo depois de conectar, ele publica uma mensagem de descoberta para cada medição, retida no broker, sob o tópico `homeassistant/`.
3. O Home Assistant lê essas mensagens e cria o dispositivo e as entidades.
4. Daí em diante, cada mensagem de telemetria atualiza os valores, e a mensagem de estado do aparelho diz se ele está disponível.

A descoberta não é um canal novo: ela usa a mesma conexão, o mesmo servidor, a mesma conta e o mesmo TLS da telemetria. O recurso existe nas três imagens: release, alpha e Air. No Air, veja as ressalvas em [No SIMUT Air](#cap-23-air).

::: {.figura #fig-23-fluxo tipo="diagrama" arquivo="23-fluxo.png" captura="Três caixas da esquerda para a direita: 'SIMUT (simut_0a1b2c)', 'Broker MQTT (broker.exemplo.com.br:1883)' e 'Home Assistant'. Setas do SIMUT para o broker, numeradas: 1 'ao conectar: homeassistant/sensor/simut_0a1b2c/<objeto>/config (retida, uma por medição)'; 2 'simut/status {status: online} (retida)'; 3 'simut/data {ts, tSTM0001, ...} a cada envio'. Seta tracejada do broker para o próprio broker: 'se o aparelho some: simut/status {status: offline} (última vontade)'. Setas do broker para o Home Assistant: 'assina homeassistant/# e cria dispositivo e entidades'; 'lê os valores em simut/data'; 'lê a disponibilidade em simut/status'. Nota no rodapé: 'o Home Assistant não manda nada ao aparelho'."}
Legenda: o caminho das mensagens. O aparelho só publica; o Home Assistant só assina. Tudo passa pelo broker.
:::

## O que aparece no Home Assistant {#cap-23-o-que-aparece}

### O dispositivo {#cap-23-dispositivo}

Neste capítulo, **dispositivo** é o registro que o Home Assistant cria, e **aparelho** continua sendo o SIMUT. Cada aparelho vira um dispositivo, com estes dados:

| Dado no Home Assistant | Valor | De onde vem |
|---|---|---|
| Nome | `simut` | Campo **Nome** da página **Configurações** ([capítulo 5](#cap-05-identidade)) |
| Fabricante | `SIMUT` | Fixo |
| Modelo | `Raspberry Pi Pico W` | Fixo |
| Versão do firmware | `2.7.1` | A versão gravada |
| Link de configuração | `http://192.0.2.10` | O IP do aparelho e a **Porta HTTP**; a porta só aparece quando não é 80 |
| Identificador | `simut_0a1b2c` | O **Client ID** do MQTT |

O identificador é o **Client ID** do aparelho, com todo caractere fora de letras, dígitos, `_` e `-` trocado por `_`. Com o **Client ID** em branco, ele vale `simut_` seguido dos seis últimos dígitos do MAC ([capítulo 20](#cap-20-identidade-mqtt)). Neste capítulo, ele se chama **nó**.

### As entidades {#cap-23-entidades}

Cada slot ativo gera uma entidade de temperatura e, se o sensor mede umidade, uma de umidade. A pressão gera uma entidade só, para o aparelho inteiro.

| Grandeza | Sensores | Unidade | Classe | Casas decimais sugeridas | Chave no JSON |
|---|---|---|---|---|---|
| Temperatura | DS18B20, DHT22, BME280, BMP280 | `°C` | `temperature` | 1 | `t` + ID de hardware |
| Umidade | DHT22, BME280 | `%` | `humidity` | 0 | `u` + ID de hardware |
| Pressão | BME280, BMP280 | `hPa` | `pressure` | 1 | `p` + ID de hardware |

- **Uma entidade de pressão por aparelho.** A telemetria leva um único valor de pressão por registro, atribuído ao primeiro slot ativo com sensor de pressão ([capítulo 21](#cap-21-json)). A descoberta segue a mesma regra.
- **Todas as entidades são medições.** O aparelho declara a classe de estado `measurement`, e o Home Assistant guarda as estatísticas de longo prazo delas.
- **Nada além disso.** O aparelho não cria entidades de alarme, de sinal Wi-Fi nem de estado do sensor, e não aceita comandos do Home Assistant.

O nome de cada entidade é o nome do slot seguido do nome da grandeza, em inglês: `Temperature`, `Humidity` ou `Pressure`. Sem nome no slot, vale o ID de hardware. O Home Assistant costuma mostrar o nome do dispositivo antes do nome da entidade.

Com dois slots, um DS18B20 chamado **Câmara 1** (ID `STM0001`) e um BME280 chamado **Sala** (ID `BME28001`), aparecem quatro entidades:

| Nome da entidade | Chave no JSON | ID único (`unique_id`) |
|---|---|---|
| `Câmara 1 Temperature` | `tSTM0001` | `simut_0a1b2c_tSTM0001` |
| `Sala Temperature` | `tBME28001` | `simut_0a1b2c_tBME28001` |
| `Sala Humidity` | `uBME28001` | `simut_0a1b2c_uBME28001` |
| `Sala Pressure` | `pBME28001` | `simut_0a1b2c_pBME28001` |

O ID único é o nó, um `_` e a chave. O `entity_id`, como `sensor.…`, é o Home Assistant quem escolhe, a partir dos nomes, na primeira descoberta; depois disso ele não muda sozinho. Para mudar o nome que aparece no painel do Home Assistant, renomeie a entidade lá ou mude o nome do slot no aparelho ([capítulo 6](#cap-06-editar)).

### A disponibilidade {#cap-23-disponibilidade}

As entidades ficam disponíveis enquanto a mensagem de estado do aparelho diz `online` e indisponíveis quando ela diz `offline` ([capítulo 21](#cap-21-estado)):

- o aparelho publica `online`, retida, a cada conexão;
- o broker publica `offline`, retida, quando o aparelho some sem se despedir, pela última vontade registrada na conexão.

O aparelho nunca se despede do broker: um reinício, uma falta de energia ou o sono do Air terminam a conexão sem aviso. O broker só percebe quando o prazo do **Keep-Alive** vence (pela regra do MQTT, uma vez e meia o valor; com os 60 s de fábrica, 90 s) ou quando a conexão cai por outro motivo. Até lá, as entidades continuam disponíveis com o último valor.

## Antes de começar {#cap-23-requisitos}

Você precisa de:

- **Um broker MQTT** que o aparelho e o Home Assistant alcancem, como o Mosquitto. A configuração mínima de um Mosquitto com senha está no [capítulo 21](#cap-21-rx-mosquitto).
- **A integração MQTT do Home Assistant** ligada a esse broker, com a descoberta ativa e o prefixo `homeassistant`. Esse é o padrão do Home Assistant. O prefixo do aparelho é fixo e não pode ser mudado.
- **A telemetria do aparelho no transporte MQTT**, com servidor, porta, conta e TLS configurados como no [capítulo 21](#cap-21-transporte).
- **O formato JSON.** Na página **Telemetria**, seção **Construtor**, o **Formato** tem de ser **JSON**. Com **CSV** ou **Dinâmico**, o aparelho não publica a descoberta e apaga a que já tinha publicado.
- **O Lote mínimo entre 1 e 5.** Veja o quadro abaixo.
- **Um Client ID e um Tópico próprios** para cada aparelho, quando houver mais de um no mesmo broker ([capítulo 20](#cap-20-identidade-mqtt)).

::: atencao
**Com lotes de mais de 5 registros, o Home Assistant não lê os valores.** Até 5 registros, o aparelho publica uma mensagem por registro, com um objeto JSON. Acima disso, publica o lote inteiro numa mensagem só, como um array ([capítulo 21](#cap-21-mensagens)). A descoberta ensina o Home Assistant a ler `value_json['tSTM0001']`, que só existe num objeto: numa mensagem em array, ele não acha o valor e a entidade não muda. Use **Lote mínimo (registros)** em 1, que manda cada registro assim que ele é gravado. Depois de uma parada longa, a fila atrasada sai em lotes maiores, e o Home Assistant só volta a mostrar valores quando a fila acaba. Para que nem a fila atrasada saia em array, use também **Lote máximo (registros)** em 5 ou menos ([capítulo 21](#cap-21-lotes)).
:::

Ligue também **Reter** (*Retain Message*) se quiser que o Home Assistant mostre o último valor logo depois de reiniciar, sem esperar a próxima medição: o broker guarda a última mensagem de medição e a entrega a quem assinar depois ([capítulo 21](#cap-21-campos-mqtt)).

## Ligar a descoberta {#cap-23-ligar}

A opção fica na página **Telemetria**, no bloco do MQTT, que só aparece com **Transporte** em **MQTT(S)**.

| Campo | Chave em `commit_all` | Console | Faixa | Fábrica | Aplicação |
|---|---|---|---|---|---|
| **Home Assistant Discovery** | `m_had`, na seção `sys` | Não há comando | Ligado ou desligado | Desligado | Reinicia (grupo `web`) |

Abaixo da opção, a página explica: **Publica mensagens de configuração retidas para o Home Assistant criar o dispositivo e seus sensores automaticamente. Exige payload JSON; as entidades aparecem no próximo envio.**

Para ligar:

1. Abra a página **Telemetria**.
2. Em **Transporte**, escolha **MQTT(S)** e preencha o broker como no [capítulo 21](#cap-21-campos-mqtt).
3. Em **Construtor**, deixe o **Formato** em **JSON**.
4. Em **Lote mínimo (registros)**, digite `1`.
5. Ligue **Home Assistant Discovery**.
6. Toque em **Salvar e reiniciar** na barra de topo e confirme.
7. Espere o primeiro envio de telemetria depois do reinício.
8. Na página **Histórico e Logs**, carregue os **Eventos do Sistema** e procure o evento 548, **Discovery HA atualizado**. O contexto é o número de mensagens publicadas.

::: nota
**Por que o aparelho reinicia.** O cliente MQTT lê a configuração uma vez, ao ligar, e a descoberta é conferida a cada conexão com o broker. O aparelho grava a opção e reinicia para que a próxima conexão já saia com ela. **Aplicar agora** também grava e reinicia, e **Testar** ignora esta opção, porque o ensaio não a enxerga ([capítulo 5](#cap-05-grupos)). Use **Salvar e reiniciar**.
:::

As entidades não aparecem no momento em que o aparelho volta. Ele só conecta ao broker quando tem algo a enviar: a primeira telemetria depois do reinício ou um evento da linha de alarmes ([capítulo 22](#cap-22)). Com o **Lote mínimo** em 1, isso acontece no primeiro registro do histórico, em até um **Intervalo Histórico (min)** ([capítulo 6](#cap-06-intervalo-historico)).

Pela API, a mesma opção vai na seção `sys` de `POST /api/commit_all` ([capítulo 26](#cap-26-commit)):

```json
{"sys":{"t_transport":1,"t_mode":0,"t_int":1,"m_had":true}}
```

::: {.figura #fig-23-telemetria-ha tipo="web" arquivo="23-telemetria-ha.png" captura="rota /telemetry; largura 1280; sessão admin; Transporte MQTT(S); IP Servidor broker.exemplo.com.br; Porta 1883; Usar MQTTS (TLS) desligado; Tópico simut/data; Client ID vazio; Usuário simut; Reter ligado; Home Assistant Discovery ligado, com a dica visível; Lote mínimo 1; Formato JSON; recorte do bloco MQTT até o Lote mínimo"}
Legenda: a opção **Home Assistant Discovery** no bloco do MQTT. Note a dica embaixo dela: a descoberta exige o formato JSON e só acontece no próximo envio.
:::

::: {.figura #fig-23-evento-548 tipo="web" arquivo="23-evento-548.png" captura="rota /history; largura 1280; sessão admin; seção Eventos do Sistema carregada, com INF marcado e o filtro 548; uma linha do evento 548 Discovery HA atualizado com contexto 4, logo abaixo de 35 MQTT conectado"}
Legenda: o evento 548 confirma a publicação. O contexto 4 é o número de entidades publicadas.
:::

## No Home Assistant, passo a passo {#cap-23-passo-a-passo}

Os menus citados aqui são os do Home Assistant, não os do aparelho.

1. No Home Assistant, abra **Configurações**, depois **Dispositivos e serviços**.
2. Se a integração MQTT não estiver lá, adicione-a e informe o endereço, a porta, o usuário e a senha do broker.
3. Confira, nas opções da integração MQTT, que a descoberta está ativa e o prefixo é `homeassistant`.
4. Configure o aparelho como em [Ligar a descoberta](#cap-23-ligar).
5. Depois do primeiro envio do aparelho, abra a integração MQTT e a lista de dispositivos. O aparelho aparece com o **Nome** dele, por exemplo `simut`.
6. Abra o dispositivo. As entidades aparecem com os nomes da tabela de [As entidades](#cap-23-entidades), já com unidade e classe.
7. Adicione as entidades a um painel do Home Assistant, se quiser.

Para ver as mensagens de descoberta direto no broker, com o tópico antes de cada uma:

```bash
mosquitto_sub -h broker.exemplo.com.br -p 1883 -u coletor -P 'senha-do-coletor' -v -t 'homeassistant/sensor/#'
```

Se o broker restringe tópicos por conta, a conta do aparelho precisa publicar nos tópicos de medição e de estado e em `homeassistant/sensor/<nó>/#`, e a conta do Home Assistant precisa ler `homeassistant/#` e os tópicos do aparelho. Um exemplo de lista de acesso do Mosquitto, com o **Tópico** `simut/data`:

```text
user simut
topic write simut/#
topic read simut/data/alarm/ack
topic write homeassistant/sensor/simut_0a1b2c/#

user homeassistant
topic read simut/#
topic readwrite homeassistant/#
```

A linha `simut/data/alarm/ack` só importa com a linha de alarmes ligada ([capítulo 22](#cap-22-mqtt)).

## Tópicos e mensagens {#cap-23-topicos}

| Tópico | Retida | Conteúdo |
|---|---|---|
| `homeassistant/sensor/<nó>/<objeto>/config` | Sim | Uma mensagem de descoberta por entidade |
| O **Tópico**, como `simut/data` | Conforme **Reter** | As medições, de onde saem os valores |
| O tópico de estado, como `simut/status` | Sim | `online` ou `offline`, de onde sai a disponibilidade |

O `<objeto>` é a chave da medição no JSON da telemetria, como `tSTM0001`, com todo caractere fora de letras, dígitos, `_` e `-` trocado por `_`. Como o ID de hardware só aceita esses caracteres ([capítulo 6](#cap-06-erros-gravar)), o objeto costuma ser igual à chave.

A mensagem de descoberta da entidade **Câmara 1 Temperature**, como o aparelho a publica, numa linha só:

```json
{"name":"Câmara 1 Temperature","uniq_id":"simut_0a1b2c_tSTM0001","stat_t":"simut/data","val_tpl":"{{ value_json['tSTM0001'] }}","unit_of_meas":"°C","dev_cla":"temperature","stat_cla":"measurement","sug_dsp_prc":1,"avty_t":"simut/status","avty_tpl":"{{ value_json.status }}","pl_avail":"online","pl_not_avail":"offline","dev":{"ids":["simut_0a1b2c"],"name":"simut","mf":"SIMUT","mdl":"Raspberry Pi Pico W","sw":"2.7.1","cu":"http://192.0.2.10"}}
```

O aparelho usa as abreviações que o Home Assistant aceita nas mensagens de descoberta:

| Chave | Nome completo | Valor no exemplo |
|---|---|---|
| `name` | `name` | Nome da entidade |
| `uniq_id` | `unique_id` | Nó + `_` + objeto |
| `stat_t` | `state_topic` | O **Tópico** das medições |
| `val_tpl` | `value_template` | Lê a chave da medição no JSON |
| `unit_of_meas` | `unit_of_measurement` | `°C`, `%` ou `hPa` |
| `dev_cla` | `device_class` | `temperature`, `humidity` ou `pressure` |
| `stat_cla` | `state_class` | Sempre `measurement` |
| `sug_dsp_prc` | `suggested_display_precision` | 1, 0 ou 1 |
| `avty_t` | `availability_topic` | O tópico de estado |
| `avty_tpl` | `availability_template` | Lê o campo `status` |
| `pl_avail`, `pl_not_avail` | `payload_available`, `payload_not_available` | `online`, `offline` |
| `dev` | `device` | O bloco do dispositivo |
| `ids`, `mf`, `mdl`, `sw`, `cu` | `identifiers`, `manufacturer`, `model`, `sw_version`, `configuration_url` | Os dados do dispositivo |

A entidade de umidade traz `"unit_of_meas":"%"`, `"dev_cla":"humidity"` e `"sug_dsp_prc":0`. A de pressão traz `"unit_of_meas":"hPa"`, `"dev_cla":"pressure"` e `"sug_dsp_prc":1`. O campo `cu` leva o IP que o aparelho tinha ao conectar; se o DHCP der outro IP, o link se corrige na conexão seguinte.

Os valores chegam pela telemetria comum, no tópico das medições:

```text
homeassistant/sensor/simut_0a1b2c/tSTM0001/config {"name":"Câmara 1 Temperature","uniq_id":"simut_0a1b2c_tSTM0001",...}
simut/status {"device":"simut","status":"online","ip":"192.0.2.10"}
simut/data {"ts":1790168400,"tSTM0001":4.25,"tBME28001":22.81,"uBME28001":55.2,"pBME28001":1013.4}
```

O Home Assistant ignora o `ts`: cada valor entra no histórico dele com a hora em que chegou.

## O que o aparelho faz em cada situação {#cap-23-situacoes}

O aparelho confere a descoberta a cada conexão com o broker. Com a opção ligada e o formato JSON, ele publica de novo todas as mensagens, retidas, mesmo que nada tenha mudado. Isso cobre um broker que reiniciou e perdeu as mensagens retidas. Um registro gravado na configuração lembra se há mensagens publicadas no broker, para que ele saiba o que apagar depois.

| Situação | O que acontece no Home Assistant |
|---|---|
| Descoberta ligada, formato JSON | Na próxima conexão, dispositivo e entidades aparecem ou são atualizados |
| Descoberta desligada | Na próxima conexão, o aparelho publica uma mensagem vazia e retida em cada tópico de descoberta, e as entidades somem |
| **Formato** trocado para **CSV** ou **Dinâmico** | Igual a desligar a descoberta |
| **Transporte** trocado para **HTTP(S)** | Nada é apagado. O aparelho não conecta mais ao broker, e as entidades ficam indisponíveis |
| **Nome** do slot mudado | Na próxima conexão, a entidade recebe o nome novo, a menos que você a tenha renomeado no Home Assistant |
| ID de hardware mudado | Surge uma entidade nova. A antiga fica, sem valores |
| Slot removido ou desligado | A entidade dele fica, sem valores |
| **Client ID** mudado | Surge um dispositivo novo. O antigo fica, indisponível |
| **Tópico** mudado | As entidades passam a ler o tópico novo na próxima conexão |
| **Nome** do aparelho mudado | O dispositivo recebe o nome novo na próxima conexão |
| Sensor em falha | A medição sai do JSON, e a entidade fica sem valor novo |
| Aparelho reiniciado ou sem energia | As entidades ficam indisponíveis quando o broker publica a última vontade, e voltam na próxima conexão |

::: atencao
**O aparelho só apaga o que ainda existe na configuração dele.** Ao desligar a descoberta, ele apaga as mensagens dos slots ativos naquele momento, com as chaves daquele momento. As mensagens de um slot já removido, de um ID de hardware antigo ou de um **Client ID** antigo ficam retidas no broker até alguém apagá-las ([Remover um aparelho](#cap-23-remover)).
:::

::: nota
**O `cfg` do aparelho muda na primeira publicação.** O registro que lembra que há mensagens no broker faz parte da configuração. Quando ele muda, na primeira publicação ou na remoção, muda também o `cfg` de `GET /api/status` ([capítulo 20](#cap-20-cabecalhos)), sem que ninguém tenha editado nada.
:::

## Remover um aparelho {#cap-23-remover}

A ordem importa: o aparelho só apaga as entidades dele enquanto ainda está no MQTT, no formato JSON e com os mesmos slots.

1. Na página **Telemetria**, desligue **Home Assistant Discovery**, sem mudar mais nada.
2. Toque em **Salvar e reiniciar** e confirme.
3. Espere o próximo envio de telemetria.
4. Confira o evento 548, **Discovery HA atualizado**, nos **Eventos do Sistema**. No console e no syslog, o texto do evento diz `HA discovery cleared` seguido do número de mensagens apagadas.
5. Confira no Home Assistant que o dispositivo sumiu.
6. Só então troque o transporte, remova slots ou desligue o aparelho.

Para tirar do Home Assistant a entidade de um slot que você vai remover, faça o mesmo em três gravações: desligue a descoberta e espere o evento 548, remova o slot, e ligue a descoberta de novo.

Para apagar uma mensagem que ficou no broker, publique uma mensagem vazia e retida no tópico dela:

```bash
mosquitto_pub -h broker.exemplo.com.br -p 1883 -u simut -P 'senha-do-aparelho' -r -n -t 'homeassistant/sensor/simut_0a1b2c/tSTM0003/config'
```

O `-r` pede a retenção e o `-n` manda a mensagem vazia. Para achar os tópicos que sobraram, assine `homeassistant/sensor/<nó>/#` com o `mosquitto_sub` e anote os que aparecem.

::: atencao
**Apagar o dispositivo no Home Assistant não basta.** Enquanto a descoberta estiver ligada no aparelho, ele publica tudo de novo na próxima conexão, e o dispositivo volta. Desligue a descoberta no aparelho primeiro.
:::

## No SIMUT Air {#cap-23-air}

[air]{.img}

O Air dorme entre as leituras e só liga o rádio nos despertares de transmissão ([capítulo 19](#cap-19)). Com a descoberta ligada:

- **A descoberta se repete a cada transmissão.** Cada despertar de transmissão é uma conexão nova, e o aparelho publica todas as mensagens de descoberta de novo antes das medições. Isso soma alguns pacotes ao tempo de rádio de cada despertar.
- **As entidades ficam indisponíveis entre os despertares.** O despertar termina num reinício do processador, sem despedida. O broker publica `offline` quando o **Keep-Alive** vence, e as entidades ficam indisponíveis até o despertar seguinte. Um **Keep-Alive** maior, até 300 s, adia o `offline`.
- **Os valores chegam em rajadas.** As medições guardadas durante o sono saem juntas no despertar, e o Home Assistant as registra com a hora da chegada, não com a hora da medição.
- **O Lote mínimo manda o formato.** Com o **Lote mínimo** acima de 5, toda transmissão sai em array, e o Home Assistant não lê nenhum valor ([Antes de começar](#cap-23-requisitos)).

Para acompanhar um Air com histórico fiel à hora da medição, receba a telemetria num coletor que use o `ts` de cada registro ([capítulo 21](#cap-21-receptores)).

## Limites {#cap-23-limites}

- **Só sensores de medição.** Temperatura, umidade e pressão. A luminosidade não é publicada, porque a telemetria não a leva.
- **Uma entidade de pressão por aparelho**, mesmo com vários sensores de pressão.
- **Só leitura.** O Home Assistant não comanda o aparelho: não reconhece alarme, não abre janela de manutenção, não muda configuração. Para isso existe a API REST ([capítulo 26](#cap-26)).
- **Só o formato JSON, e só mensagens com um registro.** Mensagens em array não atualizam as entidades.
- **A hora é a da chegada.** O Home Assistant não usa o `ts` do registro nem recupera o que ficou para trás quando ele estava fora do ar.
- **Prefixo fixo** `homeassistant`.
- **Nomes de grandeza em inglês**: `Temperature`, `Humidity` e `Pressure`, qualquer que seja o idioma do aparelho.
- **Link de configuração sempre em `http://`.** Com o HTTPS ligado na imagem release, o aparelho não atende em HTTP, e o link do Home Assistant não abre a página. Troque para `https://` no navegador ([capítulo 9](#cap-09-https)).
- **QoS 0.** Todas as mensagens, inclusive as de descoberta, saem com QoS 0 ([capítulo 21](#cap-21-campos-mqtt)).
- **Sobras no broker.** O aparelho não apaga mensagens de slots removidos, de IDs antigos nem de um **Client ID** antigo.
- **Nada durante o AP de configuração.** Enquanto o ponto de acesso de configuração está aberto, o aparelho não envia telemetria, e o Home Assistant não recebe valores ([capítulo 9](#cap-09-ap-aberto)).
- **Nada sem envio.** O aparelho só conecta ao broker quando tem algo a enviar. Com o **Lote mínimo** em 0 e a linha de alarmes desligada, ele nunca conecta, nem para apagar a descoberta.

## Códigos do log de eventos {#cap-23-eventos}

| Código | Descrição | Nível | Contexto e texto |
|---|---|---|---|
| 548 | **Discovery HA atualizado** | INF | Contexto: mensagens publicadas ou apagadas. Texto: `HA discovery published N` ou `HA discovery cleared N`, com `(skipped M)` quando alguma entidade foi pulada |
| 542 | **MQTT conectando** | INF | Texto: o Client ID |
| 35 | **MQTT conectado** | INF | Texto: o servidor |
| 36 | **MQTT desconectado** | ERR ao conectar; WRN quando a conexão cai durante um envio | Motivo no texto ([capítulo 21](#cap-21-conexao-mqtt)) |

O evento 548 vai sempre para a flash, sem o filtro de rotina ([capítulo 16](#cap-16-transicao)). O texto só aparece no console e no syslog ([capítulo 25](#cap-25)).

Uma entidade é pulada quando a chave dela tem aspas ou barra invertida, o que só acontece com um ID de hardware gravado por uma versão antiga, ou quando a mensagem de descoberta passaria de 895 bytes.
