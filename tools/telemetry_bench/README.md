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

Os certificados de teste **não** estão versionados. Gerar antes de usar TLS:

    mkdir -p certs && openssl req -x509 -newkey rsa:2048 -keyout certs/key.pem \
      -out certs/cert.pem -days 365 -nodes -subj "/CN=<IP-do-host>/O=SimutBench" \
      -addext "subjectAltName=IP:<IP-do-host>"

Os endereços do alvo e do host estão fixos no topo de `campaign.py` e `bench.py`.
