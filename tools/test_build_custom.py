#!/usr/bin/env python3
"""test_build_custom.py — a porta de entrada do tools/build_custom.py.

O perfil chega por um workflow_dispatch que a página do configurador preenche,
e o que passasse daqui viraria configuração do PlatformIO: uma quebra de linha
num nome de produto seria um [env] novo no profiles.ini. Estes casos tentam o
que um erro ou um atacante tentaria, e o válido tem de sair igual ao manifesto.
Nada é compilado: parse_spec, as regras (pela linha de comando, com o código
de saída que o workflow vê) e canonical().

    python3 tools/test_build_custom.py        # exit 1 em qualquer divergência

@project SIMUT
@license MIT License
"""
import copy
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402
import build_custom as bc  # noqa: E402

M = gf.load_manifest()
AIR = '{"v":1,"base":"pico_w_air","set":%s}'

# Cada um tem de ser recusado por parse_spec, com SpecError, antes de qualquer
# outra coisa acontecer.
HOSTILE = [
    ("vazio", ""),
    ("só espaço", "   \n"),
    ("não é JSON", "{v:1}"),
    ("JSON cortado", '{"v":1,"base":"pico_w_air"'),
    ("array", '[{"v":1,"base":"pico_w_air","set":{}}]'),
    ("string", '"pico_w_air"'),
    ("número", "1"),
    ("null", "null"),
    ("sem base", '{"v":1,"set":{}}'),
    ("sem versão", '{"base":"pico_w_air","set":{}}'),
    ("versão 2", '{"v":2,"base":"pico_w_air","set":{}}'),
    ("versão true", '{"v":true,"base":"pico_w_air","set":{}}'),
    ("versão 1.0", '{"v":1.0,"base":"pico_w_air","set":{}}'),
    ("versão em texto", '{"v":"1","base":"pico_w_air","set":{}}'),
    ("versão NaN", '{"v":NaN,"base":"pico_w_air","set":{}}'),
    ("campo a mais", '{"v":1,"base":"pico_w_air","set":{},"env":"x"}'),
    ("chave repetida", '{"v":1,"base":"pico_w_air","base":"pico_w_release","set":{}}'),
    ("chave repetida no set", AIR % '{"mdns":true,"mdns":false}'),
    ("produto desconhecido", '{"v":1,"base":"pico_w_x","set":{}}'),
    ("produto vazio", '{"v":1,"base":"","set":{}}'),
    ("produto com quebra de linha", '{"v":1,"base":"pico_w_air\\n[env:x]","set":{}}'),
    ("produto em lista", '{"v":1,"base":["pico_w_air"],"set":{}}'),
    ("produto que é o base comum", '{"v":1,"base":"pico_base","set":{}}'),
    ("set em lista", AIR % '[]'),
    ("set em texto", AIR % '"mdns"'),
    ("chave desconhecida", AIR % '{"wifi":true}'),
    ("chave com quebra de linha", AIR % '{"mdns\\nbuild_flags = -DX":true}'),
    ("chave __proto__", AIR % '{"__proto__":true}'),
    ("display no set", AIR % '{"display":"tft"}'),
    ("valor em texto", AIR % '{"mdns":"true"}'),
    ("valor numérico", AIR % '{"mdns":1}'),
    ("valor null", AIR % '{"mdns":null}'),
    ("valor objeto", AIR % '{"mdns":{"on":true}}'),
    # A rede e uma escolha, nao um interruptor (2026-10-13): `net` leva o id de um [net.*]
    # do manifesto, como texto, e nada mais. O que passasse daqui chegaria a gf.blockers( ),
    # que indexa o manifesto pelo id, e a um [env] do PlatformIO. Os valores em lista e em
    # objeto pegam o TypeError de um `in` sobre um dict antes de o tipo ser conferido.
    ("lan, a chave que a rede substituiu", AIR % '{"lan":true}'),
    ("lan com o id da rede", AIR % '{"lan":"enc28j60"}'),
    ("net em outra caixa", AIR % '{"NET":"cyw43"}'),
    ("net com espaço", AIR % '{"net ":"cyw43"}'),
    ("net fora do set", '{"v":1,"base":"pico_w_air","net":"enc28j60","set":{}}'),
    ("rede desconhecida", AIR % '{"net":"w5500"}'),
    ("rede em outra caixa", AIR % '{"net":"CYW43"}'),
    ("rede com espaço", AIR % '{"net":"cyw43 "}'),
    ("rede vazia", AIR % '{"net":""}'),
    ("rede com quebra de linha", AIR % '{"net":"cyw43\\n[env:x]\\nbuild_flags = -DX"}'),
    ("rede com comando embutido", AIR % '{"net":"cyw43\\n::warning::injetado"}'),
    ("classe no lugar do driver", AIR % '{"net":"lan"}'),
    ("classe wifi no lugar do driver", AIR % '{"net":"wifi"}'),
    ("rede true", AIR % '{"net":true}'),
    ("rede false", AIR % '{"net":false}'),
    ("rede numérica", AIR % '{"net":1}'),
    ("rede null", AIR % '{"net":null}'),
    ("rede em lista", AIR % '{"net":["cyw43"]}'),
    ("rede em objeto", AIR % '{"net":{"cyw43":true}}'),
    ("rede repetida no set", AIR % '{"net":"cyw43","net":"enc28j60"}'),
    # A UI do painel touch e a outra escolha de driver (2026-10-14): `ui` leva o id de um [ui.*] do
    # manifesto, como texto, e nada mais. Os mesmos ataques da rede: o que passasse chegaria a
    # gf.compose( ), que indexa o manifesto pelo id, e a um [env] do PlatformIO.
    ("ui em outra caixa", AIR % '{"UI":"original"}'),
    ("ui com espaço", AIR % '{"ui ":"original"}'),
    ("ui fora do set", '{"v":1,"base":"pico_w_air","ui":"angulo","set":{}}'),
    ("UI desconhecida", AIR % '{"ui":"neon"}'),
    ("UI curta", AIR % '{"ui":"x"}'),
    ("UI em outra caixa", AIR % '{"ui":"Angulo"}'),
    ("UI com acento", AIR % '{"ui":"Ângulo"}'),
    ("UI com espaço", AIR % '{"ui":"angulo "}'),
    ("UI vazia", AIR % '{"ui":""}'),
    ("UI com quebra de linha", AIR % '{"ui":"angulo\\n[env:x]\\nbuild_flags = -DX"}'),
    ("UI com comando embutido", AIR % '{"ui":"angulo\\n::warning::injetado"}'),
    ("UI true", AIR % '{"ui":true}'),
    ("UI false", AIR % '{"ui":false}'),
    ("UI numérica", AIR % '{"ui":1}'),
    ("UI null", AIR % '{"ui":null}'),
    ("UI em lista", AIR % '{"ui":["angulo"]}'),
    ("UI em objeto", AIR % '{"ui":{"angulo":true}}'),
    ("UI repetida no set", AIR % '{"ui":"original","ui":"angulo"}'),
    ("grande demais", '{"v":1,"base":"pico_w_air","set":{}}' + " " * bc.MAX_SPEC),
]

