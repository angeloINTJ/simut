#!/usr/bin/env python3
"""test_gen_notices.py — the notices follow the images (S10c C4, 2026-10-30).

tools/gen_notices.py writes the third-party list from tools/third_party.toml. Its `--check` already holds the three
outputs to the TOML; nothing held the TOML to the images. After C3 the Pico 2 W release became the Angulo image and
`pico2_w_angulo` ceased to exist, and the table still named it. This test holds:

  1. every image of the manifest ([ui_products.*]) is named by the generator (ENV) and the other way round, and each is
     either in `all_images` or a bench image;
  2. the rows marked `ui = "angulo"` name exactly the images whose profile has `ui = "angulo"` (and no other), and no
     other row is marked: so the next switch of a product's panel breaks this test, not the page;
  3. the family lists of the page and of the panel (the nine) hold none of those rows, so the originals' pages do not move;
  4. the Angulo rows' md label names their images and the original panel's FreeFont row does not name the Angulo images
     (nm of the eleven firmware.elf, 2026-10-30: the Angulo images keep none of the 9, 12 and 24 pt faces);
  5. the --check of the generator catches one image name removed from one row of the TOML, and a row whose `ui` mark is
     removed (a copy of the TOML given with --toml; the controls of the check, or it would not bite);
  6. the Angulo panel's own text (src/ui/angulo/telas/MenusLicenca.cpp) names every Angulo row and its licence, and
     the check says so when a name is missing.

    python3 tools/test_gen_notices.py
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402
import gen_notices as gn  # noqa: E402

falhas = []
casos = 0


def check(cond, msg):
    global casos
    casos += 1
    if not cond:
        falhas.append(msg)


M = gf.load_manifest()
data = gn.load()
imagens = set(M["ui_products"])
check(set(gn.ENV.values()) == imagens, f"1 ENV e as imagens do manifesto diferem: so no ENV {sorted(set(gn.ENV.values()) - imagens)}, so no manifesto {sorted(imagens - set(gn.ENV.values()))}")
bench = set(data["bench_images"])
for k in gn.ENV:
    check(k in data["all_images"] or k in bench, f"1 a imagem {k} nao esta em all_images nem em [bench_images]")
check("pico2_angulo" not in gn.ENV and "pico2_angulo" not in bench, "1 pico2_angulo (pico2_w_angulo, extinta no C3) ainda esta na tabela")

com_angulo = {k for k, n in gn.ENV.items() if n in imagens and gf.resolve_profile(n, M["profiles"]).get("ui", gf.DEFAULT_UI) == "angulo"}
marcadas = [c for c in data["component"] if c.get("ui")]
check(len(marcadas) == 3, f"2 deveria haver 3 linhas ui = \"angulo\": {[c['name'] for c in marcadas]}")
for c in marcadas:
    check(c["ui"] == "angulo", f"2 {c['name']}: ui = {c['ui']!r}")
    check(set(c["images"]) == com_angulo, f"2 {c['name']} nomeia {sorted(c['images'])}, as imagens com o painel Angulo sao {sorted(com_angulo)}")
radio, wired = gn.families(data)
nomes_gerais = {c["name"] for fam in (radio, wired) for c in gn.of_family(data, fam)}
check(not (nomes_gerais & {c["name"] for c in marcadas}), f"3 uma linha do Angulo esta nas listas das familias: {nomes_gerais & {c['name'] for c in marcadas}}")
for fam in (radio, wired):
    txt = gn.fw_list(data, fam) + gn.web_block(data, fam)
    check(not re.search(r"Bricolage|Lucide|FreeMono", txt), "3 a lista de uma familia fala do Angulo")

md = gn.notices_md(data)
linhas = {l.split("|")[1].strip(): l for l in md.splitlines() if l.startswith("| [")}
for c in marcadas:
    l = [v for k, v in linhas.items() if c["name"] in k][0]
    check("pico2_w_release" in l and "pico_wiretft_angulo" in l, f"4 a linha de {c['name']} no md nao nomeia as duas imagens: {l}")
orig = [v for k, v in linhas.items() if k.startswith("[GNU FreeFont (FreeSans Bold)")][0]
check("pico2_w_release" not in orig and "pico_wiretft_angulo" not in orig, f"4 a linha das faces do painel original nomeia uma imagem Angulo: {orig}")
check("pico2_w_original" in orig, "4 a linha das faces do painel original nao nomeia pico2_w_original")


def roda_check(toml):
    p = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_notices.py"), "--check", "--toml", toml],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


rc, _ = roda_check(os.path.join(ROOT, "tools", "third_party.toml"))
check(rc == 0, "5 --check falha na arvore")
with open(os.path.join(ROOT, "tools", "third_party.toml"), encoding="utf-8") as fh:
    toml = fh.read()
mutantes = {
    "um nome de imagem tirado de uma linha": toml.replace('images = ["wiretft_angulo", "pico2_release"]', 'images = ["pico2_release"]', 1),
    "a marca ui tirada de uma linha": toml.replace('ui = "angulo"\n', "", 1),
}
with tempfile.TemporaryDirectory() as tmp:
    for rotulo, t in mutantes.items():
        check(t != toml, f"5 a mutacao '{rotulo}' nao mudou o TOML")
        p = os.path.join(tmp, "m.toml")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write(t)
        rc, saida = roda_check(p)
        check(rc != 0, f"5 --check nao pegou: {rotulo}")

# 6. the panel's text.
with open(gn.ANGULO_PANEL, encoding="utf-8") as fh:
    painel = fh.read()
check(not gn.angulo_panel_problems(data, painel), f"6 o texto do painel nao concorda: {gn.angulo_panel_problems(data, painel)}")
for c in marcadas:
    nome = c["name"]
    check(gn.angulo_panel_problems(data, painel.replace(nome, "x" * len(nome))), f"6 o check nao pegou o nome '{nome}' tirado do painel")
    lic = c["license"]
    check(gn.angulo_panel_problems(data, painel.replace(lic, "y" * len(lic))), f"6 o check nao pegou a licenca '{lic}' tirada do painel")

# 7. the page says what the rows say: the "Images:" label of every row, read back from the generated text of each family, expands to exactly the images of the row in that
#    family (S10c C4 review: a `page_images` list made the FreeSans Bold row say, on the Angulo pages, that pico2_w_release carries the original panel's faces).
import html
KEY = {v: k for k, v in gn.ENV.items()}


def denota(label, fam):
    if label == "all images":
        return set(fam)
    if re.fullmatch(r"the \w+ images with the touch panel", label):
        return set(gn.PANEL) & set(fam)
    m = re.fullmatch(r"all images but (\S+)(?: and (\S+))?", label)
    if m:
        return set(fam) - {KEY[x] for x in m.groups() if x}
    return {KEY[x.strip()] for x in label.split(",")}


for nome_fam, fam in (("radio", radio), ("wired", wired)):
    texto = html.unescape(gn.web_block(data, fam))
    for n, c in enumerate(gn.of_family(data, fam), 1):
        m = re.search(rf"^{n}\. {re.escape(c['name'])}\n=+\n(.*?)\n\n", texto, re.M | re.S)
        lab = re.search(r"   Images:\s+(.*?)\n   URL:", m.group(1), re.S) if m else None
        check(lab is not None, f"7 {nome_fam}: {c['name']} sem linha Images: na pagina")
        if lab:
            label = " ".join(lab.group(1).split())
            check(denota(label, fam) == set(c["images"]) & set(fam),
                  f"7 {nome_fam}: {c['name']}: a pagina diz '{label}' = {sorted(denota(label, fam))}, a linha tem {sorted(set(c['images']) & set(fam))}")
orig = [c for c in data["component"] if c["name"] == "GNU FreeFont (FreeSans Bold)"][0]
check(set(orig["images"]) == {"release", "test", "test_https", "asserts", "wiretft", "pico2_original"}, f"7 a linha das faces do painel original tem {sorted(orig['images'])}")
check(all(k in gn.ROW_KEYS for c in data["component"] for k in c) if hasattr(gn, "ROW_KEYS") else False, "7 uma linha tem uma chave que o gerador nao conhece (gn.ROW_KEYS)")
with tempfile.TemporaryDirectory() as tmp:
    p = os.path.join(tmp, "m.toml")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(toml.replace('name = "GNU FreeFont (FreeSans Bold)"', 'name = "GNU FreeFont (FreeSans Bold)"\npage_images = ["release"]', 1))
    rc, saida = roda_check(p)
    check(rc != 0 and "page_images" in saida, f"7 uma chave desconhecida (page_images) numa linha nao foi recusada: rc {rc}: {saida[:120]}")

# 8. the rows against the symbols of an ELF (nm), with a fake nm: what pico2_w_release links (the nm of 2026-10-30 on its ELF, one symbol per probe), and what it does not.
VISTO = ["cyw43_init", "dhcp_server_init", "LEAmDNS::MDNSResponder::begin()", "Adafruit_GFX::drawPixel(short, short, unsigned short)", "Adafruit_ILI9341::begin(unsigned long)",
         "XPT2046_Touchscreen::begin()", "OneWirePIO::isSensorPresent()", "BuzzerPIO::stopMelody()", "PubSubClient::readPacket(unsigned char*)", "DHT22PIO::begin()",
         "br_ssl_client_init_full", "tcp_write", "lfs_mount", "tud_task_ext", "entrega::DISP9Glyphs", "entrega::MONO9Glyphs", "entrega::SANS9Glyphs", "entrega::ICONE_EYE_20_bits"]
with tempfile.TemporaryDirectory() as tmp:
    nm = os.path.join(tmp, "nm")
    with open(nm, "w", encoding="utf-8") as fh:
        fh.write("#!/bin/sh\ncat <<'EOF'\n" + "\n".join(f"10000000 T {s}" for s in VISTO) + "\nEOF\n")
    os.chmod(nm, 0o755)
    elf = os.path.join(tmp, "pico2_w_release", "firmware.elf")
    os.makedirs(os.path.dirname(elf))
    open(elf, "wb").close()

    def roda_elf(toml_path):
        p = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_notices.py"), "--check", "--toml", toml_path, "--elf", elf, "--nm", nm],
                           capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr
    rc, saida = roda_elf(os.path.join(ROOT, "tools", "third_party.toml"))
    check(rc == 0, f"8 o --elf falha na imagem Angulo com os simbolos dela: rc {rc}: {saida[-300:]}")
    casos_elf = {
        "a linha das faces do painel original nomeando pico2_release (a imagem nao tem o componente)": (toml.replace('images = ["release", "test", "test_https", "asserts", "wiretft", "pico2_original"]',
                                                                                                   'images = ["release", "test", "test_https", "asserts", "pico2_release", "wiretft", "pico2_original"]', 1), "FreeSansBold"),
        "a linha do Bricolage sem pico2_release (a imagem tem o componente)": (toml.replace('images = ["wiretft_angulo", "pico2_release"]', 'images = ["wiretft_angulo"]', 1), "entrega::DISP"),
        "a linha do BTstack nomeando pico2_release (a imagem nao tem o componente)": (toml.replace('images = ["alpha", "air"]', 'images = ["alpha", "air", "pico2_release"]', 1), "btstack"),
    }
    for rotulo, (t2, simbolo) in casos_elf.items():
        check(t2 != toml, f"8 a mutacao '{rotulo}' nao mudou o TOML")
        p = os.path.join(tmp, "m2.toml")
        with open(p, "w", encoding="utf-8") as fh:
            fh.write(t2)
        rc, saida = roda_elf(p)
        check(rc != 0 and simbolo in saida, f"8 --elf nao pegou: {rotulo}: rc {rc}: {saida[-200:]}")

# 9. the font of the live LCD view (2026-11-04): a row marked `web = "lcd"` is in the block of the /license page that WebUI.h guards with /* @IF lcd */, and in no family list and no
#    panel text, so the nine images without the LCD keep their bytes. The honest equivalent of the symbol probe: `images` are the images whose web features keep the block.
import shutil
from pathlib import Path

web = gn.web_rows(data)
check(len(web) == 1 and web[0]["name"] == "X.Org misc-fixed 5x7", f"9 deveria haver uma linha web = \"lcd\", a da fonte: {[c['name'] for c in web]}")
fonte = web[0] if web else {"name": "?", "images": []}
check(set(fonte["images"]) == {"alpha", "wire"}, f"9 a fonte nomeia {sorted(fonte['images'])}, deveria ser alpha e wire")
check(set(fonte["images"]) == gn.web_images("lcd"), f"9 as imagens da fonte {sorted(fonte['images'])} diferem das que mantem a feature web lcd: {sorted(gn.web_images('lcd'))}")
check(not gn.web_rows_problems(data), f"9 o check de imagens da linha web reclama na arvore: {gn.web_rows_problems(data)}")
check(not (gn.web_images("lcd") & {k for k in gn.ENV if k in data["all_images"] and k not in ("alpha", "wire")}), "9 uma das outras sete imagens mantem a feature web lcd")
for nome_fam, fam in (("radio", radio), ("wired", wired)):
    check(fonte["name"] not in {c["name"] for c in gn.of_family(data, fam)}, f"9 a linha da fonte esta na lista da familia {nome_fam}")
    check("misc-fixed" not in gn.fw_list(data, fam) + html.unescape(gn.web_block(data, fam)), f"9 o texto da familia {nome_fam} (painel e pagina) fala da fonte")
with open(gn.WEBUI, encoding="utf-8") as fh:
    webui = fh.read()
pagina = gn.new_webui(webui, data)
k = pagina.index('data-i18n="lic_notice"')
pre = pagina[pagina.index("<pre>", k):pagina.index("</pre>", k)]   # the notices of /license: DASH_PAGE has @IF lcd blocks of its own
a, b = pre.index("/* @IF lcd */"), pre.index("/* @ENDIF */", pre.index("/* @IF lcd */"))
check("X.Org misc-fixed 5x7" in pre[a:b] and pre.count("X.Org misc-fixed 5x7") == 1, "9 a fonte nao esta (uma vez) no bloco @IF lcd da pagina /license")
check(pre.index("/* @IF ui_angulo */") < a and b < pre.index("END OF THIRD-PARTY NOTICES"), "9 o bloco lcd nao esta depois do bloco do Angulo e antes da barra de fechamento")
with open(gn.FWH, encoding="utf-8") as fh:
    check("misc-fixed" not in gn.new_fwh(fh.read(), webui, data) and "misc-fixed" not in open(gn.FWH, encoding="utf-8").read(), "9 HelpLicenseEN.h fala da fonte")

# the page of each image after the build's cut (build_webui_gz._strip_web_features, the image's custom_web_omit from the manifest): the notice is in alpha and Wire and in none of the nine
os.chdir(ROOT)
ns = {"__name__": "build_webui_gz"}
with open(os.path.join(ROOT, "tools", "build_webui_gz.py"), encoding="utf-8") as fh:
    exec(compile(fh.read().replace("\ngenerate()\n", "\n"), "build_webui_gz.py", "exec"), ns)
for env in sorted(gn.ENV.values()):
    c = gf.compose(gf.resolve_profile(env, M["profiles"]), M)
    ns["WEB_OMIT"] = {w.strip() for w in (c["web_omit"] or "").split(",") if w.strip()}
    cortada = ns["_strip_web_features"](webui)
    lic = cortada[cortada.index("static const char LICENSE_PAGE[]"):]
    lic = lic[:lic.index(')raw";')]
    tem = "X.Org misc-fixed 5x7" in lic
    check(tem == (env in ("pico_w_alpha", "pico_wire")), f"9 {env}: a pagina /license cortada {'tem' if tem else 'nao tem'} a fonte, deveria {'nao ' if tem else ''}ter")
    check("@IF" not in lic and ("Live LCD view" in lic) == tem, f"9 {env}: sobrou marcador @IF na pagina cortada ou o cabecalho do bloco nao acompanha a fonte")

# the controls: a TOML given with --toml, a scratch tree for what only the files can say
check("web = \"lcd\"\n" in toml, "9 o TOML nao tem a marca web = \"lcd\"")
with tempfile.TemporaryDirectory() as tmp:
    def com(t, rotulo):
        check(t != toml, f"9 a mutacao '{rotulo}' nao mudou o TOML")
        pth = os.path.join(tmp, "m3.toml")
        with open(pth, "w", encoding="utf-8") as fh:
            fh.write(t)
        return pth
    # an unknown marker is refused, and so is a row marked ui and web
    pth = com(toml.replace('web = "lcd"\n', 'web = "xyz"\n', 1), "marca web desconhecida")
    rc, saida = roda_check(pth)
    check(rc != 0 and "web" in saida and "xyz" in saida, f"9 uma marca web desconhecida nao foi recusada: rc {rc}: {saida[:160]}")
    pth = com(toml.replace('web = "lcd"\n', 'web = "lcd"\nui = "angulo"\n', 1), "ui e web na mesma linha")
    rc, saida = roda_check(pth)
    check(rc != 0 and "not both" in saida, f"9 uma linha com ui e web nao foi recusada: rc {rc}: {saida[:160]}")
    # mutant 1: the marker dropped: the row falls into both family lists (so the nine would change), and --check says the page and the panel text are out of date
    pth = com(toml.replace('web = "lcd"\n', "", 1), "a marca web tirada")
    rc, saida = roda_check(pth)
    check(rc != 0 and "WebUI.h" in saida and "HelpLicenseEN.h" in saida, f"9 sem a marca, --check nao diz que a pagina e o texto do painel mudariam: rc {rc}: {saida[-260:]}")
    guarda = gn.TOML
    try:
        gn.TOML = Path(pth)
        d2 = gn.load()
    finally:
        gn.TOML = guarda
    check("X.Org misc-fixed 5x7" in {c["name"] for c in gn.of_family(d2, radio)} and "X.Org misc-fixed 5x7" in {c["name"] for c in gn.of_family(d2, wired)}
          and "misc-fixed" in gn.fw_list(d2, radio) and "misc-fixed" in gn.fw_list(d2, wired),
          "9 sem a marca a linha deveria cair nas duas listas de familia e no texto do painel (as nove imagens mudariam)")
    # the images of the row against the manifest: one image dropped, one image too many
    for rotulo, imgs in (("sem wire", '["alpha"]'), ("com wiretft a mais", '["alpha", "wire", "wiretft"]')):
        pth = com(toml.replace('images = ["alpha", "wire"]', f"images = {imgs}", 1), "imagens da fonte " + rotulo)
        rc, saida = roda_check(pth)
        check(rc != 0 and "web features keep" in saida, f"9 --check nao pegou as imagens da fonte {rotulo}: rc {rc}: {saida[-200:]}")
    # mutant 2: the block edited by hand in WebUI.h (a scratch tree: the gate reads its own tree), and the control, the same tree untouched
    arv = os.path.join(tmp, "arv")
    os.makedirs(os.path.join(arv, "tools"))
    os.makedirs(os.path.join(arv, "src", "ui", "angulo", "telas"))
    for f in os.listdir(os.path.join(ROOT, "tools")):
        if f.endswith((".py", ".toml", ".json")) and os.path.isfile(os.path.join(ROOT, "tools", f)):
            shutil.copy(os.path.join(ROOT, "tools", f), os.path.join(arv, "tools", f))
    shutil.copytree(os.path.join(ROOT, "LICENSES"), os.path.join(arv, "LICENSES"))
    for f in ("THIRD_PARTY_NOTICES.md", "WebUI.h", os.path.join("src", "HelpLicenseEN.h"), os.path.join("src", "ui", "angulo", "telas", "MenusLicenca.cpp")):
        shutil.copy(os.path.join(ROOT, f), os.path.join(arv, f))

    def check_arv():
        q = subprocess.run([sys.executable, os.path.join(arv, "tools", "gen_notices.py"), "--check"], capture_output=True, text=True)
        return q.returncode, q.stdout + q.stderr
    rc, saida = check_arv()
    check(rc == 0, f"9 o --check falha na copia da arvore, sem mutacao (o controle): rc {rc}: {saida[-200:]}")
    with open(os.path.join(arv, "WebUI.h"), encoding="utf-8") as fh:
        w = fh.read()
    i = w.index("/* @IF lcd */\n=====")
    j = w.index("A. X.Org misc-fixed 5x7", i)
    with open(os.path.join(arv, "WebUI.h"), "w", encoding="utf-8") as fh:
        fh.write(w[:j] + "A. X.Org misc-fixed 5x8" + w[j + len("A. X.Org misc-fixed 5x7"):])
    rc, saida = check_arv()
    check(rc != 0 and "WebUI.h" in saida, f"9 o bloco lcd editado a mao nao foi pego pelo --check: rc {rc}: {saida[-200:]}")

print(f"[gen notices] {casos} casos, {len(falhas)} falhas")
for f in falhas:
    print("  FALHA:", f)
sys.exit(1 if falhas else 0)
