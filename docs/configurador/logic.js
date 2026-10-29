/* logic.js — what the configurator works out, with no page around it.
 *
 * Everything here is a pure function of model.json (tools/gen_features.py
 * writes it from tools/features.toml) and of the choice on screen: which
 * product, and which switches and which network driver differ from that
 * product's defaults. No DOM, no network, so tools/test_configurator_page.py
 * runs it under node.
 *
 * The network is not a switch but a choice of driver (2026-10-13): `net` rides
 * in `set` next to the switches, as the driver's id ("cyw43", "enc28j60"), and
 * the cost matrix names it "net=<id>", the way model.json names a refusal.
 *
 * The look of the touch display is a choice of driver too (2026-10-22): `ui`
 * rides in `set` the same way, as the id of a [ui.*] of the manifest ("original",
 * "angulo"), the product's own look is not a change, and the rules that refuse
 * the Ângulo (on the Pico W with Wi-Fi, without the touch display, without the
 * panel chart) lock the option with their reason, the way a refused network
 * driver is locked. The cost matrix names the look's flip "ui=<id>", as it does
 * the network's, and the row shows its price (priceUi) the way a driver's row does;
 * while the matrix has no such entry the estimate leaves the look out and says so
 * (`missing`), and the row shows no price.
 *
 * The estimate is the measured cost matrix (tools/measure_savings.py --matrix):
 * each published product built once as shipped and once per switch flipped.
 * Two or more changes are SUMMED, which is an estimate and is labelled one;
 * the build measures the real image. */

import { matches, ruleViolations } from "./rules.js";

/* With Bluetooth linked, the .bin grows in 4 KB steps (its flash bank is
 * page-aligned), so no margin is smaller than one step. */
const BIN_STEP = 4096;

export function productById(model, id) {
  return model.products.find((p) => p.id === id) || null;
}

/* The two ceilings a product is held to: its flash slot, and the largest .bin
 * that still fits over the air once signed. Each product has its own, the
 * figures tools/check_flash_budget.py holds the images to: the Pico 2 W's slot
 * is 1,568,768 B for both, the Pico W's sketch area and OTA ceiling are
 * smaller. */
export function ceilingsOf(model, productId) {
  return productById(model, productId).ceilings;
}

/* The choice on screen: the product's configuration with `set` on top. */
export function effectiveConfig(model, productId, set) {
  const p = productById(model, productId);
  return { ...p.config, ...set };
}

/* `set` keeps only what differs from the product: a switch set back to its
 * default is not a change, and neither is the product's own network driver or
 * its own look of the touch display; the link and the profile should not carry
 * any of them. */
export function normalizeSet(model, productId, set) {
  const base = productById(model, productId).config;
  const out = {};
  if (Object.prototype.hasOwnProperty.call(set, "net") && set.net !== base.net) out.net = set.net;
  if (Object.prototype.hasOwnProperty.call(set, "ui") && set.ui !== base.ui) out.ui = set.ui;
  for (const t of model.toggles) {
    if (Object.prototype.hasOwnProperty.call(set, t.id) && set[t.id] !== base[t.id]) out[t.id] = set[t.id];
  }
  return out;
}

function costsOf(model, productId) {
  return (model.costs && model.costs.products && model.costs.products[productId]) || null;
}

/* What flipping one switch did to the product's image, as measured. */
export function flipOf(model, productId, toggleId) {
  const c = costsOf(model, productId);
  return c ? c.flips[toggleId] || null : null;
}

/* The cost matrix's name for one entry of `set`: a switch is its own id (every
 * entry is the product's switch turned over), and a choice is "key=value", as
 * model.json names a refusal and the manifest a condition ("net=enc28j60",
 * "ui=angulo"). */
function flipKey(key, value) {
  return typeof value === "boolean" ? key : `${key}=${value}`;
}

/* Bytes of flash the flip adds (negative: saves). An overflow is measured
 * too: the linker reports by how much the image's extent passed its FLASH
 * region, and that extent is the .bin. On the Pico W the .bin is 12 to 15 KB
 * more than `used` (the boot stage, the OTA stub, Bluetooth's 4 KiB page), so
 * the flip's .bin is the region plus the overflow, and its `used` is taken to
 * move by as much. Measured 2026-10-12: mDNS alone on the Air overflowed by
 * 3,148 B, and 1,044,480 + 3,148 - 1,031,244 = 16,384, the four 4 KiB pages
 * its 15,936 B take; counted from `used`, the sum missed a real build by
 * 31,352 B. */