# (perfil, código de saída do --validate-only, trecho que a saída tem de ter)
CLI = [
    ('{"v":1,"base":"pico_w_air","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_air"}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_alpha","set":{"bluetooth":false,"mdns":true}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_test","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_release","set":{"bluetooth":true}}', 0, "aviso bt-flash-core1"),
    ('{"v":1,"base":"pico2_w_release","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico2_w_release","set":{"bluetooth":true}}', 0, "aviso bt-flash-core1"),
    ('{"v":1,"base":"pico_w_release","set":{"air":true}}', 2, "regra air-needs-headless"),
    ('{"v":1,"base":"pico_w_air","set":{"air":false}}', 2, "regra headless-needs-air"),
    ('{"v":1,"base":"pico_w_air","set":{"sound_buzzer":true}}', 2, "regra air-no-buzzer"),
    # A rede por cabo (2026-10-12): vale no Pico W sem Bluetooth e sem Air, e o
    # SIMUT Wire e o produto dela; o RP2350 nao tem placa para ela. Desde
    # 2026-10-13 o orcamento de PIO tambem decide: com o DS18B20 e o DHT22, o
    # ENC28J60 nao cabe nos dois blocos do RP2040 (tools/pio_budget.py). E, no
    # mesmo dia, a rede deixou de ser o interruptor `lan` e virou a escolha de um
    # driver, `net` ("cyw43" ou "enc28j60", os [net.*] do manifesto).
    ('{"v":1,"base":"pico_wire","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wiretft","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_release","set":{"net":"enc28j60"}}', 2, "pio-budget"),
    ('{"v":1,"base":"pico_wire","set":{"sensor_ds18b20":true}}', 2, "pio-budget"),
    ('{"v":1,"base":"pico_wire","set":{"sensor_ds18b20":true,"sensor_dht22":false}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_alpha","set":{"net":"enc28j60"}}', 2, "regra bluetooth-needs-cyw43"),
    ('{"v":1,"base":"pico_w_air","set":{"net":"enc28j60","bluetooth":false}}', 2, "regra air-needs-cyw43"),
    ('{"v":1,"base":"pico_wire","set":{"bluetooth":true}}', 2, "regra bluetooth-needs-cyw43"),
    ('{"v":1,"base":"pico2_w_release","set":{"net":"enc28j60"}}', 2,
     "a rede enc28j60 nao tem placa para o chip rp2350"),
    # O cabo no SIMUT cabe sem o DS18B20 (o ENC28J60 fica com o pio0, que era dele), e a
    # build avisa que o cabo ainda nao passou por um soak.
    ('{"v":1,"base":"pico_w_release","set":{"net":"enc28j60","sensor_ds18b20":false}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_release","set":{"net":"enc28j60","sensor_ds18b20":false}}', 0,
     "aviso enc28j60-unsoaked"),
    # Escolher o driver que o produto ja tem nao muda nada, e o Wire volta para o Wi-Fi.
    ('{"v":1,"base":"pico_w_release","set":{"net":"cyw43"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico2_w_release","set":{"net":"cyw43"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wire","set":{"net":"enc28j60"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wire","set":{"net":"cyw43"}}', 0, "perfil válido"),
    # Um valor que nao e o id de um driver do manifesto, ou a chave de antes da escolha.
    ('{"v":1,"base":"pico_w_release","set":{"net":"w5500"}}', 2, "rede desconhecida: 'w5500'"),
    ('{"v":1,"base":"pico_w_release","set":{"net":true}}', 2, "net tem de ser o id de um driver de rede"),
    ('{"v":1,"base":"pico_w_release","set":{"net":1}}', 2, "net tem de ser o id de um driver de rede"),
    ('{"v":1,"base":"pico_w_release","set":{"lan":true}}', 2, "chave desconhecida: 'lan'"),
    ('{"v":1,"base":"pico_w_air","set":{"wifi":true}}', 2, "chave desconhecida"),
    # A UI do painel touch (2026-10-14): o Angulo desenha na TFT (a regra angulo-needs-tft), e o
    # original, o padrao, vale em qualquer mostrador. O pico_wiretft_angulo e o pico_wiretft com
    # `ui = "angulo"`, e o perfil nao precisa de aviso: o texto da opcao diz que e um estudo.
    ('{"v":1,"base":"pico_wiretft","set":{"ui":"angulo"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wiretft_angulo","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wiretft_angulo","set":{"ui":"original"}}', 0, "perfil válido"),
    # O Angulo no Pico W (2026-10-22): o RP2040 com o radio CYW43 nao leva o Angulo (a regra angulo-cyw43-needs-rp2350, com a evidencia no manifesto), e o
    # Pico 2 W leva. O Wire com TFT (RP2040 com o ENC28J60) continua com o Angulo: e a imagem de bancada dele.
    ('{"v":1,"base":"pico_w_release","set":{"ui":"angulo"}}', 2, "regra angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"angulo"}}', 2, "Choose the Pico 2 W"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"angulo"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"original"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico2_w_release","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"original"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"original"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"angulo","net":"enc28j60","sensor_ds18b20":false}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wiretft","set":{"ui":"angulo","net":"cyw43"}}', 2, "regra angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico_wiretft_angulo","set":{"net":"cyw43"}}', 2, "regra angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico_w_alpha","set":{"ui":"angulo"}}', 2, "regra angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico_w_air","set":{"ui":"angulo"}}', 2, "regra angulo-cyw43-needs-rp2350"),
    # O Angulo sem o grafico do painel (2026-10-22): o manifesto deixava passar e a build parava no compilador ('_graphData' was not declared in this scope). A regra
    # angulo-needs-tft-graph recusa antes, nas duas bases que o Angulo tem, e o original sem o grafico segue valido.
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"angulo","tft_graph":false}}', 2, "regra angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"angulo","tft_graph":false}}', 2, "Graph and calendar on the touch display"),
    ('{"v":1,"base":"pico_wiretft","set":{"ui":"angulo","tft_graph":false}}', 2, "regra angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico2_w_release","set":{"tft_graph":false}}', 2, "regra angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico_wiretft_angulo","set":{"tft_graph":false}}', 2, "regra angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"angulo","tft_graph":false}}', 2, "regra angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico_w_alpha","set":{"ui":"angulo"}}', 2, "regra angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"original","tft_graph":false}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_wiretft","set":{"tft_graph":false}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_alpha","set":{"ui":"original"}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_alpha","set":{"ui":"angulo"}}', 2, "regra angulo-needs-tft"),
    ('{"v":1,"base":"pico_w_air","set":{"ui":"angulo"}}', 2, "regra angulo-needs-tft"),
    ('{"v":1,"base":"pico_wire","set":{"ui":"angulo"}}', 2, "regra angulo-needs-tft"),
    ('{"v":1,"base":"pico_wiretft","set":{"ui":"angulo"}}', 0, "aviso enc28j60-unsoaked"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"neon"}}', 2, "UI desconhecida: 'neon'"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"x"}}', 2, "UI desconhecida: 'x'"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":true}}', 2, "ui tem de ser o id de um driver de UI"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":1}}', 2, "ui tem de ser o id de um driver de UI"),
    ("", 2, "perfil vazio"),
]

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


