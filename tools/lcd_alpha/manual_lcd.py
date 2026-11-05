#!/usr/bin/env python3
"""Draws, checks and lists the alpha's figures of the manual: the 17 of its 16x2 LCD (docs/images/manual/04-lcd-ap.png, 06-lcd-erro.png and 12-*.png) and the
diagram of its signal icon (12-wifi-niveis.png).

The figures are not photographs. Each is a frame that tools/lcd_alpha/host_lcd.py makes on the host from the code at the commit (src/display/AlphaFrame.h
and BigFont_HD44780.h compiled with g++ against the fake LCD of the native cases, one sample for each figure in tools/lcd_alpha/amostras.cpp) and draws with
tools/lcd_alpha/lcd.py: ROM A00 glyphs read from the HD44780U datasheet, a blue module with white dots, 639 x 137, every dot a square of whole pixels (no
resizing, no smoothing). Where the glass keeps cells of the frame before it (the boot) the sample runs the frames that really come before it. The diagram
puts side by side the six drawings of the link glyph, enlarged: on top the ones the same program loads into CGRAM slot 7 at the lower end of each band of
chapter 12's table, below the ones v2.7.1 drew (WIFI_V271, kept here: no code at HEAD draws them), labelled in the ROM's own glyphs.

  --write      draws the frames and writes the figures into --images (default docs/images/manual).
  --check      draws them again from the code at HEAD and compares the PIXELS with the committed files, never the PNG bytes (those depend on Pillow's and zlib's
               versions): one line for each figure that differs, is missing, is not a readable RGB PNG of its size (639 x 137, the diagram 639 x 271) or whose
               sample the program no longer prints; for each 04-lcd-*.png, 06-lcd-*.png and 12-*.png that no row of FIGURAS names (the figures of other kinds
               that the chapters declare are not this tool's); for each row whose figure block is gone from the chapters or has another file or kind; for each
               block of tipo="lcd" that no row names; for each block whose captura= is not the text --list prints; and for each "SIMUT x.y.z" in chapter 12
               that is not the version the figures show. Exit 0 clean, 1 with findings, 2 when it cannot draw.
  --list       the table: the file, the sample and the captura= text of the figure block in the chapter, tab-separated.
  --images     the folder of the figures (the controls point it at a scratch copy).
  --capitulos  the folder of the chapters, docs/_manual (the controls point it at a scratch copy).

The version the splash and the boot figures show is VERSAO, named below, and not SIMUT_VERSION: the loop builds that banner from SIMUT_VERSION and AlphaFrameIn takes
it as data, so a release does not move a figure. When the figures are to show a newer version, change VERSAO, run --write and the chapter text that --check names.

The captura= text says what the figure is: "desenhada pelo código do alpha no host (tools/lcd_alpha)", then the sample and how to draw it again, then the state the
glass is in. It never says the frame was captured from a device. Needs g++ and Pillow for --write and --check (exit 2 names what is missing); --list needs neither.
tools/lcd_alpha/test_manual_lcd.py holds the controls of --check.
"""
import argparse
import pathlib
import re
import sys
from typing import NamedTuple

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))        # explicit: python3 -I leaves the script's own folder off the path (host_lcd.py, lcd.py)
IMAGENS = ROOT / 'docs' / 'images' / 'manual'
CAPITULOS = ROOT / 'docs' / '_manual'
LARGURA, ALTURA = 639, 137           # lcd.tamanho( ) of the default style: about the 640 px of the panel's figures
PADROES = ('04-lcd-*.png', '06-lcd-*.png', '12-*.png')   # the names this tool gives: a file of these that no row names is an orphan
VERSAO = '2.13.0'                    # the version the splash and the boot figures show (the tip of main on 2026-11-04 had SIMUT_VERSION 2.13.0)
CAPITULO_DA_VERSAO = 'cap12-lcd-alpha.md'

try:
    from PIL import Image, ImageDraw
except ImportError:   # --list needs no Pillow
    Image = ImageDraw = None


class Figura(NamedTuple):
    bloco: str      # the id of the figure block in the chapter: ::: {.figura #fig-12-splash ...}
    arquivo: str    # docs/images/manual/<arquivo>
    capitulo: str   # the chapter of docs/_manual that holds the block
    amostra: str    # the sample of tools/lcd_alpha/amostras.cpp (the radio's build); the diagram's is icones, the link glyphs the program prints
    nota: str       # the state the glass is in: the end of the captura= text
    tipo: str = 'lcd'   # the tipo= of the block: lcd for a glass, diagrama for the signal icon's figure


