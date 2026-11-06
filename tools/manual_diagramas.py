#!/usr/bin/env python3
"""Draws, checks and lists the diagrams of the manual: the figures of tipo="diagrama" that chapters declare with an .svg (docs/images/manual/NN-slug.svg).

The diagrams are not drawn by hand and are not images of a device. Each is an SVG that this file draws from the sources at the commit: the pins from
src/simut_config.h and the build profiles (tools/generated/profiles.ini), the sensor probe's pair and addresses from src/sensors, the radio's GPIOs from
src/air/AirConfig.h. What the code does not say is a constant here, with where it comes from: the Pico W's physical pinout (the Raspberry Pi Pico W datasheet,
checked by tools/test_manual_diagramas.py against chapter 2's own table), the 4,7 kOhm of the chapter's pull-ups, and the example pins of the wiring figures.

The SVG names classes (dg-*) and never a colour: the manual's CSS (the CSS block of tools/build_manual.py) styles them with its tokens, and build_manual.py puts the <svg>
in the page itself, so a diagram follows the theme button and the page's fonts, which an <img> could not. The drawing is 720 px wide on a 4 px grid, text 13 px (12 px
in notes) in the reader's own font: its width varies from machine to machine, so the lines are broken here by a width estimate that errs on the wide side (the width of
DejaVu Sans), and the builder refuses a text that overlaps another or leaves the canvas.

  --write      draws the diagrams and writes them into --images (default docs/images/manual); the same code gives the same bytes.
  --check      draws them again from the sources and compares the TEXT with the committed files (not pixels: the drawing is deterministic, and the smoothing of a font
               changes with the version of FreeType): one line for each diagram that differs, is missing, has a literal colour (a fill, stroke or style that is not a class)
               or an id that does not start with its slug (an id in line is global to the page); for each .svg in the folder that no row of FIGURAS names; for each row whose
               block is gone from the chapters or has another file or tipo; for each block of tipo="diagrama" with an .svg that no row names; and for each block whose
               captura= is not the text --list prints. A block of tipo="diagrama" whose file still ends in .png (12-wifi-niveis.png, which tools/lcd_alpha/manual_lcd.py draws,
               and the diagrams not drawn yet) is not this tool's and is left alone. Exit 0 clean, 1 with findings, 2 when it cannot draw.
  --list       the table: the file, the chapter, the function and the captura= text of the figure block in the chapter, tab-separated.
  --preview P  writes P/<slug>.claro.html and P/<slug>.escuro.html for each diagram: the manual's own CSS (imported from build_manual.py, not copied), the figure in
               the same frame as in the page, and data-theme fixed.
  --images     the folder of the diagrams (the controls point it at a scratch copy).
  --capitulos  the folder of the chapters, docs/_manual (the controls point it at a scratch copy).
  --fonte      the root of the tree the numbers are read from, the repository by default (the controls point it at a scratch copy with one number changed).

A number that is not where the code says it is, a premise the figure rests on that the code no longer keeps (the alpha drives its LCD in parallel mode; the Air's charger
reads high with the charger on), two functions on one pin, or a role on a pin that is a sensor slot: the tool says which file and which pattern, or which pin, and exits 2.
It never draws from a guess. The captura= text says what the figure is: "desenhado pelo código (tools/manual_diagramas.py)", what it shows and where its numbers come
from. tools/test_manual_diagramas.py holds the controls of --check, one scratch copy and one fault each.
"""
import argparse
import html
import math
import pathlib
import re
import sys
import unicodedata
from typing import NamedTuple

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))        # explicit: python3 -I leaves the script's own folder off the path (build_manual.py, which --preview imports)
IMAGENS = ROOT / 'docs' / 'images' / 'manual'
CAPITULOS = ROOT / 'docs' / '_manual'
LARGURA = 720                        # the drawing's width: the manual's CSS shows it whole up to 720 px (.moldura-diagrama svg)
GRADE = 4                            # every coordinate and size is a multiple of it (Ângulo, rule 5)
FLAGS_PADRAO = re.M                  # the flags of every pattern the sources are read with; the controls mangle the matches with the same ones

CONFIG = 'src/simut_config.h'
PERFIS = 'tools/generated/profiles.ini'
AIR_CONFIG = 'src/air/AirConfig.h'
BME_SENSOR = 'src/sensors/BME280SensorDriver.h'
BME_DRIVER = 'src/sensors/BME280Driver.h'
BOOT = 'src/AppManager_Boot.cpp'


class Falta(Exception):
    """The tool cannot draw from these sources: a pattern it reads a number with is not where the code says it is, or the code no longer says what a figure rests on.
    The message names the file and the pattern (or the pin); main( ) turns it into exit 2."""


class Figura(NamedTuple):
    bloco: str      # the id of the figure block in the chapter: ::: {.figura #fig-02-ds18b20 ...}
    arquivo: str    # docs/images/manual/<arquivo>
    capitulo: str   # the chapter of docs/_manual that holds the block
    desenho: str    # the name, in this file, of the function that draws it: def desenho_x(num, slug) -> the SVG text
    nota: str       # what the figure shows and where its numbers come from: the middle of the captura= text
    tipo: str = 'diagrama'   # the tipo= of the block


