#!/usr/bin/env bash
# Third leg: true history inventory, once nothing else is loading the device.
set -u
cd "$(dirname "$0")"
exec >> results/run_rest3.out 2>&1

stamp() { echo "=== $(date +%H:%M:%S) $*"; }

stamp "waiting for leg 2"
while pgrep -f "[r]un_rest2.sh" > /dev/null; do sleep 15; done

stamp "enumerate_history (by date, not by /api/ls)"
python3 enumerate_history.py

stamp "LEG 3 DONE"