# One row per block of tipo="lcd", in the order of the chapters, and the diagram of the signal icon. The radio's build draws them all: the manual's LCD
# figures are the alpha's (Wi-Fi).
FIGURAS = (
    Figura('fig-04-lcd-ap', '04-lcd-ap.png', 'cap04-primeiro-boot.md', 'lcd-ap',
           'alpha recém-gravada, sem rede configurada; ponto de acesso aberto pelo comando ap no console USB, com o aparelho já nas leituras; página Rede: 2 de 3 com simut_SETUP na linha de baixo; é o mesmo quadro de 12-ap-2'),
    Figura('fig-06-lcd-erro', '06-lcd-erro.png', 'cap06-sensores.md', 'lcd-erro',
           'imagem alpha; 1 sensor DHT22 ativo e desligado do pino; o LCD mostra ERRO em letras grandes; é o mesmo quadro de 12-sem-sensor, porque com um só sensor e nenhum pendente o canto de baixo fica livre'),
    Figura('fig-12-splash', '12-splash.png', 'cap12-lcd-alpha.md', 'splash',
           f'logo depois de ligar; linha de cima SIMUT {VERSAO}, linha de baixo Inicializando; a versão é a da tabela FIGURAS de tools/lcd_alpha/manual_lcd.py, não a do firmware'),
    Figura('fig-12-boot', '12-boot.png', 'cap12-lcd-alpha.md', 'boot',
           f'durante o boot, com a barra pela metade: SIMUT {VERSAO} e [#######       ]; a versão é a da tabela FIGURAS de tools/lcd_alpha/manual_lcd.py, não a do firmware'),
    Figura('fig-12-conectado', '12-conectado.png', 'cap12-lcd-alpha.md', 'conectado',
           'fim do boot, na rede, com IP; Conectado! e o endereço IP na linha de baixo, 192.0.2.10, da faixa de documentação (RFC 5737)'),
    Figura('fig-12-obtendo-ip', '12-obtendo-ip.png', 'cap12-lcd-alpha.md', 'obtendo-ip',
           'fim do boot, associado ao roteador sem endereço ainda; Conectado! e Obtendo IP...'),
    Figura('fig-12-offline', '12-offline.png', 'cap12-lcd-alpha.md', 'offline',
           'fim do boot sem rede alcançável; Sem WiFi e Offline'),
    Figura('fig-12-temperatura', '12-temperatura.png', 'cap12-lcd-alpha.md', 'temperatura',
           '2 sensores ativos; na vez da temperatura do slot 0, com 23,4 °C; W e o ícone de sinal, de cinco barras, no canto de cima, S0 no canto de baixo'),
    Figura('fig-12-umidade', '12-umidade.png', 'cap12-lcd-alpha.md', 'umidade',
           'DHT22 no slot 1, na vez da umidade, com 58 %; S1 no canto de baixo'),
    Figura('fig-12-pressao', '12-pressao.png', 'cap12-lcd-alpha.md', 'pressao',
           'BME280 no slot 2, na vez da pressão, com 1013.2 hPa, com ponto decimal como o LCD escreve; S2 no canto de baixo'),
    Figura('fig-12-negativa', '12-negativa.png', 'cap12-lcd-alpha.md', 'negativa',
           'DS18B20 num congelador, com -18,5 °C; um só sensor ativo e nenhum pendente'),
    Figura('fig-12-sem-sensor', '12-sem-sensor.png', 'cap12-lcd-alpha.md', 'sem-sensor',
           'imagem alpha sem nenhum slot ativo, conectada ao Wi-Fi; ERRO em letras grandes e W com o ícone de sinal'),
    Figura('fig-12-pendentes', '12-pendentes.png', 'cap12-lcd-alpha.md', 'pendentes',
           '1 sensor ativo; telemetria ligada com o coletor fora do ar, 37 registros pendentes; 37 no canto de baixo à esquerda'),
    Figura('fig-12-pendentes-k', '12-pendentes-k.png', 'cap12-lcd-alpha.md', 'pendentes-k',
           '1 sensor ativo; 2.500 registros pendentes; 2k no canto de baixo'),
    Figura('fig-12-ap-1', '12-ap-1.png', 'cap12-lcd-alpha.md', 'ap-1',
           'AP aberto em operação pelo comando ap; página Modo AP 1 de 3 com 192.168.4.1'),
    Figura('fig-12-ap-2', '12-ap-2.png', 'cap12-lcd-alpha.md', 'ap-2',
           'mesmo estado; página Rede: 2 de 3 com simut_SETUP'),
    Figura('fig-12-ap-3', '12-ap-3.png', 'cap12-lcd-alpha.md', 'ap-3',
           'mesmo estado; página Senha: 3 de 3 com uma chave de demonstração, de 10 caracteres do alfabeto das chaves e de nenhum aparelho'),
    Figura('fig-12-wifi-niveis', '12-wifi-niveis.png', 'cap12-lcd-alpha.md', 'icones',
           'os seis desenhos do ícone de sinal, ampliados, em grade de 5 x 8 pontos, lado a lado: X, 1, 2, 3, 4 e 5 barras; em cima, a escada da v2.7.2, '
           'que o código põe no slot 7 em -127, -90, -80, -70, -60 e -50 dBm; embaixo, as linhas da v2.7.1, guardadas em tools/lcd_alpha/manual_lcd.py '
           'a partir da tag v2.7.1', 'diagrama'),
)
TIPOS = ('lcd', 'diagrama')

