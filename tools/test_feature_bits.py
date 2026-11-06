#!/usr/bin/env python3
"""Testa o "feat" do /api/status: os bits que a imagem diz ter sao as chaves do
configurador com que ela foi compilada.

Rodar: python3 tools/test_feature_bits.py

O src/FeatureBits.h e gerado do tools/features.toml, mas o valor que o firmware
imprime nao vem do manifesto: vem dos macros que o compilador de fato viu. Este
teste junta as duas pontas sem compilar firmware nenhum. Para cada um dos seis
perfis, e para cada chave invertida a partir deles, compoe os flags como o build
compoe (gen_features.compose) e pede ao pre-processador do host o valor de
SIMUT_FEATURE_BITS — o mesmo simut_config.h, os mesmos defaults. Tem de dar a
mascara das chaves ligadas. As chaves sao independentes (cada bit le o seu
macro), entao perfil + inversao unica cobre qualquer combinacao que o
configurador monte; o tft_graph, cujo default segue o mostrador, e coberto pelos
perfis de cada mostrador.

Uma combinacao que quebra regra e pulada: o configurador e o build_custom.py a
recusam antes de compilar, e algumas param num #error do simut_config.h. O mesmo
vale para um driver de rede sem placa para o chip do perfil (o ENC28J60 no RP2350,
gen_features.net_refusal): o gerador o recusa, entao nao ha flags para pre-processar.

Os bits vem de dois lugares do manifesto (2026-10-13): as chaves ([toggles.*]) e as
classes de rede ([net_class.*]) que tem `bit`. A rede e uma escolha por perfil (`net`),
e o bit de uma classe vale quando a imagem escolhe um driver dessa classe: o bit 18, a
classe `lan` (o ENC28J60), era o da chave `lan`.

As tags de boot (2026-11-06, secao 4): o SIMUT_BOOT_TAGS, de src/display/BootReplay.h, vale 1 so na
pico_w_test. O teste pre-processa o cabecalho de verdade com os flags de cada perfil e confere o
conjunto de imagens que levam as tags contra a regra escrita nele (console completo, painel TFT e
nenhum servidor HTTPS); cada condicao tirada de uma copia do cabecalho muda o conjunto exatamente
como o manifesto manda. `--header ARQUIVO` julga outra copia do cabecalho no lugar do da arvore (o
controle negativo: o do commit anterior, em que a pico_w_test_https tambem levava as tags e deixou
de linkar).
"""
import copy
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402

ok = fail = 0


def check(label, cond, detail=""):
    global ok, fail
    if cond:
        ok += 1
    else:
        fail += 1
        print(f"  FALHOU {label}" + (f"\n    {detail}" if detail else ""))


def refused(M):
    """A mensagem com que check_manifest recusa M, ou None se aceita."""
    try:
        gf.check_manifest(M)
        return None
    except SystemExit as e:
        return str(e)


M = gf.load_manifest()

# 1. Todo interruptor tem um bit, e os bits nao se repetem: nem entre as chaves, nem entre
#    elas e as classes de rede que tem bit.
bits = {t: M["toggles"][t].get("bit") for t in gf.TOGGLE_ORDER}
class_bits = {c: v["bit"] for c, v in M["net_class"].items() if "bit" in v}
todos = list(bits.values()) + list(class_bits.values())
check("toda chave tem bit", all(isinstance(b, int) for b in bits.values()), repr(bits))
check("nenhum bit se repete", len(set(todos)) == len(todos), repr((bits, class_bits)))
check("todo bit cabe em 32", all(0 <= b < 32 for b in todos if isinstance(b, int)))
check("a classe lan tem o bit 18, o que era da chave lan (um app instalado le o numero)",
      class_bits.get("lan") == 18, repr(class_bits))

m2 = copy.deepcopy(M)
m2["toggles"]["mdns"]["bit"] = M["toggles"]["cli_full"]["bit"]
v = refused(m2)
check("bit repetido e recusado pelo gerador",
      v is not None and "bit" in v and "mdns" in v, repr(v))
