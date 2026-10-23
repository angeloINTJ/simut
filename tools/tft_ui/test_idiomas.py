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
     that draws the kit's bar (168 on 2026-10-23), and the charts' page name after the pager's count (S10c B3-c: a literal copy of either moves the gap by the
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
  4. The numbers are written the language's way (S10c, B5a). The decimal separator is the pack's text for "Decimal separator" (ua::separadorDecimal( ), the kit's one place, which n1( ) and kb( )
     both call): "," in pt-BR and es-ES, and in English, which has no line for it, the point of the original panel (fmtFloat1). Every number with one decimal in the records of every group's samples carries the language's
     separator, and the control is that a mutant n1( ) with a literal "," passes the checks above and fails only this one.
  5. The calendar's names (S10c, B5a): the month of the title and the seven weekday letters of the header are the language's (English "Jan".."Dec" and "Su".."Sa", pt-BR as before, es-ES "Ene".."Dic" and
     "D L M X J V S"), each weekday centred in its column, in every calendar sample.
  6. What the translated paragraphs of the update screens leave of the foot (S10c, B5a): a refusal's two paragraphs wrap to as many lines as the language needs, and in es-ES one of them took four lines and ended
     2 px above the foot note, where nothing else came nearer than 18 px. The paragraphs must end 12 px or more above the first foot line, in every language.
  7. The identity screens draw their own bar and footer (S10c, B5b), so the kit's single sources do not reach them: the bar keeps the actions its boxes were measured from and the footer the labels of
     its boxes. The target of the bar's "Back" (the account picker, the list of accounts, one account: 20 of the 168 samples of item 1) and of its "Cancel" (the PIN keypad and the text keyboard:
     43 samples) keeps its place against the label in the three languages; the account editor's footer is its label's ink and the kit's padding, as the Ajustes' (86 buttons with them); and a name
     cut beside a badge ("Locked", "No PIN") ends within FOLGA_CORTE px of the room the badge's drawn width leaves it, never nearer than the layout leaves it (9 rows, 2 with a cut name). The mutants
     (stages/s10c_b5b/scripts/mut_b5b.py, scratch copies): a badge measured from the pt-BR literal fails the third in English, one measured from a narrower word fails it in pt-BR and es-ES, a bar
     measured from the literal "Voltar" fails the first two, a footer box measured from the literal "Cancelar" fails the footer's.
  8. The placeholder of a missing value ("--,-") carries the language's separator, like the numbers (S10c, B5b): the Painel's dec( ) wrote a literal comma in every language, and the numbers' check
     (item 4) cannot see it, because it reads digits. The control is a mutant dec( ) with the literal: it fails only the English samples of the Painel (10 of the 12 placeholders).

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

GRUPOS = ('base', 'painel', 'menus', 'ajustes', 'graficos', 'identidade')   # the groups whose texts are converted: a group joins here when its step lands (B5b: identidade, the last)
TODOS = tuple(sorted(p.stem for p in (HERE / 'amostras').glob('*.cpp')))   # every group the harness draws, read from its samples
IDIOMAS = ('en', 'pt', 'es')
PACOTES = {'pt': ROOT / 'data' / 'lang' / 'language_pt-BR.lng', 'es': ROOT / 'data' / 'lang' / 'language_es-ES.lng'}
CASOS = []                                        # (name, ok, detail)
# The samples the cross-language checks below hold, counted on 2026-10-22 (B4a-b, after the review of B4a found that losing most of them
# stayed green; B4b added the 78 footer buttons of the Ajustes samples; B5a the 21 samples of the chart and the calendar, whose bars draw the kit's back action;
# counted on 2026-10-23 (B5b): the 20 samples of the identity bars that draw the kit's back action (the account picker 7, the list of accounts 9, one account 4) raise the back
# buttons from 148 to 168, and the 4 samples of the account editor add 8 footer buttons to the 78 of the Ajustes): a step that draws fewer must lower these on purpose, and say why.
PISO_LEGENDA, PISO_VOLTAR, PISO_RODAPE = 4, 168, 86
# The bar's "Cancel" of the PIN keypad and of the text keyboard (a target of the bar that is not dead), and the rows of the account picker and of the user list whose name is cut beside a badge,
# counted on 2026-10-23 (B5b). PISO_SEM_DADO: the Painel's and the charts' placeholders for a missing value ("--,-"), the same day.
PISO_CANCELAR, PISO_SELOS, PISO_SEM_DADO = 43, 9, 12
FOLGA_CORTE = 16        # a name cut beside a badge ends at most this far past the room the layout leaves it (the cut stops at the last whole character: 0 to 8 px for these names, measured 2026-10-23);
                        # so the case sees a badge measured from another text only when the two widths differ by a character or more, about 8 px ("Locked" and "Bloqueada" differ by 29)
