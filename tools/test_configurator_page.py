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
    proibiria, e vice-versa -- com o motivo que o manifesto dá. A rede, desde
    2026-10-13, não é uma chave e sim uma escolha de driver (`net`): cada opção
    (produto x driver) está travada se, e só se, gen_features.blockers( ) tem um
    bloqueio novo para o perfil com o driver trocado -- uma regra, o driver que o
    chip recusa (gen_features.net_refusal: o cabo no RP2350) ou o orçamento de PIO --
    com as mesmas palavras, e o texto nunca cita a própria opção;
  - o link, o perfil e a estimativa levam a rede ("net=<id>", "flips" de "net=<id>"),
    e o perfil tem a grafia canônica do Python (json.dumps com sort_keys);
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
NETS = [o["id"] for o in MODEL["net"]["options"]]

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
# A rede no link (2026-10-13): só o id de um driver do modelo, uma vez. O que o link traz de errado
# é recusado COM o motivo certo (`error`, `value`), porque a página diz ao leitor o que ignorou.
HOSTILE_NET = {
    "#b=pico_w_air&net=w5500": ("net", "w5500"),                       # um driver que o modelo não tem
    "#b=pico_w_air&net=CYW43": ("net", "CYW43"),                       # os ids são minúsculos
    "#b=pico_w_air&net=": ("net", ""),                                 # presente e vazio
    "#b=pico_w_air&net=__proto__": ("net", "__proto__"),
    "#b=pico_w_air&net=constructor": ("net", "constructor"),
    "#b=pico_w_air&net=cyw43%0A": ("net", "cyw43\n"),
    "#b=pico_w_air&net=" + "a" * 41: ("net", "a" * 41),                # além do teto de 40 do id
    "#b=pico_w_air&net=cyw43&net=cyw43": ("twice", "net"),             # duas vezes, igual
    "#b=pico_w_air&net=cyw43&net=enc28j60": ("twice", "net"),          # duas vezes, diferente
    "#b=pico_w_air&on=net": ("toggle", "net"),                         # a rede não é um interruptor
    "#b=pico_w_air&off=net": ("toggle", "net"),
}
HOSTILE_HASHES += list(HOSTILE_NET)