m2 = copy.deepcopy(M)
del m2["toggles"]["syslog"]["bit"]
v = refused(m2)
check("chave sem bit e recusada pelo gerador",
      v is not None and "syslog" in v and "bit" in v, repr(v))
m2 = copy.deepcopy(M)
m2["toggles"]["syslog"]["bit"] = 32
v = refused(m2)
check("bit fora de 0..31 e recusado", v is not None and "syslog" in v, repr(v))
# O bit de uma classe de rede e do mesmo contrato: o mesmo numero de uma chave, de outra classe,
# fora de 0..31 ou que nao e inteiro, e a mensagem nomeia os dois donos.
m2 = copy.deepcopy(M)
m2["net_class"]["lan"]["bit"] = M["toggles"]["cli_full"]["bit"]
v = refused(m2)
check("bit da classe igual ao de uma chave e recusado, nomeando os dois donos",
      v is not None and "bit" in v and "net_class.lan" in v and "toggles.cli_full" in v, repr(v))
m2 = copy.deepcopy(M)
m2["net_class"]["wifi"].update(macro="SIMUT_NET_WIFI", bit=M["net_class"]["lan"]["bit"])
v = refused(m2)
check("bit repetido entre duas classes e recusado, nomeando as duas",
      v is not None and "net_class.wifi" in v and "net_class.lan" in v, repr(v))
for ruim in (32, -1, True, "18"):
    m2 = copy.deepcopy(M)
    m2["net_class"]["lan"]["bit"] = ruim
    v = refused(m2)
    check(f"bit de classe {ruim!r} e recusado", v is not None and "lan" in v and "bit" in v, repr(v))

# 1b. O driver de rede (2026-10-13) traz a placa do ambiente (`board`) e tira um flag do pico_base
#     (`unflags`), o ENC28J60: o gerador os aplica, recusa o chip que nao tem placa para ele e o
#     manifesto que nao diz por que. Nenhuma outra imagem muda de placa nem perde flag.
def composed(name, **over):
    return gf.compose(dict(gf.resolve_profile(name, M["profiles"]), **over), M)


def exits(fn):
    try:
        fn()
        return None
    except SystemExit as e:
        return str(e)


def recusa_com(descricao, mutate, *trechos):
    """Aplica `mutate` a uma copia do manifesto e confere que check_manifest o recusa, citando
    todos os `trechos` (a mensagem tem de dizer o que esta errado e onde)."""
    m2 = copy.deepcopy(M)
    mutate(m2)
    v = refused(m2)
    check(descricao, v is not None and all(t in v for t in trechos), repr(v))


wire = composed("pico_wire")
check("pico_wire usa a placa do Pico comum e tira o CYW43 do pico_base",
      wire["board"] == "rpipico" and wire["unflags"] == ["-DPICO_CYW43_SUPPORTED=1"], repr((wire["board"], wire["unflags"])))
wiretft = composed("pico_wiretft")
check("pico_wiretft (a TFT no lugar do LCD) usa a mesma placa e tira o mesmo CYW43",
      wiretft["board"] == "rpipico" and wiretft["unflags"] == ["-DPICO_CYW43_SUPPORTED=1"], repr((wiretft["board"], wiretft["unflags"])))
wiretft_angulo = composed("pico_wiretft_angulo")
check("pico_wiretft_angulo (o pico_wiretft com o driver de UI trocado) usa a mesma placa e tira o mesmo CYW43",
      wiretft_angulo["board"] == "rpipico" and wiretft_angulo["unflags"] == ["-DPICO_CYW43_SUPPORTED=1"],
      repr((wiretft_angulo["board"], wiretft_angulo["unflags"])))
