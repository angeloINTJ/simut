// SIMUT Theme Editor — frontend
const $ = (s) => document.querySelector(s);

// 24 fields da ThemePalette (mesma ordem de Themes.cpp)
const FIELDS = [
  { key: "bgMain",     label: "Fundo principal" },
  { key: "cardBg",     label: "Fundo de cards" },
  { key: "textMain",   label: "Texto principal" },
  { key: "textSub",    label: "Texto secundário" },
  { key: "textOff",    label: "Texto desabilitado" },
  { key: "accent",     label: "Destaque" },
  { key: "accentHigh", label: "Destaque alto" },
  { key: "barBg",      label: "Fundo de barras" },
  { key: "tempHot",    label: "Temp quente" },
  { key: "tempWarm",   label: "Temp morna" },
  { key: "tempOk",     label: "Temp OK" },
  { key: "tempCold",   label: "Temp fria" },
  { key: "humidity",   label: "Umidade" },
  { key: "btnText",       label: "Texto de botão" },
  { key: "btnTextActive", label: "Texto de botão SELECIONADO" },
  { key: "titleText",     label: "Texto de título / hora" },
  { key: "sensorName",    label: "Nome de sensor" },
  // Cores de estado (alarme/seleção/carimbo) — só aparecem no mapa de
  // regiões se a captura for feita com alarme/seleção/detalhe ativos;
  // os inputs e o export .thm funcionam sempre.
  { key: "alarmBg",      label: "Alarme: fundo" },
  { key: "alarmText",    label: "Alarme: texto" },
  { key: "alarmTextDim", label: "Alarme: texto secundário" },
  { key: "alarmBorder",  label: "Alarme: borda" },
  { key: "cautionBg",    label: "Botão silenciar (fundo)" },
  { key: "selBg",        label: "Seleção de slot (fundo)" },
  { key: "stampText",    label: "Data/hora no gráfico" },
];

// Cores RGB888 do simut_def (24 cores, espelho de Themes.cpp)
const BASE_THEMES = {
  simut_def: {
    bgMain:     [18, 18, 20],
    cardBg:     [35, 38, 45],
    textMain:   [245, 245, 245],
    textSub:    [180, 180, 185],
    textOff:    [90, 90, 100],
    accent:     [0, 150, 255],
    accentHigh: [50, 200, 255],
    barBg:      [50, 50, 60],
    tempHot:    [255, 60, 60],
    tempWarm:   [255, 170, 0],
    tempOk:     [0, 255, 2],
    tempCold:   [0, 200, 255],
    humidity:   [0, 121, 255],
    btnText:       [180, 180, 185],
    btnTextActive: [0, 0, 0],
    titleText:     [245, 245, 245],
    sensorName:    [245, 245, 245],
    alarmBg:      [180, 30, 30],
    alarmText:    [255, 255, 255],
    alarmTextDim: [220, 200, 200],
    alarmBorder:  [255, 60, 60],
    cautionBg:    [180, 90, 0],
    selBg:        [50, 50, 55],
    stampText:    [190, 170, 60],
  },
};

// state
const state = {
  loggedIn: false,
  origImageData: null,    // ImageData 320x240: screenshot diagnostic = "mapa de regiões"
  diagMode: false,        // true se origImageData veio de captura diagnostic (full fidelity)
  pixelToField: null,     // Uint8Array[w*h]: idx do field por pixel (0..16, 255=no match)
  baseColors565: null,
  newColors:    {},
  baseThemeKey: "simut_def",
};

// Paleta diagnostic FIXA (cores únicas RGB888 — devem bater com o tema "diagnostic"
// de presets.js/gen_presets.py, uploadado no SIMUT pra captura). Todas as 24
// permanecem únicas após o round-trip RGB565 (gen_presets.py confere).
// As 7 cores de estado só aparecem em capturas com alarme/seleção/detalhe.
const DIAG_RGB888 = {
  bgMain:        [0, 0, 0],
  cardBg:        [32, 32, 32],
  textMain:      [255, 255, 255],
  textSub:       [192, 192, 192],
  textOff:       [96, 96, 96],
  accent:        [0, 255, 255],
  accentHigh:    [255, 255, 0],
  barBg:         [48, 48, 48],
  tempHot:       [255, 0, 0],
  tempWarm:      [255, 136, 0],
  tempOk:        [0, 255, 0],
  tempCold:      [0, 0, 255],
  humidity:      [255, 0, 255],
  btnText:       [0, 255, 128],
  titleText:     [255, 128, 255],
  sensorName:    [128, 255, 0],
  btnTextActive: [80, 0, 0],
  alarmBg:       [128, 0, 64],
  alarmText:     [0, 128, 255],
  alarmTextDim:  [128, 128, 0],
  alarmBorder:   [255, 64, 0],
  cautionBg:     [0, 64, 128],
  selBg:         [64, 0, 128],
  stampText:     [192, 255, 192],
};
function genDiagThm() {
  let s = "@NAME _PreviewDiag\n@CODE _preview_diag\n@COLORS\n";
  for (const k of Object.keys(DIAG_RGB888)) {
    const [r, g, b] = DIAG_RGB888[k];
    s += `${k}=${rgbToHex(r, g, b)}\n`;
  }
  return s;
}