# One row per diagram this tool draws, in the order of the chapters. The diagrams of the manual that are not here yet keep their .png in the chapter and are left alone by
# --check; a row is added with its drawing function, its block's arquivo= moved to .svg and its captura= made the text --list prints.
FIGURAS = (
    Figura('fig-02-ds18b20', '02-ds18b20.svg', 'cap02-hardware.md', 'desenho_ds18b20',
           'ligação de uma sonda DS18B20 de três fios ao Pico W, com o resistor de 4,7 kΩ entre o dado e o 3V3; os pinos físicos são os do datasheet do Pico W, '
           'o GP2 é um exemplo e vale qualquer pino de slot (o limite vem de src/simut_config.h)'),
    Figura('fig-02-dht22', '02-dht22.svg', 'cap02-hardware.md', 'desenho_dht22',
           'ligação de um módulo DHT22 de três terminais ao Pico W, com a nota do pull-up; os pinos físicos são os do datasheet do Pico W, '
           'o GP3 é um exemplo e vale qualquer pino de slot (o limite vem de src/simut_config.h)'),
    Figura('fig-02-bmx280', '02-bmx280.svg', 'cap02-hardware.md', 'desenho_bmx280',
           'ligação de um módulo BME280 de seis pinos ao Pico W, no par de pinos que a busca de sondas tenta (src/sensors/BME280SensorDriver.h), '
           'com o endereço do driver (src/sensors/BME280Driver.h) e os pinos físicos do datasheet do Pico W'),
)


def slug_de(arquivo):
    return arquivo[:-len('.svg')]


def captura(f):
    """The captura= text of the figure's block in the chapter."""
    return f'desenhado pelo código (tools/manual_diagramas.py): {f.nota}; refazer com python3 tools/manual_diagramas.py --write'


def tabela_ok():
    """A problem with the table itself, or None. A table that lets two figures share a block or a file, a captura= that breaks the attribute or a row with no drawing
    function would be found by no figure."""
    for campo in ('bloco', 'arquivo'):
        valores = [getattr(f, campo) for f in FIGURAS]
        for v in valores:
            if valores.count(v) > 1:
                return f'FIGURAS names the {campo} {v!r} twice'
    for f in FIGURAS:
        if not re.fullmatch(r'\d\d-[a-z0-9]+(-[a-z0-9]+)*\.svg', f.arquivo):
            return f'FIGURAS row {f.bloco!r}: the file {f.arquivo!r} is not NN-slug.svg'
        if f.tipo != 'diagrama':
            return f'FIGURAS row {f.bloco!r}: the kind {f.tipo!r} is not diagrama'
        if not callable(globals().get(f.desenho)):
            return f'FIGURAS row {f.bloco!r}: there is no function {f.desenho!r} in this file'
        if re.search(r'["}\n]', captura(f)):
            return f'FIGURAS row {f.bloco!r}: the captura= text has a double quote, a closing brace or a line break (it breaks the attribute)'
    return None


# ---- the numbers, read from the sources ----------------------------------------------------------------------------------------------------------------------
class Fonte:
    """The files the numbers are read from, under `raiz`. Every pattern a number is read with is recorded in `padroes` and every file in `lidos`: the controls copy
    those files into a scratch tree and mangle each pattern's matches there, so a reader added later is held to the same control without being listed anywhere."""

    def __init__(self, raiz=ROOT):
        self.raiz = pathlib.Path(raiz)
        self._texto = {}
        self.lidos = []
        self.padroes = []

    def ler(self, rel):
        if rel not in self._texto:
            try:
                self._texto[rel] = (self.raiz / rel).read_bytes().decode('utf-8')
            except (OSError, UnicodeDecodeError) as e:
                raise Falta(f'{rel}: cannot be read ({e}); the diagrams read their numbers from it') from None
            self.lidos.append(rel)
        return self._texto[rel]

    def achar(self, rel, padrao, texto=None):
        """The match of `padrao` in the file (or in `texto`, a part of it), or Falta naming both."""
        t = self.ler(rel) if texto is None else texto
        if (rel, padrao) not in self.padroes:
            self.padroes.append((rel, padrao))
        m = re.search(padrao, t, FLAGS_PADRAO)
        if m is None:
            raise Falta(f'{rel}: the pattern {padrao} was not found; the diagrams read their numbers there, so the code and the diagrams have to be looked at together')
        return m

    def define(self, rel, nome):
        """The number of `#define NOME n` in the file."""
        return int(self.achar(rel, rf'^#define\s+{nome}\s+(\d+)\b').group(1))

    def secao(self, env):
        """The text of [env:`env`] in the build profiles."""
        t = self.ler(PERFIS)
        m = self.achar(PERFIS, rf'^\[env:{env}\][ \t]*$')
        fim = re.search(r'^\[', t[m.end():], FLAGS_PADRAO)
        return t[m.end():m.end() + fim.start()] if fim else t[m.end():]

    def flags(self, env):
        """{name: value} of the -D flags of [env:`env`]; a bare -DNAME is '1'. The profile's flag wins over simut_config.h's default, as the compiler's -D does."""
        return {k: v or '1' for k, v in re.findall(r'^[ \t]+-D(\w+)(?:=(\S+))?[ \t]*$', self.secao(env), FLAGS_PADRAO)}

    def exigir_flag(self, env, nome, valor, porque):
        """Falta unless [env:`env`] builds with -D`nome`=`valor`: a premise the figure rests on."""
        padrao = rf'^[ \t]+-D{nome}={valor}[ \t]*$'
        secao = self.secao(env)
        if (PERFIS, padrao) not in self.padroes:
            self.padroes.append((PERFIS, padrao))
        if re.search(padrao, secao, FLAGS_PADRAO) is None:
            raise Falta(f'{PERFIS}: [env:{env}] no longer matches the pattern {padrao}, and {porque}')


