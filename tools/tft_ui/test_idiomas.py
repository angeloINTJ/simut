#!/usr/bin/env python3
"""test_idiomas.py -- the Angulo's screens in the device's three languages, on the host (S10c, B3).

B3 turned the texts of the Painel and Base screens into UA_TXT( "English" ) and B4a those of the Menus screens: the device's pack gives the Portuguese or the
Spanish, and the source gives the English. This runs tools/tft_ui/host_telas.py for every group in each language, and holds what no pixel comparison can:

  1. The shipped packs draw clean. `host_telas.py <group> --idioma en|pt|es` exits 0 for every group: no glyph the fonts lack (ua::glifosFaltando( )), no
     text outside the safe area, over another, across the edge of a touch target or of the card it sits on, and no fixed text cut with "…" (only the
     long names a sample plants on purpose may be, and tools/tft_ui/cortes_aceitos.txt lists them for every language). Every group, and not only the
     converted ones, because the kit's texts reach them all: B3 translated the kit's "Voltar", three screens still built its box with the literal, and
     the charts' detail, in English and in Spanish, stopped the harness on the difference (S10c B3-b). And what a screen places by the width of a
     translated text keeps its place against it in the three languages: the touch target of the kit's back button against its label, in every sample
     that draws the kit's bar (127), and the charts' page name after the pager's count (S10c B3-c: a literal copy of either moves the gap by the
     difference of the two widths, which no check of one language can see).
  2. The checks bite, through the whole chain (a pack file, the loader, UA_TXT( ), the screen, the recorder, the check). A pack is planted with one text changed, and the
     samples that fail must be EXACTLY the samples whose frames differ from the shipped pack's (two instruments that do not read each other: the pixels and the
     checks): an "ő" (no font has it) in the label of the boot button; a label of 74 characters in the same button; a text of 102 characters in the card that says
     there is no sensor, which no touch target holds, so that only the surface check can see it; and the review of B3's reproduction, a text of 84 bytes
     on the same card whose "í" the recorder's 63-byte field cuts in two (host_telas.py died on it with a UnicodeDecodeError that named no sample). Each
     plant also must name its check in the failure, and a plant longer than the recorder's field must say that the field was full, the 17-byte one must not.
  3. The surface check's own cases (host_telas.conferir_encaixe( )) on synthetic boxes, edge by edge; the reader of a sample's texts (host_telas.ler_textos( ))
     on a file that is not UTF-8, and on records at the recorder's field and under it; and the English of a sample file (host_telas.ingles_das_amostras( ))
     against the sources: the right English passes and one letter off is named.

Usage: python3 tools/tft_ui/test_idiomas.py   exit 0 when all hold, 1 when not, 2 when it cannot build (no Adafruit GFX 1.12.6 in .pio/libdeps: run pio once).
"""
import concurrent.futures
import contextlib
import io
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

GRUPOS = ('base', 'painel', 'menus')              # the groups whose texts are converted: a group joins here when its step lands (B4b, B5)
TODOS = tuple(sorted(p.stem for p in (HERE / 'amostras').glob('*.cpp')))   # every group the harness draws, read from its samples
IDIOMAS = ('en', 'pt', 'es')
PACOTES = {'pt': ROOT / 'data' / 'lang' / 'language_pt-BR.lng', 'es': ROOT / 'data' / 'lang' / 'language_es-ES.lng'}
CASOS = []                                        # (name, ok, detail)
# The samples the two cross-language checks below hold, counted on 2026-10-22 (B4a-b, after the review of B4a found that losing most of them
# stayed green): a step that draws fewer must lower these on purpose, and say why.
PISO_LEGENDA, PISO_VOLTAR = 4, 127


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


def contagem_e_legenda(pasta):
    """{sample: gap} for the charts' pager with a page name: the ink gap from the kit's count ("1 of 3", "1 de 3") to the name drawn after
    it ("· Pressão"), read from the light theme's records host_telas.py keeps in --out. The name is placed by the count's measured width,
    so the gap is the layout's and must not change with the language (S10c B3-c: it was placed after a literal "n de m", and in English,
    "1 of 3" being 5 px narrower, the gap grew by 5 px)."""
    out = {}
    for f in sorted(pasta.glob('*_claro.textos')):
        recs = [ln.split(' ', 5) for ln in f.read_text(encoding='utf-8').splitlines()]
        cont = [r for r in recs if len(r) > 5 and r[4] == 't' and re.fullmatch(r'\d+ \S+ \d+', r[5])]
        leg = [r for r in recs if len(r) > 5 and r[4] == 't' and r[5].startswith('\u00b7 ')]
        if len(cont) == 1 and len(leg) == 1:
            out[f.name[:-len('_claro.textos')]] = int(leg[0][0]) - int(cont[0][1])
    return out


