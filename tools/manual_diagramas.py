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
RELEASE_MANIFEST = 'tools/release_manifest.py'
COMANDOS = 'src/CommandParser.cpp'
MENU = 'src/display/SettingsMenu.h'
WORKFLOW_OTA = '.github/workflows/release-ota.yml'


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
    Figura('fig-02-pinos-release', '02-pinos-release.svg', 'cap02-hardware.md', 'desenho_pinos_release',
           'vista de cima do Pico W com os 40 pinos numerados e os da release (SPI0, TFT, toque e cigarra) lidos de src/simut_config.h e do perfil pico_w_release; '
           'a pinagem física é a do datasheet do Pico W e os quatro GPIOs do rádio vêm de src/air/AirConfig.h'),
    Figura('fig-02-pinos-alpha', '02-pinos-alpha.svg', 'cap02-hardware.md', 'desenho_pinos_alpha',
           'vista de cima do Pico W com os 40 pinos numerados e os da alpha (LCD em paralelo, de GP16 a GP21, e cigarra) lidos de src/simut_config.h e do perfil pico_w_alpha; '
           'a pinagem física é a do datasheet do Pico W e os quatro GPIOs do rádio vêm de src/air/AirConfig.h'),
    Figura('fig-02-pinos-air', '02-pinos-air.svg', 'cap02-hardware.md', 'desenho_pinos_air',
           'vista de cima do Pico W com os 40 pinos numerados e os dois do Air (energia dos sensores e carregador) lidos de src/simut_config.h e do perfil pico_w_air; '
           'a pinagem física é a do datasheet do Pico W e os quatro GPIOs do rádio vêm de src/air/AirConfig.h'),
    Figura('fig-02-air-energia', '02-air-energia.svg', 'cap02-hardware.md', 'desenho_air_energia',
           'esquema do GP16 acionando a chave do lado positivo que alimenta os sensores e do divisor que leva os 5 V do carregador ao GP17; os pinos e a polaridade vêm de '
           'src/simut_config.h, o pull-down e o GP16 alto no boot de src/AppManager_Boot.cpp, e o resto é o texto do capítulo'),
    Figura('fig-02-ds18b20', '02-ds18b20.svg', 'cap02-hardware.md', 'desenho_ds18b20',
           'ligação de uma sonda DS18B20 de três fios ao Pico W, com o resistor de 4,7 kΩ entre o dado e o 3V3; os pinos físicos são os do datasheet do Pico W, '
           'o GP2 é um exemplo e vale qualquer GPIO de sensor (os limites vêm de MAX_SENSORS, em src/simut_config.h)'),
    Figura('fig-02-dht22', '02-dht22.svg', 'cap02-hardware.md', 'desenho_dht22',
           'ligação de um módulo DHT22 de três terminais ao Pico W, com a nota do pull-up; os pinos físicos são os do datasheet do Pico W, '
           'o GP3 é um exemplo e vale qualquer GPIO de sensor (os limites vêm de MAX_SENSORS, em src/simut_config.h)'),
    Figura('fig-02-bmx280', '02-bmx280.svg', 'cap02-hardware.md', 'desenho_bmx280',
           'ligação de um módulo BME280 de seis pinos ao Pico W, no par de pinos que a busca de sondas tenta (src/sensors/BME280SensorDriver.h), '
           'com o endereço do driver (src/sensors/BME280Driver.h) e os pinos físicos do datasheet do Pico W'),
    Figura('fig-03-arquivos', '03-arquivos.svg', 'cap03-instalar.md', 'desenho_arquivos',
           'os arquivos de uma versão (quatro imagens, em colunas neutras, dois pacotes de idioma, o manifesto e as somas) e o caminho de cada um até o aparelho, com o acento só nos '
           'caminhos; as imagens vêm de ENVS em tools/release_manifest.py, os pacotes de data/lang e os nomes do manifesto e das somas do passo Checksums de .github/workflows/release-ota.yml'),
    Figura('fig-04-fluxo', '04-fluxo.svg', 'cap04-primeiro-boot.md', 'desenho_primeiro_boot',
           'fluxograma do primeiro boot, da senha do admin ao aparelho pronto, com os dois caminhos para pôr o aparelho na rede; o nome e o endereço do ponto de acesso, o número do '
           'item do menu (a posição de MENU_SETUP_AP em MENU_ORDER) e as palavras dos comandos vêm de src/StorageManager.cpp, src/net/NetworkManager_Cyw43.cpp, src/WebAddress.h, '
           'src/display/SettingsMenu.h, src/CommandParser.cpp e src/AppManager_Commands.cpp'),
    Figura('fig-15-caminho', '15-caminho.svg', 'cap15-historico.md', 'desenho_caminho_historico',
           'o caminho de um registro: o bloco aberto na RAM, a cópia em .wip, a selagem no arquivo do dia e a fila de arquivos diários, com o limite de ocupação que apaga o mais antigo; '
           'os registros por bloco vêm de src/HistoryV5.h, o limite de src/StorageManager.cpp e os nomes de src/StorageManager.h e src/HistoryV5.h'),
    Figura('fig-21-cursor', '21-cursor.svg', 'cap21-telemetria.md', 'desenho_cursor',
           'a fila de registros do histórico com o cursor entre os confirmados e os pendentes, o lote, o coletor, a confirmação 2xx que move o cursor e a falha que o deixa parado; '
           'a faixa de códigos que confirma vem de src/TelemetryManager.cpp e a espera depois de uma falha de BACKOFF_MIN_MS e BACKOFF_MAX_MS em src/TelemetryManager.h'),
    Figura('fig-22-fluxo', '22-fluxo.svg', 'cap22-linha-alarmes.md', 'desenho_linha_alarmes',
           'a fila de alarmes na RAM e as duas raias, HTTP e MQTT, com a confirmação que tira os registros da fila; o reenvio, a espera da resposta e as pontas dos dois tópicos vêm de '
           'src/TelemetryManager.h, src/SystemDefs_Network.h e src/TelemetryManager.cpp'),
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
        self.listados = []

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

    def listar(self, rel, glob):
        """The names of the files of the folder `rel` that match `glob`, sorted; Falta when there is none: the diagrams name what the folder holds. The controls copy those
        folders (Fonte.listados) into a scratch tree, as they copy the files read (Fonte.lidos)."""
        if (rel, glob) not in self.listados:
            self.listados.append((rel, glob))
        try:
            nomes = sorted(p.name for p in (self.raiz / rel).glob(glob) if p.is_file())
        except OSError as e:
            raise Falta(f'{rel}: cannot be listed ({e}); the diagrams name the files it holds') from None
        if not nomes:
            raise Falta(f'{rel}: no file matches {glob}; the diagrams name the files it holds')
        return nomes

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
PINO_3V3, PINO_GND = 36, 38          # the pins the wiring figures name: 3V3 (OUT), and one of the seven GND pins, on the same row of the board
RESISTOR = '4,7 kΩ'                  # chapter 2 (lines 164, 166, 181 and 267) gives it for the DS18B20's data line and the I2C's two; the firmware does not know the resistor
EXEMPLO_DS18B20, EXEMPLO_DHT22 = 2, 3   # the example GPIOs of the wiring figures, those chapter 6 shows (cap06-sensores.md:59, a DS18B20 on GP2 and a DHT22 on GP3): any sensor GPIO would do

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
        """`gpio`, an example pin of a wiring figure, once it is known to be a sensor GPIO (GP0 to GP(MAX_SENSORS - 1))."""
        if gpio >= self.slots():
            raise Falta(f'{CONFIG}: MAX_SENSORS is {self.slots()}, so GP{gpio} is not a sensor GPIO any more, and a figure uses it as the example of one')
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

    def papeis(self, imagem):
        """{gpio: (label, device)} of the pins beyond the sensor slots that `imagem` uses, as the sources say, in the order of the physical pins. Two functions on one pin,
        or a function on a pin that is a sensor slot or is not on the edge, is Falta: the map would draw a wiring that cannot be built."""
        env, slots, p = IMAGEM_DO_PERFIL[imagem], self.slots(imagem), {}

        def poe(gpio, rotulo, dispositivo=''):
            if gpio not in EXTERNOS:
                raise Falta(f'{CONFIG}: in the {imagem} image {rotulo} is on GP{gpio}, which is {"a sensor slot pin" if gpio < slots else "not one of GP16 to GP22 and GP26 to GP28"}; '
                            f'the map of pins draws those as functions')
            if gpio in p:
                raise Falta(f'{CONFIG}: in the {imagem} image GP{gpio} has two functions, {p[gpio][0]} and {rotulo}')
            p[gpio] = (rotulo, dispositivo)

        macro = lambda nome: self.macro(nome, imagem)
        if imagem == 'release':
            spi = self.spi0()
            poe(spi['MISO'], 'MISO', 'TFT e toque')
            poe(macro('TOUCH_CS'), 'T_CS', 'toque')
            poe(spi['SCK'], 'SCK', 'TFT e toque')
            poe(spi['MOSI'], 'MOSI', 'TFT e toque')
            poe(macro('TOUCH_IRQ'), 'T_IRQ', 'toque')
            poe(macro('BUZZER_PIN'), 'cigarra')
            poe(macro('TFT_RST'), 'RST', 'TFT')
            poe(macro('TFT_DC'), 'DC', 'TFT')
            poe(macro('TFT_CS'), 'CS', 'TFT')
        elif imagem == 'alpha':
            self.f.exigir_flag(env, 'HD44780_MODE_PARALLEL', '1', 'the alpha of chapter 2 drives its LCD in parallel mode on GP16 to GP21 (the I2C backpack would take GP26 and GP27)')
            for nome, rotulo in (('HD44780_RS', 'RS'), ('HD44780_EN', 'EN'), ('HD44780_D4', 'D4'), ('HD44780_D5', 'D5'), ('HD44780_D6', 'D6'), ('HD44780_D7', 'D7')):
                poe(macro(nome), rotulo, 'LCD')
            poe(macro('BUZZER_PIN'), 'cigarra')
        else:
            self.f.exigir_flag(env, 'SIMUT_AIR', '1', 'the Air has no display and no buzzer, and only its two pins beyond the slots')
            self.carregador()
            poe(macro('AIR_SENSOR_POWER_PIN'), 'energia dos sensores', 'saída')
            poe(macro('AIR_CHARGER_PIN'), 'carregador', 'entrada')
        return p


    def carregador(self):
        """The premises of the Air's power figure, which draws a divider off 5 V into an input with a pull-down that reads high with the charger on, and the sensors' switch driven
        high while the device is awake: the polarity setting, and the two lines of AppManager_Boot.cpp that set the pins."""
        alto = self.f.define(CONFIG, 'AIR_CHARGER_ACTIVE_HIGH')
        if alto != 1:
            raise Falta(f'{CONFIG}: AIR_CHARGER_ACTIVE_HIGH is {alto}, and the figure of the Air\'s power draws a divider off 5 V that reads high with the charger on (chapter 2); '
                        f'the figure has to be drawn for what that setting means first')
        self.f.achar(BOOT, r'gpio_pull_down\(AIR_CHARGER_PIN\)')
        self.f.achar(BOOT, r'airSensorPower\(AIR_SENSOR_POWER_PIN, true\)')


    def imagens(self):
        """[(the image's published name, its build environment)] in the order tools/release_manifest.py lists them (ENVS): release, alpha, air and releasetwo."""
        m = self.f.achar(RELEASE_MANIFEST, r'^ENVS = \{([^}]*)\}')
        pares = re.findall(r'"(\w+)":\s*"(\w+)"', m.group(1))
        if len(pares) < 2:
            raise Falta(f'{RELEASE_MANIFEST}: ENVS names {len(pares)} image(s), and the figure of a version\'s files draws a column for each')
        return pares

    def publicados(self):
        """(the names of the language packs a release carries, the manifest's name, the checksum file's): the packs are what data/lang holds, the other two the Checksums
        step of the release workflow, which sums *.bin, *.uf2, *.lng and the manifest into the checksum file."""
        m = self.f.achar(WORKFLOW_OTA, r'sha256sum \*\.bin \*\.uf2 \*\.lng (\S+) > (\S+)')
        return self.f.listar('data/lang', '*.lng'), m.group(1), m.group(2)


    def ap(self):
        """The setup access point: its network name (the device's default name, which StorageManager gives a new config, and the suffix NetworkManager appends), its
        address (SIMUT_AP_IP_TEXT) and the number of its item in the panel's menu (the place of MENU_SETUP_AP in MENU_ORDER, counted from 1)."""
        nome = self.f.achar('src/StorageManager.cpp', r'safeCopy\(_currentConfig\.deviceName, "([^"]+)", sizeof').group(1)
        sufixo = self.f.achar('src/net/NetworkManager_Cyw43.cpp', r'String apName = String\(deviceName\) \+ "([^"]+)";').group(1)
        ip = self.f.achar('src/WebAddress.h', r'^#define SIMUT_AP_IP_TEXT "([^"]+)"').group(1)
        itens = re.findall(r'MENU_\w+', self.f.achar(MENU, r'MENU_ORDER\[\] = \{([^}]*)\}').group(1))
        if 'MENU_SETUP_AP' not in itens:
            raise Falta(f'{MENU}: MENU_ORDER no longer lists MENU_SETUP_AP, and the figure of the first boot sends the release to that item of the menu')
        return {'ssid': nome + sufixo, 'ip': ip, 'item': itens.index('MENU_SETUP_AP') + 1}

    def comandos(self):
        """The console commands the first boot uses, as the parser and the reload's own message spell them: ap, enable, configure terminal, system ssid, system pass
        and reload confirm. The words are captured, not matched: a command renamed in the parser is a command renamed in the figure."""
        f = self.f
        ap = f.achar(COMANDOS, r'if \(t0 == "(\w+)" \|\| [^)]*\) \{ cmd\.type = CMD_AP;').group(1)
        enable = f.achar(COMANDOS, r'if \(t0 == "(\w+)"\)\s*\{ cmd\.type = CMD_ENABLE;').group(1)
        c = f.achar(COMANDOS, r't0 == "config" \|\| t0 == "(\w+)"\) && t1 == "(\w+)"\) \{ cmd\.type = CMD_CONFIGURE;')
        sistema = f.achar(COMANDOS, r'if \(t0 == "(system)"\) \{').group(1)
        ssid = f.achar(COMANDOS, r'if \(t1 == "(\w+)"\)\s+\{ cmd\.type = CMD_SET_WIFI_SSID').group(1)
        senha = f.achar(COMANDOS, r'if \(t1 == "(\w+)"\)\s+\{ cmd\.type = CMD_SET_WIFI_PASS').group(1)
        reload = f.achar('src/AppManager_Commands.cpp', r"case CMD_RELOAD:\s+if \(!cmd\.confirmed\) \{[^}]*?Use '([^']+)' para prosseguir").group(1)
        return {'ap': ap, 'enable': enable, 'configure': f'{c.group(1)} {c.group(2)}', 'ssid': f'{sistema} {ssid}', 'pass': f'{sistema} {senha}', 'reload': reload}


    def historico(self):
        """The history's numbers: the records a block holds (H5_BLOCK_MAX_RECORDS), the percentage of the file system above which the oldest day is deleted (the cleanup
        in StorageManager.cpp), the folder, the open block's snapshot in it and the extension of a day's file."""
        f = self.f
        return {'registros': int(f.achar('src/HistoryV5.h', r'^#define H5_BLOCK_MAX_RECORDS\s+(\d+)').group(1)),
                'limite': int(f.achar('src/StorageManager.cpp', r'\(\(info\.usedBytes \* 100\) / info\.totalBytes\) <= (\d+)\) return;').group(1)),
                'pasta': f.achar('src/StorageManager.h', r'^#define DIR_HISTORY "([^"]+)"').group(1),
                'wip': f.achar('src/StorageManager.h', r'^#define FILE_H5_WIP DIR_HISTORY "([^"]+)"').group(1),
                'ext': f.achar('src/HistoryV5.h', r'^#define HISTORY_FILE_EXT\s+"([^"]+)"').group(1)}


    def telemetria(self):
        """What the collector's answer means to the telemetry: the HTTP codes that confirm a batch (the test in TelemetryManager.cpp, as "2xx") and the wait after a failure,
        in seconds, from its first to its ceiling (BACKOFF_MIN_MS and BACKOFF_MAX_MS in TelemetryManager.h)."""
        f = self.f
        m = f.achar('src/TelemetryManager.cpp', r'if \(code >= (\d+) && code < (\d+)\) \{')
        baixo, alto = int(m.group(1)), int(m.group(2))
        if baixo % 100 or alto != baixo + 100:
            raise Falta(f'src/TelemetryManager.cpp: the confirming codes are {baixo} to {alto - 1}, no longer one whole class (2xx), which is what the figure of the cursor draws')
        espera = (int(f.achar('src/TelemetryManager.h', r'BACKOFF_MIN_MS = (\d+);').group(1)), int(f.achar('src/TelemetryManager.h', r'BACKOFF_MAX_MS = (\d+);').group(1)))
        return {'ok': f'{baixo // 100}xx', 'espera_min': espera[0] // 1000, 'espera_max': espera[1] // 1000}


    def alarmes(self):
        """The alarm line's numbers: the wait before the queue is sent again (ALARM_RETRY_INTERVAL_MS), the time an HTTP answer is waited for (NET_SOCKET_TIMEOUT_MS) and the
        two MQTT topics' ends, the data topic's with "/alarm" and with "/alarm/ack"."""
        f = self.f
        return {'reenvio': int(f.achar('src/TelemetryManager.h', r'ALARM_RETRY_INTERVAL_MS = (\d+);').group(1)) // 1000,
                'espera': int(f.achar('src/SystemDefs_Network.h', r'NET_SOCKET_TIMEOUT_MS = (\d+);').group(1)) // 1000,
                'topico': f.achar('src/TelemetryManager.cpp', r'mqttAlarmTopic\( \) \{\s+return mqttDataTopic\( \) \+ "([^"]+)";').group(1),
                'ack': f.achar('src/TelemetryManager.cpp', r'mqttAlarmAckTopic\( \) \{\s+return mqttDataTopic\( \) \+ "([^"]+)";').group(1)}


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
        (' \u00a0', .32), ('ijl', .28), ("'|", .30), ('.,:;!', .34), ('()[]{}/\\-', .39), ('ft', .39), ('r', .41), ('s', .52), ('c', .55), ('k', .59), ('vxyz', .59),
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
    box, would hide a layout that does not fit. A no-break space (U+00A0, as in "3,3 V") keeps its two words on one line."""
    linhas, atual = [], ''
    for palavra in texto.split(' '):
        if not palavra:
            continue
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

    def __init__(self, slug, altura=None):
        """`altura` is the canvas height, or None to let the drawing say: the height is then its lowest point and 16 px (a flow is laid out from the top, and its height is
        what its boxes add up to)."""
        if altura is not None and altura % GRADE:
            raise ValueError(f'{slug}: the height {altura} is off the {GRADE} px grid')
        self.slug, self._altura, self.fundo = slug, altura, 0
        self.corpo, self.textos, self.pontas = [], [], set()

    @property
    def altura(self):
        return self._altura if self._altura is not None else -(-(self.fundo + 16) // GRADE) * GRADE

    def _abaixo(self, y):
        self.fundo = max(self.fundo, y)

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
        self._abaixo(y + h)
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
        self._abaixo(caixa[3])
        if caixa[0] < 4 or caixa[2] > LARGURA - 4 or caixa[1] < 0 or (self._altura is not None and caixa[3] > self._altura):
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
        self._abaixo(max(y for _, y in pontos))
        self.corpo.append(f'<polyline class="{cls}" points="{" ".join(f"{x},{y}" for x, y in pontos)}"/>')

    def seta(self, pontos, cls='dg-fio', ponta='dg-ponta'):
        """A line whose last point is an arrowhead's tip."""
        self._grade(*[v for p in pontos for v in p])
        self._abaixo(max(y for _, y in pontos))
        self.pontas.add(ponta)
        self.corpo.append(f'<polyline class="{cls}" points="{" ".join(f"{x},{y}" for x, y in pontos)}" marker-end="url(#{self.slug}-{ponta[3:]})"/>')

    def no(self, x, y, cls='dg-no'):
        """A junction or a terminal: a dot of 8 px."""
        self._grade(x, y)
        self._abaixo(y + 4)
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


def nota_do_dado(num, gpio):
    """The note under a one-wire figure: the data wire goes on any sensor GPIO, GP0 to GP(MAX_SENSORS - 1) (the chapter's word is sensor, a slot being a place in the configuration,
    not a pin: cap06-sensores.md:86), `gpio` is only the example, and the ground on any ground pin."""
    return [('', 'O dado liga num GPIO de sensor ('), ('dg-m', 'GP0'), ('', ' a '), ('dg-m', f'GP{num.slots() - 1}'), ('', '; o '), ('dg-m', f'GP{gpio}'),
            ('', ' é um exemplo) e o GND, em qualquer pino de terra.')]


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
    s.texto(24, 232, nota_do_dado(num, gpio), 'dg-n dg-c2')
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
    s.texto(24, 232, nota_do_dado(num, gpio), 'dg-n dg-c2')
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


BX, BY, BW, BH = 176, 36, 160, 428     # the board: its left edge, top edge, width and height
Y0, PASSO = 52, 20                      # the top of the first pad and the pitch of the rows
PAD_W, PAD_H = 24, 16
COL_NOME, COL_PAPEL = BX + BW + 8, BX + BW + 76       # the right side: where a pin's name starts, and what it is for

MAPAS = {
    'release': {'acento': 'TFT, toque e cigarra', '3v3': 'TFT e sensores', 'vbus': '5 V do USB', 'nota': None},
    'alpha': {'acento': 'LCD e cigarra', '3v3': 'sensores', 'vbus': '5 V do USB, para o LCD', 'nota': None},
    'air': {'acento': 'energia dos sensores e carregador', '3v3': 'sensores', 'vbus': '5 V do USB',
            'nota': ('GP{energia}', ' também é o sinal de acordado: fica em nível alto enquanto o aparelho está acordado.')},
}
CLASSES_DO_PINO = {'sensor': ('dg-caixa', ''), 'papel': ('dg-caixa-acento', ' dg-sobre-acento'), 'forte': ('dg-caixa-forte', ' dg-sobre-forte'),
                   'apagado': ('dg-caixa-apagada', ' dg-c2')}


def pad_de(pino):
    """(x, y) of the top left corner of a pin's pad: the left row runs down from pin 1, the right row up from pin 21, as seen from above."""
    linha = pino - 1 if pino <= 20 else 40 - pino
    return (BX + 4 if pino <= 20 else BX + BW - 4 - PAD_W), Y0 + PASSO * linha


def papel_do_pino(pino, papeis, slots, texto):
    """(kind, the role's label as runs or None) of a pin of the board in the image whose roles and slot count these are."""
    nome = PINAGEM[pino]
    if nome.startswith('GP'):
        g = int(nome[2:])
        if g < slots:
            return 'sensor', None
        if g in papeis:
            rotulo, dispositivo = papeis[g]
            return 'papel', (rotulo, dispositivo)
        return 'apagado', ('sem uso', '')
    if nome in ('GND', 'AGND'):
        return 'forte', None
    if nome == '3V3':
        return 'forte', (texto['3v3'], '')
    if nome == 'VBUS':
        return 'forte', (texto['vbus'], '')
    return 'apagado', None


def mapa_de_pinos(num, slug, imagem):
    papeis, slots, radio, texto = num.papeis(imagem), num.slots(imagem), num.radio(), MAPAS[imagem]
    s = Svg(slug, 544 if texto['nota'] is None else 568)
    s.caixa(BX, BY, BW, BH, 'dg-caixa-2', 12)
    s.caixa(BX + 56, 24, 48, 20, 'dg-caixa', 6)
    s.texto(BX + 80, 40, 'USB', 'dg-n dg-m dg-c2', 'middle')
    s.caixa(BX + 48, 68, 64, 48, 'dg-caixa', 6)
    s.texto(BX + 80, 96, 'RP2040', 'dg-n dg-m dg-c2', 'middle')
    s.texto(BX + 80, 216, 'Pico W', 'dg-t dg-b', 'middle')
    s.texto(BX + 80, 232, 'vista de cima', 'dg-n dg-c2', 'middle')
    s.caixa(BX + 32, 352, 96, 80, 'dg-caixa-apagada', 6)
    s.texto(BX + 80, 372, 'rádio', 'dg-n dg-c2', 'middle')
    for k, g in enumerate(radio):
        s.caixa(BX + 36 + 48 * (k % 2), 380 + 24 * (k // 2), 40, 16, 'dg-caixa-apagada', 6)
        s.texto(BX + 56 + 48 * (k % 2), 392 + 24 * (k // 2), f'GP{g}', 'dg-n dg-m dg-c2', 'middle')
    for pino in range(1, 41):
        x, y = pad_de(pino)
        tipo, papel = papel_do_pino(pino, papeis, slots, texto)
        cls, cor = CLASSES_DO_PINO[tipo]
        s.caixa(x, y, PAD_W, PAD_H, cls, 6)
        s.texto(x + 12, y + 12, str(pino), 'dg-n dg-m' + cor, 'middle')
        nome = PINAGEM[pino]
        if pino <= 20:
            s.texto(BX - 8, y + 12, nome, 'dg-t dg-m', 'end')
            continue
        s.texto(COL_NOME, y + 12, nome, 'dg-t dg-m' + (' dg-c2' if tipo == 'apagado' and papel is None else ''))
        if papel:
            rotulo, dispositivo = papel
            mono = rotulo.isupper()
            runs = [('dg-m' if mono else '', rotulo)] + ([('', f' · {dispositivo}')] if dispositivo else [])
            s.texto(COL_PAPEL, y + 12, runs, 'dg-t' + (' dg-c2' if tipo == 'apagado' else ''))
    # the group of the left row
    s.linha([(128, 60), (124, 60), (124, 440), (128, 440)], 'dg-fio-fino')
    s.texto(116, 236, 'sensores', 'dg-t', 'end')
    s.texto(116, 252, [('dg-m', 'GP0'), ('', ' a '), ('dg-m', f'GP{slots - 1}')], 'dg-t', 'end')
    s.texto(116, 268, f'{slots} slots', 'dg-n dg-c2', 'end')
    # the legend
    for k, (tipo, rot) in enumerate((('sensor', 'sensor'), ('papel', texto['acento']), ('forte', 'alimentação e terra'), ('apagado', 'sem uso, ou interno do Pico W'))):
        x, y = 16 + 352 * (k % 2), 488 + 24 * (k // 2)
        s.caixa(x, y, PAD_W, PAD_H, CLASSES_DO_PINO[tipo][0], 6)
        s.texto(x + 32, y + 12, rot, 'dg-t')
    if texto['nota'] is not None:
        energia = next(g for g, (rotulo, _) in papeis.items() if rotulo == 'energia dos sensores')
        s.texto(16, 548, [('dg-m', texto['nota'][0].format(energia=energia)), ('', texto['nota'][1])], 'dg-n dg-c2')
    return s.render()


def desenho_pinos_release(num, slug):
    return mapa_de_pinos(num, slug, 'release')


def desenho_pinos_alpha(num, slug):
    return mapa_de_pinos(num, slug, 'alpha')


def desenho_pinos_air(num, slug):
    return mapa_de_pinos(num, slug, 'air')


def caixa_de_texto(s, x, y, w, itens, cls='dg-caixa', raio=6, centro=None):
    """A box at x, `w` wide, whose text is broken to fit inside 12 px of margin on each side: `itens` is [(text, class)], each text broken by quebrar( ). The height is the
    lines' (8 px, 16 px a line, 8 px). `y` is the top, or None with `centro` the height the box is centred on. Returns (y, height)."""
    linhas = []
    for texto, c in itens:
        tam = 12 if 'dg-n' in c.split() else 13
        if isinstance(texto, str):
            linhas += [(l, c) for l in quebrar(texto, w - 24, tam, 'dg-m' in c.split())]
        else:                       # runs, in one line: the code's font for a command in the middle of a sentence
            if sum(largura_texto(tx, tam, 'dg-m' in cr.split() or 'dg-m' in c.split()) for cr, tx in texto) > w - 24:
                raise ValueError(f'{s.slug}: the line {"".join(tx for _, tx in texto)!r} does not fit in a box of {w} px')
            linhas.append((texto, c))
    h = 16 + 16 * len(linhas)
    if y is None:
        y = centro - h // 2
    s.caixa(x, y, w, h, cls, raio)
    for k, (l, c) in enumerate(linhas):
        s.texto(x + 12, y + 20 + 16 * k, l, c)
    return y, h


def desenho_air_energia(num, slug):
    num.carregador()
    g16, g17 = num.macro('AIR_SENSOR_POWER_PIN', 'air'), num.macro('AIR_CHARGER_PIN', 'air')
    s = Svg(slug, 352)
    y16, y17 = 96, 256
    s.caixa(24, 16, 152, 304, 'dg-caixa-2', 12)
    s.texto(40, 36, 'Pico W', 'dg-t dg-b')
    s.texto(160, y16 + 4, gp_e_pino(g16), 'dg-t', 'end')
    s.texto(160, y16 + 20, 'saída', 'dg-n dg-c2', 'end')
    s.texto(160, y17 + 4, gp_e_pino(g17), 'dg-t', 'end')
    s.texto(160, y17 + 20, 'entrada, com pull-down', 'dg-n dg-c2', 'end')
    # the switch the GP16 drives, and the bus it feeds
    y_chave, _ = caixa_de_texto(s, 256, None, 192, [('chave do lado positivo', 'dg-t dg-b'), ('MOSFET de canal P com acionamento, ou pino EN de um regulador de 3,3 V', 'dg-n dg-c2')],
                                'dg-caixa', 6, y16)
    caixa_de_texto(s, 544, None, 152, [('barramento de 3,3 V dos sensores', 'dg-t'), ('DS18B20, DHT22, BME280', 'dg-n dg-c2')], 'dg-caixa-2', 6, y16)
    s.seta([(176, y16), (256, y16)], 'dg-fio-acento', 'dg-ponta-acento')
    s.no(176, y16, 'dg-no-acento')
    s.texto(192, y16 - 8, 'controle', 'dg-n dg-c2')
    s.seta([(448, y16), (544, y16)])
    s.texto(460, y16 - 8, '3,3 V', 'dg-n dg-c2')
    s.seta([(352, 24), (352, y_chave)])
    s.texto(364, 40, f'entrada: 3V3 (pino {PINO_3V3}), ou a alimentação do regulador', 'dg-n dg-c2')
    s.texto(256, 176, [('dg-m', f'GP{g16}'), ('', ' alto = acordado: a chave liga os sensores')], 'dg-t')
    s.texto(256, 192, [('dg-m', f'GP{g16}'), ('', ' baixo = dormindo: os sensores ficam sem energia')], 'dg-t')
    # the charger, and the divider that brings its 5 V to the GP17
    caixa_de_texto(s, 544, None, 152, [('carregador de 5 V', 'dg-t')], 'dg-caixa-2', 6, y17)
    s.seta([(464, y17), (176, y17)], 'dg-fio-acento', 'dg-ponta-acento')
    s.linha([(544, y17), (496, y17)])
    s.caixa(464, y17 - 8, 32, 16, 'dg-comp', 6, 2)
    s.texto(480, y17 - 16, 'R1', 'dg-n dg-m dg-c2', 'middle')
    s.no(376, y17, 'dg-no-acento')
    s.linha([(376, y17), (376, y17 + 16)])
    s.caixa(368, y17 + 16, 16, 32, 'dg-comp', 6, 2)
    s.texto(392, y17 + 36, 'R2', 'dg-n dg-m dg-c2')
    s.linha([(376, y17 + 48), (376, y17 + 64)])
    for k, meia in enumerate((16, 12, 8)):
        s.linha([(376 - meia, y17 + 64 + 4 * k), (376 + meia, y17 + 64 + 4 * k)])
    s.texto(396, y17 + 76, 'GND', 'dg-n dg-c2')
    for k, l in enumerate(quebrar('o divisor leva os 5 V a 3,3 V: GP17 alto = carregador ligado', 168, 12)):
        s.texto(192, y17 + 28 + 16 * k, l, 'dg-n dg-c2')
    return s.render()


class Cx(NamedTuple):
    """A box drawn: its corner and size, and the points on its sides where a wire meets it (the middle of the side, which is on the grid when the width is a multiple of 8 and
    the height is 16 + 16 lines)."""
    x: int
    y: int
    w: int
    h: int

    def esq(self, dy=0):
        return (self.x, self.y + self.h // 2 + dy)

    def dir(self, dy=0):
        return (self.x + self.w, self.y + self.h // 2 + dy)

    def topo(self, dx=0):
        return (self.x + self.w // 2 + dx, self.y)

    def base(self, dx=0):
        return (self.x + self.w // 2 + dx, self.y + self.h)


def bloco(s, x, y, w, itens, cls='dg-caixa', raio=6, centro=None):
    """caixa_de_texto( ) that returns the box as a Cx."""
    y, h = caixa_de_texto(s, x, y, w, itens, cls, raio, centro)
    return Cx(x, y, w, h)


def desenho_arquivos(num, slug):
    imagens = [nome for nome, _ in num.imagens()]
    idiomas, manifesto, soma = num.publicados()
    s = Svg(slug, 392)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    # the images: one column for each, a row for the .uf2 and one for the .bin, none of them coloured: the column says which image, the arrow where the file goes
    largura, passo = 88, 96
    direita = 16 + passo * (len(imagens) - 1) + largura          # where the files end and the arrows start
    for i, nome in enumerate(imagens):
        s.caixa(16 + passo * i, 16, largura, 24, 'dg-caixa-2', 6)
        s.texto(16 + passo * i + largura // 2, 32, nome, 'dg-t', 'middle')
    for y, ext, titulo, nota in ((48, '.uf2', 'cabo USB, com o BOOTSEL', 'aparelho novo ou recuperação'),
                                 (104, '.bin', 'página Arquivos', 'atualização pela rede')):
        for i in range(len(imagens)):
            s.caixa(16 + passo * i, y, largura, 48, 'dg-caixa', 6)
            s.texto(16 + passo * i + largura // 2, y + 28, ext, 'dg-n dg-m', 'middle')
        d = bloco(s, 464, y, 240, [(titulo, 'dg-t'), (nota, 'dg-n dg-c2')])
        s.seta([(direita, d.esq()[1]), d.esq()], *ponta)
    # the packs, the manifest and the checksums: one chip each, ending where the images' files end, each beside its destination
    for y, nomes, titulo, nota in ((168, idiomas, 'página Arquivos', 'pasta /lang, o pacote de idioma'),
                                   (232, [manifesto], 'gestor de frota', 'soma SHA-256 de cada imagem'),
                                   (296, [soma], 'conferência do download', f'sha256sum -c {soma}')):
        d = bloco(s, 464, y, 240, [(titulo, 'dg-t'), (nota, 'dg-n dg-c2')])
        chip = min(184, (direita - 16 - 8 * (len(nomes) - 1)) // len(nomes) // 8 * 8)      # 184 wide, or as wide as the images' block allows; a multiple of 8 for the centre
        for k, nome in enumerate(nomes):
            if largura_texto(nome, 12, True) > chip - 8:
                raise ValueError(f'{slug}: {nome} does not fit in its chip of {chip} px')
            x = direita - chip - (chip + 8) * (len(nomes) - 1 - k)
            s.caixa(x, d.esq()[1] - 16, chip, 32, 'dg-caixa', 6)
            s.texto(x + chip // 2, d.esq()[1] + 4, nome, 'dg-n dg-m', 'middle')
        s.seta([(direita, d.esq()[1]), d.esq()], *ponta)
    s.texto(16, 376, [('', 'Cada imagem tem o nome '), ('dg-m', 'simut_v<versão>_<imagem>.uf2'), ('', ' e o mesmo com '), ('dg-m', '.bin'), ('', '.')], 'dg-n dg-c2')
    return s.render()


def desenho_primeiro_boot(num, slug):
    ap, cmd = num.ap(), num.comandos()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    # the first three steps, in a row: the console, the password, the panel's question (the release's only)
    passos = ([('Liga o aparelho com o console USB aberto', 'dg-t')],
              [('Anota a senha do admin', 'dg-t'), ('ela aparece uma única vez', 'dg-n dg-c2')],
              [('O painel pergunta a data e a hora', 'dg-t'), ('só na release: SALVAR ou PULAR', 'dg-n dg-c2')])
    caixas = [bloco(s, x, None, w, itens, 'dg-caixa', 6, 48) for (x, w), itens in zip(((16, 176), (224, 192), (448, 256)), passos)]
    for a, b in zip(caixas, caixas[1:]):
        s.seta([a.dir(), b.esq()], *ponta)
    # two ways to put the device on the network: the access point (release and alpha), the console (the Air)
    s.linha([caixas[2].base(), (caixas[2].base()[0], 96), (184, 96)])
    s.seta([(544, 96), (544, 112)], *ponta)
    s.no(544, 96)
    s.seta([(184, 96), (184, 112)], *ponta)
    esquerda = [
        [([('', 'Comando '), ('dg-m', cmd['ap']), ('', ' no console')], 'dg-t'), (f'ou, na release, o item {ap["item"]} do menu do painel', 'dg-n dg-c2')],
        [([('', 'Conecta na rede '), ('dg-m', ap['ssid'])], 'dg-t'), ('no celular ou no computador', 'dg-n dg-c2')],
        [([('', 'Abre '), ('dg-m', ap['ip'])], 'dg-t'), ('no navegador', 'dg-n dg-c2')],
        [([('', 'Entra como '), ('dg-m', 'admin'), ('', ' e troca a senha')], 'dg-t')],
        [('Página Rede', 'dg-t'), ('Buscar, escolher a rede e digitar a senha', 'dg-n dg-c2')],
        [('Salvar e reiniciar', 'dg-t')],
    ]
    direita = [[(cmd['enable'], 'dg-t dg-m')], [(cmd['configure'], 'dg-t dg-m')], [(f'{cmd["ssid"]} <rede>', 'dg-t dg-m')], [(f'{cmd["pass"]} <senha>', 'dg-t dg-m')],
               [(cmd['reload'], 'dg-t dg-m')]]
    altura = lambda itens: 16 + 16 * sum(1 if not isinstance(tx, str) else len(quebrar(tx, 280, 12 if 'dg-n' in c.split() else 13, 'dg-m' in c.split())) for tx, c in itens)
    alt_e = sum(altura(i) for i in esquerda) + 16 * (len(esquerda) - 1)
    folga = alt_e - sum(altura(i) for i in direita)
    if folga % (4 * (len(direita) - 1)):
        raise ValueError(f'{slug}: the Air\'s chain cannot be spread over the access point\'s height ({folga} px over {len(direita) - 1} gaps, on the 4 px grid)')
    topo, base = 152, 152 + alt_e
    s.caixa(16, 112, 336, base + 16 - 112, 'dg-caixa-2', 12)
    s.caixa(384, 112, 320, base + 16 - 112, 'dg-caixa-2', 12)
    s.texto(32, 136, 'release e alpha: pelo ponto de acesso', 'dg-t dg-b')
    s.texto(400, 136, 'Air: pelo console', 'dg-t dg-b')
    for x, w, cadeia, vao in ((32, 304, esquerda, 16), (400, 288, direita, folga // (len(direita) - 1))):
        y, anterior = topo, None
        for itens in cadeia:
            b = bloco(s, x, y, w, itens)
            if anterior:
                s.seta([anterior.base(), b.topo()], *ponta)
            anterior, y = b, y + b.h + vao
    # they meet at the device on the network, and the rest of the way is the same
    s.linha([(184, base + 16), (184, base + 32), (360, base + 32)])
    s.linha([(544, base + 16), (544, base + 32), (360, base + 32)])
    s.no(360, base + 32)
    rede = bloco(s, 256, base + 48, 208, [('Aparelho na rede', 'dg-t')])
    s.seta([(360, base + 32), rede.topo()], *ponta)
    fim = [[('Pacote de idioma e reinício', 'dg-t')], [('Fuso e hora', 'dg-t')], [('Primeiro sensor', 'dg-t')], [('PIN do painel', 'dg-t'), ('só na release', 'dg-n dg-c2')],
           [('Pronto para usar', 'dg-t dg-b')]]
    centro = rede.y + rede.h + 16 + 16 + 32
    ultima = None
    primeira = None
    for i, itens in enumerate(fim):
        b = bloco(s, 20 + 140 * i, None, 120, itens, 'dg-caixa', 6, centro)
        if primeira is None:
            primeira = b
        if ultima:
            s.seta([ultima.dir(), b.esq()], *ponta)
        ultima = b
    s.linha([rede.base(), (360, rede.y + rede.h + 16), (primeira.topo()[0], rede.y + rede.h + 16)])
    s.seta([(primeira.topo()[0], rede.y + rede.h + 16), primeira.topo()], *ponta)
    return s.render()


def desenho_caminho_historico(num, slug):
    h = num.historico()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    c = 80                                  # the height of the record's way in; the rest is placed from it
    # the record's way in: the sensors, the record at each Intervalo Histórico, the open block in the RAM
    sensores = bloco(s, 16, None, 96, [('Sensores', 'dg-t')], 'dg-caixa', 6, c)
    registro = bloco(s, 136, None, 144, [('registro a cada Intervalo Histórico', 'dg-t')], 'dg-caixa', 6, c)
    aberto = bloco(s, 296, None, 144, [('bloco aberto', 'dg-t dg-b'), (f'na RAM, até {h["registros"]} registros', 'dg-n dg-c2')], 'dg-caixa', 6, c)
    s.seta([sensores.dir(), registro.esq()], *ponta)
    s.seta([registro.dir(), aberto.esq()], *ponta)
    # the copy at each record, dashed: the snapshot that a power cut leaves
    wip = bloco(s, 296, c + 88, 144, [(f'{h["pasta"]}{h["wip"]}', 'dg-t dg-m')])
    s.seta([aberto.base(), wip.topo()], 'dg-fio-tracejado', 'dg-ponta')
    s.texto(aberto.base()[0] - 8, c + 68, 'cópia a cada registro', 'dg-n dg-c2', 'end')
    # the seal: the block goes to the day's file, a stack of blocks that each carry a CRC and the minimum and maximum of every channel
    s.texto(704, c - 56, f'{h["pasta"]}/AAAAMMDD{h["ext"]}', 'dg-n dg-m', 'end')
    s.caixa(568, c - 48, 136, 152, 'dg-caixa-2', 12)
    for k in range(3):
        y = c - 40 + 48 * k
        s.caixa(576, y, 120, 40, 'dg-caixa', 6)
        s.texto(588, y + 24, 'CRC', 'dg-n dg-m')
        s.texto(684, y + 24, 'mín/máx', 'dg-n dg-m', 'end')
    s.seta([aberto.dir(), (568, c)], *ponta)
    for k, linha in enumerate(('selar:', f'{h["registros"]} registros,', 'virada do dia,', 'correção do NTP')):
        s.texto(456, c + 24 + 16 * k, linha, 'dg-n dg-c2')
    # the day files, oldest to today: the oldest goes when the file system passes the limit, today's never does
    fila = c + 184
    s.texto(64, fila - 16, 'arquivos diários, do mais antigo ao de hoje', 'dg-n dg-c2')
    for k in range(5):
        x = 64 + 128 * k
        s.caixa(x, fila, 120, 32, 'dg-caixa-apagada' if k == 0 else 'dg-caixa', 6)
        s.texto(x + 60, fila + 20, f'AAAAMMDD{h["ext"]}', 'dg-n dg-m' + (' dg-c2' if k == 0 else ''), 'middle')
    s.texto(64, fila + 52, f'o mais antigo: apagado acima de {h["limite"]}\u00a0%', 'dg-n dg-c2')
    s.texto(696, fila + 52, 'hoje: nunca é apagado', 'dg-n dg-c2', 'end')
    s.linha([(636, c + 104), (636, fila)])
    s.texto(628, c + 152, 'o arquivo de hoje', 'dg-n dg-c2', 'end')
    return s.render()


def desenho_cursor(num, slug):
    tel = num.telemetria()
    s = Svg(slug)
    ok, perigo = ('dg-fio-ok', 'dg-ponta-ok'), ('dg-fio-perigo', 'dg-ponta-perigo')
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    confirmados, pendentes, na_ram = 8, 8, 4
    x_cursor = 16 + 20 * confirmados + 4                  # between the last confirmed record and the first pending one
    x_pend = x_cursor + 8
    x_ram = x_pend + 20 * pendentes + 4                   # the hour still open in the RAM: a box of its own, at the end of the queue
    x_dir = x_ram + 20 * na_ram + 8                       # the box's right edge
    antes, depois = 64, 176                               # the two rows of squares: before the answer and after a 2xx
    for y, quadrado in ((antes, 'dg-caixa-aviso'), (depois, 'dg-caixa-2')):
        for i in range(confirmados):
            s.caixa(16 + 20 * i, y, 16, 16, 'dg-caixa-2', 0)
        s.caixa(x_ram - 8, y - 8, 20 * na_ram + 16, 32, 'dg-caixa', 6)
        for i in range(pendentes):
            s.caixa(x_pend + 20 * i, y, 16, 16, quadrado, 0)
        for i in range(na_ram):
            s.caixa(x_ram + 20 * i, y, 16, 16, quadrado, 0)
    s.texto(16, 28, 'antes', 'dg-n dg-b')
    s.texto(16, 140, 'depois de um ' + tel['ok'], 'dg-n dg-b')
    s.texto(16, 48, 'confirmados', 'dg-n dg-c2')
    s.texto(x_pend + 28, 48, 'pendentes', 'dg-n dg-c2')
    s.texto(x_ram + 40, 48, 'hora aberta, na RAM', 'dg-n dg-c2', 'middle')
    s.linha([(x_cursor, 56), (x_cursor, 88)], 'dg-fio-acento')
    s.texto(x_cursor, 48, 'cursor', 'dg-t dg-b', 'middle')
    s.linha([(x_dir - 4, 168), (x_dir - 4, 200)], 'dg-fio-acento')
    s.texto(x_dir - 4, 160, 'cursor', 'dg-t dg-b', 'middle')
    # the batch: the first pending records, up to the Lote máximo, with the hour that is still open
    s.linha([(x_pend, 100), (x_pend, 104), (x_dir, 104), (x_dir, 100)], 'dg-fio-fino')
    s.texto((x_pend + x_dir) // 8 * 4, 124, 'lote (até o Lote máximo)', 'dg-t', 'middle')
    coletor = bloco(s, 544, None, 160, [('coletor', 'dg-t dg-b')], 'dg-caixa-2', 6, 120)
    s.seta([(x_dir, 72), (504, 72), (504, 120), coletor.esq()], *ponta)
    # the answer: a 2xx moves the cursor to the end of the batch; anything else leaves it, and the device waits and sends the same stretch again
    s.seta([(coletor.x, 128), (520, 128), (520, 184), (x_dir + 4, 184)], *ok)
    s.texto(512, 160, tel['ok'], 'dg-t dg-ok dg-b', 'end')
    s.seta([coletor.base(), (624, 216)], *perigo)
    s.texto(636, 184, 'falha', 'dg-t dg-perigo dg-b')
    bloco(s, 528, 216, 176, [('espera e reenvia', 'dg-t dg-b'), (f'{tel["espera_min"]}\u00a0s, dobrando até {tel["espera_max"]}\u00a0s', 'dg-n dg-c2'),
                             ('o cursor não se move', 'dg-n dg-c2')], 'dg-caixa', 6)
    s.texto(16, 264, 'Cada quadrado é um registro do histórico, na ordem em que foi gravado.', 'dg-n dg-c2')
    return s.render()


def desenho_linha_alarmes(num, slug):
    a, ok_http = num.alarmes(), num.telemetria()['ok']
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    ok, perigo = ('dg-fio-ok', 'dg-ponta-ok'), ('dg-fio-perigo', 'dg-ponta-perigo')
    segundos = lambda n: f'{n}\u00a0s'
    # the queue in the RAM: the same for both transports, three records waiting
    s.caixa(16, 96, 128, 232, 'dg-caixa-2', 12)
    s.texto(32, 120, 'fila na RAM', 'dg-t dg-b')
    for k in range(3):
        s.caixa(32, 168 + 32 * k, 96, 24, 'dg-caixa', 6)
        s.texto(80, 184 + 32 * k, f'seq {k + 1}', 'dg-n dg-m', 'middle')
    # the HTTP lane: the whole queue in one POST, and the collector's answer
    s.caixa(168, 16, 536, 160, 'dg-caixa-2', 12)
    s.texto(184, 40, 'HTTP', 'dg-t dg-b')
    s.caixa(568, 80, 120, 88, 'dg-caixa', 6)
    s.texto(628, 100, 'coletor', 'dg-t dg-b', 'middle')
    s.seta([(144, 112), (568, 112)], *ponta)
    s.texto(184, 104, 'POST com a fila inteira', 'dg-t')
    s.seta([(568, 136), (144, 136)], *ok)
    s.texto(184, 128, f'{ok_http}: os três registros saem da fila', 'dg-t dg-ok')
    s.seta([(568, 160), (144, 160)], *perigo)
    s.texto(184, 152, f'outro código ou nada em {segundos(a["espera"])}: a fila fica, novo envio em {segundos(a["reenvio"])}', 'dg-t dg-perigo')
    # the MQTT lane: the publish confirms nothing; only the collector's message on the ack topic takes the records out
    s.caixa(168, 192, 536, 208, 'dg-caixa-2', 12)
    s.texto(184, 216, 'MQTT', 'dg-t dg-b')
    broker = bloco(s, 360, 240, 104, [('broker', 'dg-t dg-b')])
    coletor = bloco(s, 568, 240, 120, [('coletor', 'dg-t dg-b'), ('grava e publica a confirmação', 'dg-n dg-c2')])
    s.seta([(144, 256), broker.esq()], *ponta)
    s.texto(184, 248, [('', 'publish em '), ('dg-m', f'<tópico>{a["topico"]}')], 'dg-n')
    s.seta([broker.dir(), (coletor.x, 256)], *ponta)
    s.seta([(coletor.x, 288), (broker.x + 72, 288), (broker.x + 72, broker.y + broker.h)], *ok)
    s.seta([(broker.x + 32, broker.y + broker.h), (broker.x + 32, 312), (144, 312)], *ok)
    s.texto(184, 332, [('', 'seq 1, 2 e 3 em '), ('dg-m', f'<tópico>{a["ack"]}')], 'dg-n')
    s.texto(184, 348, 'o aparelho tira os três da fila', 'dg-n dg-c2')
    s.texto(184, 384, f'Sem a confirmação, a fila inteira é publicada de novo a cada {segundos(a["reenvio"])}.', 'dg-t')
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
