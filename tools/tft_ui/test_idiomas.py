#!/usr/bin/env python3
"""test_idiomas.py -- the Angulo's screens in the device's three languages, on the host (S10c, B3).

B3 turned the texts of the Painel and Base screens into UA_TXT( "English" ): the device's pack gives the Portuguese or the Spanish, and the source
gives the English. This runs tools/tft_ui/host_telas.py for those two groups in each language, and holds what no pixel comparison can:

  1. The shipped packs draw clean. `host_telas.py <group> --idioma en|pt|es` exits 0 for base and painel: no glyph the fonts lack (ua::glifosFaltando( )), no
     text outside the safe area, over another, across the edge of a touch target or of the card it sits on, and no fixed text cut with "…" (only the
     long names a sample plants on purpose may be, and tools/tft_ui/cortes_aceitos.txt lists them for every language).
  2. The checks bite, through the whole chain (a pack file, the loader, UA_TXT( ), the screen, the recorder, the check). A pack is planted with one text changed, and the
     samples that fail must be EXACTLY the samples whose frames differ from the shipped pack's (two instruments that do not read each other: the pixels and the
     checks): an "ő" (no font has it) in the label of the boot button; a label of 70 characters in the same button; a text of 90 characters in the card that says
     there is no sensor, which no touch target holds, so that only the surface check can see it. Each plant also must name its check in the failure.
  3. The surface check's own cases (host_telas.conferir_encaixe( )) on synthetic boxes, edge by edge, and the English of a sample file (host_telas.
     ingles_das_amostras( )) against the sources: the right English passes and one letter off is named.

Usage: python3 tools/tft_ui/test_idiomas.py   exit 0 when all hold, 1 when not, 2 when it cannot build (no Adafruit GFX 1.12.6 in .pio/libdeps: run pio once).
"""
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools'))
os.environ.setdefault('LANG_GATE_NO_RUN', '1')   # tools/check_lang_packs.py runs its gate on import otherwise
import check_lang_packs as gate  # noqa: E402
import host_telas  # noqa: E402

GRUPOS = ('base', 'painel')                       # the groups whose texts are converted: a group joins here when its step lands (B4, B5)
IDIOMAS = ('en', 'pt', 'es')
PACOTES = {'pt': ROOT / 'data' / 'lang' / 'language_pt-BR.lng', 'es': ROOT / 'data' / 'lang' / 'language_es-ES.lng'}
CASOS = []                                        # (name, ok, detail)


def caso(nome, ok, detalhe=''):
    CASOS.append((nome, bool(ok), detalhe))
    print(('ok    ' if ok else 'FAIL  ') + nome + (f': {detalhe}' if detalhe and not ok else ''), flush=True)


