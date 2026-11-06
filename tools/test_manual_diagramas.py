#!/usr/bin/env python3
"""test_manual_diagramas.py -- holds tools/manual_diagramas.py --check, and the SVG in line of tools/build_manual.py, to their word (2026-11-06).

--check is only worth running if it can fail. Each control below puts ONE fault into a scratch copy of the SVG files, of the chapters or of the source files the
diagrams read their numbers from (never into the tree) and requires exactly the findings that fault is about; the first one requires none, on the files as committed.

  clean            the committed SVG files and chapters: no finding, the command line exits 0; every SVG is well-formed XML, every class it uses has a rule in the
                   manual's CSS, no id lacks the slug, no literal colour, every coordinate on the 4 px grid; the datasheet's pin table (PINAGEM) says what chapter 2's table says
  one character    one character of one SVG changed: exactly 1 finding, naming the file; the file put back: 0 (two files)
  deleted, extra   an SVG deleted: exactly 1; an SVG no row names: exactly 1; a PNG in the folder: 0 (the diagrams not drawn yet keep their .png)
  chapters         a captura= edited, a block removed, a block with another tipo, a block with another arquivo: exactly 1 each; a block of tipo="diagrama" with an
                   .svg no row names: exactly 1; one with a .png no row names: 0
  colour, id       a literal colour injected into an SVG on disk: 2 findings (the file no longer matches what the code draws, and the colour); an id without the slug:
                   2 the same way; the code made to draw them and the files written from it, so that the file matches: exactly 1 each, which is the rule alone
  the source       in a scratch copy of the files the diagrams read, a number changed: exactly the diagrams that draw it change, and no other (the table SOURCE_CASES
                   below says which); an override in the profile follows; two functions on one pin, a pin inside the sensor slots, a premise the figures depend on
                   (the alpha's parallel mode, the charger's polarity) gone: exit 2 naming the file
  pattern gone     every pattern the tool looked for, mangled in a scratch copy, exits 2 with the file and the pattern in the message (one control per pattern)
  determinism      two draws, and two --write into two folders, give the same bytes
  build_manual     the SVG goes into the page in line: inside <div class="moldura moldura-svg">, with aria-label, without the XML prologue; two SVG with one id (or
                   one id that a heading has) make its --check complain, two with different ids do not (run with pandoc through the whole build when pandoc is there);
                   the CSS shows the drawing at its width and no smaller, gives every text the one size the tool draws (TAM), and leaves the frame of a PNG diagram as it
                   was (each rule of the three held by a CSS changed in a copy)
  command line     0 clean, 1 with a finding, 2 when it cannot draw (an empty source folder); --list prints one row per diagram; --write writes the files and --check
                   passes on them; --preview writes <slug>.claro.html and <slug>.escuro.html with the manual's CSS and the figure's frame

  python3 tools/test_manual_diagramas.py     exit 0 when every control holds, 1 when one does not, 2 when it cannot draw
"""
import contextlib
import io
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import build_manual as bm         # noqa: E402
import manual_diagramas as m      # noqa: E402

CONTA = [0]
FALHAS = []
PULADOS = []

CONFIG = 'src/simut_config.h'
PERFIS = 'tools/generated/profiles.ini'
BME_DRIVER = 'src/sensors/BME280Driver.h'
BME_SENSOR = 'src/sensors/BME280SensorDriver.h'
AIR_CONFIG = 'src/air/AirConfig.h'
MENU = 'src/display/SettingsMenu.h'
RELEASE_MANIFEST = 'tools/release_manifest.py'
WORKFLOW_OTA = '.github/workflows/release-ota.yml'
TELEMETRY = 'src/TelemetryManager.cpp'
HA_DISCOVERY = 'src/HaDiscovery.h'
WEB_CORE = 'src/WebManager_Core.cpp'
WEB_METRICS = 'src/WebManager_Metrics.cpp'
WEB_AUTH = 'src/WebManager_Auth.cpp'
WEB_H = 'src/WebManager.h'
NET_DEFS = 'src/SystemDefs_Network.h'
SYSLOG_H = 'src/SyslogManager.h'
SYSLOG_CPP = 'src/SyslogManager.cpp'
LOOP = 'src/AppManager_Loop.cpp'
NETWORK = 'src/NetworkManager.cpp'
RECORDS = 'src/SystemDefs_Records.h'
WEB_OTA = 'src/WebManager_Ota.cpp'
WEB_API = 'src/WebManager_Api.cpp'
STORAGE_H = 'src/StorageManager.h'
OTA_LAYOUT = 'src/ota/ota_layout.h'
LOG_H = 'src/LogManager.h'
DISPLAY_H = 'src/DisplayManager.h'
NET_CYW43 = 'src/net/NetworkManager_Cyw43.cpp'
NET_MGR_H = 'src/NetworkManager.h'
NET_MGR = 'src/NetworkManager.cpp'
APP_CORE = 'src/AppManager_Core.cpp'
LOGCODES = 'tools/logcodes.tsv'
LOGPOLICY_H = 'src/LogPolicy.h'
LOGPOLICY_CPP = 'src/LogPolicy.cpp'
WEBUI = 'WebUI.h'
OTA_STAGING = 'src/ota/staging.cpp'
OTA_APPLIER = 'src/ota/applier.cpp'
AIR_CPP = 'src/AppManager_Air.cpp'
APP_H = 'src/AppManager.h'
BOOT = 'src/AppManager_Boot.cpp'
PARSER = 'src/CommandParser.cpp'
APP_CMDS = 'src/AppManager_Commands.cpp'


def confere(cond, msg, ok=None):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    elif ok:
        print('ok  ', ok)
    return bool(cond)


def css_do_svg(css):
    """The faults of the manual's CSS for the diagrams in line: the SVG at its drawing's width and never narrower (width and min-width of LARGURA, no max-width), one size of text
    (TAM) in every rule of svg.dg, and no rule for .moldura-diagrama, which is also the frame of a PNG diagram (the frame of the SVG is .moldura-svg)."""
    problemas = []
    regra = re.search(r'\.moldura-svg svg\{([^}]*)\}', css)
    if not regra:
        problemas.append('no rule for .moldura-svg svg')
    else:
        for prop in (f'width:{m.LARGURA}px', f'min-width:{m.LARGURA}px'):
            if not re.search(rf'(?:^|;){re.escape(prop)}(?:;|$)', regra.group(1)):
                problemas.append(f'.moldura-svg svg lacks {prop}')
        if 'max-width' in regra.group(1):
            problemas.append('.moldura-svg svg has a max-width, which would shrink it')
    tamanhos = set(re.findall(r'svg\.dg [^{}]*\{[^}]*?font-size:(\d+)px', css))
    if tamanhos != {str(m.TAM)}:
        problemas.append(f"the sizes of the diagrams' text are {sorted(tamanhos)}, not {m.TAM}")
    if '.moldura-diagrama' in css:
        problemas.append('a rule for .moldura-diagrama, which is also the frame of a PNG diagram')
    return problemas


def cli(*args, ambiente=None):
    return subprocess.run([sys.executable, '-B', str(HERE / 'manual_diagramas.py'), *args], capture_output=True, text=True, env=ambiente or os.environ.copy())


def em_processo(*args):
    """(exit code, stdout, stderr) of m.main( ) run in this process: the loop over every pattern would spawn a hundred interpreters otherwise."""
    saida, erro = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(saida), contextlib.redirect_stderr(erro):
        rc = m.main(list(args))
    return rc, saida.getvalue(), erro.getvalue()


def trocar(caminho, antigo, novo, vezes=1):
    t = caminho.read_text(encoding='utf-8')
    assert vezes is None or t.count(antigo) == vezes, f'{caminho.name}: {antigo!r} stands {t.count(antigo)} times, not {vezes}'
    assert antigo in t, f'{caminho.name}: {antigo!r} is not there'
    caminho.write_text(t.replace(antigo, novo), encoding='utf-8')


def copiar_fonte(fonte, destino):
    """The files the tool read (Fonte.lidos), in a scratch tree with the same relative paths."""
    for rel in fonte.lidos:
        alvo = destino / rel
        alvo.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fonte.raiz / rel, alvo)
    for rel, glob in fonte.listados:
        (destino / rel).mkdir(parents=True, exist_ok=True)
        for p in (fonte.raiz / rel).glob(glob):
            shutil.copyfile(p, destino / rel / p.name)


def desenhar_de(raiz):
    """({file: svg}, None) drawn from the source tree at `raiz`, or (None, the exception) when the tool cannot draw."""
    try:
        return m.desenhar_todas(m.Fonte(raiz)), None
    except (m.Falta, ValueError) as e:
        return None, e


def mudados(a, b):
    return sorted(k for k in a if a[k] != b.get(k))


def achados(imagens, capitulos, desenhadas):
    return m.conferir_imagens(imagens, desenhadas) + m.conferir_capitulos(capitulos)


def tabela_do_capitulo(capitulo):
    """{gpio number: physical pin} and the power and ground pins as chapter 2's table of the map and of the power points state them, read from the prose."""
    texto = capitulo.read_text(encoding='utf-8')
    pinos = {}
    for linha in texto.split('\n'):
        c = [x.strip() for x in linha.strip().strip('|').split('|')]
        if len(c) >= 2 and re.fullmatch(r'GP\d+', c[0]) and re.fullmatch(r'\d+', c[1]):
            pinos[c[0]] = int(c[1])
        elif len(c) >= 2 and c[0] == 'GP0 a GP15':
            ordem = []
            for pedaco in c[1].split(','):
                pedaco = pedaco.strip()
                if '–' in pedaco:
                    a, b = pedaco.split('–')
                    ordem += list(range(int(a), int(b) + 1))
                else:
                    ordem.append(int(pedaco))
            pinos.update({f'GP{i}': p for i, p in enumerate(ordem)})
    gnd = re.search(r'\| GND \| ([0-9, ]+) \|', texto)
    return pinos, [int(x) for x in gnd.group(1).split(',')] if gnd else []


# What the log policy's mirror (tools/manual_diagramas.py, EspelhoLogPolicy) is held to: the real LogPolicy.cpp, compiled on the host the way `pio test -e native_logpolicy` compiles it, run on the same
# events. The harness reads "E <ms> <0|1>" (a telemetry event, 1 for a failure) and "R <ms>" (the hourly accounting) and writes what the policy answers, one line each.
HARNES_POLITICA = """#include "LogPolicy.h"
#include "SystemDefs_Logging.h"
#include <stdio.h>
int main() {
    LogPolicy p;
    char k; unsigned long t; int falha;
    while (scanf(" %c %lu", &k, &t) == 2) {
        if (k == 'E') {
            if (scanf("%d", &falha) != 1) return 1;
            printf("%d\\n", (int)p.shouldPersist(falha ? SYS_TEL_FAIL : SYS_TEL_SENT, falha ? 3 : 1, (uint32_t)t));
        } else {
            printf("%u\\n", (unsigned)p.takeSuppressedReport((uint32_t)t));
        }
    }
    return 0;
}
"""


def compilar_politica(politica_cpp, destino):
    """The harness built with `politica_cpp` (a LogPolicy.cpp) and the repository's headers and host stubs, or the compiler's complaint."""
    gxx = shutil.which('g++')
    if gxx is None:
        return None, 'no g++'
    (destino / 'h.cpp').write_text(HARNES_POLITICA, encoding='utf-8')
    r = subprocess.run([gxx, '-std=gnu++17', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'test' / 'native_stubs'), '-DMAX_SENSORS=16', str(destino / 'h.cpp'), str(politica_cpp), '-o', str(destino / 'h')],
                       capture_output=True, text=True)
    return (destino / 'h', None) if r.returncode == 0 else (None, r.stderr.strip()[-300:])


def rodar_politica(binario, eventos):
    """[(kind, ms, failure)] through the compiled policy: the list of its answers, as ints."""
    entrada = ''.join(f'E {ms} {int(falha)}\n' if k == 'E' else f'R {ms}\n' for k, ms, falha in eventos)
    r = subprocess.run([str(binario)], input=entrada, capture_output=True, text=True)
    return [int(x) for x in r.stdout.split()]


def rodar_espelho(espelho, eventos):
    return [int(espelho.persiste(falha, ms)) if k == 'E' else espelho.relata(ms) for k, ms, falha in eventos]


