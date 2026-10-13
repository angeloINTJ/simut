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
    proibiria, ou que o gerador recusa naquele chip (gen_features.toggle_refusal:
    a rede por cabo no RP2350), e vice-versa -- com o motivo que o manifesto dá;
  - o que impede a build é uma lista só (L.blockersOf: as regras quebradas, as
    recusas do chip e, desde 2026-10-13, o orçamento de PIO e de pinos do
    model.json), e o app.js, que nenhum teste executa, a usa: o texto dele é
    conferido, porque sem o produto a trava e o botão perdem a recusa calados;
  - "não muda nada neste produto" é exatamente o `same` que a matriz mediu;
  - o erro que a página diz ter é o erro recalculado aqui.

    python3 tools/test_configurator_page.py     # exit 1 em qualquer divergência

@project SIMUT
@license MIT License
"""
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_features as gf  # noqa: E402
import pio_budget as pb  # noqa: E402
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
const out = { profiles: [], hashes: [], hostile: [], singles: [], locks: [], refusals: [], blockers: [], accuracy: null,
              margin: null, ceilings: null, overflow: null, slot: null, anchors: null };
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
    const lock = L.lockOf(model, product.config, t, p);
    out.locks.push({ p, t, lock: lock ? lock.id : null, why: lock ? lock.why : null });
    // A link can carry the switch already on: what the choice has on that the
    // generator refuses for the product's chip.
    out.refusals.push({ p, t, ids: L.refusalsIn(model, p, { ...product.config, [t]: true }).map((r) => r.id) });
    const on = { ...product.config, [t]: true };
    const estOn = L.estimate(model, p, { [t]: true });
    out.blockers.push({ p, t, ids: L.blockersOf(model, p, on).map((r) => r.id),
                        rules: ruleViolations(model, on).map((r) => r.id),
                        budget: L.budgetIn(model, p, on).map((r) => r.id),
                        verdict: typeof L.verdictOf === "function" ? L.verdictOf(model, p, { [t]: true }) : "sem verdictOf",
                        fit: estOn ? L.slotFit(model, p, estOn) : null });
  }
}
out.accuracy = L.accuracy(model);
out.margin = L.nearMargin(model);
out.ceilings = {};
out.overflow = {};
out.slot = {};
for (const p of input.products) {
  out.ceilings[p] = L.ceilingsOf(model, p);
  // A flip the linker refused, made up: the linker measures the image's extent
  // in its FLASH region, which is the .bin, so the estimate's .bin is the
  // product's own region plus the overflow it reported, whatever the product.
  const costs = model.costs && model.costs.products && model.costs.products[p];
  if (!costs) continue;
  const t = input.toggles[0];
  const fake = JSON.parse(JSON.stringify(model));
  fake.costs.products[p].flips[t] = { overflow: 100 };
  const product = L.productById(fake, p);
  const e = L.estimate(fake, p, { [t]: !product.config[t] });
  out.overflow[p] = { used: e.used, bin: e.bin };
  // The slot verdict follows the .bin: an image whose `used` is under the
  // region and whose .bin is over it does not link.
  const region = L.ceilingsOf(model, p).flash_region;
  out.slot[p] = {
    usedUnderBinOver: L.slotFit(model, p, { used: region - 1000, bin: region + 1000 }),
    bothUnder: L.slotFit(model, p, { used: region - 200000, bin: region - 190000 }),
  };
}
// An in-page anchor is not a build: the bar's "#resultado" and the skip link's
// "#main" used to reset the page to the default product (2026-10-12).
const here = (id) => id === "resultado" || id === "main";
out.anchors = ["#resultado", "#main", "#b=pico_w_air&on=mdns", "#nada", "", "#b=pico_w_air"]
  .map((h) => L.isAnchorHash(h, here));
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

# 5. As travas da página são as regras do Python -- e, onde nenhuma regra trava, a recusa
#    do gerador: uma chave que pede uma placa que o chip do produto não tem (a rede por
#    cabo no RP2350) trava como uma regra, com o motivo à vista. A regra vem primeiro, e o
#    orçamento de PIO e de pinos (gen_features.blockers, 2026-10-13) por último.
for lk in js["locks"]:
    p, t = lk["p"], lk["t"]
    prof = gf.resolve_profile(p, M["profiles"])
    cfg = gf.config_of(prof)
    now = {r["id"] for r in gf.rule_violations(cfg, M)}
    flipped = dict(cfg, **{t: not cfg[t]})
    new = [r["id"] for r in gf.rule_violations(flipped, M) if r["id"] not in now]
    fprof = dict(prof, **{t: not bool(prof.get(t, False))})
    refused = gf.toggle_refusal(fprof, M)
    budget_now = {b["id"] for b in gf.blockers(prof, M) if b["kind"] == "budget"}
    budget = [b["id"] for b in gf.blockers(fprof, M) if b["kind"] == "budget" and b["id"] not in budget_now]
    want = new[0] if new else (f"unsupported-{t}" if refused else (budget[0] if budget else None))
    check(lk["lock"] == want, f"{p}/{t}: trava da página {lk['lock']} != regras {new} / recusa do gerador "
                              f"{refused!r} / orçamento {budget}")
    # A trava do orçamento diz o que desligar para ligar ESTA chave: as outras da lista, sem ela.
    if lk["lock"] == want and want in budget:
        r = next(x for x in pb.refusals(gf.config_of(fprof), fprof.get("chip") or "rp2040") if x["id"] == want)
        esperado = gf.budget_why(r, M, without=t)
        check(lk["why"] == esperado, f"{p}/{t}: o texto da trava {lk['why']} != o do gerador sem a chave {esperado}")
        for lang in gf.LANGS:
            check(M["ui_toggles"][t]["label"][lang] not in (lk["why"] or {}).get(lang, ""),
                  f"{p}/{t}: a trava manda desligar a própria chave ({lang}): {lk['why']}")

# 5b. A rede por cabo no Pico 2 W: o gerador a recusa (o RP2350 não tem placa para ela), e a
#     página trava a chave com o motivo do manifesto, em inglês e em português. Nos produtos
#     do Pico W ela não é travada por isso -- só pelas regras dela, onde valem.
lan_texts = {lang: M["ui_toggles"]["lan"]["unsupported"][lang] for lang in ("en", "pt")}
locks = {(lk["p"], lk["t"]): lk for lk in js["locks"]}
two = locks[("pico2_w_release", "lan")]
check(two["lock"] == "unsupported-lan", f"pico2_w_release/lan: a página não trava a chave que o gerador recusa: {two}")
check(two["why"] == lan_texts, f"pico2_w_release/lan: o motivo da trava {two['why']} != o do manifesto {lan_texts}")
check(all(lan_texts.values()), "o manifesto não dá os dois textos da recusa da rede por cabo")
for p in PRODUCTS:
    if p != "pico2_w_release":
        check(locks[(p, "lan")]["lock"] != "unsupported-lan", f"{p}/lan: travada pela recusa do gerador, que é do RP2350")
# Sem regra que a proíba, a rede por cabo no SIMUT trava pelo orçamento de PIO: com o DS18B20 e o
# DHT22 ligados, o ENC28J60 não cabe nos dois blocos do RP2040 (tools/pio_budget.py).
check(locks[("pico_w_release", "lan")]["lock"] == "pio-budget",
      f"pico_w_release/lan: devia travar pelo orçamento de PIO, trava por {locks[('pico_w_release', 'lan')]['lock']}")
check(locks[("pico_w_alpha", "lan")]["lock"] == "lan-no-bluetooth", "pico_w_alpha/lan: devia travar pela regra lan-no-bluetooth")
check(locks[("pico_w_air", "lan")]["lock"] == "lan-no-bluetooth", "pico_w_air/lan: a primeira regra que ela quebra é lan-no-bluetooth")

# 5b2. O texto do orçamento: o problema (why) e o remédio (fix, com a lista; fix_one, com um item
#      só). A trava não manda desligar a chave que ela trava, e uma lista que fica vazia não deixa
#      "Desligue um destes: ." no fim (13/10: a trava do DS18B20 no SIMUT Wire mandava desligar o
#      próprio DS18B20, que estava desligado).
ub = M.get("ui_budget", {})
for rid in pb.REFUSAL_IDS:
    for campo in ("why", "fix", "fix_one"):
        for lang in gf.LANGS:
            check(str(ub.get(rid, {}).get(campo, {}).get(lang, "")).strip(),
                  f"[ui_budget.{rid}] sem {campo}.{lang}")
if all(isinstance(ub.get(rid, {}).get(c), dict) for rid in pb.REFUSAL_IDS for c in ("why", "fix", "fix_one")):
    def rot(k, lang):
        return M["ui_toggles"][k]["label"][lang]
    tres = {"id": "pio-budget", "fix": ["lan", "sensor_ds18b20", "sensor_dht22"]}
    for lang in gf.LANGS:
        w = ub["pio-budget"]
        casos = {
            "a lista inteira": (gf.budget_why(tres, M)[lang],
                                w["why"][lang] + " " + w["fix"][lang].replace(
                                    "{list}", "; ".join(rot(k, lang) for k in tres["fix"]))),
            "a trava do DS18B20": (gf.budget_why(tres, M, without="sensor_ds18b20")[lang],
                                   w["why"][lang] + " " + w["fix"][lang].replace(
                                       "{list}", rot("lan", lang) + "; " + rot("sensor_dht22", lang))),
            "sobra um": (gf.budget_why({"id": "pio-budget", "fix": ["lan", "sensor_dht22"]}, M, without="lan")[lang],
                         w["why"][lang] + " " + w["fix_one"][lang].replace("{list}", rot("sensor_dht22", lang))),
            "não sobra nenhum": (gf.budget_why({"id": "pin-conflict", "fix": ["sound_buzzer"], "pin": 16}, M,
                                               without="sound_buzzer")[lang],
                                 ub["pin-conflict"]["why"][lang].replace("{pin}", "16")),
            "um só, sem trava": (gf.budget_why({"id": "pin-conflict", "fix": ["sound_buzzer"], "pin": 16}, M)[lang],
                                 ub["pin-conflict"]["why"][lang].replace("{pin}", "16") + " "
                                 + ub["pin-conflict"]["fix_one"][lang].replace("{list}", rot("sound_buzzer", lang))),
        }
        # O conflito diz qual pino: sem o número, "dois recursos querem o mesmo pino" não deixa
        # ninguém achar o fio (sugestão da revisão, 13/10).
        check("{pin}" in ub["pin-conflict"]["why"][lang] and "GP16" in casos["não sobra nenhum"][0],
              f"[ui_budget.pin-conflict] why.{lang} devia dizer o pino ({{pin}}): {ub['pin-conflict']['why'][lang]!r}")
        for nome, (got, esperado) in casos.items():
            check(got == esperado, f"budget_why, {nome} ({lang}): {got!r} != {esperado!r}")
            check("{" not in got and "}" not in got, f"budget_why, {nome} ({lang}): sobrou um marcador em {got!r}")
# O model.json leva, por bloqueio, as chaves do remédio e a trava de cada uma.
for b in MODEL.get("budget", []):
    check(isinstance(b.get("fix"), list) and set(b["fix"]) <= set(pb.BUDGET_KEYS),
          f"model.json: o bloqueio {b['id']} {b.get('when')} sem a lista fix: {b.get('fix')}")
    check(set((b.get("lock") or {})) == set(b.get("fix") or []),
          f"model.json: o bloqueio {b['id']} {b.get('when')} tem trava para {sorted(b.get('lock') or {})}, "
          f"o remédio é {b.get('fix')}")
    for k, txt in (b.get("lock") or {}).items():
        check(txt == gf.budget_why(b, M, without=k), f"model.json: a trava de {k} em {b['id']} {b['when']} != gerador")

# 5b3. A tabela da página (gen_features.budget_table) tem uma entrada por recusa do pio_budget. Com
#      o buzzer em dois pinos, um do módulo (GP12) e outro dos dois mostradores (GP16, da TFT e do
#      LCD), são dois conflitos com a mesma condição, e a página mostrava um só: o `seen` juntava os
#      dois pelo `when`, e desligar o cabo, o único remédio que ela dava, não soltava o GP16 (achado
#      da revisão, 13/10). O model.json de hoje não tem conflito de pino; este caso o fabrica.
guardado = pb.PINS["buzzer"]
diverge, mais = [], 0
try:
    pb.PINS["buzzer"] = (12, 16)
    try:
        tabela = gf.budget_table(M)
    except SystemExit as e:  # o gerador recusa com sys.exit; aqui é uma falha, não o fim do teste
        tabela = None
        check(False, f"gen_features.budget_table( ) saiu com o buzzer em (12, 16): {e}")
    for chip in pb.CHIPS:
        for display in M["display"]:
            for bits in itertools.product((False, True), repeat=len(pb.BUDGET_KEYS)):
                cfg = dict(zip(pb.BUDGET_KEYS, bits), display=display)
                rs = pb.refusals(cfg, chip)
                mais = max(mais, sum(x["id"] == "pin-conflict" for x in rs))
                if tabela is None:
                    continue
                quer = sorted(x["reason"] for x in rs)
                tem = sorted(e["detail"] for e in tabela if e["chip"] == chip
                             and all(cfg.get(k) == v for k, v in e["when"].items()))
                if tem != quer:
                    diverge.append((chip, display, bits, tem, quer))
finally:
    pb.PINS["buzzer"] = guardado
check(mais >= 2, f"o buzzer em (12, 16) devia dar dois conflitos numa combinação, deu no máximo {mais}")
check(not diverge, f"a tabela da página devia ter uma entrada por recusa; {len(diverge)} combinações "
                   f"divergem, a primeira {diverge[:1]}")

# 5c. Um link pode trazer a chave já ligada: a escolha diz quais ligadas o gerador recusa
#     (a página as soma às regras quebradas e não deixa compilar).
for r in js["refusals"]:
    prof = gf.resolve_profile(r["p"], M["profiles"])
    refused = gf.toggle_refusal(dict(prof, **{r["t"]: True}), M)
    check(r["ids"] == ([f"unsupported-{r['t']}"] if refused else []),
          f"{r['p']}/{r['t']}: ligada, a página acusa {r['ids']}, o gerador {refused!r}")

# 5d. O que impede a build é uma lista só: as regras quebradas, as recusas do chip e o orçamento,
#     nessa ordem -- e é a mesma do Python.
refusals_by = {(r["p"], r["t"]): r["ids"] for r in js["refusals"]}
for b in js["blockers"]:
    check(b["ids"] == b["rules"] + refusals_by[(b["p"], b["t"])] + b["budget"],
          f"{b['p']}/{b['t']}: ligada, blockersOf dá {b['ids']}, as regras {b['rules']}, as recusas "
          f"{refusals_by[(b['p'], b['t'])]} e o orçamento {b['budget']}")
    prof = dict(gf.resolve_profile(b["p"], M["profiles"]), **{b["t"]: True})
    want = [x["id"] for x in gf.blockers(prof, M)]
    check(b["ids"] == want, f"{b['p']}/{b['t']}: ligada, blockersOf dá {b['ids']}, gen_features.blockers {want}")
    # O veredito do selo: o que impede a build vem antes do slot. Uma build bloqueada que caberia
    # no slot continua bloqueada (o selo dizia "Cabe" acima de "não cabe nos blocos PIO", 13/10).
    check(b["verdict"] == ("blocked" if want else b["fit"]),
          f"{b['p']}/{b['t']}: ligada, o veredito é {b['verdict']!r}; com os bloqueios {want} e o slot "
          f"{b['fit']!r} devia ser {'blocked' if want else b['fit']!r}")
check(any("unsupported-lan" in b["ids"] for b in js["blockers"] if (b["p"], b["t"]) == ("pico2_w_release", "lan")),
      "pico2_w_release/lan: ligada por um link, blockersOf não a acusa e o botão de compilar ficaria livre")

# 5e. O app.js, que nenhum teste executa, usa essas contas. Os avisos e o botão de compilar vêm de
#     L.blockersOf, e não de ruleViolations sozinho; toda trava passa o produto, sem o qual lockOf
#     devolve só as regras e a recusa do chip some sem erro nenhum.
with open(os.path.join(PAGE, "app.js"), encoding="utf-8") as fh:
    APP = fh.read()
check(APP.count("const violations = L.blockersOf(m, state.productId, config);") == 1,
      "app.js: os avisos e o botão de compilar deveriam vir de L.blockersOf(m, state.productId, config)")
check("ruleViolations(" not in APP,
      "app.js: chama ruleViolations por conta própria, e o que ele monta pode deixar a recusa do chip de fora")
selo = re.search(r"\nfunction fitSelo\(\) \{\n(.*?)\n\}\n", APP, re.S)
check(selo and "L.verdictOf(state.model, state.productId, state.set)" in selo.group(1)
      and "slotFit" not in selo.group(1),
      "app.js: o selo do veredito (fitSelo) deveria vir de L.verdictOf, que põe o bloqueio antes do slot, "
      "e não do L.slotFit sozinho")
check(APP.count('"fit.blocked":') == 2,
      f"app.js: o texto fit.blocked deveria existir em en e em pt, existe {APP.count(chr(34) + 'fit.blocked' + chr(34) + ':')} vez(es)")
lock_calls = re.findall(r"L\.lockOf\(([^()]*)\)", APP)
check(lock_calls and all(len([a for a in c.split(",") if a.strip()]) == 4 for c in lock_calls),
      f"app.js: toda chamada de L.lockOf deveria passar o produto (4 argumentos): {lock_calls}")

# 6. O erro que a página diz ter.
import check_flash_budget as cfb  # noqa: E402
checks = MODEL["costs"].get("checks", []) if MODEL.get("costs") else []
miss_used = miss_bin = n = 0
for c in checks:
    if "used" not in c:
        continue
    b = costs[c["base"]]["base"]
    flips = costs[c["base"]]["flips"]
    if any("used" not in flips[k] and "overflow" not in flips[k] for k in c["set"]):
        continue
    # A flip the linker refused counts too (2026-10-12): its .bin is the region
    # plus the overflow, the extent the linker measured, and its `used` moves by
    # the same amount. The Air grew past the room for HTTPS or mDNS alone, and a
    # check that frees Bluetooth's 140 KB and turns both on is what it exists for.
    region = cfb.ceilings(c["base"])["flash_region"]

    def delta(f, key):
        if "used" in f:
            return f[key] - b[key]
        return region + f["overflow"] - b["bin"]
    est_used = b["used"] + sum(delta(flips[k], "used") for k in c["set"])
    est_bin = b["bin"] + sum(delta(flips[k], "bin") for k in c["set"])
    miss_used = max(miss_used, abs(c["used"] - est_used))
    miss_bin = max(miss_bin, abs(c["bin"] - est_bin))
    n += 1
check(js["accuracy"] == {"n": n, "used": miss_used, "bin": miss_bin},
      f"precisão da página {js['accuracy']} != recalculada {(n, miss_used, miss_bin)}")
check(js["margin"] >= 4096 and js["margin"] >= max(miss_used, miss_bin),
      f"margem {js['margin']} menor que o erro medido ({miss_used}, {miss_bin})")

# 7. Os tetos são de cada produto (2026-10-05): o Pico 2 W é segurado ao slot de
#    1.568.768 B, nos dois números, e o Pico W ao que o portão de flash usa.
#    Os mesmos que tools/build_custom.py põe no build.json.
import check_flash_budget as cfb  # noqa: E402
check("ceilings" not in MODEL, "o model.json ainda tem um teto só para todos os produtos")
for p in MODEL["products"]:
    want = cfb.ceilings(p["id"])
    check(p.get("ceilings") == want, f"{p['id']}: tetos {p.get('ceilings')} != os do portão {want}")
    check(js["ceilings"].get(p["id"]) == want, f"{p['id']}: a página usa os tetos {js['ceilings'].get(p['id'])}")
    chip = gf.resolve_profile(p["id"], M["profiles"]).get("chip") or "rp2040"
    check(p.get("chip") == chip, f"{p['id']}: chip {p.get('chip')} != o do manifesto {chip}")
    if p["id"] in costs:
        got = js["overflow"].get(p["id"]) or {}
        base = costs[p["id"]]["base"]
        check(got.get("bin") == want["flash_region"] + 100,
              f"{p['id']}: um estouro de 100 B virou um .bin de {got.get('bin')}, "
              f"a região dele tem {want['flash_region']} B")
        check(got.get("used") == want["flash_region"] + 100 - (base["bin"] - base["used"]),
              f"{p['id']}: o used de um estouro de 100 B não anda com o .bin: {got}")
        slot = js["slot"].get(p["id"]) or {}
        check(slot.get("usedUnderBinOver") == "over",
              f"{p['id']}: used abaixo da região e .bin acima dela deu {slot.get('usedUnderBinOver')}, não 'over'")
        check(slot.get("bothUnder") == "ok", f"{p['id']}: uma imagem bem abaixo da região deu {slot.get('bothUnder')}")
# 7b. Uma âncora da própria página não é uma build (2026-10-12): seguir o
#     "Ver o resultado" ou o "Pular para o conteúdo" voltava ao produto padrão.
check(js["anchors"] == [True, True, False, False, False, False],
      f"âncoras da página lidas como build: {js['anchors']}")
# 8. Todo produto publicado tem custo medido: a página não diz "sem estimativa"
#    para uma imagem que a release publica.
for p in MODEL["products"]:
    if p["publish"]:
        check(p["id"] in costs, f"{p['id']} é publicado e não tem custo medido")

# 9. O model.json diz, por produto, as chaves cuja inversão o gerador recusa, com o motivo
#    em inglês e em português: a página trava por ele, e quem o escreve é o gerador.
for p in MODEL["products"]:
    prof = gf.resolve_profile(p["id"], M["profiles"])
    want = {}
    for t in gf.TOGGLE_ORDER:
        if gf.toggle_refusal(dict(prof, **{t: not bool(prof.get(t, False))}), M):
            want[t] = {lang: M["ui_toggles"][t]["unsupported"][lang] for lang in ("en", "pt")}
    check(p.get("refused") == want, f"{p['id']}: refused {p.get('refused')!r} != o que o gerador recusa {want!r}")
by_id = {p["id"]: p for p in MODEL["products"]}
check(list(by_id["pico2_w_release"].get("refused", {})) == ["lan"],
      f"pico2_w_release deveria recusar só a rede por cabo: {by_id['pico2_w_release'].get('refused')!r}")
check(all(p.get("refused") == {} for p in MODEL["products"] if p["chip"] == "rp2040"),
      "um produto do Pico W recusa uma chave que o gerador aceita nele")

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
