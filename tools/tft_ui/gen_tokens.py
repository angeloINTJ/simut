#!/usr/bin/env python3
"""gen_tokens.py [--check] [--report] [--spec FILE] [--out FILE] — writes src/ui/angulo/kit/angulo_tokens.h from
tools/tft_ui/tokens_tft.json: the 17 colour roles of both themes in RGB565, the "disabled"
version of each (45 % over the background, pre-mixed, since RGB565 has no alpha), the
accent palette (below), the spacing scale and the radii.

Why this exists. The header came from the TFT plan's delivery (gerar_tokens_h.js, in the
plan's renderer, which this repository does not carry), and the rule for a generated file is
that it is never edited by hand. The panel needs colours of its own (2026-10-15: on a low-cost
TFT the guide's card edges, 1.29 to 1.48:1 against what they sit on, and the card itself,
1.08:1 against the background, disappear; a TN panel compresses the tones and shifts them with
the viewing angle). The guide's palette stays the source ('guia'); the panel's deviations sit
next to it ('tft'), role by role, and this script merges the two.

With 'tft' empty it reproduced gerar_tokens_h.js byte for byte (checked on 2026-10-15 against
the header of commit 3824208): the same RGB565 macro, the same mixing (each channel expanded
from 5 or 6 bits by replicating the high bits, Math.round of the weighted sum, which rounds
halves up), the same text. Since then the header's first line names this script.

The accent palette (S10c, A'1, 2026-10-22). In the Angulo a theme is only the accent colour, picked from a palette; everything
else follows the mode (light, dark or automatic). `acentos` in the JSON is the palette. Entry 0 is today's accent and carries no
colour of its own: it is the guide's three accent roles, so TEMA_CLARO, TEMA_ESCURO and their _DESAB twins stay what they were.
Every other entry gives one `acento` per mode (one colour cannot hold 4.5:1 on both the light and the dark surfaces, and the
guide's own accent already brightens in the dark theme). The rest is derived: acentoForte is FORTE (25 %) of the mode's tinta over
the accent, acentoTinta is the mode's stock value, and the disabled twin is DESAB (45 %) over the fundo, as before. The header
gets ACENTOS_N, the 17-role table and the twin of every entry and mode, and four index arrays (TEMAS_CLARO, TEMAS_CLARO_DESAB,
TEMAS_ESCURO, TEMAS_ESCURO_DESAB) whose [0] is the stock table. Every entry and mode is held to ALVOS_ACENTO, to the rule
that the mode's acentoTinta beats the other mode's as the text on that accent, and to two structural rules: the 17 roles
distinct in RGB565, and the hue rule. A palette colour that misses a floor is moved in lightness;
a floor is never lowered. tools/tft_ui/test_acentos.py holds all of it.

--check   regenerate in memory and compare with the header; exit 1 when they differ (the
          header was edited by hand, or the JSON changed without a regeneration).
--report  print the contrast of the pairs the panel depends on, computed on the colours the
          panel shows (RGB565 expanded back to 8 bits), against the panel's targets; exit 1
          when a pair is under its target. With both, exit 1 when either fails (the CI's
          gate). Without --check or --report the header is written, after every check of the report passed: when one fails the
          program refuses (exit 1, the header untouched), as design/build-tokens.mjs does.
--spec FILE, --out FILE
          read the JSON from FILE, or write (or compare) the header at FILE: the test of the accent palette runs the program on a
          spec with one violation against a stale copy of the header.
"""
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC = os.path.join(ROOT, 'tools', 'tft_ui', 'tokens_tft.json')
OUT = os.path.join(ROOT, 'src', 'ui', 'angulo', 'kit', 'angulo_tokens.h')

PAPEIS = ['fundo', 'superficie', 'superficie2', 'tinta', 'tinta2', 'linha', 'linhaForte', 'acento', 'acentoForte',
          'acentoTinta', 'positivo', 'positivoSuave', 'alerta', 'alertaSuave', 'perigo', 'perigoSuave', 'perigoTinta']