for name, text in HOSTILE:
    try:
        got = bc.parse_spec(text, M)
    except bc.SpecError:
        continue
    except Exception as e:  # uma exceção que não é SpecError vira traceback no CI
        failures.append(f"hostil «{name}»: {type(e).__name__} em vez de SpecError: {e}")
        continue
    failures.append(f"hostil «{name}» foi ACEITO: {got!r}")

# O que é válido sai igual ao manifesto: cada produto, sem mudança, compõe o
# mesmo ambiente que o profiles.ini gerado descreve -- menos a confiança na raiz
# de bancada, que nenhuma build do configurador leva, mesmo partindo de uma imagem
# de bancada: ela vai para o campo (docs/analysis/OTA_ASSINADA.md).
BENCH = "-DSIMUT_OTA_TRUST_BENCH=1"
bench_bases = 0
for prof in M["profiles"]:
    base, changes = bc.parse_spec(json.dumps({"v": 1, "base": prof, "set": {}}), M)
    check(base == prof and changes == {}, f"{prof}: parse_spec mudou o perfil vazio")
    built = gf.compose(bc.resolve(base, changes, M), M)
    shipped = gf.compose(gf.resolve_profile(prof, M["profiles"]), M)
    bench_bases += BENCH in shipped["flags"]
    check(BENCH not in built["flags"], f"{prof}: a build do configurador confia na raiz de bancada")
    shipped["flags"] = [f for f in shipped["flags"] if f != BENCH]
    check(built == shipped, f"{prof}: sem mudanças, o ambiente composto difere do manifesto")
check(bench_bases > 0, "nenhum perfil de bancada: o caso acima não provou nada")

# Uma chave mudada chega ao ambiente, e só ela.
base, changes = bc.parse_spec(AIR % '{"bluetooth":false}', M)
flipped = bc.resolve(base, changes, M)
check(flipped["bluetooth"] is False, "bluetooth=false não chegou ao perfil resolvido")
shipped = gf.resolve_profile("pico_w_air", M["profiles"])
diff = {k for k in gf.TOGGLE_ORDER if bool(flipped.get(k)) != bool(shipped.get(k))}
check(diff == {"bluetooth"}, f"mudou mais que o pedido: {sorted(diff)}")

# A rede é a outra metade disso, e uma escolha, não um interruptor (2026-10-13): `net` chega
# ao perfil resolvido como o id do driver, muda o ambiente composto pelo que o manifesto diz
# dele ([net.<id>]: a placa e o macro do driver), e não mexe em nenhuma chave. Os valores
# saem do manifesto, não de uma tabela copiada aqui.


def parsed(text):
    """O que parse_spec devolve, ou a recusa dele: onde o perfil tem de ser aceito, uma
    recusa é uma falha da lista, e não um traceback que esconde as outras."""
    try:
        return bc.parse_spec(text, M)
    except bc.SpecError as e:
        return e


check("net" not in gf.TOGGLE_ORDER, "net entrou no TOGGLE_ORDER: deixou de ser uma escolha de driver")
for n in M["net"]:   # cada driver do manifesto entra, e só ele
    got = parsed(AIR % json.dumps({"net": n}))
    check(got == ("pico_w_air", {"net": n}), f"parse_spec recusou ou mudou o driver {n!r}: {got!r}")
cable = M["net"]["enc28j60"]
# O macro que a build passa é o do driver (a identidade): a classe sai dele em src/net/NetCaps.h,
# que recusa a classe passada de fora (2026-10-13). Lido dos flags do driver, não copiado aqui.
macro = next(f for f in cable.get("flags", []) if f.startswith("-DSIMUT_NET_"))
class_macro = f"-D{M['net_class'][cable['class']]['macro']}"
chip_board = M["chip"]["rp2040"]["board"]
asked = {"net": "enc28j60", "sensor_ds18b20": False}
got = parsed(json.dumps({"v": 1, "base": "pico_w_release", "set": asked}))
check(got == ("pico_w_release", asked), f"parse_spec mudou a rede e o sensor pedidos: {got!r}")
flipped = bc.resolve("pico_w_release", asked, M)
shipped = gf.resolve_profile("pico_w_release", M["profiles"])
check(shipped["net"] == "cyw43" and flipped["net"] == "enc28j60",
      f"net não chegou ao perfil resolvido: {shipped['net']!r} -> {flipped['net']!r}")
diff = {k for k in gf.TOGGLE_ORDER if bool(flipped.get(k)) != bool(shipped.get(k))}
check(diff == {"sensor_ds18b20"}, f"a rede mexeu em chave que ninguém pediu: {sorted(diff)}")
built, was = gf.compose(flipped, M), gf.compose(shipped, M)
check(was["board"] == chip_board, f"o SIMUT de fábrica não tem a placa do chip: {was['board']!r}")
check(built["board"] == cable["board"]["rp2040"] and built["board"] != was["board"],
      f"a placa não seguiu o driver: {was['board']!r} -> {built['board']!r}")
