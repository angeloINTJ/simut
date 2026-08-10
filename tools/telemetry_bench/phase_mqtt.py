#!/usr/bin/env python3
"""Phases A2/C2 — MQTT and MQTTS: throughput, and survival against a broken broker.

Two facts about the firmware shape this file.

`telTransport` and the MQTT client's server/port/TLS are read exactly once, in
TelemetryManager::begin(). Nothing re-reads them, so switching to MQTT — or from
MQTT to MQTTS — costs a reboot through /api/commit_all. Everything after that
runs on ONE port, and the fault modes are produced by restarting the broker
process rather than by re-pointing the device.

attemptMqttPublish splits at 5 records: five or fewer are published one message
per record, more than five as a single payload. The batch sweep straddles that
threshold on purpose.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale, wait_web  # noqa: E402

HIST_INTERVAL_S = 60


def switch(web, transport, tls, port, batch=50, interval=1000):
    fields = {
        't_transport': str(transport),
        't_sec': '1' if tls else '0',
        't_srv': C.HOST_IP,
        't_port': str(port),
        't_int': str(interval),
        't_bat': str(batch),
        't_mode': '0',
        'm_topic': 'simut/data',
        'm_qos': '1',
        'm_retain': '0',
        'm_ka': '30',
    }
    ok, back = C.commit(web, fields)
    return ok, back


def broker(name, port, tls, **kw):
    args = dict(port=port, tls=tls, cert='certs/cert.pem', key='certs/key.pem')
    args.update(kw)
    return Server('mqtt', name, C.OUT, **args)


def measure(t, srv, seconds, label):
    s0 = C.status()
    m0 = s0.get('metr', {})
    boots0, fatal0 = t.port_drops, len(t.fatal_lines)
    samples = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        s = C.status()
        s['_t'] = round(time.time() - t0, 1)
        samples.append(s)
        time.sleep(2)
    m1 = C.status().get('metr', {})
    st = srv.stats() if srv else {}

    def d(k):
        a, b = m0.get(k), m1.get(k)
        return (b - a) if isinstance(a, int) and isinstance(b, int) else None

    lat = [s.get('metr', {}).get('tl') for s in samples if s.get('metr')]
    lat = sorted(x for x in lat if isinstance(x, int) and x > 0)
    heap = [s.get('sys', {}).get('heap_f') for s in samples if s.get('sys')]
    lb = [s.get('metr', {}).get('lb') for s in samples if s.get('metr')]
    up = [s.get('sys', {}).get('uptime') for s in samples if s.get('sys')]
    resets = sum(1 for a, b in zip(up, up[1:])
                 if a is not None and b is not None and b < a)
    row = {
        'label': label, 'seconds': seconds,
        'dev_sent': d('ts'), 'dev_failed': d('tf'), 'dev_retries': d('tr'),
        'dev_bytes': d('tb'), 'dev_mqtt_conns': d('mq'),
        'dev_lat_min': lat[0] if lat else None,
        'dev_lat_med': lat[len(lat) // 2] if lat else None,
        'dev_lat_max': lat[-1] if lat else None,
        'heap_min': min([h for h in heap if h], default=None),
        'heap_max': max([h for h in heap if h], default=None),
        'largest_min': min([x for x in lb if x], default=None),
        'uptime_resets': resets, 'usb_drops': t.port_drops - boots0,
        'fatal_lines': len(t.fatal_lines) - fatal0,
        'unreachable_polls': sum(1 for s in samples if '_err' in s or '_http' in s),
        'srv_conns': st.get('conns'), 'srv_connects': st.get('connects'),
        'srv_connacks': st.get('connacks'), 'srv_publishes': st.get('publishes'),
        'srv_records': st.get('records'), 'srv_bytes': st.get('bytes_in'),
        'srv_pings': st.get('pings'),
        'srv_tls_ok': st.get('tls_ok'), 'srv_tls_fail': st.get('tls_failures'),
        'srv_qos_seen': st.get('qos_seen'), 'srv_retain_seen': st.get('retain_seen'),
        'srv_client_ids': st.get('client_ids'), 'srv_will_topics': st.get('will_topics'),
        'srv_status_msgs': st.get('status_msgs'),
        'srv_epoch_min': st.get('epoch_min'), 'srv_epoch_max': st.get('epoch_max'),
        'srv_msg_bytes': [m.get('bytes') for m in (st.get('msgs') or [])[-5:]],
        'srv_msg_n': [m.get('n') for m in (st.get('msgs') or [])[-5:]],
    }
    if row['srv_records'] is not None:
        row['records_per_s'] = round(row['srv_records'] / seconds, 2)
    return row


def perf_sweep(t, port, tls, batches, seconds, tag):
    rows = []
    for b in batches:
        label = f'{"mqtts" if tls else "mqtt"}_batch{b}'
        C.log(f'--- {label}')
        kill_stale()
        srv = broker(f'{tag}_{label}', port, tls, mode='ok')
        # Batch size is plain config, no reboot needed — only transport/TLS are
        # frozen at begin().
        t.send('enable', wait=1.2)
        t.send('configure terminal', wait=1.2)
        t.send(f'tel batch {b}', wait=1.2)
        t.send('tel interval 1000', wait=1.2)
        t.send('end', wait=1.2)
        t.send('write memory', wait=3.0)
        C.tel_reset(t)
        C.tel_sync(t, wait=30)
        time.sleep(3)
        row = measure(t, srv, seconds, label)
        row['batch'] = b
        rows.append(row)
        C.log(json.dumps({k: row[k] for k in
                          ('label', 'srv_publishes', 'srv_records', 'records_per_s',
                           'dev_lat_med', 'dev_failed', 'srv_qos_seen', 'heap_min',
                           'usb_drops', 'srv_connects')}))
        srv.stop()
    return rows


def run_fault(t, name, port, tls, seconds, server_kw=None, no_server=False, batch=10):
    C.log(f'=== mqtt fault: {name} ({seconds}s)')
    # act 1 — healthy broker, note where the cursor got to
    kill_stale()
    pre_srv = broker(f'{name}_pre', port, tls, mode='ok')
    t.send('enable', wait=1.2)
    t.send('configure terminal', wait=1.2)
    t.send(f'tel batch {batch}', wait=1.2)
    t.send('tel interval 1000', wait=1.2)
    t.send('end', wait=1.2)
    t.send('write memory', wait=3.0)
    C.tel_reset(t)
    C.tel_sync(t, wait=30)
    time.sleep(12)
    pre = pre_srv.stats()
    pre = {'publishes': pre.get('publishes'), 'records': pre.get('records'),
           'epoch_min': pre.get('epoch_min'), 'epoch_max': pre.get('epoch_max')}
    pre_srv.stop()
    C.log(f'  baseline: {pre}')

    kill_stale()
    srv = None
    if not no_server:
        srv = broker(f'{name}_fault', port, tls, **(server_kw or {}))
    C.tel_sync(t, wait=30)
    row = measure(t, srv, seconds, name)
    if srv:
        srv.stop()

    kill_stale()
    post_srv = broker(f'{name}_post', port, tls, mode='ok')
    C.tel_sync(t, wait=30)
    time.sleep(30)
    post = post_srv.stats()
    post = {'publishes': post.get('publishes'), 'records': post.get('records'),
            'epoch_min': post.get('epoch_min'), 'epoch_max': post.get('epoch_max')}
    post_srv.stop()
    C.log(f'  recovery: {post}')

    gap = lost = None
    if pre.get('epoch_max') and post.get('epoch_min'):
        gap = post['epoch_min'] - pre['epoch_max']
        lost = max(0, (gap // HIST_INTERVAL_S) - 1)
    row.update({'fault': name, 'pre': pre, 'post': post,
                'cursor_gap_s': gap, 'records_skipped': lost,
                'server_kw': server_kw, 'no_server': no_server, 'tls': tls})
    v = []
    if row['usb_drops'] or row['uptime_resets']:
        v.append('REBOOT')
    if row['fatal_lines']:
        v.append('FATAL')
    if lost:
        v.append(f'DATA-LOSS({lost} rec)')
    if row['unreachable_polls'] >= 3:
        v.append(f'WEB-STALL({row["unreachable_polls"]} polls)')
    if not post.get('records'):
        v.append('NO-RECOVERY')
    row['verdict'] = ' '.join(v) if v else 'SURVIVED'
    C.log(f'  -> {row["verdict"]} (devFail+{row["dev_failed"]} '
          f'conns={row["srv_conns"]} connacks={row["srv_connacks"]} heapMin={row["heap_min"]})')
    return row


FAULTS = [
    ('mq_refused',        dict(no_server=True)),
    ('mq_rst',            dict(server_kw={'mode': 'rst'})),
    ('mq_no_connack',     dict(server_kw={'mode': 'no_connack', 'delay': 180})),
    ('mq_slow_connack',   dict(server_kw={'mode': 'slow_connack', 'delay': 20})),
    ('mq_half_connack',   dict(server_kw={'mode': 'half_connack', 'delay': 180})),
    ('mq_connack_refuse', dict(server_kw={'mode': 'connack_refuse'})),
    ('mq_connack_unavail', dict(server_kw={'mode': 'connack_unavail'})),
    ('mq_drop_after_connack', dict(server_kw={'mode': 'drop_after_connack'})),
    ('mq_drop_on_publish', dict(server_kw={'mode': 'drop_on_publish'})),
    ('mq_rst_on_publish', dict(server_kw={'mode': 'rst_on_publish'})),
    ('mq_garbage',        dict(server_kw={'mode': 'garbage'})),
    ('mq_no_pingresp',    dict(server_kw={'mode': 'no_pingresp'})),
]

TLS_FAULTS = [
    ('mqs_tls_blackhole', dict(server_kw={'mode': 'ok', 'tls_fault': 'blackhole', 'delay': 180})),
    ('mqs_tls_garbage',   dict(server_kw={'mode': 'ok', 'tls_fault': 'garbage'})),
    ('mqs_tls_rst',       dict(server_kw={'mode': 'ok', 'tls_fault': 'rst'})),
    ('mqs_refused',       dict(no_server=True)),
    ('mqs_drop_on_publish', dict(server_kw={'mode': 'drop_on_publish'})),
]


def config_fidelity(t, web, port, seconds=70):
    """Does the wire carry what the config page promised?

    Every MQTT knob the UI exposes is checked against the bytes that actually
    reach the broker: credentials and client id in the CONNECT, QoS and retain
    in the PUBLISH flags, keepalive in the CONNECT header. A field the firmware
    accepts and then never transmits is invisible from the device side — the
    broker is the only place it shows up.
    """
    C.log('=== MQTT config fidelity ===')
    kill_stale()
    srv = broker('fidelity', port, False, mode='ok')
    fields = {
        't_transport': '1', 't_sec': '0', 't_srv': C.HOST_IP, 't_port': str(port),
        't_int': '2000', 't_bat': '3', 't_mode': '0',
        'm_topic': 'bench/telemetry/data', 'm_cid': 'benchcid7',
        'm_user': 'benchuser', 'm_pass': 'benchsecret',
        'm_qos': '1', 'm_retain': '1', 'm_ka': '45',
    }
    C.commit(web, fields)
    C.web_session(force=True)
    time.sleep(6)
    C.tel_sync(t, wait=30)
    time.sleep(seconds)
    st = srv.stats()
    srv.stop()

    wanted = {'m_cid': 'benchcid7', 'm_user': 'benchuser', 'm_pass': 'benchsecret',
              'm_qos': 1, 'm_retain': True, 'm_ka': 45,
              'm_topic': 'bench/telemetry/data'}
    # The broker is up before the commit, so the device's PRE-reboot client
    # connects to it first, carrying the OLD settings. Reading frame 0 scored
    # every field as ignored and would have reported four defects that do not
    # exist. Take the frame that belongs to the config under test — the one
    # whose will topic came from the new topic — and fall back to the last.
    frames = st.get('connect_frames') or [{}]
    want_will = wanted['m_topic'].rsplit('/', 1)[0] + '/status'
    cf = next((f for f in frames if f.get('willTopic') == want_will), frames[-1])
    got = {
        'clientId': cf.get('clientId'), 'user': cf.get('user'),
        'pass': cf.get('pass'), 'keepalive': cf.get('keepalive'),
        'willTopic': cf.get('willTopic'),
        'qos_seen': st.get('qos_seen'), 'retain_seen': st.get('retain_seen'),
        'topics': sorted({m['topic'] for m in (st.get('msgs') or [])}),
        'publishes': st.get('publishes'), 'records': st.get('records'),
    }
    checks = {
        'client_id_honoured': got['clientId'] == wanted['m_cid'],
        'user_honoured': got['user'] == wanted['m_user'],
        'password_honoured': got['pass'] == wanted['m_pass'],
        'keepalive_honoured': got['keepalive'] == wanted['m_ka'],
        'topic_honoured': wanted['m_topic'] in (got['topics'] or []),
        'qos1_honoured': bool(got['qos_seen']) and set(map(int, got['qos_seen'])) == {1},
        'retain_honoured': bool(got['retain_seen']) and got['retain_seen'].get('True', 0) > 0,
    }
    C.log('fidelity wanted=' + json.dumps(wanted))
    C.log('fidelity got=' + json.dumps(got))
    C.log('fidelity checks=' + json.dumps(checks))
    return {'wanted': wanted, 'got': got, 'checks': checks,
            'frame_used': cf, 'connect_frames': frames}


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 90
    stage = sys.argv[2] if len(sys.argv) > 2 else 'all'
    kill_stale()
    t = Target(os.path.join(C.OUT, f'serial_mqtt_{stage}.log'))
    time.sleep(1)
    web = C.web_session()
    out = {'perf': [], 'faults': []}

    if stage in ('all', 'plain'):
        C.log('=== switching to MQTT plain (reboot) ===')
        kill_stale()
        srv = broker('boot_mqtt', C.PORT_MQTT, False, mode='ok')
        ok, back = switch(web, 1, False, C.PORT_MQTT)
        C.log(f'switch ok={ok} web back in {back}s')
        C.web_session(force=True)
        time.sleep(5)
        srv.stop()
        out['perf'] += perf_sweep(t, C.PORT_MQTT, False, [1, 5, 10, 50], seconds, 'mqtt')
        C.save('phase_mqtt_plain.json', out)
        out['fidelity'] = config_fidelity(t, C.web_session(), C.PORT_MQTT)
        # config_fidelity leaves credentials and QoS 1 set; put the transport
        # back on the plain settings the fault runs assume.
        C.commit(C.web_session(), {
            't_transport': '1', 't_sec': '0', 't_srv': C.HOST_IP,
            't_port': str(C.PORT_MQTT), 't_int': '1000', 't_bat': '10',
            'm_topic': 'simut/data', 'm_cid': '', 'm_user': '',
            'm_qos': '0', 'm_retain': '0', 'm_ka': '30'})
        C.web_session(force=True)
        time.sleep(5)
        C.save('phase_mqtt_plain.json', out)
        for name, kw in FAULTS:
            out['faults'].append(run_fault(t, name, C.PORT_MQTT, False, seconds, **kw))
            C.save('phase_mqtt_plain.json', out)

    if stage in ('all', 'tls'):
        C.log('=== switching to MQTTS (reboot) ===')
        kill_stale()
        srv = broker('boot_mqtts', C.PORT_MQTTS, True, mode='ok')
        ok, back = switch(web, 1, True, C.PORT_MQTTS)
        C.log(f'switch ok={ok} web back in {back}s')
        C.web_session(force=True)
        time.sleep(5)
        srv.stop()
        out2 = {'perf': [], 'faults': []}
        out2['perf'] += perf_sweep(t, C.PORT_MQTTS, True, [1, 5, 10, 50], seconds, 'mqtts')
        C.save('phase_mqtt_tls.json', out2)
        for name, kw in TLS_FAULTS:
            out2['faults'].append(run_fault(t, name, C.PORT_MQTTS, True, seconds, **kw))
            C.save('phase_mqtt_tls.json', out2)

    t.close()
    C.log('done')


if __name__ == '__main__':
    main()