# The Raspberry Pi Pico W's pinout, from its datasheet (the Pico W pinout figure and its pin table): pin 1 is GP0 at the USB end of the left row and the pins run down the
# left (1 to 20) and up the right (21 to 40) as seen from above, so 40 is VBUS beside the USB connector. Pin 33 is AGND, the analogue ground. GP23, GP24, GP25 and GP29 have
# no pin: the Pico W uses them for the radio. tools/test_manual_diagramas.py compares this table with the one in docs/_manual/cap02-hardware.md, which was written apart.
PINAGEM = {1: 'GP0', 2: 'GP1', 3: 'GND', 4: 'GP2', 5: 'GP3', 6: 'GP4', 7: 'GP5', 8: 'GND', 9: 'GP6', 10: 'GP7', 11: 'GP8', 12: 'GP9', 13: 'GND', 14: 'GP10',
           15: 'GP11', 16: 'GP12', 17: 'GP13', 18: 'GND', 19: 'GP14', 20: 'GP15', 21: 'GP16', 22: 'GP17', 23: 'GND', 24: 'GP18', 25: 'GP19', 26: 'GP20',
           27: 'GP21', 28: 'GND', 29: 'GP22', 30: 'RUN', 31: 'GP26', 32: 'GP27', 33: 'AGND', 34: 'GP28', 35: 'ADC_VREF', 36: '3V3', 37: '3V3_EN',
           38: 'GND', 39: 'VSYS', 40: 'VBUS'}
PINO_DO_GPIO = {int(nome[2:]): pino for pino, nome in PINAGEM.items() if nome.startswith('GP')}
EXTERNOS = tuple(sorted(g for g in PINO_DO_GPIO if g >= 16))     # the GPIOs beyond GP15 that reach the edge: GP16 to GP22 and GP26 to GP28
PINO_3V3, PINO_VBUS, PINO_GND = 36, 40, 38                       # the pins the wiring figures name; 38 is one of the seven GND pins and sits beside 3V3
RESISTOR = '4,7 kΩ'                  # chapter 2 (lines 164, 166, 181, 267) and the DS18B20's and the I2C's usual pull-up; the firmware does not know the resistor
EXEMPLO_DS18B20, EXEMPLO_DHT22 = 2, 3   # the example GPIOs of DIAGRAMAS_ESPEC.md: any slot pin would do

IMAGEM_DO_PERFIL = {'release': 'pico_w_release', 'alpha': 'pico_w_alpha', 'air': 'pico_w_air'}


class Numeros:
    """What the diagrams draw, read from `fonte` when first asked and kept: the drawing functions ask for names, and each answer is a pattern in a file."""

    def __init__(self, fonte):
        self.f = fonte

    def macro(self, nome, imagem):
        """`nome` as the build of `imagem` has it: simut_config.h's #define, overridden by a -D of the profile when the profile has one."""
        valor = self.f.define(CONFIG, nome)
        sobre = self.f.flags(IMAGEM_DO_PERFIL[imagem]).get(nome)
        if sobre is not None:
            try:
                return int(sobre)
            except ValueError:
                raise Falta(f'{PERFIS}: [env:{IMAGEM_DO_PERFIL[imagem]}] sets -D{nome}={sobre}, which is not a number') from None
        return valor

    def slots(self, imagem='release'):
        """How many sensor slots the image has: GP0 to GP(n-1)."""
        return self.macro('MAX_SENSORS', imagem)

    def exemplo(self, gpio):
        """`gpio`, an example pin of a wiring figure, once it is known to be a pin a sensor slot takes."""
        if gpio >= self.slots():
            raise Falta(f'{CONFIG}: MAX_SENSORS is {self.slots()}, so GP{gpio} is not a sensor slot pin any more, and a figure uses it as the example of one')
        return gpio

    def spi0(self):
        """The three pins of the SPI0 bus the TFT and the touch share, as section 2 of simut_config.h names them."""
        m = self.f.achar(CONFIG, r'SPI bus: MOSI=GP(\d+), MISO=GP(\d+), SCK=GP(\d+)')
        return {'MOSI': int(m.group(1)), 'MISO': int(m.group(2)), 'SCK': int(m.group(3))}

    def radio(self):
        """The four GPIOs the Pico W keeps for its radio, as AirConfig.h's airPinValid( ) refuses them."""
        m = self.f.achar(AIR_CONFIG, r'case (\d+): case (\d+): case (\d+): case (\d+): return false;\s*/\* CYW43 on the Pico W \*/')
        return tuple(int(x) for x in m.groups())

    def bme(self):
        """The pair of GPIOs the sensor search probes for a BME280, and the two addresses the driver tries, the primary with SDO to GND."""
        m = self.f.achar(BME_SENSOR, r'BMx280PIO_RP2040 probe\((\d+), (\d+), BME280_ADDR_PRIMARY\)')
        sda, scl = int(m.group(1)), int(m.group(2))
        for g in (sda, scl):
            self.exemplo(g)
        m = self.f.achar(BME_DRIVER, r'\(addr == 0x([0-9A-Fa-f]{2})\) \? \(uint8_t\)0x([0-9A-Fa-f]{2}) : \(uint8_t\)0x([0-9A-Fa-f]{2})')
        primario, secundario, volta = m.groups()
        c = self.f.achar(BME_DRIVER, r'Tries primary address \(0x([0-9A-Fa-f]{2}), SDO→GND\) first, then secondary \(0x([0-9A-Fa-f]{2})\)')
        if primario != volta or (c.group(1), c.group(2)) != (primario, secundario):
            raise Falta(f'{BME_DRIVER}: the code (0x{primario}, 0x{secundario}, 0x{volta}) and its comment (0x{c.group(1)}, 0x{c.group(2)}) disagree on the BME280 addresses')
        return {'sda': sda, 'scl': scl, 'sdo_gnd': f'0x{primario}', 'sdo_3v3': f'0x{secundario}'}