# The numbers with one decimal that the samples of all the groups draw in each language, counted on 2026-10-22 (B5a: 109 in graficos, 36 in menus, 101 in painel, 16 in ajustes): the separator check holds this many in every language.
PISO_DECIMAIS = 262
CHAVE_SEPARADOR = 'Decimal separator'                            # the English key of the separator (Angulo.cpp, separadorDecimal( )); English has no line for it and draws the point
SEPARADOR = {'en': '.', 'pt': ',', 'es': ','}                    # each language's decimal separator
# The calendar's names, as the brief for B5a fixes them. The keys are the English the screen asks for (GraficosCalendario.cpp: nomeDoMes( ), inicialDaSemana( )); the weekday keys are two letters
# because the initials repeat in a language and one English gives one translation.
CHAVES_MES = 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec'.split()
CHAVES_SEMANA = 'Su Mo Tu We Th Fr Sa'.split()
MESES = {'en': 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec', 'pt': 'Jan Fev Mar Abr Mai Jun Jul Ago Set Out Nov Dez', 'es': 'Ene Feb Mar Abr May Jun Jul Ago Sep Oct Nov Dic'}
SEMANA = {'en': 'Su Mo Tu We Th Fr Sa', 'pt': 'D S T Q Q S S', 'es': 'D L M X J V S'}
# The update screens that end with the two foot lines (the seven refusals and the interrupted upload) and the room the paragraphs must leave above them: 18 px is the least any language leaves (2026-10-22, B5a, three lines
# of paragraph), and the layout that was fixed by number left 2.
PISO_OTA, FOLGA_OTA = 8, 12
PISO_CALENDARIO = 7   # the calendar samples (amostras/graficos.cpp, counted 2026-10-22): October, July, a month being read, February, six weeks, a leap February, September
DECIMAL = re.compile(r'(?<![\d.,])(?<![A-Za-z]-)\d+([.,])\d(?!\d|[.,]\d)')   # a number with one decimal; a version ("2.11.1") or an address ("192.168.4.1") has another digit or separator beside it, a licence id ("GPL-3.0") a letter and a hyphen before it


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


def espaco_do_kit(nome):
    """A spacing token of the kit (ESP2, ESP4), read from src/ui/angulo/Angulo.h, not copied here."""
    texto = (ROOT / 'src' / 'ui' / 'angulo' / 'Angulo.h').read_text(encoding='utf-8')
    m = re.search(rf'\b{nome} = (\d+)\b', texto)
    return int(m.group(1)) if m else None


def rodape_e_alvos(pasta):
    """{sample/target: slack} for the footer buttons of one group's samples in one language: the targets 40 px tall in the bottom rows (y 190 and below) that hold
    exactly one text of the light theme, and the slack is the target's width less the text's ink width. A footer button is its label's ink plus twice the kit's padding
    (the primary 2 * ESP4, the discreet one 2 * ESP2), and the group's layouts measure each label where it is drawn (AjustesComum.h: caixasRodape( ) keeps the label it
    measured), so the slack is the layout's and must not change with the language. A box measured from another text than the one drawn (S10c B3-b and B3-c found it
    for the back button and the pager's count) shifts it by the difference of the two widths, in every language whose word is not that text, and a wider box than the
    label needs, in English, is something no other check sees: the text stays inside it and centred."""
    out = {}
    for f in sorted(pasta.glob('*_claro.textos')):
        nome = f.name[:-len('_claro.textos')]
        if not (pasta / f'{nome}.alvos').exists():
            continue
        recs = [ln.split(' ', 5) for ln in f.read_text(encoding='utf-8').splitlines()]
        textos = [tuple(map(int, r[:4])) for r in recs if len(r) > 5 and r[4] == 't']
        alvos = [tuple(map(int, ln.split()[:6])) for ln in (pasta / f'{nome}.alvos').read_text().splitlines()]
        for i, (x, y, w, h, _tipo, _ind) in enumerate(alvos):
            if h != 40 or y < 190:
                continue
            dentro = [t for t in textos if x <= (t[0] + t[1]) // 2 < x + w and y <= (t[2] + t[3]) // 2 < y + h]
            if len(dentro) == 1:
                out[f'{nome}/{i}'] = w - (dentro[0][1] - dentro[0][0])
    return out


def decimais(pasta):
    """[(sample, text, separator)] for every number with one decimal in the texts of one group's samples (the light theme's records that host_telas.py keeps in --out). n1( ) and kb( ) write one
    decimal; a version or an address does not match (DECIMAL). The separator is read from what the screen drew, and the expectation from the language (SEPARADOR) and from the shipped pack."""
    out = []
    for f in sorted(pasta.glob('*_claro.textos')):
        nome = f.name[:-len('_claro.textos')]
        for ln in f.read_text(encoding='utf-8').splitlines():
            r = ln.split(' ', 5)
            if len(r) > 5 and r[4] == 't':
                out += [(nome, r[5], m.group(1)) for m in DECIMAL.finditer(r[5])]
    return out


SEM_DADO = re.compile(r'--([.,])-')   # the placeholder for a value the sensor or the day does not have ("--,-" in pt-BR)


def sem_dado(pasta):
    """[(sample, text, separator)] for every placeholder of a missing value in the texts of one group's samples (the light theme's records). The Painel's dec( ) and the charts' values write it
    ("--" + the language's separator + "-"); the separator is read from what the screen drew, as decimais( ) reads a number's."""
    out = []
    for f in sorted(pasta.glob('*_claro.textos')):
        nome = f.name[:-len('_claro.textos')]
        for ln in f.read_text(encoding='utf-8').splitlines():
            r = ln.split(' ', 5)
            if len(r) > 5 and r[4] == 't':
                out += [(nome, r[5], m.group(1)) for m in SEM_DADO.finditer(r[5])]
    return out


def selos_e_nomes(pasta, esp2):
    """{sample@y: (name, gap, cut, limit)} for the rows of the account picker (quem_*) and of the user list (usuarios_*) that carry a badge: the gap from the ink of the text that ends nearest to the badge's left
    edge (the account's name; the badge's own label starts inside the box) to the badge's box, which host_telas.py records as kind "s" (the whole box, not the label's ink). The layout leaves the name the room
    that remains once the badge's drawn width is taken: ESP2 from the badge in the picker (the badge stands where the chevron would), 2 * ESP2 in the list (the chevron sits between). So a name that is cut ("…")
    ends between `limit` and `limit` + FOLGA_CORTE from the badge, whatever the language's word for the badge is; a badge measured from another text than the one drawn shifts the gap by the difference of the
    two widths (the layout measured the pt-BR literal while the pack's word was drawn, S10c B3-b and B3-c found that defect for other labels)."""
    out = {}
    for f in sorted(pasta.glob('*_claro.textos')):
        nome = f.name[:-len('_claro.textos')]
        limite = {'quem': esp2, 'usuarios': 2 * esp2}.get(nome.split('_')[0])
        if limite is None:
            continue
        recs = [ln.split(' ', 5) for ln in f.read_text(encoding='utf-8').splitlines()]
        recs = [(int(r[0]), int(r[1]), int(r[2]), int(r[3]), r[4], r[5]) for r in recs if len(r) > 5]
        for sx0, sx1, sy0, sy1, tipo, rotulo in recs:
            if tipo != 's':
                continue
            ao_lado = [r for r in recs if r[4] == 't' and r[0] < sx0 and sy0 <= (r[2] + r[3]) // 2 <= sy1]
            if ao_lado:
                x0, x1, _, _, _, texto = max(ao_lado, key=lambda r: r[1])
                out[f'{nome}@{sy0}'] = (texto, sx0 - x1, texto.endswith('…'), limite, rotulo)
    return out


def meses_das_amostras():
    """{sample: (year, month)} of the calendar samples, read from tools/tft_ui/amostras/graficos.cpp: the table's `{ "calendario_x", desCl<kVar>, ...` and the `calendario( year, month, ...` that builds kVar, directly or
    in the lambda that marks it busy. The month a screen is asked for comes from the sample's data and not from another run's drawing: a key that is wrong in the code is wrong in every language alike."""
    texto = (HERE / 'amostras' / 'graficos.cpp').read_text(encoding='utf-8')
    por_var = {m.group(1): (int(m.group(2)), int(m.group(3)))
               for m in re.finditer(r'const VmCalendario (\w+) = (?:\[\] \{\s*VmCalendario v = )?calendario\( (\d{4}), (\d+),', texto)}
    return {m.group(1): por_var[m.group(2)] for m in re.finditer(r'\{ "(calendario_\w+)", desCl<(\w+)>', texto) if m.group(2) in por_var}


def calendarios(pasta):
    """{sample: (title or None, [(x0, x1, text)])} for the calendar samples of one group folder and language: the bar's title ("Out 2026") and the weekday header, left to right, read
    from the light theme's records. The header is the texts whose ink lies between the bar (y 28) and the first week (y 50), and the title the one text of the bar that is a name and a year."""
    out = {}
    for f in sorted(pasta.glob('calendario_*_claro.textos')):
        nome = f.name[:-len('_claro.textos')]
        recs = [ln.split(' ', 5) for ln in f.read_text(encoding='utf-8').splitlines()]
        recs = [(int(r[0]), int(r[1]), int(r[2]), int(r[3]), r[5]) for r in recs if len(r) > 5 and r[4] == 't']
        titulo = [t for x0, x1, y0, y1, t in recs if y1 <= 28 and re.fullmatch(r'\S+ \d{4}', t)]
        out[nome] = (titulo[0] if len(titulo) == 1 else None, sorted((x0, x1, t) for x0, x1, y0, y1, t in recs if 30 <= y0 and y1 < 50))
    return out


def chaves_da_fonte(funcao, primeiro, quantos):
    """[key] the source's `funcao` ( ) of src/ui/angulo/telas/GraficosCalendario.cpp asks the pack for, for the arguments primeiro .. primeiro + quantos - 1: the `case N: return UA_TXT( "key" );` that answers
    each, the `default:` answering the last. None where the source does not say. The calendar's samples draw five months (October, July, August, February, September), so this is the check of the others."""
    texto = (ROOT / 'src' / 'ui' / 'angulo' / 'telas' / 'GraficosCalendario.cpp').read_text(encoding='utf-8')
    m = re.search(r'const char \*' + funcao + r'\( int \w+ \) \{\n(.*?)\n\}\n', texto, re.S)
    if not m:
        return None
    casos = {int(n): k for n, k in re.findall(r'case (\d+): return UA_TXT\( "([^"]+)" \);', m.group(1))}
    padrao = re.findall(r'default: return UA_TXT\( "([^"]+)" \);', m.group(1))
    if len(padrao) != 1:
        return None
    casos[primeiro + quantos - 1] = padrao[0]
    return [casos.get(primeiro + i) for i in range(quantos)]


def folga_do_pe_da_ota(pasta, idioma):
    """{sample: px} for the update screens that end with the two foot lines ("Still on the current version." and "Tap the screen to go back.", the language's words): the distance from the lowest ink of what
    stands between the status line and the foot (the paragraphs) to the top of the foot's first line, read from the light theme's records."""
    pes = {traducao(idioma, 'Still on the current version.'), traducao(idioma, 'Tap the screen to go back.')}
    out = {}
    for f in sorted(pasta.glob('ota_*_claro.textos')):
        recs = [ln.split(' ', 5) for ln in f.read_text(encoding='utf-8').splitlines()]
        recs = [(int(r[2]), int(r[3]), r[5]) for r in recs if len(r) > 5 and r[4] == 't']
        pe = [r for r in recs if r[2] in pes]
        corpo = [r for r in recs if r[0] >= 100 and r[2] not in pes]
        if len(pe) == 2 and corpo:
            out[f.name[:-len('_claro.textos')]] = min(r[0] for r in pe) - max(r[1] for r in corpo)
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
            # The review of B5a: a "…" the translator wrote into a text whose English has none is a cut to the reader, not the screen's own copy
            # (B5a exempted every pack text that ends in "…"). Drawn in the base group's dialogs.
            'reticencia': ('Cancel', 'Cancelar…', 'base'),
        }
        grupo_da_planta = {n: (v[2] if len(v) > 2 else 'painel') for n, v in plantados.items()}
        packs = {nome: plantar(PACOTES['es'], v[0], v[1], tmp / f'es_{nome}.lng') for nome, v in plantados.items()}
        trabalhos = {('limpo', g, i): (g, i, None, limpo / i / g) for g in TODOS for i in IDIOMAS}
        for nome, pacote in packs.items():
            g = grupo_da_planta[nome]
            trabalhos[(nome, g, 'es')] = (g, 'es', pacote, tmp / nome / 'es' / g)
        # The review of B5a: a separator line that is not "," or "." ("5" turned 13,8 into 1358) must draw the point
        separador_planta = plantar(PACOTES['es'], CHAVE_SEPARADOR, '5', tmp / 'es_separador.lng')
        trabalhos[('separador', 'graficos', 'es')] = ('graficos', 'es', separador_planta, tmp / 'separador' / 'es' / 'graficos')
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

        # the identity bar's cancel button (the PIN keypad's and the text keyboard's): the bar keeps the action its box was measured from, so the target keeps its place against the label (S10c, B5b)
        cancela = {i: {} for i in IDIOMAS}
        for g in TODOS:
            for i in IDIOMAS:
                cancela[i].update({f'{g}/{n}': v for n, v in voltar_e_alvo(limpo / i / g, traducao(i, 'Cancel'), inerte).items()})
        iguais = sorted(set(cancela['en']) & set(cancela['pt']) & set(cancela['es']))
        grupos_can = sorted({n.split('/')[0] for n in iguais})
        caso(f'cancel button: samples with the identity bar in every language ({len(iguais)}, in {", ".join(grupos_can)})',
             len(iguais) >= PISO_CANCELAR and grupos_can == ['identidade'], f'{len(iguais)} of at least {PISO_CANCELAR}: {grupos_can}')
        movidos = [n for n in iguais if len({cancela[i][n] for i in IDIOMAS}) != 1]
        caso('cancel button: the gap from its target to its label is the same in the three languages', not movidos,
             '; '.join(f'{n}: ' + ', '.join(f'{i} {cancela[i][n]} px' for i in IDIOMAS) for n in movidos[:4]))

        # the Ajustes footers: each button's box is its label's ink and the kit's padding, in every language (S10c B4b)
        esp2, esp4 = espaco_do_kit('ESP2'), espaco_do_kit('ESP4')
        folgas = {i: {f'{g}/{k}': v for g in ('ajustes', 'identidade') for k, v in rodape_e_alvos(limpo / i / g).items()} for i in IDIOMAS}   # the account editor's footer is built like the Ajustes' (B5b)
        caso(f'footer buttons: the same buttons in every language ({len(folgas["pt"])} in pt-BR, in ajustes and identidade)',
             esp2 and esp4 and len(folgas['pt']) >= PISO_RODAPE and all(set(folgas[i]) == set(folgas['pt']) for i in IDIOMAS),
             f'{ {i: len(v) for i, v in folgas.items()} } of at least {PISO_RODAPE}; ESP2 {esp2}, ESP4 {esp4}')
        fora = sorted((i, k, v) for i in IDIOMAS for k, v in folgas[i].items() if v not in (2 * (esp2 or 0), 2 * (esp4 or 0)))
        caso(f'footer buttons: every box is its label\'s ink and the kit\'s padding ({2 * (esp2 or 0)} or {2 * (esp4 or 0)} px of slack)', not fora,
             '; '.join(f'{i} {k}: {v} px' for i, k, v in fora[:4]))
        mudou = [k for k in folgas['pt'] if len({folgas[i].get(k) for i in IDIOMAS}) != 1]
        caso('footer buttons: the slack is the same in the three languages', not mudou,
             '; '.join(f'{k}: ' + ', '.join(f'{i} {folgas[i].get(k)} px' for i in IDIOMAS) for k in mudou[:4]))

        # the decimal separator: the pack's text for "." in every language, in every group (S10c, B5a)
        achados = {i: [(f'{g}/{n}', t, s) for g in TODOS for n, t, s in decimais(limpo / i / g)] for i in IDIOMAS}
        caso(f'decimal separator: the packs\' text for "{CHAVE_SEPARADOR}" is "," in pt-BR and in es-ES',
             all(traducao(i, CHAVE_SEPARADOR) == SEPARADOR[i] for i in ('pt', 'es')), str({i: traducao(i, CHAVE_SEPARADOR) for i in ('pt', 'es')}))
        caso(f'decimal separator: the samples draw numbers with a decimal in every language ({len(achados["pt"])} in pt-BR)',
             all(len(achados[i]) >= PISO_DECIMAIS for i in IDIOMAS), str({i: len(v) for i, v in achados.items()}) + f' of at least {PISO_DECIMAIS}')
        for i in IDIOMAS:
            errados = [(n, t, s) for n, t, s in achados[i] if s != SEPARADOR[i]]
            caso(f'decimal separator in {i}: every number with a decimal is written with "{SEPARADOR[i]}" ({len(achados[i])} found)', not errados,
                 '; '.join(f'{n} "{t}" has "{s}"' for n, t, s in errados[:4]) + f' ({len(errados)} of {len(achados[i])})')

        # the placeholder of a missing value: the same separator as the numbers (S10c, B5b: the Painel's dec( ) wrote a literal comma in every language)
        faltas = {i: [(f'{g}/{n}', t, s) for g in TODOS for n, t, s in sem_dado(limpo / i / g)] for i in IDIOMAS}
        caso(f'missing value: the samples draw the placeholder in every language ({len(faltas["pt"])} in pt-BR)',
             all(len(faltas[i]) >= PISO_SEM_DADO for i in IDIOMAS), str({i: len(v) for i, v in faltas.items()}) + f' of at least {PISO_SEM_DADO}')
        for i in IDIOMAS:
            errados = [(n, t, s) for n, t, s in faltas[i] if s != SEPARADOR[i]]
            caso(f'missing value in {i}: the placeholder is written with "{SEPARADOR[i]}" ({len(faltas[i])} found)', not errados,
                 '; '.join(f'{n} "{t}" has "{s}"' for n, t, s in errados[:4]) + f' ({len(errados)} of {len(faltas[i])})')

        # the badges: a name cut beside a badge takes the room the badge's drawn width leaves (S10c, B5b)
        selos = {i: selos_e_nomes(limpo / i / 'identidade', esp2 or 0) for i in IDIOMAS}
        caso(f'badges: the same rows carry a badge in every language ({len(selos["pt"])} in pt-BR, {sum(1 for v in selos["pt"].values() if v[2])} with a cut name)',
             esp2 and len(selos['pt']) >= PISO_SELOS and sum(1 for v in selos['pt'].values() if v[2]) >= 2 and all(set(selos[i]) == set(selos['pt']) for i in IDIOMAS),
             str({i: sorted(v) for i, v in selos.items()}))
        for i in IDIOMAS:
            palavras = {'quem': traducao(i, 'Locked'), 'usuarios': traducao(i, 'No PIN')}
            erradas = [f'{k}: "{v[4]}"' for k, v in selos[i].items() if v[4] != palavras[k.split('_')[0]]]
            caso(f'badges in {i}: every badge says the pack\'s word ("{palavras["quem"]}", "{palavras["usuarios"]}")', not erradas, '; '.join(erradas[:4]))
            perto = [f'{k} "{v[0]}" is {v[1]} px from its badge, the layout leaves {v[3]}' for k, v in selos[i].items() if v[1] < v[3]]
            caso(f'badges in {i}: no name comes nearer to its badge than the layout leaves it ({len(selos[i])} rows)', not perto, '; '.join(perto[:4]))
            longe = [f'{k} "{v[0]}" is {v[1]} px from its badge, the layout leaves {v[3]}' for k, v in selos[i].items() if v[2] and v[1] > v[3] + FOLGA_CORTE]
            caso(f'badges in {i}: a cut name ends within {FOLGA_CORTE} px of the room its badge leaves it', not longe, '; '.join(longe[:4]))

        # the calendar's names: the months of the title and the weekday header, in every language (S10c, B5a)
        caso('calendar names: the packs give the months and the weekday letters of the brief',
             all([traducao(i, k) for k in CHAVES_MES] == MESES[i].split() and [traducao(i, k) for k in CHAVES_SEMANA] == SEMANA[i].split() for i in IDIOMAS),
             str({i: ([traducao(i, k) for k in CHAVES_MES], [traducao(i, k) for k in CHAVES_SEMANA]) for i in IDIOMAS}))
        cal = {i: calendarios(limpo / i / 'graficos') for i in IDIOMAS}
        caso(f'calendar names: the same calendar samples in every language ({len(cal["pt"])} in pt-BR)',
             len(cal['pt']) >= PISO_CALENDARIO and all(set(cal[i]) == set(cal['pt']) for i in IDIOMAS), str({i: sorted(v) for i, v in cal.items()}))
        meses_fonte, semana_fonte = chaves_da_fonte('nomeDoMes', 1, 12), chaves_da_fonte('inicialDaSemana', 0, 7)
        caso('calendar names: the source asks the pack for the twelve months and the seven weekdays by the brief\'s keys, in order (the samples draw five months)',
             meses_fonte == CHAVES_MES and semana_fonte == CHAVES_SEMANA, f'months {meses_fonte}, weekdays {semana_fonte}')
        espaco2 = espaco_do_kit('ESP2')
        datas = meses_das_amostras()
        caso(f'calendar names: the samples\' own months are read from the sample file ({len(datas)} of the {len(cal["pt"])} samples)',
             set(datas) == set(cal['pt']), f'{sorted(datas)} against {sorted(cal["pt"])}')
        for i in IDIOMAS:
            ruins = []
            for nome, (titulo, cab) in sorted(cal[i].items()):
                ano, mes = datas.get(nome, (None, None))   # what the sample asks the screen for, not what another run drew
                esperado = f'{MESES[i].split()[mes - 1]} {ano}' if mes else None
                if titulo != esperado:
                    ruins.append(f'{nome}: title "{titulo}", expected "{esperado}"')
                if [t for _, _, t in cab] != SEMANA[i].split():
                    ruins.append(f'{nome}: header {[t for _, _, t in cab]}, expected {SEMANA[i].split()}')
                    continue
                for k, (x0, x1, t) in enumerate(cab):
                    centro = (espaco2 or 0) + 43 * k + 21.5   # the card starts ESP2 in, in columns of 43 px (GraficosCalendario.cpp: W_CEL)
                    if abs((x0 + x1) / 2 - centro) > 2:
                        ruins.append(f'{nome}: "{t}" ink {x0}..{x1} is not centred in its column (centre {centro})')
            caso(f'calendar names in {i}: the title\'s month and the seven weekday letters ({MESES[i].split()[9]} .. , {SEMANA[i]}) in every calendar sample, centred in their columns',
                 not ruins and espaco2, '; '.join(ruins[:4]) + f' ({len(ruins)})')

        # the update screens: the paragraphs stay clear of the foot in every language (S10c, B5a)
        ota = {i: folga_do_pe_da_ota(limpo / i / 'graficos', i) for i in IDIOMAS}
        caso(f'update screens: the same screens with a foot in every language ({len(ota["pt"])} in pt-BR)',
             len(ota['pt']) >= PISO_OTA and all(set(ota[i]) == set(ota['pt']) for i in IDIOMAS), str({i: sorted(v) for i, v in ota.items()}))
        for i in IDIOMAS:
            apertados = sorted((n, v) for n, v in ota[i].items() if v < FOLGA_OTA)
            caso(f'update screens in {i}: the paragraphs end {FOLGA_OTA} px or more above the foot note (the least is {min(ota[i].values(), default=None)} px)', not apertados,
                 '; '.join(f'{n} {v} px' for n, v in apertados[:4]))

        # 2. the plants fail exactly where their frames change
        esperado_check = {'glifo': 'glyphs or icons missing', 'rotulo': 'target', 'cartao': 'runs past the edge of the surface',
                          'acento': 'runs past the edge of the surface', 'reticencia': 'is cut with "…"'}
        campo = host_telas.campo_do_registro()
        caso('the recorder\'s field is read from Angulo.h', campo is not None and campo > 3, str(campo))
        bruto = plantados['acento'][1].encode('utf-8')
        caso(f'plant "acento": the recorder\'s cut at byte {campo} falls inside a character (else it tests nothing)',
             campo is not None and len(bruto) > campo and (bruto[campo] & 0xC0) == 0x80, f'{bruto[campo - 2:campo + 2]!r}' if campo else '')
        for nome in plantados:
            g = grupo_da_planta[nome]
            rc, saida = res[(nome, g, 'es')]
            falhou, soltas = falhas(saida)
            mudou = quadros_diferentes(limpo / 'es' / g, tmp / nome / 'es' / g) if (tmp / nome / 'es' / g).is_dir() else set()
            caso(f'plant "{nome}": the run fails', rc == 1, f'exit {rc}')
            caso(f'plant "{nome}": the samples that fail are exactly the samples whose frames changed ({sorted(mudou)})',
                 mudou and set(falhou) == mudou and not soltas, f'failing {sorted(falhou)}, changed {sorted(mudou)}, loose {soltas[:2]}')
            caso(f'plant "{nome}": the failure names its check ("{esperado_check[nome]}")',
                 any(esperado_check[nome] in ln for v in falhou.values() for ln in v), '\n'.join(next(iter(falhou.values()), [])[:2]))
            cheio = any('bytes the recorder keeps of a text' in ln for v in falhou.values() for ln in v)
            longo = campo is not None and len(plantados[nome][1].encode('utf-8')) > campo
            caso(f'plant "{nome}": ' + ('the run says the recorder\'s field was full' if longo else 'the run does not say the field was full'),
                 cheio == longo, f'{len(plantados[nome][1].encode("utf-8"))} bytes, field {campo}')

        # the separator's planted line: the point, on the same numbers the shipped pack draws (before the review of B5a, "5" was taken)
        rc, saida = res[('separador', 'graficos', 'es')]
        lidos, limpos = decimais(tmp / 'separador' / 'es' / 'graficos'), decimais(limpo / 'es' / 'graficos')
        caso(f'decimal separator: a pack line that is not "," or "." (planted "5") draws the point, on every number the shipped pack draws ({len(limpos)})',
             rc == 0 and limpos and len(lidos) == len(limpos) and all(s == '.' for _, _, s in lidos),
             f'exit {rc}, {len(lidos)} numbers against {len(limpos)}, separators {sorted({s for _, _, s in lidos})}')

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
            host_telas.ACEITOS.write_text('menus | menu_x | Conta: maria.a ¦ Account: maria. ¦ Cuenta: maria. | why\n', encoding='utf-8')
            err = io.StringIO()
            try:
                with contextlib.redirect_stderr(err):
                    fora = host_telas.aceitos('menus', 'xx')
                caso('accepted cuts: a language the lines do not list is refused and named, not a traceback', fora is None and 'xx' in err.getvalue(), err.getvalue()[-120:])
            except ValueError as e:
                caso('accepted cuts: a language the lines do not list is refused and named, not a traceback', False, f'ValueError: {e}')
        finally:
            host_telas.ACEITOS = orig

    # a screen's own ellipsis: the loading message is the pack's text since B5a, and is no cut (host_telas.textos_da_secao( ))
    for i in ('pt', 'es'):
        proprios = host_telas.textos_da_secao(host_telas.secao_angulo(PACOTES[i])[0])
        inteiro = traducao(i, 'Reading the history…')
        caso(f'own ellipsis in {i}: the loading message "{inteiro}" is a text of the pack, and a cut of it is not',
             inteiro.endswith('…') and inteiro in proprios and inteiro[:-2] + '…' not in proprios, inteiro)

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