def rodar(grupo, idioma, lng=None, saida=None):
    """(exit code, stdout + stderr) of host_telas.py for one group in one language."""
    cmd = [sys.executable, str(HERE / 'host_telas.py'), grupo, '--idioma', idioma]
    if lng:
        cmd += ['--lng', str(lng)]
    if saida:
        cmd += ['--out', str(saida)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def falhas(texto):
    """({sample: [failure lines]}, [lines that name no sample]) of a host_telas.py run."""
    por, soltas = {}, []
    for ln in texto.splitlines():
        if not ln.startswith('FAIL: '):
            continue
        m = re.match(r'FAIL: ([a-z0-9_]+?)(?:_escuro|_claro)?: ', ln)
        if m and not ln.startswith('FAIL: cortes_aceitos'):
            por.setdefault(m.group(1), []).append(ln)
        else:
            soltas.append(ln)
    return por, soltas


def plantar(pacote, ingles, novo, destino):
    """A copy of the pack with the text of `ingles` changed to `novo`: exactly one line is rewritten, or the plant is refused."""
    dados = pacote.read_bytes()
    chave = b'\n%08x ' % gate.fnv1a32(ingles)
    assert dados.count(chave) == 1, f'{ingles!r} has {dados.count(chave)} lines in {pacote.name}'
    i = dados.index(chave) + len(chave)
    j = dados.index(b'\n', i)
    destino.write_bytes(dados[:i] + novo.encode('utf-8') + dados[j:])
    return destino


def quadros_diferentes(a, b):
    """The samples whose frame differs between two --out folders of the same group."""
    nomes = sorted({p.name[:-len('_escuro.png')] for p in a.glob('*_escuro.png')})
    return {n for n in nomes for t in ('escuro', 'claro') if (a / f'{n}_{t}.png').read_bytes() != (b / f'{n}_{t}.png').read_bytes()}


def main():
    if not host_telas.find_gfx():
        print('Adafruit GFX Library 1.12.6 not found (run pio run -e pico_wiretft_angulo once)', file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix='idiomas_') as t:
        tmp = pathlib.Path(t)
        limpo = tmp / 'limpo'
        plantados = {
            'glifo': ('Skip the wait', 'Omitir lő espera'),
            'rotulo': ('Skip the wait', 'Omitir la espera de la red y seguir con el arranque del equipo ahora mismo'),
            'cartao': ('No active sensor', 'Ningun sensor esta activo en este momento, ni uno solo, y el equipo no tiene nada que mostrar aqui hoy'),
        }
        packs = {nome: plantar(PACOTES['es'], en, novo, tmp / f'es_{nome}.lng') for nome, (en, novo) in plantados.items()}
        trabalhos = {('limpo', g, i): (g, i, None, limpo / i / g) for g in GRUPOS for i in IDIOMAS}
        for nome, pacote in packs.items():
            trabalhos[(nome, 'painel', 'es')] = ('painel', 'es', pacote, tmp / nome / 'es' / 'painel')
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(trabalhos), os.cpu_count() or 4)) as ex:
            feitos = {k: ex.submit(rodar, *v) for k, v in trabalhos.items()}
            res = {k: f.result() for k, f in feitos.items()}

        # 1. the shipped packs draw clean
        for g in GRUPOS:
            for i in IDIOMAS:
                rc, saida = res[('limpo', g, i)]
                caso(f'{g} in {i}: host_telas.py exits 0', rc == 0, '\n'.join(saida.splitlines()[-6:]))
                resumo = saida.strip().splitlines()[-1] if saida.strip() else ''
                caso(f'{g} in {i}: no failure and texts held to their surface', ' 0 failures' in resumo and re.search(r'[1-9]\d* texts held to their surface', resumo), resumo)

        # 2. the plants fail exactly where their frames change
        limpo_es = limpo / 'es' / 'painel'
        esperado_check = {'glifo': 'glyphs or icons missing', 'rotulo': 'target', 'cartao': 'runs past the edge of the surface'}
        for nome in plantados:
            rc, saida = res[(nome, 'painel', 'es')]
            falhou, soltas = falhas(saida)
            mudou = quadros_diferentes(limpo_es, tmp / nome / 'es' / 'painel') if (tmp / nome / 'es' / 'painel').is_dir() else set()
            caso(f'plant "{nome}": the run fails', rc == 1, f'exit {rc}')
            caso(f'plant "{nome}": the samples that fail are exactly the samples whose frames changed ({sorted(mudou)})',
                 mudou and set(falhou) == mudou and not soltas, f'failing {sorted(falhou)}, changed {sorted(mudou)}, loose {soltas[:2]}')
            caso(f'plant "{nome}": the failure names its check ("{esperado_check[nome]}")',
                 any(esperado_check[nome] in ln for v in falhou.values() for ln in v), '\n'.join(next(iter(falhou.values()), [])[:2]))

    # 3. the surface check's own cases, edge by edge
    conferir = host_telas.conferir_encaixe

    def achados(texto, superficies):
        e = []
        conferir('s', 'claro', [texto], superficies, e)
        return e

    cartao, selo = (8, 100, 304, 56), (60, 110, 80, 20)
    caso('surface: a text inside its card passes', not achados((20, 200, 120, 140, 't'), [cartao]))
    caso('surface: ink touching both edges of its card passes', not achados((8, 312, 120, 140, 't'), [cartao]))
    caso('surface: one pixel past the right edge fails', len(achados((20, 313, 120, 140, 't'), [cartao])) == 1)
    caso('surface: one pixel past the left edge fails', len(achados((7, 200, 120, 140, 't'), [cartao])) == 1)
    caso('surface: a descender below a pill passes (the height is the layout\'s)', not achados((70, 130, 111, 131, 't'), [selo]))
    caso('surface: no surface holds the middle of the text, so there is nothing to hold it to', not achados((0, 40, 10, 20, 't'), [cartao]))
    caso('surface: the smallest surface is the one that counts', len(achados((50, 160, 112, 128, 't'), [cartao, selo])) == 1)
    caso('surface: returns how many texts sat on a surface',
         conferir('s', 'claro', [(20, 200, 120, 140, 'a'), (0, 40, 10, 20, 'b')], [cartao], []) == 1)

    # the English of a sample file against the sources
    with tempfile.TemporaryDirectory(prefix='idiomas_amostra_') as t:
        d = pathlib.Path(t)
        orig = host_telas.HERE
        (d / 'amostras').mkdir()
        host_telas.HERE = d
        try:
            (d / 'amostras' / 'certa.cpp').write_text('void f( ) { x( UA_TXT( "Got it" ), UA_TXT( "Mute all sounds?" ) ); }\n', encoding='utf-8')
            (d / 'amostras' / 'errada.cpp').write_text('void f( ) { x( UA_TXT( "Got it" ), UA_TXT( "Mute all sound?" ) ); }\n', encoding='utf-8')
            (d / 'amostras' / 'sem.cpp').write_text('void f( ) { x( "PIN salvo." ); }\n', encoding='utf-8')
            caso('sample English: the sources\' English passes', host_telas.ingles_das_amostras('certa') == [])
            erradas = host_telas.ingles_das_amostras('errada')
            caso('sample English: one letter off is named, once', len(erradas) == 1 and 'Mute all sound?' in erradas[0], str(erradas))
            caso('sample English: a sample with no UA_TXT( ) has nothing to hold', host_telas.ingles_das_amostras('sem') == [])
        finally:
            host_telas.HERE = orig
    caso('sample English: the shipped samples of the converted groups pass', all(host_telas.ingles_das_amostras(g) == [] for g in GRUPOS))

    ruins = [c for c in CASOS if not c[1]]
    print(f'\n{len(CASOS)} checks, {len(ruins)} failed')
    return 1 if ruins else 0


if __name__ == '__main__':
    sys.exit(main())
