#!/usr/bin/env python3
"""
Static consistency gate for the SIMUT Air build (no hardware, no PlatformIO).

Each check maps to a finding of the 2026-09-06 review
(docs/analysis/SIMUT_AIR_PLANO_FIX.md, F15–F19). The gate is meant to run
before every push of the Air branch and, once green, in CI next to
check_authz / check_fsguard.

  C1  "[AIR]" console markers in shared translation units must sit inside
      #if SIMUT_AIR — otherwise the TFT release prints Air debug lines (F16).
  C2  the emergency help block (HelpLicenseEN.h, #if !SIMUT_CLI_FULL) may only
      advertise `air …` under an #if SIMUT_AIR guard (F15).
  C3  every command line of the EN emergency help exists in the @HELP section
      of every language pack (F15/F20), and the pack lists none the help does
      not; a wired build's help (SIMUT_NET_LAN) is held to @HELPWIRE the same
      way (2026-10-13).
  C4  tools/check_cli_help.py must not expect CMD_AIR_* unconditionally: the
      list has to depend on SIMUT_AIR (F15).
  C5  every Air method declared in AppManager.h has a definition (F17).
  C6  the pico_w_air rationale in tools/features.toml agrees with its cli_full
      setting (F18) — since the P1 switchover the air env is generated from there.
  C7  AirConfig fields marked obsolete in the header must not be written by
      firmware code outside AirConfig.h (F17) — informational.
  C8  LogManager's scratch register map documents the Air marker in
      scratch[0] (F19).
  C9  `air stop` starts the network and the web server each under its own
      flag: a wake that raises the radio has the network and no listener
      (2026-11-04).
  C10 `ap` is refused while the sleep cycle is active, in both CLI languages,
      with the way out (`air stop`) in the text (2026-11-04).

Exit code: 0 clean, 1 findings. Run: python3 tools/check_air_consistency.py [--verbose]

Project: SIMUT
License: MIT
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import cli_gates  # noqa: E402

AIR_ONLY_FILES = {'AppManager_Air.cpp', 'DisplayManager_None.cpp'}

problems = []
verbose = '--verbose' in sys.argv


def read(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return f.read()


def problem(check, msg):
    problems.append((check, msg))
    print(f'[air-consistency] {check}: {msg}')


def blank_comments(src):
    """Replace comment bodies with spaces, keeping every newline in place.

    Line numbers have to survive because the findings quote them, so this
    blanks characters instead of deleting them. Without it the checker reports
    its own explanatory comments: prose that mentions "[AIR]" while explaining
    the guard is not a marker being printed.
    """
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith('/*', i):
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(c if c == '\n' else ' ' for c in src[i:j]))
            i = j
        elif src.startswith('//', i):
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        else:
            out.append(src[i])
            i += 1
    return ''.join(out)


def guarded_lines(src):
    """Yield (lineno, line, inside_air_guard) tracking #if SIMUT_AIR nesting."""
    depth_stack = []       # True when the frame is an SIMUT_AIR guard
    for n, line in enumerate(src.splitlines(), 1):
        s = line.strip()
        if s.startswith('#if'):
            depth_stack.append(bool(re.match(r'#if\s+SIMUT_AIR\b', s)))
        elif s.startswith('#else') and depth_stack:
            depth_stack[-1] = False        # the else branch of #if SIMUT_AIR is not Air
        elif s.startswith('#endif') and depth_stack:
            depth_stack.pop()
        yield n, line, any(depth_stack)


# C1 — [AIR] markers outside guards in shared files -----------------------

def c1_air_markers():
    for path in sorted(glob.glob(os.path.join(SRC, '**', '*.cpp'), recursive=True)):
        name = os.path.basename(path)
        if name in AIR_ONLY_FILES or os.sep + 'air' + os.sep in path:
            continue
        for n, line, guarded in guarded_lines(blank_comments(read(path))):
            if '"[AIR]' in line and not guarded:
                problem('C1', f'{os.path.relpath(path, ROOT)}:{n}: "[AIR]" marker outside #if SIMUT_AIR')