# ---- the diagram of the signal icon ----------------------------------------------------------------------------------------------------------------
# Six cells in two rows, the levels over them and the versions beside them, on the glass's blue, every dot a square of whole pixels as lcd.py draws.
NIVEIS_RSSI = (-127, -90, -80, -70, -60, -50)   # X, 1, 2, 3, 4 and 5 bars: the lower end of each band of chapter 12's table, and no link for the X
ROTULOS_NIVEIS = ('X', '1', '2', '3', '4', '5')
ROTULOS_VERSOES = ('v2.7.2', 'v2.7.1')           # the top row, the bottom row
D_PONTO, D_VAO = 10, 2      # a dot of a cell of the diagram, and the gap between two: a cell of 58 x 94
R_PONTO, R_VAO = 3, 1       # a dot of a label's character, and the gap: a character of 19 x 31, drawn without its unlit dots
R_ENTRE = 3                 # between two characters of a label
D_MARGEM = 10               # around everything, as lcd.py's margin
D_ENTRE_CELULAS = 23        # between two cells of a row
D_ROTULO = 27               # between the versions' labels and the first cell
D_CABECALHO = 12            # between the levels' labels and the first row of cells
D_ENTRE_LINHAS = 20         # between the two rows of cells

# What v2.7.1 drew for the six levels: BF_WIFI0 to BF_WIFI5 of src/display/BigFont_HD44780.h at the tag v2.7.1 (a14e7aaa8f, 2026-09-22), one more full row
# for each level, from the bottom. No code at HEAD draws them, so they are history and are kept here; test_manual_lcd.py compares them with the tag when the
# clone has it. CI's checkout is shallow and has no tags, so there the control says it was skipped and the bottom row is held by the committed PNG alone: an
# edit of these rows followed by --write passes CI and fails a local run in a full clone (the review of 2026-11-04 showed it with one dot of level 3).
WIFI_V271 = (
    (0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b00000, 0b00000),
    (0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000),
    (0b00000, 0b00000, 0b00000, 0b00000, 0b01111, 0b11111, 0b00000, 0b00000),
    (0b00000, 0b00000, 0b00000, 0b00111, 0b01111, 0b11111, 0b00000, 0b00000),
    (0b00000, 0b00000, 0b00011, 0b00111, 0b01111, 0b11111, 0b00000, 0b00000),
    (0b00000, 0b00001, 0b00011, 0b00111, 0b01111, 0b11111, 0b00000, 0b00000),
)


def _vao(ponto, vao, n):
    """n dots of `ponto` px with `vao` px between two."""
    return n * ponto + (n - 1) * vao


