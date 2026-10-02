/* app.js — the SIMUT build configurator (docs/configurador/).
 *
 * The page draws what model.json says: the products, the switches grouped as a
 * tree, the rules (each measured by compiling the combination it forbids) and
 * the per-product cost of every switch. Nothing about the firmware is typed in
 * this file; tools/features.toml is the one source, and logic.js does the sums.
 *
 * The page is not the authority on what builds. It locks what a rule forbids so
 * nobody asks for it, and tools/build_custom.py applies the same rules again
 * before compiling (tools/test_configurator_rules.py keeps the two in step).
 *
 * Two safety lines hold throughout. Text from the model, the link and the
 * GitHub API is only ever set as textContent, never parsed as HTML. The GitHub
 * token stays in this browser and is sent to api.github.com only; the page's
 * Content-Security-Policy allows no other destination. */

import { ruleViolations, hazardHits } from "./rules.js";
import * as L from "./logic.js";

const REPO = "angeloINTJ/simut";
const WORKFLOW = "build-custom.yml";
const API = "https://api.github.com";
const STORAGE_KEY = "simut:configurador:token";   // where the token is kept, not a token
const LANG_KEY = "simut-lang";          // the landing page's key: one choice for the site
const THEME_KEY = "angulo:tema";        // every Ângulo page's key
const TOKEN_RE = /^(github_pat_[A-Za-z0-9_]{20,250}|ghp_[A-Za-z0-9]{36})$/;
const POLL_MS = 10000;