# The pairs the screens depend on, and the panel's minimum for each. Text is held to 7:1 for
# grey text (the TN panel loses mid-tones first) and 4.5:1 for coloured text and status tags;
# an edge that tells a component from what it sits on is held to 3:1 (WCAG 2.1 1.4.11).
ALVOS = [
    ('tinta', 'superficie', 7.0, 'main text on a card'),
    ('tinta', 'fundo', 7.0, 'main text on the background'),
    ('tinta', 'superficie2', 7.0, 'main text on a secondary button'),
    ('tinta2', 'superficie', 7.0, 'secondary text on a card'),
    ('tinta2', 'fundo', 7.0, 'secondary text on the background'),
    ('tinta2', 'superficie2', 7.0, 'secondary text on a secondary button'),
    ('acento', 'superficie', 4.5, 'accent text on a card'),
    ('acento', 'fundo', 4.5, 'accent text on the background'),
    ('acentoTinta', 'acento', 4.5, 'text on the accent button'),
    ('perigo', 'superficie', 4.5, 'alarm reading on a card'),
    ('positivo', 'positivoSuave', 4.5, 'positive tag'),
    ('alerta', 'alertaSuave', 4.5, 'warning tag'),
    ('perigo', 'perigoSuave', 4.5, 'danger tag'),
    ('linha', 'superficie', 3.0, 'a card edge or divider on a card'),
    ('linha', 'fundo', 3.0, 'a card edge against the background'),
    ('linha', 'superficie2', 3.0, 'a secondary button edge'),
    ('linhaForte', 'superficie', 4.5, 'a strong line (field edge) on a card'),
]

MODOS = ('claro', 'escuro')

# The accent palette (S10c, A'1; S10C_A_PLANO.md 1). acentoForte is FORTE of the mode's tinta over the accent; the disabled twin
# is DESAB of each role over the fundo.
FORTE = 0.25
DESAB = 0.45

# What every entry of `acentos` is held to, in both modes (the pairs ALVOS holds for the stock accent are among them, here for each
# entry): the accent is used as text on a card and on the background, so 4.5:1; against a secondary button 3:1; the text on the
# accent and on its pressed colour 4.5:1; the pressed colour against a card 3:1. A colour that misses one is moved in lightness,
# never the floor.
ALVOS_ACENTO = [
    ('acento', 'superficie', 4.5, 'accent text on a card'),
    ('acento', 'fundo', 4.5, 'accent text on the background'),
    ('acento', 'superficie2', 3.0, 'the accent against a secondary button'),
    ('acentoTinta', 'acento', 4.5, 'text on the accent button'),
    ('acentoTinta', 'acentoForte', 4.5, 'text on the pressed accent button'),
    ('acentoForte', 'superficie', 3.0, 'the pressed accent against a card'),
]

# The hue rule, on the colours the panel shows (HSV hue in degrees). Only the accent calls attention (ANGULO.md 1 and 2, rule 1), so
# an accent must not read as a status colour, and two entries of a mode must not be taken for each other.
MATIZ_DE_PERIGO = 30
MATIZ_DE_ALERTA = 30
MATIZ_DE_POSITIVO = 15
MATIZ_ENTRE_ACENTOS = 20


def jsround(x):
    return math.floor(x + 0.5)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def hex565(h):
    n = int(h[1:], 16)
    return rgb565((n >> 16) & 255, (n >> 8) & 255, n & 255)


def expand565(c):
    r5, g6, b5 = c >> 11, (c >> 5) & 63, c & 31
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def blend565(fg, bg, a):
    f, b = expand565(fg), expand565(bg)
    return rgb565(*(jsround(x * a + y * (1 - a)) for x, y in zip(f, b)))


def temas(spec):
    out = {}
    for nome in ('claro', 'escuro'):
        h = dict(spec['guia'][nome])
        for k, v in spec['tft'][nome].items():
            if k not in h:
                sys.exit(f'gen_tokens: tft.{nome}.{k} is not a role of the guide')
            h[k] = v
        out[nome] = h
    return out


def hex8(c):
    return '#%02x%02x%02x' % expand565(c)


