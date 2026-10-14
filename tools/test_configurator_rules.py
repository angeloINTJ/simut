#!/usr/bin/env python3
"""test_configurator_rules.py — a página e o CI concordam sobre o que é proibido.

As regras entre recursos (tools/features.toml [[rules]] e [[hazards]]) são
aplicadas em dois lugares: tools/gen_features.py rule_violations( ), que
tools/build_custom.py usa antes de compilar, e docs/configurador/rules.js, que a
página usa para travar uma opção. Duas implementações da mesma regra divergem em
silêncio — esta suíte roda as duas contra a mesma tabela
(tools/configurator_cases.json) e reprova se qualquer uma se afastar da tabela
ou da outra. Também reprova se alguma regra ou aviso não tem caso que o acenda:
regra sem teste é regra que ninguém sabe se ainda funciona.

Desde 2026-10-13 a mesma tabela diz também, em `blocks`, tudo o que impede a build
de cada caso: as regras, o driver de rede que o chip recusa (unsupported-net) e o
orçamento de PIO e de pinos (tools/pio_budget.py). O Python é gen_features.blockers( ), o
que o CI aplica; o JS é logic.js blockersOf( ), o que a página aplica, e os dois têm de
dar a mesma lista, na mesma ordem. Todo bloqueio de orçamento e toda recusa de chip que
o model.json carrega precisa de um caso. A rede é uma escolha de driver: um caso a troca
com `"set": {"net": "<id>"}`, e as regras a leem como `net = "<id>"`.

O lado JS precisa de node (o runner ubuntu-latest do CI traz). Fora do CI, sem
node, só o lado Python roda, e a saída diz isso.

    python3 tools/test_configurator_rules.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402

ROOT = gf.ROOT
CASES = os.path.join(HERE, "configurator_cases.json")

# Roda rules.js e logic.js numa pasta temporária com um package.json de módulo,
# como tools/test_configurator_page.py: o logic.js importa "./rules.js", o que uma
# data: URL não resolve. O model.json e a tabela vêm por stdin; o model.json é o
# MESMO arquivo que a página publicada lê.
NODE_SCRIPT = r"""
import * as rules from "./rules.js";
import * as L from "./logic.js";
const input = JSON.parse(await new Promise((ok) => {
  let s = ""; process.stdin.on("data", (d) => (s += d)); process.stdin.on("end", () => ok(s));
}));
const { model, cases } = input;
const config = (c) => ({ ...model.products.find((p) => p.id === c.base).config, ...c.set });
const out = { cases: {}, blocks: {}, hazards: {} };
for (const c of cases.cases) {
  out.cases[c.name] = rules.ruleViolations(model, config(c)).map((r) => r.id).sort();
  out.blocks[c.name] = L.blockersOf(model, c.base, config(c)).map((b) => b.id);
}
for (const c of cases.hazard_cases)
  out.hazards[c.name] = rules.hazardHits(model, config(c)).map((h) => h.id).sort();