check(macro in built["flags"] and macro not in was["flags"],
      f"o macro do driver {macro} não foi só para a build com o cabo")
check(not any(f.startswith(class_macro) for f in built["flags"] + was["flags"]),
      f"a classe ({class_macro}) foi passada por -D: ela sai do driver em src/net/NetCaps.h")
# No sentido contrário, o Wire volta ao Wi-Fi e perde tudo o que era do cabo; e escolher o
# driver que o produto já tem compõe o mesmo ambiente que não escolher.
wire = bc.resolve("pico_wire", {"net": "cyw43"}, M)
back = gf.compose(wire, M)
check(wire["net"] == "cyw43" and back["board"] == chip_board and macro not in back["flags"],
      f"o Wire com net=cyw43 não voltou ao Wi-Fi: {back['board']!r}")
for prof in M["profiles"]:
    own = gf.resolve_profile(prof, M["profiles"])["net"]
    check(gf.compose(bc.resolve(prof, {"net": own}, M), M) == gf.compose(bc.resolve(prof, {}, M), M),
          f"{prof}: escolher o próprio driver ({own}) mudou o ambiente")

# A UI do painel touch é a outra escolha de driver (2026-10-14), e anda como a rede: `ui` chega ao
# perfil resolvido como o id do driver, muda o ambiente composto só pelo que o manifesto diz dele
# ([ui.<id>]: os flags, a identidade), e não mexe em nenhuma chave. Os valores saem do manifesto.
check("ui" not in gf.TOGGLE_ORDER, "ui entrou no TOGGLE_ORDER: deixou de ser uma escolha de driver")
for n in M["ui"]:   # cada driver do manifesto entra, e só ele
    got = parsed(AIR % json.dumps({"ui": n}))
    check(got == ("pico_w_air", {"ui": n}), f"parse_spec recusou ou mudou o driver de UI {n!r}: {got!r}")
ui_flags = M["ui"]["angulo"]["flags"]
asked = {"ui": "angulo", "sensor_dht22": False}
got = parsed(json.dumps({"v": 1, "base": "pico_wiretft", "set": asked}))
check(got == ("pico_wiretft", asked), f"parse_spec mudou a UI e o sensor pedidos: {got!r}")
flipped = bc.resolve("pico_wiretft", asked, M)
shipped = gf.resolve_profile("pico_wiretft", M["profiles"])
check("ui" not in shipped and flipped["ui"] == "angulo",
      f"ui não chegou ao perfil resolvido: {shipped.get('ui')!r} -> {flipped.get('ui')!r}")
diff = {k for k in gf.TOGGLE_ORDER if bool(flipped.get(k)) != bool(shipped.get(k))}
check(diff == {"sensor_dht22"}, f"a UI mexeu em chave que ninguém pediu: {sorted(diff)}")
built, was = gf.compose(flipped, M), gf.compose(shipped, M)
check(all(f in built["flags"] and f not in was["flags"] for f in ui_flags),
      f"a identidade do driver de UI {ui_flags} não foi só para a build que o escolhe")
check(not any(re.match(r"-DSIMUT_UI_(ORIGINAL|CUSTOM_THEMES)\b", f) for f in built["flags"] + was["flags"]),
      "um macro derivado da UI foi passado por -D: ele sai do driver em src/ui/UiCaps.h")
# O perfil de bancada é o pico_wiretft com a UI trocada, e escolher o driver que o produto já tem
# compõe o mesmo ambiente que não escolher.
check(gf.compose(bc.resolve("pico_wiretft_angulo", {}, M), M) == gf.compose(bc.resolve("pico_wiretft", {"ui": "angulo"}, M), M),
      "o pico_wiretft_angulo não é o pico_wiretft com ui = angulo")
# O pico2_w_original (2026-10-30, S10c C3) é o pico2_w_release com a UI trocada de volta, e mais nada: o configurador compõe os dois iguais, e o ambiente que o
# gerador escreve, o de verdade, também, sem a raiz de bancada (ela é de imagem de bancada, e esta é o painel original do produto, sem ela como o pai).
_b = gf.compose(bc.resolve("pico2_w_release", {"ui": "original"}, M), M)
check(gf.compose(bc.resolve("pico2_w_original", {}, M), M) == _b, "o pico2_w_original não é o pico2_w_release com ui = original")
check(gf.compose(gf.resolve_profile("pico2_w_original", M["profiles"]), M) == _b,
      "o ambiente pico2_w_original não é o pico2_w_release com ui = original, e mais nada (nem a raiz de bancada)")
for prof in M["profiles"]:
    own = gf.resolve_profile(prof, M["profiles"]).get("ui", gf.DEFAULT_UI)
    check(gf.compose(bc.resolve(prof, {"ui": own}, M), M) == gf.compose(bc.resolve(prof, {}, M), M),
          f"{prof}: escolher a própria UI ({own}) mudou o ambiente")

# Forma canônica: a ordem em que as chaves chegam não muda o nome da build.
a = bc.canonical("pico_w_alpha", {"mdns": True, "bluetooth": False})
b = bc.canonical("pico_w_alpha", {"bluetooth": False, "mdns": True})
check(a == b, "canonical depende da ordem das chaves")
check(a == '{"base":"pico_w_alpha","set":{"bluetooth":false,"mdns":true},"v":1}',
      f"canonical mudou de grafia (a página escreve a mesma): {a}")
# `net` ordena com as outras chaves, e é texto, não booleano: a grafia que a página escreve.
a = bc.canonical("pico_w_release", {"sensor_ds18b20": False, "net": "enc28j60", "mdns": True})
b = bc.canonical("pico_w_release", {"net": "enc28j60", "mdns": True, "sensor_ds18b20": False})
check(a == b, "canonical depende da ordem de `net` entre as chaves")
check(a == '{"base":"pico_w_release","set":{"mdns":true,"net":"enc28j60","sensor_ds18b20":false},"v":1}',
      f"canonical mudou a grafia de `net` (a página escreve a mesma): {a}")
check(bc.canonical("pico_w_release", {"net": "enc28j60"}) != bc.canonical("pico_w_release", {"net": "cyw43"}),
      "canonical dá o mesmo nome a duas redes diferentes")
