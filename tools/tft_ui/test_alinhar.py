#!/usr/bin/env python3
"""test_alinhar.py -- the display alignment's preview and the glass it leaves, on the host (2026-10-31).

The defect (v2.13.0 CHANGELOG, *Known*, found by reading the code on 2026-10-31 after the walk of 2026-10-30 stopped on this screen): the alignment screen puts its preview
on the glass at once (actOffsetPreview( )), and only Voltar (actOffsetBack( )) put the saved offset back. The 30 s idle return, the CLI's `screen *`, a theme change and an update left
the preview on the glass, and the next entry (showSettingsDisplayOffset( )) took the glass for the "saved" offset, so the preview became the saved one. The state and the actions are
core code, shared by both panels; the two panels differ in their touch handling and in how they repaint. The fix: Core 1's tickDisplayOffset( ) puts the saved offset back whenever the
screen is not shown (and the preview back when it is) and asks for a full repaint; the entry starts from the RAM mirror (_offsetSaved*) and not from the glass; the Apply loads the
new offset into that mirror (loadDisplayOffset( )) before the calibration's entry. This holds it:

  1. The glass RUN (host/test_alinhar_main.cpp): the pieces below cut out of the sources verbatim, without comments, compiled with the real SettingsSteps.h and OtaScreen.h against a
     stand-in (a fake panel that records every value the glass was given and whether Core 0 painted the margins, the mutex, the touch gates and the event queue), once for the
     original panel and once for the Angulo. Eight cases for each (the numbering follows the review: there is no C8); C1 and C4 run the preview as Right, as Down and as both, and every move of the glass on Core 1 must paint the margins:
       C1  saved 0,0, Right, the 30 s idle return, ONE pass: the glass is 0,0 and the full repaint was asked for; before that, a pass leaves the preview on the glass while the screen is shown
       C2  the next entry starts at 0,0
       C3  Core 0's exits (`screen dash`, `screen set`, an update while Core 1 runs, before the stage parks it or after a failed one: the parked case is the
           documented exception, which the host cannot run, and a theme change), and a preview left in any mode (the dashboard, the chart, its loading screen and detail, the calendar, the settings
           screens, the update screen), leave the glass at the saved offset and the screen repainted in full at it; the tick raises exactly the requests that screen takes, and none is left standing
       C4  `screen offset` while a preview is shown, then Voltar: the saved offset is back (the preview is not taken for the saved one)
       C5  Voltar, the control: the saved offset goes back at once and the tick has nothing left to correct
       C6  Salvar: the glass keeps the new offset through the whole Apply (Core 1 interleaved at each point the Apply passes), and the next entry starts at it
       C7  the boot's load interleaved with Core 1 at each point: the glass converges to the loaded offset, never shows another value once it showed it, and Core 0 never paints margins
       C9  `screen offset` over a stale preview left in the members, with a Core 1 pass after each store the entry makes: the glass never shows the old preview
     The pieces: showSettingsDisplayOffset( ), loadDisplayOffset( ), tickDisplayOffset( ) (CalibrationCore.cpp), actOffsetUp( ) .. actOffsetApply( ) (Actions.h), the idle block of
     handleTouch( ) (Touch.cpp), forceDashboard( ) and refreshTheme( ) (DisplayManager.cpp), showSettingsMain( ), otaTick( ), the Apply branch of the event handler (AppManager_Events.cpp),
     the original's touch branch (UiTouchChain.h) and the Angulo's case (DisplayManager_UiA_Ajustes.cpp), and the Angulo's uiThemeChanged( ). What the stand-in models, not cuts: the
     screens' draw chain (uiPass( )), the original's theme hook (it draws) and the calibration's entry (it changes the mode): items 3 and 4 hold those to the files.
  2. The text: T1 the tick is defined once, called once, from loopCore1( ), between `handleTouch( );` and `otaTick( );` and outside _stateMutex (never on Core 0); T2 in the Apply,
     `loadDisplayOffset(` comes before `showTouchCalibration(`; T3 the entry does not touch the glass and writes the preview, a release fence, then the mode; T4 loadDisplayOffset( ) writes _offsetSaved* before every store to the glass, with a release fence between; T7 the tick reads the glass and the mode, then an acquire fence, then the targets.
  3. T5 the original's screen chain: each mode branch of uiPass( ) tests the flag the stand-in assumes (the dashboard pullSnapshot( ), the chart _repaintGraph, the calendar
     _repaintCalendar, the loading screen _repaintLoading, every other screen _repaintSettings), and the update screen is painted by otaTick( ) from _otaRedrawAll.
  4. T6 the Angulo's screen chain takes every request (_repaintSettings, _repaintGraph, _repaintLoading, _repaintCalendar, _forceSettingsRedraw), its frame identity holds the glass's
     alignment, and its update screen turns _otaRedrawAll into a full repaint.
  5. The mutants (the cut pieces edited one statement at a time, the loop's text reordered), each of which must fail the script: M1 the tick without its mode test, M2 the entry reading
     the glass, M3 the Apply without loadDisplayOffset( ) or with it after showTouchCalibration( ), M4 the tick out of order, M5 the tick without the repaint request, M6 the load storing
     the glass before the saved value, M7 the tick raising every request whatever the screen is, M8 the load without its release fence, M9 the tick without its acquire fence, and the review's N7 the tick comparing the X axis only, N7b the tick taking the saved Y while the screen is shown, N4 the restore without the margins, M10 the entry without its release fence, M11 the tick reading the mode after its fence, M12 the entry writing the mode before the preview, and the re-review's M13 and M14 a fence in dead code (`if (0)`), in the entry and in the tick. A mutant that does not compile, or an anchor not found exactly once, fails this script: a mutant must be killed by a check and not by the compiler.

Expected failures: XFAIL (below) lists what the defect still fails. The script exits 0 and lists them; an XPASS (an expected failure that passes) makes it exit 1, because the bug has
closed and the mark must leave.

Usage: python3 tools/tft_ui/test_alinhar.py   exit 0 when all hold, 1 when not, 2 when it cannot build (no g++).
"""
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SRC = ROOT / 'src'
MAIN = HERE / 'host' / 'test_alinhar_main.cpp'

