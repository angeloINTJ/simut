#!/usr/bin/env python3
"""test_rede.py -- the network glyph of the Angulo's status bar (DisplayManager::uaVmStatus( )), on the host (S10c, the Wi-Fi status bar).

The original panel decides the glyph of its top bar (DisplayManager::drawTopBar( ), src/ui/original/DisplayManager_Dashboard.cpp) from the level NetworkManager::getRssi( ) gives: on a cable image the cable, green above -100 and
grey at or below; on a Wi-Fi image four bars, none at -100 or less, one up to -75, two up to -65, three up to -55, four above. The Angulo says the same with Lucide's own icons (the maintainer's decision "levels like the
original", 2026-10-30): on a Wi-Fi image signal-low, signal-medium, signal-high and signal for one to four bars ("B, barras de sinal"; the one-bar icon in the alert colour: from -78 down the device holds telemetry, RSSI_MIN_THRESHOLD) and wifi-off in
the bar's neutral grey for no signal (not an error, so not the danger colour); on a cable image ethernet-port with a link and unplug without, both in the same neutral grey ("icone proprio, em cinza": the shape carries the state, as on Wi-Fi; no network state draws in the danger colour). Until the fix of 2026-10-30 uaVmStatus( ) answered
ua::Rede::CABO or CABO_SEM_LINK on every image, so the Pico 2 W's release (Wi-Fi, the Angulo since S10c C3) drew a cable. This holds it:

  1. The original's thresholds, READ from its source and not copied: the cable's `> -100` and the ladder -100 / -55 / -65 / -75 of the Wi-Fi image's `barras`. If the ladder moves or changes shape
     this fails and says so, and the glue's constants and the named points of host/test_rede_main.cpp are to be read again.
  2. The glue RUN (host/test_rede_main.cpp): uaVmStatus( ) cut out of the glue verbatim, without comments, and compiled with the real kit against a stand-in, once as a Wi-Fi image (-DSIMUT_NET_WIFI=1) and
     once as the cable's (=0): every level from -200 to +20 dBm and the extremes of int against the old ladder, the named points (strong, each threshold and the step beside it, weak, disconnected), the fields
     the function does not own, and the frames: the status bar drawn by the real kit from the view model the glue made. What the network icon adds to the bar must be the expected Lucide icon, pixel for pixel
     (drawn alone by the kit), all in the expected ink; no icon missing; a different picture for each state.
  3. The mutants of the cut piece: the glue edited one statement at a time (the three-state ladder of the first version, every threshold one dB either side, inclusive for exclusive, a level drawn as another, no
     signal answering NENHUMA, the level not read, the old line back on a Wi-Fi image), each of which the model must fail. And the mutants of the kit: a copy of Telas.cpp with one change (no signal in the danger
     colour or with the one-bar icon, one bar without the alert colour, a level with another level's icon, the cable without link back in the danger colour or back to ethernet-port), each of which the frames must fail. And the mutants of the registry: a copy of Angulo.cpp built with AnguloIcones.h edited so that two signal icons have each other's bitmap (the frames compare the bar with the kit's own drawing of the same icon, so only the lit pixels and the pieces of each icon see a swap). A mutant that does not
     compile, or an anchor not found exactly once, fails this script: a mutant must be killed by a check and not by the compiler.
  4. One place decides the glyph: every assignment to a `.rede` under src/ui/angulo is in uaVmStatus( ).

Usage: python3 tools/tft_ui/test_rede.py [--out DIR] [--glue FILE]   exit 0 when all hold, 1 when not, 2 when it cannot build (no Adafruit GFX 1.12.6 in .pio/libdeps: run pio once).
  --out DIR   keeps the frames there: a PNG per frame and a contact sheet (_barras.png).
  --glue FILE reads uaVmStatus( ) from FILE instead of src/ui/angulo/DisplayManager_UiA.cpp (the controls: the glue of the commit before, the first version's three states put back); the source guard is then skipped.
"""
import argparse
import concurrent.futures
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)

UA = ROOT / 'src' / 'ui' / 'angulo'
GLUE = UA / 'DisplayManager_UiA.cpp'
TELAS = UA / 'telas' / 'Telas.cpp'
ORIGINAL = ROOT / 'src' / 'ui' / 'original' / 'DisplayManager_Dashboard.cpp'   # S11: the original's units live in src/ui/original/
MAIN = HERE / 'host' / 'test_rede_main.cpp'
SHIM = HERE / 'host' / 'shim'
PECA = 'uaVmStatus.inc'

