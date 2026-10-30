#!/usr/bin/env python3
"""test_web_omit_ui.py — the panel's UI driver cuts the right parts of the /license page (S10c C4, 2026-10-30).

The Angulo's fonts and icons are listed on the /license page of the images that carry them and of no other image. The
cut rides on the UI unit the way the radio's rides on [net_class.*] and the sensor slots' on [net.*]: [ui.original]
has `web_omit = "ui_angulo"`, so every image with the original panel (the default, too) drops the
`/* @IF ui_angulo */` block of WebUI.h, and an image with `ui = "angulo"` keeps it. The page's builder is
tools/build_webui_gz.py (`custom_web_omit`); compose( ) adds the UI's omit after the network's. This test holds:

  1. the manifest: [ui.original].web_omit is "ui_angulo", [ui.angulo] has none, and every profile of the manifest cuts
     `ui_angulo` if and only if its panel is not the Angulo; a mutation of the key (removed, renamed, put on the
     Angulo) gives a finding, or the test would not bite;
  2. the builder knows the feature (WEB_FEATURES, read from its source: the module only runs inside PlatformIO) and the
     cut pattern here is the builder's own;
  3. the page, cut the way the builder cuts it for each profile: the licence list names the Angulo's three sets
     (Bricolage Grotesque, GNU FreeFont FreeSans/FreeMono Bold, Lucide) with their licences and sources on the Angulo
     profiles and names none on the others; and the block is purely additive: the Angulo page without it is the page of the
     sibling profile with the original panel, byte for byte (pico_wiretft_angulo / pico_wiretft, pico2_w_release /
     pico2_w_original);
  4. the page marker mutated (the block no longer cut by the original's omit) gives a finding;
  5. check_manifest( ) refuses a [ui.*].web_omit that is not a simple name.

    python3 tools/test_web_omit_ui.py
"""
import copy
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402

falhas = []
casos = 0


def check(cond, msg):
    global casos
    casos += 1
    if not cond:
        falhas.append(msg)


def omitidos(c):
    return {n.strip() for n in (c["web_omit"] or "").split(",") if n.strip()}


def angulo(prof):
    return prof.get("ui", gf.DEFAULT_UI) == "angulo"


def achados_do_manifesto(M):
    out = []
    for nome in M["profiles"]:
        prof = gf.resolve_profile(nome, M["profiles"])
        try:
            c = gf.compose(prof, M)
        except SystemExit as e:
            out.append(f"1 {nome} nao compoe: {e}")
            continue
        corta = "ui_angulo" in omitidos(c)
        if angulo(prof) and corta:
            out.append(f"1 {nome} tem o painel Angulo e corta 'ui_angulo': {c['web_omit']!r}")
        if not angulo(prof) and not corta:
            out.append(f"1 {nome} tem o painel original e nao corta 'ui_angulo': {c['web_omit']!r}")
    return out


# 1. The manifest.
M = gf.load_manifest()
for a in achados_do_manifesto(M):
    check(False, a)
check(True, "1 as imagens do manifesto")
check(M["ui"]["original"].get("web_omit") == "ui_angulo", f"1 [ui.original] deveria ter web_omit = \"ui_angulo\": {M['ui']['original'].get('web_omit')!r}")
check("web_omit" not in M["ui"]["angulo"], f"1 [ui.angulo] nao deveria cortar nada: {M['ui']['angulo']!r}")
check(any(angulo(gf.resolve_profile(n, M["profiles"])) for n in M["profiles"]), "1 nenhum perfil do manifesto tem o painel Angulo")
mutacoes = {
    "tirado do original": lambda m: m["ui"]["original"].pop("web_omit", None),
    "renomeado no original": lambda m: m["ui"]["original"].update(web_omit="ui_angulo2"),
    "posto no angulo": lambda m: m["ui"]["angulo"].update(web_omit="ui_angulo"),
}
for rotulo, muda in mutacoes.items():
    m2 = copy.deepcopy(M)
    muda(m2)
    ach = achados_do_manifesto(m2)
    # a renamed key still composes but is not the name the page uses: the page check (3) is the one that sees it
    if rotulo.startswith("renomeado"):
        ach = ach or ([1] if m2["ui"]["original"]["web_omit"] != "ui_angulo" else [])
    check(ach, f"1 o web_omit {rotulo} nao deu achado: o teste nao morde")

# 2. The builder knows the name, and the cut pattern below is its own.
with open(os.path.join(ROOT, "tools", "build_webui_gz.py"), encoding="utf-8") as fh:
    fonte = fh.read()
