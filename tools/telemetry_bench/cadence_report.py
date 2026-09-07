#!/usr/bin/env python3
"""Turn results/phase_cadence.json into the tables the energy plan quotes.

Three tables, one per phase, plus the derived per-request cost model:

  capacity   records/s, requests/s and seconds per request by transport and
             batch — and the fit  t_req = t_fixed + t_rec * batch, which is the
             number the batch-size rule is built on.
  latency    what a server delay does to the cadence: measured interval between
             sends against the injected delay, and where the failures start.
  wake       awake seconds per 1000 records, the energy proxy.

Usage: python3 cadence_report.py [results/phase_cadence.json]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def fit_line(xs, ys):
    """Least squares y = a + b x; returns (a, b) or (None, None)."""
    n = len(xs)
    if n < 2:
        return None, None
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None, None
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return my - b * mx, b


def fmt(v, nd=1):
    if v is None:
        return '—'
    if isinstance(v, float):
        return f'{v:.{nd}f}'
    return str(v)


def table(rows, cols, title):
    print(f'\n### {title}\n')
    print('| ' + ' | '.join(c[0] for c in cols) + ' |')
    print('|' + '|'.join('---' for _ in cols) + '|')
    for r in rows:
        print('| ' + ' | '.join(fmt(r.get(c[1]), c[2] if len(c) > 2 else 1) for c in cols) + ' |')


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'results', 'phase_cadence.json')
    d = json.load(open(path))
    rows = d['rows']
    print(f'# Phase E — cadence & batch, {d.get("seconds")} s windows, host {d.get("host")}')

    # Every count below is what the server logged INSIDE the window, by wall
    # clock (phase_cadence.window). The first matrix used the server's
    # cumulative counters, which also held the drain the device resumed on
    # boot and the restart `tel_reset` causes — read at the time as 22–35%
    # duplicates on the wire. They were the bench's own doing, not the
    # firmware's; the per-request log settles it.
    cap = [r for r in rows if r.get('phase') == 'capacity']
    if cap:
        for r in cap:
            act = r.get('active_s') or r.get('seconds') or 1
            if r.get('srv_requests'):
                r['req_per_s'] = round(r['srv_requests'] / act, 2)
                r['ms_per_req'] = round(1000.0 * act / r['srv_requests'], 0)
        table(sorted(cap, key=lambda r: (r['tls'], r['batch'])),
              [('transporte', 'label'), ('lote', 'batch'), ('ativo s', 'active_s'),
               ('req', 'srv_requests'), ('reg', 'srv_records'),
               ('reg/s', 'records_per_s'), ('req/s', 'req_per_s'),
               ('ms/req', 'ms_per_req', 0), ('reg/req', 'records_per_send'),
               ('srv p50 ms', 'srv_ms_p50'),
               ('lat_med ms', 'dev_lat_med'), ('lat_max ms', 'dev_lat_max'),
               ('falhas', 'dev_failed'), ('heap_min', 'heap_min'), ('lb_min', 'largest_min'),
               ('reboots', 'uptime_resets'), ('pré-janela', 'pre_window_records')],
              'capacidade (servidor responde na hora)')
        for tls in (False, True):
            sub = [r for r in cap if r['tls'] == tls and r.get('ms_per_req') and r.get('records_per_send')]
            if len(sub) >= 2:
                a, b = fit_line([r['records_per_send'] for r in sub], [r['ms_per_req'] for r in sub])
                if a is not None:
                    print(f'\n{"HTTPS" if tls else "HTTP"}: t_req ≈ {a:.0f} ms + {b:.2f} ms × registros'
                          f'  → custo fixo {a:.0f} ms, marginal {b:.2f} ms/registro'
                          f'; lote em que o fixo cai a 10% do total ≈ {a / b / 0.111:.0f}' if b > 0 else '')

    lat = [r for r in rows if r.get('phase') == 'latency']
    if lat:
        table(sorted(lat, key=lambda r: r['delay']),
              [('rótulo', 'label'), ('atraso s', 'delay'), ('req', 'srv_requests'),
               ('reg/s', 'records_per_s'), ('s entre envios', 's_between_sends'),
               ('lat_med ms', 'dev_lat_med'), ('lat_max ms', 'dev_lat_max'),
               ('falhas', 'dev_failed'), ('retries', 'dev_retries'), ('reboots', 'uptime_resets')],
              'latência do servidor (injetada)')

    wk = [r for r in rows if r.get('phase') == 'wake']
    if wk:
        table(wk, [('rótulo', 'label'), ('lote', 'batch'), ('t_int ms', 'interval_ms'),
                   ('janelas acordado s', 'awake_windows_s'), ('reg entregues', 'srv_records'),
                   ('req', 'srv_requests'), ('s acordado / 1000 reg', 'awake_s_per_1000_records')],
              'um wake M1 por configuração (sonda)')


if __name__ == '__main__':
    main()
