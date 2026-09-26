#!/usr/bin/env python3
"""test_configurator_page.py — o que a página do configurador calcula, conferido.

docs/configurador/logic.js faz as contas da página: a estimativa, as travas, o
link e o perfil que ela manda compilar. Este teste roda esse arquivo de verdade
no node, contra o docs/configurador/model.json gerado, e confere cada resposta
com o lado Python, que é a autoridade:

  - o perfil que a página escreve é aceito por tools/build_custom.py e tem a
    mesma grafia canônica (o mesmo nome de build) que ele calcula;
  - o link vai e volta igual, e os links hostis são recusados;
  - com uma chave mudada, a estimativa é EXATAMENTE a medida da matriz — a
    soma só é soma a partir de duas;
  - uma chave travada na página é uma que gen_features.rule_violations
    proibiria, e vice-versa;
  - "não muda nada neste produto" é exatamente o `same` que a matriz mediu;
  - o erro que a página diz ter é o erro recalculado aqui.

    python3 tools/test_configurator_page.py     # exit 1 em qualquer divergência

@project SIMUT
@license MIT License
"""
import itertools
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402
import build_custom as bc  # noqa: E402

PAGE = os.path.join(ROOT, "docs", "configurador")
M = gf.load_manifest()
with open(os.path.join(PAGE, "model.json"), encoding="utf-8") as fh:
    MODEL = json.load(fh)
TOGGLES = [t["id"] for t in MODEL["toggles"]]
PRODUCTS = [p["id"] for p in MODEL["products"]]

HOSTILE_HASHES = [
    "#b=pico_w_nada",
    "#b=pico_w_air&on=wifi",
    "#b=pico_w_air&on=__proto__",
    "#b=pico_w_air&on=constructor",
    "#b=pico_w_air&on=mdns&off=mdns",
    "#b=pico_w_air&on=mdns,mdns",
    "#b=<img src=x onerror=alert(1)>",
    "#b=pico_w_air&on=mdns%0A",
    "#b=" + "a" * 2000,
]

# O script que o node roda: recebe os casos por stdin, devolve as respostas.
JS = r"""
import * as L from "./logic.js";
import { ruleViolations } from "./rules.js";
const input = JSON.parse(await new Promise((ok) => {
  let s = ""; process.stdin.on("data", (d) => (s += d)); process.stdin.on("end", () => ok(s));
}));
const model = input.model;
const out = { profiles: [], hashes: [], hostile: [], singles: [], locks: [], accuracy: null, margin: null };
for (const [p, set] of input.sets) {
  out.profiles.push(L.profileJson(p, set));
  out.hashes.push(L.parseHash(model, L.formatHash(p, L.normalizeSet(model, p, set))));
}
for (const h of input.hostile) out.hostile.push(L.parseHash(model, h));
for (const p of input.products) {
  const product = L.productById(model, p);
  for (const t of input.toggles) {
    const set = { [t]: !product.config[t] };
    out.singles.push({ p, t, est: L.estimate(model, p, set), same: L.noEffect(model, p, t) });
    const lock = L.lockOf(model, product.config, t);
    out.locks.push({ p, t, lock: lock ? lock.id : null });
  }
}
out.accuracy = L.accuracy(model);
out.margin = L.nearMargin(model);
process.stdout.write(JSON.stringify(out));
"""

failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def run_js(payload):
    node = shutil.which("node")
    if not node:
        if os.environ.get("CI"):
            sys.exit("test_configurator_page: node não encontrado — no CI isto é falha")
        print("test_configurator_page: node ausente, pulado (no CI é falha)")
        sys.exit(0)
    with tempfile.TemporaryDirectory() as tmp:
        for f in ("logic.js", "rules.js"):
            shutil.copy(os.path.join(PAGE, f), tmp)
        with open(os.path.join(tmp, "package.json"), "w") as fh:
            fh.write('{"type": "module"}\n')
        with open(os.path.join(tmp, "run.js"), "w") as fh:
            fh.write(JS)
        r = subprocess.run([node, "run.js"], cwd=tmp, input=json.dumps(payload),
                           capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"test_configurator_page: o node falhou\n{r.stderr}")
    return json.loads(r.stdout)


# Conjuntos a conferir: cada chave sozinha nos dois sentidos, e cada par.
sets = []
for p in PRODUCTS:
    sets.append((p, {}))
    for t in TOGGLES:
        sets.append((p, {t: True}))
        sets.append((p, {t: False}))
    for a, b in itertools.combinations(TOGGLES, 2):
        sets.append((p, {a: True, b: False}))

