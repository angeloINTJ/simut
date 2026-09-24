# Prometheus {#cap-24}

Este capítulo mostra como coletar os números do aparelho com o Prometheus: a rota `/metrics`, a conta que ela exige, a lista completa de métricas e exemplos de configuração, consultas e alertas para cadeia fria. É para a equipe de TI que já usa Prometheus e Grafana.

## Como funciona {#cap-24-como}

O aparelho oferece a rota `GET /metrics`, na porta da interface web, no formato de texto do Prometheus. O Prometheus consulta a rota em intervalos regulares e guarda a série.

- **É uma foto do agora.** O aparelho responde com os valores do momento e não guarda nada para depois. Se o Prometheus não consultar, aquele instante se perde; o aparelho não reenvia nada.
- **Não precisa de configuração no aparelho.** Não há opção para ligar ou desligar a rota. Basta uma conta com a permissão certa.
- **Existe nas três imagens.** Na imagem release, a rota segue o protocolo da interface web: HTTP, ou só HTTPS com certificado instalado. Nas imagens alpha e Air, só HTTP. No Air, só com o aparelho acordado ([No SIMUT Air](#cap-24-air)).

Para guardar as medições com a hora exata de cada registro, sem buracos, use a telemetria ([capítulo 21](#cap-21)). O Prometheus serve para painéis, tendências e alertas.

::: {.figura #fig-24-coleta tipo="diagrama" arquivo="24-coleta.png" captura="À esquerda, 'Prometheus (a cada 60 s)'; à direita, 'SIMUT 192.0.2.10'. Seta do Prometheus para o aparelho: 'GET /metrics, Authorization: Basic metricas:…'. Dentro do aparelho, uma caixa 'confere a senha (5.000 rodadas, cerca de 0,7 s)' e uma caixa 'endereço bloqueado?'. Quatro setas de volta: '200 texto do Prometheus', '401 senha errada', '403 conta sem Painel', '429 endereço bloqueado'. Embaixo, uma escada de bloqueio: '1º erro 2 s, 2º 4 s, 3º 8 s … 9º em diante 300 s', com a nota 'mesmo bloqueio da página de entrada'."}
Legenda: uma leitura do Prometheus. O aparelho confere a senha a cada pedido, e as senhas erradas alimentam o mesmo bloqueio da página de entrada.
:::

## Endereço e autenticação {#cap-24-autenticacao}

| Situação | Endereço |
|---|---|
| HTTP, porta de fábrica | `http://192.0.2.10/metrics` |
| HTTP, outra **Porta HTTP** | `http://192.0.2.10:8080/metrics` |
| [release]{.img} HTTPS com certificado instalado | `https://192.0.2.10/metrics` (porta 443) |

As regras de porta e protocolo são as da API REST ([capítulo 26](#cap-26-endereco)).

A rota aceita duas formas de entrar:

- **HTTP Basic**, com a conta e a senha de uma conta do aparelho. É o que o Prometheus usa, porque ele não faz a entrada em dois passos da interface web. A senha vai como foi digitada, e o aparelho calcula o resumo e confere.
- **A sessão da interface web**, pelo cookie ou pelo cabeçalho `Authorization: Bearer` ([capítulo 26](#cap-26-token)). Serve para abrir a rota no navegador já logado, mas não para o Prometheus: a sessão expira com 15 min sem uso e some a cada reinício.

A conta precisa da permissão **Painel** [PERM_DASHBOARD]{.perm}.

| Resposta | Quando |
|---|---|
| `200`, `text/plain; version=0.0.4; charset=utf-8` | Conta e senha certas, numa conta com **Painel** |
| `401` `unauthorized`, com `WWW-Authenticate: Basic realm="SIMUT metrics"` | Sem conta e senha, cabeçalho malformado ou senha errada |
| `403` `forbidden` | Conta certa, mas sem **Painel** |
| `429` `{"ok":false,"err":2,"lockSec":42}` | O endereço de origem está bloqueado por senhas erradas; `lockSec` é o que falta, em segundos |

A resposta de bloqueio não traz o cabeçalho `Retry-After`: leia o `lockSec` do corpo.

::: atencao
**Em HTTP, a senha viaja aberta.** O HTTP Basic só codifica a conta e a senha em base64, e isso vai em todo pedido. Na rede local, qualquer um que capture o tráfego lê a senha. Use uma conta só com **Painel**, que não muda nada no aparelho, e prefira HTTPS na imagem release ([capítulo 9](#cap-09-https)).
:::

## A conta do Prometheus {#cap-24-conta}

Crie uma conta só para o Prometheus, com a permissão **Painel** e nenhuma outra. Assim, uma senha vazada dá acesso de leitura ao estado do aparelho e a mais nada.

1. Na página **Usuários**, crie a conta `metricas` com a permissão **Painel** e sem PIN ([capítulo 8](#cap-08-criar)).
2. Toque em **Salvar e reiniciar** e anote a senha da janela **Senha temporária**.
3. Entre uma vez na interface web com a conta `metricas` e defina a senha definitiva na troca obrigatória ([capítulo 13](#cap-13-troca-obrigatoria)). Escolha uma senha longa: o HTTP Basic aceita até 128 caracteres.
4. Saia da interface web.
5. Grave a senha num arquivo legível só pelo Prometheus, por exemplo `/etc/prometheus/simut_senha`.

::: {.figura #fig-24-conta-metricas tipo="web" arquivo="24-conta-metricas.png" captura="rota /users; largura 1280; sessão admin; formulário Adicionar com Nome metricas, só a caixa Painel marcada, PIN vazio; a conta aparece na tabela com Pendente: Novo, ainda não gravada"}
Legenda: a conta do Prometheus, com a permissão Painel e nenhuma outra.
:::

A senha temporária já funciona no HTTP Basic, sem a troca. Troque-a mesmo assim: ela tem só 8 caracteres e apareceu numa janela da interface web.

Para testar do servidor do Prometheus:

```bash
curl -s -u "metricas:$SIMUT_METRICS_PASS" http://192.0.2.10/metrics | head -4
```

```text
# TYPE simut_build_info gauge
simut_build_info{version="2.7.1",device="simut"} 1
# TYPE simut_uptime_seconds gauge
simut_uptime_seconds 86412
```

## O bloqueio por senha errada {#cap-24-bloqueio}

A conta do HTTP Basic passa pelo mesmo bloqueio da página de entrada ([capítulo 26](#cap-26-bloqueio)):

- cada senha errada bloqueia o endereço de origem por 2 s, 4 s, 8 s e assim por diante, até 300 s a partir do nono erro;
- durante o bloqueio, todo pedido desse endereço recebe `429`, mesmo com a senha certa, e o aparelho nem confere a senha;
- a contagem de erros só volta a zero com uma entrada bem-sucedida pela interface web a partir daquele endereço. Uma leitura bem-sucedida do `/metrics` não zera a contagem;
- o bloqueio vale para o endereço, não para a conta: o servidor do Prometheus e quem abre a interface web a partir da mesma máquina, ou atrás do mesmo NAT, dividem o bloqueio.

Cada senha errada grava o evento 301, **Falha de login**, no log de eventos. O nome da conta vai no texto do evento, que só aparece no console e no syslog ([capítulo 25](#cap-25)). Com uma senha errada no Prometheus, depois do nono erro isso vira um evento a cada 300 s, mais ou menos, sem parar, até alguém corrigir a senha. Os pedidos recusados durante o bloqueio não gravam evento. As leituras bem-sucedidas não gravam nada no log.

::: nota
**Por que o `/metrics` divide o bloqueio com a página de entrada.** Se a rota tivesse contagem própria, ela seria a porta barata para testar senhas, sem o custo que a página de entrada impõe.
:::

Depois de corrigir uma senha errada no Prometheus, entre uma vez na interface web a partir do servidor do Prometheus para zerar a contagem. Só o tempo não zera a contagem: enquanto ela estiver alta, uma única senha errada nova já bloqueia por 300 s.

## Intervalo de coleta {#cap-24-intervalo}

Use `scrape_interval` de 60 s. Não use menos de 15 s.

- **Cada leitura com HTTP Basic confere a senha inteira**, com as mesmas 5.000 rodadas da página de entrada ([capítulo 8](#cap-08-hash)). Medido: cerca de 0,69 s por leitura, seis leituras seguidas (v2.2.13, 19/08/2026).
- **Durante esse tempo, o aparelho não atende outro pedido.** Ele atende um pedido por vez ([capítulo 26](#cap-26-um-por-vez)); uma página aberta na interface web espera a leitura acabar.
- **Os valores mudam devagar.** As medições vêm da média filtrada dos sensores; consultar muito mais rápido que isso não traz informação nova.
- **Dois servidores de Prometheus dobram a carga.** Num par redundante, conte as duas leituras.

Deixe o `scrape_timeout` em 10 s, o padrão do Prometheus. Em HTTPS, o aparelho atende uma conexão segura por vez; as conexões persistentes, ligadas de fábrica, evitam refazer o TLS a cada leitura ([capítulo 9](#cap-09-servidor-web)).

## Configurar o Prometheus {#cap-24-scrape}

Um trabalho de coleta com dois aparelhos em HTTP:

```yaml
scrape_configs:
  - job_name: simut
    scrape_interval: 60s
    scrape_timeout: 10s
    metrics_path: /metrics
    scheme: http
    basic_auth:
      username: metricas
      password_file: /etc/prometheus/simut_senha
    static_configs:
      - targets: ['192.0.2.10:80']
        labels:
          local: camara-fria-1
      - targets: ['192.0.2.11:80']
        labels:
          local: camara-fria-2
```

Para um aparelho release em HTTPS, mude o esquema e indique o certificado:

```yaml
    scheme: https
    tls_config:
      ca_file: /etc/prometheus/simut_ca.pem
```

O certificado instalado no aparelho precisa valer para o endereço usado no alvo. Se ele foi emitido para um nome, use o nome no alvo, ou indique-o em `tls_config.server_name` ([capítulo 9](#cap-09-instalar)).

Dê a cada aparelho um endereço fixo, por reserva no DHCP ou IP estático ([capítulo 9](#cap-09)): o alvo do Prometheus é o endereço. O `uid` do aparelho não aparece nas métricas; o rótulo `device` de `simut_build_info` traz o **Nome**, que pode se repetir entre aparelhos.

## As métricas {#cap-24-metricas}

A resposta tem 45 famílias de métricas. Cada uma vem com a linha `# TYPE`, sem `# HELP`. Os contadores (`counter`) começam em zero a cada reinício do aparelho; as funções `rate()` e `increase()` do Prometheus já tratam essa volta a zero.

### Aparelho {#cap-24-m-aparelho}

| Métrica | Tipo | Unidade | O que é |
|---|---|---|---|
| `simut_build_info{version,device}` | gauge | — | Sempre 1. Os rótulos trazem a versão do firmware e o **Nome** do aparelho |
| `simut_uptime_seconds` | gauge | s | Tempo desde o último reinício |
| `simut_ntp_synced` | gauge | 0 ou 1 | Veja o aviso abaixo |

::: atencao
**`simut_ntp_synced` não diz se a hora foi acertada.** Na v2.7.1, a métrica vale 1 também antes da primeira sincronização, porque o aparelho conta a hora provisória como hora certa ([capítulo 10](#cap-10)). Não a use para alertar sobre falta de NTP. Para isso, acompanhe os eventos 512 e 513 do log de eventos, por exemplo pelo syslog ([capítulo 25](#cap-25)).
:::

### Memória e armazenamento {#cap-24-m-memoria}

| Métrica | Tipo | Unidade | O que é |
|---|---|---|---|
| `simut_heap_free_bytes` | gauge | bytes | Memória RAM livre agora |
| `simut_heap_total_bytes` | gauge | bytes | Tamanho total da área de memória dinâmica |
| `simut_heap_min_bytes` | gauge | bytes | Menor memória livre vista desde o reinício |
| `simut_heap_largest_block_bytes` | gauge | bytes | Maior bloco livre contínuo agora |
| `simut_heap_largest_block_min_bytes` | gauge | bytes | Menor valor do maior bloco desde o reinício |
| `simut_fs_used_bytes` | gauge | bytes | Espaço usado no sistema de arquivos |
| `simut_fs_total_bytes` | gauge | bytes | Tamanho do sistema de arquivos |

O aparelho relê o sistema de arquivos no máximo a cada 10 s; entre uma leitura e outra, as duas métricas `simut_fs_*` repetem o último valor.

### Rede {#cap-24-m-rede}

| Métrica | Tipo | Unidade | O que é |
|---|---|---|---|
| `simut_wifi_connected` | gauge | 0 ou 1 | 1 com Wi-Fi conectado e sinal acima de −78 dBm, a mesma condição que a telemetria exige para enviar |
| `simut_wifi_rssi_dbm` | gauge | dBm | Sinal Wi-Fi agora |
| `simut_wifi_reconnects_total` | counter | — | Vezes que o aparelho obteve IP desde o reinício, contando a primeira |
| `simut_mqtt_reconnects_total` | counter | — | Conexões ao broker MQTT desde o reinício, contando a primeira |
| `simut_mqtt_connected` | gauge | 0 ou 1 | 1 com o cliente MQTT conectado. No transporte HTTP, sempre 0 |

### Telemetria {#cap-24-m-telemetria}

| Métrica | Tipo | Unidade | O que é |
|---|---|---|---|
| `simut_telemetry_enabled` | gauge | 0 ou 1 | 1 com o **Lote mínimo (registros)** acima de 0 ([capítulo 21](#cap-21-lotes)) |
| `simut_telemetry_pending_records` | gauge | registros | Registros esperando envio, os **Registros Pendentes** do **Painel de Controle** |
| `simut_telemetry_sent_total` | counter | — | Envios bem-sucedidos |
| `simut_telemetry_failed_total` | counter | — | Envios que falharam |
| `simut_telemetry_retries_total` | counter | — | Novas tentativas |
| `simut_telemetry_sent_bytes_total` | counter | bytes | Bytes de medição enviados |
| `simut_telemetry_last_latency_seconds` | gauge | s | Duração do último envio, com 3 casas |

### Linha de alarmes {#cap-24-m-alarmes}

| Métrica | Tipo | Unidade | O que é |
|---|---|---|---|
| `simut_alarm_line_enabled` | gauge | 0 ou 1 | 1 com a linha de alarmes ligada ([capítulo 22](#cap-22)) |
| `simut_alarm_pending_records` | gauge | registros | Registros na fila da linha, esperando confirmação |
| `simut_alarm_queued_total` | counter | — | Registros aceitos na fila |
| `simut_alarm_sent_total` | counter | — | Registros enviados |
| `simut_alarm_acked_total` | counter | — | Confirmações recebidas do coletor |
| `simut_alarm_failed_total` | counter | — | Ciclos de envio que falharam |
| `simut_alarm_dropped_total` | counter | — | Registros perdidos porque a fila estava cheia |
| `simut_alarm_error_records_total` | counter | — | Registros gerados por sensor em falha |

### Sensores e configuração {#cap-24-m-sensores}

| Métrica | Tipo | Unidade | O que é |
|---|---|---|---|
| `simut_sensor_reads_ok_total` | counter | — | Leituras bem-sucedidas, somando todos os sensores |
| `simut_sensor_reads_error_total` | counter | — | Leituras com erro, somando todos os sensores |
| `simut_config_saves_total` | counter | — | Pedidos de gravação da configuração, inclusive os que não mudavam nada |

### Diagnóstico interno {#cap-24-m-diagnostico}

Estas métricas acompanham a gravação na flash, o segundo núcleo do processador e os pedidos web interrompidos. Servem ao suporte, num teste longo de estabilidade; no dia a dia, não precisam de alerta.

| Métrica | Tipo | Rótulos | O que é |
|---|---|---|---|
| `simut_flash_ops_total` | counter | — | Operações de gravação na flash |
| `simut_flash_op_max_seconds` | gauge | — | A operação de flash mais longa, em segundos |
| `simut_flash_ops_over50ms_total` | counter | — | Operações de flash com mais de 50 ms |
| `simut_flash_unguarded_ops_total` | counter | — | Operações de flash feitas com o segundo núcleo ativo |
| `simut_core1_heartbeat_age_seconds` | gauge | — | Tempo desde o último sinal de vida do segundo núcleo |
| `simut_core1_launches_total` | counter | — | Partidas do segundo núcleo |
| `simut_core1_kills_total` | counter | `cause`: `lockout`, `health`, `quiet` | Paradas forçadas do segundo núcleo, por motivo |
| `simut_web_aborts_total` | counter | `cause`: `deadline`, `guard`, `disconnect` | Respostas web interrompidas, por motivo |

### Medições por slot {#cap-24-m-medicoes}

| Métrica | Tipo | Unidade | Casas | O que é |
|---|---|---|---|---|
| `simut_sensor_ok` | gauge | 0 ou 1 | — | 0 com o sensor em falha, 1 nos outros casos |
| `simut_temperature_celsius` | gauge | °C | 2 | Temperatura |
| `simut_humidity_percent` | gauge | % | 1 | Umidade relativa |
| `simut_pressure_hpa` | gauge | hPa | 1 | Pressão |

As quatro têm os mesmos rótulos:

| Rótulo | Valor | Exemplo |
|---|---|---|
| `slot` | Número do slot, de 0 a 15 | `"0"` |
| `hwid` | ID de hardware do slot ([capítulo 6](#cap-06-editor)) | `"STM0001"` |
| `name` | Nome do slot | `"Câmara 1"` |

- **O valor é o que o aparelho mostra.** É a média filtrada das leituras, já com a calibração, a mesma que o **Painel de Controle** mostra ([capítulo 6](#cap-06-intervalos)).
- **Cada slot ativo aparece em `simut_sensor_ok`.** Só os slots com o sensor em bom estado aparecem nas outras três. Um sensor em falha fica com `simut_sensor_ok` em 0 e some das métricas de medição até se recuperar. Um valor ainda não lido também não aparece.
- **A pressão aparece por slot.** Ao contrário da telemetria, que leva um único valor de pressão, cada BME280 e cada BMP280 tem a sua linha.
- **Sem sensores ativos**, as linhas `# TYPE` das quatro famílias continuam lá, sem valores.

::: nota
**Use o `hwid` nas regras, não o `name`.** O nome do slot é texto livre e pode ser editado; quando ele muda, o Prometheus passa a ver outra série. O ID de hardware só muda quando o sensor físico muda.
:::

### Um exemplo completo {#cap-24-exemplo}

A resposta de um aparelho com um DS18B20 no slot 0 (**Câmara 1**, `STM0001`) e um BME280 no slot 1 (**Sala**, `BME28001`), com telemetria MQTT e a linha de alarmes desligada:

```text
# TYPE simut_build_info gauge
simut_build_info{version="2.7.1",device="simut"} 1
# TYPE simut_uptime_seconds gauge
simut_uptime_seconds 86412
# TYPE simut_ntp_synced gauge
simut_ntp_synced 1
# TYPE simut_heap_free_bytes gauge
simut_heap_free_bytes 40796
# TYPE simut_heap_total_bytes gauge
simut_heap_total_bytes 229376
# TYPE simut_heap_min_bytes gauge
simut_heap_min_bytes 31204
# TYPE simut_heap_largest_block_bytes gauge
simut_heap_largest_block_bytes 29390
# TYPE simut_heap_largest_block_min_bytes gauge
simut_heap_largest_block_min_bytes 17680
# TYPE simut_fs_used_bytes gauge
simut_fs_used_bytes 385024
# TYPE simut_fs_total_bytes gauge
simut_fs_total_bytes 1048576
# TYPE simut_wifi_connected gauge
simut_wifi_connected 1
# TYPE simut_wifi_rssi_dbm gauge
simut_wifi_rssi_dbm -61
# TYPE simut_wifi_reconnects_total counter
simut_wifi_reconnects_total 1
# TYPE simut_mqtt_reconnects_total counter
simut_mqtt_reconnects_total 2
# TYPE simut_mqtt_connected gauge
simut_mqtt_connected 1
# TYPE simut_telemetry_enabled gauge
simut_telemetry_enabled 1
# TYPE simut_telemetry_pending_records gauge
simut_telemetry_pending_records 0
# TYPE simut_telemetry_sent_total counter
simut_telemetry_sent_total 1440
# TYPE simut_telemetry_failed_total counter
simut_telemetry_failed_total 3
# TYPE simut_telemetry_retries_total counter
simut_telemetry_retries_total 3
# TYPE simut_telemetry_sent_bytes_total counter
simut_telemetry_sent_bytes_total 129600
# TYPE simut_telemetry_last_latency_seconds gauge
simut_telemetry_last_latency_seconds 0.412
# TYPE simut_alarm_line_enabled gauge
simut_alarm_line_enabled 0
# TYPE simut_alarm_pending_records gauge
simut_alarm_pending_records 0
# TYPE simut_alarm_queued_total counter
simut_alarm_queued_total 0
# TYPE simut_alarm_sent_total counter
simut_alarm_sent_total 0
# TYPE simut_alarm_acked_total counter
simut_alarm_acked_total 0
# TYPE simut_alarm_failed_total counter
simut_alarm_failed_total 0
# TYPE simut_alarm_dropped_total counter
simut_alarm_dropped_total 0
# TYPE simut_alarm_error_records_total counter
simut_alarm_error_records_total 0
# TYPE simut_sensor_reads_ok_total counter
simut_sensor_reads_ok_total 172788
# TYPE simut_sensor_reads_error_total counter
simut_sensor_reads_error_total 12
# TYPE simut_config_saves_total counter
simut_config_saves_total 1
# TYPE simut_flash_ops_total counter
simut_flash_ops_total 3120
# TYPE simut_flash_op_max_seconds gauge
simut_flash_op_max_seconds 0.046
# TYPE simut_flash_ops_over50ms_total counter
simut_flash_ops_over50ms_total 0
# TYPE simut_flash_unguarded_ops_total counter
simut_flash_unguarded_ops_total 0
# TYPE simut_core1_heartbeat_age_seconds gauge
simut_core1_heartbeat_age_seconds 0.012
# TYPE simut_core1_launches_total counter
simut_core1_launches_total 1
# TYPE simut_core1_kills_total counter
simut_core1_kills_total{cause="lockout"} 0
simut_core1_kills_total{cause="health"} 0
simut_core1_kills_total{cause="quiet"} 0
# TYPE simut_web_aborts_total counter
simut_web_aborts_total{cause="deadline"} 0
simut_web_aborts_total{cause="guard"} 0
simut_web_aborts_total{cause="disconnect"} 0
# TYPE simut_sensor_ok gauge
simut_sensor_ok{slot="0",hwid="STM0001",name="Câmara 1"} 1
simut_sensor_ok{slot="1",hwid="BME28001",name="Sala"} 1
# TYPE simut_temperature_celsius gauge
simut_temperature_celsius{slot="0",hwid="STM0001",name="Câmara 1"} 4.25
simut_temperature_celsius{slot="1",hwid="BME28001",name="Sala"} 22.81
# TYPE simut_humidity_percent gauge
simut_humidity_percent{slot="1",hwid="BME28001",name="Sala"} 55.2
# TYPE simut_pressure_hpa gauge
simut_pressure_hpa{slot="1",hwid="BME28001",name="Sala"} 1013.4
```

Os números acima são ilustrativos; a ordem, os nomes, os rótulos e as casas decimais são os que o aparelho usa.

## Consultas úteis {#cap-24-consultas}

| Pergunta | Consulta |
|---|---|
| Temperatura agora, por sensor | `simut_temperature_celsius` |
| Máxima das últimas 24 h de um sensor | `max_over_time(simut_temperature_celsius{hwid="STM0001"}[24h])` |
| Fração das últimas 24 h acima de 8 °C | `avg_over_time((simut_temperature_celsius{hwid="STM0001"} > bool 8)[24h:1m])` |
| Aparelhos por versão | `count by (version) (simut_build_info)` |
| Falhas de telemetria por hora | `increase(simut_telemetry_failed_total[1h])` |
| Leituras com erro por hora, por aparelho | `increase(simut_sensor_reads_error_total[1h])` |
| Menor memória livre desde o reinício | `simut_heap_min_bytes` |

A fração acima de 8 °C vai de 0 a 1: multiplique por 1.440 para ter os minutos fora da faixa num dia.

## Alertas para cadeia fria {#cap-24-alertas}

Um arquivo de regras com os alertas mais comuns. Ajuste a faixa, o `hwid` e os tempos ao seu caso.

```yaml
groups:
  - name: simut-cadeia-fria
    rules:
      - alert: SimutTemperaturaForaDaFaixa
        expr: |
          simut_temperature_celsius{hwid="STM0001"} > 8
          or simut_temperature_celsius{hwid="STM0001"} < 2
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "{{ $labels.name }} em {{ $value }} °C, fora de 2 a 8 °C"

      - alert: SimutForaDoAr
        expr: up{job="simut"} == 0
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "O aparelho {{ $labels.instance }} não responde ao /metrics"

      - alert: SimutSensorEmFalha
        expr: simut_sensor_ok == 0
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "Sensor {{ $labels.hwid }} ({{ $labels.name }}) em falha"

      - alert: SimutLeituraAusente
        expr: absent_over_time(simut_temperature_celsius{hwid="STM0001"}[10m])
        labels:
          severity: warning
        annotations:
          summary: "Nenhuma temperatura válida de STM0001 em 10 min"

      - alert: SimutLeiturasParadas
        expr: increase(simut_sensor_reads_ok_total[10m]) == 0
        labels:
          severity: warning
        annotations:
          summary: "{{ $labels.instance }} não faz leitura válida há 10 min"

      - alert: SimutTelemetriaAtrasada
        expr: simut_telemetry_enabled == 1 and simut_telemetry_pending_records > 60
        for: 30m
        labels:
          severity: warning
        annotations:
          summary: "{{ $labels.instance }} com {{ $value }} registros sem envio"

      - alert: SimutReiniciou
        expr: simut_uptime_seconds < 600
        labels:
          severity: info
        annotations:
          summary: "{{ $labels.instance }} reiniciou há menos de 10 min"

      - alert: SimutSinalFraco
        expr: simut_wifi_rssi_dbm <= -78
        for: 15m
        labels:
          severity: warning
        annotations:
          summary: "Sinal Wi-Fi de {{ $labels.instance }} em {{ $value }} dBm"
```

- **Temperatura fora da faixa.** O `for: 5m` evita alarme por uma porta aberta por um instante. Um alarme que precisa de segundos, com registro de quem agiu, é papel da linha de alarmes ([capítulo 22](#cap-22)).
- **Fora do ar.** A métrica `up` é do próprio Prometheus: vale 0 quando a leitura falha, inclusive por senha errada ou bloqueio.
- **Leitura ausente.** Dispara quando o valor some das métricas: sensor em falha, sensor ainda sem leitura válida, ou aparelho fora do ar. O alerta **SimutForaDoAr** separa o último caso.
- **Leituras paradas.** Olha o contador de todas as leituras do aparelho; se ele não sobe em 10 min, nenhum sensor está sendo lido.
- **Telemetria atrasada.** Registros acumulando no aparelho significam que o coletor não está recebendo. Com o histórico a cada minuto, 60 registros são uma hora de atraso.
- **Sinal fraco.** Com −78 dBm ou menos, o aparelho segura a telemetria ([capítulo 20](#cap-20-rede)).

## No SIMUT Air {#cap-24-air}

[air]{.img}

O Air só atende na rede quando está acordado em M0 ([capítulo 19](#cap-19)). Nos despertares do ciclo (M1) e durante o sono, a rota não responde, e o Prometheus registra o alvo como fora do ar.

Uma leitura com conta e senha certas conta como uso do aparelho e reinicia o prazo de inatividade que leva o Air de volta ao sono (de fábrica, 300 s). Um Prometheus que consulta um Air em M0 com intervalo menor que esse prazo o mantém acordado, com o rádio ligado, até a bateria acabar. Ligado ao carregador, o Air não dorme de qualquer forma. Para acompanhar um Air, use a telemetria ([capítulo 21](#cap-21)) e deixe-o fora do Prometheus, ou só o consulte ligado ao carregador.

## Limites {#cap-24-limites}

- **Só o agora.** Não há histórico nem reenvio por esta rota. O que o Prometheus não consultou, perdeu.
- **Uma conferência de senha por leitura.** Cerca de 0,69 s de aparelho ocupado a cada leitura com HTTP Basic.
- **Sem `uid` nas métricas.** Identifique o aparelho pelo alvo ou por rótulos que você mesmo põe na configuração.
- **`simut_ntp_synced` sempre 1** na v2.7.1 ([Aparelho](#cap-24-m-aparelho)).
- **Sem `# HELP`**, só `# TYPE`.
- **Contadores zerados a cada reinício.**
- **Nenhuma métrica de alarme por slot.** O estado de alarme de cada sensor não sai por aqui; compare os valores com a sua faixa nas regras do Prometheus, ou use a linha de alarmes.
- **Com o AP de configuração aberto**, o aparelho só atende pela rede do próprio AP, e o Prometheus da rede local não o alcança ([capítulo 9](#cap-09-ap-aberto)).