F_CAL = SRC / 'DisplayManager_CalibrationCore.cpp'
F_ACT = SRC / 'DisplayManager_Actions.h'
F_TOQUE = SRC / 'DisplayManager_Touch.cpp'
F_DM = SRC / 'DisplayManager.cpp'
F_MENU = SRC / 'DisplayManager_SettingsCore.cpp'
F_OTA = SRC / 'DisplayManager_OtaCore.cpp'
F_EVT = SRC / 'AppManager_Events.cpp'
F_CADEIA = SRC / 'ui' / 'original' / 'UiTouchChain.h'
F_ORIG = SRC / 'ui' / 'original' / 'UiOriginal.h'
F_AJ = SRC / 'ui' / 'angulo' / 'DisplayManager_UiA_Ajustes.cpp'
F_UIA = SRC / 'ui' / 'angulo' / 'DisplayManager_UiA.cpp'
F_ALVO = SRC / 'ui' / 'angulo' / 'telas' / 'AjustesTela.h'

# The expected failures: none. A check that is marked here fails without failing the script, and one marked here that passes (XPASS) fails it, so a known bug can be written
# down with its mark and has to leave it when it is fixed. No mutant runs while a mark stands: it is judged against a base that is green.
XFAIL = frozenset()

NOMES = ('entrada.inc', 'carga.inc', 'tique.inc', 'acoes.inc', 'ocioso.inc', 'forca.inc', 'menu.inc', 'ota.inc', 'tema.inc', 'tema_ang.inc', 'evento.inc', 'toque_orig.inc', 'toque_ang.inc',
         'alinh.inc')
CASOS = [f'{c}/{ui}' for c in ('C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C9') for ui in ('original', 'angulo')]
TEXTOS = ['T1', 'T2', 'T3', 'T4', 'T5', 'T6', 'T7']

CONTA = [0]
FALHAS = []
MODELO = [0, 0]   # the checks and the failures the C++ model counted in the unmutated run


def confere(cond, msg):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    return bool(cond)


# ---- cutting the sources (comments removed, newlines kept; strings and chars left alone; 1'000 is a digit separator) ----
def apaga(t):
    saida, i, n = [], 0, len(t)
    while i < n:
        c, d = t[i], (t[i + 1] if i + 1 < n else '')
        if c == '/' and d == '*':
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            saida.append(re.sub(r'[^\n]', ' ', t[i:j]))
            i = j
        elif c == '/' and d == '/':
            j = t.find('\n', i)
            j = n if j < 0 else j
            saida.append(' ' * (j - i))
            i = j
        elif c == '"' or (c == "'" and not (i and t[i - 1].isalnum() and d.isalnum())):
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            saida.append(t[i:j + 1])
            i = j + 1
        else:
            saida.append(c)
            i += 1
    return ''.join(saida)


def casa_chaves(t, abre):
    prof, i, n = 0, abre, len(t)
    while i < n:
        c = t[i]
        if c == '"' or (c == "'" and not (i and t[i - 1].isalnum() and i + 1 < n and t[i + 1].isalnum())):
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            i = j + 1
            continue
        if c == '{':
            prof += 1
        elif c == '}':
            prof -= 1
            if prof == 0:
                return i
        i += 1
    return -1


def corpo_da_funcao(limpo, cabeca):
    m = list(re.finditer(cabeca, limpo))
    if len(m) != 1:
        return None
    abre = limpo.find('{', m[0].end())
    fecha = casa_chaves(limpo, abre) if abre >= 0 else -1
    return limpo[abre:fecha + 1] if fecha > 0 else None


