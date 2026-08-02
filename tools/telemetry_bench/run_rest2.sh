#!/usr/bin/env bash
# Second leg: the two runs that need the MQTT phases out of the way first.
set -u
cd "$(dirname "$0")"
exec >> results/run_rest2.out 2>&1

stamp() { echo "=== $(date +%H:%M:%S) $*"; }

stamp "waiting for leg 1"
while pgrep -f "[r]un_rest.sh" > /dev/null; do sleep 10; done

stamp "phase_mqtt_oversize"
python3 phase_mqtt_oversize.py

stamp "restoring HTTP transport for the full drain (reboot)"
python3 - <<'PY'
import sys, time
sys.path.insert(0, '.')
import campaign as C
w = C.web_session()
C.commit(w, {'t_transport': '0', 't_sec': '0', 't_srv': C.HOST_IP,
             't_port': str(C.PORT_HTTP), 't_path': '/ingest',
             't_int': '1000', 't_bat': '50', 't_mode': '0',
             't_glob': '{"dev":"{DEV}","mac":"{MAC}","data":[{DATA}]}',
             't_line': '{"ts":{TS},"t0_ID":{t0},"u0_ID":{u0}}',
             't_sep': ','})
C.web_session(force=True)
time.sleep(5)
print('transport back on HTTP')
PY

stamp "phase_drain_full"
python3 phase_drain_full.py 4200

stamp "LEG 2 DONE"