for name in M["profiles"]:
    prof = gf.resolve_profile(name, M["profiles"])
    c = composed(name)
    chip = prof.get("chip", "rp2040")
    if name not in ("pico_wire", "pico_wiretft", "pico_wiretft_angulo"):
        check(f"{name}: a placa e o flag do pico_base ficam como eram",
              c["board"] == M["chip"][chip].get("board", "") and c["unflags"] == [], repr((c["board"], c["unflags"])))
    # A classe da rede (o macro de [net_class.*]) nunca vai por -D: o src/net/NetCaps.h a deriva da identidade
    # do driver e recusa quem a passa (tools/test_net_caps.py). O que vai por -D da rede e a identidade, nos
    # `flags` do driver. As unidades de uma classe que nao e a do perfil e as de um driver que nao e o do
    # perfil saem do build, e as da classe do perfil e as do driver dele ficam; a biblioteca e dele.
    meu = M["net"][prof["net"]]
    minha_classe = M["net_class"][meu["class"]]
    da_classe = {f"-D{v['macro']}" for v in M["net_class"].values() if "macro" in v}
    macros_de_rede = [f for f in c["flags"] if f.startswith("-DSIMUT_NET_")]
    check(f"{name}: a classe da rede nao vai por -D; so a identidade do driver (os `flags` dele)",
          macros_de_rede == [f for f in meu.get("flags", []) if f.startswith("-DSIMUT_NET_")]
          and not any(f.split("=")[0] in da_classe for f in c["flags"]), repr(c["flags"]))
    dos_outros = [u for n, d in M["net"].items() if n != prof["net"] for u in d.get("units", [])]
    das_outras_classes = [u for n, d in M["net_class"].items() if n != meu["class"] for u in d.get("units", [])]
    check(f"{name}: sai do build toda unidade das outras classes de rede e dos outros drivers, e nenhuma da dele",
          all(u in c["excludes"] for u in dos_outros + das_outras_classes)
          and not any(u in c["excludes"] for u in meu.get("units", []) + minha_classe.get("units", [])),
          repr((c["excludes"], dos_outros, das_outras_classes)))
    # A classe cabeada (`lan`) tem as duas unidades da build cabeada (o ENC28J60 e um cabecalho, nao uma unidade),
    # e so as imagens que nao sao cabeadas as perdem.
    lan_units = M["net_class"]["lan"].get("units", [])
    check(f"{name}: as unidades da classe cabeada ficam so na imagem cabeada",
          {"net/NetworkManager_Wired.cpp", "net/WiredLink.cpp"} <= set(lan_units)
          and (meu["class"] == "lan") == (not any(u in c["excludes"] for u in lan_units)),
          repr((meu["class"], lan_units, c["excludes"])))
    # O CYW43 tem a unidade do radio no NetworkManager (2026-10-13), e so as imagens que nao sao dele a perdem:
    # nas cabeadas ela seria codigo morto, e o link morreria em __wrap_cyw43_cb_* numa placa sem o radio.
    cyw_units = M["net"]["cyw43"].get("units", [])
    check(f"{name}: a unidade do radio fica so na imagem com o radio",
          "net/NetworkManager_Cyw43.cpp" in cyw_units
          and (prof["net"] == "cyw43") == (not any(u in c["excludes"] for u in cyw_units)),
          repr((prof["net"], cyw_units, c["excludes"])))
    check(f"{name}: a biblioteca do driver so vem com ele",
          c["lib_deps"] == meu.get("lib_deps", []), repr(c["lib_deps"]))
P2 = gf.resolve_profile("pico2_w_release", M["profiles"])
why = exits(lambda: composed("pico2_w_release", net="enc28j60"))
check("o ENC28J60 no RP2350 e recusado pelo compose, nomeando a rede e o chip",
      why is not None and "enc28j60" in why and "rp2350" in why, repr(why))
msg = gf.net_refusal(dict(P2, net="enc28j60"), M)
check("net_refusal diz o mesmo sem sair: o driver, o chip e os chips que o driver conhece",
      msg is not None and "enc28j60" in msg and "rp2350" in msg and "rp2040" in msg, repr(msg))
check("nada recusa o RP2350 com o driver de rede dele (o CYW43, sem `board`: fica a placa do chip)",
      gf.net_refusal(P2, M) is None and composed("pico2_w_release")["board"] == M["chip"]["rp2350"]["board"])
check("o ENC28J60 no RP2040 nao e recusado", gf.net_refusal(dict(P2, chip="rp2040", net="enc28j60"), M) is None)

