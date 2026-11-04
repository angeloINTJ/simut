#!/usr/bin/env python3
"""Draws, checks and lists the 17 LCD figures of the manual (the alpha's 16x2): docs/images/manual/04-lcd-ap.png, 06-lcd-erro.png and 12-*.png.

The figures are not photographs. Each is a frame that tools/lcd_alpha/host_lcd.py makes on the host from the code at the commit (src/display/AlphaFrame.h
and BigFont_HD44780.h compiled with g++ against the fake LCD of the native cases, one sample for each figure in tools/lcd_alpha/amostras.cpp) and draws with
tools/lcd_alpha/lcd.py: ROM A00 glyphs read from the HD44780U datasheet, a blue module with white dots, 639 x 137, every dot a square of whole pixels (no
resizing, no smoothing). Where the glass keeps cells of the frame before it (the boot) the sample runs the frames that really come before it.

  --write      draws the frames and writes the figures into --images (default docs/images/manual).
  --check      draws them again from the code at HEAD and compares the PIXELS with the committed files, never the PNG bytes (those depend on Pillow's and zlib's
               versions): one line for each figure that differs, is missing, is not a readable 639 x 137 RGB PNG or whose sample the program no longer prints;
               for each 04-lcd-*.png, 06-lcd-*.png and 12-*.png that no row of FIGURAS names (the figures of other kinds that the chapters declare, such as
               12-wifi-niveis.png, are not this tool's); for each row whose figure block is gone from the chapters or has another file or kind; for each block of
               tipo="lcd" that no row names; for each block whose captura= is not the text --list prints; and for each "SIMUT x.y.z" in chapter 12 that is not
               the version the figures show. Exit 0 clean, 1 with findings, 2 when it cannot draw.
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
    from PIL import Image
except ImportError:   # --list needs no Pillow
    Image = None


class Figura(NamedTuple):
    bloco: str      # the id of the figure block in the chapter: ::: {.figura #fig-12-splash ...}
    arquivo: str    # docs/images/manual/<arquivo>
    capitulo: str   # the chapter of docs/_manual that holds the block
    amostra: str    # the sample of tools/lcd_alpha/amostras.cpp (the radio's build)
    nota: str       # the state the glass is in: the end of the captura= text


# One row per block of tipo="lcd", in the order of the chapters. The radio's build draws them all: the manual's LCD figures are the alpha's (Wi-Fi).
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
)


def captura(f):
    """The captura= text of the figure's block in the chapter."""
    return (f'desenhada pelo código do alpha no host (tools/lcd_alpha), amostra {f.amostra}, ROM A00, {LARGURA} x {ALTURA}; '
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
        elif (b.tipo, b.arquivo, b.capitulo) != ('lcd', f.arquivo, f.capitulo):
            achados.append(f'{f.bloco}: the block says tipo={b.tipo!r}, arquivo={b.arquivo!r} in {b.capitulo}, the row says tipo=\'lcd\', arquivo={f.arquivo!r} in {f.capitulo}')
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
    """({file: its image}, [the rows whose sample the program does not print]), from the code at HEAD (or from an edited copy of AlphaFrame.h in `mutante`).
    Raises host_lcd.Falta when it cannot draw."""
    import host_lcd
    amostras = host_lcd.quadros('wifi', VERSAO, mutante)
    imagens, sem = {}, []
    for f in FIGURAS:
        if f.amostra in amostras:
            imagens[f.arquivo] = host_lcd.desenhar(amostras[f.amostra])
        else:
            sem.append(f)
    return imagens, sem


def conferir_imagens(imagens, desenhadas, sem, capitulos):
    """One line for each figure that differs from the code, is missing or unreadable, and for each orphan."""
    achados = []
    for f in FIGURAS:
        if f in sem:
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
        if img.mode != 'RGB' or img.size != (LARGURA, ALTURA):
            achados.append(f'{f.arquivo}: {img.size[0]}x{img.size[1]} {img.mode}, not {LARGURA}x{ALTURA} RGB')
            continue
        esperada = desenhadas[f.arquivo]
        if img.tobytes() != esperada.tobytes():
            n, p = diferenca(img, esperada)
            achados.append(f'{f.arquivo}: {n} of {LARGURA * ALTURA} pixels differ from the sample {f.amostra}, the first at x={p[0]} y={p[1]}')
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
