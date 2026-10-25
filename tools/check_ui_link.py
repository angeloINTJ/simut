#!/usr/bin/env python3
"""check_ui_link.py -- the Angulo images must not link the original UI driver: the function table decides, the name patterns check what it does not cover (S10b: C6, 2026-10-21; C6b, C6c, C6d and C6e, 2026-10-22).

usage: check_ui_link.py [--nm PATH] [--readelf PATH] [--tsv FILE] [--original-units A,B] ELF [ELF...]    each ELF an image that carries the Angulo (pico_wiretft_angulo, pico2_w_angulo)
       check_ui_link.py --selftest                  the gate's own cases on synthetic `nm` text and synthetic table rows, a read of the real table, and the command line's own function (main) on a fake nm and a fake readelf, a shell script each, then the script itself run as a process, the way CI runs it (no toolchain, no ELF: it can run in the gates job)
exit 0: every ELF clean. 1: a finding (a symbol the Angulo image must not hold, or an allowance that no longer applies). 2: a refusal (no ELF, nm or readelf missing or failed, not an Angulo image, a table it cannot read).

Why. After S4 to S10b the Angulo links none of the original's draws and none of the pixel primitives it drew with: --gc-sections drops the units that are still compiled, and the builds no longer compile the eleven units that [ui.original].units of
tools/features.toml lists (Themes.cpp since C4a, seven more since C6, Dashboard.cpp since S10c D1, DisplayManager_AlarmUi.cpp since S10c D5, DisplayManager_Settings.cpp since S10c D2). Nothing else in CI holds that: the flash budget counts bytes, not names, and the checks the S10b steps ran by hand (nm, a reachability script) lived in scratch space. One original draw reached again by a
core path, or a primitive by a hook, would put the original's pixels on the Angulo's panel with no gate saying so.

The first version (C6) was a grep of the names `DisplayManager::(draw[A-Z]\\w*|render|redraw\\w*)`, simutFont and availableThemes. Its review (2026-10-21) planted nine draw functions under other names in a real symbol table and the gate passed all nine; of the 188 symbols of
pico_wiretft that the C6 Wire Angulo lacks, the patterns matched 88 and missed 100, 56 of them draws (drawMinMaxTempRow, kbPopupCard, blitPinCards, commitScreenStrip) defined in units the Angulo still compiles (Dashboard, Users, Settings), where no link error covers them.

How it decides now. `nm -S -C` of the ELF, then three things.
1. The function table (docs/analysis/UI_DRIVERS_funcoes.tsv), read with bytes_original.py's own code (key_of, row_of, the unit list, the file symbols of readelf): a symbol that falls on a row of class `original` is a finding unless ALLOWED names it. A row
   of class `mixed` is one too (C6c): a function that holds original lines and core lines, linked into an Angulo image, carries the original's share with it; two are allowed today, each with its reason. A row of class core is not a finding: core is shared code by the table's definition. A header is a unit like
   any other (its rows are read by the name, and the unit says which header). By name for every unit, a unit that [ui.original].units lists and the image does not link included, and that is where this differs from bytes_original.py, which counts bytes: its unit rule leaves out the rows of an
   absent unit, so a planted `uiButton` (UiWidgets.cpp, absent from both Angulo images) would pass. Here such a hit says "another unit's definition of the same name, or the original leaking in without a file symbol", and only ALLOWED can excuse it: the Angulo's
   own Temas.cpp defines the six catalogue functions that Themes.cpp does (C4a), four of them are named with the unit, and the allowance holds only while that unit has no file symbol in the image (in C0's ELF Themes.cpp is linked and the same four names are findings).
2. The name patterns of C6 (RULES), still on every symbol, and one more: a name with a component that begins with draw, render or blit in any case (`draw`, `drawX`, `Draw3dBar`, `draw_bar`, `renderAlarmRow`, `blitTitleBar`), read on the qualified name without its parameters,
   outside the Angulo's own namespace (`ua::`) and the Adafruit GFX classes the Angulo and the original both link. The Angulo names its functions in Portuguese, so an English draw name in its image is the original's, a library's or the core's; the core ones that
   exist are in ALLOWED. A word inside an identifier (`uiDrawX`, `firstDraw`, `withdrawn`, `endRaw`) is not read by the patterns: the table covers those, and the near misses are cases of --selftest.
3. ALLOWED is by exact name (demangled, with the parameter list) and each entry has its reason. An allowance that is not in the image is a finding (the mark comes down in the change that shrinks), and so is one that is in the image and that nothing flags any more.

Known limits (C6c, measured on 2026-10-22 on the C6b images).
1. nm lists what the linker kept: code the compiler inlined into another function has no symbol. The core path that keeps a function (a keeper) is not read: that is reach.py's job, outside the repository.
2. The table is the S0 scanner's classification of the original's tree, kept by the step that touches it, and the gate knows what the table lists. A name that is neither a row nor matched by a pattern is invisible to both checks, and nothing in CI forces a row: tsv_check.py, which holds
   the table to the code (every definition of a unit has a row, the lines are right), is a scratch script of the UI-drivers branch, outside the repository. So a function added to Users.cpp (a unit the Angulo still compiles, where no link error covers it; Dashboard.cpp was one too until S10c D1, and Settings.cpp until S10c D2) with no row and a name no pattern matches
   (`tidyDashSlots`, `montarCartaoUsuario`) passes. So does a function or a table that a header defines: the header rows are the 16 original and 2 core names that the review of C6b found (below; C6d turned three from core to original), not every definition of those headers.
3. Core rows and library code get no rule. A symbol on a row of class core is shared code by the table's definition, and library or compiler code is linked by whoever calls it. The count that shows the size of it, the 188 symbols that pico_wiretft links and the C6b Wire Angulo does not, appended
   to the real Wire Angulo nm text: the C6b gate flagged 140 and passed 48. Of the 48, 18 had no row (code and tables that headers define: panelDHT22, minMax*, SENSOR_PANEL_DISPATCH, PinKb::keyAt, PwdKb::*, CLOCK_COL_*, themeTint, fmtFloat1/2 ...), 2 sat on a mixed row (handleTouchPanelV24
   and its lambda), 11 on core rows (dmaTft*, fillWindowDma, channelLabel ...) and 17 were compiler or library code (Adafruit_GFX, libgcc's veneers, switch tables). Now 158 are flagged (155 at C6c): 16 of the 18 are the original's and have rows (15 rows were added in C6c, 10 of them original; C6d turned PinKb::keyAt, fmtFloat1 and fmtFloat2 from core to original, the review of C6c having found that only the original's draws and
   touch handler call them; menuLabelNoNumber and sensorNthChannel stay core), and the 2 mixed are findings. The 30 that pass are 13 on core rows and the 17 compiler or library names.

Measured on 2026-10-22 (S10b, C6c; the exits again at C6d): on the C6b images (pico_wiretft_angulo and pico2_w_angulo) it exits 0 with the same 22 allowances used on both; on the images as they were before C4a (C0's Wire, C0b's Wi-Fi), with the theme table still linked, it exits 1 and names availableThemes through the table; the review's nine
planted names each exit 1 (the C6 gate exited 0 on all nine); on product images (no ua::Pintor) and on a missing file it exits 2. --selftest runs the command line's own function on a fake nm too, to see exits 0, 1 and 2 whatever the order of the ELFs, and 1 for a stale allowance (the review of C6c planted both: main( ) returning the last ELF's code, and check( ) leaving the stale ones out of the exit code), and runs the script itself as a process on the same fakes (C6e): every case above calls main( ) in this process with a readelf that prints nothing, and the final review of S10b-1 planted three mutants that passed all of them, `sys.exit(main(...))` without the sys.exit( ) (the process exits 0 whatever the findings), `scan(..., files)` as `scan(..., set())` (an image that links Themes.cpp, with its allowance, exits 0) and `rc.count(1) == 1` in main( ) (two images with findings exit 0). CI runs --selftest in the `gates` job and the gate itself on the ELF of each `_angulo` image in the `firmware` job (.github/workflows/build.yml).

Project: SIMUT
License: MIT
"""
import argparse, contextlib, csv, io, os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from elf_funcoes import TOOL  # noqa: E402  (one place says where the PlatformIO toolchain is)
import bytes_original as bo   # noqa: E402  (the function table's reader: rows, units, names; not copied here)
NM_DEFAULT = str(TOOL / 'arm-none-eabi-nm')
READELF_DEFAULT = str(TOOL / 'arm-none-eabi-readelf')
TSV_DEFAULT = os.path.join(HERE, '..', 'docs', 'analysis', 'UI_DRIVERS_funcoes.tsv')
MANIFEST = os.path.join(HERE, 'features.toml')

