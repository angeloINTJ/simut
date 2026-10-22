#!/usr/bin/env python3
"""check_ui_link.py -- the Angulo images must not link the original UI driver (S10b, C6, 2026-10-21).

usage: check_ui_link.py [--nm PATH] ELF [ELF...]    each ELF an image that carries the Angulo (pico_wiretft_angulo, pico2_w_angulo)
       check_ui_link.py --selftest                  the gate's own cases on synthetic `nm -C` text (no toolchain, no ELF: it can run in the gates job)
exit 0: every ELF clean. 1: a finding (a symbol the Angulo image must not hold, or an allowance that matches nothing). 2: a refusal (no ELF, nm failed, not an Angulo image).

Why. After S4 to S10b the Angulo links none of the original's draws and none of the pixel primitives it drew with: --gc-sections drops the units that are still compiled, and C6 stopped compiling eight of them ([ui.original].units in tools/features.toml). Nothing in CI holds
that: the flash budget counts bytes, not names, and the checks the S10b steps ran by hand (nm, a reachability script) lived in scratch space, not in the repository. One original draw reached again by a core path, or a primitive by a hook, would
put the original's pixels on the Angulo's panel with no gate saying so. This is the grep the S10b steps ran by hand on every Angulo image (`nm -C | grep -E 'DisplayManager::(draw[A-Z]\\w*|render|redraw\\w*)\\b|
simutFont|availableThemes'`), plus the three pixel primitives C5b took out (fastFillRect, fastClearScreen, blitCanvas). A core function the pattern matches is an ALLOWANCE below, by exact
name and with its reason; the pattern is never widened to hide it, and an allowance that matches nothing is a finding too: the mark comes down in the change that shrinks.

Measured on 2026-10-21 (S10b, probe P4): on the Angulo images as they were before C4a, with the theme table still linked, it exits 1 naming only availableThemes; on
the C5b and C6 images (Wire and Wi-Fi) it exits 0 with the one allowance matched; on pico_wiretft, the original, it exits 2. CI runs --selftest in the `gates` job and the gate itself on the ELF of
each `_angulo` image in the `firmware` job (.github/workflows/build.yml).

Project: SIMUT
License: MIT
"""
import os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from elf_funcoes import TOOL  # noqa: E402  (one place says where the PlatformIO toolchain is; bytes_original.py does the same)
NM_DEFAULT = str(TOOL / 'arm-none-eabi-nm')

RULES = (
    ("the original's draw", re.compile(r'DisplayManager::(?:draw[A-Z]\w*|render|redraw\w*)\b')),
    ("a pixel primitive of the original", re.compile(r'DisplayManager::(?:fastFillRect|fastClearScreen|blitCanvas)\b')),
    ("the original's font", re.compile(r'simutFont')),
    ("the original's theme table (the Angulo defines kTemas)", re.compile(r'availableThemes')),
)
# Core names the pattern matches that the Angulo links on purpose, exact (demangled with the parameter list), each with its reason.
ALLOWED = {
    'DisplayManager::redrawTopPanel()': 'core (defined inline in DisplayManager_Actions.h): the Angulo\'s uaToquePainel( ) and the core\'s handleTouch( ) call it (objdump of the C5b images), so it stays linked and the pattern is not widened to hide it',
}
ANGULO_MARK = 'ua::Pintor'                   # check_ui_ram.py's test for an image that carries the Angulo

def scan(nm_text):
    """-> (is_angulo, findings [(name, why)], allowed_seen). Parses nm -C lines: <8 hex or blanks> <type> <name>; undefined symbols (U) are not in the image."""
    names = []
    for ln in nm_text.splitlines():
        m = re.match(r'^(?:[0-9a-fA-F]{8}| {8}) (\S) (.*)$', ln)
        if m and m.group(1) not in 'Uu': names.append(m.group(2))
    is_angulo = any(ANGULO_MARK in n for n in names)
    findings, seen = [], set()
    for n in names:
        for why, rx in RULES:
            if rx.search(n):
                if n in ALLOWED: seen.add(n)
                else: findings.append((n, why))
                break
    for a in sorted(set(ALLOWED) - seen):
        findings.append((a, 'a stale allowance: the name is not in the image, so the allowance goes (%s)' % ALLOWED[a]))
    return is_angulo, findings, seen