def acentos(spec, T):
    """The palette: one dict per entry, in order, {'id', 'claro': {17 roles in hex}, 'escuro': {...}}. Entry 0 has no colour of its
    own: its tables are the stock ones (T). Every other entry gives an `acento` per mode; acentoForte is FORTE of the mode's tinta
    over it, and acentoTinta and the other roles are the mode's. The shape of the JSON is checked here: a spec that breaks it is
    refused (entry 0 with a hex, an entry with a text, a repeated id)."""
    lista = spec.get('acentos')
    if not isinstance(lista, list) or not lista:
        sys.exit("gen_tokens: acentos must be a list, and entry 0 is the guide's accent")
    out, vistos = [], set()
    for k, e in enumerate(lista):
        chaves = {'id'} if k == 0 else {'id', 'claro', 'escuro'}
        if not isinstance(e, dict) or set(e) != chaves:
            sys.exit(f'gen_tokens: acentos[{k}] must have exactly the keys {sorted(chaves)}'
                     + (" (entry 0 carries no hex: it is the guide's accent)" if k == 0 else ' (no texts: the names are panel texts)'))
        if not isinstance(e['id'], str) or not re.fullmatch(r'[a-z][a-z0-9-]*', e['id']) or e['id'] in vistos:
            sys.exit(f'gen_tokens: acentos[{k}].id must be a lower-case slug and not repeat')
        vistos.add(e['id'])
        item = {'id': e['id']}
        for nome in MODOS:
            h = dict(T[nome])
            if k > 0:
                if not isinstance(e[nome], str) or not re.fullmatch(r'#[0-9a-f]{6}', e[nome]):
                    sys.exit(f'gen_tokens: acentos[{k}].{nome} must be a lower-case #rrggbb')
                h['acento'] = e[nome]
                h['acentoForte'] = hex8(blend565(hex565(h['tinta']), hex565(e[nome]), FORTE))
            item[nome] = h
        out.append(item)
    return out


def bloco_acentos(T, E):
    """The header's accent section: ACENTOS_N, the table and the twin of every entry but 0 (entry 0 is TEMA_CLARO and TEMA_ESCURO,
    above, unchanged), and the four index arrays."""
    hex4 = lambda v: '0x' + format(v, 'X').zfill(4)
    out = (f'// Acentos: o tema e so a cor de destaque, escolhida numa paleta; o resto segue o modo (claro ou escuro).\n'
           f'// A entrada 0 e o acento de hoje (TEMA_CLARO e TEMA_ESCURO, acima, palavra por palavra). Cada outra traz os 17 papeis\n'
           f'// com o acento proprio de cada modo: acentoForte e {round(FORTE * 100)} % da tinta do modo sobre o acento, acentoTinta e o\n'
           f'// do modo, e a versao desabilitada de cada tabela e a de sempre ({round(DESAB * 100)} % sobre o fundo).\n'
           f'constexpr uint8_t ACENTOS_N = {len(E)};\n\n')
    nomes = {m: [] for m in MODOS}
    for k, e in enumerate(E):
        for nome in MODOS:
            N = nome.upper()
            if k == 0:
                nomes[nome].append((f'TEMA_{N}', f'TEMA_{N}_DESAB', e['id']))
                continue
            h = e[nome]
            c = {p: hex565(h[p]) for p in PAPEIS}
            tab = f'TEMA_{N}_' + e['id'].upper().replace('-', '_')
            linhas = '\n'.join(f'  /* {p.ljust(13)} {h[p]} */ {hex4(c[p])},' for p in PAPEIS)
            desab = '\n'.join(f'  /* {p.ljust(13)} */ {hex4(blend565(c[p], c["fundo"], DESAB))},' for p in PAPEIS)
            out += (f'// Acento {k} ({e["id"]}), tema {nome}\nconst Tema {tab} PROGMEM = {{\n{linhas}\n}};\n'
                    f'// Acento {k} ({e["id"]}), tema {nome}, estado desabilitado\nconst Tema {tab}_DESAB PROGMEM = {{\n{desab}\n}};\n\n')
            nomes[nome].append((tab, tab + '_DESAB', e['id']))
    out += '// Por indice do acento (0 = o de hoje): a tabela de cada modo e a sua versao desabilitada\n'
    for nome in MODOS:
        for sufixo, col in (('', 0), ('_DESAB', 1)):
            itens = '\n'.join(f'  &{t[col]},  // {k} {t[2]}' for k, t in enumerate(nomes[nome]))
            out += f'const Tema *const TEMAS_{nome.upper()}{sufixo}[ACENTOS_N] PROGMEM = {{\n{itens}\n}};\n'
    return out + '\n'


