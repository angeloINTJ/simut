#!/usr/bin/env python3
"""test_acentos.py -- proves the Angulo's accent palette (S10c, A'1): the list `acentos` of tools/tft_ui/tokens_tft.json, what
tools/tft_ui/gen_tokens.py derives from it, and the header it writes (src/ui/angulo/kit/angulo_tokens.h).

In the Angulo a theme is only the accent colour, picked from a palette; everything else follows the mode (light or dark).
Entry 0 is today's accent and carries no hex of its own; entries 1 to 5 give one `acento` per mode; acentoForte is 25 % of the
mode's tinta over the accent, acentoTinta is the mode's stock value, and each table has a twin at 45 % over the fundo.

What is held:
  * the golden table (S10C_A_PLANO.md 1): acento on superficie, on fundo, on superficie2 and the text on the accent, to 2 decimals,
    for every entry and mode, and each entry's hue; the figures are the generator's own, and they equal the plan's hand-computed
    ones to 2 decimals in all 48 cells;
  * every floor, the 17 distinct roles, the hue rule, both derivations, and entry 0 equal to the guide;
  * the JSON's shape: entry 0 has no hex, no entry has a text, the ids are slugs;
  * the hue limits at their boundary, in exact arithmetic: a hue exactly at a limit passes, the nearest one under it fails;
  * the controls: each check is run on the real header and again on a copy with one violation put in, and must report it; and the
    generator, run on a spec with one violation, must exit 1, name the pair, and leave the header it would write untouched.

The checks read the WORDS in the header (what the firmware compiles), with colour arithmetic written here and not imported from
gen_tokens.py, so the two cannot agree by sharing a mistake. Host only, standard library only: it runs in CI's `gates` job.
"""
import colorsys
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import math
import os
import re
import subprocess
import sys
import tempfile
from fractions import Fraction

sys.dont_write_bytecode = True   # gen_tokens.py is imported below: no tools/tft_ui/__pycache__

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GEN = os.path.join(HERE, 'gen_tokens.py')
SPEC = os.path.join(HERE, 'tokens_tft.json')
HDR = os.path.join(ROOT, 'src', 'ui', 'angulo', 'kit', 'angulo_tokens.h')

MODOS = ('claro', 'escuro')
IDS_ESPERADOS = ['verde-azulado', 'petroleo', 'azul', 'indigo', 'violeta', 'magenta']

# The floors of S10C_A_PLANO.md 1, written out here so that loosening one in gen_tokens.py fails this test: the fix for an entry that
# misses a floor is its lightness, never the floor.
PISOS = [('acento', 'superficie', 4.5), ('acento', 'fundo', 4.5), ('acento', 'superficie2', 3.0),
         ('acentoTinta', 'acento', 4.5), ('acentoTinta', 'acentoForte', 4.5), ('acentoForte', 'superficie', 3.0)]
MATIZ_MIN = {'perigo': 30, 'alerta': 30, 'positivo': 15}   # degrees from the status colours of the same mode
MATIZ_ENTRE = 20                                           # degrees between two entries of the same mode

# (acento on superficie, on fundo, on superficie2, acentoTinta on acento), 2 decimals, per entry: the generator's own figures
# (2026-10-22). They equal the plan's hand-computed table (S10C_A_PLANO.md 1; its dark columns come as fundo, superficie,
# superficie2, text, reordered here) in all 48 cells, so no gap needs explaining.
GOLDEN = {
    'claro': [(7.32, 6.83, 6.17, 7.00), (6.59, 6.15, 5.56, 6.31), (7.79, 7.27, 6.57, 7.45),
              (7.53, 7.03, 6.35, 7.21), (7.07, 6.60, 5.96, 6.77), (6.43, 6.00, 5.42, 6.15)],
    'escuro': [(6.66, 7.33, 6.05, 6.73), (6.99, 7.70, 6.35, 7.07), (6.24, 6.87, 5.67, 6.31),
               (6.95, 7.66, 6.32, 7.03), (6.58, 7.25, 5.98, 6.66), (7.11, 7.83, 6.47, 7.19)],
}
HUES = {'claro': [168, 193, 214, 241, 279, 320], 'escuro': [165, 194, 217, 239, 279, 321]}   # degrees, rounded
MAIS_PERTO = {'claro': ('petroleo', 'azul', 20.8), 'escuro': ('azul', 'indigo', 21.9)}        # the closest pair, per mode
# Worked by hand from the formula (25 % of the mode's tinta over the accent, on the RGB565 colours the panel shows), not by a program.
FORTE_A_MAO = {('petroleo', 'claro'): 0x1A8C, ('azul', 'escuro'): 0x8D9D}

