#!/usr/bin/env python3
"""test_confirma_ap.py -- the Angulo's own frame for the setup-network confirmation (MODE_CONFIRM_AP), on the host (S10c, D7).

Before D7 the mode had no frame in the Angulo: uaMontarBase( ) answered false, uaMontar( ) went on to uaMontarPendente( ) and the glass said "This screen (mode N)...". D7 makes the mode
the third user of the confirmation dialog (VmDialogo), after the mute and the account-delete confirmations, in two members of DisplayManager (uaMontarConfirmaAp( ) and
uaToqueConfirmaAp( )) defined in ONE `#if SIMUT_NET_WIFI` block of DisplayManager_UiA.cpp, with stubs in its #else for the Wire (the wired Angulo image cannot reach the mode). This holds it:

  1. The sources: the MODE_CONFIRM_AP case in both dispatchers, one `#if SIMUT_NET_WIFI` (with its #else) in the glue file and no other `#if` on the network class (uaVmStatus( ) reads the class as an `if constexpr`, which check_feature_sprawl.py counts), both members
     defined only inside it, both declared in Members.h.
  2. The glue RUN (host/test_confirma_ap_main.cpp): the pieces above cut out of the glue verbatim, without comments, and compiled with the real kit against a stand-in, once as a Wi-Fi image
     (-DSIMUT_NET_WIFI=1) and once as the wired one (=0): the frame (texts, destructive weight, two targets, the body in at most four lines and none cut) and the touch (Cancel: the
     original's zone 0xC2 and showSettingsMain( ) once, no event; Confirm: zone 0xC3 and the real actStartAp( ), one EVT_START_AP, the mode left to Core 0; no target and a refused gate: nothing).
  3. The mutants: the cut pieces edited one statement at a time, each of which the model must fail (SIM and NAO swapped, the 0xC3 gate dropped, Cancel calling forceDashboard( ), and more).
  4. The texts in the packs: the three new keys (and the existing "Cancel") in the @ANGULO of both, by the hash of the English.
  5. The frames: host_telas.py base --idioma en|pt|es draws the sample dialogo_ap (exit 0: nothing outside the safe area, over another text, across a target's edge, cut with "..."), and its
     records hold the title, the body in at most four lines, Cancel and Confirm, each the language's text, in both themes.
  6. The gates: check_ui_texts.py and check_lang_packs.py exit 0.

Usage: python3 tools/tft_ui/test_confirma_ap.py   exit 0 when all hold, 1 when not, 2 when it cannot build (no Adafruit GFX 1.12.6 in .pio/libdeps: run pio once).
"""
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('LANG_GATE_NO_RUN', '1')   # tools/check_lang_packs.py runs its gate on import otherwise
import check_lang_packs as gate  # noqa: E402
import host_telas  # noqa: E402
from host_ui import find_gfx  # noqa: E402

UA = ROOT / 'src' / 'ui' / 'angulo'
GLUE = UA / 'DisplayManager_UiA.cpp'
MEMBROS = UA / 'Members.h'
ACOES = ROOT / 'src' / 'DisplayManager_Actions.h'
MAIN = HERE / 'host' / 'test_confirma_ap_main.cpp'

TITULO = 'Open the setup network?'
CORPO = 'The device leaves its Wi-Fi network and opens its own setup network. Telemetry stops until it rejoins, 15 minutes later.'
CONFIRMA = 'Start setup'
CANCELA = 'Cancel'
# (title, body, Confirm) a pack gives; Cancel is the existing key. The es-ES drafts await the native review (S10c plan, D7).
TRADUCOES = {
    'pt': ('Abrir a rede de configuração?',
           'O aparelho sai da rede Wi-Fi e abre a própria rede de configuração. A telemetria para até ele voltar, 15 minutos depois.', 'Abrir rede'),
    'es': ('¿Abrir la red de configuración?',
           'El equipo sale de la red Wi-Fi y abre su propia red de configuración. La telemetría se detiene hasta que vuelva, 15 minutos después.', 'Abrir red'),
}
PACOTES = {'pt': 'language_pt-BR.lng', 'es': 'language_es-ES.lng'}

