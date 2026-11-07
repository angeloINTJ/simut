#!/usr/bin/env python3
"""Draws and checks the README's architecture diagram: docs/images/architecture*.svg, one file for each language of the READMEs and each theme of the Ângulo
(2026-11-07, issue #58).

Until 2026-11-07 the README showed an ASCII sketch of the firmware, and it had drifted from the code: the issue that asked for this file quoted it with lines that
had moved, a DHT22 on I2C and a Bluetooth console on BLE, and the sketch itself put TouchPriority on Core 1, which only Core 0 reads. So the drawing that replaces
it is not drawn by hand. This file draws it from the sources at the commit, as tools/manual_diagramas.py draws the manual's diagrams, with that file's builder (Svg:
the 4 px grid, a text that may not overlap another nor cross the edge of a box, the same bytes from the same calls) and its readers (Fonte, Numeros). What it reads:

  the managers     every src/*Manager.h, each a class of its name. The role this file gives each one (PAPEIS) is prose: a manager without a role, or a role whose
                   manager is gone, stops the draw.
  the two cores    the loop( ) of src/main.cpp feeds the watchdog and runs app.loop( ); Core 1 is launched on DisplayManager::core1Entry( ), which runs loopCore1( );
                   docs/CONCURRENCY.md still says that Core 0 runs everything but the display and that Core 1 never touches the flash.
  between them     the state crosses as a snapshot under _stateMutex (pullSnapshot( )); the panel's events come back through a lock-free ring of UI_EV_RING slots, which
                   Core 0 drains; for TOUCH_PRIORITY_MS after a touch, Core 0 holds the log's and the history's writes (TouchPriority). What Core 0 checks of each event
                   is not drawn: docs/AUTHORIZATION.md and tools/check_authz.py hold that, with the events left ungated and the four saves registered as deviations.
  the boards       the build profiles (tools/generated/profiles.ini, and platformio.ini's [pico_base]): each image's board, the Wire's ENC28J60 and its slots, the
                   panel each image draws.
  the hardware     the sensor types (the enum of SystemDefs_Records.h) and their buses (the @brief of each driver), the slots (MAX_SENSORS), the panel's size and its
                   two chips, the LCD, the buzzer's pin, the console's Bluetooth (classic SPP, SerialBT) and the images that have it.
  the network      the telemetry's two transports, Home Assistant's discovery, the route of the metrics, the syslog's RFC and protocol (the manual's own readers),
                   and the NTP client.

The colours are the Ângulo's. docs/assets/angulo.css, the copy of the tokens the site and tools/gen_logo.py read, gives the values of the two themes; the classes
(dg-*) and their rules are the manual's own (the CSS block of tools/build_manual.py), so the README's figure and the manual's look alike. An SVG shown by <img>
cannot see the page's CSS, so each file carries the rules it uses in a <style>, with its theme's values written in. The README picks the file with <picture> and
prefers-color-scheme, as it picks the wordmark: an SVG that switched by a media query of its own would follow the operating system, not GitHub's theme (AGENTS.md
section 7). The drawing is 832 px wide because GitHub's README column is 838 px wide in windows of 1280, 1440 and 1920 px (measured on 2026-11-07, Chrome, signed
out), so its 13 px text is shown at 13 px there.

  --write      draws the six files into --images (default docs/images); the same sources give the same bytes.
  --check      draws them again and compares the text with the committed files: one line for each file that differs or is missing, and for each README whose
               architecture section does not show its language's two files through a <picture> with an alt text, or still carries a box-drawing sketch.
               Exit 0 clean, 1 with findings, 2 when it cannot draw (a pattern gone from the sources, a manager without a role, a text that does not fit).
  --preview P  writes P/architecture.<language>.html: each theme's file through an <img>, on the background GitHub gives that theme, at the column's width.
  --images     the folder of the six files (the controls point it at a scratch copy).
  --readmes    the folder of the three READMEs (the controls point it at a scratch copy).
  --fonte      the root of the tree the facts are read from, the repository by default (the controls point it at a scratch copy).

tools/test_readme_diagram.py holds the controls of --check.
"""
import argparse
import functools
import html
import pathlib
import re
import sys

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import manual_diagramas as md     # noqa: E402

LARGURA = 832                        # GitHub's README column is 838 px (2026-11-07): the drawing is shown at its size, its 13 px text at 13 px
SLUG = 'arquitetura'                 # the prefix of the ids (the arrowheads' markers)
IMAGENS = ROOT / 'docs' / 'images'
FOLHA_ANGULO = 'docs/assets/angulo.css'
IDIOMAS = ('en', 'pt-BR', 'es-ES')
TEMAS = ('claro', 'escuro')
README = {'en': 'README.md', 'pt-BR': 'README.pt-BR.md', 'es-ES': 'README.es-ES.md'}
SECAO = {'en': 'Architecture', 'pt-BR': 'Arquitetura', 'es-ES': 'Arquitectura'}
FUNDO_GITHUB = {'claro': '#ffffff', 'escuro': '#0d1117'}   # the preview's backgrounds: GitHub's light and dark page (2026-11-07); never written into a diagram

# The boards PlatformIO names in the profiles: the name a buyer knows and the chip on it (the Raspberry Pi datasheets). A board the table does not know stops the
# draw: the figure names every board an image is built for.
PLACAS = {'rpipicow': ('Pico W', 'RP2040'), 'rpipico2w': ('Pico 2 W', 'RP2350'), 'rpipico': ('Pico', 'RP2040')}
RADIO = 'CYW43439'                   # the radio of the Pico W and of the Pico 2 W (their datasheets); the code knows it by its driver, cyw43
WIRE = ('pico_wire', 'pico_wiretft')  # the SIMUT Wire's two images (CLAUDE.md): a Pico without the radio, its network on an ENC28J60 module

# Each sensor driver's @brief names its bus; the figure says it in the reader's language.
BARRAMENTOS = {'1-Wire': {'en': '1-Wire', 'pt-BR': '1-Wire', 'es-ES': '1-Wire'},
               'single-wire': {'en': 'single wire', 'pt-BR': 'fio único', 'es-ES': 'un solo hilo'},
               'I2C': {'en': 'I2C', 'pt-BR': 'I2C', 'es-ES': 'I2C'}}
DRIVER_DO_TIPO = {'DS18B20': 'DS18B20', 'DHT22': 'DHT22', 'BME280': 'BME280', 'BMP280': 'BME280'}   # the BMP280 is read by the BME280's driver (the BMx280 family)