# The patterns of C6, on the whole demangled name, then the free names of C6b on the qualified name without its parameters (FREE, minus OWN).
RULES = (
    ("the original's draw", re.compile(r'DisplayManager::(?:draw[A-Z]\w*|render|redraw\w*)\b')),
    ("a pixel primitive of the original", re.compile(r'DisplayManager::(?:fastFillRect|fastClearScreen|blitCanvas)\b')),
    ("the original's font", re.compile(r'simutFont')),
    ("the original's theme table (the Angulo defines kTemas)", re.compile(r'availableThemes')),
)
FREE = re.compile(r'(?i)(?<![A-Za-z0-9])(?:draw|render|blit)')
FREE_WHY = "a draw, render or blit name outside the Angulo's own namespace: the Angulo names its functions in Portuguese, so this is the original's, a library's or the core's (name it in ALLOWED with its reason if it is not the original's)"
OWN = re.compile(r'(?:^|\s)(?:ua|Adafruit_GFX|Adafruit_SPITFT|GFXcanvas16)::')   # the Angulo's namespace and the libraries both drivers link
ANGULO_MARK = 'ua::Pintor'                   # check_ui_ram.py's test for an image that carries the Angulo

_THEME = "the Angulo defines it in src/ui/angulo/Temas.cpp; the original's row is Themes.cpp's, a unit this image does not compile (C4a's collision, told apart by unit)"
_TOUCH = "mixed row, DisplayManager_Touch.cpp: the original image's body holds the original's mode chain (ui/original/UiTouchChain.h, which ui/UiDriverTouch.h includes inside the function); the Angulo images include ui/angulo/UiTouchChain.h at that place, so what is linked is the core's gates and the Angulo's chain; loopCore1( ) calls it, so it stays"
_CORE1 = "mixed row, DisplayManager.cpp: Core 1's main loop (core1Entry( ) runs it); 10 of the 11 images link it, all but pico_w_air (nm of the eleven images, 2026-10-22), and both Angulo images are among the 10; the table's note counts the first paint, the theme redraw and the mode dispatch as the original's share, and in this function they are now calls to the driver's uiBegin( ), uiThemeChanged( ) and uiPass( ); the row's ranges are 192e1ad's"
# Exact name (demangled, with the parameter list) -> (reason, unit). A unit says the allowance holds only while that unit has no file symbol in the image (the name is then another unit's).
ALLOWED = {
    'getThemeCount()': (_THEME, 'Themes.cpp'),
    'getThemePalette(int)': (_THEME, 'Themes.cpp'),
    'scanCustomThemes()': (_THEME, 'Themes.cpp'),
    'loadTheme(int)': (_THEME, 'Themes.cpp'),
    'DisplayManager::redrawTopPanel()': ("core (defined inline in DisplayManager_Actions.h, class core in the table): the Angulo's uaToquePainel( ) and the core's handleTouch( ) call it (objdump of the C5b images), so it stays linked and the pattern is not widened to hide it", None),
    'AppManager::renderGraphOptimized(int, int, bool, int, long long)': ("core (AppManager.h): the graph request, no function of a UI driver's unit; only its name is a render", None),
    'AppManager::renderGraphOptimized(int, int, bool, int, long long)::rangeDuration': ("a constant local to AppManager::renderGraphOptimized( ), allowed with it", None),
    'CommandManager::renderSystemInfo(SystemConfig const&)': ("core (CommandManager.h): the console's system report, no function of a UI driver's unit", None),
    'WebManager::RenderGuard::RenderGuard(DisplayManager*)': ("core (WebManager.h): pauses the display's rendering for the scope of a web handler (pauseRendering( true )); draws nothing", None),
    'WebManager::RenderGuard::~RenderGuard()': ("core (WebManager.h): ends that pause (pauseRendering( false )); draws nothing", None),
    'DisplayManager::handleTouch()': (_TOUCH, None),
    'DisplayManager::loopCore1()': (_CORE1, None),
}