# (name, the piece, the statement, what replaces it, the build: 1 a Wi-Fi image, 0 the cable's): each edit must make the model fail. The statements are the glue's, as cut (comments removed).
MUTANTES = [
    ('the cable states back on a Wi-Fi image (the old line)', PECA, 'if constexpr ( SIMUT_NET_WIFI )', 'if constexpr ( 0 )', 1),
    ('the radio\'s states on a cable image', PECA, 'if constexpr ( SIMUT_NET_WIFI )', 'if constexpr ( 1 )', 0),
    ('one bar only from -74', PECA, 'ATE_1_BARRA = -75;', 'ATE_1_BARRA = -74;', 1),
    ('one bar only to -76', PECA, 'ATE_1_BARRA = -75;', 'ATE_1_BARRA = -76;', 1),
    ('two bars only to -64', PECA, 'ATE_2_BARRAS = -65;', 'ATE_2_BARRAS = -64;', 1),
    ('two bars only to -66', PECA, 'ATE_2_BARRAS = -65;', 'ATE_2_BARRAS = -66;', 1),
    ('three bars only to -54', PECA, 'ATE_3_BARRAS = -55;', 'ATE_3_BARRAS = -54;', 1),
    ('three bars only to -56', PECA, 'ATE_3_BARRAS = -55;', 'ATE_3_BARRAS = -56;', 1),
    ('no link from -99 (Wi-Fi image)', PECA, 'SEM_SINAL = -100;', 'SEM_SINAL = -99;', 1),
    ('no link only below -101 (cable image)', PECA, 'SEM_SINAL = -100;', 'SEM_SINAL = -101;', 0),
    ('the no-link threshold exclusive (Wi-Fi image)', PECA, 'nivel <= SEM_SINAL ? ua::Rede::WIFI_SEM', 'nivel < SEM_SINAL ? ua::Rede::WIFI_SEM', 1),
    ('the one-bar threshold exclusive', PECA, 'nivel <= ATE_1_BARRA ?', 'nivel < ATE_1_BARRA ?', 1),
    ('the two-bar threshold exclusive', PECA, 'nivel <= ATE_2_BARRAS ?', 'nivel < ATE_2_BARRAS ?', 1),
    ('the three-bar threshold exclusive', PECA, 'nivel <= ATE_3_BARRAS ?', 'nivel < ATE_3_BARRAS ?', 1),
    ('the no-link threshold inclusive (cable image)', PECA, 'nivel > SEM_SINAL ? ua::Rede::CABO :', 'nivel >= SEM_SINAL ? ua::Rede::CABO :', 0),
    ('two bars drawn as three', PECA, 'ua::Rede::WIFI_2', 'ua::Rede::WIFI_3', 1),
    ('three bars drawn as four', PECA, 'ua::Rede::WIFI_3', 'ua::Rede::WIFI_4', 1),
    ('two bars drawn as four', PECA, 'ua::Rede::WIFI_2', 'ua::Rede::WIFI_4', 1),
    ('one bar drawn as two', PECA, 'ua::Rede::WIFI_1', 'ua::Rede::WIFI_2', 1),
    ('no signal drawn as one bar', PECA, 'nivel <= SEM_SINAL ? ua::Rede::WIFI_SEM', 'nivel <= SEM_SINAL ? ua::Rede::WIFI_1', 1),
    ('no signal answers NENHUMA (Wi-Fi image)', PECA, 'ua::Rede::WIFI_SEM', 'ua::Rede::NENHUMA', 1),
    ('no link answers NENHUMA (cable image)', PECA, 'ua::Rede::CABO : ua::Rede::CABO_SEM_LINK;', 'ua::Rede::CABO : ua::Rede::NENHUMA;', 0),
    ('the cable states swapped', PECA, 'ua::Rede::CABO : ua::Rede::CABO_SEM_LINK;', 'ua::Rede::CABO_SEM_LINK : ua::Rede::CABO;', 0),
    ('the level not read', PECA, 'const int nivel = _lastRenderedState.wifiRssi;', 'const int nivel = -50;', 1),
    ('pendentes dropped', PECA, 's.pendentes = _lastRenderedState.pendingPkts;', 's.pendentes = 0;', 1),
]