def pino_de(gpio):
    return PINO_DO_GPIO[gpio]


# ---- the SVG builder -----------------------------------------------------------------------------------------------------------------------------------------
def n(v):
    """A number as the SVG writes it: whole numbers without ".0", the rest with at most two decimals and no trailing zero."""
    if isinstance(v, float):
        if v == int(v):
            return str(int(v))
        return f'{v:.2f}'.rstrip('0').rstrip('.')
    return str(v)


# Advance widths, in em, of DejaVu Sans, which is about as wide as a system-ui face gets: a line that fits with these fits in Segoe UI, Roboto, Helvetica and Arial.
_EM = {}
for _letras, _largura in (
        (' ', .32), ('ijl', .28), ("'|", .30), ('.,:;!', .34), ('()[]{}/\\-', .39), ('ft', .39), ('r', .41), ('s', .52), ('c', .55), ('k', .59), ('vxyz', .59),
        ('aeopbdqghnu', .63), ('m', .97), ('w', .82), ('0123456789', .64),
        ('IJ', .30), ('L', .56), ('F', .58), ('E', .63), ('P', .60), ('T', .61), ('Y', .61), ('S', .63), ('B', .69), ('K', .66), ('R', .70), ('AVXZ', .69), ('C', .70),
        ('DNUH', .75), ('GOQ', .79), ('M', .86), ('W', .99),
        ('+=<>~', .84), ('%', .95), ('&', .78), ('*', .50), ('_', .50), ('·', .32), ('–', .50), ('−', .84), ('—', 1.0), ('Ω', .76), ('²', .40), ('×', .84), ('°', .50),
        ('→', .84), ('≥', .84), ('µ', .64), ('#', .84), ('@', 1.0), ('?', .53), ('"', .46), ('$', .64)):
    for _c in _letras:
        _EM[_c] = _largura
LARGURA_MONO = .62       # DejaVu Sans Mono's .602 and a margin
LARGURA_OUTROS = .65     # a character the table does not know (an accented letter is its base letter)


def largura_texto(s, tam=13, mono=False):
    """A conservative width, in px, of `s` set at `tam` px: the text is drawn in the reader's font, so the width varies from machine to machine."""
    if mono:
        return math.ceil(len(s) * LARGURA_MONO * tam)
    return math.ceil(sum(_EM.get(unicodedata.normalize('NFD', c)[0], LARGURA_OUTROS) for c in s) * tam)


def quebrar(texto, largura, tam=13, mono=False):
    """`texto` broken at spaces into lines that fit in `largura` px by largura_texto( ). A word wider than a line is an error: breaking inside it, or letting it out of its
    box, would hide a layout that does not fit."""
    linhas, atual = [], ''
    for palavra in texto.split():
        if largura_texto(palavra, tam, mono) > largura:
            raise ValueError(f'the word {palavra!r} is wider than the {largura} px of its line')
        tentativa = f'{atual} {palavra}' if atual else palavra
        if atual and largura_texto(tentativa, tam, mono) > largura:
            linhas.append(atual)
            atual = palavra
        else:
            atual = tentativa
    if atual:
        linhas.append(atual)
    return linhas