# What each manager is for, in a few words: the one thing about them the code does not say. Each role summarizes the @brief of the manager's header, and the phrase it
# summarizes is a premise: when the comment changes, the draw stops, and the role is read again against it. AppManager and DisplayManager are drawn apart: the first
# is Core 0's loop, the second is all that Core 1 runs.
PAPEIS = {
    'SensorManager': (r'@brief Sensor orchestration layer — runtime sensor list, periodic reads',
                      {'en': 'reads the slots', 'pt-BR': 'lê os slots', 'es-ES': 'lee los slots'}),
    'StorageManager': (r'@brief LittleFS storage layer with dual-bank CRC32 configuration',
                       {'en': 'configuration, history', 'pt-BR': 'configuração, histórico', 'es-ES': 'configuración, historial'}),
    'LogManager': (r'@brief System logger with flash persistence, ring buffer, and cross-core watchdog',
                   {'en': 'event log; watches Core 1', 'pt-BR': 'log de eventos; vigia o Core 1', 'es-ES': 'registro de eventos; vigila el Core 1'}),
    'NetworkManager': (r'@brief WiFi connectivity, NTP synchronization',
                       {'en': 'Wi-Fi or Ethernet, NTP', 'pt-BR': 'Wi-Fi ou Ethernet, NTP', 'es-ES': 'Wi-Fi o Ethernet, NTP'}),
    'WebManager': (r'@brief Embedded web server with multi-user sessions, RBAC',
                   {'en': 'web UI, REST API', 'pt-BR': 'interface web, API REST', 'es-ES': 'interfaz web, API REST'}),
    'TelemetryManager': (r'@brief Telemetry uploader supporting HTTP POST and MQTT publish',
                         {'en': 'telemetry, alarm line', 'pt-BR': 'telemetria, linha de alarmes', 'es-ES': 'telemetría, línea de alarmas'}),
    'SyslogManager': (r'@brief RFC 5424 / UDP syslog forwarder — the audit trail that leaves the box',
                      {'en': 'remote audit log', 'pt-BR': 'log de auditoria remoto', 'es-ES': 'registro de auditoría remoto'}),
    'CommandManager': (r'@brief CLI command parser and dual-channel console output \(USB \+ Bluetooth\)',
                       {'en': 'the console', 'pt-BR': 'o console', 'es-ES': 'la consola'}),
    'BluetoothManager': (r'@brief Bluetooth Serial CLI interface',
                         {'en': 'the console over Bluetooth', 'pt-BR': 'o console por Bluetooth', 'es-ES': 'la consola por Bluetooth'}),
    'SoundManager': (r'@brief Sound event manager using BuzzerPIO_RP2040 library for PIO-driven audio',
                     {'en': 'the buzzer', 'pt-BR': 'o buzzer', 'es-ES': 'el zumbador'}),
    'MetricsManager': (r'@brief Operational metrics singleton — counters',
                       {'en': 'operational counters', 'pt-BR': 'contadores de operação', 'es-ES': 'contadores de operación'}),
}
ORDEM = ('SensorManager', 'StorageManager', 'LogManager', 'NetworkManager', 'WebManager', 'TelemetryManager', 'SyslogManager', 'CommandManager',
         'BluetoothManager', 'SoundManager', 'MetricsManager')   # the rows of Core 0: the data, then the network, then the operator; a manager not here goes last

# The words of the figure. {chaves} are filled from the sources; `code` marks what is set in the code's font.
TEXTOS = {
    'titulo': {'en': 'SIMUT architecture', 'pt-BR': 'Arquitetura do SIMUT', 'es-ES': 'Arquitectura de SIMUT'},
    'chip_nota': {'en': '{rp2040} or {rp2350}: two cores', 'pt-BR': '{rp2040} ou {rp2350}: dois núcleos', 'es-ES': '{rp2040} o {rp2350}: dos núcleos'},
    'core0_nota': {'en': 'Everything but the display, in one loop: `loop( )` feeds the watchdog and runs `AppManager::loop( )`. The only core that writes the flash; '
                         'for {toque_s} s after a touch it holds the log\'s and the history\'s writes back (`TouchPriority`).',
                   'pt-BR': 'Tudo menos o display, num laço só: o `loop( )` alimenta o watchdog e roda o `AppManager::loop( )`. O único núcleo que escreve no flash; '
                            'por {toque_s} s depois de um toque, ele segura as escritas do log e do histórico (`TouchPriority`).',
                   'es-ES': 'Todo menos la pantalla, en un solo bucle: `loop( )` alimenta el watchdog y ejecuta `AppManager::loop( )`. El único núcleo que escribe en la flash; '
                            'durante {toque_s} s después de un toque, retiene las escrituras del registro y del historial (`TouchPriority`).'},
    'core1_nota': {'en': 'Only `DisplayManager::loopCore1( )`: reads the touch and draws the screen. It never touches the flash: a flash write pauses it.',
                   'pt-BR': 'Só o `DisplayManager::loopCore1( )`: lê o toque e desenha a tela. Nunca toca o flash: uma escrita no flash o pausa.',
                   'es-ES': 'Solo `DisplayManager::loopCore1( )`: lee el toque y dibuja la pantalla. Nunca toca la flash: una escritura en la flash lo pausa.'},
    'estado': {'en': 'the state: a snapshot under a mutex', 'pt-BR': 'o estado: um retrato sob um mutex', 'es-ES': 'el estado: una instantánea bajo un mutex'},
    'eventos': {'en': 'the panel\'s events: a ring of {anel} without a lock, which Core 0 drains', 'pt-BR': 'os eventos do painel: um anel de {anel} sem trava, que o Core 0 esvazia',
                'es-ES': 'los eventos del panel: un anillo de {anel} sin bloqueo, que el Core 0 vacía'},
    'original': {'en': 'the panel, original look', 'pt-BR': 'o painel, visual original', 'es-ES': 'el panel, aspecto original'},
    'angulo': {'en': 'the panel, Ângulo look', 'pt-BR': 'o painel, visual Ângulo', 'es-ES': 'el panel, aspecto Ángulo'},
    'lcd_imagens': {'en': 'the {c}×{l} LCD', 'pt-BR': 'o LCD {c}×{l}', 'es-ES': 'el LCD {c}×{l}'},
    'wire_painel': {'en': 'the LCD or the panel', 'pt-BR': 'o LCD ou o painel', 'es-ES': 'el LCD o el panel'},
    'air': {'en': 'not started', 'pt-BR': 'não é iniciado', 'es-ES': 'no se inicia'},
    'sensores': {'en': 'sensors', 'pt-BR': 'sensores', 'es-ES': 'sensores'},
    'slots': {'en': '{n} slots, `GP0` to `GP{ultimo}`', 'pt-BR': '{n} slots, `GP0` a `GP{ultimo}`', 'es-ES': '{n} slots, `GP0` a `GP{ultimo}`'},
    'slots_wire': {'en': '{n} on the Wire', 'pt-BR': '{n} no Wire', 'es-ES': '{n} en el Wire'},
    'console': {'en': 'console', 'pt-BR': 'console', 'es-ES': 'consola'},
    'bluetooth': {'en': 'Bluetooth Classic: {imagens}', 'pt-BR': 'Bluetooth clássico: {imagens}', 'es-ES': 'Bluetooth clásico: {imagens}'},
    'buzzer': {'en': 'buzzer', 'pt-BR': 'buzzer', 'es-ES': 'zumbador'},
    'buzzer_nota': {'en': '`GP{pino}`, driven by PIO', 'pt-BR': '`GP{pino}`, por PIO', 'es-ES': '`GP{pino}`, por PIO'},
    'buzzer_imagens': {'en': 'all but the Air', 'pt-BR': 'todas menos o Air', 'es-ES': 'todas menos el Air'},
    'painel': {'en': 'panel', 'pt-BR': 'painel', 'es-ES': 'panel'},
    'tft': {'en': '{tft} TFT, {w}×{h}', 'pt-BR': 'TFT {tft}, {w}×{h}', 'es-ES': 'TFT {tft}, {w}×{h}'},
    'toque_chip': {'en': '{toque} touch, SPI0', 'pt-BR': 'toque {toque}, SPI0', 'es-ES': 'táctil {toque}, SPI0'},
    'lcd': {'en': 'LCD {c}×{l}', 'pt-BR': 'LCD {c}×{l}', 'es-ES': 'LCD {c}×{l}'},
    'lcd_nota': {'en': '{chip}, parallel', 'pt-BR': '{chip}, paralelo', 'es-ES': '{chip}, paralelo'},
    'flash': {'en': 'flash', 'pt-BR': 'flash', 'es-ES': 'flash'},
    'flash_nota': {'en': 'the firmware, and LittleFS:', 'pt-BR': 'o firmware e o LittleFS:', 'es-ES': 'el firmware y LittleFS:'},  # the folders follow, on a line of their own
    'flash_log': {'en': 'and the event log', 'pt-BR': 'e o log de eventos', 'es-ES': 'y el registro de eventos'},
    'rede': {'en': 'network', 'pt-BR': 'rede', 'es-ES': 'red'},
    'wifi': {'en': 'Wi-Fi: {radio}', 'pt-BR': 'Wi-Fi: {radio}', 'es-ES': 'Wi-Fi: {radio}'},
    'ethernet': {'en': 'Ethernet: {eth}, on\u00a0the\u00a0Wire', 'pt-BR': 'Ethernet: {eth}, no\u00a0Wire', 'es-ES': 'Ethernet: {eth}, en\u00a0el\u00a0Wire'},
    'navegador': {'en': 'browser', 'pt-BR': 'navegador', 'es-ES': 'navegador'},
    'navegador_nota': {'en': 'web UI, REST\u00a0API', 'pt-BR': 'interface web, API\u00a0REST', 'es-ES': 'interfaz web, API\u00a0REST'},
    'telemetria': {'en': 'telemetry server', 'pt-BR': 'servidor de telemetria', 'es-ES': 'servidor de telemetría'},
    'telemetria_nota': {'en': '{a}(S) or {b}(S)', 'pt-BR': '{a}(S) ou {b}(S)', 'es-ES': '{a}(S) o {b}(S)'},
    'alarmes_nota': {'en': 'and the alarm line', 'pt-BR': 'e a linha de alarmes', 'es-ES': 'y la línea de alarmas'},
    'ha_nota': {'en': 'MQTT Discovery', 'pt-BR': 'MQTT Discovery', 'es-ES': 'MQTT Discovery'},
    'syslog_nota': {'en': '{rfc}, {protocolo}', 'pt-BR': '{rfc}, {protocolo}', 'es-ES': '{rfc}, {protocolo}'},
    'ntp_nota': {'en': 'the clock', 'pt-BR': 'o relógio', 'es-ES': 'el reloj'},
}