recusa_com("o motivo da recusa (en e pt) e pedido a quem tem `board` que nao cobre todo chip",
           lambda m: m["ui_nets"]["enc28j60"].pop("unsupported"), "enc28j60", "unsupported")
recusa_com("e o motivo so em ingles tambem",
           lambda m: m["ui_nets"]["enc28j60"].update(unsupported={"en": "so em ingles"}), "unsupported")
recusa_com("driver sem classe e recusado", lambda m: m["net"]["cyw43"].pop("class"), "cyw43", "class")
recusa_com("board com um chip que nao existe e recusado",
           lambda m: m["net"]["enc28j60"].update(board={"rp9999": "rpipico"}), "board", "enc28j60")
recusa_com("board vazio e recusado", lambda m: m["net"]["enc28j60"].update(board={}), "board")
recusa_com("board com placa que nao e texto e recusado",
           lambda m: m["net"]["enc28j60"].update(board={"rp2040": 3}), "board")
recusa_com("unflags que nao e flag e recusado",
           lambda m: m["net"]["enc28j60"].update(unflags=["PICO_CYW43_SUPPORTED"]), "unflags")
recusa_com("flags que nao e -D e recusado",
           lambda m: m["net"]["enc28j60"].update(flags=["MAX_SENSORS=11"]), "flags")
recusa_com("units que nao e lista e recusado",
           lambda m: m["net"]["enc28j60"].update(units="net/NetworkManager_Wired.cpp"), "units")
recusa_com("unit com espaco (quebraria o build_src_filter) e recusada",
           lambda m: m["net"]["enc28j60"].update(units=["net/Wired Link.cpp"]), "units")
# `units` de um driver continua valendo, para um chip que traz um .cpp proprio: sai das imagens dos outros
# drivers e fica na dele, como a unidade de uma classe.
m2 = copy.deepcopy(M)
m2["net"]["enc28j60"]["units"] = ["net/Enc28j60Extra.cpp"]
gf.check_manifest(m2)
radio_c = gf.compose(gf.resolve_profile("pico_w_release", m2["profiles"]), m2)
cabo_c = gf.compose(gf.resolve_profile("pico_wire", m2["profiles"]), m2)
check("a unidade de um driver (um chip com .cpp proprio) sai das imagens dos outros e fica na dele",
      "net/Enc28j60Extra.cpp" in radio_c["excludes"] and "net/Enc28j60Extra.cpp" not in cabo_c["excludes"],
      repr((radio_c["excludes"], cabo_c["excludes"])))
recusa_com("units de classe que nao e lista e recusado",
           lambda m: m["net_class"]["lan"].update(units="net/NetworkManager_Wired.cpp"), "units", "lan")
recusa_com("unit de classe com espaco (quebraria o build_src_filter) e recusada",
           lambda m: m["net_class"]["lan"].update(units=["net/Wired Link.cpp"]), "units", "lan")
recusa_com("units de classe com um item que nao e texto e recusado",
           lambda m: m["net_class"]["lan"].update(units=["net/NetworkManager_Wired.cpp", 3]), "units", "lan")
recusa_com("lib_deps que anda (um ramo) e recusado",
           lambda m: m["net"]["enc28j60"].update(lib_deps=["https://github.com/angeloINTJ/pico-enc28j60.git#main"]),
           "lib_deps", "enc28j60")
recusa_com("lib_deps que nao e lista e recusado",
           lambda m: m["net"]["enc28j60"].update(lib_deps="https://x#" + "a" * 40), "lib_deps")
recusa_com("chave de driver que ninguem le (typo) e recusada, em vez de ignorada",
           lambda m: m["net"]["enc28j60"].update(lib_dep=["x"]), "lib_dep", "enc28j60")
recusa_com("driver de uma classe que nao existe e recusado",
           lambda m: m["net"]["enc28j60"].update(**{"class": "wan"}), "class", "wan")
recusa_com("macro de classe que nao e SIMUT_ e recusado",
           lambda m: m["net_class"]["lan"].update(macro="NET_LAN"), "macro", "lan")