// ─── Utils RGB565 ────────────────────────────────────────────────────
function rgb888to565(r, g, b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
function rgb565to888(c) {
  const r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
  return [(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)];
}
function rgbToHex(r, g, b) {
  return "#" + [r, g, b].map(v => v.toString(16).padStart(2, "0")).join("").toUpperCase();
}
function hexToRgb(hex) {
  const m = hex.replace("#", "").match(/.{2}/g);
  return [parseInt(m[0], 16), parseInt(m[1], 16), parseInt(m[2], 16)];
}

// ─── Login ───────────────────────────────────────────────────────────
$("#login-form").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const user = ev.target.user.value.trim();
  const pass = ev.target.pass.value;
  if (!user || !pass) return;
  $("#login-msg").textContent = "...";
  try {
    const initRes = await fetch("/api/login_init", { credentials: "include" });
    const init = await initRes.json();
    if (init.locked) throw new Error(`Lockout ${init.lockSec}s`);
    const passLatin1 = window.utf8ToLatin1(pass);
    const passHash = window.sha256(passLatin1);
    const body = new URLSearchParams({ user, pass: passHash, nonce: init.nonce });
    const r = await fetch("/api/login", {
      method: "POST", credentials: "include",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: body.toString(),
    });
    const j = await r.json();
    if (j.ok) {
      state.loggedIn = true;
      $("#conn-status").textContent = `autenticado como ${user}`;
      $("#conn-status").className = "badge good";
      $("#login-msg").textContent = "✓";
      $("#cap-btn").disabled = false;
    } else {
      throw new Error(`err=${j.err}${j.lockSec ? ` lockout=${j.lockSec}s` : ""}`);
    }
  } catch (e) {
    $("#login-msg").textContent = "✗ " + e.message;
  }
});

// ─── Capture screenshot (flow diagnostic) ────────────────────────────
async function api(method, path, opts = {}) {
  const r = await fetch(path, { method, credentials: "include", ...opts });
  if (!r.ok) throw new Error(`${method} ${path} → HTTP ${r.status}`);
  const ct = r.headers.get("content-type") || "";
  return ct.includes("application/json") ? await r.json() : await r.arrayBuffer();
}
async function getCurrentTheme() {
  const st = await api("GET", "/api/status");
  return (st && st.sys && typeof st.sys.theme === "number") ? st.sys.theme : 0;
}
async function getThemeIdByCode(code) {
  const list = await api("GET", "/api/themes");
  const found = list.find(t => (t.name || "").toLowerCase().includes(code.toLowerCase())
                            || String(t.id) === String(code));
  return found ? found.id : null;
}
async function applyTheme(idx) {
  const r = await fetch(`/api/save_sys?theme=${idx}`, {
    method: "POST", credentials: "include"
  });
  if (!r.ok) throw new Error(`save_sys → HTTP ${r.status}`);
  await new Promise(res => setTimeout(res, 700));  // espera display redesenhar
}
async function uploadDiagThm() {
  const blob = new Blob([genDiagThm()], { type: "text/plain" });
  const fd = new FormData();
  fd.append("uploadDir", "/themes");
  fd.append("file", blob, "_preview_diag.thm");
  const r = await fetch("/api/upload", {
    method: "POST", credentials: "include", body: fd
  });
  if (!r.ok) throw new Error(`upload → HTTP ${r.status}`);
}
async function deleteDiagThm() {
  // Best-effort cleanup do tema temporário; falhas silenciosas (próximo
  // capture sobrescreve mesmo). Path codificado pra match upload acima.
  try {
    const fd = new URLSearchParams({ file: "/themes/_preview_diag.thm" });
    await fetch("/api/delete", {
      method: "POST", credentials: "include",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: fd.toString(),
    });
  } catch (_) {}
}

