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
import json
import os
import subprocess
import sys

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
    ("grande demais", '{"v":1,"base":"pico_w_air","set":{}}' + " " * bc.MAX_SPEC),
]

# (perfil, código de saída do --validate-only, trecho que a saída tem de ter)
CLI = [
    ('{"v":1,"base":"pico_w_air","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_air"}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_alpha","set":{"bluetooth":false,"mdns":true}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_test","set":{}}', 0, "perfil válido"),
    ('{"v":1,"base":"pico_w_release","set":{"bluetooth":true}}', 0, "aviso bt-flash-core1"),
    ('{"v":1,"base":"pico_w_release","set":{"air":true}}', 2, "regra air-needs-headless"),
    ('{"v":1,"base":"pico_w_air","set":{"air":false}}', 2, "regra headless-needs-air"),
    ('{"v":1,"base":"pico_w_air","set":{"sound_buzzer":true}}', 2, "regra air-no-buzzer"),
    ('{"v":1,"base":"pico_w_air","set":{"wifi":true}}', 2, "chave desconhecida"),
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

# Forma canônica: a ordem em que as chaves chegam não muda o nome da build.
a = bc.canonical("pico_w_alpha", {"mdns": True, "bluetooth": False})
b = bc.canonical("pico_w_alpha", {"bluetooth": False, "mdns": True})
check(a == b, "canonical depende da ordem das chaves")
check(a == '{"base":"pico_w_alpha","set":{"bluetooth":false,"mdns":true},"v":1}',
      f"canonical mudou de grafia (a página escreve a mesma): {a}")

for text, want, needle in CLI:
    r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"],
                       env={**os.environ, "SIMUT_PROFILE": text}, capture_output=True, text=True)
    out = r.stdout + r.stderr
    check(r.returncode == want, f"--validate-only {text!r}: saiu {r.returncode}, esperado {want}\n{out}")
    check(needle in out, f"--validate-only {text!r}: a saída não diz «{needle}»\n{out}")

# No Actions, uma linha de log que começa com :: é um comando. Um perfil cuja
# chave carrega uma quebra de linha e um comando não pode emitir esse comando;
# a recusa sai como UMA anotação de erro, escapada.
evil = AIR % '{"mdns\\n::warning::injetado":true}'
r = subprocess.run([sys.executable, os.path.join(HERE, "build_custom.py"), "--validate-only"],
                   env={**os.environ, "SIMUT_PROFILE": evil, "GITHUB_ACTIONS": "true"},
                   capture_output=True, text=True)
lines = r.stdout.splitlines()
check(r.returncode == 2, f"perfil com comando embutido saiu {r.returncode}, esperado 2")
check(not any(ln.startswith("::warning") for ln in lines), f"comando injetado no log:\n{r.stdout}")
check(sum(ln.startswith("::error title=build_custom::") for ln in lines) == 1,
      f"a recusa não virou uma anotação de erro:\n{r.stdout}")

if failures:
    print("test_build_custom: FALHOU")
    for f in failures:
        print("  -", f)
    sys.exit(1)
print(f"test_build_custom: {len(HOSTILE)} perfis hostis recusados, {len(M['profiles'])} produtos "
      f"iguais ao manifesto, {len(CLI)} casos pela linha de comando, log sem comando injetado.")
