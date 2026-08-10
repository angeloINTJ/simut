#!/usr/bin/env bash
# Runs the remaining phases back to back. Only one may hold the serial port at
# a time, so this is strictly sequential by design.
set -u
cd "$(dirname "$0")"
exec >> results/run_rest.out 2>&1

stamp() { echo "=== $(date +%H:%M:%S) $*"; }

stamp "waiting for phase_perf to finish"
while pgrep -f "[p]hase_perf.py" > /dev/null; do sleep 5; done

stamp "phase_payload"
python3 phase_payload.py

stamp "phase_drain"
python3 phase_drain.py 2400

stamp "phase_survive http"
python3 phase_survive.py 120 http

stamp "phase_survive tls"
python3 phase_survive.py 120 tls

stamp "phase_mqtt plain"
python3 phase_mqtt.py 90 plain

stamp "phase_mqtt tls"
python3 phase_mqtt.py 90 tls

stamp "ALL PHASES DONE"
