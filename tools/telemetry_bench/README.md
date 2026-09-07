# Bancada de telemetria

Servidores instrumentados e orquestração usados na campanha de 2026-08-02
(ver `docs/telemetry-campaign-2026-08-02/`). Python 3 + pyserial + requests.

- `server_http.py` — sink HTTP/HTTPS com injeção de falha (`--mode`, `--tls-fault`)
- `server_mqtt.py` — broker MQTT 3.1.1 escrito do formato de fio, para poder
  mentir (CONNACK recusado, meio CONNACK, queda no publish…)
- `bench.py` — serial com detecção de reboot, cliente web, ciclo de vida dos servidores
- `campaign.py` — controle do alvo; `phase_*.py` — as fases; `revalidate.py` — regressões
- `rescore.py` — recontagem de perda de dados contando o que a falha recebeu
- `soak24.py` — soak longo, monitorado só pela serial

Fase E (cadência e lote, 2026-09-07, build Air — ver
`docs/analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md`):

- `gen_synth_history.py` — dias sintéticos `.h5` para encher o `/history`
- `phase_cadence.py` — `capacity` (transporte × lote), `latency` (atraso injetado),
  `wake` (um wake M1 por configuração, cronometrado pela sonda GP16 do PicoHand).
  A janela é cortada do log **por request** do servidor, por relógio de parede
  (`window( )`): o que o aparelho manda antes dela — o dreno que retoma no boot,
  o recomeço do `tel_reset` — sai em `pre_window_records`, não na medição
- `cadence_report.py` — as tabelas do plano a partir de `results/phase_cadence[_tag].json`
- `serial_probe.py` — uma janela com a serial capturada o tempo todo
- `cadence_cleanup.py` — remove os dias sintéticos, devolve os reais e a config

Os certificados de teste **não** estão versionados. Gerar antes de usar TLS:

    mkdir -p certs && openssl req -x509 -newkey rsa:2048 -keyout certs/key.pem \
      -out certs/cert.pem -days 365 -nodes -subj "/CN=<IP-do-host>/O=SimutBench" \
      -addext "subjectAltName=IP:<IP-do-host>"

Os endereços do alvo e do host estão fixos no topo de `campaign.py` e `bench.py`.
