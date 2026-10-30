#!/usr/bin/env python3
"""Draws, checks and lists the figures of chapter 11A of the manual (the Ângulo panel).

The figures docs/images/manual/11a-*.png are not captures of a device. Each is a frame that tools/tft_ui/host_telas.py draws on the host from the code at the
commit (src/ui/angulo, compiled with Adafruit GFX 1.12.6, the texts from the @ANGULO section of the pack of the figure's language), taken from the sample, mode
and language that FIGURAS names, and enlarged to 640 x 480 by repeating every pixel in a 2 x 2 block: the geometry of the 11-*.png of the original panel (21 files,
measured on 2026-10-30: 640 x 480 RGB, every 2 x 2 block uniform, no black row or column at an edge). The renderer's 4 px black margin (MARGEM in
src/ui/angulo/Angulo.h) stays in the figure: the glass has it, and cropping it would give 2 x 312 x 232 = 624 x 464, a size no existing figure has.

  --write   draws the frames and writes the figures into --images (default docs/images/manual).
  --check   draws them again from the code at HEAD and compares the PIXELS with the committed files, never the PNG bytes (those depend on Pillow's and zlib's
            versions): one line for each figure that differs, is missing, is not a readable 640 x 480 RGB PNG or whose frame the renderer no longer draws, and for
            each 11a-*.png that no row of FIGURAS names. Exit 0 clean, 1 with findings, 2 when it cannot draw.
  --list    the table: the file, the frame and the captura= text of the figure block in the chapter, tab-separated.
  --images  the folder of the figures (the controls point it at a scratch copy).
  --lng     a pack in place of the shipped one, host_telas.py's own --lng (for the controls: a changed text has to change exactly the figures that draw it).

The captura= text says what the figure is: "desenhada pelo código do painel (tools/tft_ui)", then the sample, the mode, the pack and how to draw it again. The
frames come from the host renderer and not from the board, and the text never says otherwise.

Needs g++, Adafruit GFX 1.12.6 in .pio/libdeps (host_ui.py, find_gfx( )) and Pillow. Exit 2 names what is missing.
"""
import argparse
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile
from typing import NamedTuple

try:
    from PIL import Image
except ImportError:   # --list needs no Pillow
    Image = None

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
HOST_TELAS = HERE / 'host_telas.py'
IMAGENS = ROOT / 'docs' / 'images' / 'manual'
W, H, ESCALA = 320, 240, 2          # the panel's frame, and the enlargement: 640 x 480, like the 11-*.png
GRUPOS = ('ajustes', 'base', 'graficos', 'identidade', 'menus', 'painel')   # the groups of tools/tft_ui/amostras
MODOS = ('claro', 'escuro')         # light and dark, as host_telas.py names them
PACOTES = {'pt': 'pt-BR', 'es': 'es-ES', 'en': 'inglês, sem pacote'}


class Figura(NamedTuple):
    slug: str           # docs/images/manual/11a-<slug>.png; the figure id in the chapter is fig-11a-<slug>
    grupo: str          # the group of samples: tools/tft_ui/amostras/<grupo>.cpp
    amostra: str        # the sample's name, the first argument of its registration there
    modo: str           # claro or escuro
    idioma: str = 'pt'  # host_telas.py --idioma: the pack whose @ANGULO the frame is drawn with
    nota: str = ''      # what the chapter's caption has to know about the frame, appended to captura=


