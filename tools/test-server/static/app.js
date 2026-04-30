// SIMUT Test Server — frontend
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const state = { config: {}, listeners: {}, samples: [] };
let ws = null;
let wsReconnectTimer = null;

// ─── WebSocket ───────────────────────────────────────────────────────
function connectWs() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => {
    setConn(true);
    termPush("conexão WebSocket estabelecida", "info");
  };
  ws.onclose = () => {
    setConn(false);
    termPush("WebSocket desconectado, reconectando em 2s", "warn");
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = setTimeout(connectWs, 2000);
  };
  ws.onerror = () => setConn(false);
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    handleMessage(msg);
  };
}

function setConn(ok) {
  const el = $("#conn-status");
  el.textContent = ok ? "conectado" : "desconectado";
  el.className = "badge " + (ok ? "good" : "bad");
}

function handleMessage(msg) {
  switch (msg.type) {
    case "init":
      state.config = msg.config;
      state.listeners = msg.listeners;
      state.samples = msg.samples;
      renderConfig();
      renderListeners();
      renderAllSamples();
      termPush(`init: ${state.samples.length} samples carregados`, "info");
      break;
    case "config":
      state.config = msg.config;
      renderConfig();
      termPush("config atualizada pelo servidor", "cfg");
      break;
    case "listeners":
      state.listeners = msg.listeners;
      renderListeners();
      break;
    case "sample":
      state.samples.push(msg.sample);
      if (state.samples.length > 200) state.samples.shift();
      prependSample(msg.sample);
      const s = msg.sample;
      const where = s.path || s.mqtt_topic || "";
      const cls = (s.decoded.ok && s.auth_ok) ? "recv" : "recv-bad";
      const flag = (s.decoded.ok && s.auth_ok) ? "✓" : "✗";
      termPush(`${flag} ${s.transport} ${s.method || ""} ${where} (${s.body_size}B)`, cls);
      if (!s.decoded.ok) termPush(`  └─ decode error: ${s.decoded.error}`, "error");
      if (!s.auth_ok) termPush(`  └─ auth: ${s.auth_msg}`, "error");
      break;
    case "log":
      termPush(msg.line, msg.level === "info" ? "info" : msg.level);
      break;
    case "cleared":
      state.samples = [];
      renderAllSamples();
      termPush("samples limpos", "info");
      break;
  }
}

// ─── Config form ─────────────────────────────────────────────────────
function renderConfig() {
  for (const [key, val] of Object.entries(state.config)) {
    const el = document.querySelector(`[name="${key}"]`);
    if (!el) continue;
    if (el.type === "checkbox") el.checked = !!val;
    else el.value = val ?? "";
  }
}

$("#config-form").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const form = ev.target;
  const data = {};
  for (const el of form.elements) {
    if (!el.name) continue;
    if (el.type === "checkbox") data[el.name] = el.checked;
    else if (el.type === "number") data[el.name] = el.value === "" ? null : Number(el.value);
    else data[el.name] = el.value;
  }
  const res = await fetch("/api/config", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(data),
  });
  const json = await res.json();
  $("#config-status").textContent = json.ok ? "✓ aplicado" : "✗ erro";
  setTimeout(() => $("#config-status").textContent = "", 2500);
});

// ─── Listeners ───────────────────────────────────────────────────────
function renderListeners() {
  for (const [name, info] of Object.entries(state.listeners)) {
    const row = document.querySelector(`tr[data-name="${name}"]`);
    if (!row) continue;
    const stateEl = row.querySelector(".state");
    const detailEl = row.querySelector(".detail");
    const btn = row.querySelector(".toggle");
    const running = !!info.running;
    stateEl.textContent = running ? "UP" : "down";
    stateEl.className = "state " + (running ? "up" : "down");
    if (name === "http" || name === "https") {
      detailEl.textContent = running ? `:${info.port}` : `(porta ${state.config[name + "_port"]})`;
    } else {
      detailEl.textContent = running
        ? `${info.broker || ""} → ${state.config.mqtt_topic}`
        : `(${state.config.mqtt_broker_host}:${name === "mqtts" ? state.config.mqtt_broker_port_tls : state.config.mqtt_broker_port})`;
    }
    btn.textContent = running ? "Parar" : "Iniciar";
    btn.className = "toggle " + (running ? "danger" : "");
  }
}

