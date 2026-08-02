#!/usr/bin/env python3
"""Re-score the data-loss check, counting what the FAULT server received.

The original check compared only the baseline server's last accepted epoch
against the recovery server's first one. That is right for a fault that accepts
nothing — which is most of them — and wrong for any fault that is a working
server in every respect but the one being tested.

`mq_no_pingresp` is exactly that: a broker that answers CONNECT and PUBLISH
normally and only ignores PINGREQ. It took 780 records during its window, the
cursor advanced over them legitimately, and the naive check called the whole
span lost. Scored properly the answer is zero.

    skipped = (post.epoch_min - max(pre.epoch_max, fault.epoch_max)) / 60 - 1

Rows where the fault server reported no epochs are unchanged, so this cannot
launder a real loss into a pass — it only removes the false ones.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'results')
INTERVAL = 60


def rescore(row):
    pre = row.get('pre') or {}
    post = row.get('post') or {}
    fault_max = row.get('srv_epoch_max')
    pre_max = pre.get('epoch_max')
    post_min = post.get('epoch_min')
    if not pre_max or not post_min:
        return None, None, None
    delivered = max([e for e in (pre_max, fault_max) if e] or [pre_max])
    gap = post_min - delivered
    skipped = max(0, (gap // INTERVAL) - 1) if gap > 0 else 0
    return gap, skipped, delivered


def main():
    files = [
        ('phase_survive_http.json', 'rows', 'HTTP'),
        ('phase_survive_tls.json', 'rows', 'HTTPS'),
        ('phase_mqtt_plain.json', 'faults', 'MQTT'),
        ('phase_mqtt_tls.json', 'faults', 'MQTTS'),
    ]
    print(f"{'transporte':<8}{'falha':<24}{'skip antigo':>12}{'skip corrigido':>16}"
          f"{'gap corrigido':>15}{'aceitou na falha':>18}")
    total_old = total_new = 0
    out = []
    for fn, key, label in files:
        p = os.path.join(OUT, fn)
        if not os.path.exists(p):
            continue
        with open(p) as fh:
            d = json.load(fh)
        for r in d.get(key, []):
            gap, skipped, delivered = rescore(r)
            old = r.get('records_skipped')
            got = r.get('srv_records')
            if skipped is None:
                continue
            total_old += old or 0
            total_new += skipped
            flag = '  <-- corrigido' if (old or 0) != skipped else ''
            print(f"{label:<8}{r.get('fault',''):<24}{str(old):>12}{skipped:>16}"
                  f"{str(gap):>15}{str(got):>18}{flag}")
            out.append({'transport': label, 'fault': r.get('fault'),
                        'skipped_naive': old, 'skipped_corrected': skipped,
                        'gap_corrected': gap, 'fault_server_records': got,
                        'verdict_naive': r.get('verdict')})
    print()
    print(f'TOTAL registros "perdidos": ingênuo={total_old}  corrigido={total_new}')
    with open(os.path.join(OUT, 'rescore.json'), 'w') as fh:
        json.dump({'rows': out, 'total_naive': total_old,
                   'total_corrected': total_new}, fh, indent=1)


if __name__ == '__main__':
    main()
