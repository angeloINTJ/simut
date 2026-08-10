#!/usr/bin/env python3
"""Turn a storm_net.py run into the tables the report needs.

Two things this has to get right, because both read as data and are not:

  - A reboot zeroes the device's counters, so a window that rebooted shows
    *negative* ts/tf deltas. Reporting "-21 sends" as a measurement would be
    nonsense; the honest answer is "not measurable across a reboot".
  - Every `pio run -t upload` writes a benign `[FTL][SYS] code=1` at the next
    boot (the 1200 bps touch goes through watchdog_reboot). Counting FTL lines
    by grep inflates the death count. Deaths are counted from USB re-enumeration
    and uptime regression, and the FTL text is quoted for the autopsy only.

Usage: storm_report.py <outdir> [more outdirs...]
"""
import json
import os
import sys


def load(outdir):
    rows = []
    p = os.path.join(outdir, 'storm_rows.json')
    if os.path.exists(p):
        rows = json.load(open(p)).get('rows', [])
    ser = []
    p = os.path.join(outdir, 'serial_samples.jsonl')
    if os.path.exists(p):
        ser = [json.loads(l) for l in open(p) if l.strip()]
    ev = []
    p = os.path.join(outdir, 'events.json')
    if os.path.exists(p):
        ev = json.load(open(p))
    web = {}
    p = os.path.join(outdir, 'storm_rows.json')
    if os.path.exists(p):
        web = json.load(open(p)).get('web', {})
    return rows, ser, ev, web


def fmt_delta(v, rebooted):
    if rebooted:
        return 'n/a¹'
    return '—' if v is None else f'{v:+d}'


def pbuf_by_fault(ser):
    """PBUF allocation failures attributable to each window.

    The device counts them cumulatively since boot, so the max over a window is
    the running total and would mark every window after the first offender.
    What the window actually caused is last-minus-first — and a reboot inside
    the window resets the counter, which shows up as a negative and is reported
    as not measurable rather than as zero."""
    out = {}
    for s in ser:
        f = s.get('fault')
        v = s.get('pbuf_fail')
        if f is None or v is None:
            continue
        cur = out.setdefault(f, {'first': v, 'last': v, 'peak': 0})
        cur['last'] = v
        cur['peak'] = max(cur['peak'], s.get('pbuf_peak') or 0)
    for f, c in out.items():
        d = c['last'] - c['first']
        # None means "the counter went backwards", i.e. a reboot reset it.
        # A fault with no entry at all is a different thing — no sample — and
        # the caller must not print the two the same way.
        c['delta'] = d if d >= 0 else None
    return out