document.addEventListener("click", async (ev) => {
  const btn = ev.target.closest(".toggle");
  if (!btn) return;
  const row = btn.closest("tr");
  const name = row.dataset.name;
  const running = state.listeners[name]?.running;
  const action = running ? "stop" : "start";
  btn.disabled = true;
  try {
    const res = await fetch(`/api/listeners/${name}/${action}`, {method: "POST"});
    const json = await res.json();
    if (!json.ok) termPush(`falha ao ${action} ${name}: ${json.error || "?"}`, "error");
  } finally {
    btn.disabled = false;
  }
});

// ─── Terminal ────────────────────────────────────────────────────────
const termEl = () => $("#terminal");
const MAX_TERM_LINES = 500;

function termPush(line, cls = "info") {
  const el = termEl();
  const div = document.createElement("span");
  div.className = "term-line";
  const t = new Date().toLocaleTimeString("pt-BR", {hour12: false}) +
            "." + String(Date.now() % 1000).padStart(3, "0");
  div.innerHTML = `<span class="term-time">${t}</span><span class="term-${cls}">${escapeHtml(line)}</span>`;
  el.appendChild(div);
  while (el.childNodes.length > MAX_TERM_LINES) el.removeChild(el.firstChild);
  if ($("#autoscroll").checked) el.scrollTop = el.scrollHeight;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
  })[c]);
}

$("#terminal-clear-btn").addEventListener("click", () => termEl().innerHTML = "");
$("#clear-btn").addEventListener("click", async () => {
  await fetch("/api/clear", {method: "POST"});
});

// ─── Samples ─────────────────────────────────────────────────────────
function renderAllSamples() {
  const cont = $("#samples");
  cont.innerHTML = "";
  const reversed = [...state.samples].reverse();
  reversed.forEach((s, i) => cont.appendChild(makeSampleEl(s, i === 0)));
}

function prependSample(s) {
  const cont = $("#samples");
  // Colapsa o que estava aberto antes (era o "mais recente" anterior)
  const prevOpen = cont.querySelector("details[open]");
  if (prevOpen && !$("#keep-open").checked) prevOpen.open = false;
  cont.insertBefore(makeSampleEl(s, true), cont.firstChild);
  while (cont.childNodes.length > 50) cont.removeChild(cont.lastChild);
}

function makeSampleEl(s, openByDefault = false) {
  const det = document.createElement("details");
  if (openByDefault) det.open = true;
  const sum = document.createElement("summary");
  const where = s.path || s.mqtt_topic || "";
  const okHtml = (s.decoded.ok && s.auth_ok)
    ? `<span class="ok">✓</span>`
    : `<span class="err">✗</span>`;
  // Preview do body (1 linha, max 80 chars)
  const preview = s.body.replace(/\s+/g, " ").trim().slice(0, 80);
  sum.innerHTML = `
    <span class="ts">${escapeHtml(s.ts.split(" ")[1] || s.ts)}</span>
    <span class="kind ${s.transport}">${s.transport}</span>
    <span class="where">${escapeHtml(s.method ? s.method + " " : "")}${escapeHtml(where)}</span>
    <span class="size">${s.body_size}B</span>
    ${okHtml}
    <span class="preview">${escapeHtml(preview)}${s.body.length > 80 ? "…" : ""}</span>
  `;
  det.appendChild(sum);
  const body = document.createElement("div");
  body.className = "sample-detail";
  let html = "";
  if (Object.keys(s.headers || {}).length) {
    html += `<h3>Headers</h3><pre>${escapeHtml(formatHeaders(s.headers))}</pre>`;
  }
  if (s.mqtt_topic) {
    html += `<h3>MQTT</h3><pre>topic: ${escapeHtml(s.mqtt_topic)}\nqos: ${s.mqtt_qos}</pre>`;
  }
  html += `<h3>Body raw (${s.body_size} bytes)</h3><pre>${escapeHtml(s.body)}</pre>`;
  html += `<h3>Decoded (modo: ${escapeHtml(state.config.mode)})</h3>`;
  if (s.decoded.ok) {
    html += `<pre>${escapeHtml(JSON.stringify(s.decoded.result, null, 2))}</pre>`;
  } else {
    html += `<pre style="color:var(--bad)">ERRO: ${escapeHtml(s.decoded.error)}</pre>`;
  }
  if (!s.auth_ok) {
    html += `<h3>Auth</h3><pre style="color:var(--bad)">${escapeHtml(s.auth_msg)}</pre>`;
  }
  body.innerHTML = html;
  det.appendChild(body);
  return det;
}

function formatHeaders(h) {
  return Object.entries(h).map(([k, v]) => `${k}: ${v}`).join("\n");
}

// ─── Boot ────────────────────────────────────────────────────────────
connectWs();