# (name, the piece, the statement, what replaces it, the build): each edit must make the model fail. The statements are the glue's, as cut (comments removed).
MUTANTES = [
    ('SIM and NAO swapped', 'membros.inc', 'a->tipo == DLG_SIM', 'a->tipo == DLG_NAO', 1),
    ('the 0xC3 gate dropped', 'membros.inc', 'if ( acceptTouch( 0xC3 ) ) actStartAp( );', 'actStartAp( );', 1),
    ('Cancel calling forceDashboard( )', 'membros.inc', 'showSettingsMain( )', 'forceDashboard( )', 1),
    ('the Cancel gate 0xC2 -> 0xC0 (the mute one)', 'membros.inc', 'acceptTouch( 0xC2 )', 'acceptTouch( 0xC0 )', 1),
    ('the Confirm gate 0xC3 -> 0xC2', 'membros.inc', 'acceptTouch( 0xC3 )', 'acceptTouch( 0xC2 )', 1),
    ('Confirm sends no event', 'membros.inc', 'actStartAp( );', '( void )0;', 1),
    ('the null target unguarded', 'membros.inc', 'if ( !a ) return UaToque::TRATADO;', '', 1),
    ('the dialog not destructive', 'membros.inc', 'vm.destrutivo = true;', 'vm.destrutivo = false;', 1),
    ('a Cancel label of another key', 'membros.inc', 'UA_TXT( "Cancel" )', 'UA_TXT( "Back" )', 1),
    ('the title of another text', 'membros.inc', 'UA_TXT( "Open the setup network?" )', 'UA_TXT( "Start the setup network?" )', 1),
    ('the wired stub claims the frame', 'membros.inc', 'return false;', 'return true;', 0),
    ('the wired stub takes the touch', 'membros.inc', 'return UaToque::NAO_E_MEU;', 'return UaToque::TRATADO;', 0),
    ('the case of uaMontarBase( ) gone', 'montar_caso.inc', 'return uaMontarConfirmaAp( q );', 'return false;', 1),
    ('the case of uaToqueBase( ) gone', 'toque_caso.inc', 'return uaToqueConfirmaAp( a );', 'return UaToque::NAO_E_MEU;', 1),
]

CONTA = [0]
FALHAS = []


def confere(cond, msg):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    return bool(cond)


# ---- cutting the glue (comments removed, newlines kept; strings and chars left alone; 1'000 is a digit separator) ----
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


def fatia_do_caso(corpo, modo='MODE_CONFIRM_AP'):
    """The statements of `case <modo>:` up to the next label of the same switch, or None when the case is not there exactly once."""
    m = list(re.finditer(r'\bcase\s+' + modo + r'\s*:', corpo))
    if len(m) != 1:
        return None
    rot = re.compile(r'(?:case\s+\w+\s*:|default\s*:)')
    i, prof = m[0].end(), 0
    while i < len(corpo):
        c = corpo[i]
        if c == '{':
            prof += 1
        elif c == '}':
            if prof == 0:
                break
            prof -= 1
        elif prof == 0 and rot.match(corpo, i) and not (corpo[i - 1].isalnum() or corpo[i - 1] == '_'):
            break
        i += 1
    return corpo[m[0].end():i] + '\n'


def blocos_se(limpo, padrao):
    """[(first line, text)] of the conditional blocks (to the matching #endif) whose opening line matches `padrao`."""
    linhas = limpo.split('\n')
    achados = []
    for k, ln in enumerate(linhas):
        if re.match(padrao, ln):
            prof = 0
            for j in range(k, len(linhas)):
                if re.match(r'\s*#\s*if(?:n?def)?\b', linhas[j]):
                    prof += 1
                elif re.match(r'\s*#\s*endif\b', linhas[j]):
                    prof -= 1
                    if prof == 0:
                        achados.append((k + 1, '\n'.join(linhas[k:j + 1]) + '\n'))
                        break
    return achados