const STR = {
  en: {
    "skip": "Skip to content", "nav.home": "Home", "nav.manual": "Manual",
    "title": "Build configurator",
    "lede": "Pick a product, switch features on or off, and see whether the image still fits before you compile it.",
    "loading": "Loading the feature model…",
    "ft.colophon": "SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria. MIT licensed.",
    "ft.source": "Feature model", "ft.design": "How it works", "ft.edit": "Edit this page",
    "theme.dark": "Use dark theme", "theme.light": "Use light theme",
    "sec.product": "Product", "sec.features": "Features", "sec.result": "Result",
    "bench.show": "Show bench images",
    "product.size": "{n} B of flash",
    "product.nosize": "Not measured: bench image",
    "changed": "Changed",
    "locked": "Locked. {why}",
    "noeffect": "Changes nothing in this product: built both ways, the image came out byte for byte the same.",
    "price.more": "With it on, this product's image is {n} B larger (measured).",
    "price.less": "With it on, this product's image is {n} B smaller (measured).",
    "price.over": "With it on, this product no longer fits the flash slot (measured).",
    "result.title": "This build", "selo.estimate": "Estimate", "selo.none": "No estimate",
    "fit.ok": "Fits", "fit.near": "Tight", "fit.over": "Does not fit", "fixa.link": "See the result",
    "result.stock": "{p}, as shipped",
    "result.changes1": "{p}, 1 change", "result.changesN": "{p}, {n} changes",
    "meter.flash": "Flash slot", "meter.ota": ".bin for OTA",
    "meter.of": "{a} of {b} B", "meter.left": "{n} B to spare", "meter.over": "{n} B over",
    "ram": "RAM in use: {n} B", "ram.unknown": "RAM: no estimate, because an image that does not fit has no RAM figure.",
    "note": "The sum of the differences measured one switch at a time on {date}; the build measures the exact figure.",
    "note.acc": "Against {n} real builds of combined changes, the sum missed by up to {used} B of flash and {bin} B of .bin, so within {margin} B of a ceiling the page says Tight.",
    "warn.over": "Does not fit the flash slot. Switch something off.",
    "warn.near": "Close to the limit, closer than the estimate has been seen to miss. The build gives the exact figure.",
    "warn.otaOver": "The .bin is over the OTA ceiling: install the .uf2 over USB.",
    "warn.rule": "Not allowed. {why}",
    "warn.hazard": "Risk. {why}",
    "warn.missing": "No measurement for {list}, so the estimate leaves it out.",
    "warn.bench": "No estimate: the cost matrix measures the published products only. The build gives the figure.",
    "warn.stock": "No changes. This product's published image is in the latest release; compiling here builds today's main, which can be ahead of it.",
    "warn.stockLink": "Open the latest release",
    "action.build": "Compile this build", "action.building": "Compiling…",
    "action.copy": "Copy link", "action.copied": "Link copied",
    "action.copyFailed": "The browser did not allow copying. The link is the address in the address bar.",
    "action.request": "Request this build",
    "local.title": "Compile on your machine",
    "local.help": "From a clone of the repository, with PlatformIO installed:",
    "local.gh": "Or start the workflow with the GitHub CLI:",
    "token.title": "GitHub access",
    "token.help": "Compiling runs a workflow in the repository, and only accounts with write access can start one. Paste a fine-grained token limited to this repository, with Actions: read and write and a short expiry. It stays in this browser and goes to api.github.com only.",
    "token.label": "Token",
    "token.remember": "Remember in this browser",
    "token.save": "Save token",
    "token.create": "Create a fine-grained token",
    "token.saved": "Token saved, ending in {last}.",
    "token.forget": "Forget the token",
    "token.bad": "This is not a GitHub token. A fine-grained one starts with github_pat_.",
    "token.noaccess": "No write access? Request this build: it opens an issue with this configuration.",
    "run.for": "Build requested: {what}.",
    "run.dispatching": "Sending to GitHub…",
    "run.finding": "Waiting for the run to appear…",
    "run.queued": "In the queue at GitHub.",
    "run.approval": "Compiled. Waiting for the maintainer to approve the signature: a device installs only signed images over the air.",
    "run.progress": "Compiling: {m} min so far.",
    "run.ok": "Compiled: .bin {bin} B, RAM {ram} B.",
    "run.okOta": "It fits the OTA ceiling: install the .bin through the web interface, or the .uf2 over USB.",
    "run.okUsb": "The .bin is over the OTA ceiling: install the .uf2 over USB.",
    "run.download": "Download .uf2 and .bin",
    "run.page": "Open the run",
    "run.failed": "The build failed at step “{step}”.",
    "run.refused": "The workflow refused the profile: open the run to see the rule it named.",
    "run.cancelled": "The run was cancelled.",
    "run.lost": "The run did not appear within a minute. Look for it in the repository's Actions tab.",
    "err.401": "GitHub refused the token (401): it expired or was revoked. Forget it and paste a new one.",
    "err.403": "The token cannot start workflows here (403). It needs Actions: read and write on this repository.",
    "err.404": "GitHub did not find the workflow (404): the token has no access to this repository.",
    "err.422": "GitHub refused the request (422): {m}",
    "err.other": "GitHub answered {status}: {m}",
    "err.net": "Could not reach GitHub: {m}",
    "err.model": "Could not load the feature model ({m}). Reload the page.",
    "err.hash": "The link asked for something this model does not know (“{v}”), so it was ignored.",
    "err.hashLong": "The link is too long to be one of this page's, so it was ignored.",
  },
  pt: {
    "skip": "Pular para o conteúdo", "nav.home": "Início", "nav.manual": "Manual",
    "title": "Configurador de build",
    "lede": "Escolha um produto, ligue e desligue recursos, e veja se a imagem ainda cabe antes de compilar.",
    "loading": "Carregando o modelo de recursos…",
    "ft.colophon": "SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria. Licença MIT.",
    "ft.source": "Modelo de recursos", "ft.design": "Como funciona", "ft.edit": "Editar esta página",
    "theme.dark": "Usar tema escuro", "theme.light": "Usar tema claro",
    "sec.product": "Produto", "sec.features": "Recursos", "sec.result": "Resultado",
    "bench.show": "Mostrar imagens de bancada",
    "product.size": "{n} B de flash",
    "product.nosize": "Sem medida: imagem de bancada",
    "changed": "Mudou",
    "locked": "Bloqueada. {why}",
    "noeffect": "Não muda nada neste produto: compilada dos dois jeitos, a imagem saiu idêntica, byte a byte.",
    "price.more": "Ligada, a imagem deste produto fica {n} B maior (medido).",
    "price.less": "Ligada, a imagem deste produto fica {n} B menor (medido).",
    "price.over": "Ligada, este produto deixa de caber no slot de flash (medido).",
    "result.title": "Esta build", "selo.estimate": "Estimativa", "selo.none": "Sem estimativa",
    "fit.ok": "Cabe", "fit.near": "No limite", "fit.over": "Não cabe", "fixa.link": "Ver o resultado",
    "result.stock": "{p}, como publicado",
    "result.changes1": "{p}, 1 mudança", "result.changesN": "{p}, {n} mudanças",
    "meter.flash": "Slot de flash", "meter.ota": ".bin para OTA",
    "meter.of": "{a} de {b} B", "meter.left": "sobram {n} B", "meter.over": "passa em {n} B",
    "ram": "RAM em uso: {n} B", "ram.unknown": "RAM: sem estimativa, porque uma imagem que não cabe não tem número de RAM.",
    "note": "Soma das diferenças medidas uma chave por vez em {date}; a build mede o número exato.",
    "note.acc": "Contra {n} builds reais de mudanças combinadas, a soma errou por até {used} B de flash e {bin} B de .bin; por isso, a menos de {margin} B de um teto, a página diz No limite.",
    "warn.over": "Não cabe no slot de flash. Desligue algum recurso.",
    "warn.near": "Perto do limite, mais perto do que a estimativa já errou. A build dá o número exato.",
    "warn.otaOver": "O .bin passa do teto de OTA: grave o .uf2 pelo USB.",
    "warn.rule": "Proibido. {why}",
    "warn.hazard": "Risco. {why}",
    "warn.missing": "Sem medida para {list}, então a estimativa deixa de fora.",
    "warn.bench": "Sem estimativa: a matriz de custo mede só os produtos publicados. A build dá o número.",
    "warn.stock": "Sem mudanças. A imagem publicada deste produto está na última release; compilar aqui gera o main de hoje, que pode estar à frente dela.",
    "warn.stockLink": "Abrir a última release",
    "action.build": "Compilar esta build", "action.building": "Compilando…",
    "action.copy": "Copiar link", "action.copied": "Link copiado",
    "action.copyFailed": "O navegador não deixou copiar. O link é o endereço na barra do navegador.",
    "action.request": "Pedir esta build",
    "local.title": "Compilar na sua máquina",
    "local.help": "De um clone do repositório, com o PlatformIO instalado:",
    "local.gh": "Ou dispare o workflow pela CLI do GitHub:",
    "token.title": "Acesso ao GitHub",
    "token.help": "Compilar roda um workflow no repositório, e só contas com acesso de escrita podem disparar um. Cole um token fine-grained limitado a este repositório, com Actions: leitura e escrita e validade curta. Ele fica neste navegador e só vai para api.github.com.",
    "token.label": "Token",
    "token.remember": "Lembrar neste navegador",
    "token.save": "Salvar token",
    "token.create": "Criar um token fine-grained",
    "token.saved": "Token salvo, terminado em {last}.",
    "token.forget": "Esquecer o token",
    "token.bad": "Isto não é um token do GitHub. Um fine-grained começa com github_pat_.",
    "token.noaccess": "Sem acesso de escrita? Peça esta build: abre um pedido no GitHub com esta configuração.",
    "run.for": "Build pedida: {what}.",
    "run.dispatching": "Enviando ao GitHub…",
    "run.finding": "Esperando a execução aparecer…",
    "run.queued": "Na fila do GitHub.",
    "run.approval": "Compilada. Esperando o mantenedor aprovar a assinatura: o aparelho só instala imagem assinada pelo ar.",
    "run.progress": "Compilando: {m} min até agora.",
    "run.ok": "Compilada: .bin {bin} B, RAM {ram} B.",
    "run.okOta": "Cabe no teto de OTA: instale o .bin pela interface web, ou o .uf2 pelo USB.",
    "run.okUsb": "O .bin passa do teto de OTA: grave o .uf2 pelo USB.",
    "run.download": "Baixar .uf2 e .bin",
    "run.page": "Abrir a execução",
    "run.failed": "A build falhou no passo “{step}”.",
    "run.refused": "O workflow recusou o perfil: abra a execução para ver a regra que ele citou.",
    "run.cancelled": "A execução foi cancelada.",
    "run.lost": "A execução não apareceu em um minuto. Procure-a na aba Actions do repositório.",
    "err.401": "O GitHub recusou o token (401): ele expirou ou foi revogado. Esqueça-o e cole um novo.",
    "err.403": "O token não pode disparar workflows aqui (403). Ele precisa de Actions: leitura e escrita neste repositório.",
    "err.404": "O GitHub não achou o workflow (404): o token não tem acesso a este repositório.",
    "err.422": "O GitHub recusou o pedido (422): {m}",
    "err.other": "O GitHub respondeu {status}: {m}",
    "err.net": "Não deu para falar com o GitHub: {m}",
    "err.model": "Não deu para carregar o modelo de recursos ({m}). Recarregue a página.",
    "err.hash": "O link pedia algo que este modelo não conhece (“{v}”), e foi ignorado.",
    "err.hashLong": "O link é longo demais para ser desta página, e foi ignorado.",
  },
};