# O script que o node roda: recebe os casos por stdin, devolve as respostas.
JS = r"""
import * as L from "./logic.js";
import { ruleViolations } from "./rules.js";
const input = JSON.parse(await new Promise((ok) => {
  let s = ""; process.stdin.on("data", (d) => (s += d)); process.stdin.on("end", () => ok(s));
}));
const model = input.model;
const out = { profiles: [], hashes: [], hostile: [], singles: [], locks: [], refusals: [], blockers: [], nets: [],
              netCost: {}, netLinks: null, accuracy: null,
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
// The network is a choice of driver (2026-10-13), not a switch. For each product, in its own configuration
// and with one more change on top (a switch turned over, or the other driver already chosen, so a blocker
// the choice already has is in play), each driver as the next choice: its lock, what stops the build with
// it, the chip's refusal, the verdict. The lock is asked of the CURRENT choice, as the page asks it.
const lockNet = typeof L.netLockOf === "function" ? L.netLockOf : () => "sem netLockOf";
const priceNet = typeof L.priceNet === "function" ? L.priceNet : () => "sem priceNet";
for (const p of input.products) {
  for (const ctx of input.contexts[p]) {
    const now = L.effectiveConfig(model, p, ctx);
    for (const n of input.nets) {
      const next = L.normalizeSet(model, p, { ...ctx, net: n });
      const cfg = L.effectiveConfig(model, p, next);
      const lock = lockNet(model, now, n, p);
      const est = L.estimate(model, p, next);
      out.nets.push({ p, ctx, n,
                      lock: lock === "sem netLockOf" ? lock : lock ? lock.id : null,
                      why: lock && lock !== "sem netLockOf" ? lock.why : null,
                      ids: L.blockersOf(model, p, cfg).map((r) => r.id),
                      refusal: L.refusalsIn(model, p, cfg).map((r) => r.id),
                      verdict: L.verdictOf(model, p, next),
                      fit: est ? L.slotFit(model, p, est) : null });
    }
  }
}
// What a driver costs: the matrix measures "net=<id>" flips (tools/measure_savings.py), which the file
// of today does not have yet, so a made-up flip stands in for each kind the matrix can write, the way the
// overflow below does, and the model as it is says "no measurement".
for (const p of input.products) {
  const costs = model.costs && model.costs.products && model.costs.products[p];
  if (!costs) continue;
  const own = L.productById(model, p).config.net;
  const other = input.nets.find((n) => n !== own);
  const b = costs.base;
  const variants = {
    measured: { used: b.used + 1234, bin: b.bin + 1200, ram: b.ram - 56 },
    smaller: { used: b.used - 4096, bin: b.bin - 4096, ram: b.ram - 8 },
    same: { same: true },
    rule: { rule: "bluetooth-needs-cyw43" },
    error: { error: "the build failed" },
    overflow: { overflow: 100 },
  };
  const cost = { other, own: priceNet(model, p, own),
                 real: { price: priceNet(model, p, other), est: L.estimate(model, p, { net: other }) }, variants: {} };
  for (const [name, flip] of Object.entries(variants)) {
    const fake = JSON.parse(JSON.stringify(model));
    fake.costs.products[p].flips[`net=${other}`] = flip;
    cost.variants[name] = { price: priceNet(fake, p, other), est: L.estimate(fake, p, { net: other }) };
  }
  out.netCost[p] = cost;
}
// The link, in the words the reader sees: "net=" after the product, before the switches.
out.netLinks = {
  one: L.formatHash("pico_w_release", { net: "enc28j60" }),
  mixed: L.formatHash("pico_w_release", { sensor_ds18b20: false, mdns: true, net: "enc28j60" }),
  mixedBack: L.parseHash(model, "#b=pico_w_release&on=mdns&off=sensor_ds18b20&net=enc28j60"),
  sameAsProduct: L.parseHash(model, "#b=pico_wire&net=enc28j60"),
  wifiBack: L.parseHash(model, "#b=pico_wire&net=cyw43"),
  normalized: L.normalizeSet(model, "pico_wire", { net: "enc28j60", mdns: true }),
};
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

# A rede (2026-10-13): cada driver sozinho e com cada chave nos dois sentidos. A chave `net` entra ANTES, de
# propósito: a grafia canônica ordena as chaves, e `air`, `bluetooth`, `mdns`... vêm antes de `net`.
for p in PRODUCTS:
    for n in NETS:
        sets.append((p, {"net": n}))
        for t in TOGGLES:
            sets.append((p, {"net": n, t: True}))
            sets.append((p, {"net": n, t: False}))

# Os estados em que se pergunta pela trava de uma opção de rede: o produto como é, cada chave virada, e o
# outro driver já escolhido.
CONTEXTS = {}
for p in PRODUCTS:
    base = gf.config_of(gf.resolve_profile(p, M["profiles"]))
    CONTEXTS[p] = ([{}] + [{t: not base[t]} for t in TOGGLES] + [{"net": n} for n in NETS if n != base["net"]])

js = run_js({"model": MODEL, "sets": sets, "hostile": HOSTILE_HASHES,
             "products": PRODUCTS, "toggles": TOGGLES, "nets": NETS, "contexts": CONTEXTS})

# 1. O perfil da página é o do build_custom.py, com a mesma grafia. A grafia canônica é a do Python:
#    json.dumps com as chaves ordenadas em todo nível e sem espaços -- conferida contra o json.dumps daqui
#    (a fonte) e contra bc.canonical( ) (o que o build usa para dar nome à build), porque `net` entra no
#    meio das chaves e a página tem de ordená-las.
for (p, s), text in zip(sets, js["profiles"]):
    spelled = json.dumps({"v": 1, "base": p, "set": s}, sort_keys=True, separators=(",", ":"))
    check(text == spelled, f"grafia da página {text} != json.dumps(sort_keys) {spelled}")
    check(text == bc.canonical(p, s), f"grafia da página {text} != canônica {bc.canonical(p, s)}")
    try:
        base, changes = bc.parse_spec(text, M)
    except bc.SpecError as e:
        failures.append(f"perfil da página recusado pelo build_custom: {text} ({e})")
        continue
    check((base, changes) == (p, s), f"perfil {text} lido como {(base, changes)}")

# 2. O link vai e volta.
for (p, s), got in zip(sets, js["hashes"]):
    base = gf.config_of(gf.resolve_profile(p, M["profiles"]))
    want = {k: v for k, v in s.items() if v != base[k]}
    check(got.get("productId") == p and got.get("set") == want,
          f"link de {p} {s} voltou como {got}")

# 3. Links hostis: recusados, nunca aceitos pela metade -- e os da rede, com o motivo certo.
for h, got in zip(HOSTILE_HASHES, js["hostile"]):
    check("error" in got, f"link hostil aceito: {h[:60]!r} -> {got}")
    if h in HOSTILE_NET:
        check((got.get("error"), got.get("value")) == HOSTILE_NET[h],
              f"link {h[:60]!r}: a página recusa com {(got.get('error'), got.get('value'))}, devia ser {HOSTILE_NET[h]}")

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

# 5. As travas da página são as do Python: uma chave está travada se, e só se, virá-la traz um
#    bloqueio que a escolha de agora não tem -- o primeiro, na ordem de gen_features.blockers( ) (a regra,
#    o driver que o chip recusa, o orçamento de PIO e de pinos; 2026-10-13) --, com as palavras dele.
for lk in js["locks"]:
    p, t = lk["p"], lk["t"]
    prof = gf.resolve_profile(p, M["profiles"])
    fprof = dict(prof, **{t: not bool(prof.get(t, False))})
    now = {b["id"] for b in gf.blockers(prof, M)}
    new = [b for b in gf.blockers(fprof, M) if b["id"] not in now]
    want = new[0]["id"] if new else None
    check(lk["lock"] == want, f"{p}/{t}: trava da página {lk['lock']} != bloqueios novos {[b['id'] for b in new]}")
    if want and lk["lock"] == want:
        if new[0]["kind"] == "budget":
            # A trava do orçamento diz o que desligar para ligar ESTA chave: as outras da lista, sem ela.
            r = next(x for x in pb.refusals(gf.config_of(fprof), fprof.get("chip") or "rp2040") if x["id"] == want)
            esperado = gf.budget_why(r, M, without=t)
            check(lk["why"] == esperado, f"{p}/{t}: o texto da trava {lk['why']} != o do gerador sem a chave {esperado}")
            for lang in gf.LANGS:
                check(M["ui_toggles"][t]["label"][lang] not in (lk["why"] or {}).get(lang, ""),
                      f"{p}/{t}: a trava manda desligar a própria chave ({lang}): {lk['why']}")
        else:
            check(lk["why"] == new[0]["why"], f"{p}/{t}: o texto da trava {lk['why']} != o do gerador {new[0]['why']}")

# 5b. A rede é uma escolha de driver (2026-10-13), e cada opção, em cada estado, está travada se, e só
#     se, o Python tem um bloqueio NOVO para o perfil com o driver trocado -- o primeiro, com as mesmas
#     palavras --; o veredito é "blocked" se, e só se, o Python tem algum bloqueio; a recusa do chip
#     (unsupported-net) é a que o gerador faz; e o texto de uma trava nunca cita a própria opção.
cable_texts = {lang: M["ui_nets"]["enc28j60"]["unsupported"][lang] for lang in ("en", "pt")}
locked_kinds = set()
for r in js["nets"]:
    p, n, ctx = r["p"], r["n"], r["ctx"]
    tag = f"{p}/{ctx or 'como publicado'}/net={n}"
    prof = dict(gf.resolve_profile(p, M["profiles"]), **ctx)
    nxt = dict(prof, net=n)
    now = {b["id"] for b in gf.blockers(prof, M)}
    after = gf.blockers(nxt, M)
    new = [b for b in after if b["id"] not in now]
    check(r["lock"] == (new[0]["id"] if new else None),
          f"{tag}: trava da página {r['lock']} != bloqueios novos {[b['id'] for b in new]}")
    if new and r["lock"] == new[0]["id"]:
        locked_kinds.add(new[0]["kind"])
        check(r["why"] == new[0]["why"], f"{tag}: o texto da trava {r['why']} != o do gerador {new[0]['why']}")
        for lang in gf.LANGS:
            check(M["ui_nets"][n]["label"][lang] not in (r["why"] or {}).get(lang, ""),
                  f"{tag}: a trava cita a própria opção ({lang}): {r['why']}")
    check(r["ids"] == [b["id"] for b in after], f"{tag}: blockersOf dá {r['ids']}, gen_features.blockers {[b['id'] for b in after]}")
    check(r["refusal"] == (["unsupported-net"] if gf.net_refusal(nxt, M) else []),
          f"{tag}: a página acusa a recusa do chip {r['refusal']}, o gerador {gf.net_refusal(nxt, M)!r}")
    check(r["verdict"] == ("blocked" if after else r["fit"]),
          f"{tag}: o veredito é {r['verdict']!r}; com os bloqueios {[b['id'] for b in after]} e o slot {r['fit']!r} "
          f"devia ser {'blocked' if after else r['fit']!r}")
# Sem uma opção travada de cada tipo, a comparação acima não prova nada: uma trava que nunca trava passaria.
check(locked_kinds == {"rule", "unsupported", "budget"},
      f"as opções de rede travadas são dos tipos {sorted(locked_kinds)}: o teste não está mordendo")

# As travas que o desenho da página promete, em palavras: no estado de cada produto como é publicado.
net_locks = {(r["p"], r["n"]): r for r in js["nets"] if not r["ctx"]}
two = net_locks[("pico2_w_release", "enc28j60")]
check(two["lock"] == "unsupported-net", f"pico2_w_release/net=enc28j60: a página não trava o driver que o gerador recusa: {two}")
check(two["why"] == cable_texts, f"pico2_w_release/net=enc28j60: o motivo da trava {two['why']} != o do manifesto {cable_texts}")
check(all(cable_texts.values()), "o manifesto não dá os dois textos da recusa do cabo")
for p in PRODUCTS:
    if p != "pico2_w_release":
        check(net_locks[(p, "enc28j60")]["lock"] != "unsupported-net", f"{p}/net=enc28j60: travada pela recusa do gerador, que é do RP2350")
    own = gf.config_of(gf.resolve_profile(p, M["profiles"]))["net"]
    check(net_locks[(p, own)]["lock"] is None, f"{p}/net={own}: o driver do próprio produto está travado: {net_locks[(p, own)]}")
# Sem regra que a proíba, o cabo no SIMUT trava pelo orçamento de PIO: com o DS18B20 e o DHT22 ligados, o
# ENC28J60 não cabe nos dois blocos do RP2040 (tools/pio_budget.py); o texto é o inteiro, porque nenhuma
# chave do remédio é a rede.
check(net_locks[("pico_w_release", "enc28j60")]["lock"] == "pio-budget",
      f"pico_w_release/net=enc28j60: devia travar pelo orçamento de PIO, trava por {net_locks[('pico_w_release', 'enc28j60')]['lock']}")
check(net_locks[("pico_w_alpha", "enc28j60")]["lock"] == "bluetooth-needs-cyw43",
      "pico_w_alpha/net=enc28j60: devia travar pela regra bluetooth-needs-cyw43")
check(net_locks[("pico_w_air", "enc28j60")]["lock"] == "bluetooth-needs-cyw43",
      "pico_w_air/net=enc28j60: a primeira regra que ele quebra é bluetooth-needs-cyw43")
# O Wi-Fi de volta no SIMUT Wire: nada o trava, e o produto de cabo continua com a opção do cabo livre.
check(net_locks[("pico_wire", "cyw43")]["lock"] is None, f"pico_wire/net=cyw43: o Wi-Fi de volta está travado: {net_locks[('pico_wire', 'cyw43')]}")

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
    tres = {"id": "pio-budget", "fix": ["sound_buzzer", "sensor_ds18b20", "sensor_dht22"]}
    for lang in gf.LANGS:
        w = ub["pio-budget"]
        casos = {
            "a lista inteira": (gf.budget_why(tres, M)[lang],
                                w["why"][lang] + " " + w["fix"][lang].replace(
                                    "{list}", "; ".join(rot(k, lang) for k in tres["fix"]))),
            "a trava do DS18B20": (gf.budget_why(tres, M, without="sensor_ds18b20")[lang],
                                   w["why"][lang] + " " + w["fix"][lang].replace(
                                       "{list}", rot("sound_buzzer", lang) + "; " + rot("sensor_dht22", lang))),
            "sobra um": (gf.budget_why({"id": "pio-budget", "fix": ["sound_buzzer", "sensor_dht22"]}, M,
                                       without="sound_buzzer")[lang],
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
#      da revisão, 13/10). O model.json de hoje não tem conflito de pino; este caso o fabrica. O
#      driver da rede (2026-10-13) é uma dimensão a mais: o GP12 é do módulo, então só o cabo o tem.
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
            for net in M["net"]:
                for bits in itertools.product((False, True), repeat=len(pb.BUDGET_KEYS)):
                    cfg = dict(zip(pb.BUDGET_KEYS, bits), display=display, net=net)
                    rs = pb.refusals(cfg, chip)
                    mais = max(mais, sum(x["id"] == "pin-conflict" for x in rs))
                    if tabela is None:
                        continue
                    quer = sorted(x["reason"] for x in rs)
                    tem = sorted(e["detail"] for e in tabela if e["chip"] == chip
                                 and all(cfg.get(k) == v for k, v in e["when"].items()))
                    if tem != quer:
                        diverge.append((chip, display, net, bits, tem, quer))
finally:
    pb.PINS["buzzer"] = guardado
check(mais >= 2, f"o buzzer em (12, 16) devia dar dois conflitos numa combinação, deu no máximo {mais}")
check(not diverge, f"a tabela da página devia ter uma entrada por recusa; {len(diverge)} combinações "
                   f"divergem, a primeira {diverge[:1]}")

# 5c. Um link pode trazer uma chave já ligada, ou o driver já escolhido: a escolha diz o que o gerador
#     recusa no chip do produto (a página soma isso às regras quebradas e não deixa compilar). Nenhuma
#     chave é recusada hoje -- o que o chip recusa é um driver de rede (net_refusal) --, e o produto
#     publicado nunca recusa o próprio driver.
for r in js["refusals"]:
    prof = gf.resolve_profile(r["p"], M["profiles"])
    refused = gf.net_refusal(dict(prof, **{r["t"]: True}), M)
    check(r["ids"] == (["unsupported-net"] if refused else []),
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
check(any("unsupported-net" in r["ids"] and "unsupported-net" in r["refusal"] and r["verdict"] == "blocked"
          for r in js["nets"] if (r["p"], r["n"], r["ctx"]) == ("pico2_w_release", "enc28j60", {})),
      "pico2_w_release/net=enc28j60: escolhido por um link, blockersOf não o acusa e o botão de compilar ficaria livre")

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

# 5e2. A escolha da rede (2026-10-13): o app.js desenha, no grupo que o model.json nomeia, antes das chaves,
#      um fieldset com o título do modelo e um rádio por driver, e trava a opção pelo mesmo L.netLockOf( )
#      que este teste conferiu acima -- com o produto, sem o qual a recusa do chip some sem erro nenhum.
net_calls = re.findall(r"L\.netLockOf\(([^()]*)\)", APP)
check(len(net_calls) == 1 and len([a for a in net_calls[0].split(",") if a.strip()]) == 4,
      f"app.js: a escolha da rede deveria chamar L.netLockOf uma vez, com o produto (4 argumentos): {net_calls}")
feat = re.search(r"\nfunction renderFeatures\(\) \{\n(.*?)\n\}\n", APP, re.S)
check(feat and "m.net.group" in feat.group(1) and "renderNet(config)" in feat.group(1),
      "app.js: renderFeatures deveria desenhar renderNet(config) no grupo de m.net.group")
rnet = re.search(r"\nfunction renderNet\(config\) \{\n(.*?)\n\}\n", APP, re.S)
check(rnet is not None, "app.js: não há renderNet(config), a escolha da rede")
body = rnet.group(1) if rnet else ""
for needle, what in (('h("fieldset"', "um fieldset"), ("label(m.net.label)", "o título do modelo (m.net.label)"),
                     ("m.net.options", "uma opção por driver do modelo (m.net.options)"),
                     ('type: "radio"', "um rádio nativo por opção"), ('name: "rede"', "o mesmo name nos rádios"),
                     ("L.netLockOf(m, config, o.id, state.productId)", "a trava de L.netLockOf com o produto"),
                     ("L.priceNet(m, state.productId, o.id)", "o preço medido de L.priceNet"),
                     ('t("locked", { why: label(lock.why) })', "o texto da trava, como o de uma chave"),
                     ("disabled: Boolean(lock)", "o rádio desabilitado quando travado"),
                     ("checked: config.net === o.id", "marcado o driver da escolha de agora"),
                     ("chooseNet(o.id)", "chooseNet(id) ao mudar")):
    check(needle in body, f"app.js: renderNet deveria ter {what}: {needle}")
choose = re.search(r"\nfunction chooseNet\(netId\) \{\n(.*?)\n\}\n", APP, re.S)
check(choose and "L.normalizeSet(state.model, state.productId, next)" in choose.group(1)
      and "writeHash()" in choose.group(1) and "render()" in choose.group(1) and "net: netId" in choose.group(1),
      "app.js: chooseNet(netId) deveria pôr net no set, normalizar, escrever o link e redesenhar, como flip( )")
# O que acusa "sem medida" e o texto da issue falam de uma chave do `set`; `net` não está em m.toggles, e
# procurá-lo lá derrubaria a página (TypeError) na primeira troca de rede.
check("est.missing.map(missingName)" in APP and re.search(r"\nfunction missingName\(id\) \{\n(.*?)\n\}\n", APP, re.S)
      and '"net"' in re.search(r"\nfunction missingName\(id\) \{\n(.*?)\n\}\n", APP, re.S).group(1),
      "app.js: o aviso 'sem medida' deveria nomear `net` pelo driver escolhido (missingName), e não procurá-lo em m.toggles")
check("m.toggles.find((x) => x.id === id).label" not in APP,
      "app.js: ainda procura toda chave do set em m.toggles, e `net` não está lá")
issue = re.search(r"\nfunction issueUrl\(\) \{\n(.*?)\n\}\n", APP, re.S)
check(issue and "changeLines(" in issue.group(1) and "m.toggles.find" not in issue.group(1),
      "app.js: issueUrl deveria montar as mudanças por changeLines(), que sabe o que é `net`, e não por m.toggles.find")
check(APP.count("err.hash") >= 2 and "readHash" in APP,
      "app.js: o link com uma rede inválida usa o mesmo err.hash do resto")


def str_keys(lang):
    block = re.search(r"\n  %s: \{\n(.*?)\n  \},\n" % lang, APP, re.S)
    return set(re.findall(r'"([\w.]+)":', block.group(1))) if block else set()


check(str_keys("en") and str_keys("en") == str_keys("pt"),
      f"app.js: os textos em en e em pt deveriam ter as mesmas chaves; só em um deles: {sorted(str_keys('en') ^ str_keys('pt'))}")
check({"price.net.more", "price.net.less", "price.net.over"} <= str_keys("en") & str_keys("pt"),
      "app.js: faltam os textos do preço de uma opção de rede (price.net.*) em en ou em pt")

# 6. O erro que a página diz ter.
import check_flash_budget as cfb  # noqa: E402
checks = MODEL["costs"].get("checks", []) if MODEL.get("costs") else []
miss_used = miss_bin = n = 0
for c in checks:
    if "used" not in c:
        continue
    b = costs[c["base"]]["base"]
    flips = costs[c["base"]]["flips"]
    fk = {k: (k if isinstance(v, bool) else f"{k}={v}") for k, v in c["set"].items()}   # a rede: "net=<id>"
    if any(fk[k] not in flips or ("used" not in flips[fk[k]] and "overflow" not in flips[fk[k]]) for k in c["set"]):
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
    est_used = b["used"] + sum(delta(flips[fk[k]], "used") for k in c["set"])
    est_bin = b["bin"] + sum(delta(flips[fk[k]], "bin") for k in c["set"])
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

# 9. O model.json diz, por produto, as escolhas que o gerador recusa ("chave=valor": net=enc28j60), com o
#    motivo em inglês e em português: a página trava por ele, e quem o escreve é o gerador.
for p in MODEL["products"]:
    prof = gf.resolve_profile(p["id"], M["profiles"])
    want = {}
    for net_id in M["net"]:
        if gf.net_refusal(dict(prof, net=net_id), M):
            want[f"net={net_id}"] = {lang: M["ui_nets"][net_id]["unsupported"][lang] for lang in ("en", "pt")}
    check(p.get("refused") == want, f"{p['id']}: refused {p.get('refused')!r} != o que o gerador recusa {want!r}")
by_id = {p["id"]: p for p in MODEL["products"]}
check(list(by_id["pico2_w_release"].get("refused", {})) == ["net=enc28j60"],
      f"pico2_w_release deveria recusar só o cabo: {by_id['pico2_w_release'].get('refused')!r}")
check(all(p.get("refused") == {} for p in MODEL["products"] if p["chip"] == "rp2040"),
      "um produto do Pico W recusa uma escolha que o gerador aceita nele")

# 10. A rede no modelo que a página desenha (2026-10-13): o grupo onde ela fica existe (sem ele o fieldset
#     nunca seria desenhado, calado), há uma opção por driver do manifesto, cada uma com texto em en e pt,
#     e todo produto escolhe um driver que existe.
check(MODEL["net"]["group"] in {g["id"] for g in MODEL["groups"]},
      f"model.json: o grupo da rede, {MODEL['net']['group']!r}, não é um grupo da página")
check(NETS == sorted(M["net"], key=lambda n: M["ui_nets"][n]["order"]), f"as opções de rede {NETS} != as do manifesto")
for o in MODEL["net"]["options"]:
    for campo in ("label", "help"):
        for lang in gf.LANGS:
            check(str(o[campo].get(lang, "")).strip(), f"model.json: a opção de rede {o['id']} sem {campo}.{lang}")
for lang in gf.LANGS:
    check(str(MODEL["net"]["label"].get(lang, "")).strip(), f"model.json: o título da rede sem {lang}")
for p in MODEL["products"]:
    check(p["config"].get("net") in NETS, f"{p['id']}: config.net {p['config'].get('net')!r} não é uma opção de rede")

# 11. O preço de uma opção de rede ("net=<id>" nos flips): a matriz de hoje ainda não mede a rede como
#     escolha, então um flip feito de mentira prova a conta de cada tipo que a matriz escreve, e o
#     model.json como está diz "sem medida" -- até a matriz medir, e então confere a medida.
for p, c in js["netCost"].items():
    base, other = costs[p]["base"], c["other"]
    region = cfb.ceilings(p)["flash_region"]
    v = c["variants"]
    check(c["own"] is None, f"{p}: o preço do driver do próprio produto devia ser nulo, é {c['own']}")
    check(v["measured"]["price"] == {"flash": 1234, "overflow": False},
          f"{p}: um flip medido de +1.234 B deu o preço {v['measured']['price']}")
    check(v["measured"]["est"] == {"used": base["used"] + 1234, "bin": base["bin"] + 1200, "ram": base["ram"] - 56,
                                   "ramKnown": True, "missing": []},
          f"{p}: a estimativa do flip net={other} é {v['measured']['est']}, não a base mais a medida")
    check(v["smaller"]["price"] == {"flash": -4096, "overflow": False},
          f"{p}: um flip que economiza 4.096 B deu o preço {v['smaller']['price']}")
    for kind in ("same", "rule", "error"):
        check(v[kind]["price"] is None and v[kind]["est"]["missing"] == ["net"],
              f"{p}: o flip net={other} '{kind}' devia dar preço nulo e a rede em missing: {v[kind]}")
        check(v[kind]["est"]["bin"] == base["bin"], f"{p}: o flip '{kind}' mexeu na estimativa: {v[kind]['est']}")
    check(v["overflow"]["price"] == {"flash": region + 100 - base["bin"], "overflow": True},
          f"{p}: um estouro de 100 B deu o preço {v['overflow']['price']}")
    check(v["overflow"]["est"]["bin"] == region + 100 and v["overflow"]["est"]["missing"] == [],
          f"{p}: um estouro de 100 B virou a estimativa {v['overflow']['est']}")
    real = costs[p]["flips"].get(f"net={other}")
    if real is None:
        check(c["real"]["price"] is None and c["real"]["est"]["missing"] == ["net"],
              f"{p}: sem medida de net={other}, a página devia dizer 'sem medida': {c['real']}")
    elif "used" in real:
        check(c["real"]["est"]["used"] == real["used"] and c["real"]["est"]["bin"] == real["bin"]
              and c["real"]["price"] == {"flash": real["used"] - base["used"], "overflow": False},
              f"{p}: a medida de net={other} {real} virou {c['real']}")

# 12. O link com a rede (2026-10-13): "net=" logo depois do produto e antes dos interruptores; volta igual;
#     e um driver igual ao do produto não é uma mudança.
nl = js["netLinks"]
check(nl["one"] == "#b=pico_w_release&net=enc28j60", f"formatHash com a rede deu {nl['one']!r}")
check(nl["mixed"] == "#b=pico_w_release&net=enc28j60&on=mdns&off=sensor_ds18b20",
      f"formatHash com a rede e interruptores deu {nl['mixed']!r}")
rel_base = gf.config_of(gf.resolve_profile("pico_w_release", M["profiles"]))
want_mixed = {k: v for k, v in {"mdns": True, "sensor_ds18b20": False, "net": "enc28j60"}.items() if v != rel_base[k]}
check(nl["mixedBack"] == {"productId": "pico_w_release", "set": want_mixed}
      and want_mixed.get("net") == "enc28j60", f"o link com net e interruptores voltou como {nl['mixedBack']}")
check(nl["sameAsProduct"] == {"productId": "pico_wire", "set": {}},
      f"net= igual ao do produto não é uma mudança, mas voltou como {nl['sameAsProduct']}")
check(nl["wifiBack"] == {"productId": "pico_wire", "set": {"net": "cyw43"}}, f"o Wi-Fi de volta no Wire voltou como {nl['wifiBack']}")
wire_base = gf.config_of(gf.resolve_profile("pico_wire", M["profiles"]))
want_norm = {k: v for k, v in {"net": "enc28j60", "mdns": True}.items() if v != wire_base[k]}
check(nl["normalized"] == want_norm and "net" not in want_norm,
      f"normalizeSet deveria tirar net quando é o do produto ({want_norm}): {nl['normalized']}")

if failures:
    print("test_configurator_page: FALHOU")
    for f in failures[:40]:
        print("  -", f)
    if len(failures) > 40:
        print(f"  ... e mais {len(failures) - 40}")
    sys.exit(1)
print(f"test_configurator_page: {len(sets)} perfis e links, {len(HOSTILE_HASHES)} links hostis, "
      f"{len(js['singles'])} chaves medidas, {len(js['locks'])} travas, {len(js['nets'])} opções de rede; "
      f"erro medido {max(miss_used, miss_bin)} B em {n} conferências, margem {js['margin']} B.")
