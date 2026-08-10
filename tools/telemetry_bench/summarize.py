#!/usr/bin/env python3
"""Turn the campaign's JSON output into the tables that go in the report."""
import datetime as dt
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'results')


def load(name):
    p = os.path.join(OUT, name)
    if not os.path.exists(p):
        return None
    with open(p) as fh:
        return json.load(fh)


def ts(e):
    if not e:
        return '—'
    return dt.datetime.fromtimestamp(e).strftime('%Y-%m-%d %H:%M')


def md_table(rows, cols, headers=None):
    headers = headers or cols
    out = ['| ' + ' | '.join(headers) + ' |',
           '|' + '|'.join(['---'] * len(cols)) + '|']
    for r in rows:
        out.append('| ' + ' | '.join(str(r.get(c, '—')) for c in cols) + ' |')
    return '\n'.join(out)


def perf_table():
    d = load('phase_perf_http.json')
    if not d:
        return '_(sem dados)_'
    rows = []
    for r in d['rows']:
        rows.append({
            'transporte': 'HTTPS' if r.get('tls') else 'HTTP',
            'lote': r.get('batch'),
            'envios': r.get('srv_requests'),
            'registros': r.get('srv_records'),
            'reg/s': r.get('records_per_s'),
            'B/s': r.get('bytes_per_s'),
            'lat med (ms)': r.get('dev_lat_med'),
            'lat max (ms)': r.get('dev_lat_max'),
            'falhas': r.get('dev_failed'),
            'heap min': r.get('heap_min'),
            'maior bloco min': r.get('largest_min'),
            'reboots': (r.get('usb_drops') or 0) + (r.get('uptime_resets') or 0),
        })
    return md_table(rows, list(rows[0].keys()))


def mqtt_perf_table():
    out = []
    for f, lbl in (('phase_mqtt_plain.json', 'MQTT'), ('phase_mqtt_tls.json', 'MQTTS')):
        d = load(f)
        if not d:
            continue
        rows = []
        for r in d.get('perf', []):
            rows.append({
                'transporte': lbl,
                'lote': r.get('batch'),
                'publishes': r.get('srv_publishes'),
                'registros': r.get('srv_records'),
                'reg/s': r.get('records_per_s'),
                'lat med (ms)': r.get('dev_lat_med'),
                'falhas': r.get('dev_failed'),
                'conexoes': r.get('srv_connects'),
                'qos visto': json.dumps(r.get('srv_qos_seen') or {}),
                'heap min': r.get('heap_min'),
                'reboots': (r.get('usb_drops') or 0) + (r.get('uptime_resets') or 0),
            })
        if rows:
            out.append(md_table(rows, list(rows[0].keys())))
    return '\n\n'.join(out) if out else '_(sem dados)_'


def survive_table():
    rows = []
    for f in ('phase_survive_all.json', 'phase_survive_http.json',
              'phase_survive_tls.json'):
        d = load(f)
        if not d:
            continue
        for r in d['rows']:
            rows.append({
                'falha': r.get('fault'),
                'transporte': 'HTTPS' if r.get('tls') else 'HTTP',
                'janela (s)': r.get('seconds'),
                'reboots': (r.get('usb_drops') or 0) + (r.get('uptime_resets') or 0),
                'FTL': r.get('fatal_lines'),
                'falhas tel': r.get('dev_failed_delta'),
                'envios tel': r.get('dev_sent_delta'),
                'heap min': r.get('heap_min'),
                'web mudo (polls)': r.get('web_worst_streak_polls'),
                'reg perdidos': r.get('records_skipped'),
                'recuperou': 'sim' if (r.get('post') or {}).get('records') else 'NAO',
                'veredito': r.get('verdict'),
            })
    for f, lbl in (('phase_mqtt_plain.json', 'MQTT'), ('phase_mqtt_tls.json', 'MQTTS')):
        d = load(f)
        if not d:
            continue
        for r in d.get('faults', []):
            rows.append({
                'falha': r.get('fault'),
                'transporte': lbl,
                'janela (s)': r.get('seconds'),
                'reboots': (r.get('usb_drops') or 0) + (r.get('uptime_resets') or 0),
                'FTL': r.get('fatal_lines'),
                'falhas tel': r.get('dev_failed'),
                'envios tel': r.get('dev_sent'),
                'heap min': r.get('heap_min'),
                'web mudo (polls)': r.get('unreachable_polls'),
                'reg perdidos': r.get('records_skipped'),
                'recuperou': 'sim' if (r.get('post') or {}).get('records') else 'NAO',
                'veredito': r.get('verdict'),
            })
    if not rows:
        return '_(sem dados)_'
    return md_table(rows, list(rows[0].keys()))


def drain_block():
    d = load('phase_drain.json')
    if not d:
        return '_(sem dados)_'
    t, g, c = d['telemetry'], d['ground_truth'], d['coverage']
    return f"""**Via telemetria** (lote {t['batch']}, intervalo {t['interval']} ms, HTTP puro)

| métrica | valor |
|---|---|
| duração | {t['wall_s']} s |
| envios HTTP | {t['requests']} |
| registros aceitos | {t['records']} |
| epochs únicos | {t['unique_epochs']} |
| duplicados | {t['duplicates']} |
| bytes recebidos | {t['bytes_in']} |
| primeiro registro | {ts(t['epoch_min'])} |
| último registro | {ts(t['epoch_max'])} |
| reboots | {t['usb_drops']} |

**Verdade de solo** (todos os `.h5` baixados e decodificados pelo codec de referência)

| métrica | valor |
|---|---|
| arquivos listados | {g['files_listed']} |
| arquivos baixados | {g['files_downloaded']} |
| falhas de download | {len(g['download_failures'])} |
| bytes | {g['total_bytes']} |
| registros no disco | {g['total_records']} |
| epochs únicos | {g['unique_epochs']} |
| primeiro | {ts(g['epoch_min'])} |
| último | {ts(g['epoch_max'])} |
| erros de decodificação | {len(g['decode_errors'])} |

**Cobertura**: {c['via_telemetry']} de {c['on_disk']} epochs = **{c['pct']}%**.
Faltaram {c['missing_count']} registros, de {ts(c['missing_first'])} a {ts(c['missing_last'])}.
"""


def fidelity_block():
    d = load('phase_mqtt_plain.json')
    if not d or 'fidelity' not in d:
        return '_(sem dados)_'
    f = d['fidelity']
    rows = []
    for k, v in f['checks'].items():
        rows.append({'verificação': k, 'resultado': 'OK' if v else 'FALHOU'})
    return (md_table(rows, ['verificação', 'resultado']) +
            '\n\nEnviado: `' + json.dumps(f['wanted']) + '`\n\n' +
            'Recebido no broker: `' + json.dumps(f['got']) + '`')


if __name__ == '__main__':
    print('## Desempenho HTTP/HTTPS\n')
    print(perf_table())
    print('\n## Desempenho MQTT/MQTTS\n')
    print(mqtt_perf_table())
    print('\n## Fidelidade da config MQTT\n')
    print(fidelity_block())
    print('\n## Sobrevivência\n')
    print(survive_table())
    print('\n## Descarga do histórico\n')
    print(drain_block())