function flipDelta(model, productId, f) {
  const base = costsOf(model, productId).base;
  if (!f) return null;
  if ("used" in f) {
    return { used: f.used - base.used, bin: f.bin - base.bin, ram: f.ram - base.ram };
  }
  if ("overflow" in f) {
    const bin = ceilingsOf(model, productId).flash_region + f.overflow - base.bin;
    return { used: bin, bin, ram: null };
  }
  return null;
}

/* The price of having the switch ON in this product, whatever its state now:
 * a stable number beside each switch. Negative when ON saves bytes. */
export function priceOn(model, productId, toggleId) {
  const f = flipOf(model, productId, toggleId);
  if (!f || f.same || f.rule || f.error) return null;
  const d = flipDelta(model, productId, f);
  if (!d) return null;
  const onByDefault = productById(model, productId).config[toggleId] === true;
  return { flash: onByDefault ? -d.used : d.used, overflow: "overflow" in f };
}

/* The price of choosing a driver over the product's own, as the matrix measured
 * it ("net=<id>", "ui=<id>"): bytes of flash the choice adds, negative when it
 * saves. null when there is nothing to show: it is the product's own choice, no
 * measurement, or the matrix found a rule, an error or no board there. */
function choicePrice(model, productId, key, value) {
  if (value === productById(model, productId).config[key]) return null;
  const f = flipOf(model, productId, flipKey(key, value));
  if (!f || f.same || f.rule || f.error || f.unsupported) return null;
  const d = flipDelta(model, productId, f);
  if (!d) return null;
  return { flash: d.used, overflow: "overflow" in f };
}

/* The price of a network driver. */
export function priceNet(model, productId, netId) {
  return choicePrice(model, productId, "net", netId);
}

/* The price of a look of the touch display (2026-10-29): the Ângulo's over the
 * product's own. The same answer as a driver's; null until the matrix has it. */
export function priceUi(model, productId, uiId) {
  return choicePrice(model, productId, "ui", uiId);
}

/* A choice the generator refuses for this product's chip: it asks for a board
 * the chip does not have (the cable module on the Pico 2 W). model.json lists
 * them per product, `refused`, keyed "key=value" ("net=enc28j60") with the
 * reason in each language, the way a rule carries `why`; the lock has the same
 * shape as a rule's, `id` and `why`. */
function refusalOf(model, productId, key, value) {
  const p = productId ? productById(model, productId) : null;
  const why = p && p.refused ? p.refused[`${key}=${value}`] : null;
  return why ? { id: `unsupported-${key}`, why } : null;
}

/* Why a switch cannot move: flipping it would add something that stops the
 * build, which the current choice does not already have. The first such thing,
 * in blockersOf's order: a rule, then the network driver the generator refuses
 * for the product's chip, then the PIO and pin budget. Returns {id, why}, or
 * null. A budget lock says what else to turn off: its words leave this switch
 * out of the list (the generator writes them, model.json `lock`). */
export function lockOf(model, config, toggleId, productId) {
  const now = new Set(blockersOf(model, productId, config).map((b) => b.id));
  const flipped = { ...config, [toggleId]: !config[toggleId] };
  const b = blockersOf(model, productId, flipped).find((x) => !now.has(x.id)) || null;
  return b && b.lock && b.lock[toggleId] ? { id: b.id, why: b.lock[toggleId] } : b;
}

/* Why a choice of driver cannot be taken: the same question as lockOf, asked of
 * the choice. The first blocker of this choice with `key` set to `value` that
 * the current choice does not already have, in blockersOf's order; {id, why} or
 * null. The chosen one is never locked (it adds nothing). A budget blocker's
 * remedy is a list of switches to turn off and a choice is never in it, so the
 * lock shows the generator's whole text. */
function choiceLockOf(model, config, key, value, productId) {
  const now = new Set(blockersOf(model, productId, config).map((b) => b.id));
  const b = blockersOf(model, productId, { ...config, [key]: value }).find((x) => !now.has(x.id)) || null;
  return b ? { id: b.id, why: b.why } : null;
}

/* Why a network driver cannot be chosen. */
export function netLockOf(model, config, netId, productId) {
  return choiceLockOf(model, config, "net", netId, productId);
}

/* Why a look of the touch display cannot be chosen (2026-10-22). The Ângulo is
 * refused by three rules of the manifest, each with its reason: on the Pico W
 * with Wi-Fi, without the touch display, and without the panel chart. The last
 * has two ends and the page locks whichever move comes first: with the chart
 * switch off this locks the Ângulo, and with the Ângulo chosen lockOf locks the
 * chart switch on, because it asks the same question of the switch. */
export function uiLockOf(model, config, uiId, productId) {
  return choiceLockOf(model, config, "ui", uiId, productId);
}