# C2 — emergency help advertising `air` without a guard -----------------

def emergency_help_block(src):
    """Return (help_text, first_line) of the emergency help raw string.

    Only the text inside R"raw( ... )raw" of the #if !SIMUT_CLI_FULL block is
    help; the surrounding C++ (comment banner, declaration) must not be read
    as commands. Preprocessor lines inside the block are kept so C2 can see
    an #if SIMUT_AIR guard around the air commands (two adjacent raw literals).
    """
    m = re.search(r'^#if\s+!SIMUT_CLI_FULL\b[^\n]*\n', src, re.MULTILINE)
    if not m:
        return '', 0
    # The #else that closes it, found by depth: the block holds #if/#else pairs
    # of its own since 2026-10-13 (the wired build's lines), and taking the
    # first "\n#else" cut C3 down to two commands without a word.
    depth, off, end = 0, m.end(), None
    for line in src[m.end():].split('\n'):
        s = line.strip()
        if s.startswith('#if'):
            depth += 1
        elif s.startswith('#endif'):
            if depth == 0:
                end = off
                break
            depth -= 1
        elif s.startswith(('#else', '#elif')) and depth == 0:
            end = off
            break
        off += len(line) + 1
    if end is None:
        return '', 0
    block = src[m.end():end]
    first = block.find('R"raw(')
    last = block.rfind(')raw"')
    if first < 0 or last < 0:
        return '', 0
    text = block[first + len('R"raw('):last]
    # drop the C++ between two adjacent raw literals, keep #if/#else/#endif
    text = re.sub(r'\)raw"[^\n]*\n(?:(?!#)[^\n]*\n)*?[^\n]*R"raw\(', '\n', text)
    line = src[:m.start()].count('\n') + 1 + block[:first].count('\n')
    return text, line


def c2_emergency_help_guard():
    path = os.path.join(SRC, 'HelpLicenseEN.h')
    src = read(path)
    block, start = emergency_help_block(src)
    if not block:
        problem('C2', 'HelpLicenseEN.h: emergency help block not found')
        return
    for n, line, guarded in guarded_lines(blank_comments(block)):
        if re.match(r'\s*air\s', line) and not guarded:
            problem('C2', f'src/HelpLicenseEN.h:{start + n - 1}: "{line.strip()}" advertised to every '
                          f'emergency image (release/alpha have no air commands) — guard with #if SIMUT_AIR')


# C3 — packs' @HELP vs EN emergency help ---------------------------------

def help_commands(text):
    """Command lines = non-indented lines that look like `word …`, excluding banners."""
    cmds = []
    for line in text.splitlines():
        if not line or line[0].isspace() or line.startswith('=') or line.startswith(')'):
            continue
        if line.startswith('R"') or line.startswith('#') or line.startswith('SIMUT') or ':' in line[:12]:
            continue
        cmds.append(line.strip())
    return cmds


def pack_help_section(text, name='HELP'):
    m = re.search(rf'^@{name}\s*$(.*?)(?=^@[A-Z]+\s*$|\Z)', text, re.DOTALL | re.MULTILINE)
    return m.group(1) if m else ''


def emergency_help_text(src, lan):
    """The emergency help as an image with this network compiles it, Air off.

    tools/cli_gates.py blanks the branches of HelpLicenseEN.h such an image does
    not build — the full CLI's text, the Air lines (C2's business), the other
    network's lines — and the raw literals left in HELP_TEXT_EN are joined the
    way the compiler joins adjacent ones."""
    kept = cli_gates.strip_gated(src, {'SIMUT_CLI_FULL': 0, 'SIMUT_AIR': 0,
                                       'SIMUT_NET_LAN': int(lan)})
    start = kept.find('HELP_TEXT_EN[] PROGMEM =')
    end = kept.find(')raw";', start)
    if start < 0 or end < 0:
        return ''
    return ''.join(re.findall(r'R"raw\((.*?)\)raw"', kept[start:end + len(')raw"')], re.DOTALL))