const state = {
  model: null,
  lang: "en",
  productId: null,
  set: {},
  showBench: false,
  notice: null,
  token: null,
  tokenOpen: false,
  tokenError: null,
  run: null,
  copied: false,
  copyFailed: false,
  open: {},        // the collapsible groups the reader opened or closed
  localId: null,   // the request id shown in the gh command, stable per visit
};

/* ---- small tools ---------------------------------------------------------- */

function t(key, vars) {
  let s = (STR[state.lang] && STR[state.lang][key]) || STR.en[key] || key;
  if (vars) for (const [k, v] of Object.entries(vars)) s = s.split(`{${k}}`).join(String(v));
  return s;
}

function num(n) {
  return new Intl.NumberFormat(state.lang === "pt" ? "pt-BR" : "en").format(n);
}

function kb(n) {
  const f = new Intl.NumberFormat(state.lang === "pt" ? "pt-BR" : "en",
                                  { minimumFractionDigits: 1, maximumFractionDigits: 1 });
  return `${n < 0 ? "−" : ""}${f.format(Math.abs(n) / 1024)} KB`;
}

/* Elements are built here and nowhere else. A string child becomes a text
 * node; there is no path from data to markup. */
function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k === "class") el.className = v;
    else if (k === "text") el.textContent = v;
    else if (k.startsWith("on") && typeof v === "function") el.addEventListener(k.slice(2), v);
    else if (v === true) el.setAttribute(k, "");
    else el.setAttribute(k, String(v));
  }
  for (const kid of kids.flat(Infinity)) {
    if (kid === null || kid === undefined || kid === false) continue;
    el.append(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  }
  return el;
}

function label(entry) {
  return entry[state.lang] || entry.en;
}

function storageGet(store, key) {
  try { return window[store].getItem(key); } catch (e) { return null; }
}

function storageSet(store, key, value) {
  try {
    if (value === null) window[store].removeItem(key); else window[store].setItem(key, value);
  } catch (e) { /* private mode or blocked storage: the page works without it */ }
}

/* ---- language and theme (the shell outside #app) -------------------------- */

function detectLang() {
  const saved = storageGet("localStorage", LANG_KEY);
  if (saved === "en" || saved === "pt") return saved;
  for (const l of navigator.languages || [navigator.language || "en"]) {
    const s = String(l).toLowerCase();
    if (s.startsWith("pt")) return "pt";
    if (s.startsWith("en")) return "en";
  }
  return "en";
}

function applyShell() {
  document.documentElement.lang = state.lang === "pt" ? "pt-BR" : "en";
  document.title = `SIMUT — ${t("title")}`;
  for (const el of document.querySelectorAll("[data-i18n]")) el.textContent = t(el.dataset.i18n);
  for (const b of document.querySelectorAll(".lang-btn")) {
    b.setAttribute("aria-pressed", String(b.dataset.lang === state.lang));
  }
  const sw = document.querySelector(".tema");
  if (sw) {
    const l = document.documentElement.dataset.theme === "escuro" ? t("theme.light") : t("theme.dark");
    sw.setAttribute("aria-label", l);
    sw.title = l;
  }
}