# `ui` é texto e ordena com as outras chaves: a grafia que a página escreve.
a = bc.canonical("pico_wiretft", {"ui": "angulo", "sensor_dht22": False, "mdns": True})
b = bc.canonical("pico_wiretft", {"mdns": True, "sensor_dht22": False, "ui": "angulo"})
check(a == b, "canonical depende da ordem de `ui` entre as chaves")
check(a == '{"base":"pico_wiretft","set":{"mdns":true,"sensor_dht22":false,"ui":"angulo"},"v":1}',
      f"canonical mudou a grafia de `ui` (a página escreve a mesma): {a}")
check(bc.canonical("pico_wiretft", {"ui": "angulo"}) != bc.canonical("pico_wiretft", {"ui": "original"}),
      "canonical dá o mesmo nome a duas UIs diferentes")

for text, want, needle in CLI:
    r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"],
                       env={**os.environ, "SIMUT_PROFILE": text}, capture_output=True, text=True)
    out = r.stdout + r.stderr
    check(r.returncode == want, f"--validate-only {text!r}: saiu {r.returncode}, esperado {want}\n{out}")
    check(needle in out, f"--validate-only {text!r}: a saída não diz «{needle}»\n{out}")

# O que a saida NAO pode dizer: a regra do Angulo no Pico W nao fala do Wire (so o radio a acende), nem do Pico 2 W, nem de quem fica no visual original.
CLI_ABSENT = [
    ('{"v":1,"base":"pico_wire","set":{"ui":"angulo"}}', "angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico_wiretft","set":{"ui":"angulo"}}', "angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"angulo"}}', "angulo-cyw43-needs-rp2350"),
    ('{"v":1,"base":"pico_w_release","set":{"ui":"original"}}', "angulo-cyw43-needs-rp2350"),
    # E a do grafico (2026-10-22) nao fala de quem tem o grafico, nem de quem fica no visual original sem ele.
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"angulo"}}', "angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico_wiretft","set":{"ui":"angulo"}}', "angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico2_w_release","set":{}}', "angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico_wiretft_angulo","set":{}}', "angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico2_w_release","set":{"ui":"original","tft_graph":false}}', "angulo-needs-tft-graph"),
    ('{"v":1,"base":"pico_wiretft","set":{"tft_graph":false}}', "angulo-needs-tft-graph"),
]
for text, needle in CLI_ABSENT:
    r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"],
                       env={**os.environ, "SIMUT_PROFILE": text}, capture_output=True, text=True)
    check(needle not in r.stdout + r.stderr, f"--validate-only {text!r}: a saída diz «{needle}», e a regra não é desta combinação\n{r.stdout}{r.stderr}")

# O controle da regra (2026-10-22): sem ela, o caso que esperava a recusa deixa de ser recusado. Primeiro em memoria, pelo mesmo gf.blockers( ) que o
# build_custom.py aplica; depois pela linha de comando, com o manifesto sem o bloco da regra (o build_custom.py sobre um gen_features que le a copia).
NEW_RULE = "angulo-cyw43-needs-rp2350"
M_sem = copy.deepcopy(M)
M_sem["rules"] = [r for r in M_sem["rules"] if r["id"] != NEW_RULE]
check(len(M_sem["rules"]) == len(M["rules"]) - 1, f"a regra {NEW_RULE} não está no manifesto: o controle não provou nada")
for base_, set_ in (("pico_w_release", {"ui": "angulo"}), ("pico_wiretft", {"ui": "angulo", "net": "cyw43"}), ("pico_w_alpha", {"ui": "angulo"})):
    com = [b["id"] for b in gf.blockers(bc.resolve(base_, set_, M), M)]
    sem = [b["id"] for b in gf.blockers(bc.resolve(base_, set_, M_sem), M_sem)]
    check(NEW_RULE in com, f"{base_} {set_}: com a regra, deveria ser recusado por {NEW_RULE}: {com}")
    check(NEW_RULE not in sem, f"{base_} {set_}: sem a regra, ainda é recusado por ela: {sem}")
for base_, set_ in (("pico2_w_release", {"ui": "angulo"}), ("pico_wiretft", {"ui": "angulo"}), ("pico2_w_release", {}), ("pico_wiretft_angulo", {})):
    check(gf.blockers(bc.resolve(base_, set_, M), M) == [], f"{base_} {set_}: deveria passar pelas regras: {gf.blockers(bc.resolve(base_, set_, M), M)}")
with open(gf.MANIFEST, encoding="utf-8") as fh:
    toml_text = fh.read()
bloco = re.search(r'\[\[rules\]\]\nid\s*=\s*"' + re.escape(NEW_RULE) + r'"\n(?:(?!\n\n).)*\n\n', toml_text, re.S)
check(bloco is not None, f"o bloco [[rules]] de {NEW_RULE} não está no features.toml: o controle pela linha de comando não provou nada")
if bloco:
    with tempfile.TemporaryDirectory() as pasta:
        copia = os.path.join(pasta, "features_sem_a_regra.toml")
        with open(copia, "w", encoding="utf-8") as fh:
            fh.write(toml_text.replace(bloco.group(0), ""))
        harness = ("import sys; sys.path.insert(0, %r); import gen_features as gf; gf.MANIFEST = %r; "
                   "import build_custom as bc; sys.argv = ['build_custom.py', '--validate-only']; sys.exit(bc.main())" % (HERE, copia))
        # Sem a regra o perfil ainda sai 2, mas por outro motivo: o orçamento de RAM do Ângulo não tem ambiente nesse hardware (o RP2040 com o radio), a segunda guarda.
        # O que o controle prova é o que a regra dá: o nome dela na saída. O caso que espera «regra angulo-cyw43-needs-rp2350» reprova sem ela.
        for text in ('{"v":1,"base":"pico_w_release","set":{"ui":"angulo"}}', '{"v":1,"base":"pico_wiretft","set":{"ui":"angulo","net":"cyw43"}}'):
            r = subprocess.run([sys.executable, "-c", harness], env={**os.environ, "SIMUT_PROFILE": text}, capture_output=True, text=True)
            check(NEW_RULE not in r.stdout + r.stderr, f"controle: sem a regra, a saída de {text!r} ainda a nomeia\n{r.stdout}{r.stderr}")
            check(r.returncode == 2 and "orçamento de RAM" in r.stdout + r.stderr,
                  f"controle: sem a regra, {text!r} deveria ser recusado ainda pela segunda guarda (o orçamento de RAM), saiu {r.returncode}\n{r.stdout}{r.stderr}")
            r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"], env={**os.environ, "SIMUT_PROFILE": text},
                               capture_output=True, text=True)
            check(r.returncode == 2 and NEW_RULE in r.stdout + r.stderr, f"com a regra, {text!r} deveria sair 2 nomeando {NEW_RULE}, saiu {r.returncode}\n{r.stdout}{r.stderr}")