class Svg:
    """One diagram's SVG, built from shapes on a 4 px grid. The attributes of every element are written in a fixed order and the numbers by n( ), so the same calls give the
    same bytes. Boxes are drawn inside their coordinates (a 1 px line at x + .5, so that it is a whole pixel at 720 px). Text is placed by its baseline; the builder keeps the
    estimated box of every text and refuses one that overlaps another, leaves the canvas or sits off the grid."""

    def __init__(self, slug, altura):
        if altura % GRADE:
            raise ValueError(f'{slug}: the height {altura} is off the {GRADE} px grid')
        self.slug, self.altura = slug, altura
        self.corpo, self.textos, self.pontas = [], [], set()

    def _grade(self, *valores):
        for v in valores:
            if v % GRADE:
                raise ValueError(f'{self.slug}: {v} is off the {GRADE} px grid')

    def caixa(self, x, y, w, h, cls='dg-caixa', raio=6, traco=1):
        """A box of w x h with its corner at (x, y); `raio` is 6 (a small box), 12 (a container) or 0; `traco` is the width of its line, drawn inside the box."""
        self._grade(x, y, w, h)
        if raio not in (0, 6, 12):
            raise ValueError(f'{self.slug}: a corner radius of {raio}: the Ângulo has 6 and 12')
        d = traco / 2
        self.corpo.append(f'<rect class="{cls}" x="{n(x + d)}" y="{n(y + d)}" width="{n(w - traco)}" height="{n(h - traco)}" rx="{raio}"/>')

    def texto(self, x, y, conteudo, cls='dg-t', ancora='start'):
        """A text with its baseline at y. `conteudo` is a string or a list of (class, string) runs, the class '' for plain text: the runs are how a GP name goes in the
        code's font in the middle of a sentence. The size comes from `cls`: dg-t is 13 px, dg-n is 12 px."""
        self._grade(x, y)
        runs = [('', conteudo)] if isinstance(conteudo, str) else conteudo
        classes = cls.split()
        tam = 12 if 'dg-n' in classes else 13
        w = sum(largura_texto(s, tam, 'dg-m' in classes or 'dg-m' in c.split()) for c, s in runs)
        x0 = x if ancora == 'start' else x - w if ancora == 'end' else x - w / 2
        caixa = (x0, y - tam + 2, x0 + w, y + 4)
        rotulo = ''.join(s for _, s in runs)
        if caixa[0] < 4 or caixa[2] > LARGURA - 4 or caixa[1] < 0 or caixa[3] > self.altura:
            raise ValueError(f'{self.slug}: the text {rotulo!r} (about {w} px wide, from x={n(x0)}) leaves the canvas')
        for outra, rot in self.textos:
            if min(caixa[2], outra[2]) - max(caixa[0], outra[0]) > 1 and min(caixa[3], outra[3]) - max(caixa[1], outra[1]) > 1:
                raise ValueError(f'{self.slug}: the text {rotulo!r} overlaps {rot!r}')
        self.textos.append((caixa, rotulo))
        corpo = ''.join(html.escape(s, quote=False) if not c else f'<tspan class="{c}">{html.escape(s, quote=False)}</tspan>' for c, s in runs)
        a = '' if ancora == 'start' else f' text-anchor="{ancora}"'
        self.corpo.append(f'<text class="{cls}" x="{x}" y="{y}"{a}>{corpo}</text>')

    def linha(self, pontos, cls='dg-fio'):
        self._grade(*[v for p in pontos for v in p])
        self.corpo.append(f'<polyline class="{cls}" points="{" ".join(f"{x},{y}" for x, y in pontos)}"/>')

    def seta(self, pontos, cls='dg-fio', ponta='dg-ponta'):
        """A line whose last point is an arrowhead's tip."""
        self._grade(*[v for p in pontos for v in p])
        self.pontas.add(ponta)
        self.corpo.append(f'<polyline class="{cls}" points="{" ".join(f"{x},{y}" for x, y in pontos)}" marker-end="url(#{self.slug}-{ponta[3:]})"/>')

    def no(self, x, y, cls='dg-no'):
        """A junction or a terminal: a dot of 8 px."""
        self._grade(x, y)
        self.corpo.append(f'<circle class="{cls}" cx="{x}" cy="{y}" r="4"/>')

    def render(self):
        partes = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {LARGURA} {self.altura}" role="img" class="dg">']
        if self.pontas:
            partes.append('<defs>')
            for ponta in sorted(self.pontas):
                partes.append(f'<marker id="{self.slug}-{ponta[3:]}" viewBox="0 0 8 8" refX="8" refY="4" markerWidth="8" markerHeight="8" markerUnits="userSpaceOnUse" '
                              f'orient="auto"><path class="{ponta}" d="M0 0L8 4L0 8z"/></marker>')
            partes.append('</defs>')
        partes += self.corpo
        partes.append('</svg>')
        return '\n'.join(partes) + '\n'


# ---- the drawings --------------------------------------------------------------------------------------------------------------------------------------------
def gp_e_pino(gpio):
    """The runs of a Pico W terminal's label: the GPIO in the code's font, then its physical pin."""
    return [('dg-m', f'GP{gpio}'), ('', f' · pino {pino_de(gpio)}')]


def caixas_da_ligacao(s, h, titulo, subtitulo):
    """The two boxes of a wiring figure, drawn before the wires so that a terminal's dot sits on top of the box's line: the Pico W at the left and the sensor's module at the
    right, both `h` tall and 232 px apart, 192 and 232 px wide."""
    s.caixa(24, 16, 192, h, 'dg-caixa-2', 12)
    s.texto(40, 36, 'Pico W', 'dg-t dg-b')
    s.caixa(464, 16, 232, h, 'dg-caixa-2', 12)
    s.texto(480, 36, titulo, 'dg-t dg-b')
    s.texto(480, 52, subtitulo, 'dg-n dg-c2')


