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

O lado JS precisa de node (o runner ubuntu-latest do CI traz). Fora do CI, sem
node, só o lado Python roda, e a saída diz isso.

    python3 tools/test_configurator_rules.py
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402

ROOT = gf.ROOT
CASES = os.path.join(HERE, "configurator_cases.json")

# Importa rules.js por data: URL para não exigir package.json no site nem flag
# de módulo; o model.json é o MESMO arquivo que a página publicada lê.
NODE_SCRIPT = r"""
import fs from 'node:fs';
const src = fs.readFileSync('docs/configurador/rules.js', 'utf8');
const rules = await import('data:text/javascript;base64,' + Buffer.from(src).toString('base64'));
const model = JSON.parse(fs.readFileSync('docs/configurador/model.json', 'utf8'));
const cases = JSON.parse(fs.readFileSync('tools/configurator_cases.json', 'utf8'));
const config = (c) => ({ ...model.products.find((p) => p.id === c.base).config, ...c.set });
const out = { cases: {}, hazards: {} };
for (const c of cases.cases)
  out.cases[c.name] = rules.ruleViolations(model, config(c)).map((r) => r.id).sort();
for (const c of cases.hazard_cases)
  out.hazards[c.name] = rules.hazardHits(model, config(c)).map((h) => h.id).sort();
process.stdout.write(JSON.stringify(out));
"""


def py_config(M, case):
    prof = gf.resolve_profile(case["base"], M["profiles"])
    prof.update(case["set"])
    return gf.config_of(prof)


def main():
    fails = []
    M = gf.load_manifest()
    with open(CASES, encoding="utf-8") as fh:
        table = json.load(fh)

    # A própria tabela tem de falar de produtos e chaves que existem.
    for c in table["cases"] + table["hazard_cases"]:
        if c["base"] not in M["profiles"]:
            fails.append(f"caso '{c['name']}': produto desconhecido {c['base']}")
        for k, v in c["set"].items():
            if k not in gf.TOGGLE_ORDER or not isinstance(v, bool):
                fails.append(f"caso '{c['name']}': {k}={v!r} não é uma chave true/false")
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
        r = subprocess.run([node, "--input-type=module", "-e", NODE_SCRIPT],
                           cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            fails.append("node falhou:\n" + r.stderr.strip())
        else:
            js = json.loads(r.stdout)
            sides = "Python e JS"
            for c in table["cases"]:
                got = js["cases"][c["name"]]
                if got != py[c["name"]]:
                    fails.append(f"JS e Python divergem em '{c['name']}': JS {got}, Python {py[c['name']]}")
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