# (name, the statement of Telas.cpp, what replaces it, the build): the renderer's side of the same decision; the frames must fail each.
KIT_MUTANTES = [
    ('no signal in the danger colour', 'case Rede::WIFI_SEM: rede = IconeNome::WIFI_OFF; break;', 'case Rede::WIFI_SEM: rede = IconeNome::WIFI_OFF; corRede = T.perigo; break;', 1),
    ('no signal with the one-bar icon', 'rede = IconeNome::WIFI_OFF;', 'rede = IconeNome::SIGNAL_LOW;', 1),
    ('one bar without the alert colour', 'rede = IconeNome::SIGNAL_LOW; corRede = T.alerta; break;', 'rede = IconeNome::SIGNAL_LOW; break;', 1),
    ('one bar in the danger colour', 'rede = IconeNome::SIGNAL_LOW; corRede = T.alerta; break;', 'rede = IconeNome::SIGNAL_LOW; corRede = T.perigo; break;', 1),
    ('three bars with the two-bar icon', 'rede = IconeNome::SIGNAL_HIGH;', 'rede = IconeNome::SIGNAL_MEDIUM;', 1),
    ('four bars with the three-bar icon', 'case Rede::WIFI_4: rede = IconeNome::SIGNAL;', 'case Rede::WIFI_4: rede = IconeNome::SIGNAL_HIGH;', 1),
    ('two bars with the full icon', 'rede = IconeNome::SIGNAL_MEDIUM;', 'rede = IconeNome::SIGNAL;', 1),
    ('the cable without link back in the danger colour', 'case Rede::CABO_SEM_LINK: rede = IconeNome::UNPLUG; break;', 'case Rede::CABO_SEM_LINK: rede = IconeNome::UNPLUG; corRede = T.perigo; break;', 0),
    ('the cable without link back to ethernet-port', 'case Rede::CABO_SEM_LINK: rede = IconeNome::UNPLUG; break;', 'case Rede::CABO_SEM_LINK: rede = IconeNome::ETHERNET_PORT; break;', 0),
    ('the cable drawn with the no-signal Wi-Fi icon', 'case Rede::CABO: rede = IconeNome::ETHERNET_PORT; break;', 'case Rede::CABO: rede = IconeNome::WIFI_OFF; break;', 0),
]

# (name, [(statement, replacement)], the build): the registry's rows (AnguloIcones.h, generated) with two bitmaps swapped. The frames compare the bar with the kit's own drawing of the same icon, which goes through the
# same registry, so a swap is invisible to them (the final review of 2026-10-30 swapped signal-high and signal-medium and the test still passed); the model counts the lit pixels and the pieces of each signal icon.
REG_MUTANTES = [
    ('the bitmaps of signal-high and signal-medium swapped in the registry',
     [('IconeNome::SIGNAL_HIGH, 16, EXTRA_SIGNAL_HIGH_16_bits', 'IconeNome::SIGNAL_HIGH, 16, EXTRA_SIGNAL_MEDIUM_16_bits'),
      ('IconeNome::SIGNAL_MEDIUM, 16, EXTRA_SIGNAL_MEDIUM_16_bits', 'IconeNome::SIGNAL_MEDIUM, 16, EXTRA_SIGNAL_HIGH_16_bits')], 1),
    ('the bitmaps of signal-low and signal swapped in the registry',
     [('IconeNome::SIGNAL_LOW, 16, EXTRA_SIGNAL_LOW_16_bits', 'IconeNome::SIGNAL_LOW, 16, EXTRA_SIGNAL_16_bits'),
      ('IconeNome::SIGNAL, 16, EXTRA_SIGNAL_16_bits', 'IconeNome::SIGNAL, 16, EXTRA_SIGNAL_LOW_16_bits')], 1),
]

CONTA = [0]
FALHAS = []
MODELO = [0, 0]   # the checks and the failures the C++ model counted, summed over its runs


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