recusa_com("web_omit de classe que nao e um nome de @IF e recusado",
           lambda m: m["net_class"]["lan"].update(web_omit="wifi, tft"), "web_omit", "lan")
recusa_com("chave de classe que ninguem le e recusada",
           lambda m: m["net_class"]["lan"].update(on_web_omit="wifi"), "on_web_omit", "lan")
recusa_com("classe com bit e sem macro e recusada (o bit sai do macro)",
           lambda m: m["net_class"]["wifi"].update(bit=30), "wifi", "macro")
recusa_com("driver sem texto da pagina e recusado",
           lambda m: m["ui_nets"].pop("enc28j60"), "enc28j60", "ui_nets")
recusa_com("texto da pagina de um driver que nao existe e recusado",
           lambda m: m["ui_nets"].update(w5500=dict(m["ui_nets"]["cyw43"])), "w5500", "ui_nets")
recusa_com("driver sem help em pt e recusado",
           lambda m: m["ui_nets"]["cyw43"]["help"].pop("pt"), "cyw43", "help")
recusa_com("driver sem order e recusado",
           lambda m: m["ui_nets"]["cyw43"].pop("order"), "cyw43", "order")
recusa_com("falta o [ui_net] (o grupo e o rotulo da escolha)", lambda m: m.pop("ui_net"), "ui_net")
recusa_com("[ui_net] com um grupo que nao existe e recusado",
           lambda m: m["ui_net"].update(group="nada"), "ui_net", "nada")
recusa_com("[ui_net] sem rotulo em pt e recusado", lambda m: m["ui_net"]["label"].pop("pt"), "ui_net", "label")

# O que era do interruptor e foi para o driver: uma chave velha ignorada em silencio comporia outra imagem.
for chave, valor in (("on_board", {"rp2040": "rpipico"}), ("on_unflags", ["-DX=1"]),
                     ("on_lib_deps", ["https://x#" + "a" * 40]), ("on_web_omit", "wifi")):
    recusa_com(f"{chave} num interruptor e recusada: mudou para a rede",
               lambda m, chave=chave, valor=valor: m["toggles"]["air"].update({chave: valor}),
               chave, "air", "2026-10-13")

# O perfil escolhe a rede: sem ela, com uma que nao existe, ou com a chave velha, e recusado.
recusa_com("perfil sem `net` e recusado, nomeando o perfil",
           lambda m: m["profiles"]["pico_w_release"].pop("net"), "pico_w_release", "rede")
recusa_com("perfil com um `net` que nao e um [net.*] e recusado, nomeando o perfil e o valor",
           lambda m: m["profiles"]["pico_w_alpha"].update(net="w5500"), "pico_w_alpha", "w5500")
recusa_com("perfil que ainda diz `lan = true` e recusado em vez de compor uma imagem Wi-Fi",
           lambda m: m["profiles"]["pico_wire"].update(lan=True), "pico_wire", "lan")
recusa_com("regra com uma rede que nao existe e recusada",
           lambda m: m["rules"][-1]["require"].update(net="w5500"), "rules", "w5500")
recusa_com("aviso com uma rede que nao existe e recusado",
           lambda m: m["hazards"][0]["when"].update(net="w5500"), "hazards", "w5500")
recusa_com("regra com `net` que nao e texto e recusada",
           lambda m: m["rules"][-1]["require"].update(net=True), "rules", "rede desconhecida")

# O ORDER de tools/pio_budget.py nomeia redes do manifesto: a chave `net=<id>` de um driver que o
# manifesto nao tem seria um programa PIO na conta sem imagem que o leve.
import pio_budget as pb  # noqa: E402

ordem_certa = pb.ORDER
try:
    pb.ORDER = ordem_certa + (("w5500", "net=w5500"),)
    v = refused(M)
    check("chave net=<id> do ORDER de um driver que o manifesto nao tem e recusada",
          v is not None and "net=w5500" in v and "ORDER" in v, repr(v))
    pb.ORDER = ordem_certa + (("w5500", "display=tft"),)
    v = refused(M)
    check("escolha do ORDER que nao e `net` e recusada", v is not None and "display=tft" in v, repr(v))