def geometria():
    """The diagram's layout in whole pixels, a formula of the constants above: the size of a cell (cw, ch) and of a label's character (lw, lh), where the
    cells start (x_celulas, y_linhas), and the image (largura, altura). The drawing and the captura= text both read it."""
    cw, ch = _vao(D_PONTO, D_VAO, 5), _vao(D_PONTO, D_VAO, 8)
    lw, lh = _vao(R_PONTO, R_VAO, 5), _vao(R_PONTO, R_VAO, 8)
    n = max(len(r) for r in ROTULOS_VERSOES)
    x_celulas = D_MARGEM + n * lw + (n - 1) * R_ENTRE + D_ROTULO
    y_linhas = D_MARGEM + lh + D_CABECALHO
    k = len(ROTULOS_NIVEIS)
    return {'cw': cw, 'ch': ch, 'lw': lw, 'lh': lh, 'x_celulas': x_celulas, 'y_linhas': y_linhas,
            'largura': x_celulas + k * cw + (k - 1) * D_ENTRE_CELULAS + D_MARGEM,
            'altura': y_linhas + len(ROTULOS_VERSOES) * ch + (len(ROTULOS_VERSOES) - 1) * D_ENTRE_LINHAS + D_MARGEM}


def tamanho_de(f):
    """(width, height) of a row's figure, without drawing it (--list needs no Pillow)."""
    if f.tipo == 'diagrama':
        g = geometria()
        return g['largura'], g['altura']
    return LARGURA, ALTURA