# O Angulo sem o grafico do painel (2026-10-22). Sem a regra angulo-needs-tft-graph, ui = angulo com tft_graph desligado passava pelo manifesto (exit 0) e a build parava no
# compilador: src/ui/angulo/DisplayManager_UiA.cpp:293:44: error: '_graphData' was not declared in this scope (o membro so existe com SIMUT_TFT_GRAPH). O controle e o do bloco
# acima, em memoria e pela linha de comando com o manifesto sem o bloco: sem a regra o perfil passa, e o caso que espera a recusa reprova.
GRAPH_RULE = "angulo-needs-tft-graph"
GRAPH_OFF = (("pico2_w_release", {"ui": "angulo", "tft_graph": False}), ("pico_wiretft", {"ui": "angulo", "tft_graph": False}),
             ("pico2_w_release", {"tft_graph": False}), ("pico_wiretft_angulo", {"tft_graph": False}))
M_sem_grafico = copy.deepcopy(M)
M_sem_grafico["rules"] = [r for r in M_sem_grafico["rules"] if r["id"] != GRAPH_RULE]
check(len(M_sem_grafico["rules"]) == len(M["rules"]) - 1, f"a regra {GRAPH_RULE} não está no manifesto: o controle não provou nada")
for base_, set_ in GRAPH_OFF:
    com = [b["id"] for b in gf.blockers(bc.resolve(base_, set_, M), M)]
    sem = [b["id"] for b in gf.blockers(bc.resolve(base_, set_, M_sem_grafico), M_sem_grafico)]
    check(com == [GRAPH_RULE], f"{base_} {set_}: com a regra, deveria ser recusado só por {GRAPH_RULE}: {com}")
    check(sem == [], f"{base_} {set_}: sem a regra, nada mais recusa o perfil (foi o compilador que o recusou): {sem}")
for base_, set_ in (("pico2_w_release", {"ui": "original", "tft_graph": False}), ("pico_wiretft", {"tft_graph": False}),
                    ("pico2_w_release", {"ui": "angulo"}), ("pico_wiretft", {"ui": "angulo"}), ("pico2_w_release", {}), ("pico_wiretft_angulo", {})):
    got = [b["id"] for b in gf.blockers(bc.resolve(base_, set_, M), M)]
    check(got == [], f"{base_} {set_}: deveria passar pelas regras (tem o gráfico, ou fica no visual original): {got}")
bloco_g = re.search(r'\[\[rules\]\]\nid\s*=\s*"' + re.escape(GRAPH_RULE) + r'"\n(?:(?!\n\n).)*\n\n', toml_text, re.S)
check(bloco_g is not None, f"o bloco [[rules]] de {GRAPH_RULE} não está no features.toml: o controle pela linha de comando não provou nada")
if bloco_g:
    with tempfile.TemporaryDirectory() as pasta:
        copia = os.path.join(pasta, "features_sem_a_regra_do_grafico.toml")
        with open(copia, "w", encoding="utf-8") as fh:
            fh.write(toml_text.replace(bloco_g.group(0), ""))
        harness = ("import sys; sys.path.insert(0, %r); import gen_features as gf; gf.MANIFEST = %r; "
                   "import build_custom as bc; sys.argv = ['build_custom.py', '--validate-only']; sys.exit(bc.main())" % (HERE, copia))
        for base_, set_ in GRAPH_OFF[:2]:
            text = json.dumps({"v": 1, "base": base_, "set": set_}, separators=(",", ":"))
            r = subprocess.run([sys.executable, "-c", harness], env={**os.environ, "SIMUT_PROFILE": text}, capture_output=True, text=True)
            out = r.stdout + r.stderr
            check(r.returncode == 0 and GRAPH_RULE not in out and "perfil válido" in out, f"controle: sem a regra, {text!r} deveria passar (exit 0), saiu {r.returncode}\n{out}")
            r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"], env={**os.environ, "SIMUT_PROFILE": text},
                               capture_output=True, text=True)
            out = r.stdout + r.stderr
            check(r.returncode == 2 and GRAPH_RULE in out, f"com a regra, {text!r} deveria sair 2 nomeando {GRAPH_RULE}, saiu {r.returncode}\n{out}")