def main():
    outdirs = sys.argv[1:] or ['results/netstorm']
    all_rows, all_ser, all_ev, all_web = [], [], [], {}
    for d in outdirs:
        r, s, e, w = load(d)
        all_rows += r
        all_ser += s
        all_ev += e
        for k, v in w.items():
            all_web[k] = all_web.get(k, 0) + v

    pbuf = pbuf_by_fault(all_ser)

    print(f'# Resultados — {len(all_rows)} janelas\n')
    hdr = ('| falha | s | veredito | reboots | fx | PBUF falhas Δ | PBUF pico | '
           'kills C1 | ts Δ | tf Δ | heap min | ping perdido | sink conns |')
    print(hdr)
    print('|' + '---|' * 13)
    for r in all_rows:
        reb = bool(r.get('usb_drops') or r.get('uptime_resets'))
        pb = pbuf.get(r['fault'])
        if pb is None:
            pbcell = '—²'          # nenhuma amostra serial cobriu esta janela
        elif pb['delta'] is None:
            pbcell = 'n/a¹'        # contador zerado pelo reboot
        else:
            pbcell = f"+{pb['delta']}"
        print(f"| `{r['fault']}` | {r['seconds']:.0f} | "
              f"{'**' + r['verdict'] + '**' if r['verdict'] != 'SURVIVED' else r['verdict']} | "
              f"{r.get('usb_drops', 0)}/{r.get('uptime_resets', 0)} | "
              f"{r.get('fx_max')} | {pbcell} | "
              f"{(pb or {}).get('peak', '—')} | "
              f"{r.get('core1_kills_health')} | "
              f"{fmt_delta(r.get('ts_delta'), reb)} | {fmt_delta(r.get('tf_delta'), reb)} | "
              f"{r.get('heap_min')} | {r.get('ping_lost', 0)} | {r.get('srv_conns')} |")
    print('\n¹ contador zerado por reboot dentro da janela — delta não é medida.')
    print('² sem amostra serial nesta janela — ausência de dado, não ausência '
          'de falhas.\n')
    print('> O termo `PBUF=N` que aparece no veredito bruto vem do contador '
          'CUMULATIVO desde o boot; a coluna de delta acima é a que mede o que '
          'a janela causou.\n')

    surv = [r for r in all_rows if r['verdict'] == 'SURVIVED']
    print(f'**Sobreviveram: {len(surv)}/{len(all_rows)}**\n')

    bad = [r for r in all_rows if r['verdict'] != 'SURVIVED']
    if bad:
        print('## Janelas que falharam\n')
        for r in bad:
            print(f"### `{r['fault']}` — {r['verdict']}\n")
            if r.get('fatal_text'):
                print('```')
                for line in r['fatal_text']:
                    print(line.strip())
                print('```\n')

    if all_ser:
        pb = [s.get('pbuf_peak') for s in all_ser if s.get('pbuf_peak') is not None]
        pf = [s.get('pbuf_fail') for s in all_ser if s.get('pbuf_fail') is not None]
        hb = [s.get('core1_hb_ms') for s in all_ser if s.get('core1_hb_ms') is not None]
        kh = [s.get('core1_kills_health') for s in all_ser
              if s.get('core1_kills_health') is not None]
        ex = [s.get('core1_exposed') for s in all_ser
              if s.get('core1_exposed') is not None]
        rs = [s.get('rssi') for s in all_ser if s.get('rssi') is not None]
        irq = [s.get('irqoff_max_us') for s in all_ser
               if s.get('irqoff_max_us') is not None]
        print('## Sinais só acessíveis pela serial\n')
        print('| sinal | mín | máx | critério | passa |')
        print('|---|---|---|---|---|')

        def line(name, vals, crit, ok):
            if not vals:
                print(f'| {name} | — | — | {crit} | sem amostra |')
                return
            print(f'| {name} | {min(vals)} | {max(vals)} | {crit} | '
                  f'{"✅" if ok(vals) else "❌"} |')

        line('PBUF em uso (pico)', pb, '< 24', lambda v: max(v) < 24)
        line('PBUF falhas', pf, '== 0', lambda v: max(v) == 0)
        line('Core 1 heartbeat (ms)', hb, '< 1000', lambda v: max(v) < 1000)
        line('Core 1 kills saúde', kh, '== 0', lambda v: max(v) == 0)
        line('Core 1 exposto (flash)', ex, '== 0', lambda v: max(v) == 0)
        line('RSSI (dBm)', rs, '[-120, 0)', lambda v: -120 <= min(v) and max(v) < 0)
        line('IRQ-off máx (us)', irq, '< 60000', lambda v: max(v) < 60000)
        print()

    if all_web:
        print('## Carga web aplicada\n')
        print('| métrica | valor |')
        print('|---|---|')
        for k, v in all_web.items():
            print(f'| {k} | {v} |')
        print()

    if all_ev:
        kinds = {}
        for e in all_ev:
            kinds[e['kind']] = kinds.get(e['kind'], 0) + 1
        print('## Eventos\n')
        print('| tipo | n |')
        print('|---|---|')
        for k, v in sorted(kinds.items(), key=lambda x: -x[1]):
            print(f'| {k} | {v} |')


if __name__ == '__main__':
    main()