def pecas_do_glue():
    """{file name: text} for the host program, and the problems with the sources (item 1). A piece not found is empty, so that the model still runs and fails by what it checks."""
    pecas = {'montar_caso.inc': '', 'toque_caso.inc': '', 'membros.inc': '', 'decls.inc': '', 'acao.inc': ''}
    limpo = apaga(GLUE.read_text(encoding='utf-8'))
    fm = corpo_da_funcao(limpo, r'bool\s+DisplayManager::uaMontarBase\s*\(')
    ft = corpo_da_funcao(limpo, r'UaToque\s+DisplayManager::uaToqueBase\s*\(')
    confere(fm is not None and ft is not None, 'uaMontarBase( ) and uaToqueBase( ) were not found exactly once in DisplayManager_UiA.cpp')
    cm = fatia_do_caso(fm) if fm else None
    ct = fatia_do_caso(ft) if ft else None
    if confere(cm is not None and 'uaMontarConfirmaAp' in cm, 'uaMontarBase( ) has no `case MODE_CONFIRM_AP:` returning uaMontarConfirmaAp( q ): the mode falls to uaMontarPendente( ) and the glass says "This screen (mode N)..."'):
        pecas['montar_caso.inc'] = cm
    if confere(ct is not None and 'uaToqueConfirmaAp' in ct, 'uaToqueBase( ) has no `case MODE_CONFIRM_AP:` returning uaToqueConfirmaAp( a ): the touch on the confirmation goes to the next group'):
        pecas['toque_caso.inc'] = ct
    se_rede = re.compile(r'^\s*#\s*(?:if|elif)\b[^\n]*SIMUT_NET_')
    condicionais = [ln for ln in limpo.split('\n') if se_rede.match(ln)]
    confere(len(condicionais) == 1 and re.match(r'^\s*#\s*if\s+SIMUT_NET_WIFI\s*$', condicionais[0]),
            f'DisplayManager_UiA.cpp must hold exactly one `#if` on the network class, `#if SIMUT_NET_WIFI` (the sprawl watermark 364 -> 365): it holds {len(condicionais)}')
    blocos = blocos_se(limpo, r'^\s*#\s*if\s+SIMUT_NET_WIFI\s*$')
    if confere(len(blocos) == 1 and re.search(r'^\s*#\s*else\b', blocos[0][1], re.M), 'the `#if SIMUT_NET_WIFI` block with an #else (the wired stubs) was not found exactly once'):
        pecas['membros.inc'] = blocos[0][1]
    for nome in ('uaMontarConfirmaAp', 'uaToqueConfirmaAp'):
        padrao = r'\bDisplayManager::' + nome + r'\s*\('
        total = len(re.findall(padrao, limpo))
        dentro = len(re.findall(padrao, pecas['membros.inc']))
        confere(total == 2 and dentro == 2, f'DisplayManager::{nome}( ) must be defined twice and only inside the #if block (the real body and the stub): {total} in the file, {dentro} inside')
    mh = apaga(MEMBROS.read_text(encoding='utf-8'))
    decls = [ln.strip() for ln in mh.split('\n') if re.search(r'\bua(?:Montar|Toque)ConfirmaAp\b', ln)]
    confere(len(decls) == 2 and all(d.endswith(';') for d in decls), f'ui/angulo/Members.h must declare uaMontarConfirmaAp( ) and uaToqueConfirmaAp( ): {len(decls)} declaration(s) found')
    pecas['decls.inc'] = '\n'.join(decls) + '\n'
    ac = apaga(ACOES.read_text(encoding='utf-8'))
    m = list(re.finditer(r'void\s+DisplayManager::actStartAp\s*\(', ac))
    if confere(len(m) == 1, 'DisplayManager::actStartAp( ) was not found exactly once in DisplayManager_Actions.h'):
        ini = ac.rfind('\n', 0, m[0].start()) + 1
        fecha = casa_chaves(ac, ac.find('{', m[0].end()))
        pecas['acao.inc'] = ac[ini:fecha + 1] + '\n'
    return pecas


def literal(s):
    assert '"' not in s and '\\' not in s
    return '"' + s + '"'


def esperado():
    return ''.join(f'[[maybe_unused]] static const char *const {k} = {literal(v)};\n' for k, v in (('TITULO', TITULO), ('CORPO', CORPO), ('CONFIRMA', CONFIRMA), ('CANCELA', CANCELA)))


# ---- building and running the model ----
def compila(src, obj, inc, estrito):
    r = subprocess.run(['g++', *inc, *(['-Wall', '-Wextra', '-Werror'] if estrito else ['-w']), '-c', str(src), '-o', str(obj)], capture_output=True, text=True)
    return r.returncode, r.stderr


def kit(tmp, gfx):
    telas = UA / 'telas'
    inc = ['-std=gnu++17', '-O1', '-DARDUINO=100', '-DUA_HOST', f'-I{host_telas.SHIM}', f'-I{gfx}', f'-I{UA}', f'-I{telas}']
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