CASOS = []


def caso(nome, problemas):
    """A case passes when it has nothing to report. `problemas` is a list of strings, or a bool."""
    if isinstance(problemas, bool):
        problemas = [] if problemas else ['false']
    ok = not problemas
    CASOS.append((nome, ok))
    mais = f' (+{len(problemas) - 4} more)' if len(problemas) > 4 else ''
    print(('ok   ' if ok else 'FAIL ') + nome + ('' if ok else ': ' + '; '.join(problemas[:4]) + mais), flush=True)


# ---- colour arithmetic, written here (not imported) --------------------------------------------------------------------------

def hex_565(h):
    n = int(h[1:], 16)
    return (((n >> 16) & 0xF8) << 8) | (((n >> 8) & 0xFC) << 3) | ((n & 0xFF) >> 3)


def amplia(w):
    """An RGB565 word as the 8-bit colour the panel shows (the high bits replicated)."""
    r5, g6, b5 = w >> 11, (w >> 5) & 63, w & 31
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def lum(w):
    def f(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = amplia(w)
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def razao(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def mistura(fg, bg, a):
    """`a` of fg over bg, per channel on the shown colours, rounding halves up, back to RGB565 (the header's own recipe)."""
    r, g, b = (math.floor(x * a + y * (1 - a) + 0.5) for x, y in zip(amplia(fg), amplia(bg)))
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def matiz(w):
    r, g, b = (v / 255 for v in amplia(w))
    h, s, _ = colorsys.rgb_to_hsv(r, g, b)
    return None if s == 0 else h * 360


def dmatiz(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()


# ---- the header, read back ----------------------------------------------------------------------------------------------------

def le_cabecalho(texto):
    """(roles in struct order, {table name: {role: word}}, {index array: [table names]}, ACENTOS_N)."""
    m = re.search(r'struct Tema \{(.*?)\};', texto, re.S)
    n = re.search(r'constexpr uint8_t ACENTOS_N = (\d+);', texto)
    if not m or not n:
        raise SystemExit('test_acentos: the header has no struct Tema or no ACENTOS_N (is it generated from the accent palette?)')
    campos = re.findall(r'uint16_t (\w+);', m.group(1))
    tabelas = {}
    for t in re.finditer(r'constexpr Tema (\w+) PROGMEM = \{\n(.*?)\n\};', texto, re.S):
        palavras = [int(x, 16) for x in re.findall(r'\*/ 0x([0-9A-Fa-f]{4}),', t.group(2))]
        if len(palavras) != len(campos):
            raise SystemExit(f'test_acentos: {t.group(1)} has {len(palavras)} words, the struct {len(campos)}')
        tabelas[t.group(1)] = dict(zip(campos, palavras))
    indices = {}
    for t in re.finditer(r'constexpr const Tema \*(\w+)\[ACENTOS_N\] PROGMEM = \{\n(.*?)\n\};', texto, re.S):
        indices[t.group(1)] = re.findall(r'&(\w+),', t.group(2))
    return campos, tabelas, indices, int(n.group(1))


def monta(texto):
    """({(entry, mode): table}, {(entry, mode): disabled twin}, index arrays, roles, N) through the header's own index arrays."""
    campos, tabelas, indices, n = le_cabecalho(texto)
    T, D = {}, {}
    for modo in MODOS:
        nomes, gemeas = indices.get(f'TEMAS_{modo.upper()}', []), indices.get(f'TEMAS_{modo.upper()}_DESAB', [])
        if len(nomes) != n or len(gemeas) != n:
            raise SystemExit(f'test_acentos: TEMAS_{modo.upper()} and its _DESAB must list {n} tables, they list {len(nomes)} and {len(gemeas)}')
        for k in range(n):
            T[(k, modo)], D[(k, modo)] = tabelas[nomes[k]], tabelas[gemeas[k]]
    return T, D, indices, campos, n


# ---- the checks, each a list of problems --------------------------------------------------------------------------------------

class Ctx:
    spec = None
    ids = None
    campos = None


def c_golden(T):
    p = []
    for modo in MODOS:
        for k, ident in enumerate(Ctx.ids):
            t = T[(k, modo)]
            obtido = (razao(t['acento'], t['superficie']), razao(t['acento'], t['fundo']),
                      razao(t['acento'], t['superficie2']), razao(t['acentoTinta'], t['acento']))
            for nome, o, g in zip(('on superficie', 'on fundo', 'on superficie2', 'text'), obtido, GOLDEN[modo][k]):
                if f'{o:.2f}' != f'{g:.2f}':
                    p.append(f'{ident} {modo} acento {nome}: {o:.2f}, golden {g:.2f}')
    return p


def c_pisos(T):
    p = []
    for (k, modo), t in sorted(T.items()):
        for a, b, piso in PISOS:
            r = razao(t[a], t[b])
            if r < piso:
                p.append(f'{Ctx.ids[k]} {modo}: {a} on {b} is {r:.2f}:1, under {piso}')
        outro = T[(k, 'escuro' if modo == 'claro' else 'claro')]['acentoTinta']
        if razao(t['acentoTinta'], t['acento']) <= razao(outro, t['acento']):
            p.append(f"{Ctx.ids[k]} {modo}: the other mode's acentoTinta is as good a text on the accent")
    return p


def c_distintos(T):
    return [f'{Ctx.ids[k]} {modo}: {len(set(t.values()))} distinct roles of {len(t)}'
            for (k, modo), t in sorted(T.items()) if len(set(t.values())) != len(t)]


def c_matizes(T):
    p = []
    for modo in MODOS:
        base = T[(0, modo)]
        hs = [matiz(T[(k, modo)]['acento']) for k in range(len(Ctx.ids))]
        for k, h in enumerate(hs):
            if h is None:
                p.append(f'{Ctx.ids[k]} {modo}: the accent has no hue')
                continue
            for papel, minimo in MATIZ_MIN.items():
                d = dmatiz(h, matiz(base[papel]))
                if d < minimo:
                    p.append(f'{Ctx.ids[k]} {modo}: hue {h:.0f} is {d:.1f} deg from {papel}, under {minimo}')
        for i in range(len(hs)):
            for j in range(i + 1, len(hs)):
                if hs[i] is not None and hs[j] is not None and dmatiz(hs[i], hs[j]) < MATIZ_ENTRE:
                    p.append(f'{Ctx.ids[i]} and {Ctx.ids[j]} {modo}: {dmatiz(hs[i], hs[j]):.1f} deg apart, under {MATIZ_ENTRE}')
    return p


def c_matizes_golden(T):
    p = []
    for modo in MODOS:
        hs = [matiz(T[(k, modo)]['acento']) for k in range(len(Ctx.ids))]
        for k, h in enumerate(hs):
            if round(h) != HUES[modo][k]:
                p.append(f'{Ctx.ids[k]} {modo}: hue {h:.1f}, golden {HUES[modo][k]}')
        par = min(((dmatiz(hs[i], hs[j]), Ctx.ids[i], Ctx.ids[j]) for i in range(len(hs)) for j in range(i + 1, len(hs))))
        a, b, d = MAIS_PERTO[modo]
        if (par[1], par[2], f'{par[0]:.1f}') != (a, b, f'{d:.1f}'):
            p.append(f'{modo}: closest pair {par[1]}-{par[2]} at {par[0]:.1f}, golden {a}-{b} at {d:.1f}')
    return p


def c_derivacoes(TD):
    T, D = TD
    p = []
    for modo in MODOS:
        base = T[(0, modo)]
        for k, ident in enumerate(Ctx.ids):
            t = T[(k, modo)]
            if k > 0:
                ac = hex_565(Ctx.spec['acentos'][k][modo])
                if t['acento'] != ac:
                    p.append(f'{ident} {modo}: acento 0x{t["acento"]:04X}, the JSON says 0x{ac:04X}')
                forte = mistura(base['tinta'], ac, 0.25)
                if t['acentoForte'] != forte:
                    p.append(f'{ident} {modo}: acentoForte 0x{t["acentoForte"]:04X}, 25 % of tinta over the accent is 0x{forte:04X}')
                if t['acentoTinta'] != base['acentoTinta']:
                    p.append(f"{ident} {modo}: acentoTinta is not the mode's stock value")
                for papel in Ctx.campos:
                    if papel not in ('acento', 'acentoForte') and t[papel] != base[papel]:
                        p.append(f'{ident} {modo}: {papel} differs from the stock table')
            for papel in Ctx.campos:
                gemea = mistura(t[papel], t['fundo'], 0.45)
                if D[(k, modo)][papel] != gemea:
                    p.append(f'{ident} {modo}: the twin of {papel} is 0x{D[(k, modo)][papel]:04X}, 45 % over the fundo is 0x{gemea:04X}')
    return p


def c_entrada0(TI):
    T, indices = TI
    p = []
    for modo in MODOS:
        h = dict(Ctx.spec['guia'][modo])
        h.update(Ctx.spec['tft'][modo])
        t = T[(0, modo)]
        for papel in Ctx.campos:
            if t[papel] != hex_565(h[papel]):
                p.append(f'{modo}: entry 0 {papel} is 0x{t[papel]:04X}, the guide says 0x{hex_565(h[papel]):04X}')
        M = modo.upper()
        if indices[f'TEMAS_{M}'][0] != f'TEMA_{M}' or indices[f'TEMAS_{M}_DESAB'][0] != f'TEMA_{M}_DESAB':
            p.append(f'{modo}: entry 0 is not TEMA_{M} and TEMA_{M}_DESAB')
    return p


def c_limites(g):
    """The generator's own limits are the plan's: a floor or a hue limit loosened there must fail here, not pass quietly."""
    p = []
    if g['pisos'] != PISOS:
        p.append(f"ALVOS_ACENTO rows {g['pisos']} are not the plan's {PISOS}")
    if g['matiz'] != (MATIZ_MIN['perigo'], MATIZ_MIN['alerta'], MATIZ_MIN['positivo'], MATIZ_ENTRE):
        p.append(f"hue limits {g['matiz']} are not the plan's")
    if (g['forte'], g['desab']) != (0.25, 0.45):
        p.append(f"FORTE and DESAB are {g['forte']} and {g['desab']}, not 0.25 and 0.45")
    return p


def matiz_exato(w):
    """The hue of an RGB565 word in degrees, as an exact fraction (None for a grey). colorsys works in floats, and the float noise
    at a limit is what the boundary case below is about."""
    r, g, b = tuple(amplia(w))
    mx, mn = max(r, g, b), min(r, g, b)
    if mx == mn:
        return None
    d = Fraction(mx - mn)
    if mx == r:
        h = (Fraction(g - b) / d) % 6
    elif mx == g:
        h = Fraction(b - r) / d + 2
    else:
        h = Fraction(r - g) / d + 4
    return h * 60


def fronteira(g):
    """The hue limits at their boundary (review of A'1, 2026-10-22). In exact arithmetic, every accent whose hue is exactly at a
    limit must pass and the nearest hue under it must fail, through the generator's own rules. The floats the generator computes
    land a hair under some exact limits (#8c5500 against perigo's light hue is 29.999999999999996 deg), and `>` for `>=` is the
    other way to be wrong. The limits: perigo, alerta and positivo in both modes, and azul against petroleo."""
    problemas = []
    flut = [(w, float(h), h) for w, h in ((w, matiz_exato(w)) for w in range(65536)) if h is not None]
    T = g.temas(copy.deepcopy(Ctx.spec))

    def reprovadas(modo, w):
        s = copy.deepcopy(Ctx.spec)
        s['acentos'][2][modo] = '#%02x%02x%02x' % tuple(amplia(w))
        t = g.temas(s)
        return g.reprovados(t, g.acentos(s, t))

    def confere(rotulo, modo, minimo, ref, titulo, marca):
        fref = float(ref)
        no_limite, abaixo = [], None
        for w, f, h in flut:
            e = abs(f - fref) % 360
            d = min(e, 360 - e)
            if abs(d - minimo) < 1e-6 and dmatiz(h, ref) == minimo:
                no_limite.append(w)
            elif d < minimo - 1e-6 and (abaixo is None or d > abaixo[0]):
                abaixo = (d, w)
        for w in no_limite:
            if [1 for t, l in reprovadas(modo, w) if t == titulo and re.search(marca, l)]:
                problemas.append(f'{rotulo}: #%02x%02x%02x at exactly {minimo} deg is refused' % tuple(amplia(w)))
        if abaixo is None or not [1 for t, l in reprovadas(modo, abaixo[1]) if t == titulo and re.search(marca, l)]:
            problemas.append(f'{rotulo}: the nearest hue under {minimo} deg is not refused')
        return len(no_limite)

    ns = nb = 0   # accents exactly at a status limit, and at the limit between entries
    for modo in MODOS:
        for papel, minimo in (('perigo', 30), ('alerta', 30), ('positivo', 15)):
            ref = matiz_exato(hex_565(T[modo][papel]))
            ns += confere(f'{modo} acento against {papel}', modo, minimo, ref, f'== acento 2 azul {modo}', f'from {papel} hue')
        ref = matiz_exato(hex_565(Ctx.spec['acentos'][1][modo]))
        nb += confere(f'{modo} azul against petroleo', modo, 20, ref, f'== acentos among themselves, {modo}', r'petroleo \(\d+\) and azul \(')
    print(f'       ({ns} accents exactly at a status limit and {nb} at the limit between entries, each one run through the generator)')
    # a limit with no RGB565 hue exactly on it (positivo, and azul against petroleo in the dark) has nothing to pass; the case must not be empty
    if not ns or not nb:
        problemas.append(f'no accent sits exactly at a limit ({ns} status, {nb} between entries): the case would pass for nothing')
    return problemas


def com_controle(nome, confere, dado, mutar):
    """The case on the real data and its negative control: the same check, on a copy with one violation put in, must report it."""
    caso(nome, confere(dado))
    copia = copy.deepcopy(dado)
    mutar(copia)
    caso(nome + ' (control: bites)', [] if confere(copia) else ['the check did not report the mutation'])


# ---- the generator, run as a program -----------------------------------------------------------------------------------------

def gera(*args):
    r = subprocess.run([sys.executable, GEN] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def carrega_gerador():
    sp = importlib.util.spec_from_file_location('gen_tokens_sob_teste', GEN)
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


def colide(linha):
    """The roles a distinct-roles FAIL line names as equal: 'FAIL 17 roles distinct in RGB565: tinta2 = acento (0xB5B0)'."""
    return set(linha.split(': ', 1)[1].split(' (0x')[0].split(' = ')) if ': ' in linha else set()


def reprovas(saida):
    """[(section title, FAIL line)] of a --report output."""
    out, titulo = [], ''
    for linha in saida.splitlines():
        if linha.startswith('== '):
            titulo = linha
        elif linha.lstrip().startswith('FAIL'):
            out.append((titulo, linha.strip()))
    return out


def main():
    sha_cabecalho = sha(HDR)
    Ctx.spec = json.load(open(SPEC, encoding='utf-8'))
    Ctx.ids = [e['id'] for e in Ctx.spec.get('acentos', [])]
    texto = open(HDR, encoding='utf-8').read()

    # -- the instrument first
    caso('instrument: #000000 on #ffffff is 21.00:1', f'{razao(0x0000, 0xFFFF):.2f}' == '21.00')
    caso('instrument: the hues of red, green and blue are 0, 120 and 240', [round(matiz(w)) for w in (0xF800, 0x07E0, 0x001F)] == [0, 120, 240])
    caso('instrument: white over black at 45 % is 0x738E', mistura(0xFFFF, 0x0000, 0.45) == 0x738E)
    caso('instrument: #1a6478 is 0x1B2F and shows as (24, 101, 123)', hex_565('#1a6478') == 0x1B2F and amplia(0x1B2F) == (24, 101, 123))

    # -- the JSON's shape
    ac = Ctx.spec.get('acentos', [])
    caso('json: six entries, the plan\'s ids in the plan\'s order', Ctx.ids == IDS_ESPERADOS)
    caso('json: entry 0 carries no hex of its own', ac[:1] == [{'id': 'verde-azulado'}])
    caso('json: entries 1 to 5 carry only id, claro and escuro (no texts), the hexes lower-case #rrggbb',
         len(ac) == 6 and all(set(e) == {'id', 'claro', 'escuro'} and all(re.fullmatch(r'#[0-9a-f]{6}', e[m]) for m in MODOS) for e in ac[1:])
         and all(re.fullmatch(r'[a-z][a-z0-9-]*', i) for i in Ctx.ids) and len(set(Ctx.ids)) == len(Ctx.ids))
    caso('json: the top-level keys are _doc, guia, tft and acentos', set(Ctx.spec) == {'_doc', 'guia', 'tft', 'acentos'})

    # -- the header, read back
    try:
        T, D, indices, campos, n = monta(texto)
    except SystemExit as e:
        caso('header: parses (struct, ACENTOS_N, four index arrays)', [str(e)])
        return finaliza(sha_cabecalho)
    Ctx.campos = campos
    caso('header: ACENTOS_N is the number of entries and the struct has the 17 roles of the guide',
         n == len(Ctx.ids) and sorted(campos) == sorted(Ctx.spec['guia']['claro']) and len(campos) == 17)
    esperados = []
    for k, ident in enumerate(Ctx.ids):
        for modo in MODOS:
            base = f'TEMA_{modo.upper()}' + ('' if k == 0 else '_' + ident.upper().replace('-', '_'))
            esperados.append((indices[f'TEMAS_{modo.upper()}'][k], base))
            esperados.append((indices[f'TEMAS_{modo.upper()}_DESAB'][k], base + '_DESAB'))
    caso('header: the index arrays list, entry by entry, TEMA_<MODE>[_<ID>] and its _DESAB', [f'{a} != {b}' for a, b in esperados if a != b])
    rc, saida = gera('--check')
    caso('header: gen_tokens.py --check says it is what the JSON generates', rc == 0)

    # -- what is held, each with its control
    caso('golden: the plan\'s table (2 decimals) has six rows per mode', all(len(GOLDEN[m]) == len(Ctx.ids) == len(HUES[m]) for m in MODOS))
    com_controle('golden: acento on superficie, fundo, superficie2 and the text on it, 2 decimals, 12 tables', c_golden, T,
                 lambda t: t[(2, 'claro')].update(acento=t[(2, 'claro')]['acento'] ^ 0x0800))
    com_controle('golden: each entry\'s hue and the closest pair per mode', c_matizes_golden, T,
                 lambda t: t[(3, 'escuro')].update(acento=hex_565('#9acaf0')))
    com_controle('floors: every PISOS row and the text colour that beats the other mode\'s, 12 tables', c_pisos, T,
                 lambda t: t[(2, 'claro')].update(acento=hex_565('#3d78d0')))
    com_controle('distinct: the 17 roles of every table differ in RGB565', c_distintos, T,
                 lambda t: t[(2, 'escuro')].update(acentoForte=t[(2, 'escuro')]['acento']))
    com_controle('hue: 30 deg from perigo and alerta, 15 from positivo, 20 between entries', c_matizes, T,
                 lambda t: t[(4, 'claro')].update(acento=hex_565('#8c2f1c')))
    com_controle('hue between entries: no two accents of a mode within 20 deg', c_matizes, T,
                 lambda t: t[(2, 'claro')].update(acento=t[(1, 'claro')]['acento']))
    com_controle('derivation: acento from the JSON, acentoForte 25 % of tinta over it, acentoTinta and the rest the mode\'s, the 45 % twins',
                 c_derivacoes, (T, D),
                 lambda td: td[0][(1, 'claro')].update(acentoForte=td[0][(1, 'claro')]['acentoForte'] ^ 0x0001))
    com_controle('entry 0: the guide\'s roles (guia with the panel\'s deviations), at index 0 of the arrays', c_entrada0, (T, indices),
                 lambda ti: ti[0][(0, 'escuro')].update(acentoTinta=ti[0][(0, 'escuro')]['acentoTinta'] ^ 0x0020))
    for (ident, modo), palavra in FORTE_A_MAO.items():
        k = Ctx.ids.index(ident)
        caso(f'derivation: acentoForte of {ident}, {modo}, worked by hand, is 0x{palavra:04X}', T[(k, modo)]['acentoForte'] == palavra)
    gm = carrega_gerador()
    limites = {'pisos': [(a, b, v) for a, b, v, _ in gm.ALVOS_ACENTO],
               'matiz': (gm.MATIZ_DE_PERIGO, gm.MATIZ_DE_ALERTA, gm.MATIZ_DE_POSITIVO, gm.MATIZ_ENTRE_ACENTOS),
               'forte': gm.FORTE, 'desab': gm.DESAB}
    com_controle("generator: its floors, hue limits, FORTE and DESAB are the plan's (loosening one fails here)", c_limites, limites,
                 lambda g: g['pisos'].__setitem__(1, ('acento', 'fundo', 3.0)))

    # -- the report prints every ratio, and they are the ones the header gives
    rc, saida = gera('--check', '--report')
    caso('report: --check --report exits 0 on the real tree', rc == 0)
    vistos, p = 0, []
    for m in re.finditer(r'^== acento (\d+) (\S+) (claro|escuro)\n((?:  .*\n)+)', saida + '\n', re.M):
        k, modo = int(m.group(1)), m.group(3)
        for linha in m.group(4).splitlines():
            r = re.match(r'\s+(?:ok  |FAIL) +(\d+\.\d\d):1 \(>= ([\d.]+)\) (\w+) on (\w+): ', linha)
            if r:
                vistos += 1
                esperado = razao(T[(k, modo)][r.group(3)], T[(k, modo)][r.group(4)])
                if f'{esperado:.2f}' != r.group(1):
                    p.append(f'entry {k} {modo} {r.group(3)} on {r.group(4)}: report {r.group(1)}, header {esperado:.2f}')
    caso('report: 72 ratios (6 floors x 12 tables) printed, each equal to the header\'s', p + ([] if vistos == 72 else [f'{vistos} ratios read']))
    contagem = {'the text-colour rule': (len(re.findall(r"acentoTinta on acento beats the other mode's on it", saida)), 12),
                'the distinct-roles rule': (saida.count('roles distinct in RGB565'), 12),
                'the hue against the status colours': (saida.count(' deg (>= 30) acento hue') + saida.count(' deg (>= 15) acento hue'), 36),
                'the hue between entries': (saida.count(' deg (>= 20) '), 30)}
    caso('report: every rule prints for every table (text colour 12, distinct roles 12, hue vs status 36, hue between entries 30)',
         [f'{k}: {v[0]} lines, expected {v[1]}' for k, v in contagem.items() if v[0] != v[1]])
    caso('report: no FAIL line on the real tree', not reprovas(saida))

    # -- controls on the program: one violation in the spec
    with tempfile.TemporaryDirectory() as tmp:
        seq = [0]

        def spec_com(muda):
            s = copy.deepcopy(Ctx.spec)
            muda(s)
            seq[0] += 1
            p = os.path.join(tmp, f'spec{seq[0]}.json')
            json.dump(s, open(p, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
            return p

        velho = os.path.join(tmp, 'velho.h')

        def semeia():
            open(velho, 'w', encoding='utf-8').write('stale\n')
            return sha(velho)

        # the write path works on the unmutated spec: the stale file becomes the real header (so "unchanged" below means something)
        antes = semeia()
        rc, saida = gera('--out', velho)
        caso('write: the unmutated spec rewrites the stale header into exactly the real one', rc == 0 and sha(velho) == sha_cabecalho != antes)

        # 1. a light acento under 4.5:1
        p1 = spec_com(lambda s: s['acentos'][2].update(claro='#3d78d0'))
        rc, saida = gera('--report', '--spec', p1)
        r = reprovas(saida)
        caso('control 1: a light acento under 4.5:1 makes --report exit 1 and name the pair',
             rc == 1 and any(t == '== acento 2 azul claro' and 'acento on fundo' in l for t, l in r)
             and all(t == '== acento 2 azul claro' for t, l in r))
        antes = semeia()
        rc, saida = gera('--spec', p1, '--out', velho)
        caso('control 1: the write mode refuses, names the pair and leaves the header\'s sha256 unchanged',
             rc == 1 and sha(velho) == antes and 'acento on fundo' in saida and 'refusing' in saida)

        # 2. a dark acento equal to tinta2
        p2 = spec_com(lambda s: s['acentos'][2].update(escuro=s['tft']['escuro']['tinta2']))
        rc, saida = gera('--report', '--spec', p2)
        d = [(t, l) for t, l in reprovas(saida) if 'distinct' in l]
        caso('control 2: a dark acento equal to tinta2 fails the distinct-roles rule, once, naming both roles',
             rc == 1 and len(d) == 1 and d[0][0] == '== acento 2 azul escuro' and colide(d[0][1]) == {'acento', 'tinta2'})
        rc, saida = gera('--report')
        caso('control 2: the unmutated spec has no distinct-roles failure', rc == 0 and not [1 for t, l in reprovas(saida) if 'distinct' in l])
        antes = semeia()
        rc, saida = gera('--spec', p2, '--out', velho)
        caso('control 2: the write mode refuses the distinct-roles failure, names it, and leaves the header\'s sha256 unchanged',
             rc == 1 and sha(velho) == antes and 'refusing' in saida and 'roles distinct' in saida)

        # 3. a hue of 10 degrees (HSV 10, 0.80, 0.55 = #8c2f1c: it passes every contrast floor, so only the hue rule speaks)
        h10 = '#%02x%02x%02x' % tuple(round(v * 255) for v in colorsys.hsv_to_rgb(10 / 360, 0.80, 0.55))
        p3 = spec_com(lambda s: s['acentos'][4].update(claro=h10))
        rc, saida = gera('--report', '--spec', p3)
        r = reprovas(saida)
        caso('control 3: a hue of 10 deg fails the hue rule (perigo, alerta) and nothing else',
             rc == 1 and h10 == '#8c2f1c' and len(r) == 2 and all(t == '== acento 4 violeta claro' and ' deg ' in l for t, l in r)
             and any('perigo' in l for t, l in r) and any('alerta' in l for t, l in r))
        antes = semeia()
        rc, saida = gera('--spec', p3, '--out', velho)
        caso('control 3: the write mode refuses the hue failure, names it, and leaves the header\'s sha256 unchanged',
             rc == 1 and sha(velho) == antes and 'refusing' in saida and ' deg ' in saida and 'from perigo hue' in saida)

        # 4. acentoForte equal to acento: the share of tinta mixed over the accent set to 0
        g = carrega_gerador()
        g.FORTE = 0.0
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = g.main(['--report', '--spec', SPEC])
        r = reprovas(buf.getvalue())
        caso('control 4: acentoForte equal to acento fails the distinct-roles rule in the ten tables of entries 1 to 5, and only there',
             rc == 1 and len(r) == 10 and all('distinct' in l and colide(l) == {'acento', 'acentoForte'} for t, l in r)
             and sorted(t for t, l in r) == sorted(f'== acento {k} {Ctx.ids[k]} {m}' for k in range(1, 6) for m in MODOS))
        antes = semeia()
        with contextlib.redirect_stdout(io.StringIO()):
            rc = g.main(['--spec', SPEC, '--out', velho])
        caso('control 4: the write mode refuses a structural failure too, header unchanged', rc == 1 and sha(velho) == antes)

        # 5. the shape of the JSON: restrictions 2 and 3, and a repeated id
        for nome, muda in (('entry 0 carries a hex', lambda s: s['acentos'][0].update(claro='#1f6355')),
                           ('an entry carries a text', lambda s: s['acentos'][1].update(nome='Petroleo')),
                           ('an id is repeated', lambda s: s['acentos'][2].update(id='petroleo'))):
            antes = semeia()
            rc, saida = gera('--spec', spec_com(muda), '--out', velho)
            caso(f'control 5: the generator refuses a spec where {nome}, header unchanged', rc != 0 and sha(velho) == antes)

        # 7. two entries on the same hue: azul takes petroleo's light hex (it passes every floor; only the pair rule speaks)
        p7 = spec_com(lambda s: s['acentos'][2].update(claro=s['acentos'][1]['claro']))
        rc, saida = gera('--report', '--spec', p7)
        r = reprovas(saida)
        caso('control 7: two accents 0 deg apart fail the hue rule between entries, and nothing else',
             rc == 1 and len(r) == 1 and r[0][0] == '== acentos among themselves, claro' and 'petroleo' in r[0][1] and 'azul' in r[0][1])
        antes = semeia()
        rc, saida = gera('--spec', p7, '--out', velho)
        caso('control 7: the write mode refuses the pair between entries, names it, and leaves the header\'s sha256 unchanged',
             rc == 1 and sha(velho) == antes and 'refusing' in saida and 'petroleo' in saida and 'azul' in saida)

        # 8. the hue limits at their boundary, in exact arithmetic (review of A'1)
        caso('boundary: a hue exactly at a limit passes and the nearest one under it fails, for every status limit and between entries',
             fronteira(carrega_gerador()))

        # 6. --check still bites: a header edited by hand
        editado = os.path.join(tmp, 'editado.h')
        open(editado, 'w', encoding='utf-8').write(texto.replace('0x1B2F', '0x1B2E', 1))
        rc, saida = gera('--check', '--out', editado)
        caso('control 6: --check fails on a header with one word edited by hand', rc == 1 and texto != open(editado, encoding='utf-8').read())

    return finaliza(sha_cabecalho)


def finaliza(sha_cabecalho):
    caso('the controls did not touch the real header', sha(HDR) == sha_cabecalho)
    falhas = [n for n, ok in CASOS if not ok]
    print(f'{len(CASOS)} cases, {len(falhas)} failed')
    return 1 if falhas else 0


if __name__ == '__main__':
    sys.exit(main())
