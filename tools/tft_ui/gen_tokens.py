#!/usr/bin/env python3
"""gen_tokens.py [--check] [--report] — writes src/ui/angulo/kit/angulo_tokens.h from
tools/tft_ui/tokens_tft.json: the 17 colour roles of both themes in RGB565, the "disabled"
version of each (45 % over the background, pre-mixed, since RGB565 has no alpha), the
spacing scale and the radii.

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

--check   regenerate in memory and compare with the header; exit 1 when they differ (the
          header was edited by hand, or the JSON changed without a regeneration).
--report  print the contrast of the pairs the panel depends on, computed on the colours the
          panel shows (RGB565 expanded back to 8 bits), against the panel's targets; exit 1
          when a pair is under its target. With both, exit 1 when either fails (the CI's
          gate). Without --check or --report the header is written.
"""
import json
import math
import os
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


def header(T):
    hex4 = lambda v: '0x' + format(v, 'X').zfill(4)
    campos = '\n'.join(f'  uint16_t {p};' for p in PAPEIS)
    blocos = ''
    for nome in ('claro', 'escuro'):
        h = T[nome]
        c = {p: hex565(h[p]) for p in PAPEIS}
        linhas = '\n'.join(f'  /* {p.ljust(13)} {h[p]} */ {hex4(c[p])},' for p in PAPEIS)
        desab = '\n'.join(f'  /* {p.ljust(13)} */ {hex4(blend565(c[p], c["fundo"], 0.45))},' for p in PAPEIS)
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

{blocos}// Escala de espaco (grade de 4 px): toda margem, folga e padding e um destes
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


def relatorio(T):
    falhas = 0
    for nome in ('claro', 'escuro'):
        c = {p: hex565(T[nome][p]) for p in PAPEIS}
        print(f'== {nome}')
        for a, b, alvo, desc in ALVOS:
            r = contraste(c[a], c[b])
            ok = r >= alvo
            falhas += not ok
            print(f'  {"ok  " if ok else "FAIL"} {r:5.2f}:1 (>= {alvo}) {a} on {b}: {desc}')
    return falhas


def main():
    spec = json.load(open(SPEC, encoding='utf-8'))
    T = temas(spec)
    texto = header(T)
    falhou = False
    if '--report' in sys.argv:
        f = relatorio(T)
        print(f'{f} pair(s) under target' if f else 'every pair at or over its target')
        falhou = f > 0
        if '--check' not in sys.argv:
            return 1 if falhou else 0
    if '--check' in sys.argv:
        atual = open(OUT, encoding='utf-8').read()
        if atual != texto:
            print(f'gen_tokens: {os.path.relpath(OUT, ROOT)} differs from what {os.path.relpath(SPEC, ROOT)} '
                  f'generates: run python3 tools/tft_ui/gen_tokens.py')
            return 1
        print('gen_tokens: the header matches the JSON')
        return 1 if falhou else 0
    open(OUT, 'w', encoding='utf-8').write(texto)
    print(f'gen_tokens: wrote {os.path.relpath(OUT, ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
