/* logic.js — what the configurator works out, with no page around it.
 *
 * Everything here is a pure function of model.json (tools/gen_features.py
 * writes it from tools/features.toml) and of the choice on screen: which
 * product, and which switches differ from that product's defaults. No DOM, no
 * network, so tools/test_configurator_page.py runs it under node.
 *
 * The estimate is the measured cost matrix (tools/measure_savings.py --matrix):
 * each published product built once as shipped and once per switch flipped.
 * Two or more changes are SUMMED, which is an estimate and is labelled one;
 * the build measures the real image. */

import { ruleViolations } from "./rules.js";

/* With Bluetooth linked, the .bin grows in 4 KB steps (its flash bank is
 * page-aligned), so no margin is smaller than one step. */
const BIN_STEP = 4096;

export function productById(model, id) {
  return model.products.find((p) => p.id === id) || null;
}

/* The choice on screen: the product's configuration with `set` on top. */
export function effectiveConfig(model, productId, set) {
  const p = productById(model, productId);
  return { ...p.config, ...set };
}

/* `set` keeps only what differs from the product: a switch set back to its
 * default is not a change, and the link and the profile should not carry it. */
export function normalizeSet(model, productId, set) {
  const base = productById(model, productId).config;
  const out = {};
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

/* Bytes of flash the flip adds (negative: saves). An overflow is measured
 * too: the linker reports by how much the FLASH region was exceeded. */
function flipDelta(model, productId, f) {
  const base = costsOf(model, productId).base;
  if (!f) return null;
  if ("used" in f) {
    return { used: f.used - base.used, bin: f.bin - base.bin, ram: f.ram - base.ram };
  }
  if ("overflow" in f) {
    const used = model.ceilings.flash_region + f.overflow - base.used;
    return { used, bin: used, ram: null };
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

/* Why a switch cannot move: flipping it would break a rule that the current
 * choice does not already break. Returns the rule, or null. */
export function lockOf(model, config, toggleId) {
  const now = new Set(ruleViolations(model, config).map((r) => r.id));
  const flipped = { ...config, [toggleId]: !config[toggleId] };
  return ruleViolations(model, flipped).find((r) => !now.has(r.id)) || null;
}

/* The switch has no reader in this product: flipped, the image came out
 * byte-identical to the shipped one. */
export function noEffect(model, productId, toggleId) {
  const f = flipOf(model, productId, toggleId);
  return Boolean(f && f.same);
}

/* The sum. `missing` names the changes with no measurement behind them. */
export function estimate(model, productId, set) {
  const c = costsOf(model, productId);
  if (!c) return null;
  const est = { used: c.base.used, bin: c.base.bin, ram: c.base.ram, ramKnown: true, missing: [] };
  for (const [t] of Object.entries(set)) {
    const d = flipDelta(model, productId, c.flips[t]);
    if (!d) { est.missing.push(t); continue; }
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

/* The profile tools/build_custom.py accepts, in its canonical spelling: the
 * same choice always gives the same bytes, so the same file name. */
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

/* ---- the link: #b=<product>&on=<ids>&off=<ids> -------------------------- */

const ID = /^[a-z0-9_]{1,40}$/;
const MAX_HASH = 1024;

export function formatHash(productId, set) {
  const on = Object.keys(set).filter((k) => set[k]).sort();
  const off = Object.keys(set).filter((k) => !set[k]).sort();
  let h = `b=${productId}`;
  if (on.length) h += `&on=${on.join(",")}`;
  if (off.length) h += `&off=${off.join(",")}`;
  return `#${h}`;
}

/* Strict: a link names a product and switches this model knows, or it is
 * refused with the first thing that is wrong. The page shows the refusal as
 * text (never as HTML) and opens the default product. */
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
