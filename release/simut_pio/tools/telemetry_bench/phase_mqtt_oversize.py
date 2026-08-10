#!/usr/bin/env python3
"""Targeted test — an MQTT payload bigger than the client buffer can ever be.

attemptMqttPublish grows the PubSubClient buffer to fit the payload, but clamps
the request at 8192:

    uint16_t needed = min((size_t)8192, payload.length( ) + 64);
    _mqttClient.setBufferSize(needed);

PubSubClient::publish() refuses any packet that does not fit the buffer, so a
payload past ~8 KB cannot be published *at all* — and since the failure is
deterministic, the retry never succeeds either. That is a permanent stall, not a
transient error, and it is reachable from the config page: batch 50 with a long
custom line template gets there with the sensors this bench already has.

The test builds exactly that configuration, runs it against a healthy broker,
and checks whether anything is published. It then verifies the device is still
alive and recovers once the batch comes back down.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402

# ~205 bytes per record once the tokens expand: comfortably over 8192 at 50.
LONG_LINE = ('{"timestamp_utc_seconds":{TS},"ch_a_temperature_celsius":{t0},'
             '"ch_b_temperature_celsius":{t1},"ch_c_temperature_celsius":{t3},'
             '"ch_c_relative_humidity":{u3},"ch_d_temperature":{t4},'
             '"ch_e_temperature":{t10},"ch_e_humidity":{u10}}')


def run(t, batch, mode, label, seconds=70):
    kill_stale()
    srv = Server('mqtt', f'oversize_{label}', C.OUT, port=C.PORT_MQTT,
                 mode='ok')
    t.send('enable', wait=1.2)
    t.send('configure terminal', wait=1.2)
    t.send(f'tel batch {batch}', wait=1.2)
    t.send('tel interval 1000', wait=1.2)
    t.send(f'tel mode {mode}', wait=1.2)
    t.send('end', wait=1.2)
    t.send('write memory', wait=3.0)
    C.tel_reset(t)
    C.tel_sync(t, wait=30)
    boots0 = t.port_drops
    time.sleep(seconds)
    st = srv.stats()
    srv.stop()
    s = C.status()
    msgs = st.get('msgs') or []
    return {
        'label': label, 'batch': batch, 'mode': mode,
        'publishes': st.get('publishes'), 'records': st.get('records'),
        'connects': st.get('connects'), 'connacks': st.get('connacks'),
        'largest_msg_bytes': max([m.get('bytes', 0) for m in msgs], default=0),
        'msg_bytes': [m.get('bytes') for m in msgs[-5:]],
        'usb_drops': t.port_drops - boots0,
        'heap': s.get('sys', {}).get('heap_f'),
        'dev_failed': s.get('metr', {}).get('tf'),
        'dev_sent': s.get('metr', {}).get('ts'),
        'uptime': s.get('sys', {}).get('uptime'),
    }


def main():
    kill_stale()
    t = Target(os.path.join(C.OUT, 'serial_mqtt_oversize.log'))
    time.sleep(1)
    web = C.web_session()

    C.log('=== switching to MQTT plain with a long custom line template ===')
    kill_stale()
    boot = Server('mqtt', 'oversize_boot', C.OUT, port=C.PORT_MQTT, mode='ok')
    C.commit(web, {
        't_transport': '1', 't_sec': '0', 't_srv': C.HOST_IP,
        't_port': str(C.PORT_MQTT), 't_int': '1000', 't_bat': '50',
        't_mode': '2',
        't_glob': '[{DATA}]',
        't_line': LONG_LINE,
        't_sep': ',',
        'm_topic': 'simut/data', 'm_qos': '0', 'm_retain': '0', 'm_ka': '30',
    })
    C.web_session(force=True)
    time.sleep(5)
    boot.stop()

    out = {}
    # Small batch first: proves the template itself publishes fine.
    out['small_batch5'] = run(t, 5, 'custom', 'small_batch5')
    C.log(json.dumps(out['small_batch5']))
    # Then the same template at batch 50, where the payload passes 8 KB.
    out['big_batch50'] = run(t, 50, 'custom', 'big_batch50')
    C.log(json.dumps(out['big_batch50']))
    # And back down, to show the stall is the payload size and not damage.
    out['recover_batch5'] = run(t, 5, 'custom', 'recover_batch5')
    C.log(json.dumps(out['recover_batch5']))

    big = out['big_batch50']
    out['verdict'] = ('STALL: nothing published at batch 50'
                      if not big['publishes'] else
                      f'published {big["publishes"]} msgs, '
                      f'largest {big["largest_msg_bytes"]} B')
    C.log('VERDICT: ' + out['verdict'])
    C.save('phase_mqtt_oversize.json', out)
    t.close()


if __name__ == '__main__':
    main()