function wireShell() {
  for (const b of document.querySelectorAll(".lang-btn")) {
    b.addEventListener("click", () => {
      state.lang = b.dataset.lang === "pt" ? "pt" : "en";
      storageSet("localStorage", LANG_KEY, state.lang);
      applyShell();
      if (state.model) render();
    });
  }
  const sw = document.querySelector(".tema");
  if (sw) sw.addEventListener("click", () => {
    const next = document.documentElement.dataset.theme === "escuro" ? "claro" : "escuro";
    document.documentElement.dataset.theme = next;
    storageSet("localStorage", THEME_KEY, next);
    applyShell();
  });
}

/* ---- the choice, and the link that carries it ----------------------------- */

function defaultProduct() {
  const p = state.model.products.find((x) => x.publish) || state.model.products[0];
  return p.id;
}

function readHash() {
  const r = L.parseHash(state.model, location.hash);
  if (r.error) {
    state.notice = r.error === "long" ? t("err.hashLong")
      : t("err.hash", { v: String(r.value || "").slice(0, 40) });
    state.productId = defaultProduct();
    state.set = {};
    return;
  }
  state.productId = r.productId || defaultProduct();
  state.set = r.set;
  const p = L.productById(state.model, state.productId);
  if (p.bench) state.showBench = true;
}

function writeHash() {
  const h = L.formatHash(state.productId, state.set);
  if (location.hash !== h) history.replaceState(null, "", h);
}

/* A finished run belongs to the choice that started it and goes when the
 * choice changes; one still compiling stays on screen, labelled with what it
 * is compiling, until it ends. */
function dropFinishedRun() {
  if (state.run && state.run.done) state.run = null;
}

function choose(productId) {
  state.productId = productId;
  state.set = {};
  dropFinishedRun();
  writeHash();
  render();
}

function flip(toggleId, value) {
  const next = { ...state.set, [toggleId]: value };
  state.set = L.normalizeSet(state.model, state.productId, next);
  dropFinishedRun();
  writeHash();
  render();
}

/* ---- rendering ------------------------------------------------------------ */

function render() {
  const app = document.getElementById("app");
  const focused = document.activeElement && document.activeElement.id;
  app.replaceChildren(
    h("div", { class: "cfg-grade" },
      h("div", { class: "cfg-coluna" }, state.notice ? renderNotice() : null, renderProducts(), renderFeatures()),
      renderResult()),
    renderFixa());
  if (focused) {
    const el = document.getElementById(focused);
    if (el) el.focus({ preventScroll: true });
  }
  watchResult();
}

/* The phone bar repeats the result card, so it steps aside while the card
 * itself is on screen. */
let resultObserver = null;
function watchResult() {
  if (!("IntersectionObserver" in window)) return;
  if (resultObserver) resultObserver.disconnect();
  const card = document.getElementById("resultado");
  const bar = document.querySelector(".cfg-fixa");
  if (!card || !bar) return;
  resultObserver = new IntersectionObserver((entries) => {
    bar.classList.toggle("cfg-fixa-oculta", entries.some((e) => e.isIntersecting));
  });
  resultObserver.observe(card);
}

/* The verdict in one word, as a selo: the fill of the flash slot decides it. */
function fitSelo(est) {
  if (!est) return h("span", { class: "selo", text: t("selo.none") });
  const fit = L.fitOf(est.used, state.model.ceilings.flash_region, L.nearMargin(state.model));
  const tone = { ok: "selo-positivo", near: "selo-alerta", over: "selo-perigo" }[fit];
  return h("span", { class: `selo ${tone}`, text: t(`fit.${fit}`) });
}

/* On a phone the result card is below the tree; this bar keeps its figure in
 * sight while the reader flips switches further down. */
function renderFixa() {
  const m = state.model;
  const est = L.estimate(m, state.productId, state.set);
  return h("div", { class: "cfg-fixa" },
    fitSelo(est),
    est ? h("span", { class: "cfg-fixa-valor", text: t("meter.of", { a: num(est.used), b: num(m.ceilings.flash_region) }) }) : null,
    h("a", { href: "#resultado", text: t("fixa.link") }));
}

function renderNotice() {
  return h("p", { class: "cfg-aviso", role: "status" }, state.notice);
}

function renderProducts() {
  const m = state.model;
  const shown = m.products.filter((p) => p.publish || state.showBench || p.id === state.productId);
  return h("section", { class: "cfg-secao", "aria-labelledby": "h-produto" },
    h("h2", { class: "cfg-secao-titulo", id: "h-produto", text: t("sec.product") }),
    h("fieldset", { class: "cfg-produtos" },
      h("legend", { class: "sr-only", text: t("sec.product") }),
      shown.map((p) => {
        const costs = m.costs && m.costs.products && m.costs.products[p.id];
        const id = `p-${p.id}`;
        return h("label", { class: "cfg-produto", for: id },
          h("input", { type: "radio", name: "produto", id, value: p.id,
                       checked: p.id === state.productId, onchange: () => choose(p.id) }),
          h("span", { class: "cfg-produto-nome", text: label(p.label) }),
          h("span", { class: "cfg-produto-desc", text: label(p.desc) }),
          h("span", { class: "cfg-produto-num",
                      text: costs ? t("product.size", { n: num(costs.base.used) }) : t("product.nosize") }));
      })),
    h("label", { class: "cfg-bancada", for: "mostrar-bancada" },
      h("input", { type: "checkbox", id: "mostrar-bancada", checked: state.showBench,
                   onchange: (e) => { state.showBench = e.target.checked; render(); } }),
      t("bench.show")));
}

