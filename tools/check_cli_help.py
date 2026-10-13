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

So the invariant checked here is not "is it mentioned somewhere" but one that
matters to a user at a prompt: some help block that renders in a mode where the
command is accepted must list it. Not yet "in every mode it is accepted in" —
on 2026-09-24 that stronger form flagged three commands (`alarm show` in config
mode, `debug` and `sensor scan` in user EXEC), each listed in another mode
where it works too.

A command with no row in the CLI_COMMANDS table is accepted wherever the
lookup's fallback says — every mode — and until 2026-09-24 this script skipped
it, because it walked the mask rather than the parser. That is how `touch hold`
was merged in #159 accepted at every prompt, user EXEC included, and listed at
none, and how the five `air` commands went unlisted on the Air from the day it
got this CLI (2026-09-18). The walk now starts from what the parser can
produce, minus comments and the branches this image does not compile: the
Air-only block outside the Air and, since 2026-10-13, the radio's commands on a
wired build (tools/cli_gates.py evaluates the `#if`).

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
sys.path.insert(0, os.path.join(PROJECT, "tools"))
import cli_gates  # noqa: E402


def _flag(name, default):
    """The value the compiler sees for -D<name>=<n>, or the header's default.
    The env still carries build_flags as raw strings at pre: time (CPPDEFINES
    is only populated later), and several -D land joined in one entry; the
    last -D wins, as it does for the compiler."""
    flags = " ".join(str(f) for f in env.get("BUILD_FLAGS", []))
    m = re.findall(rf'-D{name}=(\d+)', flags)
    return int(m[-1]) if m else default


# The image ships one of two CLIs (SIMUT_CLI_FULL in SystemDefs_Cli.h), and the
# invariant is the same for both: a user at the prompt must be able to discover
# every command that prompt accepts. What differs is where the help comes from
# — printModeHelp( ) for the full CLI, the short HELP_TEXT_EN block for the
# emergency one — so the check picks its source from the build flags. The
# defaults are the headers': SystemDefs_Cli.h for the CLI, simut_config.h for
# the network.
FLAGS = {
    'SIMUT_CLI_FULL': _flag('SIMUT_CLI_FULL', 1),
    'SIMUT_AIR': _flag('SIMUT_AIR', 0),
    'SIMUT_NET_LAN': _flag('SIMUT_NET_LAN', 0),
}
CLI_FULL = FLAGS['SIMUT_CLI_FULL'] != 0
AIR = FLAGS['SIMUT_AIR'] != 0
LAN = FLAGS['SIMUT_NET_LAN'] != 0

# Commands the emergency image is expected to reach. Keeping this list here —
# rather than deriving it — is the point: gating or ungating a command in
# CommandParser.cpp without updating the short HELP_TEXT_EN block trips this,
# which is exactly the drift the guard exists to catch.
EMERGENCY_EXPECTED = {
    'CMD_HELP', 'CMD_RELOAD', 'CMD_SHOW_LOGS', 'CMD_SHOW_SYSINFO',
    'CMD_SHOW_NET', 'CMD_DEBUG', 'CMD_RESET_ADMIN', 'CMD_FACTORY_RESET',
    'CMD_FORMAT_FS', 'CMD_HTTPS_OFF', 'CMD_AP', 'CMD_UNKNOWN',
    'CMD_SET_WIFI_SSID', 'CMD_SET_WIFI_PASS',
    # 'system cors' is emergency-surface for a reason that is not recovery: the
    # release image builds with SIMUT_CLI_FULL=0, so gating it would mean the
    # published firmware has NO way to set the origin — and the manager page
    # cannot set it either, because without it the page cannot reach the device.
    'CMD_SET_CORS_ORIGIN',
    # 'time' since 2026-10-01: a unit with no network configured says at boot
    # that its clock is provisional and names this command, and on the alpha
    # the console is the only way to answer — so the release and alpha images
    # have to parse it, not only the full CLI.
    'CMD_SET_TIME',
}

# The `air` family exists only where SIMUT_AIR is set: both the parser block
# and the help block are guarded, so expecting them everywhere would force the
# release and alpha help to advertise commands their firmware cannot run.
if AIR:
    EMERGENCY_EXPECTED |= {'CMD_AIR_STOP', 'CMD_AIR_IDLE', 'CMD_AIR_CHARGER',
                           'CMD_AIR_HIBERNATE', 'CMD_AIR_STATUS'}