def roda(tmp, nome, pecas, wifi, objs, inc):
    """(returncode, output) of the model built from `pecas`; returncode None when it does not compile."""
    d = tmp / nome
    d.mkdir()
    for k, v in {**pecas, 'esperado.inc': esperado()}.items():
        (d / k).write_text(v, encoding='utf-8')
    obj = d / 'main.o'
    r = subprocess.run(['g++', *inc, f'-I{d}', f'-DSIMUT_NET_WIFI={wifi}', '-Wall', '-Wextra', '-c', str(MAIN), '-o', str(obj)], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[:3000]
    exe = d / 'modelo'
    r = subprocess.run(['g++', *objs, str(obj), '-o', str(exe)], capture_output=True, text=True)
    if r.returncode:
        return None, r.stderr[:3000]
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    return r.returncode, r.stdout + r.stderr


def modelo_e_mutantes(tmp, pecas, objs, inc):
    base_ok = True
    for wifi in (1, 0):
        rc, out = roda(tmp, f'base{wifi}', pecas, wifi, objs, inc)
        print(out.rstrip())
        if confere(rc == 0, f'the glue model, {"Wi-Fi" if wifi else "wired"} branch, passes (exit {rc})'):
            continue
        base_ok = False
    if not base_ok:
        print('mutants not run: the unmutated model fails (the base has no code to mutate)')
        return
    for k, (nome, peca, antigo, novo, wifi) in enumerate(MUTANTES):
        if not confere(pecas[peca].count(antigo) == 1, f'mutant "{nome}": its anchor `{antigo}` is not in {peca} exactly once'):
            continue
        mut = dict(pecas)
        mut[peca] = pecas[peca].replace(antigo, novo)
        rc, out = roda(tmp, f'mut{k}', mut, wifi, objs, inc)
        morto = rc is None or rc != 0
        confere(morto, f'mutant "{nome}" must fail the model, and it passed')
        if morto:
            print(f'  killed: {nome} (' + (out.strip().splitlines() or ['no output'])[0][:110] + ')')


# ---- the packs and the frames ----
def pacote(lang):
    secao, prob = host_telas.secao_angulo(ROOT / 'data' / 'lang' / PACOTES[lang])
    if secao is None:
        return {}
    return gate.angulo_entries(secao)[0]


def textos_dos_pacotes():
    for lang, (titulo, corpo, confirma) in TRADUCOES.items():
        entradas = pacote(lang)
        for en, esperado_ in ((TITULO, titulo), (CORPO, corpo), (CONFIRMA, confirma)):
            got = entradas.get(gate.fnv1a32(en))
            confere(got == esperado_, f'{PACOTES[lang]}: the line {gate.fnv1a32(en):08x} for {en[:34]!r} reads {got!r}, wanted {esperado_!r}')
        confere(bool(entradas.get(gate.fnv1a32(CANCELA))), f'{PACOTES[lang]}: the existing Cancel line is gone')


def ler_registros(caminho):
    regs = []
    for ln in caminho.read_text(encoding='utf-8').splitlines():
        p = ln.split(' ', 5)
        if len(p) == 6:
            regs.append((int(p[0]), int(p[1]), int(p[2]), int(p[3]), p[4], p[5]))
    return regs


def quadros(tmp):
    for lang in ('en', 'pt', 'es'):
        if lang == 'en':
            titulo, corpo, confirma, cancela = TITULO, CORPO, CONFIRMA, CANCELA
        else:
            titulo, corpo, confirma = TRADUCOES[lang]
            cancela = pacote(lang).get(gate.fnv1a32(CANCELA), '?')
        d = tmp / ('quadros_' + lang)
        d.mkdir()
        r = subprocess.run([sys.executable, '-B', str(HERE / 'host_telas.py'), 'base', '--idioma', lang, '--out', str(d)], capture_output=True, text=True)
        if not confere(r.returncode == 0, f'host_telas.py base --idioma {lang} exits 0 (exit {r.returncode}): ' + ' | '.join((r.stdout + r.stderr).strip().splitlines()[:3])[:420]):
            continue
        for tema in ('claro', 'escuro'):
            arq = d / f'dialogo_ap_{tema}.textos'
            if not confere(arq.is_file(), f'{lang}/{tema}: the sample dialogo_ap has no record in the base group'):
                continue
            regs = ler_registros(arq)
            tit = [r_[5] for r_ in regs if r_[2] < 30]
            corp = [r_[5] for r_ in regs if 30 <= r_[2] < 190]
            bot = [r_[5] for r_ in sorted((r_ for r_ in regs if r_[2] >= 190), key=lambda r_: r_[0])]
            confere(tit == [titulo], f'{lang}/{tema}: the title reads {tit!r}, wanted {[titulo]!r}')
            confere(1 <= len(corp) <= 4 and ' '.join(corp) == corpo, f'{lang}/{tema}: the body is {len(corp)} line(s), {" ".join(corp)!r}; wanted at most four, whole: {corpo!r}')
            confere(bot == [cancela, confirma], f'{lang}/{tema}: the buttons read {bot!r}, wanted {[cancela, confirma]!r}')
            confere(not any('…' in r_[5] for r_ in regs), f'{lang}/{tema}: a text is cut with an ellipsis')


def porta(args, rotulo):
    r = subprocess.run([sys.executable, '-B', *args], cwd=ROOT, capture_output=True, text=True)
    confere(r.returncode == 0, f'{rotulo} exits 0 (exit {r.returncode}): ' + ' | '.join((r.stdout + r.stderr).strip().splitlines()[-2:])[:300])


def main():
    gfx = find_gfx()
    if not gfx:
        print('Adafruit GFX Library 1.12.6 not found: run pio once', file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as t:
        tmp = pathlib.Path(t)
        pecas = pecas_do_glue()
        objs, inc = kit(tmp, gfx)
        if objs is None:
            return 2
        modelo_e_mutantes(tmp, pecas, objs, inc)
        textos_dos_pacotes()
        quadros(tmp)
        porta(['tools/check_ui_texts.py'], 'check_ui_texts.py')
        porta(['tools/check_lang_packs.py'], 'check_lang_packs.py')
    print(f'RESULT: {CONTA[0]} checks, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