class Refusal(Exception):
    pass

NM_LINE = re.compile(r'^[0-9a-fA-F]{8} (?:[0-9a-fA-F]{8} )?(\S) (.*)$')   # <address> [<size>] <type> <name>; an undefined symbol has blanks where the address is, and `-S` leaves the size out when it is 0

def parse_nm(nm_text):
    """-> [(type, name)] of the symbols the image defines (an undefined one, weak or not, is not in the image)."""
    return [(m.group(1), m.group(2)) for m in map(NM_LINE.match, nm_text.splitlines()) if m]

def load_table(tsv, units_arg=None):
    """-> (rows, listed). Refuses (Refusal) a table that cannot say what is original: a table with no original row would pass anything."""
    try:
        rows = bo.load_tsv(tsv)
    except (OSError, UnicodeDecodeError, csv.Error) as e:
        raise Refusal('the function table %s cannot be read (%s)' % (tsv, e))
    if not rows or not {'unit', 'symbol', 'class'} <= set(rows[0]):
        raise Refusal('the function table %s has no rows or lacks the columns unit, symbol and class' % tsv)
    if not any(r['class'] == 'original' for r in rows):
        raise Refusal('the function table %s has no row of class original: it would pass anything' % tsv)
    try:
        listed = {u for u in units_arg.split(',') if u} if units_arg is not None else bo.manifest_original_units(MANIFEST)
    except OSError as e:
        raise Refusal('%s cannot be read (%s)' % (MANIFEST, e))
    return rows, listed

def flag(name, table, cls_of, gone):
    """Why this symbol must not be in an Angulo image, or None."""
    k = bo.key_of(name)
    row = bo.row_of(k, table)
    if row is not None and cls_of[row] == 'original':
        unit = table[row][0]['unit']
        if unit in gone:
            return "an original row of %s in the function table, a unit this image does not link: another unit's definition of the same name, or the original leaking in without a file symbol" % unit
        return 'an original row of %s in the function table' % unit
    if row is not None and cls_of[row] == 'mixed':           # C6c: a function with original and core lines in it, or rows that classify one name two ways
        unit = table[row][0]['unit']
        return 'a mixed row of %s in the function table (a function that holds original lines and core lines; rows that disagree on a name count as mixed)%s' % (unit, ", a unit this image does not link: another unit's definition of the same name, or the original leaking in without a file symbol" if unit in gone else '')
    for why, rx in RULES:
        if rx.search(name): return why
    if FREE.search(k) and not OWN.search(k): return FREE_WHY
    return None

def scan(nm_text, rows, listed, files, allowed=None):
    """-> (is_angulo, findings [(name, why)], allowances used [name]). `files` are the file symbols of the ELF (readelf), `listed` the units the Angulo build does not compile."""
    allowed = ALLOWED if allowed is None else allowed
    names = list(dict.fromkeys(n for t, n in parse_nm(nm_text)))     # one per name (a constructor's two aliases are one), in order
    table, cls_of = bo.build_table(rows)                             # by name: the unit rule is applied below, as an excuse that ALLOWED may use, not as a way to drop rows
    gone = bo.absent_units(listed, files, rows)
    findings, used, unflagged = [], set(), set()
    for n in names:
        why = flag(n, table, cls_of, gone)
        a = allowed.get(n)
        if why is None:
            if a: unflagged.add(n)
        elif a and (a[1] is None or a[1] in gone): used.add(n)
        else: findings.append((n, why + (('; its allowance holds only while %s has no file symbol in the image' % a[1]) if a else '')))
    present = set(names)
    for a in sorted(set(allowed) - used):
        if a not in present: findings.append((a, 'a stale allowance: the name is not in the image, so the allowance goes (%s)' % allowed[a][0]))
        elif a in unflagged: findings.append((a, 'a stale allowance: the name is in the image and nothing flags it any more, so the allowance goes (%s)' % allowed[a][0]))
    return any(ANGULO_MARK in n for n in names), findings, used

def check(elf, nm, readelf, rows, listed):
    if not os.path.isfile(elf): print('check_ui_link: %s: no such file, a refusal and not a pass' % elf); return 2
    try:
        r = subprocess.run([nm, '-S', '-C', elf], capture_output=True, text=True, errors='replace')
    except OSError as e: print('check_ui_link: %s: cannot run %s (%s), a refusal and not a pass' % (elf, nm, e)); return 2
    if r.returncode != 0: print('check_ui_link: %s: nm failed (exit %d): %s, a refusal and not a pass' % (elf, r.returncode, r.stderr.strip()[:120])); return 2
    if not any(ANGULO_MARK in n for t, n in parse_nm(r.stdout)): print('check_ui_link: %s: no %s symbol, so this is not an image that carries the Angulo: a refusal and not a pass' % (elf, ANGULO_MARK)); return 2
    try: files = bo.linked_files(readelf, elf)
    except (OSError, subprocess.CalledProcessError) as e: print('check_ui_link: %s: readelf failed (%s), a refusal and not a pass' % (elf, e)); return 2
    is_angulo, findings, used = scan(r.stdout, rows, listed, files)
    print('check_ui_link: %s: %d finding(s); %d of %d allowance(s) used' % (elf, len(findings), len(used), len(ALLOWED)))
    for n, why in findings: print('  FINDING: %s  <- %s' % (n, why))
    return 1 if findings else 0

