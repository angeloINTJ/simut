#!/usr/bin/env python3
"""test_rede.py -- the network glyph of the Angulo's status bar (DisplayManager::uaVmStatus( )), on the host (S10c, the Wi-Fi status bar).

Until this fix uaVmStatus( ) answered ua::Rede::CABO or CABO_SEM_LINK on every image, so the Pico 2 W's release (Wi-Fi, the Angulo since S10c C3) drew a cable. The original panel decides the glyph of
its top bar (DisplayManager::drawTopBar( ), src/DisplayManager_Dashboard.cpp) from the same number, the level NetworkManager::getRssi( ) gives: on a cable image the cable, green above -100 and grey at or
below; on a Wi-Fi image four bars, none at -100 or less, one up to -75, two up to -65, three up to -55, four above. The Angulo's icon has three Wi-Fi states and no levels: no signal (WIFI_SEM), weak
(WIFI_FRACO: the old bar's lowest level, one bar) and good (WIFI: two to four bars). This holds it:

  1. The original's thresholds, READ from its source and not copied: the cable's `> -100` and the ladder -100 / -55 / -65 / -75 of the Wi-Fi image's `barras`. If the ladder moves or changes shape
     this fails and says so, and the glue's constants and the named points of host/test_rede_main.cpp are to be read again.
  2. The glue RUN (host/test_rede_main.cpp): uaVmStatus( ) cut out of the glue verbatim, without comments, and compiled with the real kit against a stand-in, once as a Wi-Fi image (-DSIMUT_NET_WIFI=1) and
     once as the cable's (=0): every level from -200 to +20 dBm and the extremes of int against the old ladder, the named points (strong, each threshold and the step beside it, weak, disconnected),
     the fields the function does not own, and the frames: the status bar drawn by the real kit from the view model the glue made (no icon missing, the alert ink only in the weak frame, the danger ink only in
     the disconnected one, a different picture for each state).
  3. The mutants: the cut piece edited one statement at a time, each of which the model must fail (the old line back on a Wi-Fi image among them). A mutant that does not compile, or an anchor not found
     exactly once, fails this script: a mutant must be killed by a check and not by the compiler.
  4. One place decides the glyph: every assignment to a `.rede` under src/ui/angulo is in uaVmStatus( ).

Usage: python3 tools/tft_ui/test_rede.py [--out DIR] [--glue FILE]   exit 0 when all hold, 1 when not, 2 when it cannot build (no Adafruit GFX 1.12.6 in .pio/libdeps: run pio once).
  --out DIR   keeps the frames there: a PNG per frame and a contact sheet (_barras.png).
  --glue FILE reads uaVmStatus( ) from FILE instead of src/ui/angulo/DisplayManager_UiA.cpp (the controls: the glue of the commit before, or the old line put back); the source guard is then skipped.
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from host_ui import find_gfx  # noqa: E402  (the GFX lookup the host tools share)

UA = ROOT / 'src' / 'ui' / 'angulo'
GLUE = UA / 'DisplayManager_UiA.cpp'
ORIGINAL = ROOT / 'src' / 'DisplayManager_Dashboard.cpp'
MAIN = HERE / 'host' / 'test_rede_main.cpp'
SHIM = HERE / 'host' / 'shim'
PECA = 'uaVmStatus.inc'

# (name, the piece, the statement, what replaces it, the build: 1 a Wi-Fi image, 0 the cable's): each edit must make the model fail. The statements are the glue's, as cut (comments removed).
MUTANTES = [
    ('the cable states back on a Wi-Fi image (the old line)', PECA, 'if constexpr ( SIMUT_NET_WIFI )', 'if constexpr ( 0 )', 1),
    ('the radio\'s states on a cable image', PECA, 'if constexpr ( SIMUT_NET_WIFI )', 'if constexpr ( 1 )', 0),
    ('weak up to -65 (two bars called weak)', PECA, 'UMA_BARRA = -75;', 'UMA_BARRA = -65;', 1),
    ('weak only from -85', PECA, 'UMA_BARRA = -75;', 'UMA_BARRA = -85;', 1),
    ('the weak threshold inclusive', PECA, 'nivel > UMA_BARRA', 'nivel >= UMA_BARRA', 1),
    ('no link from -99 (Wi-Fi image)', PECA, 'SEM_SINAL = -100;', 'SEM_SINAL = -99;', 1),
    ('no link only below -101 (cable image)', PECA, 'SEM_SINAL = -100;', 'SEM_SINAL = -101;', 0),
    ('the no-link threshold inclusive (Wi-Fi image)', PECA, 'nivel > SEM_SINAL ? ( nivel', 'nivel >= SEM_SINAL ? ( nivel', 1),
    ('the no-link threshold inclusive (cable image)', PECA, 'nivel > SEM_SINAL ? ua::Rede::CABO :', 'nivel >= SEM_SINAL ? ua::Rede::CABO :', 0),
    ('weak and none swapped', PECA, 'ua::Rede::WIFI_FRACO ) : ua::Rede::WIFI_SEM;', 'ua::Rede::WIFI_SEM ) : ua::Rede::WIFI_FRACO;', 1),
    ('the ladder inverted (good called weak)', PECA, 'ua::Rede::WIFI : ua::Rede::WIFI_FRACO )', 'ua::Rede::WIFI_FRACO : ua::Rede::WIFI )', 1),
    ('no link answers NENHUMA (Wi-Fi image)', PECA, ': ua::Rede::WIFI_SEM;', ': ua::Rede::NENHUMA;', 1),
    ('no link answers NENHUMA (cable image)', PECA, 'ua::Rede::CABO : ua::Rede::CABO_SEM_LINK;', 'ua::Rede::CABO : ua::Rede::NENHUMA;', 0),
    ('the cable states swapped', PECA, 'ua::Rede::CABO : ua::Rede::CABO_SEM_LINK;', 'ua::Rede::CABO_SEM_LINK : ua::Rede::CABO;', 0),
    ('the level not read', PECA, 'const int nivel = _lastRenderedState.wifiRssi;', 'const int nivel = -50;', 1),
    ('pendentes dropped', PECA, 's.pendentes = _lastRenderedState.pendingPkts;', 's.pendentes = 0;', 1),
]

CONTA = [0]
FALHAS = []


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
    if not confere(corpo is not None, 'DisplayManager::drawTopBar( ) is not in src/DisplayManager_Dashboard.cpp exactly once'):
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


def modelo_e_mutantes(tmp, pecas, objs, inc, quadros):
    base_ok = True
    for wifi in (1, 0):
        rc, out = roda(tmp, f'base{wifi}', pecas, wifi, objs, inc, quadros)
        print(out.rstrip())
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
        trabalho.append((k, nome, mut, wifi))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
        fs = [(nome, ex.submit(roda, tmp, f'mut{k}', mut, wifi, objs, inc)) for k, nome, mut, wifi in trabalho]
        for nome, f in fs:
            rc, out = f.result()
            if rc is None:
                confere(False, f'mutant "{nome}" does not compile (a mutant is killed by a check, not by the compiler): {out.strip().splitlines()[0][:160] if out.strip() else ""}')
                continue
            confere(rc == 1, f'mutant "{nome}" must fail the model (exit 1), and it exited {rc}')
            if rc == 1:
                print(f'  killed: {nome} (' + (out.strip().splitlines() or ['no output'])[0][:110] + ')')


# ---- the frames ----
def png_dos_quadros(pasta, saida):
    from PIL import Image, ImageDraw
    saida.mkdir(parents=True, exist_ok=True)
    ordem = ['forte', 'limite_bom', 'limite_fraco', 'fraco', 'sem', 'cabo', 'cabo_sem']
    imgs = {}
    for f in sorted(pasta.glob('*.565')):
        v = memoryview(f.read_bytes()).cast('H')
        px = bytearray()
        for w in v:
            r5, g6, b5 = (w >> 11) & 31, (w >> 5) & 63, w & 31
            px += bytes(((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)))
        img = Image.frombytes('RGB', (320, 40), bytes(px))
        img.save(saida / (f.stem + '.png'))
        imgs[f.stem] = img
    linhas = [n for n in ordem if f'{n}_escuro' in imgs]
    esc = 2
    folha = Image.new('RGB', (110 + 2 * (320 * esc + 10), 10 + len(linhas) * (40 * esc + 10)), (128, 128, 128))
    d = ImageDraw.Draw(folha)
    for i, n in enumerate(linhas):
        y = 10 + i * (40 * esc + 10)
        d.text((4, y + 12), n, fill=(255, 255, 255))
        for j, tema in enumerate(('escuro', 'claro')):
            folha.paste(imgs[f'{n}_{tema}'].resize((320 * esc, 40 * esc), Image.NEAREST), (110 + j * (320 * esc + 10), y))
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
        if args.out and any(quadros.glob('*.565')):
            png_dos_quadros(quadros, pathlib.Path(args.out))
    print(f'RESULT: {CONTA[0]} checks, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
