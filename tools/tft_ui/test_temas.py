#!/usr/bin/env python3
"""test_temas.py -- proves the Angulo's mode and accent on the glass (S10c, A'2 to A'2d) on the host.

In the Angulo a theme is the mode (light, dark, automatic) plus one accent of the palette; cfg.themeIndex holds both in one byte
(ua::escolhaDoIndice / ua::indiceDaEscolha, src/ui/angulo/Angulo.cpp). Builds tools/tft_ui/host/test_temas_main.cpp with the kit
and Adafruit GFX 1.12.6 (as test_cantos.py does) and runs it: the decode over all 256 int8_t values, ua::tema( ) and ua::desab( )
against the generated tables (17 roles x every entry x 2 modes), and the frame identity the glue hashes (two frames that differ
only in the accent hash differently and ask for a full repaint).

Then the glue is RUN (S10c, A'2c). Three reviews in a row (A'2, A'3, A'2b) found mutants of DisplayManager_UiA.cpp that passed every
check of the step, each outside what the test had cut out or read as text; A'2b and A'3b had linked a few pieces of the file. Now six whole
functions of it (uaTema, uaRelogio, uaPintar, uaEstado, uaQuadro, uiPaintOta) and the file's state are cut out verbatim, without their
comments, by brace matching, and tools/tft_ui/host/test_temas_glue_main.cpp compiles them against a stand-in for what they call (the
panel's push, the clock, the saved configuration) and the kit as it is, and runs them: every saved index, clock line and screen on show
through uaQuadro( ), the preview's scoping, the update screen, and the repaint decisions; what the glass must show is computed from the
generated tables, and every pixel of it is compared. A static rule over the file complements the run: the glue picks a palette in one
place, uaTema( ), so a paint site that reads ua::tema( ) or temaClaro( ) itself is refused. The exact-text checks of the earlier steps
stay as a second layer. ACENTOS_N is named only inside the two encoding functions and in static_assert statements, and the comment
above those functions names the files of those statements.

Then the instrument is bitten: the same program built with one mutant of the kit each (the decode by a modulo instead of the range
check, desab( ) and tema( ) stuck on entry 0, the identity or the full-repaint trigger without the accent, the table without its
accent field) must exit 1, so must the glue program for every mutant of GLUE_MUTANTES (the accent, the mode, the hour, the preview, the
hash, the clear, the margin or the mode on the glass dropped or taken from the wrong place), and so must it when one pixel of every
pushed strip is wrong; and the text checks must report a glue that drops the theme or the trigger.

The preview's scoping (S10c, A'3b): the preview reaches the glass on the Tema screen's frame alone. The colour names are held in
lower case after the colon of the words line and after the comma of the catalogue (ANGULO.md section 7).

S10c, A'2d (the review of A'3b and A'2c, which found ten mutants of the glue and three of the Tema touch passing): the stand-in's push is deferred, as the DMA is (a strip reaches the glass when
its push is waited for, from the strip as it is then, and no push may be in flight when uaPintar( ) returns), its screen draws bars across the margin that ua::margemPreta( ) blackens, the clock is
held at the bounds of the hour and of the tail, uaEstado( ) is read with the screen on show not the one on the glass, and the counters Core 0 waits on are asserted. escolhaNaTela( ) and the Tema
case of uaToqueMenus( ), in DisplayManager_UiA_Menus.cpp, are cut out the same way and run (tools/tft_ui/host/test_temas_toque_main.cpp), and the case is also held to its five statements by the
kit's text rule, ported, with its mutants as controls. A piece of either file that holds a preprocessor conditional is refused at the cut, and so is one that a conditional the test does not name
encloses.

Usage: python3 tools/tft_ui/test_temas.py   exit 0 when all hold, 1 when not, 2 when it cannot build.
"""
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import unicodedata

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)

KIT = ROOT / 'src' / 'ui' / 'angulo'
GLUE = KIT / 'DisplayManager_UiA.cpp'
MENUS_GLUE = KIT / 'DisplayManager_UiA_Menus.cpp'
MENUS_H = KIT / 'telas' / 'MenusConfig.h'
MENUS_CPP = KIT / 'telas' / 'MenusConfig.cpp'
TEMAS_CPP = KIT / 'Temas.cpp'
ANGULO = KIT / 'Angulo.cpp'
GLUE_MAIN = HERE / 'host' / 'test_temas_glue_main.cpp'
TOQUE_MAIN = HERE / 'host' / 'test_temas_toque_main.cpp'

CASOS = []


def caso(nome, problemas):
    if isinstance(problemas, bool):
        problemas = [] if problemas else ['false']
    CASOS.append((nome, not problemas))
    print(('ok   ' if not problemas else 'FAIL ') + nome + ('' if not problemas else ': ' + '; '.join(problemas[:4])), flush=True)


# Each mutant: (name, text it replaces in Angulo.cpp, what replaces it, the check that must notice). Every `old` is the kit's own line.
MUTANTES = [
    ('decode by modulo instead of the range check',
     '  if ( t < 0 || t >= 3 * entrega::ACENTOS_N ) return Escolha{ MODO_CLARO, 0 };\n',
     '  t = (int8_t)( t % ( 3 * entrega::ACENTOS_N ) );\n'),
    ('desab( ) takes every twin from entry 0',
     '[t.entrada < kAcentos ? t.entrada : 0];', '[0];'),
    ('tema( ) gives entry 0 for every accent',
     'return kPaleta.t[escuro ? 1 : 0][acento < kAcentos ? acento : 0];', 'return kPaleta.t[escuro ? 1 : 0][0 * acento];'),
    ('the table never records its accent',
     'e.perigoSuave, e.perigoTinta, escuro, entrada };', 'e.perigoSuave, e.perigoTinta, escuro, 0 * entrada };'),
    ('the identity leaves the accent out of the bytes',
     '(uint8_t)t.escuro, t.entrada, ox, oy };', '(uint8_t)t.escuro, 0, ox, oy };'),
    ('the full-repaint trigger leaves the accent out',
     ' || acento != antes.acento;', ';'),
]


# Two statements of uaQuadro( ) in the glue (S10c, A'3b): the one that drops the preview, and the line that returns while no look is due.
RESET = '  if ( _uiMode != MODE_SETTINGS_THEMES ) _uaPrevia = -1;\n'
OLHAR = '  if ( !_uaSujo && !_uaTudo && agora - _uaVisto < 250 ) return;\n'

