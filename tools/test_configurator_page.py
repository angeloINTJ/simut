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
  - a UI da tela touch (2026-10-22, C1b) é uma escolha de driver como a rede, e não uma chave: `ui` vai no
    `set`, o link (`&ui=`), o perfil que a página manda compilar e a estimativa a levam, e a UI do próprio
    produto não se escreve em lugar nenhum; cada opção está travada se, e só se, o gerador tem um bloqueio
    novo para o perfil com a UI trocada (o Ângulo no Pico W com Wi-Fi, sem a tela touch, sem o gráfico do
    painel), com o motivo da regra à vista, e com o Ângulo escolhido é a chave do gráfico que fica travada.
    O app.js de verdade roda num DOM de mentira (JS_APP) e o que se confere é o que está na tela: o texto, o
    estado, a ordem, o link e (2026-10-22, C1b-b) o que o leitor de tela recebe -- o nome e a descrição de cada
    rádio (cada id que eles citam existe, uma vez só, e é o do título, da ajuda ou da trava), o grupo de cada
    escolha, a marca "mudou" nas duas línguas --, e a build pronta, que sai da tela quando a escolha muda;

    python3 tools/test_configurator_page.py     # exit 1 em qualquer divergência

@project SIMUT
@license MIT License
"""
import copy
import itertools
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from decimal import Decimal, ROUND_HALF_UP
from urllib.parse import parse_qs

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
UIS = [o["id"] for o in MODEL["ui"]["options"]]

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
# O visual da tela touch no link (2026-10-22): só o id de um [ui.*] do modelo, uma vez, como a rede. O que o
# link traz de errado é recusado COM o motivo certo, porque a página diz ao leitor o que ignorou.
HOSTILE_UI = {
    "#b=pico2_w_release&ui=nonsense": ("ui", "nonsense"),              # uma UI que o modelo não tem
    "#b=pico2_w_release&ui=ANGULO": ("ui", "ANGULO"),                  # os ids são minúsculos
    "#b=pico2_w_release&ui=": ("ui", ""),                              # presente e vazio
    "#b=pico2_w_release&ui=__proto__": ("ui", "__proto__"),
    "#b=pico2_w_release&ui=constructor": ("ui", "constructor"),
    "#b=pico2_w_release&ui=angulo%0A": ("ui", "angulo\n"),
    "#b=pico2_w_release&ui=" + "a" * 41: ("ui", "a" * 41),             # além do teto de 40 do id
    "#b=pico2_w_release&ui=cyw43": ("ui", "cyw43"),                    # um id de rede não é uma UI
    "#b=pico2_w_release&ui=angulo&ui=angulo": ("twice", "ui"),         # duas vezes, igual
    "#b=pico2_w_release&ui=angulo&ui=original": ("twice", "ui"),       # duas vezes, diferente
    "#b=pico2_w_release&on=ui": ("toggle", "ui"),                      # a UI não é um interruptor
    "#b=pico2_w_release&off=ui": ("toggle", "ui"),
    "#b=pico2_w_release&net=ui": ("net", "ui"),                        # nem um id de UI é um driver de rede
}
HOSTILE_HASHES += list(HOSTILE_UI)

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

# O visual da tela touch (2026-10-22): cada UI sozinha, com cada rede e com cada chave nos dois sentidos.
for p in PRODUCTS:
    for u in UIS:
        sets.append((p, {"ui": u}))
        for n in NETS:
            sets.append((p, {"ui": u, "net": n}))
        for t in TOGGLES:
            sets.append((p, {"ui": u, t: True}))
            sets.append((p, {"ui": u, t: False}))

# Os estados em que se pergunta pela trava de uma opção de rede: o produto como é, cada chave virada, e o
# outro driver já escolhido.
CONTEXTS = {}
for p in PRODUCTS:
    base = gf.config_of(gf.resolve_profile(p, M["profiles"]))
    CONTEXTS[p] = ([{}] + [{t: not base[t]} for t in TOGGLES] + [{"net": n} for n in NETS if n != base["net"]]
                  + [{"ui": u} for u in UIS if u != base["ui"]])

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
    if h in HOSTILE_UI:
        check((got.get("error"), got.get("value")) == HOSTILE_UI[h],
              f"link {h[:60]!r}: a página recusa com {(got.get('error'), got.get('value'))}, devia ser {HOSTILE_UI[h]}")

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
cable_texts = {lang: M["ui_nets"]["enc28j60"]["unsupported"][lang] for lang in gf.LANGS}
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
    if gf.resolve_profile(p, M["profiles"]).get("chip", "rp2040") != "rp2350":
        check(net_locks[(p, "enc28j60")]["lock"] != "unsupported-net", f"{p}/net=enc28j60: travada pela recusa do gerador, que é do RP2350")
    else:   # o Pico 2 W e o Angulo nele (2026-10-21): o gerador recusa o cabo, e a página o trava
        check(net_locks[(p, "enc28j60")]["lock"] == "unsupported-net", f"{p}/net=enc28j60: a página não trava o driver que o gerador recusa")
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
check({"price.ui.more", "price.ui.less", "price.ui.over"} <= str_keys("en") & str_keys("pt"),
      "app.js: faltam os textos do preço de uma opção de UI (price.ui.*) em en ou em pt")

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
            want[f"net={net_id}"] = {lang: M["ui_nets"][net_id]["unsupported"][lang] for lang in gf.LANGS}
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


# ---------------------------------------------------------------------------------------------------------
# 13. O visual da tela touch (2026-10-22, C1b). `ui` é uma escolha de driver como a rede, e não uma chave:
#     vai no `set` ao lado dela, o produto traz a sua em `config.ui`, e as regras do Ângulo (no Pico W com
#     Wi-Fi, sem a tela touch, sem o gráfico do painel) a travam com o motivo à vista. A página não decide
#     nada disso: o que se confere aqui é que ela mostra o que o gerador (gen_features.blockers) diz.
# ---------------------------------------------------------------------------------------------------------

# 13a. A UI no modelo que a página desenha: o grupo onde ela fica existe (sem ele a escolha nunca seria
#      desenhada, calado), há uma opção por [ui.*] na ordem de [ui_panels].order, cada uma com o texto do
#      manifesto em en e pt, e todo produto tem uma UI que existe -- a que o manifesto lhe dá.
check(MODEL["ui"]["group"] in {g["id"] for g in MODEL["groups"]},
      f"model.json: o grupo da UI, {MODEL['ui']['group']!r}, não é um grupo da página")
check(MODEL["ui"]["group"] == M["ui_panel"]["group"], "model.json: o grupo da UI não é o de [ui_panel]")
check(UIS == sorted(M["ui"], key=lambda n: M["ui_panels"][n]["order"]), f"as opções de UI {UIS} != as do manifesto")
for lang in gf.LANGS:
    check(MODEL["ui"]["label"].get(lang) == M["ui_panel"]["label"][lang], f"model.json: o título da UI em {lang} != o do manifesto")
    for o in MODEL["ui"]["options"]:
        for campo in ("label", "help"):
            check(o[campo].get(lang) == M["ui_panels"][o["id"]][campo][lang],
                  f"model.json: a opção de UI {o['id']} tem {campo}.{lang} diferente do manifesto")
for p in MODEL["products"]:
    check(p["config"].get("ui") == gf.config_of(gf.resolve_profile(p["id"], M["profiles"]))["ui"] and p["config"]["ui"] in UIS,
          f"{p['id']}: config.ui {p['config'].get('ui')!r} não é a UI que o manifesto dá ao produto")


# 13a'. O que o leitor lê na página não cita um caminho do repositório (C1d, 2026-10-29): "docs/..." não serve a quem usa
#       o configurador, e o lugar da citação é o comentário do manifesto, onde o mantenedor a lê. E a ajuda de uma UI não
#       traz um tamanho em bytes: a linha mostra o que a matriz mediu, com a data da medida na própria página, e um
#       segundo número escrito no texto envelhece sozinho (o "31.008 B" de 21/10 ficaria ao lado do da matriz).
def lang_dicts(node, path=""):
    """Todo texto de duas línguas ou mais do modelo ({en, pt, ...}) com o seu caminho no model.json (fora da matriz)."""
    if isinstance(node, dict):
        if "en" in node and "pt" in node and all(isinstance(v, str) for v in node.values()):
            yield path, node
            return
        for k, v in node.items():
            if k != "costs":
                yield from lang_dicts(v, f"{path}.{k}" if path else k)
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from lang_dicts(v, f"{path}[{i}]")


for where, texts in lang_dicts(MODEL):
    for lang, text in texts.items():
        check("docs/" not in text, f"model.json: o texto {where}.{lang} cita um caminho do repositório: {text!r}")
for o in MODEL["ui"]["options"]:
    for lang, text in o["help"].items():
        check(not re.search(r"\d[\d.,]*\s?(?:KB|B)\b", text),
              f"model.json: a ajuda da UI {o['id']} ({lang}) traz um tamanho em bytes, que a linha mostra pela matriz: {text!r}")


# 13a''. O espanhol no modelo (C1d, 2026-10-29): todo texto de duas línguas do model.json também tem es, e o gerador o põe ali
#        quando o manifesto o tem. A página mostra o inglês onde um texto não o tem (label( ), testado nas visitas em es),
#        mas o que se publica tem todos: um texto novo sem es é um achado, e não um inglês calado.
n_lang_texts = 0
for where, texts in lang_dicts(MODEL):
    n_lang_texts += 1
    check(str(texts.get("es", "")).strip(), f"model.json: o texto {where} não tem es: {texts}")
check(n_lang_texts >= 90, f"model.json: só {n_lang_texts} textos de duas línguas lidos, devia haver uns cem")

# 13b. O preço da UI (2026-10-29, C1d). A matriz de custo guarda o da UI na chave "ui=<id>", como o da rede
#      ("net=<id>"): sem a entrada a linha da UI não mostra preço e a estimativa diz "sem medida"; com ela a linha
#      mostra o que a matriz mediu. Os modelos abaixo são o model.json de verdade com SÓ a matriz trocada; os
#      números são inventados aqui (provam a conta da página, e não são uma medida do firmware).
def matrix_model(flips):
    """O model.json com a matriz de custo trocada: nenhum flip `ui=...` além dos de `flips`, que mapeia
    (produto, chave) a um flip, ou a uma função da base do produto que o devolve."""
    m = copy.deepcopy(MODEL)
    for pc in m["costs"]["products"].values():
        pc["flips"] = {k: v for k, v in pc["flips"].items() if not k.startswith("ui=")}
    for (pid, key), f in flips.items():
        pc = m["costs"]["products"][pid]
        pc["flips"][key] = f(pc["base"]) if callable(f) else f
    return m


MODELS = {
    "real": MODEL,
    "unmeasured": matrix_model({}),
    # 41 KB a mais no Pico 2 W, um estouro do slot no Air, uma regra no Pico W, um erro no Alpha; e um flip de
    # `ui=original` no Alpha, que a página não pode mostrar (é a UI do próprio produto).
    "measured": matrix_model({
        ("pico2_w_release", "ui=angulo"): lambda b: {"used": b["used"] + 41984, "bin": b["bin"] + 42336, "ram": b["ram"] + 7036},
        ("pico_w_air", "ui=angulo"): {"overflow": 3148},
        ("pico_w_release", "ui=angulo"): {"rule": "angulo-cyw43-needs-rp2350"},
        ("pico_w_alpha", "ui=angulo"): {"error": "the build failed"},
        ("pico_w_alpha", "ui=original"): lambda b: {"used": b["used"] + 1024, "bin": b["bin"] + 1024, "ram": b["ram"]},
    }),
    "smaller": matrix_model({("pico2_w_release", "ui=angulo"): lambda b: {"used": b["used"] - 1024, "bin": b["bin"] - 1024, "ram": b["ram"]}}),
    "overflow": matrix_model({("pico2_w_release", "ui=angulo"): {"overflow": 3148}}),
}


def noeffect_model(pairs):
    """O model.json de verdade com a matriz dizendo que virar a chave não muda a imagem (`same`), para (produto, chave)."""
    m = copy.deepcopy(MODEL)
    for pid, tid in pairs:
        m["costs"]["products"][pid]["flips"][tid] = {"same": True}
    return m


# No Air o buzzer está travado (air-no-buzzer): a chave que tem as duas linhas, o motivo e o "não muda nada".
MODELS["noeffect"] = noeffect_model([("pico_w_release", "mdns"), ("pico_w_air", "sound_buzzer")])

# O JS da lógica: a trava de cada opção de UI, a das chaves com a UI escolhida, o link, o perfil, a estimativa.
JS_UI = r"""
import * as L from "./logic.js";
const input = JSON.parse(await new Promise((ok) => {
  let s = ""; process.stdin.on("data", (d) => (s += d)); process.stdin.on("end", () => ok(s));
}));
const model = input.model;
const out = { uis: [], toggles: [], profiles: [], estimates: [], links: null };
const lockUi = typeof L.uiLockOf === "function" ? L.uiLockOf : () => "sem uiLockOf";
for (const p of input.products) {
  // Cada opção de UI como a próxima escolha, em cada estado de partida: a trava é perguntada à escolha de
  // agora, como a página a pergunta.
  for (const ctx of input.contexts[p]) {
    const now = L.effectiveConfig(model, p, ctx);
    for (const u of input.uis) {
      const next = L.normalizeSet(model, p, { ...ctx, ui: u });
      const cfg = L.effectiveConfig(model, p, next);
      const lock = lockUi(model, now, u, p);
      const est = L.estimate(model, p, next);
      out.uis.push({ p, ctx, u, lock: lock === "sem uiLockOf" ? lock : lock ? lock.id : null,
                     why: lock && lock !== "sem uiLockOf" ? lock.why : null,
                     ids: L.blockersOf(model, p, cfg).map((r) => r.id),
                     verdict: L.verdictOf(model, p, next), fit: est ? L.slotFit(model, p, est) : null });
    }
  }
  // Cada chave, com cada UI já escolhida: o gráfico do painel fica ligado quando o Ângulo está escolhido.
  for (const u of input.uis) {
    const cfg = L.effectiveConfig(model, p, { ui: u });
    for (const t of input.toggles) {
      const lock = L.lockOf(model, cfg, t, p);
      out.toggles.push({ p, u, t, lock: lock ? lock.id : null, why: lock ? lock.why : null });
    }
    const set = L.normalizeSet(model, p, { ui: u });
    out.profiles.push({ p, u, set, profile: L.profileJson(p, set), hash: L.formatHash(p, set) });
    out.estimates.push({ p, u, set, est: L.estimate(model, p, set) });
  }
}
// O preço de escolher cada UI no lugar da do produto (priceUi), em cada modelo: o de verdade e os de matriz trocada.
const priceUi = typeof L.priceUi === "function" ? L.priceUi : () => "sem priceUi";
out.prices = [];
for (const [name, fx] of Object.entries(input.fixtures)) {
  for (const p of input.products) {
    for (const u of input.uis) {
      out.prices.push({ fixture: name, p, u, price: priceUi(fx, p, u), est: L.estimate(fx, p, L.normalizeSet(fx, p, { ui: u })) });
    }
  }
}
out.links = {
  one: L.formatHash("pico2_w_release", { ui: "angulo" }),
  oneBack: L.parseHash(model, "#b=pico2_w_release&ui=angulo"),
  mixed: L.formatHash("pico_w_release", { sensor_ds18b20: false, mdns: true, net: "enc28j60", ui: "angulo" }),
  mixedBack: L.parseHash(model, "#b=pico_w_release&net=enc28j60&ui=angulo&on=mdns&off=sensor_ds18b20"),
  sameAsProduct: L.parseHash(model, "#b=pico2_w_angulo&ui=angulo"),
  otherThanProduct: L.parseHash(model, "#b=pico2_w_angulo&ui=original"),
  originalDefault: L.parseHash(model, "#b=pico2_w_release&ui=original"),
  defaultNotWritten: L.formatHash("pico2_w_angulo", L.normalizeSet(model, "pico2_w_angulo", { ui: "angulo" })),
  normalized: L.normalizeSet(model, "pico2_w_angulo", { ui: "angulo", mdns: true }),
};
out.hostile = input.hostile.map((h) => L.parseHash(model, h));
process.stdout.write(JSON.stringify(out));
"""

# O JS da página: o app.js de verdade, num DOM de mentira (só o que ele usa: createElement, atributos,
# filhos, ouvintes, getElementById), com o modelo no lugar do fetch. Cada cenário é uma visita: o hash de
# entrada, a língua, os cliques que o leitor dá; devolve, depois da carga e de cada clique, o que está na
# tela. Um clique em controle desabilitado não acontece (o navegador não o entrega) e vem como "disabled".
# O botão de compilar se aperta ("press"); o GitHub é um fetch de mentira que responde ao pedido de build
# ("api": "finish": a build termina, e o teste espera por ela) ou nunca responde ("api": "hang": a build fica em
# curso); a espera de segundos entre duas consultas dele vira imediata.
JS_APP = r"""
// The page's own app.js, run against a small stand-in for the DOM.
const input = JSON.parse(await new Promise((ok) => {
  let s = ""; process.stdin.on("data", (d) => (s += d)); process.stdin.on("end", () => ok(s));
}));
const errors = [];
const brief = (e) => String(e && e.stack ? e.stack.split("\n").slice(0, 3).join(" | ") : e);
process.on("unhandledRejection", (e) => errors.push("unhandledRejection: " + brief(e)));
process.on("uncaughtException", (e) => errors.push("uncaughtException: " + brief(e)));
// The page sleeps seconds between two looks at GitHub (sleep( ) in app.js); here the long waits are immediate.
// The stand-in's own waits (1 ms) are the real ones.
const realSetTimeout = globalThis.setTimeout;
globalThis.setTimeout = (fn, ms, ...rest) => realSetTimeout(fn, ms >= 1000 ? 0 : ms, ...rest);

