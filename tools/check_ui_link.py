#!/usr/bin/env python3
"""check_ui_link.py -- the Angulo images must not link the original UI driver: the function table decides, the name patterns check what it does not cover (S10b: C6, 2026-10-21; C6b, 2026-10-22).

usage: check_ui_link.py [--nm PATH] [--readelf PATH] [--tsv FILE] [--original-units A,B] ELF [ELF...]    each ELF an image that carries the Angulo (pico_wiretft_angulo, pico2_w_angulo)
       check_ui_link.py --selftest                  the gate's own cases on synthetic `nm` text and synthetic table rows, and a read of the real table (no toolchain, no ELF: it can run in the gates job)
exit 0: every ELF clean. 1: a finding (a symbol the Angulo image must not hold, or an allowance that no longer applies). 2: a refusal (no ELF, nm or readelf missing or failed, not an Angulo image, a table it cannot read).

Why. After S4 to S10b the Angulo links none of the original's draws and none of the pixel primitives it drew with: --gc-sections drops the units that are still compiled, and the builds no longer compile the eight units that [ui.original].units of
tools/features.toml lists (Themes.cpp since C4a, the other seven since C6). Nothing else in CI holds that: the flash budget counts bytes, not names, and the checks the S10b steps ran by hand (nm, a reachability script) lived in scratch space. One original draw reached again by a
core path, or a primitive by a hook, would put the original's pixels on the Angulo's panel with no gate saying so.

The first version (C6) was a grep of the names `DisplayManager::(draw[A-Z]\\w*|render|redraw\\w*)`, simutFont and availableThemes. Its review (2026-10-21) planted nine draw functions under other names in a real symbol table and the gate passed all nine; of the 188 symbols of
pico_wiretft that the C6 Wire Angulo lacks, the patterns matched 88 and missed 100, 56 of them draws (drawMinMaxTempRow, kbPopupCard, blitPinCards, commitScreenStrip) defined in units the Angulo still compiles (Dashboard, Users, Settings), where no link error covers them.

How it decides now. `nm -S -C` of the ELF, then three things.
1. The function table (docs/analysis/UI_DRIVERS_funcoes.tsv), read with bytes_original.py's own code (key_of, row_of, the unit list, the file symbols of readelf): a symbol that falls on a row of class `original` is a finding unless ALLOWED names it. A row
   of class core or mixed is not a finding by itself. By name for every unit, a unit that [ui.original].units lists and the image does not link included, and that is where this differs from bytes_original.py, which counts bytes: its unit rule leaves out the rows of an
   absent unit, so a planted `uiButton` (UiWidgets.cpp, absent from both Angulo images) would pass. Here such a hit says "another unit's definition of the same name, or the original leaking in without a file symbol", and only ALLOWED can excuse it: the Angulo's
   own Temas.cpp defines the six catalogue functions that Themes.cpp does (C4a), four of them are named with the unit, and the allowance holds only while that unit has no file symbol in the image (in C0's ELF Themes.cpp is linked and the same four names are findings).
2. The name patterns of C6 (RULES), still on every symbol, and one more: a name with a component that begins with draw, render or blit in any case (`draw`, `drawX`, `Draw3dBar`, `draw_bar`, `renderAlarmRow`, `blitTitleBar`), read on the qualified name without its parameters,
   outside the Angulo's own namespace (`ua::`) and the Adafruit GFX classes the Angulo and the original both link. The Angulo names its functions in Portuguese, so an English draw name in its image is the original's, a library's or the core's; the core ones that
   exist are in ALLOWED. A word inside an identifier (`uiDrawX`, `firstDraw`, `withdrawn`, `endRaw`) is not read by the patterns: the table covers those, and the near misses are cases of --selftest.
3. ALLOWED is by exact name (demangled, with the parameter list) and each entry has its reason. An allowance that is not in the image is a finding (the mark comes down in the change that shrinks), and so is one that is in the image and that nothing flags any more.

Known limits. nm lists what the linker kept: code the compiler inlined into another function has no symbol. A name that is neither a row nor matched by a pattern (a Portuguese or innocent name) is invisible to both checks; the table is the S0 scanner's classification of the original's tree and is kept
by the C7 step that touches it. The core path that keeps a function (a keeper) is not read: that is reach.py's job, outside the repository.

Measured on 2026-10-22 (S10b, C6b): on the C7 images (pico_wiretft_angulo and pico2_w_angulo) it exits 0 with the same 18 allowances used on both; on the images as they were before C4a (C0's Wire, C0b's Wi-Fi), with the theme table still linked, it exits 1 and names availableThemes through the table; the review's nine
planted names each exit 1 (the C6 gate exited 0 on all nine); on product images (no ua::Pintor) and on a missing file it exits 2. CI runs --selftest in the `gates` job and the gate itself on the ELF of each `_angulo` image in the `firmware` job (.github/workflows/build.yml).

Project: SIMUT
License: MIT
"""
import argparse, csv, os, re, subprocess, sys, tempfile
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