# The section of a pack that holds each build's emergency help: a radio build
# reads @HELP, a wired one @HELPWIRE (src/LangPackIndex.h).
HELP_PROFILES = (('HELP', False), ('HELPWIRE', True))


def key_of(cmd_line):
    """`system ssid <name>` -> ('system', 'ssid'); `reload [confirm]` -> ('reload',)."""
    toks = [t for t in cmd_line.split() if not t.startswith('<') and not t.startswith('[')]
    return tuple(toks[:2])


def c3_packs_have_every_command():
    """Packs must carry every command EVERY emergency image has.

    A `.lng` is shared by all builds, and the non-English console serves its
    @HELP verbatim, so an Air-only command listed there would be advertised on
    release and alpha too — the same defect C2 catches on the C++ side. Only the
    commands outside `#if SIMUT_AIR` are required here; the Air ones need a
    firmware-side marker before a shared pack can carry them (plan F15).
    """
    src = read(os.path.join(SRC, 'HelpLicenseEN.h'))
    for name, lan in HELP_PROFILES:
        en_cmds = help_commands(emergency_help_text(src, lan))
        if not en_cmds:
            problem('C3', f'src/HelpLicenseEN.h: no emergency help read for @{name}')
            continue
        en_keys = {key_of(c): c for c in en_cmds}
        for pack in sorted(glob.glob(os.path.join(ROOT, 'data', 'lang', '*.lng'))):
            rel = os.path.relpath(pack, ROOT)
            sect = pack_help_section(read(pack), name)
            if not sect:
                problem('C3', f'{rel}: no @{name} section')
                continue
            pack_cmds = {key_of(c): c for c in help_commands(sect)}
            for k, c in sorted(en_keys.items()):
                if k not in pack_cmds:
                    problem('C3', f'{rel}: @{name} lacks "{c}"')
            # And the other way: a command the pack advertises that this
            # image does not have answers "unknown" — `ap` on a wired build.
            for k, c in sorted(pack_cmds.items()):
                if k not in en_keys:
                    problem('C3', f'{rel}: @{name} lists "{c}", which the EN help of '
                                  f'that build does not')
        if verbose:
            print(f'[air-consistency] C3: EN emergency help for @{name} lists '
                  f'{len(en_cmds)} commands')


# C4 — check_cli_help.py expects CMD_AIR_* regardless of SIMUT_AIR ------

def c4_cli_help_gate_is_build_aware():
    src = read(os.path.join(ROOT, 'tools', 'check_cli_help.py'))
    if 'CMD_AIR_' in src and 'SIMUT_AIR' not in src:
        problem('C4', 'tools/check_cli_help.py expects CMD_AIR_* for every emergency image but never '
                      'reads SIMUT_AIR from the build flags — release/alpha help would have to lie')


# C5 — declared-but-undefined Air methods --------------------------------

def c5_declared_methods_defined():
    hdr = read(os.path.join(SRC, 'AppManager.h'))
    m = re.search(r'#if SIMUT_AIR(.*?)#endif /\* SIMUT_AIR \*/', hdr, re.DOTALL)
    if not m:
        problem('C5', 'AppManager.h: SIMUT_AIR block not found')
        return
    declared = set(re.findall(r'\b(air[A-Z]\w*)\s*\(', m.group(1)))
    all_src = ''.join(read(p) for p in glob.glob(os.path.join(SRC, '**', '*.cpp'), recursive=True))
    for name in sorted(declared):
        if not re.search(r'AppManager::' + name + r'\s*\(', all_src):
            problem('C5', f'src/AppManager.h declares {name}() but nothing defines AppManager::{name}')


# C6 — platformio.ini comment vs flag ------------------------------------