class NodeBase {}
class Txt extends NodeBase {
  constructor(t) { super(); this.data = String(t); }
  get textContent() { return this.data; }
}
class El extends NodeBase {
  constructor(tag) { super(); this.tag = tag; this.attrs = {}; this.kids = []; this.listeners = {}; this.parent = null; this.style = {}; }
  setAttribute(k, v) { this.attrs[k] = String(v); }
  hasAttribute(k) { return k in this.attrs; }
  append(...kids) { for (const k of kids) { k.parent = this; this.kids.push(k); } }
  replaceChildren(...kids) { this.kids = []; this.append(...kids); }
  addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); }
  focus() {}
  set className(v) { this.attrs.class = String(v); }
  get className() { return this.attrs.class || ""; }
  set textContent(v) { this.kids = []; this.append(new Txt(v)); }
  get textContent() { return this.kids.map((k) => k.textContent).join(""); }
  get checked() { return this._checked !== undefined ? this._checked : "checked" in this.attrs; }
  set checked(v) { this._checked = Boolean(v); }
  get disabled() { return "disabled" in this.attrs; }
}
function* walk(e) { if (e instanceof El) { yield e; for (const k of e.kids) yield* walk(k); } }
const cls = (e, c) => (e.attrs.class || "").split(/\s+/).includes(c);
const find = (root, pred) => [...walk(root)].filter(pred);
const closest = (e, pred) => { for (let x = e; x; x = x.parent) if (x instanceof El && pred(x)) return x; return null; };
const define = (name, value) => Object.defineProperty(globalThis, name, { value, configurable: true, writable: true });
const first = (list) => (list.length ? list[0] : null);

// What an id named by an aria-* attribute is on the page: the row's own title, help or lock, some other element,
// nothing ("missing") or more than one element ("duplicate"). A reader of the page is told by these, and by
// nothing else, what a radio is called and why it is locked.
function role(root, id, own) {
  const hits = find(root, (e) => e.attrs.id === id);
  if (hits.length === 0) return "missing";
  if (hits.length > 1) return "duplicate";
  return Object.keys(own).find((r) => own[r] === hits[0]) || "other";
}
const named = (root, attr, own) => (attr === undefined ? null : attr.split(/\s+/).filter(Boolean).map((id) => role(root, id, own)));
// The texts a reader of the page is given for the ids an aria-describedby names, in the order it names them: the text of the
// element, or "missing" / "duplicate" for an id that names none / more than one element.
const described = (root, attr) => (attr === undefined ? [] : attr.split(/\s+/).filter(Boolean).map((id) => {
  const hits = find(root, (e) => e.attrs.id === id);
  return hits.length === 1 ? hits[0].textContent : hits.length ? "duplicate" : "missing";
}));

function row(label, root) {
  const input = first(find(label, (e) => e.tag === "input"));
  const el = (c) => first(find(label, (x) => cls(x, c)));
  const text = (c) => { const e = el(c); return e ? e.textContent : null; };
  const own = { title: el("cfg-chave-nome"), help: el("cfg-apoio"), lock: el("cfg-chave-trava") };
  return { id: input.attrs.id, radio: input.attrs.name, name: text("cfg-chave-nome"), help: text("cfg-apoio"),
           lock: text("cfg-chave-trava"), cost: text("cfg-chave-custo"),
           costTitle: el("cfg-chave-custo") ? (el("cfg-chave-custo").attrs.title || null) : null,
           checked: input.checked, disabled: input.disabled,
           labelledby: named(root, input.attrs["aria-labelledby"], own), labelledbyAttr: input.attrs["aria-labelledby"] || null,
           describedby: named(root, input.attrs["aria-describedby"], own), describedbyAttr: input.attrs["aria-describedby"] || null };
}

function snapshot(appEl, loc) {
  const snap = { hash: loc.hash, fieldsets: [], switches: [], warnings: [], notice: null, product: null,
                 build: null, summary: null, selos: [], profile: null, issue: null, radios: [], run: null };
  for (const f of find(appEl, (e) => e.tag === "fieldset" && cls(e, "cfg-rede"))) {
    const legend = first(find(f, (e) => e.tag === "legend"));
    const block = closest(f.parent, (e) => cls(e, "cfg-bloco"));
    const title = block && first(find(block, (e) => cls(e, "cfg-bloco-titulo")));
    const siblings = block ? block.kids.filter((k) => k instanceof El) : [];
    const firstSwitch = siblings.findIndex((k) => cls(k, "cfg-chave"));
    const badges = find(legend, (e) => cls(e, "selo")).map((e) => e.textContent);
    snap.fieldsets.push({
      legend: legend.kids[0].textContent, changed: badges.length > 0, badges,
      block: title ? title.textContent : null,
      beforeSwitches: firstSwitch === -1 || siblings.indexOf(f) < firstSwitch,
      rows: find(f, (e) => e.tag === "label" && cls(e, "cfg-rede-opcao")).map((l) => row(l, appEl)),
    });
  }
  for (const i of find(appEl, (e) => e.tag === "input" && e.attrs.role === "switch")) {
    const r = closest(i, (e) => cls(e, "cfg-chave"));
    const help = first(find(r, (e) => cls(e, "cfg-apoio")));
    snap.switches.push({ id: i.attrs.id, checked: i.checked, disabled: i.disabled,
                         locks: find(r, (e) => cls(e, "cfg-chave-trava")).map((e) => e.textContent),
                         badges: find(r, (e) => cls(e, "selo")).map((e) => e.textContent),
                         help: help ? help.textContent : null,
                         description: described(appEl, i.attrs["aria-describedby"]), describedbyAttr: i.attrs["aria-describedby"] || null });
  }
  snap.warnings = find(appEl, (e) => e.tag === "li" && cls(e, "cfg-aviso")).map((e) => ({ tone: e.attrs["data-tom"], text: e.textContent }));
  const notice = first(find(appEl, (e) => e.tag === "p" && cls(e, "cfg-aviso") && e.attrs.role === "status"));
  snap.notice = notice ? notice.textContent : null;
  const radio = first(find(appEl, (e) => e.tag === "input" && e.attrs.name === "produto" && e.checked));
  snap.product = radio ? radio.attrs.value : null;
  const build = first(find(appEl, (e) => e.attrs.id === "compilar"));
  snap.build = build ? { disabled: build.disabled } : null;
  const summary = first(find(appEl, (e) => cls(e, "cfg-resumo-texto")));
  snap.summary = summary ? summary.textContent : null;
  const selos = first(find(appEl, (e) => cls(e, "cfg-selos")));
  snap.selos = selos ? find(selos, (e) => cls(e, "selo")).map((e) => e.textContent) : [];
  const pre = first(find(appEl, (e) => e.tag === "pre" && cls(e, "cfg-comando")));
  snap.profile = pre ? pre.textContent : null;
  const a = first(find(appEl, (e) => e.tag === "a" && cls(e, "botao") && String(e.attrs.href).includes("/issues/new")));
  if (a) {
    const href = a.attrs.href;
    snap.issue = { title: decodeURIComponent(/[?&]title=([^&]*)/.exec(href)[1]), body: decodeURIComponent(/[?&]body=(.*)$/.exec(href)[1]) };
  }
  snap.radios = find(appEl, (e) => e.tag === "input" && e.attrs.type === "radio").map((e) => ({ id: e.attrs.id, name: e.attrs.name }));
  const runBox = first(find(appEl, (e) => cls(e, "cfg-estado")));
  snap.run = runBox ? { texts: find(runBox, (e) => e.tag === "p").map((e) => e.textContent),
                        links: find(runBox, (e) => e.tag === "a").map((e) => e.attrs.href) } : null;
  return snap;
}

async function settle(appEl) {
  for (let i = 0; i < 500 && appEl.kids.length === 0 && errors.length === 0; i++) await new Promise((r) => realSetTimeout(r, 1));
}