# One row per figure, in the order of the chapter's sections (the plan's section 1.2, S10C_C6_PLANO.md). Light mode and the pt-BR pack unless a row says otherwise.
FIGURAS = (
    # principal: the home screen
    Figura('principal', 'painel', 'painel_dht22', 'claro'),
    Figura('minmax', 'painel', 'painel_minmax_cima', 'claro'),
    Figura('paginas', 'painel', 'painel_9_sensores_p1', 'claro', nota='nove sensores, a primeira das três páginas de botões'),
    Figura('paginas-3', 'painel', 'painel_9_sensores_p3', 'claro', nota='nove sensores, a terceira das três páginas de botões; não há amostra da segunda'),
    # grafico: loading, chart, detail, calendar
    Figura('carregando', 'graficos', 'carregando_24h', 'claro'),
    Figura('grafico', 'graficos', 'grafico_24h', 'claro'),
    Figura('detalhe', 'graficos', 'detalhe_temperatura', 'claro'),
    Figura('calendario', 'graficos', 'calendario_outubro', 'claro'),
    # menu: the settings menu, one figure per page
    Figura('menu', 'menus', 'menu_admin_p1', 'claro', nota='menu da conta administradora, página 1 de 4'),
    Figura('menu-p2', 'menus', 'menu_admin_p2', 'claro', nota='página 2 de 4'),
    Figura('menu-p3', 'menus', 'menu_admin_p3', 'claro', nota='página 3 de 4'),
    Figura('menu-p4', 'menus', 'menu_admin_p4', 'claro', nota='página 4 de 4'),
    # ajustes: one figure per screen of the settings
    Figura('alarmes', 'menus', 'alarmes_3', 'claro', nota='lista de sensores com alarme, três sensores'),
    Figura('sensor', 'menus', 'sensor_liberado', 'claro', nota='menu de um sensor'),
    Figura('limites', 'ajustes', 'limites_temperatura', 'claro', nota='edição dos limites de temperatura'),
    Figura('manutencao', 'ajustes', 'manut_aberta', 'claro', nota='manutenção em andamento'),
    Figura('sons', 'ajustes', 'sons_p1', 'claro', nota='página 1'),
    Figura('melodia', 'ajustes', 'melodia_alarme', 'claro', nota='escolha da melodia do alarme'),
    Figura('data-hora', 'ajustes', 'relogio_menu', 'claro'),
    Figura('status', 'menus', 'status_sistema', 'claro', nota='página Sistema; os valores, a versão do firmware (2.11.1) entre eles, são dados da amostra'),
    Figura('seguranca-pin', 'ajustes', 'pin_6_cartoes', 'claro'),
    Figura('calibrar', 'ajustes', 'calibrar_ponto_1', 'claro', nota='ponto 1 de 4'),
    Figura('sensibilidade', 'ajustes', 'sensibilidade_metade', 'claro'),
    Figura('alinhar', 'ajustes', 'alinhar_ajuste', 'claro'),
    Figura('ap-confirmar', 'base', 'dialogo_ap', 'claro', nota='confirmação para abrir a rede de configuração'),
    Figura('usuarios', 'identidade', 'usuarios_3_contas', 'claro'),
    # conta-pin: who is at the panel
    Figura('quem', 'identidade', 'quem_3_contas', 'claro'),
    Figura('teclado', 'identidade', 'pin_cartoes6_vazio', 'claro', nota='teclado de cartões, seis cartões, nenhum dígito digitado'),
    Figura('pin-novo', 'identidade', 'pin_definir_fase1', 'claro', nota='primeira fase da troca de PIN'),
    Figura('sem-permissao', 'base', 'mensagem_erro', 'claro', nota='é o mesmo quadro de 11a-mensagem-erro: a recusa é essa mensagem'),
    # alarme
    Figura('alarme', 'painel', 'acao_acima', 'claro', nota='alarme de valor acima do limite'),
    Figura('silenciado', 'painel', 'painel_silenciado', 'claro'),
    # temas: the theme screen, and the home screen in dark mode
    Figura('temas', 'menus', 'temas_claro', 'claro'),
    Figura('principal-escuro', 'painel', 'painel_dht22', 'escuro', nota='o mesmo quadro de 11a-principal, no modo escuro'),
    # idioma
    Figura('idioma', 'menus', 'idioma_pacote', 'claro'),
    Figura('idioma-reiniciar', 'base', 'mensagem_reiniciar', 'claro',
           nota='a mensagem do núcleo 0 depois de escolher um pacote que só será carregado na próxima partida: o aparelho iniciou em inglês, por isso a mensagem e o botão vêm em inglês'),
    # licenca
    Figura('licenca', 'menus', 'licenca_p1', 'claro', nota='página 1'),
    # mensagens
    Figura('mensagem-ok', 'base', 'mensagem_ok', 'claro'),
    Figura('mensagem-erro', 'base', 'mensagem_erro', 'claro', nota='é a mensagem de recusa, Sem permissão.'),
    # atualizacao: the OTA states the original panel's chapter also shows
    Figura('ota-recebendo', 'graficos', 'ota_recebendo_42', 'claro', nota='barra em 42 %'),
    Figura('ota-instalando', 'graficos', 'ota_instalando', 'claro'),
    Figura('ota-recusada', 'graficos', 'ota_recusada_sem_assinatura', 'claro', nota='recusa de uma imagem sem assinatura'),
    Figura('ota-interrompida', 'graficos', 'ota_interrompida', 'claro'),
)


