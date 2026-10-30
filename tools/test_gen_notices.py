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

print(f"[gen notices] {casos} casos, {len(falhas)} falhas")
for f in falhas:
    print("  FALHA:", f)
sys.exit(1 if falhas else 0)
