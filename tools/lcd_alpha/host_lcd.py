#!/usr/bin/env python3
"""host_lcd.py -- the alpha's 16x2 LCD drawn on the host, from the alpha's own code (2026-11-04).

What the alpha's LCD shows is composed by src/display/AlphaFrame.h (alphaLoadLinkGlyph( ) and alphaDrawFrame( ), templates on the LCD type), and a host
program can run them against test/native_stubs/FakeHd44780.h, the fake the native cases use: it keeps the DDRAM at each blit( ) and the CGRAM at each
createChar( ). tools/lcd_alpha/amostras.cpp is that program, with one SAMPLE for each of the manual's 17 LCD figures, named after it: an AlphaFrameIn (and
the frames before it, where the screen does not clear) that is a state the loop really reaches. It prints each sample's glass, the two DDRAM rows and the
eight CGRAM slots, as the JSON tools/lcd_alpha/lcd.py reads. This script builds it, runs it and draws every frame with lcd.py (ROM A00, 639 x 137).
The headers are included from src/, never copied: a change to AlphaFrame.h, to BigFont_HD44780.h or to PendingLabel.h moves the frames at the next run.

Both network variants are built, as the native cases build them: the radio's (the default) and the cable's, -DSIMUT_NET_ENC28J60=1 (net/NetCaps.h derives
SIMUT_NET_LAN from it; a -DSIMUT_NET_LAN=1 is an #error there). The cable differs in four places and the cable's expectations name them: the corner cell of
a reading is a space and the cable's glyph (never "W", never "AP"), the offline text says "Sem rede", slot 7 holds the cable or the X, and there is no
setup-network screen, so the cable's program has no AP sample.

  python3 tools/lcd_alpha/host_lcd.py --out DIR [--rede wifi|cabo] [--versao X.Y.Z]     draw every sample into DIR: <sample>.png and <sample>.json
  python3 tools/lcd_alpha/host_lcd.py --selftest                                       the checks below; exit 0 when all hold, 1 when one fails

Exit 2 when it cannot run, with what is missing: g++ absent, the build fails, Pillow absent (drawing needs it).

--selftest holds, on a version nobody ships (9.8.7, so the banner is shown to be data and not SIMUT_VERSION):
  * the glass: for each of the 17 samples, both rows and the CGRAM slots against what the figure's captura= says (docs/_manual/cap04, cap06, cap12), written
    out here by hand, cell by cell, and not taken from a run: the rows of the splash, the boot bar at 7 of 14, "Conectado!" over an IP of the documentation
    range, "Sem WiFi"/"Offline", 23.4 / 58 / 1013.2 hPa / -18.5, ERRO, 37 and 2k pending, the three pages of the setup network; slots 0-6 the big-digit
    pieces and slot 7 the five-bar icon (or the X), with the bitmaps READ from BigFont_HD44780.h;
  * the cable: the four differences above, and every other cell as on the radio;
  * the sources the harness leans on: the loop still builds its banner as "SIMUT " and SIMUT_VERSION; the demonstration key is one the firmware could
    generate (ApPsk.h's alphabet and length); the address is in 192.0.2.0/24 (RFC 5737); every frame draws (a code with no glyph is an error);
  * the checker bites: an expectation changed in one cell, a frame changed in one cell or one CGRAM bit, a sample missing and a sample extra give
    exactly one finding each;
  * the program bites: AlphaFrame.h is copied, edited at one anchor and built again, and the samples that change must be exactly the ones the edit is
    about: the degree sign a column to the left (the four temperatures), the slot label from the third slot on and from the first (the 3 and the 4 samples
    with that many active slots), " Sem WiFi" and " Sem rede" misspelled (offline), " Conectado!" without its bang (the two samples that print it), the
    boot bar's brackets, the first page's title. An anchor that is not found the expected number of times, or a mutant that does not build, fails the run:
    a mutant must be killed by a check and not by the compiler.
"""
import argparse
import concurrent.futures
import ipaddress
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))        # explicit: python3 -I leaves the script's own folder off the path (lcd.py, rom_hd44780.py)
SRC = ROOT / 'src'
STUBS = ROOT / 'test' / 'native_stubs'
AMOSTRAS_CPP = HERE / 'amostras.cpp'
ALFA = SRC / 'display' / 'AlphaFrame.h'
BIGFONT = SRC / 'display' / 'BigFont_HD44780.h'
LOOP = SRC / 'DisplayManager_Alpha.cpp'
APPSK = SRC / 'ApPsk.h'
LIMITES = SRC / 'SystemDefs_Limits.h'