def terminal(s, y, pico, modulo, papel=None, fio='dg-fio', ate=464, mono=True):
    """One wire at height y from the Pico W's edge to the module's: `pico` is the label's runs at the Pico's side, `modulo` the terminal's name at the module's (in the code's
    font when `mono`) and `papel` what it is for, in the note's size. The wire ends in a dot on each box's line."""
    s.texto(200, y + 4, pico, 'dg-t', 'end')
    s.texto(480, y + 4, modulo, 'dg-t dg-m' if mono else 'dg-t')
    if papel:
        s.texto(528, y + 4, papel, 'dg-n dg-c2')
    s.linha([(216, y), (ate, y)], fio)
    ponto = 'dg-no-acento' if fio == 'dg-fio-acento' else 'dg-no'
    s.no(216, y, ponto)
    s.no(ate, y, ponto)


def desenho_ds18b20(num, slug):
    gpio = num.exemplo(EXEMPLO_DS18B20)
    a, b, c = 72, 120, 168
    s = Svg(slug, 248)
    caixas_da_ligacao(s, 184, 'DS18B20', 'sonda de três fios')
    terminal(s, a, [('dg-m', '3V3'), ('', f' · pino {PINO_3V3}')], 'VDD', 'alimentação')
    terminal(s, b, gp_e_pino(gpio), 'DQ', 'dado')
    terminal(s, c, [('dg-m', 'GND'), ('', f' · pino {PINO_GND}')], 'GND', 'terra')
    # the pull-up: the resistor between the data wire and the 3V3 wire, the one thing the figure draws in the accent
    x = 400
    s.linha([(x, a), (x, a + 12)], 'dg-fio-acento')
    s.caixa(x - 8, a + 12, 16, 24, 'dg-comp-acento', 6, 2)
    s.linha([(x, a + 36), (x, b)], 'dg-fio-acento')
    s.no(x, a, 'dg-no-acento')
    s.no(x, b, 'dg-no-acento')
    s.texto(x + 20, a + 28, RESISTOR, 'dg-t')
    s.texto(24, 232, f'O dado liga num GPIO de slot (o GP{gpio} é um exemplo) e o GND, em qualquer pino de terra.', 'dg-n dg-c2')
    return s.render()


def desenho_dht22(num, slug):
    gpio = num.exemplo(EXEMPLO_DHT22)
    a, b, c = 72, 120, 168
    s = Svg(slug, 248)
    caixas_da_ligacao(s, 184, 'DHT22', 'módulo de três terminais')
    terminal(s, a, [('dg-m', '3V3'), ('', f' · pino {PINO_3V3}')], '+', 'alimentação', mono=False)
    terminal(s, b, gp_e_pino(gpio), 'dados', mono=False)
    terminal(s, c, [('dg-m', 'GND'), ('', f' · pino {PINO_GND}')], '−', 'terra', mono=False)
    s.texto(232, b + 28, 'pull-up conforme o módulo', 'dg-n dg-c2')
    s.texto(24, 232, f'O dado liga num GPIO de slot (o GP{gpio} é um exemplo) e o GND, em qualquer pino de terra.', 'dg-n dg-c2')
    return s.render()


def desenho_bmx280(num, slug):
    bme = num.bme()
    v, csb, sda, scl, sdo, gnd = 72, 104, 152, 200, 248, 280
    s = Svg(slug, 360)
    caixas_da_ligacao(s, 296, 'BME280', 'módulo de seis pinos')
    terminal(s, v, [('dg-m', '3V3'), ('', f' · pino {PINO_3V3}')], 'VCC', 'alimentação')
    terminal(s, sda, gp_e_pino(bme['sda']), 'SDA', 'dados', 'dg-fio-acento')
    terminal(s, scl, gp_e_pino(bme['scl']), 'SCL', 'relógio', 'dg-fio-acento')
    terminal(s, gnd, [('dg-m', 'GND'), ('', f' · pino {PINO_GND}')], 'GND', 'terra')
    # CSB and SDO are tied to the 3V3 and GND wires: a branch at x=424
    x = 424
    s.linha([(x, v), (x, csb), (464, csb)])
    s.no(x, v)
    s.no(464, csb)
    s.texto(480, csb + 4, 'CSB', 'dg-t dg-m')
    s.texto(528, csb + 4, '3V3: modo I²C', 'dg-n dg-c2')
    s.linha([(x, gnd), (x, sdo), (464, sdo)])
    s.no(x, gnd)
    s.no(464, sdo)
    s.texto(480, sdo + 4, 'SDO', 'dg-t dg-m')
    s.texto(528, sdo + 4, f'GND: endereço {bme["sdo_gnd"]}', 'dg-n dg-c2')
    s.texto(24, 344, f'pull-ups de {RESISTOR} em SDA e SCL, se o módulo não tiver', 'dg-n dg-c2')
    return s.render()


# ---- the chapters --------------------------------------------------------------------------------------------------------------------------------------------
class Bloco(NamedTuple):
    id: str
    tipo: str
    arquivo: str
    captura: str
    capitulo: str
    linha: int
    legenda: str


