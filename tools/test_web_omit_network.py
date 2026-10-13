#!/usr/bin/env python3
"""test_web_omit_network.py — a rede escolhida corta as paginas web certas.

Uma imagem com a rede por cabo (a classe `lan`, a do driver enc28j60) nao tem radio: o SSID,
a senha, a busca de redes e o sinal do painel nao lhe pertencem. A classe diz isso ao
montador das paginas (tools/build_webui_gz.py) pelo `web_omit = "wifi"` de [net_class.lan],
o espelho do `off_web_omit` que as chaves usam quando estao desligadas. Este teste prende:

  1. toda imagem do manifesto cujo `net` e de uma classe com `web_omit` corta o que ela
     diz ("wifi" na classe lan), e nenhuma outra corta;
  2. `wifi` e uma feature que o montador conhece (WEB_FEATURES), lida do fonte dele;
  3. compose( ) aplica o web_omit da classe da rede escolhida e o off_web_omit so da chave
     desligada, num manifesto de brinquedo (o `net` do perfil alterna entre cyw43 e
     enc28j60), sem alterar o perfil nem o manifesto que recebe;
  4. check_manifest( ) recusa um web_omit de classe que nao seja um nome simples.

    python3 tools/test_web_omit_network.py
"""
import copy
import os
import re
import subprocess
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


# 1. As imagens do manifesto.
M = gf.load_manifest()
for nome in M["profiles"]:
    prof = gf.resolve_profile(nome, M["profiles"])
    c = gf.compose(prof, M)
    classe = M["net"][prof["net"]]["class"]
    if classe == "lan":
        check("wifi" in omitidos(c), f"1 {nome} tem a rede por cabo e nao corta 'wifi' das paginas: {c['web_omit']!r}")
    else:
        check("wifi" not in omitidos(c), f"1 {nome} tem o radio e corta 'wifi' das paginas: {c['web_omit']!r}")
check(M["net_class"]["lan"].get("web_omit") == "wifi",
      f"1 [net_class.lan] deveria ter web_omit = \"wifi\": {M['net_class']['lan'].get('web_omit')!r}")
check("web_omit" not in M["net_class"]["wifi"],
      f"1 [net_class.wifi] nao deveria cortar nada: {M['net_class']['wifi']!r}")

# 2. O montador conhece o nome. O modulo dele so roda dentro do PlatformIO, entao a
#    tabela e lida do fonte.
with open(os.path.join(ROOT, "tools", "build_webui_gz.py"), encoding="utf-8") as fh:
    fonte = fh.read()
tabela = re.search(r"^WEB_FEATURES = \{(.*?)^\}", fonte, re.S | re.M)
check(tabela is not None and re.search(r'^\s*"wifi"\s*:', tabela.group(1), re.M),
      "2 WEB_FEATURES do build_webui_gz.py nao tem a feature 'wifi'")

# 3. compose( ) num manifesto de brinquedo: a classe lan com web_omit (e a wifi sem) e uma chave
#    qualquer com off_web_omit, nas quatro combinacoes de rede (cyw43 ou enc28j60) e chave.
base = copy.deepcopy(M)
for nome_chave, chave in base["toggles"].items():
    chave.pop("off_web_omit", None)
for classe in base["net_class"].values():
    classe.pop("web_omit", None)
base["net_class"]["lan"]["web_omit"] = "wifi"
outra = gf.TOGGLE_ORDER[0]
base["toggles"][outra]["off_web_omit"] = "syslog"
perfil0 = dict(gf.resolve_profile("pico_wire", M["profiles"]))
for rede in ("cyw43", "enc28j60"):
    cabo = rede == "enc28j60"
    for ligada in (False, True):
        prof = dict(perfil0, net=rede, **{outra: ligada})
        antes_p, antes_m = copy.deepcopy(prof), copy.deepcopy(base)
        try:
            c = gf.compose(prof, base)
        except SystemExit as e:
            check(False, f"3 compose recusou net={rede} {outra}={ligada}: {e}")
            continue
        o = omitidos(c)
        check(("wifi" in o) == cabo, f"3 net={rede}: 'wifi' cortado={('wifi' in o)}")
        check(("syslog" in o) == (not ligada), f"3 {outra}={ligada}: 'syslog' cortado={('syslog' in o)}")
        check(prof == antes_p, "3 compose alterou o perfil que recebeu")
        check(base == antes_m, "3 compose alterou o manifesto que recebeu")


# 4. check_manifest( ) recusa um nome que nao e simples. Roda num processo filho com o
#    manifesto adulterado, porque check_manifest sai com sys.exit.
def recusa(valor):
    codigo = (
        "import sys; sys.path.insert(0, %r)\n"
        "import gen_features as gf\n"
        "M = gf.load_manifest()\n"
        "M['net_class']['lan']['web_omit'] = %r\n"
        "gf.check_manifest(M)\n"
    ) % (os.path.join(ROOT, "tools"), valor)
    r = subprocess.run([sys.executable, "-B", "-c", codigo], capture_output=True, text=True)
    return r.returncode != 0, (r.stdout + r.stderr).strip()


for ruim in ("", "wifi, tft", "Wi-Fi", 3, ["wifi"]):
    recusou, saida = recusa(ruim)
    check(recusou, f"4 check_manifest aceitou [net_class.lan].web_omit = {ruim!r}")
recusou, saida = recusa("wifi")
check(not recusou, f"4 check_manifest recusou [net_class.lan].web_omit = 'wifi': {saida}")

if falhas:
    print("test_web_omit_network: FALHOU")
    for f in falhas:
        print("  " + f)
    sys.exit(1)
print(f"test_web_omit_network: {casos} casos.")