# ---- 1. the original's thresholds, read from its source ----
def escada_do_original():
    """(no link, 4 bars, 3 bars, 2 bars) of DisplayManager::drawTopBar( ), as ints, or None when the code is not of the shape this reads."""
    t = apaga(ORIGINAL.read_text(encoding='utf-8'))
    corpo = corpo_da_funcao(t, r'void\s+DisplayManager::drawTopBar\s*\(')
    if not confere(corpo is not None, 'DisplayManager::drawTopBar( ) is not in src/ui/original/DisplayManager_Dashboard.cpp exactly once'):
        return None
    cabo = re.findall(r'state\.wifiRssi\s*>\s*(-\d+)\s*\)\s*\?\s*C_TEMP_OK\s*:\s*C_BAR_BG', corpo)
    escada = re.findall(r'int\s+barras\s*=\s*0\s*;\s*if\s*\(\s*state\.wifiRssi\s*>\s*(-\d+)\s*\)\s*\{\s*'
                        r'if\s*\(\s*state\.wifiRssi\s*>\s*(-\d+)\s*\)\s*barras\s*=\s*4\s*;\s*'
                        r'else\s+if\s*\(\s*state\.wifiRssi\s*>\s*(-\d+)\s*\)\s*barras\s*=\s*3\s*;\s*'
                        r'else\s+if\s*\(\s*state\.wifiRssi\s*>\s*(-\d+)\s*\)\s*barras\s*=\s*2\s*;\s*'
                        r'else\s+barras\s*=\s*1\s*;\s*\}', corpo)
    ok = confere(len(cabo) == 1, f'the cable glyph of drawTopBar( ) (`state.wifiRssi > N ? C_TEMP_OK : C_BAR_BG`) is not there exactly once ({len(cabo)}): read the original again')
    ok &= confere(len(escada) == 1, f'the Wi-Fi ladder of drawTopBar( ) (`barras` 4 / 3 / 2 / 1 under `> N`) is not there exactly once ({len(escada)}): read the original again')
    if not ok:
        return None
    sem, n4, n3, n2 = (int(x) for x in escada[0])
    confere(int(cabo[0]) == sem, f'the cable glyph tests > {cabo[0]} and the ladder > {sem}: they were one threshold, no link')
    confere(n4 > n3 > n2 > sem, f'the ladder is not descending: {n4} {n3} {n2} {sem}')
    return sem, n4, n3, n2


def esperado_inc(sem, n4, n3, n2):
    return (f'constexpr int ANTIGO_SEM_LINK = {sem};\nconstexpr int ANTIGO_4_BARRAS = {n4};\n'
            f'constexpr int ANTIGO_3_BARRAS = {n3};\nconstexpr int ANTIGO_2_BARRAS = {n2};\n')


# ---- the glue's piece ----
CABECA = r'void\s+DisplayManager::uaVmStatus\s*\(\s*ua::VmStatus\s*&\s*s\s*\)'


def peca_do_glue(glue):
    limpo = apaga(glue.read_text(encoding='utf-8'))
    corpo = corpo_da_funcao(limpo, CABECA)
    if not confere(corpo is not None, f'DisplayManager::uaVmStatus( ua::VmStatus &s ) is not in {glue.name} exactly once'):
        return None
    return {PECA: 'void DisplayManager::uaVmStatus( ua::VmStatus &s ) ' + corpo + '\n'}


def um_so_lugar(corpo):
    """Every assignment to a `.rede` under src/ui/angulo is inside uaVmStatus( )."""
    padrao = re.compile(r'\.rede\s*=(?!=)')
    total, donos = 0, []
    for f in sorted(list(UA.rglob('*.cpp')) + list(UA.rglob('*.h'))):
        n = len(padrao.findall(apaga(f.read_text(encoding='utf-8'))))
        total += n
        if n:
            donos.append(f.relative_to(ROOT).as_posix())
    dentro = len(padrao.findall(corpo))
    confere(dentro >= 1 and total == dentro, f'{total} assignment(s) to a .rede under src/ui/angulo ({", ".join(donos) or "none"}), {dentro} of them in uaVmStatus( ): the glyph is decided in one place')