js = run_js({"model": MODEL, "sets": sets, "hostile": HOSTILE_HASHES,
             "products": PRODUCTS, "toggles": TOGGLES})

# 1. O perfil da página é o do build_custom.py, com a mesma grafia.
for (p, s), text in zip(sets, js["profiles"]):
    try:
        base, changes = bc.parse_spec(text, M)
    except bc.SpecError as e:
        failures.append(f"perfil da página recusado pelo build_custom: {text} ({e})")
        continue
    check((base, changes) == (p, s), f"perfil {text} lido como {(base, changes)}")
    check(text == bc.canonical(p, s), f"grafia da página {text} != canônica {bc.canonical(p, s)}")

# 2. O link vai e volta.
for (p, s), got in zip(sets, js["hashes"]):
    base = gf.config_of(gf.resolve_profile(p, M["profiles"]))
    want = {k: v for k, v in s.items() if v != base[k]}
    check(got.get("productId") == p and got.get("set") == want,
          f"link de {p} {s} voltou como {got}")

# 3. Links hostis: recusados, nunca aceitos pela metade.
for h, got in zip(HOSTILE_HASHES, js["hostile"]):
    check("error" in got, f"link hostil aceito: {h[:60]!r} -> {got}")

# 4. Uma chave: a estimativa É a medida. E `same` é o que a matriz mediu.
costs = MODEL["costs"]["products"] if MODEL.get("costs") else {}
for s in js["singles"]:
    p, t, est = s["p"], s["t"], s["est"]
    if p not in costs:
        check(est is None, f"{p} não tem custo medido, mas a página estimou {est}")
        continue
    flip = costs[p]["flips"][t]
    check(s["same"] == bool(flip.get("same")), f"{p}/{t}: same da página {s['same']} != matriz")
    if "used" in flip:
        check(est["used"] == flip["used"] and est["bin"] == flip["bin"] and est["ram"] == flip["ram"],
              f"{p}/{t}: estimativa {est} != medida {flip}")
    else:
        check(t in est["missing"] or "overflow" in flip, f"{p}/{t}: sem medida mas fora de missing")

# 5. As travas da página são as regras do Python.
for lk in js["locks"]:
    p, t = lk["p"], lk["t"]
    cfg = gf.config_of(gf.resolve_profile(p, M["profiles"]))
    now = {r["id"] for r in gf.rule_violations(cfg, M)}
    flipped = dict(cfg, **{t: not cfg[t]})
    new = [r["id"] for r in gf.rule_violations(flipped, M) if r["id"] not in now]
    check(lk["lock"] == (new[0] if new else None), f"{p}/{t}: trava da página {lk['lock']} != regras {new}")

# 6. O erro que a página diz ter.
checks = MODEL["costs"].get("checks", []) if MODEL.get("costs") else []
miss_used = miss_bin = n = 0
for c in checks:
    if "used" not in c:
        continue
    b = costs[c["base"]]["base"]
    flips = costs[c["base"]]["flips"]
    if any("used" not in flips[k] for k in c["set"]):
        continue
    est_used = b["used"] + sum(flips[k]["used"] - b["used"] for k in c["set"])
    est_bin = b["bin"] + sum(flips[k]["bin"] - b["bin"] for k in c["set"])
    miss_used = max(miss_used, abs(c["used"] - est_used))
    miss_bin = max(miss_bin, abs(c["bin"] - est_bin))
    n += 1
check(js["accuracy"] == {"n": n, "used": miss_used, "bin": miss_bin},
      f"precisão da página {js['accuracy']} != recalculada {(n, miss_used, miss_bin)}")
check(js["margin"] >= 4096 and js["margin"] >= max(miss_used, miss_bin),
      f"margem {js['margin']} menor que o erro medido ({miss_used}, {miss_bin})")

if failures:
    print("test_configurator_page: FALHOU")
    for f in failures[:40]:
        print("  -", f)
    if len(failures) > 40:
        print(f"  ... e mais {len(failures) - 40}")
    sys.exit(1)
print(f"test_configurator_page: {len(sets)} perfis e links, {len(HOSTILE_HASHES)} links hostis, "
      f"{len(js['singles'])} chaves medidas, {len(js['locks'])} travas; "
      f"erro medido {max(miss_used, miss_bin)} B em {n} conferências, margem {js['margin']} B.")