def funcao(limpo, cabeca):
    """The whole function, signature included (from the start of its line), or None when it is not there exactly once."""
    m = list(re.finditer(cabeca, limpo))
    if len(m) != 1:
        return None
    ini = limpo.rfind('\n', 0, m[0].start()) + 1
    abre = limpo.find('{', m[0].end())
    fecha = casa_chaves(limpo, abre) if abre >= 0 else -1
    return limpo[ini:fecha + 1] + '\n' if fecha > 0 else None


def ramo(limpo, ancora, escolhe=None):
    """The block an anchor (a regex ending at its `{`) opens, with the anchor, or None when it is not there exactly once (`escolhe` picks among several)."""
    ms = [m for m in re.finditer(ancora, limpo) if casa_chaves(limpo, m.end() - 1) > 0]
    if escolhe:
        ms = [m for m in ms if escolhe(limpo[m.start():casa_chaves(limpo, m.end() - 1) + 1])]
    if len(ms) != 1:
        return None
    return limpo[ms[0].start():casa_chaves(limpo, ms[0].end() - 1) + 1] + '\n'


def le(f):
    return apaga(f.read_text(encoding='utf-8'))


# ---- the pieces ----
def pecas_das_fontes():
    """({name: text} for the host program, the text of loopCore1( )), and the problems with the sources (as failed checks). A piece not found is empty, so that the model still runs."""
    p = {n: '' for n in NOMES}
    cal, act, toq, dm, menu, ota, evt, cad, aj, uia, alvo = (le(f) for f in (F_CAL, F_ACT, F_TOQUE, F_DM, F_MENU, F_OTA, F_EVT, F_CADEIA, F_AJ, F_UIA, F_ALVO))

    def poe(nome, texto, o_que):
        if confere(texto is not None, f'{o_que} was not found exactly once'):
            p[nome] = texto
    poe('entrada.inc', funcao(cal, r'void\s+DisplayManager::showSettingsDisplayOffset\s*\('), 'showSettingsDisplayOffset( ) in DisplayManager_CalibrationCore.cpp')
    poe('carga.inc', funcao(cal, r'void\s+DisplayManager::loadDisplayOffset\s*\('), 'loadDisplayOffset( ) in DisplayManager_CalibrationCore.cpp')
    p['tique.inc'] = funcao(cal, r'void\s+DisplayManager::tickDisplayOffset\s*\(') or ''   # absent before the fix: T1 says so
    acoes = [funcao(act, r'DisplayManager::actOffset' + n + r'\s*\(') for n in ('Up', 'Down', 'Left', 'Right', 'Centre', 'Preview', 'Back', 'Apply')]
    poe('acoes.inc', ''.join(acoes) if all(acoes) else None, 'actOffsetUp( ) .. actOffsetApply( ) in DisplayManager_Actions.h (eight functions)')
    poe('ocioso.inc', ramo(toq, r'if\s*\(\s*_uiMode\s*!=\s*MODE_DASHBOARD\s*&&\s*_uiMode\s*!=\s*MODE_OTA_UPDATE\s*&&\s*!_sharedState\.isBooting\s*\)\s*\{'),
        'the idle return in DisplayManager_Touch.cpp')
    poe('forca.inc', funcao(dm, r'void\s+DisplayManager::forceDashboard\s*\('), 'forceDashboard( ) in DisplayManager.cpp')
    poe('menu.inc', funcao(menu, r'void\s+DisplayManager::showSettingsMain\s*\('), 'showSettingsMain( ) in DisplayManager_SettingsCore.cpp')
    poe('ota.inc', funcao(ota, r'void\s+DisplayManager::otaTick\s*\('), 'otaTick( ) in DisplayManager_OtaCore.cpp')
    poe('tema.inc', funcao(dm, r'void\s+DisplayManager::refreshTheme\s*\('), 'refreshTheme( ) in DisplayManager.cpp')
    t = funcao(uia, r'void\s+DisplayManager::uiThemeChanged\s*\(')
    poe('tema_ang.inc', t.replace('uiThemeChanged', 'uiThemeChangedAngulo') if t else None, 'uiThemeChanged( ) in the Angulo\'s DisplayManager_UiA.cpp')
    poe('evento.inc', ramo(evt, r'else\s+if\s*\(\s*uiEv\.type\s*==\s*UiEvent::EVT_APPLY_DISPLAY_OFFSET\s*\)\s*\{'), 'the EVT_APPLY_DISPLAY_OFFSET branch in AppManager_Events.cpp')
    poe('toque_orig.inc', ramo(cad, r'else\s+if\s*\(\s*_uiMode\s*==\s*MODE_SETTINGS_DISPLAY_OFFSET\s*\)\s*\{'), 'the alignment branch in the original\'s UiTouchChain.h')
    poe('toque_ang.inc', ramo(aj, r'case\s+MODE_SETTINGS_DISPLAY_OFFSET\s*:\s*\{', lambda b: 'actOffsetBack' in b), 'the touch case of the alignment screen in DisplayManager_UiA_Ajustes.cpp')
    m = re.search(r'enum\s+AlvoAlinhar\s*:\s*uint8_t\s*\{[^}]*\};', alvo)
    poe('alinh.inc', m.group(0) + '\n' if m else None, 'enum AlvoAlinhar in telas/AjustesTela.h')
    laco = corpo_da_funcao(dm, r'void\s+DisplayManager::loopCore1\s*\(')
    confere(laco is not None, 'DisplayManager::loopCore1( ) was not found exactly once in DisplayManager.cpp')
    return p, laco