/* The network driver this choice has that the generator refuses for the
 * product's chip. The page cannot choose one that is locked, but a link can
 * carry it, and the build would be refused: it counts with the broken rules and
 * stops the build button. No switch is refused any more: a chip lacks a board,
 * and the board belongs to the driver. */
export function refusalsIn(model, productId, config) {
  const r = refusalOf(model, productId, "net", config.net);
  return r ? [r] : [];
}

/* The PIO and pin budget (tools/pio_budget.py), as data: model.json lists, for
 * each chip, every combination of the switches the budget reads that does not
 * fit, with the reason in each language. The page matches the choice against
 * them the way it matches a rule, and counts no block: the counting lives in one
 * place, and tools/test_configurator_rules.py holds this to the Python. */
export function budgetIn(model, productId, config) {
  const p = productId ? productById(model, productId) : null;
  if (!p) return [];
  return (model.budget || [])
    .filter((b) => b.chip === p.chip && matches(config, b.when))
    .map((b) => ({ id: b.id, why: b.why, lock: b.lock }));
}

/* Everything that stops a build of this choice: the rules it breaks, the
 * network driver the generator refuses for the product's chip, then the PIO and
 * pin budget. The page's warnings and its build button read this one list, so a
 * refusal cannot be shown and built. tools/gen_features.py blockers( ) is the
 * same list, in the same order. */
export function blockersOf(model, productId, config) {
  return [...ruleViolations(model, config), ...refusalsIn(model, productId, config),
          ...budgetIn(model, productId, config)];
}

/* The switch has no reader in this product: flipped, the image came out
 * byte-identical to the shipped one. */
export function noEffect(model, productId, toggleId) {
  const f = flipOf(model, productId, toggleId);
  return Boolean(f && f.same);
}

/* The sum. `missing` names the changes with no measurement behind them: the
 * key of `set` ("net" for the network, whose flip is "net=<id>", and "ui" for
 * the look of the touch display, whose flip is "ui=<id>"): a key whose flip the
 * matrix does not have yet is named, and left out of the sum. */
export function estimate(model, productId, set) {
  const c = costsOf(model, productId);
  if (!c) return null;
  const est = { used: c.base.used, bin: c.base.bin, ram: c.base.ram, ramKnown: true, missing: [] };
  for (const [key, value] of Object.entries(set)) {
    const d = flipDelta(model, productId, c.flips[flipKey(key, value)]);
    if (!d) { est.missing.push(key); continue; }
    est.used += d.used;
    est.bin += d.bin;
    if (d.ram === null) est.ramKnown = false; else est.ram += d.ram;
  }
  return est;
}

/* How far the sum misses real builds. `checks` in the cost file are
 * combinations compiled for real (tools/measure_savings.py); this runs the
 * page's own sum over each and keeps the largest miss, so the claim the page
 * makes about its accuracy is measured, and it moves when the firmware does. */
export function accuracy(model) {
  const checks = (model.costs && model.costs.checks) || [];
  let used = 0, bin = 0, n = 0;
  for (const c of checks) {
    if (!("used" in c) || !productById(model, c.base)) continue;
    const est = estimate(model, c.base, c.set);
    if (!est || est.missing.length) continue;
    used = Math.max(used, Math.abs(c.used - est.used));
    bin = Math.max(bin, Math.abs(c.bin - est.bin));
    n += 1;
  }
  return { n, used, bin };
}

/* Closer than this to a ceiling, the estimate cannot promise a fit: the
 * largest miss measured, rounded up to a whole KB, and never under one step
 * of the .bin. */
export function nearMargin(model) {
  const a = accuracy(model);
  return Math.max(BIN_STEP, Math.ceil(Math.max(a.used, a.bin) / 1024) * 1024);
}

/* "ok", "near" or "over", against one ceiling. */
export function fitOf(value, ceiling, margin) {
  if (value > ceiling) return "over";
  if (ceiling - value < margin) return "near";
  return "ok";
}

/* Whether an estimate links: the linker holds the image's extent, the .bin, to
 * the product's FLASH region, not `used`. An estimate between the two, under
 * the region by `used` and over it by the .bin, does not link. */
export function slotFit(model, productId, est) {
  return fitOf(est.bin, ceilingsOf(model, productId).flash_region, nearMargin(model));
}

/* The verdict in one word, the result's selo: what stops the build comes first
 * (blockersOf: a broken rule, a network driver the chip refuses, the PIO and
 * pin budget), then the fill of the flash slot. A blocked build that would fit
 * the slot is still blocked; null when no estimate judges the slot. */