finally:
    pb.ORDER = ordem_certa
check("com o ORDER de volta o manifesto e aceito", refused(M) is None, repr(refused(M)))

# As regras que pedem o radio (net = "cyw43"): o Bluetooth e o Air com o cabo as quebram, e com o radio nao.
for chave, regra in (("bluetooth", "bluetooth-needs-cyw43"), ("air", "air-needs-cyw43")):
    cabo = dict(gf.resolve_profile("pico_wire", M["profiles"]), **{chave: True})
    radio = dict(cabo, net="cyw43")
    check(f"{chave} com o ENC28J60 quebra {regra}",
          regra in [r["id"] for r in gf.rule_violations(gf.config_of(cabo), M)])
    check(f"{chave} com o CYW43 nao quebra {regra}",
          regra not in [r["id"] for r in gf.rule_violations(gf.config_of(radio), M)])
check("o aviso enc28j60-unsoaked acende com o cabo e nao com o radio",
      [h["id"] for h in gf.hazard_hits(gf.config_of(gf.resolve_profile("pico_wire", M["profiles"])), M)] == ["enc28j60-unsoaked"]
      and gf.hazard_hits(gf.config_of(gf.resolve_profile("pico_w_release", M["profiles"])), M) == [])

# 2. O cabecalho gerado e o do manifesto (o --check do gerador tambem cobra).
header = open(os.path.join(ROOT, "src", "FeatureBits.h"), encoding="utf-8").read()
check("o FeatureBits.h commitado e o que o gerador escreve",
      header == gf.build_feature_header(M))


# 3. O pre-processador do host, com os flags de cada perfil e inversao.
def mask_of(cfg):
    """As chaves ligadas da config e, se a rede dela e de uma classe com bit (a lan, o 18), o da classe."""
    mask = sum(1 << bits[t] for t in gf.TOGGLE_ORDER if cfg[t])
    bit = M["net_class"][M["net"][cfg["net"]]["class"]].get("bit")
    return mask if bit is None else mask | (1 << bit)


