#!/usr/bin/env python3
"""Re-run, against the fixed firmware, every test that failed or exposed a defect.

A fix is only a fix if the thing that caught it now passes and nothing else
moved. So this runs three groups:

  regressions   the faults that killed or misreported (huge1mb, drip, error500,
                error401) — same servers, same windows, compared to the numbers
                the broken build produced
  fixes         the defects proved by inspection rather than by a crash
                (CSV header, /api/ls completeness, MQTT credentials)
  no-regression a perf spot-check at the two batch sizes that matter, to show
                the bounded read loops did not cost throughput
"""
import datetime as dt
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import campaign as C  # noqa: E402
import phase_survive as PS  # noqa: E402
from bench import Target, Server, kill_stale  # noqa: E402

OUT = C.OUT


def reboot_and_wait(t, why=''):
    """Give the run a clean network stack.

    D14: a burst of 1 MB responses leaks the 12-entry lwIP pbuf pool and the web
    server goes silent until reboot — with the device otherwise alive. Since
    /api/status is one of the three instruments, a run that starts on an
    exhausted pool measures the leak instead of the fix. Reboot between groups
    and let the ordering keep the leaky test last.
    """
    C.log(f'--- reboot {why}')
    t.send('enable', wait=1.5)
    t.send('reload confirm', wait=3.0)
    time.sleep(8)
    back = C.wait_web(C.DEV, timeout=120)
    C.web_session(force=True)
    C.log(f'    web back after {back}s')
    return back


def group_regressions(t, seconds=120):
    rows = []
    # huge1mb LAST on purpose: it is the one that leaks the pbuf pool, so
    # anything after it would be measuring D14 rather than its own fault.
    for name, kw in [
        ('error500', dict(server_kw={'mode': 'error500'})),
        ('error401', dict(server_kw={'mode': 'error401'})),
        ('drip',     dict(server_kw={'mode': 'drip', 'drip_ms': 400})),
    ]:
        rows.append(PS.run_fault(t, name, C.PORT_HTTP, False, seconds, **kw))
        reboot_and_wait(t, f'after {name}')
    # The TLS kill is reached through a different door — WiFiClientSecure's
    # available() drives the BearSSL engine, so the same unbounded drain in
    # HTTPClient::disconnect() blocks there too — but it should fall to the
    # same fix. Verify rather than assume.
    rows.append(PS.run_fault(t, 'tls_slow20', C.PORT_HTTPS, True, seconds,
                             server_kw={'mode': 'ok', 'tls_fault': 'slow',
                                        'delay': 20}))
    reboot_and_wait(t, 'after tls_slow20')
    rows.append(PS.run_fault(t, 'huge1mb', C.PORT_HTTP, False, seconds,
                             server_kw={'mode': 'huge', 'huge_bytes': 1048576}))
    return rows


def check_csv_header(t, seconds=45):
    kill_stale()
    srv = Server('http', 'reval_csv', OUT, port=C.PORT_HTTP, mode='ok',
                 raw_dump=6)
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=5,
               interval=2000, mode='csv', path='/ingest')
    C.tel_reset(t)
    C.tel_sync(t, wait=30)
    time.sleep(seconds)
    st = srv.stats()
    srv.stop()
    body = None
    for b in st.get('raw_bodies', []):
        if b.startswith('timestamp;'):
            body = b
            break
    res = {'found_csv_body': body is not None}
    if body:
        lines = [l for l in body.split('\n') if l]
        hdr = lines[0].split(';')
        widths = {len(l.split(';')) for l in lines[1:]}
        res.update({
            'header_cols': len(hdr),
            'row_cols': sorted(widths),
            'match': len(widths) == 1 and len(hdr) == list(widths)[0],
            'header': lines[0][:400],
            'first_row': lines[1][:200] if len(lines) > 1 else None,
        })
    # Put the transport back the way the other checks expect it.
    C.cfg_http(t, C.HOST_IP, C.PORT_HTTP, crypto=False, batch=10,
               interval=1000, mode='json', path='/ingest')
    return res


