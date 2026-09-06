"""
PlatformIO pre-build guard — every usable CLI command must be documented.

A command is reachable through three independent places, and they drift:

    SystemDefs_Cli.h   declares CMD_*
    CommandParser.cpp  decides which text produces it
    CommandManager.cpp getCommandModeMask( ) says where it is accepted,
                       printModeHelp( )      says where it is advertised

This project has already paid for that drift twice. Commit d48b9a7 restored 21
commands the parser could no longer reach. Later, `system touch reset` turned
out to be usable but absent from every help screen, so the only way to find it
was to read the source — and `language` / `time` were listed exclusively inside
the config-mode block while their mask is USER|PRIV, meaning showIf( ) filtered
them out of the one section that mentioned them and they rendered nowhere at
all.

So the invariant checked here is not "is it mentioned somewhere" but the one
that matters to a user at a prompt: for every mode a command is accepted in,
some help block that renders in that mode must list it.

Navigation commands are exempt: printModeHelp writes them as literal
consolePrintln lines rather than through showIf, and they are verified by
reading the rendered output, not this script.

Project: SIMUT
License: MIT
"""

import os
import re
import sys

Import("env")

PROJECT = env.subst("$PROJECT_DIR")

# The image ships one of two CLIs (SIMUT_CLI_FULL in SystemDefs_Cli.h), and the
# invariant is the same for both: a user at the prompt must be able to discover
# every command that prompt accepts. What differs is where the help comes from
# — printModeHelp( ) for the full CLI, the short HELP_TEXT_EN block for the
# emergency one — so the check picks its source from the build flags.
def _cli_full():
    # The env still carries build_flags as raw strings at pre: time (CPPDEFINES
    # is only populated later), and several -D land joined in one entry.
    flags = " ".join(str(f) for f in env.get("BUILD_FLAGS", []))
    m = re.findall(r'-DSIMUT_CLI_FULL=(\d+)', flags)
    if m:
        return m[-1] != "0"       # last -D wins, as it does for the compiler
    return True                   # header default


CLI_FULL = _cli_full()

# Commands the emergency image is expected to reach. Keeping this list here —
# rather than deriving it — is the point: gating or ungating a command in
# CommandParser.cpp without updating the short HELP_TEXT_EN block trips this,
# which is exactly the drift the guard exists to catch.
EMERGENCY_EXPECTED = {
    'CMD_HELP', 'CMD_RELOAD', 'CMD_SHOW_LOGS', 'CMD_SHOW_SYSINFO',
    'CMD_SHOW_NET', 'CMD_DEBUG', 'CMD_RESET_ADMIN', 'CMD_FACTORY_RESET',
    'CMD_FORMAT_FS', 'CMD_HTTPS_OFF', 'CMD_AP', 'CMD_UNKNOWN',
    'CMD_AIR_STOP', 'CMD_AIR_IDLE', 'CMD_AIR_HIBERNATE', 'CMD_AIR_STATUS',
    'CMD_SET_WIFI_SSID', 'CMD_SET_WIFI_PASS',
}


def strip_gated(src):
    """Drop #if SIMUT_CLI_FULL blocks — what the emergency image compiles."""
    out, depth = [], 0
    for line in src.splitlines():
        s = line.lstrip()
        if s.startswith('#if SIMUT_CLI_FULL'):
            depth += 1
            continue
        if depth:
            if s.startswith('#if'):
                depth += 1
            elif s.startswith('#endif'):
                depth -= 1
            continue
        out.append(line)
    return '\n'.join(out)


def strip_comments(src):
    """CMD_* names are discussed in prose too — the file header explains which
    command the slot-first catch-all used to swallow. Scanning comments makes
    that read as a reachable command."""
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.DOTALL)
    return re.sub(r'//[^\n]*', '', src)


def check_emergency_cli(parser_src):
    reachable = set(re.findall(r'(CMD_[A-Z0-9_]+)',
                               strip_comments(strip_gated(parser_src))))
    extra = reachable - EMERGENCY_EXPECTED
    missing = EMERGENCY_EXPECTED - reachable
    if extra or missing:
        print('[cli-help] FATAL: the emergency CLI surface moved.')
        for c in sorted(extra):
            print(f'[cli-help]   reachable but undocumented: {c}')
        for c in sorted(missing):
            print(f'[cli-help]   documented but unreachable: {c}')
        print('[cli-help] Update the short HELP_TEXT_EN block in HelpLicenseEN.h')
        print('[cli-help] and the @HELP section of both data/lang/*.lng to match,')
        print('[cli-help] then update EMERGENCY_EXPECTED in this file.')
        sys.exit(1)
    print(f'[cli-help] OK emergency CLI: {len(reachable)} commands, help in sync')