BLOCO = re.compile(r'^::: \{\.figura #(\S+)([^}\n]*)\}[ \t]*$', re.M)
ATRIBUTO = re.compile(r'(\w+)="([^"]*)"')


def ler_blocos(capitulos):
    """{id: Bloco} of every figure block of the chapters in `capitulos`. Raises OSError when the folder holds no chapter."""
    capitulos = pathlib.Path(capitulos)
    arquivos = sorted(capitulos.glob('cap*.md'))
    if not arquivos:
        raise OSError(f'no cap*.md in {capitulos}')
    blocos = {}
    for md in arquivos:
        texto = md.read_text(encoding='utf-8')
        for m in BLOCO.finditer(texto):
            a = dict(ATRIBUTO.findall(m.group(2)))
            legenda = texto[m.end():].split('\n:::', 1)[0].strip()
            blocos[m.group(1)] = Bloco(m.group(1), a.get('tipo', ''), a.get('arquivo', ''), a.get('captura', ''), md.name, texto.count('\n', 0, m.start()) + 1, legenda)
    return blocos


def conferir_capitulos(capitulos):
    """One line for each row whose block is gone or says another file or kind, each captura= that is not --list's, and each block of tipo="diagrama" with an .svg that no
    row names. A block of that kind whose file ends in .png is not this tool's."""
    achados = []
    blocos = ler_blocos(capitulos)
    nomeados = {f.bloco for f in FIGURAS}
    for f in FIGURAS:
        b = blocos.get(f.bloco)
        if b is None:
            achados.append(f'{f.bloco}: the figure block is gone from the chapters (the row of FIGURAS names {f.capitulo} and {f.arquivo})')
        elif (b.tipo, b.arquivo, b.capitulo) != (f.tipo, f.arquivo, f.capitulo):
            achados.append(f'{f.bloco}: the block says tipo={b.tipo!r}, arquivo={b.arquivo!r} in {b.capitulo}, the row says tipo={f.tipo!r}, arquivo={f.arquivo!r} in {f.capitulo}')
        elif b.captura != captura(f):
            achados.append(f'{f.bloco}: the captura= of the block ({b.capitulo}:{b.linha}) is not the text of FIGURAS (python3 tools/manual_diagramas.py --list prints it)')
    for b in blocos.values():
        if b.tipo == 'diagrama' and b.arquivo.endswith('.svg') and b.id not in nomeados:
            achados.append(f'{b.id}: a block of tipo="diagrama" with the SVG {b.arquivo} ({b.capitulo}:{b.linha}) that no row of FIGURAS in tools/manual_diagramas.py names')
    return achados


# ---- the files -----------------------------------------------------------------------------------------------------------------------------------------------
ATRIBUTO_DE_COR = re.compile(r'\s(fill|stroke|stop-color|flood-color|lighting-color|color|style)="([^"]*)"')
SEM_COR = ('none', 'currentColor', 'inherit')


def cores_literais(svg):
    """[(line, what)] for each place the SVG paints with something that is not a class: a <style>, a style= or any fill, stroke or colour attribute with a value of its own
    (a #hex, an rgb( ), a name). The diagrams are styled by the manual's CSS and its tokens; a colour in the file would not follow the theme."""
    achados = []
    for m in ATRIBUTO_DE_COR.finditer(svg):
        nome, valor = m.groups()
        if nome != 'style' and (valor in SEM_COR or valor.startswith('url(#')):
            continue
        achados.append((svg.count('\n', 0, m.start()) + 1, f'{nome}="{valor}"'))
    for m in re.finditer(r'<style\b', svg):
        achados.append((svg.count('\n', 0, m.start()) + 1, '<style>'))
    return sorted(achados)


def ids_sem_prefixo(svg, slug):
    """[(line, id)] for each id that does not start with the slug and a hyphen: an id in line is global to the page, and the manual holds a dozen diagrams."""
    return [(svg.count('\n', 0, m.start()) + 1, m.group(1)) for m in re.finditer(r'\sid="([^"]*)"', svg) if not m.group(1).startswith(slug + '-')]