def traducao(idioma, ingles):
    """The text the shipped pack of `idioma` gives for `ingles` (the English itself for en, or when the pack has no line for it)."""
    if idioma == 'en':
        return ingles
    chave = b'\n%08x ' % gate.fnv1a32(ingles)
    dados = PACOTES[idioma].read_bytes()
    if chave not in dados:
        return ingles
    i = dados.index(chave) + len(chave)
    return dados[i:dados.index(b'\n', i)].decode('utf-8')


def voltar_e_alvo(pasta, rotulo, inerte):
    """{sample: gap} for the samples whose title bar draws the kit's back button with `rotulo` (the language's word for "Back"): the gap
    from the touch target that holds the label to the label's ink. The target is computed from the label's width, so the gap is the
    layout's and must not change with the language (S10c B3-b: three screens computed it from a literal "Voltar", and in English their
    targets began 10 px further left than the "Back" drawn; no other check saw two of the three)."""
    out = {}
    for f in sorted(pasta.glob('*_claro.textos')):
        nome = f.name[:-len('_claro.textos')]
        recs = [ln.split(' ', 5) for ln in f.read_text(encoding='utf-8').splitlines()]
        rot = [tuple(map(int, r[:4])) for r in recs if len(r) > 5 and r[4] == 't' and r[5] == rotulo and int(r[3]) <= 36]
        if len(rot) != 1 or not (pasta / f'{nome}.alvos').exists():
            continue
        x0, x1, y0, y1 = rot[0]
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        alvos = [tuple(map(int, ln.split()[:6])) for ln in (pasta / f'{nome}.alvos').read_text().splitlines()]
        dono = [a for a in alvos if a[4] != inerte and a[0] <= cx < a[0] + a[2] and a[1] <= cy < a[1] + a[3]]
        if len(dono) == 1:
            out[nome] = x0 - dono[0][0]
    return out


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
            # The review of B3, word for word: 84 bytes, the "í" at bytes 62 and 63, where the recorder's field of 63 cuts.
            'acento': ('Activate a sensor on the web.', 'Active un sensor desde la interfaz web del equipo para ver aquí todas sus lecturas.'),
        }
        packs = {nome: plantar(PACOTES['es'], en, novo, tmp / f'es_{nome}.lng') for nome, (en, novo) in plantados.items()}
        trabalhos = {('limpo', g, i): (g, i, None, limpo / i / g) for g in TODOS for i in IDIOMAS}
        for nome, pacote in packs.items():
            trabalhos[(nome, 'painel', 'es')] = ('painel', 'es', pacote, tmp / nome / 'es' / 'painel')
        with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(trabalhos), os.cpu_count() or 4)) as ex:
            feitos = {k: ex.submit(rodar, *v) for k, v in trabalhos.items()}
            res = {k: f.result() for k, f in feitos.items()}

        # 1. the shipped packs draw clean
        caso(f'the harness has groups to draw ({", ".join(TODOS)})', set(GRUPOS) <= set(TODOS) and len(TODOS) >= 6, str(TODOS))
        for g in TODOS:
            for i in IDIOMAS:
                rc, saida = res[('limpo', g, i)]
                caso(f'{g} in {i}: host_telas.py exits 0', rc == 0, '\n'.join(saida.splitlines()[-6:]))
                resumo = saida.strip().splitlines()[-1] if saida.strip() else ''
                caso(f'{g} in {i}: no failure and texts held to their surface', ' 0 failures' in resumo and re.search(r'[1-9]\d* texts held to their surface', resumo), resumo)

        # the charts' pager: the page's name keeps its gap after the count in every language
        lacunas = {i: contagem_e_legenda(limpo / i / 'graficos') for i in IDIOMAS}
        caso(f'charts\' pager: the same samples draw a count and a page name in every language ({len(lacunas["pt"])} in pt-BR)',
             len(lacunas['pt']) >= PISO_LEGENDA and all(set(lacunas[i]) == set(lacunas['pt']) for i in IDIOMAS), str({i: sorted(v) for i, v in lacunas.items()}))
        diferem = sorted(n for n in lacunas['pt'] if len({lacunas[i].get(n) for i in IDIOMAS}) != 1)
        caso('charts\' pager: the gap from the count to the page name is the same in the three languages', not diferem,
             '; '.join(f'{n}: ' + ', '.join(f'{i} {lacunas[i].get(n)} px' for i in IDIOMAS) for n in diferem[:4]))

        # the kit's back button: its target keeps its place against the label drawn, in every language and every group
        inerte = host_telas.alvo_inerte()
        voltas = {i: {} for i in IDIOMAS}
        for g in TODOS:
            for i in IDIOMAS:
                voltas[i].update({f'{g}/{n}': v for n, v in voltar_e_alvo(limpo / i / g, traducao(i, 'Back'), inerte).items()})
        comuns = sorted(set(voltas['en']) & set(voltas['pt']) & set(voltas['es']))
        grupos_com = sorted({n.split('/')[0] for n in comuns})
        caso(f'back button: samples with the kit\'s bar in every language ({len(comuns)}, in {", ".join(grupos_com)})',
             len(comuns) >= PISO_VOLTAR and {'ajustes', 'menus', 'graficos'} <= set(grupos_com), f'{len(comuns)} of at least {PISO_VOLTAR}: {grupos_com}')
        movidos = [n for n in comuns if len({voltas[i][n] for i in IDIOMAS}) != 1]
        caso('back button: the gap from its target to its label is the same in the three languages', not movidos,
             '; '.join(f'{n}: ' + ', '.join(f'{i} {voltas[i][n]} px' for i in IDIOMAS) for n in movidos[:4]))

        # 2. the plants fail exactly where their frames change
        limpo_es = limpo / 'es' / 'painel'
        esperado_check = {'glifo': 'glyphs or icons missing', 'rotulo': 'target', 'cartao': 'runs past the edge of the surface',
                          'acento': 'runs past the edge of the surface'}
        campo = host_telas.campo_do_registro()
        caso('the recorder\'s field is read from Angulo.h', campo is not None and campo > 3, str(campo))
        bruto = plantados['acento'][1].encode('utf-8')
        caso(f'plant "acento": the recorder\'s cut at byte {campo} falls inside a character (else it tests nothing)',
             campo is not None and len(bruto) > campo and (bruto[campo] & 0xC0) == 0x80, f'{bruto[campo - 2:campo + 2]!r}' if campo else '')
        for nome in plantados:
            rc, saida = res[(nome, 'painel', 'es')]
            falhou, soltas = falhas(saida)
            mudou = quadros_diferentes(limpo_es, tmp / nome / 'es' / 'painel') if (tmp / nome / 'es' / 'painel').is_dir() else set()
            caso(f'plant "{nome}": the run fails', rc == 1, f'exit {rc}')
            caso(f'plant "{nome}": the samples that fail are exactly the samples whose frames changed ({sorted(mudou)})',
                 mudou and set(falhou) == mudou and not soltas, f'failing {sorted(falhou)}, changed {sorted(mudou)}, loose {soltas[:2]}')
            caso(f'plant "{nome}": the failure names its check ("{esperado_check[nome]}")',
                 any(esperado_check[nome] in ln for v in falhou.values() for ln in v), '\n'.join(next(iter(falhou.values()), [])[:2]))
            cheio = any('bytes the recorder keeps of a text' in ln for v in falhou.values() for ln in v)
            longo = campo is not None and len(plantados[nome][1].encode('utf-8')) > campo
            caso(f'plant "{nome}": ' + ('the run says the recorder\'s field was full' if longo else 'the run does not say the field was full'),
                 cheio == longo, f'{len(plantados[nome][1].encode("utf-8"))} bytes, field {campo}')

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

    # the reader of a sample's texts: a file that is not UTF-8 is named, and a record at the recorder's field may have lost its end
    campo = host_telas.campo_do_registro() or 63
    with tempfile.TemporaryDirectory(prefix='idiomas_ler_') as t:
        d = pathlib.Path(t)
        quebrado = b'10 60 4 20 t Ok\n10 300 30 46 t Active un sensor desde la interfaz web del equipo para ver aqu\xc3\n'
        (d / 'quebrado_claro.textos').write_bytes(quebrado)
        e = []
        caso('reader: a file that is not UTF-8 gives None, and one error that names the sample, the theme and the byte',
             host_telas.ler_textos('quebrado', 'claro', d, campo, e) is None and len(e) == 1 and e[0].startswith('quebrado_claro: ')
             and f'byte {quebrado.index(bytes([0xc3]))} ' in e[0], str(e))
        cheio = 'x' * (campo - 3)
        (d / 'cheio_claro.textos').write_bytes(f'10 60 4 20 t {cheio}\n10 60 30 46 t {cheio[:-1]}\n70 90 4 20 s web\n'.encode('utf-8'))
        e = []
        lidos = host_telas.ler_textos('cheio', 'claro', d, campo, e)
        caso(f'reader: a record of {campo - 3} bytes is an error, one of {campo - 4} is not',
             lidos is not None and len(e) == 1 and f'"{cheio}" is {campo - 3} bytes, within 3 of the {campo} bytes' in e[0], str(e))
        caso('reader: texts and badges come apart, each with its box', lidos is not None and lidos[0] == [(10, 60, 4, 20, cheio), (10, 60, 30, 46, cheio[:-1])]
             and lidos[1] == [(70, 90, 4, 20, 'web')], str(lidos))

    # the accepted cuts (tools/tft_ui/cortes_aceitos.txt): a line gives one start for every language, or three in the order pt-BR, English, Spanish (S10c, B4a:
    # the menu's "Conta: maria.a…", "Account: maria.…" and "Cuenta: maria.…"), and each language's run holds its cuts to its own start (B4a-b: a shared "WWWWWW"
    # fitted the Spanish cut and accepted a Portuguese one a letter short)
    with tempfile.TemporaryDirectory(prefix='idiomas_cortes_') as t:
        d = pathlib.Path(t)
        orig = host_telas.ACEITOS
        host_telas.ACEITOS = d / 'cortes.txt'
        try:
            host_telas.ACEITOS.write_text('menus | menu_x | Conta: maria.a ¦ Account: maria. ¦ Cuenta: maria. | why\nmenus | menu_w | WWWWWWW ¦ WWWWWWWWWW ¦ WWWWWW | why\n'
                                          'menus | menu_y | Camara fria | why\nidentidade | conta_z | Ana | why\n', encoding='utf-8')
            por = {i: host_telas.aceitos('menus', i) for i in IDIOMAS}
            caso('accepted cuts: three starts give each language its own, in the order pt-BR, English, Spanish, and one start serves all three',
                 por['pt'] == [('menu_x', 'Conta: maria.a', 1), ('menu_w', 'WWWWWWW', 2), ('menu_y', 'Camara fria', 3)]
                 and por['en'] == [('menu_x', 'Account: maria.', 1), ('menu_w', 'WWWWWWWWWW', 2), ('menu_y', 'Camara fria', 3)]
                 and por['es'] == [('menu_x', 'Cuenta: maria.', 1), ('menu_w', 'WWWWWW', 2), ('menu_y', 'Camara fria', 3)], str(por))
            caso('accepted cuts: a cut a letter short fails its own language\'s start and would have passed the Spanish one',
                 not 'WWWWWW…'.startswith(por['pt'][1][1]) and 'WWWWWW…'.startswith(por['es'][1][1]) and 'WWWWWWW…'.startswith(por['pt'][1][1])
                 and not 'Account: maria…'.startswith(por['en'][0][1]) and 'Account: maria.…'.startswith(por['en'][0][1]))
            for linha, motivo in (('menus | menu_x | Conta: maria.a ¦ | why\n', 'an empty start is refused, so that it cannot accept every cut'),
                                  ('menus | menu_x | Conta: maria.a ¦ Account: maria. | why\n', 'two starts are refused: one for all or one for each of the three')):
                host_telas.ACEITOS.write_text(linha, encoding='utf-8')
                with contextlib.redirect_stderr(io.StringIO()):
                    caso(f'accepted cuts: {motivo}', host_telas.aceitos('menus', 'pt') is None)
        finally:
            host_telas.ACEITOS = orig

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