def c6_env_comment_matches_flag():
    # Since the P1 switchover (docs/analysis/MODELO_DE_RECURSOS.md) the air
    # environment is generated from tools/features.toml; the cli_full setting and
    # the rationale comment that used to explain it both live there, not in a
    # platformio.ini [env:] block. The check is the same: the two must agree.
    import tomllib
    raw = read(os.path.join(ROOT, 'tools', 'features.toml'))
    try:
        prof = tomllib.loads(raw)['profiles']['pico_w_air']
    except Exception:
        problem('C6', 'tools/features.toml: [profiles.pico_w_air] not found')
        return
    cli_full = 1 if prof.get('cli_full') else 0
    m = re.search(r'((?:#[^\n]*\n)+)\[profiles\.pico_w_air\]', raw)
    comment = m.group(1) if m else ''
    said = re.findall(r'SIMUT_CLI_FULL=(\d)', comment)
    if said and int(said[-1]) != cli_full:
        problem('C6', f'features.toml: pico_w_air rationale says SIMUT_CLI_FULL={said[-1]} '
                      f'but the profile sets cli_full={"true" if cli_full else "false"}')


# C7 — obsolete AirConfig fields written by firmware (informational) -----

DEAD_FIELD_CANDIDATES = ('wifiScanTimeoutMs', 'flushTimeoutMs')   # plan F17


def c7_obsolete_fields_unused():
    """Informational: AirConfig fields the plan calls dead must be read
    somewhere (then they are alive again — update the plan) or removed."""
    hdr = read(os.path.join(SRC, 'air', 'AirConfig.h'))
    present = [f for f in DEAD_FIELD_CANDIDATES if re.search(r'\b' + f + r'\b', hdr)]
    if not present:
        return
    srcs = [(p, read(p)) for p in glob.glob(os.path.join(SRC, '**', '*.cpp'), recursive=True)]
    for f in present:
        readers = [os.path.relpath(p, ROOT) for p, s in srcs if re.search(r'\.' + f + r'\b', s)]
        if readers:
            print(f'[air-consistency] C7 info: {f} is read by {", ".join(readers)} — no longer dead, update the plan')
        else:
            print(f'[air-consistency] C7 info: AirConfig.{f} is stored but never read (dead field, plan F17)')


# C9 — `air stop` starts the web server under its own flag ----------------

def block_body(src, start):
    """The text inside the {...} block whose '{' is the first one at or after `start`, nested braces counted."""
    i = src.index('{', start)
    depth = 0
    for j in range(i, len(src)):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                return src[i + 1:j], j
    raise ValueError('unbalanced braces')


# The call, with its first argument: the boot's BLOG( ) lines name `_webMgr->begin( )` inside string literals, which a bare
# `_webMgr->begin(` would match.
WEB_BEGIN_RE = re.compile(r'_webMgr->begin\(\s*_storageMgr\b')