def chamadas_do_tique():
    """Statements that call tickDisplayOffset( ) in src/ (the definition and the declaration do not start with the name)."""
    n = 0
    for f in sorted(list(SRC.rglob('*.cpp')) + list(SRC.rglob('*.h'))):
        n += len(re.findall(r'^[ \t]*tickDisplayOffset\s*\(\s*\)\s*;', apaga(f.read_text(encoding='utf-8', errors='replace')), re.M))
    return n


# ---- the text checks ----
def t1(laco, chamadas, tique):
    msgs, pos = [], {}
    if laco is None:
        return 'FAIL', 'loopCore1( ) not found'
    for n, rx in (('handleTouch', r'\bhandleTouch\s*\(\s*\)\s*;'), ('tick', r'\btickDisplayOffset\s*\(\s*\)\s*;'), ('otaTick', r'\botaTick\s*\(\s*\)\s*;')):
        ms = [m.start() for m in re.finditer(rx, laco)]
        if len(ms) != 1:
            msgs.append(f'{n}( ) is called {len(ms)} time(s) in loopCore1( ), wanted once')
        pos[n] = ms[0] if len(ms) == 1 else None
    if all(v is not None for v in pos.values()):
        if not pos['handleTouch'] < pos['tick'] < pos['otaTick']:
            msgs.append('the tick is not between `handleTouch( );` and `otaTick( );`')
        aberto = len(re.findall(r'\bmutex_enter_blocking\b', laco[:pos['tick']])) - len(re.findall(r'\bmutex_exit\b', laco[:pos['tick']]))
        if aberto != 0:
            msgs.append('the tick is called with _stateMutex held')
    if chamadas != 1:
        msgs.append(f'{chamadas} call(s) of tickDisplayOffset( ) in src/, wanted exactly one (loopCore1( ), Core 1)')
    if not tique.strip():
        msgs.append('tickDisplayOffset( ) is not defined in DisplayManager_CalibrationCore.cpp')
    return ('FAIL', '; '.join(msgs)) if msgs else ('PASS', '')


def t2(evento):
    a = [m.start() for m in re.finditer(r'\bloadDisplayOffset\s*\(', evento)]
    b = [m.start() for m in re.finditer(r'\bshowTouchCalibration\s*\(', evento)]
    if len(a) != 1 or len(b) != 1:
        return 'FAIL', f'the Apply calls loadDisplayOffset( ) {len(a)} time(s) and showTouchCalibration( ) {len(b)}, wanted once each'
    if a[0] > b[0]:
        return 'FAIL', 'loadDisplayOffset( ) comes after showTouchCalibration( ) in the Apply'
    return 'PASS', ''


def cercas(texto, ordem):
    """The positions of the fences of that order that stand as a statement of their own: alone at the start of their line, and not the body of an
    if/for/while/else on the line before. A fence in dead code (`if (0) __atomic_thread_fence( ... );`) passed the position checks below; the
    re-review of 2026-10-31 found it. These checks pin the fence in the text; that it is a DMB in the image is read from the disassembly, by hand."""
    linhas, pos, achadas = texto.split('\n'), 0, []
    for i, l in enumerate(linhas):
        if re.match(r'[ \t]*__atomic_thread_fence\s*\(\s*' + ordem + r'\s*\)\s*;', l):
            antes = next((x for x in reversed(linhas[:i]) if x.strip()), '')
            if not re.search(r'^\s*(?:\}\s*)?(?:(?:if|for|while)\b.*\)|else)\s*$', antes):
                achadas.append(pos + l.index('__atomic_thread_fence'))
        pos += len(l) + 1
    return achadas