export function verdictOf(model, productId, set) {
  if (blockersOf(model, productId, effectiveConfig(model, productId, set)).length) return "blocked";
  const est = estimate(model, productId, set);
  return est ? slotFit(model, productId, est) : null;
}

/* The profile tools/build_custom.py accepts, in its canonical spelling: the
 * same choice always gives the same bytes, so the same file name. `net` and
 * `ui` are keys of `set` like the switches, and sort among them. */
export function profileOf(productId, set) {
  const sorted = {};
  for (const k of Object.keys(set).sort()) sorted[k] = set[k];
  return { v: 1, base: productId, set: sorted };
}

export function profileJson(productId, set) {
  const p = profileOf(productId, set);
  // Python's json.dumps(sort_keys=True, separators=(",", ":")): keys sorted at
  // every level, no spaces. "base" < "set" < "v", so the order is written out.
  return `{"base":${JSON.stringify(p.base)},"set":${JSON.stringify(p.set)},"v":1}`;
}

/* ---- the link: #b=<product>&net=<driver>&ui=<look>&on=<ids>&off=<ids> -- */

const ID = /^[a-z0-9_]{1,40}$/;
const MAX_HASH = 1024;

export function formatHash(productId, set) {
  const switches = Object.keys(set).filter((k) => k !== "net" && k !== "ui");
  const on = switches.filter((k) => set[k]).sort();
  const off = switches.filter((k) => !set[k]).sort();
  let h = `b=${productId}`;
  if (set.net) h += `&net=${set.net}`;
  if (set.ui) h += `&ui=${set.ui}`;
  if (on.length) h += `&on=${on.join(",")}`;
  if (off.length) h += `&off=${off.join(",")}`;
  return `#${h}`;
}

/* Strict: a link names a product, at most one network driver, at most one look
 * of the touch display and switches this model knows, or it is refused with the
 * first thing that is wrong. The page shows the refusal as text (never as HTML)
 * and opens the default product. */
/* Whether a hash is a place on the page rather than a build: a bare id (no
 * "=") of an element that exists. The bar's "#resultado" and the skip link's
 * "#main" are such places, and the hashchange that following them fires read
 * as a hash with no product: the page went back to the default product with
 * nothing changed (a choice on the Pico 2 W came back as SIMUT, Chrome,
 * 2026-10-12). `exists` is the page's own lookup, so this stays testable. */
export function isAnchorHash(hash, exists) {
  const id = String(hash || "").replace(/^#/, "");
  return id !== "" && !id.includes("=") && Boolean(exists(id));
}

export function parseHash(model, hash) {
  const text = String(hash || "").replace(/^#/, "");
  if (!text) return { productId: null, set: {} };
  if (text.length > MAX_HASH) return { error: "long" };
  let params;
  try { params = new URLSearchParams(text); } catch (e) { return { error: "syntax" }; }
  const b = params.get("b");
  if (b === null) return { productId: null, set: {} };
  if (!ID.test(b) || !productById(model, b)) return { error: "product", value: b };
  const known = new Set(model.toggles.map((t) => t.id));
  const set = {};
  // The network: a driver id of this model, once. Not a switch, so on=net is refused below.
  if (params.has("net")) {
    if (params.getAll("net").length > 1) return { error: "twice", value: "net" };
    const n = params.get("net");
    if (!ID.test(n) || !model.net.options.some((o) => o.id === n)) return { error: "net", value: n };
    set.net = n;
  }
  // The look of the touch display: a [ui.*] id of this model, once, refused the way a network driver is.
  // Not a switch either, so on=ui is refused below.
  if (params.has("ui")) {
    if (params.getAll("ui").length > 1) return { error: "twice", value: "ui" };
    const u = params.get("ui");
    if (!ID.test(u) || !model.ui.options.some((o) => o.id === u)) return { error: "ui", value: u };
    set.ui = u;
  }
  for (const [key, value] of [["on", true], ["off", false]]) {
    const list = params.get(key);
    if (list === null || list === "") continue;
    for (const id of list.split(",")) {
      if (!ID.test(id) || !known.has(id)) return { error: "toggle", value: id };
      if (Object.prototype.hasOwnProperty.call(set, id)) return { error: "twice", value: id };
      set[id] = value;
    }
  }
  return { productId: b, set: normalizeSet(model, b, set) };
}

/* A request id the workflow accepts (^[a-z0-9-]{8,40}$) and nobody guesses. */
export function requestId(random) {
  const alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
  const bytes = random(10);
  let s = "";
  for (const b of bytes) s += alphabet[b % alphabet.length];
  return `cfg-${s}`;
}