$("#cap-btn").addEventListener("click", async () => {
  let origTheme = null;
  try {
    $("#cap-btn").disabled = true;
    $("#cap-info").textContent = "1/4 lendo tema atual...";
    origTheme = await getCurrentTheme();

    $("#cap-info").textContent = "2/4 instalando tema diagnostic...";
    await uploadDiagThm();
    const diagId = await getThemeIdByCode("PreviewDiag");
    if (diagId === null) throw new Error("diagnostic não apareceu na lista");

    $("#cap-info").textContent = "3/4 aplicando + capturando mapa...";
    await applyTheme(diagId);
    const buf = new Uint8Array(await api("GET", "/api/screenshot"));
    const img = parseBmp(buf);

    $("#cap-info").textContent = `4/5 restaurando tema ${origTheme}...`;
    await applyTheme(origTheme);

    $("#cap-info").textContent = "5/5 limpando tema temporário...";
    await deleteDiagThm();

    state.origImageData = img;
    state.diagMode = true;
    buildPixelToFieldMap(img);
    drawImageData($("#canvas-orig"), img);
    rerender();
    $("#cap-info").textContent = `✓ mapa diagnostic: ${img.width}×${img.height}, ${state._diagPixelCount} pixels mapeados`;
  } catch (e) {
    $("#cap-info").textContent = "✗ " + e.message;
    if (origTheme !== null) {
      try { await applyTheme(origTheme); } catch(_) {}
    }
    try { await deleteDiagThm(); } catch(_) {}
  } finally {
    $("#cap-btn").disabled = false;
  }
});

// Decodifica BMP 24-bit (320x240)
function parseBmp(buf) {
  if (buf[0] !== 0x42 || buf[1] !== 0x4D) throw new Error("não é BMP");
  const w = buf[18] | (buf[19] << 8) | (buf[20] << 16) | (buf[21] << 24);
  const h = buf[22] | (buf[23] << 8) | (buf[24] << 16) | (buf[25] << 24);
  const pixOff = buf[10] | (buf[11] << 8);
  const rowSize = ((w * 3 + 3) >> 2) << 2;
  const imgData = new ImageData(w, h);
  // BMP linhas armazenadas bottom-up
  for (let y = 0; y < h; y++) {
    const srcRow = pixOff + (h - 1 - y) * rowSize;
    const dstRow = y * w * 4;
    for (let x = 0; x < w; x++) {
      const sp = srcRow + x * 3;
      const dp = dstRow + x * 4;
      // BMP é BGR
      imgData.data[dp]     = buf[sp + 2];
      imgData.data[dp + 1] = buf[sp + 1];
      imgData.data[dp + 2] = buf[sp];
      imgData.data[dp + 3] = 255;
    }
  }
  return imgData;
}

function drawImageData(canvas, img) {
  canvas.getContext("2d").putImageData(img, 0, 0);
}

// ─── Palette detection (info pro user) ───────────────────────────────
function detectPalette(img) {
  const counts = new Map();
  for (let i = 0; i < img.data.length; i += 4) {
    const c565 = rgb888to565(img.data[i], img.data[i + 1], img.data[i + 2]);
    counts.set(c565, (counts.get(c565) || 0) + 1);
  }
  const top = [...counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 8);
  $("#palette-info").innerHTML = "Cores dominantes da captura: " +
    top.map(([c, n]) => {
      const [r, g, b] = rgb565to888(c);
      return `<span class="swatch" style="background:rgb(${r},${g},${b})" title="${rgbToHex(r,g,b)} (${n}px)"></span>`;
    }).join("");
}

// ─── Color grid ──────────────────────────────────────────────────────
function buildColorGrid() {
  const grid = $("#color-grid");
  grid.innerHTML = "";
  // Detecta cores duplicadas no TEMA BASE — só relevante se NÃO está em diagMode
  // (no diagMode cada field tem cor única no screenshot, não há ambiguidade).
  const baseDupes = {};
  if (!state.diagMode) {
    const baseSeen = new Map();
    for (const f of FIELDS) {
      const [r, g, b] = BASE_THEMES[state.baseThemeKey][f.key];
      const c565 = rgb888to565(r, g, b);
      if (baseSeen.has(c565)) {
        (baseDupes[f.key] = baseDupes[f.key] || []).push(baseSeen.get(c565));
      } else {
        baseSeen.set(c565, f.key);
      }
    }
  }
  for (const f of FIELDS) {
    const row = document.createElement("div");
    row.className = "color-row";
    const [r, g, b] = state.newColors[f.key];
    const dupes = baseDupes[f.key];
    const warn = dupes
      ? `<span class="color-warn" title="Cor base duplicada de: ${dupes.join(", ")}. A preview não mostra esta cor separadamente — só após upload no SIMUT.">⚠</span>`
      : "";
    row.innerHTML = `
      <input type="color" data-key="${f.key}" value="${rgbToHex(r, g, b)}">
      <span class="color-label">${f.label} ${warn}</span>
      <code class="color-hex">${rgbToHex(r, g, b)}</code>
    `;
    grid.appendChild(row);
  }
  grid.querySelectorAll("input[type=color]").forEach(inp => {
    inp.addEventListener("input", (ev) => {
      const key = ev.target.dataset.key;
      state.newColors[key] = hexToRgb(ev.target.value);
      ev.target.parentElement.querySelector(".color-hex").textContent =
        ev.target.value.toUpperCase();
      rerender();
    });
  });
}

