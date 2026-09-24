# Syslog {#cap-25}

Este capítulo mostra como mandar o log de eventos do aparelho para um coletor syslog, no formato RFC 5424 sobre UDP, e como receber, guardar e filtrar essas linhas com rsyslog, syslog-ng ou journald. É para a equipe de TI que precisa de uma trilha de auditoria fora do aparelho.

## O que o syslog manda {#cap-25-o-que}

O aparelho manda uma cópia de cada evento do log de eventos ([capítulo 16](#cap-16)), a partir do nível mínimo escolhido, como uma linha de texto num datagrama UDP. Não há confirmação, nova tentativa nem fila na flash.

- **Não é telemetria.** O syslog leva eventos, como uma entrada, uma falha de rede ou uma mudança de configuração. As medições saem pela telemetria ([capítulo 21](#cap-21)).
- **Não passa pelo filtro de rotina.** O log de eventos só grava um evento de rotina quando o estado muda ([capítulo 16](#cap-16-transicao)). O syslog manda todas as ocorrências: com o nível **Info**, cada envio de telemetria vira uma linha.
- **Leva o texto do evento.** O registro na flash guarda só o código e o contexto. A linha do syslog traz também a descrição e o detalhe, como o nome da conta numa falha de entrada.
- **Existe nas três imagens.** No Air, só com o aparelho acordado ([No SIMUT Air](#cap-25-air)).

::: {.figura #fig-25-caminho tipo="diagrama" arquivo="25-caminho.png" captura="Da esquerda para a direita: 'evento (qualquer núcleo)' → 'nível ≥ Nível Mínimo?' → 'fila de 8 linhas na RAM (a mais antiga sai quando enche)' → 'laço principal, núcleo 0: até 8 linhas por passada, só com Wi-Fi conectado e fora do AP' → 'UDP para 192.0.2.10:514' → 'coletor (rsyslog, syslog-ng)'. Marcas vermelhas de perda: 'antes do encaminhamento subir, no boot', 'fila cheia', 'envio falhou', 'datagrama perdido na rede', 'travamento ou falta de energia'. Uma seta lateral: 'reinício pedido (quase todos): esvazia a fila antes de reiniciar'. Embaixo, em paralelo, 'log de eventos na flash (filtro de rotina, só código e contexto)'."}
Legenda: o caminho de uma linha de syslog e os pontos onde ela pode se perder. O log de eventos local segue por outro caminho e não depende do syslog.
:::

## O formato da linha {#cap-25-formato}

Cada datagrama leva uma linha:

```text
<PRI>1 TIMESTAMP HOSTNAME APP-NAME - MSGID [simut@32473 ctx="…" core="…" up="…"] MSG
```

| Campo | Valor |
|---|---|
| `PRI` | Facilidade `local0` (16) × 8 + a severidade: `<135>` a `<130>` |
| `1` | Versão do formato RFC 5424 |
| `TIMESTAMP` | Data e hora em UTC, com `Z` e segundos inteiros, como `2026-09-23T13:01:00Z`. Antes de o aparelho acertar a hora, `-` |
| `HOSTNAME` | O **Nome** do aparelho ([capítulo 5](#cap-05-identidade)) |
| `APP-NAME` | O módulo que registrou o evento, como `SEC`, `NET` ou `TEL` |
| `PROCID` | Sempre `-` |
| `MSGID` | O código do evento, em números, como `301` |
| Dados estruturados | `ctx`: o contexto do evento; `core`: o núcleo, `0` ou `1`; `up`: segundos desde o último reinício |
| `MSG` | A descrição do evento e, quando há, `: ` e o detalhe |

Os níveis do log viram severidades do syslog assim:

| Nível no aparelho | Severidade | `PRI` |
|---|---|---|
| Depuração (`DBG`) | 7, Debug | `<135>` |
| Informação (`INF`) | 6, Informational | `<134>` |
| Aviso (`WRN`) | 4, Warning | `<132>` |
| Erro (`ERR`) | 3, Error | `<131>` |
| Fatal (`FTL`) | 2, Critical | `<130>` |

Detalhes que importam para o coletor:

- **O `MSGID` é a chave.** O código não muda com o idioma, e a lista completa está no [apêndice B](#ap-b). Filtre por ele, não pelo texto.
- **O texto sai no idioma do aparelho.** Com o pacote pt-BR, a descrição e boa parte dos detalhes saem em português; sem pacote, em inglês ([capítulo 13](#cap-13-idioma)).
- **O `HOSTNAME` é um nome, não uma chave.** Espaços, letras acentuadas e qualquer caractere fora do ASCII visível viram `-`: **Câmara 1** sai como `C--mara-1`. Com o **Nome** vazio, sai `-`. O `uid` do aparelho não vai na linha ([capítulo 20](#cap-20-identidade)); dê nomes únicos aos aparelhos ou identifique-os pelo IP de origem.
- **`simut@32473` é um identificador de exemplo.** O número 32473 é o que a IANA reserva para documentação. Os coletores o aceitam como identificador de dados estruturados válido.
- **Tamanho.** A linha tem no máximo 255 bytes, e o `MSG`, no máximo 191. Um detalhe mais longo é cortado.
- **Caracteres de controle** no texto viram espaço. Acentos passam como UTF-8.
- **Sem hora certa, `-`.** Enquanto o relógio do aparelho não foi acertado pelo NTP ou à mão, o campo `TIMESTAMP` sai como `-`, e não com uma data provisória. Nesse caso, use a hora de chegada ao coletor ([capítulo 10](#cap-10)).

### Exemplos {#cap-25-exemplos}

Com o **Nome** `simut` e o pacote pt-BR:

```text
<134>1 2026-09-23T13:00:05Z simut TEL - 30 [simut@32473 ctx="200" core="0" up="86412"] Telemetria enviada: HTTP OK: 1834 bytes, code 200
<132>1 2026-09-23T13:01:00Z simut SEC - 301 [simut@32473 ctx="0" core="0" up="86467"] Falha de login: Falha de login: metricas
<132>1 2026-09-23T13:02:10Z simut SEC - 303 [simut@32473 ctx="0" core="0" up="86537"] Config alterada: Admin aplicou alterações — reiniciando
<134>1 2026-09-23T13:04:41Z simut TEL - 548 [simut@32473 ctx="4" core="0" up="42"] Discovery HA atualizado: HA discovery published 4
<132>1 - simut SEC - 301 [simut@32473 ctx="0" core="0" up="95"] Falha de login: Falha de login: metricas
```

- A primeira linha é um envio de telemetria por HTTP; o contexto é o código HTTP da resposta.
- A segunda é uma senha errada no `/metrics` ([capítulo 24](#cap-24-bloqueio)); o nome da conta só existe nesta linha e no console.
- A terceira é uma gravação pela interface web que reiniciou o aparelho; o contexto é o número da conta que gravou, 0 para o `admin`.
- A quarta é a descoberta do Home Assistant ([capítulo 23](#cap-23-eventos)).
- A última mostra um aparelho sem hora acertada.

::: nota
**Por que a descrição parece repetida.** A descrição vem da tabela de códigos, e o detalhe vem de quem registrou o evento, que às vezes repete a mesma ideia. No código 301, a descrição é **Falha de login** e o detalhe é `Falha de login: <conta>`.
:::

## Configurar {#cap-25-configurar}

As opções ficam na página **Configurações**, seção **Syslog Remoto (Auditoria)** (*Remote Syslog (Audit Trail)*), descrita no [capítulo 5](#cap-05-syslog).

| Campo | Chave em `commit_all` | Console | Faixa | Fábrica | Aplicação |
|---|---|---|---|---|---|
| **Habilitar encaminhamento syslog** (*Enable syslog forwarding*) | `slog_en` | Não há comando | Ligado ou desligado | Desligado | Reinicia (grupo `web`) |
| **IP do Coletor** (*Collector IP*) | `slog_srv` | Não há comando | IPv4, como `192.0.2.10`; vazio desliga | Vazio | Reinicia (grupo `web`) |
| **Porta UDP** (*UDP Port*) | `slog_port` | Não há comando | 1 a 65535 | 514 | Reinicia (grupo `web`) |
| **Nível Mínimo** (*Minimum Level*) | `slog_lvl` | Não há comando | **Debug** (0), **Info** (1), **Warning** (2), **Error** (3) ou **Fatal** (4) | **Info** | Reinicia (grupo `web`) |

- **O coletor é um endereço IPv4.** O aparelho não aceita nome, como `coletor.exemplo.com.br`, porque guarda o endereço em 4 bytes e não faz consulta de DNS para o syslog. Um valor que não é IPv4 volta como recusado ([capítulo 5](#cap-05-recusados)).
- **O interruptor e o endereço são independentes.** Com o interruptor ligado e o **IP do Coletor** vazio, nada sai. O interruptor continua ligado, e basta preencher o endereço depois.
- **As quatro chaves vão na seção `sys`.** Uma chave que não vem no pedido mantém o valor gravado. O nível é o número entre parênteses.
- **Não há comando no console** para estas opções. Use a interface web ou `POST /api/commit_all` ([capítulo 26](#cap-26-commit)).

Para ligar:

1. Abra a página **Configurações** e vá até **Syslog Remoto (Auditoria)**.
2. Ligue **Habilitar encaminhamento syslog**.
3. Em **IP do Coletor**, digite o IPv4 do coletor, por exemplo `192.0.2.10`.
4. Em **Porta UDP**, deixe `514` ou use a porta do seu coletor.
5. Em **Nível Mínimo**, escolha o nível ([Qual nível usar](#cap-25-nivel)).
6. Toque em **Salvar e reiniciar** e confirme.

::: nota
**Por que o aparelho reinicia.** O encaminhamento lê as opções uma vez, ao ligar. **Aplicar agora** também grava e reinicia, e **Testar** ignora estas opções, porque o ensaio não as enxerga ([capítulo 5](#cap-05-grupos)). Use **Salvar e reiniciar**.
:::

Pela API:

```json
{"sys":{"slog_en":true,"slog_srv":"192.0.2.10","slog_port":514,"slog_lvl":2}}
```

`GET /api/config` devolve as mesmas quatro chaves, com `slog_srv` vazio (`""`) quando não há coletor.

### Qual nível usar {#cap-25-nivel}

| Nível | O que chega | Volume |
|---|---|---|
| **Debug** | Tudo, inclusive eventos de depuração que o log de eventos nunca grava | Alto |
| **Info** | Funcionamento normal e todos os problemas: entradas e saídas, cada envio de telemetria, cada conexão | Alto: uma linha por envio de telemetria, no mínimo |
| **Warning** | Problemas e recusas: senhas erradas, acessos negados, mudanças de configuração, falhas de rede | Baixo |
| **Error** | Só falhas | Muito baixo |
| **Fatal** | Nada. Os únicos eventos fatais são os registros de partida, gravados antes de o encaminhamento começar | Nenhum |

Para uma trilha de auditoria completa, use **Info**: as entradas bem-sucedidas (código 300) são de nível Informação. Com **Warning**, você perde as entradas bem-sucedidas, mas guarda as senhas erradas (301), os acessos negados (302) e as mudanças de configuração (303).

## Receber com rsyslog {#cap-25-rsyslog}

O rsyslog reconhece o formato RFC 5424 sem módulo extra. Um arquivo `/etc/rsyslog.d/30-simut.conf` que recebe na porta 514 e grava um arquivo por aparelho:

```text
module(load="imudp")
input(type="imudp" port="514" ruleset="simut")

template(name="SimutPorAparelho" type="string" string="/var/log/simut/%HOSTNAME%.log")

ruleset(name="simut") {
    action(type="omfile" dynaFile="SimutPorAparelho" template="RSYSLOG_SyslogProtocol23Format")
}
```

1. Crie o arquivo acima.
2. Reinicie o rsyslog: `sudo systemctl restart rsyslog`.
3. Libere a porta UDP 514 no firewall do coletor, só para os endereços dos aparelhos.
4. Ligue o encaminhamento no aparelho e espere o reinício.
5. Acompanhe: `sudo tail -f /var/log/simut/simut.log`.

O modelo `RSYSLOG_SyslogProtocol23Format` grava a linha no mesmo formato RFC 5424 em que ela chegou. Se o `TIMESTAMP` veio como `-`, a hora de chegada está na propriedade `timegenerated` do rsyslog.

## Receber com syslog-ng {#cap-25-syslog-ng}

O `syslog()` do syslog-ng lê o formato RFC 5424 e separa os dados estruturados. Um arquivo `/etc/syslog-ng/conf.d/simut.conf`:

```text
source s_simut {
    syslog(ip("0.0.0.0") port(514) transport("udp"));
};

destination d_simut {
    file("/var/log/simut/${HOST}.log" create-dirs(yes)
         template("${R_ISODATE} ${HOST} ${PROGRAM} ${MSGID} ctx=${.SDATA.simut@32473.ctx} ${MESSAGE}\n"));
};

log { source(s_simut); destination(d_simut); };
```

- `${PROGRAM}` é o módulo, e `${MSGID}` é o código do evento.
- `${.SDATA.simut@32473.ctx}` é o contexto; troque `ctx` por `core` ou `up` para os outros campos.
- `${R_ISODATE}` é a hora de chegada ao coletor; `${ISODATE}` é a hora que veio na linha.

## Ver no journald {#cap-25-journald}

O journald não recebe syslog pela rede. Para ver as linhas com `journalctl`, receba-as com o rsyslog e repasse-as ao journal com o módulo `omjournal`:

```text
module(load="imudp")
module(load="omjournal")
input(type="imudp" port="514" ruleset="simut-journal")

ruleset(name="simut-journal") {
    action(type="omjournal")
}
```

Depois de reiniciar o rsyslog, `journalctl -f` mostra as linhas chegando. Guarde uma cópia em arquivo também, como na seção do rsyslog: o arquivo mantém a linha RFC 5424 completa, com os dados estruturados.

## Filtrar por código {#cap-25-filtrar}

Filtre pelo `MSGID`, que é o código do evento e não muda com o idioma. Alguns códigos úteis numa trilha de auditoria:

| Código | Descrição |
|---|---|
| 300 | **Login bem-sucedido** (também as saídas; o detalhe diz qual) |
| 301 | **Falha de login** |
| 302 | **Acesso não autorizado** |
| 303 | **Config alterada** |
| 31 | **Falha de telemetria** |
| 36 | **MQTT desconectado** |
| 512 | **Histórico pulado: sem referência de hora** |
| 513 | **Histórico retomado: referência de hora obtida** |

A lista completa está no [apêndice B](#ap-b).

No rsyslog, separe os eventos de segurança num arquivo próprio, dentro do `ruleset`:

```text
if $msgid == "301" or $msgid == "302" or $msgid == "303" then {
    action(type="omfile" file="/var/log/simut/seguranca.log" template="RSYSLOG_SyslogProtocol23Format")
}
```

Outras propriedades úteis do rsyslog: `$app-name` (o módulo), `$hostname` (o **Nome**), `$fromhost-ip` (o endereço de origem) e `$syslogseverity` (a severidade, de 0 a 7).

No syslog-ng, o mesmo filtro:

```text
filter f_simut_seguranca { match("^30[1-3]$" value("MSGID")); };
destination d_simut_seguranca { file("/var/log/simut/seguranca.log"); };
log { source(s_simut); filter(f_simut_seguranca); destination(d_simut_seguranca); };
```

## O que se perde, e quando a linha sai {#cap-25-perdas}

**Quando sai.** O aparelho guarda a linha numa fila de 8 linhas na RAM e a manda na passada seguinte do laço principal, que roda o tempo todo, inclusive com um menu aberto no painel. Cada passada manda até 8 linhas. Ao contrário da telemetria, o syslog não espera sinal forte: basta o Wi-Fi estar conectado.

**O que se perde:**

- **Os eventos do começo do boot.** O encaminhamento só começa depois que a rede e a telemetria sobem. Os registros de partida, inclusive o que explica o reinício (código 1, **Boot do sistema**, com a autópsia de um travamento), ficam só no log de eventos local ([capítulo 16](#cap-16-travamento)).
- **O excesso da fila.** Se mais de 8 linhas esperam, a mais antiga sai para dar lugar à nova. Isso acontece no boot, com o Wi-Fi fora do ar ou com o AP de configuração aberto: nesses casos, só as 8 linhas mais novas chegam quando a rede volta.
- **Um envio que falha.** A linha é descartada, sem nova tentativa.
- **O que a rede perde.** O UDP não confirma a entrega. Um datagrama perdido no caminho não deixa rastro no aparelho nem no coletor.
- **A fila num travamento ou falta de energia.** A fila está na RAM. Na maioria dos reinícios pedidos, como **Salvar e reiniciar**, o aparelho esvazia a fila antes de reiniciar. Ao formatar o sistema de arquivos e ao restaurar um backup com arquivos, ele reinicia sem esvaziar. Num travamento ou numa queda de energia, a fila se perde.
- **O que acontece com o AP de configuração aberto.** Enquanto o ponto de acesso de configuração está aberto, o aparelho não manda nada ([capítulo 9](#cap-09-ap-aberto)).
- **Texto longo.** O que passa de 255 bytes na linha é cortado.

::: atencao
**O syslog não substitui o log de eventos.** Ele é uma cópia de melhor esforço, sem confirmação. O registro de referência continua sendo o log de eventos na flash do aparelho, com os últimos 800 a 1.600 eventos ([capítulo 16](#cap-16-guarda)). Para uma auditoria sem buracos, confira os dois: um código que falta no coletor pode estar no aparelho.
:::

## O syslog e o log de eventos {#cap-25-log-eventos}

| | Log de eventos (flash) | Syslog |
|---|---|---|
| Onde fica | No aparelho, de 800 a 1.600 eventos | No coletor, pelo tempo que você quiser |
| Filtro de rotina | Sim: rotina só na mudança de estado | Não: toda ocorrência |
| Eventos de depuração | Nunca | Com **Nível Mínimo** em **Debug** |
| Texto do evento | Não: só código e contexto | Sim: descrição e detalhe |
| Registros do começo do boot | Sim | Não |
| Entrega | Garantida enquanto a flash funciona | Melhor esforço, sem confirmação |
| Códigos | Os do [apêndice B](#ap-b) | Os mesmos, no `MSGID` |

O [capítulo 16](#cap-16) explica o log de eventos e como ler um travamento. O [apêndice B](#ap-b) lista todos os códigos, com a descrição em português.

## Segurança {#cap-25-seguranca}

- **O UDP não autentica nada.** Qualquer máquina da rede pode mandar ao coletor uma linha que parece vir do aparelho. Aceite no coletor só os endereços dos aparelhos, pelo firewall ou por uma regra como `if $fromhost-ip != "192.0.2.10" then stop` no rsyslog.
- **O texto viaja aberto.** As linhas levam nomes de contas e de arquivos. Mantenha o coletor na mesma rede local dos aparelhos.
- **O aparelho não cifra o syslog.** Não há syslog sobre TLS.

## No SIMUT Air {#cap-25-air}

[air]{.img}

O Air só manda syslog acordado em M0 ([capítulo 19](#cap-19)). Nos despertares do ciclo (M1), o laço principal não esvazia a fila e o despertar termina num reinício do processador sem passar pelo esvaziamento: as linhas desses despertares não saem. Os eventos continuam no log de eventos local ([capítulo 16](#cap-16-air)).

## Limites {#cap-25-limites}

- **Só IPv4**, sem nome de servidor.
- **Só UDP**, sem TCP nem TLS.
- **Um coletor só.** Para mais destinos, repasse a partir do coletor.
- **Facilidade fixa** `local0`.
- **Sem os registros do começo do boot**, inclusive a autópsia de um travamento. Por isso, com o **Nível Mínimo** em **Fatal**, nada é enviado.
- **Sem garantia de entrega** e sem aviso de perda.
- **Sem hora** (`-`) até o aparelho acertar o relógio.
- **Nada com o AP de configuração aberto** nem nos despertares do Air.