def header(T, E):
    hex4 = lambda v: '0x' + format(v, 'X').zfill(4)
    campos = '\n'.join(f'  uint16_t {p};' for p in PAPEIS)
    blocos = ''
    for nome in ('claro', 'escuro'):
        h = T[nome]
        c = {p: hex565(h[p]) for p in PAPEIS}
        linhas = '\n'.join(f'  /* {p.ljust(13)} {h[p]} */ {hex4(c[p])},' for p in PAPEIS)
        desab = '\n'.join(f'  /* {p.ljust(13)} */ {hex4(blend565(c[p], c["fundo"], DESAB))},' for p in PAPEIS)
        N = nome.upper()
        blocos += (f'// Tema {nome}\nconst Tema TEMA_{N} PROGMEM = {{\n{linhas}\n}};\n'
                   f'// Tema {nome}, estado desabilitado: cada papel misturado a 45 % sobre o fundo\n'
                   f'const Tema TEMA_{N}_DESAB PROGMEM = {{\n{desab}\n}};\n\n')
    return f'''// Tokens do Angulo para o TFT 320x240 RGB565 (gerado por tools/tft_ui/gen_tokens.py a partir de
// tools/tft_ui/tokens_tft.json; nao editar a mao).
// Hex do guia -> RGB565 pela macro ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3).
#pragma once
#include <Arduino.h>

struct Tema {{
{campos}
}};

{blocos}{bloco_acentos(T, E)}// Escala de espaco (grade de 4 px): toda margem, folga e padding e um destes
enum Espaco : uint8_t {{ ESP1 = 4, ESP2 = 8, ESP3 = 12, ESP4 = 16, ESP5 = 24, ESP6 = 32, ESP7 = 48 }};

// Raios: controle (botao, tecla, campo, chip), cartao e total (pilula/circulo)
enum Raio : uint8_t {{ RAIO_CONTROLE = 6, RAIO_CARTAO = 12 }};
#define RAIO_TOTAL(h) ((h) / 2)

// Mistura generica (para derivar qualquer cor desabilitada em tempo de execucao, se preciso)
static inline uint16_t misturar565(uint16_t fg, uint16_t bg, uint8_t pct) {{
  uint8_t fr = (fg >> 11) & 31, fgc = (fg >> 5) & 63, fb = fg & 31;
  uint8_t br = (bg >> 11) & 31, bgc = (bg >> 5) & 63, bb = bg & 31;
  // expande para 8 bits (bits altos replicados), mistura e volta a quantizar
  auto ex5 = [](uint8_t v) {{ return (uint8_t)((v << 3) | (v >> 2)); }};
  auto ex6 = [](uint8_t v) {{ return (uint8_t)((v << 2) | (v >> 4)); }};
  uint8_t r = (ex5(fr) * pct + ex5(br) * (100 - pct) + 50) / 100;
  uint8_t g = (ex6(fgc) * pct + ex6(bgc) * (100 - pct) + 50) / 100;
  uint8_t b = (ex5(fb) * pct + ex5(bb) * (100 - pct) + 50) / 100;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}}
'''