def check(elf, nm):
    if not os.path.isfile(elf): print('check_ui_link: %s: no such file, a refusal and not a pass' % elf); return 2
    r = subprocess.run([nm, '-C', elf], capture_output=True, text=True, errors='replace')
    if r.returncode != 0: print('check_ui_link: %s: nm failed (exit %d): %s, a refusal and not a pass' % (elf, r.returncode, r.stderr.strip()[:120])); return 2
    is_angulo, findings, seen = scan(r.stdout)
    if not is_angulo: print('check_ui_link: %s: no %s symbol, so this is not an image that carries the Angulo: a refusal and not a pass' % (elf, ANGULO_MARK)); return 2
    print('check_ui_link: %s: %d finding(s); %d allowance(s) matched (%s)' % (elf, len(findings), len(seen), ', '.join(sorted(seen)) or 'none'))
    for n, why in findings: print('  FINDING: %s  <- %s' % (n, why))
    return 1 if findings else 0

def selftest():
    base = ['00000100 T ua::Pintor::fillRect(int)', '00000200 T DisplayManager::uiTouch(short, short)', '00000300 W DisplayManager::redrawTopPanel()', '00000400 T DisplayManager::drawing()',
            '00000500 T ua::Pintor::renderX()', '00000600 T DisplayManager::renderer()', '00000700 T DisplayManager::fastFill()', '         U DisplayManager::drawSettingsMain()']
    n = bad = 0
    def case(label, ok, detail=''):
        nonlocal n, bad
        n += 1
        if not ok: bad += 1
        print('%s %s%s' % ('ok  ' if ok else 'FAIL', label, (' | ' + detail) if detail else ''))
    a, f, s = scan('\n'.join(base))
    case('the clean image (near misses drawing, renderer, fastFill, an undefined draw; the allowance present): 0 findings', a and not f and s == {'DisplayManager::redrawTopPanel()'}, str(f))
    plants = ['DisplayManager::drawSettingsMain()', 'DisplayManager::render()', 'DisplayManager::redrawAlarmFlash()', 'DisplayManager::fastFillRect(short, short, short, short, unsigned short)',
              'DisplayManager::fastClearScreen(unsigned short)', 'DisplayManager::blitCanvas(short, short)', 'simutFontBold', 'availableThemes', 'DisplayManager::redrawTopPanel(int)']
    for p in plants:
        a, f, s = scan('\n'.join(base + ['00000900 T ' + p]))
        case('planting %s gives exactly one finding, that one' % p, len(f) == 1 and f[0][0] == p, str(f))
    a, f, s = scan('\n'.join(x for x in base if 'redrawTopPanel' not in x))
    case('the allowance without its symbol is a finding (stale)', len(f) == 1 and 'stale' in f[0][1], str(f))
    a, f, s = scan('\n'.join(x for x in base if 'ua::Pintor' not in x))
    case('an image without ua::Pintor is not an Angulo image (the caller refuses)', not a)
    g = re.compile(r"DisplayManager::(draw[A-Z]\w*|render|redraw\w*)\b|simutFont|availableThemes")
    txt = '\n'.join(base + ['00000900 T DisplayManager::drawSettingsMain()', '00000a00 T availableThemes', '00000b00 t DisplayManager::redrawAlarmFlash()'])
    a, f, s = scan(txt)
    live = [x for x in txt.splitlines() if x[9:10] != 'U' and not x.startswith(' ')]
    case("the gate and G-NM's grep agree on the same text (matches = findings + allowances seen)", sum(1 for x in live if g.search(x)) == len(f) + len(s), '%d vs %d' % (sum(1 for x in live if g.search(x)), len(f) + len(s)))
    print('RESULT: %d cases, %d failed' % (n, bad)); return 1 if bad else 0

if __name__ == '__main__':
    args = sys.argv[1:]
    if args == ['--selftest']: sys.exit(selftest())
    nm = NM_DEFAULT
    if args[:1] == ['--nm'] and len(args) >= 2: nm, args = args[1], args[2:]
    if not args: print(__doc__.split('\n\n')[0]); sys.exit(2)
    rc = [check(e, nm) for e in args]
    sys.exit(2 if 2 in rc else (1 if 1 in rc else 0))
