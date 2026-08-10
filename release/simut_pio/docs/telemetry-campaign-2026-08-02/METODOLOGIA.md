# Campanha de telemetria — metodologia e bancada

**Data:** 2026-08-02
**Alvo:** Raspberry Pi Pico W, firmware SIMUT **2.0.1-alpha**, env `pico_w_test`
(`SIMUT_CLI_FULL=1` — a CLI reduzida do release não tem `tel …`, então a suíte
não roda nela).
**IP do dispositivo:** 192.168.3.24 · **host de teste:** 192.168.3.31
**Serial:** `/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_E6642815E34C1824-if00`

## 1. Por que três instrumentos

Cada medição é lida de três lugares independentes, porque nenhum deles sozinho
distingue "o dispositivo está bem" de "o dispositivo está mudo":

| instrumento | responde | cegueira que ele tem |
|---|---|---|
| **servidor de teste** | o que de fato chegou (bytes, registros, epochs, relógio de parede) | não vê o dispositivo |
| **`/api/status`** (autenticado) | o que o dispositivo acredita: `ts`/`tf`/`tl`/`tb`, heap, maior bloco, `pending` | morre junto com o dispositivo |
| **serial (CDC)** | reboot (o USB re-enumera), `[FTL]`, `SOFT PANIC`, `HW WATCHDOG` | não é confiável durante um travamento do Core 0 |

`/api/status` **exige sessão**: uma sonda sem login recebe `{"error":"Forbidden"}`
e um coletor descuidado lê isso como "dispositivo respondendo". A biblioteca da
bancada faz login uma vez e refaz a sessão em 401/403.

## 2. O que a bancada precisou saber antes de medir

**Backoff.** `TelemetryManager::escalateBackoff()` dobra o intervalo a partir de
5 s até 300 s. Uma janela de medição aberta logo depois de trocar o servidor cai
**dentro** de um backoff herdado e mede o temporizador, não o transporte — foi
exatamente o que aconteceu na primeira tentativa de HTTPS (0 conexões em 45 s, e
o servidor estava perfeito). `forceSync()` chama `resetBackoff()` **antes** de
qualquer outra coisa, então `tel sync` é a única forma de zerar o backoff sem
reboot. Toda janela começa com `tel reset` + `tel sync`.

**O que exige reboot.** `telTransport`, o cliente MQTT e o TLS do MQTT são lidos
**uma única vez**, em `TelemetryManager::begin()`. Trocar HTTP↔MQTT ou
MQTT↔MQTTS custa um `POST /api/commit_all` (que reinicia). Já servidor, porta,
lote, intervalo e modo do caminho HTTP são relidos a cada envio e mudam pela CLI
sem reboot.

Consequência de projeto para os testes de falha: **todos rodam na mesma porta**,
e o modo de falha vem de reiniciar o processo do servidor — nunca de
reconfigurar o dispositivo. Isso mantém o dispositivo estável entre falhas e
elimina o reboot como variável.

**Ponto de partida repetível.** `tel reset` apaga o cursor; o próximo
`collectBatch` cai no piso de `lastRecorded − 30 dias`. Toda corrida começa daí,
então todas veem a mesma fila grande e os números comparam.

## 3. Servidores de teste

Escritos para este trabalho (`scratchpad/telbench/`), em Python puro, porque o
ponto é **errar de propósito** — nenhum servidor real tem chave para "responda
metade do cabeçalho e feche".

### `server_http.py` — sink HTTP/HTTPS instrumentado

Modos: `ok`, `error500`, `error401`, `blackhole` (aceita e nunca responde),
`slow N`, `half` (linha de status parcial + FIN), `rst` (RST no accept),
`rst_mid` (RST no meio dos cabeçalhos), `garbage` (bytes não-HTTP), `huge`
(corpo de 1 MB), `drip` (1 byte a cada N ms), `close_early`.
Falhas de TLS antes de qualquer HTTP: `--tls-fault blackhole|garbage|rst|slow`.
TLS fixado em 1.2 com cert autoassinado, igual ao servidor real do usuário.

### `server_mqtt.py` — broker MQTT 3.1.1 instrumentado

Implementado do formato de fio (não é wrapper do mosquitto) para poder mentir:
`rst`, `no_connack`, `slow_connack`, `half_connack` (2 dos 4 bytes),
`connack_refuse/badproto/badid/unavail`, `drop_after_connack`,
`drop_on_publish`, `rst_on_publish`, `garbage`, `no_pingresp`. Fala
CONNECT/CONNACK, PUBLISH (QoS 0 e 1), PUBACK, SUBSCRIBE/SUBACK,
PINGREQ/PINGRESP e DISCONNECT — o suficiente para o PubSubClient do firmware.
Registra os campos do CONNECT (usuário, senha, client id, keepalive, will) e as
flags de cada PUBLISH (QoS, retain, dup), que é a única forma de checar se a
config prometida na web chega ao fio.

## 4. Estrutura de um teste de sobrevivência

"Não reiniciou" é metade da pergunta. A outra metade é se o dispositivo jogou
dado fora em silêncio. Por isso cada falha tem três atos:

1. **linha de base** — sink bom, `tel reset` + `tel sync`, anota o último epoch
   aceito (E1);
2. **falha** — troca o servidor pelo defeituoso e roda a janela inteira;
3. **recuperação** — sink bom de volta, `tel sync`, anota o primeiro epoch
   aceito (E2).

Se **E2 > E1 + 1 intervalo**, o dispositivo avançou o cursor por cima de
registros que servidor nenhum confirmou: perda de dados. Sem os atos 1 e 3, uma
falha que come histórico é indistinguível de uma que o dispositivo ignorou.

## 5. Endereços que não são servidores

Duas falhas não podem ser produzidas por um socket escutando, e por isso trocam
o endereço em vez do modo:

- `syn_blackhole` → `192.0.2.1` (TEST-NET-1, RFC 5737): roteado para lugar
  nenhum, o SYN é engolido em vez de recusado, e o `connect()` bloqueia em vez
  de falhar rápido;
- `dns_fail` → `nao-existe.invalid`: a resolução é que bloqueia.

## 6. Integridade, não só vazão

Vazão sem conferir valor não vale nada. A fase de payload:

- captura os corpos crus das requisições nos três modos (`json`, `csv`,
  `custom`) e confere que são o que prometem;
- baixa os `.h5` dos mesmos dias por `/download` e decodifica com o codec de
  referência (`tools/history_v5.py`), comparando **valor a valor** contra o que
  chegou pela telemetria.

## 7. Configuração original do usuário (restaurada no fim)

Salva em `results/ORIGINAL_CONFIG.json`:

```json
{"t_transport": 0, "t_sec": true, "t_srv": "192.168.3.206", "t_port": 8443,
 "t_path": "/api.php", "t_int": 10000, "t_bat": 50, "t_mode": 0,
 "m_topic": "simut/data", "m_qos": 0, "m_retain": false, "m_ka": 60, "h_int": 1}
```

Usuário web descartável criado para a campanha: `telb` (admin), apagado no fim.