tabela = re.search(r"^WEB_FEATURES = \{(.*?)^\}", fonte, re.S | re.M)
check(tabela is not None and re.search(r'^\s*"ui_angulo"\s*:', tabela.group(1), re.M),
      "2 WEB_FEATURES do build_webui_gz.py nao tem a feature 'ui_angulo'")
PADRAO = r"[ \t]*/\*\s*@IF\s+(!?\w+)\s*\*/[^\n]*\n(.*?)[ \t]*/\*\s*@ENDIF\s*\*/[^\n]*\n"
check(PADRAO in fonte, "2 o padrao de corte do teste nao e o do build_webui_gz.py (_IF_RE): atualize os dois")
IF_RE = re.compile(PADRAO, re.S)


def corta(texto, omit):
    def fica(feature):
        return (feature[1:] in omit) if feature.startswith("!") else (feature not in omit)
    return IF_RE.sub(lambda m: m.group(2) if fica(m.group(1)) else "", texto)


def lista(webui, omit):
    """The <pre> of the licence page after the cut for an image that omits `omit`."""
    t = corta(webui, omit)
    i = t.index('data-i18n="lic_notice"')
    a = t.index("<pre>", i) + len("<pre>")
    return t[a:t.index("</pre>", a)]


with open(os.path.join(ROOT, "WebUI.h"), encoding="utf-8") as fh:
    WEBUI = fh.read()
CABECALHO = "Angulo touch panel: fonts and icons"
SETS = ["Bricolage Grotesque", "GNU FreeFont (FreeSans, FreeMono Bold)", "Lucide",
        "SIL Open Font License 1.1", "GPL-3.0-or-later with the font exception", "ISC",
        "https://github.com/ateliertriay/bricolage", "https://www.gnu.org/software/freefont/", "https://lucide.dev"]


def paginas(M, WEBUI):
    """{profile: <pre> text} for every profile of the manifest."""
    out = {}
    for nome in M["profiles"]:
        prof = gf.resolve_profile(nome, M["profiles"])
        c = gf.compose(prof, M)
        out[nome] = (angulo(prof), lista(WEBUI, omitidos(c)))
    return out


def achados_das_paginas(M, WEBUI):
    out = []
    pg = paginas(M, WEBUI)
    for nome, (ang, pre) in pg.items():
        if ang:
            falta = [s for s in SETS if s not in pre]
            if falta or CABECALHO not in pre:
                out.append(f"3 {nome} (Angulo) nao lista: {falta or CABECALHO}")
        else:
            tem = [s for s in SETS[:3] + ["Angulo"] if s in pre]
            if tem:
                out.append(f"3 {nome} (original) lista do Angulo: {tem}")
    # additive: the Angulo page without its block is its sibling's page
    for nome, (ang, pre) in pg.items():
        if not ang or CABECALHO not in pre:
            continue
        irmao = nome.replace("_angulo", "") if nome.endswith("_angulo") else {"pico2_w_release": "pico2_w_original"}.get(nome)
        if irmao not in pg:
            out.append(f"3 {nome}: sem irmao com o painel original ({irmao!r})")
            continue
        i = pre.index(CABECALHO)
        i = pre.rindex("=" * 80, 0, i)                       # the bar above the heading
        j = pre.index("=" * 80 + "\n\nEND OF THIRD-PARTY NOTICES")
        if pre[:i] + pre[j:] != pg[irmao][1]:
            out.append(f"3 {nome} sem o bloco do Angulo nao e a pagina de {irmao}")
    return out


for a in achados_das_paginas(M, WEBUI):
    check(False, a)
check(True, "3 as paginas de licenca de todo perfil")
check(CABECALHO in WEBUI, "3 WebUI.h nao tem o bloco do Angulo")

# 4. The page marker renamed: the original's omit no longer cuts the block.
w2 = WEBUI.replace("/* @IF ui_angulo */", "/* @IF ui_angulo2 */")
check(w2 != WEBUI and not (CABECALHO in WEBUI and not achados_das_paginas(M, w2)),
      "4 o marcador do bloco renomeado nao deu achado: o teste nao morde")

# 5. check_manifest( ) refuses a web_omit that is not a simple name, on [ui.*].
for valor in ("ui angulo", "UI_ANGULO", "ui_angulo,tft", 7):
    m2 = copy.deepcopy(M)
    m2["ui"]["original"]["web_omit"] = valor
    try:
        gf.check_manifest(m2)
        recusou = False
    except SystemExit:
        recusou = True
    check(recusou, f"5 check_manifest( ) aceitou [ui.original].web_omit = {valor!r}")

print(f"[web omit ui] {casos} casos, {len(falhas)} falhas")
for f in falhas:
    print("  FALHA:", f)
sys.exit(1 if falhas else 0)