_DASH = "Dashboard.cpp keeper: uaMontarPainel( ) and uaToquePainel( ) call buildDashLayout( ), so the unit stays linked; S10b-2 splits it (P4's table, docs/analysis/UI_DRIVERS.md section 2.15)"
_CAL = "Calibration.cpp keeper: AppManager::core0Yield( ) calls showTouchCalibration( ) and showSettingsDisplayOffset( ), and WebManager::handleResetTouchCal( ) calls showTouchCalibration( ), so the unit keeps its statics; S10b-2 splits it (P4's table, docs/analysis/UI_DRIVERS.md section 2.15)"
_THEME = "the Angulo defines it in src/ui/angulo/Temas.cpp; the original's row is Themes.cpp's, a unit this image does not compile (C4a's collision, told apart by unit)"
# Exact name (demangled, with the parameter list) -> (reason, unit). A unit says the allowance holds only while that unit has no file symbol in the image (the name is then another unit's).
ALLOWED = {
    'DisplayManager::buildDashLayout(DisplayManager::DashBtn*, int*, bool*)': (_DASH, None),
    '(anonymous namespace)::g_lastCalStep': (_CAL, None),
    '(anonymous namespace)::g_lastSensDone': (_CAL, None),
    '(anonymous namespace)::g_sensFirstDraw': (_CAL, None),
    '(anonymous namespace)::g_lastCalPointIdx': (_CAL, None),
    '(anonymous namespace)::g_lastCalHoldReady': (_CAL, None),
    '(anonymous namespace)::g_lastSensStability': (_CAL, None),
    '(anonymous namespace)::g_lastSensThreshold': (_CAL, None),
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
            row('Dash.cpp', 'DisplayManager::blitTitleBar', 'original'), row('Wid.cpp', 'uiWidget', 'original'), row('Wid.cpp', 'uiShared', 'original'), row('Wid.cpp', 'uiButton', 'original'), row('Fon.cpp', 'FreeSansBold9pt8b', 'original')]
    LISTED = {'Wid.cpp'}
    ALLOW = {'DisplayManager::redrawTopPanel()': ('core, pattern side', None), 'DisplayManager::keepMe(int)': ('keeper, table side', None), 'uiShared(int)': ('collision with the Angulo\'s own', 'Wid.cpp')}
    BASE = ['00000100 00000010 T ua::Pintor::fillRect(int)', '00000200 00000020 T DisplayManager::uiTouch(short, short)', '00000300 00000008 W DisplayManager::redrawTopPanel()', '00000400 00000008 T DisplayManager::keepMe(int)',
            '00000500 00000008 T DisplayManager::coreOne(int)', '00000600 T DisplayManager::halfHalf(int)', '00000700 00000008 T uiShared(int)',
            '00000800 00000008 T Adafruit_GFX::drawLine(short, short, short, short, unsigned short)', '00000900 00000008 T ua::Faixa::drawPixel(short, short, unsigned short)',
            '00000a00 00000008 t non-virtual thunk to GFXcanvas16::drawPixel(short, short, unsigned short)', '00000b00 00000008 T withdrawn(int)', '00000c00 00000008 T prerenderCache()', '00000d00 00000008 T WebDictValue::endRaw()',
            '00000e00 00000008 T DisplayManager::pauseRendering(bool)', '00000f00 00000008 t (anonymous namespace)::g_firstDraw', '         U DisplayManager::drawSettingsMain()', '         w DisplayManager::blitTitleBar(char const*)']
    def run(lines, rows=ROWS, files=(), allowed=ALLOW): return scan('\n'.join(lines), rows, LISTED, set(files), allowed)
    a, f, u = run(BASE)
    case('the clean image (the near misses withdrawn, prerenderCache, endRaw, pauseRendering and g_firstDraw; the library and ua:: draws; an undefined and a weak undefined draw; core and mixed rows; the three allowances present): 0 findings, 3 used', a and not f and len(u) == 3, str(f))
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
    for gone in ('uiShared(int)', 'DisplayManager::keepMe(int)', 'DisplayManager::redrawTopPanel()'):
        a, f, u = run([x for x in BASE if gone not in x])
        case('the allowance for %s without its symbol is a finding (stale)' % gone, len(f) == 1 and f[0][0] == gone and 'stale' in f[0][1], str(f))
    a, f, u = run(BASE, rows=[r if r['symbol'] != 'DisplayManager::keepMe' else row('Dash.cpp', 'DisplayManager::keepMe', 'core') for r in ROWS])
    case('the allowance whose symbol the table no longer calls original (the row went core) is stale too: nothing flags it', len(f) == 1 and f[0][0] == 'DisplayManager::keepMe(int)' and 'nothing flags it' in f[0][1], str(f))
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
        case('the real table: the manifest lists the eight units the Angulo does not compile, each with a row', len(listed) == 8 and all(any(r['unit'] == x for r in rows) for x in listed), str(sorted(listed)))
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
    if a.selftest: return selftest()
    if not a.elf: ap.error('an ELF is required (or --selftest)')
    beside = os.path.join(os.path.dirname(a.nm), 'arm-none-eabi-readelf')
    readelf = a.readelf or (beside if os.path.isfile(beside) else READELF_DEFAULT)
    try: rows, listed = load_table(a.tsv, a.original_units)
    except Refusal as e: print('check_ui_link: %s: a refusal and not a pass' % e); return 2
    rc = [check(e, a.nm, readelf, rows, listed) for e in a.elf]
    return 2 if 2 in rc else (1 if 1 in rc else 0)

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