# The glue's own statements about the theme (S10c, A'2b, run whole since A'2c): each entry is a change to DisplayManager_UiA.cpp that the glue program must fail. (name, the glue's text, what replaces it,
# how many times the text occurs; or, since A'2d, (name, [(text, replacement, count), ...]) for a mutant of several edits.) The first three are the mutants the review of A'2 found passing every check of A'2; the others were found by mutating the same statements.
GLUE_MUTANTES = [
    ('the accent never reaches the glass: uaTema( ) asks ua::tema( ) for entry 0', 'return ua::tema( escuro, e.acento );', 'return ua::tema( escuro, 0 );', 1),
    ('the dark mode inverted in uaTema( )', 'bool escuro = e.modo == ua::MODO_ESCURO;', 'bool escuro = e.modo != ua::MODO_ESCURO;', 1),
    ('both hashes cut the identity after 2 bytes, so the accent leaves them', 'h = fnv( &id, sizeof id, h );', 'h = fnv( &id, 2, h );', 2),
    ('the update screen hash cuts the identity after 2 bytes', '(uint8_t)MODE_OTA_UPDATE, tema, 0, 0 );\n  h = fnv( &id, sizeof id, h );',
     '(uint8_t)MODE_OTA_UPDATE, tema, 0, 0 );\n  h = fnv( &id, 2, h );', 1),
    ('the paint hash cuts the identity after 2 bytes', '(uint8_t)_uiMode, tema, ox, oy );\n  h = fnv( &id, sizeof id, h );',
     '(uint8_t)_uiMode, tema, ox, oy );\n  h = fnv( &id, 2, h );', 1),
    ('the identity restarts the hash and drops the view model', 'h = fnv( &id, sizeof id, h );', 'h = fnv( &id, sizeof id );', 2),
    ('the view model leaves the hash', 'uint32_t h = fnv( q.vm, q.tam );', 'uint32_t h = fnv( q.vm, 0 );', 2),
    ('the paint identity swaps ox and oy', 'tema, ox, oy );', 'tema, oy, ox );', 1),
    ('the paint identity leaves the screen out', '(uint8_t)_uiMode, tema, ox, oy )', '0, tema, ox, oy )', 1),
    ('the automatic mode inverted', 'if ( e.modo == ua::MODO_AUTOMATICO )', 'if ( e.modo != ua::MODO_AUTOMATICO )', 1),
    ('dark from 20:00 instead of 19:00', '( h >= 19 || h < 7 )', '( h >= 20 || h < 7 )', 1),
    ('dark only until 5:59 instead of 6:59', '( h >= 19 || h < 7 )', '( h >= 19 || h < 6 )', 1),
    ('dark only where both bounds hold', '( h >= 19 || h < 7 )', '( h >= 19 && h < 7 )', 1),
    ('no clock reads as night', 'uaRelogio( hora, h, provisoria ) && (', 'uaRelogio( hora, h, provisoria ) || (', 1),
    ('the preview of index 0 is ignored', '_uaPrevia >= 0 )', '_uaPrevia > 0 )', 1),
    ('every frame takes the preview, not the Tema screen alone', '( modo == MODE_SETTINGS_THEMES && _uaPrevia >= 0 )', '( (void)modo, _uaPrevia >= 0 )', 1),
    ('the saved theme is ignored', '_uaPrevia : _sysConfigPtr ? _sysConfigPtr->themeIndex : 0 )', '_uaPrevia : 0 )', 1),
    # S10c, A'3b: the preview's scoping (the review of A'3: M1 uaTema( ) ignores the frame's mode, M2 the update screen asks for the screen on show's theme, M3 the reset deleted)
    ('uaTema( ) asks the screen on show, not the frame\'s mode, whether the preview applies', '( modo == MODE_SETTINGS_THEMES && _uaPrevia >= 0 )', '( _uiMode == MODE_SETTINGS_THEMES && _uaPrevia >= 0 )', 1),
    ('the update screen takes the theme of the screen on show', 'const ua::Tema &tema = uaTema( MODE_OTA_UPDATE );', 'const ua::Tema &tema = uaTema( _uiMode );', 1),
    ('the preview is dropped on the Tema screen and kept elsewhere', 'if ( _uiMode != MODE_SETTINGS_THEMES ) _uaPrevia = -1;', 'if ( _uiMode == MODE_SETTINGS_THEMES ) _uaPrevia = -1;', 1),
    ('the preview is reset to 0 and not to none', 'MODE_SETTINGS_THEMES ) _uaPrevia = -1;', 'MODE_SETTINGS_THEMES ) _uaPrevia = 0;', 1),
    ('the reset of the preview is deleted', RESET, '', 1),
    # S10c, A'2c: the paint path. The first six are what the review of A'2b found passing (N1, N3, N4) and two of its other mutants (N5, N6); the others are the step's own
    ('the accent never reaches the glass at the paint site: the Pintor takes ua::tema( dark, 0 )', 'ua::Pintor p( faixa, tema, faixa );', 'ua::Pintor p( faixa, ua::tema( tema.escuro, 0 ), faixa );', 1),
    ("the strips are cleared to the light theme's background in every theme", 'faixa.fillScreen( tema.fundo );', 'faixa.fillScreen( ua::temaClaro( ).fundo );', 1),
    ("the screen's mode on the glass is always light", 's_escuroNaTela = tema.escuro;', 's_escuroNaTela = false;', 1),
    ('the hour of the real clock is read one hour late', "h = ( hora[0] - '0' ) * 10 + ( hora[1] - '0' );", "h = ( hora[0] - '0' ) * 10 + ( hora[1] - '0' ) + 1;", 1),
    ("the hash recorded with the frame on the glass is another frame's", 'if ( uaPintar( q, _uiMode, h ) ) {', 'if ( uaPintar( q, _uiMode, h + 1 ) ) {', 1),
    ('the strips are cleared to the surface colour, not the background', 'faixa.fillScreen( tema.fundo );', 'faixa.fillScreen( tema.superficie );', 1),
    ("the frame on the glass is recorded for the screen on show, not the frame's mode", 's_modo = modo;', 's_modo = _uiMode;', 1),
    ('the strip hashes are not kept, so a later paint pushes every strip', '    s_faixa[k] = h;\n', '', 1),
    ('a full repaint is not taken: the flag stays up after the paint', '  _uaTudo = false;\n  s_seq = s_seq + 1;', '  s_seq = s_seq + 1;', 1),
    ('the mode on the glass comes from the screen on show, not from the frame painted', 's_escuroNaTela = tema.escuro;', 's_escuroNaTela = uaTema( _uiMode ).escuro;', 1),
    ('the margin is not blackened after the screen', '    ua::margemPreta( p );\n', '', 1),
    ('a full repaint leaves the alignment band as it was', '    _driver.tft->fillMarginsBlack( );\n', '', 1),
    # S10c, A'2d: what the review of A'3b and A'2c found passing (its P1 to P5, P10, P17, P18, P22, P23) and the mutants made of the same statements. A pair (name, [edits]) is a mutant of several edits.
    ('the margin is blackened before the screen is drawn, not after it',
     [('    ua::margemPreta( p );\n', '', 1), ('    if ( p.gravado( ) ) p.repetir( ); else q.desenhar( p, q.vm );\n', '    ua::margemPreta( p );\n    if ( p.gravado( ) ) p.repetir( ); else q.desenhar( p, q.vm );\n', 1)]),
    ('a strip is composed again in the half of the canvas whose push is in flight, which is not waited for', '      uaEmpurrarEsperar( );\n      voando = nullptr;\n', '      voando = nullptr;\n', 1),
    ('the last push is not waited for before uaPintar( ) returns', "  uaEmpurrarEsperar( );\n  /* The canvas's spare rows go back", "  /* The canvas's spare rows go back", 1),
    ('the hours up to 29 are a clock', '  return h >= 0 && h < 24;', '  return h >= 0 && h < 30;', 1),
    ('the hour 23 is no clock', '  return h >= 0 && h < 24;', '  return h >= 0 && h < 23;', 1),
    ('an hour of -1 is a clock', '  return h >= 0 && h < 24;', '  return h >= -1 && h < 24;', 1),
    ('a tail of 4 characters after the separator is a clock', 'strlen( sep + 3 ) < 5', 'strlen( sep + 3 ) < 4', 1),
    ('a tail of exactly 5 characters after the separator is no clock', 'strlen( sep + 3 ) < 5', 'strlen( sep + 3 ) < 6', 1),
    ('the provisional separator is not seen', 'provisoria = sep != nullptr;', 'provisoria = false;', 1),
    ("uaEstado( ) reports the glass's screen as the one on show", '    e.modoAtual = (uint8_t)_uiMode;', '    e.modoAtual = (uint8_t)s_modo;', 1),
    ("uaEstado( ) reports the screen on show as the glass's", '    e.modo = (uint8_t)s_modo;', '    e.modo = (uint8_t)_uiMode;', 1),
    ('the boot counts a frame of any screen', 'if ( _lastRenderedState.isBooting && _uiMode == MODE_DASHBOARD ) _bootPaintSeq++;', 'if ( _lastRenderedState.isBooting ) _bootPaintSeq++;', 1),
    ('the boot counts no frame', '&& _uiMode == MODE_DASHBOARD ) _bootPaintSeq++;', '&& _uiMode == MODE_DASHBOARD ) _bootPaintSeq += 0;', 1),
    ('the heartbeat is fed on Core 0 and not on Core 1', 'if ( get_core_num( ) == 1 ) _lastHeartbeat = millis( );', 'if ( get_core_num( ) == 0 ) _lastHeartbeat = millis( );', 1),
    ('the heartbeat is never fed', 'if ( get_core_num( ) == 1 ) _lastHeartbeat = millis( );', 'if ( get_core_num( ) == 7 ) _lastHeartbeat = millis( );', 1),
    ('the early return of the update screen does not say that the sequence is on the glass', '    __atomic_store_n( &_otaDrawnSeq, seq, __ATOMIC_RELEASE );\n    return;', '    return;', 1),
    ('the early return of the update screen is taken for a new sequence, not for the same one', 'if ( seq == s_otaSeq && !_otaRedrawAll', 'if ( seq != s_otaSeq && !_otaRedrawAll', 1),
    ('the percentage drawn of the update screen is left at 0', '  _otaPctDrawn = __atomic_load_n( &_otaPct, __ATOMIC_ACQUIRE );', '  _otaPctDrawn = 0;', 1),
    # S10c, A'2e: what the review of A'2d and A'6 found passing: the alignment band blackened while a push is in flight (its O1a, O1b: the library's fill is
    # synchronous and needs the bus free), and compose and push made to run one after the other (its O2, O5: the same pixels, only the count of waits sees it).
    ('the alignment band is blackened inside the first burst, with a push in flight', [('    _driver.tft->fillMarginsBlack( );\n', '', 1), ('    s_faixa[k] = h;\n    enviadas++;\n', '    s_faixa[k] = h;\n    enviadas++;\n    if ( tudo && k == 0 ) {\n    _driver.tft->fillMarginsBlack( );\n    }\n', 1)]),
    ('the alignment band is blackened just before the last wait, with a push in flight', [('    _driver.tft->fillMarginsBlack( );\n', '', 1), ("  uaEmpurrarEsperar( );\n  /* The canvas's spare rows go back", "  if ( tudo ) {\n    _driver.tft->fillMarginsBlack( );\n  }\n  uaEmpurrarEsperar( );\n  /* The canvas's spare rows go back", 1)]),
    ('every push is waited for at once, so compose and push no longer overlap', '    voando = uaEmpurrar( buf, 0, (int16_t)( k * ALTURA_FAIXA ), LARGURA_FAIXA, ALTURA_FAIXA ) ? buf : nullptr;\n', '    voando = uaEmpurrar( buf, 0, (int16_t)( k * ALTURA_FAIXA ), LARGURA_FAIXA, ALTURA_FAIXA ) ? buf : nullptr;\n    uaEmpurrarEsperar( );\n    voando = nullptr;\n', 1),
    ('the wait before composing is taken when the push in flight reads the other half', '    if ( voando == buf ) {\n', '    if ( voando && voando != buf ) {\n', 1),
]


def compila(src, obj, inc, estrito):
    r = subprocess.run(['g++', *inc, *(['-Wall', '-Wextra', '-Werror'] if estrito else ['-w']), '-c', str(src), '-o', str(obj)],
                       capture_output=True, text=True)
    return r.returncode, r.stderr


def constroi(tmp, nome, texto_angulo, gfx_obj, main_obj, inc):
    """The program with Angulo.cpp as given (None = the file itself). Returns the executable, or None and the compiler's words."""
    d = tmp / nome
    d.mkdir()
    fonte = ANGULO
    if texto_angulo is not None:
        fonte = d / 'Angulo.cpp'
        fonte.write_text(texto_angulo, encoding='utf-8')
    rc, err = compila(fonte, d / 'Angulo.o', inc, True)
    if rc:
        return None, err
    exe = d / 'temas'
    r = subprocess.run(['g++', str(gfx_obj), str(d / 'Angulo.o'), str(main_obj), '-o', str(exe)], capture_output=True, text=True)
    return (exe, '') if r.returncode == 0 else (None, r.stderr)


def corre(exe):
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    return r.returncode, r.stdout