def arquivo(f):
    return f'11a-{f.slug}.png'


def quadro_de(f):
    return f'{f.grupo}/{f.amostra}_{f.modo}.png'


def captura(f):
    """The captura= text of the figure's block in the chapter."""
    texto = (f'desenhada pelo código do painel (tools/tft_ui), amostra {f.amostra} do grupo {f.grupo}, modo {f.modo}, pacote {PACOTES[f.idioma]}, ampliada {ESCALA}x; '
             f'refazer com python3 tools/tft_ui/manual_frames.py --write')
    return texto + (f'; {f.nota}' if f.nota else '')


def tabela_ok():
    """A problem with the table itself, or None. A table that lets two figures share a file, or a captura= that breaks the attribute, would be found by no figure."""
    vistos = set()
    for f in FIGURAS:
        if f.slug in vistos:
            return f'FIGURAS names the slug {f.slug!r} twice'
        vistos.add(f.slug)
        if f.grupo not in GRUPOS or f.modo not in MODOS or f.idioma not in PACOTES:
            return f'FIGURAS row {f.slug!r}: group {f.grupo!r}, mode {f.modo!r} or language {f.idioma!r} is not one the renderer has'
        if '"' in captura(f) or 'capturada no aparelho' in captura(f):
            return f'FIGURAS row {f.slug!r}: the captura= text has a double quote (it breaks the attribute) or says "capturada no aparelho" (the frame comes from the host renderer)'
    return None


def desenhar(chaves, lng, pasta):
    """Draws every (language, group) of `chaves` with host_telas.py, as the V-PIX reference is drawn (python3 tools/tft_ui/host_telas.py <group> --idioma <l> --out <dir>),
    into pasta/<language>/<group>. Returns {key: CompletedProcess}. The groups build in separate temporary folders, so they run side by side."""
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')

    def um(chave):
        idioma, grupo = chave
        cmd = [sys.executable, '-B', str(HOST_TELAS), grupo, '--idioma', idioma, '--out', str(pasta / idioma / grupo)]
        if lng:
            cmd += ['--lng', str(lng)]
        return chave, subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True)

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(len(chaves), os.cpu_count() or 2))) as ex:
        return dict(ex.map(um, chaves))


def ampliar(img):
    """Every pixel as a ESCALA x ESCALA block, by hand and not by resize( ): exact by construction, whatever Pillow's resampling does."""
    w, h = img.size
    cru = img.tobytes()
    linhas = []
    for y in range(h):
        linha = cru[3 * w * y:3 * w * (y + 1)]
        linhas.append(b''.join(linha[i:i + 3] * ESCALA for i in range(0, 3 * w, 3)) * ESCALA)
    return Image.frombytes('RGB', (w * ESCALA, h * ESCALA), b''.join(linhas))


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


def carregar_quadros(pasta):
    """{figure: its frame (320 x 240 RGB) or None when the renderer did not write it}."""
    quadros = {}
    for f in FIGURAS:
        p = pasta / f.idioma / f.grupo / f'{f.amostra}_{f.modo}.png'
        img = Image.open(p).convert('RGB') if p.is_file() else None
        if img is not None and img.size != (W, H):
            raise SystemExit(f'manual_frames: {p} is {img.size[0]}x{img.size[1]}, not {W}x{H}: the renderer changed its frame')
        quadros[f.slug] = img
    return quadros