def host_bits(flags, tmp):
    probe = os.path.join(tmp, "probe.c")
    with open(probe, "w") as fh:
        fh.write('#include "FeatureBits.h"\nFEAT_PROBE SIMUT_FEATURE_BITS\n')
    r = subprocess.run(["cpp", "-P", "-I" + os.path.join(ROOT, "src")] + flags + [probe],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr.strip().splitlines()[-1:] or ["cpp falhou"]
    line = [ln for ln in r.stdout.splitlines() if ln.startswith("FEAT_PROBE")][0]
    expr = line[len("FEAT_PROBE"):].strip()
    if not re.fullmatch(r"[0-9a-fA-FxXuUlL()|<\s]+", expr):
        return None, [f"expressao inesperada: {expr!r}"]
    return eval(re.sub(r"(?<=[0-9a-fA-F])[uUlL]+", "", expr)), None


if shutil.which("cpp") is None:
    check("cpp do host disponivel", False, "instale o gcc: o teste pre-processa o cabecalho")
else:
    tmp = tempfile.mkdtemp(prefix="feat_bits_")
    cases = 0
    for name in M["profiles"]:
        base = gf.resolve_profile(name, M["profiles"])
        variants = [(name, base)]
        for t in gf.TOGGLE_ORDER:
            flip = dict(base)
            flip[t] = not bool(base.get(t, False))
            variants.append((f"{name} {t}={'on' if flip[t] else 'off'}", flip))
        # A rede tambem: o bit da classe lan acende com o ENC28J60 e apaga com o CYW43.
        for n in M["net"]:
            if n != base["net"]:
                variants.append((f"{name} net={n}", dict(base, net=n)))
        for label, prof in variants:
            cfg = gf.config_of(prof)
            if gf.rule_violations(cfg, M) or gf.net_refusal(prof, M):
                continue
            flags = gf.compose(prof, M)["flags"]
            got, err = host_bits(flags, tmp)
            cases += 1
            check(f"{label}: SIMUT_FEATURE_BITS = chaves ligadas",
                  err is None and got == mask_of(cfg),
                  f"esperado {mask_of(cfg):#07x}, obtido {got if got is None else hex(got)} {err or ''}")
    shutil.rmtree(tmp, ignore_errors=True)
    check("cobriu os seis perfis e as inversoes validas", cases > 6 * 10, f"{cases} casos")

# 4. As tags de boot (2026-11-06): o SIMUT_BOOT_TAGS, de src/display/BootReplay.h, so vale 1 na pico_w_test.
#    A condicao e o console completo, o painel TFT e a falta do servidor HTTPS. A pico_w_test_https tem os dois
#    primeiros e nao tem lugar para as tags: depois da #324 o .bin dela estava 1.828 B abaixo do slot de 1.044.480 B,
#    as tags pedem 2.248 B, e o ld disse "region `FLASH' overflowed by 420 bytes" em 2026-11-06. O teste pre-processa
#    o cabecalho de verdade, com os flags que o build compoe para cada perfil, e confere o conjunto de imagens que
#    levam as tags contra a regra escrita aqui, que nao sai do cabecalho. O console tirado do cabecalho so aparece numa
#    variante (todo perfil com painel e sem servidor tem console), por isso a pico_w_test sem console e um dos casos.
TAGS_IMAGES = {"pico_w_test"}      # o que o AGENTS.md, o CHANGELOG, o manual e o CLI-Manual dizem
TAGS_DEFINE_RE = re.compile(r"^#define SIMUT_BOOT_TAGS \(.*\)$", re.M)
TAGS_DEFINE = "#define SIMUT_BOOT_TAGS (SIMUT_CLI_FULL && SIMUT_DISPLAY_TFT && !SIMUT_FEAT_WEB_HTTPS)"
tags_header = os.path.join(ROOT, "src", "display", "BootReplay.h")
tags_other = "--header" in sys.argv
if tags_other:
    tags_header = os.path.abspath(sys.argv[sys.argv.index("--header") + 1])


def tags_rule(cfg, panel):
    """A regra, escrita aqui e nao lida do cabecalho: o console completo, o painel TFT e nenhum servidor HTTPS."""
    return bool(cfg["cli_full"]) and panel and not cfg["web_https"]


def tags_cases():
    """[(rotulo, flags, esperado)]: as onze imagens e a variante que separa o console das outras duas condicoes."""
    cases = []
    for name in M["profiles"]:
        prof = gf.resolve_profile(name, M["profiles"])
        cases.append((name, gf.compose(prof, M)["flags"], tags_rule(gf.config_of(prof), prof.get("display") == "tft")))
    prof = dict(gf.resolve_profile("pico_w_test", M["profiles"]), cli_full=False)
    check("a variante pico_w_test sem o console e uma combinacao valida (nenhuma regra a recusa)",
          not gf.rule_violations(gf.config_of(prof), M) and not gf.net_refusal(prof, M))
    cases.append(("pico_w_test sem o console", gf.compose(prof, M)["flags"], tags_rule(gf.config_of(prof), True)))
    return cases


def tags_on(flags, tmp, header_dir=None):
    """True ou False: o SIMUT_BOOT_TAGS como o cpp do host o avalia com `flags`; None se o cpp falha. `header_dir`, se dado,
    tem um display/BootReplay.h que vale no lugar do da arvore."""
    probe = os.path.join(tmp, "tags_probe.c")
    with open(probe, "w") as fh:
        fh.write('#include "display/BootReplay.h"\n#if SIMUT_BOOT_TAGS\nTAGS_ON\n#else\nTAGS_OFF\n#endif\n')
    # O cabecalho puxa o SystemDefs_Cli.h, que inclui o <Arduino.h>: o do host, o que as suites nativas veem.
    inc = ["-I" + os.path.join(ROOT, "src"), "-I" + os.path.join(ROOT, "test", "native_stubs")]
    if header_dir:
        inc = ["-I" + header_dir] + inc
    r = subprocess.run(["cpp", "-x", "c++", "-std=gnu++17", "-P"] + inc + flags + [probe], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    lines = r.stdout.splitlines()
    return True if "TAGS_ON" in lines and "TAGS_OFF" not in lines else False if "TAGS_OFF" in lines and "TAGS_ON" not in lines else None


def tags_wrong(cases, tmp, header_dir=None):
    """Os rotulos em que o cabecalho diz o contrario da regra (ou nao diz nada)."""
    return {label for label, flags, want in cases if tags_on(flags, tmp, header_dir) is not want}


if shutil.which("cpp") is None:
    check("cpp do host disponivel para as tags de boot", False, "instale o gcc: o teste pre-processa o cabecalho")
else:
    tmp = tempfile.mkdtemp(prefix="boot_tags_")
    cases = tags_cases()
    check("a regra, aplicada ao manifesto, da as tags so a " + ", ".join(sorted(TAGS_IMAGES)),
          {label for label, _, want in cases[:len(M["profiles"])] if want} == TAGS_IMAGES,
          repr([(label, want) for label, _, want in cases]))
    https = [c for c in cases if c[0] == "pico_w_test_https"][0]
    check("a pico_w_test_https tem console e painel, e so o servidor HTTPS a deixa sem as tags",
          gf.resolve_profile("pico_w_test_https", M["profiles"]).get("cli_full")
          and gf.resolve_profile("pico_w_test_https", M["profiles"]).get("display") == "tft"
          and gf.resolve_profile("pico_w_test_https", M["profiles"]).get("web_https") and https[2] is False)
    if tags_other:
        d = os.path.join(tmp, "outro", "display")
        os.makedirs(d)
        shutil.copy(tags_header, os.path.join(d, "BootReplay.h"))
        wrong = tags_wrong(cases, tmp, os.path.join(tmp, "outro"))
        check(f"{tags_header}: as tags estao so onde a regra manda", not wrong, f"contra a regra: {sorted(wrong)}")
    else:
        wrong = tags_wrong(cases, tmp)
        check("src/display/BootReplay.h: as tags estao so onde a regra manda (a pico_w_test)", not wrong,
              f"contra a regra: {sorted(wrong)}")
        src = open(tags_header, encoding="utf-8").read()
        check("a definicao do SIMUT_BOOT_TAGS e a que o teste conhece (um controle que mexe numa linha que nao esta la nao prova nada)",
              TAGS_DEFINE_RE.findall(src) == [TAGS_DEFINE], repr(TAGS_DEFINE_RE.findall(src)))
        # Os controles: cada condicao tirada de uma copia do cabecalho muda o conjunto exatamente como o manifesto manda.
        mutants = (("sem o servidor HTTPS", "#define SIMUT_BOOT_TAGS (SIMUT_CLI_FULL && SIMUT_DISPLAY_TFT)", {"pico_w_test_https"}),
                   ("sem o painel", "#define SIMUT_BOOT_TAGS (SIMUT_CLI_FULL && !SIMUT_FEAT_WEB_HTTPS)", {"pico_w_air"}),
                   ("sem o console", "#define SIMUT_BOOT_TAGS (SIMUT_DISPLAY_TFT && !SIMUT_FEAT_WEB_HTTPS)", {"pico_w_test sem o console"}),
                   ("sempre 0", "#define SIMUT_BOOT_TAGS 0", {"pico_w_test"}))
        for k, (nome, nova, esperado) in enumerate(mutants):
            d = os.path.join(tmp, f"mut{k}", "display")
            os.makedirs(d)
            with open(os.path.join(d, "BootReplay.h"), "w", encoding="utf-8") as fh:
                fh.write(TAGS_DEFINE_RE.sub(lambda m: nova, src))
            wrong = tags_wrong(cases, tmp, os.path.join(tmp, f"mut{k}"))
            check(f"controle: a definicao {nome} e acusada, e so em {sorted(esperado)}", wrong == esperado,
                  f"acusou {sorted(wrong)}")
    shutil.rmtree(tmp, ignore_errors=True)

print(f"{ok} ok, {fail} falha(s)")
sys.exit(1 if fail else 0)