def t3(entrada):
    achou = re.findall(r'getOffset[XY]|setDisplayOffset|_driver\.tft', entrada)
    if achou:
        return 'FAIL', f'the entry touches the glass ({", ".join(sorted(set(achou)))}); it must start from _offsetSaved*'
    pv = [m.start() for m in re.finditer(r'_offsetPreview[XY]\s*=(?!=)', entrada)]
    ce = cercas(entrada, '__ATOMIC_RELEASE')
    md = [m.start() for m in re.finditer(r'\b_uiMode\s*=(?!=)', entrada)]
    if len(pv) != 2 or len(ce) != 1 or len(md) != 1 or not max(pv) < ce[0] < md[0]:
        return 'FAIL', f'the entry must write the preview (X and Y), then a release fence, then the mode: {len(pv)} preview store(s), {len(ce)} fence(s), {len(md)} mode store(s), in that order or not'
    return 'PASS', ''


def t4(carga):
    msgs = []
    achados = list(re.finditer(r'setDisplayOffset\s*\(', carga))
    if not achados:
        return 'FAIL', 'loadDisplayOffset( ) does not store to the glass'
    for m in achados:
        antes = carga[:m.start()]
        if not (re.search(r'_offsetSavedX\s*=', antes) and re.search(r'_offsetSavedY\s*=', antes)):
            msgs.append('a store to the glass comes before the _offsetSaved* stores: Core 1 could see a new glass with an old saved value')
            break
        cs = cercas(antes, '__ATOMIC_RELEASE')
        if not cs or not re.fullmatch(r'__atomic_thread_fence\s*\(\s*__ATOMIC_RELEASE\s*\)\s*;[^;{}]*', antes[cs[-1]:]):
            msgs.append('no release fence (__atomic_thread_fence( __ATOMIC_RELEASE )) between the _offsetSaved* stores and the store to the glass')
            break
    return ('FAIL', '; '.join(msgs)) if msgs else ('PASS', '')


def t7(tique):
    """The tick reads the glass and the mode, then an acquire fence, then the targets: loadDisplayOffset( ) writes the mirror before the glass and the entry writes the preview before the mode, so a pass that read the new glass, or the mode "shown", reads the new values."""
    if not tique.strip():
        return 'FAIL', 'tickDisplayOffset( ) is not defined'
    vidro = [m.start() for m in re.finditer(r'getOffset[XY]\s*\(', tique)]
    cerca = cercas(tique, '__ATOMIC_ACQUIRE')
    modo = [m.start() for m in re.finditer(r'\b_uiMode\b', tique)]
    alvos = [m.start() for m in re.finditer(r'\b_offsetSaved[XY]\b|\b_offsetPreview[XY]\b', tique)]
    if len(vidro) != 2 or len(cerca) != 1 or len(modo) != 1 or not alvos:
        return 'FAIL', f'the tick reads the glass {len(vidro)} time(s) (wanted 2, X and Y), the mode {len(modo)} time(s) (wanted 1) and has {len(cerca)} acquire fence(s) (wanted 1)'
    if not (max(vidro) < cerca[0] and modo[0] < cerca[0] < min(alvos)):
        return 'FAIL', 'the tick must read the glass and the mode, then an acquire fence (__atomic_thread_fence), then the targets'
    return 'PASS', ''


# The original's screen chain: which flag each mode branch of uiPass( ) tests (the table the stand-in's uiPass( ) assumes).
def cadeia_original():
    corpo = corpo_da_funcao(le(F_ORIG), r'UI_HOOK\s+void\s+DisplayManager::uiPass\s*\(')
    if corpo is None:
        return 'FAIL', 'uiPass( ) not found exactly once in ui/original/UiOriginal.h', []
    ramos = list(re.finditer(r'(?:else\s+)?if\s*\(\s*_uiMode\s*==\s*(MODE_\w+)\s*\)\s*\{', corpo))
    msgs, tabela = [], []
    for k, m in enumerate(ramos):
        fim = ramos[k + 1].start() if k + 1 < len(ramos) else len(corpo)
        texto = corpo[m.start():fim]
        modo = m.group(1)
        if modo == 'MODE_DASHBOARD':
            quer = 'pullSnapshot'
        elif modo == 'MODE_GRAPH_LOADING':
            quer = '_repaintLoading'
        elif modo in ('MODE_GRAPH_VIEW', 'MODE_GRAPH_DETAIL'):
            quer = '_repaintGraph'
        elif modo == 'MODE_CALENDAR':
            quer = '_repaintCalendar'
        else:
            quer = '_repaintSettings'
        tabela.append((modo, quer))
        if quer not in texto:
            msgs.append(f'{modo} does not test {quer}')
    defs = apaga((SRC / 'SystemDefs_Records.h').read_text(encoding='utf-8'))
    e = re.search(r'enum\s+UiMode\s*\{(.*?)\}\s*;', defs, re.S)
    if e:
        sem_ramo = [m for m in re.findall(r'\b(MODE_\w+)\b', e.group(1)) if m not in {t[0] for t in tabela} and m != 'MODE_OTA_UPDATE']
        if sem_ramo:
            msgs.append('modes with no branch in uiPass( ), which tickDisplayOffset( ) would give the settings pair: ' + ', '.join(sem_ramo))
    else:
        msgs.append('enum UiMode not found in SystemDefs_Records.h')
    if len(ramos) < 15:
        msgs.append(f'only {len(ramos)} mode branches found in uiPass( ), wanted at least 15')
    ota = le(F_OTA)
    if not re.search(r'_otaRedrawAll\s*=\s*true', ota):
        msgs.append('otaTick( ) no longer raises _otaRedrawAll when it takes the panel')
    return ('FAIL', '; '.join(msgs), tabela) if msgs else ('PASS', '', tabela)