# O orcamento de RAM estatica do Angulo numa build do configurador (2026-10-22). O ambiente `_custom` nao tem entrada no tools/ui_ram_budget.json, e por isso
# nenhuma build do configurador com o Angulo linkava. O build_custom.py nomeia o ambiente de verdade cuja entrada vale para ela, derivado do hardware (chip,
# rede, mostrador) e do que o orcamento registra, e o escreve como opcao do ambiente temporario; o portao (tools/check_ui_ram.py) le a opcao, e a entrada
# continua a de la: o numero nao e copiado para ca.
import check_ui_ram as cur  # noqa: E402
budgets, erro_orc = cur.ler_orcamentos(bc.UI_RAM_BUDGET)
check(budgets is not None, f"o orçamento de RAM não leu: {erro_orc}")
if budgets is not None:
    for base_, set_, want in (("pico2_w_release", {"ui": "angulo"}, "pico2_w_release"),
                              ("pico2_w_release", {}, "pico2_w_release"),
                              ("pico_wiretft", {"ui": "angulo"}, "pico_wiretft_angulo"),
                              ("pico_wiretft_angulo", {}, "pico_wiretft_angulo"),
                              ("pico_w_release", {"ui": "angulo", "net": "enc28j60", "sensor_ds18b20": False}, "pico_wiretft_angulo")):
        got, why = bc.ui_ram_env(bc.resolve(base_, set_, M), M, budgets)
        check(got == want, f"{base_} {set_}: o orçamento de RAM é o de {got!r} ({why}), esperado {want!r}")
    # Derivado, nao copiado: o nome vem do hardware, e o numero e o da entrada que existe agora.
    mudado = {**budgets, "pico2_w_release": {"budget": 1}}
    check(bc.ui_ram_env(bc.resolve("pico2_w_release", {"ui": "angulo"}, M), M, mudado)[0] == "pico2_w_release",
          "o ambiente do orçamento mudou com o número da entrada: o nome deveria sair do hardware")
    semente = {k: v for k, v in budgets.items() if k != "pico2_w_release"}
    got, why = bc.ui_ram_env(bc.resolve("pico2_w_release", {"ui": "angulo"}, M), M, semente)
    check(got is None and "ui_ram_budget.json" in why, f"sem a entrada do pico2_w_release, deveria recusar e dizer onde registrá-la: {got!r} {why!r}")
    # Toda combinacao de hardware que as regras deixam passar com o Angulo tem um ambiente com orcamento (e so as duas de hoje passam).
    cobertas = set()
    for nome in M["profiles"]:
        for rede in M["net"]:
            prof = bc.resolve(nome, {"ui": "angulo", "net": rede}, M)
            if gf.blockers(prof, M):
                continue
            got, why = bc.ui_ram_env(prof, M, budgets)
            check(got is not None, f"{nome} com o Angulo e net={rede} passa nas regras e não tem orçamento de RAM: {why}")
            cobertas.add((bc.chip_of(nome, M), rede))
    check(cobertas == {("rp2040", "enc28j60"), ("rp2350", "cyw43")}, f"o hardware que leva o Angulo mudou: {sorted(cobertas)}")
    # O texto do ambiente temporario: o de sempre, mais a opcao so quando a build leva o Angulo.
    prof_a = bc.resolve("pico2_w_release", {"ui": "angulo"}, M)
    prof_o = bc.resolve("pico2_w_release", {"ui": "original"}, M)
    texto_a = bc.custom_env(prof_a, gf.compose(prof_a, M), "pico2_w_release")
    check(texto_a == gf.emit_env(bc.ENV, gf.compose(prof_a, M)) + f"{cur.OPCAO_ORCAMENTO} = pico2_w_release\n",
          f"o ambiente temporário do Angulo não é o de sempre mais a opção do orçamento:\n{texto_a}")
    check(bc.custom_env(prof_o, gf.compose(prof_o, M), None) == gf.emit_env(bc.ENV, gf.compose(prof_o, M)),
          "o ambiente temporário de uma build sem o Angulo mudou")

# No Actions, uma linha de log que começa com :: é um comando. Um perfil cuja
# chave carrega uma quebra de linha e um comando não pode emitir esse comando;
# a recusa sai como UMA anotação de erro, escapada. Vale para a chave e, desde que a
# rede é uma escolha de texto (2026-10-13), para o valor de `net`.
for where, evil in (("na chave", AIR % '{"mdns\\n::warning::injetado":true}'),
                    ("no valor de net", AIR % '{"net":"cyw43\\n::warning::injetado"}'),
                    ("no valor de ui", AIR % '{"ui":"angulo\\n::warning::injetado"}')):
    r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"],
                       env={**os.environ, "SIMUT_PROFILE": evil, "GITHUB_ACTIONS": "true"},
                       capture_output=True, text=True)
    lines = r.stdout.splitlines()
    check(r.returncode == 2, f"perfil com comando embutido {where} saiu {r.returncode}, esperado 2")
    check(not any(ln.startswith("::warning") for ln in lines), f"comando injetado {where} no log:\n{r.stdout}")
    check(sum(ln.startswith("::error title=build_custom::") for ln in lines) == 1,
          f"a recusa {where} não virou uma anotação de erro:\n{r.stdout}")

# ── O Pico 2 W (RP2350), no configurador desde 2026-10-05 ───────────────────
# As duas imagens dele não são o firmware.bin e o firmware.uf2 do Pico W. O .bin
# é a atualização, marcada para teste: vai para o outro slot e sobe em teste
# (docs/analysis/OTA_AB_RP2350.md). O .uf2 é o de fábrica, com a tabela de
# partições na frente (tools/check_rp2350_image.py). E o teto é o do slot, nos
# dois casos, não o da OTA do Pico W.
import check_flash_budget as cfb  # noqa: E402
import release_manifest as rm  # noqa: E402
sys.path.insert(0, os.path.join(HERE, "rp2350"))
import gen_memmap  # noqa: E402  (tools/rp2350/gen_memmap.py)

check(bc.outputs_for("pico_w_air", M) == {"bin": "firmware.bin", "uf2": "firmware.uf2"},
      f"Pico W: o configurador entregaria {bc.outputs_for('pico_w_air', M)}")
check(bc.outputs_for("pico2_w_release", M) == {"bin": "firmware_ota.bin", "uf2": "firmware_factory.uf2",
                                                "table": "partition_table.uf2"},
      f"Pico 2 W: o configurador entregaria {bc.outputs_for('pico2_w_release', M)}")
for prof in M["profiles"]:
    chip = gf.resolve_profile(prof, M["profiles"]).get("chip") or "rp2040"
    check(bc.chip_of(prof, M) == chip, f"{prof}: chip_of diz {bc.chip_of(prof, M)}, o manifesto {chip}")
slot = gen_memmap.partitions()["A"][1]
trailer = cfb.sig_trailer_len()
check(cfb.ceilings("pico2_w_release") == {"flash_region": slot, "ota_bin": slot - trailer},
      f"Pico 2 W: tetos {cfb.ceilings('pico2_w_release')}, o slot tem {slot} B")
with open(cfb.BUDGET_FILE, encoding="utf-8") as fh:
    sketch = json.load(fh)["ceiling"]
for prof in ("pico_w_release", "pico_w_alpha", "pico_w_air"):
    check(cfb.ceilings(prof) == {"flash_region": sketch, "ota_bin": cfb.ota_bin_max()},
          f"{prof}: tetos {cfb.ceilings(prof)}, os do Pico W são {sketch} e {cfb.ota_bin_max()}")

# Depois da assinatura (o job `sign` do build-custom.yml): o build.json passa a
# descrever o .bin assinado, e o .uf2 do Pico 2 W é refeito a partir dele, como a
# release faz (tools/release_manifest.py), e a tabela sai do artefato.
EXE_RP2350, TBYB = 0x1021, 0x8000