def apaga_comentarios(t):
    """`t` with every comment turned into blanks, the newlines kept, so that offsets and lines stay: a string or a character literal is skipped, not read as a comment."""
    saida, i, n = [], 0, len(t)
    while i < n:
        c = t[i]
        if t.startswith('//', i):
            j = t.find('\n', i)
            j = n if j < 0 else j
            saida.append(' ' * (j - i))
            i = j
        elif t.startswith('/*', i):
            j = t.find('*/', i + 2)
            j = n if j < 0 else j + 2
            saida.append(''.join(ch if ch == '\n' else ' ' for ch in t[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            saida.append(t[i:j + 1])
            i = j + 1
        else:
            saida.append(c)
            i += 1
    return ''.join(saida)


def fim_do_bloco(t, abre):
    """The index just after the `}` that closes the `{` at t[abre], -1 when none does. `t` has no comments."""
    d, i, n = 0, abre, len(t)
    while i < n:
        c = t[i]
        if c in '"\'':
            i += 1
            while i < n and t[i] != c:
                i += 2 if t[i] == '\\' else 1
        elif c == '{':
            d += 1
        elif c == '}':
            d -= 1
            if d == 0:
                return i + 1
        i += 1
    return -1


def declaracoes(t):
    """The top-level statements of a namespace's body, as (start, end, text). One ends at a `;` outside every bracket, or at the `}` that closes a function body (a `}` followed by `;` ends a struct or an array)."""
    saida, ini, d, i, n = [], 0, 0, 0, len(t)
    while i < n:
        c = t[i]
        if c in '"\'':
            i += 1
            while i < n and t[i] != c:
                i += 2 if t[i] == '\\' else 1
        elif c in '{([':
            d += 1
        elif c in '})]':
            d -= 1
            if d == 0 and c == '}':
                j = i + 1
                while j < n and t[j] in ' \t\r\n':
                    j += 1
                if j >= n or t[j] != ';':
                    saida.append((ini, i + 1, t[ini:i + 1]))
                    ini = i + 1
        elif c == ';' and d == 0:
            saida.append((ini, i + 1, t[ini:i + 1]))
            ini = i + 1
        i += 1
    if t[ini:].strip():
        saida.append((ini, n, t[ini:]))
    return saida


PECAS_DO_GLUE = ('uaTema', 'uaRelogio', 'uaPintar', 'uaEstado', 'uaQuadro', 'uiPaintOta')   # the theme and paint path; the file name of each is its name in lower case, .inc


def estado_do_glue(limpo):
    """The anonymous namespace(s) of the glue minus the table of Core 0's messages (the one thing in them that needs the language keys): the strip constants, the frame on the glass, the sequence counter and fnv( ).
    Returns (the text, [(start, end)] of each declaration kept in the glue's text), or (None, [])."""
    corpos, posicoes = [], []
    for m in re.finditer(r'^namespace \{', limpo, re.M):
        f = fim_do_bloco(limpo, m.end() - 1)
        if f > 0:
            mantidas = [(m.end() + a, m.end() + b, s) for a, b, s in declaracoes(limpo[m.end():f - 1]) if 'TextoMsg' not in s]
            corpos.append(''.join(s for _, _, s in mantidas))
            posicoes += [(a, b) for a, b, _ in mantidas]
    return ('\n'.join(corpos), posicoes) if corpos else (None, [])


def posicao_da_definicao(limpo, cabeca):
    """(start, end) of the function definition whose head matches the regex `cabeca` (written to begin at a line's start), from the start of its line to its closing brace; None when the text holds it zero times or several, or
    only as a declaration. `limpo` has no comments."""
    achados = list(re.finditer(cabeca, limpo, re.M))
    if len(achados) != 1:
        return None
    m = achados[0]
    i, d = m.end() - 1, 0
    while i < len(limpo):
        d += (limpo[i] == '(') - (limpo[i] == ')')
        i += 1
        if d == 0:
            break
    k = limpo.find('{', i)
    if k < 0 or ';' in limpo[i:k]:   # a declaration, not a definition
        return None
    f = fim_do_bloco(limpo, k)
    return (m.start(), f) if f > 0 else None


def funcao_do_glue(limpo, nome):
    """The definition of DisplayManager::<nome>( ), from the start of its line to its closing brace; None when the glue holds it zero times or several. `limpo` has no comments."""
    pos = posicao_da_definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::' + re.escape(nome) + r'\s*\(')
    return limpo[pos[0]:pos[1]] + '\n' if pos else None


# ---------------------------------------------------------------------------------------------------------------------------------- the cut and the preprocessor (S10c, A'2d)
# The review of A'2c: the cut reads the text and ignores the preprocessor, so an `#if 0` around a function (the host program would still compile and run it) and an `#ifdef` inside one (it would run one branch of two) passed.
# A piece the host programs compile is refused when it holds a conditional (`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`), whichever way the preprocessor would go on the firmware; and the conditionals that
# enclose it in its file must be the ones this list names, by piece. Read from DisplayManager_UiA.cpp and DisplayManager_UiA_Menus.cpp on 2026-10-22: nothing encloses any piece (the first file has no conditional at all; the second
# has `#if SIMUT_NET_LAN` ... `#endif` at lines 229 to 231, in the status screen's builder, which no piece includes). A conditional that comes to enclose a piece is added here, in the same change, with the reason it is safe to
# run the piece without it.
CONDICIONAIS_QUE_ENVOLVEM = {
    'the file-level state': [],
    'DisplayManager::uaTema( )': [],
    'DisplayManager::uaRelogio( )': [],
    'DisplayManager::uaPintar( )': [],
    'DisplayManager::uaEstado( )': [],
    'DisplayManager::uaQuadro( )': [],
    'DisplayManager::uiPaintOta( )': [],
    'escolhaNaTela( )': [],
    'DisplayManager::uaToqueMenus( ), the statements before its switch': [],
    'DisplayManager::uaToqueMenus( ), case MODE_SETTINGS_THEMES': [],
    'DisplayManager::uaMontarMenus( ), case MODE_SETTINGS_THEMES': [],
}
DIRETIVA = re.compile(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b[^\n]*', re.M)


def diretivas(limpo):
    """The preprocessor conditionals of a text without comments, in order: [(offset, line, kind, text)]. The newlines of a comment are kept, so the line is the file's."""
    return [(m.start(), limpo.count('\n', 0, m.start()) + 1, m.group(1), ' '.join(m.group(0).split())) for m in DIRETIVA.finditer(limpo)]


def abertas_em(limpo, pos):
    """The conditional groups open at offset `pos`, outermost first: [text, line] of each, the text being its `#if` and, after an `#elif` or `#else`, the chain (`#if X / #else`)."""
    pilha = []
    for ini, linha, tipo, texto in diretivas(limpo):
        if ini >= pos:
            break
        if tipo in ('if', 'ifdef', 'ifndef'):
            pilha.append([texto, linha])
        elif tipo in ('elif', 'else') and pilha:
            pilha[-1][0] += ' / ' + texto
        elif tipo == 'endif' and pilha:
            pilha.pop()
    return pilha


def problemas_de_preprocessador(limpo, pecas, nomeado=None):
    """[problem] for the pieces `pecas` ([(label, start, end)] in `limpo`, the file's text without comments): a conditional inside a piece, or a conditional enclosing it that the list `nomeado` (default
    CONDICIONAIS_QUE_ENVOLVEM) does not name for its label. Each problem names the piece and the line."""
    nomeado = CONDICIONAIS_QUE_ENVOLVEM if nomeado is None else nomeado
    p = []
    todas = diretivas(limpo)
    for rotulo, ini, fim in pecas:
        a = ini + len(limpo[ini:fim]) - len(limpo[ini:fim].lstrip())   # its first visible character
        l0, l1 = limpo.count('\n', 0, a) + 1, limpo.count('\n', 0, fim) + 1
        for d_ini, d_linha, _, d_texto in todas:
            if a <= d_ini < fim:
                p.append(f'{rotulo} (lines {l0} to {l1}) holds `{d_texto}` at line {d_linha}: the host program compiles the text between its braces whichever way the preprocessor goes on the firmware')
        abertas = abertas_em(limpo, a)
        if [t for t, _ in abertas] != nomeado.get(rotulo, []):
            quem = ', '.join(f'`{t}` (opened at line {k})' for t, k in abertas) or 'no conditional'
            p.append(f'{rotulo} (line {l0}) is enclosed by {quem}, and the test names {nomeado.get(rotulo, [])!r} (CONDICIONAIS_QUE_ENVOLVEM): the host program runs it whichever way the preprocessor goes on the firmware')
    return p


def recorta_glue(glue, nomeado=None):
    """The pieces of the glue that the host program compiles (S10c, A'2c): {file name: text}, and what could not be found (or is refused by the preprocessor rule of A'2d). Whole functions, verbatim and without their comments."""
    limpo = apaga_comentarios(glue)
    pecas, falta, posicoes = {}, [], []
    estado, pos_estado = estado_do_glue(limpo)
    if not estado or 'fnv(' not in estado or 's_escuroNaTela' not in estado or 'TextoMsg' in estado:
        falta.append('estado.inc: the anonymous namespace with fnv( ) and the state of the frame on the glass was not found in DisplayManager_UiA.cpp')
    else:
        pecas['estado.inc'] = estado
        posicoes += [('the file-level state', a, b) for a, b in pos_estado]
    for nome in PECAS_DO_GLUE:
        pos = posicao_da_definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::' + re.escape(nome) + r'\s*\(')
        if pos is None:
            falta.append(f'{nome.lower()}.inc: DisplayManager::{nome}( ) was not found exactly once, as a definition, in DisplayManager_UiA.cpp')
        else:
            pecas[nome.lower() + '.inc'] = limpo[pos[0]:pos[1]] + '\n'
            posicoes.append((f'DisplayManager::{nome}( )', *pos))
    return pecas, falta + problemas_de_preprocessador(limpo, posicoes, nomeado)


def constroi_glue(tmp, nome, glue, angulo_o, gfx_obj, inc, extra=()):
    """The host program of the glue's functions (S10c, A'2c): cuts them out of `glue`, compiles test_temas_glue_main.cpp around them (`extra`: more compiler flags) and links it with the kit's object.
    Returns (executable, '') or (None, why). No -Werror: a mutant that leaves a name unused is judged by what it does, not by the compiler."""
    pecas, falta = recorta_glue(glue)
    if falta:
        return None, 'cut: ' + '; '.join(falta)
    d = tmp / nome
    d.mkdir()
    for arq, texto in pecas.items():
        (d / arq).write_text(texto, encoding='utf-8')
    rc, err = compila(GLUE_MAIN, d / 'glue_main.o', inc + [f'-I{d}', *extra], False)
    if rc:
        return None, err
    exe = d / 'glue'
    r = subprocess.run(['g++', str(gfx_obj), str(angulo_o), str(d / 'glue_main.o'), '-o', str(exe)], capture_output=True, text=True)
    return (exe, '') if r.returncode == 0 else (None, r.stderr)


def problemas_do_glue(glue):
    """The glue's side of the hash: [problem]."""
    p = []
    if 'extra[' in glue:
        p.append('an extra[ ] array is left in the glue: the frame identity must come from ua::identidadeDoQuadro( )')
    chamadas = re.findall(r'ua::identidadeDoQuadro\( \(uint8_t\)(\w+), (\w+), ', glue)
    if sorted(chamadas) != [('MODE_OTA_UPDATE', 'tema'), ('_uiMode', 'tema')]:
        p.append(f'ua::identidadeDoQuadro( ) must be called twice, for _uiMode and for MODE_OTA_UPDATE, with the theme uaTema( ) gave: {chamadas}')
    if len(re.findall(r'\.repintaTudo\( s_id \)', glue)) != 1 or len(re.findall(r'\bs_id = id;', glue)) != 1:
        p.append('the full-repaint trigger must be id.repintaTudo( s_id ) and s_id = id; must follow a painted frame')
    if 'ua::escolhaDoIndice(' not in glue or re.search(r'escolha == [12]\b', glue):
        p.append('uaTema( ) must decode themeIndex with ua::escolhaDoIndice( ), not compare it with 1 and 2')
    return p


GATILHO = '  if ( id.repintaTudo( s_id ) ) _uaTudo = true;\n'
PULO = '  if ( !_uaTudo && s_temQuadro && h == s_hash && s_modo == _uiMode ) return;\n'


def problemas_do_glue_exato(glue):
    """The statements of the glue that no run reaches, held to their exact text (S10c, A'2b): the mode each call of uaTema( ) takes, and the full-repaint trigger, its place and
    the record of the frame it compares with, the update screen's test of the hash, the alignment the identity hashes, and (S10c, A'3b) the statement that drops the preview, its place and who may name it."""
    p = []
    g = sem_comentarios(glue)
    chamadas = re.findall(r'const ua::Tema &tema = uaTema\( (\w+) \);', g)
    if chamadas != ['modo', '_uiMode', 'MODE_OTA_UPDATE']:
        p.append(f'the three frames must take their theme with uaTema( modo ) (uaPintar( )), uaTema( _uiMode ) (the paint identity) and uaTema( MODE_OTA_UPDATE ) (the update screen), in that order: {chamadas}')
    if len(re.findall(r'(?<![:\w])uaTema\( [^)]', g)) != 3:
        p.append('uaTema( ) is called from somewhere other than those three declarations')
    if len(re.findall(r'^  if \( id\.repintaTudo\( s_id \) \) _uaTudo = true;$', g, re.M)) != 1:
        p.append('the trigger must be the statement `if ( id.repintaTudo( s_id ) ) _uaTudo = true;`, once')
    i, j = g.find('id.repintaTudo( s_id )'), g.find('if ( !_uaTudo && s_temQuadro && h == s_hash && s_modo == _uiMode ) return;')
    if not 0 <= i < j:
        p.append('the full-repaint trigger must come before the line that skips an unchanged frame')
    if g.count('s_id = id;') != 1 or not re.search(r'if \( uaPintar\( q, _uiMode, h \) \) \{[^{}]*\bs_id = id;\s*\}', g):
        p.append('s_id = id; must be written once, as the last statement of the branch that follows a painted frame')
    if g.count('if ( _uaTudo || !s_temQuadro || h != s_hash || s_modo != MODE_OTA_UPDATE ) {') != 1:
        p.append('the update screen must repaint on a changed hash, `if ( _uaTudo || !s_temQuadro || h != s_hash || s_modo != MODE_OTA_UPDATE ) {`, written once: the hash is its only trigger for the accent')
    if g.count('const int8_t ox = _driver.tft->getOffsetX( ), oy = _driver.tft->getOffsetY( );') != 1:
        p.append('the alignment the identity hashes must be read as `const int8_t ox = _driver.tft->getOffsetX( ), oy = _driver.tft->getOffsetY( );`, once')
    # S10c, A'3b: the preview reaches the glass on the Tema screen's frame alone. uaTema( modo ) takes it for that frame only (the link runs it); these keep every other path from
    # reading or keeping it
    if g.count('if ( _uiMode != MODE_SETTINGS_THEMES ) _uaPrevia = -1;') != 1:
        p.append('uaQuadro( ) must drop the preview with `if ( _uiMode != MODE_SETTINGS_THEMES ) _uaPrevia = -1;`, once')
    ir, jo = g.find('_uaPrevia = -1;'), g.find('if ( !_uaSujo && !_uaTudo && agora - _uaVisto < 250 ) return;')
    if not 0 <= ir < jo:
        p.append('the preview must be dropped before the line that returns while no look is due: after it, a frame of another mode would keep the preview until the next look')
    nomes = len(re.findall(r'\b_uaPrevia\b', g))
    if nomes != 3:
        p.append(f'_uaPrevia must be named three times in DisplayManager_UiA.cpp (uaTema( ) reads it twice, uaQuadro( ) drops it once), not {nomes}: no other statement may read or keep it')
    pinta = re.findall(r'\buaPintar\( q, (\w+), h \)', g)
    if pinta != ['_uiMode', 'MODE_OTA_UPDATE']:
        p.append(f'uaPintar( ) must be called for _uiMode (uaQuadro( )) and for MODE_OTA_UPDATE (uiPaintOta( )), in that order: {pinta}')
    return p


def problemas_de_leitura_do_tema(glue):
    """S10c, A'2c, the static half of the paint path's rule: the glue picks a palette in one place, DisplayManager::uaTema( ). A paint site that reads ua::tema( ), temaClaro( ) or temaEscuro( ) itself, or sets a
    Pintor's theme with p.tema( ), is refused whatever it passes: the run proves the sites the glue has, this keeps a new one from existing."""
    g = apaga_comentarios(glue)
    f = funcao_do_glue(g, 'uaTema')
    if f is None:
        return ['DisplayManager::uaTema( ) was not found exactly once, as a definition']
    resto = g.replace(f[:-1], '')
    p = [f'the glue reads the palette outside uaTema( ): `{m.group(0)}` in `{resto[max(0, m.start() - 24):m.end() + 24].strip()}`'
         for m in re.finditer(r'\b(?:ua::)?(?:tema|temaClaro|temaEscuro)\s*\(', resto)]
    n = len(re.findall(r'\bua::tema\s*\(', f))
    if n != 1:
        p.append(f'uaTema( ) must read ua::tema( ) once, not {n} times')
    return p


def acentos_n_fora(angulo, outros):
    """ACENTOS_N must appear only inside escolhaDoIndice and indiceDaEscolha, in static_assert statements (S10c, A'3: the catalogue and the Tema screen tie their sizes to the
    palette's) and in the generated header that defines it."""
    p = []
    dentro = 0
    for nome in ('Escolha escolhaDoIndice', 'int8_t indiceDaEscolha'):
        m = re.search(r'^' + re.escape(nome) + r'\(.*?\n^\}', angulo, re.S | re.M)
        if not m:
            p.append(f'{nome}( ) not found in Angulo.cpp')
            continue
        dentro += m.group(0).count('ACENTOS_N')
        angulo = angulo.replace(m.group(0), '')
    if dentro < 2:
        p.append(f'ACENTOS_N is read {dentro} time(s) inside the two encoding functions')
    for nome, texto in [('Angulo.cpp', angulo)] + outros:
        if 'ACENTOS_N' in re.sub(r'static_assert\s*\([^;]*;', '', sem_comentarios(texto)):   # a comment names it, it reads nothing
            p.append(f'ACENTOS_N appears in {nome} outside the two encoding functions')
    return p


def leitores_nomeados(angulo, outros):
    """S10c, A'2c: the comment above the two encoding functions says who else reads ACENTOS_N, and it is checked against the sources: it names every file that does (the static_assert statements of A'3 in Temas.cpp and
    MenusConfig.cpp) and no file that does not."""
    m = re.search(r'^// The two encoding functions:.*?(?=^Escolha escolhaDoIndice)', angulo, re.S | re.M)
    if not m:
        return ['the comment above escolhaDoIndice( ), "// The two encoding functions: ...", was not found']
    comentario = m.group(0)
    leem = sorted({pathlib.PurePosixPath(n).name for n, t in outros if 'ACENTOS_N' in sem_comentarios(t)})
    p = [f'{f} reads ACENTOS_N and the comment above the encoding functions does not name it' for f in leem if f not in comentario]
    p += [f'the comment above the encoding functions names {f}, which does not read ACENTOS_N' for f in sorted(set(re.findall(r'\b\w+\.(?:cpp|h)\b', comentario))) if f not in leem]
    return p


def sem_comentarios(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def pedaco(texto, ini, fim):
    i = texto.find(ini)
    j = texto.find(fim, i + len(ini)) if i >= 0 else -1
    return texto[i:j] if i >= 0 and j > i else None


def intervalos_do_caso(texto, rotulo='case MODE_SETTINGS_THEMES: {'):
    """(start, end) of each braced body that begins with `rotulo` in a text: from the label to the closing brace."""
    out, i = [], 0
    while True:
        i = texto.find(rotulo, i)
        if i < 0:
            return out
        k, d = texto.index('{', i), 0
        for j in range(k, len(texto)):
            d += (texto[j] == '{') - (texto[j] == '}')
            if d == 0:
                break
        out.append((i, j + 1))
        i = j


def blocos_do_caso(texto, rotulo='case MODE_SETTINGS_THEMES: {'):
    """The braced bodies of `case MODE_SETTINGS_THEMES: {` in a file (the builder's and the touch's)."""
    return [texto[a:b] for a, b in intervalos_do_caso(texto, rotulo)]


def problemas_de_literais(mh, mc, menus, ua):  # the 6 is not looked for in uaTema( ): its `char hora[6]` is the clock's text
    """S10c, A'3, brief restriction 3: the number of modes and the palette's size are written only in the encoding functions and in static_assert statements. The Tema
    screen, its touch and its builder, and uaTema( ) carry no bare 3 or 6 (comments, strings and static_asserts apart)."""
    p = []
    blocos = [('MenusConfig.h', pedaco(mh, 'enum EscolhaTema', '/* Idioma (MODE_SETTINGS_LANG)'), '36'), ('MenusConfig.cpp', pedaco(mc, '/* Tema:', '/* Idioma:'), '36'),
              ('DisplayManager_UiA.cpp, uaTema( )', pedaco(ua, 'const ua::Tema &DisplayManager::uaTema(', '/* "HH:MM"'), '3')]
    g = blocos_do_caso(menus)
    if len(g) != 2:
        p.append(f'DisplayManager_UiA_Menus.cpp has {len(g)} bodies of case MODE_SETTINGS_THEMES: {{, expected 2 (the builder and the touch)')
    blocos += [(f'DisplayManager_UiA_Menus.cpp, body {n + 1}', b, '36') for n, b in enumerate(g)]
    for nome, b, algarismos in blocos:
        if b is None:
            p.append(f'{nome}: the block was not found')
            continue
        b = re.sub(r'static_assert\s*\([^;]*;', '', sem_comentarios(b))
        b = re.sub(r'"(?:\\.|[^"\\])*"', '""', b)
        for m in re.finditer(r'(?<![\w.])[' + algarismos + r'](?![\w.])', b):
            p.append(f'{nome}: a bare {m.group(0)} in `{b[max(0, m.start() - 24):m.end() + 12].strip()}`')
    return p


def pedacos_do_catalogo(cat, mh):
    rows = re.findall(r'TEMA\(\s*"([^"]*)"\s*,\s*"([^"]*)"\s*\)', cat)
    cores = re.findall(r'"([^"]*)"', pedaco(mh, 'TEMA_CORES[] =', '};') or '')
    modos = re.findall(r'"([^"]*)"', pedaco(mh, 'TEMA_MODOS[TM_N] =', '};') or '')
    return rows, cores, modos


def problemas_do_catalogo(cat, mh):
    """The 18 ids and names of Temas.cpp: each row at the index ua::escolhaDoIndice( ) gives it (mode t % 3, accent t / 3), the stock accent's three unchanged, an id at most
    15 B, a name at most 23 B of UTF-8, none repeated, and the static_assert that ties the count to the palette."""
    p = []
    rows, cores, modos = pedacos_do_catalogo(cat, mh)
    ids = ['claro', 'escuro', 'auto']
    if len(modos) != 3 or len(cores) < 2:
        return [f'the screen header gives {len(modos)} modes and {len(cores)} accents']
    if len(rows) != 3 * len(cores):
        p.append(f'{len(rows)} rows, expected {3 * len(cores)} (three modes by {len(cores)} accents)')
    for t, (i, n) in enumerate(rows):
        m, k = t % 3, t // 3
        if k >= len(cores):
            break
        chave = unicodedata.normalize('NFKD', cores[k]).encode('ascii', 'ignore').decode().lower()
        want = (ids[m], modos[m]) if k == 0 else (f'{ids[m]}-{chave}', f'{modos[m]}, {cores[k]}')
        if (i, n) != want:
            p.append(f'row {t} is ({i!r}, {n!r}), expected {want}')
        if len(i.encode('utf-8')) > 15 or not re.fullmatch(r'[a-z]+(-[a-z]+)?', i):
            p.append(f'row {t}: the id {i!r} is {len(i.encode("utf-8"))} B or not lower-case ASCII (at most 15 B)')
        if len(n.encode('utf-8')) > 23:
            p.append(f'row {t}: the name {n!r} is {len(n.encode("utf-8"))} B of UTF-8 (at most 23)')
    if len(set(i for i, _ in rows)) != len(rows) or len(set(n for _, n in rows)) != len(rows):
        p.append('an id or a name is repeated')
    if not re.search(r'static_assert\(\s*NUM_TEMAS\s*==\s*3\s*\*\s*entrega::ACENTOS_N', cat):
        p.append('Temas.cpp lacks static_assert( NUM_TEMAS == 3 * entrega::ACENTOS_N )')
    return p


def problemas_da_previa_nos_menus(menus):
    """S10c, A'3b: in the menus glue the preview is named only inside the two bodies of `case MODE_SETTINGS_THEMES: {` (the builder's and the touch's): no other screen reads, shows or sets it."""
    p = []
    g = sem_comentarios(menus)
    corpos = blocos_do_caso(g)
    total = len(re.findall(r'\b_uaPrevia\b', g))
    dentro = sum(len(re.findall(r'\b_uaPrevia\b', c)) for c in corpos)
    if len(corpos) != 2:
        p.append(f'DisplayManager_UiA_Menus.cpp has {len(corpos)} bodies of case MODE_SETTINGS_THEMES: {{, expected 2')
    if total == 0 or total != dentro:
        p.append(f'_uaPrevia is named {total} time(s) in DisplayManager_UiA_Menus.cpp and {dentro} inside the two cases of the Tema screen: no other screen may name it')
    return p


def problemas_de_caixa(cat, mh):
    """S10c, A'3b, ANGULO.md section 7 (sentence case): a colour's name follows a colon on the words line ("Cor de destaque: azul") and a comma in the catalogue ("Claro, azul"), so it starts in
    lower case in both, and the words line is `Cor de destaque: %s`."""
    p = []
    rows, cores, _ = pedacos_do_catalogo(cat, mh)
    if not cores:
        p.append('no accent name found in TEMA_CORES')
    for c in cores:
        if not c[:1].islower():
            p.append(f'the accent name {c!r} starts in upper case, after the colon of the words line')
    for _, n in rows:
        if ', ' in n and not n.split(', ', 1)[1][:1].islower():
            p.append(f'the catalogue name {n!r} starts its colour in upper case, after the comma')
    fmt = re.search(r'TEMA_FMT_COR = "([^"]*)";', mh)
    if not fmt or fmt.group(1) != 'Cor de destaque: %s':
        p.append(f'the words line is {(fmt.group(1) if fmt else None)!r}, expected "Cor de destaque: %s"')
    return p


def edicoes_de(m):
    """The edits of a glue mutant: (name, old, new, count) is one edit; (name, [(old, new, count), ...]) is several, applied in order."""
    return m[1] if isinstance(m[1], list) else [(m[1], m[2], m[3])]


def aplica(texto, edicoes):
    """`texto` with each edit (old, new, count) applied in order; None when an anchor does not occur exactly `count` times where it is applied."""
    for velho, novo, n in edicoes:
        if texto.count(velho) != n:
            return None
        texto = texto.replace(velho, novo)
    return texto


# ----------------------------------------------------------------------------------------------------------------------------------------- the Tema screen's touch (S10c, A'2d)
# The review of A'3b and A'2c: a tap on a mode that resets the accent (MM1), a tap on an accent that resets the mode (MM2) and escolhaNaTela( ) ignoring a preview of 0 (MM3) passed test_temas.py, because the Tema
# screen's touch is in DisplayManager_UiA_Menus.cpp, outside the six functions of the paint path. The kit's rule (tools_s10c/glue_check.py, outside the repository) refused MM1 and MM2 and never read escolhaNaTela( ).
# Two layers now: the pieces that decide what a tap does are cut out and RUN (test_temas_toque_main.cpp), and the text of the case is held to its five statements, as the kit held it.
TOQUE_PROLOGO = r'^[ \t]*const bool voltar\b[^;]*;\s*const int i\b[^;]*;'   # the two statements before the switch of uaToqueMenus( ) that the Tema case reads


def recorta_menus(menus, nomeado=None):
    """The pieces of the menus glue that the Tema touch program compiles: {file name: text}, and what could not be found (or is refused by the preprocessor rule). Verbatim and without comments."""
    limpo = apaga_comentarios(menus)
    pecas, falta, posicoes = {}, [], []
    pos = posicao_da_definicao(limpo, r'^(?!\s)[^\n;{}()]*\bescolhaNaTela\s*\(')
    if pos is None:
        falta.append('escolhanatela.inc: escolhaNaTela( ) was not found exactly once, as a definition, in DisplayManager_UiA_Menus.cpp')
    else:
        pecas['escolhanatela.inc'] = limpo[pos[0]:pos[1]] + '\n'
        posicoes.append(('escolhaNaTela( )', *pos))
    f = posicao_da_definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::uaToqueMenus\s*\(')
    if f is None:
        falta.append('toque_tema.inc: DisplayManager::uaToqueMenus( ) was not found exactly once, as a definition, in DisplayManager_UiA_Menus.cpp')
    else:
        corpo = limpo[f[0]:f[1]]
        pre = list(re.finditer(TOQUE_PROLOGO, corpo, re.M))
        if len(pre) != 1:
            falta.append(f'toque_prologo.inc: the statements `const bool voltar = ...;` and `const int i = ...;` of uaToqueMenus( ) were found {len(pre)} times, expected once')
        else:
            pecas['toque_prologo.inc'] = pre[0].group(0).strip() + '\n'
            posicoes.append(('DisplayManager::uaToqueMenus( ), the statements before its switch', f[0] + pre[0].start(), f[0] + pre[0].end()))
        casos = intervalos_do_caso(corpo, 'case MODE_SETTINGS_THEMES: {')
        if len(casos) != 1:
            falta.append(f'toque_tema.inc: uaToqueMenus( ) has {len(casos)} bodies of `case MODE_SETTINGS_THEMES: {{`, expected 1')
        else:
            pecas['toque_tema.inc'] = corpo[casos[0][0]:casos[0][1]] + '\n'
            posicoes.append(('DisplayManager::uaToqueMenus( ), case MODE_SETTINGS_THEMES', f[0] + casos[0][0], f[0] + casos[0][1]))
    m = posicao_da_definicao(limpo, r'^[^\n;{}()]*\bDisplayManager::uaMontarMenus\s*\(')
    if m is None:
        falta.append('montar_tema.inc: DisplayManager::uaMontarMenus( ) was not found exactly once')
    else:
        corpo_m = limpo[m[0]:m[1]]
        cs = intervalos_do_caso(corpo_m, 'case MODE_SETTINGS_THEMES: {')
        if len(cs) != 1:
            falta.append('montar_tema.inc: uaMontarMenus( ) has %d bodies of the Tema case' % len(cs))
        else:
            pecas['montar_tema.inc'] = corpo_m[cs[0][0]:cs[0][1]] + '\n'
            posicoes.append(('DisplayManager::uaMontarMenus( ), case MODE_SETTINGS_THEMES', m[0] + cs[0][0], m[0] + cs[0][1]))
    return pecas, falta + problemas_de_preprocessador(limpo, posicoes, nomeado)


def constroi_toque(tmp, nome, menus, angulo_o, gfx_obj, inc):
    """The Tema touch program (S10c, A'2d): cuts the pieces out of `menus`, compiles test_temas_toque_main.cpp around them and links it with the kit's object. Returns (executable, '') or (None, why)."""
    pecas, falta = recorta_menus(menus)
    if falta:
        return None, 'cut: ' + '; '.join(falta)
    d = tmp / nome
    d.mkdir()
    for arq, texto in pecas.items():
        (d / arq).write_text(texto, encoding='utf-8')
    rc, err = compila(TOQUE_MAIN, d / 'toque_main.o', inc + [f'-I{d}'], False)
    if rc:
        return None, err
    exe = d / 'toque'
    r = subprocess.run(['g++', str(gfx_obj), str(angulo_o), str(d / 'toque_main.o'), '-o', str(exe)], capture_output=True, text=True)
    return (exe, '') if r.returncode == 0 else (None, r.stderr)


# The five statements of the Tema case of uaToqueMenus( ), the kit's rule (tools_s10c/glue_check.py, tema_rule( )) ported: the shown choice is the preview else the saved one; a segment tap previews its mode and a swatch tap its accent,
# gated, without posting; "Aplicar tema" posts ua::indiceDaEscolha( m, k ) of the choice on show, once, gated, and only on its own live target (the disabled button is the inert target, which no arm answers); Back drops the preview and
# posts nothing; actApplyTheme( ) is called once in the file; no numeric literal bounds the touch (the old `i < 3`).
TEMA_PRE = 'const ua::Escolha e = escolhaNaTela( _sysConfigPtr, _uaPrevia );'
TEMA_RAMOS = [
    ('Back drops the preview and posts nothing', 'if ( voltar )', '{ if ( acceptTouch( ZONA_VOLTAR ) ) { _uaPrevia = -1; showSettingsMain( ); } }'),
    ('a segment tap previews its mode', 'else if ( a->tipo == TT_MODO )', '{ if ( acceptTouch( ZONA_LINHA ) ) _uaPrevia = ua::indiceDaEscolha( (uint8_t)a->indice, e.acento ); }'),
    ('a swatch tap previews its accent', 'else if ( a->tipo == TT_COR )', '{ if ( acceptTouch( ZONA_LINHA ) ) _uaPrevia = ua::indiceDaEscolha( e.modo, (uint8_t)a->indice ); }'),
    ('Aplicar tema posts indiceDaEscolha( m, k ) once, on its own live target', 'else if ( a->tipo == TT_APLICAR )', '{ if ( acceptTouch( ZONA_LINHA ) ) actApplyTheme( ua::indiceDaEscolha( e.modo, e.acento ) ); }'),
]


def problemas_do_toque_tema(menus):
    """[problem]: the Tema case of uaToqueMenus( ) held to its five statements, in the kit's words."""
    p = []
    limpo = apaga_comentarios(menus)
    f = funcao_do_glue(limpo, 'uaToqueMenus')
    if f is None:
        return ['DisplayManager::uaToqueMenus( ) was not found exactly once, as a definition']
    corpos = blocos_do_caso(f)
    if len(corpos) != 1:
        return [f'uaToqueMenus( ) has {len(corpos)} bodies for MODE_SETTINGS_THEMES, expected 1']
    t = ' '.join(corpos[0][len('case MODE_SETTINGS_THEMES:'):].split())
    if not (t.startswith('{ ') and t.endswith(' }')):
        return ['the Tema case is not a braced block']
    t = t[2:-2]
    n = len(re.findall(r'\bactApplyTheme\s*\(', limpo))
    if n != 1:
        p.append(f'actApplyTheme( ) is called {n} times in the file, expected 1 (Aplicar tema)')
    for m in re.finditer(r'(?<![\w.])\d+', t.replace('_uaPrevia = -1;', '')):
        p.append(f'a numeric literal in the Tema touch: `{t[max(0, m.start() - 20):m.end() + 10]}`')
    if not t.startswith(TEMA_PRE + ' '):
        p.append(f'the choice on show is not `{TEMA_PRE}`')
    for nome, cond, corpo in TEMA_RAMOS:
        if cond + ' ' + corpo not in t:
            p.append(f'{nome}: expected `{cond} {corpo}`')
    exp = ' '.join([TEMA_PRE] + [cond + ' ' + corpo for _, cond, corpo in TEMA_RAMOS] + ['break;'])
    if not p and t != exp:
        p.append('the Tema case is not exactly the five statements: ' + t[:160])
    # The routing (S10c, A'2e, the review of A'2d and A'6): the case above is reached only if the first switch of uaToqueMenus( ) owns MODE_SETTINGS_THEMES, and a tap on
    # no target is a dead zone the screen takes (TRATADO), not a touch for no one. Its X6 and X7 passed every run: the run starts inside the case.
    norma = ' '.join(f.split())
    posse = re.search(r'switch \( _uiMode \) \{ ((?:case MODE_\w+: )+)break; default: return UaToque::NAO_E_MEU; \} if \( !a \) return UaToque::TRATADO;', norma)
    if posse is None or norma.find('switch ( _uiMode )') != posse.start():
        p.append('the first statement of uaToqueMenus( ) is not the switch of the screens it owns, ending `default: return UaToque::NAO_E_MEU;`, '
                 'followed by `if ( !a ) return UaToque::TRATADO;`')
    elif 'case MODE_SETTINGS_THEMES:' not in posse.group(1):
        p.append('uaToqueMenus( ) does not own MODE_SETTINGS_THEMES in its first switch: no tap would reach the Tema case')
    return p


APL = 'actApplyTheme( ua::indiceDaEscolha( e.modo, e.acento ) );'
# Each: (name, edits to DisplayManager_UiA_Menus.cpp, the text rule must refuse it, the run must fail it). T1 to T13 are the kit's mutants of the rule; MM1 to MM3 the review's. T4 changes nothing for a target the screen can have
# (its modes are 0 to 2) and only the text sees it; MM3 is in escolhaNaTela( ), which the text rule does not read.
TOQUE_MUTANTES = [
    ('T1 applying on a swatch tap', [('if ( acceptTouch( ZONA_LINHA ) ) _uaPrevia = ua::indiceDaEscolha( e.modo, (uint8_t)a->indice );',
                                      'if ( acceptTouch( ZONA_LINHA ) ) actApplyTheme( ua::indiceDaEscolha( e.modo, (uint8_t)a->indice ) );', 1)], True, True),
    ('T2 Aplicar tema posting the accent only', [(APL, 'actApplyTheme( e.acento );', 1)], True, True),
    ('T3 Aplicar tema posting the mode only', [(APL, 'actApplyTheme( e.modo );', 1)], True, True),
    ('T4 keeping `i < 3` on the segment', [('else if ( a->tipo == TT_MODO ) {', 'else if ( a->tipo == TT_MODO && i < 3 ) {', 1)], True, False),
    ('T5 the old row rule in place of the swatch arm', [('} else if ( a->tipo == TT_COR ) {', '} else if ( linha && i >= 0 && i < 3 ) {', 1)], True, True),
    ('T6 Back applying', [('_uaPrevia = -1;\n          showSettingsMain( );', APL + '\n          showSettingsMain( );', 1)], True, True),
    ('T7 Back keeping the preview', [('          _uaPrevia = -1;\n', '', 1)], True, True),
    ('T8 a segment tap previewing an accent', [('_uaPrevia = ua::indiceDaEscolha( (uint8_t)a->indice, e.acento );', '_uaPrevia = ua::indiceDaEscolha( e.modo, (uint8_t)a->indice );', 1)], True, True),
    ('T9 a swatch tap previewing a mode', [('_uaPrevia = ua::indiceDaEscolha( e.modo, (uint8_t)a->indice );\n      } else if ( a->tipo == TT_APLICAR )',
                                            '_uaPrevia = ua::indiceDaEscolha( (uint8_t)a->indice, e.acento );\n      } else if ( a->tipo == TT_APLICAR )', 1)], True, True),
    ('T10 the disabled (inert) button acting', [('} else if ( a->tipo == TT_APLICAR ) {', '} else if ( a->tipo == TT_APLICAR || a->tipo == ALVO_INERTE ) {', 1)], True, True),
    ('T11 Aplicar tema not gated by the touch zone', [('if ( acceptTouch( ZONA_LINHA ) ) ' + APL, APL, 1)], True, True),
    ('T12 Aplicar tema posting twice', [(APL, APL + ' ' + APL, 1)], True, True),
    ('T13 the choice on show ignoring the preview', [('escolhaNaTela( _sysConfigPtr, _uaPrevia );\n      if ( voltar )', 'escolhaNaTela( _sysConfigPtr, -1 );\n      if ( voltar )', 1)], True, True),
    ('MM1 a tap on a mode resets the accent', [('ua::indiceDaEscolha( (uint8_t)a->indice, e.acento )', 'ua::indiceDaEscolha( (uint8_t)a->indice, 0 )', 1)], True, True),
    ('MM2 a tap on an accent resets the mode', [('ua::indiceDaEscolha( e.modo, (uint8_t)a->indice )', 'ua::indiceDaEscolha( 0, (uint8_t)a->indice )', 1)], True, True),
    ('MM3 the screen ignores a preview of 0', [('previa >= 0 ? previa :', 'previa > 0 ? previa :', 1)], False, True),
    # S10c, A'2e, the review of A'2d and A'6: the routing to the Tema case (X6, X7: only the text sees them, the run starts inside the case) and the screen's builder
    # (B1, B2: only the run sees them, since A'2e cuts and runs the builder's case).
    ('X6 the menus touch does not own the Tema screen', [('    case MODE_SETTINGS_MAIN:\n    case MODE_SETTINGS_THEMES:\n    case MODE_SETTINGS_LANG:\n', '    case MODE_SETTINGS_MAIN:\n    case MODE_SETTINGS_LANG:\n', 1)], True, False),
    ('X7 a tap on no target is not taken as a dead zone', [('if ( !a ) return UaToque::TRATADO;', 'if ( !a ) return UaToque::NAO_E_MEU;', 1)], True, False),
    ('B1 the builder marks the shown choice as the saved one', [('vm.salvo = ua::indiceDaEscolha( s.modo, s.acento );', 'vm.salvo = ua::indiceDaEscolha( e.modo, e.acento );', 1)], False, True),
    ('B2 the builder shows the stock accent', [('vm.acento = e.acento;', 'vm.acento = 0;', 1)], False, True),
]


def diz(problemas, quem, linha):
    """Whether some problem names `quem` and the line `linha` (a refusal that says which piece and where)."""
    return any(quem in q and re.search(rf'\bline {linha}\b', q) for q in problemas)


def linha_de(texto, marca):
    return texto.count('\n', 0, texto.index(marca)) + 1


def main():
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found (run pio run -e pico_wiretft_angulo once)', file=sys.stderr)
        return 2
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{HERE / "host" / "shim"}', f'-I{gfx}', f'-I{KIT}']
    with tempfile.TemporaryDirectory(prefix='temas_host_') as t:
        tmp = pathlib.Path(t)
        gfx_obj, main_obj = tmp / 'Adafruit_GFX.o', tmp / 'test_temas_main.o'
        for src, obj, estrito in ((gfx / 'Adafruit_GFX.cpp', gfx_obj, False), (HERE / 'host' / 'test_temas_main.cpp', main_obj, True)):
            rc, err = compila(src, obj, inc, estrito)
            if rc:
                print(err, file=sys.stderr)
                return 2
        texto = ANGULO.read_text(encoding='utf-8')
        for nome, velho, novo in MUTANTES:
            if texto.count(velho) != 1:
                print(f'the mutant "{nome}" does not find its line in Angulo.cpp exactly once', file=sys.stderr)
                return 2
        # the real program and one program per mutant, built side by side
        jobs = [('real', None)] + [(f'm{i}', texto.replace(v, n)) for i, (_, v, n) in enumerate(MUTANTES)]
        with concurrent.futures.ThreadPoolExecutor(4) as ex:
            feitos = list(ex.map(lambda j: constroi(tmp, j[0], j[1], gfx_obj, main_obj, inc), jobs))
        if feitos[0][0] is None:
            print(feitos[0][1], file=sys.stderr)
            return 2
        rc, saida = corre(feitos[0][0])
        falhas = [l for l in saida.splitlines() if l.startswith('FAIL')]
        resumo = saida.strip().splitlines()[-1] if saida.strip() else 'no output'
        caso(f'host program: mode, accent, tema( ), desab( ) and the frame identity against the generated header ({resumo})', not falhas and rc == 0 and resumo.endswith(' 0 failed'))
        for l in falhas[:6]:
            print('       ' + l)
        for l in saida.splitlines():
            if l.startswith('MEDIDA'):
                print('       ' + l)
        for (nome, _, _), (exe, err) in zip(MUTANTES, feitos[1:]):
            if exe is None:
                caso(f'control: {nome}', [f'the mutant does not build: {err.strip()[:200]}'])
                continue
            rc, saida = corre(exe)
            caso(f'control: {nome} must fail the program', rc == 1 and 'FAIL' in saida)

        # the glue's own functions, cut out whole, compiled against a stand-in and run (S10c, A'2c): the real text first, the same with one wrong pixel in every pushed strip (the instrument's
        # control), then one program per mutant of it
        glue0 = GLUE.read_text(encoding='utf-8')
        angulo_o = tmp / 'real' / 'Angulo.o'
        menus0 = MENUS_GLUE.read_text(encoding='utf-8')
        aplicaveis = [(i, t) for i, t in ((i, aplica(glue0, edicoes_de(m))) for i, m in enumerate(GLUE_MUTANTES)) if t is not None]
        jobs_g = [('glue_real', glue0, ()), ('glue_pixel', glue0, ('-DPROVA_PIXEL',))] + [(f'g{i}', t, ()) for i, t in aplicaveis]
        # the Tema screen's touch (S10c, A'2d): the menus glue as it is, then one program per mutant of it that the run must fail
        aplicaveis_t = [(i, t) for i, t in ((i, aplica(menus0, m[1])) for i, m in enumerate(TOQUE_MUTANTES) if m[3]) if t is not None]
        jobs_t = [('toque_real', menus0)] + [(f't{i}', t) for i, t in aplicaveis_t]
        with concurrent.futures.ThreadPoolExecutor(4) as ex:
            fg = [ex.submit(constroi_glue, tmp, j[0], j[1], angulo_o, gfx_obj, inc, j[2]) for j in jobs_g]
            ft = [ex.submit(constroi_toque, tmp, j[0], j[1], angulo_o, gfx_obj, inc) for j in jobs_t]
            ligados = [f.result() for f in fg]
            ligados_t = [f.result() for f in ft]
        exe, err = ligados[0]
        if exe is None:
            caso("glue link: the glue's own functions build against the stand-in", [f'does not build: {err.strip()[:3000]}'])
        else:
            rc, saida = corre(exe)
            falhas = [l for l in saida.splitlines() if l.startswith('FAIL')]
            resumo = saida.strip().splitlines()[-1] if saida.strip() else 'no output'
            caso(f"glue link: the glue's own theme and paint path, run against the generated header ({resumo})", not falhas and rc == 0 and resumo.endswith(' 0 failed'))
            for l in falhas[:6]:
                print('       ' + l)
            for l in saida.splitlines():
                if l.startswith('MEDIDA'):
                    print('       ' + l)
        exe_px, err_px = ligados[1]
        if exe_px is None:
            caso('control: one wrong pixel in every pushed strip must fail the glue link', [f'the program does not build: {err_px.strip()[:600]}'])
        else:
            rc, saida = corre(exe_px)
            caso('control: one wrong pixel in every pushed strip must fail the glue link (the instrument sees the glass)', rc == 1 and 'FAIL' in saida)
        feitos_g = dict(zip([i for i, _ in aplicaveis], ligados[2:]))
        for i, m in enumerate(GLUE_MUTANTES):
            nome = m[0]
            if i not in feitos_g:
                caso(f'control: {nome} must fail the glue link', ['an anchor of its edit is not in DisplayManager_UiA.cpp the expected number of times'])
                continue
            exe, err = feitos_g[i]
            if exe is None:
                # a mutant that leaves nothing to cut is refused at the cut; any other failure to build is not a kill
                caso(f'control: {nome} must fail the glue link', True if err.startswith('cut: ') else [f'the mutant does not build: {err.strip()[:600]}'])
                continue
            rc, saida = corre(exe)
            caso(f'control: {nome} must fail the glue link', rc == 1 and 'FAIL' in saida)

        # the Tema screen's touch, cut out of the menus glue and run (S10c, A'2d)
        exe, err = ligados_t[0]
        if exe is None:
            caso("Tema touch link: escolhaNaTela( ) and the Tema case of uaToqueMenus( ) build against the stand-in", [f'does not build: {err.strip()[:3000]}'])
        else:
            rc, saida = corre(exe)
            falhas = [l for l in saida.splitlines() if l.startswith('FAIL')]
            resumo = saida.strip().splitlines()[-1] if saida.strip() else 'no output'
            caso(f"Tema touch link: escolhaNaTela( ) and the Tema case of uaToqueMenus( ), run against the decode listed from the generated header ({resumo})", not falhas and rc == 0 and resumo.endswith(' 0 failed'))
            for l in falhas[:6]:
                print('       ' + l)
            for l in saida.splitlines():
                if l.startswith('MEDIDA'):
                    print('       ' + l)
        feitos_t = dict(zip([i for i, _ in aplicaveis_t], ligados_t[1:]))
        for i, m in enumerate(TOQUE_MUTANTES):
            if not m[3]:
                continue
            nome = m[0]
            if i not in feitos_t:
                caso(f'control: {nome} must fail the Tema touch link', ['an anchor of its edit is not in DisplayManager_UiA_Menus.cpp the expected number of times'])
                continue
            exe, err = feitos_t[i]
            if exe is None:
                caso(f'control: {nome} must fail the Tema touch link', True if err.startswith('cut: ') else [f'the mutant does not build: {err.strip()[:600]}'])
                continue
            rc, saida = corre(exe)
            caso(f'control: {nome} must fail the Tema touch link', rc == 1 and 'FAIL' in saida)

    glue = GLUE.read_text(encoding='utf-8')
    caso('glue: the frame identity comes from ua::identidadeDoQuadro( ) in both paints, uaTema( ) decodes with ua::escolhaDoIndice( )', problemas_do_glue(glue))
    caso("glue: the three calls of uaTema( ), the full-repaint trigger, the record of the frame and the preview's reset are written exactly", problemas_do_glue_exato(glue))
    caso("control: the painted frame taking the theme of _uiMode instead of its own mode is reported",
         bool(problemas_do_glue_exato(glue.replace('const ua::Tema &tema = uaTema( modo );', 'const ua::Tema &tema = uaTema( _uiMode );'))))
    caso("control: the update screen taking the theme of _uiMode is reported",
         bool(problemas_do_glue_exato(glue.replace('const ua::Tema &tema = uaTema( MODE_OTA_UPDATE );', 'const ua::Tema &tema = uaTema( _uiMode );'))))
    caso("control: a trigger that clears the flag is reported",
         bool(problemas_do_glue_exato(glue.replace('.repintaTudo( s_id ) ) _uaTudo = true;', '.repintaTudo( s_id ) ) _uaTudo = false;'))))
    caso("control: a trigger after the line that skips an unchanged frame is reported", bool(problemas_do_glue_exato(glue.replace(GATILHO + PULO, PULO + GATILHO))))
    caso("control: the record of the frame hoisted before the paint is reported",
         bool(problemas_do_glue_exato(glue.replace('  if ( uaPintar( q, _uiMode, h ) ) {', '  s_id = id;\n  if ( uaPintar( q, _uiMode, h ) ) {'))))
    caso("control: an update screen that no longer compares the hash is reported",
         bool(problemas_do_glue_exato(glue.replace('|| h != s_hash || s_modo != MODE_OTA_UPDATE', '|| s_modo != MODE_OTA_UPDATE'))))
    caso("control: an alignment read from the wrong axis is reported",
         bool(problemas_do_glue_exato(glue.replace('oy = _driver.tft->getOffsetY( );', 'oy = _driver.tft->getOffsetX( );'))))
    caso('control: a glue that never drops the preview is reported', bool(problemas_do_glue_exato(glue.replace(RESET, ''))))
    caso('control: a preview dropped after the line that returns while no look is due is reported', bool(problemas_do_glue_exato(glue.replace(RESET + OLHAR, OLHAR + RESET))))
    caso('control: a second reader of the preview in the glue is reported',
         bool(problemas_do_glue_exato(glue.replace('  if ( !uaMontarOta( q ) ) return;\n', '  if ( !uaMontarOta( q ) || _uaPrevia > 99 ) return;\n'))))
    caso('control: the update screen painted as the screen on show is reported', bool(problemas_do_glue_exato(glue.replace('uaPintar( q, MODE_OTA_UPDATE, h );', 'uaPintar( q, _uiMode, h );'))))
    caso('control: a glue that hashes a fixed theme instead of uaTema( )\'s is reported',
         problemas_do_glue(glue.replace('ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, tema,', 'ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, ua::temaClaro( ),')) != [] and
         bool(problemas_do_glue(glue.replace('ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, tema,', 'ua::identidadeDoQuadro( (uint8_t)MODE_OTA_UPDATE, ua::temaClaro( ),'))))
    caso('control: a glue without the full-repaint trigger is reported', bool(problemas_do_glue(glue.replace('.repintaTudo( s_id )', '.pronto( )'))))
    caso('control: a glue with its own extra[ ] array is reported', bool(problemas_do_glue(glue + '\nconst uint8_t extra[2] = { 0, 0 };\n')))
    caso('glue: the palette is picked in uaTema( ) alone (no paint site reads ua::tema( ), temaClaro( ) or temaEscuro( ) itself)', problemas_de_leitura_do_tema(glue))
    caso('control: a paint site that reads ua::tema( ) itself is reported',
         bool(problemas_de_leitura_do_tema(glue.replace('ua::Pintor p( faixa, tema, faixa );', 'ua::Pintor p( faixa, ua::tema( tema.escuro, 0 ), faixa );'))))
    caso('control: a paint site that reads temaClaro( ) itself is reported', bool(problemas_de_leitura_do_tema(glue.replace('faixa.fillScreen( tema.fundo );', 'faixa.fillScreen( ua::temaClaro( ).fundo );'))))
    caso("control: a paint site that sets the Pintor's theme itself is reported", bool(problemas_de_leitura_do_tema(glue.replace('  p.gravar( );\n', '  p.gravar( );\n  p.tema( ua::temaEscuro( ) );\n'))))
    caso('control: a comment that names ua::tema( ) is not a read',
         not problemas_de_leitura_do_tema(glue.replace('  const ua::Tema &tema = uaTema( modo );\n', '  const ua::Tema &tema = uaTema( modo );  // not ua::tema( x, 0 )\n')))
    pecas, falta = recorta_glue(glue)
    caso('glue cut: the file-level state and the six functions of the theme and paint path are found', falta + ([] if len(pecas) == 1 + len(PECAS_DO_GLUE) else [f'{len(pecas)} pieces']))
    caso('control: a glue without DisplayManager::uaPintar( ) is refused at the cut', bool(recorta_glue(glue.replace('bool DisplayManager::uaPintar(', 'bool DisplayManager::uaPintarX('))[1]))
    caso('control: a glue that defines uaQuadro( ) twice is refused at the cut', bool(recorta_glue(glue + '\nvoid DisplayManager::uaQuadro( ) {}\n')[1]))
    caso('control: a reformatted signature and CRLF line ends are still cut',
         not recorta_glue(glue.replace('DisplayManager::uaTema( UiMode modo )', 'DisplayManager::uaTema(UiMode modo)').replace('\n', '\r\n'))[1])
    caso('control: a comment that names a function is not cut in its place', recorta_glue('// bool DisplayManager::uaQuadro( ) { }\n' + glue)[0].get('uaquadro.inc') == pecas.get('uaquadro.inc'))
    outros = []
    for f in sorted(KIT.rglob('*')):
        if f.suffix in ('.cpp', '.h') and f.name not in ('Angulo.cpp', 'angulo_tokens.h'):
            outros.append((str(f.relative_to(ROOT)), f.read_text(encoding='utf-8', errors='replace')))
    caso('sources: ACENTOS_N appears only inside the two encoding functions', acentos_n_fora(texto, outros))
    caso('control: an ACENTOS_N in a screen file is reported', bool(acentos_n_fora(texto, outros + [('telas/X.cpp', 'int n = ACENTOS_N;')])))
    caso('control: an ACENTOS_N in tema( ) is reported', bool(acentos_n_fora(texto.replace('acento < kAcentos ? acento : 0', 'acento < ACENTOS_N ? acento : 0'), outros)))
    caso('sources: the comment above the encoding functions names every other file that reads ACENTOS_N, and only those', leitores_nomeados(texto, outros))
    caso('control: a file that reads ACENTOS_N and the comment does not name is reported', bool(leitores_nomeados(texto, outros + [('telas/X.cpp', 'static_assert( X == ACENTOS_N, "x" );')])))
    caso('control: a comment that names a file that does not read it is reported', bool(leitores_nomeados(texto.replace('MenusConfig.cpp', 'MenusConfig.cpp, Fantasma.cpp'), outros)))

    cat, mh, mc = TEMAS_CPP.read_text(encoding='utf-8'), MENUS_H.read_text(encoding='utf-8'), MENUS_CPP.read_text(encoding='utf-8')
    menus = MENUS_GLUE.read_text(encoding='utf-8')
    caso("menus glue: the preview is named only inside the two cases of the Tema screen", problemas_da_previa_nos_menus(menus))
    caso('control: another screen naming the preview is reported', bool(problemas_da_previa_nos_menus(menus + '\nint8_t leMenus( ) { return _uaPrevia; }\n')))
    caso('catalogue: 18 ids and names, each at the index the decode gives it, ids within 15 B and names within 23 B', problemas_do_catalogo(cat, mh))
    r0 = re.findall(r'TEMA\(\s*"[^"]*"\s*,\s*"[^"]*"\s*\)', cat)
    caso('control: an id of 16 B is reported', bool(problemas_do_catalogo(cat.replace('"escuro-petroleo"', '"escuro-petroleos"'), mh)))
    caso('control: a name of 24 B is reported', bool(problemas_do_catalogo(cat.replace('"Automático, petróleo"', '"Automático, petróleos2"'), mh)))
    caso('control: two rows swapped are reported', bool(problemas_do_catalogo(cat.replace(r0[4], '@@').replace(r0[5], r0[4]).replace('@@', r0[5]), mh)))
    caso('control: a catalogue of 17 rows is reported', bool(problemas_do_catalogo(cat.replace(r0[17] + ',', '', 1), mh)))
    caso('control: a catalogue without its static_assert is reported', bool(problemas_do_catalogo(cat.replace('static_assert( NUM_TEMAS == 3 *', 'static_assert( NUM_TEMAS >= 3 *'), mh)))
    caso('microcopy: a colour name is in lower case after the colon of the words line and after the comma of the catalogue (ANGULO.md section 7)', problemas_de_caixa(cat, mh))
    caso('control: a capitalised colour in the screen\'s list is reported', bool(problemas_de_caixa(cat, mh.replace('"azul"', '"Azul"'))))
    caso('control: a capitalised colour in the catalogue is reported', bool(problemas_de_caixa(cat.replace('"Claro, azul"', '"Claro, Azul"'), mh)))
    caso('control: a words line other than "Cor de destaque: %s" is reported', bool(problemas_de_caixa(cat, mh.replace('"Cor de destaque: %s"', '"Cor de Destaque: %s"'))))
    caso('sources: the Tema screen, its touch and uaTema( ) carry no bare 3 or 6 outside static_asserts', problemas_de_literais(mh, mc, menus, glue))
    caso('control: an `i < 3` in the touch is reported', bool(problemas_de_literais(mh, mc, menus.replace('else if ( a->tipo == TT_MODO ) {', 'else if ( a->tipo == TT_MODO && i < 3 ) {'), glue)))
    caso('control: a 6 in the screen is reported', bool(problemas_de_literais(mh, mc.replace('for ( int k = 0; k < TEMA_CORES_N; k++ ) desenharCor', 'for ( int k = 0; k < 6; k++ ) desenharCor'), menus, glue)))
    caso('control: a static_assert with the 3 and the 6 is not reported', not problemas_de_literais(mh, mc.replace('/* Idioma:', 'static_assert( 3 * 6 == 18, "x" );\n/* Idioma:', 1), menus, glue))

    # S10c, A'2d: the Tema touch's text, held to its five statements (the kit's rule, ported) with its 13 mutants and the review's three, and the cut's preprocessor rule
    caso('menus glue: the Tema case of uaToqueMenus( ) is exactly its five statements (the choice on show, Back, a mode, an accent, Aplicar tema), actApplyTheme( ) is called once and no literal bounds the touch', problemas_do_toque_tema(menus))
    for nome, edicoes, texto_refusa, _ in TOQUE_MUTANTES:
        if texto_refusa:
            t = aplica(menus, edicoes)
            caso(f'control: {nome} is reported by the text rule', t is not None and bool(problemas_do_toque_tema(t)))
    caso('control: a menus glue without uaToqueMenus( ) is reported by the text rule', bool(problemas_do_toque_tema(menus.replace('DisplayManager::uaToqueMenus(', 'DisplayManager::uaToqueMenusX('))))
    t = aplica(menus, [('    case MODE_SETTINGS_THEMES: {\n      /* A tap changes', '    case MODE_SETTINGS_THEMES : {\n      /* A tap changes', 1)])
    caso('control: a Tema case whose label is no longer found is reported by the text rule', t is not None and bool(problemas_do_toque_tema(t)))
    t = aplica(menus, [('        if ( acceptTouch( ZONA_LINHA ) ) _uaPrevia = ua::indiceDaEscolha( e.modo, (uint8_t)a->indice );', '        if ( acceptTouch( ZONA_LINHA ) )\n          _uaPrevia = ua::indiceDaEscolha( e.modo, (uint8_t)a->indice );', 1)])
    caso('control: the same statements on other lines are not reported by the text rule', t is not None and not problemas_do_toque_tema(t))
    t = aplica(menus, [("        /* The original's Back (handleTouch( ), Touch.cpp): SAIR is back to the menu, nothing applied. */", '        /* another comment, with `actApplyTheme( 1 )` and a 3 in it */', 1)])
    caso('control: a comment in the Tema case is not a change for the text rule', t is not None and not problemas_do_toque_tema(t))
    pecas_m, falta_m = recorta_menus(menus)
    caso('menus cut: escolhaNaTela( ), the statements before the switch of uaToqueMenus( ) and its Tema case are found, with no conditional inside them or around them but the named ones', falta_m + ([] if len(pecas_m) == 4 else [f'{len(pecas_m)} pieces']))
    caso('control: a menus glue without escolhaNaTela( ) is refused at the cut', bool(recorta_menus(menus.replace('ua::Escolha escolhaNaTela(', 'ua::Escolha escolhaNaTelaX('))[1]))
    caso('control: a menus glue that defines escolhaNaTela( ) twice is refused at the cut', bool(recorta_menus(menus + '\nua::Escolha escolhaNaTela( const SystemConfig *, int8_t ) { return ua::Escolha{ 0, 0 }; }\n')[1]))
    caso('control: a reformatted escolhaNaTela( ) and CRLF line ends are still cut',
         not recorta_menus(menus.replace('ua::Escolha escolhaNaTela( const SystemConfig *cfg, int8_t previa ) {', 'ua::Escolha escolhaNaTela(const SystemConfig *cfg, int8_t previa) {').replace('\n', '\r\n'))[1])
    caso('control: a comment that names escolhaNaTela( ) is not cut in its place', recorta_menus('// ua::Escolha escolhaNaTela( int ) { }\n' + menus)[0].get('escolhanatela.inc') == pecas_m.get('escolhanatela.inc'))

    # the preprocessor (the review's S2a and S2b2): a conditional around or inside a piece is refused at the cut, and the message names the piece and the line
    a = glue.index('bool DisplayManager::uaRelogio(')
    fim = '  return h >= 0 && h < 24;\n}\n'
    b = glue.index(fim) + len(fim)
    s2a = glue[:a] + '#if 0\n' + glue[a:b] + '#endif\n' + glue[b:]
    caso('control: an `#if 0` around uaRelogio( ) is refused at the cut, naming the function and the line', glue.count('#if 0\n') == 0 and diz(recorta_glue(s2a)[1], 'uaRelogio', linha_de(s2a, '#if 0\n')))
    s2b = glue.replace('  return ua::tema( escuro, e.acento );', '#ifdef ARDUINO_ARCH_RP2040\n  escuro = false;\n#endif\n  return ua::tema( escuro, e.acento );')
    caso('control: an `#ifdef ARDUINO_ARCH_RP2040` inside uaTema( ) is refused at the cut, naming the function and the line', s2b != glue and diz(recorta_glue(s2b)[1], 'uaTema', linha_de(s2b, '#ifdef ARDUINO_ARCH_RP2040')))
    s_est = glue.replace('uint32_t s_hash = 0;\n', '#if 0\nuint32_t s_hash = 0;\n#endif\n')
    caso('control: an `#if 0` around a declaration of the file-level state is refused at the cut, naming the state and the line', s_est != glue and diz(recorta_glue(s_est)[1], 'file-level state', linha_de(s_est, '#if 0\n')))
    s_else = glue.replace('  _otaRedrawAll = false;\n  s_otaSeq = seq;', '#if 1\n  _otaRedrawAll = false;\n#else\n  _otaRedrawAll = true;\n#endif\n  s_otaSeq = seq;')
    caso('control: an `#if 1 ... #else ... #endif` inside uiPaintOta( ) is refused at the cut, naming the function and the lines of the `#if` and of the `#else`',
         s_else != glue and diz(recorta_glue(s_else)[1], 'uiPaintOta', linha_de(s_else, '#if 1\n')) and diz(recorta_glue(s_else)[1], 'uiPaintOta', linha_de(s_else, '#else\n')))
    caso('control: a conditional that is inside no piece and around none is not reported', not recorta_glue(glue + '\n#if SIMUT_NET_LAN\nint s_outro = 0;\n#endif\n')[1])
    caso('control: a conditional named in a comment is not a conditional', not recorta_glue(glue.replace('  const ua::Tema &tema = uaTema( modo );\n', '  const ua::Tema &tema = uaTema( modo );  // not an #if 0, and no #endif\n'))[1])
    caso('control: a conditional around a piece that the list names is accepted', not recorta_glue(s2a, {**CONDICIONAIS_QUE_ENVOLVEM, 'DisplayManager::uaRelogio( )': ['#if 0']})[1])
    a = menus.index('ua::Escolha escolhaNaTela(')
    b = menus.index('\n}\n', a) + 3
    m_a = menus[:a] + '#if 0\n' + menus[a:b] + '#endif\n' + menus[b:]
    caso('control: an `#if 0` around escolhaNaTela( ) is refused at the cut, naming the function and the line', menus.count('#if 0\n') == 0 and diz(recorta_menus(m_a)[1], 'escolhaNaTela', linha_de(m_a, '#if 0\n')))
    m_b = menus.replace('_uaPrevia = -1;\n          showSettingsMain( );', '_uaPrevia = -1;\n#ifdef SIMUT_NET_LAN\n          showSettingsMain( );\n#endif')
    caso('control: an `#ifdef` inside the Tema case of uaToqueMenus( ) is refused at the cut, naming the case and the line', m_b != menus and diz(recorta_menus(m_b)[1], 'case MODE_SETTINGS_THEMES', linha_de(m_b, '#ifdef SIMUT_NET_LAN')))
    i0 = menus.rindex('    case MODE_SETTINGS_THEMES: {\n', 0, menus.index('/* A tap changes one half'))
    m_c = (menus[:i0] + '#if SIMUT_NET_LAN\n' + menus[i0:]).replace('    case MODE_SETTINGS_LANG:\n      if ( voltar ) {', '#endif\n    case MODE_SETTINGS_LANG:\n      if ( voltar ) {')
    caso('control: an `#if` around the Tema case of uaToqueMenus( ) is refused at the cut, naming the case and the line', diz(recorta_menus(m_c)[1], 'case MODE_SETTINGS_THEMES', linha_de(m_c, '#if SIMUT_NET_LAN\n    case MODE_SETTINGS_THEMES: {')))

    falhas = [n for n, ok in CASOS if not ok]
    print(f'{len(CASOS)} cases, {len(falhas)} failed')
    return 1 if falhas else 0


if __name__ == '__main__':
    sys.exit(main())