def conferir(imagens, quadros):
    """One line for each figure that differs from the code and for each 11a-*.png with no row."""
    achados = []
    for f in FIGURAS:
        nome, q = arquivo(f), quadros[f.slug]
        if q is None:
            achados.append(f'{nome}: the renderer does not draw {quadro_de(f)} ({f.idioma}) any more: the sample was renamed or removed')
            continue
        caminho = imagens / nome
        if not caminho.is_file():
            achados.append(f'{nome}: missing (python3 tools/tft_ui/manual_frames.py --write draws it)')
            continue
        try:
            img = Image.open(caminho)
            img.load()
        except Exception as e:   # noqa: BLE001  (any decoder error is a finding, not a crash)
            achados.append(f'{nome}: not a readable PNG ({e})')
            continue
        if img.mode != 'RGB' or img.size != (W * ESCALA, H * ESCALA):
            achados.append(f'{nome}: {img.size[0]}x{img.size[1]} {img.mode}, not {W * ESCALA}x{H * ESCALA} RGB like the 11-*.png')
            continue
        esperado = ampliar(q)
        if img.tobytes() != esperado.tobytes():
            n, p = diferenca(img, esperado)
            achados.append(f'{nome}: {n} of {W * H * ESCALA * ESCALA} pixels differ from {quadro_de(f)} ({f.idioma}), the first at x={p[0]} y={p[1]} of the figure')
    nomes = {arquivo(f) for f in FIGURAS}
    for p in sorted(imagens.glob('11a-*.png')):
        if p.name not in nomes:
            achados.append(f'{p.name}: no row of FIGURAS in tools/tft_ui/manual_frames.py names it')
    return achados


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    modo = ap.add_mutually_exclusive_group(required=True)
    modo.add_argument('--write', action='store_true', help='draw the frames and write the figures')
    modo.add_argument('--check', action='store_true', help='draw them again and compare the pixels with the committed figures')
    modo.add_argument('--list', action='store_true', help='file, frame and captura= text of every figure, tab-separated')
    ap.add_argument('--images', default=str(IMAGENS), help='the folder of the figures (default docs/images/manual)')
    ap.add_argument('--lng', help="a pack in place of the shipped one (host_telas.py's --lng, for the controls)")
    args = ap.parse_args()
    problema = tabela_ok()
    if problema:
        print(f'manual_frames: {problema}', file=sys.stderr)
        return 2
    if args.list:
        for f in FIGURAS:
            print('\t'.join((arquivo(f), f'{quadro_de(f)} ({f.idioma})', captura(f))))
        return 0
    if Image is None:
        print('manual_frames: Pillow is needed (pip install pillow)', file=sys.stderr)
        return 2
    imagens = pathlib.Path(args.images)
    chaves = sorted({(f.idioma, f.grupo) for f in FIGURAS})
    with tempfile.TemporaryDirectory(prefix='manual_frames_') as tmp:
        tmp = pathlib.Path(tmp)
        for (idioma, grupo), r in sorted(desenhar(chaves, args.lng, tmp).items()):
            if r.returncode:
                linhas = [ln for ln in (r.stdout + '\n' + r.stderr).splitlines() if ln.strip() and not ln.startswith('warning:')]
                print(f'manual_frames: host_telas.py {grupo} --idioma {idioma} exited {r.returncode}, so its frames are not to be trusted:', file=sys.stderr)
                print('\n'.join('  ' + ln for ln in linhas[-12:]), file=sys.stderr)
                return 2
        quadros = carregar_quadros(tmp)
        if args.write:
            sem = [arquivo(f) for f in FIGURAS if quadros[f.slug] is None]
            if sem:
                print(f'manual_frames: the renderer does not draw the frame of {", ".join(sem)}', file=sys.stderr)
                return 2
            imagens.mkdir(parents=True, exist_ok=True)
            for f in FIGURAS:
                ampliar(quadros[f.slug]).save(imagens / arquivo(f), format='PNG', optimize=True)
            print(f'manual_frames: wrote {len(FIGURAS)} figures to {imagens}')
            return 0
        achados = conferir(imagens, quadros)
        for a in achados:
            print(a)
        print(f'manual_frames: {len(FIGURAS)} figures drawn again from the code and compared by pixels, {len(achados)} finding(s)')
        return 1 if achados else 0


if __name__ == '__main__':
    sys.exit(main())