process.stdout.write(JSON.stringify(out));
"""


def run_node(node, table):
    with open(os.path.join(ROOT, "docs", "configurador", "model.json"), encoding="utf-8") as fh:
        model = json.load(fh)
    with tempfile.TemporaryDirectory() as tmp:
        for f in ("rules.js", "logic.js"):
            shutil.copy(os.path.join(ROOT, "docs", "configurador", f), tmp)
        with open(os.path.join(tmp, "package.json"), "w") as fh:
            fh.write('{"type": "module"}\n')
        with open(os.path.join(tmp, "run.js"), "w") as fh:
            fh.write(NODE_SCRIPT)
        r = subprocess.run([node, "run.js"], cwd=tmp, input=json.dumps({"model": model, "cases": table}),
                           capture_output=True, text=True)
    return r, model


def py_profile(M, case):
    prof = gf.resolve_profile(case["base"], M["profiles"])
    prof.update(case["set"])
    return prof


def py_config(M, case):
    return gf.config_of(py_profile(M, case))


def main():
    fails = []
    M = gf.load_manifest()
    with open(CASES, encoding="utf-8") as fh:
        table = json.load(fh)

    # A própria tabela tem de falar de produtos, chaves e drivers de rede que existem.
    for c in table["cases"] + table["hazard_cases"]:
        if c["base"] not in M["profiles"]:
            fails.append(f"caso '{c['name']}': produto desconhecido {c['base']}")
        for k, v in c["set"].items():
            if k == "net":
                if v not in M["net"]:
                    fails.append(f"caso '{c['name']}': net={v!r} não é um driver de rede ({', '.join(M['net'])})")
            elif k not in gf.TOGGLE_ORDER or not isinstance(v, bool):
                fails.append(f"caso '{c['name']}': {k}={v!r} não é uma chave true/false")
    for c in table["cases"]:
        if not isinstance(c.get("blocks"), list):
            fails.append(f"caso '{c['name']}': sem a lista `blocks`")
    if fails:
        print("\n".join(fails))
        return 1

    py = {c["name"]: sorted(r["id"] for r in gf.rule_violations(py_config(M, c), M))
          for c in table["cases"]}
    pyh = {c["name"]: sorted(h["id"] for h in gf.hazard_hits(py_config(M, c), M))
           for c in table["hazard_cases"]}
    for c in table["cases"]:
        if py[c["name"]] != sorted(c["violates"]):
            fails.append(f"python, '{c['name']}': quebra {py[c['name']]}, a tabela diz {sorted(c['violates'])}")
    pyb = {c["name"]: [b["id"] for b in gf.blockers(py_profile(M, c), M)] for c in table["cases"]}
    for c in table["cases"]:
        if sorted(pyb[c["name"]]) != sorted(c["blocks"]):
            fails.append(f"python, '{c['name']}': bloqueia {pyb[c['name']]}, a tabela diz {c['blocks']}")
    for c in table["hazard_cases"]:
        if pyh[c["name"]] != sorted(c["hazards"]):
            fails.append(f"python, '{c['name']}': avisa {pyh[c['name']]}, a tabela diz {sorted(c['hazards'])}")

    node = shutil.which("node")
    sides = "Python"
    if not node:
        if os.environ.get("CI"):
            fails.append("node ausente no CI: o lado JS (a página) não foi testado")
        else:
            print("AVISO: node ausente — só o lado Python foi testado.")
    else:
        r, model = run_node(node, table)
        if r.returncode != 0:
            fails.append("node falhou:\n" + r.stderr.strip())
        else:
            js = json.loads(r.stdout)
            sides = "Python e JS"
            for c in table["cases"]:
                got = js["cases"][c["name"]]
                if got != py[c["name"]]:
                    fails.append(f"JS e Python divergem em '{c['name']}': JS {got}, Python {py[c['name']]}")
                got = js["blocks"][c["name"]]
                if got != pyb[c["name"]]:
                    fails.append(f"JS e Python divergem no que bloqueia '{c['name']}': JS {got}, "
                                 f"Python {pyb[c['name']]}")
            lit_blocks = {bid for c in table["cases"] for bid in c["blocks"]}
            for b in model.get("budget", []):
                if b["id"] not in lit_blocks:
                    fails.append(f"o bloqueio de orçamento {b['id']} ({b['chip']}, {b['when']}) não tem caso que o acenda")
            # Cada recusa de chip que o model.json carrega ("net=enc28j60" no Pico 2 W) tem um caso do
            # produto que a faz, e o caso a acusa como unsupported-net: uma recusa sem caso é uma trava
            # que ninguém sabe se ainda trava.
            for p in model["products"]:
                for key in p.get("refused", {}):
                    k, _, v = key.partition("=")
                    if not any(c["base"] == p["id"] and c["set"].get(k) == v and "unsupported-" + k in c["blocks"]
                               for c in table["cases"]):
                        fails.append(f"a recusa {key} de {p['id']} não tem caso que a acenda (unsupported-{k})")
            for c in table["hazard_cases"]:
                got = js["hazards"][c["name"]]
                if got != pyh[c["name"]]:
                    fails.append(f"JS e Python divergem no aviso '{c['name']}': JS {got}, Python {pyh[c['name']]}")

    lit_rules = {rid for c in table["cases"] for rid in c["violates"]}
    lit_hazards = {hid for c in table["hazard_cases"] for hid in c["hazards"]}
    for r in M.get("rules", []):
        if r["id"] not in lit_rules:
            fails.append(f"a regra {r['id']} não tem caso que a quebre")
    for h in M.get("hazards", []):
        if h["id"] not in lit_hazards:
            fails.append(f"o aviso {h['id']} não tem caso que o acenda")

    if fails:
        print("test_configurator_rules: reprovado")
        for f in fails:
            print("  " + f)
        return 1
    n = len(table["cases"]) + len(table["hazard_cases"])
    print(f"test_configurator_rules: {n} casos; {sides} concordam com a tabela.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
