#!/usr/bin/env python3
"""test_web_omit_network.py — a rede escolhida corta as paginas web certas.

Uma imagem com a rede por cabo (a classe `lan`, a do driver enc28j60) corta duas coisas do WebUI.h,
por dois motivos que nao sao o mesmo:

  - `wifi`, pela CLASSE (`web_omit = "wifi"` de [net_class.lan]): nao tem radio, entao o SSID, a senha,
    a busca de redes e o sinal do painel nao lhe pertencem;
  - `slots16`, pelo DRIVER (`web_omit = "slots16"` de [net.enc28j60]): o modulo ENC28J60 toma o
    GP11-GP15, que eram cinco dos dezesseis slots de sensor, e a pagina diz onze. Outro modulo cabeado
    nao tomaria esses pinos, e por isso isto e do driver e nao da classe (2026-10-13; antes os doze
    blocos de slots eram `@IF wifi`, e "tem radio" e "tem dezesseis slots" eram a mesma pergunta).

O montador das paginas e tools/build_webui_gz.py (`custom_web_omit`); compose( ) soma o da classe e,
depois dele, o do driver. Este teste prende:

  1. toda imagem do manifesto com a rede por cabo corta `wifi` e `slots16`, e nenhuma imagem com o
     radio corta um dos dois; e que uma mutacao do `web_omit` do driver (tirado, trocado por outro
     nome, ou posto no driver do radio) da um achado, ou o teste nao morderia;
  2. `wifi` e `slots16` sao features que o montador conhece (WEB_FEATURES), lidas do fonte dele;
  3. compose( ) aplica o web_omit da classe e o do driver da rede escolhida, nessa ordem, e o
     off_web_omit so da chave desligada, num manifesto de brinquedo (o `net` do perfil alterna entre
     cyw43 e enc28j60), sem alterar o perfil nem o manifesto que recebe;
  4. check_manifest( ) recusa um web_omit de classe ou de driver que nao seja um nome simples;
  5. no WebUI.h os blocos de slots sao `slots16` (em pares: o `@IF slots16` e o `@IF !slots16` do
     mesmo trecho), e nenhum bloco `wifi` fala de slots.

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


def achados_das_imagens(M):
    """Os achados de uma imagem que corta o que nao devia, ou que nao corta o que devia: as duas
    features de rede, `wifi` (a classe) e `slots16` (o driver), contra a classe de cada perfil."""
    out = []
    for nome in M["profiles"]:
        prof = gf.resolve_profile(nome, M["profiles"])
        try:
            c = gf.compose(prof, M)
        except SystemExit as e:
            out.append(f"1 {nome} nao compoe: {e}")
            continue
        cabo = M["net"][prof["net"]]["class"] == "lan"
        corta = omitidos(c)
        for feature in ("wifi", "slots16"):
            if cabo and feature not in corta:
                out.append(f"1 {nome} tem a rede por cabo e nao corta '{feature}' das paginas: {c['web_omit']!r}")
            if not cabo and feature in corta:
                out.append(f"1 {nome} tem o radio e corta '{feature}' das paginas: {c['web_omit']!r}")
    return out


# 1. As imagens do manifesto.
M = gf.load_manifest()
for achado in achados_das_imagens(M):
    check(False, achado)
check(True, "1 as imagens do manifesto")
check(M["net_class"]["lan"].get("web_omit") == "wifi",
      f"1 [net_class.lan] deveria ter web_omit = \"wifi\": {M['net_class']['lan'].get('web_omit')!r}")
check("web_omit" not in M["net_class"]["wifi"],
      f"1 [net_class.wifi] nao deveria cortar nada: {M['net_class']['wifi']!r}")
check(M["net"]["enc28j60"].get("web_omit") == "slots16",
      f"1 [net.enc28j60] deveria ter web_omit = \"slots16\": {M['net']['enc28j60'].get('web_omit')!r}")
check("web_omit" not in M["net"]["cyw43"], f"1 [net.cyw43] nao deveria cortar nada: {M['net']['cyw43']!r}")

# 1b. O teste morde: uma mutacao do web_omit do driver da um achado.
mutacoes = {
    "tirado do enc28j60": lambda m: m["net"]["enc28j60"].pop("web_omit", None),
    "trocado por tft no enc28j60": lambda m: m["net"]["enc28j60"].update(web_omit="tft"),
    "trocado por wifi no enc28j60": lambda m: m["net"]["enc28j60"].update(web_omit="wifi"),
    "posto no cyw43": lambda m: m["net"]["cyw43"].update(web_omit="slots16"),
    "tirado da classe lan": lambda m: m["net_class"]["lan"].pop("web_omit", None),
}
for rotulo, muda in mutacoes.items():
    m2 = copy.deepcopy(M)
    muda(m2)
    check(achados_das_imagens(m2), f"1b o web_omit {rotulo} nao deu achado: o teste nao morde")

# 2. O montador conhece os nomes. O modulo dele so roda dentro do PlatformIO, entao a tabela e lida
#    do fonte.
with open(os.path.join(ROOT, "tools", "build_webui_gz.py"), encoding="utf-8") as fh:
    fonte = fh.read()
tabela = re.search(r"^WEB_FEATURES = \{(.*?)^\}", fonte, re.S | re.M)
for feature in ("wifi", "slots16"):
    check(tabela is not None and re.search(rf'^\s*"{feature}"\s*:', tabela.group(1), re.M),
          f"2 WEB_FEATURES do build_webui_gz.py nao tem a feature '{feature}'")

# 3. compose( ) num manifesto de brinquedo: a classe lan com web_omit (e a wifi sem), o driver enc28j60
#    com o dele (e o cyw43 sem) e uma chave qualquer com off_web_omit, nas quatro combinacoes de rede
#    (cyw43 ou enc28j60) e chave.
base = copy.deepcopy(M)
for nome_chave, chave in base["toggles"].items():
    chave.pop("off_web_omit", None)
for classe in base["net_class"].values():
    classe.pop("web_omit", None)
for driver in base["net"].values():
    driver.pop("web_omit", None)
base["net_class"]["lan"]["web_omit"] = "wifi"
base["net"]["enc28j60"]["web_omit"] = "slots16"
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
        check(("slots16" in o) == cabo, f"3 net={rede}: 'slots16' cortado={('slots16' in o)}")
        check(("syslog" in o) == (not ligada), f"3 {outra}={ligada}: 'syslog' cortado={('syslog' in o)}")
        if cabo:
            ordem = [n.strip() for n in c["web_omit"].split(",")]
            check("wifi" in ordem and "slots16" in ordem and ordem.index("wifi") + 1 == ordem.index("slots16"),
                  f"3 o web_omit do driver (slots16) vem logo depois do da classe (wifi): {ordem}")
        check(prof == antes_p, "3 compose alterou o perfil que recebeu")
        check(base == antes_m, "3 compose alterou o manifesto que recebeu")


# 4. check_manifest( ) recusa um nome que nao e simples, na classe e no driver. Roda num processo filho
#    com o manifesto adulterado, porque check_manifest sai com sys.exit.
def recusa(tabela_do_manifesto, id_, valor):
    codigo = (
        "import sys; sys.path.insert(0, %r)\n"
        "import gen_features as gf\n"
        "M = gf.load_manifest()\n"
        "M[%r][%r]['web_omit'] = %r\n"
        "gf.check_manifest(M)\n"
    ) % (os.path.join(ROOT, "tools"), tabela_do_manifesto, id_, valor)
    r = subprocess.run([sys.executable, "-B", "-c", codigo], capture_output=True, text=True)
    return r.returncode != 0, (r.stdout + r.stderr).strip()


for tabela_do_manifesto, id_, bom in (("net_class", "lan", "wifi"), ("net", "enc28j60", "slots16")):
    for ruim in ("", "slots16, tft", "Slots-16", 3, ["slots16"]):
        recusou, saida = recusa(tabela_do_manifesto, id_, ruim)
        check(recusou, f"4 check_manifest aceitou [{tabela_do_manifesto}.{id_}].web_omit = {ruim!r}")
        if recusou and id_ == "enc28j60":
            check("web_omit" in saida and "enc28j60" in saida,
                  f"4 a recusa do web_omit do driver nao diz o que e onde: {saida[-200:]!r}")
    recusou, saida = recusa(tabela_do_manifesto, id_, bom)
    check(not recusou, f"4 check_manifest recusou [{tabela_do_manifesto}.{id_}].web_omit = {bom!r}: {saida}")

# 5. O WebUI.h: os blocos de slots sao `slots16`, aos pares, e nenhum bloco `wifi` fala de slots.
with open(os.path.join(ROOT, "WebUI.h"), encoding="utf-8") as fh:
    pagina = fh.read()
blocos = re.findall(r"[ \t]*/\*\s*@IF\s+(!?\w+)\s*\*/[^\n]*\n(.*?)[ \t]*/\*\s*@ENDIF\s*\*/[^\n]*\n", pagina, re.S)
# Os jeitos de a pagina dizer quantos slots de sensor ha: o texto de ajuda, as etiquetas, os limites.
SLOTS = re.compile(r"GP0-GP1[05]|All 1[16] slots|\{t1[05]\}|\(0-1[05]\)|> 15\b|rec\.slots\.length|i < 16|cs\.length \|\| 11")
pos = [b for f, b in blocos if f == "slots16"]
neg = [b for f, b in blocos if f == "!slots16"]
check(pos and len(pos) == len(neg),
      f"5 os blocos `@IF slots16` ({len(pos)}) e `@IF !slots16` ({len(neg)}) do WebUI.h vem em pares")
for f, b in blocos:
    if f.lstrip("!") == "slots16":
        check(SLOTS.search(b), f"5 um bloco @IF {f} do WebUI.h nao fala de slots: {b.strip()[:90]!r}")
    if f.lstrip("!") == "wifi":
        achou = SLOTS.search(b)
        check(not achou, f"5 um bloco @IF {f} do WebUI.h fala de slots ({achou and achou.group(0)!r}); "
                         f"a quantidade de slots e do driver (@IF slots16), nao do radio: {b.strip()[:90]!r}")

if falhas:
    print("test_web_omit_network: FALHOU")
    for f in falhas:
        print("  " + f)
    sys.exit(1)
print(f"test_web_omit_network: {casos} casos.")