def cadeia_angulo():
    uia = le(F_UIA)
    msgs = []
    for f in ('_repaintSettings', '_repaintGraph', '_repaintLoading', '_repaintCalendar', '_forceSettingsRedraw'):
        if not re.search(r'if\s*\(\s*' + f + r'\s*\)\s*\{\s*' + f + r'\s*=\s*false\s*;\s*pedido\s*=\s*true\s*;', uia):
            msgs.append(f'the Angulo\'s uiPass( ) no longer takes {f} into a request')
    if not re.search(r'identidadeDoQuadro\s*\(\s*\(uint8_t\)_uiMode\s*,\s*tema\s*,\s*ox\s*,\s*oy\s*\)', uia):
        msgs.append('the Angulo\'s frame identity no longer holds the glass\'s alignment (ox, oy)')
    if not re.search(r'if\s*\(\s*_otaRedrawAll\s*\)\s*_uaTudo\s*=\s*true\s*;', uia):
        msgs.append('the Angulo\'s update screen no longer turns _otaRedrawAll into _uaTudo')
    return ('FAIL', '; '.join(msgs)) if msgs else ('PASS', '')


def textos(p, laco, chamadas):
    t5 = cadeia_original()
    return {'T1': t1(laco, chamadas, p['tique.inc']), 'T2': t2(p['evento.inc']), 'T3': t3(p['entrada.inc']), 'T4': t4(p['carga.inc']),
            'T5': t5[:2], 'T6': cadeia_angulo(), 'T7': t7(p['tique.inc'])}, t5[2]