function renderFeatures() {
  const m = state.model;
  const config = L.effectiveConfig(m, state.productId, state.set);
  const groups = m.groups.map((g) => {
    const rows = m.toggles.filter((x) => x.group === g.id).map((x) => renderToggle(x, config));
    const title = label(g.label);
    if (g.advanced) {
      const changed = m.toggles.some((x) => x.group === g.id && x.id in state.set);
      const open = g.id in state.open ? state.open[g.id] : changed;
      return h("details", { class: "cfg-bloco cfg-detalhes", open,
                            ontoggle: (e) => { state.open[g.id] = e.target.open; } },
        h("summary", {}, h("h3", { class: "cfg-bloco-titulo", text: title })), rows);
    }
    return h("div", { class: "cfg-bloco" }, h("h3", { class: "cfg-bloco-titulo", text: title }), rows);
  });
  return h("section", { class: "cfg-secao", "aria-labelledby": "h-recursos" },
    h("h2", { class: "cfg-secao-titulo", id: "h-recursos", text: t("sec.features") }), groups);
}

function renderToggle(x, config) {
  const m = state.model;
  const id = `t-${x.id}`;
  const lock = L.lockOf(m, config, x.id);
  const same = L.noEffect(m, state.productId, x.id) && !(x.id in state.set);
  const price = L.priceOn(m, state.productId, x.id);
  const changed = x.id in state.set;
  let priceText = null, priceTitle = null;
  if (price && !lock) {
    priceText = kb(price.flash);
    priceTitle = price.overflow ? t("price.over")
      : t(price.flash >= 0 ? "price.more" : "price.less", { n: num(Math.abs(price.flash)) });
  }
  const helpId = `${id}-ajuda`;
  return h("div", { class: "cfg-chave" },
    h("div", { class: "cfg-chave-texto" },
      h("div", { class: "cfg-chave-nome" },
        h("label", { for: id, text: label(x.label) }),
        changed ? h("span", { class: "selo", text: t("changed") }) : null),
      h("p", { class: "cfg-apoio", id: helpId, text: label(x.help) }),
      lock ? h("p", { class: "cfg-chave-trava", text: t("locked", { why: label(lock.why) }) }) : null,
      same ? h("p", { class: "cfg-chave-trava", text: t("noeffect") }) : null),
    h("div", { class: "cfg-chave-lado" },
      priceText && !same ? h("span", { class: "cfg-chave-custo", title: priceTitle, text: priceText }) : null,
      h("input", { type: "checkbox", role: "switch", class: "cfg-interruptor", id,
                   "aria-describedby": helpId, checked: config[x.id] === true,
                   disabled: Boolean(lock) || same,
                   onchange: (e) => flip(x.id, e.target.checked) })));
}

function meter(labelText, value, ceiling) {
  const fit = L.fitOf(value, ceiling, L.nearMargin(state.model));
  const pct = Math.max(0, Math.min(100, (value / ceiling) * 100));
  const bar = h("span", {});
  bar.style.width = `${pct.toFixed(1)}%`;   // CSSOM, which the CSP allows; no style attribute
  const tone = fit === "over" ? "perigo" : fit === "near" ? "alerta" : "positivo";
  const rest = ceiling - value;
  return h("div", { class: "cfg-medidor" },
    h("div", { class: "cfg-medidor-linha" },
      h("span", { class: "cfg-medidor-rotulo", text: labelText }),
      h("span", { class: "cfg-medidor-valor", text: t("meter.of", { a: num(value), b: num(ceiling) }) })),
    h("div", { class: "cfg-barra", "data-estado": tone, role: "presentation" }, bar),
    h("span", { class: "cfg-apoio",
                text: rest >= 0 ? t("meter.left", { n: num(rest) }) : t("meter.over", { n: num(-rest) }) }));
}