def check_ls_complete():
    """Does /api/ls now report everything — and say so when it cannot?"""
    w = C.web_session(force=True)
    listings = []
    for _ in range(4):
        try:
            j = w.get('/api/ls?dir=/history').json()
            listings.append({'n': len(j.get('entries', [])),
                             'truncated': j.get('truncated', False),
                             'names': sorted(e['n'] for e in j.get('entries', []))})
        except Exception as e:
            listings.append({'err': str(e)[:80]})
            w = C.web_session(force=True)
        time.sleep(1.5)
    ok = [l for l in listings if 'names' in l]
    stable = len({tuple(l['names']) for l in ok}) <= 1 if ok else False
    inv = os.path.join(OUT, 'history_inventory.json')
    truth = None
    if os.path.exists(inv):
        with open(inv) as fh:
            truth = json.load(fh)
    res = {
        'listings': [{k: v for k, v in l.items() if k != 'names'} for l in listings],
        'stable_across_calls': stable,
        'counts': [l.get('n') for l in listings],
    }
    if truth and ok:
        real = {p['file'] for p in truth['per_file']}
        got = set(ok[-1]['names'])
        res.update({
            'files_on_flash': len(real),
            'files_listed': len(got),
            'missing_from_listing': sorted(real - got),
            'complete': real.issubset(got),
        })
    return res


def check_mqtt_credentials(t, seconds=70):
    """D1: does the password typed on the config page reach the broker?"""
    import phase_mqtt as PM
    res = PM.config_fidelity(t, C.web_session(), C.PORT_MQTT, seconds=seconds)
    # Put the transport back on HTTP before anything else runs. telTransport is
    # read once, in TelemetryManager::begin( ), so only a commit_all + reboot
    # moves it — a CLI `tel server`/`tel port` cannot. Skipping this left the
    # device speaking MQTT at an HTTP sink and the next perf run measured
    # 0 records/s, which reads exactly like a throughput regression and is not.
    C.log('--- restoring HTTP transport after the MQTT check')
    C.commit(C.web_session(), {
        't_transport': '0', 't_sec': '0', 't_srv': C.HOST_IP,
        't_port': str(C.PORT_HTTP), 't_path': '/ingest',
        't_int': '1000', 't_bat': '50', 't_mode': '0',
        'm_topic': 'simut/data', 'm_cid': '', 'm_user': '',
        'm_qos': '0', 'm_retain': '0', 'm_ka': '60'})
    C.web_session(force=True)
    time.sleep(5)
    return res


def perf_spotcheck(t, seconds=90):
    import phase_perf as PP
    rows = []
    rows += PP.http_matrix(t, False, C.PORT_HTTP, [50], seconds, 'reval')
    rows += PP.http_matrix(t, True, C.PORT_HTTPS, [50], seconds, 'reval')
    return rows


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else 'all'
    kill_stale()
    t = Target(os.path.join(OUT, 'serial_reval.log'))
    time.sleep(1)
    out = {'started': dt.datetime.now().isoformat()}

    if which in ('all', 'fixes'):
        C.log('=== CSV header')
        out['csv'] = check_csv_header(t)
        C.log(json.dumps({k: v for k, v in out['csv'].items() if k != 'header'}))
        C.log('  header: ' + str(out['csv'].get('header'))[:300])
        C.save('revalidate.json', out)

        C.log('=== /api/ls completeness')
        out['api_ls'] = check_ls_complete()
        C.log(json.dumps({k: v for k, v in out['api_ls'].items()
                          if k != 'listings'}))
        C.save('revalidate.json', out)

        C.log('=== MQTT credentials on the wire')
        out['mqtt_fidelity'] = check_mqtt_credentials(t)
        C.log(json.dumps(out['mqtt_fidelity']['checks']))
        C.save('revalidate.json', out)

    if which in ('all', 'perf'):
        C.log('=== perf spot-check (no-regression)')
        out['perf'] = perf_spotcheck(t)
        C.save('revalidate.json', out)

    # Last, because huge1mb leaks the pbuf pool and takes the web server with
    # it (D14). Everything that needs /api/status has already run by now.
    if which in ('all', 'regress'):
        C.log('=== regressions: the faults that failed on the broken build')
        out['regressions'] = group_regressions(t)
        for r in out['regressions']:
            C.log(f'  {r["fault"]:10s} {r["verdict"]:40s} '
                  f'reboots={r["usb_drops"]} devFail+{r["dev_failed_delta"]}')
        C.save('revalidate.json', out)

    t.close()
    C.save('revalidate.json', out)
    C.log('revalidation done')


if __name__ == '__main__':
    main()