function loadBaseTheme(key) {
  state.baseThemeKey = key;
  const t = BASE_THEMES[key];
  state.baseColors565 = new Map();
  state.newColors = {};
  for (const f of FIELDS) {
    const [r, g, b] = t[f.key];
    state.baseColors565.set(rgb888to565(r, g, b), f.key);
    state.newColors[f.key] = [r, g, b];
  }
  buildColorGrid();
  rerender();
}

$("#base-theme").addEventListener("change", (ev) => loadBaseTheme(ev.target.value));
$("#reset-btn").addEventListener("click", () => loadBaseTheme(state.baseThemeKey));

// ─── Remap (tempo real) ──────────────────────────────────────────────
// Modo diagnostic: cada pixel do screenshot foi capturado com paleta de 24
// cores únicas (DIAG_RGB888). Mapeamos cada pixel pro field exato (Uint8Array,
// 0..23 = índice em FIELDS, 255 = sem match). Anti-alias usa closest-color
// pra preservar bordas suaves de fonte.
function buildPixelToFieldMap(img) {
  const w = img.width, h = img.height;
  const map = new Uint8Array(w * h);
  // Lista (rgb565 esperado, fieldIdx) — em ordem de FIELDS
  const targets = FIELDS.map((f, i) => {
    const [r, g, b] = DIAG_RGB888[f.key];
    return { fieldIdx: i, c565: rgb888to565(r, g, b), r, g, b };
  });
  // Map pra lookup rápido por cor exata
  const exact = new Map();
  for (const t of targets) if (!exact.has(t.c565)) exact.set(t.c565, t.fieldIdx);

  let mapped = 0;
  for (let i = 0; i < img.data.length; i += 4) {
    const r = img.data[i], g = img.data[i + 1], b = img.data[i + 2];
    const c565 = rgb888to565(r, g, b);
    const ex = exact.get(c565);
    if (ex !== undefined) {
      map[i / 4] = ex;
      mapped++;
    } else {
      // Closest-color (anti-alias de fonte)
      let bestD = 800, bestIdx = 255;
      for (const t of targets) {
        const dr = r - t.r, dg = g - t.g, db = b - t.b;
        const d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; bestIdx = t.fieldIdx; }
      }
      map[i / 4] = bestIdx;
      if (bestIdx !== 255) mapped++;
    }
  }
  state.pixelToField = map;
  state._diagPixelCount = mapped;
}

function rerender() {
  const canvas = $("#canvas-prev");
  if (!state.origImageData) {
    canvas.getContext("2d").clearRect(0, 0, 320, 240);
    return;
  }
  const src = state.origImageData;
  const dst = new ImageData(src.width, src.height);

  if (state.diagMode && state.pixelToField) {
    // Modo fidelidade total: lookup direto via mapa de fields
    const map = state.pixelToField;
    for (let i = 0, p = 0; i < src.data.length; i += 4, p++) {
      const fi = map[p];
      if (fi !== 255) {
        const [r, g, b] = state.newColors[FIELDS[fi].key];
        dst.data[i] = r; dst.data[i + 1] = g; dst.data[i + 2] = b;
      } else {
        dst.data[i] = src.data[i];
        dst.data[i + 1] = src.data[i + 1];
        dst.data[i + 2] = src.data[i + 2];
      }
      dst.data[i + 3] = 255;
    }
    drawImageData(canvas, dst);
    return;
  }

  // Fallback (sem captura ou capture antigo): remap por cor do tema base
  const remap = new Map();
  for (const f of FIELDS) {
    const [r, g, b] = BASE_THEMES[state.baseThemeKey][f.key];
    const c565 = rgb888to565(r, g, b);
    if (!remap.has(c565)) remap.set(c565, state.newColors[f.key]);
  }
  const baseList = [...remap.keys()].map(c => {
    const [r, g, b] = rgb565to888(c);
    return { c565: c, r, g, b, target: remap.get(c) };
  });
  for (let i = 0; i < src.data.length; i += 4) {
    const r = src.data[i], g = src.data[i + 1], b = src.data[i + 2];
    const c565 = rgb888to565(r, g, b);
    let target = remap.get(c565);
    if (!target) {
      let bestD = 1500;
      for (const e of baseList) {
        const dr = r - e.r, dg = g - e.g, db = b - e.b;
        const d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; target = e.target; }
      }
    }
    if (target) {
      dst.data[i] = target[0]; dst.data[i + 1] = target[1]; dst.data[i + 2] = target[2];
    } else {
      dst.data[i] = r; dst.data[i + 1] = g; dst.data[i + 2] = b;
    }
    dst.data[i + 3] = 255;
  }
  drawImageData(canvas, dst);
}