def lum(c):
    def f(v):
        v /= 255
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = expand565(c)
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def contraste(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


def matiz(c):
    """HSV hue, in degrees, of an RGB565 colour as the panel shows it; None for a grey (no hue)."""
    r, g, b = expand565(c)
    mx, mn = max(r, g, b), min(r, g, b)
    if mx == mn:
        return None
    d = mx - mn
    if mx == r:
        h = ((g - b) / d) % 6
    elif mx == g:
        h = (b - r) / d + 2
    else:
        h = (r - g) / d + 4
    return h * 60


def distancia_de_matiz(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


def linha_alvo(c, a, b, alvo, desc):
    r = contraste(c[a], c[b])
    ok = r >= alvo
    return ok, f'  {"ok  " if ok else "FAIL"} {r:5.2f}:1 (>= {alvo}) {a} on {b}: {desc}'


def secoes(T, E):
    """Every check, as [(title, [(ok, line)])]: the panel's pairs on the two stock tables (ALVOS); then, for each entry and mode, the
    accent's floors (ALVOS_ACENTO), the text colour (it must beat the other mode's acentoTinta on that accent), the 17 distinct roles
    and the hue against the status colours; then the entries among themselves, mode by mode."""
    out = []
    for nome in MODOS:
        c = {p: hex565(T[nome][p]) for p in PAPEIS}
        out.append((f'== {nome}', [linha_alvo(c, *r) for r in ALVOS]))
    for k, e in enumerate(E):
        for nome in MODOS:
            c = {p: hex565(e[nome][p]) for p in PAPEIS}
            ls = [linha_alvo(c, *r) for r in ALVOS_ACENTO]
            outro = hex565(T['escuro' if nome == 'claro' else 'claro']['acentoTinta'])
            r1, r2 = contraste(c['acentoTinta'], c['acento']), contraste(outro, c['acento'])
            ls.append((r1 > r2, f'  {"ok  " if r1 > r2 else "FAIL"} {r1:5.2f}:1 over {r2:5.2f}:1 acentoTinta on acento beats '
                                f"the other mode's on it: the text colour is the better one"))
            quem = {}
            for p in PAPEIS:
                quem.setdefault(c[p], []).append(p)
            iguais = [f'{" = ".join(v)} (0x{w:04X})' for w, v in quem.items() if len(v) > 1]
            ls.append((not iguais, f'  {"ok  " if not iguais else "FAIL"} {len(PAPEIS)} roles distinct in RGB565'
                                   + (': ' + ', '.join(iguais) if iguais else '')))
            h = matiz(c['acento'])
            if h is None:
                ls.append((False, '  FAIL acento has no hue (a grey): it could not be told from a status colour'))
            else:
                base = {p: hex565(T[nome][p]) for p in PAPEIS}
                for papel, minimo in (('perigo', MATIZ_DE_PERIGO), ('alerta', MATIZ_DE_ALERTA), ('positivo', MATIZ_DE_POSITIVO)):
                    hp = matiz(base[papel])
                    d = round(distancia_de_matiz(h, hp), 6)   # a hue exactly at a limit is not refused by float noise
                    ls.append((d >= minimo, f'  {"ok  " if d >= minimo else "FAIL"} {d:5.1f} deg (>= {minimo}) '
                                            f'acento hue {h:.0f} from {papel} hue {hp:.0f}'))
            out.append((f'== acento {k} {e["id"]} {nome}', ls))
    for nome in MODOS:
        hs = [(e['id'], matiz(hex565(e[nome]['acento']))) for e in E]
        ls = []
        for i in range(len(hs)):
            for j in range(i + 1, len(hs)):
                (ia, ha), (ib, hb) = hs[i], hs[j]
                if ha is None or hb is None:
                    ls.append((False, f'  FAIL {ia} and {ib}: no hue to compare'))
                    continue
                d = round(distancia_de_matiz(ha, hb), 6)
                ls.append((d >= MATIZ_ENTRE_ACENTOS, f'  {"ok  " if d >= MATIZ_ENTRE_ACENTOS else "FAIL"} {d:5.1f} deg '
                                                     f'(>= {MATIZ_ENTRE_ACENTOS}) {ia} ({ha:.0f}) and {ib} ({hb:.0f})'))
        out.append((f'== acentos among themselves, {nome}', ls))
    return out


def relatorio(T, E):
    falhas = 0
    for titulo, linhas in secoes(T, E):
        print(titulo)
        for ok, texto in linhas:
            falhas += not ok
            print(texto)
    return falhas


def reprovados(T, E):
    return [(titulo, texto) for titulo, linhas in secoes(T, E) for ok, texto in linhas if not ok]


def rel(p):
    return os.path.relpath(p, ROOT)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else list(argv)
    spec_path, out_path = SPEC, OUT
    for flag in ('--spec', '--out'):
        if flag in argv:
            i = argv.index(flag)
            if i + 1 >= len(argv):
                sys.exit(f'gen_tokens: {flag} needs a file')
            caminho = argv[i + 1]
            del argv[i:i + 2]
            if flag == '--spec':
                spec_path = caminho
            else:
                out_path = caminho
    spec = json.load(open(spec_path, encoding='utf-8'))
    T = temas(spec)
    E = acentos(spec, T)
    texto = header(T, E)
    falhou = False
    if '--report' in argv:
        f = relatorio(T, E)
        print(f'{f} pair(s) under target' if f else 'every pair at or over its target')
        falhou = f > 0
        if '--check' not in argv:
            return 1 if falhou else 0
    if '--check' in argv:
        atual = open(out_path, encoding='utf-8').read()
        if atual != texto:
            print(f'gen_tokens: {rel(out_path)} differs from what {rel(spec_path)} '
                  f'generates: run python3 tools/tft_ui/gen_tokens.py')
            return 1
        print('gen_tokens: the header matches the JSON')
        return 1 if falhou else 0
    # The write mode checks first and refuses on any failure, as design/build-tokens.mjs does: a header that breaks a floor or a
    # structural rule is never written.
    reprovas = reprovados(T, E)
    if reprovas:
        print(f'gen_tokens: refusing to write {rel(out_path)}: {len(reprovas)} check(s) failed')
        for titulo, linha in reprovas:
            print(f'  [{titulo[3:]}] {linha.strip()}')
        return 1
    open(out_path, 'w', encoding='utf-8').write(texto)
    print(f'gen_tokens: wrote {rel(out_path)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