function renderResult() {
  const m = state.model;
  const p = L.productById(m, state.productId);
  const config = L.effectiveConfig(m, state.productId, state.set);
  const violations = ruleViolations(m, config);
  const hazards = hazardHits(m, config);
  const est = L.estimate(m, state.productId, state.set);
  const nChanges = Object.keys(state.set).length;
  const summary = nChanges === 0 ? t("result.stock", { p: label(p.label) })
    : t(nChanges === 1 ? "result.changes1" : "result.changesN", { p: label(p.label), n: nChanges });

  const warnings = [];
  for (const r of violations) warnings.push(["perigo", t("warn.rule", { why: label(r.why) })]);
  let slotFit = null, otaFit = null;
  const margin = L.nearMargin(m);
  if (est) {
    slotFit = L.fitOf(est.used, m.ceilings.flash_region, margin);
    otaFit = L.fitOf(est.bin, m.ceilings.ota_bin, margin);
    if (slotFit === "over") warnings.push(["perigo", t("warn.over")]);
    else if (otaFit === "over") warnings.push(["alerta", t("warn.otaOver")]);
    if (slotFit === "near" || (slotFit === "ok" && otaFit === "near")) warnings.push(["alerta", t("warn.near")]);
    if (est.missing.length) {
      const names = est.missing.map((id) => label(m.toggles.find((x) => x.id === id).label));
      warnings.push(["alerta", t("warn.missing", { list: names.join(", ") })]);
    }
  } else {
    warnings.push(["neutro", t("warn.bench")]);
  }
  for (const hz of hazards) warnings.push(["alerta", t("warn.hazard", { why: label(hz.why) })]);

  const blocked = violations.length > 0 || slotFit === "over";
  announce(est ? `${summary}. ${t("meter.of", { a: num(est.used), b: num(m.ceilings.flash_region) })}` : summary);

  return h("aside", { class: "cfg-secao cfg-resultado", id: "resultado", "aria-labelledby": "h-resultado" },
    h("h2", { class: "cfg-secao-titulo", id: "h-resultado", text: t("sec.result") }),
    h("div", { class: "cfg-bloco" },
      h("div", { class: "cfg-resultado-cabeca" },
        h("h3", { class: "cfg-bloco-titulo", text: t("result.title") }),
        h("span", { class: "cfg-selos" }, fitSelo(est), est ? h("span", { class: "selo", text: t("selo.estimate") }) : null)),
      h("p", { class: "cfg-apoio cfg-resumo-texto", text: summary }),
      est ? [
        meter(t("meter.flash"), est.used, m.ceilings.flash_region),
        meter(t("meter.ota"), est.bin, m.ceilings.ota_bin),
        h("p", { class: "cfg-apoio", text: est.ramKnown ? t("ram", { n: num(est.ram) }) : t("ram.unknown") }),
      ] : null,
      warnings.length ? h("ul", { class: "cfg-avisos" },
        warnings.map(([tone, text]) => h("li", { class: "cfg-aviso", "data-tom": tone, text }))) : null,
      nChanges === 0 && p.publish ? h("p", { class: "cfg-apoio" }, t("warn.stock"), " ",
        h("a", { href: `https://github.com/${REPO}/releases/latest`, text: t("warn.stockLink") })) : null,
      est && m.costs.measured_at ? h("p", { class: "cfg-apoio cfg-nota", text: noteText() }) : null,
      h("div", { class: "cfg-acoes" },
        h("button", { type: "button", class: "botao botao-primario", id: "compilar",
                      disabled: blocked || isRunning(), onclick: onBuild },
          isRunning() ? t("action.building") : t("action.build")),
        h("button", { type: "button", class: "botao botao-secundario", id: "copiar", onclick: onCopy },
          state.copied ? t("action.copied") : t("action.copy")),
        h("a", { class: "botao botao-secundario", href: issueUrl(), target: "_blank", rel: "noopener",
                 text: t("action.request") })),
      state.copyFailed ? h("p", { class: "cfg-apoio", role: "status", text: t("action.copyFailed") }) : null,
      state.tokenOpen || state.token ? renderToken() : null,
      state.run ? renderRun() : null,
      renderLocal()));
}

let lastAnnounced = "";
function announce(text) {
  const el = document.getElementById("cfg-anuncio");
  if (el && text !== lastAnnounced) { el.textContent = text; lastAnnounced = text; }
}

/* What the estimate is, and how far it has been seen to miss. */
function noteText() {
  const m = state.model;
  const a = L.accuracy(m);
  const date = formatDate(m.costs.measured_at.date);
  if (!a.n) return t("note", { date });
  return `${t("note", { date })} ${t("note.acc", { n: a.n, used: num(a.used), bin: num(a.bin), margin: num(L.nearMargin(m)) })}`;
}

function formatDate(iso) {
  const d = new Date(`${iso}T12:00:00Z`);
  if (Number.isNaN(d.getTime())) return iso;
  return new Intl.DateTimeFormat(state.lang === "pt" ? "pt-BR" : "en", { dateStyle: "long", timeZone: "UTC" }).format(d);
}

function renderLocal() {
  const json = L.profileJson(state.productId, state.set);
  return h("details", { class: "cfg-detalhes cfg-local" },
    h("summary", { text: t("local.title") }),
    h("p", { class: "cfg-apoio", text: t("local.help") }),
    h("pre", { class: "cfg-comando" }, `python3 tools/build_custom.py --profile '${json}'`),
    h("p", { class: "cfg-apoio", text: t("local.gh") }),
    h("pre", { class: "cfg-comando" },
      `gh workflow run ${WORKFLOW} --repo ${REPO} \\\n  -f profile='${json}' \\\n  -f request_id=${state.localId}`));
}

/* ---- sharing and requesting ------------------------------------------------ */

async function onCopy() {
  writeHash();
  try {
    await navigator.clipboard.writeText(location.href);
    state.copied = true;
    render();
    setTimeout(() => { state.copied = false; render(); }, 2000);
  } catch (e) {
    // No clipboard permission: the link is already in the address bar, so say so.
    state.copyFailed = true;
    render();
  }
}

/* The issue is in English whatever the page's language: the repository's
 * working language, and the one its maintainer triages in. */
function issueUrl() {
  const m = state.model;
  const p = L.productById(m, state.productId);
  const lines = Object.entries(state.set).map(([id, v]) => {
    const x = m.toggles.find((y) => y.id === id);
    return `- ${x.label.en}: ${v ? "on" : "off"}`;
  });
  const est = L.estimate(m, state.productId, state.set);
  const page = `https://angelointj.github.io/simut/configurador/${L.formatHash(state.productId, state.set)}`;
  const body = [
    "Requested from the build configurator.",
    "",
    `Product: ${p.label.en} (\`${p.id}\`)`,
    lines.length ? "Changes:" : "Changes: none (the published image)",
    ...lines,
    "",
    est ? `Estimate: ${est.used} of ${m.ceilings.flash_region} B of flash; .bin ${est.bin} of ${m.ceilings.ota_bin} B for OTA.` : "Estimate: none (bench image).",
    "",
    "Profile for `tools/build_custom.py`:",
    "```json",
    L.profileJson(state.productId, state.set),
    "```",
    "",
    `Configurator: ${page}`,
  ].join("\n");
  const title = lines.length ? `Build request: ${p.label.en}, ${lines.length} change${lines.length > 1 ? "s" : ""}`
    : `Build request: ${p.label.en}`;
  return `https://github.com/${REPO}/issues/new?title=${encodeURIComponent(title)}&body=${encodeURIComponent(body)}`;
}