def desenhar_diagrama(icones):
    """The diagram: the top row the glyphs of `icones` ({RSSI: CGRAM slot 7}, the program's) at NIVEIS_RSSI, the bottom row WIFI_V271, the labels in ROM A00."""
    import lcd
    from rom_hd44780 import ROM_A00
    g = geometria()
    celula, rotulo = lcd.Estilo(ponto=D_PONTO, vao_ponto=D_VAO), lcd.Estilo(ponto=R_PONTO, vao_ponto=R_VAO)
    img = Image.new('RGB', (g['largura'], g['altura']), lcd.Estilo().fundo)
    d = ImageDraw.Draw(img)

    def escreve(texto, x, y):
        for k, c in enumerate(texto):
            lcd.celula_em(d, rotulo, x + k * (g['lw'] + R_ENTRE), y, ROM_A00[ord(c)], apagados=False)

    for k, nivel in enumerate(ROTULOS_NIVEIS):
        escreve(nivel, g['x_celulas'] + k * (g['cw'] + D_ENTRE_CELULAS) + (g['cw'] - g['lw']) // 2, D_MARGEM)
    for n, (versao, glifos) in enumerate(zip(ROTULOS_VERSOES, (tuple(icones[r] for r in NIVEIS_RSSI), WIFI_V271))):
        y = g['y_linhas'] + n * (g['ch'] + D_ENTRE_LINHAS)
        escreve(versao, D_MARGEM, y + (g['ch'] - g['lh']) // 2)
        for k, glifo in enumerate(glifos):
            lcd.celula_em(d, celula, g['x_celulas'] + k * (g['cw'] + D_ENTRE_CELULAS), y, glifo)
    return img


def captura(f):
    """The captura= text of the figure's block in the chapter."""
    largura, altura = tamanho_de(f)
    return (f'desenhada pelo código do alpha no host (tools/lcd_alpha), amostra {f.amostra}, ROM A00, {largura} x {altura}; '
            f'refazer com python3 tools/lcd_alpha/manual_lcd.py --write; {f.nota}')


def tabela_ok():
    """A problem with the table itself, or None. A table that lets two figures share a block or a file, or a captura= that breaks the attribute, would be found by no figure."""
    for campo in ('bloco', 'arquivo'):
        valores = [getattr(f, campo) for f in FIGURAS]
        for v in valores:
            if valores.count(v) > 1:
                return f'FIGURAS names the {campo} {v!r} twice'
    for f in FIGURAS:
        if '"' in captura(f) or 'capturada no aparelho' in captura(f) or '\n' in captura(f):
            return f'FIGURAS row {f.bloco!r}: the captura= text has a double quote or a line break (it breaks the attribute) or says "capturada no aparelho" (the frame comes from the host)'
        if not any(pathlib.PurePath(f.arquivo).match(p) for p in PADROES):
            return f'FIGURAS row {f.bloco!r}: the file {f.arquivo!r} matches none of PADROES, so an orphan of that name would not be found'
        if f.tipo not in TIPOS:
            return f'FIGURAS row {f.bloco!r}: the kind {f.tipo!r} is none of {TIPOS}'
    if [f.tipo for f in FIGURAS].count('diagrama') != 1:
        return 'FIGURAS has to hold exactly one row of tipo diagrama: the signal icon\'s figure'
    return None


# ---- the chapters ----------------------------------------------------------------------------------------------------------------------------------
class Bloco(NamedTuple):
    id: str
    tipo: str
    arquivo: str
    captura: str
    capitulo: str
    linha: int


BLOCO = re.compile(r'^::: \{\.figura #(\S+)([^}\n]*)\}[ \t]*$', re.M)
ATRIBUTO = re.compile(r'(\w+)="([^"]*)"')
VERSAO_NO_TEXTO = re.compile(r'SIMUT (\d+\.\d+\.\d+)')


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
            blocos[m.group(1)] = Bloco(m.group(1), a.get('tipo', ''), a.get('arquivo', ''), a.get('captura', ''), md.name, texto.count('\n', 0, m.start()) + 1)
    return blocos


def conferir_capitulos(capitulos):
    """One line for each row whose block is gone or says another file or kind, each lcd block no row names, each captura= that is not --list's, and each
    "SIMUT x.y.z" of the version chapter that is not VERSAO."""
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
            achados.append(f'{f.bloco}: the captura= of the block ({b.capitulo}:{b.linha}) is not the text of FIGURAS (python3 tools/lcd_alpha/manual_lcd.py --list prints it)')
    for b in blocos.values():
        if b.tipo == 'lcd' and b.id not in nomeados:
            achados.append(f'{b.id}: a block of tipo="lcd" ({b.capitulo}:{b.linha}) that no row of FIGURAS in tools/lcd_alpha/manual_lcd.py names')
    md = pathlib.Path(capitulos) / CAPITULO_DA_VERSAO
    if md.is_file():
        for n, linha in enumerate(md.read_text(encoding='utf-8').splitlines(), 1):
            for m in VERSAO_NO_TEXTO.finditer(linha):
                if m.group(1) != VERSAO:
                    achados.append(f'{CAPITULO_DA_VERSAO}:{n}: says SIMUT {m.group(1)} and the figures show SIMUT {VERSAO}')
    return achados


# ---- the figures -----------------------------------------------------------------------------------------------------------------------------------
def diferenca(a, b):
    """(how many pixels differ, the first as (x, y)) between two RGB images of the same size."""
    ra, rb = a.tobytes(), b.tobytes()
    n, primeiro = 0, None
    for i in range(0, len(ra), 3):
        if ra[i:i + 3] != rb[i:i + 3]:
            n += 1
            if primeiro is None:
                primeiro = ((i // 3) % a.width, (i // 3) // a.width)
    return n, primeiro


def desenhar_figuras(mutante=None):
    """({file: its image}, [the rows whose sample the program does not print]), from the code at HEAD (or from the edited copies host_lcd.mutante_em( ) put
    in `mutante`). Raises host_lcd.Falta when it cannot draw."""
    import host_lcd
    amostras, icones = host_lcd.quadros('wifi', VERSAO, mutante)
    imagens, sem = {}, []
    for f in FIGURAS:
        if f.tipo == 'diagrama' and all(r in icones for r in NIVEIS_RSSI):
            imagens[f.arquivo] = desenhar_diagrama(icones)
        elif f.tipo == 'lcd' and f.amostra in amostras:
            imagens[f.arquivo] = host_lcd.desenhar(amostras[f.amostra])
        else:
            sem.append(f)
    return imagens, sem


def conferir_imagens(imagens, desenhadas, sem, capitulos):
    """One line for each figure that differs from the code, is missing or unreadable, and for each orphan."""
    achados = []
    for f in FIGURAS:
        if f in sem:
            if f.tipo == 'diagrama':
                achados.append(f'{f.arquivo}: the program does not print the link glyph of every RSSI of NIVEIS_RSSI {NIVEIS_RSSI} any more')
            else:
                achados.append(f'{f.arquivo}: the program does not print the sample {f.amostra} any more: it was renamed or removed')
            continue
        caminho = imagens / f.arquivo
        if not caminho.is_file():
            achados.append(f'{f.arquivo}: missing (python3 tools/lcd_alpha/manual_lcd.py --write draws it)')
            continue
        try:
            img = Image.open(caminho)
            img.load()
        except Exception as e:   # noqa: BLE001  (any decoder error is a finding, not a crash)
            achados.append(f'{f.arquivo}: not a readable PNG ({e})')
            continue
        esperada = desenhadas[f.arquivo]
        if img.mode != 'RGB' or img.size != esperada.size:
            achados.append(f'{f.arquivo}: {img.size[0]}x{img.size[1]} {img.mode}, not {esperada.size[0]}x{esperada.size[1]} RGB')
            continue
        if img.tobytes() != esperada.tobytes():
            n, p = diferenca(img, esperada)
            achados.append(f'{f.arquivo}: {n} of {esperada.size[0] * esperada.size[1]} pixels differ from the sample {f.amostra}, the first at x={p[0]} y={p[1]}')
    nomes = {f.arquivo for f in FIGURAS}
    de_outro_tipo = {b.arquivo for b in ler_blocos(capitulos).values() if b.tipo != 'lcd'}
    orfaos = sorted({p.name for padrao in PADROES for p in imagens.glob(padrao)} - nomes - de_outro_tipo)
    for nome in orfaos:
        achados.append(f'{nome}: no row of FIGURAS in tools/lcd_alpha/manual_lcd.py names it (and no chapter declares it as a figure of another kind)')
    return achados


def versao_do_firmware( ):
    """SIMUT_VERSION as src/SystemDefs_Limits.h defines it, read from the source; None when it cannot be read (the note is then skipped)."""
    try:
        t = (ROOT / 'src' / 'SystemDefs_Limits.h').read_text(encoding='utf-8')
    except OSError:
        return None
    m = re.search(r'^#define\s+SIMUT_VERSION\s+"([^"]+)"', t, re.M)
    return m.group(1) if m else None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    modo = ap.add_mutually_exclusive_group(required=True)
    modo.add_argument('--write', action='store_true', help='draw the frames and write the figures')
    modo.add_argument('--check', action='store_true', help='draw them again and compare the pixels with the committed figures, and read the chapters')
    modo.add_argument('--list', action='store_true', help='file, sample and captura= text of every figure, tab-separated')
    ap.add_argument('--images', default=str(IMAGENS), help='the folder of the figures (default docs/images/manual)')
    ap.add_argument('--capitulos', default=str(CAPITULOS), help='the folder of the chapters (default docs/_manual)')
    a = ap.parse_args(argv)
    problema = tabela_ok()
    if problema:
        print(f'manual_lcd: {problema}', file=sys.stderr)
        return 2
    if a.list:
        for f in FIGURAS:
            print('\t'.join((f.arquivo, f.amostra, captura(f))))
        return 0
    if Image is None:
        print('manual_lcd: Pillow is needed (pip install pillow)', file=sys.stderr)
        return 2
    import host_lcd
    imagens = pathlib.Path(a.images)
    try:
        desenhadas, sem = desenhar_figuras()
        if a.write:
            if sem:
                print(f'manual_lcd: the program does not print the sample of {", ".join(f.arquivo for f in sem)}', file=sys.stderr)
                return 2
            imagens.mkdir(parents=True, exist_ok=True)
            for f in FIGURAS:
                desenhadas[f.arquivo].save(imagens / f.arquivo, format='PNG', optimize=True)
            print(f'manual_lcd: wrote {len(FIGURAS)} figures to {imagens}')
            return 0
        achados = conferir_imagens(imagens, desenhadas, sem, a.capitulos) + conferir_capitulos(a.capitulos)
    except host_lcd.Falta as e:
        print(f'manual_lcd: {e}', file=sys.stderr)
        return 2
    except OSError as e:
        print(f'manual_lcd: cannot read the chapters: {e}', file=sys.stderr)
        return 2
    for linha in achados:
        print(linha)
    firmware = versao_do_firmware( )
    if firmware and firmware != VERSAO:
        # A note and not a finding: the figures are pinned on purpose, and chapter 12 says which version they show. The note keeps the gap
        # visible in the CI log after a release (the review of 2026-11-04: the cover follows SIMUT_VERSION, the figures do not).
        print(f'manual_lcd: note: the figures show SIMUT {VERSAO} and the firmware is {firmware}; to follow it, change VERSAO, run --write '
              f'and the chapter text --check names')
    print(f'manual_lcd: {len(FIGURAS)} figures drawn again from the code and compared by pixels, the chapters read, {len(achados)} finding(s)')
    return 1 if achados else 0


if __name__ == '__main__':
    sys.exit(main())