# A wired build (SIMUT_NET_LAN, 2026-10-13) has no radio: no `ap`, no network
# name or key to set. Its address takes their place — `ip` and `dns`, the full
# CLI's own commands — because a Wire on a network without DHCP has no other
# way in from this console.
if LAN:
    EMERGENCY_EXPECTED -= {'CMD_AP', 'CMD_SET_WIFI_SSID', 'CMD_SET_WIFI_PASS'}
    EMERGENCY_EXPECTED |= {'CMD_IP_CFG', 'CMD_SET_DNS_CFG'}


def strip_gated(src):
    """The source as this image compiles it, line count unchanged.

    Until 2026-10-13 this knew two guards by the start of a line — `#if
    SIMUT_CLI_FULL` for the emergency image, `#if SIMUT_AIR` outside the Air —
    and dropped the whole block, `#else` included. A wired build adds `#if
    SIMUT_NET_WIFI` and `#if SIMUT_CLI_FULL || SIMUT_NET_LAN`, so the
    expression is evaluated now (tools/cli_gates.py); a guard over any other
    macro is kept, as it always was.
    """
    return cli_gates.strip_gated(src, FLAGS)


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
        print('[cli-help] and the @HELP section (@HELPWIRE on a wired build) of')
        print('[cli-help] both data/lang/*.lng to match,')
        print('[cli-help] then update EMERGENCY_EXPECTED in this file.')
        sys.exit(1)
    print(f'[cli-help] OK emergency CLI{" (wired)" if LAN else ""}: '
          f'{len(reachable)} commands, help in sync')


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
    # The help too: a showIf( ) under `#if SIMUT_NET_WIFI` is not on a wired
    # build's screen.
    mgr = strip_gated(mgr)

    # The mode mask lives in the CLI_COMMANDS table now (was a switch in
    # getCommandModeMask). Each row is `{ CMD_X, MASK },`; a command with no row
    # takes the fallback the lookup returns. The full CLI only: the emergency
    # image defines getCommandModeMask as a one-liner and carries no table.
    tbl = mgr.find('CLI_COMMANDS[] = {')
    fn = mgr.find('uint8_t getCommandModeMask(DemandType t) {')
    if tbl < 0 or fn < 0:
        print('[cli-help] WARNING: CLI_COMMANDS / getCommandModeMask not found — check skipped')
        return
    table = mgr[tbl:mgr.find('\n};', tbl)]
    masks = dict(re.findall(r'\{\s*(CMD_[A-Z0-9_]+),\s*([A-Z_ |]+?)\s*\}', table))
    fnbody = mgr[fn:mgr.find('\n}\n', fn)]
    m = re.search(r'return\s+(CLI_VALID_[A-Z_ |]+);', fnbody)
    default = m.group(1) if m else None
    reachable = set(re.findall(r'(CMD_[A-Z0-9_]+)',
                               strip_comments(strip_gated(parser))))

    start = mgr.find('void CommandManager::printModeHelp')
    if start < 0:
        print('[cli-help] WARNING: printModeHelp not found — check skipped')
        return
    body = mgr[start:]
    end = body.find('\n}\n')
    listed = blocks_of(body[:end] if end > 0 else body)

    problems = []
    checked = defaulted = 0
    for cmd in sorted(reachable - NAV_EXEMPT - SENTINELS):
        expr = masks.get(cmd, default)
        if expr is None:
            problems.append(f'{cmd}: no row in CLI_COMMANDS and no fallback mask')
            continue
        checked += 1
        defaulted += cmd not in masks
        usable = modes_of(expr)
        if not usable:
            continue
        shown = listed.get(cmd, set()) & usable
        if not shown:
            where = ','.join(sorted(listed.get(cmd, set()))) or 'nowhere'
            how = '' if cmd in masks else ' (no case: the default mask)'
            problems.append(f'{cmd}: accepted in {",".join(sorted(usable))}{how} '
                            f'but only listed in {where}')

    if problems:
        print('[cli-help] FATAL: commands users can run but cannot discover:')
        for p in problems:
            print(f'[cli-help]   {p}')
        print('[cli-help] Add a showIf( ) in a block that renders in that mode,')
        print('[cli-help] or add its row to the CLI_COMMANDS table.')
        sys.exit(1)

    print(f'[cli-help] OK {checked} commands ({defaulted} on the default mask), '
          f'all discoverable where accepted')


check_cli_help()