/* ---- the token --------------------------------------------------------------- */

function loadToken() {
  state.token = storageGet("sessionStorage", STORAGE_KEY) || storageGet("localStorage", STORAGE_KEY);
}

function renderToken() {
  if (state.token) {
    return h("div", { class: "cfg-campo" },
      h("p", { class: "cfg-apoio", text: t("token.saved", { last: state.token.slice(-4) }) }),
      h("div", { class: "cfg-acoes" },
        h("button", { type: "button", class: "botao botao-discreto", id: "esquecer-token", onclick: forgetToken },
          t("token.forget"))));
  }
  return h("form", { class: "cfg-token", onsubmit: saveToken },
    h("h4", { class: "cfg-token-titulo", text: t("token.title") }),
    h("p", { class: "cfg-apoio", text: t("token.help") }),
    h("div", { class: "cfg-campo", "data-erro": state.tokenError ? "" : null },
      h("label", { for: "token", text: t("token.label") }),
      h("input", { id: "token", name: "token", type: "password", autocomplete: "off", spellcheck: "false",
                   "aria-describedby": "token-nota", "aria-invalid": state.tokenError ? "true" : null }),
      h("p", { class: state.tokenError ? "cfg-campo-erro" : "cfg-apoio", id: "token-nota" },
        state.tokenError ? state.tokenError : h("a", {
          href: "https://github.com/settings/personal-access-tokens/new", target: "_blank", rel: "noopener",
          text: t("token.create") }))),
    h("label", { class: "cfg-bancada", for: "lembrar" },
      h("input", { type: "checkbox", id: "lembrar" }), t("token.remember")),
    h("div", { class: "cfg-acoes" },
      h("button", { type: "submit", class: "botao botao-secundario", id: "salvar-token" }, t("token.save"))),
    h("p", { class: "cfg-apoio", text: t("token.noaccess") }));
}

function saveToken(e) {
  e.preventDefault();
  const value = String(document.getElementById("token").value || "").trim();
  if (!TOKEN_RE.test(value)) {
    state.tokenError = t("token.bad");
    render();
    document.getElementById("token").focus();
    return;
  }
  const remember = document.getElementById("lembrar").checked;
  storageSet(remember ? "localStorage" : "sessionStorage", STORAGE_KEY, value);
  storageSet(remember ? "sessionStorage" : "localStorage", STORAGE_KEY, null);
  state.token = value;
  state.tokenError = null;
  render();
  document.getElementById("compilar").focus();
}

function forgetToken() {
  storageSet("localStorage", STORAGE_KEY, null);
  storageSet("sessionStorage", STORAGE_KEY, null);
  state.token = null;
  state.tokenOpen = true;
  render();
}

/* ---- compiling through GitHub Actions --------------------------------------- */

function randomBytes(n) {
  const a = new Uint8Array(n);
  crypto.getRandomValues(a);
  return a;
}

function isRunning() {
  return Boolean(state.run && !state.run.done);
}

async function gh(path, options = {}) {
  const res = await fetch(`${API}${path}`, {
    ...options,
    headers: {
      "Accept": "application/vnd.github+json",
      "X-GitHub-Api-Version": "2022-11-28",
      "Authorization": `Bearer ${state.token}`,
      ...(options.body ? { "Content-Type": "application/json" } : {}),
    },
    cache: "no-store",
    referrerPolicy: "no-referrer",
  });
  if (res.status === 204) return null;
  let data = null;
  try { data = await res.json(); } catch (e) { data = null; }
  if (!res.ok) {
    const msg = (data && data.message ? String(data.message) : res.statusText).slice(0, 200);
    const err = new Error(msg);
    err.status = res.status;
    throw err;
  }
  return data;
}

function explain(err) {
  if (!err.status) return t("err.net", { m: err.message });
  if (err.status === 401) return t("err.401");
  if (err.status === 403) return t("err.403");
  if (err.status === 404) return t("err.404");
  if (err.status === 422) return t("err.422", { m: err.message });
  return t("err.other", { status: err.status, m: err.message });
}

function setRun(patch) {
  state.run = { ...(state.run || {}), ...patch };
  render();
}

async function onBuild() {
  if (!state.token) {
    state.tokenOpen = true;
    render();
    const f = document.getElementById("token");
    if (f) f.focus();
    return;
  }
  const id = L.requestId(randomBytes);
  const profile = L.profileJson(state.productId, state.set);
  state.run = null;
  setRun({ id, phase: "dispatching", done: false, error: null, url: null, started: Date.now(),
           forProduct: state.productId, forSet: { ...state.set } });
  try {
    await gh(`/repos/${REPO}/actions/workflows/${WORKFLOW}/dispatches`, {
      method: "POST",
      body: JSON.stringify({ ref: "main", inputs: { profile, request_id: id } }),
    });
  } catch (err) {
    setRun({ phase: "error", done: true, error: explain(err) });
    return;
  }
  setRun({ phase: "finding" });
  const run = await findRun(id);
  if (!run) { setRun({ phase: "lost", done: true }); return; }
  setRun({ phase: run.status, runId: run.id, url: runUrl(run) });
  await follow(run.id);
}