# ---- building and running the model ----
def roda(tmp, nome, p, sem_tique):
    """(returncode, output) of the model built from the pieces `p`; returncode None when it does not compile."""
    d = tmp / nome
    d.mkdir()
    for k, v in p.items():
        (d / k).write_text(v, encoding='utf-8')
    exe = d / 'modelo'
    r = subprocess.run(['g++', '-std=gnu++17', '-O0', '-Wall', '-Wextra', f'-DSEM_TIQUE={1 if sem_tique else 0}', f'-I{SRC}', f'-I{d}', str(MAIN), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[:3000]
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


def resultados(rc, out, p, laco, chamadas):
    """{id: (PASS|FAIL, detail)} for the model's cases and the text checks; the model's checks and failures."""
    res = {}
    for c, ui, st, det in re.findall(r'^CASE (\S+) (\S+) (PASS|FAIL) ?(.*)$', out, re.M):
        res[f'{c}/{ui}'] = (st, det)
    m = re.search(r'RESULT: (\d+) checks, (\d+) failed', out)
    t, _ = textos(p, laco, chamadas)
    res.update(t)
    return res, (int(m.group(1)), int(m.group(2))) if m else (0, 0)


# (name, the piece, the statement, what replaces it): each edit must make the model or the text checks fail. The statements are the tree's, as cut (comments removed).
MUTANTES = [
    ('M1 the tick without its mode test', 'tique.inc', 'mode == MODE_SETTINGS_DISPLAY_OFFSET', 'false'),
    ('M2 the entry reading the glass', 'entrada.inc', '_offsetPreviewX = _offsetSavedX;',
     '_offsetSavedX = _driver.tft ? _driver.tft->getOffsetX( ) : 0; _offsetSavedY = _driver.tft ? _driver.tft->getOffsetY( ) : 0; _offsetPreviewX = _offsetSavedX;'),
    ('M3a the Apply without loadDisplayOffset( )', 'evento.inc', '_displayMgr->loadDisplayOffset(ofs);', '( void )0;'),
    ('M3b the Apply with loadDisplayOffset( ) after showTouchCalibration( )', 'evento.inc', None, None),
    ('M5 the tick without the repaint request', 'tique.inc', None, None),
    ('M6 the load storing the glass before the saved value', 'carga.inc', None, None),
    ('M7 the tick raising every request, whatever the screen is', 'tique.inc', None, None),
    ('M8 the load without its release fence', 'carga.inc', None, None),
    ('N7 the tick comparing the X axis only', 'tique.inc', 'if (glassX == targetX && glassY == targetY) return;', 'if (glassX == targetX) return;'),
    ('N7b the tick taking the saved Y while the screen is shown', 'tique.inc', 'const int8_t targetY = shown ? _offsetPreviewY : _offsetSavedY;', 'const int8_t targetY = _offsetSavedY;'),
    ('N4 the restore without the margins', 'tique.inc', '_driver.tft->setDisplayOffset(targetX, targetY);', '_driver.tft->setDisplayOffset(targetX, targetY, false);'),
    ('M10 the entry without its release fence', 'entrada.inc', None, None),
    ('M11 the tick reading the mode after its fence', 'tique.inc', None, None),
    ('M12 the entry writing the mode before the preview', 'entrada.inc', None, None),
    ('M9 the tick without its acquire fence', 'tique.inc', None, None),
    ('M13 the entry\'s release fence in dead code', 'entrada.inc', '__atomic_thread_fence(__ATOMIC_RELEASE);', 'if (0) __atomic_thread_fence(__ATOMIC_RELEASE);'),
    ('M14 the tick\'s acquire fence in dead code', 'tique.inc', '__atomic_thread_fence(__ATOMIC_ACQUIRE);', 'if (0) __atomic_thread_fence(__ATOMIC_ACQUIRE);'),
]


def aplica(nome, p, laco):
    """(pieces, loop text) after the mutant `nome`, or None when its anchor is not exactly where it must be."""
    q = dict(p)
    for n, peca, antigo, novo in MUTANTES:
        if n != nome:
            continue
        if antigo is not None:
            if p[peca].count(antigo) != 1:
                return None
            q[peca] = p[peca].replace(antigo, novo)
        elif n.startswith('M3b'):
            ev = p['evento.inc']
            chamada, tela = '_displayMgr->loadDisplayOffset(ofs);', '_displayMgr->showTouchCalibration( );'
            if ev.count(chamada) != 1 or ev.count(tela) != 1:
                return None
            q['evento.inc'] = ev.replace(chamada, '( void )0;').replace(tela, tela + ' ' + chamada)
        elif n.startswith('M5'):
            ti = p['tique.inc']
            achou = list(re.finditer(r'(setDisplayOffset\s*\(\s*targetX\s*,\s*targetY\s*\)\s*;).*\n\}\s*$', ti, re.S))
            if len(achou) != 1:
                return None
            q['tique.inc'] = ti[:achou[0].end(1)] + '\n}\n'
        elif n.startswith('M10'):
            novo_en = re.sub(r'__atomic_thread_fence\s*\(\s*__ATOMIC_RELEASE\s*\)\s*;', '', p['entrada.inc'])
            if novo_en == p['entrada.inc']:
                return None
            q['entrada.inc'] = novo_en
        elif n.startswith('M11'):
            ti = p['tique.inc']
            fe = re.search(r'__atomic_thread_fence\s*\(\s*__ATOMIC_ACQUIRE\s*\)\s*;', ti)
            if not fe or 'const UiMode mode' not in ti:
                return None
            sem = ti[:fe.start()] + ti[fe.end():]
            k = sem.index('const UiMode mode')
            q['tique.inc'] = sem[:k] + fe.group(0) + ' ' + sem[k:]
        elif n.startswith('M12'):
            en = p['entrada.inc']
            mm = re.search(r'_uiMode\s*=\s*MODE_SETTINGS_DISPLAY_OFFSET\s*;', en)
            pv = re.search(r'_offsetPreviewX\s*=', en)
            if not mm or not pv or mm.start() < pv.start():
                return None
            sem = en[:mm.start()] + en[mm.end():]
            k = sem.index('_offsetPreviewX')
            q['entrada.inc'] = sem[:k] + mm.group(0) + ' ' + sem[k:]
        elif n.startswith('M8'):
            novo_cg = re.sub(r'__atomic_thread_fence\s*\(\s*__ATOMIC_RELEASE\s*\)\s*;', '', p['carga.inc'])
            if novo_cg == p['carga.inc']:
                return None
            q['carga.inc'] = novo_cg
        elif n.startswith('M9'):
            novo_ti = re.sub(r'__atomic_thread_fence\s*\(\s*__ATOMIC_ACQUIRE\s*\)\s*;', '', p['tique.inc'])
            if novo_ti == p['tique.inc']:
                return None
            q['tique.inc'] = novo_ti
        elif n.startswith('M7'):
            ti = p['tique.inc']
            sw = re.search(r'switch\s*\(\s*mode\s*\)\s*\{', ti)
            fim = casa_chaves(ti, sw.end() - 1) if sw else -1
            if fim < 0:
                return None
            q['tique.inc'] = (ti[:sw.start()] + '_forceSettingsRedraw = true; _repaintSettings = true; _repaintGraph = true; _repaintCalendar = true; _otaRedrawAll = true; '
                              'mutex_enter_blocking(&_stateMutex); _isDirty = true; _forceFullRedraw = true; mutex_exit(&_stateMutex);' + ti[fim + 1:])
        elif n.startswith('M6'):
            cg = p['carga.inc']
            gl = list(re.finditer(r'[^\n;{}]*setDisplayOffset\s*\([^;]*\)\s*;', cg))
            sv = list(re.finditer(r'\s*_offsetSavedX\s*=\s*ox\s*;\s*_offsetSavedY\s*=\s*oy\s*;', cg))
            if len(sv) != 1 or len(gl) < 2:
                return None
            glass = gl[-1]
            if glass.start() < sv[0].end():
                return None
            q['carga.inc'] = cg[:sv[0].start()] + '\n' + glass.group(0) + cg[sv[0].start():glass.start()] + cg[glass.end():]
    return q, laco


def mutante_do_laco(laco):
    """M4: the tick moved after otaTick( )."""
    tique = re.compile(r'\btickDisplayOffset\s*\(\s*\)\s*;')
    ota = re.compile(r'(\botaTick\s*\(\s*\)\s*;)')
    if len(tique.findall(laco)) != 1 or len(ota.findall(laco)) != 1:
        return None
    return ota.sub(r'\1 tickDisplayOffset( );', tique.sub('', laco))


def mutantes(tmp, p, laco, chamadas, sem_tique):
    trabalho = []
    for nome, *_ in MUTANTES:
        r = aplica(nome, p, laco)
        if not confere(r is not None, f'mutant "{nome}": its anchor is not in the piece exactly once'):
            continue
        trabalho.append((nome, r[0], r[1]))
    m4 = mutante_do_laco(laco)
    if confere(m4 is not None, 'mutant "M4 the tick out of order": loopCore1( ) has not `tickDisplayOffset( );` and `otaTick( );` once each'):
        trabalho.append(('M4 the tick out of order', p, m4))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        fs = [(nome, q, lc, ex.submit(roda, tmp, f'mut{k}', q, sem_tique)) for k, (nome, q, lc) in enumerate(trabalho)]
        for nome, q, lc, f in fs:
            rc_m, out_m = f.result()
            if rc_m is None:
                confere(False, f'mutant "{nome}" does not compile (a mutant is killed by a check, not by the compiler): {out_m.strip().splitlines()[0][:160] if out_m.strip() else ""}')
                continue
            res_m, _ = resultados(rc_m, out_m, q, lc, chamadas)
            mortos = [i for i in CASOS + TEXTOS if res_m.get(i, ('FAIL', ''))[0] == 'FAIL' and i not in XFAIL]
            if confere(bool(mortos), f'mutant "{nome}" must fail the script, and every check passed'):
                print(f'  killed: {nome} by {", ".join(mortos[:6])}{" ..." if len(mortos) > 6 else ""} ({res_m[mortos[0]][1][:100]})')




def julga(res):
    """Print each id; an expected failure that fails is XFAIL (listed), one that passes is XPASS (a failure), anything else that fails is a failure."""
    xfail = xpass = 0
    for i in CASOS + TEXTOS:
        st, det = res.get(i, ('FAIL', 'no result'))
        if i in XFAIL:
            if st == 'FAIL':
                xfail += 1
                print(f'  XFAIL {i}: {det}')
            else:
                xpass += 1
                confere(False, f'XPASS {i}: an expected failure passes; the bug has closed, take {i} out of XFAIL')
        else:
            if confere(st == 'PASS', f'{i} failed: {det}'):
                print(f'  ok    {i}')
    return xfail, xpass


def main():
    t0 = time.time()
    p, laco = pecas_das_fontes()
    chamadas = chamadas_do_tique()
    sem_tique = not p['tique.inc'].strip()
    with tempfile.TemporaryDirectory() as t:
        tmp = pathlib.Path(t)
        rc, out = roda(tmp, 'base', p, sem_tique)
        if rc is None:
            print('cannot build the model:\n' + out)
            return 2
        res, modelo = resultados(rc, out, p, laco, chamadas)
        MODELO[0], MODELO[1] = modelo
        tabela = textos(p, laco, chamadas)[1]
        print(f'the original\'s screen chain, read from ui/original/UiOriginal.h ({len(tabela)} branches): ' + ', '.join(f'{m[5:].lower()} -> {q}' for m, q in tabela))
        print('the base:')
        xfail, xpass = julga(res)
        confere(modelo[0] > 0 and len(re.findall(r'^CASE ', out, re.M)) == len(CASOS), f'the model ran {len(re.findall(r"^CASE ", out, re.M))} of {len(CASOS)} cases')
        if not FALHAS and not XFAIL:   # a mutant is judged against a base that is green
            mutantes(tmp, p, laco, chamadas, sem_tique)
    print(f'RESULT: {CONTA[0]} script checks, {MODELO[0]} model checks, {len(FALHAS)} failures, {xfail} expected failures (XFAIL), {xpass} XPASS, {time.time() - t0:.1f} s')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
