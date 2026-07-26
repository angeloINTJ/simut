"""PlatformIO pre-build guard — the five log-code tables must agree.

A log code lives in the LogCode enum, translateCodeEn( ), EVT_NAMES_EN,
EVT_NAMES_PT and the @LOGCODES block of each pack. Adding one is five manual
edits; v1.5.6-beta shipped five codes that never reached the browser tables and
one that reached no English table at all, rendering as "?" everywhere.

The real work is in tools/gen_logcodes.py; this wrapper only wires --check into
the build so the drift is caught locally, not two releases later. Same pattern
as check_cli_help.py.

Project: SIMUT
License: MIT
"""

import subprocess
import sys

Import("env")

PROJECT = env.subst("$PROJECT_DIR")
r = subprocess.run([sys.executable, f"{PROJECT}/tools/gen_logcodes.py", "--check"],
                   capture_output=True, text=True)
print(r.stdout.rstrip() or r.stderr.rstrip())
if r.returncode != 0:
    sys.exit(1)