const results = [];
let n = 0, hung = false;
for (const sc of input.scenarios) {
  errors.length = 0;
  const body = new El("body"), appEl = new El("div"), ann = new El("p");
  appEl.attrs.id = "app"; ann.attrs.id = "cfg-anuncio"; body.append(ann, appEl);
  const loc = { hash: sc.hash, get href() { return "https://example.test/configurador/" + this.hash; } };
  const store = () => { const m = new Map(); return { getItem: (k) => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, v), removeItem: (k) => m.delete(k) }; };
  define("Node", NodeBase);
  define("document", { documentElement: { dataset: {}, lang: "" }, title: "", activeElement: null,
                       createElement: (t) => new El(t), createTextNode: (t) => new Txt(t),
                       getElementById: (id) => [...walk(body)].find((e) => e.attrs.id === id) || null,
                       querySelectorAll: () => [], querySelector: () => null });
  define("location", loc);
  define("history", { replaceState: (a, b, h) => { loc.hash = h; } });
  define("navigator", { languages: [sc.lang === "pt" ? "pt-BR" : "en"], language: sc.lang });
  define("window", { localStorage: store(), sessionStorage: store(), addEventListener() {} });
  if (sc.token) window.sessionStorage.setItem(input.api.tokenKey, input.api.token);
  // The model for the page's own fetch; for GitHub's API (the one address the page may send the token to) the
  // answers of a build that is asked for, found, followed and finished -- or never answered ("hang").
  const gh = { dispatched: [] };
  define("fetch", async (url, opts = {}) => {
    const u = String(url);
    if (!u.startsWith(input.api.base)) return { ok: true, status: 200, json: async () => JSON.parse(JSON.stringify(input.models[sc.model || "real"])) };
    const path = u.slice(input.api.base.length);
    const reply = (data, status = 200) => ({ ok: status < 400, status, statusText: String(status), json: async () => data });
    if (path.endsWith("/dispatches")) {
      const sent = JSON.parse(opts.body).inputs;
      gh.dispatched.push({ profile: sent.profile, id: sent.request_id });
      if (sc.api === "hang") return new Promise(() => {});
      return reply(null, 204);
    }
    const id = gh.dispatched.length ? gh.dispatched[gh.dispatched.length - 1].id : null;
    if (path.includes("/workflows/")) {
      return reply({ workflow_runs: [{ display_title: `custom build ${id}`, id: input.api.runId, status: "queued",
                                       html_url: `https://github.com/${input.api.repo}/actions/runs/${input.api.runId}` }] });
    }
    if (path.endsWith("/artifacts")) {
      return reply({ artifacts: [{ name: `simut-custom-bin${input.api.bin}-ram${input.api.ram}`, id: input.api.artifactId }] });
    }
    if (path.endsWith(`/actions/runs/${input.api.runId}`)) return reply({ status: "completed", conclusion: "success" });
    return reply({ message: "not found" }, 404);
  });
  const rec = { name: sc.name, snaps: [], steps: [], errors: [], dispatched: [] };
  try {
    await import("./app.js?run=" + n++);
    await settle(appEl);
    rec.snaps.push(snapshot(appEl, loc));
    for (const a of sc.actions) {
      const id = a.click || a.press;
      const el = first(find(body, (e) => e.attrs.id === id));
      const step = { click: id, result: null };
      if (!el) step.result = "missing";
      else if (el.disabled) step.result = "disabled";
      else if (a.press) {
        // A button: its click listeners run; a build that is answered is waited for to its end -- for at most
        // 5 s (it takes milliseconds), so that a page whose build never ends fails the test instead of holding it.
        try {
          const ended = Promise.all((el.listeners.click || []).map((fn) => fn({ target: el, preventDefault() {} })));
          let won = true, timer;
          if (sc.api !== "hang") {
            won = await Promise.race([ended.then(() => true), new Promise((r) => { timer = realSetTimeout(() => r(false), 5000); })]);
            clearTimeout(timer);
          }
          hung = hung || !won;
          step.result = won ? "pressed" : "timeout";
        } catch (e) { step.result = "threw: " + brief(e); }
      } else {
        el.checked = el.attrs.type === "checkbox" ? !el.checked : true;
        try { for (const fn of el.listeners.change || []) fn({ target: el }); step.result = "clicked"; }
        catch (e) { step.result = "threw: " + brief(e); }
      }
      rec.steps.push(step);
      rec.snaps.push(snapshot(appEl, loc));
    }
  } catch (e) { rec.errors.push(brief(e)); }
  rec.dispatched = gh.dispatched.map((d) => d.profile);
  rec.errors.push(...errors);
  results.push(rec);
  if (hung) break;   // that page is still polling in the background: what the next visit showed would be its doing
}
// A build that never ended is still running in the background: leave once the answer is written out.
process.stdout.write(JSON.stringify(results), () => { if (hung) process.exit(0); });
"""

def run_node(script, payload, logic_text=None, app_text=None):
    """Roda um script de node ao lado de cópias da página: o logic.js e o app.js dados (ou os de verdade) e o
    rules.js. Um node que falha (uma cópia com defeito) volta como {"node_error": ...}, que vira achado."""
    node = shutil.which("node")
    if not node:
        sys.exit("test_configurator_page: node não encontrado")
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copy(os.path.join(PAGE, "rules.js"), tmp)
        for name, text in (("logic.js", logic_text), ("app.js", app_text)):
            if text is None:
                shutil.copy(os.path.join(PAGE, name), tmp)
            else:
                with open(os.path.join(tmp, name), "w", encoding="utf-8") as fh:
                    fh.write(text)
        with open(os.path.join(tmp, "package.json"), "w") as fh:
            fh.write('{"type": "module"}\n')
        with open(os.path.join(tmp, "run.js"), "w", encoding="utf-8") as fh:
            fh.write(script)
        r = subprocess.run([node, "run.js"], cwd=tmp, input=json.dumps(payload), capture_output=True, text=True)
    if r.returncode != 0:
        return {"node_error": r.stderr[-1500:]}
    return json.loads(r.stdout)


def str_of(lang, key):
    """O texto que o app.js diz em `lang` para `key`, lido do STR dele."""
    block = re.search(r"\n  %s: \{\n(.*?)\n  \},\n" % lang, APP, re.S)
    m = re.search(r'"%s":\s*"((?:[^"\\]|\\.)*)"' % re.escape(key), block.group(1)) if block else None
    if not m:   # ausência é um achado legível, e não um traceback
        return f"<texto {key!r} ausente em {lang}>"
    return json.loads('"' + m.group(1) + '"')


def say(lang, key, **kw):
    s = str_of(lang, key)
    for k, v in kw.items():
        s = s.replace("{" + k + "}", str(v))
    return s


def rule_why(rid):
    return {lang: next(r for r in M["rules"] if r["id"] == rid)["why"][lang] for lang in gf.LANGS}


def new_blockers(prof, **change):
    """Os bloqueios que `change` traz ao perfil e que ele ainda não tinha: a pergunta de toda trava."""
    now = {b["id"] for b in gf.blockers(prof, M)}
    return [b for b in gf.blockers(dict(prof, **change), M) if b["id"] not in now]


def label_of(pid, lang):
    return M["ui_products"][pid]["label"][lang]


# Como o Intl.NumberFormat da página escreve um inteiro e um tamanho em KB (en e pt-BR agrupam de 4 dígitos para cima).
LOCALE_SEPS = {"en": (",", "."), "pt": (".", ",")}


def num_text(lang, n):
    th = LOCALE_SEPS[lang][0]
    return ("-" if n < 0 else "") + f"{abs(n):,}".replace(",", th)


def kb_text(lang, n):
    th, dec = LOCALE_SEPS[lang]
    q = (Decimal(abs(n)) / 1024).quantize(Decimal("0.1"), rounding=ROUND_HALF_UP)
    whole, frac = str(q).split(".")
    return ("−" if n < 0 else "") + f"{int(whole):,}".replace(",", th) + dec + frac + " KB"


def expected_price(model, pid, u):
    """O que a página deve a quem escolhe a UI `u` no lugar da do produto, lido da matriz do modelo (chave
    "ui=<id>", como "net=<id>"): bytes de flash a mais (a menos, se negativo), ou nada."""
    prod = next(x for x in model["products"] if x["id"] == pid)
    costs = (model.get("costs") or {}).get("products", {}).get(pid)
    if costs is None or u == prod["config"]["ui"]:
        return None
    f = costs["flips"].get(f"ui={u}")
    if not f or f.get("same") or any(k in f for k in ("rule", "error", "unsupported")):
        return None
    if "used" in f:
        return {"flash": f["used"] - costs["base"]["used"], "overflow": False}
    if "overflow" in f:
        return {"flash": prod["ceilings"]["flash_region"] + f["overflow"] - costs["base"]["bin"], "overflow": True}
    return None


def expected_ui_price(model, pid, u, lang, ctx=None):
    """(o texto do preço, o título dele) que a linha da UI `u` deve mostrar, ou (None, None): a linha travada
    não mostra preço."""
    pr = expected_price(model, pid, u)
    if pr is None or new_blockers(dict(gf.resolve_profile(pid, M["profiles"]), **(ctx or {})), ui=u):
        return (None, None)
    title = say(lang, "price.ui.over") if pr["overflow"] else say(
        lang, "price.ui.more" if pr["flash"] >= 0 else "price.ui.less", n=num_text(lang, abs(pr["flash"])))
    return (kb_text(lang, pr["flash"]), title)


ANGULO_RULES = {"angulo-needs-tft", "angulo-cyw43-needs-rp2350", "angulo-needs-tft-graph"}


def app_const(name):
    m = re.search(r'\nconst %s = "([^"]*)";' % name, APP)
    return m.group(1) if m else None


# O GitHub de mentira da página (2026-10-22): os endereços e a chave do token são lidos do app.js, de onde a página
# os tira; o que a build "terminada" devolve (a execução, o artefato, os tamanhos) é inventado aqui.
PAGE_API = {"base": app_const("API"), "repo": app_const("REPO"), "tokenKey": app_const("STORAGE_KEY"),
            "token": "github_pat_" + "x" * 30, "runId": 777, "artifactId": 5, "bin": 900000, "ram": 120000}
check(all(PAGE_API[k] for k in ("base", "repo", "tokenKey")), f"app.js: não achei as constantes API, REPO e STORAGE_KEY: {PAGE_API}")


def ui_logic_findings(js):
    """A lógica da UI (logic.js) contra o Python: cada trava, o link, o perfil e a estimativa."""
    fs = []

    def ok(cond, msg):
        if not cond:
            fs.append(msg)

    if "node_error" in js:
        return [f"o node falhou na lógica da UI: {js['node_error']}"]
    by_ui = {(r["p"], r["u"], json.dumps(r["ctx"], sort_keys=True)): r for r in js["uis"]}

    def get(p, u, ctx=None):
        return by_ui[(p, u, json.dumps(ctx or {}, sort_keys=True))]

    # 1. Cada opção de UI, em cada estado de partida, está travada se, e só se, o Python tem um bloqueio NOVO
    #    para o perfil com a UI trocada -- o primeiro, com as mesmas palavras --, e o veredito é "blocked" se,
    #    e só se, o Python tem algum bloqueio.
    for r in js["uis"]:
        p, u, ctx = r["p"], r["u"], r["ctx"]
        tag = f"{p}/{ctx or 'como publicado'}/ui={u}"
        prof = dict(gf.resolve_profile(p, M["profiles"]), **ctx)
        new = new_blockers(prof, ui=u)
        after = gf.blockers(dict(prof, ui=u), M)
        ok(r["lock"] == (new[0]["id"] if new else None),
           f"{tag}: trava da página {r['lock']} != bloqueios novos {[b['id'] for b in new]}")
        if new and r["lock"] == new[0]["id"]:
            ok(r["why"] == new[0]["why"], f"{tag}: o texto da trava {r['why']} != o do gerador {new[0]['why']}")
        ok(r["ids"] == [b["id"] for b in after], f"{tag}: blockersOf dá {r['ids']}, gen_features.blockers {[b['id'] for b in after]}")
        ok(r["verdict"] == ("blocked" if after else r["fit"]), f"{tag}: o veredito é {r['verdict']!r}, com os bloqueios {[b['id'] for b in after]}")
    # Sem uma opção travada por cada regra do Ângulo, e uma livre, a comparação acima não prova nada.
    locked = {r["lock"] for r in js["uis"] if r["lock"]}
    ok(ANGULO_RULES <= locked, f"as regras do Ângulo que travam alguma opção: {sorted(locked)}; faltam {sorted(ANGULO_RULES - locked)}")
    ok(any(r["lock"] is None and r["u"] == "angulo" and not r["ctx"] for r in js["uis"]), "nenhum produto deixa o Ângulo livre")

    # 2. O que o desenho promete, em palavras. O Pico W com Wi-Fi: o Ângulo travado em todo perfil, e nos de
    #    tela touch com o gráfico ligado (o release) a razão é a do Pico W; o Pico 2 W e o SIMUT Wire com tela
    #    touch: livre. A UI do próprio produto nunca está travada.
    pico_w = [p["id"] for p in MODEL["products"] if p["config"]["chip"] == "rp2040" and p["config"]["net"] == "cyw43"]
    ok("pico_w_release" in pico_w and len(pico_w) >= 4, f"os perfis do Pico W com Wi-Fi: {pico_w}")
    for p in pico_w:
        cfg = next(x for x in MODEL["products"] if x["id"] == p)["config"]
        lock = get(p, "angulo")["lock"]
        ok(lock is not None and lock.startswith("angulo-"), f"{p}: o Ângulo devia estar travado no Pico W com Wi-Fi, trava por {lock}")
        if cfg["display"] == "tft" and cfg["tft_graph"]:
            ok(lock == "angulo-cyw43-needs-rp2350", f"{p}: a razão devia ser a do Pico W, é {lock}")
            ok(get(p, "angulo")["why"] == rule_why("angulo-cyw43-needs-rp2350"), f"{p}: o motivo da trava não é o da regra do Pico W")
    for p in ("pico2_w_release", "pico_wiretft"):
        ok(get(p, "angulo")["lock"] is None, f"{p}: o Ângulo devia estar livre, trava por {get(p, 'angulo')['lock']}")
        ok(get(p, "original")["lock"] is None, f"{p}: o visual original devia estar livre")
    for x in MODEL["products"]:
        ok(get(x["id"], x["config"]["ui"])["lock"] is None, f"{x['id']}: a UI do próprio produto está travada")
    #    O gráfico do painel: sem ele o Ângulo está travado, com o motivo da regra; e com o Ângulo escolhido
    #    é a chave do gráfico que fica travada, ligada. Das duas pontas, o mesmo motivo.
    for p in ("pico2_w_release", "pico_wiretft"):
        r = get(p, "angulo", {"tft_graph": False})
        ok(r["lock"] == "angulo-needs-tft-graph" and r["why"] == rule_why("angulo-needs-tft-graph"),
           f"{p}: com o gráfico desligado o Ângulo devia travar por angulo-needs-tft-graph: {r['lock']}")
        sw_on = next(t for t in js["toggles"] if (t["p"], t["u"], t["t"]) == (p, "angulo", "tft_graph"))
        ok(sw_on["lock"] == "angulo-needs-tft-graph" and sw_on["why"] == rule_why("angulo-needs-tft-graph"),
           f"{p}: com o Ângulo escolhido a chave do gráfico devia travar por angulo-needs-tft-graph: {sw_on}")
        sw_off = next(t for t in js["toggles"] if (t["p"], t["u"], t["t"]) == (p, "original", "tft_graph"))
        ok(sw_off["lock"] is None, f"{p}: com o visual original a chave do gráfico devia estar livre: {sw_off}")
    ok(get("pico_wiretft", "angulo", {"net": "cyw43"})["lock"] == "angulo-cyw43-needs-rp2350",
       "pico_wiretft com o Wi-Fi de volta: o Ângulo devia travar pela regra do Pico W")

    # 3. As chaves, com cada UI escolhida: travadas se, e só se, virá-las traz um bloqueio novo (o primeiro).
    for r in js["toggles"]:
        p, u, t = r["p"], r["u"], r["t"]
        prof = dict(gf.resolve_profile(p, M["profiles"]), ui=u)
        new = new_blockers(prof, **{t: not bool(prof.get(t, False))})
        ok(r["lock"] == (new[0]["id"] if new else None),
           f"{p}/ui={u}/{t}: trava da página {r['lock']} != bloqueios novos {[b['id'] for b in new]}")
        if new and r["lock"] == new[0]["id"] and new[0]["kind"] == "rule":
            ok(r["why"] == new[0]["why"], f"{p}/ui={u}/{t}: o texto da trava {r['why']} != o do gerador {new[0]['why']}")

    # 4. A UI no link e no perfil: a do produto não é uma mudança e não se escreve; a outra vai e volta igual,
    #    e o perfil que a página manda compilar leva `ui` só nesse caso, na grafia do Python.
    for r in js["profiles"]:
        p, u = r["p"], r["u"]
        base_ui = gf.config_of(gf.resolve_profile(p, M["profiles"]))["ui"]
        want_set = {} if u == base_ui else {"ui": u}
        ok(r["set"] == want_set, f"{p}: normalizeSet de ui={u} deu {r['set']}, devia ser {want_set}")
        spelled = json.dumps({"v": 1, "base": p, "set": want_set}, sort_keys=True, separators=(",", ":"))
        ok(r["profile"] == spelled == bc.canonical(p, want_set), f"{p}/ui={u}: o perfil {r['profile']} != {spelled}")
        try:
            ok(bc.parse_spec(r["profile"], M) == (p, want_set), f"{p}/ui={u}: o build_custom leu o perfil {r['profile']} de outro jeito")
        except bc.SpecError as e:
            fs.append(f"{p}/ui={u}: o build_custom recusou o perfil {r['profile']} ({e})")
        ok(r["hash"] == f"#b={p}" + (f"&ui={u}" if want_set else ""), f"{p}/ui={u}: o link é {r['hash']}")
    nl = js["links"]
    ok(nl["one"] == "#b=pico2_w_release&ui=angulo", f"formatHash com a UI deu {nl['one']!r}")
    ok(nl["oneBack"] == {"productId": "pico2_w_release", "set": {"ui": "angulo"}}, f"o link com ui=angulo voltou como {nl['oneBack']}")
    ok(nl["mixed"] == "#b=pico_w_release&net=enc28j60&ui=angulo&on=mdns&off=sensor_ds18b20",
       f"formatHash com a rede, a UI e interruptores deu {nl['mixed']!r}")
    rel = gf.config_of(gf.resolve_profile("pico_w_release", M["profiles"]))
    want_mixed = {k: v for k, v in {"mdns": True, "sensor_ds18b20": False, "net": "enc28j60", "ui": "angulo"}.items() if v != rel[k]}
    ok(nl["mixedBack"] == {"productId": "pico_w_release", "set": want_mixed} and want_mixed.get("ui") == "angulo",
       f"o link com a rede, a UI e interruptores voltou como {nl['mixedBack']}")
    ok(nl["sameAsProduct"] == {"productId": "pico2_w_angulo", "set": {}}, f"ui= igual ao do produto não é uma mudança: {nl['sameAsProduct']}")
    ok(nl["otherThanProduct"] == {"productId": "pico2_w_angulo", "set": {"ui": "original"}}, f"ui=original no produto do Ângulo voltou como {nl['otherThanProduct']}")
    ok(nl["originalDefault"] == {"productId": "pico2_w_release", "set": {}}, f"ui=original no Pico 2 W não é uma mudança: {nl['originalDefault']}")
    ok(nl["defaultNotWritten"] == "#b=pico2_w_angulo", f"a UI do produto foi escrita no link: {nl['defaultNotWritten']!r}")
    ok(nl["normalized"] == {}, f"normalizeSet deveria tirar ui e mdns quando são os do produto: {nl['normalized']}")
    #    Os links hostis da UI, lidos por esta cópia da lógica: recusados, com o motivo certo.
    for h, got in zip(HOSTILE_UI, js["hostile"]):
        ok((got.get("error"), got.get("value")) == HOSTILE_UI[h],
           f"link {h[:60]!r}: a página recusa com {(got.get('error'), got.get('value'))}, devia ser {HOSTILE_UI[h]}")

    # 5. O custo: sem a medida de `ui=<id>` nos flips a estimativa a deixa de fora e diz que deixou ("sem
    #    medida", como a rede); com ela, é a base mais a medida. Até a matriz medir, e depois.
    costs_by = MODEL["costs"]["products"] if MODEL.get("costs") else {}
    for r in js["estimates"]:
        p, u, e = r["p"], r["u"], r["est"]
        if p not in costs_by:
            ok(e is None, f"{p} não tem custo medido, mas a página estimou {e}")
            continue
        base = costs_by[p]["base"]
        flip = costs_by[p]["flips"].get(f"ui={u}")
        if not r["set"]:
            ok(e["used"] == base["used"] and e["missing"] == [], f"{p}/ui={u}: a UI do produto mexeu na estimativa: {e}")
        elif flip is None or "used" not in flip and "overflow" not in flip:
            ok(e["missing"] == ["ui"] and e["used"] == base["used"] and e["bin"] == base["bin"],
               f"{p}/ui={u}: sem medida, a estimativa devia deixar `ui` em missing e a base como está: {e}")
        elif "used" in flip:
            ok(e["used"] == flip["used"] and e["bin"] == flip["bin"] and e["ram"] == flip["ram"] and e["missing"] == [],
               f"{p}/ui={u}: a estimativa {e} != a medida {flip}")

    # 5b. O preço de escolher a UI (L.priceUi, C1d): em cada modelo, o que a matriz dele diz de `ui=<id>`.
    for r in js["prices"]:
        want = expected_price(MODELS[r["fixture"]], r["p"], r["u"])
        ok(r["price"] == want, f"{r['fixture']}/{r['p']}/ui={r['u']}: priceUi deu {r['price']!r}, devia ser {want!r}")
    priced = {(r["p"], r["u"]) for r in js["prices"] if r["fixture"] == "measured" and isinstance(r["price"], dict)}
    ok(priced == {("pico2_w_release", "angulo"), ("pico_w_air", "angulo")},
       f"o modelo medido devia dar preço ao Ângulo do Pico 2 W e ao do Air (estouro) e a mais nenhuma UI: {sorted(priced)}")
    ok(all(r["price"] is None for r in js["prices"] if r["fixture"] == "unmeasured"), "sem a entrada na matriz nenhuma UI tem preço")
    cell = {(r["fixture"], r["p"], r["u"]): r["est"] for r in js["prices"]}
    b = MODELS["measured"]["costs"]["products"]["pico2_w_release"]["base"]
    e = cell[("measured", "pico2_w_release", "angulo")]
    ok(e and e["missing"] == [] and e["used"] == b["used"] + 41984 and e["bin"] == b["bin"] + 42336 and e["ram"] == b["ram"] + 7036,
       f"measured/pico2_w_release/angulo: a estimativa com a medida é a base mais ela: {e}")
    e = cell[("unmeasured", "pico2_w_release", "angulo")]
    ok(e and e["missing"] == ["ui"], f"unmeasured/pico2_w_release/angulo: sem a entrada `ui` fica em missing: {e}")
    return fs


def ui_scenarios():
    """As visitas à página: cada produto nas duas línguas, e as que se fazem com cliques."""
    sc = [{"name": f"sweep:{lang}:{p}", "hash": f"#b={p}", "lang": lang, "actions": []}
          for lang in ("en", "pt") for p in PRODUCTS]
    sc += [
        {"name": "two", "hash": "#b=pico2_w_release", "lang": "en", "model": "unmeasured",
         "actions": [{"click": "u-angulo"}, {"click": "t-tft_graph"}, {"click": "u-original"}]},
        {"name": "nochart", "hash": "#b=pico2_w_release&off=tft_graph", "lang": "en",
         "actions": [{"click": "u-angulo"}, {"click": "t-tft_graph"}, {"click": "u-angulo"}]},
        {"name": "wiretft", "hash": "#b=pico_wiretft", "lang": "en",
         "actions": [{"click": "u-angulo"}, {"click": "n-cyw43"}]},
        {"name": "twoangulo", "hash": "#b=pico2_w_angulo", "lang": "en",
         "actions": [{"click": "u-original"}, {"click": "u-angulo"}]},
        {"name": "linkblocked", "hash": "#b=pico_w_release&ui=angulo", "lang": "en", "actions": [{"click": "u-original"}]},
        {"name": "linkunknown", "hash": "#b=pico2_w_release&ui=nonsense", "lang": "en", "actions": []},
        {"name": "ptflow", "hash": "#b=pico2_w_release", "lang": "pt", "model": "unmeasured", "actions": [{"click": "u-angulo"}]},
        # A rede mudada e de volta, e a chave mudada, nas duas línguas: a marca "mudou" de cada uma (C1b-b).
        {"name": "netchange:en", "hash": "#b=pico_wire", "lang": "en", "actions": [{"click": "n-cyw43"}, {"click": "n-enc28j60"}]},
        {"name": "netchange:pt", "hash": "#b=pico_wire", "lang": "pt", "actions": [{"click": "n-cyw43"}]},
        {"name": "togglechange:pt", "hash": "#b=pico2_w_release&off=tft_graph", "lang": "pt", "actions": [{"click": "t-tft_graph"}]},
        # A build pronta pertence à escolha que a pediu: sai da tela quando o leitor muda a UI, como quando muda a
        # rede; uma build em curso fica, com o nome do que ela compila (dropFinishedRun( ), C1b-b).
        {"name": "run:look", "hash": "#b=pico2_w_release", "lang": "en", "api": "finish", "token": True,
         "actions": [{"press": "compilar"}, {"click": "u-angulo"}]},
        {"name": "run:net", "hash": "#b=pico_wire", "lang": "en", "api": "finish", "token": True,
         "actions": [{"press": "compilar"}, {"click": "n-cyw43"}]},
        {"name": "run:busy", "hash": "#b=pico2_w_release", "lang": "en", "api": "hang", "token": True,
         "actions": [{"press": "compilar"}, {"click": "u-angulo"}]},
        # O preço da linha da UI (C1d): a matriz com a medida, com uma economia, com o estouro do slot, com uma regra
        # (o Pico W) e sem a entrada. As do "two" e do "ptflow" (sem medida) rodam no modelo sem a entrada, para
        # valerem também no dia em que a matriz de verdade a tiver.
        {"name": "price:measured", "hash": "#b=pico2_w_release", "lang": "en", "model": "measured", "actions": [{"click": "u-angulo"}]},
        {"name": "price:measured:pt", "hash": "#b=pico2_w_release", "lang": "pt", "model": "measured", "actions": []},
        {"name": "price:smaller", "hash": "#b=pico2_w_release", "lang": "en", "model": "smaller", "actions": []},
        {"name": "price:overflow", "hash": "#b=pico2_w_release", "lang": "en", "model": "overflow", "actions": []},
        {"name": "price:rule", "hash": "#b=pico_w_release", "lang": "en", "model": "measured", "actions": [{"click": "u-angulo"}]},
        {"name": "price:unmeasured", "hash": "#b=pico2_w_release&ui=angulo", "lang": "en", "model": "unmeasured", "actions": []},
        # A matriz mediu o Ângulo no produto como publicado; com o gráfico desligado a opção está travada, e a linha travada não mostra preço.
        {"name": "price:locked", "hash": "#b=pico2_w_release&off=tft_graph", "lang": "en", "model": "measured", "actions": []},
        # Uma chave sem efeito, e uma travada que também não tem efeito: o motivo e a frase chegam ao leitor de tela (C1d).
        {"name": "noeffect:release", "hash": "#b=pico_w_release", "lang": "en", "model": "noeffect", "actions": []},
        {"name": "noeffect:air:pt", "hash": "#b=pico_w_air", "lang": "pt", "model": "noeffect", "actions": []},
    ]
    return sc


def ui_app_findings(res):
    """O app.js de verdade, visita por visita: o que o leitor vê da escolha da UI e do que ela trava, o que o
    leitor de tela recebe de cada escolha, a marca de "mudou" e a build pronta."""
    fs = []

    def ok(cond, msg):
        if not cond:
            fs.append(msg)

    by = {r["name"]: r for r in res}
    for r in res:
        for e in r["errors"]:
            fs.append(f"{r['name']}: a página deu erro: {e}")
        for st in r["steps"]:
            if st["result"] == "timeout":
                fs.append(f"{r['name']}: a build pedida ao apertar {st['click']} não terminou em 5 s; as visitas depois dela não rodaram")
    for want in ui_scenarios():
        if want["name"] not in by:
            fs.append(f"{want['name']}: a visita não voltou")
        elif len(by[want["name"]]["snaps"]) != 1 + len(want["actions"]):
            fs.append(f"{want['name']}: {len(by[want['name']]['snaps'])} telas para {len(want['actions'])} cliques")
    if fs:
        return fs

    # Um elemento que não está na tela volta como um vazio que reprova as comparações, e não como um None
    # que derrubaria o teste: ausência é um achado, não um traceback.
    def fieldset(s, radio):
        got = [f for f in s["fieldsets"] if f["rows"] and f["rows"][0]["radio"] == radio]
        return got[0] if len(got) == 1 else {"legend": None, "changed": None, "badges": None, "block": None, "beforeSwitches": None, "rows": []}

    def row(s, rid):
        return next((x for f in s["fieldsets"] for x in f["rows"] if x["id"] == rid),
                    {"id": rid, "radio": None, "name": None, "help": None, "lock": "ausente", "cost": None, "costTitle": None, "checked": None, "disabled": None,
                     "labelledby": None, "labelledbyAttr": None, "describedby": None, "describedbyAttr": None})

    def switch(s, sid):
        return next((x for x in s["switches"] if x["id"] == sid), {"id": sid, "checked": None, "disabled": None, "locks": ["ausente"], "badges": None,
                                                       "help": None, "description": None, "describedbyAttr": None})

    # 1. A varredura: cada produto, nas duas línguas. A escolha está no grupo do modelo, antes das chaves, com
    #    o título do modelo e uma opção por UI do manifesto, com o nome e a ajuda dele; a do produto marcada;
    #    a opção que uma regra recusa desabilitada, com o motivo da regra à vista, no texto de toda trava.
    #    A escolha da rede, que já existia, é lida do mesmo jeito: é o controle do instrumento.
    for lang in ("en", "pt"):
        for p in PRODUCTS:
            s = by[f"sweep:{lang}:{p}"]["snaps"][0]
            tag = f"{p} ({lang})"
            prof = gf.resolve_profile(p, M["profiles"])
            base = gf.config_of(prof)
            ok(s["product"] == p, f"{tag}: a página abriu {s['product']}")
            f = fieldset(s, "visual")
            if not f["rows"]:
                fs.append(f"{tag}: a página devia desenhar uma escolha de UI (rádios 'visual'), e não desenhou uma só")
                continue
            group = next(g for g in MODEL["groups"] if g["id"] == MODEL["ui"]["group"])
            ok(f["block"] == group["label"][lang], f"{tag}: a escolha da UI está no bloco {f['block']!r}, devia estar em {group['label'][lang]!r}")
            ok(f["legend"] == MODEL["ui"]["label"][lang], f"{tag}: o título da escolha é {f['legend']!r}")
            ok(f["beforeSwitches"], f"{tag}: a escolha da UI devia vir antes das chaves do grupo")
            ok(not f["changed"], f"{tag}: o produto como publicado já mostra 'mudou' na UI")
            ok([x["id"] for x in f["rows"]] == [f"u-{u}" for u in UIS], f"{tag}: as opções são {[x['id'] for x in f['rows']]}")
            for x, u in zip(f["rows"], UIS):
                new = new_blockers(prof, ui=u)
                ok(x["name"] == M["ui_panels"][u]["label"][lang], f"{tag}/{u}: o nome é {x['name']!r}")
                ok(x["help"] == M["ui_panels"][u]["help"][lang], f"{tag}/{u}: a ajuda não é a do manifesto")
                ok(x["checked"] == (u == base["ui"]), f"{tag}/{u}: marcada={x['checked']}, a UI do produto é {base['ui']}")
                ok(x["disabled"] == bool(new), f"{tag}/{u}: desabilitada={x['disabled']}, bloqueios novos {[b['id'] for b in new]}")
                ok(x["lock"] == (say(lang, "locked", why=new[0]["why"][lang]) if new else None),
                   f"{tag}/{u}: o texto da trava é {x['lock']!r}, devia ser o motivo de {new[0]['id'] if new else None}")
                want = expected_ui_price(MODEL, p, u, lang)
                ok((x["cost"], x["costTitle"]) == want,
                   f"{tag}/{u}: a opção mostra o preço {x['cost']!r} (título {x['costTitle']!r}), a matriz do modelo dá {want}")
            n = fieldset(s, "rede")
            ok([x["id"] for x in n["rows"]] == [f"n-{o}" for o in NETS], f"{tag}: a escolha da rede não foi lida")
            for x, o in zip(n["rows"], NETS):
                new = new_blockers(prof, net=o)
                ok(x["disabled"] == bool(new) and x["lock"] == (say(lang, "locked", why=new[0]["why"][lang]) if new else None),
                   f"{tag}/net={o}: a página mostra {x['disabled']}/{x['lock']!r}, os bloqueios novos são {[b['id'] for b in new]}")
            # O que o leitor de tela recebe de cada rádio das duas escolhas (C1b-b). O nome é o título da linha
            # (aria-labelledby), a descrição é a ajuda e, se a opção está travada, o motivo (aria-describedby):
            # cada id citado existe, uma vez só, e é o elemento certo da própria linha, e a descrição lê a ajuda ANTES do motivo da trava (a ordem
            # dos ids é a ordem em que o leitor de tela fala; trocá-la passava por todas as conferências até a revisão do B2a, 2026-10-22).
            # Os rádios de uma escolha
            # têm um nome só (o grupo: as setas andam entre eles, um só fica marcado), e nenhuma outra escolha o usa.
            # A chave (renderToggle) tem outro construtor -- um <label for>, só a ajuda na descrição e a trava num
            # <p> sem id --, e fica fora desta leitura: o que se lê aqui é o que as duas escolhas dividem.
            for choice, group, opts, key in ((f, "visual", UIS, "ui"), (n, "rede", NETS, "net")):
                for x, o in zip(choice["rows"], opts):
                    want_desc = ["help", "lock"] if new_blockers(prof, **{key: o}) else ["help"]
                    ok(x["labelledby"] == ["title"],
                       f"{tag}/{key}={o}: o nome do rádio (aria-labelledby={x['labelledbyAttr']!r}) vem de {x['labelledby']}, devia ser só o título da linha")
                    ok((x["describedby"] or ["-"]) == want_desc,
                       f"{tag}/{key}={o}: a descrição do rádio (aria-describedby={x['describedbyAttr']!r}) vem de {x['describedby']}, devia ser {want_desc}, nessa ordem")
                ok({x["radio"] for x in choice["rows"]} == {group},
                   f"{tag}: os rádios da escolha {key} têm os nomes {sorted(str(x['radio']) for x in choice['rows'])}, devia ser todos {group!r}")
            ids = [r["id"] for r in s["radios"]]
            groups = {}
            for r in s["radios"]:
                groups.setdefault(r["name"], []).append(r["id"])
            want_groups = {"produto": sorted(i for i in ids if i.startswith("p-")),
                           "rede": sorted(f"n-{o}" for o in NETS), "visual": sorted(f"u-{u}" for u in UIS)}
            ok({k: sorted(v) for k, v in groups.items()} == want_groups,
               f"{tag}: os grupos de rádio da página são {groups}, devia ser um por escolha: {want_groups}")

    # 2. O Pico 2 W: escolher o Ângulo, e voltar. A chave do gráfico fica travada, ligada, com o motivo da
    #    regra; a estimativa diz que não tem medida da UI; o perfil, o link e o pedido levam `ui`; voltar à
    #    UI do produto deixa a tela IGUAL à de partida (o padrão não se escreve em lugar nenhum).
    r = by["two"]
    ok([st["result"] for st in r["steps"]] == ["clicked", "disabled", "clicked"], f"two: cliques {r['steps']}")
    s0, s1, s2, s3 = r["snaps"]
    chart = rule_why("angulo-needs-tft-graph")
    ang_en = M["ui_panels"]["angulo"]["label"]["en"]
    ok(s0["hash"] == "#b=pico2_w_release" and row(s0, "u-original")["checked"] and not row(s0, "u-angulo")["checked"]
       and not row(s0, "u-angulo")["disabled"], f"two: a tela de partida: {s0['hash']}")
    ok(switch(s0, "t-tft_graph")["checked"] and not switch(s0, "t-tft_graph")["disabled"], "two: o gráfico devia estar ligado e livre")
    ok(s1["hash"] == "#b=pico2_w_release&ui=angulo", f"two: o link com o Ângulo é {s1['hash']!r}")
    ok(row(s1, "u-angulo")["checked"] and not row(s1, "u-original")["checked"], "two: o Ângulo devia estar marcado")
    ok(fieldset(s1, "visual")["changed"], "two: o título da escolha devia dizer 'mudou'")
    sw1 = switch(s1, "t-tft_graph")
    ok(sw1["checked"] and sw1["disabled"] and sw1["locks"] == [say("en", "locked", why=chart["en"])],
       f"two: a chave do gráfico devia estar ligada e travada pelo motivo da regra: {sw1}")
    ok(s1["build"] == {"disabled": False}, f"two: o botão de compilar com o Ângulo: {s1['build']}")
    ok(s1["summary"] == say("en", "result.changes1", p=label_of("pico2_w_release", "en")), f"two: o resumo é {s1['summary']!r}")
    ok(say("en", "warn.missing", list=ang_en) in [w["text"] for w in s1["warnings"]],
       f"two: sem o aviso de que a UI não tem medida: {[w['text'] for w in s1['warnings']]}")
    ok(s1["profile"] == f"python3 tools/build_custom.py --profile '{bc.canonical('pico2_w_release', {'ui': 'angulo'})}'",
       f"two: o perfil que a página manda compilar é {s1['profile']!r}")
    ok(s1["issue"] and f"- {M['ui_panel']['label']['en']}: {ang_en}" in s1["issue"]["body"].splitlines()
       and s1["issue"]["title"] == f"Build request: {label_of('pico2_w_release', 'en')}, 1 change",
       f"two: o pedido não leva a UI: {s1['issue']}")
    ok(s2 == s1, "two: um clique em controle travado mexeu na tela")
    ok(s3 == s0, f"two: ao voltar ao visual original a tela devia ser a de partida; hash {s3['hash']!r}, perfil {s3['profile']!r}")

    # 3. Sem o gráfico do painel o Ângulo está travado, com o mesmo motivo; ligado o gráfico, ele se liberta.
    r = by["nochart"]
    ok([st["result"] for st in r["steps"]] == ["disabled", "clicked", "clicked"], f"nochart: cliques {r['steps']}")
    s0, s1, s2, s3 = r["snaps"]
    ang = row(s0, "u-angulo")
    ok(ang["disabled"] and not ang["checked"] and ang["lock"] == say("en", "locked", why=chart["en"]),
       f"nochart: o Ângulo sem o gráfico devia estar travado pelo motivo da regra: {ang}")
    ok(not switch(s0, "t-tft_graph")["checked"] and not switch(s0, "t-tft_graph")["disabled"], "nochart: a chave do gráfico devia estar desligada e livre")
    ok(s1 == s0, "nochart: um clique em controle travado mexeu na tela")
    ok(s2["hash"] == "#b=pico2_w_release" and not row(s2, "u-angulo")["disabled"] and row(s2, "u-angulo")["lock"] is None,
       f"nochart: ligado o gráfico o Ângulo devia se libertar: {s2['hash']}, {row(s2, 'u-angulo')}")
    ok(s3["hash"] == "#b=pico2_w_release&ui=angulo" and switch(s3, "t-tft_graph")["disabled"], f"nochart: {s3['hash']}")

    # 4. O SIMUT Wire com tela touch (RP2040 com o cabo): o Ângulo é livre; escolhido, o Wi-Fi de volta trava
    #    pela regra do Pico W, que é a pergunta que a escolha da rede faz.
    r = by["wiretft"]
    ok([st["result"] for st in r["steps"]] == ["clicked", "disabled"], f"wiretft: cliques {r['steps']}")
    s0, s1, s2 = r["snaps"]
    ok(s0["hash"] == "#b=pico_wiretft" and not row(s0, "u-angulo")["disabled"] and row(s0, "u-original")["checked"], f"wiretft: partida {s0['hash']}")
    ok(not row(s0, "n-cyw43")["disabled"], "wiretft: antes do Ângulo o Wi-Fi não devia estar travado")
    prof = gf.resolve_profile("pico_wiretft", M["profiles"])
    nb = new_blockers(dict(prof, ui="angulo"), net="cyw43")
    ok(nb and nb[0]["id"] == "angulo-cyw43-needs-rp2350", f"wiretft: o Python devia travar o Wi-Fi com o Ângulo pela regra do Pico W: {[b['id'] for b in nb]}")
    ok(s1["hash"] == "#b=pico_wiretft&ui=angulo" and s1["build"] == {"disabled": False}, f"wiretft: {s1['hash']}, {s1['build']}")
    ok(row(s1, "n-cyw43")["disabled"] and row(s1, "n-cyw43")["lock"] == say("en", "locked", why=rule_why("angulo-cyw43-needs-rp2350")["en"]),
       f"wiretft: escolhido o Ângulo, o Wi-Fi devia travar pelo motivo da regra: {row(s1, 'n-cyw43')}")
    ok(s2 == s1, "wiretft: um clique em controle travado mexeu na tela")

    # 5. O produto do Ângulo: a UI dele é a do produto e não se escreve; trocar para o original é a mudança.
    r = by["twoangulo"]
    ok([st["result"] for st in r["steps"]] == ["clicked", "clicked"], f"twoangulo: cliques {r['steps']}")
    s0, s1, s2 = r["snaps"]
    ok(s0["hash"] == "#b=pico2_w_angulo" and row(s0, "u-angulo")["checked"] and not row(s0, "u-original")["disabled"]
       and not fieldset(s0, "visual")["changed"], f"twoangulo: partida {s0['hash']}")
    ok(s1["hash"] == "#b=pico2_w_angulo&ui=original" and row(s1, "u-original")["checked"] and fieldset(s1, "visual")["changed"],
       f"twoangulo: ao escolher o original o link é {s1['hash']!r}")
    ok(s2 == s0, "twoangulo: voltar ao Ângulo devia deixar a tela como a de partida")

    # 6. Um link com o Ângulo no Pico W: a página o mostra como está, bloqueado, com o motivo, e a saída
    #    (o visual original) livre; escolhê-la limpa o bloqueio.
    r = by["linkblocked"]
    s0, s1 = r["snaps"]
    ok(s0["hash"] == "#b=pico_w_release&ui=angulo", f"linkblocked: {s0['hash']!r}")
    ok(row(s0, "u-angulo")["checked"] and not row(s0, "u-angulo")["disabled"] and not row(s0, "u-original")["disabled"],
       f"linkblocked: a UI escolhida e a saída deviam estar livres: {row(s0, 'u-angulo')}, {row(s0, 'u-original')}")
    ok(s0["build"] == {"disabled": True} and s0["selos"][:1] == [say("en", "fit.blocked")], f"linkblocked: {s0['build']}, {s0['selos']}")
    texts = [w["text"] for w in s0["warnings"]]
    ok(say("en", "warn.rule", why=rule_why("angulo-cyw43-needs-rp2350")["en"]) in texts, f"linkblocked: sem o motivo da regra no aviso: {texts}")
    ok(s1["hash"] == "#b=pico_w_release" and s1["build"] == {"disabled": False}, f"linkblocked: ao escolher o original {s1['hash']}, {s1['build']}")
    ok(not any(w["text"].startswith(say("en", "warn.rule", why="")) for w in s1["warnings"]), "linkblocked: o aviso da regra ficou")

    # 7. Um link com uma UI que o modelo não conhece: ignorado, com o aviso de sempre, no produto padrão.
    s = by["linkunknown"]["snaps"][0]
    ok(s["notice"] == say("en", "err.hash", v="nonsense"), f"linkunknown: o aviso é {s['notice']!r}")
    ok(s["product"] == "pico_w_release" and s["hash"] == "#b=pico_w_release", f"linkunknown: abriu {s['product']} em {s['hash']!r}")

    # 8. Em português: o motivo da trava, o aviso de que não há medida e o pedido, que é sempre em inglês.
    r = by["ptflow"]
    s1 = r["snaps"][1]
    ang_pt = M["ui_panels"]["angulo"]["label"]["pt"]
    sw1 = switch(s1, "t-tft_graph")
    ok(sw1["disabled"] and sw1["locks"] == [say("pt", "locked", why=chart["pt"])], f"ptflow: a trava da chave do gráfico em português: {sw1}")
    ok(say("pt", "warn.missing", list=ang_pt) in [w["text"] for w in s1["warnings"]], "ptflow: sem o aviso de que a UI não tem medida, em português")
    ok(f"- {M['ui_panel']['label']['en']}: {M['ui_panels']['angulo']['label']['en']}" in (s1["issue"] or {"body": ""})["body"].splitlines(),
       "ptflow: o pedido devia dizer a UI em inglês")

    # 9. A marca "mudou" de uma linha mudada diz a palavra da língua da página, e só aparece onde há mudança: no
    #    título da UI e no da rede (a escolha está no `set`, e o `set` é o que o link leva) e na linha de cada chave.
    #    Lida em toda tela de toda visita; o link que diz o que mudou é lido aqui, de fora da página.
    for want in ui_scenarios():
        mark = say(want["lang"], "changed")
        ok(mark.strip(), f"{want['name']}: o texto 'changed' do app.js em {want['lang']} é vazio")
        for i, s in enumerate(by[want["name"]]["snaps"]):
            q = parse_qs(s["hash"].lstrip("#"))
            moved = {t for v in q.get("on", []) + q.get("off", []) for t in v.split(",") if t}
            for radio, key in (("visual", "ui"), ("rede", "net")):
                badges = [mark] if key in q else []
                ok(fieldset(s, radio)["badges"] == badges,
                   f"{want['name']}[{i}] {s['hash']}: a marca do título da escolha {key} é {fieldset(s, radio)['badges']!r}, devia ser {badges!r}")
            for sw in s["switches"]:
                badges = [mark] if sw["id"][2:] in moved else []
                ok(sw["badges"] == badges, f"{want['name']}[{i}] {s['hash']}: a marca da chave {sw['id']} é {sw['badges']!r}, devia ser {badges!r}")

    # 10. A rede mudada e de volta (o Wi-Fi no SIMUT Wire, e o cabo de novo): o link leva a rede só enquanto ela difere
    #     da do produto, o título diz "mudou" só então, e voltar deixa a tela igual à de partida. A chave, em português.
    r = by["netchange:en"]
    ok([st["result"] for st in r["steps"]] == ["clicked", "clicked"], f"netchange:en: cliques {r['steps']}")
    s0, s1, s2 = r["snaps"]
    ok(s0["hash"] == "#b=pico_wire" and row(s0, "n-enc28j60")["checked"] and not fieldset(s0, "rede")["changed"],
       f"netchange:en: partida {s0['hash']}")
    ok(s1["hash"] == "#b=pico_wire&net=cyw43" and row(s1, "n-cyw43")["checked"] and not row(s1, "n-enc28j60")["checked"]
       and fieldset(s1, "rede")["changed"], f"netchange:en: ao escolher o Wi-Fi o link é {s1['hash']!r}")
    ok(s2 == s0, "netchange:en: voltar ao cabo devia deixar a tela como a de partida")
    r = by["netchange:pt"]
    ok([st["result"] for st in r["steps"]] == ["clicked"] and r["snaps"][1]["hash"] == "#b=pico_wire&net=cyw43"
       and fieldset(r["snaps"][1], "rede")["changed"], f"netchange:pt: {r['steps']}, {r['snaps'][1]['hash']!r}")
    r = by["togglechange:pt"]
    ok([st["result"] for st in r["steps"]] == ["clicked"] and r["snaps"][0]["hash"] == "#b=pico2_w_release&off=tft_graph"
       and not switch(r["snaps"][0], "t-tft_graph")["checked"] and r["snaps"][1]["hash"] == "#b=pico2_w_release",
       f"togglechange:pt: {r['steps']}, {[x['hash'] for x in r['snaps']]}")

    # 11. A build pronta pertence à escolha que a pediu: dropFinishedRun( ) a tira da tela quando o leitor muda a
    #     UI, como quando muda a rede -- só a que terminou --, e uma build em curso fica, com o nome do que ela
    #     compila, até acabar. O pedido que foi ao GitHub é o perfil da escolha de antes.
    def stock_of(pid):
        return say("en", "run.for", what=say("en", "result.stock", p=label_of(pid, "en")))

    for name, pid, click, suffix in (("run:look", "pico2_w_release", "u-angulo", "&ui=angulo"),
                                     ("run:net", "pico_wire", "n-cyw43", "&net=cyw43")):
        r = by[name]
        ok([st["result"] for st in r["steps"]] == ["pressed", "clicked"], f"{name}: cliques {r['steps']}")
        s0, s1, s2 = r["snaps"]
        ok(s0["run"] is None and s0["build"] == {"disabled": False}, f"{name}: a tela de partida: {s0['run']}, {s0['build']}")
        ok(r["dispatched"] == [bc.canonical(pid, {})], f"{name}: o pedido que foi ao GitHub é {r['dispatched']}, devia ser o perfil de partida")
        done = s1["run"] or {"texts": [], "links": []}
        ok(done["texts"][:1] == [stock_of(pid)] and say("en", "run.ok", bin=f"{PAGE_API['bin']:,}", ram=f"{PAGE_API['ram']:,}") in done["texts"],
           f"{name}: a build pronta não está na tela, com o que pediu e os tamanhos: {s1['run']}")
        ok(f"https://github.com/{PAGE_API['repo']}/actions/runs/{PAGE_API['runId']}/artifacts/{PAGE_API['artifactId']}" in done["links"],
           f"{name}: a build pronta não tem o link do artefato: {done['links']}")
        ok(s1["build"] == {"disabled": False}, f"{name}: terminada a build o botão devia estar livre: {s1['build']}")
        ok(s2["hash"] == s0["hash"] + suffix, f"{name}: ao escolher {click} o link é {s2['hash']!r}")
        ok(s2["run"] is None, f"{name}: escolhido {click}, o resultado da build pronta, que é de outra escolha, continua na tela: {s2['run']}")
        ok(s2["build"] == {"disabled": False}, f"{name}: escolhido {click} o botão de compilar devia estar livre: {s2['build']}")
    r = by["run:busy"]
    ok([st["result"] for st in r["steps"]] == ["pressed", "clicked"], f"run:busy: cliques {r['steps']}")
    s0, s1, s2 = r["snaps"]
    going = {"texts": [stock_of("pico2_w_release"), say("en", "run.dispatching")], "links": []}
    ok(s1["run"] == going and s1["build"] == {"disabled": True}, f"run:busy: a build em curso: {s1['run']}, {s1['build']}")
    ok(s2["hash"] == "#b=pico2_w_release&ui=angulo" and s2["run"] == going and s2["build"] == {"disabled": True},
       f"run:busy: escolhida a UI, a build em curso devia ficar, com o nome do que ela compila: {s2['run']}, {s2['build']}")
    ok(s2["summary"] == say("en", "result.changes1", p=label_of("pico2_w_release", "en")),
       f"run:busy: o resumo da página devia ser o da escolha nova: {s2['summary']!r}")

    # 12. O preço da linha da UI (C1d, 2026-10-29), no formato da linha da rede (L.priceNet): "41.0 KB" na linha e a
    #     frase no título, só na linha livre que não é a do produto. A linha travada, a do produto e a sem medida
    #     não mostram nada; com a medida, escolher o Ângulo não deixa o aviso de "sem medida" na tela. Cada
    #     conferência traz também a literal esperada: a de um instrumento contra ele mesmo não prova nada.
    for name, lang, key in (("price:measured", "en", "measured"), ("price:measured:pt", "pt", "measured"), ("price:smaller", "en", "smaller"),
                            ("price:overflow", "en", "overflow"), ("price:rule", "en", "measured"), ("price:unmeasured", "en", "unmeasured")):
        sc = next(w for w in ui_scenarios() if w["name"] == name)
        pid = sc["hash"][1:].split("&")[0].split("=")[1]
        for u in UIS:
            x = row(by[name]["snaps"][0], f"u-{u}")
            want = expected_ui_price(MODELS[key], pid, u, lang)
            ok((x["cost"], x["costTitle"]) == want, f"{name}/ui={u}: preço {x['cost']!r} com o título {x['costTitle']!r}, devia ser {want}")
    s0, s1 = by["price:measured"]["snaps"]
    a0 = row(s0, "u-angulo")
    ok(a0["cost"] == "41.0 KB" and "41,984 B" in (a0["costTitle"] or "") and not a0["disabled"],
       f"price:measured: a linha do Ângulo mostra {a0['cost']!r} com o título {a0['costTitle']!r}, devia ser 41.0 KB e 41,984 B")
    ok(row(s0, "u-original")["cost"] is None, "price:measured: a UI do próprio produto não devia ter preço")
    ok(row(s1, "u-angulo")["checked"] and row(s1, "u-angulo")["cost"] == "41.0 KB",
       "price:measured: escolhido o Ângulo o preço continua na linha (é o de escolhê-lo)")
    texts = [w["text"] for w in s1["warnings"]]
    ok(say("en", "warn.missing", list=ang_en) not in texts, f"price:measured: com a medida na matriz o aviso de 'sem medida' não devia estar na tela: {texts}")
    ok(row(by["price:measured:pt"]["snaps"][0], "u-angulo")["cost"] == "41,0 KB", "price:measured:pt: o preço em português é 41,0 KB")
    a = row(by["price:smaller"]["snaps"][0], "u-angulo")
    ok(a["cost"] == "−1.0 KB" and "1,024 B" in (a["costTitle"] or ""), f"price:smaller: {a['cost']!r}, {a['costTitle']!r}")
    a = row(by["price:overflow"]["snaps"][0], "u-angulo")
    ok(a["cost"] is not None and a["costTitle"] == say("en", "price.ui.over"), f"price:overflow: {a['cost']!r}, {a['costTitle']!r}")
    a = row(by["price:rule"]["snaps"][0], "u-angulo")
    ok(a["disabled"] and a["cost"] is None and a["lock"] == say("en", "locked", why=rule_why("angulo-cyw43-needs-rp2350")["en"]),
       f"price:rule: o Ângulo no Pico W devia estar travado pela regra e sem preço: {a}")
    ok([st["result"] for st in by["price:rule"]["steps"]] == ["disabled"], f"price:rule: cliques {by['price:rule']['steps']}")
    a = row(by["price:locked"]["snaps"][0], "u-angulo")
    ok(a["disabled"] and a["cost"] is None and a["costTitle"] is None and a["lock"] == say("en", "locked", why=chart["en"])
       and expected_ui_price(MODELS["measured"], "pico2_w_release", "angulo", "en", {"tft_graph": False}) == (None, None),
       f"price:locked: sem o gráfico a opção está travada e a linha travada não mostra o preço que a matriz mediu: {a}")
    s0 = by["price:unmeasured"]["snaps"][0]
    ok(row(s0, "u-angulo")["cost"] is None and say("en", "warn.missing", list=ang_en) in [w["text"] for w in s0["warnings"]],
       f"price:unmeasured: sem a entrada na matriz a linha não mostra preço e o aviso de 'sem medida' fica: {row(s0, 'u-angulo')['cost']!r}, {[w['text'] for w in s0['warnings']]}")

    # 13. O que o leitor de tela recebe de cada CHAVE (C1d, 2026-10-29): a descrição (aria-describedby) é a ajuda e, depois
    #     dela, toda linha de motivo que a chave mostra -- a trava e a frase de "não muda nada" -- na ordem da tela. Antes
    #     só a ajuda: o motivo de uma chave travada estava na tela e em descrição nenhuma. Cada id citado existe uma vez
    #     só ("missing" e "duplicate" são achados). Quatro classes de chave, e cada uma tem de aparecer para a conferência
    #     valer: a travada, a sem efeito, a que é as duas e a livre (só a ajuda).
    seen = {"locked": 0, "noeffect": 0, "both": 0, "free": 0}
    for name, lang in [(f"sweep:{lg}:{p}", lg) for lg in ("en", "pt") for p in PRODUCTS] + [("noeffect:release", "en"), ("noeffect:air:pt", "pt")]:
        prefix, noeff = say(lang, "locked", why=""), say(lang, "noeffect")
        for sw in by[name]["snaps"][0]["switches"]:
            help_text = M["ui_toggles"][sw["id"][2:]]["help"][lang]
            has_lock, has_same = any(x.startswith(prefix) for x in sw["locks"]), noeff in sw["locks"]
            kind = "both" if has_lock and has_same else "locked" if has_lock else "noeffect" if has_same else "free"
            seen[kind] += 1
            ok(sw["help"] == help_text, f"{name}/{sw['id']}: a ajuda na tela é {sw['help']!r}, devia ser a do manifesto")
            ok(sw["description"] == [help_text] + sw["locks"],
               f"{name}/{sw['id']} ({kind}): a descrição que o leitor de tela recebe (aria-describedby={sw['describedbyAttr']!r}) é "
               f"{sw['description']}, devia ser a ajuda e depois {sw['locks']}")
    ok(all(seen.values()), f"as classes de chave lidas (cada uma devia aparecer): {seen}")
    return fs


def ui_source_findings(app_text):
    """O que só se vê no texto do app.js: o produto passa a toda trava de UI, sem o qual a recusa por chip some
    sem erro nenhum."""
    fs = []
    calls = re.findall(r"L\.uiLockOf\(([^()]*)\)", app_text)
    if not (len(calls) == 1 and len([a for a in calls[0].split(",") if a.strip()]) == 4):
        fs.append(f"app.js: a escolha da UI deveria chamar L.uiLockOf uma vez, com o produto (4 argumentos): {calls}")
    calls = re.findall(r"L\.priceUi\(([^()]*)\)", app_text)
    if not (len(calls) == 1 and len([a for a in calls[0].split(",") if a.strip()]) == 3):
        fs.append(f"app.js: a linha da UI deveria pedir o preço a L.priceUi uma vez, com o produto (3 argumentos): {calls}")
    return fs


def ui_run(logic_text=None, app_text=None):
    """A lógica e a página, conferidas: a lista de achados e as telas de cada visita."""
    fs = ui_logic_findings(run_node(JS_UI, {"model": MODEL, "products": PRODUCTS, "toggles": TOGGLES, "uis": UIS,
                                            "contexts": CONTEXTS, "hostile": list(HOSTILE_UI), "fixtures": MODELS}, logic_text=logic_text))
    fs += ui_source_findings(APP if app_text is None else app_text)
    res = run_node(JS_APP, {"models": MODELS, "scenarios": ui_scenarios(), "api": PAGE_API}, logic_text=logic_text, app_text=app_text)
    if isinstance(res, dict):
        return fs + [f"o node falhou na página: {res['node_error']}"], []
    return fs + ui_app_findings(res), res


ui_findings, ui_visits = ui_run()
failures.extend(ui_findings)
if os.environ.get("SIMUT_PAGE_SNAPSHOTS"):   # para quem quer ver as telas: a lista de visitas, em JSON
    with open(os.environ["SIMUT_PAGE_SNAPSHOTS"], "w", encoding="utf-8") as fh:
        json.dump(ui_visits, fh, ensure_ascii=False, indent=1)

# 13c. Os controles: uma quebra plantada numa cópia da página, uma de cada vez, e as conferências acima têm
#      de acusá-la. Sem isto uma conferência que nunca morde passaria calada (a âncora tem de aparecer uma
#      vez só, para a quebra ser a que se diz). Os marcados "(instrumento)" quebram a escolha da rede, a chave
#      ou a build em curso, que já existiam: é a prova de que o instrumento (o DOM de mentira) lê a trava de
#      uma escolha de driver, a marca de uma chave e o que o leitor de tela recebe da rede, como da UI. Os dez
#      B1 a B10 são as quebras que a revisão da C1b plantou à mão (2026-10-22): cinco (B3, B4, B5, B9, B10)
#      passavam por todas as conferências da época, e duas delas (B3, B10) só davam erro porque a edição mexia
#      na âncora de outro controle -- agora cada uma é acusada pela leitura de uma tela.
with open(os.path.join(PAGE, "logic.js"), encoding="utf-8") as fh:
    LOGIC = fh.read()
CONTROLS = [
    ("normalizeSet esquece a UI", "logic",
     '  if (Object.prototype.hasOwnProperty.call(set, "ui") && set.ui !== base.ui) out.ui = set.ui;\n', ""),
    ("normalizeSet guarda a UI do próprio produto", "logic",
     'hasOwnProperty.call(set, "ui") && set.ui !== base.ui) out.ui = set.ui;', 'hasOwnProperty.call(set, "ui")) out.ui = set.ui;'),
    ("formatHash trata ui como interruptor", "logic", 'k !== "net" && k !== "ui"', 'k !== "net"'),
    ("formatHash não escreve a UI", "logic", "  if (set.ui) h += `&ui=${set.ui}`;\n", ""),
    ("parseHash ignora ui", "logic", '  if (params.has("ui")) {', "  if (false) {"),
    ("parseHash aceita qualquer ui", "logic",
     'if (!ID.test(u) || !model.ui.options.some((o) => o.id === u)) return { error: "ui", value: u };',
     'if (!ID.test(u)) return { error: "ui", value: u };'),
    ("parseHash aceita ui duas vezes", "logic", '    if (params.getAll("ui").length > 1) return { error: "twice", value: "ui" };\n', ""),
    ("uiLockOf nunca trava", "logic",
     '  return choiceLockOf(model, config, "ui", uiId, productId);', "  return null;"),
    ("uiLockOf pergunta pela rede", "logic",
     '  return choiceLockOf(model, config, "ui", uiId, productId);', '  return choiceLockOf(model, config, "net", uiId, productId);'),
    ("a trava conta o bloqueio que a escolha já tem", "logic",
     "{ ...config, [key]: value }).find((x) => !now.has(x.id))", "{ ...config, [key]: value }).find(() => true)"),
    ("netLockOf nunca trava", "logic",
     '  return choiceLockOf(model, config, "net", netId, productId);', "  return null;"),
    ("renderFeatures não desenha a UI", "app",
     "    const ui = g.id === m.ui.group ? renderUi(config) : null;", "    const ui = null;"),
    ("a UI vai para o grupo da rede", "app", "g.id === m.ui.group ? renderUi", "g.id === m.net.group ? renderUi"),
    ("a UI vem depois das chaves", "app",
     'return h("div", { class: "cfg-bloco" }, h("h3", { class: "cfg-bloco-titulo", text: title }), net, ui, rows);',
     'return h("div", { class: "cfg-bloco" }, h("h3", { class: "cfg-bloco-titulo", text: title }), net, rows, ui);'),
    ("a opção recusada continua habilitada", "app",
     "checked: config.ui === o.id, disabled: Boolean(lock),", "checked: config.ui === o.id,"),
    ("a trava da UI não mostra o motivo", "app",
     '''          lock ? h("span", { class: "cfg-chave-trava", id: lockId, text: t("locked", { why: label(lock.why) }) }) : null),
        h("span", { class: "cfg-chave-lado" },
          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,
          h("input", { type: "radio", class: "cfg-radio", name: "visual",''',
     '''          null),
        h("span", { class: "cfg-chave-lado" },
          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,
          h("input", { type: "radio", class: "cfg-radio", name: "visual",'''),
    ("a marcada não é a escolha de agora", "app", "checked: config.ui === o.id", "checked: state.set.ui === o.id"),
    ("missingName não sabe ui", "app",
     '''  if (id === "ui") {
    const o = m.ui.options.find((x) => x.id === state.set.ui);
    return label(o ? o.label : m.ui.label);
  }
''', ""),
    ("changeLines não sabe ui", "app",
     '''    if (id === "ui") {
      const o = m.ui.options.find((y) => y.id === v);
      return `- ${m.ui.label.en}: ${o ? o.label.en : v}`;
    }
''', ""),
    ("chooseUi não normaliza", "app",
     "  const next = { ...state.set, ui: uiId };\n  state.set = L.normalizeSet(state.model, state.productId, next);",
     "  const next = { ...state.set, ui: uiId };\n  state.set = next;"),
    ("chooseUi não escreve o link", "app",
     "  state.set = L.normalizeSet(state.model, state.productId, next);\n  dropFinishedRun();\n  writeHash();\n  render();\n}\n\n/* ---- rendering",
     "  state.set = L.normalizeSet(state.model, state.productId, next);\n  dropFinishedRun();\n  render();\n}\n\n/* ---- rendering"),
    ("a trava da UI sem o produto", "app", "L.uiLockOf(m, config, o.id, state.productId)", "L.uiLockOf(m, config, o.id)"),
    ("o título da UI não diz mudou", "app", '  const changed = "ui" in state.set;', "  const changed = false;"),
    ("a trava da rede não mostra o motivo (instrumento)", "app",
     '''          lock ? h("span", { class: "cfg-chave-trava", id: lockId, text: t("locked", { why: label(lock.why) }) }) : null),
        h("span", { class: "cfg-chave-lado" },
          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,
          h("input", { type: "radio", class: "cfg-radio", name: "rede",''',
     '''          null),
        h("span", { class: "cfg-chave-lado" },
          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,
          h("input", { type: "radio", class: "cfg-radio", name: "rede",'''),
    ("o rádio da rede não desabilita (instrumento)", "app",
     "checked: config.net === o.id, disabled: Boolean(lock),", "checked: config.net === o.id,"),
    # As dez quebras da revisão (C1b-b). A âncora de cada uma é a da revisão; a quebra é acusada pela leitura de
    # uma tela (o nome, a descrição, o grupo, a marca, a build), e não por uma âncora de outro controle.
    ("B1 a trava da UI diz um motivo vazio", "app",
     'lock ? h("span", { class: "cfg-chave-trava", id: lockId, text: t("locked", { why: label(lock.why) }) }) : null),\n        h("span", { class: "cfg-chave-lado" },\n          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,\n          h("input", { type: "radio", class: "cfg-radio", name: "visual",',
     'lock ? h("span", { class: "cfg-chave-trava", id: lockId, text: t("locked", { why: "" }) }) : null),\n        h("span", { class: "cfg-chave-lado" },\n          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,\n          h("input", { type: "radio", class: "cfg-radio", name: "visual",'),
    ("B2 o rádio da UI nunca é desabilitado", "app",
     "checked: config.ui === o.id, disabled: Boolean(lock),", "checked: config.ui === o.id, disabled: Boolean(lock) && false,"),
    ("B3 o segundo rádio da UI sai do grupo", "app",
     'name: "visual", id, value: o.id,', 'name: o.id === "original" ? "visual" : "visual2", id, value: o.id,'),
    ("B4 a descrição do rádio da UI perde a trava", "app",
     '"aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui',
     '"aria-describedby": helpId,\n                       checked: config.ui'),
    ("B5 o nome do rádio da UI aponta para um id que não existe", "app",
     '"aria-labelledby": `${id}-nome`, "aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui',
     '"aria-labelledby": `${id}-nom`, "aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui'),
    ("B6 a UI do próprio produto vai para o link", "logic",
     'if (Object.prototype.hasOwnProperty.call(set, "ui") && set.ui !== base.ui) out.ui = set.ui;',
     'if (Object.prototype.hasOwnProperty.call(set, "ui") && set.ui !== undefined) out.ui = set.ui;'),
    ("B7 parseHash aceita um id de rede como UI", "logic",
     "model.ui.options.some((o) => o.id === u)",
     "model.net.options.some((o) => o.id === u) || model.ui.options.some((o) => o.id === u)"),
    ("B8 a trava da opção é a do último bloqueio novo", "logic",
     ".find((x) => !now.has(x.id)) || null;\n  return b ? { id: b.id, why: b.why } : null;",
     ".filter((x) => !now.has(x.id)).pop() || null;\n  return b ? { id: b.id, why: b.why } : null;"),
    ("B9 a marca 'mudou' da UI sai sem texto", "app",
     'changed ? [" ", h("span", { class: "selo", text: t("changed") })] : null),\n    m.ui.options.map',
     'changed ? [" ", h("span", { class: "selo", text: "" })] : null),\n    m.ui.options.map'),
    ("B10 chooseUi deixa a build pronta na tela", "app",
     "  const next = { ...state.set, ui: uiId };\n  state.set = L.normalizeSet(state.model, state.productId, next);\n  dropFinishedRun();",
     "  const next = { ...state.set, ui: uiId };\n  state.set = L.normalizeSet(state.model, state.productId, next);"),
    # O que B4 e B5 deixam solto: um id que existe mas é de outro elemento (a ajuda no lugar do título), e a
    # trava citada na descrição de uma opção que não tem trava (o id não existe).
    ("o nome do rádio da UI é a ajuda, que existe", "app",
     '"aria-labelledby": `${id}-nome`, "aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui',
     '"aria-labelledby": helpId, "aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui'),
    ("a descrição do rádio da UI cita a trava que a opção livre não tem", "app",
     '"aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui',
     '"aria-describedby": `${helpId} ${lockId}`,\n                       checked: config.ui'),
    ("a trava da UI repete o id da ajuda", "app",
     "const lock = L.uiLockOf(m, config, o.id, state.productId);\n      const helpId = `${id}-ajuda`, lockId = `${id}-trava`;",
     "const lock = L.uiLockOf(m, config, o.id, state.productId);\n      const helpId = `${id}-ajuda`, lockId = helpId;"),
    # A ordem da descrição (a ajuda, depois o motivo da trava): a quebra que a revisão do B2a achou (2026-10-22), `lockId helpId` no lugar de
    # `helpId lockId`, passava por todas as conferências, que liam o conjunto e não a ordem.
    ("a descrição do rádio da UI lê a trava antes da ajuda", "app",
     '"aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.ui',
     '"aria-describedby": lock ? `${lockId} ${helpId}` : helpId,\n                       checked: config.ui'),
    ("a descrição do rádio da rede lê a trava antes da ajuda", "app",
     '"aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.net',
     '"aria-describedby": lock ? `${lockId} ${helpId}` : helpId,\n                       checked: config.net'),
    # Os mesmos achados, na escolha da rede, na chave e na build em curso: sem estes, uma conferência que
    # lê as duas escolhas poderia estar lendo só uma.
    ("o segundo rádio da rede sai do grupo (instrumento)", "app",
     'name: "rede", id, value: o.id,', 'name: o.id === "cyw43" ? "rede" : "rede2", id, value: o.id,'),
    ("a descrição do rádio da rede perde a trava (instrumento)", "app",
     '"aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.net',
     '"aria-describedby": helpId,\n                       checked: config.net'),
    ("o nome do rádio da rede aponta para um id que não existe (instrumento)", "app",
     '"aria-labelledby": `${id}-nome`, "aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.net',
     '"aria-labelledby": `${id}-nom`, "aria-describedby": lock ? `${helpId} ${lockId}` : helpId,\n                       checked: config.net'),
    ("a marca 'mudou' da rede sai sem texto (instrumento)", "app",
     'changed ? [" ", h("span", { class: "selo", text: t("changed") })] : null),\n    m.net.options.map',
     'changed ? [" ", h("span", { class: "selo", text: "" })] : null),\n    m.net.options.map'),
    ("a marca 'mudou' da chave sai sem texto (instrumento)", "app",
     'changed ? h("span", { class: "selo", text: t("changed") }) : null),\n      h("p", { class: "cfg-apoio", id: helpId',
     'changed ? h("span", { class: "selo", text: "" }) : null),\n      h("p", { class: "cfg-apoio", id: helpId'),
    ("chooseNet deixa a build pronta na tela (instrumento)", "app",
     "  const next = { ...state.set, net: netId };\n  state.set = L.normalizeSet(state.model, state.productId, next);\n  dropFinishedRun();",
     "  const next = { ...state.set, net: netId };\n  state.set = L.normalizeSet(state.model, state.productId, next);"),
    ("dropFinishedRun larga também a build em curso (instrumento)", "app",
     "  if (state.run && state.run.done) state.run = null;", "  state.run = null;"),
    # O preço da UI (C1d, 2026-10-29): a conta (logic), a linha (app) e as frases do título.
    ("priceUi nunca dá preço", "logic", '  return choicePrice(model, productId, "ui", uiId);', "  return null;"),
    ("priceUi pergunta pela chave da rede", "logic",
     '  return choicePrice(model, productId, "ui", uiId);', '  return choicePrice(model, productId, "net", uiId);'),
    ("o preço da escolha conta a do próprio produto", "logic",
     "  if (value === productById(model, productId).config[key]) return null;\n", ""),
    ("a linha da UI não mostra o preço", "app",
     '          priceText ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,\n          h("input", { type: "radio", class: "cfg-radio", name: "visual",',
     '          null,\n          h("input", { type: "radio", class: "cfg-radio", name: "visual",'),
    ("a linha travada da UI mostra o preço", "app",
     '      if (price && !lock) {\n        priceText = kb(price.flash);\n        priceTitle = price.overflow ? t("price.ui.over")',
     '      if (price) {\n        priceText = kb(price.flash);\n        priceTitle = price.overflow ? t("price.ui.over")'),
    ("o preço da UI sai em bytes e não em KB", "app",
     '        priceText = kb(price.flash);\n        priceTitle = price.overflow ? t("price.ui.over")',
     '        priceText = String(price.flash);\n        priceTitle = price.overflow ? t("price.ui.over")'),
    ("o título do preço da UI é sempre 'maior'", "app",
     't(price.flash >= 0 ? "price.ui.more" : "price.ui.less", {', 't("price.ui.more", {'),
    ("o título do estouro da UI usa a frase da rede", "app", 'price.overflow ? t("price.ui.over")', 'price.overflow ? t("price.net.over")'),
    # O que o leitor de tela recebe de uma chave (C1d, 2026-10-29): a descrição, e os ids das linhas de motivo.
    ("a descrição da chave perde o motivo da trava", "app",
     '  const described = [helpId, lock ? lockId : null, same ? sameId : null].filter(Boolean).join(" ");',
     '  const described = [helpId, same ? sameId : null].filter(Boolean).join(" ");'),
    ("a descrição da chave perde a frase de sem efeito", "app",
     '  const described = [helpId, lock ? lockId : null, same ? sameId : null].filter(Boolean).join(" ");',
     '  const described = [helpId, lock ? lockId : null].filter(Boolean).join(" ");'),
    ("a descrição da chave lê o motivo antes da ajuda", "app",
     '  const described = [helpId, lock ? lockId : null, same ? sameId : null].filter(Boolean).join(" ");',
     '  const described = [lock ? lockId : null, helpId, same ? sameId : null].filter(Boolean).join(" ");'),
    ("a descrição da chave só tem a ajuda", "app",
     '  const described = [helpId, lock ? lockId : null, same ? sameId : null].filter(Boolean).join(" ");', '  const described = helpId;'),
    ("a descrição da chave cita a trava que a chave livre não tem", "app",
     '  const described = [helpId, lock ? lockId : null, same ? sameId : null].filter(Boolean).join(" ");',
     '  const described = [helpId, lockId, sameId].join(" ");'),
    ("a trava da chave não tem id", "app",
     'lock ? h("p", { class: "cfg-chave-trava", id: lockId, text: t("locked", { why: label(lock.why) }) }) : null,\n      same',
     'lock ? h("p", { class: "cfg-chave-trava", text: t("locked", { why: label(lock.why) }) }) : null,\n      same'),
    ("a frase de sem efeito não tem id", "app",
     'h("p", { class: "cfg-chave-trava", id: sameId, text: t("noeffect") })', 'h("p", { class: "cfg-chave-trava", text: t("noeffect") })'),
    ("o id da trava da chave é o da ajuda", "app",
     '  const helpId = `${id}-ajuda`, lockId = `${id}-trava`, sameId = `${id}-sem-efeito`;',
     '  const helpId = `${id}-ajuda`, lockId = helpId, sameId = `${id}-sem-efeito`;'),
    ("o id da trava é o mesmo em toda chave", "app",
     '  const helpId = `${id}-ajuda`, lockId = `${id}-trava`, sameId = `${id}-sem-efeito`;',
     '  const helpId = `${id}-ajuda`, lockId = "trava", sameId = `${id}-sem-efeito`;'),
]
control_hits = {}
for cname, which, old, new in CONTROLS:
    src = LOGIC if which == "logic" else APP
    if src.count(old) != 1:
        failures.append(f"controle '{cname}': a âncora aparece {src.count(old)} vez(es) em {which}.js, devia ser uma")
        continue
    broken = src.replace(old, new)
    got, _ = ui_run(logic_text=broken if which == "logic" else None, app_text=broken if which == "app" else None)
    control_hits[cname] = len(got)
    check(got, f"controle '{cname}': a quebra plantada em {which}.js não foi acusada por nenhuma conferência")
if os.environ.get("SIMUT_PAGE_CONTROLS"):   # para quem quer ver quantos achados cada quebra dá
    with open(os.environ["SIMUT_PAGE_CONTROLS"], "w", encoding="utf-8") as fh:
        json.dump(control_hits, fh, ensure_ascii=False, indent=1)


if failures:
    print("test_configurator_page: FALHOU")
    for f in failures[:40]:
        print("  -", f)
    if len(failures) > 40:
        print(f"  ... e mais {len(failures) - 40}")
    sys.exit(1)
print(f"test_configurator_page: {len(sets)} perfis e links, {len(HOSTILE_HASHES)} links hostis, "
      f"{len(js['singles'])} chaves medidas, {len(js['locks'])} travas, {len(js['nets'])} opções de rede; "
      f"{len(ui_visits)} visitas à página, {len(UIS)} UIs, {len(CONTROLS)} quebras plantadas acusadas; "
      f"erro medido {max(miss_used, miss_bin)} B em {n} conferências, margem {js['margin']} B.")