def primeira_diferenca(a, b):
    """(line, column), from 1, of the first character where two texts differ."""
    i = next((k for k, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    return a.count('\n', 0, i) + 1, i - (a.rfind('\n', 0, i) + 1) + 1


def desenhar_todas(fonte):
    """{file: the SVG text} of every row, drawn from the sources in `fonte`. Raises Falta (or ValueError, for a layout that does not fit) when it cannot."""
    num = Numeros(fonte)
    return {f.arquivo: globals()[f.desenho](num, slug_de(f.arquivo)) for f in FIGURAS}


def conferir_imagens(imagens, desenhados):
    """One line for each diagram that differs from the code, is missing, paints with a literal colour or has an id without its slug, and for each orphan .svg."""
    imagens = pathlib.Path(imagens)
    achados = []
    for f in FIGURAS:
        caminho = imagens / f.arquivo
        if not caminho.is_file():
            achados.append(f'{f.arquivo}: missing (python3 tools/manual_diagramas.py --write draws it)')
            continue
        try:
            texto = caminho.read_bytes().decode('utf-8')
        except UnicodeDecodeError as e:
            achados.append(f'{f.arquivo}: not UTF-8 text ({e})')
            continue
        if texto != desenhados[f.arquivo]:
            linha, coluna = primeira_diferenca(texto, desenhados[f.arquivo])
            achados.append(f'{f.arquivo}: differs from what the code draws, first at line {linha} column {coluna} (python3 tools/manual_diagramas.py --write draws it again)')
        for linha, valor in cores_literais(texto):
            achados.append(f'{f.arquivo}:{linha}: literal colour {valor}; a diagram is styled by classes and the manual\'s tokens, never by a colour of its own')
        for linha, ident in ids_sem_prefixo(texto, slug_de(f.arquivo)):
            achados.append(f'{f.arquivo}:{linha}: the id {ident!r} does not start with {slug_de(f.arquivo)}-; an id in line is global to the page')
    nomes = {f.arquivo for f in FIGURAS}
    for p in sorted(imagens.glob('*.svg')):
        if p.name not in nomes:
            achados.append(f'{p.name}: no row of FIGURAS in tools/manual_diagramas.py names it')
    return achados


def gravar(imagens, desenhados):
    imagens = pathlib.Path(imagens)
    imagens.mkdir(parents=True, exist_ok=True)
    for f in FIGURAS:
        with open(imagens / f.arquivo, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(desenhados[f.arquivo])


# ---- the preview ---------------------------------------------------------------------------------------------------------------------------------------------
def pagina_previa(f, svg, legenda, tema):
    """A page with the manual's CSS and the figure in the frame the manual gives it, `tema` fixed: what a reader sees, one diagram at a time."""
    import build_manual as bm
    quadro, _ = bm.svg_fragment(svg, re.sub(r'`', '', legenda))
    texto = html.escape(legenda, quote=False)
    texto = re.sub(r'`([^`]*)`', r'<code>\1</code>', texto)
    css = bm.CSS.replace('__FONT__', '')
    return (f'<!doctype html>\n<html lang="pt-BR" data-theme="{tema}">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>{slug_de(f.arquivo)} ({tema})</title>\n<style>{css}</style>\n</head>\n<body>\n'
            f'<div class="pagina">\n<figure id="{f.bloco}" class="fig fig-diagrama"><div class="moldura moldura-diagrama">{quadro}</div>'
            f'<figcaption><b>Figura 1.</b> {texto}</figcaption></figure>\n</div>\n</body>\n</html>\n')


def previa(pasta, desenhados, capitulos):
    pasta = pathlib.Path(pasta)
    pasta.mkdir(parents=True, exist_ok=True)
    blocos = ler_blocos(capitulos)
    for f in FIGURAS:
        legenda = blocos[f.bloco].legenda if f.bloco in blocos else slug_de(f.arquivo)
        for tema in ('claro', 'escuro'):
            (pasta / f'{slug_de(f.arquivo)}.{tema}.html').write_text(pagina_previa(f, desenhados[f.arquivo], legenda, tema), encoding='utf-8')


# ---- the command line ----------------------------------------------------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    modo = ap.add_mutually_exclusive_group(required=True)
    modo.add_argument('--write', action='store_true', help='draw the diagrams and write them')
    modo.add_argument('--check', action='store_true', help='draw them again and compare the text with the committed files, and read the chapters')
    modo.add_argument('--list', action='store_true', help='file, chapter, function and captura= text of every diagram, tab-separated')
    modo.add_argument('--preview', metavar='PASTA', help='write <slug>.claro.html and <slug>.escuro.html for each diagram into PASTA')
    ap.add_argument('--images', default=str(IMAGENS), help='the folder of the diagrams (default docs/images/manual)')
    ap.add_argument('--capitulos', default=str(CAPITULOS), help='the folder of the chapters (default docs/_manual)')
    ap.add_argument('--fonte', default=str(ROOT), help='the root of the tree the numbers are read from (default: the repository)')
    a = ap.parse_args(argv)
    problema = tabela_ok()
    if problema:
        print(f'manual_diagramas: {problema}', file=sys.stderr)
        return 2
    if a.list:
        for f in FIGURAS:
            print('\t'.join((f.arquivo, f.capitulo, f.desenho, captura(f))))
        return 0
    try:
        desenhados = desenhar_todas(Fonte(a.fonte))
    except (Falta, ValueError) as e:
        print(f'manual_diagramas: {e}', file=sys.stderr)
        return 2
    if a.write:
        gravar(a.images, desenhados)
        print(f'manual_diagramas: wrote {len(FIGURAS)} diagrams to {a.images}')
        return 0
    try:
        if a.preview:
            previa(a.preview, desenhados, a.capitulos)
            print(f'manual_diagramas: wrote {2 * len(FIGURAS)} pages to {a.preview}')
            return 0
        achados = conferir_imagens(a.images, desenhados) + conferir_capitulos(a.capitulos)
    except OSError as e:
        print(f'manual_diagramas: cannot read the chapters: {e}', file=sys.stderr)
        return 2
    for linha in achados:
        print(linha)
    print(f'manual_diagramas: {len(FIGURAS)} diagrams drawn again from the sources and compared as text, the chapters read, {len(achados)} finding(s)')
    return 1 if achados else 0


if __name__ == '__main__':
    sys.exit(main())