def main():
    try:
        fonte = m.Fonte(m.ROOT)
        desenhadas = m.desenhar_todas(fonte)
    except (m.Falta, ValueError) as e:
        print(f'test_manual_diagramas: {e}', file=sys.stderr)
        return 2
    nomes = [f.arquivo for f in m.FIGURAS]

    def precisa(*arquivos):
        """True when every diagram a control is about has its row. A control about a diagram the table no longer draws is a FAIL, not a skip: a skipped control would
        let the removal of a row pass for a clean tree (the controls were skipped, and said so, only while the table grew a few diagrams at a time, 2026-11-06)."""
        faltam = [a for a in arquivos if a not in nomes]
        confere(not faltam, f'a control is about {", ".join(faltam)}, which has no row of FIGURAS')
        return not faltam

    with tempfile.TemporaryDirectory(prefix='manual_diagramas_ctl_') as tmp:
        tmp = pathlib.Path(tmp)
        img, cap = tmp / 'img', tmp / 'cap'
        shutil.copytree(m.IMAGENS, img, ignore=lambda d, ns: [n for n in ns if not n.endswith('.svg')])
        shutil.copytree(m.CAPITULOS, cap)

        def agora():
            return achados(img, cap, desenhadas)

        # ---- clean: the committed state --------------------------------------------------------------------------------------------------------------------
        confere(m.tabela_ok() is None, f'the table itself: {m.tabela_ok()}', 'the table of FIGURAS holds: no file or block twice, no captura= that breaks the attribute')
        confere(all(re.fullmatch(r'\d\d-[a-z0-9]+(-[a-z0-9]+)*\.svg', a) for a in nomes) and len(set(nomes)) == len(nomes), f'the files FIGURAS names: {nomes}',
                'every file is NN-slug.svg, once')
        a = agora()
        confere(not a, f'the committed SVG files and chapters give {len(a)} finding(s): {a[:3]}', f'clean: {len(m.FIGURAS)} diagrams drawn again, 0 findings')
        confere(set(desenhadas) == set(nomes) and all((m.IMAGENS / n).is_file() for n in nomes), 'a diagram has no committed file', 'every row has its committed file')
        r = cli('--check')
        confere(r.returncode == 0 and r.stdout.rstrip().endswith('0 finding(s)'), f'the command line on the committed tree exited {r.returncode}: {r.stdout.strip()[-200:]} {r.stderr.strip()[-200:]}',
                'command line: exit 0 on the committed tree')

        # every SVG, as drawn: well-formed, one root, only the manual's classes, on the grid
        regras = set(re.findall(r'\.(dg[\w-]*)', bm.CSS))
        for f in m.FIGURAS:
            svg = desenhadas[f.arquivo]
            slug = m.slug_de(f.arquivo)
            try:
                raiz = ET.fromstring(svg)
            except ET.ParseError as e:
                confere(False, f'{f.arquivo} is not well-formed XML: {e}')
                continue
            altura = raiz.get('viewBox', '').split()[-1:]
            confere(raiz.tag == '{http://www.w3.org/2000/svg}svg' and raiz.get('role') == 'img' and raiz.get('viewBox') == f'0 0 {m.LARGURA} {altura[0] if altura else "?"}'
                    and svg.endswith('</svg>\n') and '\r' not in svg and not svg.startswith('<?xml'),
                    f'{f.arquivo}: root {raiz.tag} role={raiz.get("role")} viewBox={raiz.get("viewBox")}', f'{f.arquivo}: well-formed, role="img", viewBox 0 0 {m.LARGURA} {altura[0] if altura else "?"}, LF and a final newline')
            classes = {c for e in raiz.iter() for c in e.get('class', '').split()}
            sem_regra = sorted(c for c in classes if c not in regras)
            confere(not sem_regra and all(c.startswith('dg') for c in classes), f'{f.arquivo}: classes with no rule in the manual CSS: {sem_regra}',
                    f'{f.arquivo}: every class it uses has a rule in the manual CSS ({len(classes)} classes)')
            confere(not m.cores_literais(svg) and not m.ids_sem_prefixo(svg, slug), f'{f.arquivo}: colours {m.cores_literais(svg)}, ids {m.ids_sem_prefixo(svg, slug)}',
                    f'{f.arquivo}: no literal colour, every id starts with {slug}-')
            fora = []
            for e in raiz.iter():
                tag = e.tag.split('}')[-1]
                if tag == 'rect':
                    d = 1 if 'dg-comp' in e.get('class', '') else .5      # a box is drawn inside its coordinates: half its line's width in, all round
                    vals = [float(e.get('x')) - d, float(e.get('y')) - d, float(e.get('width')) + 2 * d, float(e.get('height')) + 2 * d]
                elif tag in ('polyline', 'polygon'):
                    vals = [float(v) for par in e.get('points').split() for v in par.split(',')]
                elif tag == 'circle':
                    vals = [float(e.get('cx')), float(e.get('cy'))]
                elif tag == 'text':
                    vals = [float(e.get('x')), float(e.get('y'))]
                else:
                    continue
                fora += [(tag, v) for v in vals if v % 4]
            confere(not fora, f'{f.arquivo}: coordinates off the 4 px grid: {fora[:5]}', f'{f.arquivo}: every coordinate and size on the 4 px grid')

        # the datasheet's table, kept in the tool, against chapter 2's own
        pinos, gnd = tabela_do_capitulo(m.CAPITULOS / 'cap02-hardware.md')
        errados = {g: (p, m.PINAGEM.get(p)) for g, p in pinos.items() if m.PINAGEM.get(p) != g}
        confere(len(pinos) == 26 and not errados, f'PINAGEM against chapter 2: {len(pinos)} GPIOs read, wrong: {errados}',
                'PINAGEM (the Pico W datasheet) names every pin of chapter 2\'s table as the table does: 26 GPIOs, from GP0 at pin 1 to GP28 at pin 34')
        confere(sorted(gnd) == sorted(p for p, nome in m.PINAGEM.items() if nome == 'GND') and m.PINAGEM[36] == '3V3' and m.PINAGEM[40] == 'VBUS',
                f'PINAGEM ground pins {sorted(p for p, nome in m.PINAGEM.items() if nome == "GND")}, chapter 2 says {gnd}',
                'PINAGEM: the same seven GND pins as chapter 2, 3V3 at 36, VBUS at 40')

        # the examples of the Home Assistant figure (the node, the broker and its port, the sensor's id), constants of the tool, against what chapter 23 says
        cap23 = (m.CAPITULOS / 'cap23-home-assistant.md').read_text(encoding='utf-8')
        confere(f'`{m.NO_EXEMPLO}`' in cap23 and f'IP Servidor {m.BROKER_EXEMPLO}; Porta {m.PORTA_EXEMPLO};' in cap23 and f'`t{m.HWID_EXEMPLO}`' in cap23,
                f'chapter 23 does not say the examples of the figure: node {m.NO_EXEMPLO}, broker {m.BROKER_EXEMPLO}:{m.PORTA_EXEMPLO}, key t{m.HWID_EXEMPLO}',
                'the Home Assistant figure\'s examples (node, broker and port, sensor id) are the ones chapter 23 writes')
        # the Prometheus figure's examples (the address, the account, the scrape interval, the permission's name), constants of the tool, against what chapter 24 says
        cap24 = (m.CAPITULOS / 'cap24-prometheus.md').read_text(encoding='utf-8')
        confere(f'`http://{m.IP_EXEMPLO}/metrics`' in cap24 and f'conta `{m.CONTA_EXEMPLO}`' in cap24 and f'scrape_interval: {m.INTERVALO_PROMETHEUS}s' in cap24
                and f'**{m.PERMISSAO_PAINEL}** [PERM_DASHBOARD]' in cap24 and f'Use `scrape_interval` de {m.INTERVALO_PROMETHEUS} s' in cap24,
                f'chapter 24 does not say the examples of the figure: {m.IP_EXEMPLO}, {m.CONTA_EXEMPLO}, {m.INTERVALO_PROMETHEUS} s, {m.PERMISSAO_PAINEL} for PERM_DASHBOARD',
                'the Prometheus figure\'s examples (address, account, scrape interval, the permission Painel for PERM_DASHBOARD) are the ones chapter 24 writes')
        # the syslog figure's examples (the collector's address, the level's name, the two collectors' names), against chapter 25
        cap25 = (m.CAPITULOS / 'cap25-syslog.md').read_text(encoding='utf-8')
        confere(f'"slog_srv":"{m.IP_COLETOR_EXEMPLO}"' in cap25 and f'**{m.NIVEL_MINIMO}**' in cap25 and all(f'{nome}' in cap25 for nome in m.COLETORES_SYSLOG),
                f'chapter 25 does not say the examples of the figure: {m.IP_COLETOR_EXEMPLO}, {m.NIVEL_MINIMO}, {m.COLETORES_SYSLOG}',
                'the syslog figure\'s examples (collector address, the level\'s name Nível Mínimo, rsyslog and syslog-ng) are the ones chapter 25 writes')
        # the integration figure's words for the peers, against chapter 20 (the table of what the device offers, the one of ports)
        cap20 = (m.CAPITULOS / 'cap20-integracao-visao-geral.md').read_text(encoding='utf-8')
        confere(all(palavra in cap20 for palavra in (m.PAR_GESTAO, m.PAR_PROMETHEUS, m.PAR_REDE_LOCAL, 'Coletor de syslog')) and 'Anúncio local' in cap20,
                f'chapter 20 does not say the names of the figure\'s peers: {m.PAR_GESTAO}, {m.PAR_PROMETHEUS}, {m.PAR_REDE_LOCAL}',
                'the integration figure\'s peers (servidor de gestão, Prometheus, rede local) are the words chapter 20 uses')
        # the fleet figure's words for its peers, against chapter 27
        cap27 = (m.CAPITULOS / 'cap27-frota.md').read_text(encoding='utf-8')
        confere(m.PAR_GESTOR in cap27 and 'GitHub' in cap27 and m.PAR_PROMETHEUS in cap27 and 'coletor' in cap27.lower(),
                f'chapter 27 does not say the names of the fleet figure\'s peers: {m.PAR_GESTOR}, GitHub, {m.PAR_PROMETHEUS}',
                'the fleet figure\'s peers (gestor de frota, GitHub, Prometheus, coletor) are the words chapter 27 uses')
        # the waves figure's waves (their names and what each holds) against the three paragraphs of chapter 27's "Em ondas", and the words of the barrier
        confere(all(f'**{nome}.** {texto[0].upper()}{texto[1:]}' in cap27 for nome, texto in m.ONDAS) and all(palavra in cap27 for palavra in ('versão', 'telemetria', 'linha de alarmes', 'período')),
                f'chapter 27 does not say the waves of the figure: {m.ONDAS}',
                'the waves figure\'s three waves are the three paragraphs of chapter 27 (Onda 0, 1 and 2), and the words of the barrier (versão, telemetria, alarmes, período) are in the chapter')
        # the block diagram's flash numbers, which the figure reads from src/ota/ota_layout.h, against the table of chapter 1 (the program's and the file system's sizes) and its words for the cores
        cap01 = (m.CAPITULOS / 'cap01-o-simut.md').read_text(encoding='utf-8')
        camadas = m.Numeros(fonte).flash()
        confere(f'| Programa | {m.milhar(camadas["programa"])} KiB |' in cap01 and f'| Sistema de arquivos (LittleFS) | {m.milhar(camadas["sistema"])} KiB |' in cap01 and '**o Core 0**' in cap01 and '**o Core 1**' in cap01,
                f'chapter 1 does not say what ota_layout.h gives: program {camadas["programa"]} KiB, file system {camadas["sistema"]} KiB',
                'the flash numbers read from ota_layout.h (program and LittleFS) are the ones chapter 1\'s table gives')
        # the reconnection ladder read from the network code, against the table of chapter 9
        cap09 = (m.CAPITULOS / 'cap09-rede.md').read_text(encoding='utf-8')
        escada = m.Numeros(fonte).escada()
        linhas_do_capitulo = [f'| Queda da conexão (ponto de partida) | {escada["esperas"][0]} s |'] + [f'| {k}ª | {w} s |' for k, w in enumerate(escada['esperas'][1:], 1)] + \
                             [f'| {len(escada["esperas"])}ª | Dormência: {escada["dormencia"] // 60} min |']
        confere(all(l in cap09 for l in linhas_do_capitulo) and f'Uma tentativa de entrar na rede leva até {escada["tentativa"]} s' in cap09 and f'Cada busca tem {escada["busca"]} s de prazo' in cap09
                and f'O aparelho cumpre {["", "uma", "duas", "três"][escada["dormentes"]]} dormências' in cap09,
                f'chapter 9 does not say the ladder the code gives: {escada}', 'the ladder read from the network code (the waits, the dormancy, the 20 s attempt, the 15 s search, the three dormancies) is the table of chapter 9')
        # the time correction's events, with the names the log code table gives them, against what chapter 10 calls them, and its 5 s
        cap10 = (m.CAPITULOS / 'cap10-data-hora.md').read_text(encoding='utf-8')
        correcao = m.Numeros(fonte).correcao()
        confere(all(f'**{correcao["eventos"][chave][1]}** ({correcao["eventos"][chave][0]})' in cap10 for chave in ('provisorio', 'corrigindo', 'corrigidos', 'caches')) and f'Se a diferença passa de {correcao["limiar"]} s' in cap10,
                f'chapter 10 does not call the events as the log code table does: {correcao["eventos"]}, threshold {correcao["limiar"]} s',
                'the time correction\'s events (524, 408, 409, 410) are named in chapter 10 as tools/logcodes.tsv names them, and its threshold is the code\'s 5 s')
        # the update timeline's measured times (constants of the tool) and the apply's "cerca de 25 s", against chapter 17
        cap17 = (m.CAPITULOS / 'cap17-atualizacao-backup.md').read_text(encoding='utf-8')
        virgula = lambda x: f'{x:.1f}'.replace('.', ',')
        ota = m.Numeros(fonte).ota_linha_do_tempo()
        confere(f'| Envio e conferência da imagem | {virgula(m.ENVIO_MEDIDO_S[0])} a {virgula(m.ENVIO_MEDIDO_S[1])} s ({m.MEDIDO_EM}, 6 atualizações) |' in cap17
                and f'| Da aplicação até o aparelho voltar | {m.VOLTA_MEDIDA_S[0]} a {m.VOLTA_MEDIDA_S[1]} s ({m.MEDIDO_EM}, 6 de 6 atualizações bem-sucedidas) |' in cap17
                and f'Aplicação, os cerca de {ota["aplicacao"]} s em que a imagem é copiada' in cap17,
                f'chapter 17 does not say the times of the figure: send {m.ENVIO_MEDIDO_S}, back {m.VOLTA_MEDIDA_S}, apply {ota["aplicacao"]} s',
                'the update timeline\'s times (the measured send and the measured return, with their version and date, and the apply\'s "cerca de 25 s" read from the web page) are the ones chapter 17 gives')
        # the Air wake's measured times (constants of the tool), against chapter 19's table and the energy analysis, which measured them
        cap19 = (m.CAPITULOS / 'cap19-air.md').read_text(encoding='utf-8')
        plano = (m.ROOT / 'docs' / 'analysis' / 'SIMUT_AIR_PLANO_ENERGIA.md').read_text(encoding='utf-8')
        vir = lambda x: str(x).replace('.', ',')
        confere(f'| Despertar de leitura, do boot até a decisão | {vir(m.AIR_ATE_DECIDIR_S)} s, dos quais ' in cap19 and f'| Despertar de telemetria | cerca de {vir(m.AIR_DESPERTAR_TELEMETRIA_S)} s |' in cap19
                and f'com o **Intervalo Histórico** de {m.AIR_INTERVALO_S // 60} min' in cap19
                and f'| `setup( )` até a primeira fase | 10,73 s | **{vir(m.AIR_SETUP_S)} s** |' in plano and f'| wake até o DECIDE | 25,55 s | **{vir(m.AIR_ATE_DECIDIR_S)} s** |' in plano
                and '`[AIR] phase=WARMUP @10733` | fim do `setup( )`' in plano and '(o de 12,8 s é o wake de telemetria)' in plano,
                f'chapter 19 or the energy analysis does not say the wake times of the figure: setup {m.AIR_SETUP_S} s (where setup( ) ends and the WARMUP phase begins), to the decision {m.AIR_ATE_DECIDIR_S} s, telemetry {m.AIR_DESPERTAR_TELEMETRIA_S} s (chapter 19; the analysis read 12,8 s), '
                f'interval {m.AIR_INTERVALO_S} s',
                'the Air wake figure\'s measured times are the ones chapter 19\'s table and the energy analysis (08/09/2026) give: the setup (2,47 s) from the analysis, the decision (9,31 s) from both, the telemetry wake (12,7 s) and the interval from the chapter, the analysis\' probe having read 12,8 s')

        # the entry figure's examples (constants of the tool) against chapter 26, which gives them
        cap26 = (m.CAPITULOS / 'cap26-api-rest.md').read_text(encoding='utf-8')
        confere(f'H=http://{m.IP_EXEMPLO}' in cap26 and '"nonce":"' + m.NONCE_EXEMPLO.rstrip('…') in cap26,
                f'chapter 26 does not give the examples of the entry figure: the address {m.IP_EXEMPLO} (H=http://...) and a code that starts {m.NONCE_EXEMPLO}',
                'the entry figure\'s examples (the address and the start of the code) are chapter 26\'s own')

        # the update figure's wait between two tries of login_init is read from the web page; chapter 26 says the same number, and a code that changes it leaves the chapter stale
        espera = m.Numeros(m.Fonte(m.ROOT)).api_ota()['espera_s']
        confere(f'repetindo `GET /api/login_init` a cada {espera} s' in cap26, f'chapter 26 does not say "a cada {espera} s" for the tries of login_init, which is what the web page waits',
                'the wait between two tries of login_init (read from the web page) is the one chapter 26 gives')

        # the modes figure reads the idle timeout, the grace and the held event from the code; chapter 19 says the same numbers, and a code that changes one leaves the chapter stale
        modos = m.Numeros(m.Fonte(m.ROOT)).modos()
        confere(f'{modos["idle_s"]} s de fábrica' in cap19 and f'M0 por só {modos["graca_s"]} s' in cap19 and f'**{modos["nome"]}** ({modos["codigo"]})' in cap19,
                f'chapter 19 does not say what the code says: {modos["idle_s"]} s de fábrica, M0 por só {modos["graca_s"]} s, **{modos["nome"]}** ({modos["codigo"]})',
                'the idle timeout, the grace and the held event (read from the code) are the ones chapter 19 gives')

        # the flash figure reads the sizes from the layout; chapter 28's table gives the same ones, and a layout that changes one leaves the chapter stale
        cap28 = (m.CAPITULOS / 'cap28-especificacoes.md').read_text(encoding='utf-8')
        flash = m.Numeros(m.Fonte(m.ROOT)).flash_e_atualizacao()
        confere(f'| Espaço do programa | {m.milhar(flash["programa"])} KiB,' in cap28 and f'| Dados da atualização | {flash["dados"]} KiB no fim da flash |' in cap28
                and f'({m.milhar(flash["teto"])} KiB) |' in cap28 and f'| Sistema de arquivos (LittleFS) | {flash["sistema"] // 1024} MiB |' in cap28,
                f'chapter 28 does not say what the layout says: program {flash["programa"]} KiB, file system {flash["sistema"]} KiB, ceiling {flash["teto"]} KiB, data {flash["dados"]} KiB',
                'the sizes of the flash (read from the layout) are the ones chapter 28\'s table gives')

        # ---- the builder's guards --------------------------------------------------------------------------------------------------------------------------
        s = m.Svg('99-teste', 100)
        s.texto(16, 28, 'um texto')
        for descr, fn in (('a text over another', lambda: s.texto(20, 28, 'outro texto em cima')),
                          ('a text past the canvas', lambda: s.texto(m.LARGURA - 8, 60, 'passa do quadro')),
                          ('a box off the 4 px grid', lambda: s.caixa(10, 10, 40, 40)),
                          ('a word wider than its line', lambda: m.quebrar('uma_palavra_enorme_sem_espacos_nenhuns', 40))):
            try:
                fn()
                confere(False, f'the builder let through {descr}')
            except ValueError:
                confere(True, None, f'the builder refuses {descr}')
        # a text is wholly inside a box or wholly outside it: the edge of a box through a glyph is refused, whichever is drawn first, unless a filled box drawn later holds the text
        def cena(faca):
            g = m.Svg('99-teste', 200)
            g.caixa(100, 40, 80, 40)
            return faca(g)
        for descr, fn, recusa in (('a text wholly inside a box', lambda g: g.texto(108, 64, 'dentro'), False),
                                  ('a text wholly outside a box', lambda g: g.texto(16, 64, 'fora'), False),
                                  ('a text across the edge of a box', lambda g: g.texto(148, 64, 'atravessa a borda'), True),
                                  ('a box drawn over part of a text', lambda g: (g.texto(60, 120, 'um texto comprido'), g.caixa(100, 100, 80, 40)), True),
                                  ('a text on a filled box that stands over the edge of another', lambda g: (g.caixa(116, 24, 48, 20), g.texto(140, 40, 'USB', 'dg-n', 'middle')), False),
                                  ('a text on a box with no fill over the edge of another', lambda g: (g.caixa(116, 24, 48, 20, 'dg-caixa-apagada'), g.texto(140, 40, 'USB', 'dg-n', 'middle')), True)):
            try:
                cena(fn)
                feito = False
            except ValueError as e:
                feito = 'crosses the edge' in str(e)
            confere(feito == recusa, f'{descr}: refused={feito}, expected {recusa}', f'the builder {"refuses" if recusa else "draws"} {descr}')
        linhas = m.quebrar('um dois tres quatro cinco', 80)
        confere(len(linhas) >= 2 and ' '.join(linhas) == 'um dois tres quatro cinco' and all(m.largura_texto(l) <= 80 for l in linhas),
                f'quebrar gave {linhas}', f'quebrar breaks a line that does not fit at a space and keeps every line inside its width: {linhas}')
        confere(m.n(24.0) == '24' and m.n(24.5) == '24.5' and m.n(4) == '4', f'n( ) wrote {m.n(24.0)} {m.n(24.5)} {m.n(4)}', 'numbers are written without ".0"')

        # ---- one character, deleted, extra ----------------------------------------------------------------------------------------------------------------
        for nome in (nomes[0], nomes[-1]):
            alvo = img / nome
            original = alvo.read_bytes()
            t = original.decode('utf-8')
            i = t.index('class="dg-') + len('class="dg')       # the hyphen of the first class: a hyphen becomes an underscore
            alvo.write_text(t[:i] + '_' + t[i + 1:], encoding='utf-8')
            a = agora()
            confere(len(a) == 1 and a[0].startswith(f'{nome}: differs from what the code draws'), f'one character changed in {nome} gave {a}', f'one character changed in {nome}: exactly 1 finding, naming the file')
            alvo.write_bytes(original)
            confere(not agora(), f'{nome} put back is not clean', f'{nome} put back: 0 findings')
        alvo = img / nomes[0]
        original = alvo.read_bytes()
        alvo.unlink()
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{nomes[0]}: missing'), f'{nomes[0]} deleted gave {a}', 'an SVG deleted: exactly 1 finding (missing)')
        alvo.write_bytes(original)
        (img / '99-x.svg').write_bytes(original)
        a = agora()
        confere(len(a) == 1 and a[0].startswith('99-x.svg: no row of FIGURAS'), f'an SVG no row names gave {a}', 'an SVG no row names: exactly 1 finding')
        (img / '99-x.svg').unlink()
        (img / '12-wifi-niveis.png').write_bytes(b'not a diagram of this tool')
        (img / '99-x.png').write_bytes(b'x')
        confere(not agora(), 'a PNG in the folder is a finding', 'a PNG in the folder (12-wifi-niveis.png, a diagram not drawn yet): 0 findings')
        (img / '12-wifi-niveis.png').unlink()
        (img / '99-x.png').unlink()

        # ---- the chapters ----------------------------------------------------------------------------------------------------------------------------------
        primeira = m.FIGURAS[0]
        capitulo = cap / primeira.capitulo
        original_cap = capitulo.read_text(encoding='utf-8')
        linhas = original_cap.split('\n')
        i = next(k for k, l in enumerate(linhas) if l.startswith(f'::: {{.figura #{primeira.bloco} '))
        capitulo.write_text('\n'.join(linhas[:i] + linhas[i + 3:]), encoding='utf-8')     # a block is three lines: the opening, the caption, the closing
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{primeira.bloco}: the figure block is gone'), f'a block removed gave {a}', 'a figure block removed: exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        trocar(capitulo, f'arquivo="{primeira.arquivo}"', f'arquivo="{m.slug_de(primeira.arquivo)}.png"')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{primeira.bloco}: the block says'), f'a block with another arquivo gave {a}', 'a block with another arquivo (the .png it had): exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        trocar(capitulo, f'#{primeira.bloco} tipo="diagrama"', f'#{primeira.bloco} tipo="tft"')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f"{primeira.bloco}: the block says tipo='tft'"), f'a block with another tipo gave {a}', 'a block with another tipo: exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        b = m.ler_blocos(cap)[primeira.bloco]
        trocar(capitulo, f'captura="{b.captura}"', f'captura="{b.captura}."')
        a = agora()
        confere(len(a) == 1 and a[0].startswith(f'{primeira.bloco}: the captura= of the block'), f'a captura= edited gave {a}', 'a captura= edited: exactly 1 finding')
        capitulo.write_text(original_cap, encoding='utf-8')
        capitulo.write_text(original_cap + '\n::: {.figura #fig-02-zzz tipo="diagrama" arquivo="02-zzz.svg" captura="x"}\nUm diagrama a mais.\n:::\n', encoding='utf-8')
        a = agora()
        confere(len(a) == 1 and a[0].startswith('fig-02-zzz: a block of tipo="diagrama" with the SVG 02-zzz.svg'), f'a diagram block with an SVG no row names gave {a}',
                'a tipo="diagrama" block with an .svg no row names: exactly 1 finding')
        capitulo.write_text(original_cap + '\n::: {.figura #fig-02-zzz tipo="diagrama" arquivo="02-zzz.png" captura="x"}\nUm diagrama a mais.\n:::\n', encoding='utf-8')
        confere(not agora(), 'a diagram block with a .png no row names is a finding', 'a tipo="diagrama" block with a .png no row names (not drawn yet): 0 findings')
        capitulo.write_text(original_cap, encoding='utf-8')
        confere(not agora(), 'the chapter put back is not clean')

        # ---- colour and id: on disk, and the rule alone ----------------------------------------------------------------------------------------------------
        alvo = img / nomes[0]
        original = alvo.read_bytes()
        t = original.decode('utf-8')
        for descr, antigo, novo in (('a hex colour', '<rect ', '<rect fill="#ff0000" '), ('a colour name', '<rect ', '<rect stroke="red" '),
                                    ('an rgb( ) in a style', '<rect ', '<rect style="fill:rgb(255,0,0)" ')):
            alvo.write_text(t.replace(antigo, novo, 1), encoding='utf-8')
            a = agora()
            confere(len(a) == 2 and a[0].startswith(f'{nomes[0]}: differs') and 'literal colour' in a[1] and a[1].startswith(f'{nomes[0]}:'),
                    f'{descr} injected on disk gave {a}', f'{descr} injected on disk: 2 findings, that the file differs from the code and the literal colour')
        alvo.write_text(t.replace('<rect ', '<rect id="sem-prefixo" ', 1), encoding='utf-8')
        a = agora()
        confere(len(a) == 2 and a[0].startswith(f'{nomes[0]}: differs') and 'does not start with' in a[1], f'an id without the slug injected on disk gave {a}',
                'an id without the slug injected on disk: 2 findings, that the file differs and the id')
        alvo.write_bytes(original)
        confere(not agora(), 'the file put back is not clean')
        # the code itself drawing them: the files are written from it, so the file matches the drawing and only the rule is left
        desenho = getattr(m, m.FIGURAS[0].desenho)
        for descr, antigo, novo, pedaco in (('a literal colour', '<rect ', '<rect fill="#ff0000" ', 'literal colour'),
                                            ('an id without the slug', '<rect ', '<rect id="sem-prefixo" ', 'does not start with')):
            setattr(m, m.FIGURAS[0].desenho, lambda num, slug, antigo=antigo, novo=novo: desenho(num, slug).replace(antigo, novo, 1))
            try:
                mutadas = m.desenhar_todas(m.Fonte(m.ROOT))
            finally:
                setattr(m, m.FIGURAS[0].desenho, desenho)
            escritos = tmp / 'escritos'
            shutil.rmtree(escritos, ignore_errors=True)
            m.gravar(escritos, mutadas)
            a = m.conferir_imagens(escritos, mutadas)
            confere(len(a) == 1 and pedaco in a[0] and a[0].startswith(f'{nomes[0]}:'), f'{descr} drawn by the code gave {a}', f'{descr} drawn by the code and written: exactly 1 finding, the rule alone')

        # ---- determinism -----------------------------------------------------------------------------------------------------------------------------------
        outra = m.desenhar_todas(m.Fonte(m.ROOT))
        confere(outra == desenhadas, 'two draws differ: ' + ', '.join(mudados(desenhadas, outra)), 'two draws in a row give the same text')
        for pasta in ('a', 'b'):
            rc, so, se = em_processo('--write', '--images', str(tmp / pasta))
            confere(rc == 0, f'--write into {pasta} exited {rc}: {se.strip()[-200:]}')
        iguais = all((tmp / 'a' / n).read_bytes() == (tmp / 'b' / n).read_bytes() == (m.IMAGENS / n).read_bytes() for n in nomes) and sorted(p.name for p in (tmp / 'a').iterdir()) == sorted(nomes)
        confere(iguais, 'two --write runs, or a run and the committed files, differ', 'two --write runs write the same bytes as the committed files, and only the table\'s files')

        # ---- the source: a number changes exactly the diagrams that draw it ------------------------------------------------------------------------------
        def casos_da_fonte():
            """(description, [(file, old, new, how many times)], the diagrams that change, or the text the exit-2 message has to carry)."""
            mapas = ('02-pinos-release.svg', '02-pinos-alpha.svg', '02-pinos-air.svg')
            return [
                ('TFT_CS 28 -> 21, the release\'s chip select', [(CONFIG, '#define TFT_CS 28', '#define TFT_CS 21', 1)], {'02-pinos-release.svg'}),
                ('HD44780_D7 21 -> 28, the alpha\'s last data line', [(CONFIG, '#define HD44780_D7 21', '#define HD44780_D7 28', 1)], {'02-pinos-alpha.svg'}),
                ('AIR_CHARGER_PIN 17 -> 21', [(CONFIG, '#define AIR_CHARGER_PIN 17', '#define AIR_CHARGER_PIN 21', 1)], {'02-pinos-air.svg', '02-air-energia.svg'}),
                ('AIR_SENSOR_POWER_PIN 16 -> 21', [(CONFIG, '#define AIR_SENSOR_POWER_PIN 16', '#define AIR_SENSOR_POWER_PIN 21', 1)], {'02-pinos-air.svg', '02-air-energia.svg', '19-despertar.svg'}),
                ('MAX_SENSORS 16 -> 11 (the LAN build\'s slots): the three maps, the two figures whose note names the range and the block diagram\'s GP range, not the BME280\'s',
                 [(CONFIG, '#define MAX_SENSORS 16 ', '#define MAX_SENSORS 11 ', 1)], set(mapas) | {'02-ds18b20.svg', '02-dht22.svg', '01-arquitetura.svg'}),
                ('SPI0\'s MISO named GP21 in the TFT section', [(CONFIG, 'MOSI=GP19, MISO=GP16, SCK=GP18', 'MOSI=GP19, MISO=GP21, SCK=GP18', 1)], {'02-pinos-release.svg'}),
                ('BUZZER_PIN 22 -> 21 with HD44780_D7 21 -> 28 (a buzzer on a pin both images leave free)',
                 [(CONFIG, '#define BUZZER_PIN 22', '#define BUZZER_PIN 21', 1), (CONFIG, '#define HD44780_D7 21', '#define HD44780_D7 28', 1)],
                 {'02-pinos-release.svg', '02-pinos-alpha.svg'}),
                ('the radio\'s fourth GPIO 29 -> 30 in AirConfig.h', [(AIR_CONFIG, 'case 25: case 29: return false;', 'case 25: case 30: return false;', 1)], set(mapas)),
                ('an image renamed in ENVS of release_manifest.py (releasetwo -> pico2)', [(RELEASE_MANIFEST, '"releasetwo": "pico2_w_release"', '"pico2": "pico2_w_release"', 1)], {'03-arquivos.svg'}),
                ('a language pack taken out of data/lang', [('data/lang/language_es-ES.lng', None, None, None)], {'03-arquivos.svg'}),
                ('the checksum file renamed in the Checksums step of the release workflow', [(WORKFLOW_OTA, '> SHA256SUMS', '> SUMS256', 1)], {'03-arquivos.svg'}),
                ('the setup AP\'s address 192.168.4.1 -> 192.168.5.1', [('src/WebAddress.h', '#define SIMUT_AP_IP_TEXT "192.168.4.1"', '#define SIMUT_AP_IP_TEXT "192.168.5.1"', 1)], {'04-fluxo.svg'}),
                ('the default device name simut -> medidor (the AP is medidor_SETUP)', [('src/StorageManager.cpp', 'safeCopy(_currentConfig.deviceName, "simut"', 'safeCopy(_currentConfig.deviceName, "medidor"', 1)], {'04-fluxo.svg'}),
                ('the setup AP moved to the 9th place of the panel\'s menu', [(MENU, 'MENU_PIN_POLICY, MENU_SETUP_AP,\n\tMENU_LANG,', 'MENU_PIN_POLICY, MENU_LANG,\n\tMENU_SETUP_AP,', 1)], {'04-fluxo.svg'}),
                ('the console\'s enable renamed in the parser', [('src/CommandParser.cpp', 'if (t0 == "enable")', 'if (t0 == "habilitar")', 1)], {'04-fluxo.svg'}),
                ('the reload\'s own message names another command', [('src/AppManager_Commands.cpp', "Use 'reload confirm' para prosseguir", "Use 'reboot confirm' para prosseguir", 1)], {'04-fluxo.svg'}),
                ('the records a block holds 60 -> 120 in HistoryV5.h', [('src/HistoryV5.h', '#define H5_BLOCK_MAX_RECORDS    60 ', '#define H5_BLOCK_MAX_RECORDS    120', 1)], {'15-caminho.svg'}),
                ('the cleanup\'s limit 86 -> 90 percent in StorageManager.cpp', [('src/StorageManager.cpp', '/ info.totalBytes) <= 86) return;', '/ info.totalBytes) <= 90) return;', 1)], {'15-caminho.svg'}),
                ('the history\'s extension .h5 -> .h6', [('src/HistoryV5.h', '#define HISTORY_FILE_EXT        ".h5"', '#define HISTORY_FILE_EXT        ".h6"', 1)], {'15-caminho.svg'}),
                ('the telemetry\'s confirming class 2xx -> 3xx', [('src/TelemetryManager.cpp', 'code >= 200 && code < 300', 'code >= 300 && code < 400', None)], {'21-cursor.svg', '22-fluxo.svg'}),
                ('the wait after a failure, 5 s -> 7 s', [('src/TelemetryManager.h', 'BACKOFF_MIN_MS = 5000;', 'BACKOFF_MIN_MS = 7000;', 1)], {'21-cursor.svg'}),
                ('the wait\'s ceiling, 300 s -> 600 s', [('src/TelemetryManager.h', 'BACKOFF_MAX_MS = 300000;', 'BACKOFF_MAX_MS = 600000;', 1)], {'21-cursor.svg'}),
                ('the alarm line\'s resend 15 s -> 20 s', [('src/TelemetryManager.h', 'ALARM_RETRY_INTERVAL_MS = 15000;', 'ALARM_RETRY_INTERVAL_MS = 20000;', 1)], {'22-fluxo.svg'}),
                ('the socket timeout 4 s -> 6 s (the alarm line\'s wait for an answer)', [('src/SystemDefs_Network.h', 'NET_SOCKET_TIMEOUT_MS = 4000;', 'NET_SOCKET_TIMEOUT_MS = 6000;', 1)], {'22-fluxo.svg'}),
                ('the alarm topic\'s end /alarm -> /alerta', [('src/TelemetryManager.cpp', 'return mqttDataTopic( ) + "/alarm";', 'return mqttDataTopic( ) + "/alerta";', 1)], {'22-fluxo.svg'}),
                ('the ack topic\'s end /alarm/ack -> /alerta/ack', [('src/TelemetryManager.cpp', 'return mqttDataTopic( ) + "/alarm/ack";', 'return mqttDataTopic( ) + "/alerta/ack";', 1)], {'22-fluxo.svg'}),
                ('the discovery prefix homeassistant -> casa in HaDiscovery.h', [(HA_DISCOVERY, 'constexpr const char* PREFIX = "homeassistant";', 'constexpr const char* PREFIX = "casa";', 1)], {'23-fluxo.svg'}),
                ('the discovery topic\'s component sensor -> leitor', [(HA_DISCOVERY, '"%s/sensor/%s/%s/config"', '"%s/leitor/%s/%s/config"', 1)], {'23-fluxo.svg'}),
                ('the discovery topic\'s end config -> cfg', [(HA_DISCOVERY, '"%s/sensor/%s/%s/config"', '"%s/sensor/%s/%s/cfg"', 1)], {'23-fluxo.svg'}),
                ('the default data topic simut/data -> lab/dados, in the new configuration and in the blank field\'s fallback',
                 [('src/StorageManager.cpp', 'safeCopy(_currentConfig.mqttTopic, "simut/data"', 'safeCopy(_currentConfig.mqttTopic, "lab/dados"', 1),
                  (TELEMETRY, 'if (t.length( ) == 0) t = "simut/data";', 'if (t.length( ) == 0) t = "lab/dados";', 1)], {'23-fluxo.svg'}),
                ('the status topic\'s end /status -> /estado, with and without a folder in the data topic',
                 [(TELEMETRY, 'return base.substring(0, lastSlash) + "/status";', 'return base.substring(0, lastSlash) + "/estado";', 1),
                  (TELEMETRY, 'return base + "/status";', 'return base + "/estado";', 1)], {'23-fluxo.svg'}),
                ('the word of an online device online -> ligado, in the device\'s message and in what Home Assistant waits for',
                 [(TELEMETRY, '\\"status\\":\\"online\\",\\"ip\\":', '\\"status\\":\\"ligado\\",\\"ip\\":', 1), (HA_DISCOVERY, '\\"pl_avail\\":\\"online\\"', '\\"pl_avail\\":\\"ligado\\"', 1)], {'23-fluxo.svg'}),
                ('the word of a vanished device offline -> apagado, in the last will and in what Home Assistant waits for',
                 [(TELEMETRY, '\\"status\\":\\"offline\\"}', '\\"status\\":\\"apagado\\"}', 1), (HA_DISCOVERY, '\\"pl_not_avail\\":\\"offline\\"', '\\"pl_not_avail\\":\\"apagado\\"', 1)], {'23-fluxo.svg'}),
                ('the availability message\'s key status -> estado, in the device\'s two messages and in the template Home Assistant reads it with',
                 [(TELEMETRY, '\\"status\\":\\"online\\",\\"ip\\":', '\\"estado\\":\\"online\\",\\"ip\\":', 1), (TELEMETRY, '\\"status\\":\\"offline\\"}', '\\"estado\\":\\"offline\\"}', 1),
                  (HA_DISCOVERY, '{{ value_json.status }}', '{{ value_json.estado }}', 1)], {'23-fluxo.svg'}),
                ('the telemetry record\'s timestamp key ts -> t0', [(TELEMETRY, '"{\\"ts\\":%lu", (unsigned long)rec.epoch', '"{\\"t0\\":%lu", (unsigned long)rec.epoch', 1)], {'23-fluxo.svg'}),
                ('the temperature key\'s prefix t -> T', [(TELEMETRY, 'snprintf(tKey, sizeof(tKey), "t%s", hwid);', 'snprintf(tKey, sizeof(tKey), "T%s", hwid);', 1)], {'23-fluxo.svg'}),
                ('the rounds of the password hash 5000 -> 10000', [(NET_DEFS, 'constexpr uint16_t PASSWORD_HMAC_ROUNDS = 5000;', 'constexpr uint16_t PASSWORD_HMAC_ROUNDS = 10000;', 1)], {'24-coleta.svg'}),
                ('the measured time of a /metrics read with Basic 699 ms -> 1400 ms, in the comment that records it', [(NET_DEFS, 'a /metrics read with Basic 699 ms', 'a /metrics read with Basic 1400 ms', 1)], {'24-coleta.svg'}),
                ('the lockout\'s ceiling 300 s -> 280 s', [(NET_DEFS, 'AUTH_LOCKOUT_MAX_MS = 300000;', 'AUTH_LOCKOUT_MAX_MS = 280000;', 1)], {'24-coleta.svg'}),
                ('the lockout\'s first penalty 2 s -> 4 s (the shift starts from 2000 ms), one rung fewer', [(NET_DEFS, 'uint32_t ms = 1000UL << n;', 'uint32_t ms = 2000UL << n;', 1)], {'24-coleta.svg'}),
                ('the route /metrics -> /metricas', [(WEB_CORE, '_server->on("/metrics", HTTP_GET', '_server->on("/metricas", HTTP_GET', 1)], {'24-coleta.svg', '20-integracao.svg', '27-arquitetura.svg'}),
                ('the scheme of the credentials Basic -> Token', [(WEB_METRICS, 'if (!h.startsWith("Basic ")) return 0;', 'if (!h.startsWith("Token ")) return 0;', 1)], {'24-coleta.svg', '27-arquitetura.svg'}),
                ('the locked address\'s answer 429 -> 423', [(WEB_METRICS, 'if (respondIfLockedOut(ls, 429)) return -1;', 'if (respondIfLockedOut(ls, 423)) return -1;', 1)], {'24-coleta.svg'}),
                ('the wrong password\'s answer 401 -> 407', [(WEB_METRICS, '_server->send(401, "text/plain", "unauthorized\\n");', '_server->send(407, "text/plain", "unauthorized\\n");', 1)], {'24-coleta.svg'}),
                ('the missing permission\'s answer 403 -> 451', [(WEB_METRICS, '_server->send(403, "text/plain", "forbidden\\n");', '_server->send(451, "text/plain", "forbidden\\n");', 1)], {'24-coleta.svg'}),
                ('the answer with the text 200 -> 203', [(WEB_METRICS, '_server->send(200, "text/plain; version=0.0.4; charset=utf-8", "");', '_server->send(203, "text/plain; version=0.0.4; charset=utf-8", "");', 1)], {'24-coleta.svg'}),
                ('the syslog ring\'s depth 8 -> 6 (and the lines a pass sends)', [(SYSLOG_H, 'static constexpr int SLOTS = 8;', 'static constexpr int SLOTS = 6;', 1)], {'25-caminho.svg'}),
                ('the syslog default port 514 -> 5514', [('src/SystemDefs_Records.h', 'constexpr uint16_t SYSLOG_DEFAULT_PORT = 514;', 'constexpr uint16_t SYSLOG_DEFAULT_PORT = 5514;', 1)], {'25-caminho.svg', '20-integracao.svg'}),
                ('the mDNS service name simut -> casa', [(NETWORK, 'MDNS.addService(nullptr, "simut", "tcp", _advPort)', 'MDNS.addService(nullptr, "casa", "tcp", _advPort)', 1)], {'20-integracao.svg', '27-arquitetura.svg'}),
                ('the alpha image gets the mDNS announcement too (it loses -DSIMUT_MDNS=0)', [(PERFIS, '    -DHD44780_MODE_PARALLEL=1\n    -DSIMUT_CLI_FULL=0\n    -DSIMUT_MDNS=0\n    -DSIMUT_BLUETOOTH=1',
                                                                                                          '    -DHD44780_MODE_PARALLEL=1\n    -DSIMUT_CLI_FULL=0\n    -DSIMUT_BLUETOOTH=1', 1)], {'20-integracao.svg', '27-arquitetura.svg'}),
                ('the identity header X-SIMUT-Ver renamed X-SIMUT-Versao', [(TELEMETRY, 'http.addHeader("X-SIMUT-Ver", SIMUT_VERSION);', 'http.addHeader("X-SIMUT-Versao", SIMUT_VERSION);', 1)], {'27-arquitetura.svg'}),
                ('the session\'s Bearer scheme -> Token', [(WEB_AUTH, 'if (a.startsWith("Bearer ")) return "SIMUTSESS=" + a.substring(7);', 'if (a.startsWith("Token ")) return "SIMUTSESS=" + a.substring(7);', 1)], {'26-entrada.svg', '27-arquitetura.svg'}),
                ('the session cookie SIMUTSESS -> SESSAO, wherever the code names it', [(WEB_AUTH, 'SIMUTSESS', 'SESSAO', None)], {'26-entrada.svg', '27-arquitetura.svg'}),
                ('the manifest renamed in the release workflow', [(WORKFLOW_OTA, '*.lng manifest.json > SHA256SUMS', '*.lng manifesto.json > SHA256SUMS', 1)], {'03-arquivos.svg', '27-arquitetura.svg'}),
                ('the backup route /api/backup -> /api/copia', [(WEB_CORE, '_server->on("/api/backup", HTTP_GET', '_server->on("/api/copia", HTTP_GET', 1)], {'26-ota.svg', '27-ondas.svg'}),
                ('the restore\'s stage operation renamed send, in the code and in the file\'s own header', [(WEB_OTA, 'bool is_stage = (op == "stage");', 'bool is_stage = (op == "send");', 1),
                                                                                                          (WEB_OTA, 'restore (validate/apply/stage)', 'restore (validate/apply/send)', 1)], {'26-ota.svg', '27-ondas.svg'}),
                ('the commit flag of the stage 1 -> 2', [(WEB_OTA, 'bool commit = (_server->arg("commit") == "1");', 'bool commit = (_server->arg("commit") == "2");', 1)], {'26-ota.svg', '27-ondas.svg'}),
                ('the folder of the language packs /lang -> /idiomas', [(STORAGE_H, '#define DIR_LANG "/lang"', '#define DIR_LANG "/idiomas"', 1)], {'27-ondas.svg', '01-arquitetura.svg'}),
                ('the version\'s key in the status ver -> versao', [(WEB_API, '\\"name\\":\\"%s\\",\\"ver\\":\\"%s\\"', '\\"name\\":\\"%s\\",\\"versao\\":\\"%s\\"', 1)], {'26-ota.svg', '27-ondas.svg'}),
                ('a new sensor type, SHT31, in the enum', [(RECORDS, ' TYPE_BMP280 = 5            /**< temperature + pressure */', ' TYPE_BMP280 = 5,           /**< temperature + pressure */\n TYPE_SHT31 = 6', 1)], {'01-arquitetura.svg'}),
                ('the file system 1 MiB -> 512 KiB, in ota_layout.h and in platformio.ini', [(OTA_LAYOUT, '#define OTA_FILESYSTEM_SIZE      (1u * 1024u * 1024u)', '#define OTA_FILESYSTEM_SIZE      (512u * 1024u)', 1),
                                                                                                    ('platformio.ini', 'board_build.filesystem_size = 1m', 'board_build.filesystem_size = 512k', 1)], {'01-arquitetura.svg', '28-flash.svg'}),
                ('the config folder /config -> /conf', [(STORAGE_H, '#define DIR_CONFIG "/config"', '#define DIR_CONFIG "/conf"', 1)], {'01-arquitetura.svg'}),
                ('the event log\'s file /system.blog -> /eventos.blog', [(LOG_H, '#define LOG_FILE_CURRENT "/system.blog"', '#define LOG_FILE_CURRENT "/eventos.blog"', 1)], {'01-arquitetura.svg'}),
                ('the Air loses the Bluetooth (-DSIMUT_BLUETOOTH=1 out of its profile)', [(PERFIS, '    -DSIMUT_MDNS=0\n    -DSIMUT_BLUETOOTH=1\n    -DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH\n    -DSIMUT_AIR=1\n',
                                                                                              '    -DSIMUT_MDNS=0\n    -DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH\n    -DSIMUT_AIR=1\n', 1)], {'01-arquitetura.svg'}),
                ('the ladder\'s first wait 5 s -> 4 s', [(NET_DEFS, 'WIFI_RECONNECT_BASE_MS = 5000;', 'WIFI_RECONNECT_BASE_MS = 4000;', 1)], {'09-escada.svg'}),
                ('the fast cycles before the dormancy 5 -> 4', [(NET_DEFS, 'WIFI_MAX_CONNECT_CYCLES = 5;', 'WIFI_MAX_CONNECT_CYCLES = 4;', 1)], {'09-escada.svg'}),
                ('the dormancy\'s wait 10 min -> 5 min', [(NET_DEFS, 'WIFI_DORMANT_DELAY_MS = 600000;', 'WIFI_DORMANT_DELAY_MS = 300000;', 1)], {'09-escada.svg'}),
                ('the dormant waits served before the restart 3 -> 2', [(NET_DEFS, 'WIFI_DORMANT_MAX_WAITS = 3;', 'WIFI_DORMANT_MAX_WAITS = 2;', 1)], {'09-escada.svg'}),
                ('the attempt\'s deadline 20 s -> 10 s', [(NET_CYW43, 'else if (timeSince(_stateTimer, 20000)) {', 'else if (timeSince(_stateTimer, 10000)) {', 1)], {'09-escada.svg'}),
                ('the search\'s deadline 15 s -> 10 s', [(NET_DEFS, 'WIFI_SCAN_TIMEOUT_MS = 15000;', 'WIFI_SCAN_TIMEOUT_MS = 10000;', 1)], {'09-escada.svg'}),
                ('the correction\'s threshold 5 s -> 9 s', [(NET_MGR, 'if (_timeSyncCb && abs(delta) > 5) {', 'if (_timeSyncCb && abs(delta) > 9) {', 1)], {'10-correcao.svg'}),
                ('the name of event 408 in the log code table', [(LOGCODES, 'NTP correcting timestamps\tNTP corrigindo timestamps\t', 'NTP correcting timestamps\tNTP corrigindo marcas\t', 1)], {'10-correcao.svg'}),
                ('the code of the provisional time 524 -> 525 in the log code table', [(LOGCODES, '524\tNET_PROVISIONAL_TIME\t', '525\tNET_PROVISIONAL_TIME\t', 1)], {'10-correcao.svg'}),
                ('the heartbeat of the log policy 1 h -> 2 h', [(LOGPOLICY_H, '#define LOGPOL_HEARTBEAT_MS 3600000UL', '#define LOGPOL_HEARTBEAT_MS 7200000UL', 1)], {'16-transicao.svg'}),
                ('the cadence of the suppressed accounting 1 h -> 2 h', [(LOGPOLICY_H, '#define LOGPOL_REPORT_MS 3600000UL', '#define LOGPOL_REPORT_MS 7200000UL', 1)], {'16-transicao.svg'}),
                ('the name of the accounting event 5 in the log code table', [(LOGCODES, 'Routine log records suppressed\tRegistros de rotina suprimidos\t', 'Routine log records suppressed\tRegistros suprimidos\t', 1)], {'16-transicao.svg'}),
                ('the web page\'s estimate of the apply 25 s -> 30 s', [(WEBUI, 'Apply + reboot: ~25 s', 'Apply + reboot: ~30 s', 1)], {'17-ota-linha-tempo.svg'}),
                ('the web page\'s estimate of the boot 25 s -> 30 s', [(WEBUI, 'Full boot: ~25 s', 'Full boot: ~30 s', 1)], {'17-ota-linha-tempo.svg'}),
                ('the Air\'s warm-up 400 ms -> 800 ms', [(AIR_CPP, 'if (timeSince(_airPhaseTimer, 400)) {', 'if (timeSince(_airPhaseTimer, 800)) {', 1)], {'19-despertar.svg'}),
                ('the code\'s lifetime 60 s -> 30 s', [(WEB_H, 'NONCE_LIFETIME_MS = 60000', 'NONCE_LIFETIME_MS = 30000', 1)], {'26-entrada.svg'}),
                ('the login route /api/login -> /api/entrar', [(WEB_CORE, '_server->on("/api/login", HTTP_POST', '_server->on("/api/entrar", HTTP_POST', 1)], {'26-entrada.svg'}),
                ('the key lockSec of the code\'s answer -> lock_sec', [(WEB_AUTH, '\\"locked\\":%s,\\"lockSec\\":%lu}"', '\\"locked\\":%s,\\"lock_sec\\":%lu}"', 1)], {'26-entrada.svg'}),
                ('the answer of a logout with a Bearer token 204 -> 205', [(WEB_AUTH, '_server->send(204, "text/plain", "");', '_server->send(205, "text/plain", "");', 1)], {'26-entrada.svg'}),
                ('the page\'s wait between two tries of login_init 3 s -> 4 s', [(WEBUI, "await new Promise(r => setTimeout(r, 3000));\n                    try { let p = await fetch('/api/login_init", "await new Promise(r => setTimeout(r, 4000));\n                    try { let p = await fetch('/api/login_init", 1)], {'26-ota.svg'}),
                ('the answer of the apply 202 -> 200', [(WEB_OTA, '_server->send(202, "application/json", "{\\"accepted\\":true,\\"mode\\":\\"apply\\"}");', '_server->send(200, "application/json", "{\\"accepted\\":true,\\"mode\\":\\"apply\\"}");', 1)], {'26-ota.svg'}),
                ('the good answer of a stage 200 -> 201', [(WEB_OTA, '_server->send(overall_ok ? 200 : 422, "application/json", buf);', '_server->send(overall_ok ? 201 : 422, "application/json", buf);', 1)], {'26-ota.svg'}),
                ('the header with the backup\'s CRC renamed', [(WEB_OTA, '_server->sendHeader("X-Backup-PCrc"', '_server->sendHeader("X-Backup-Crc"', 1)], {'26-ota.svg'}),
                ('the extension of a backup .bkp -> .bak', [(WEB_OTA, '"backup_%s_%lu.bkp"', '"backup_%s_%lu.bak"', 1)], {'26-ota.svg'}),
                ('the idle timeout a build starts with 300 s -> 600 s', [(CONFIG, '#define AIR_IDLE_TIMEOUT_SEC 300', '#define AIR_IDLE_TIMEOUT_SEC 600', 1)], {'19-modos.svg'}),
                ('the grace of an interrupted cycle 10 s -> 20 s', [(CONFIG, '#define AIR_RESUME_GRACE_SEC 10', '#define AIR_RESUME_GRACE_SEC 20', 1)], {'19-modos.svg'}),
                ('the unclean boots that hold M0 3 -> 4', [(CONFIG, '#define AIR_MAX_DIRTY_BOOTS 3', '#define AIR_MAX_DIRTY_BOOTS 4', 1)], {'19-modos.svg'}),
                ('the name of the held event in the log code table', [(LOGCODES, 'Air cycle held in M0\tCiclo Air retido em M0\t', 'Air cycle held in M0\tCiclo Air retido\t', 1)], {'19-modos.svg'}),
                ('the word of the hibernate command hibernate -> hibernar', [(PARSER, 't1 == "hibernate" || t1 == "sleep"', 't1 == "hibernar" || t1 == "sleep"', 1)], {'19-modos.svg'}),
                ('the web update\'s ceiling one sector -> two sectors under the end of the program space', [(OTA_LAYOUT, '(OTA_APP_MAX_SIZE - OTA_FLASH_SECTOR_SIZE)  /* 1016 KiB */', '(OTA_APP_MAX_SIZE - 2u * OTA_FLASH_SECTOR_SIZE)  /* 1016 KiB */', 1)], {'28-flash.svg'}),
                ('the BME280 probe pair (4, 5) -> (6, 7)', [(BME_SENSOR, 'probe(4, 5, BME280_ADDR_PRIMARY)', 'probe(6, 7, BME280_ADDR_PRIMARY)', 1)], {'02-bmx280.svg'}),
                ('the BME280 addresses 0x76/0x77 -> 0x75/0x78 in the driver, code and comments', [(BME_DRIVER, '0x76', '0x75', None), (BME_DRIVER, '0x77', '0x78', None)], {'02-bmx280.svg'}),
            ]

        def aplica(raiz, edicoes):
            for rel, antigo, novo, vezes in edicoes:
                if antigo is None:
                    (raiz / rel).unlink()                  # a file taken out of a folder the tool lists
                else:
                    trocar(raiz / rel, antigo, novo, vezes)

        for k, (descr, edicoes, esperado) in enumerate(casos_da_fonte()):
            if not precisa(*sorted(esperado)):
                continue
            raiz = tmp / f'fonte_{k}'
            copiar_fonte(fonte, raiz)
            aplica(raiz, edicoes)
            novas, falta = desenhar_de(raiz)
            confere(falta is None and mudados(desenhadas, novas) == sorted(esperado), f'{descr}: changed {mudados(desenhadas, novas) if novas else falta}, expected {sorted(esperado)}',
                    f'{descr}: exactly {", ".join(sorted(esperado))} change')
        if precisa('02-pinos-release.svg'):
            raiz = tmp / 'fonte_cli'
            copiar_fonte(fonte, raiz)
            aplica(raiz, [(CONFIG, '#define TFT_CS 28', '#define TFT_CS 21', 1)])
            r = cli('--check', '--fonte', str(raiz), '--images', str(img), '--capitulos', str(cap))
            confere(r.returncode == 1 and r.stdout.count('differs from what the code draws') == 1 and r.stdout.startswith('02-pinos-release.svg:'),
                    f'--check on a source whose pin moved exited {r.returncode}: {r.stdout.strip()[-300:]}', 'command line: the source says the TFT\'s CS moved, the committed SVG is stale: exit 1, exactly the release\'s map')
            # the profile's -D flag wins over simut_config.h's default, as the compiler does
            raiz = tmp / 'fonte_perfil'
            copiar_fonte(fonte, raiz)
            perfis = (raiz / PERFIS)
            t = perfis.read_text(encoding='utf-8')
            i = t.index('[env:pico_w_release]')
            j = t.index('    -DNDEBUG', i)
            perfis.write_text(t[:j] + '    -DTFT_CS=21\n' + t[j:], encoding='utf-8')
            novas, falta = desenhar_de(raiz)
            confere(falta is None and mudados(desenhadas, novas) == ['02-pinos-release.svg'], f'-DTFT_CS=21 in the release profile: {mudados(desenhadas, novas) if novas else falta}',
                    'a -DTFT_CS=21 in [env:pico_w_release] moves the release\'s CS in the map, and only there')

        # what has to refuse to draw (exit 2): two functions on one pin, a pin among the slots, a premise the figures depend on
        recusas = [
            ('TOUCH_CS 17 -> 28, the TFT\'s CS pin', [(CONFIG, '#define TOUCH_CS 17', '#define TOUCH_CS 28', 1)], 'GP28', '02-pinos-release.svg'),
            ('TFT_DC 27 -> 5, a sensor slot', [(CONFIG, '#define TFT_DC 27', '#define TFT_DC 5', 1)], 'GP5', '02-pinos-release.svg'),
            ('the alpha loses -DHD44780_MODE_PARALLEL=1 (it would be the I2C backpack on GP26/GP27)', [(PERFIS, '    -DHD44780_MODE_PARALLEL=1\n    -DSIMUT_CLI_FULL=0\n    -DSIMUT_MDNS=0\n    -DSIMUT_BLUETOOTH=1',
                                                                                                         '    -DSIMUT_CLI_FULL=0\n    -DSIMUT_MDNS=0\n    -DSIMUT_BLUETOOTH=1', 1)], 'HD44780_MODE_PARALLEL', '02-pinos-alpha.svg'),
            ('AIR_CHARGER_ACTIVE_HIGH 1 -> 0 (the figure draws a divider off 5 V)', [(CONFIG, '#define AIR_CHARGER_ACTIVE_HIGH 1', '#define AIR_CHARGER_ACTIVE_HIGH 0', 1)], 'AIR_CHARGER_ACTIVE_HIGH', '02-air-energia.svg'),
            ('the Air no longer builds with -DSIMUT_AIR=1', [(PERFIS, '    -DSIMUT_AIR=1\n', '', 1)], 'SIMUT_AIR', '02-pinos-air.svg'),
            ('the discovery topic\'s component sensor -> a name of 29 characters: the topic, in two lines, no longer fits between the device\'s line and the broker\'s',
             [(HA_DISCOVERY, '"%s/sensor/%s/%s/config"', '"%s/binary_sensor_with_a_long_name/%s/%s/config"', 1)], 'px wide from x=92', '23-fluxo.svg'),
            ('the route no longer checks the lock before the password', [(WEB_METRICS, 'if (respondIfLockedOut(ls, 429)) return -1;', 'if (false) return -1;', 1)], 'respondIfLockedOut', '24-coleta.svg'),
            ('a wrong password on /metrics no longer feeds the lockout', [(WEB_METRICS, 'if (ls >= 0) applyExponentialPenalty(ls);', 'if (ls >= 0) (void)ls;', 1)], 'same penalty', '24-coleta.svg'),
            ('a wrong password on the login page no longer raises the penalty', [(WEB_AUTH, '\t\t\tuint32_t penaltyMs = applyExponentialPenalty(ls);\n\t\t\tLOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Login Failed: ")) + u);',
                                                                                          '\t\t\tuint32_t penaltyMs = 0;\n\t\t\tLOG_CODE(LOG_WARN, "SEC", SEC_LOGIN_FAIL, 0, String(TRL("Login Failed: ")) + u);', 1)], 'same penalty', '24-coleta.svg'),
            ('/metrics asks for another permission than PERM_DASHBOARD', [(WEB_METRICS, 'if (!((uint16_t)auth & PERM_DASHBOARD)) {', 'if (!((uint16_t)auth & PERM_CONFIG)) {', 1)], 'PERM_DASHBOARD', '24-coleta.svg'),
            ('the lockout is no longer a shift capped at the ceiling', [(NET_DEFS, 'return (ms > AUTH_LOCKOUT_MAX_MS) ? AUTH_LOCKOUT_MAX_MS : ms;', 'return ms;', 1)], 'AUTH_LOCKOUT_MAX_MS', '24-coleta.svg'),
            ('an image builds without the /metrics route', [(PERFIS, '    -DSIMUT_AIR=1\n', '    -DSIMUT_AIR=1\n    -DSIMUT_WEB_METRICS=0\n', 1)], 'SIMUT_WEB_METRICS', '24-coleta.svg'),
            ('the syslog gate is no longer the minimum level', [(SYSLOG_CPP, 'if (!_enabled || ev.level < _minLevel) return;', 'if (!_enabled) return;', 1)], 'minimum level', '25-caminho.svg'),
            ('the syslog ring no longer drops its oldest line when full', [(SYSLOG_CPP, 'if (_count == SLOTS) {', 'if (_count == SLOTS + 1) {', 1)], 'oldest', '25-caminho.svg'),
            ('the syslog pass is run in the setup access point too', [(LOOP, ' if (!_isApMode) {\n /* Syslog drain:', ' if (true) {\n /* Syslog drain:', 1)], 'access point', '25-caminho.svg'),
            ('a requested reboot no longer flushes the syslog ring', [(BOOT, 'app._syslogMgr->flushBlocking( );', 'app._syslogMgr->flushBlockingNot( );', 1)], 'flushes', '25-caminho.svg'),
            ('the syslog sink is installed before the telemetry is up', [(BOOT, ' _syslogMgr->configure(*_storageMgr);\n LogManager::instance( ).setSyslogSink(', ' LogManager::instance( ).setSyslogSink(', 1)], 'after the telemetry', '25-caminho.svg'),
            ('the syslog copy goes through the routine filter', [('src/LogManager.cpp', ' writeCompactToFlash(rec);\n }\n\n /* Syslog forwarding.', ' writeCompactToFlash(rec);\n\n /* Syslog forwarding.', 1)], 'routine filter', '25-caminho.svg'),
            ('the syslog queue is no longer in the RAM', [(SYSLOG_H, 'char _ring[SLOTS][LINE_MAX];', 'static char* _ring;', 1)], 'RAM', '25-caminho.svg'),
            ('the link is no longer the station joined to an access point', [('src/net/NetLink.h', 'return WiFi.status( ) == WL_CONNECTED; }', 'return true; }', 1)], 'joined', '25-caminho.svg'),
            ('the telemetry gets a third transport', [(RECORDS, ' TEL_TRANSPORT_MQTT = 1\n', ' TEL_TRANSPORT_MQTT = 1,\n TEL_TRANSPORT_COAP = 2\n', 1)], 'two transports', '20-integracao.svg'),
            ('the syslog forwarder is no longer UDP', [(SYSLOG_H, '@brief RFC 5424 / UDP syslog forwarder', '@brief RFC 5424 / TCP syslog forwarder', 1)], 'UDP', '20-integracao.svg'),
            ('the alarm line\'s POST no longer carries the identity headers', [(TELEMETRY, '\t\taddTelemetryAuthHeader(http, cfg);\n\t\taddIdentityHeaders(http, _storageRef);', '\t\taddTelemetryAuthHeader(http, cfg);', 1)],
             'identity headers', '27-arquitetura.svg'),
            ('the telemetry\'s POST no longer carries the identity headers', [(TELEMETRY, ' addIdentityHeaders(http, _storageRef);\n\n http.setTimeout(NET_SOCKET_TIMEOUT_MS);', ' http.setTimeout(NET_SOCKET_TIMEOUT_MS);', 1)], 'identity headers', '27-arquitetura.svg'),
            ('the session is read from the Bearer header before the cookie', [(WEB_AUTH, '\tif (_server->hasHeader("Cookie")) return _server->header("Cookie");\n\tif (_server->hasHeader("Authorization")) {', '\tif (_server->hasHeader("Authorization")) {', 1)],
             'cookie', '27-arquitetura.svg'),
            ('an apply of a backup that was not the one just validated is no longer refused', [(WEB_OTA, 'NOT_VALIDATED ? 409', 'NOT_VALIDATED ? 200', 1)], 'validated', '27-ondas.svg'),
            ('the restore has a fourth operation', [(WEB_OTA, 'restore (validate/apply/stage)', 'restore (validate/apply/stage/rollback)', 1)], 'restore', '27-ondas.svg'),
            ('the Air launches Core 1', [(DISPLAY_H, '#if SIMUT_AIR\n\tstatic constexpr bool kUsesCore1 = false;', '#if SIMUT_AIR\n\tstatic constexpr bool kUsesCore1 = true;', 1)], 'Core 1', '01-arquitetura.svg'),
            ('the file system in platformio.ini is no longer the one ota_layout.h says', [('platformio.ini', 'board_build.filesystem_size = 1m', 'board_build.filesystem_size = 2m', 1)], 'ota_layout.h', '01-arquitetura.svg'),
            ('the wait no longer doubles', [(NET_CYW43, '_reconnectDelay = min(_reconnectDelay * 2, MAX_RECONNECT_DELAY);', '_reconnectDelay = _reconnectDelay;', 1)], 'doubles', '09-escada.svg'),
            ('the ceiling of the wait drops below the largest wait of the ladder', [(NET_MGR_H, 'MAX_RECONNECT_DELAY = 120000;', 'MAX_RECONNECT_DELAY = 60000;', 1)], 'ceiling', '09-escada.svg'),
            ('the ladder asks for the setup AP when it ends', [(NET_CYW43, ' resetReconnectLadder( );\n LOG_CODE(LOG_INFO, "NET", NET_DORMANT_MODE, 0,', ' resetReconnectLadder( );\n beginAP(_deviceName);\n LOG_CODE(LOG_INFO, "NET", NET_DORMANT_MODE, 0,', 1)],
             'setup AP', '09-escada.svg'),
            ('the restart no longer goes back to the first wait', [('src/NetworkManager.cpp', '_reconnectDelay = WIFI_RECONNECT_BASE_MS;', '_reconnectDelay = WIFI_DORMANT_DELAY_MS;', 1)], 'first wait', '09-escada.svg'),
            ('a connection no longer resets the ladder', [(NET_CYW43, ' _connectCycles = 0; /* Success: reset cycle counter */\n resetReconnectLadder( );', ' _connectCycles = 0; /* Success: reset cycle counter */', 1)], 'resets the ladder', '09-escada.svg'),
            ('the correction no longer shifts the history', [(APP_CORE, 'blocks = _storageMgr->shiftHistoryTimeV5(delta, "", bootTs);', 'blocks = 0;', 1)], 'shifts', '10-correcao.svg'),
            ('the correction no longer limits itself to what this boot wrote', [(NET_MGR, '_timeSyncCb(_provisionalBase, delta);', '_timeSyncCb(0, delta);', 1)], 'this boot', '10-correcao.svg'),
            ('an event of the correction leaves the log code table', [(LOGCODES, 'APP_CACHE_INVALIDATED', 'APP_CACHE_DROPPED', 1)], 'APP_CACHE_INVALIDATED', '10-correcao.svg'),
            ('the first failure of a family is no longer written', [(LOGPOLICY_CPP, ' gf.faulty = true;\n gf.lastPersistMs = nowMs;\n return true;', ' gf.faulty = true;\n gf.lastPersistMs = nowMs;\n return false;', 1)], 'first failure', '16-transicao.svg'),
            ('a family that keeps failing is no longer held to one record an hour', [(LOGPOLICY_CPP, 'if (elapsed(nowMs, gf.lastPersistMs, LOGPOL_HEARTBEAT_MS)) {', 'if (true) {', 1)], 'an hour', '16-transicao.svg'),
            ('the telemetry failure is no longer a fault of its family', [(LOGPOLICY_CPP, 'LOGPOL_RULE(SYS_TEL_FAIL,             LOGGRP_TEL,  1),', 'LOGPOL_RULE(SYS_TEL_FAIL,             LOGGRP_TEL,  0),', 1)], 'one family', '16-transicao.svg'),
            ('the stage no longer writes the staging area', [(OTA_STAGING, 'uint32_t flash_offs = OTA_STAGING_OFFSET + offset_in_staging;', 'uint32_t flash_offs = OTA_APP_OFFSET + offset_in_staging;', None)], 'staging area', '17-ota-linha-tempo.svg'),
            ('the applier no longer rewrites the program from the start of the flash', [(OTA_APPLIER, 'flash_range_program(i * OTA_FLASH_SECTOR_SIZE,', 'flash_range_program(OTA_STAGING_OFFSET + i * OTA_FLASH_SECTOR_SIZE,', 1)], 'program', '17-ota-linha-tempo.svg'),
            ('the wake no longer writes the history before it chooses between the radio and the sleep', [(AIR_CPP, '  processHistoryLogging( );\n  _storageMgr->flushWipV5( );', '  _storageMgr->flushWipV5( );', 1)], 'history', '19-despertar.svg'),
            ('the wake goes to the radio whether it has a link or not', [(AIR_CPP, '_airPhase = online ? AIR_PHASE_CONNECT : AIR_PHASE_SLEEP;', '_airPhase = AIR_PHASE_CONNECT;', 1)], 'radio', '19-despertar.svg'),
            ('the sampling no longer connects the Wi-Fi in parallel', [(AIR_CPP, 'if (_airRadioWake && !_airNetGaveUp) {', 'if (false) {', 1)], 'parallel', '19-despertar.svg'),
            ('the phases are no longer in the order the figure draws', [(APP_H, '  AIR_PHASE_WARMUP,    /* M1: power sensors, settle */\n  AIR_PHASE_SAMPLE,', '  AIR_PHASE_SAMPLE,\n  AIR_PHASE_WARMUP,    /* M1: power sensors, settle */', 1)], 'order', '19-despertar.svg'),
            ('a code is no longer spent by the attempt that matches it', [(WEB_AUTH, "if (ls >= 0) { _loginStates[ls].nonce[0] = '\\0'; _loginStates[ls].prevNonce[0] = '\\0'; }", "if (ls >= 0) { _loginStates[ls].prevNonce[0] = '\\0'; }", 1)],
             'spent by the attempt that matches it', '26-entrada.svg'),
            ('a code is no longer spent by an attempt that does not match it', [(WEB_AUTH, "_loginStates[ls].nonce[0] = '\\0';\n\t\t_loginStates[ls].prevNonce[0] = '\\0';\n\t\tif (nonceExpired)", "if (nonceExpired)", 1)],
             'does not match it too', '26-entrada.svg'),
            ('the login page no longer hashes the password', [(WEBUI, 'if (pass.length !== 64) pass = sha256(pass);', 'if (pass.length !== 64) pass = pass;', 1)], 'in the browser', '26-entrada.svg'),
            ('a logout ends every session that has the token, not only the first', [(WEB_AUTH, '_activeSessions[i].token = "";\n\t\t\t\tbreak;', '_activeSessions[i].token = "";', 1)], 'only the session', '26-entrada.svg'),
            ('a Bearer logout is no longer told apart by the cookie', [(WEB_AUTH, 'const bool byBearer = !_server->hasHeader("Cookie") && _server->hasHeader("Authorization");', 'const bool byBearer = _server->hasHeader("Authorization");', 1)],
             'no cookie', '26-entrada.svg'),
            ('the login sets another cookie than the one the session is read from', [(WEB_AUTH, 'String cookieFlags = "SIMUTSESS=" + newToken', 'String cookieFlags = "SIMUTID=" + newToken', 1)], 'one cookie', '26-entrada.svg'),
            ('an image staged and valid with commit is no longer the only one committed', [(WEB_OTA, 'if (ok_staged && valid && commit) {', 'if (ok_staged && commit) {', 1)], 'staged and valid image', '26-ota.svg'),
            ('the apply answers after the device is already restarting', [(WEB_OTA, ' _server->client( ).flush( );\n delay(500); /* TCP flush before the reboot, as below. */', ' delay(500); /* TCP flush before the reboot, as below. */', 1)],
             'before the device restarts', '26-ota.svg'),
            ('air stop no longer disarms the cycle', [(APP_CMDS, '_airCfg.flags &= (uint8_t)~AIR_FLAG_CYCLE_ARMED;', '/* kept armed */', 1)], 'disarms the cycle', '19-modos.svg'),
            ('a wake on the charger no longer leaves the cycle armed', [(BOOT, 'AIR_BOOT_MARK("charger present: staying awake in M0");', '_airCfg.flags &= (uint8_t)~AIR_FLAG_CYCLE_ARMED; AIR_BOOT_MARK("charger present: staying awake in M0");', 1)],
             'leaves the cycle armed', '19-modos.svg'),
            ('a crash loop is no longer held in M0', [(BOOT, '} else if (dirty >= AIR_MAX_DIRTY_BOOTS) {', '} else if (false) {', 1)], 'held in M0', '19-modos.svg'),
            ('a wake starts the web server', [(BOOT, 'AIR_BOOT_MARK("web skipped (wake)");', 'AIR_BOOT_MARK("web started");', 1)], 'web server', '19-modos.svg'),
            ('a wake starts Bluetooth', [(BOOT, '_cmdMgr->beginBluetooth(_storageMgr->getConfig().deviceName);', 'if (true) _cmdMgr->beginBluetooth(_storageMgr->getConfig().deviceName);', 1)], 'does not start Bluetooth', '19-modos.svg'),
            ('the radio no longer counts the record the wake writes', [(AIR_CPP, 'getPendingEstimate( ) + 1UL >= minBatch;', 'getPendingEstimate( ) >= minBatch;', 1)], 'counting the one the wake writes', '19-modos.svg'),
            ('the idle timeout runs on the charger', [(LOOP, '} else if (timeSince(_airLastActivityMs, idleSec * 1000UL)) {', '} if (timeSince(_airLastActivityMs, idleSec * 1000UL)) {', 1)], 'does not run on the charger', '19-modos.svg'),
            ('M0 no longer lasts the grace when there is one', [(LOOP, ': (uint32_t)_airCfg.idleTimeoutSec;', ': (uint32_t)_airCfg.idleTimeoutSec + 1;', 1)], 'the grace when there is one', '19-modos.svg'),
            ('the staging area is no longer the size of the file system', [(OTA_LAYOUT, '(OTA_FILESYSTEM_SIZE)          /* 0x100000 (1024 KB) */', '(OTA_FILESYSTEM_SIZE / 2)        /* 0x100000 (1024 KB) */', 1)], 'file system\'s area', '28-flash.svg'),
            ('the staging area no longer starts where the program space ends', [(OTA_LAYOUT, '(OTA_APP_MAX_SIZE)             /* 0x0FF000 */', '(OTA_APP_MAX_SIZE + OTA_FLASH_SECTOR_SIZE) /* 0x0FF000 */', 1)], 'where the program space ends', '28-flash.svg'),
            ('the update\'s data no longer end the flash', [(OTA_LAYOUT, 'OTA_METADATA_OFFSET + OTA_METADATA_SIZE == OTA_FLASH_TOTAL', 'OTA_METADATA_OFFSET + OTA_METADATA_SIZE <= OTA_FLASH_TOTAL', 1)], 'end the flash', '28-flash.svg'),
            ('the program space no longer ends where the staging area starts', [(OTA_LAYOUT, 'static_assert(OTA_APP_OFFSET + OTA_APP_MAX_SIZE == OTA_STAGING_OFFSET', 'static_assert(OTA_APP_OFFSET + OTA_APP_MAX_SIZE <= OTA_STAGING_OFFSET', 1)], 'follows the program space', '28-flash.svg'),
            ('the largest image the web update takes is no longer the ceiling', [(OTA_LAYOUT, '#define OTA_IMAGE_MAX      OTA_APP_SAFE_MAX_SIZE', '#define OTA_IMAGE_MAX      OTA_APP_MAX_SIZE', 1)], 'largest image', '28-flash.svg'),
            ('a reading-only wake starts the network', [(BOOT, 'else if (!_airRadioWake) {', 'else if (false) {', 1)], 'never starts', '19-despertar.svg'),
            ('the wake no longer cuts the sensors\' power when it goes to sleep', [(AIR_CPP, 'airSensorPower(_airCfg.sensorPowerPin, false);', 'airSensorPower(_airCfg.sensorPowerPin, true);', 1)], 'falls', '19-despertar.svg'),
            ('the discovery configs are no longer published retained', [(TELEMETRY, 'ok = _mqttClient.publish(topic, payload, true);', 'ok = _mqttClient.publish(topic, payload, false);', 1)],
             'retained', '23-fluxo.svg'),
            ('the online message is no longer published retained', [(TELEMETRY, 'onlinePayload.c_str( ), true);', 'onlinePayload.c_str( ), false);', 1)], 'retained', '23-fluxo.svg'),
            ('the last will is no longer retained, in the connection with an account and in the one without', [(TELEMETRY, ' 0,\n true,\n willPayload.c_str( )', ' 0,\n false,\n willPayload.c_str( )', 2)],
             'retained', '23-fluxo.svg'),
            ('the device says ligado while Home Assistant still waits for online', [(TELEMETRY, '\\"status\\":\\"online\\",\\"ip\\":', '\\"status\\":\\"ligado\\",\\"ip\\":', 1)],
             'disagree on the availability message', '23-fluxo.svg'),
            ('Home Assistant reads the availability from a key the device does not write', [(HA_DISCOVERY, '{{ value_json.status }}', '{{ value_json.estado }}', 1)],
             'disagree on the availability message', '23-fluxo.svg'),
            ('a new configuration starts with a data topic the blank field does not fall back to', [('src/StorageManager.cpp', 'safeCopy(_currentConfig.mqttTopic, "simut/data"', 'safeCopy(_currentConfig.mqttTopic, "lab/dados"', 1)],
             'falls back', '23-fluxo.svg'),
            ('the status topic ends in /estado when the data topic has no folder', [(TELEMETRY, 'return base + "/status";', 'return base + "/estado";', 1)], '/estado', '23-fluxo.svg'),
            ('the telemetry confirms 200 to 399, no longer one class', [('src/TelemetryManager.cpp', 'code >= 200 && code < 300', 'code >= 200 && code < 400', None)], '2xx', '21-cursor.svg'),
        ]
        for k, (descr, edicoes, agulha, figura) in enumerate(recusas):
            if not precisa(figura):
                continue
            raiz = tmp / f'recusa_{k}'
            copiar_fonte(fonte, raiz)
            aplica(raiz, edicoes)
            novas, falta = desenhar_de(raiz)
            confere(falta is not None and agulha in str(falta), f'{descr}: {falta if falta else "drew all the same"}', f'{descr}: refuses to draw, and says {agulha}')
            rc, so, se = em_processo('--write', '--images', str(tmp / 'recusado'), '--fonte', str(raiz))
            confere(rc == 2 and agulha in se and not (tmp / 'recusado').exists(), f'--write on that source exited {rc}: {se.strip()[-200:]}', f'  and --write on it exits 2 and writes nothing')

        # ---- a pattern the tool looks for, gone: exit 2, naming the file and the pattern ---------------------------------------------------------------------
        for k, (rel, padrao) in enumerate(fonte.padroes):
            raiz = tmp / f'padrao_{k}'
            copiar_fonte(fonte, raiz)
            t = (raiz / rel).read_text(encoding='utf-8')
            estragado = re.sub(padrao, lambda mm: '§' + mm.group(0)[1:], t, flags=m.FLAGS_PADRAO)
            if estragado == t:
                confere(False, f'{rel}: the pattern {padrao} matches nothing in the file the tool read it from')
                continue
            (raiz / rel).write_text(estragado, encoding='utf-8')
            rc, so, se = em_processo('--check', '--fonte', str(raiz), '--images', str(img), '--capitulos', str(cap))
            confere(rc == 2 and rel in se and padrao in se, f'{rel}, pattern {padrao} mangled: exit {rc}, message {se.strip()[-240:]}', f'{rel}: the pattern {padrao[:60]} mangled: exit 2 naming the file and the pattern')
        confere(len(fonte.padroes) >= len(m.FIGURAS), f'the tool recorded only {len(fonte.padroes)} patterns for {len(m.FIGURAS)} diagrams', f'{len(fonte.padroes)} patterns recorded, each held to the control above')
        if precisa('03-arquivos.svg'):
            sem_pacote = tmp / 'fonte_sem_pacote'
            copiar_fonte(fonte, sem_pacote)
            for p in (sem_pacote / 'data' / 'lang').glob('*.lng'):
                p.unlink()
            rc, so, se = em_processo('--check', '--fonte', str(sem_pacote), '--images', str(img), '--capitulos', str(cap))
            confere(rc == 2 and 'data/lang' in se, f'no language pack in data/lang: exit {rc}, {se.strip()[-200:]}', 'data/lang with no pack: exit 2 naming the folder')
        sem_arquivo = tmp / 'fonte_sem_arquivo'
        copiar_fonte(fonte, sem_arquivo)
        (sem_arquivo / CONFIG).unlink()
        rc, so, se = em_processo('--check', '--fonte', str(sem_arquivo), '--images', str(img), '--capitulos', str(cap))
        confere(rc == 2 and CONFIG in se, f'{CONFIG} missing: exit {rc}, {se.strip()[-200:]}', f'{CONFIG} deleted: exit 2 naming it')

        # ---- the log policy's mirror, against the real policy compiled on the host ---------------------------------------------------------------------------
        if precisa('16-transicao.svg'):
            import random
            transicao = m.Numeros(fonte).transicao()
            cenario = []
            for ev in m.simular_transicao(m.EspelhoLogPolicy(transicao['batimento_ms'], transicao['relatorio_ms']), transicao)['entrada']:
                cenario.append(ev)
            aleatorio = random.Random(20261106)
            sequencias = [cenario]
            for _ in range(300):
                agora, evs = 0, []
                for _ in range(60):
                    agora += aleatorio.choice((0, 1000, 60_000, 600_000, 3_000_000, 3_600_000, 4_000_000)) + aleatorio.randrange(0, 5000)
                    evs.append(('E', agora, aleatorio.random() < 0.35))
                    if aleatorio.random() < 0.6:
                        evs.append(('R', agora + aleatorio.randrange(0, 3_700_000), False))
                sequencias.append(evs)
            oficina = tmp / 'politica'
            oficina.mkdir()
            binario, motivo = compilar_politica(m.ROOT / LOGPOLICY_CPP, oficina)
            if binario is None and motivo == 'no g++':
                PULADOS.append('the log policy\'s mirror against the compiled policy: no g++')
            else:
                confere(binario is not None, f'LogPolicy.cpp does not build on the host: {motivo}')
                if binario is not None:
                    todos = []
                    for evs in sequencias:
                        # the report may be asked at a time before the last event's: the times only have to rise in each kind, as millis( ) does, so sort
                        evs = sorted(evs, key=lambda e: e[1])
                        real, espelho = rodar_politica(binario, evs), rodar_espelho(m.EspelhoLogPolicy(transicao['batimento_ms'], transicao['relatorio_ms']), evs)
                        todos.append(real == espelho)
                    confere(all(todos), f'the mirror and the compiled policy disagree on {todos.count(False)} of {len(todos)} runs (the figure\'s scenario is the first)',
                            f'the log policy\'s mirror answers like the real LogPolicy.cpp compiled on the host: the figure\'s scenario and {len(todos) - 1} random runs, {sum(len(e) for e in sequencias)} events')
                    # a control that bites: a change in the policy's logic, in a scratch copy, and the compiled policy no longer answers like the mirror
                    for descr, antigo, novo in (('every failure is persisted', 'if (!gf.faulty) {', 'if (true) {'),
                                                ('a success after a failure is no longer a transition', 'if (!g.seen || g.faulty) {', 'if (!g.seen) {'),
                                                ('the heartbeat of a healthy family is gone', 'if (elapsed(nowMs, g.lastPersistMs, LOGPOL_HEARTBEAT_MS)) {', 'if (false) {')):
                        copia = tmp / f'politica_{abs(hash(descr))}'
                        copia.mkdir()
                        texto = (m.ROOT / LOGPOLICY_CPP).read_text(encoding='utf-8')
                        assert texto.count(antigo) == 1, antigo
                        (copia / 'LogPolicy.cpp').write_text(texto.replace(antigo, novo), encoding='utf-8')
                        mutado, motivo = compilar_politica(copia / 'LogPolicy.cpp', copia)
                        diferem = mutado is not None and any(rodar_politica(mutado, sorted(evs, key=lambda e: e[1])) != rodar_espelho(m.EspelhoLogPolicy(transicao['batimento_ms'], transicao['relatorio_ms']), sorted(evs, key=lambda e: e[1])) for evs in sequencias)
                        confere(diferem, f'{descr}: the mutated policy still answers like the mirror ({motivo})', f'{descr}, in a scratch copy of LogPolicy.cpp: the compiled policy no longer answers like the mirror')

        # ---- build_manual.py: the SVG in the page -----------------------------------------------------------------------------------------------------------
        figs = tmp / 'figs'
        figs.mkdir()
        svg_a = (m.IMAGENS / nomes[0]).read_text(encoding='utf-8')
        (figs / 'a.svg').write_text('<?xml version="1.0" encoding="UTF-8"?>\n' + svg_a, encoding='utf-8')
        (figs / 'b.svg').write_text(svg_a.replace('<svg ', '<svg id="outro-b" ', 1), encoding='utf-8')
        (figs / 'c.svg').write_text(svg_a.replace('<svg ', '<svg id="outro-b" ', 1), encoding='utf-8')
        svg, ids = bm.inline_svg(str(figs / 'a.svg'), 'Uma legenda com "aspas" & <tags>')
        confere(svg.startswith('<svg ') and '<?xml' not in svg and 'aria-label="Uma legenda com &quot;aspas&quot; &amp; &lt;tags&gt;"' in svg and 'role="img"' in svg and svg.rstrip().endswith('</svg>'),
                f'inline_svg( ) gave {svg[:200]}', 'inline_svg( ): the XML prologue is gone, the root is <svg>, aria-label carries the caption escaped, role="img" is there')
        confere(ids == re.findall(r'\sid="([^"]+)"', svg), f'inline_svg( ) ids {ids}', 'inline_svg( ) names the ids the SVG holds')
        (figs / 'd.svg').write_text(svg_a.replace('<svg ', '<svg id="outro-d" ', 1), encoding='utf-8')

        def pagina(*nomes_svg):
            """(the page's HTML, the figures) of render_figures( ) over pandoc-shaped blocks for the files named, read from the scratch folder."""
            antes = bm.IMG
            bm.IMG = str(figs)
            try:
                frag = '<p>Texto.</p>\n' + '\n'.join(
                    f'<div id="fig-99-{x}" class="figura" data-tipo="diagrama" data-arquivo="{x}.svg" data-captura="como">\n<p>Legenda <code>{x}</code>.</p>\n</div>' for x in nomes_svg)
                figuras = []
                return bm.render_figures(frag, '99', 'Teste', figuras), figuras
            finally:
                bm.IMG = antes

        html_fig, figuras = pagina('a', 'b', 'c')
        um = re.search(r'<figure id="fig-99-a" class="fig fig-diagrama"><div class="moldura moldura-svg"><svg [^>]*aria-label="Legenda a\."[^>]*>.*?</svg></div><figcaption>', html_fig, re.S)
        confere(um is not None and '<img' not in html_fig and '<?xml' not in html_fig and [f['svg_ids'] for f in figuras] == [[], ['outro-b'], ['outro-b']],
                f'render_figures( ) gave {html_fig[:300]}', 'render_figures( ): the SVG sits in <div class="moldura moldura-svg"> inside the <figure>, with aria-label, no <img>, no prologue')
        # the CSS of the diagrams: the drawing's width and no smaller, one size of text, the PNG diagram's frame left alone; each rule held by the CSS changed in a copy
        confere(css_do_svg(bm.CSS) == [], f'the manual\'s CSS: {css_do_svg(bm.CSS)}', 'the manual\'s CSS shows the SVG at its width (LARGURA) and no smaller, gives all the text of the diagrams one size (TAM) and has no rule for the PNG diagram\'s frame')
        for descr, antes, depois in (('min-width 560 px, the first rule', 'min-width:%dpx' % m.LARGURA, 'min-width:560px'), ('a width of 640 px', ';width:%dpx;' % m.LARGURA, ';width:640px;'),
                                     ('a max-width of 100 %', 'min-width:%dpx;' % m.LARGURA, 'min-width:%dpx;max-width:100%%;' % m.LARGURA), ('the notes at 12 px', 'svg.dg .dg-n{font-size:%dpx}' % m.TAM, 'svg.dg .dg-n{font-size:12px}'),
                                     ('a rule for the PNG diagram\'s frame', '.moldura-svg{', '.moldura-diagrama{background:var(--superficie)}\n.moldura-svg{')):
            mudado = bm.CSS.replace(antes, depois, 1)
            confere(mudado != bm.CSS and css_do_svg(mudado) != [], f'the check let through {descr}', f'the CSS check refuses {descr}: {css_do_svg(mudado)[0] if css_do_svg(mudado) else ""}')
        (figs / 'p.png').write_bytes(b'\x89PNG\r\n\x1a\n')
        antes_img = bm.IMG
        bm.IMG = str(figs)
        try:
            png = bm.render_figures('<div id="fig-99-p" class="figura" data-tipo="diagrama" data-arquivo="p.png" data-captura="como">\n<p>Legenda p.</p>\n</div>', '99', 'Teste', [])
        finally:
            bm.IMG = antes_img
        confere('<div class="moldura moldura-diagrama"><img ' in png and 'moldura-svg' not in png, f'a PNG diagram gave {png[:200]}', 'a diagram that is a PNG keeps its frame, moldura-diagrama: the SVG\'s frame is another class')
        erros = bm.svg_id_errors(figuras, html_fig)
        confere(len(erros) == 1 and 'outro-b' in erros[0], f'two SVG with one id gave {erros}', 'svg_id_errors( ): two SVG with the same id: exactly 1 error, naming the id')
        html_ok, figuras_ok = pagina('a', 'b', 'd')
        confere(bm.svg_id_errors(figuras_ok, html_ok) == [], f'two SVG with different ids gave {bm.svg_id_errors(figuras_ok, html_ok)}', 'svg_id_errors( ): two SVG with different ids: no error')
        erros = bm.svg_id_errors(figuras_ok, html_ok + '<h2 id="outro-d">Um título</h2>')
        confere(len(erros) == 1 and 'outro-d' in erros[0], f'an id the page already has gave {erros}', 'svg_id_errors( ): an SVG id that a heading of the page also has: exactly 1 error')
        html_png = bm.render_figures('<div id="fig-99-x" class="figura" data-tipo="tft" data-arquivo="nao-existe.png" data-captura="como">\n<p>x</p>\n</div>', '99', 'Teste', [])
        confere('pendente' in html_png and '<svg' not in html_png, 'a missing file did not stay a pending box', 'a figure whose file is missing is still the pending box (the .png path is untouched)')

        if shutil.which('pandoc'):
            raiz = tmp / 'arvore'
            for rel in ('tools/build_manual.py', 'tools/gen_logcodes.py', 'tools/logcodes.tsv', 'src/SystemDefs_Limits.h'):
                (raiz / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(m.ROOT / rel, raiz / rel)
            shutil.copytree(m.CAPITULOS, raiz / 'docs' / '_manual')
            shutil.copytree(m.IMAGENS, raiz / 'docs' / 'images' / 'manual', ignore=lambda d, ns: [n for n in ns if n.endswith('.png')])

            def build(*args):
                return subprocess.run([sys.executable, '-B', str(raiz / 'tools' / 'build_manual.py'), *args], capture_output=True, text=True, cwd=raiz)
            r = build('--check')
            confere(r.returncode == 0 and r.stdout.strip() == '', f'build_manual.py --check on the committed chapters and SVG exited {r.returncode}: {r.stdout.strip()[-300:]}',
                    'build_manual.py --check on a copy with the SVG: exit 0, nothing to say')
            r = build()
            pagina = (raiz / 'docs' / 'MANUAL.pt-BR.html').read_text(encoding='utf-8') if r.returncode == 0 else ''
            quadro = re.search(rf'<figure id="{re.escape(m.FIGURAS[0].bloco)}" class="fig fig-diagrama"><div class="moldura moldura-svg"><svg [^>]*aria-label="[^"]+"', pagina)
            confere(quadro is not None and '<?xml' not in pagina and f'data:image/svg+xml' not in pagina, f'the page built from the copy: {r.stderr.strip()[-200:]} {pagina[:100]}',
                    'build_manual.py on a copy: the page has the <svg> in the <figure> with aria-label, no prologue, no data: URI')
            # a second figure with the same SVG, in the copy's chapter 2, and the same id put into both
            pasta_img = raiz / 'docs' / 'images' / 'manual'
            shutil.copyfile(pasta_img / nomes[0], pasta_img / '99-copia.svg')
            with open(raiz / 'docs' / '_manual' / 'cap02-hardware.md', 'a', encoding='utf-8') as fh:
                fh.write('\n::: {.figura #fig-99-copia tipo="diagrama" arquivo="99-copia.svg" captura="uma cópia"}\nUma cópia do primeiro diagrama.\n:::\n')
            for nome in (nomes[0], '99-copia.svg'):
                trocar(pasta_img / nome, '<svg ', '<svg id="repetido" ', 1)
            r = build('--check')
            confere(r.returncode == 1 and r.stdout.count('repetido') == 1, f'two SVG with one id: build_manual.py --check exited {r.returncode}: {r.stdout.strip()[-300:]}',
                    'build_manual.py --check: two SVG with one id: exit 1, exactly one line about it')
        else:
            PULADOS.append('build_manual.py through pandoc')
            print('skip build_manual.py run through pandoc: pandoc is not installed (the controls above read render_figures( ) and svg_id_errors( ) directly)')

        # ---- the command line ------------------------------------------------------------------------------------------------------------------------------
        r = cli('--list')
        linhas = r.stdout.splitlines()
        confere(r.returncode == 0 and len(linhas) == len(m.FIGURAS) and all(l.count('\t') == 3 for l in linhas) and [l.split('\t')[0] for l in linhas] == nomes,
                f'--list: exit {r.returncode}, {len(linhas)} rows', f'--list: {len(linhas)} rows of file, chapter, function and captura=')
        r = cli('--check', '--fonte', str(tmp / 'vazia'))
        confere(r.returncode == 2 and 'cannot be read' in r.stderr and 'src/' in r.stderr, f'an empty source folder: exit {r.returncode}: {r.stderr.strip()[-200:]}', 'command line: exit 2 when the source cannot be read')
        alvo = img / nomes[0]
        original = alvo.read_bytes()
        alvo.unlink()
        r = cli('--check', '--images', str(img), '--capitulos', str(cap))
        confere(r.returncode == 1 and r.stdout.count('missing') == 1, f'a deleted SVG: exit {r.returncode}: {r.stdout.strip()[-200:]}', 'command line: exit 1 with one finding')
        alvo.write_bytes(original)
        escritos = tmp / 'escritos2'
        r = cli('--write', '--images', str(escritos))
        r2 = cli('--check', '--images', str(escritos))
        confere(r.returncode == 0 and r2.returncode == 0, f'--write exited {r.returncode}, --check on what it wrote exited {r2.returncode}: {r2.stdout.strip()[-200:]}', 'command line: --write, then --check on the files it wrote: exit 0')
        previa = tmp / 'previa'
        r = cli('--preview', str(previa))
        esperados = sorted(f'{m.slug_de(n)}.{t}.html' for n in nomes for t in ('claro', 'escuro'))
        confere(r.returncode == 0 and sorted(p.name for p in previa.iterdir()) == esperados, f'--preview exited {r.returncode} and wrote {sorted(p.name for p in previa.iterdir()) if previa.is_dir() else None}',
                f'--preview: {len(esperados)} pages, {{slug}}.claro.html and {{slug}}.escuro.html for each diagram')
        boa = []
        for f in m.FIGURAS:
            for tema in ('claro', 'escuro'):
                p = (previa / f'{m.slug_de(f.arquivo)}.{tema}.html').read_text(encoding='utf-8')
                for cond, o_que in ((f'<html lang="pt-BR" data-theme="{tema}">' in p, 'the fixed theme'), ('.dg-caixa{' in p or '.dg-caixa,' in p, 'the manual\'s CSS'),
                                    ('<div class="moldura moldura-svg"><svg ' in p, 'the frame'), ('<figcaption><b>Figura 1.</b> ' in p, 'the caption'),
                                    (p.count('<svg ') == 1 and '__FONT__' not in p, 'one SVG')):
                    if not cond:
                        boa.append(f'{f.arquivo} {tema}: lacks {o_que}')
        confere(not boa, f'a preview page lacks: {boa[:3]}', '--preview pages: data-theme fixed, the manual\'s CSS, the same frame, the caption, one SVG')
    print(f'test_manual_diagramas: {CONTA[0]} controls, {len(FALHAS)} failed' + (f', {len(PULADOS)} skipped ({"; ".join(PULADOS)})' if PULADOS else ''))
    return 1 if FALHAS else 0


if __name__ == '__main__':
    sys.exit(main())
