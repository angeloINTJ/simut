#!/usr/bin/env python3
"""Soak de 24 h com a configuração real do usuário.

Serve a dois propósitos: é o teste de aceitação das 11 correções desta campanha,
e cobre a classe de problema que as rajadas de 90–150 s não tocam — deriva de
cursor, fragmentação de heap ao longo de horas, interação com o GC do histórico,
NTP re-sincronizando, e a detecção de RSSI implausível (D15) que ainda não foi
vista disparar.

Monitoramento é só pela serial, de propósito: `/api/status` exigiria deixar um
usuário web admin descartável vivo por 24 h, e a CLI já entrega tudo que
importa. Nada de servidor de teste — o alvo é o servidor real do usuário.

Amostra a cada 5 min (288 amostras). Cada linha anômala vai para o log com
prefixo ANOMALIA, para `grep` valer como triagem.

Parar antes da hora:  pkill -f soak24.py
"""
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target  # noqa: E402

DURACAO_S = 24 * 3600
PERIODO_S = 300
OUT = os.path.join(C.OUT, 'soak24.ndjson')


def amostra(t):
    d = {'wall': time.time()}
    m = t.metrics(wait=4.0)
    for k in ('uptime', 'heap', 'heap_min', 'largest', 'largest_min',
              'tel_sent', 'tel_failed', 'tel_retries', 'tel_bytes', 'tel_lat',
              'reads_ok', 'reads_err', 'flash_ops', 'core1_exposed',
              'wifi_conns', 'mqtt_conns'):
        d[k] = m.get(k)
    net = t.send('show net status', wait=4.0)
    r = re.search(r'RSSI:\s*(-?\d+)', net)
    p = re.search(r'PBUF pool:\s*(\d+) em uso / pico (\d+) / (\d+) total, (\d+) falhas', net)
    ip = re.search(r'IP:\s*([\d.]+)', net)
    d['rssi'] = int(r.group(1)) if r else None
    d['ip'] = ip.group(1) if ip else None
    if p:
        d['pbuf_uso'], d['pbuf_pico'], d['pbuf_total'], d['pbuf_falhas'] = \
            (int(x) for x in p.groups())
    return d


def main():
    t = Target(os.path.join(C.OUT, 'serial_soak24.log'))
    time.sleep(1)
    t0 = time.time()
    base = amostra(t)
    boots0, fatal0 = t.port_drops, len(t.fatal_lines)
    C.log(f'SOAK 24h iniciado. base={json.dumps(base)}')
    fh = open(OUT, 'a', buffering=1)
    fh.write(json.dumps({'evento': 'inicio', **base}) + '\n')

    prev = base
    n = 0
    while time.time() - t0 < DURACAO_S:
        time.sleep(PERIODO_S)
        n += 1
        d = amostra(t)
        d['t_h'] = round((time.time() - t0) / 3600, 2)
        d['reboots_usb'] = t.port_drops - boots0
        d['ftl'] = len(t.fatal_lines) - fatal0
        fh.write(json.dumps(d) + '\n')

        alerta = []
        # 1. Reboot ou pânico — critério de parada imediata.
        if d['reboots_usb'] or d['ftl']:
            alerta.append(f"REBOOT/FTL usb={d['reboots_usb']} ftl={d['ftl']}")
        if (prev.get('uptime') and d.get('uptime')
                and d['uptime'] < prev['uptime']):
            alerta.append(f"uptime regrediu {prev['uptime']}->{d['uptime']}")
        # 2. PBUF falhando fora de rajada: com o pool em 24 e sem carga de
        #    teste, qualquer falha aqui é sinal, não ruído.
        if d.get('pbuf_falhas'):
            alerta.append(f"PBUF falhas={d['pbuf_falhas']}")
        # 3. RSSI implausível — é o gatilho do D15. Se aparecer, o log serial
        #    deve trazer "Implausible RSSI twice" logo em seguida.
        if d.get('rssi') is not None and (d['rssi'] >= 0 or d['rssi'] < -120):
            alerta.append(f"RSSI implausivel={d['rssi']}")
        # 4. Heap: deriva > 5% contra a linha de base é o critério de sucesso.
        if base.get('heap') and d.get('heap'):
            drift = abs(d['heap'] - base['heap']) / base['heap']
            if drift > 0.05:
                alerta.append(f"heap {base['heap']}->{d['heap']} ({drift:.1%})")
        if d.get('ip') != base.get('ip'):
            alerta.append(f"IP mudou {base.get('ip')}->{d.get('ip')}")

        if alerta:
            C.log(f"ANOMALIA t={d['t_h']}h :: " + ' | '.join(alerta))
        elif n % 12 == 0:   # resumo de hora em hora
            C.log(f"ok t={d['t_h']}h up={d['uptime']}s heap={d['heap']} "
                  f"lb={d['largest']} rssi={d['rssi']} "
                  f"pbuf={d.get('pbuf_uso')}/{d.get('pbuf_total')}({d.get('pbuf_falhas')}) "
                  f"sent={d['tel_sent']} fail={d['tel_failed']}")
        prev = d

    fim = amostra(t)
    fim['evento'] = 'fim'
    fim['reboots_usb'] = t.port_drops - boots0
    fim['ftl'] = len(t.fatal_lines) - fatal0
    fh.write(json.dumps(fim) + '\n')
    C.log(f'SOAK 24h concluido. {json.dumps(fim)}')
    fh.close()
    t.close()


if __name__ == '__main__':
    main()