// ─── Save / load .thm ────────────────────────────────────────────────
$("#save-btn").addEventListener("click", () => {
  const name = $("#name").value.trim() || "Custom Theme";
  // 15 = THM_ID_MAX-1 do firmware (strncpy trunca em silêncio acima disso)
  const code = ($("#code").value.trim() || "custom").replace(/[^A-Za-z0-9_]/g, "_").slice(0, 15);
  let txt = `@NAME ${name}\n@CODE ${code}\n@COLORS\n`;
  for (const f of FIELDS) {
    txt += `${f.key}=${rgbToHex(...state.newColors[f.key])}\n`;
  }
  const blob = new Blob([txt], { type: "text/plain" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = `${code}.thm`;
  a.click();
  URL.revokeObjectURL(a.href);
});

$("#load-btn").addEventListener("click", () => $("#load-input").click());
$("#load-input").addEventListener("change", async (ev) => {
  const file = ev.target.files[0];
  if (!file) return;
  const txt = await file.text();
  const lines = txt.split(/\r?\n/);
  let inColors = false;
  for (const line of lines) {
    if (line.startsWith("@NAME ")) $("#name").value = line.slice(6).trim();
    else if (line.startsWith("@CODE ")) $("#code").value = line.slice(6).trim();
    else if (line.startsWith("@COLORS")) inColors = true;
    else if (inColors) {
      const m = line.match(/^([a-zA-Z]+)\s*=\s*(#[0-9a-fA-F]{6})/);
      if (m && state.newColors[m[1]] !== undefined) {
        state.newColors[m[1]] = hexToRgb(m[2]);
      }
    }
  }
  buildColorGrid();
  rerender();
});

// ─── Galeria de presets ──────────────────────────────────────────────
function buildGallery(filter = "") {
  const grid = $("#gallery-grid");
  grid.innerHTML = "";
  const f = filter.toLowerCase();
  for (const p of (window.PRESET_THEMES || [])) {
    if (f && !p.name.toLowerCase().includes(f) && !p.id.toLowerCase().includes(f)) continue;
    const card = document.createElement("div");
    card.className = "preset-card";
    // 5 cores principais como swatch strip
    const c = p.colors;
    const strip = ["bgMain", "cardBg", "accent", "tempHot", "tempOk"]
      .map(k => `<div style="background:rgb(${c[k][0]},${c[k][1]},${c[k][2]})"></div>`).join("");
    card.innerHTML = `
      <div class="preset-name">${p.name}</div>
      <div class="preset-id">${p.id}</div>
      <div class="preset-strip">${strip}</div>
    `;
    card.addEventListener("click", () => {
      applyPreset(p);
      $("#gallery-modal").classList.add("hidden");
    });
    grid.appendChild(card);
  }
}

function applyPreset(preset) {
  state.newColors = {};
  for (const f of FIELDS) {
    // Preset antigo (17 cores) cai no simut_def pros campos de estado
    const src = preset.colors[f.key] || BASE_THEMES.simut_def[f.key];
    state.newColors[f.key] = [...src];
  }
  $("#name").value = preset.name;
  $("#code").value = preset.id;
  buildColorGrid();
  rerender();
}

$("#gallery-btn").addEventListener("click", () => {
  buildGallery($("#gallery-search").value);
  $("#gallery-modal").classList.remove("hidden");
});
$("#gallery-close").addEventListener("click", () => $("#gallery-modal").classList.add("hidden"));
$("#gallery-search").addEventListener("input", (ev) => buildGallery(ev.target.value));
$("#gallery-modal").addEventListener("click", (ev) => {
  if (ev.target.id === "gallery-modal") $("#gallery-modal").classList.add("hidden");
});

// ─── Boot ────────────────────────────────────────────────────────────
loadBaseTheme("simut_def");