def c9_air_stop_starts_the_web():
    """A telemetry wake raises the radio (_airRadioUp) and still has no web server: the boot starts the server in M0 only
    (AppManager_Boot.cpp, "web skipped (wake)"). `air stop` used to start the network AND the server under
    `if (!_airRadioUp)`, so on a telemetry wake it started neither the server nor anything else and left M0 with no
    browser, against docs/_manual/cap19-air.md and F13 of the plan. Held by three facts: _airWebUp exists and starts false;
    the boot sets it, `!_airActive`, in the #if SIMUT_AIR block that skips the server on a wake (so it is true exactly when
    the boot starts the server); `air stop` starts the server under `if (!_airWebUp)`, outside the radio's block."""
    hdr = read(os.path.join(SRC, 'AppManager.h'))
    m = re.search(r'#if SIMUT_AIR(.*?)#endif /\* SIMUT_AIR \*/', hdr, re.DOTALL)
    if not m or not re.search(r'\bbool\s+_airWebUp\s*=\s*false\s*;', blank_comments(m.group(1))):
        problem('C9', 'src/AppManager.h: the SIMUT_AIR block does not declare `bool _airWebUp = false;`')
    boot = blank_comments(read(os.path.join(SRC, 'AppManager_Boot.cpp')))
    begins = [x.start() for x in WEB_BEGIN_RE.finditer(boot)]
    sets = list(re.finditer(r'\b_airWebUp\s*=\s*([^;]*);', boot))
    if len(begins) != 1 or len(sets) != 1 or ' '.join(sets[0].group(1).split()) != '!_airActive':
        problem('C9', 'src/AppManager_Boot.cpp: the boot must assign the flag once, `_airWebUp = !_airActive;`: it starts the server exactly when this is not a wake')
    else:
        s0 = sets[0].start()
        between = boot[sets[0].end():begins[0]] if s0 < begins[0] else ''
        skip = re.search(r'if\s*\(\s*_airActive\s*\)\s*\{[^{}]*\}\s*else', between)
        if not [g for n, _l, g in guarded_lines(boot) if n == boot.count('\n', 0, s0) + 1 and g]:
            problem('C9', 'src/AppManager_Boot.cpp: `_airWebUp = !_airActive;` is outside #if SIMUT_AIR')
        elif not skip or re.search(r'#\s*(?:if|endif)', between[:skip.start()]):
            problem('C9', 'src/AppManager_Boot.cpp: `_airWebUp = !_airActive;` must sit in the same #if SIMUT_AIR block as the `if (_airActive) { ... } else` '
                          'that skips the web server on a wake, before the one `_webMgr->begin(`')
    cmds = blank_comments(read(os.path.join(SRC, 'AppManager_Commands.cpp')))
    i = cmds.find('case CMD_AIR_STOP:')
    j = cmds.find('case CMD_AIR_IDLE', i)
    if i < 0 or j < 0:
        problem('C9', 'src/AppManager_Commands.cpp: `case CMD_AIR_STOP:` not found')
        return
    stop = cmds[i:j]
    radio = re.search(r'if\s*\(\s*!\s*_airRadioUp\s*\)\s*\{', stop)
    web = re.search(r'if\s*\(\s*!\s*_airWebUp\s*\)\s*\{', stop)
    if not radio:
        problem('C9', 'src/AppManager_Commands.cpp: `air stop` has no `if (!_airRadioUp) {` block for the network')
        return
    radio_body, radio_end = block_body(stop, radio.end() - 1)
    if '_netMgr->begin(' not in radio_body:
        problem('C9', 'src/AppManager_Commands.cpp: `air stop` does not start the network under `if (!_airRadioUp)`')
    if WEB_BEGIN_RE.search(radio_body):
        problem('C9', 'src/AppManager_Commands.cpp: `air stop` starts the web server inside `if (!_airRadioUp)`: a telemetry wake has the radio and no server')
    if not web:
        problem('C9', 'src/AppManager_Commands.cpp: `air stop` has no `if (!_airWebUp) {` block for the web server')
        return
    web_body, _ = block_body(stop, web.end() - 1)
    if web.start() < radio_end:
        problem('C9', 'src/AppManager_Commands.cpp: the `if (!_airWebUp)` block of `air stop` is inside the radio block')
    if not WEB_BEGIN_RE.search(web_body) or not re.search(r'\b_airWebUp\s*=\s*true\s*;', web_body):
        problem('C9', 'src/AppManager_Commands.cpp: `if (!_airWebUp)` must call `_webMgr->begin(` and set `_airWebUp = true;`')


# C10 — `ap` is refused while the sleep cycle is active ---------------------

AP_REFUSAL_RE = re.compile(
    r'#if\s+SIMUT_AIR\s*\n\s*if\s*\(\s*_airActive\s*\)\s*\{\s*'
    r'_cmdMgr->printError\(\s*_cmdMgr->isPt\(\s*\)\s*\?\s*"(?P<pt>(?:[^"\\]|\\.)*)"\s*:\s*"(?P<en>(?:[^"\\]|\\.)*)"\s*\)\s*;\s*'
    r'break\s*;\s*\}\s*#endif\s*startApMode\s*\(\s*\)\s*;')