def t(chave, idioma, **campos):
    return TEXTOS[chave][idioma].format(**campos)


def runs_de(texto, cls=''):
    """[(class, text)] of a text in which `code` marks what is set in the code's font."""
    partes = texto.split('`')
    if len(partes) % 2 == 0:
        raise ValueError(f'unbalanced backticks in {texto!r}')
    return [(('dg-m' if k % 2 else cls), p) for k, p in enumerate(partes) if p]


_largura = functools.lru_cache(maxsize=None)(md.largura_texto)    # a pure function of its text; the search between the cores asks it the same words hundreds of times


def largura_runs(runs):
    """The width Svg.texto( ) estimates for these runs, run by run as it does."""
    return sum(_largura(s, md.TAM, 'dg-m' in c.split()) for c, s in runs)


def juntar(runs):
    """Adjacent runs of one class merged, so a line is written as few <tspan> as it needs."""
    saida = []
    for c, s in runs:
        if saida and saida[-1][0] == c:
            saida[-1] = (c, saida[-1][1] + s)
        else:
            saida.append((c, s))
    return saida


def palavras_de(runs):
    """The words of `runs`, each a list of (class, text): a plain run breaks at its spaces, a run in the code's font never does (`loop( )` has a space of the house style
    inside it), and what touches with no space between (the parenthesis before `TouchPriority`, the full stop after `loop( )`) is one word."""
    palavras, colar = [], False         # colar: the next piece joins the last word
    for c, s in runs:
        pedacos = [s] if 'dg-m' in c.split() else s.split(' ')
        for k, p in enumerate(pedacos):
            if k > 0:
                colar = False
            if p:
                if colar and palavras:
                    palavras[-1].append((c, p))
                else:
                    palavras.append([(c, p)])
                colar = True
        if 'dg-m' not in c.split() and s.endswith(' '):
            colar = False
    return palavras


def quebrar_runs(runs, largura):
    """`runs` broken into lines that fit in `largura` px by the builder's estimate (palavras_de( ) says what may not be broken). A word wider than the line is an error,
    as in md.quebrar( ): breaking inside it, or letting it out of its box, would hide a layout that does not fit."""
    linhas, atual = [], []
    for palavra in palavras_de(runs):
        if largura_runs(juntar(palavra)) > largura:
            raise ValueError(f'the word {"".join(s for _, s in palavra)!r} is wider than the {largura} px of its line')
        tentativa = atual + ([('', ' ')] if atual else []) + palavra
        if atual and largura_runs(juntar(tentativa)) > largura:
            linhas.append(juntar(atual))
            atual = list(palavra)
        else:
            atual = tentativa
    if atual:
        linhas.append(juntar(atual))
    return linhas


class SvgReadme(md.Svg):
    """The manual's builder at the README's width. The file is shown by an <img>: it carries its size, a <title> and the rules of the classes it uses."""
    largura = LARGURA

    def render(self, titulo='', estilo=''):
        """The file: its size, a <title>, the <style>, and the frame the manual gives a diagram (.moldura-svg: the surface, a 1 px line, the card's corner) under the
        drawing. With its own background the file reads wherever it is opened alone (the raw file, a reader that ignores <picture>), and every text sits on a background
        of the Ângulo's table of combinations, not on the page's."""
        partes = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.largura}" height="{self.altura}" viewBox="0 0 {self.largura} {self.altura}" role="img" class="dg">',
                  f'<title>{html.escape(titulo, quote=False)}</title>', f'<style>{estilo}</style>',
                  f'<rect class="dg-quadro" x=".5" y=".5" width="{self.largura - 1}" height="{self.altura - 1}" rx="12"/>']
        if self.pontas:
            partes.append('<defs>')
            for ponta in sorted(self.pontas):
                partes.append(f'<marker id="{self.slug}-{ponta[3:]}" viewBox="0 0 8 8" refX="8" refY="4" markerWidth="8" markerHeight="8" markerUnits="userSpaceOnUse" '
                              f'orient="auto"><path class="{ponta}" d="M0 0L8 4L0 8z"/></marker>')
            partes.append('</defs>')
        partes += self.corpo
        partes.append('</svg>')
        return '\n'.join(partes) + '\n'