# Written as literal lines in the navigation block, not via showIf.
NAV_EXEMPT = {
    'CMD_ENABLE', 'CMD_DISABLE', 'CMD_CONFIGURE', 'CMD_EXIT', 'CMD_END',
    'CMD_DO', 'CMD_HELP', 'CMD_SENSOR_ENTER',
}
SENTINELS = {'CMD_NONE', 'CMD_UNKNOWN'}

MODE_BITS = {
    'CLI_VALID_USER': {'USER'},
    'CLI_VALID_PRIV': {'PRIV'},
    'CLI_VALID_CONFIG': {'CONFIG'},
    'CLI_VALID_SENSOR': {'SENSOR'},
    'CLI_VALID_READONLY': {'USER', 'PRIV', 'CONFIG'},
    'CLI_VALID_ALL': {'USER', 'PRIV', 'CONFIG', 'SENSOR'},
}


def modes_of(expr):
    modes = set()
    for name, bits in MODE_BITS.items():
        if re.search(rf'\b{name}\b', expr):
            modes |= bits
    return modes


def blocks_of(help_body):
    """Map each showIf'd command to the modes whose help block contains it."""
    listed = {}
    current = {'USER', 'PRIV', 'CONFIG', 'SENSOR'}
    for line in help_body.splitlines():
        if '_cliMode != CLI_MODE_SENSOR_CONFIG' in line:
            current = {'USER', 'PRIV', 'CONFIG'}
        elif '_cliMode == CLI_MODE_USER_EXEC' in line and '_cliMode == CLI_MODE_PRIV_EXEC' in line:
            current = {'USER', 'PRIV'}
        elif '_cliMode == CLI_MODE_PRIV_EXEC' in line and 'showIf' not in line:
            current = {'PRIV'}
        elif '_cliMode == CLI_MODE_GLOBAL_CONFIG' in line:
            current = {'CONFIG'}
        elif '_cliMode == CLI_MODE_SENSOR_CONFIG' in line and 'showIf' not in line:
            current = {'SENSOR'}
        m = re.search(r'showIf\(\s*(CMD_[A-Z0-9_]+)', line)
        if m:
            listed.setdefault(m.group(1), set()).update(current)
    return listed


def check_cli_help(*args, **kwargs):
    try:
        mgr = open(os.path.join(PROJECT, 'src/CommandManager.cpp'),
                   encoding='utf-8').read()
        parser = open(os.path.join(PROJECT, 'src/CommandParser.cpp'),
                      encoding='utf-8').read()
    except Exception as exc:
        print(f"[cli-help] WARNING: could not read sources ({exc}) — check skipped")
        return

    if not CLI_FULL:
        check_emergency_cli(parser)
        return

    masks = dict(re.findall(r'case\s+(CMD_[A-Z0-9_]+):\s*return\s+([A-Z_ |]+);', mgr))
    reachable = set(re.findall(r'(CMD_[A-Z0-9_]+)', parser))

    start = mgr.find('void CommandManager::printModeHelp')
    if start < 0:
        print('[cli-help] WARNING: printModeHelp not found — check skipped')
        return
    body = mgr[start:]
    end = body.find('\n}\n')
    listed = blocks_of(body[:end] if end > 0 else body)

    problems = []
    for cmd, expr in sorted(masks.items()):
        if cmd in NAV_EXEMPT or cmd in SENTINELS or cmd not in reachable:
            continue
        usable = modes_of(expr)
        if not usable:
            continue
        shown = listed.get(cmd, set()) & usable
        if not shown:
            where = ','.join(sorted(listed.get(cmd, set()))) or 'nowhere'
            problems.append(f'{cmd}: accepted in {",".join(sorted(usable))} '
                            f'but only listed in {where}')

    if problems:
        print('[cli-help] FATAL: commands users can run but cannot discover:')
        for p in problems:
            print(f'[cli-help]   {p}')
        print('[cli-help] Add a showIf( ) in a block that renders in that mode,')
        print('[cli-help] or widen the mask in getCommandModeMask( ).')
        sys.exit(1)

    print(f'[cli-help] OK {len(masks)} commands, all discoverable where accepted')


check_cli_help()