def ap_refusal_texts():
    """(pt, en): what `ap` answers while the sleep cycle is active, cut out of `case CMD_AP:` of AppManager_Commands.cpp, or None when that
    case does not hold the refusal in its place (under #if SIMUT_AIR, on _airActive, with a break, before startApMode( ))."""
    cmds = blank_comments(read(os.path.join(SRC, 'AppManager_Commands.cpp')))
    i = cmds.find('case CMD_AP:')
    if i < 0:
        return None
    j = cmds.find('case CMD_', i + 10)
    m = AP_REFUSAL_RE.search(cmds[i:j if j > 0 else len(cmds)])
    return (m.group('pt'), m.group('en')) if m else None


def c10_ap_refused_during_the_cycle():
    """A wake has neither the radio nor the web server to keep, and the cycle goes on under the command: the SLEEP phase powers the
    radio down, so the AP that `ap` opened (startApMode( ) never touches _airActive or the web server) died with the wake, announced
    and serving nothing. The maintainer's decision of 2026-11-04: refuse, say what to do (`air stop` first), in both CLI languages,
    and do not stop the cycle on the operator's behalf. The pt text is unaccented ASCII, like every other pt message of the file."""
    texts = ap_refusal_texts()
    if texts is None:
        problem('C10', 'src/AppManager_Commands.cpp: `case CMD_AP:` must refuse under `#if SIMUT_AIR` on `_airActive` with '
                       '`_cmdMgr->printError(_cmdMgr->isPt( ) ? "pt" : "en"); break;` before `startApMode( )`')
        return
    pt, en = texts
    for lang, text in (('pt', pt), ('en', en)):
        if "air stop" not in text:
            problem('C10', f'src/AppManager_Commands.cpp: the {lang} refusal of `ap` does not say what to do: it must name `air stop`')
    if not pt.isascii():
        problem('C10', 'src/AppManager_Commands.cpp: the pt refusal of `ap` has a non-ASCII character (the CLI\'s pt messages are unaccented)')


# C8 — LogManager scratch map documents the Air marker --------------------

def c8_scratch_map():
    src = read(os.path.join(SRC, 'LogManager.cpp'))
    m = re.search(r'SCRATCH REGISTER MAP.*?\*/', src, re.DOTALL)
    if not m:
        problem('C8', 'LogManager.cpp: scratch register map comment not found')
        return
    block = m.group(0)
    uses_scratch0 = 'scratch[0]' in read(os.path.join(SRC, 'air', 'AirConfig.h'))
    if uses_scratch0 and not re.search(r'scratch\[0\][^\n]*(Air|AIR)', block):
        problem('C8', 'src/LogManager.cpp: scratch map says scratch[0..2] are reserved, but the Air '
                      'dormant marker lives in scratch[0] — document it there')


def main():
    for check in (c1_air_markers, c2_emergency_help_guard, c3_packs_have_every_command,
                  c4_cli_help_gate_is_build_aware, c5_declared_methods_defined,
                  c6_env_comment_matches_flag, c7_obsolete_fields_unused, c8_scratch_map,
                  c9_air_stop_starts_the_web, c10_ap_refused_during_the_cycle):
        try:
            check()
        except Exception as exc:  # a broken check must not pass silently
            problem(check.__name__[:2].upper(), f'check crashed: {type(exc).__name__}: {exc}')
    if problems:
        by = {}
        for c, _ in problems:
            by[c] = by.get(c, 0) + 1
        print(f'[air-consistency] FAIL: {len(problems)} finding(s) ' +
              ', '.join(f'{k}={v}' for k, v in sorted(by.items())))
        return 1
    print('[air-consistency] OK: C1–C10 clean')
    return 0


if __name__ == '__main__':
    sys.exit(main())