async function findRun(id) {
  const title = `custom build ${id}`;
  for (let i = 0; i < 20; i++) {
    await sleep(3000);
    try {
      const data = await gh(`/repos/${REPO}/actions/workflows/${WORKFLOW}/runs?event=workflow_dispatch&per_page=20`);
      const run = (data.workflow_runs || []).find((r) => r.display_title === title);
      if (run) return run;
    } catch (err) {
      setRun({ phase: "error", done: true, error: explain(err) });
      return null;
    }
  }
  return null;
}

async function follow(runId) {
  for (;;) {
    let run;
    try {
      run = await gh(`/repos/${REPO}/actions/runs/${runId}`);
    } catch (err) {
      setRun({ phase: "error", done: true, error: explain(err) });
      return;
    }
    if (run.status !== "completed") {
      setRun({ phase: run.status });
      await sleep(POLL_MS);
      continue;
    }
    if (run.conclusion === "success") {
      try {
        const data = await gh(`/repos/${REPO}/actions/runs/${runId}/artifacts`);
        const art = (data.artifacts || []).find((a) => /^simut-/.test(a.name));
        const sizes = art && /-bin(\d+)-ram(\d+)$/.exec(art.name);
        setRun({ phase: "success", done: true,
                 bin: sizes ? Number(sizes[1]) : null, ram: sizes ? Number(sizes[2]) : null,
                 download: art ? `https://github.com/${REPO}/actions/runs/${runId}/artifacts/${art.id}` : null });
      } catch (err) {
        setRun({ phase: "success", done: true });
      }
      return;
    }
    if (run.conclusion === "cancelled") { setRun({ phase: "cancelled", done: true }); return; }
    let step = null;
    try {
      const jobs = await gh(`/repos/${REPO}/actions/runs/${runId}/jobs`);
      for (const j of jobs.jobs || []) {
        const s = (j.steps || []).find((x) => x.conclusion === "failure");
        if (s) { step = s.name; break; }
      }
    } catch (err) { /* the run page still says it */ }
    setRun({ phase: "failure", done: true, step });
    return;
  }
}

/* The run's page, as the API names it, but only if it is this repository's:
 * an href is never taken on trust. */
function runUrl(run) {
  const prefix = `https://github.com/${REPO}/actions/runs/`;
  return typeof run.html_url === "string" && run.html_url.startsWith(prefix) ? run.html_url : null;
}

function summaryOf(productId, set) {
  const p = L.productById(state.model, productId);
  const n = Object.keys(set).length;
  return n === 0 ? t("result.stock", { p: label(p.label) })
    : t(n === 1 ? "result.changes1" : "result.changesN", { p: label(p.label), n });
}

function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

function renderRun() {
  const r = state.run;
  const kids = [h("p", { class: "cfg-apoio", text: t("run.for", { what: summaryOf(r.forProduct, r.forSet) }) })];
  const say = (text, tone) => kids.push(h("p", { class: tone ? "cfg-aviso" : "cfg-apoio", "data-tom": tone || null, text }));
  if (r.phase === "dispatching") say(t("run.dispatching"));
  else if (r.phase === "finding") say(t("run.finding"));
  /* "waiting" is the run held at the `release` Environment for the maintainer's
     approval before the signing job (build-custom.yml): a person, not a queue. */
  else if (r.phase === "waiting") say(t("run.approval"));
  else if (r.phase === "queued" || r.phase === "requested" || r.phase === "pending") say(t("run.queued"));
  else if (r.phase === "in_progress") say(t("run.progress", { m: Math.max(1, Math.round((Date.now() - r.started) / 60000)) }));
  else if (r.phase === "success") {
    if (r.bin !== null && r.bin !== undefined) {
      say(t("run.ok", { bin: num(r.bin), ram: num(r.ram) }), "positivo");
      say(r.bin <= state.model.ceilings.ota_bin ? t("run.okOta") : t("run.okUsb"));
    }
    if (r.download) kids.push(h("div", { class: "cfg-acoes" },
      h("a", { class: "botao botao-secundario", href: r.download, text: t("run.download") })));
  } else if (r.phase === "failure") {
    const refused = r.step && /profile|request id/i.test(r.step);
    say(refused ? t("run.refused") : t("run.failed", { step: r.step || "?" }), "perigo");
  } else if (r.phase === "cancelled") say(t("run.cancelled"), "alerta");
  else if (r.phase === "lost") say(t("run.lost"), "alerta");
  else if (r.phase === "error") say(r.error, "perigo");
  if (r.url) kids.push(h("p", { class: "cfg-apoio" }, h("a", { href: r.url, target: "_blank", rel: "noopener", text: t("run.page") })));
  return h("div", { class: "cfg-estado", role: "status" }, kids);
}

/* ---- start -------------------------------------------------------------------- */

async function start() {
  state.lang = detectLang();
  wireShell();
  applyShell();
  loadToken();
  const app = document.getElementById("app");
  try {
    const res = await fetch("model.json", { cache: "no-cache" });
    if (!res.ok) throw new Error(String(res.status));
    state.model = await res.json();
  } catch (err) {
    app.replaceChildren(h("p", { class: "cfg-falha", text: t("err.model", { m: err.message }) }));
    return;
  }
  state.localId = L.requestId(randomBytes);
  readHash();
  writeHash();
  render();
  window.addEventListener("hashchange", () => { state.notice = null; readHash(); render(); });
  if ("serviceWorker" in navigator) {
    navigator.serviceWorker.register("sw.js").catch(() => { /* the page works without it */ });
  }
}

start();