# ---- the facts ----------------------------------------------------------------------------------------------------------------------------------------------
class Fatos:
    """What the figure draws, read from `fonte` (a md.Fonte): every pattern is recorded there, so the controls mangle each one in a scratch copy."""

    def __init__(self, fonte):
        self.f = fonte
        self.num = md.Numeros(fonte)

    def macro(self, env, nome, arquivo=md.CONFIG):
        """`nome` as the build of `env` has it: the header's #define, overridden by a -D of the profile."""
        sobre = self.f.flags(env).get(nome)
        if sobre is not None:
            if not re.fullmatch(r'\d+', sobre):
                raise md.Falta(f'{md.PERFIS}: [env:{env}] sets -D{nome}={sobre}, which is not a number')
            return int(sobre)
        return self.f.define(arquivo, nome)

    def placa(self, env):
        """The board of `env`: its profile's `board =`, or platformio.ini's [pico_base] one, which every profile extends."""
        m = re.search(r'^board\s*=\s*(\w+)[ \t]*$', self.f.secao(env), md.FLAGS_PADRAO)
        if m is None:
            m = self.f.achar('platformio.ini', r'^\[pico_base\][^\[]*?^board\s*=\s*(\w+)[ \t]*$')
        if m.group(1) not in PLACAS:
            raise md.Falta(f'{md.PERFIS}: [env:{env}] is built for the board {m.group(1)}, which the figure does not know (PLACAS in tools/readme_diagram.py)')
        return m.group(1)

    def gestores(self):
        """The managers: every src/*Manager.h, each one declaring the class of its name. A manager without a role in PAPEIS, or a role without its manager, is Falta."""
        nomes = [p[:-2] for p in self.f.listar('src', '*Manager.h')]
        for nome in nomes:
            self.f.achar(f'src/{nome}.h', rf'^class {nome}\b')
        for especial in ('AppManager', 'DisplayManager'):
            if especial not in nomes:
                raise md.Falta(f'src/{especial}.h: gone, and the figure draws Core 0 as its loop and Core 1 as the DisplayManager\'s')
        outros = [n for n in nomes if n not in ('AppManager', 'DisplayManager')]
        sem = [n for n in outros if n not in PAPEIS]
        if sem:
            raise md.Falta(f'src/: {", ".join(sem)} has no role in PAPEIS (tools/readme_diagram.py): say in a few words what it is for, in the three languages')
        orfaos = [n for n in PAPEIS if n not in outros]
        if orfaos:
            raise md.Falta(f'tools/readme_diagram.py: PAPEIS gives a role to {", ".join(orfaos)}, which src/ no longer has')
        for nome in outros:
            self.f.exigir(f'src/{nome}.h', PAPEIS[nome][0], f'the figure\'s role for {nome} ({PAPEIS[nome][1]["en"]!r}, PAPEIS in tools/readme_diagram.py) summarizes that comment: '
                          f'read the two together')
        self.f.exigir('src/net/NetworkManager_Wired.cpp', r"@brief The SIMUT Wire's side of NetworkManager: a wired module",
                      'the figure\'s role for NetworkManager says Wi-Fi or Ethernet')
        return sorted(outros, key=lambda n: (ORDEM.index(n) if n in ORDEM else len(ORDEM), n))

    def nucleos(self):
        """The premises of the two cores, and the ring's size."""
        f = self.f
        f.exigir('src/main.cpp', r'^void loop\( \) \{[\s\S]*?watchdog_update\( \);[\s\S]*?^ app\.loop\( \);',
                 'the figure says that Core 0\'s loop( ) feeds the watchdog and runs AppManager::loop( )')
        f.exigir('docs/CONCURRENCY.md', r'Core 0 runs everything except the display, cooperatively, and owns the\s+watchdog\. Core 1 runs only `DisplayManager::loopCore1\( \)`',
                 'the figure says that Core 0 runs everything but the display and that Core 1 runs only DisplayManager::loopCore1( )')
        f.exigir('docs/CONCURRENCY.md', r'^1\. \*\*Core 1 never touches flash/LittleFS\.\*\*', 'the figure says that Core 1 never touches the flash')
        f.exigir('docs/CONCURRENCY.md', r'`multicore_lockout` victim: LittleFS flash writes stall it', 'the figure says that a flash write pauses Core 1')
        f.exigir('src/DisplayManager.cpp', r'multicore_launch_core1_with_stack\(core1Entry, ', 'the figure says that Core 1 is launched on DisplayManager::core1Entry( )')
        f.exigir('src/DisplayManager.cpp', r'^void DisplayManager::core1Entry\( \) \{ if \(_instance\) _instance->loopCore1\( \); \}',
                 'the figure says that Core 1 runs DisplayManager::loopCore1( )')
        f.exigir('src/DisplayManager.h', r'^\tmutex_t _stateMutex;', 'the figure draws the state crossing to Core 1 under a mutex')
        f.exigir('src/DisplayManager.cpp', r'^bool DisplayManager::pullSnapshot\(SystemState& localSnapshot\) \{[^}]*?mutex_enter_timeout_us\(&_stateMutex, ',
                 'the figure draws the state crossing to Core 1 as a snapshot taken under the mutex')
        f.exigir('src/DisplayManager.h', r'/\* SPSC lock-free UI event ring \(Core 1 produces, Core 0 consumes\)\.', 'the figure draws the panel\'s events as a ring without a lock, from Core 1 to Core 0')
        f.exigir('src/AppManager_Events.cpp', r'while \(_displayMgr->getUiEvent\(uiEv\)\)', 'the figure draws Core 0 draining the panel\'s events')
        toque_ms = int(f.achar('src/AppManager.h', r'static constexpr uint32_t TOUCH_PRIORITY_MS = (\d+);').group(1))
        if toque_ms % 1000:
            raise md.Falta(f'src/AppManager.h: TOUCH_PRIORITY_MS is {toque_ms} ms, and the figure says the window in whole seconds')
        f.exigir('src/AppManager_Core.cpp', r'bool AppManager::isUserInteracting\( \) const \{\s+uint32_t lastTouch = _displayMgr->getLastTouchTimestamp\( \);\s+'
                 r'if \(lastTouch == 0\) return false;\s+return !timeSince\(lastTouch, TOUCH_PRIORITY_MS\);',
                 'the figure says that Core 0 holds its writes for TOUCH_PRIORITY_MS after a touch')
        f.exigir('src/AppManager_Boot.cpp', r'TouchPriority::setProvider\(\[\]\( \) -> bool \{\s+return app\.isUserInteracting\( \);',
                 'the figure says that TouchPriority is the window after a touch (AppManager::isUserInteracting( ))')
        f.exigir('src/LogManager.cpp', r'/\* During touch interaction: buffer in RAM \*/\s+if \(TouchPriority::isActive\( \)\) \{',
                 'the figure says that a touch makes Core 0 hold its flash writes: the event log keeps them in the RAM')
        f.exigir('src/StorageManager.cpp', r'if \(isHeavyTaskLocked\( \) \|\| TouchPriority::isActive\( \)\) return;\s+flushWipV5\( \);',
                 'the figure says that a touch makes Core 0 hold its flash writes: the history waits')
        # what each core is said to do that the comments above do not pin: Core 1 reads the touch, Core 0 watches Core 1 (LogManager's role)
        f.exigir('src/DisplayManager.cpp', r'C1_PHASE\(C1P_TOUCH_READ\);[^}]*?const bool touched = ts->touched\( \);', 'the figure says that Core 1 reads the touch')
        f.exigir(md.LOOP, r'^ LogManager::instance\( \)\.checkCrossCoreHealth\( \);', 'the figure says that LogManager, run by Core 0\'s loop, watches Core 1')
        return {'anel': int(f.achar('src/DisplayManager.h', r'static constexpr uint32_t UI_EV_RING = (\d+);').group(1)), 'toque_s': toque_ms // 1000}

    def ler(self):
        f, num = self.f, self.num
        a = num.arquitetura()             # the manual's block diagram: the sensor types, the slots, the folders, the log, the Bluetooth images, and its premises on Core 1
        integ = num.integracao()          # the syslog's RFC and protocol, the metrics' route, the two transports (a premise), Home Assistant's and the alarm line's premises
        num.homeassistant()
        publicadas = dict(num.imagens())  # {published name: environment}, tools/release_manifest.py
        for nome in ('release', 'alpha', 'air', 'releasetwo'):
            if nome not in publicadas:
                raise md.Falta(f'{md.RELEASE_MANIFEST}: no image is published as {nome}, and the figure says what {nome} draws')
        rel, two, alpha, air = (publicadas[n] for n in ('release', 'releasetwo', 'alpha', 'air'))
        # the boards and the chips
        placas = {env: self.placa(env) for env in (rel, two, alpha, air) + WIRE}
        if placas[rel] != 'rpipicow' or placas[two] != 'rpipico2w' or any(placas[w] != 'rpipico' for w in WIRE):
            raise md.Falta(f'{md.PERFIS}: the release is built for {placas[rel]}, releasetwo for {placas[two]} and the Wire for {", ".join(placas[w] for w in WIRE)}; '
                           f'the figure draws the Pico W, the Pico 2 W and a plain Pico')
        # the network: the radio everywhere but the Wire, the ENC28J60 on the Wire
        for env in (rel, two, alpha, air) + WIRE:
            eth = self.macro(env, 'SIMUT_NET_ENC28J60', 'src/net/NetCaps.h')
            if eth != (1 if env in WIRE else 0):
                raise md.Falta(f'{md.PERFIS}: [env:{env}] has SIMUT_NET_ENC28J60={eth}, and the figure draws the ENC28J60 on the Wire alone')
        f.exigir('src/net/NetCaps.h', r'^#define SIMUT_NET_CYW43 \(!SIMUT_NET_ENC28J60\)', 'the figure draws the radio on every image that has no ENC28J60')
        eth_chip = f.achar('src/net/Enc28j60Device.h', r'@brief .*?\b(ENC28J60)\b').group(1)
        # what Core 1 draws in each image
        if self.macro(rel, 'SIMUT_UI_ANGULO', 'src/ui/UiCaps.h') != 0 or self.macro(two, 'SIMUT_UI_ANGULO', 'src/ui/UiCaps.h') != 1:
            raise md.Falta(f'{md.PERFIS}: the release does not draw the original panel on the Pico W, or releasetwo the Ângulo on the Pico 2 W, which is what the figure says')
        for env, tft, lcd in ((rel, 1, 0), (two, 1, 0), (alpha, 0, 1), (WIRE[0], 0, 1), (WIRE[1], 1, 0)):
            if (self.macro(env, 'SIMUT_DISPLAY_TFT'), self.macro(env, 'SIMUT_DISPLAY_ALPHA')) != (tft, lcd):
                raise md.Falta(f'{md.PERFIS}: [env:{env}] no longer draws on the {"TFT" if tft else "LCD"} alone, which is what the figure says')
        if self.macro(air, 'SIMUT_AIR') != 1:
            raise md.Falta(f'{md.PERFIS}: [env:{air}] is not the Air (SIMUT_AIR), whose Core 1 the figure says is not started')
        f.exigir_flag(alpha, 'HD44780_MODE_PARALLEL', '1', 'the figure draws the LCD in parallel mode')
        f.exigir_flag(WIRE[0], 'HD44780_MODE_PARALLEL', '1', 'the figure draws the LCD in parallel mode')
        tft_w = int(f.achar('src/display/ILI9341_320x240.h', r'^\tstatic constexpr int16_t W = (\d+);').group(1))
        tft_h = int(f.achar('src/display/ILI9341_320x240.h', r'^\tstatic constexpr int16_t H = (\d+);').group(1))
        tft_chip = f.achar('src/display/ILI9341_320x240.h', r'^struct (Ili9341)_\d+x\d+ \{').group(1).upper()
        toque_chip = f.achar('src/display/XPT2046.h', r'^struct (Xpt2046)_Touch \{').group(1).upper()
        lcd = f.achar('src/display/HD44780_16x2.h', r'^struct (Hd44780)_(\d+)x(\d+) \{')
        self.num.spi0()                   # the SPI0 bus the TFT and the touch share (section 2 of simut_config.h)
        f.exigir(md.CONFIG, r'SPI bus: MOSI=GP\d+, MISO=GP\d+, SCK=GP\d+ \(fixed by SPI0 peripheral\)', 'the figure says the panel and the touch are on SPI0')
        # the hardware around Core 0
        barramentos = {}
        for tipo in a['sensores']:
            if tipo not in DRIVER_DO_TIPO:
                raise md.Falta(f'{md.RECORDS}: the sensor type {tipo} has no driver in DRIVER_DO_TIPO (tools/readme_diagram.py), and the figure names the bus of each type')
            driver = DRIVER_DO_TIPO[tipo]
            bus = f.achar(f'src/sensors/{driver}Driver.h', rf'^ \* @brief {driver} (1-Wire|single-wire|I2C) ').group(1)
            barramentos[tipo] = bus
        slots_wire = {self.macro(w, 'MAX_SENSORS') for w in WIRE}
        if len(slots_wire) != 1:
            raise md.Falta(f'{md.PERFIS}: the Wire\'s two images have {sorted(slots_wire)} sensor slots, and the figure gives the Wire one number')
        for env in (rel, two, alpha, air):
            if self.macro(env, 'MAX_SENSORS') != a['slots']:
                raise md.Falta(f'{md.PERFIS}: [env:{env}] has {self.macro(env, "MAX_SENSORS")} sensor slots, and the figure gives every published image {a["slots"]}')
        buzzer = self.macro(rel, 'BUZZER_PIN')
        for env in (rel, two, alpha) + WIRE:
            if self.macro(env, 'SIMUT_SOUND_BUZZER') != 1 or self.macro(env, 'BUZZER_PIN') != buzzer:
                raise md.Falta(f'{md.PERFIS}: [env:{env}] has no buzzer on GP{buzzer}, and the figure draws it on that pin')
        if self.macro(air, 'SIMUT_SOUND_BUZZER') != 0:
            raise md.Falta(f'{md.PERFIS}: [env:{air}] has the buzzer (SIMUT_SOUND_BUZZER), and the figure says every image has it but the Air')
        for env in (rel, two, alpha, air) + WIRE:
            for chave, o_que in (('SIMUT_TEL_MQTT', 'MQTT(S) and MQTT Discovery'), ('SIMUT_TEL_TLS', 'the (S) of HTTP(S) and MQTT(S)'), ('SIMUT_WEB_METRICS', 'GET /metrics'),
                                 ('SIMUT_SYSLOG', 'SyslogManager and the syslog peer')):
                if self.macro(env, chave) != 1:
                    raise md.Falta(f'{md.PERFIS}: [env:{env}] has {chave} off, and the figure draws {o_que} with no image left out')
        f.exigir('src/BluetoothManager.h', r'^#include <SerialBT\.h>', 'the figure says the console\'s Bluetooth is the classic one (SerialBT, SPP), not BLE')
        return {
            'gestores': self.gestores(), 'nucleos': self.nucleos(),
            'sensores': a['sensores'], 'barramentos': barramentos, 'slots': a['slots'], 'slots_wire': slots_wire.pop(),
            'pastas': a['pastas'], 'log': a['log'], 'bluetooth': [{'air': 'Air'}.get(i, i) for i in a['bluetooth']],
            'tft': {'chip': tft_chip, 'w': tft_w, 'h': tft_h, 'toque': toque_chip}, 'lcd': {'chip': lcd.group(1).upper(), 'c': int(lcd.group(2)), 'l': int(lcd.group(3))},
            'buzzer': buzzer, 'eth': eth_chip, 'chips': sorted({PLACAS[p][1] for p in placas.values()}),
            'syslog': (integ['rfc'], integ['protocolo']), 'metricas': (integ['metodo'], integ['rota']),
        }


# ---- the style -------------------------------------------------------------------------------------------------------------------------------------------------
def tokens(fonte):
    """{'claro': {name: value}, 'escuro': {...}} of the colours, and {'texto': stack, 'mono': stack} of the families, read from docs/assets/angulo.css."""
    blocos = {'claro': fonte.achar(FOLHA_ANGULO, r'^:root,\s*\[data-theme="claro"\] \{([^}]*)\}').group(1),
              'escuro': fonte.achar(FOLHA_ANGULO, r'^\[data-theme="escuro"\] \{([^}]*)\}').group(1)}
    cores = {tema: dict(re.findall(r'--([a-z0-9-]+):\s*(#[0-9a-f]{6});', bloco)) for tema, bloco in blocos.items()}
    familias = {nome: fonte.achar(FOLHA_ANGULO, rf'^\s+--font-{nome}:\s+([^;]+);').group(1).strip() for nome in ('texto', 'mono')}
    return cores, familias


def estilo(css, cores, familias, usadas):
    """The rules of the manual's CSS for the diagrams (`css`, tools/build_manual.py) that the drawing uses, with the theme's values written in place of the tokens."""
    regras = []
    for seletores, corpo in re.findall(r'(svg\.dg[^{}]*)\{([^}]*)\}', css):
        manter = []
        for sel in seletores.split(','):
            classes = re.findall(r'\.(dg-[\w-]+)', sel)
            if not classes or classes[-1] in usadas:
                manter.append(sel.strip())
        if not manter:
            continue

        def valor(m):
            nome = m.group(1)
            if nome.startswith('font-'):
                if nome[5:] not in familias:
                    raise md.Falta(f'{FOLHA_ANGULO}: the manual\'s CSS asks for --{nome}, which the tokens do not define')
                return familias[nome[5:]]
            if nome not in cores:
                raise md.Falta(f'{FOLHA_ANGULO}: the manual\'s CSS asks for --{nome}, which the tokens do not define')
            return cores[nome]
        regras.append(','.join(manter) + '{' + re.sub(r'var\(--([\w-]+)\)', valor, corpo) + '}')
    for nome in ('superficie', 'linha'):
        if nome not in cores:
            raise md.Falta(f'{FOLHA_ANGULO}: the frame asks for --{nome}, which the tokens do not define')
    regras.append(f'svg.dg .dg-quadro{{fill:{cores["superficie"]};stroke:{cores["linha"]};stroke-width:1}}')   # the manual's .moldura-svg, which is the page's CSS there
    return ''.join(regras)


# ---- the drawing ------------------------------------------------------------------------------------------------------------------------------------------------
E_X, E_W = 16, 176           # the left column: what is wired to the board
C_X, C_W = 216, 416          # the middle: the board, with the chip's two cores and the flash
D_X, D_W = 656, 160          # the right column: the network
NOME_W = 136                 # the managers' names, in the code's font: TelemetryManager is 129 px by the builder's estimate


def caixa_com_linhas(s, x, y, w, linhas, cls='dg-caixa', raio=6):
    """A box at (x, y), `w` wide, with `linhas` [(runs, class)], each already broken to fit in 12 px of margin each side; the height is 16 + 16 a line. Returns md.Cx."""
    for runs, c in linhas:
        if largura_runs(runs) > w - 24:
            raise ValueError(f'{s.slug}: the line {"".join(t for _, t in runs)!r} does not fit in a box of {w} px')
    h = 16 + 16 * len(linhas)
    s.caixa(x, y, w, h, cls, raio)
    escrever(s, x + 12, y + 20, linhas)
    return md.Cx(x, y, w, h)


def linhas_de(texto, largura, cls):
    """[(runs, class)] of `texto` (with `code` marks) broken to `largura`."""
    return [(r, cls) for r in quebrar_runs(runs_de(texto), largura)]


def escrever(s, x, y, linhas):
    """Each of `linhas` [(runs, class)] at (x, y + 16 k)."""
    for k, (runs, c) in enumerate(linhas):
        s.texto(x, y + 16 * k, runs, c)


def entre_os_nucleos(estado, eventos, x0, x1):
    """(x of the state's arrow, its lines, x of the events' arrow, their lines) for the band between the cores, which runs from x0 to x1: the state goes down on the left,
    the events come up on the right, each with its words to its right. The events' arrow stands at the grid point that gives the fewest lines to the longer label (then
    the most even pair, then the leftmost point), so the band is as short as the words allow."""
    xa = x0 + 12
    melhor = None
    for xb in range(xa + 12 + 96, x1 - 12 - 96 + 1, md.GRADE):
        try:
            a = quebrar_runs(estado, xb - 16 - (xa + 12))
            b = quebrar_runs(eventos, x1 - 12 - (xb + 12))
        except ValueError:
            continue
        nota = (max(len(a), len(b)), abs(len(a) - len(b)), xb)
        if melhor is None or nota < melhor[0]:
            melhor = (nota, xb, a, b)
    if melhor is None:
        raise ValueError('the words between the cores fit nowhere in the band')
    _, xb, a, b = melhor
    return xa, a, xb, b


def desenhar(fatos, idioma, tema_css):
    """The SVG of the figure in `idioma`, its <style> being `tema_css(the classes it uses)`."""
    s = SvgReadme(SLUG)
    T = lambda chave, **kw: t(chave, idioma, **kw)
    c0_x, c0_w = C_X + 16, C_W - 32           # the two cores and the flash, inside the board
    lado = c0_x + c0_w                        # their right edge; the flash's wire runs in the board's margin beyond it

    # ---- the measures, before anything is drawn: a box is drawn before the texts inside it ----
    nota0 = linhas_de(T('core0_nota', toque_s=fatos['nucleos']['toque_s']), c0_w - 24, 'dg-n dg-c2')
    gestores = fatos['gestores']
    y0 = 64
    y_lista = y0 + 36 + 16 * len(nota0) + 8
    y0b = y_lista + 16 * (len(gestores) - 1) + 12
    xa, estado, xb, eventos = entre_os_nucleos(runs_de(T('estado')), runs_de(T('eventos', anel=fatos['nucleos']['anel'])), c0_x, lado)
    y1 = y0b + 16 + 16 * max(len(estado), len(eventos)) + 8
    nota1 = linhas_de(T('core1_nota'), c0_w - 24, 'dg-n dg-c2')
    lcd = fatos['lcd']
    imagens1 = []
    for nome, chave in (('release', T('original')), ('releasetwo', T('angulo')), ('alpha', T('lcd_imagens', c=lcd['c'], l=lcd['l'])),
                        ('Wire', T('wire_painel')), ('Air', T('air'))):
        imagens1 += [(r, 'dg-n') for r in quebrar_runs([('dg-b', nome), ('', ': ' + chave)], c0_w - 24)]
    y_im = y1 + 36 + 16 * len(nota1) + 8
    y1b = y_im + 16 * (len(imagens1) - 1) + 12
    linhas_f = ([([('dg-b', T('flash'))], 'dg-t')] + linhas_de(T('flash_nota'), c0_w - 24, 'dg-n')
                + [([('dg-m', ' '.join(fatos['pastas']))], 'dg-n')] + linhas_de(f'{T("flash_log")} `{fatos["log"]}`', c0_w - 24, 'dg-n'))
    y_f = y1b + 16

    # ---- the board, the chip's two cores, and the flash ----
    altura_placa = y_f + 16 + 16 * len(linhas_f) + 16 - 16
    s.caixa(C_X, 16, C_W, altura_placa, 'dg-caixa-2', 12)
    s.caixa(c0_x, y0, c0_w, y0b - y0, 'dg-caixa', 6)
    s.caixa(c0_x, y1, c0_w, y1b - y1, 'dg-caixa', 6)
    chips = fatos['chips']
    s.texto(C_X + 16, 36, 'Pico W · Pico 2 W · Pico (Wire)', 'dg-t dg-b')
    s.texto(C_X + 16, 52, T('chip_nota', rp2040=chips[0], rp2350=chips[1]), 'dg-n dg-c2')
    s.texto(c0_x + 12, y0 + 20, 'Core 0', 'dg-t dg-b')
    escrever(s, c0_x + 12, y0 + 36, nota0)
    for k, nome in enumerate(gestores):
        s.texto(c0_x + 12, y_lista + 16 * k, nome, 'dg-n dg-m')
        s.texto(c0_x + 12 + NOME_W, y_lista + 16 * k, PAPEIS[nome][1][idioma], 'dg-n dg-c2')
    s.seta([(xa, y0b), (xa, y1)])
    escrever(s, xa + 12, y0b + 20, [(r, 'dg-n dg-c2') for r in estado])
    s.seta([(xb, y1), (xb, y0b)])
    escrever(s, xb + 12, y0b + 20, [(r, 'dg-n dg-c2') for r in eventos])
    s.texto(c0_x + 12, y1 + 20, 'Core 1', 'dg-t dg-b')
    escrever(s, c0_x + 12, y1 + 36, nota1)
    escrever(s, c0_x + 12, y_im, imagens1)
    flash = caixa_com_linhas(s, c0_x, y_f, c0_w, linhas_f)
    # the flash is wired to Core 0 alone: out of its right edge, down the board's margin, into the flash's
    x_fio, y_fio = lado + 8, y0b - 20
    s.linha([(lado, y_fio), (x_fio, y_fio), (x_fio, flash.y + flash.h // 2), (lado, flash.y + flash.h // 2)])

    # ---- the left column: what is wired to the board ----
    def bloco_e(y, titulo, notas):
        linhas = [([('dg-b', titulo)], 'dg-t')]
        for nota in notas:
            linhas += linhas_de(nota, E_W - 24, 'dg-n dg-c2')
        return caixa_com_linhas(s, E_X, y, E_W, linhas)

    familias = {}                        # the types one driver reads, listed together (the BME280 and the BMP280)
    for tipo in fatos['sensores']:
        familias.setdefault(DRIVER_DO_TIPO[tipo], []).append(tipo)
    sens = bloco_e(y0, T('sensores'), [T('slots', n=fatos['slots'], ultimo=fatos['slots'] - 1), T('slots_wire', n=fatos['slots_wire'])]
                   + [f'{", ".join(tipos)} · {BARRAMENTOS[fatos["barramentos"][tipos[0]]][idioma]}' for tipos in familias.values()])
    cons = bloco_e(sens.y + sens.h + 8, T('console'), ['USB', T('bluetooth', imagens=', '.join(fatos['bluetooth']))])
    buz = bloco_e(cons.y + cons.h + 8, T('buzzer'), [T('buzzer_nota', pino=fatos['buzzer']), T('buzzer_imagens')])
    for cx, sentido in ((sens, 1), (cons, 0), (buz, -1)):
        y = cx.y + cx.h // 2
        if y > y0b - 12:
            raise ValueError(f'{s.slug}: a box on the left ({cx}) meets Core 0 below its bottom ({y0b})')
        if sentido > 0:
            s.seta([(E_X + E_W, y), (c0_x, y)])
        elif sentido < 0:
            s.seta([(c0_x, y), (E_X + E_W, y)])
        else:
            s.linha([(E_X + E_W, y), (c0_x, y)])
    tft = fatos['tft']
    pain = bloco_e(y1, T('painel'), [T('tft', tft=tft['chip'], w=tft['w'], h=tft['h']), T('toque_chip', toque=tft['toque'])])
    lcdb = bloco_e(pain.y + pain.h + 8, T('lcd', c=lcd['c'], l=lcd['l']), [T('lcd_nota', chip=lcd['chip'])])
    for cx, sentido in ((pain, 0), (lcdb, -1)):
        y = cx.y + cx.h // 2
        if y > y1b - 12:
            raise ValueError(f'{s.slug}: a box on the left ({cx}) meets Core 1 below its bottom ({y1b})')
        if sentido < 0:
            s.seta([(c0_x, y), (E_X + E_W, y)])
        else:
            s.linha([(E_X + E_W, y), (c0_x, y)])

    # ---- the right column: the network ----
    s.texto(D_X, 36, T('rede'), 'dg-t dg-b')
    cab = linhas_de(T('wifi', radio=RADIO), D_W, 'dg-n dg-c2') + linhas_de(T('ethernet', eth=fatos['eth']), D_W, 'dg-n dg-c2')
    escrever(s, D_X, 52, cab)
    pares = [(T('navegador'), [T('navegador_nota')]),
             (T('telemetria'), [T('telemetria_nota', a='HTTP', b='MQTT'), T('alarmes_nota')]),
             ('Home Assistant', [T('ha_nota')]),
             ('Prometheus', ['`' + ' '.join(fatos['metricas']) + '`']),
             ('syslog', [T('syslog_nota', rfc=fatos['syslog'][0], protocolo=fatos['syslog'][1])]),
             ('NTP', [T('ntp_nota')])]
    y, centros = 52 + 16 * len(cab), []
    for titulo, notas in pares:
        linhas = linhas_de(titulo, D_W - 24, 'dg-t')
        for nota in notas:
            linhas += linhas_de(nota, D_W - 24, 'dg-n dg-c2')
        cx = caixa_com_linhas(s, D_X, y, D_W, linhas)
        centros.append(cx.y + cx.h // 2)
        y = cx.y + cx.h + 8
    x_bus, y_rede = D_X - 12, y0 + 16
    s.linha([(lado, y_rede), (x_bus, y_rede)])
    s.linha([(x_bus, y_rede), (x_bus, centros[-1])])
    for c in centros:
        s.linha([(x_bus, c), (D_X, c)])

    usadas = set(s.pontas)
    for m in re.finditer(r'class="([^"]+)"', '\n'.join(s.corpo)):
        usadas.update(m.group(1).split())
    return s.render(T('titulo'), tema_css(usadas))


def arquivo(idioma, tema):
    """docs/images/<this>: architecture.svg and architecture-dark.svg in English, with .pt-BR or .es-ES before .svg in the other two."""
    return f'architecture{"-dark" if tema == "escuro" else ""}{"" if idioma == "en" else "." + idioma}.svg'


def desenhar_todas(fonte):
    """{file: the SVG text} of the six files, drawn from the sources in `fonte`. Raises md.Falta (or ValueError, for a layout that does not fit) when it cannot."""
    import build_manual as bm         # its CSS: the rules of the dg-* classes, the manual's look
    fatos = Fatos(fonte).ler()
    cores, familias = tokens(fonte)
    return {arquivo(i, tema): desenhar(fatos, i, lambda usadas, tema=tema: estilo(bm.CSS, cores[tema], familias, usadas)) for i in IDIOMAS for tema in TEMAS}


# ---- the READMEs ------------------------------------------------------------------------------------------------------------------------------------------------
def secao(texto, titulo):
    """The text of the README's section `## titulo`, up to the next `## `, or None."""
    m = re.search(rf'^## {re.escape(titulo)}[ \t]*\n(.*?)(?=^## |\Z)', texto, re.M | re.S)
    return m.group(1) if m else None


def conferir_readmes(pasta):
    """One line for each README whose architecture section does not show its language's two files through a <picture> with an alt text and the drawing's width, or still has a
    box-drawing sketch."""
    achados = []
    for idioma in IDIOMAS:
        nome = README[idioma]
        try:
            texto = (pathlib.Path(pasta) / nome).read_text(encoding='utf-8')
        except OSError as e:
            achados.append(f'{nome}: cannot be read ({e})')
            continue
        corpo = secao(texto, SECAO[idioma])
        if corpo is None:
            achados.append(f'{nome}: no section "## {SECAO[idioma]}"')
            continue
        claro, escuro = (f'docs/images/{arquivo(idioma, tema)}' for tema in TEMAS)
        figura = re.search(r'<picture>\s*<source media="\(prefers-color-scheme: dark\)" srcset="([^"]+)">\s*'
                           r'<source media="\(prefers-color-scheme: light\)" srcset="([^"]+)">\s*<img src="([^"]+)" alt="([^"]*)"([^>]*)>\s*</picture>', corpo)
        if figura is None:
            achados.append(f'{nome}: the section "## {SECAO[idioma]}" has no <picture> with a dark and a light <source> and an <img>')
        else:
            if figura.groups()[:3] != (escuro, claro, claro):
                achados.append(f'{nome}: the <picture> shows {", ".join(figura.groups()[:3])}, not {escuro}, {claro}, {claro}')
            if not figura.group(4).strip():
                achados.append(f'{nome}: the <img> of the architecture has no alt text')
            if not re.search(rf'\swidth="{LARGURA}"', figura.group(5)):
                achados.append(f'{nome}: the <img> of the architecture is not width="{LARGURA}", the width the figure is drawn at')
        caixa = re.search(r'[─-╿]', corpo)
        if caixa:
            linha = texto[:texto.index(corpo)].count('\n') + corpo[:caixa.start()].count('\n') + 1
            achados.append(f'{nome}:{linha}: a box-drawing character ({caixa.group(0)!r}) in the section "## {SECAO[idioma]}": the sketch the figure replaced')
    return achados


def conferir_imagens(imagens, desenhados):
    """One line for each of the six files that is missing or differs from what the code draws."""
    achados = []
    for nome, svg in desenhados.items():
        caminho = pathlib.Path(imagens) / nome
        if not caminho.is_file():
            achados.append(f'docs/images/{nome}: missing (python3 tools/readme_diagram.py --write draws it)')
            continue
        try:
            texto = caminho.read_bytes().decode('utf-8')
        except UnicodeDecodeError as e:
            achados.append(f'docs/images/{nome}: not UTF-8 text ({e})')
            continue
        if texto != svg:
            linha, coluna = md.primeira_diferenca(texto, svg)
            achados.append(f'docs/images/{nome}: differs from what the code draws, first at line {linha} column {coluna} (python3 tools/readme_diagram.py --write draws it again)')
    return achados


def gravar(imagens, desenhados):
    pasta = pathlib.Path(imagens)
    pasta.mkdir(parents=True, exist_ok=True)
    for nome, svg in desenhados.items():
        with open(pasta / nome, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(svg)


def previa(pasta, imagens):
    """P/architecture.<language>.html: each theme's file through an <img>, on the background GitHub gives that theme, in a column of GitHub's width."""
    pasta = pathlib.Path(pasta)
    pasta.mkdir(parents=True, exist_ok=True)
    for idioma in IDIOMAS:
        blocos = ''.join(f'<div style="background:{FUNDO_GITHUB[tema]};padding:16px 0"><div style="width:838px;margin:0 auto">'
                         f'<img src="{(pathlib.Path(imagens) / arquivo(idioma, tema)).resolve().as_uri()}" alt="" style="max-width:100%"></div></div>' for tema in TEMAS)
        (pasta / f'architecture.{idioma}.html').write_text(f'<!doctype html><html><head><meta charset="utf-8"><title>{idioma}</title></head>'
                                                           f'<body style="margin:0">{blocos}</body></html>\n', encoding='utf-8')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    modo = ap.add_mutually_exclusive_group(required=True)
    modo.add_argument('--write', action='store_true', help='draw the six files and write them')
    modo.add_argument('--check', action='store_true', help='draw them again, compare the text with the committed files, and read the READMEs')
    modo.add_argument('--preview', metavar='PASTA', help='write architecture.<language>.html into PASTA')
    ap.add_argument('--images', default=str(IMAGENS), help='the folder of the six files (default docs/images)')
    ap.add_argument('--readmes', default=str(ROOT), help='the folder of the three READMEs (default: the repository)')
    ap.add_argument('--fonte', default=str(ROOT), help='the root of the tree the facts are read from (default: the repository)')
    a = ap.parse_args(argv)
    if a.preview:
        previa(a.preview, a.images)
        print(f'readme_diagram: wrote {len(IDIOMAS)} pages to {a.preview}')
        return 0
    try:
        desenhados = desenhar_todas(md.Fonte(a.fonte))
    except (md.Falta, ValueError) as e:
        print(f'readme_diagram: {e}', file=sys.stderr)
        return 2
    if a.write:
        gravar(a.images, desenhados)
        print(f'readme_diagram: wrote {len(desenhados)} files to {a.images}')
        return 0
    achados = conferir_imagens(a.images, desenhados) + conferir_readmes(a.readmes)
    for linha in achados:
        print(linha)
    print(f'readme_diagram: {len(desenhados)} files drawn again from the sources and compared as text, the READMEs read, {len(achados)} finding(s)')
    return 1 if achados else 0


if __name__ == '__main__':
    sys.exit(main())