def selftest():
    n = bad = 0
    def case(label, ok, detail=''):
        nonlocal n, bad
        n += 1
        if not ok: bad += 1
        print('%s %s%s' % ('ok  ' if ok else 'FAIL', label, (' | ' + detail) if detail else ''))
    def row(unit, symbol, cls): return {'unit': unit, 'symbol': symbol, 'class': cls}
    ROWS = [row('Dash.cpp', 'DisplayManager::keepMe', 'original'), row('Dash.cpp', 'DisplayManager::coreOne', 'core'), row('Dash.cpp', 'DisplayManager::halfHalf', 'mixed'), row('Dash.cpp', 'kbPopupCard', 'original'),
            row('Dash.cpp', 'DisplayManager::blitTitleBar', 'original'), row('Wid.cpp', 'uiWidget', 'original'), row('Wid.cpp', 'uiShared', 'original'), row('Wid.cpp', 'uiButton', 'original'), row('Fon.cpp', 'FreeSansBold9pt8b', 'original'),
            row('Wid.cpp', 'uiHalf', 'mixed'), row('Dash.cpp', 'twiceNamed', 'core'), row('Wid.cpp', 'twiceNamed', 'original'), row('Kb.h', 'PwdKb::popupHit', 'original'), row('Kb.h', 'PwdKb::GROUPS', 'original'), row('Kb.h', 'KbModel::cellAt', 'core')]
    LISTED = {'Wid.cpp'}
    ALLOW = {'DisplayManager::redrawTopPanel()': ('core, pattern side', None), 'DisplayManager::keepMe(int)': ('keeper, table side', None), 'uiShared(int)': ('collision with the Angulo\'s own', 'Wid.cpp'), 'DisplayManager::halfHalf(int)': ('mixed row, table side', None)}
    BASE = ['00000100 00000010 T ua::Pintor::fillRect(int)', '00000200 00000020 T DisplayManager::uiTouch(short, short)', '00000300 00000008 W DisplayManager::redrawTopPanel()', '00000400 00000008 T DisplayManager::keepMe(int)',
            '00000500 00000008 T DisplayManager::coreOne(int)', '00000600 T DisplayManager::halfHalf(int)', '00000700 00000008 T uiShared(int)',
            '00000800 00000008 T Adafruit_GFX::drawLine(short, short, short, short, unsigned short)', '00000900 00000008 T ua::Faixa::drawPixel(short, short, unsigned short)',
            '00000a00 00000008 t non-virtual thunk to GFXcanvas16::drawPixel(short, short, unsigned short)', '00000b00 00000008 T withdrawn(int)', '00000c00 00000008 T prerenderCache()', '00000d00 00000008 T WebDictValue::endRaw()',
            '00000e00 00000008 T DisplayManager::pauseRendering(bool)', '00000f00 00000008 t (anonymous namespace)::g_firstDraw', '         U DisplayManager::drawSettingsMain()', '         w DisplayManager::blitTitleBar(char const*)']
    def run(lines, rows=ROWS, files=(), allowed=ALLOW): return scan('\n'.join(lines), rows, LISTED, set(files), allowed)
    a, f, u = run(BASE)
    case('the clean image (the near misses withdrawn, prerenderCache, endRaw, pauseRendering and g_firstDraw; the library and ua:: draws; an undefined and a weak undefined draw; a core row and a mixed row that ALLOW names; the four allowances present): 0 findings, 4 used', a and not f and len(u) == 4, str(f))
    plants = ['DisplayManager::drawSettingsMain()', 'DisplayManager::render()', 'DisplayManager::redrawAlarmFlash()', 'DisplayManager::fastFillRect(short, short, short, short, unsigned short)', 'DisplayManager::fastClearScreen(unsigned short)',
              'DisplayManager::blitCanvas(short, short)', 'simutFontBold', 'availableThemes', 'DisplayManager::redrawTopPanel(int)']
    for p in plants:
        a, f, u = run(BASE + ['00001000 T ' + p])
        case('C6 pattern: planting %s gives exactly one finding, that one' % p, len(f) == 1 and f[0][0] == p, str(f))
    review = ['ui::drawThermometerLarge(GFXcanvas16*, short, short)', 'drawDropMini(GFXcanvas16*, short, short)', 'DisplayManager::renderAlarmRow(int, short&)', 'DisplayManager::blitTitleBar(char const*, int, int)',
              'uiButton(Adafruit_GFX*, short, short, short, short, char const*, int)', 'drawKnob(int) [clone .constprop.0]', 'DisplayManager::draw()', 'DisplayManager::Draw3dBar()', 'DisplayManager::draw_bar()']
    for p in review:
        a, f, u = run(BASE + ['00001000 t ' + p])
        case("C6 review's plant %s gives exactly one finding, that one" % p, len(f) == 1 and f[0][0] == p, str(f))
    a, f, u = run(BASE + ['00001000 T kbPopupCard(int)'])
    case('the table alone: kbPopupCard matches no pattern and is an original row of a unit the image compiles: one finding that names the table and Dash.cpp', len(f) == 1 and 'function table' in f[0][1] and 'Dash.cpp' in f[0][1] and not any(rx.search('kbPopupCard(int)') for w, rx in RULES) and not FREE.search('kbPopupCard'), str(f))
    a, f, u = run(BASE + ['00001000 T FreeSansBold9pt8bGlyphs'])
    case("bytes_original's font rule is the table's: FreeSansBold9pt8bGlyphs is one finding", len(f) == 1 and f[0][0] == 'FreeSansBold9pt8bGlyphs', str(f))
    a, f, u = run(BASE + ['00001000 T uiWidget(int)'])
    a2, f2, u2 = run(BASE + ['00001000 T uiWidget(int)'], files={'Wid.cpp'})
    fw, fw2 = [x for x in f if x[0] == 'uiWidget(int)'], [x for x in f2 if x[0] == 'uiWidget(int)']
    case('an original row of a listed unit the image does not link is a finding (bytes_original.py would drop the row), and so is the same name when the unit is linked, with another reason', len(f) == 1 and len(fw) == 1 and 'does not link' in fw[0][1] and len(fw2) == 1 and 'does not link' not in fw2[0][1], str((f, f2)))
    a, f, u = run(BASE, files={'Wid.cpp'})
    case('an allowance with a unit holds only while the unit is absent: with Wid.cpp linked, uiShared( int ) is one finding and is not also reported stale', len(f) == 1 and f[0][0] == 'uiShared(int)' and 'stale' not in f[0][1], str(f))
    for gone in ('uiShared(int)', 'DisplayManager::keepMe(int)', 'DisplayManager::redrawTopPanel()', 'DisplayManager::halfHalf(int)'):
        a, f, u = run([x for x in BASE if gone not in x])
        case('the allowance for %s without its symbol is a finding (stale)' % gone, len(f) == 1 and f[0][0] == gone and 'stale' in f[0][1], str(f))
    a, f, u = run(BASE, rows=[r if r['symbol'] != 'DisplayManager::keepMe' else row('Dash.cpp', 'DisplayManager::keepMe', 'core') for r in ROWS])
    case('the allowance whose symbol the table no longer calls original (the row went core) is stale too: nothing flags it', len(f) == 1 and f[0][0] == 'DisplayManager::keepMe(int)' and 'nothing flags it' in f[0][1], str(f))
    a, f, u = run(BASE, allowed={k: v for k, v in ALLOW.items() if k != 'DisplayManager::halfHalf(int)'})
    case('the mixed rule (C6c): a symbol on a mixed row is a finding unless ALLOWED names it (halfHalf( int ) without its allowance: one finding, that names a mixed row and Dash.cpp)', len(f) == 1 and f[0][0] == 'DisplayManager::halfHalf(int)' and 'mixed row of Dash.cpp' in f[0][1], str(f))
    a, f, u = run(BASE, rows=[r if r['symbol'] != 'DisplayManager::halfHalf' else row('Dash.cpp', 'DisplayManager::halfHalf', 'core') for r in ROWS])
    case('the allowance whose row went from mixed to core is stale: nothing flags it', len(f) == 1 and f[0][0] == 'DisplayManager::halfHalf(int)' and 'nothing flags it' in f[0][1], str(f))
    a, f, u = run(BASE + ['00001000 T uiHalf(int)'])
    case('a mixed row of a unit the image does not link is a finding, told apart like an original row of one', len(f) == 1 and f[0][0] == 'uiHalf(int)' and 'does not link' in f[0][1], str(f))
    a, f, u = run(BASE + ['00001000 T twiceNamed(int)'])
    case('rows that disagree on a name (a core row and an original row) make it mixed: one finding', len(f) == 1 and f[0][0] == 'twiceNamed(int)' and 'mixed row' in f[0][1], str(f))
    for plant in ('PwdKb::popupHit(signed char, short, short)', 'PwdKb::GROUPS'):
        a, f, u = run(BASE + ['00001000 r ' + plant])
        case('a header is a unit (C6c): planting %s, which no pattern matches, is exactly one finding that names Kb.h' % plant, len(f) == 1 and f[0][0] == plant and 'Kb.h' in f[0][1] and not any(rx.search(plant) for w, rx in RULES) and not FREE.search(bo.key_of(plant)), str(f))
    a, f, u = run(BASE + ['00001000 W KbModel::cellAt(KbModel::Grid const&, short, short)'])
    case('a core row of a header is no finding (core is shared code): KbModel::cellAt( ) planted, 0 findings', not f, str(f))
    a, f, u = run([x for x in BASE if 'ua::Pintor' not in x])
    case('an image without ua::Pintor is not an Angulo image (the caller refuses)', not a)
    txt = '\n'.join(BASE + ['00001000 T DisplayManager::drawSettingsMain()', '00001100 T availableThemes', '00001200 t DisplayManager::redrawAlarmFlash()'])
    a, f, u = scan(txt, ROWS, LISTED, set(), ALLOW)
    g = re.compile(r"DisplayManager::(draw[A-Z]\w*|render|redraw\w*)\b|simutFont|availableThemes")
    live = [x for x in parse_nm(txt) if g.search(x[1])]
    case("the gate and the S10b steps' hand-run grep agree on C6's text: every line the grep matches is a finding or an allowance used", len(live) == len([x for x in f if g.search(x[0])]) + len([x for x in u if g.search(x)]), '%d vs %d' % (len(live), len(f) + len(u)))
    case('parse_nm reads the lines with a size, without one, and the plain `nm -C` form, and skips an undefined symbol', parse_nm('00000100 00000010 T a()\n00000200 t b()\n00000300 W c\n         U d\n         w e') == [('T', 'a()'), ('t', 'b()'), ('W', 'c')])
    # The real table and the real allowances: every allowance must be flagged by the real table or the patterns, the nine review plants must be findings, and the refusals.
    try:
        rows, listed = load_table(TSV_DEFAULT)
        real = ['00000100 T ua::Pintor::fillRect(int)'] + ['00001000 T ' + k for k in ALLOWED]
        a, f, u = scan('\n'.join(real), rows, listed, set())
        case('the real table: the text of every allowance alone (Themes.cpp absent) gives 0 findings and all %d used: each is flagged by a row or a pattern today' % len(ALLOWED), a and not f and len(u) == len(ALLOWED), str(f[:3]))
        a, f, u = scan('\n'.join(real), rows, listed, {'Themes.cpp'})
        case('the real table: with Themes.cpp linked, the four catalogue names are findings (the unit rule)', sorted(x[0] for x in f) == sorted(k for k, v in ALLOWED.items() if v[1]), str([x[0] for x in f]))
        for p in review:
            a, f, u = scan('\n'.join(real + ['00002000 t ' + p]), rows, listed, set())
            case("the real table: the review's plant %s is exactly one finding" % p, len(f) == 1 and f[0][0] == p, str(f))
        case('the real table: the manifest lists the eleven units the Angulo does not compile, each with a row', len(listed) == 11 and all(any(r['unit'] == x for r in rows) for x in listed), str(sorted(listed)))
        case('the real table: no unit of [ui.original].units is a header (a header has no file symbol, so the unit rule would drop its rows from every image)', all(not u.endswith(('.h', '.hpp')) for u in listed), str(sorted(listed)))
        H_ORIG = ['CLOCK_COL_X', 'CLOCK_COL_W', 'PwdKb::GROUPS', 'PwdKb::DIGITS', 'PwdKb::SYMBOLS', 'PwdKb::popupHit(signed char, short, short)', 'themeTint(unsigned short)', 'panelDHT22(GFXcanvas16*, float)', 'panelBMP280(int)', 'minMaxDS18B20(int)', 'minMaxDHT22(int)', 'minMaxBME280(int)', 'SENSOR_PANEL_DISPATCH',
                  'PinKb::keyAt(PinKb::Grid const&, short, short)', 'fmtFloat1(char*, unsigned int, float) [clone .isra.0]', 'fmtFloat2(char*, unsigned int, float) [clone .isra.0]']
        miss = []
        for p in H_ORIG:
            a, f, u = scan('\n'.join(real + ['00002000 r ' + p]), rows, listed, set())
            if not (len(f) == 1 and f[0][0] == p and '.h in the function table' in f[0][1]): miss.append((p, f))
        case('the real table (C6c, C6d): the %d original symbols of headers that C6b passed (pico_wiretft links them, neither Angulo image does; C6d added keyAt, fmtFloat1 and fmtFloat2) are each exactly one finding that names a header' % len(H_ORIG), not miss, str(miss[:2]))
        miss, n_h = [], 0
        for r in rows:
            if r['class'] == 'original' and r['unit'].endswith('.h') and ' [' not in r['symbol']:
                for nm in r['symbol'].split(', '):
                    n_h += 1
                    a, f, u = scan('\n'.join(real + ['00002000 r ' + nm + '(int)']), rows, listed, set())
                    if not (len(f) == 1 and r['unit'] in f[0][1]): miss.append((nm, f))
        case('the real table: every name of every original row of a header unit, planted, is one finding that names that header (%d names)' % n_h, not miss and n_h >= len(H_ORIG), str(miss[:2]))
        H_MIXED = ['DisplayManager::handleTouchPanelV24(short, short)', 'DisplayManager::handleTouchPanelV24(short, short)::{lambda(int)#1}::operator()(int) const']
        miss = [(p, f) for p in H_MIXED for a, f, u in [scan('\n'.join(real + ['00002000 t ' + p]), rows, listed, set())] if not (len(f) == 1 and f[0][0] == p and 'mixed row of DisplayManager_Users.cpp' in f[0][1])]
        case('the real table: handleTouchPanelV24( ) and its lambda (a mixed row; C6b passed both) are each exactly one finding', not miss, str(miss[:2]))
        H_PASS = ['menuLabelNoNumber(char const*)', 'sensorNthChannel(SensorType, unsigned char)', 'DisplayManager::channelLabel(unsigned char)',
                  'dmaTftOpen(TftWithOffset*, short, short, short, short)', 'fillWindowDma(TftWithOffset*, unsigned short, short, short, short, short) [clone .constprop.0]', 'Adafruit_GFX::drawCircle(short, short, short, unsigned short)', 'Adafruit_GFX::charBounds(unsigned char, short*, short*, short*, short*, short*, short*)', 'CSWTCH.349', '__gnu_thumb1_case_si']
        miss = [(p, f) for p in H_PASS for a, f, u in [scan('\n'.join(real + ['00002000 t ' + p]), rows, listed, set())] if f]
        case('the real table: core rows, library and compiler code get no rule (%d of the 48 that C6b passed and that still pass after C6d, planted: 0 findings)' % len(H_PASS), not miss, str(miss[:2]))
        MIX = ['DisplayManager::handleTouch()', 'DisplayManager::loopCore1()']
        tb, cl = bo.build_table(rows)
        case('the real table: the two mixed-row functions that both Angulo images link are in ALLOWED, and each sits on a mixed row', all(k in ALLOWED and cl.get(bo.row_of(bo.key_of(k), tb)) == 'mixed' for k in MIX), '')
    except Refusal as e:
        case('the real table can be read', False, str(e))
    with tempfile.TemporaryDirectory() as d:
        for label, text in (('an empty table', ''), ('a table without the class column', 'unit\tsymbol\nA.cpp\tx\n'), ('a table with no original row', 'unit\tsymbol\tclass\nA.cpp\tx\tcore\n')):
            p = os.path.join(d, 't.tsv'); open(p, 'w').write(text)
            try: load_table(p); ok = False
            except Refusal: ok = True
            case('a refusal: %s' % label, ok)
        try: load_table(os.path.join(d, 'absent.tsv')); ok = False
        except Refusal: ok = True
        case('a refusal: a table that is not there', ok)
    rows1 = [row('A.cpp', 'x', 'original')]
    case('a refusal (exit 2): an ELF that is not there', check('/nonexistent/x.elf', NM_DEFAULT, READELF_DEFAULT, rows1, set()) == 2)
    case('a refusal (exit 2): an nm that is not there, not a traceback (this file stands for the ELF)', check(os.path.abspath(__file__), '/nonexistent/arm-none-eabi-nm', READELF_DEFAULT, rows1, set()) == 2)
    with tempfile.TemporaryDirectory() as d:        # the command line's own function, on a fake nm and a fake readelf (a shell script each): the exits 0, 1 and 2 of main( ), which nothing above calls
        def script(name, body):
            p = os.path.join(d, name)
            with open(p, 'w') as fh: fh.write('#!/bin/sh\n' + body)
            os.chmod(p, 0o755); return p
        def text(name, lines):
            p = os.path.join(d, name)
            with open(p, 'w') as fh: fh.write('\n'.join(lines) + '\n')
            return p
        PINT = '00000100 00000010 T ua::Pintor::fillRect(int)'
        t_clean = text('clean.txt', [PINT] + ['00001000 00000010 T ' + k for k in ALLOWED])
        t_one = text('one.txt', [PINT] + ['00001000 00000010 T ' + k for k in ALLOWED] + ['00002000 00000010 T DisplayManager::drawSettingsMain()'])
        t_prod = text('prod.txt', ['00001000 00000010 T DisplayManager::drawSettingsMain()'])
        t_stale = text('stale.txt', [PINT] + ['00001000 00000010 T ' + k for k in ALLOWED if k != 'DisplayManager::loopCore1()'])   # one allowed name missing from the image
        t_btn = text('btn.txt', [PINT] + ['00001000 00000010 T ' + k for k in ALLOWED] + ['00002000 00000010 T uiButton(Adafruit_GFX*, short, short, short, short, char const*, int)'])   # an original row of UiWidgets.cpp, a unit the manifest lists
        elfs = {}
        for k in 'abcdef':
            elfs[k] = os.path.join(d, k + '.elf'); open(elfs[k], 'w').close()
        fake_nm = script('nm', 'case "$3" in\n  */a.elf) cat %s;;\n  */b.elf) cat %s;;\n  */c.elf) cat %s;;\n  */d.elf) cat %s; echo boom >&2; exit 3;;\n  */e.elf) cat %s;;\n  */f.elf) cat %s;;\n  *) echo boom >&2; exit 3;;\nesac\n' % (t_clean, t_one, t_prod, t_clean, t_stale, t_btn))
        fake_re, bad_re = script('readelf', 'exit 0\n'), script('readelf_fails', 'echo boom >&2\nexit 1\n')
        def go(*args):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf): rc = main(list(args))
            return rc, buf.getvalue()
        tool = ['--nm', fake_nm, '--readelf', fake_re]
        rc, out = go(*tool, elfs['a']); case('main path: an image with the Angulo marker and every allowed name exits 0', rc == 0, '%d %s' % (rc, out[:100]))
        rc, out = go(*tool, elfs['b']); case('main path: the same image with one planted draw exits 1 and prints it as a FINDING', rc == 1 and 'FINDING: DisplayManager::drawSettingsMain()' in out, '%d %s' % (rc, out[:160]))
        rc, out = go(*tool, elfs['c']); case('main path: nm text without ua::Pintor (a product image) exits 2, the refusal, and says why', rc == 2 and 'ua::Pintor' in out and 'FINDING' not in out, '%d %s' % (rc, out[:160]))
        rc, out = go(*tool, elfs['d']); case('main path: an nm that prints a clean image and then fails (exit 3) is a refusal, exit 2', rc == 2, '%d %s' % (rc, out[:160]))
        rc, out = go('--nm', fake_nm, '--readelf', bad_re, elfs['a']); case('main path: a readelf that fails exits 2', rc == 2, '%d %s' % (rc, out[:160]))
        rc, out = go(*tool, '--tsv', os.path.join(d, 'absent.tsv'), elfs['a']); case('main path: a table it cannot read exits 2', rc == 2, '%d %s' % (rc, out[:160]))
        rc, out = go(*tool, elfs['a'], elfs['b']); case('main path: a clean image and one with a finding exit 1', rc == 1, str(rc))
        rc, out = go(*tool, elfs['a'], elfs['c']); case('main path: a clean image and a refusal exit 2', rc == 2, str(rc))
        rc, out = go(*tool, elfs['b'], elfs['c']); case('main path: a finding and a refusal exit 2 (the refusal wins)', rc == 2, str(rc))
        # C6d: the order of the ELFs decides nothing. Every case above puts the worse image last, so "main( ) returns the last ELF's code" passed them (the review of C6c); CI passes one ELF, but the usage allows several.
        rc, out = go(*tool, elfs['b'], elfs['a']); case('main path: a finding and then a clean image exit 1', rc == 1, str(rc))
        rc, out = go(*tool, elfs['c'], elfs['a']); case('main path: a refusal and then a clean image exit 2', rc == 2, str(rc))
        rc, out = go(*tool, elfs['c'], elfs['b']); case('main path: a refusal and then a finding exit 2 (the refusal wins in either order)', rc == 2, str(rc))
        rc, out = go(*tool, elfs['a'], elfs['b'], elfs['a']); case('main path: a finding between two clean images exits 1', rc == 1, str(rc))
        # C6d: the stale allowances reach the exit code through check( ) (the review of C6c: the stale cases stopped at scan( ), so "check( ) leaves them out" passed)
        rc, out = go(*tool, elfs['e']); case('main path: an image with one allowed name missing exits 1 and prints it as the one stale finding', rc == 1 and 'FINDING: DisplayManager::loopCore1()' in out and 'the name is not in the image' in out and '1 finding(s)' in out, '%d %s' % (rc, out[:200]))
        with open(TSV_DEFAULT, encoding='utf-8', newline='') as fh: real_lines = fh.read().split('\n')
        cut = [x.split('\t') for x in real_lines]
        hit = [f for f in cut if len(f) > 4 and f[3] == 'DisplayManager::handleTouch' and f[4] == 'mixed']
        for f in hit: f[4] = 'core'
        tsv_core = os.path.join(d, 'touch_core.tsv')
        with open(tsv_core, 'w', encoding='utf-8', newline='') as fh: fh.write('\n'.join('\t'.join(f) for f in cut))
        rc, out = go(*tool, '--tsv', tsv_core, elfs['a']); case('main path: a table in which handleTouch( ) is core leaves its allowance with nothing to excuse: exit 1, one stale finding that says nothing flags it', len(hit) == 1 and rc == 1 and 'FINDING: DisplayManager::handleTouch()' in out and 'nothing flags it' in out and '1 finding(s)' in out, '%d %s' % (rc, out[:200]))
        err = io.StringIO()
        try:
            with contextlib.redirect_stderr(err): main([])
            code = 'returned'
        except SystemExit as e: code = e.code
        case('main path: no ELF (and no --selftest) is a usage error, exit 2, and not a pass', code == 2, str(code))
        rc, out = go(*tool, elfs['f']); rc2, out2 = go(*tool, '--original-units', '', elfs['f'])
        case("main path: --original-units replaces the manifest's list: a planted uiButton (an original row of UiWidgets.cpp, a unit the manifest lists) is 'a unit this image does not link' by default and a plain original row with an empty list, exit 1 both", rc == 1 and rc2 == 1 and 'does not link' in out and 'does not link' not in out2 and 'an original row of UiWidgets.cpp' in out2, '%d %d %s | %s' % (rc, rc2, out[-160:], out2[-160:]))
        # C6e: the command line itself. The cases above call main( ) in this process, with a readelf that prints nothing, so three changes passed all 79 of them (the final review of S10b-1): `sys.exit(main(...))` as `main(...)` (the process exits 0 whatever the findings), `scan(..., files)` as `scan(..., set())` (readelf's answer is ignored: an image that links Themes.cpp, with its allowance, exits 0) and `rc.count(1) == 1` in main( ) (two images with findings exit 0).
        # CI runs `python3 tools/check_ui_link.py ELF` and reads the exit status, so the next four cases run this file as a process, on the same fake nm and, for the third, a fake readelf that prints the FILE lines of a real `readelf -sW`; the two after them are the third change, which a call to main( ) can show.
        def cli(*args):
            r = subprocess.run([sys.executable, os.path.abspath(__file__)] + list(args), capture_output=True, text=True, errors='replace')
            return r.returncode, r.stdout + r.stderr
        t_re = text('readelf_themes.txt', ["Symbol table '.symtab' contains 3 entries:", '   Num:    Value  Size Type    Bind   Vis      Ndx Name', '     0: 00000000     0 NOTYPE  LOCAL  DEFAULT  UND ', '     1: 00000000     0 FILE    LOCAL  DEFAULT  ABS Dashboard.cpp', '     2: 00000000     0 FILE    LOCAL  DEFAULT  ABS Themes.cpp'])
        fake_re_themes = script('readelf_themes', 'cat %s\n' % t_re)
        themes = [k for k, v in ALLOWED.items() if v[1] == 'Themes.cpp']
        rc, out = cli(*tool, elfs['a']); case('command line: run as a process, an image with the Angulo marker and every allowed name exits 0', rc == 0 and '0 finding(s); %d of %d allowance(s) used' % (len(ALLOWED), len(ALLOWED)) in out, '%d %s' % (rc, out[:160]))
        rc, out = cli(*tool, elfs['b']); case('command line: run as a process, the same image with one planted draw exits 1 and prints it as a FINDING', rc == 1 and 'FINDING: DisplayManager::drawSettingsMain()' in out, '%d %s' % (rc, out[:160]))
        rc, out = cli('--nm', fake_nm, '--readelf', fake_re_themes, elfs['a']); case("command line: run as a process, a readelf that names Themes.cpp ends the four catalogue allowances (the image links the original's unit): exit 1, exactly those four as findings", len(themes) == 4 and rc == 1 and all('FINDING: %s  <- an original row of Themes.cpp in the function table; its allowance holds only while Themes.cpp has no file symbol in the image' % k in out for k in themes) and '%d finding(s); %d of %d allowance(s) used' % (len(themes), len(ALLOWED) - len(themes), len(ALLOWED)) in out, '%d %s' % (rc, out[:200]))
        rc, out = cli(*tool, elfs['c']); case('command line: run as a process, nm text without ua::Pintor (a product image) exits 2, the refusal, and says why', rc == 2 and 'ua::Pintor' in out and 'FINDING' not in out, '%d %s' % (rc, out[:160]))
        rc, out = go(*tool, elfs['b'], elfs['e']); case('main path: two images that both have findings (a planted draw, a stale allowance) exit 1 and print both: the code is the worst of them, not "exactly one image had a finding"', rc == 1 and 'FINDING: DisplayManager::drawSettingsMain()' in out and 'FINDING: DisplayManager::loopCore1()' in out, '%d %s' % (rc, out[:200]))
        rc, out = go(*tool, elfs['c'], elfs['c']); case('main path: two refused images exit 2', rc == 2, str(rc))
    print('RESULT: %d cases, %d failed' % (n, bad)); return 1 if bad else 0

def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('elf', nargs='*')
    ap.add_argument('--nm', default=NM_DEFAULT)
    ap.add_argument('--readelf', help='default: next to --nm when it is there, else the PlatformIO toolchain')
    ap.add_argument('--tsv', default=TSV_DEFAULT)
    ap.add_argument('--original-units', help='the units only the original compiles (default: [ui.original].units of tools/features.toml)')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv)
    if a.selftest: sys.exit(selftest())   # C6e: the verdict leaves here, not through the `sys.exit(main(...))` of __main__ that the command-line cases test: with that line's sys.exit( ) dropped, a return value is lost and --selftest would exit 0 with cases FAILing
    if not a.elf: ap.error('an ELF is required (or --selftest)')
    beside = os.path.join(os.path.dirname(a.nm), 'arm-none-eabi-readelf')
    readelf = a.readelf or (beside if os.path.isfile(beside) else READELF_DEFAULT)
    try: rows, listed = load_table(a.tsv, a.original_units)
    except Refusal as e: print('check_ui_link: %s: a refusal and not a pass' % e); return 2
    rc = [check(e, a.nm, readelf, rows, listed) for e in a.elf]
    return 2 if 2 in rc else (1 if 1 in rc else 0)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
