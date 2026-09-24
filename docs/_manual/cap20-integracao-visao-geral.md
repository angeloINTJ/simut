# Visão geral da integração {#cap-20}

Este capítulo mostra tudo o que o SIMUT troca com outros sistemas: o que ele envia por conta própria, o que ele oferece para ser consultado e como cada peça identifica o aparelho. Ele é para o integrador e para a equipe de TI que vão escolher o mecanismo, abrir as portas na rede e escrever o servidor.

## Os dois sentidos do tráfego {#cap-20-sentidos}

O aparelho conversa com servidores de dois jeitos, e eles não dependem um do outro.

- **O aparelho empurra.** Ele abre a conexão e manda dados para um servidor que você indica: as medições, os eventos de alarme, o log de eventos e as mensagens de descoberta do Home Assistant. O servidor que recebe não precisa entrar no aparelho nem ter conta nele.
- **O servidor consulta ou comanda.** O aparelho atende pedidos na porta da interface web: a API REST, as métricas do Prometheus e o anúncio na rede local por mDNS. Para comandar o aparelho, o servidor usa uma conta do aparelho, com sessão.

Um receptor de telemetria, portanto, não comanda nada, e quem comanda não precisa receber telemetria. São dois componentes do seu lado.

::: {.figura #fig-20-integracao tipo="diagrama" arquivo="20-integracao.png" captura="Diagrama com o aparelho SIMUT à esquerda e quatro destinos à direita. Setas saindo do aparelho: 'Telemetria (HTTP/HTTPS POST ou MQTT/MQTTS)' e 'Linha de alarmes (mesmo transporte, com confirmação)' para o coletor; 'Descoberta Home Assistant (MQTT, mensagens retidas)' para o broker; 'Syslog RFC 5424 (UDP 514)' para o coletor de syslog. Setas chegando ao aparelho, na porta web: 'API REST com sessão' vinda do servidor de gestão; 'GET /metrics' vinda do Prometheus; 'mDNS _simut._tcp' como anúncio na rede local. Uma legenda separa 'o aparelho abre a conexão' de 'o servidor abre a conexão'."}
Legenda: os dois sentidos do tráfego. As setas que saem do aparelho são conexões que ele mesmo abre; as que chegam usam a porta da interface web.
:::

## O que o aparelho envia {#cap-20-envia}

| Mecanismo | Para onde | Transporte | Onde configurar | Detalhes |
|---|---|---|---|---|
| Telemetria de medições | Coletor | HTTP ou HTTPS (POST); MQTT ou MQTTS (publish) | Página **Telemetria** | [Capítulo 21](#cap-21) |
| Linha de alarmes | Coletor | O mesmo da telemetria, com confirmação de recebimento | Página **Telemetria** | [Capítulo 22](#cap-22) |
| Home Assistant | Broker MQTT | Mensagens de descoberta retidas, mais a telemetria em JSON | Página **Telemetria**, transporte MQTT | [Capítulo 23](#cap-23) |
| Syslog | Coletor de syslog | RFC 5424 sobre UDP, porta 514 de fábrica | Página **Configurações**, **Syslog Remoto (Auditoria)** | [Capítulo 25](#cap-25) |

A telemetria manda os registros do histórico: um registro por intervalo do histórico, com a hora e o valor de cada canal ativo. A linha de alarmes manda eventos: um limite ultrapassado, um sensor em falha, uma janela de manutenção aberta ou fechada, uma ação feita no painel. As duas usam o mesmo servidor, a mesma porta, a mesma chave de acesso e o mesmo TLS; só o formato e o caminho são próprios de cada uma.

O syslog não é um terceiro transporte de telemetria. Ele manda as linhas do log de eventos a partir de um nível mínimo, uma vez cada, por UDP, sem confirmação e sem nova tentativa.

## O que o aparelho oferece {#cap-20-oferece}

| Mecanismo | Quem pede | Onde | Detalhes |
|---|---|---|---|
| API REST | O servidor de gestão, com uma conta do aparelho | Porta da interface web, rotas `/api/...` | [Capítulo 26](#cap-26) |
| Métricas do Prometheus | O Prometheus, com sessão ou HTTP Basic | `GET /metrics`, na porta da interface web | [Capítulo 24](#cap-24) |
| Anúncio mDNS | Qualquer máquina da rede local | Serviço `_simut._tcp`, com `uid`, `ver`, `env` e `tls` no registro TXT | [Capítulo 27](#cap-27) |

As métricas mostram o estado de agora. O aparelho não guarda nem reenvia nada por esse caminho: quem guarda a série é o Prometheus.

### Em cada imagem {#cap-20-imagens}

| Recurso | release | alpha | Air |
|---|---|---|---|
| Telemetria e linha de alarmes | Sim | Sim | Sim, nos despertares de transmissão ([capítulo 19](#cap-19)) |
| Interface web, API REST e `/metrics` | HTTP, ou só HTTPS com certificado instalado | Só HTTP | Só em M0 ([capítulo 19](#cap-19)) |
| Anúncio mDNS | Sim | Não | Não |

No Air, os alarmes só são avaliados com o aparelho acordado (M0). Num despertar do ciclo (M1), ele lê os sensores e grava o histórico, mas não confere limites nem gera eventos da linha de alarmes. E cada despertar termina num reinício do processador, então a fila, que vive na RAM, não atravessa o sono.

## Identidade do aparelho {#cap-20-identidade}

Cinco valores dizem quem é o aparelho. Só um deles serve de chave.

| Valor | Exemplo | De onde vem | Muda quando |
|---|---|---|---|
| `uid` | `E6614C311B7A2F2D` | Número de série da placa, 16 dígitos hexadecimais em maiúsculas | Não muda com configuração, atualização ou restauração |
| Nome | `simut` | Campo **Nome** da página **Configurações** | Alguém edita |
| MAC | `28:cd:c1:0a:1b:2c` | Endereço Wi-Fi, em minúsculas e com dois-pontos | Não muda |
| Versão | `2.7.1` | O firmware gravado | A cada atualização |
| Variante | `release`, `alpha` ou `air` | A imagem gravada | Ao trocar de imagem |

Use o `uid` como chave primária do aparelho no seu banco. O endereço IP vem do DHCP e muda; o nome é editável e pode se repetir entre aparelhos.

O mesmo `uid` aparece:

- no cabeçalho `X-SIMUT-Uid` de todo POST de telemetria e da linha de alarmes;
- na resposta de `GET /api/status`, campo `uid`;
- no registro TXT do anúncio mDNS, campo `uid`;
- no marcador `{DHT_ID}` do formato **Dinâmico** da telemetria ([capítulo 21](#cap-21)).

### Os cabeçalhos X-SIMUT {#cap-20-cabecalhos}

Todo POST HTTP ou HTTPS que o aparelho faz, de telemetria ou da linha de alarmes, leva quatro cabeçalhos de identidade. O corpo não muda por causa deles.

```http
X-SIMUT-Uid: E6614C311B7A2F2D
X-SIMUT-Ver: 2.7.1
X-SIMUT-Env: release
X-SIMUT-Cfg: 3F2A91C4
```

| Cabeçalho | O que é |
|---|---|
| `X-SIMUT-Uid` | O número de série da placa: a chave do aparelho |
| `X-SIMUT-Ver` | A versão do firmware |
| `X-SIMUT-Env` | A variante da imagem: `release`, `alpha` ou `air` |
| `X-SIMUT-Cfg` | O CRC-32 da configuração em uso, em 8 dígitos hexadecimais em maiúsculas |

Se o `X-SIMUT-Cfg` de um aparelho mudou desde o último POST, alguém mudou a configuração dele. Se `X-SIMUT-Ver` mudou, a atualização entrou. Você descobre as duas coisas sem abrir sessão no aparelho, só guardando os cabeçalhos do POST que já ia chegar.

Compare o `X-SIMUT-Cfg` de um aparelho com os valores anteriores dele mesmo, não com os de outro aparelho. A configuração inclui as contas, e cada aparelho gera sementes aleatórias para elas: dois aparelhos ajustados do mesmo jeito quase nunca têm o mesmo valor.

::: atencao
**Os cabeçalhos não autenticam o aparelho.** Qualquer máquina da rede pode mandar um POST com o `X-SIMUT-Uid` de outro aparelho. O que separa um aparelho legítimo de um impostor é a chave de acesso do campo **API Key** e o TLS ([capítulo 21](#cap-21)).
:::

### No MQTT {#cap-20-identidade-mqtt}

Uma mensagem MQTT não tem cabeçalhos, e o corpo da telemetria não traz o `uid`. No MQTT, o que identifica o aparelho é o tópico. Por isso, dê a cada aparelho um **Tópico** próprio, por exemplo `fabrica/camara1/data`. A mensagem de estado que o aparelho publica ao conectar traz o nome e o IP ([capítulo 21](#cap-21)).

Com o **Client ID** em branco, o aparelho se apresenta ao broker como `simut_` seguido dos seis últimos dígitos do MAC, como `simut_0a1b2c`.

## Qual mecanismo usar {#cap-20-decisao}

| Você precisa de | Use | Capítulo |
|---|---|---|
| Guardar todas as medições num banco, sem buracos | Telemetria, em HTTP e JSON | [21](#cap-21) |
| Falar com um receptor que já existe e tem formato fixo | Telemetria no formato **Dinâmico** | [21](#cap-21) |
| Saber em segundos que um sensor passou do limite ou falhou | Linha de alarmes | [22](#cap-22) |
| Saber quem desligou um alarme, mudou um limite ou abriu uma manutenção no painel | Linha de alarmes | [22](#cap-22) |
| Distinguir um sensor em manutenção de um sensor com defeito | Linha de alarmes (`maint_on` e `maint_off`) | [22](#cap-22) |
| O aparelho no Home Assistant, sem YAML | MQTT com **Home Assistant Discovery** | [23](#cap-23) |
| Gráficos e alertas no Prometheus e no Grafana | `GET /metrics` | [24](#cap-24) |
| Trilha de auditoria fora do aparelho | Syslog | [25](#cap-25) |
| Criar contas, mudar limites ou abrir manutenção a partir do servidor | API REST | [26](#cap-26) |
| Inventário de muitos aparelhos, versão e configuração de cada um | mDNS, `GET /api/status` e os cabeçalhos X-SIMUT | [27](#cap-27) |

A telemetria e a linha de alarmes se completam. A telemetria entrega tudo, no ritmo do lote, e não perde medição: o registro fica na flash até o coletor confirmar. A linha de alarmes entrega pouco e rápido, mas vive na RAM: um reinício perde o que não foi confirmado.

## Rede e portas {#cap-20-rede}

O aparelho é cliente na telemetria, na linha de alarmes, no syslog e no NTP. Para esses, a rede só precisa deixar o aparelho sair; nada precisa entrar nele.

| Tráfego | Quem abre | Protocolo e porta | Observação |
|---|---|---|---|
| Telemetria e linha de alarmes | O aparelho | TCP, na **Porta** da página **Telemetria** | De fábrica, 80. O usual é 80 (HTTP), 443 (HTTPS), 1883 (MQTT) e 8883 (MQTTS) |
| Syslog | O aparelho | UDP, 514 de fábrica | O coletor é um endereço IPv4, não um nome |
| NTP | O aparelho | UDP 123 | Servidor de fábrica: `pool.ntp.org` |
| DNS | O aparelho | UDP 53 | Só quando o servidor é um nome, como `coletor.exemplo.com.br` |
| Interface web, API REST e `/metrics` | O seu servidor | TCP 80 (HTTP); 443 (HTTPS, imagem release com certificado); ou a porta configurada | [Capítulo 9](#cap-09) |
| mDNS | Anúncio local | UDP 5353, multicast | Só na imagem release |

Três regras valem para a telemetria e a linha de alarmes:

- **Sinal fraco adia o envio.** Com o sinal Wi-Fi em −78 dBm ou abaixo, o aparelho não envia telemetria nem a linha de alarmes. As medições continuam gravadas e saem quando o sinal melhora.
- **O coletor tem 4 s para responder.** O aparelho espera a resposta por 4 s. Um coletor mais lento que isso vira falha, e o lote é reenviado.
- **Um envio por vez.** O aparelho não abre conexões paralelas para o mesmo coletor.

::: nota
**Por que o aparelho não envia com sinal fraco.** Num enlace fraco os pacotes se perdem, e cada envio vira uma sequência de esperas e novas tentativas que prende o processador. O aparelho prefere guardar e enviar depois.
:::

## Hora certa: por que o NTP importa {#cap-20-hora}

Toda a integração conta o tempo pela hora do aparelho. Cada registro de telemetria e cada evento da linha de alarmes leva um `ts`: segundos desde 01/01/1970, em UTC, lidos do relógio do aparelho.

Uma hora errada estraga a integração de quatro jeitos:

- **Sem referência de hora, o histórico não grava.** O aparelho pula a gravação e registra o evento 512, **Histórico pulado: sem referência de hora**. Sem histórico, a telemetria não tem o que mandar. O evento 513, **Histórico retomado: referência de hora obtida**, marca a volta.
- **A hora provisória pode carimbar errado.** Até a primeira sincronização, o aparelho usa uma hora provisória guardada na flash (evento 524, **Hora provisória do flash**). O [capítulo 10](#cap-10) explica os limites dessa hora.
- **O cursor da telemetria é um instante.** O aparelho envia o que tem hora depois do último registro confirmado. Registros com hora anterior a 13/09/2020 nunca são enviados, e registros com hora mais de 1 dia à frente do relógio do aparelho ficam para depois ([capítulo 21](#cap-21)).
- **O seu banco ordena por `ts`.** Um aparelho adiantado ou atrasado grava fora de ordem e duplica ou esconde medições na chave (`uid`, `ts`).

Mantenha a sincronização ligada: página **Configurações**, bloco **Data e Hora**, opção **Sincronizar automaticamente via NTP**. Numa rede sem acesso à internet, aponte o aparelho para um servidor NTP local, no campo **Endereço do Servidor** do bloco **Servidor de Hora (NTP)** da página **Rede**. O [capítulo 10](#cap-10) cobre o assunto.

## Segurança, em resumo {#cap-20-seguranca}

- **A telemetria pode levar uma chave de acesso.** O campo **API Key** vira um cabeçalho `Authorization: Bearer ...` ou um cabeçalho com o nome que você escolher ([capítulo 21](#cap-21)). Recuse no coletor o POST que não a trouxer.
- **O TLS cifra e, com certificado, autentica o coletor.** Com **Usar TLS / SSL** ligado e o arquivo `/cert.pem` enviado, o aparelho confere o certificado do coletor. Sem o arquivo, a conexão é cifrada, mas o aparelho aceita qualquer certificado.
- **O aparelho não tem certificado de cliente.** O coletor só reconhece o aparelho pela chave de acesso e pelo `uid`.
- **Mantenha aparelhos e coletor numa rede fechada.** Não exponha o aparelho à internet, nem com porta redirecionada.

O [capítulo 29](#cap-29) trata da segurança por inteiro.