def pico2_image(n=5000):
    """A imagem sintética do test_release_manifest.py: a definição de imagem em
    0x124, marcada para teste, e a etiqueta da variante; assinada (um trailer)."""
    img = bytearray((i * 31 + 7) & 0xFF for i in range(n))
    words = [0xFFFFDED3, 0x42 | (1 << 8) | ((EXE_RP2350 | TBYB) << 16), 0xFF | (1 << 8), 0, 0xAB123579]
    img[0x124:0x124 + 20] = struct.pack("<5I", *words)
    tag = f"SIMUT-ENV:releasetwo;v={rm.version_from_source()};".encode()
    img[2000:2000 + len(tag)] = tag
    return bytes(img) + bytes((i * 7) & 0xFF for i in range(trailer))


def uf2_table():
    b = bytearray(512)
    struct.pack_into("<8I", b, 0, 0x0A324655, 0x9E5D5157, 0x2000, 0x10000000, 256, 0, 1, 0xE48BFF57)
    b[32:288] = bytes((i * 5 + 1) & 0xFF for i in range(256))
    struct.pack_into("<I", b, 508, 0x0AB16F30)
    return bytes(b)


def finished(chip, signed, table):
    """Uma pasta como o job `build` a entrega, já com o .bin assinado; roda o
    --finish-signed e devolve (código, build.json, .uf2, a tabela ainda existe)."""
    with tempfile.TemporaryDirectory() as t:
        name = f"simut_{'pico2_w_release' if chip == 'rp2350' else 'pico_w_air'}_0123abcd"
        with open(os.path.join(t, f"{name}.bin"), "wb") as fh:
            fh.write(signed)
        with open(os.path.join(t, f"{name}.uf2"), "wb") as fh:
            fh.write(b"o .uf2 que o build deixou")
        if table is not None:
            with open(os.path.join(t, "partition_table.uf2"), "wb") as fh:
                fh.write(table)
        report = {"chip": chip, "files": {
            "bin": {"file": f"{name}.bin", "sha256": "0" * 64, "bytes": 1},
            "uf2": {"file": f"{name}.uf2", "sha256": "0" * 64, "bytes": 1}}}
        with open(os.path.join(t, "build.json"), "w", encoding="utf-8") as fh:
            json.dump(report, fh)
        r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--finish-signed", t],
                           capture_output=True, text=True)
        with open(os.path.join(t, "build.json"), encoding="utf-8") as fh:
            rep = json.load(fh)
        with open(os.path.join(t, f"{name}.uf2"), "rb") as fh:
            uf2 = fh.read()
        return r.returncode, rep, uf2, os.path.exists(os.path.join(t, "partition_table.uf2")), r.stdout + r.stderr


signed2, table = pico2_image(), uf2_table()
with tempfile.TemporaryDirectory() as t:
    with open(os.path.join(t, "pt.uf2"), "wb") as fh:
        fh.write(table)
    want_uf2 = rm.rp2350_factory(signed2, os.path.join(t, "pt.uf2"))
code, rep, uf2, left, out = finished("rp2350", signed2, table)
check(code == 0, f"--finish-signed recusou o Pico 2 W: {out}")
check(uf2 == want_uf2, "o .uf2 do Pico 2 W não é o de fábrica refeito do .bin assinado (release_manifest)")
check(not left, "a tabela de partições ficou no artefato")
check(rep.get("signed") is True, "o build.json não diz signed")
check(rep["files"]["bin"]["sha256"] == hashlib.sha256(signed2).hexdigest()
      and rep["files"]["bin"]["bytes"] == len(signed2), f"o build.json não descreve o .bin assinado: {rep['files']['bin']}")
check(rep["files"]["uf2"]["sha256"] == hashlib.sha256(want_uf2).hexdigest()
      and rep["files"]["uf2"]["bytes"] == len(want_uf2), f"o build.json não descreve o .uf2 novo: {rep['files']['uf2']}")

signed1 = b"\x01" * 3000 + bytes(trailer)
code, rep, uf2, left, out = finished("rp2040", signed1, None)
check(code == 0, f"--finish-signed recusou o Pico W: {out}")
check(uf2 == b"o .uf2 que o build deixou", "o .uf2 do Pico W foi mexido depois da assinatura")
check(rep.get("signed") is True and rep["files"]["bin"]["sha256"] == hashlib.sha256(signed1).hexdigest(),
      f"Pico W: o build.json não descreve o .bin assinado: {rep}")
check(rep["files"]["uf2"]["bytes"] == 1, "Pico W: o build.json do .uf2 foi reescrito")

code, rep, uf2, left, out = finished("rp2350", signed2, None)
check(code != 0, "--finish-signed aceitou um Pico 2 W sem a tabela de partições")
check(rep.get("signed") is not True, "um Pico 2 W sem tabela saiu marcado como assinado")

# O workflow chama o acabamento, e instala o framework do RP2350 antes do patch.
with open(os.path.join(gf.ROOT, ".github", "workflows", "build-custom.yml"), encoding="utf-8") as fh:
    wf = fh.read()
check("python3 tools/build_custom.py --finish-signed build-custom" in wf,
      "o job sign do build-custom.yml não roda o --finish-signed")
i_pkg, i_patch = wf.find("pio pkg install -e pico2_w_release"), wf.find("bash tools/arduino_pico_overrides/patch.sh")
check(0 <= i_pkg < i_patch, "o build-custom.yml não instala o pico2_w_release antes do patch do framework")

if failures:
    print("test_build_custom: FALHOU")
    for f in failures:
        print("  -", f)
    sys.exit(1)
print(f"test_build_custom: {len(HOSTILE)} perfis hostis recusados, {len(M['profiles'])} produtos "
      f"iguais ao manifesto, {len(CLI)} casos pela linha de comando, a rede como escolha de "
      f"driver ({len(M['net'])} do manifesto: {', '.join(M['net'])}), a UI do painel como a outra "
      f"({len(M['ui'])}: {', '.join(M['ui'])}), log sem comando injetado; "
      f"o Ângulo recusado no Pico W e aceito no Pico 2 W e no Wire com TFT, com o controle sem a regra, e o ambiente do "
      f"orçamento de RAM dele derivado do hardware; "
      f"o Ângulo sem o gráfico do painel recusado nas duas bases que o têm, com o controle sem a regra; "
      f"imagens e tetos de cada chip, e o acabamento depois da assinatura (Pico 2 W, Pico W, "
      f"Pico 2 W sem tabela).")