# ---- building and running the model ----
def compila(src, obj, inc, estrito):
    r = subprocess.run(['g++', *inc, *(['-Wall', '-Wextra', '-Werror'] if estrito else ['-w']), '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
    return r.returncode, r.stderr


def kit(tmp, gfx):
    telas = UA / 'telas'
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{SHIM}', f'-I{gfx}', f'-I{UA}', f'-I{telas}']
    unidades = [(gfx / 'Adafruit_GFX.cpp', False), (UA / 'Angulo.cpp', True), (UA / 'Textos.cpp', True), (telas / 'Telas.cpp', True), (telas / 'Base.cpp', True)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as ex:
        fs = [(ex.submit(compila, s, tmp / (s.parent.name + '_' + s.stem + '.o'), inc, e), s) for s, e in unidades]
        objs = []
        for f, s in fs:
            rc, err = f.result()
            if rc:
                print(f'cannot build {s.name}:\n{err}')
                return None, None
            objs.append(str(tmp / (s.parent.name + '_' + s.stem + '.o')))
    return objs, inc


def roda(tmp, nome, pecas, wifi, objs, inc, pasta=None):
    """(returncode, output) of the model built from `pecas`; returncode None when it does not compile."""
    d = tmp / nome
    d.mkdir()
    for k, v in pecas.items():
        (d / k).write_text(v, encoding='utf-8')
    obj = d / 'main.o'
    r = subprocess.run(['g++', *inc, f'-I{d}', f'-DSIMUT_NET_WIFI={wifi}', '-Wall', '-Wextra', '-c', str(MAIN), '-o', str(obj)], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[:3000]
    exe = d / 'modelo'
    r = subprocess.run(['g++', *objs, str(obj), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[:3000]
    r = subprocess.run([str(exe)] + ([str(pasta)] if pasta else []), capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


def mata(nome, f, rotulo):
    """Judge one mutant's run: it must be killed by a check (exit 1), never by the compiler."""
    rc, out = f.result()
    if rc is None:
        confere(False, f'{rotulo} "{nome}" does not compile (a mutant is killed by a check, not by the compiler): {out.strip().splitlines()[0][:160] if out.strip() else ""}')
        return
    confere(rc == 1, f'{rotulo} "{nome}" must fail the model (exit 1), and it exited {rc}')
    if rc == 1:
        print(f'  killed: {nome} (' + (out.strip().splitlines() or ['no output'])[0][:110] + ')')


def modelo_e_mutantes(tmp, pecas, objs, inc, quadros):
    base_ok = True
    for wifi in (1, 0):
        sub = quadros / ('wifi' if wifi else 'cabo')
        sub.mkdir()
        rc, out = roda(tmp, f'base{wifi}', pecas, wifi, objs, inc, sub)
        print(out.rstrip())
        for m in re.finditer(r'(\d+) checks, (\d+) failed', out):
            MODELO[0] += int(m.group(1))
            MODELO[1] += int(m.group(2))
        if confere(rc == 0, f'the glue model, {"Wi-Fi" if wifi else "cable"} image, passes (exit {rc})'):
            continue
        base_ok = False
    if not base_ok:
        print('mutants not run: the unmutated model fails (the base has no code to mutate)')
        return
    trabalho = []
    for k, (nome, peca, antigo, novo, wifi) in enumerate(MUTANTES):
        if not confere(pecas[peca].count(antigo) == 1, f'mutant "{nome}": its anchor `{antigo}` is not in {peca} exactly once'):
            continue
        mut = dict(pecas)
        mut[peca] = pecas[peca].replace(antigo, novo)
        trabalho.append((k, nome, mut, wifi, objs))
    telas = TELAS.read_text(encoding='utf-8')
    for k, (nome, antigo, novo, wifi) in enumerate(KIT_MUTANTES):
        if not confere(telas.count(antigo) == 1, f'kit mutant "{nome}": its anchor `{antigo}` is not in Telas.cpp exactly once'):
            continue
        d = tmp / f'kitmut{k}'
        d.mkdir()
        (d / 'Telas.cpp').write_text(telas.replace(antigo, novo), encoding='utf-8')
        rc, err = compila(d / 'Telas.cpp', d / 'Telas.o', inc, True)
        if not confere(rc == 0, f'kit mutant "{nome}" does not compile: {err.strip().splitlines()[0][:160] if err.strip() else ""}'):
            continue
        trabalho.append((1000 + k, nome, pecas, wifi, [str(d / 'Telas.o') if o.endswith('telas_Telas.o') else o for o in objs]))
    cabecalho = (UA / 'AnguloIcones.h').read_text(encoding='utf-8')
    for k, (nome, trocas, wifi) in enumerate(REG_MUTANTES):
        mut_h, achou = cabecalho, True
        for antigo, novo in trocas:
            if not confere(mut_h.count(antigo) == 1, f'registry mutant "{nome}": its anchor `{antigo}` is not in AnguloIcones.h exactly once'):
                achou = False
                break
            mut_h = mut_h.replace(antigo, novo)
        if not achou:
            continue
        d = tmp / f'regmut{k}'
        d.mkdir()
        (d / 'AnguloIcones.h').write_text(mut_h, encoding='utf-8')   # found first by Angulo.cpp's quoted include; every other header comes from -I{UA}
        shutil.copy(UA / 'Angulo.cpp', d / 'Angulo.cpp')
        rc, err = compila(d / 'Angulo.cpp', d / 'Angulo.o', inc, True)
        if not confere(rc == 0, f'registry mutant "{nome}" does not compile: {err.strip().splitlines()[0][:160] if err.strip() else ""}'):
            continue
        trabalho.append((2000 + k, nome, pecas, wifi, [str(d / 'Angulo.o') if o.endswith('angulo_Angulo.o') else o for o in objs]))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        fs = [(k, nome, ex.submit(roda, tmp, f'mut{k}', mut, wifi, mobjs, inc)) for k, nome, mut, wifi, mobjs in trabalho]
        for k, nome, f in fs:
            mata(nome, f, 'registry mutant' if k >= 2000 else ('kit mutant' if k >= 1000 else 'mutant'))


# ---- the frames ----
def png_dos_quadros(pasta, saida):
    from PIL import Image, ImageDraw
    saida.mkdir(parents=True, exist_ok=True)
    imgs, linhas = {}, []
    for sub in ('wifi', 'cabo'):
        ordem = pasta / sub / '_ordem.txt'
        if not ordem.exists():
            continue
        for ln in ordem.read_text(encoding='utf-8').splitlines():
            nome, rssi, estado = ln.split()
            linhas.append((nome, rssi, estado))
            for tema in ('escuro', 'claro'):
                v = memoryview((pasta / sub / f'{nome}_{tema}.565').read_bytes()).cast('H')
                px = bytearray()
                for w in v:
                    r5, g6, b5 = (w >> 11) & 31, (w >> 5) & 63, w & 31
                    px += bytes(((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)))
                img = Image.frombytes('RGB', (320, 40), bytes(px))
                img.save(saida / f'{nome}_{tema}.png')
                imgs[(nome, tema)] = img
    esc = 2
    folha = Image.new('RGB', (150 + 2 * (320 * esc + 10), 10 + len(linhas) * (40 * esc + 10)), (128, 128, 128))
    d = ImageDraw.Draw(folha)
    for i, (nome, rssi, estado) in enumerate(linhas):
        y = 10 + i * (40 * esc + 10)
        d.text((4, y + 24), f'{nome} {rssi} dBm', fill=(255, 255, 255))
        d.text((4, y + 40), estado, fill=(255, 255, 255))
        for j, tema in enumerate(('escuro', 'claro')):
            folha.paste(imgs[(nome, tema)].resize((320 * esc, 40 * esc), Image.NEAREST), (150 + j * (320 * esc + 10), y))
    folha.save(saida / '_barras.png')
    print(f'frames: {len(imgs)} PNGs and the contact sheet in {saida}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', help='keep the frames here: a PNG per frame and a contact sheet')
    ap.add_argument('--glue', help='read uaVmStatus( ) from this file instead of the tree\'s glue (the controls)')
    args = ap.parse_args()
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found: run pio once', file=sys.stderr)
        return 2
    escada = escada_do_original()
    if escada is None:
        print(f'RESULT: {CONTA[0]} checks, {len(FALHAS)} failed')
        return 1
    glue = pathlib.Path(args.glue) if args.glue else GLUE
    pecas = peca_do_glue(glue)
    if pecas is None:
        print(f'RESULT: {CONTA[0]} checks, {len(FALHAS)} failed')
        return 1
    pecas['esperado.inc'] = esperado_inc(*escada)
    print(f'the original\'s ladder, read from {ORIGINAL.relative_to(ROOT)}: no link at {escada[0]} or less, then 4 bars above {escada[1]}, 3 above {escada[2]}, 2 above {escada[3]}, else 1')
    if not args.glue:
        um_so_lugar(pecas[PECA])
    with tempfile.TemporaryDirectory() as t:
        tmp = pathlib.Path(t)
        objs, inc = kit(tmp, gfx)
        if objs is None:
            return 2
        quadros = tmp / 'quadros'
        quadros.mkdir()
        modelo_e_mutantes(tmp, pecas, objs, inc, quadros)
        if args.out and any(quadros.glob('*/*.565')):
            png_dos_quadros(quadros, pathlib.Path(args.out))
    print(f'RESULT: {CONTA[0]} script checks, {MODELO[0]} model checks, {len(FALHAS)} script failures, {MODELO[1]} model failures')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