REDES = {'wifi': (), 'cabo': ('-DSIMUT_NET_ENC28J60=1',)}
VERSAO_DO_TESTE = '9.8.7'

# CGRAM slots 0-6 as BigFontSlot numbers them, and the codes a row writes for them; written out here and not read from the enum, so a slot that moved breaks a check.
PECAS = ('BF_LT', 'BF_UB', 'BF_RT', 'BF_LL', 'BF_LB', 'BF_LR', 'BF_UMB')
LT, UB, RT, LL, LB, LR, UMB, LINK = (chr(i) for i in range(8))


class Falta(Exception):
    """The harness cannot run: no g++, the build failed, the program failed, or something it needs is missing. Exit 2."""


# ---- building and running the samples --------------------------------------------------------------------------------------------------------------
def compilador():
    g = shutil.which('g++')
    if not g:
        raise Falta('g++ not found: the samples are built with it (apt install g++, or the C++17 compiler of your system)')
    return g


def construir(rede, pasta, mutante=None):
    """Builds amostras.cpp for `rede` (wifi or cabo) into pasta and returns the executable. `mutante` is a folder holding display/AlphaFrame.h, an edited copy that
    the include path finds before src/display/AlphaFrame.h (its own relative includes fall back on -I src/display)."""
    exe = pathlib.Path(pasta) / f'amostras_{rede}'
    # -U_FORTIFY_SOURCE: Ubuntu's g++ turns it on from -O1, and glibc 2.38 and later then define strlcpy( ) in string.h, which test/native_stubs/Arduino.h
    # declares inline again ("redeclared inline without gnu_inline"); the firmware's libc has no such definition and the stub is not edited here.
    cmd = [compilador(), '-std=gnu++17', '-O1', '-U_FORTIFY_SOURCE', '-Wall', '-Wextra']
    if mutante is not None:
        cmd.append(f'-I{mutante}')
    cmd += [f'-I{SRC}', f'-I{SRC / "display"}', f'-I{STUBS}', *REDES[rede], str(AMOSTRAS_CPP), '-o', str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise Falta(f'g++ could not build {AMOSTRAS_CPP.name} ({rede}{", with a mutant" if mutante else ""}):\n' + r.stderr.strip()[-2500:])
    return exe


def executar(exe, versao):
    """Runs the program: (the network it says it was built for, {sample: (the two rows as bytes, the eight CGRAM slots as bytes)})."""
    if not re.fullmatch(r'[0-9A-Za-z.+_-]{1,10}', versao):
        raise Falta(f'the version {versao!r} is not 1 to 10 of [0-9A-Za-z.+_-]: it has to fit the banner of row 0')
    r = subprocess.run([str(exe), versao], capture_output=True, text=True, timeout=60)
    if r.returncode:
        raise Falta(f'{pathlib.Path(exe).name} exited {r.returncode}: {r.stderr.strip()}')
    try:
        dados = json.loads(r.stdout)
        amostras = {nome: (tuple(bytes.fromhex(h) for h in q['linhas']), [bytes.fromhex(h) for h in q['cgram']]) for nome, q in dados['amostras'].items()}
        rede = dados['rede']
    except (ValueError, KeyError, TypeError, AttributeError) as e:
        raise Falta(f'{pathlib.Path(exe).name} printed something that is not the frames: {e}')
    for nome, (linhas, cgram) in amostras.items():
        if len(linhas) != 2 or any(len(l) != 16 for l in linhas) or len(cgram) != 8 or any(len(s) != 8 for s in cgram):
            raise Falta(f'sample {nome}: the frame is not two rows of 16 cells and eight slots of 8 bytes')
    return rede, amostras


def quadros(rede, versao, mutante=None):
    """Builds and runs the samples of one network: {sample: (rows, cgram)}."""
    with tempfile.TemporaryDirectory(prefix='host_lcd_') as tmp:
        declarada, amostras = executar(construir(rede, tmp, mutante), versao)
    if declarada != rede:
        raise Falta(f'built for {rede} and the program says {declarada}: the network switch of the build did not take')
    return amostras


def desenhar(quadro):
    """The image (639 x 137, RGB) of one sample's glass."""
    try:
        import lcd
    except ImportError as e:
        raise Falta(f'Pillow is needed to draw ({e}): pip install pillow')
    linhas, cgram = quadro
    try:
        return lcd.desenhar(linhas, cgram)
    except ValueError as e:
        raise Falta(f'a frame cannot be drawn: {e}')


def versao_do_firmware():
    m = re.search(r'#define\s+SIMUT_VERSION\s+"([^"]+)"', LIMITES.read_text(encoding='utf-8'))
    if not m:
        raise Falta(f'SIMUT_VERSION not found in {LIMITES}')
    return m.group(1)


# ---- what the figures say, by hand ------------------------------------------------------------------------------------------------------------------
def bitmaps():
    """Every `const uint8_t BF_*[8]` of BigFont_HD44780.h, read from the source (the glyph bitmaps are the firmware's, not copied here)."""
    t = BIGFONT.read_text(encoding='utf-8')
    saida = {m.group(1): bytes(int(v, 0) for v in m.group(2).replace(' ', '').split(','))
             for m in re.finditer(r'const uint8_t (BF_[A-Z0-9]+)\[8\]\s*=\s*\{([^}]*)\}', t)}
    for n in PECAS + ('BF_WIFI0', 'BF_WIFI5', 'BF_WIRED'):
        if n not in saida or len(saida[n]) != 8:
            raise Falta(f'{n} not found in {BIGFONT.name}')
    return saida


def esperado_wifi(versao):
    """{sample: (row 0, row 1, the bitmap of CGRAM slot 7)} for the radio's build, each row exactly 16 cells (a CGRAM piece is chr(0..7)), written from the figure's
    captura= text and not from a run. The digits: 2 = {UMB UMB RT / LL LB LB}, 3 = {UMB UMB RT / LB LB LR}, 4 = {LL LB RT / row 1 only its LR in the third
    cell}, 5 = {LT UMB UMB / LB LB LR}, 8 = {LT UMB RT / LL LB LR}, 1 = {RT in the middle cell / LR}, ERRO = E R R O of three cells each."""
    banner = f'SIMUT {versao}'.ljust(16)
    erro0 = f'W{LINK} {LT}{UMB}{UMB}{LT}{UB}{RT}{LT}{UB}{RT}{LT}{UB}{RT} '
    erro1 = f'   {LL}{LB}{LB}{LL}{UB}{RT}{LL}{UB}{RT}{LL}{LB}{LR} '
    temp0 = f'W{LINK}  {UMB}{UMB}{RT}{UMB}{UMB}{RT} {LL}{LB}{RT}o '

    def temp1(canto):
        return f'{canto}{LL}{LB}{LB}{LB}{LB}{LR},  {LR} C'

    e = {
        # 12-splash: "logo depois de ligar; linha de cima SIMUT <versao>, linha de baixo Inicializando"
        'splash': (banner, '  Inicializando ', 'BF_WIFI0'),
        # 12-boot: "durante o boot, com a barra pela metade: SIMUT <versao> e [#######       ]"
        'boot': (banner, '[#######       ]', 'BF_WIFI0'),
        # 12-conectado: "Conectado! e o endereco IP na linha de baixo" (an address of the documentation range)
        'conectado': (' Conectado!     ', '192.0.2.10      ', 'BF_WIFI5'),
        # 12-obtendo-ip: "associado ao roteador sem endereco ainda; Conectado! e Obtendo IP..."
        'obtendo-ip': (' Conectado!     ', '  Obtendo IP... ', 'BF_WIFI5'),
        # 12-offline: "fim do boot sem rede alcancavel; Sem WiFi e Offline"
        'offline': (' Sem WiFi       ', '    Offline     ', 'BF_WIFI0'),
        # 12-temperatura: "2 sensores ativos; temperatura do slot 0, com 23,4; W e o icone de sinal no canto de cima, S0 no canto de baixo"
        'temperatura': (temp0, temp1('S0  '), 'BF_WIFI5'),
        # 12-umidade: "DHT22 no slot 1, na vez da umidade, com 58 %; S1 no canto de baixo"
        'umidade': (f'W{LINK}     {LT}{UMB}{UMB}{LT}{UMB}{RT}   ', f'S1     {LB}{LB}{LR}{LL}{LB}{LR}%UR', 'BF_WIFI5'),
        # 12-pressao: "BME280 no slot 2, na vez da pressao, com 1013,2 hPa; S2 no canto de baixo" (plain characters, with a point)
        'pressao': (f'W{LINK} 1013.2 hPa   ', 'S2              ', 'BF_WIFI5'),
        # 12-negativa: "DS18B20 num congelador, com -18,5; um so sensor ativo e nenhum pendente" (the dash before the digits: LB in column 3)
        'negativa': (f'W{LINK} {LB} {RT} {LT}{UMB}{RT} {LT}{UMB}{UMB}o ', f'     {LR} {LL}{LB}{LR},{LB}{LB}{LR} C', 'BF_WIFI5'),
        # 12-sem-sensor: "sem nenhum slot ativo, conectada ao Wi-Fi; ERRO em letras grandes e W com o icone de sinal"
        'sem-sensor': (erro0, erro1, 'BF_WIFI5'),
        # 06-lcd-erro: "1 sensor DHT22 ativo e desligado do pino; o LCD mostra ERRO em letras grandes" (one slot, none pending: the corner is free)
        'lcd-erro': (erro0, erro1, 'BF_WIFI5'),
        # 12-pendentes: "1 sensor ativo; 37 registros pendentes; 37 no canto de baixo a esquerda"
        'pendentes': (temp0, temp1('37  '), 'BF_WIFI5'),
        # 12-pendentes-k: "1 sensor ativo; 2.500 registros pendentes; 2k no canto de baixo"
        'pendentes-k': (temp0, temp1('2k  '), 'BF_WIFI5'),
        # 12-ap-1/2/3: the three pages, "Modo AP  1 de 3" with 192.168.4.1, "Rede:    2 de 3" with simut_SETUP, "Senha:   3 de 3" with a demonstration key
        'ap-1': ('Modo AP  1 de 3 ', '192.168.4.1     ', 'BF_WIFI0'),
        'ap-2': ('Rede:    2 de 3 ', 'simut_SETUP     ', 'BF_WIFI0'),
        'ap-3': ('Senha:   3 de 3 ', 'K7PX4MRW2A      ', 'BF_WIFI0'),
        # 04-lcd-ap: "pagina Rede: 2 de 3 com simut_SETUP na linha de baixo" (the same page as 12-ap-2)
        'lcd-ap': ('Rede:    2 de 3 ', 'simut_SETUP     ', 'BF_WIFI0'),
    }
    for nome, (r0, r1, _) in e.items():
        assert len(r0) == 16 and len(r1) == 16, f'{nome}: a row of the LCD is exactly sixteen cells'
    return e


LEITURAS = ('temperatura', 'umidade', 'pressao', 'negativa', 'sem-sensor', 'lcd-erro', 'pendentes', 'pendentes-k')   # the samples that draw the sensor screen
PAGINAS_DO_AP = ('ap-1', 'ap-2', 'ap-3', 'lcd-ap')


def esperado_cabo(w):
    """The cable's expectations from the radio's by the four differences, and nothing else: (1) the corner cell of a reading is a space and the link glyph, no "W"
    (and no "AP"); (2) offline says "Sem rede"; (3) slot 7 holds the cable's glyph where the radio held the five bars (link up), the X where it held the X;
    (4) there is no setup-network screen, so no AP sample."""
    c = {}
    for nome, (r0, r1, g7) in w.items():
        if nome in PAGINAS_DO_AP:
            continue
        if nome in LEITURAS:
            assert r0[0] == 'W'
            r0 = ' ' + r0[1:]
        if nome == 'offline':
            r0 = ' Sem rede       '
        c[nome] = (r0, r1, 'BF_WIRED' if g7 == 'BF_WIFI5' else g7)
    return c


def mostra(b):
    """Cells as text for a message: printable ASCII as it is, anything else as <n> (the CGRAM pieces 0-7)."""
    return ''.join(chr(c) if 0x20 <= c < 0x7F else f'<{c}>' for c in b)


def conferir(amostras, esperado, bm):
    """One line for each way `amostras` differs from `esperado`: a sample missing or not expected, a row that is not what the figure says, a CGRAM slot that is
    not the piece (slots 0-6) or the link glyph (slot 7) expected."""
    achados = []
    for nome in sorted(set(esperado) - set(amostras)):
        achados.append(f'{nome}: the program prints no such sample')
    for nome in sorted(set(amostras) - set(esperado)):
        achados.append(f'{nome}: a sample no figure of the manual names')
    for nome in sorted(set(esperado) & set(amostras)):
        linhas, cgram = amostras[nome]
        r0, r1, g7 = esperado[nome]
        for i, (obtido, quer) in enumerate(zip(linhas, (r0, r1))):
            quer = quer.encode('latin-1')
            if obtido != quer:
                achados.append(f'{nome}: row {i} is |{mostra(obtido)}| and the figure says |{mostra(quer)}|')
        for s, bitmap in enumerate(PECAS + (g7,)):
            if cgram[s] != bm[bitmap]:
                achados.append(f'{nome}: CGRAM slot {s} is not {bitmap}')
    return achados


# (name, build, the text of AlphaFrame.h, what replaces it, how many times the text stands there, the samples that must change and no others)
MUTANTES = (
    ('the degree sign a column to the left', 'wifi', "lcd.setCursor(14, 0); lcd.write('o');", "lcd.setCursor(13, 0); lcd.write('o');", 1,
     {'temperatura', 'negativa', 'pendentes', 'pendentes-k'}),
    ('the slot label from the third slot on', 'wifi', 'if (in.slots > 1) {', 'if (in.slots > 2) {', 1,
     {'temperatura', 'umidade', 'pressao'}),
    ('the slot label from the first slot on', 'wifi', 'if (in.slots > 1) {', 'if (in.slots >= 1) {', 1,
     {'negativa', 'lcd-erro', 'pendentes', 'pendentes-k'}),
    ('Sem WiFi misspelled', 'wifi', 'lcd.print(" Sem WiFi       ");', 'lcd.print(" Sem Wifi       ");', 1, {'offline'}),
    ('Sem rede misspelled (the cable)', 'cabo', 'lcd.print(" Sem rede       ");', 'lcd.print(" Sem Rede       ");', 1, {'offline'}),
    ('Conectado! without its bang', 'wifi', 'lcd.print(" Conectado!     ");', 'lcd.print(" Conectado      ");', 2, {'conectado', 'obtendo-ip'}),
    ('the boot bar without its closing bracket', 'wifi', "lcd.write(']');\n\t\tbreak;\n\n\tcase ALPHA_SCREEN_BOOT_FULL:", "lcd.write('|');\n\t\tbreak;\n\n\tcase ALPHA_SCREEN_BOOT_FULL:", 1,
     {'boot'}),
    ('the first page of the setup network retitled', 'wifi', 'case 0: lcd.print("Modo AP  1 de 3"); break;', 'case 0: lcd.print("Modo AP  1 de 4"); break;', 1, {'ap-1'}),
)


def mutante_em(pasta, antigo, novo, vezes):
    """A copy of AlphaFrame.h with `antigo` replaced, in pasta/display/AlphaFrame.h; the anchor has to stand exactly `vezes` times."""
    t = ALFA.read_text(encoding='utf-8')
    if t.count(antigo) != vezes:
        raise Falta(f'the mutant anchor {antigo!r} stands {t.count(antigo)} times in {ALFA.name}, expected {vezes}: the mutant would test nothing')
    d = pathlib.Path(pasta) / 'display'
    d.mkdir(parents=True)
    (d / 'AlphaFrame.h').write_text(t.replace(antigo, novo), encoding='utf-8')
    return pathlib.Path(pasta)


def mudaram(a, b):
    return {nome for nome in set(a) | set(b) if a.get(nome) != b.get(nome)}


# ---- the self-test ------------------------------------------------------------------------------------------------------------------------------------
CONTA = [0]
FALHAS = []


def confere(cond, msg, ok=None):
    CONTA[0] += 1
    if not cond:
        FALHAS.append(msg)
        print('FAIL', msg)
    elif ok:
        print('ok  ', ok)
    return bool(cond)


def fontes_ok(w):
    """What the harness leans on in the sources, read at every run."""
    banner = re.search(r'^#define\s+ALPHA_BANNER\s+"SIMUT "\s+SIMUT_VERSION\s*$', LOOP.read_text(encoding='utf-8'), re.M)
    confere(banner, f'{LOOP.name} no longer defines ALPHA_BANNER as "SIMUT " SIMUT_VERSION: the banner the splash and boot samples build is not the loop\'s',
            'the loop builds its banner as "SIMUT " and SIMUT_VERSION')
    t = APPSK.read_text(encoding='utf-8')
    alfabeto = re.search(r'#define\s+AP_PSK_ALPHABET\s+"([^"]+)"', t)
    tamanho = re.search(r'#define\s+AP_PSK_LEN\s+(\d+)', t)
    chave = w['ap-3'][1].strip()
    confere(alfabeto and tamanho and len(chave) == int(tamanho.group(1)) and set(chave) <= set(alfabeto.group(1)),
            f'the demonstration key {chave!r} is not one ApPsk.h could generate', 'the demonstration key is 10 characters of ApPsk.h\'s alphabet')
    ip = w['conectado'][1].strip()
    confere(ipaddress.ip_address(ip) in ipaddress.ip_network('192.0.2.0/24'), f'{ip} is not in 192.0.2.0/24 (RFC 5737)', f'{ip} is in the documentation range 192.0.2.0/24')
    confere(w['ap-2'][1].strip() == 'simut_SETUP' and w['ap-1'][1].strip() == '192.168.4.1',
            'the network pages do not say simut_SETUP and 192.168.4.1', 'the setup network is simut_SETUP at 192.168.4.1')


def selftest():
    versao = VERSAO_DO_TESTE
    bm = bitmaps()
    w = esperado_wifi(versao)
    c = esperado_cabo(w)
    fontes_ok(w)
    nucleos = max(2, min(8, os.cpu_count() or 2))
    with concurrent.futures.ThreadPoolExecutor(max_workers=nucleos) as ex, tempfile.TemporaryDirectory(prefix='host_lcd_self_') as tmp:
        tmp = pathlib.Path(tmp)

        def roda(chave, rede, mutante=None):
            pasta = tmp / chave
            pasta.mkdir()
            declarada, amostras = executar(construir(rede, pasta, mutante), versao)
            if declarada != rede:
                raise Falta(f'{chave}: built for {rede} and the program says {declarada}')
            return amostras

        base = {rede: ex.submit(roda, rede, rede) for rede in REDES}
        muts = []
        for i, (nome, rede, antigo, novo, vezes, _) in enumerate(MUTANTES):
            muts.append(ex.submit(roda, f'm{i}', rede, mutante_em(tmp / f'src{i}', antigo, novo, vezes)))
        amostras = {rede: f.result() for rede, f in base.items()}
        resultados = [f.result() for f in muts]

    # the glass of the radio and of the cable
    for rede, quer in (('wifi', w), ('cabo', c)):
        achados = conferir(amostras[rede], quer, bm)
        for a in achados:
            confere(False, f'{rede}: {a}')
        confere(not achados, 'unreachable', f'{rede}: {len(quer)} samples, both rows and the eight CGRAM slots as the figures say')
    confere(len(w) == 17, f'the radio\'s table holds {len(w)} samples, the manual has 17 LCD figures')
    confere(set(amostras['cabo']) == set(amostras['wifi']) - set(PAGINAS_DO_AP), 'the cable\'s samples are not the radio\'s without the AP pages',
            'the cable has no setup-network sample')

    # every frame draws: no code without a glyph, and the size of the figures
    try:
        for rede in REDES:
            for nome, q in amostras[rede].items():
                tam = desenhar(q).size
                confere(tam == (639, 137), f'{rede}/{nome} draws {tam}, not 639 x 137')
        confere(True, '', f'every frame of both builds draws at 639 x 137 (ROM A00 has the glyph of every code)')
    except Falta as e:
        confere(False, f'drawing: {e}')

    # the checker bites
    limpo = len(conferir(amostras['wifi'], w, bm))   # 0 when the run is clean: a control adds exactly one finding to what is there

    def uma(nome, achados):
        confere(len(achados) == limpo + 1, f'control "{nome}" gave {len(achados)} findings, not {limpo} + 1: {achados}', f'control "{nome}": exactly one finding')

    e1 = dict(w); r0, r1, g = e1['conectado']; e1['conectado'] = (r0, r1.replace('10', '11'), g)
    uma('an expected cell changed', conferir(amostras['wifi'], e1, bm))
    a1 = dict(amostras['wifi']); l, cg = a1['pressao']; a1['pressao'] = ((l[0][:5] + b'X' + l[0][6:], l[1]), cg)
    uma('a frame cell changed', conferir(a1, w, bm))
    a2 = dict(amostras['wifi']); l, cg = a2['temperatura']; cg = list(cg); cg[3] = bytes([cg[3][0] ^ 1]) + cg[3][1:]; a2['temperatura'] = (l, cg)
    uma('a CGRAM bit changed', conferir(a2, w, bm))
    a3 = dict(amostras['wifi']); del a3['offline']
    uma('a sample missing', conferir(a3, w, bm))
    a4 = dict(amostras['wifi']); a4['extra'] = a4['offline']
    uma('a sample extra', conferir(a4, w, bm))
    e2 = dict(w); r0, r1, g = e2['umidade']; e2['umidade'] = (r0, r1, 'BF_WIFI0')
    uma('a link glyph other than the one expected', conferir(amostras['wifi'], e2, bm))

    # the program bites
    for (nome, rede, antigo, novo, vezes, quer), obtido in zip(MUTANTES, resultados):
        mudou = mudaram(amostras[rede], obtido)
        confere(mudou == quer, f'mutant "{nome}" changed {sorted(mudou)}, expected exactly {sorted(quer)}',
                f'mutant "{nome}" changes {len(mudou)} sample(s), exactly {sorted(mudou)}')
        confere(len(mudou) > 0, f'mutant "{nome}" changed nothing')
    print(f'host_lcd: selftest {CONTA[0]} checks, {len(FALHAS)} failed')
    return 1 if FALHAS else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', help='draw every sample here: <sample>.png (639 x 137) and <sample>.json')
    ap.add_argument('--rede', choices=sorted(REDES), default='wifi', help='the network variant to build (default wifi; the cable has no AP samples)')
    ap.add_argument('--versao', help='the version on the splash and the boot screens (default SIMUT_VERSION of src/SystemDefs_Limits.h)')
    ap.add_argument('--selftest', action='store_true', help='run the checks and the mutants (exit 0 when all hold)')
    a = ap.parse_args()
    try:
        if a.selftest:
            return selftest()
        if not a.out:
            ap.error('--out DIR or --selftest')
        versao = a.versao or versao_do_firmware()
        pasta = pathlib.Path(a.out)
        pasta.mkdir(parents=True, exist_ok=True)
        amostras = quadros(a.rede, versao)
        for nome, (linhas, cgram) in sorted(amostras.items()):
            desenhar((linhas, cgram)).save(pasta / f'{nome}.png')
            (pasta / f'{nome}.json').write_text(json.dumps({'linhas': [l.hex() for l in linhas], 'cgram': [s.hex() for s in cgram]}), encoding='utf-8')
        print(f'host_lcd: {len(amostras)} samples ({a.rede}, version {versao}) drawn into {pasta}')
        return 0
    except Falta as e:
        print(f'host_lcd: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
