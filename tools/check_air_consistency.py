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
      of every language pack (F15/F20).
  C4  tools/check_cli_help.py must not expect CMD_AIR_* unconditionally: the
      list has to depend on SIMUT_AIR (F15).
  C5  every Air method declared in AppManager.h has a definition (F17).
  C6  the [env:pico_w_air] comment block agrees with its -DSIMUT_CLI_FULL flag (F18).
  C7  AirConfig fields marked obsolete in the header must not be written by
      firmware code outside AirConfig.h (F17) — informational.
  C8  LogManager's scratch register map documents the Air marker in
      scratch[0] (F19).

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
    m = re.search(r'#if\s+!SIMUT_CLI_FULL(.*?)\n#else', src, re.DOTALL)
    if not m:
        return '', 0
    block = m.group(1)
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


def pack_help_section(text):
    m = re.search(r'^@HELP\s*$(.*?)(?=^@[A-Z]+\s*$|\Z)', text, re.DOTALL | re.MULTILINE)
    return m.group(1) if m else ''


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
    block, _ = emergency_help_block(src)
    universal = '\n'.join(line for _n, line, guarded
                          in guarded_lines(blank_comments(block)) if not guarded)
    en_cmds = help_commands(universal)
    en_keys = {key_of(c): c for c in en_cmds}
    for pack in sorted(glob.glob(os.path.join(ROOT, 'data', 'lang', '*.lng'))):
        sect = pack_help_section(read(pack))
        if not sect:
            problem('C3', f'{os.path.relpath(pack, ROOT)}: no @HELP section')
            continue
        keys = {key_of(c) for c in help_commands(sect)}
        for k, c in sorted(en_keys.items()):
            if k not in keys:
                problem('C3', f'{os.path.relpath(pack, ROOT)}: @HELP lacks "{c}"')
    if verbose:
        print(f'[air-consistency] C3: EN emergency help lists {len(en_cmds)} commands')


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
    ini = read(os.path.join(ROOT, 'platformio.ini'))
    m = re.search(r'(;[^\n]*\n)+\[env:pico_w_air\](.*?)(?=\n\[env:|\Z)', ini, re.DOTALL)
    if not m:
        problem('C6', 'platformio.ini: [env:pico_w_air] not found')
        return
    comment = m.group(0)[:m.group(0).find('[env:pico_w_air]')]
    body = m.group(2)
    flag = re.findall(r'-DSIMUT_CLI_FULL=(\d)', body)
    said = re.findall(r'SIMUT_CLI_FULL=(\d)', comment)
    if flag and said and said[-1] != flag[-1]:
        problem('C6', f'platformio.ini: pico_w_air comment says SIMUT_CLI_FULL={said[-1]} but the env sets {flag[-1]}')


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
                  c6_env_comment_matches_flag, c7_obsolete_fields_unused, c8_scratch_map):
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
    print('[air-consistency] OK: C1–C8 clean')
    return 0


if __name__ == '__main__':
    sys.exit(main())
